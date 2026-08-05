#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Tests del ingestor de StudIA. Sin dependencias externas: usa unittest.

    python tools/studia/test_ingest.py

Tambien corre como parte de la suite del proyecto (`tests.bat`), registrado en
CMakeLists.txt como test_studia_ingest.
"""

import os
import sqlite3
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ingest import (DIRS_IGNORADOS, EXT_SOPORTADAS, MIN_CHARS_FRAGMENTO,
                    abrir_db, extraer_texto_plano, fragmentar, guardar,
                    listar, normalizar, pagina_en, partes_ruta,
                    ruta_ignorada, sin_acentos, unir_paginas)


class TestRutas(unittest.TestCase):
    def test_extrae_anio_cuatri_codigo_y_materia(self):
        rel = os.path.join('5A', '10C', '(3_007)Redes_de_Comunicacion_Industriales', 'a.pdf')
        anio, cuatri, cod, materia, sub = partes_ruta(rel)
        self.assertEqual(anio, '5A')
        self.assertEqual(cuatri, '10C')
        self.assertEqual(cod, '3_007')
        self.assertEqual(materia, 'Redes de Comunicacion Industriales')
        self.assertEqual(sub, '')

    def test_captura_la_subruta(self):
        rel = os.path.join('4A', '8C', '(0_033)Tecnologias', 'TPs', 'entrega', 'x.pdf')
        _a, _c, _cod, materia, sub = partes_ruta(rel)
        self.assertEqual(materia, 'Tecnologias')
        self.assertEqual(sub, os.path.join('TPs', 'entrega'))

    def test_carpeta_sin_codigo_entre_parentesis(self):
        rel = os.path.join('1A', '1C', 'Ingles', 'a.pdf')
        _a, _c, cod, materia, _s = partes_ruta(rel)
        self.assertEqual(cod, '')
        self.assertEqual(materia, 'Ingles')

    def test_ruta_corta_no_rompe(self):
        anio, cuatri, cod, materia, sub = partes_ruta('suelto.pdf')
        self.assertEqual(anio, 'suelto.pdf')
        self.assertEqual((cuatri, cod, sub), ('', '', ''))

    def test_detecta_carpetas_de_software(self):
        self.assertTrue(ruta_ignorada(os.path.join('a', 'site-packages', 'x.txt')))
        self.assertTrue(ruta_ignorada(os.path.join('a', '__pycache__', 'x.txt')))
        self.assertTrue(ruta_ignorada(os.path.join('a', 'numpy-1.0.dist-info', 'x.txt')))
        self.assertTrue(ruta_ignorada(os.path.join('a', 'algo.egg-info', 'x.txt')))
        self.assertFalse(ruta_ignorada(os.path.join('5A', '10C', 'Redes', 'x.pdf')))

    def test_el_propio_archivo_no_cuenta_como_carpeta(self):
        # Un archivo llamado 'venv.txt' en una carpeta normal debe pasar.
        self.assertFalse(ruta_ignorada(os.path.join('5A', 'Redes', 'venv.txt')))

    def test_sin_acentos(self):
        self.assertEqual(sin_acentos('Mecánica'), 'mecanica')
        # La enie tambien pierde la virgulilla. Es lo buscado: el filtro
        # --materia normaliza los dos lados, asi que escribir "diseno"
        # encuentra la carpeta "Diseño_de_Componentes".
        self.assertEqual(sin_acentos('DISEÑO'), 'diseno')


class TestNormalizar(unittest.TestCase):
    def test_colapsa_espacios_y_saltos(self):
        self.assertEqual(normalizar('hola   mundo'), 'hola mundo')
        self.assertEqual(normalizar('a\n\n\n\nb'), 'a\n\nb')
        self.assertEqual(normalizar('  x  '), 'x')

    def test_normaliza_fin_de_linea_windows(self):
        self.assertEqual(normalizar('a\r\nb'), 'a\nb')

    def test_elimina_nulos_y_surrogates(self):
        # Los PDFs rotos traen estos caracteres y SQLite los rechaza.
        self.assertEqual(normalizar('a\x00b'), 'ab')
        self.assertNotIn('\udbc0', normalizar('texto\udbc0roto'))

    def test_texto_vacio(self):
        self.assertEqual(normalizar(''), '')
        self.assertEqual(normalizar(None), '')


class TestFragmentar(unittest.TestCase):
    def test_texto_corto_es_un_solo_fragmento(self):
        texto, mapa = unir_paginas([(1, 'Una idea corta pero suficientemente larga.')])
        frags = fragmentar(texto, mapa)
        self.assertEqual(len(frags), 1)
        self.assertEqual(frags[0][0], 1)      # pagina
        self.assertEqual(frags[0][1], 0)      # orden

    def test_texto_largo_se_parte(self):
        texto, mapa = unir_paginas([(1, 'palabra ' * 2000)])
        frags = fragmentar(texto, mapa, tam=1000, solape=100)
        self.assertGreater(len(frags), 5)
        for _pag, _orden, t in frags:
            self.assertLessEqual(len(t), 1000)

    def test_los_fragmentos_se_solapan(self):
        texto = 'A' * 500 + ' ' + 'B' * 500 + ' ' + 'C' * 500
        frags = fragmentar(texto, [(0, 1)], tam=600, solape=150)
        self.assertGreaterEqual(len(frags), 2)
        # El final del primero reaparece al principio del segundo.
        cola = frags[0][2][-80:]
        self.assertIn(cola[:40], frags[1][2])

    def test_descarta_fragmentos_minusculos(self):
        frags = fragmentar('xy', [(0, 1)])
        self.assertEqual(frags, [])

    def test_orden_es_consecutivo(self):
        texto, mapa = unir_paginas([(1, 'palabra ' * 900)])
        frags = fragmentar(texto, mapa, tam=800, solape=80)
        self.assertEqual([f[1] for f in frags], list(range(len(frags))))

    def test_asigna_la_pagina_correcta(self):
        paginas = [(1, 'uno ' * 300), (2, 'dos ' * 300), (3, 'tres ' * 300)]
        texto, mapa = unir_paginas(paginas)
        frags = fragmentar(texto, mapa, tam=400, solape=0)
        vistas = {f[0] for f in frags}
        self.assertEqual(vistas, {1, 2, 3})
        # El primer fragmento sale de la pagina 1.
        self.assertEqual(frags[0][0], 1)

    def test_paginas_vacias_no_desplazan_el_mapa(self):
        texto, mapa = unir_paginas([(1, ''), (2, 'contenido real de la pagina dos')])
        self.assertEqual(mapa, [(0, 2)])
        self.assertEqual(pagina_en(mapa, 0), 2)

    def test_pagina_en_sin_mapa(self):
        self.assertEqual(pagina_en([], 10), 0)


class TestListar(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def _crear(self, rel, contenido='contenido de prueba'):
        full = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, 'w', encoding='utf-8') as f:
            f.write(contenido)
        return full

    def test_toma_documentos_y_descarta_software(self):
        self._crear(os.path.join('5A', '10C', 'Redes', 'apunte.pdf'))
        self._crear(os.path.join('5A', '10C', 'Redes', 'notas.txt'))
        self._crear(os.path.join('5A', '10C', 'Redes', 'script.py'))
        self._crear(os.path.join('5A', '10C', 'Redes', 'site-packages', 'README.txt'))
        self._crear(os.path.join('5A', '10C', 'Redes', '__pycache__', 'x.txt'))
        nombres = sorted(os.path.basename(a[0]) for a in listar(self.root))
        self.assertEqual(nombres, ['apunte.pdf', 'notas.txt'])

    def test_descarta_metadata_de_paquetes(self):
        self._crear(os.path.join('1A', '1C', 'Mat', 'top_level.txt'))
        self._crear(os.path.join('1A', '1C', 'Mat', 'entry_points.txt'))
        self._crear(os.path.join('1A', '1C', 'Mat', 'apunte.txt'))
        nombres = [os.path.basename(a[0]) for a in listar(self.root)]
        self.assertEqual(nombres, ['apunte.txt'])

    def test_descarta_archivos_de_bloqueo_de_office(self):
        self._crear(os.path.join('1A', '1C', 'Mat', '~$informe.docx'))
        self._crear(os.path.join('1A', '1C', 'Mat', 'informe.txt'))
        nombres = [os.path.basename(a[0]) for a in listar(self.root)]
        self.assertEqual(nombres, ['informe.txt'])

    def test_filtro_por_materia_ignora_acentos(self):
        self._crear(os.path.join('3A', '5C', 'Termodinámica', 'a.txt'))
        self._crear(os.path.join('5A', '10C', 'Redes', 'b.txt'))
        r = listar(self.root, 'termodinamica')     # sin tilde
        self.assertEqual(len(r), 1)
        self.assertTrue(r[0][1].endswith('a.txt'))

    def test_incluye_formatos_viejos_para_registrarlos(self):
        # .doc no se puede leer, pero debe aparecer para quedar registrado.
        self._crear(os.path.join('1A', '1C', 'Mat', 'viejo.doc'))
        nombres = [os.path.basename(a[0]) for a in listar(self.root)]
        self.assertIn('viejo.doc', nombres)

    def test_extensiones_soportadas_son_las_esperadas(self):
        self.assertIn('.pdf', EXT_SOPORTADAS)
        self.assertIn('.docx', EXT_SOPORTADAS)
        self.assertNotIn('.py', EXT_SOPORTADAS)
        self.assertNotIn('.exe', EXT_SOPORTADAS)
        self.assertIn('site-packages', DIRS_IGNORADOS)


class TestBaseDeDatos(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.db = os.path.join(self.tmp.name, 'sub', 'idx.db')

    def tearDown(self):
        self.tmp.cleanup()

    def _meta(self, ruta='C:/x/a.pdf', estado='ok'):
        return {'ruta': ruta, 'nombre': 'a.pdf', 'anio': '5A', 'cuatri': '10C',
                'materia_cod': '3_007', 'materia': 'Redes', 'subruta': '',
                'ext': '.pdf', 'bytes': 10, 'huella': 'h1', 'paginas': 2,
                'chars': 100, 'estado': estado, 'detalle': ''}

    def test_crea_el_esquema_y_la_tabla_fts(self):
        con = abrir_db(self.db)
        tablas = {r[0] for r in con.execute(
            "SELECT name FROM sqlite_master WHERE type IN ('table','view')")}
        self.assertIn('documentos', tablas)
        self.assertIn('fragmentos', tablas)
        self.assertIn('fragmentos_fts', tablas)
        con.close()

    def test_guardar_indexa_para_busqueda(self):
        con = abrir_db(self.db)
        guardar(con, self._meta(), [(1, 0, 'El protocolo Modbus TCP sobre Ethernet.')])
        con.commit()
        r = con.execute("SELECT COUNT(*) FROM fragmentos_fts "
                        "WHERE fragmentos_fts MATCH '\"modbus\"'").fetchone()
        self.assertEqual(r[0], 1)
        con.close()

    def test_la_busqueda_ignora_tildes(self):
        con = abrir_db(self.db)
        guardar(con, self._meta(), [(1, 0, 'Análisis de la mecánica del sólido.')])
        con.commit()
        for termino in ('"mecanica"', '"mecánica"', '"analisis"'):
            n = con.execute("SELECT COUNT(*) FROM fragmentos_fts "
                            "WHERE fragmentos_fts MATCH ?", (termino,)).fetchone()[0]
            self.assertEqual(n, 1, 'fallo con %s' % termino)
        con.close()

    def test_reingestar_reemplaza_sin_duplicar(self):
        con = abrir_db(self.db)
        guardar(con, self._meta(), [(1, 0, 'version vieja del texto')])
        guardar(con, self._meta(), [(1, 0, 'version nueva del texto')])
        con.commit()
        self.assertEqual(con.execute('SELECT COUNT(*) FROM documentos').fetchone()[0], 1)
        self.assertEqual(con.execute('SELECT COUNT(*) FROM fragmentos').fetchone()[0], 1)
        # El indice FTS tambien quedo limpio: no debe encontrar la version vieja.
        n = con.execute("SELECT COUNT(*) FROM fragmentos_fts "
                        "WHERE fragmentos_fts MATCH '\"vieja\"'").fetchone()[0]
        self.assertEqual(n, 0)
        con.close()

    def test_documento_sin_fragmentos_se_registra_igual(self):
        con = abrir_db(self.db)
        guardar(con, self._meta(estado='necesita_ocr'), [])
        con.commit()
        fila = con.execute('SELECT estado, fragmentos FROM documentos').fetchone()
        self.assertEqual(fila, ('necesita_ocr', 0))
        con.close()

    def test_limpiar_borra_la_base(self):
        con = abrir_db(self.db)
        guardar(con, self._meta(), [(1, 0, 'algo de texto para el indice')])
        con.commit()
        con.close()
        con = abrir_db(self.db, limpiar=True)
        self.assertEqual(con.execute('SELECT COUNT(*) FROM documentos').fetchone()[0], 0)
        con.close()


class TestExtraccion(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.tmp.cleanup()

    def test_lee_utf8(self):
        p = os.path.join(self.tmp.name, 'a.txt')
        with open(p, 'w', encoding='utf-8') as f:
            f.write('Análisis de señales')
        paginas, n = extraer_texto_plano(p)
        self.assertEqual(n, 1)
        self.assertIn('señales', paginas[0][1])

    def test_cae_a_latin1_si_no_es_utf8(self):
        p = os.path.join(self.tmp.name, 'b.txt')
        with open(p, 'wb') as f:
            f.write('Mecánica'.encode('cp1252'))
        paginas, _ = extraer_texto_plano(p)
        self.assertIn('Mec', paginas[0][1])


if __name__ == '__main__':
    unittest.main(verbosity=2)
