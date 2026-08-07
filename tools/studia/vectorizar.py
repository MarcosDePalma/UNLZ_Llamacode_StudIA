#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Calcula los embeddings de los fragmentos del índice de StudIA y los guarda
dentro de la misma base.

    tools\\studia\\servidor_embeddings.bat gpu        (en otra consola)
    python vectorizar.py --db "D:\\FACULTAD\\PPS\\StudIA\\studia.db" \
                         --url http://127.0.0.1:8081

Por qué hace falta: la búsqueda por palabras (BM25) no entiende significado. Dos
formas de preguntar lo mismo ("¿qué pasa si…?" / "¿qué sucede si…?") se comportan
distinto sólo porque una palabra es más frecuente que la otra. Los embeddings
comparan por SENTIDO y resuelven esa clase de problema de raíz.

Necesita un servidor OpenAI-compatible que exponga /v1/embeddings: un segundo
`llama-server` con un modelo de embeddings chico, que levanta
`servidor_embeddings.bat`. Se usa bge-m3 (568M) y no el modelo de chat porque el
de chat es de otra clase y le lleva ~15 s por fragmento contra ~0,1 s: el corpus
entero pasa de unas tres semanas a unas cuatro horas.

bge-m3 ademas codifica igual la pregunta y el documento, y es multilingue, que
es lo que permite preguntar en castellano y encontrar el parrafo en un libro en
ingles -- dos tercios de la bibliografia de varias materias.

Es incremental y re-ejecutable: sólo procesa los fragmentos que todavía no
tienen vector, así que se puede cortar y retomar.
"""

import argparse
import json
import math
import os
import sqlite3
import struct
import sys
import time
import urllib.error
import urllib.request

# El servidor procesa cada request en una sola pasada, así que la suma de TOKENS
# del lote tiene que entrar en su --ubatch-size. Por eso los lotes se arman por
# presupuesto de tokens y no sólo por cantidad: cinco fragmentos cortos entran
# donde no entran dos largos.
#
# El presupuesto tiene que dar holgura para TOPE_FRAGMENTOS fragmentos
# (8 x ~342 = ~2700 tokens); si no, se vuelve el límite real y el tope calibrado
# no se usa nunca. Queda debajo del --ubatch-size del servidor en modo GPU
# (8192), que es el techo de verdad.
PRESUPUESTO_DEFAULT = 4096
CHARS_POR_TOKEN = 3.5          # estimación conservadora para castellano/inglés

# Tope de fragmentos por request. Medido con bge-m3 en GPU sobre fragmentos
# reales del índice, cada texto usado UNA sola vez: 4 da ~6,5 frag/s, 8 da ~11,
# y de 12 en adelante el servidor corta la conexión sin dejar nada en el log.
#
# La advertencia importa: medir con textos repetidos da números hasta cinco
# veces mayores, porque llama.cpp reutiliza el cálculo cuando el texto coincide.
# Cualquier medición futura de esto tiene que usar textos distintos.
#
# Mandar varios requests en paralelo NO acelera: medido alternando y repitiendo,
# 1 en paralelo da mediana 9,2 frag/s y 4 en paralelo 10,0, con más requests
# rechazados. La diferencia queda dentro del ruido, así que el script es
# deliberadamente secuencial.
#
# El valor no es sagrado: si el servidor rechaza un lote, el script baja el tope
# solo y sigue. Por eso alcanza con que sea razonable, no exacto.
TOPE_FRAGMENTOS = 8
TIMEOUT = 180                  # s por request
REINTENTOS = 1                 # sólo para cortes transitorios de red


ESQUEMA = """
CREATE TABLE IF NOT EXISTS vectores (
    frag_id INTEGER PRIMARY KEY REFERENCES fragmentos(id) ON DELETE CASCADE,
    dim     INTEGER NOT NULL,
    vec     BLOB    NOT NULL
);
-- Metadatos del vectorizado: sirve para avisar si el índice se generó con otro
-- modelo que el que se está usando para consultar (los vectores no serían
-- comparables entre sí).
CREATE TABLE IF NOT EXISTS vectores_info (
    clave TEXT PRIMARY KEY,
    valor TEXT
);
"""


def vec_a_blob(v):
    return struct.pack("<%df" % len(v), *v)


def embeber(url, textos):
    """Pide los embeddings de una lista de textos. Devuelve lista de listas."""
    cuerpo = json.dumps({"input": textos, "model": "studia-embed"}).encode("utf-8")
    req = urllib.request.Request(
        url.rstrip("/") + "/v1/embeddings", data=cuerpo,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        datos = json.loads(r.read().decode("utf-8"))
    filas = datos.get("data") or []
    if len(filas) != len(textos):
        raise RuntimeError("el servidor devolvió %d vectores para %d textos"
                           % (len(filas), len(textos)))
    # El campo 'index' dice a qué texto corresponde cada vector.
    salida = [None] * len(textos)
    for f in filas:
        salida[int(f.get("index", 0))] = f.get("embedding") or []
    if any(v is None or not v for v in salida):
        raise RuntimeError("respuesta con vectores vacíos")
    return salida


def normalizar(v):
    """Se guardan normalizados: así el coseno es un producto escalar."""
    n = math.sqrt(sum(x * x for x in v))
    return [x / n for x in v] if n > 0 else v


def tokens_estimados(texto):
    return max(1, int(len(texto) / CHARS_POR_TOKEN) + 8)


def siguiente_lote(pendientes, desde, presupuesto, tope):
    """Arma el lote que empieza en `pendientes[desde]`.

    Se agrupa por tokens y no sólo por cantidad porque el límite del servidor es
    de tokens: cinco fragmentos cortos entran en un request y dos largos no. Un
    fragmento que solo ya excede el presupuesto igual se manda solo — no hay
    forma de partirlo sin romper el fragmento.

    Los lotes se arman de a uno, sobre la marcha, en vez de precalcularlos todos:
    el tamaño puede cambiar en medio de la corrida cuando el servidor rechaza un
    lote, y así el siguiente ya sale con el valor corregido.

    Se recorre por índice y no con `pendientes[desde:]`: esa forma copia toda la
    lista restante en cada lote y el costo total pasa a ser cuadrático. Con una
    materia (3.000 fragmentos) no se nota; con el corpus entero (150.000) el
    armado de lotes tarda más que los embeddings — medido, 3 frag/s contra 52."""
    lote, suma = [], 0
    for k in range(desde, len(pendientes)):
        item = pendientes[k]
        t = tokens_estimados(item[1])
        if lote and (suma + t > presupuesto or len(lote) >= tope):
            break
        lote.append(item)
        suma += t
    return lote


def embeber_lote(url, textos):
    """Un request, con un reintento por corte transitorio. Devuelve los vectores
    o None si no se pudo."""
    for intento in range(REINTENTOS + 1):
        try:
            return embeber(url, textos)
        except Exception:
            if intento < REINTENTOS:
                time.sleep(0.4)
    return None


def main():
    ap = argparse.ArgumentParser(description="Vectoriza el índice de StudIA")
    ap.add_argument("--db", required=True)
    ap.add_argument("--url", default="http://127.0.0.1:8081",
                    help="servidor con /v1/embeddings (default 127.0.0.1:8081)")
    ap.add_argument("--modelo", default="",
                    help="nombre del modelo, sólo para registrarlo")
    ap.add_argument("--limpiar", action="store_true",
                    help="borrar los vectores previos y recalcular todo")
    ap.add_argument("--materia", default=None,
                    help="vectorizar SOLO esa materia (subcadena). Sirve para "
                         "probar el circuito completo sin esperar el corpus entero")
    ap.add_argument("--presupuesto", type=int, default=PRESUPUESTO_DEFAULT,
                    help="tokens por request. Subilo si el servidor corre con un "
                         "--ubatch-size grande: entran más fragmentos por request "
                         "y va proporcionalmente más rápido")
    ap.add_argument("--max-fragmentos", type=int, default=TOPE_FRAGMENTOS,
                    help="tope de fragmentos por request. 1 = el modo seguro, "
                         "compatible con cualquier servidor")
    args = ap.parse_args()

    if not os.path.exists(args.db):
        print("ERROR: no existe el índice %s" % args.db)
        return 2

    con = sqlite3.connect(args.db)
    con.executescript(ESQUEMA)
    if args.limpiar:
        con.execute("DELETE FROM vectores")
        con.commit()

    # El filtro por materia acota tanto el conteo como los pendientes.
    donde, params = "", []
    if args.materia:
        donde = ("JOIN documentos d ON d.id = f.doc_id "
                 "WHERE d.materia LIKE ?")
        params = ["%" + args.materia + "%"]
        print("Filtro: materia contiene «%s»" % args.materia)

    total = con.execute(
        "SELECT COUNT(*) FROM fragmentos f " + donde, params).fetchone()[0]
    hechos = con.execute(
        "SELECT COUNT(*) FROM fragmentos f "
        "JOIN vectores v ON v.frag_id = f.id " + donde, params).fetchone()[0]
    print("Fragmentos: %d   ya vectorizados: %d   faltan: %d"
          % (total, hechos, total - hechos), flush=True)
    if total == 0:
        print("No hay fragmentos que coincidan con el filtro.")
        con.close()
        return 0
    if hechos >= total:
        print("Nada que hacer.")
        con.close()
        return 0

    # Prueba de conexión antes de largar el lote completo.
    try:
        dim = len(embeber(args.url, ["prueba de conexión"])[0])
    except (urllib.error.URLError, urllib.error.HTTPError, OSError) as e:
        print("ERROR: no se pudo conectar a %s (%s).\n"
              "       Levantá un servidor de embeddings, por ejemplo:\n"
              "       llama-server -m <modelo-embeddings>.gguf --embeddings --port 8081"
              % (args.url, e))
        con.close()
        return 3
    except Exception as e:
        print("ERROR: el servidor no devolvió embeddings (%s).\n"
              "       ¿Lo arrancaste con --embeddings?" % e)
        con.close()
        return 4
    print("Servidor OK, dimensión del vector: %d\n" % dim, flush=True)

    cur = con.cursor()
    if args.materia:
        pendientes = cur.execute(
            "SELECT f.id, f.texto FROM fragmentos f "
            "JOIN documentos d ON d.id = f.doc_id "
            "LEFT JOIN vectores v ON v.frag_id = f.id "
            "WHERE v.frag_id IS NULL AND d.materia LIKE ?", params).fetchall()
    else:
        pendientes = cur.execute(
            "SELECT f.id, f.texto FROM fragmentos f "
            "LEFT JOIN vectores v ON v.frag_id = f.id "
            "WHERE v.frag_id IS NULL").fetchall()

    t0 = time.time()
    hechos_ahora = 0
    fallidos = 0
    presupuesto = args.presupuesto
    tope = max(1, args.max_fragmentos)
    print("Hasta %d fragmentos por request (presupuesto %d tokens)\n"
          % (tope, presupuesto), flush=True)

    i = 0
    avisado = 0
    while i < len(pendientes):
        lote = siguiente_lote(pendientes, i, presupuesto, tope)
        vectores = embeber_lote(args.url, [t for _id, t in lote])

        if vectores is None and len(lote) > 1:
            # El servidor no pudo con un lote de este tamaño. Se baja el tope y
            # se rearma DESDE EL MISMO PUNTO: no se saltea ningún fragmento y el
            # resto de la corrida hereda el tamaño que sí funciona.
            tope = max(1, len(lote) // 2)
            print("  ! rechazó un lote de %d; bajo a %d fragmentos por request"
                  % (len(lote), tope), flush=True)
            continue

        if vectores is None:
            # Falló un fragmento solo: se anota y se sigue. Volver a correr el
            # script lo reintenta, porque los pendientes se recalculan.
            fallidos += 1
            i += 1
            continue

        con.executemany(
            "INSERT OR REPLACE INTO vectores(frag_id, dim, vec) VALUES (?,?,?)",
            [(lote[j][0], len(vectores[j]), vec_a_blob(normalizar(vectores[j])))
             for j in range(len(lote)) if vectores[j]])
        con.commit()
        hechos_ahora += len(lote)
        i += len(lote)

        if hechos_ahora - avisado >= 400 or i >= len(pendientes):
            avisado = hechos_ahora
            transcurrido = time.time() - t0
            ritmo = hechos_ahora / transcurrido if transcurrido > 0 else 0
            falta = (len(pendientes) - hechos_ahora) / ritmo if ritmo > 0 else 0
            print("  [%d/%d] %.1f frag/s · faltan ~%d min"
                  % (hechos_ahora, len(pendientes), ritmo, falta / 60), flush=True)

    con.execute("INSERT OR REPLACE INTO vectores_info(clave,valor) VALUES (?,?)",
                ("dim", str(dim)))
    con.execute("INSERT OR REPLACE INTO vectores_info(clave,valor) VALUES (?,?)",
                ("modelo", args.modelo or args.url))
    con.commit()

    print("\n=== RESULTADO ===")
    print("  vectorizados %d" % hechos_ahora)
    if fallidos:
        print("  fallidos     %d  (volvé a correrlo para reintentarlos)" % fallidos)
    print("  tiempo       %.1f min" % ((time.time() - t0) / 60))
    con.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
