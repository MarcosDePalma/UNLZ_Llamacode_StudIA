#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Verifica que el MODELO REAL obedezca el formato que le pide cada modo de StudIA.

    tests.bat Release                       (deja los prompts en %TEMP%)
    python probar_modos.py "Redes" "modelo OSI capas" "el modelo OSI"

Por qué existe: los tests de C++ comprueban que la instrucción esté en el
prompt. Eso no dice nada sobre si el modelo la cumple, y varias veces no la
cumplía. El formato de los modos estrictos se calibró CON esta herramienta:

- Poner el formato sólo en el prompt de sistema no alcanzaba: quedaba sepultado
  entre las reglas generales y el modelo respondía como en conversación. Va al
  final del mensaje de usuario, que es lo último que lee antes de generar.
- Pedirle las diez preguntas primero y las diez respuestas después, separadas
  por una línea, tampoco: es una estructura global y un 7B pierde el hilo a
  mitad de camino. Con pares «P:/R:» —un patrón local que se repite— lo sostiene.

Necesita el servidor de chat en 127.0.0.1:8080 y los prompts volcados por el
test `zz_volcarPromptsParaProbarContraElModelo` de tests/test_studia.cpp.
"""

import io
import json
import os
import re
import sqlite3
import sys
import urllib.request

URL = "http://127.0.0.1:8080/v1/chat/completions"
DB_DEFAULT = r"D:\FACULTAD\PPS\StudIA\studia.db"
PROMPTS = os.path.join(os.environ.get("TEMP", "/tmp"), "studia_prompts")

# Los mismos criterios que StudiaTexto::paresQR: si esto reconoce el par, la app
# también.
RX_P = re.compile(
    r"^[ \t>]*\**\s*(\d+)\s*[.)]\s*\**\s*(?:P|Pregunta)\s*[:.\-]\s*\**\s*(.*)$",
    re.IGNORECASE)
RX_R = re.compile(
    r"^[ \t>]*\**\s*(?:\d+\s*[.)]\s*)?\**\s*(?:R|Respuesta)\s*[:.\-]\s*\**\s*(.*)$",
    re.IGNORECASE)


def leer(nombre, sufijo=""):
    ruta = os.path.join(PROMPTS, nombre + sufijo + ".txt")
    if not os.path.exists(ruta):
        print("ERROR: falta %s\n       Corré tests.bat primero." % ruta)
        sys.exit(2)
    with io.open(ruta, encoding="utf-8") as f:
        return f.read()


def fragmentos(db, materia, consulta, k=6):
    """Los mismos fragmentos que le pasaría la app."""
    con = sqlite3.connect(db)
    q = """SELECT d.materia, d.nombre, f.pagina, f.texto
           FROM fragmentos_fts fts
           JOIN fragmentos f ON f.id = fts.rowid
           JOIN documentos d ON d.id = f.doc_id
           WHERE fragmentos_fts MATCH ? AND d.materia LIKE ?
           ORDER BY bm25(fragmentos_fts) LIMIT ?"""
    filas = list(con.execute(q, (consulta, "%" + materia + "%", k)))
    con.close()
    out = "### Fragmentos de la documentación académica\n\n"
    for i, (mat, doc, pag, txt) in enumerate(filas, 1):
        out += "[%d] %s · %s · pág. %s\n%s\n\n" % (i, mat, doc, pag, txt.strip())
    return out, len(filas)


def preguntar(sistema, usuario, max_tokens=2600):
    cuerpo = json.dumps({
        "messages": [{"role": "system", "content": sistema},
                     {"role": "user", "content": usuario}],
        "temperature": 0.6, "max_tokens": max_tokens, "stream": False,
    }).encode("utf-8")
    req = urllib.request.Request(URL, data=cuerpo,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=900) as r:
        d = json.loads(r.read().decode("utf-8"))
    return d["choices"][0]["message"]["content"]


def pares(salida):
    out, estado = [], 0
    for linea in salida.split("\n"):
        m = RX_P.match(linea)
        if m:
            out.append([m.group(2).strip(" *"), ""])
            estado = 1
            continue
        if out:
            m = RX_R.match(linea)
            if m:
                out[-1][1] = m.group(1).strip(" *")
                estado = 2
                continue
        l = linea.strip()
        if not l or not out:
            continue
        if estado == 1:
            out[-1][0] += " " + l.strip(" *")
        elif estado == 2:
            out[-1][1] += " " + l.strip(" *")
    return [p for p in out if p[0] and p[1]]


def revisar_pares(nombre, salida, esperados, max_palabras=None):
    print("\n===== %s =====" % nombre)
    ps = pares(salida)
    ok = (len(ps) == esperados)
    print("  pares completos: %d   (esperado %d)" % (len(ps), esperados))
    if not ok:
        print("  --- salida (primeros 500) ---")
        print("  " + salida[:500].replace("\n", "\n  "))
    if ps:
        largos = [len(p[1].split()) for p in ps]
        print("  palabras por respuesta: min %d  mediana %d  max %d"
              % (min(largos), sorted(largos)[len(largos) // 2], max(largos)))
        if max_palabras and max(largos) > max_palabras * 2:
            print("  AVISO: respuestas demasiado largas para una flashcard")
            ok = False
    primera = salida.strip().split("\n")[0].strip()
    if not RX_P.match(primera):
        print("  AVISO: arranca con texto suelto -> %r" % primera[:90])
        ok = False
    if "Frente" in salida or "Dorso" in salida:
        print("  AVISO: usa los rótulos Frente/Dorso")
        ok = False
    print("  %s" % ("OK" if ok else "REVISAR"))
    return ok


def main():
    materia = sys.argv[1] if len(sys.argv) > 1 else "Redes"
    consulta = sys.argv[2] if len(sys.argv) > 2 else "modelo OSI capas protocolo"
    tema = sys.argv[3] if len(sys.argv) > 3 else "el modelo OSI"
    db = sys.argv[4] if len(sys.argv) > 4 else DB_DEFAULT

    frags, n = fragmentos(db, materia, consulta)
    if n == 0:
        print("Sin fragmentos para «%s» en %s: probá otra consulta." % (consulta, materia))
        return 2
    print("materia: %s · tema: %s · fragmentos: %d" % (materia, tema, n))

    r = []
    pregunta = "### Pregunta del estudiante\n" + tema + "\n"

    r.append(revisar_pares("AUTOEVALUACION", preguntar(
        leer("autoevaluacion"),
        frags + pregunta + "\n" + leer("autoevaluacion", "_formato")), 10))

    r.append(revisar_pares("FLASHCARDS", preguntar(
        leer("flashcards"),
        frags + pregunta + "\n" + leer("flashcards", "_formato")), 5, 20))

    # Plan, primer turno: describe los temas y pide los datos, sin armar nada.
    print("\n===== PLAN (primer turno) =====")
    salida = preguntar(leer("plan1"),
                       frags + "### Pregunta del estudiante\nquiero un plan para "
                       + "estudiar " + tema + "\n\n" + leer("plan1", "_formato"))
    temas = "Temas que abarca" in salida
    pide = "necesito saber" in salida.lower() or "fecha" in salida.lower()
    arma = bool(re.search(r"\|.*sesi|## *Sesiones", salida, re.IGNORECASE))
    print("  lista los temas: %s   pide los datos: %s   arma el plan igual: %s"
          % (temas, pide, arma))
    ok = temas and pide and not arma
    if not ok:
        print("  " + salida[:500].replace("\n", "\n  "))
    print("  %s" % ("OK" if ok else "REVISAR"))
    r.append(ok)

    # Plan, segundo turno. Se le dan datos ESTRECHOS a propósito —3 días, y un
    # tema difícil contra uno fácil— porque ahí es donde fallaba: armaba planes
    # de una semana y le daba las mismas horas a todos los temas.
    print("\n===== PLAN (segundo turno) =====")
    conv = ("### Conversación previa (contexto, NO es documentación)\n\n"
            "Estudiante: quiero un plan para estudiar " + tema + "\n\n"
            "Vos: ## Temas que abarca\n- Primer subtema\n- Segundo subtema\n"
            "- Tercer subtema\n\n## Para armártelo necesito saber\n"
            "- Cuánto te cuesta cada tema\n- Cuántas horas por día\n"
            "- Para qué fecha\n\n"
            "Estudiante: el primer subtema me cuesta MUCHO, el segundo es "
            "normal y el tercero es MUY FÁCIL. Tengo 3 días y puedo dedicarle "
            "2 horas por día.\n\n")
    salida = preguntar(leer("plan2"),
                       conv + frags + "### Pregunta del estudiante\narmame el "
                       + "plan con esos datos\n\n" + leer("plan2", "_formato"))
    arma = bool(re.search(r"## *Sesiones", salida, re.IGNORECASE))
    revuelve = bool(re.search(r"necesito saber|para armártelo|"
                              r"cuántas horas por (semana|día) podés|"
                              r"para qué fecha lo necesitás", salida, re.IGNORECASE))
    # ¿Respeta los 3 días? Un "Día 4" o "Día 5" significa que se los inventó.
    dias = [int(d) for d in re.findall(r"[Dd]ía\s*(\d+)", salida)]
    de_mas = [d for d in dias if d > 3]
    # ¿Reparte desparejo? Se miran las horas del bloque "## Reparto".
    bloque = re.split(r"##\s*Reparto", salida)
    reparto = re.split(r"\n##", bloque[1])[0] if len(bloque) > 1 else ""
    horas = [float(h.replace(",", ".")) for h in
             re.findall(r"(\d+(?:[.,]\d+)?)\s*(?:h\b|horas?\b)", reparto)]
    desparejo = len(set(horas)) > 1
    # La tabla: una fila por día y las horas totales que dijo tener. Ojo, las
    # sesiones duran todas lo mismo por definición (las horas de un día); lo
    # desparejo se ve en CUÁNTOS días toca cada tema, no en la duración.
    tabla = re.split(r"##\s*Sesiones", salida)
    cuerpo = re.split(r"\n##", tabla[1])[0] if len(tabla) > 1 else ""
    filas = [f for f in re.findall(r"^\|[^\n]*$", cuerpo, re.MULTILINE)
             if not re.match(r"^\|[\s|:-]*$", f)          # separador de la tabla
             and "Qué estudiar" not in f]                 # encabezado
    dur = [float(d.replace(",", ".")) for f in filas
           for d in re.findall(r"\|\s*(\d+(?:[.,]\d+)?)\s*h", f)[:1]]
    total = sum(dur)
    filas_ok = (len(filas) == 3)          # tres días → tres filas
    total_ok = (abs(total - 6.0) < 0.01)  # 3 días × 2 h
    print("  arma las sesiones: %s   vuelve a pedir datos: %s" % (arma, revuelve))
    print("  días que menciona: %s   inventados (>3): %s"
          % (sorted(set(dias)) or "ninguno", de_mas or "ninguno"))
    print("  horas del reparto: %s   (desparejo: %s)"
          % (sorted(set(horas)) or "ninguna", desparejo))
    print("  filas de la tabla: %d (esperado 3)   horas totales: %g (esperado 6)"
          % (len(filas), total))
    ok = (arma and not revuelve and not de_mas and desparejo
          and filas_ok and total_ok)
    if not ok:
        print("  --- salida (primeros 900) ---")
        print("  " + salida[:900].replace("\n", "\n  "))
    print("  %s" % ("OK" if ok else "REVISAR"))
    r.append(ok)

    print("\n%d de %d bien" % (sum(r), len(r)))
    return 0 if all(r) else 1


if __name__ == "__main__":
    sys.exit(main())
