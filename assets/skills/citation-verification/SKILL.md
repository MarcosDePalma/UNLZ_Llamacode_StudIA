---
name: citation-verification
description: Verifica que referencias, DOI y citas respalden exactamente las afirmaciones asociadas.
version: 1.0.0
author: UNLZ LlamaCode
---

# Verificación de citas

Para cada afirmación citada:

1. Localizá la fuente primaria y verificá autores, título, año, revista y DOI.
2. Abrí el pasaje relevante; no aceptes como evidencia sólo título o abstract.
3. Clasificá el respaldo como directo, parcial, contradictorio o no verificable.
4. Detectá referencias retractadas, corregidas o duplicadas.
5. Señalá citas secundarias presentadas incorrectamente como primarias.

Devolvé una tabla afirmación → fuente → pasaje/ubicación → veredicto → corrección.
Nunca completes metadatos bibliográficos por intuición.
