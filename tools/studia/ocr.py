#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OCR de los documentos escaneados del índice de StudIA.

    python ocr.py --db "D:\\FACULTAD\\PPS\\StudIA\\studia.db"
    python ocr.py --db ... --min-paginas 200      # sólo los libros
    python ocr.py --db ... --materia "Sistemas de Control"
    python ocr.py --db ... --procesos 8           # menos carga en la máquina

Toma los documentos que la ingesta marcó como `necesita_ocr` —PDFs sin capa de
texto, o sea fotos de páginas— los pasa por Tesseract y guarda los fragmentos
resultantes en el mismo índice, marcados como provenientes de OCR.

Reparte las páginas entre varios procesos: Tesseract usa un núcleo por página,
así que el trabajo escala casi lineal con la cantidad de procesos. Cada worker
mantiene abierto el PDF que está procesando para no reabrirlo por página.

Es re-ejecutable: procesa sólo lo que sigue en `necesita_ocr`, así que se puede
cortar y retomar. Reusa el troceado de ingest.py para que no existan dos
implementaciones distintas.

Requisitos:
    winget install UB-Mannheim.TesseractOCR      (con el paquete de español)
    pip install pytesseract pypdfium2

Limitación conocida: Tesseract reconoce texto, no matemática. En páginas con
fórmulas la prosa sale bien y las ecuaciones salen mal. Por eso los documentos
OCR quedan marcados en `detalle`, para poder medirlos aparte.
"""

import argparse
import multiprocessing as mp
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ingest import abrir_db, fragmentar, guardar, unir_paginas

MARCA_OCR = "texto obtenido por OCR"
# 220 DPI medido contra 150 y 180 sobre una tabla técnica: a menos resolución se
# pierden columnas de números enteras y se confunden caracteres. No bajarlo.
DPI_DEFAULT = 220
MIN_CHARS_PAGINA = 25      # menos que esto, la página se considera sin texto
MIN_CHARS_DOC = 200        # menos que esto, el documento no se da por recuperado


# ── Worker ───────────────────────────────────────────────────────────────────
# Estado por proceso: el PDF abierto se reusa entre páginas del mismo documento.
_W = {"doc": None, "ruta": None, "idiomas": "spa+eng", "dpi": DPI_DEFAULT}


def _init_worker(tesseract_cmd, idiomas, dpi):
    import pytesseract
    if tesseract_cmd:
        pytesseract.pytesseract.tesseract_cmd = tesseract_cmd
    _W["idiomas"] = idiomas
    _W["dpi"] = dpi


def _ocr_pagina(tarea):
    """(ruta, indice) -> (numero_de_pagina, texto). Nunca lanza: una página
    rota no puede tumbar el libro entero."""
    ruta, i = tarea
    import pypdfium2 as pdfium
    import pytesseract
    try:
        if _W["ruta"] != ruta:
            if _W["doc"] is not None:
                _W["doc"].close()
            _W["doc"] = pdfium.PdfDocument(ruta)
            _W["ruta"] = ruta
        imagen = _W["doc"][i].render(scale=_W["dpi"] / 72.0).to_pil()
        texto = pytesseract.image_to_string(imagen, lang=_W["idiomas"])
    except Exception:
        return (i + 1, "")
    return (i + 1, texto or "")


# ── Dependencias ─────────────────────────────────────────────────────────────

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
        # El instalador de Windows no siempre agrega Tesseract al PATH, y una
        # terminal abierta antes de instalarlo tampoco lo ve.
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


def ruta_tesseract(pytesseract):
    cmd = getattr(pytesseract.pytesseract, "tesseract_cmd", "")
    return cmd if os.path.isabs(str(cmd)) else ""


# ── Deduplicación ────────────────────────────────────────────────────────────

def original_ya_procesado(con, huella, doc_id):
    """Si otro documento con la MISMA huella ya fue reconocido, devuelve su
    (id, nombre). Evita gastar media hora de CPU en el mismo libro guardado dos
    veces en carpetas distintas."""
    if not huella:
        return None
    return con.execute(
        "SELECT id, nombre FROM documentos "
        "WHERE huella = ? AND id <> ? AND estado = 'ok' LIMIT 1",
        (huella, doc_id)).fetchone()


def main():
    ap = argparse.ArgumentParser(description="OCR de los escaneados de StudIA")
    ap.add_argument("--db", required=True)
    ap.add_argument("--materia", default=None, help="acotar a una materia")
    ap.add_argument("--min-paginas", type=int, default=0,
                    help="sólo documentos de al menos N páginas")
    ap.add_argument("--max-paginas", type=int, default=0,
                    help="tope de páginas por documento (0 = todas)")
    ap.add_argument("--dpi", type=int, default=DPI_DEFAULT)
    ap.add_argument("--procesos", type=int, default=0,
                    help="páginas en paralelo (0 = todos los núcleos)")
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
    procesos = args.procesos if args.procesos > 0 else (os.cpu_count() or 4)
    print("Tesseract OK · idiomas: %s · %d DPI · %d procesos\n"
          % (idiomas, args.dpi, procesos), flush=True)
    if idiomas == "eng":
        print("AVISO: falta el paquete de español; el OCR va a ser peor.\n", flush=True)

    con = abrir_db(args.db)
    cur = con.cursor()
    sql = ("SELECT id, ruta, nombre, materia, paginas, huella FROM documentos "
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
    ok = fallidos = salteados = 0
    paginas_hechas = 0
    pool = mp.Pool(procesos, initializer=_init_worker,
                   initargs=(ruta_tesseract(pytesseract), idiomas, args.dpi))
    try:
        for idx, (doc_id, ruta, nombre, materia, npag, huella) in enumerate(pendientes, 1):
            gemelo = original_ya_procesado(con, huella, doc_id)
            if gemelo:
                cur.execute("UPDATE documentos SET estado='duplicado', detalle=? "
                            "WHERE id=?", ("copia de: %s (ya reconocido)" % gemelo[1], doc_id))
                con.commit()
                salteados += 1
                print("  [%d/%d] %s → duplicado de uno ya reconocido, se saltea"
                      % (idx, len(pendientes), nombre[:50]), flush=True)
                continue
            if not os.path.isfile(ruta):
                print("  [%d/%d] %s → el archivo ya no está"
                      % (idx, len(pendientes), nombre[:50]))
                fallidos += 1
                continue

            try:
                doc = pdfium.PdfDocument(ruta)
                del_pdf = len(doc)
                doc.close()
            except Exception as e:
                print("      no se pudo abrir: %s" % type(e).__name__)
                fallidos += 1
                continue
            limite = del_pdf if args.max_paginas <= 0 else min(del_pdf, args.max_paginas)
            print("  [%d/%d] %s (%d pág)…" % (idx, len(pendientes), nombre[:50], limite),
                  flush=True)

            tareas = [(ruta, i) for i in range(limite)]
            paginas = []
            hechas = 0
            td = time.time()
            for numero, texto in pool.imap(_ocr_pagina, tareas, chunksize=2):
                hechas += 1
                if texto and len(texto.strip()) >= MIN_CHARS_PAGINA:
                    paginas.append((numero, texto))
                # Aviso periódico: sin esto, un libro largo pasaba media hora
                # sin dar señales de vida.
                if hechas % 25 == 0 or hechas == limite:
                    dt = time.time() - td
                    print("        %d/%d pág · %.1f pág/s" % (hechas, limite, hechas / dt if dt else 0),
                          flush=True)
            paginas.sort(key=lambda x: x[0])

            texto, mapa = unir_paginas(paginas)
            if len(texto) < MIN_CHARS_DOC:
                print("      sin texto reconocible; queda pendiente")
                fallidos += 1
                continue

            parcial = limite < del_pdf
            detalle = MARCA_OCR + (" (parcial: %d de %d páginas)" % (limite, del_pdf)
                                   if parcial else "")
            meta = {'ruta': ruta, 'nombre': nombre, 'anio': '', 'cuatri': '',
                    'materia_cod': '', 'materia': materia, 'subruta': '', 'ext': '.pdf',
                    'bytes': os.path.getsize(ruta), 'huella': huella or '',
                    'paginas': limite, 'chars': len(texto),
                    'estado': 'necesita_ocr' if parcial else 'ok', 'detalle': detalle}
            prev = cur.execute("SELECT anio, cuatri, materia_cod, subruta "
                               "FROM documentos WHERE id=?", (doc_id,)).fetchone()
            if prev:
                (meta['anio'], meta['cuatri'],
                 meta['materia_cod'], meta['subruta']) = prev
            frags = fragmentar(texto, mapa)
            guardar(con, meta, frags)
            con.commit()
            ok += 1
            paginas_hechas += limite
            print("      %d fragmentos" % len(frags), flush=True)
    finally:
        pool.close()
        pool.join()

    transcurrido = time.time() - t0
    print("\n=== RESULTADO ===")
    print("  recuperados  %d" % ok)
    print("  salteados    %d  (duplicados de otro ya reconocido)" % salteados)
    print("  fallidos     %d" % fallidos)
    print("  páginas      %d" % paginas_hechas)
    print("  tiempo       %.1f min" % (transcurrido / 60))
    if paginas_hechas:
        print("  ritmo        %.2f pág/s" % (paginas_hechas / transcurrido))
    if ok:
        print("\n  Los recuperados quedaron marcados con «%s» en detalle,\n"
              "  para poder evaluarlos aparte." % MARCA_OCR)
    con.close()
    return 0


if __name__ == "__main__":
    mp.freeze_support()      # necesario en Windows
    sys.exit(main())
