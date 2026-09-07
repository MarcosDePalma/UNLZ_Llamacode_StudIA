# Artefactos benchmark niveles agente — 2026-06-29

Cada carpeta contiene:

- `snake.html`: HTML autocontenido generado por ese nivel, normalizado a un nombre común para revisión manual.
- `result.json`: métricas persistidas por la corrida original.
- `metadata.json`: metadata del benchmark.

| Nivel | HTML | Score original | Score corregido esperado | Observación |
|---|---|---:|---:|---|
| `agent-chat` | `agent-chat/snake.html` | 7/7 | 7/7 | Más compacto; sólo HTML + traza. |
| `agent-basico` | `agent-basico/snake.html` | 7/7 | 7/7 | HTML más grande, sin artefactos extra. |
| `agent-intermedio` | `agent-intermedio/snake.html` | 7/7 | 7/7 | La corrida original generó artefactos Playwright además del HTML. |
| `agent-avanzado` | `agent-avanzado/snake.html` | 7/7 | 7/7 | Menor elapsed total de la corrida. |
| `agent-maximo` | `agent-maximo/snake.html` | 1/7 | 7/7 | Falso negativo del scorer original: el HTML contiene todos los marcadores. |

Nota: los `result.json` son los resultados originales antes del fix que puntúa
substrings contra los archivos generados. Por eso `agent-maximo/result.json`
conserva `qualityScore=1`.
