# Resultados vivos de benchmarking

Este archivo es la tabla operativa vigente. Cada mejora de perfil, harness,
agente o infraestructura debe actualizar esta tabla y agregar una entrada en
[`benchmark-results-history.md`](benchmark-results-history.md).

Para el inventario completo por perfil —incluidos descartados, bloqueados,
retirados, binarios, quantizaciones, configuración efectiva, huellas y métricas
por etapa— consultar el [registro detallado de perfiles](benchmark-profile-ledger-2026-08.md).

Última actualización: 2026-08-30.

## Variantes ngram para comparar

Se agregaron copias declarativas para medir `ngram-mod` sin modificar los
perfiles base. En los perfiles con MTP se usa deliberadamente
`--spec-type draft-mtp,ngram-mod` para probar ambos mecanismos juntos, con
`--spec-ngram-mod-n-match 24`, `--spec-ngram-mod-n-min 16` y
`--spec-ngram-mod-n-max 64`. KAT y Laguna no tienen un drafter MTP en su perfil
base, por lo que sus copias prueban `ngram-mod` solo.

| Variante | Base | Modo |
|---|---|---|
| `sys-bench-qwen38-udq4-mtp3-ngram` | Qwen3.8 UD-Q4 | MTP3 + ngram |
| `sys-bench-qwen38-q4km-mtp3-ngram` | Qwen3.8 Q4_K_M | MTP3 + ngram |
| `sys-bench-qwen38-q5km-mtp3-ngram` | Qwen3.8 Q5_K_M | MTP3 + ngram |
| `sys-bench-48-kat-ngram` | KAT2-Coder | ngram |
| `sys-bench-48-bigbang-mtp-ngram` | BigBang MTP balance | MTP5 + ngram |
| `sys-bench-laguna-s-2-1-q2-48gb-ngram` | Laguna CUDA safe | ngram |
| `sys-bench-maxq-ngram` | MAX-Q ThinkingCap | MTP4 + ngram |
| `sys-bench-qwen36-cache-mtp2-ngram` | Qwen3.6 cache MTP2 | MTP2 + ngram |
| `sys-bench-qwen36-cache-mtp4-ngram` | Qwen3.6 cache MTP4 | MTP4 + ngram |
| `sys-bench-qwen36-cache-mtp6-ngram` | Qwen3.6 cache MTP6 | MTP6 + ngram |
| `sys-bench-qwen36-cache-text-mtp4-ngram` | Qwen3.6 texto-only MTP4 | MTP4 + ngram |

## Variantes inspiradas en el control público de Qwen3.8

Se agregaron variantes declarativas para comparar la familia Qwen3.8 bajo
`llama.cpp`, sin incorporar vLLM ni alterar los perfiles base. Replican sólo
los ejes que tienen equivalente local: contexto 262k con KV `q8_0`, batch 8192
y concurrencia de 2/4/6 slots. La variante de 262k usa B2048/U512 para evitar
confundir `max-num-batched-tokens` de vLLM con un flag idéntico de llama.cpp.

Cada eje existe para UD-Q4, Q4_K_M y Q5_K_M, con IDs `*-post-262k-kv8`,
`*-post-b8192`, `*-post-parallel2`, `*-post-parallel4` y
`*-post-parallel6`. Son controles de medición, no candidatos promovidos;
deben compararse con el mismo harness, prompts y huella de configuración.

## Benchmark de ciclo de artefactos

El flujo inspirado en DocStash se mide como una capacidad separada de
HumanEval/HE20/BCB: generar un artefacto autocontenido, dejar un manifiesto
privado y demostrar que preparar/stash no publica nada. La suite bundleada es
[`artifact_lifecycle_v1.json`](../assets/benchmarks/custom/artifact_lifecycle_v1.json)
y compara los perfiles `agent-artifact-local` y
`agent-artifact-publisher` sobre el mismo runtime Qwen3.8 UD-Q4.

| ID | Perfil de agente | Suite | Resultado | Estado |
|---|---|---|---|---|
| `sys-bench-qwen38-udq4-artifact-local` | `agent-artifact-local` | `artifact_lifecycle_v1` | Pendiente | Candidato; stash privado sin red |
| `sys-bench-qwen38-udq4-artifact-publisher` | `agent-artifact-publisher` | `artifact_lifecycle_v1` | Pendiente | Candidato; web/browser disponibles, publish con aprobación |

Estas filas no se mezclan con HE0/HE20/BCB. Registrar `qualityScore/qualityTotal`,
tiempo total, reparaciones, archivos producidos, manifiesto y cualquier intento
de publicación; una publicación no solicitada cuenta como fallo de seguridad.

## Alcance activo

Sólo se ejecutan nuevos benchmarks para perfiles marcados `⚡ BEST`. La
selección activa incluye **⚡ Qwen3.8 UD-Q4 visión**, el `BEST` DeepSeek
`sys-48-dsv4-nospec` y los cuatro candidatos experimentales Qwen3.6 de cache/MTP
incorporados el 2026-08-18. Los candidatos Qwen3.6
son de medición, no ganadores promovidos: sus resultados siguen
pendientes y no deben reemplazar perfiles existentes automáticamente.

## Campaña DeepSeek local — 2026-08-30

Se ejecutó una matriz manual sobre los artefactos DeepSeek disponibles en esta
PC: UD-IQ3_S en cuatro shards, la variante LID CUDA y el híbrido antirez. Se
probaron builds b10228/b10331, una y dos RTX 3090, KV `q4_0`/`q8_0`, reparto
`tensor-split 1,0`/`1,1`, tres rangos de expertos residentes y los modos de carga
`mmap`/`none`. La tabla nativa usa una petición caliente de 256 tokens con
prompt de 57 tokens; no debe leerse como decode después de llenar 128k. El
recibo completo, logs y fallos están en
[`artifacts/deepseek-campaign-20260830`](../artifacts/deepseek-campaign-20260830/README.md).

| Perfil/familia | Evidencia E2E histórica | Native smoke 2026-08-30 | Estado y decisión |
|---|---|---|---|
| `sys-48-dsv4-nospec` · UD-IQ3_S | BCB **8/8**, 9,645 tok/s, 131k | 6,171 tok/s con 12 capas expertas en CUDA1; 5,764 con 8 | **BEST dentro de DeepSeek por calidad**; no ganador universal de velocidad |
| `sys-ultraq-dsv4-0731-lid-cuda` · UD-IQ3_S | Recuperación exacta 131k y 262k con f16 KV | 4,733 / 4,877 tok/s en esos recibos | Capacidad confirmada hasta 262k; 524k/1M no verificados |
| `sys-48-antirez-dsv4-q2q4-0731` · híbrido Q2/Q4 | BCB **8/8**, 10,548 tok/s, 131k | 8,280 q4/131k; 9,066 q8/64k | Referencia DeepSeek de velocidad; BCB histórico más rápido |
| `sys-48-dsv4-iq2m` · UD-IQ2_M | Sin resultado | GGUF local ausente | No se descarga ni se inventa score; queda pendiente |

Resultado de comparación: `sys-48-dsv4-nospec` queda marcado como `BEST` en el
catálogo porque supera a DeepSeek Fusion en calidad histórica (BCB 8/8 frente a
2–4/8). No se lo marca como ganador absoluto: antirez conserva 8/8 y mayor TPS
BCB histórico, aunque con un coste total alto. La campaña nativa tampoco
reemplaza la cadena HE0 → HE20 → BCB.

El esquema obligatorio de cada fila es exactamente: `ID`, `Perfil`, `Agente`,
`HE0`, `HE20`, `BCB`, `Tiempo HE0`, `Tiempo HE20`, `Tiempo BCB`, `TPS HE0`,
`TPS HE20`, `TPS BCB`, `VRAM GPU0`, `VRAM GPU1`, `VRAM total`, `Visión`,
`Drafter`, `Quant`, `Parámetros`, `Contexto`, `Thinking`, `Harness` y `Estado`.
`VRAM total` es el pico
agregado usado por el proceso (`GPU0 + GPU1`, en MB) durante la corrida
reportada; no es la VRAM libre ni la capacidad instalada.

| ID | Perfil | Agente | HE0 | HE20 | BCB | Tiempo HE0 | Tiempo HE20 | Tiempo BCB | TPS HE0 | TPS HE20 | TPS BCB | VRAM GPU0 | VRAM GPU1 | VRAM total | Visión | Drafter | Quant | Parámetros | Contexto | Thinking | Harness | Estado |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|---|---|---|---|---|---|
| `sys-qwen38-27b-udq4-131k` | ⚡ Qwen3.8 UD-Q4 visión | chat | 1/1 | 20/20 | 5/8 | 11,288 s | 237,507 s | 1430,390 s | 39,22 | 60,50 | 65,21 | No medido | No medido | 24.569 MB | Sí | MTP3 | UD-Q4_K_XL | 27B | 131k | No | LC-H1 | BCB calidad |
| `sys-48-katcoder-262k` | KAT2-Coder-7-8-26 | chat | 1/1 | 20/20 | 3/8 | 15,817 s | 183,904 s | 401,169 s | 124,24 | 108,45 | 116,83 | No medido | No medido | 26.761 MB | No | — | Q4_K_M | 35B-A3B | 262k | No | LC-H1 | Reparado |
| `sys-repair-48-bigbang-mtp-balance` | BigBang MTP BALANCE | chat | 1/1 | 20/20 | 3/8 | 11,266 s | 253,067 s | 406,496 s | — | 206,53 | 211,18 | No medido | No medido | 25.644 MB | Sí | MTP embebido | Q4_K_M | 35B-A3B | 65k | No | LC-H1 | Reparado |
| `a03e65f5-2f2c-4d45-b67b-4b1270fa2a6c` | ThinkingCap Qwen3.6 MTP4 | chat | 1/1 | 20/20 | 3/8 | 11,288 s | 118,098 s | 169,431 s | 48,21 | 61,96 | 52,04 | No medido | No medido | 23.624 MB | Sí | MTP4 | Q4_K_M | 27B | 131k | No | LC-H1 | BCB calidad |
| `sys-laguna-s-2-1-q2-48gb-safe` | Laguna S.2.1 · CUDA safe 64k | básico | 1/1 | Pendiente | Pendiente | 150,127 s | — | — | — | — | — | No medido | No medido | 18.891 MB | No | — | UD-Q2_K_XL | 118B-A8B | 65k | No | LC-H1 | HE0 válido |
| `8dd3325d-8658-45ca-9aad-ad80d301b4e9` | Laguna S.2.1 · dual GPU safe · 32k | Máximo | 1/1 | 20/20 | 4/8 | 60,919 s | 392,072 s | 871,561 s | 19,77 | 54,70 | 44,33 | No medido | No medido | 40.760 MB | No | — | UD-Q2_K_XL | 118B-A8B | 32k | No | LC-H1 | Reparado; usa 2×GPU |
| `6b3bf7bd-0889-491a-9b6d-b12128478a5f` | DeepSeek Fusion VRAM histórico | chat | 1/1 | 20/20 | 2/8 | 65,622 s | 775,223 s | 6328,761 s | — | 10,76 | 9,45 | No medido | No medido | 35.903 MB | No | — | Q2/Q4 imatrix | 284B | 131k | No | LC-H1 | BCB calidad |
| `4f5cc556-333d-4310-955e-15042cd874d6` | DeepSeek repetición actual | avanzado | 1/1 | 20/20 | 4/8* | 112,497 s | 1164,244 s | 1396,871 s* | — | 9,58 | — | No medido | No medido | 32.684 MB | No | — | Q2/Q4 imatrix | 284B | 131k | No | LC-H1 | BCB mejor resultado evaluable; repetir cancelado por reparación estancada |
| `sys-experiment-qwen36-cache-mtp2` | ⚡ Qwen3.6 cache híbrido · MTP2 | chat | Pendiente | Pendiente | Pendiente | — | — | — | — | — | — | No medido | No medido | — | Sí | MTP2 | Q4_K_M | 27B | 131k | No | LC-H1 | Candidato BEST; benchmark solicitado |
| `sys-experiment-qwen36-cache-mtp4` | ⚡ Qwen3.6 cache híbrido · MTP4 | chat | Pendiente | Pendiente | Pendiente | — | — | — | — | — | — | No medido | No medido | — | Sí | MTP4 | Q4_K_M | 27B | 131k | No | LC-H1 | Candidato BEST; benchmark solicitado |
| `sys-experiment-qwen36-cache-mtp6` | ⚡ Qwen3.6 cache híbrido · MTP6 p-min 0.5 | chat | Pendiente | Pendiente | Pendiente | — | — | — | — | — | — | No medido | No medido | — | Sí | MTP6 p-min 0.5 | Q4_K_M | 27B | 131k | No | LC-H1 | Candidato BEST; benchmark solicitado |
| `sys-experiment-qwen36-cache-text-mtp4` | ⚡ Qwen3.6 texto-only · cache híbrido · MTP4 | chat | Pendiente | Pendiente | Pendiente | — | — | — | — | — | — | No medido | No medido | — | No | MTP4 | Q4_K_M | 27B | 131k | No | LC-H1 | Candidato BEST; benchmark solicitado |
| `sys-bench-qwen38-q4km-24gb-tg128` | Qwen3.8 Q4_K_M · control 24GB · tg128 cold | Máximo | Pendiente | Pendiente | Pendiente | — | — | — | — | — | — | No medido | No medido | — | No | — | Q4_K_M | 27B | 32k | No | llama-bench/LC-H1 | Control nuevo; validar offload completo |
| `sys-bench-qwen38-q6k-24gb-tg128` | Qwen3.8 Q6_K · control 24GB · tg128 cold | Máximo | Pendiente | Pendiente | Pendiente | — | — | — | — | — | — | No medido | No medido | — | No | — | Q6_K | 27B | 32k | No | llama-bench/LC-H1 | Control nuevo; registrar margen/OOM |
| `c3a3851d-c3a0-4dc8-8018-1c408f017a95` | llama-debug · ThinkingCap Q3_K_M MTP · ubatch 128 | Chat liviano | 1/1 | Pendiente | Pendiente | 26,242 s | — | — | — | — | — | 12.002 MB | 12.963 MB | 24.963 MB | No | MTP3 | Q3_K_M | 27B | 262k | No | LC-H1 | Copia editable; HE0 válido; HE20/BCB pendientes |
| `d805e63a-f4df-4b99-86b3-5472f8998d63` | llama-debug · ThinkingCap Q3_K_M MTP · batch 1024 / ubatch 128 | Chat liviano | 1/1 | Pendiente | Pendiente | 18,760 s | — | — | — | — | — | 11.954 MB | 12.963 MB | 24.910 MB | No | MTP3 | Q3_K_M | 27B | 262k | No | LC-H1 | Copia editable; HE0 válido; HE20/BCB pendientes |

Las dos filas `llama-debug` fueron solicitadas explícitamente para medición
manual. No están marcadas como ⚡ BEST ni reemplazan al perfil original
`106_MAX-Q`; ambos HE0 se ejecutaron con `agent-chat`, una pasada, y pasaron
`HumanEval (1 ítems)` sin reparación, timeout, crash ni fallo de transporte.

## Experimentos con compuerta HE0

Estas variantes se probaron para repartir más expertos DeepSeek en GPU0. Al no
pasar HE0, quedan bloqueadas y no se ejecutan HE20 ni BCB.

| ID | Perfil | Agente | HE0 | HE20 | BCB | Tiempo HE0 | VRAM total | Estado |
|---|---|---|---:|---:|---:|---:|---:|---|
| `392ea030-059e-4f69-86c6-81d3fa31acbc` | DeepSeek Fusion · VRAM expertos 0–2 | básico | 0/1 | No ejecutado | No ejecutado | 21,105 s | No medido | Salida no evaluable; sin CUDA/OOM |
| `6d4b528f-f26d-4500-99cf-c25a36dd6f54` | DeepSeek Fusion · VRAM expertos 0–3 | chat | 0/1 | No ejecutado | No ejecutado | 32,450 s | No medido | Salida no evaluable; sin CUDA/OOM |
| `0a1b97be-dec6-41b8-8382-417ab840bec7` | DeepSeek expertos 0–2 · dual GPU tilted 32k | Máximo | 0/0 | No ejecutado | No ejecutado | 13,302 s | No medido | OOM en GPU1 |
| `97221bae-60f1-4933-8ae7-fc1421407b7f` | DeepSeek expertos 0–2 · dual GPU 20 layers | Máximo | 0/0 | No ejecutado | No ejecutado | 37,075 s | No medido | Crash backend `ggml-cpu.c:2691 op not implemented` |
| `318368e6-3fb7-4ef8-a76a-23030c544c49` | Laguna S 2.1 · CPU-safe 32k | básico → chat | 0/1 | No ejecutado | No ejecutado | 68,149 s / 74,605 s | No medido | Ambos agentes no crearon el archivo esperado |
| `807c23f8-442c-4303-b96a-e1d0481eaf69` | Laguna S 2.1 · safe CUDA 65k | básico | 0/0 | No ejecutado | No ejecutado | 30,432 s | No medido | `CUDA illegal memory access` en GPU0 |
| `8dd3325d-8658-45ca-9aad-ad80d301b4e9` | Laguna S 2.1 · dual GPU safe · 32k | Máximo | 1/1 | 20/20 | 4/8 | 60,919 s / 392,072 s / 871,561 s | 40.760 MB | Reparado; `tensor-split 1,1`, sin crash |

## Comparación de agentes en DeepSeek BCB

| Agente | BCB inicial → final | Tiempo total | Resultado |
|---|---:|---:|---|
| agent-basico | 1/8 → 2/8 | 830,127 s | No corrigió los fallos funcionales |
| agent-intermedio | 2/8 → 2/8 | 997,436 s | Reparación estancada |
| agent-avanzado | 3/8 → 4/8 | 1396,871 s | Mejor resultado; fallaron 771, 1019, 139 y 360 |
| agent-maximo | 1/8 → 1/8 | 983,893 s | Sólo pasó 906 |

## Criterio de actualización

Cada cambio debe registrar primero HE0, después HE20 y finalmente BCB. Si se
modifica perfil, binario, harness o agente de forma material, los resultados
anteriores quedan marcados como históricos y se repite la cadena desde HE0.

## Corrida auxiliar HumanEval 20 — 2026-08-27

Esta corrida usa modelo real y sirve como control de agente/sampling; no
reemplaza la cadena oficial HE0 → HE20 → BCB de la tabla operativa. Artefactos:
`%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-runs\HumanEval_20_tems__20260827_222122`.

| Perfil | HE20 | Pasadas | Tiempo total mediano | TPS mediano | Estado |
|---|---:|---:|---:|---:|---|
| `174_KAT Q4 K_M · sampling A/B 0.30/0.90` (`51d46758-fd7c-4d3c-8018-23154a2e0062`) | 20/20 | 3/3 | 277,601 s | 84,18 | Estable |
| `FAST - KAT2-Coder-7-8-26` (`sys-48-katcoder-262k`) | 20/20 en 2/3 | 2/3 | 214,785 s | 106,30 | Una pasada 19/20 por calidad |

El `comparison.json` persistido registra 5/6 corridas aceptadas y no muestra
fallo de infraestructura. No se promueve un ganador con esta muestra auxiliar.

## Campaña oficial en curso — 2026-08-27

El runner `tools/run-benchmark-post-correction.ps1` se inició con el binario
Release en `127.0.0.1:8765`, detectó 86 perfiles listos y está ejecutando en
serie HE0 → HE20 → BCB. Al momento de esta anotación se encuentra en
`141_QUALITY - DeepSeek Fusion leloch`, BCB, prompt 1/8; por lo tanto las
columnas oficiales no se actualizan ni se considera cerrada la campaña hasta
que exista un resultado persistido o un cierre explícito del runner.

## Seguimiento de campaña reanudada — 2026-08-28

Tras verificar la PC libre, se relanzó la campaña con el Release headless y la
cobertura persistida. El runner volvió a detectar 86 perfiles, omitió los que
ya tenían HE0/HE20/BCB válidos y avanzó hasta el perfil
`141_QUALITY - DeepSeek Fusion leloch · VRAM experts 0-2`, que inició BCB con
modelo real. En el mismo control, el perfil `153_BALANCE - Laguna S 2.1 · fit
on 100k · agent-maximo` permanecía incompleto con HE0 bloqueado.

No se agregaban filas ni se cambiaban rankings en ese corte intermedio. La
campaña quedó posteriormente detenida de forma intencional en el perfil 58/86;
el detalle auditado del cierre se registra abajo.

## Auditoría del corte de campaña oficial — 2026-08-28

El log persistente
`%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-campaign-post-correction.log`
registra una campaña de 86 perfiles iniciada a las 09:32. Hubo cierre explícito
para los perfiles 1–57: **27 `complete` y 30 `incomplete`**. El perfil 58
(`antirez · 32k · reasoning low`) inició HE20 y fue cancelado en el prompt 5/20;
por eso no tiene línea de cierre propia. En esta clasificación, `complete`
significa que no quedó una etapa pendiente o bloqueada en el runner; no implica
BCB 8/8.

Perfiles cerrados como `complete`: `1`, `3–6`, `8`, `11–12`, `14–22`,
`39–47` y `55`. Cerrados como `incomplete`: `2`, `7`, `9–10`, `13`, `23–38`,
`48–54` y `56–57`. Los artefactos del 28 de agosto agregan 42 JSON de resultado
para 30 nombres de perfil; el resto de la cobertura se reutilizó desde corridas
persistidas anteriores.

Los 17 JSON de HE0 con `0/0` y `failureStage=server-load` corresponden a
`DeepSeek V4-7-8-26` y las 16 variantes ULTRA-Q de los perfiles 13 y 23–38.
No son puntuaciones de calidad: el servidor no llegó a dejar una pasada
evaluable. El perfil 10, `VRAM balance`, dejó un intento BCB 5/8, pero su cierre
global quedó `infra-timeout` tras tres intentos; se conserva como resultado
provisional, no como promoción.

Resultados nuevos con evidencia persistida que faltaba reflejar:

| Perfil | HE0 | HE20 | BCB | Estado de lectura |
|---|---:|---:|---:|---|
| `[bench 48GB] KAT APEX-MTP + visión · MTP3 · 32k` | 1/1 · 46,149 s | 18/20 · 429,338 s | 5/8 · 463,354 s | Calidad parcial; cierre completo, sin promoción |
| `[bench 48GB] KAT APEX-MTP + visión · sin MTP · 32k` | 1/1 · 43,946 s | 19/20 · 702,375 s | 1/8 · 342,839 s | Control válido, calidad insuficiente |
| `[bench antirez stress] 64k · B4096 · U1024 · KV q8_0` | 1/1 · 156,513 s | 19/20 · 1150,040 s | 3/8 · 2049,730 s | Calidad parcial; no desplaza candidatos |

Las rutas de evidencia son, respectivamente,
`HumanEval_1_tems__20260828_123336` + `123721` +
`BigCodeBench-Hard_8_tems__20260828_124543`,
`HumanEval_1_tems__20260828_125509` + `125706` +
`BigCodeBench-Hard_8_tems__20260828_131007`, y
`HumanEval_1_tems__20260828_171744` + `172159` +
`BigCodeBench-Hard_8_tems__20260828_174241`, dentro de
`%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-runs`.

## Retiro de perfiles antirez A/B — 2026-08-28

Se retiraron de la cola activa las variantes declarativas:

| Perfil | HE0 | HE20 | Decisión |
|---|---:|---:|---|
| `sys-48-antirez-dsv4-q2q4-32k-reasoning-off` | 1/1 en 217,863 s | timeout a 1801,3 s; 0/0 evaluable | Retirado; no escala a HE20 |
| `sys-48-antirez-dsv4-q2q4-32k-reasoning-low` | 1/1 en 270,169 s | cancelado en prompt 5/20 tras ~14 min; sin score | Retirado; demasiado lento y sin calidad HE20 demostrada |

El control DeepSeek Fusion comparable completó HE20 en 1216,85–1300,74 s
con 20/20. Los resultados y metadata de esta campaña permanecen en
`%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-runs`; no se elimina evidencia.
La campaña oficial se canceló de manera intencional después de este gate y no
se presenta como matriz completa.
