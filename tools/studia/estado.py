#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Informe del indice de StudIA: que se ingesto, que quedo afuera y como responde
una busqueda. Sirve para verificar la calidad del corpus y para sacar numeros
para el informe de la PPS.

Uso:
    python estado.py --db "D:\\FACULTAD\\PPS\\StudIA\\studia.db"
    python estado.py --db ... --buscar "modbus tcp" -k 3
"""

import argparse
import io
import os
import sqlite3
import sys

if hasattr(sys.stdout, 'buffer'):
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')


def linea(t=''):
    print(t, flush=True)


def resumen(cur):
    linea('=== ESTADO GENERAL ===')
    total = cur.execute('SELECT COUNT(*) FROM documentos').fetchone()[0]
    for estado, n in cur.execute(
            'SELECT estado, COUNT(*) FROM documentos GROUP BY estado ORDER BY 2 DESC'):
        linea('  %-16s %6d   %5.1f%%' % (estado, n, 100.0 * n / total))
    linea('  %-16s %6d' % ('TOTAL', total))

    f, c = cur.execute('SELECT COUNT(*), COALESCE(SUM(chars),0) FROM fragmentos').fetchone()
    linea('')
    linea('  fragmentos       %6d' % f)
    linea('  caracteres       %6.1f M' % (c / 1e6))
    linea('  chars/fragmento  %6d (promedio)' % (c / f if f else 0))
    pag = cur.execute("SELECT COALESCE(SUM(paginas),0) FROM documentos WHERE estado='ok'").fetchone()[0]
    linea('  paginas indexadas %5d' % pag)


def por_materia(cur):
    linea('')
    linea('=== POR MATERIA (documentos indexados / fragmentos) ===')
    filas = cur.execute("""
        SELECT d.anio, d.materia,
               SUM(CASE WHEN d.estado='ok' THEN 1 ELSE 0 END)           AS ok,
               SUM(CASE WHEN d.estado='necesita_ocr' THEN 1 ELSE 0 END) AS ocr,
               (SELECT COUNT(*) FROM fragmentos f
                JOIN documentos d2 ON d2.id=f.doc_id
                WHERE d2.materia=d.materia)                            AS frags
        FROM documentos d
        GROUP BY d.anio, d.materia
        ORDER BY d.anio, d.materia
    """).fetchall()
    linea('  %-4s %-52s %5s %5s %8s' % ('Ano', 'Materia', 'OK', 'OCR', 'Frag'))
    for anio, materia, ok, ocr, frags in filas:
        linea('  %-4s %-52s %5d %5d %8d' % (anio, (materia or '')[:52], ok, ocr, frags))


def problemas(cur, n=10):
    linea('')
    linea('=== DOCUMENTOS CON ERROR (primeros %d) ===' % n)
    filas = cur.execute(
        "SELECT nombre, materia, detalle FROM documentos WHERE estado='error' LIMIT ?",
        (n,)).fetchall()
    if not filas:
        linea('  (ninguno)')
    for nom, mat, det in filas:
        linea('  %s' % nom[:70])
        linea('      %s | %s' % ((mat or '')[:40], (det or '')[:90]))


def buscar(cur, consulta, k):
    linea('')
    linea('=== BUSQUEDA: "%s" ===' % consulta)
    filas = cur.execute("""
        SELECT d.nombre, d.materia, d.anio, f.pagina,
               bm25(fragmentos_fts) AS score, f.texto
        FROM fragmentos_fts
        JOIN fragmentos f  ON f.id = fragmentos_fts.rowid
        JOIN documentos d  ON d.id = f.doc_id
        WHERE fragmentos_fts MATCH ?
        ORDER BY score
        LIMIT ?
    """, (consulta, k)).fetchall()
    if not filas:
        linea('  (sin resultados)')
        return
    for i, (nom, mat, anio, pag, score, texto) in enumerate(filas, 1):
        linea('')
        linea('--- [%d] %s' % (i, nom[:65]))
        linea('    %s (%s) · pagina %s · score %.2f' % ((mat or '')[:45], anio, pag, score))
        linea('    ' + texto[:600].replace('\n', '\n    '))


def main():
    ap = argparse.ArgumentParser(description='Informe del indice de StudIA')
    ap.add_argument('--db', required=True)
    ap.add_argument('--buscar', default=None)
    ap.add_argument('-k', type=int, default=3)
    ap.add_argument('--materias', action='store_true', help='desglose por materia')
    args = ap.parse_args()

    if not os.path.exists(args.db):
        linea('ERROR: no existe %s' % args.db)
        return 2
    con = sqlite3.connect(args.db)
    cur = con.cursor()
    linea('Indice: %s  (%.1f MB)' % (args.db, os.path.getsize(args.db) / (1024 * 1024)))
    linea('')
    resumen(cur)
    if args.materias:
        por_materia(cur)
    problemas(cur)
    if args.buscar:
        buscar(cur, args.buscar, args.k)
    con.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
