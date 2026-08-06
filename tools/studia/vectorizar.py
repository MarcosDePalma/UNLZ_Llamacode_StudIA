#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Calcula los embeddings de los fragmentos del índice de StudIA y los guarda
dentro de la misma base.

    python vectorizar.py --db "D:\\FACULTAD\\PPS\\StudIA\\studia.db" \
                         --url http://127.0.0.1:8081

Por qué hace falta: la búsqueda por palabras (BM25) no entiende significado. Dos
formas de preguntar lo mismo ("¿qué pasa si…?" / "¿qué sucede si…?") se comportan
distinto sólo porque una palabra es más frecuente que la otra. Los embeddings
comparan por SENTIDO y resuelven esa clase de problema de raíz.

Necesita un servidor OpenAI-compatible que exponga /v1/embeddings. Lo natural es
un segundo `llama-server` con un modelo de embeddings chico:

    llama-server -m nomic-embed-text-v1.5.Q8_0.gguf --embeddings --port 8081

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

LOTE = 32            # fragmentos por request
TIMEOUT = 180        # s por request; un lote grande en CPU puede tardar
REINTENTOS = 3


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
    for i in range(0, len(pendientes), LOTE):
        lote = pendientes[i:i + LOTE]
        textos = [t for _id, t in lote]
        vectores = None
        for intento in range(REINTENTOS):
            try:
                vectores = embeber(args.url, textos)
                break
            except Exception as e:
                if intento == REINTENTOS - 1:
                    print("  ! lote %d falló definitivamente: %s" % (i // LOTE, e))
                    fallidos += len(lote)
                else:
                    time.sleep(1.5 * (intento + 1))
        if vectores is None:
            continue

        con.executemany(
            "INSERT OR REPLACE INTO vectores(frag_id, dim, vec) VALUES (?,?,?)",
            [(lote[j][0], len(vectores[j]), vec_a_blob(normalizar(vectores[j])))
             for j in range(len(lote))])
        con.commit()
        hechos_ahora += len(lote)

        if (i // LOTE) % 10 == 0 or i + LOTE >= len(pendientes):
            transcurrido = time.time() - t0
            ritmo = hechos_ahora / transcurrido if transcurrido > 0 else 0
            falta = (len(pendientes) - hechos_ahora) / ritmo if ritmo > 0 else 0
            print("  [%d/%d] %.0f frag/s · faltan ~%d min"
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
