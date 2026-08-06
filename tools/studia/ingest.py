#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Ingestor de StudIA — asistente de estudio para Ingenieria Mecatronica (PPS UNLZ).

Recorre el corpus academico, extrae el texto de cada documento, lo parte en
fragmentos y arma un indice SQLite con busqueda de texto completo (FTS5).

El corpus se lee en modo SOLO LECTURA: nunca se modifica, mueve ni borra nada
de la carpeta original.

Uso tipico:
    python ingest.py --corpus "D:\\FACULTAD\\PPS\\DATA" --db "D:\\FACULTAD\\PPS\\StudIA\\studia.db"
    python ingest.py ... --materia Redes      # solo las materias que matcheen
    python ingest.py ... --limpiar            # rehacer el indice desde cero

Es re-ejecutable: los documentos ya ingestados (misma huella) se saltean, asi
que se puede cortar y retomar sin perder trabajo.
"""

import argparse
import hashlib
import os
import re
import sqlite3
import sys
import time
import unicodedata
from datetime import datetime

# ── Que archivos entran ───────────────────────────────────────────────────────

# Formatos que sabemos leer hoy.
EXT_SOPORTADAS = {'.pdf', '.docx', '.pptx', '.xlsx', '.txt', '.md'}
# Formatos viejos de Office: se registran pero no se pueden leer con estas libs.
EXT_VIEJAS = {'.doc', '.ppt', '.xls', '.rtf', '.odt'}

# Carpetas de software que se cuelan en el corpus (instalaciones de Python,
# librerias JS, entornos virtuales). Todo lo que este adentro se ignora.
DIRS_IGNORADOS = {
    '__pycache__', 'site-packages', 'node_modules', '.git', '.svn', '.hg',
    'venv', '.venv', 'env', '.env', 'dist-info', 'egg-info', 'lib2to3',
    'dlls', 'scripts', 'tcl', 'tk', 'idlelib', 'ensurepip', 'setuptools',
    'pip', 'wheel', 'pkg_resources', '.ipynb_checkpoints', 'target',
}
# Nombres de archivo que son metadata de paquetes, nunca material de estudio.
NOMBRES_IGNORADOS = {
    'top_level.txt', 'entry_points.txt', 'license.txt', 'licence.txt',
    'requires.txt', 'sources.txt', 'installed-files.txt', 'record.txt',
    'metadata.txt', 'pkg-info.txt', 'dependency_links.txt', 'namespace_packages.txt',
}

# Un documento con menos de esto (tras extraer) se considera sin texto util.
MIN_CHARS_UTIL = 200
# Tamano objetivo de cada fragmento y cuanto se solapa con el siguiente.
TAM_FRAGMENTO = 1200
SOLAPE = 200
# Fragmento mas corto que esto se descarta (ruido).
MIN_CHARS_FRAGMENTO = 40


# ── Esquema de la base ────────────────────────────────────────────────────────

ESQUEMA = """
CREATE TABLE IF NOT EXISTS documentos (
    id           INTEGER PRIMARY KEY,
    ruta         TEXT NOT NULL UNIQUE,   -- ruta absoluta al archivo original
    nombre       TEXT NOT NULL,
    anio         TEXT,                   -- 1A .. 5A
    cuatri       TEXT,                   -- 1C .. 10C
    materia_cod  TEXT,                   -- 3_007
    materia      TEXT,                   -- Redes de Comunicacion Industriales
    subruta      TEXT,                   -- lo que cuelga debajo de la materia
    ext          TEXT,
    bytes        INTEGER,
    huella       TEXT,                   -- md5(primeros 4MB) + tamano
    paginas      INTEGER DEFAULT 0,
    chars        INTEGER DEFAULT 0,
    fragmentos   INTEGER DEFAULT 0,
    estado       TEXT NOT NULL,          -- ok | necesita_ocr | formato_viejo | duplicado | error
    detalle      TEXT,
    ingestado_en TEXT
);
CREATE INDEX IF NOT EXISTS ix_doc_materia ON documentos(materia);
CREATE INDEX IF NOT EXISTS ix_doc_estado  ON documentos(estado);
CREATE INDEX IF NOT EXISTS ix_doc_huella  ON documentos(huella);

CREATE TABLE IF NOT EXISTS fragmentos (
    id      INTEGER PRIMARY KEY,
    doc_id  INTEGER NOT NULL REFERENCES documentos(id) ON DELETE CASCADE,
    pagina  INTEGER,
    orden   INTEGER,
    chars   INTEGER,
    texto   TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_frag_doc ON fragmentos(doc_id);

-- Busqueda de texto completo. remove_diacritics=2 hace que "mecanica"
-- encuentre "mecanica" y "mecanica" indistintamente (clave en espanol).
CREATE VIRTUAL TABLE IF NOT EXISTS fragmentos_fts USING fts5(
    texto,
    content='fragmentos',
    content_rowid='id',
    tokenize="unicode61 remove_diacritics 2"
);
"""


# ── Utilidades ────────────────────────────────────────────────────────────────

def huella_archivo(path, size):
    """Identidad barata del archivo: md5 de los primeros 4 MB + su tamano.
    Alcanza para detectar copias identicas sin leer 10 GB dos veces."""
    h = hashlib.md5()
    with open(path, 'rb') as f:
        h.update(f.read(4 * 1024 * 1024))
    return f"{h.hexdigest()}-{size}"


def ruta_ignorada(rel):
    partes = [p.lower() for p in rel.split(os.sep)[:-1]]
    for p in partes:
        if p in DIRS_IGNORADOS or p.endswith('.dist-info') or p.endswith('.egg-info'):
            return True
    return False


def sin_acentos(s):
    return ''.join(c for c in unicodedata.normalize('NFD', s)
                   if unicodedata.category(c) != 'Mn').lower()


def partes_ruta(rel):
    """De '5A\\10C\\(3_007)Redes_...\\sub\\a.pdf' saca anio, cuatri, codigo,
    nombre de materia y subruta."""
    p = rel.split(os.sep)
    anio   = p[0] if len(p) > 0 else ''
    cuatri = p[1] if len(p) > 1 else ''
    carpeta = p[2] if len(p) > 2 else ''
    sub = os.sep.join(p[3:-1]) if len(p) > 4 else ''
    m = re.match(r'^\((\d+_\d+)\)(.*)$', carpeta)
    if m:
        cod, nombre = m.group(1), m.group(2)
    else:
        cod, nombre = '', carpeta
    nombre = nombre.replace('_', ' ').strip()
    return anio, cuatri, cod, nombre, sub


def normalizar(t):
    if not t:
        return ''
    t = t.replace('\r\n', '\n').replace('\r', '\n')
    t = t.replace('\x00', '')
    # Algunos PDFs traen "surrogates" sueltos (bytes que no forman un caracter
    # valido). SQLite no los acepta y rompen la ingesta: se descartan.
    t = t.encode('utf-8', 'ignore').decode('utf-8')
    t = re.sub(r'[ \t\u00a0]+', ' ', t)
    t = re.sub(r'\n[ \t]*(?:\n[ \t]*)+', '\n\n', t)
    return t.strip()


# ── Extractores por formato ───────────────────────────────────────────────────
# Todos devuelven (paginas, n_paginas) donde paginas = [(nro, texto), ...].

def extraer_pdf(path):
    from pypdf import PdfReader
    r = PdfReader(path)
    if r.is_encrypted:
        try:
            r.decrypt('')
        except Exception:
            raise RuntimeError('PDF protegido con contrasena')
    n = len(r.pages)
    out = []
    for i in range(n):
        try:
            out.append((i + 1, r.pages[i].extract_text() or ''))
        except Exception:
            out.append((i + 1, ''))
    return out, n


def extraer_docx(path):
    import docx
    d = docx.Document(path)
    partes = [p.text for p in d.paragraphs]
    for t in d.tables:
        for fila in t.rows:
            partes.append(' | '.join(c.text.strip() for c in fila.cells))
    return [(1, '\n'.join(partes))], 1


def extraer_pptx(path):
    from pptx import Presentation
    pr = Presentation(path)
    paginas = []
    for i, slide in enumerate(pr.slides, 1):
        trozos = []
        for sh in slide.shapes:
            try:
                if sh.has_text_frame and sh.text_frame.text.strip():
                    trozos.append(sh.text_frame.text)
                if getattr(sh, 'has_table', False) and sh.has_table:
                    for fila in sh.table.rows:
                        trozos.append(' | '.join(c.text.strip() for c in fila.cells))
            except Exception:
                pass
        paginas.append((i, '\n'.join(trozos)))
    return paginas, len(paginas)


def extraer_xlsx(path):
    import openpyxl
    wb = openpyxl.load_workbook(path, read_only=True, data_only=True)
    hojas = wb.worksheets
    n = len(hojas)
    paginas = []
    for i, ws in enumerate(hojas, 1):
        filas = []
        for fila in ws.iter_rows(values_only=True):
            vals = [str(v).strip() for v in fila if v is not None and str(v).strip()]
            if vals:
                filas.append(' | '.join(vals))
            if len(filas) >= 3000:
                break
        paginas.append((i, 'Hoja: %s\n%s' % (ws.title, '\n'.join(filas))))
    wb.close()
    return paginas, n


def extraer_texto_plano(path):
    for enc in ('utf-8', 'utf-8-sig', 'cp1252', 'latin-1'):
        try:
            with open(path, 'r', encoding=enc) as f:
                return [(1, f.read(4 * 1024 * 1024))], 1
        except (UnicodeDecodeError, LookupError):
            continue
    return [(1, '')], 1


EXTRACTORES = {
    '.pdf':  extraer_pdf,
    '.docx': extraer_docx,
    '.pptx': extraer_pptx,
    '.xlsx': extraer_xlsx,
    '.txt':  extraer_texto_plano,
    '.md':   extraer_texto_plano,
}


# ── Fragmentacion ─────────────────────────────────────────────────────────────

def unir_paginas(paginas):
    """Concatena las paginas y devuelve (texto, mapa) donde mapa permite saber
    a que pagina pertenece cada posicion del texto."""
    partes, mapa, off = [], [], 0
    for nro, txt in paginas:
        txt = normalizar(txt)
        if not txt:
            continue
        mapa.append((off, nro))
        partes.append(txt)
        off += len(txt) + 2
    return '\n\n'.join(partes), mapa


def pagina_en(mapa, pos):
    nro = mapa[0][1] if mapa else 0
    for off, n in mapa:
        if off > pos:
            break
        nro = n
    return nro


def fragmentar(texto, mapa, tam=TAM_FRAGMENTO, solape=SOLAPE):
    """Parte el texto en fragmentos de ~tam caracteres, cortando en limites
    naturales (parrafo > oracion > palabra) y solapando para no partir ideas."""
    frags = []
    n = len(texto)
    i, orden = 0, 0
    while i < n:
        fin = min(i + tam, n)
        if fin < n:
            piso = i + tam // 2
            corte = texto.rfind('\n\n', piso, fin)
            if corte == -1:
                corte = texto.rfind('. ', piso, fin)
            if corte == -1:
                corte = texto.rfind(' ', piso, fin)
            if corte != -1:
                fin = corte + 1
        frag = texto[i:fin].strip()
        if len(frag) >= MIN_CHARS_FRAGMENTO:
            frags.append((pagina_en(mapa, i), orden, frag))
            orden += 1
        if fin >= n:
            break
        i = max(fin - solape, i + 1)
    return frags


# ── Base de datos ─────────────────────────────────────────────────────────────

def abrir_db(path, limpiar=False):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    if limpiar and os.path.exists(path):
        os.remove(path)
    con = sqlite3.connect(path)
    con.execute('PRAGMA journal_mode=WAL')
    con.execute('PRAGMA synchronous=NORMAL')
    con.executescript(ESQUEMA)
    con.commit()
    return con


def guardar(con, meta, frags):
    cur = con.cursor()
    cur.execute('SELECT id FROM documentos WHERE ruta=?', (meta['ruta'],))
    fila = cur.fetchone()
    if fila:
        cur.execute('DELETE FROM fragmentos_fts WHERE rowid IN '
                    '(SELECT id FROM fragmentos WHERE doc_id=?)', (fila[0],))
        cur.execute('DELETE FROM fragmentos WHERE doc_id=?', (fila[0],))
        cur.execute('DELETE FROM documentos WHERE id=?', (fila[0],))
    cur.execute(
        'INSERT INTO documentos (ruta,nombre,anio,cuatri,materia_cod,materia,subruta,'
        'ext,bytes,huella,paginas,chars,fragmentos,estado,detalle,ingestado_en) '
        'VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',
        (meta['ruta'], meta['nombre'], meta['anio'], meta['cuatri'],
         meta['materia_cod'], meta['materia'], meta['subruta'], meta['ext'],
         meta['bytes'], meta['huella'], meta['paginas'], meta['chars'],
         len(frags), meta['estado'], meta['detalle'],
         datetime.now().isoformat(timespec='seconds')))
    doc_id = cur.lastrowid
    for pagina, orden, texto in frags:
        cur.execute('INSERT INTO fragmentos (doc_id,pagina,orden,chars,texto) '
                    'VALUES (?,?,?,?,?)', (doc_id, pagina, orden, len(texto), texto))
        cur.execute('INSERT INTO fragmentos_fts (rowid,texto) VALUES (?,?)',
                    (cur.lastrowid, texto))
    return doc_id


# ── Recorrido y proceso ───────────────────────────────────────────────────────

def listar(corpus, filtro_materia=None):
    """Devuelve la lista de archivos candidatos (ya filtrada)."""
    salida = []
    filtro = sin_acentos(filtro_materia) if filtro_materia else None
    for dirpath, dirs, files in os.walk(corpus):
        dirs[:] = [d for d in dirs
                   if d.lower() not in DIRS_IGNORADOS
                   and not d.lower().endswith(('.dist-info', '.egg-info'))]
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext not in EXT_SOPORTADAS and ext not in EXT_VIEJAS:
                continue
            if f.lower() in NOMBRES_IGNORADOS:
                continue
            # '~$algo.docx' son archivos de bloqueo que Word/PowerPoint crean
            # mientras el documento esta abierto. No tienen contenido.
            if f.startswith('~$'):
                continue
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, corpus)
            if ruta_ignorada(rel):
                continue
            if filtro and filtro not in sin_acentos(rel):
                continue
            salida.append((full, rel, ext))
    salida.sort(key=lambda x: x[1])
    return salida


def procesar(con, corpus, archivos, verbose_cada=25):
    vistas = {}
    cur = con.cursor()
    for h, r in cur.execute('SELECT huella, ruta FROM documentos WHERE estado!="error"'):
        vistas.setdefault(h, r)
    # Se saltean los ya procesados, PERO no los que fallaron: si se corrige la
    # causa (una libreria que faltaba, por ejemplo) se reintentan al re-correr.
    ya = {r for (r,) in cur.execute(
        'SELECT ruta FROM documentos WHERE estado != "error"')}

    stats = {'ok': 0, 'necesita_ocr': 0, 'duplicado': 0,
             'formato_viejo': 0, 'error': 0, 'saltado': 0}
    total_frags, total_chars = 0, 0
    t0 = time.time()

    for idx, (full, rel, ext) in enumerate(archivos, 1):
        if idx % verbose_cada == 0 or idx == len(archivos):
            transcurrido = time.time() - t0
            ritmo = idx / transcurrido if transcurrido > 0 else 0
            falta = (len(archivos) - idx) / ritmo if ritmo > 0 else 0
            print('  [%d/%d] %.1f doc/s · faltan ~%d min · %d fragmentos'
                  % (idx, len(archivos), ritmo, falta / 60, total_frags), flush=True)

        if full in ya:
            stats['saltado'] += 1
            continue

        anio, cuatri, cod, materia, sub = partes_ruta(rel)
        try:
            size = os.path.getsize(full)
            hue = huella_archivo(full, size)
        except OSError as e:
            size, hue = 0, ''
            print('  ! no se pudo leer %s (%s)' % (rel, e), flush=True)

        meta = {'ruta': full, 'nombre': os.path.basename(full), 'anio': anio,
                'cuatri': cuatri, 'materia_cod': cod, 'materia': materia,
                'subruta': sub, 'ext': ext, 'bytes': size, 'huella': hue,
                'paginas': 0, 'chars': 0, 'estado': 'error', 'detalle': ''}

        if hue and hue in vistas:
            meta['estado'] = 'duplicado'
            meta['detalle'] = 'copia de: %s' % os.path.relpath(vistas[hue], corpus)
            guardar(con, meta, [])
            stats['duplicado'] += 1
            continue

        if ext in EXT_VIEJAS:
            meta['estado'] = 'formato_viejo'
            meta['detalle'] = 'formato %s no soportado por las librerias actuales' % ext
            guardar(con, meta, [])
            stats['formato_viejo'] += 1
            if hue:
                vistas[hue] = full
            continue

        try:
            paginas, n = EXTRACTORES[ext](full)
            meta['paginas'] = n
            texto, mapa = unir_paginas(paginas)
            meta['chars'] = len(texto)
            if len(texto) < MIN_CHARS_UTIL:
                meta['estado'] = 'necesita_ocr'
                meta['detalle'] = 'sin texto extraible (%d chars)' % len(texto)
                guardar(con, meta, [])
                stats['necesita_ocr'] += 1
            else:
                frags = fragmentar(texto, mapa)
                meta['estado'] = 'ok'
                guardar(con, meta, frags)
                stats['ok'] += 1
                total_frags += len(frags)
                total_chars += len(texto)
        except Exception as e:
            meta['estado'] = 'error'
            meta['detalle'] = '%s: %s' % (type(e).__name__, str(e)[:200])
            guardar(con, meta, [])
            stats['error'] += 1

        if hue:
            vistas[hue] = full
        if idx % 50 == 0:
            con.commit()

    con.commit()
    return stats, total_frags, total_chars, time.time() - t0


def ingestar_archivo(db_path, archivo, materia):
    """Suma UN documento suelto al indice (bibliografia propia del estudiante).

    Se usa desde la app cuando el usuario adjunta un PDF con el clip. La materia
    llega por parametro porque el archivo no cuelga del arbol anio/cuatri/materia
    del corpus: lo elige el estudiante en la UI.
    Devuelve 0 si quedo indexado, !=0 si no se pudo."""
    if not os.path.isfile(archivo):
        print('ERROR: no existe el archivo %s' % archivo)
        return 2
    ext = os.path.splitext(archivo)[1].lower()
    if ext not in EXT_SOPORTADAS and ext not in EXT_VIEJAS:
        print('ERROR: formato no soportado (%s). Soportados: %s'
              % (ext, ', '.join(sorted(EXT_SOPORTADAS))))
        return 3

    con = abrir_db(db_path)
    size = os.path.getsize(archivo)
    meta = {'ruta': os.path.abspath(archivo), 'nombre': os.path.basename(archivo),
            'anio': '', 'cuatri': '', 'materia_cod': '', 'materia': materia,
            'subruta': '', 'ext': ext, 'bytes': size,
            'huella': huella_archivo(archivo, size), 'paginas': 0, 'chars': 0,
            'estado': 'error', 'detalle': ''}

    if ext in EXT_VIEJAS:
        meta['estado'] = 'formato_viejo'
        meta['detalle'] = 'formato %s no soportado por las librerias actuales' % ext
        guardar(con, meta, [])
        con.commit(); con.close()
        print('FORMATO_VIEJO %s' % meta['nombre'])
        return 4

    try:
        paginas, n = EXTRACTORES[ext](archivo)
        meta['paginas'] = n
        texto, mapa = unir_paginas(paginas)
        meta['chars'] = len(texto)
        if len(texto) < MIN_CHARS_UTIL:
            meta['estado'] = 'necesita_ocr'
            meta['detalle'] = 'sin texto extraible (%d chars)' % len(texto)
            guardar(con, meta, [])
            con.commit(); con.close()
            print('SIN_TEXTO %s' % meta['nombre'])
            return 5
        frags = fragmentar(texto, mapa)
        meta['estado'] = 'ok'
        guardar(con, meta, frags)
        con.commit(); con.close()
        print('OK %s | %d paginas | %d fragmentos' % (meta['nombre'], n, len(frags)))
        return 0
    except Exception as e:
        meta['estado'] = 'error'
        meta['detalle'] = '%s: %s' % (type(e).__name__, str(e)[:200])
        guardar(con, meta, [])
        con.commit(); con.close()
        print('ERROR %s: %s' % (meta['nombre'], meta['detalle']))
        return 6


def quitar_archivo(db_path, ruta):
    """Saca un documento del indice (y sus fragmentos). Lo usa la app cuando el
    estudiante elimina algo de su bibliografia propia. Nunca se aplica al indice
    de la carpeta DATA: ese es de solo lectura."""
    if not os.path.exists(db_path):
        print('ERROR: no existe el indice %s' % db_path)
        return 2
    con = abrir_db(db_path)
    cur = con.cursor()
    objetivo = os.path.abspath(ruta)
    fila = cur.execute('SELECT id, nombre FROM documentos WHERE ruta=?',
                       (objetivo,)).fetchone()
    if not fila:
        # Segundo intento por nombre de archivo: la UI puede pasar la ruta con
        # separadores distintos.
        fila = cur.execute('SELECT id, nombre FROM documentos WHERE nombre=?',
                           (os.path.basename(ruta),)).fetchone()
    if not fila:
        con.close()
        print('ERROR: el documento no esta en el indice')
        return 3
    doc_id, nombre = fila
    cur.execute('DELETE FROM fragmentos_fts WHERE rowid IN '
                '(SELECT id FROM fragmentos WHERE doc_id=?)', (doc_id,))
    cur.execute('DELETE FROM fragmentos WHERE doc_id=?', (doc_id,))
    cur.execute('DELETE FROM documentos WHERE id=?', (doc_id,))
    con.commit()
    con.close()
    print('QUITADO %s' % nombre)
    return 0


def main():
    ap = argparse.ArgumentParser(description='Ingestor de StudIA')
    ap.add_argument('--corpus', help='carpeta raiz del material')
    ap.add_argument('--db', required=True, help='ruta del indice SQLite a generar')
    ap.add_argument('--materia', default=None, help='filtrar por materia (subcadena)')
    ap.add_argument('--limpiar', action='store_true', help='borrar el indice y rehacerlo')
    ap.add_argument('--archivo', default=None,
                    help='ingestar UN archivo suelto en vez de recorrer --corpus '
                         '(bibliografia propia; requiere --materia)')
    ap.add_argument('--quitar', default=None,
                    help='sacar del indice el documento de esa ruta')
    args = ap.parse_args()

    if args.quitar:
        return quitar_archivo(args.db, args.quitar)

    # Modo archivo suelto: lo usa la app al adjuntar bibliografia.
    if args.archivo:
        if not args.materia:
            print('ERROR: --archivo requiere --materia')
            return 2
        return ingestar_archivo(args.db, args.archivo, args.materia)

    if not args.corpus:
        print('ERROR: falta --corpus (o --archivo para un documento suelto)')
        return 2
    if not os.path.isdir(args.corpus):
        print('ERROR: no existe la carpeta %s' % args.corpus)
        return 2

    print('Corpus : %s' % args.corpus)
    print('Indice : %s' % args.db)
    if args.materia:
        print('Filtro : materia contiene "%s"' % args.materia)
    print('\nBuscando documentos...', flush=True)
    archivos = listar(args.corpus, args.materia)
    print('Documentos a procesar: %d\n' % len(archivos), flush=True)
    if not archivos:
        print('Nada que hacer.')
        return 0

    con = abrir_db(args.db, args.limpiar)
    stats, frags, chars, seg = procesar(con, args.corpus, archivos)

    print('\n=== RESULTADO ===')
    for k in ('ok', 'necesita_ocr', 'formato_viejo', 'duplicado', 'error', 'saltado'):
        if stats[k]:
            print('  %-15s %5d' % (k, stats[k]))
    print('  %-15s %5d' % ('fragmentos', frags))
    print('  %-15s %5.1f M' % ('caracteres', chars / 1e6))
    print('  %-15s %5.1f min' % ('tiempo', seg / 60))
    tam = os.path.getsize(args.db) / (1024 * 1024)
    print('  %-15s %5.1f MB' % ('tamano indice', tam))
    con.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
