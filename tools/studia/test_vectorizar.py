#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Tests del armado de lotes de vectorizar.py.

Lo que se cubre es la parte que no toca la red: como se agrupan los fragmentos
en requests y, sobre todo, que la reduccion del tamano de lote se APLIQUE.

Ese fue un error real: los lotes se precalculaban todos de entrada y, cuando el
servidor rechazaba uno, se rearmaba la lista mientras el `for` la recorria. En
Python eso no tiene efecto -- el iterador sigue sobre la lista vieja -- asi que
el tamano nunca bajaba: todos los lotes fallaban, cada uno se reintentaba y
recien despues caia al modo de a uno. La vectorizacion andaba a 2 fragmentos por
segundo en vez de 38.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from vectorizar import siguiente_lote, tokens_estimados  # noqa: E402


def frags(cantidad, chars=1200):
    """Fragmentos como los del indice real: ~1200 caracteres, ~342 tokens."""
    return [(i, "x" * chars) for i in range(cantidad)]


class TestSiguienteLote(unittest.TestCase):

    def test_respeta_el_tope_de_fragmentos(self):
        lote = siguiente_lote(frags(100), 0, presupuesto=10 ** 6, tope=4)
        self.assertEqual(len(lote), 4)

    def test_respeta_el_presupuesto_de_tokens(self):
        # 342 tokens por fragmento: en 800 entran dos, el tercero no.
        lote = siguiente_lote(frags(100), 0, presupuesto=800, tope=32)
        self.assertEqual(len(lote), 2)

    def test_arranca_donde_se_le_pide(self):
        pendientes = [(i, "x" * 100) for i in range(10)]
        lote = siguiente_lote(pendientes, 6, presupuesto=10 ** 6, tope=3)
        self.assertEqual([i for i, _ in lote], [6, 7, 8])

    def test_un_fragmento_gigante_va_solo(self):
        # Aunque no entre en el presupuesto: partirlo romperia el fragmento.
        pendientes = [(0, "x" * 100000), (1, "y" * 100)]
        lote = siguiente_lote(pendientes, 0, presupuesto=500, tope=32)
        self.assertEqual(len(lote), 1)
        self.assertEqual(lote[0][0], 0)

    def test_nunca_devuelve_vacio_si_quedan_pendientes(self):
        # Si devolviera [] el bucle de main() no avanzaria nunca.
        for presupuesto in (1, 10, 500):
            lote = siguiente_lote(frags(5), 0, presupuesto=presupuesto, tope=1)
            self.assertTrue(lote, "presupuesto %d dio lote vacio" % presupuesto)

    def test_el_tope_mas_chico_da_lotes_de_a_uno(self):
        lote = siguiente_lote(frags(10), 0, presupuesto=10 ** 6, tope=1)
        self.assertEqual(len(lote), 1)


class ListaVigilada(list):
    """Una lista que anota si alguien le pide una rebanada."""

    def __init__(self, *a):
        super().__init__(*a)
        self.rebanadas = 0

    def __getitem__(self, k):
        if isinstance(k, slice):
            self.rebanadas += 1
        return super().__getitem__(k)


class TestSinCopias(unittest.TestCase):
    """`siguiente_lote` no debe rebanar la lista de pendientes.

    Otro error real: se recorria con `pendientes[desde:]`, que copia todo lo que
    queda en CADA lote. El costo total se vuelve cuadratico. Con una materia no
    se nota; con el corpus entero armar los lotes tardaba mas que calcular los
    embeddings -- 3 fragmentos por segundo en vez de 52.

    Se comprueba contando rebanadas y no midiendo tiempo: asi el test es
    deterministico y no depende de que tan cargada este la maquina."""

    def test_no_rebana_la_lista(self):
        pendientes = ListaVigilada(frags(500))
        i = 0
        while i < len(pendientes):
            lote = siguiente_lote(pendientes, i, 10 ** 6, 8)
            i += len(lote)
        self.assertEqual(pendientes.rebanadas, 0,
                         "copia la lista restante: el costo es cuadratico")


class TestDegradacion(unittest.TestCase):
    """El lazo de main(): al rechazar un lote se baja el tope y se rearma DESDE
    EL MISMO indice. Se reproduce aca con un servidor simulado."""

    def recorrer(self, pendientes, acepta_hasta, tope_inicial=4):
        """Simula la corrida. Devuelve (ids vectorizados, tamanos usados)."""
        tope = tope_inicial
        hechos, tamanos = [], []
        i = 0
        vueltas = 0
        while i < len(pendientes):
            vueltas += 1
            self.assertLess(vueltas, 1000, "el lazo no termina")
            lote = siguiente_lote(pendientes, i, 10 ** 6, tope)
            if len(lote) > acepta_hasta and len(lote) > 1:
                tope = max(1, len(lote) // 2)      # el servidor lo rechaza
                continue
            tamanos.append(len(lote))
            hechos += [x for x, _ in lote]
            i += len(lote)
        return hechos, tamanos

    def test_baja_el_tope_y_no_pierde_fragmentos(self):
        pendientes = frags(10)
        hechos, tamanos = self.recorrer(pendientes, acepta_hasta=2)
        self.assertEqual(hechos, list(range(10)))   # estan todos, en orden
        self.assertEqual(max(tamanos), 2)           # se ajusto al limite real

    def test_el_tope_reducido_se_mantiene(self):
        # El nucleo del error viejo: bajarlo una vez y que siga bajo.
        _, tamanos = self.recorrer(frags(20), acepta_hasta=1)
        self.assertEqual(set(tamanos), {1})

    def test_sin_rechazos_usa_el_tope_completo(self):
        _, tamanos = self.recorrer(frags(12), acepta_hasta=99)
        self.assertEqual(tamanos, [4, 4, 4])

    def test_el_ultimo_lote_puede_ser_parcial(self):
        _, tamanos = self.recorrer(frags(9), acepta_hasta=99)
        self.assertEqual(tamanos, [4, 4, 1])


class TestTokensEstimados(unittest.TestCase):

    def test_crece_con_el_texto(self):
        self.assertLess(tokens_estimados("hola"), tokens_estimados("hola " * 100))

    def test_nunca_es_cero(self):
        # Un fragmento vacio con estimacion 0 haria un lote infinito.
        self.assertGreaterEqual(tokens_estimados(""), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
