# Qwen3.8-27B — HumanEval por etapas

Fecha: 2026-08-14. Hardware: 2× RTX 3090 (48 GB VRAM), 128 GB RAM.
Binario: `llama.cpp b10331` CUDA 12.4. Todos los perfiles usaron MTP y
`mmproj-BF16.gguf`, con la plantilla `qwen38-tools-fixed.jinja` y sampling
conservador (`temp 0.60`, `top-p 0.95`, `top-k 20`).

## Protocolo ejecutado

1. HumanEval/0: 30 perfiles, exactamente 10 por GGUF.
2. Selección: resultados válidos 1/1; desempate por TPS del ítem.
3. HumanEval/20: los 3 ganadores de cada GGUF, 9 corridas en total.

La medición se ejecutó con `target=model`. El target `agent` no se incluyó:
el backend de agente no inició de forma determinista durante esta tanda,
mientras que el target de modelo sí ejecutó y corrigió el código de HumanEval
con el mismo grader nativo. Las corridas inválidas por la primera versión de la
plantilla no entran en el ranking.

## HumanEval/0

30/30 perfiles terminaron válidamente con `1/1`. Los tres primeros de cada
GGUF fueron:

| GGUF | Perfil | TPS HumanEval/0 |
|---|---|---:|
| UD-Q4_K_XL | `sys-bench-qwen38-udq4-mtp2-64k` | 85.53 |
| UD-Q4_K_XL | `sys-bench-qwen38-udq4-mtp4` | 85.51 |
| UD-Q4_K_XL | `sys-qwen38-27b-udq4-131k` | 84.93 |
| Q4_K_M | `sys-bench-qwen38-q4km-mtp3-64k-kv8` | 80.06 |
| Q4_K_M | `sys-bench-qwen38-q4km-mtp3-b2048` | 79.42 |
| Q4_K_M | `sys-bench-qwen38-q4km-mtp3-kv8` | 79.35 |
| Q5_K_M | `sys-bench-qwen38-q5km-mtp3-64k` | 78.15 |
| Q5_K_M | `sys-bench-qwen38-q5km-mtp4` | 77.70 |
| Q5_K_M | `sys-bench-qwen38-q5km-mtp3-b1024` | 77.37 |

## HumanEval/20 — finalistas

| GGUF | Perfil | Score | Tiempo total |
|---|---|---:|---:|
| UD-Q4_K_XL | `sys-bench-qwen38-udq4-mtp2-64k` | 20/20 | 92.2 s |
| UD-Q4_K_XL | `sys-bench-qwen38-udq4-mtp4` | 20/20 | 92.1 s |
| UD-Q4_K_XL | `sys-qwen38-27b-udq4-131k` | 20/20 | 90.8 s |
| Q4_K_M | `sys-bench-qwen38-q4km-mtp3-64k-kv8` | 20/20 | 101.9 s |
| Q4_K_M | `sys-bench-qwen38-q4km-mtp3-b2048` | 20/20 | 100.2 s |
| Q4_K_M | `sys-bench-qwen38-q4km-mtp3-kv8` | 20/20 | 106.6 s |
| Q5_K_M | `sys-bench-qwen38-q5km-mtp3-64k` | 20/20 | 105.8 s |
| Q5_K_M | `sys-bench-qwen38-q5km-mtp4` | 20/20 | 106.9 s |
| Q5_K_M | `sys-bench-qwen38-q5km-mtp3-b1024` | 20/20 | 107.0 s |

## Lectura

En esta tanda no hubo separación de calidad: los nueve finalistas hicieron
20/20. La diferencia operativa queda en latencia: UD-Q4 fue el grupo más rápido,
Q4_K_M quedó en el medio y Q5_K_M fue el más lento. Esto no demuestra que los
quants sean equivalentes fuera de estos 20 ítems; sí confirma que, con la
plantilla, MTP y mmproj actuales, ningún finalista perdió capacidad en
HumanEval/20.

El smoke de throughput previo contra ThinkingCap/MAX-Q queda documentado en
`docs/qwen38-benchmark-20260814.md`: ThinkingCap Q4_K_M MTP4 alcanzó 54.3–72.9
tok/s, mientras que Qwen3.8 alcanzó hasta 49.6 tok/s en esa configuración de
medición. Son métricas distintas de HumanEval y no deben mezclarse.
