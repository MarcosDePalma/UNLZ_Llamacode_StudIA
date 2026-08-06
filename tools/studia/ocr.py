#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OCR de los documentos escaneados del índice de StudIA.

    python ocr.py --db "D:\\FACULTAD\\PPS\\StudIA\\studia.db"
    python ocr.py --db ... --min-paginas 200      # sólo los libros
    python ocr.py --db ... --materia "Sistemas de Control"

Toma los documentos que la ingesta marcó como `necesita_ocr` —PDFs sin capa de
texto, o sea fotos de páginas— los pasa por Tesseract y guarda los fragmentos
resultantes en el mismo índice, marcados como provenientes de OCR.

Es re-ejecutable: procesa sólo lo que sigue en `necesita_ocr`, así que se puede
cortar y retomar. Reusa el troceado de ingest.py para que no existan dos
implementaciones distintas.

Requisitos:
    winget install UB-Mannheim.TesseractOCR      (o el instalador del proyecto)
    pip install pytesseract pypdfium2

Limitación conocida: Tesseract reconoce texto, no matemática. En páginas con
fórmulas la prosa sale bien y las ecuaciones salen mal. Por eso los documentos
OCR quedan marcados en `detalle`, para poder medirlos aparte.
"""

import argparse
import os
import sqlite3
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ingest import abrir_db, fragmentar, guardar, unir_paginas

MARCA_OCR = "texto obtenido por OCR"
DPI_DEFAULT = 220          # suficiente para 10-12 pt; más DPI = más lento
MIN_CHARS_PAGINA = 25      # menos que esto, la página se considera sin texto


def cargar_dependencias():
    """Importa lo pesado recién cuando hace falta, y explica qué falta."""
    try:
        import pypdfium2  # noqa: F401
    except ImportError:
        return None, None, "Falta pypdfium2: pip install pypdfium2"
    try:
        import pytesseract
    except ImportError:
        return None, None, "Falta pytesseract: pip install pytesseract"
    import pypdfium2 as pdfium
    try:
        pytesseract.get_tesseract_version()
    except Exception:
        # El instalador de Windows no siempre agrega Tesseract al PATH, y la
        # terminal abierta antes de instalarlo tampoco lo ve. Se lo busca en
        # las ubicaciones habituales antes de darse por vencido.
        candidatos = [
            os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"),
                         "Tesseract-OCR", "tesseract.exe"),
            os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                         "Tesseract-OCR", "tesseract.exe"),
            os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs",
                         "Tesseract-OCR", "tesseract.exe"),
        ]
        encontrado = next((c for c in candidatos if os.path.isfile(c)), None)
        if not encontrado:
            return None, None, ("No se encontró el ejecutable de Tesseract. "
                                "Instalalo con: winget install UB-Mannheim.TesseractOCR")
        pytesseract.pytesseract.tesseract_cmd = encontrado
        try:
            pytesseract.get_tesseract_version()
        except Exception as e:
            return None, None, "Tesseract está en %s pero no responde (%s)" % (encontrado, e)
    return pdfium, pytesseract, None


def idiomas_disponibles(pytesseract):
    """spa+eng si está el español; si no, lo que haya."""
    try:
        langs = set(pytesseract.get_languages(config=""))
    except Exception:
        langs = set()
    if "spa" in langs and "eng" in langs:
        return "spa+eng"
    if "spa" in langs:
        return "spa"
    return "eng"


def ocr_documento(pdfium, pytesseract, ruta, idiomas, dpi, max_paginas):
    """Devuelve [(pagina, texto)] con lo que Tesseract pudo leer."""
    doc = pdfium.PdfDocument(ruta)
    total = len(doc)
    limite = total if max_paginas <= 0 else min(total, max_paginas)
    escala = dpi / 72.0
    paginas = []
    for i in range(limite):
        try:
            imagen = doc[i].render(scale=escala).to_pil()
            texto = pytesseract.image_to_string(imagen, lang=idiomas)
        except Exception:
            continue                      # una página rota no aborta el libro
        if texto and len(texto.strip()) >= MIN_CHARS_PAGINA:
            paginas.append((i + 1, texto))
    doc.close()
    # Se devuelve cuántas se PROCESARON, no cuántas tiene el PDF: si no, con
    # --max-paginas el documento quedaba marcado como completo teniendo sólo
    # las primeras páginas, y no se volvía a procesar nunca.
    return paginas, limite, total


def main():
    ap = argparse.ArgumentParser(description="OCR de los escaneados de StudIA")
    ap.add_argument("--db", required=True)
    ap.add_argument("--materia", default=None, help="acotar a una materia")
    ap.add_argument("--min-paginas", type=int, default=0,
                    help="sólo documentos de al menos N páginas (los libros)")
    ap.add_argument("--max-paginas", type=int, default=0,
                    help="tope de páginas por documento (0 = todas)")
    ap.add_argument("--dpi", type=int, default=DPI_DEFAULT)
    ap.add_argument("--limite", type=int, default=0,
                    help="procesar como mucho N documentos y salir")
    args = ap.parse_args()

    if not os.path.exists(args.db):
        print("ERROR: no existe el índice %s" % args.db)
        return 2

    pdfium, pytesseract, falta = cargar_dependencias()
    if falta:
        print("ERROR: %s" % falta)
        return 3
    idiomas = idiomas_disponibles(pytesseract)
    print("Tesseract OK · idiomas: %s · %d DPI\n" % (idiomas, args.dpi), flush=True)
    if idiomas == "eng":
        print("AVISO: no está el paquete de español. El OCR va a ser peor en\n"
              "       textos en castellano. Bajá spa.traineddata de\n"
              "       https://github.com/tesseract-ocr/tessdata y copialo en\n"
              "       la carpeta tessdata de Tesseract.\n", flush=True)

    con = abrir_db(args.db)
    cur = con.cursor()
    sql = ("SELECT id, ruta, nombre, materia, paginas FROM documentos "
           "WHERE estado='necesita_ocr' AND ext='.pdf'")
    params = []
    if args.materia:
        sql += " AND materia LIKE ?"
        params.append("%" + args.materia + "%")
    if args.min_paginas > 0:
        sql += " AND paginas >= ?"
        params.append(args.min_paginas)
    sql += " ORDER BY paginas DESC"
    pendientes = cur.execute(sql, params).fetchall()
    if args.limite > 0:
        pendientes = pendientes[:args.limite]

    if not pendientes:
        print("No hay documentos pendientes de OCR con ese filtro.")
        con.close()
        return 0
    total_pag = sum(p[4] or 0 for p in pendientes)
    print("Documentos: %d · páginas: %d\n" % (len(pendientes), total_pag), flush=True)

    t0 = time.time()
    ok = fallidos = 0
    paginas_hechas = 0
    for idx, (doc_id, ruta, nombre, materia, npag) in enumerate(pendientes, 1):
        if not os.path.isfile(ruta):
            print("  ! %s: el archivo ya no está" % nombre)
            fallidos += 1
            continue
        print("  [%d/%d] %s (%s pág)…" % (idx, len(pendientes), nombre[:55], npag),
              flush=True)
        try:
            paginas, leidas, del_pdf = ocr_documento(pdfium, pytesseract, ruta,
                                                     idiomas, args.dpi,
                                                     args.max_paginas)
        except Exception as e:
            print("      error: %s: %s" % (type(e).__name__, str(e)[:120]))
            fallidos += 1
            continue

        texto, mapa = unir_paginas(paginas)
        if len(texto) < 200:
            print("      sin texto reconocible; queda pendiente")
            fallidos += 1
            continue

        frags = fragmentar(texto, mapa)
        # Si se truncó por --max-paginas queda como parcial y se puede retomar.
        parcial = leidas < del_pdf
        detalle = MARCA_OCR
        if parcial:
            detalle += " (parcial: %d de %d páginas)" % (leidas, del_pdf)
        meta = {
            'ruta': ruta, 'nombre': nombre, 'anio': '', 'cuatri': '',
            'materia_cod': '', 'materia': materia, 'subruta': '', 'ext': '.pdf',
            'bytes': os.path.getsize(ruta), 'huella': '', 'paginas': leidas,
            'chars': len(texto),
            'estado': 'necesita_ocr' if parcial else 'ok', 'detalle': detalle,
        }
        # Se conservan año/cuatrimestre/huella del registro original.
        prev = cur.execute("SELECT anio, cuatri, materia_cod, subruta, huella "
                           "FROM documentos WHERE id=?", (doc_id,)).fetchone()
        if prev:
            (meta['anio'], meta['cuatri'], meta['materia_cod'],
             meta['subruta'], meta['huella']) = prev
        guardar(con, meta, frags)
        con.commit()
        ok += 1
        paginas_hechas += leidas
        transcurrido = time.time() - t0
        ritmo = paginas_hechas / transcurrido if transcurrido > 0 else 0
        print("      %d fragmentos · %.1f pág/s" % (len(frags), ritmo), flush=True)

    print("\n=== RESULTADO ===")
    print("  recuperados  %d" % ok)
    print("  fallidos     %d" % fallidos)
    print("  páginas      %d" % paginas_hechas)
    print("  tiempo       %.1f min" % ((time.time() - t0) / 60))
    if ok:
        print("\n  Los documentos recuperados quedaron marcados con «%s» en el\n"
              "  campo detalle, para poder evaluarlos aparte." % MARCA_OCR)
    con.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
