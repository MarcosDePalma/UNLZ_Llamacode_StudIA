#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Arma una copia del corpus con SOLO los documentos que alimentan el índice.

    python copiar_corpus.py --db studia.db --origen D:\\...\\DATA --destino D:\\...\\DATA_StudIA
    python copiar_corpus.py ... --simular      (no copia: sólo informa)

El corpus original tiene mucho que el índice no usa: instalaciones de software,
duplicados exactos, formatos viejos que las librerías no leen, y documentos sin
texto extraíble que se descartaron a mano. Esta copia deja fuera todo eso.

Se copian los documentos en estado 'ok', que son los que tienen fragmentos en el
índice. Se CONSERVA LA RUTA RELATIVA de cada uno: el ingestor saca de ahí el
año, el cuatrimestre y la materia, así que una copia con la estructura aplanada
perdería esa información y habría que reindexar a mano.

El origen se lee y nunca se modifica.
"""

import argparse
import os
import shutil
import sqlite3
import sys


def humano(n):
    for u in ("B", "KB", "MB", "GB"):
        if n < 1024 or u == "GB":
            return "%.1f %s" % (n, u)
        n /= 1024.0


def main():
    ap = argparse.ArgumentParser(description="Copia reducida del corpus de StudIA")
    ap.add_argument("--db", required=True, help="índice del que se lee qué se usa")
    ap.add_argument("--origen", required=True, help="carpeta DATA original (sólo lectura)")
    ap.add_argument("--destino", required=True, help="carpeta nueva a crear")
    ap.add_argument("--simular", action="store_true",
                    help="no copia nada: informa cuánto ocuparía")
    args = ap.parse_args()

    if not os.path.isfile(args.db):
        print("ERROR: no existe el índice %s" % args.db)
        return 2
    if not os.path.isdir(args.origen):
        print("ERROR: no existe el origen %s" % args.origen)
        return 2
    if os.path.normcase(os.path.abspath(args.origen)) == \
       os.path.normcase(os.path.abspath(args.destino)):
        print("ERROR: origen y destino son la misma carpeta")
        return 2

    con = sqlite3.connect(args.db)
    # 'ok' es el único estado con fragmentos en el índice. Los demás
    # (duplicado, descartado, formato_viejo, error) no aportan nada.
    filas = list(con.execute(
        "SELECT ruta, bytes, estado FROM documentos WHERE estado='ok' ORDER BY ruta"))
    resumen = list(con.execute(
        "SELECT estado, COUNT(*), SUM(bytes) FROM documentos GROUP BY estado"))
    con.close()

    print("=== qué hay en el índice ===")
    for estado, n, b in sorted(resumen, key=lambda x: -x[1]):
        marca = "  SE COPIA" if estado == "ok" else ""
        print("  %-14s %5d documentos  %10s%s" % (estado, n, humano(b or 0), marca))

    total_origen = 0
    for raiz, _dirs, archivos in os.walk(args.origen):
        for a in archivos:
            try:
                total_origen += os.path.getsize(os.path.join(raiz, a))
            except OSError:
                pass

    copiables, faltantes, bytes_copia = [], [], 0
    for ruta, _b, _e in filas:
        if not os.path.isfile(ruta):
            faltantes.append(ruta)
            continue
        rel = os.path.relpath(ruta, args.origen)
        if rel.startswith(".."):
            faltantes.append(ruta)      # fuera del origen: bibliografía propia
            continue
        copiables.append((ruta, rel))
        bytes_copia += os.path.getsize(ruta)

    print("\n=== resultado ===")
    print("  carpeta original : %10s" % humano(total_origen))
    print("  copia reducida   : %10s   (%d archivos)" % (humano(bytes_copia),
                                                         len(copiables)))
    if total_origen:
        print("  ahorro           : %10s   (%.0f%% menos)"
              % (humano(total_origen - bytes_copia),
                 100.0 * (total_origen - bytes_copia) / total_origen))
    if faltantes:
        print("  no encontrados   : %d (se omiten)" % len(faltantes))

    if args.simular:
        print("\n(simulación: no se copió nada)")
        return 0

    print("\nCopiando a %s ..." % args.destino)
    hechos = 0
    for origen, rel in copiables:
        destino = os.path.join(args.destino, rel)
        carpeta = os.path.dirname(destino)
        if carpeta and not os.path.isdir(carpeta):
            os.makedirs(carpeta)
        # copy2 conserva fechas; si ya existe con el mismo tamaño se saltea,
        # así se puede reanudar una copia interrumpida.
        if os.path.isfile(destino) and os.path.getsize(destino) == os.path.getsize(origen):
            hechos += 1
            continue
        shutil.copy2(origen, destino)
        hechos += 1
        if hechos % 200 == 0:
            print("  %d / %d" % (hechos, len(copiables)), flush=True)

    print("\nListo: %d archivos en %s" % (hechos, args.destino))
    print("La carpeta original no se tocó.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
