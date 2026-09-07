# Qwen3.6 vs Qwen3.8 · barrido MTP local · 2026-08-14

Comparación controlada en 2× RTX 3090 (48 GB agregados), llama.cpp b10331 CUDA
12.4, GGUF Q4_K_M, contexto 65k, KV q8, MMPROJ activo, `temp 0.6`, `top-p
0.95`, `top-k 20`, `min-p 0.0`. Se probaron MTP 2–8, dos pasadas y seis
prompts deterministas por punto. El warm-up quedó fuera de throughput y TTFT.

Esto no replica literalmente el post: el post usa FP8 en RTX PRO 6000; acá la
comparación es Q4_K_M contra Q4_K_M en hardware dual 3090.

## Resultados

| MTP | Qwen3.8 tok/s | Qwen3.8 TTFT ms | Qwen3.8 aceptación | Qwen3.6 tok/s | Qwen3.6 TTFT ms | Qwen3.6 aceptación |
|---:|---:|---:|---:|---:|---:|---:|
| 2 | 47,00 | 201,8 | 87,2% | 45,15 | 206,9 | 89,1% |
| 3 | 48,44 | 188,1 | 77,0% | 43,74 | 207,0 | 85,7% |
| 4 | **50,71** | 189,6 | 74,2% | 47,28 | 209,7 | 82,3% |
| 5 | 47,26 | 192,8 | 67,8% | 47,52 | 215,9 | 76,2% |
| 6 | 40,86 | 200,5 | 62,1% | 41,92 | 215,8 | 69,0% |
| 7 | 35,69 | 203,8 | 57,0% | 44,88 | 193,1 | 66,4% |
| 8 | 57,12 | 204,1 | 52,3% | **68,75** | 193,4 | 61,0% |

Los tokens/s y TTFT son medianas por punto. La aceptación se extrajo de los
`draft acceptance` del log de llama.cpp, excluyendo el warm-up; cada fila tiene
12 muestras. MTP8 debe tomarse con cautela: mejora el throughput observado,
pero cae a la menor aceptación y no representa necesariamente mejor rendimiento
en respuestas con otra longitud.

## Calidad

La primera pasada de exact-match no es válida como quality score comparable:
el arnés exigía que `FINAL:` fuera literalmente el último texto, mientras que
Qwen3.8 devolvió razonamiento/contenido válido fuera de ese formato. Por eso no
se usa ese conteo para declarar degradación. La aceptación MTP es una métrica de
especulación, no una métrica de calidad de salida: por construcción, MTP debe
conservar la salida del modelo objetivo.

## Lectura práctica

- Para Qwen3.8 local, MTP4 fue el mejor punto estable de esta corrida.
- Qwen3.6/ThinkingCap fue más tolerante a MTP alto y alcanzó más throughput en
  MTP8, pero con una ventaja clara de aceptación sobre Qwen3.8.
- No aparece evidencia de que MTP cambie la calidad intrínseca; sí aparece el
  trade-off esperado: al subir `n-max`, la aceptación baja.
- Para comparar calidad de verdad falta repetir la batería con grading por
  contenido (sin exigir posición exacta de `FINAL:`), idealmente con varias
  semillas y prompts de coding/razonamiento similares al post.

Datos crudos: `docs/qwen36-vs-qwen38-mtp-sweep-20260814.json`.
