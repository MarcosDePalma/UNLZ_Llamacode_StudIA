#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Lista TODO lo que el ingestor de StudIA deja afuera del indice, con el motivo.

Sirve para auditar el filtro: si algo importante quedo excluido por error, aca
aparece y se puede buscar por nombre. Reusa las mismas reglas que ingest.py
(se importan de ahi) para que ambos no se desincronicen nunca.

Uso:
    python listar_excluidos.py --corpus "D:\\FACULTAD\\PPS\\DATA" --salida "D:\\FACULTAD\\PPS"

Genera dos archivos:
    excluidos.csv          todo lo excluido, con motivo
    excluidos_revisar.csv  solo lo que vale la pena mirar a ojo
"""

import argparse
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ingest import (EXT_SOPORTADAS, EXT_VIEJAS, DIRS_IGNORADOS,
                    NOMBRES_IGNORADOS, partes_ruta)

# Extensiones que NO se indexan pero podrian ser trabajo propio de la carrera.
# Si aparecen fuera de una carpeta de software, conviene revisarlas.
EXT_SOSPECHOSAS = {
    '.ipynb', '.m', '.slx', '.mdl', '.c', '.cpp', '.h', '.ino', '.py',
    '.zip', '.rar', '.7z', '.mp4', '.avi', '.mkv', '.csv', '.json',
    '.sldprt', '.sldasm', '.dwg', '.step', '.stp', '.igs',
}


def es_carpeta_software(rel):
    for p in [x.lower() for x in rel.split(os.sep)[:-1]]:
        if p in DIRS_IGNORADOS or p.endswith('.dist-info') or p.endswith('.egg-info'):
            return True
    return False


def main():
    ap = argparse.ArgumentParser(description='Auditoria del filtro de StudIA')
    ap.add_argument('--corpus', required=True)
    ap.add_argument('--salida', required=True, help='carpeta donde dejar los CSV')
    args = ap.parse_args()

    os.makedirs(args.salida, exist_ok=True)
    filas, revisar = [], []
    incluidos = 0

    for dirpath, _dirs, files in os.walk(args.corpus):
        for f in files:
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, args.corpus)
            ext = os.path.splitext(f)[1].lower()
            try:
                mb = round(os.path.getsize(full) / (1024 * 1024), 3)
            except OSError:
                mb = 0.0

            en_software = es_carpeta_software(rel)
            es_doc = ext in EXT_SOPORTADAS or ext in EXT_VIEJAS

            if es_doc and not en_software and f.lower() not in NOMBRES_IGNORADOS:
                incluidos += 1
                continue

            # Motivo de la exclusion, del mas especifico al mas general.
            if f.lower() in NOMBRES_IGNORADOS:
                motivo = 'nombre_metadata_paquete'
            elif en_software and es_doc:
                motivo = 'documento_en_carpeta_software'
            elif en_software:
                motivo = 'archivo_de_software'
            else:
                motivo = 'extension_no_indexable'

            anio, cuatri, cod, materia, sub = partes_ruta(rel)
            fila = {'Motivo': motivo, 'Ext': ext or '(sin ext)', 'MB': mb,
                    'Anio': anio, 'Materia': materia, 'Archivo': f,
                    'RutaRel': rel}
            filas.append(fila)

            # Candidatos a falso positivo: documentos que quedaron atrapados en
            # una carpeta de software, o formatos de trabajo propio fuera de ellas.
            if motivo == 'documento_en_carpeta_software':
                revisar.append(fila)
            elif motivo == 'extension_no_indexable' and ext in EXT_SOSPECHOSAS:
                revisar.append(fila)

    campos = ['Motivo', 'Ext', 'MB', 'Anio', 'Materia', 'Archivo', 'RutaRel']
    for nombre, datos in (('excluidos.csv', filas),
                          ('excluidos_revisar.csv', revisar)):
        ruta = os.path.join(args.salida, nombre)
        with open(ruta, 'w', newline='', encoding='utf-8') as fh:
            w = csv.DictWriter(fh, fieldnames=campos)
            w.writeheader()
            w.writerows(sorted(datos, key=lambda r: (r['Motivo'], r['Ext'], r['RutaRel'])))
        print('%-24s %6d filas' % (nombre, len(datos)))

    print('\n=== RESUMEN ===')
    print('  indexados        %6d' % incluidos)
    print('  excluidos        %6d' % len(filas))
    print('  a revisar        %6d' % len(revisar))

    from collections import Counter
    print('\n=== POR MOTIVO ===')
    for m, n in Counter(r['Motivo'] for r in filas).most_common():
        print('  %-32s %6d' % (m, n))

    print('\n=== A REVISAR, por extension ===')
    for e, n in Counter(r['Ext'] for r in revisar).most_common(20):
        mb = sum(r['MB'] for r in revisar if r['Ext'] == e)
        print('  %-12s %5d   %8.1f MB' % (e, n, mb))
    return 0


if __name__ == '__main__':
    sys.exit(main())
