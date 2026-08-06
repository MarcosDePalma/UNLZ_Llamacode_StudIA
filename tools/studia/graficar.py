#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Genera el PNG de un gráfico de funciones para StudIA.

    python graficar.py --spec spec.txt --salida grafico.png

El "spec" es un formato declarativo mínimo, pensado para que un modelo de 7B lo
emita bien sin equivocarse. NO se ejecuta código Python arbitrario del modelo:
sólo se evalúa la expresión matemática dentro de un espacio de nombres cerrado
(sin builtins, sólo funciones de numpy). Ver `_espacio_seguro`.

Formato:

    funcion: sin(x)/x           # una o varias líneas 'funcion:'
    funcion: cos(x)
    rango: -10, 10              # dominio en x (default -10, 10)
    area: 1, 2                  # opcional: sombrea bajo la curva (integrales)
    titulo: Función sinc
    xlabel: x
    ylabel: f(x)

Salidas: 0 si escribió el PNG; !=0 con el motivo por stdout.
"""

import argparse
import os
import sys

# Backend sin ventana: esto corre como sidecar, no hay display.
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


# Paleta alineada con el tema de la app (Catppuccin Mocha) para que el PNG no
# desentone con el chat.
COLOR_FONDO   = "#1e1e2e"
COLOR_EJES    = "#45475a"
COLOR_TEXTO   = "#cdd6f4"
COLORES_CURVA = ["#89b4fa", "#a6e3a1", "#f9e2af", "#f38ba8", "#cba6f7", "#94e2d5"]

MAX_FUNCIONES = 6
PUNTOS = 800


def _espacio_seguro():
    """Namespace para evaluar la expresión: sólo matemática, sin builtins.

    Es lo que evita que un `funcion:` malicioso o alucinado haga algo distinto
    de calcular números. No hay open, import, exec ni acceso a atributos útiles.
    """
    permitido = [
        "sin", "cos", "tan", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh",
        "exp", "log", "log10", "log2", "sqrt", "abs", "sign", "floor", "ceil",
        "power", "maximum", "minimum", "where", "pi", "e",
    ]
    ns = {n: getattr(np, n) for n in permitido if hasattr(np, n)}
    ns["ln"] = np.log
    ns["__builtins__"] = {}
    return ns


def parsear(texto):
    """spec de texto → dict. Tolera espacios, mayúsculas y líneas vacías."""
    datos = {"funcion": [], "rango": (-10.0, 10.0), "area": None,
             "titulo": "", "xlabel": "x", "ylabel": ""}
    for linea in texto.splitlines():
        linea = linea.strip()
        if not linea or linea.startswith("#") or ":" not in linea:
            continue
        clave, valor = linea.split(":", 1)
        clave = clave.strip().lower()
        valor = valor.strip()
        if not valor:
            continue
        if clave in ("funcion", "función", "f", "y"):
            # Acepta "y = x**2" y "f(x) = x**2": se queda con el lado derecho.
            if "=" in valor:
                valor = valor.split("=", 1)[1].strip()
            if len(datos["funcion"]) < MAX_FUNCIONES:
                datos["funcion"].append(valor)
        elif clave in ("rango", "dominio", "x"):
            par = _dos_numeros(valor)
            if par:
                datos["rango"] = par
        elif clave in ("area", "área", "integral"):
            datos["area"] = _dos_numeros(valor)
        elif clave in ("titulo", "título"):
            datos["titulo"] = valor
        elif clave == "xlabel":
            datos["xlabel"] = valor
        elif clave == "ylabel":
            datos["ylabel"] = valor
    return datos


def _dos_numeros(valor):
    partes = [p for p in valor.replace(";", ",").replace("..", ",").split(",") if p.strip()]
    if len(partes) < 2:
        return None
    try:
        a, b = float(partes[0]), float(partes[1])
    except ValueError:
        return None
    if a == b:
        return None
    return (min(a, b), max(a, b))


def evaluar(expr, x):
    ns = _espacio_seguro()
    ns["x"] = x
    with np.errstate(all="ignore"):          # /0 y log(-1) dan nan, no excepción
        y = eval(expr, ns)                   # noqa: S307 — namespace cerrado
    y = np.asarray(y, dtype=float)
    if y.shape == ():                        # constante: se estira al dominio
        y = np.full_like(x, float(y))
    # Cortar las asíntotas para que no aplaste la escala.
    finitos = y[np.isfinite(y)]
    if finitos.size:
        lim = np.percentile(np.abs(finitos), 99) * 5 + 1e-9
        y = np.where(np.abs(y) > lim, np.nan, y)
    return y


def graficar(datos, salida):
    if not datos["funcion"]:
        print("ERROR: el gráfico no declara ninguna 'funcion:'")
        return 2

    x0, x1 = datos["rango"]
    x = np.linspace(x0, x1, PUNTOS)

    fig, ax = plt.subplots(figsize=(7.2, 4.2), dpi=150)
    fig.patch.set_facecolor(COLOR_FONDO)
    ax.set_facecolor(COLOR_FONDO)

    dibujadas = 0
    for i, expr in enumerate(datos["funcion"]):
        try:
            y = evaluar(expr, x)
        except Exception as e:
            print("ERROR: no se pudo evaluar «%s» (%s)" % (expr, type(e).__name__))
            plt.close(fig)
            return 3
        color = COLORES_CURVA[i % len(COLORES_CURVA)]
        ax.plot(x, y, color=color, linewidth=2, label=expr)
        dibujadas += 1

        # Área sombreada bajo la PRIMERA curva: el caso de las integrales.
        if i == 0 and datos["area"]:
            a, b = datos["area"]
            xa = np.linspace(max(a, x0), min(b, x1), PUNTOS)
            if xa.size > 1:
                try:
                    ax.fill_between(xa, 0, evaluar(expr, xa), color=color, alpha=0.28)
                except Exception:
                    pass                      # el área es un extra, no aborta

    ax.axhline(0, color=COLOR_EJES, linewidth=1)
    ax.axvline(0, color=COLOR_EJES, linewidth=1)
    ax.grid(True, color=COLOR_EJES, alpha=0.35, linewidth=0.6)
    for lado in ax.spines.values():
        lado.set_color(COLOR_EJES)
    ax.tick_params(colors=COLOR_TEXTO, labelsize=8)
    ax.set_xlabel(datos["xlabel"], color=COLOR_TEXTO, fontsize=9)
    if datos["ylabel"]:
        ax.set_ylabel(datos["ylabel"], color=COLOR_TEXTO, fontsize=9)
    if datos["titulo"]:
        ax.set_title(datos["titulo"], color=COLOR_TEXTO, fontsize=11, pad=10)
    if dibujadas > 1 or datos["area"]:
        leg = ax.legend(fontsize=8, facecolor=COLOR_FONDO, edgecolor=COLOR_EJES)
        for t in leg.get_texts():
            t.set_color(COLOR_TEXTO)

    os.makedirs(os.path.dirname(os.path.abspath(salida)), exist_ok=True)
    fig.tight_layout()
    fig.savefig(salida, facecolor=COLOR_FONDO)
    plt.close(fig)
    print("OK %s" % salida)
    return 0


def main():
    ap = argparse.ArgumentParser(description="Gráfico de funciones para StudIA")
    ap.add_argument("--spec", required=True, help="archivo con el spec del gráfico")
    ap.add_argument("--salida", required=True, help="PNG a generar")
    args = ap.parse_args()

    if not os.path.isfile(args.spec):
        print("ERROR: no existe el spec %s" % args.spec)
        return 2
    # utf-8-sig: descarta el BOM si el que escribió el spec lo puso (Windows lo
    # agrega con frecuencia). Sin esto la primera clave del archivo no matchea.
    with open(args.spec, "r", encoding="utf-8-sig") as f:
        return graficar(parsear(f.read()), args.salida)


if __name__ == "__main__":
    sys.exit(main())
