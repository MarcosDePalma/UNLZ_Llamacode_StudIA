---
name: reproducible-data-analysis
description: Analiza datasets con trazabilidad, controles de calidad y artefactos reproducibles sin modificar los datos fuente.
version: 1.0.0
author: UNLZ LlamaCode
---

# Análisis reproducible de datos

Tratà los datos originales como sólo lectura. Registrá origen, hash, esquema,
unidades, valores faltantes, duplicados y anomalías. Generá transformaciones en
scripts deterministas y guardá tablas derivadas separadas.

Antes del análisis inferencial:

- describí la población y el proceso de muestreo;
- visualizá distribuciones y calidad;
- declarà decisiones de limpieza;
- comprobá supuestos de cada método;
- reportá tamaños de efecto e incertidumbre, no sólo p-valores.

Entregá código, parámetros, semillas, versiones, resultados y un README que permita
repetir el análisis desde los datos fuente.
