#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Tests del graficador de StudIA.

Lo que se cubre es la traduccion de notacion: el modelo escribe matematica como
se escribe a mano y el evaluador espera Python.

El caso que motivo esto: `x^2` no daba un error de potencia sino un TypeError
incomprensible, porque en Python `^` es XOR de bits y sobre un array de
decimales explota con "ufunc 'bitwise_xor' not supported". En el chat aparecia
"No se pudo graficar: ERROR: ... TypeError" y no habia forma de entender que
faltaba.
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    import numpy as np
    from graficar import _espacio_seguro, evaluar, normalizar_expresion, parsear
    HAY_DEPS = True
except ImportError:
    HAY_DEPS = False


@unittest.skipUnless(HAY_DEPS, "falta numpy/matplotlib")
class TestNormalizar(unittest.TestCase):

    def test_potencia_con_circunflejo(self):
        # El caso que rompia: en Python ^ es XOR de bits.
        self.assertEqual(normalizar_expresion("x^2"), "x**2")
        self.assertEqual(normalizar_expresion("e^x"), "e**x")

    def test_superindices_unicode(self):
        # Al resto de la app se le pide notacion Unicode, asi que el modelo la
        # usa tambien en los graficos.
        self.assertEqual(normalizar_expresion("x²"), "x**2")
        self.assertEqual(normalizar_expresion("x¹⁰"), "x**10")
        self.assertEqual(normalizar_expresion("x⁻¹"), "x**-1")

    def test_multiplicacion_implicita(self):
        self.assertEqual(normalizar_expresion("2x"), "2*x")
        self.assertEqual(normalizar_expresion("3sin(x)"), "3*sin(x)")
        self.assertEqual(normalizar_expresion("2(x+1)"), "2*(x+1)")
        self.assertEqual(normalizar_expresion("(x+1)(x-2)"), "(x+1)*(x-2)")
        self.assertEqual(normalizar_expresion("sin(x)cos(x)"), "sin(x)*cos(x)")

    def test_no_rompe_la_notacion_cientifica(self):
        # En 3e-5 la `e` no es una variable: meterle un * lo destruye.
        self.assertEqual(normalizar_expresion("3e-5*x"), "3e-5*x")
        self.assertEqual(normalizar_expresion("1.5E+3"), "1.5E+3")

    def test_simbolos_matematicos(self):
        self.assertEqual(normalizar_expresion("x − 1"), "x - 1")   # U+2212
        self.assertEqual(normalizar_expresion("2·x"), "2*x")
        self.assertEqual(normalizar_expresion("2×x"), "2*x")
        self.assertEqual(normalizar_expresion("π*x"), "pi*x")
        self.assertEqual(normalizar_expresion("√(x)"), "sqrt(x)")

    def test_barras_de_valor_absoluto(self):
        self.assertEqual(normalizar_expresion("|x|"), "abs(x)")
        self.assertEqual(normalizar_expresion("|x-1|+2"), "abs(x-1)+2")

    def test_barra_impar_se_deja_igual(self):
        # Mejor dejarlo y que falle con su motivo que inventar un cierre.
        self.assertEqual(normalizar_expresion("|x"), "|x")

    def test_lo_correcto_no_se_toca(self):
        for e in ("x**2", "sin(x)/x", "exp(-x**2)", "log(x)", "sqrt(x+1)"):
            self.assertEqual(normalizar_expresion(e), e)


@unittest.skipUnless(HAY_DEPS, "falta numpy/matplotlib")
class TestEvaluar(unittest.TestCase):
    """Lo que importa de verdad: que la expresion se pueda calcular."""

    def setUp(self):
        self.x = np.linspace(1.0, 5.0, 20)

    def _ok(self, expr):
        y = evaluar(expr, self.x)
        self.assertEqual(y.shape, self.x.shape)
        self.assertTrue(np.isfinite(y).any(), "todo NaN en %s" % expr)
        return y

    def test_las_formas_que_escribe_el_modelo(self):
        for expr in ("x^2", "x²", "2x", "3sin(x)", "e^x", "√(x)", "π*x",
                     "|x-3|", "(x+1)(x-1)", "sin(x)cos(x)", "x − 1", "2·x"):
            with self.subTest(expr=expr):
                self._ok(expr)

    def test_potencia_da_el_valor_correcto(self):
        # No alcanza con que no falle: tiene que dar la potencia, no otra cosa.
        np.testing.assert_allclose(evaluar("x^2", self.x), self.x ** 2)
        np.testing.assert_allclose(evaluar("x²", self.x), self.x ** 2)
        np.testing.assert_allclose(evaluar("2x", self.x), 2 * self.x)

    def test_log_con_base(self):
        np.testing.assert_allclose(evaluar("log(x, 10)", self.x),
                                   np.log10(self.x))
        np.testing.assert_allclose(evaluar("log(x)", self.x), np.log(self.x))

    def test_max_y_min(self):
        np.testing.assert_allclose(evaluar("max(x, 3)", self.x),
                                   np.maximum(self.x, 3))

    def test_una_constante_se_estira_al_dominio(self):
        y = evaluar("5", self.x)
        self.assertEqual(y.shape, self.x.shape)
        self.assertTrue(np.all(y == 5))

    def test_sigue_sin_poder_ejecutar_codigo(self):
        # La traduccion no puede haber abierto una puerta: el namespace sigue
        # cerrado y sin builtins.
        for maligna in ("__import__('os').system('echo hola')",
                        "open('x','w')",
                        "eval('1+1')"):
            with self.subTest(expr=maligna):
                with self.assertRaises(Exception):
                    evaluar(maligna, self.x)

    def test_el_espacio_no_tiene_builtins(self):
        self.assertEqual(_espacio_seguro()["__builtins__"], {})


@unittest.skipUnless(HAY_DEPS, "falta numpy/matplotlib")
class TestVariasCurvas(unittest.TestCase):
    """El otro error real: "funcion: 100 - 2*x, 20 + 3*x".

    Asi escribe el modelo la oferta y la demanda de un punto de equilibrio. Si
    las dos expresiones no se separan, eval() devuelve una TUPLA de arrays y
    matplotlib falla con "x and y must have same first dimension, but have
    shapes (800,) and (2,800)".
    """

    def test_la_coma_de_nivel_cero_separa_curvas(self):
        d = parsear("funcion: 100 - 2*x, 20 + 3*x")
        self.assertEqual(d["funcion"], ["100 - 2*x", "20 + 3*x"])

    def test_la_coma_dentro_de_parentesis_no_separa(self):
        # log(x, 10) es UNA curva: esa coma es del logaritmo.
        self.assertEqual(parsear("funcion: log(x, 10)")["funcion"],
                         ["log(x, 10)"])
        self.assertEqual(parsear("funcion: max(x, 0), min(x, 5)")["funcion"],
                         ["max(x, 0)", "min(x, 5)"])

    def test_cada_curva_evalua_a_una_dimension(self):
        x = np.linspace(0.0, 40.0, 800)
        for expr in parsear("funcion: 100 - 2*x, 20 + 3*x")["funcion"]:
            self.assertEqual(evaluar(expr, x).shape, x.shape)

    def test_una_expresion_multiple_avisa_con_su_motivo(self):
        # Red de seguridad por si llega igual: mejor un mensaje entendible que
        # un error de dimensiones de matplotlib.
        x = np.linspace(0.0, 5.0, 10)
        with self.assertRaises(ValueError) as ctx:
            evaluar("where(x > 0, [x, x], [x, x])", x)
        self.assertIn("curva", str(ctx.exception))

    def test_respeta_el_tope_de_funciones(self):
        d = parsear("funcion: x, x, x, x, x, x, x, x, x, x")
        self.assertLessEqual(len(d["funcion"]), 6)


@unittest.skipUnless(HAY_DEPS, "falta numpy/matplotlib")
class TestParsear(unittest.TestCase):

    def test_spec_minimo(self):
        d = parsear("funcion: x^2\nrango: -2, 2\ntitulo: Parábola")
        self.assertEqual(d["funcion"], ["x^2"])
        self.assertEqual(d["rango"], (-2.0, 2.0))
        self.assertEqual(d["titulo"], "Parábola")

    def test_se_queda_con_el_lado_derecho_del_igual(self):
        self.assertEqual(parsear("funcion: y = x + 1")["funcion"], ["x + 1"])
        self.assertEqual(parsear("y: f(x) = x + 1")["funcion"], ["x + 1"])

    def test_una_linea_sin_clave_se_ignora(self):
        # El spec es declarativo: sin "funcion:" no hay nada que graficar.
        self.assertEqual(parsear("y = x + 1")["funcion"], [])

    def test_varias_funciones(self):
        d = parsear("funcion: sin(x)\nfuncion: cos(x)")
        self.assertEqual(len(d["funcion"]), 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
