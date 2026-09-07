# Qwen3.8-27B · benchmark local · 2026-08-14

Medición manual reproducible en 2× RTX 3090 (48 GB VRAM agregados), con llama.cpp
b10331 CUDA 12.4, `ctx 131072`, KV `q4_0`, `batch 512`, `ubatch 64`, `MTP`,
`mmproj-BF16`, `temp 0.60`, `top-p 0.95`, `top-k 20`, `min-p 0.0` y tres prompts
de 256 tokens. Se descartó el primer turno frío al interpretar el throughput.

## Archivos verificados

| Variante | GGUF | mmproj |
|---|---:|---:|
| UD-Q4_K_XL | 17,923,394,624 bytes | 931,146,432 bytes |
| Q4_K_M | 17,106,775,008 bytes | 931,146,432 bytes |
| Q5_K_M | 19,834,055,648 bytes | 931,146,432 bytes |

Fuente: `unsloth/Qwen3.8-27B-GGUF`. Los pesos viven en `D:\Models\llamacpp` y
no se versionan.

## Resultados

| Perfil | MTP | Throughput observado | Aceptación MTP observada |
|---|---:|---:|---:|
| Qwen3.8 UD-Q4 + visión | 3 | 34,7–40,4 tok/s | 44–57% |
| Qwen3.8 Q4_K_M + visión | 3 | 31,7–39,5 tok/s | 43–64% |
| Qwen3.8 Q5_K_M + visión | 3 | 43,8–49,6 tok/s | 38–51% |
| ThinkingCap Q4_K_M + visión | 4 | 54,3–72,9 tok/s | 49–75% |

El primer request frío de UD-Q4 tardó 7,3 s y el de Q4_K_M 64,4 s por el prefill
inicial/reutilización de graphs; no se mezcló con el rango caliente. Q5 fue el
mejor Qwen3.8 en esta corrida, pero ThinkingCap siguió siendo más rápido.

La carga de los tres Qwen3.8 confirmó `loaded multimodal model` y
`draft acceptance`, por lo que MTP y MMPROJ están activos. Para tareas de
grounding visual, b10331 recomienda `--image-min-tokens 1024`; no se incluyó en
la prueba textual.

La comparación de calidad no se rankeó con estos tres prompts: son smoke tests de
throughput, no sustituyen Agent efficiency E2E/BigCodeBench. La siguiente corrida
recomendada es ejecutar la suite larga con thinking off/on por separado.
