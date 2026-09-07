# Registro detallado de perfiles, métricas y decisiones

## Propósito y alcance

Este registro es el inventario auditable de la campaña serial de **86 perfiles** iniciada el 2026-08-28. Se documenta cada perfil que el runner alcanzó, aunque haya sido descartado, bloqueado o retirado. También se deja constancia explícita del tramo que no llegó a ejecutarse para no confundir “sin evidencia” con “perfil malo”.

Fuentes cruzadas:

- Catálogo declarativo: [`assets/system_profiles.json`](../assets/system_profiles.json).
- Perfiles de usuario: [`profiles/launches.json`](../profiles/launches.json), con sus backends, modelos y runtimes asociados.
- Log de campaña: `%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-campaign-post-correction.log`.
- Resultados por etapa: `%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-runs`.
- Tabla operativa: [`benchmark-results.md`](benchmark-results.md).
- Historia narrativa: [`benchmark-results-history.md`](benchmark-results-history.md).

El runner llegó hasta **58/86**. Los perfiles 1–57 tienen cierre explícito: 27 `complete` y 30 `incomplete`. El perfil 58 comenzó HE20 y fue cancelado en el prompt 5/20; no tiene cierre de runner. Los perfiles 59–86 no se iniciaron. Por lo tanto, este documento no atribuye calidad, velocidad ni descarte por mérito a ese tramo no alcanzado.

La campaña generó o reintentó **42 JSON para 30 nombres de perfil** el 28/08. En las filas de abajo, “último persistido” significa el último resultado disponible por etapa para ese ID, incluso si proviene de una corrida histórica anterior. La columna `Campaña` conserva por separado el estado de la ejecución oficial del 28/08.

## Convenciones para leer las tablas

- `HE0` = HumanEval de 1 ítem; `HE20` = HumanEval de 20 ítems; `BCB` = BigCodeBench Hard de 8 ítems.
- Cada celda de métrica usa `score · segundos · tok/s`. `—` significa que no existe una medición evaluable, no calidad cero.
- `server-load`, `server-start`, `hard-timeout`, `request` y `agent` son causas de infraestructura/transporte. No se convierten en cero de calidad.
- `quality/acceptance` sí es un fallo evaluable del contenido o del contrato del harness.
- `FP` muestra los primeros 12 caracteres de la huella SHA-256 de configuración persistida, en orden `HE0 / HE20 / BCB`. Si difieren, las etapas no se deben mezclar como una sola corrida reproducible.
- Los tiempos de las corridas antiguas y de la campaña pueden diferir por intento, memoria adaptativa, proceso residual y estado térmico. El registro conserva ambos cuando la diferencia cambia la decisión.
- `complete` sólo indica que el runner no dejó una etapa pendiente; no significa BCB 8/8 ni promoción.

## Condiciones comunes de la campaña

| Dimensión | Valor congelado u observado |
|---|---|
| Máquina | 2× RTX 3090 de 24 GB, 128 GB de RAM; ejecución local Windows. |
| Aplicación | `build\Release\LlamaCode.exe`, daemon headless, API de control en `127.0.0.1:8765`. El binario Release se compiló/desplegó antes de la campaña y pasó el smoke de reinicio/scheduler. |
| Servidor | `llama-server` local en `127.0.0.1:8021`. En artefactos recientes se observa `D:\Models\llamacpp\llama.cpp-b10331-cuda12.4\llama-server.exe`; el mínimo declarado puede ser menor en perfiles legacy. |
| Harness | Target `agent`; suite HE0 → HE20 → BCB; los JSON identifican `harnessEngineId=legacy`, versión 1, semilla de agente 4242. El protocolo del proyecto lo denomina LC-H1. |
| Timeout | 1800 s nominales; los hard timeouts aparecen alrededor de 1801 s por sobrecarga de cierre. El runner permite dos reintentos de infraestructura. |
| Sampling base | `temp 0.60`, `top-p 0.95`, `top-k 20`, `min-p 0.0`, `repeat-penalty 1.0`, `presence-penalty 0.0`. |
| Excepciones | KAT A/B: `temp 0.30`, `top-p 0.90`, `min-p 0.05`; BigBang rápido: `temp 0.70`, `top-p 0.08`; Browser/DSH Dynamic: `temp 1.00` y reasoning medium/xhigh según fila. |
| Política KV | El KV K/V queda limitado a `q8_0` o menor. Los nombres antiguos con `f16` fueron capados a `q8_0` y no se mezclan con resultados históricos f16. `mmproj` F16/BF16 es proyector auxiliar de visión, no quant de pesos/KV. |

## Addendum fuera de la campaña serial — 2026-08-29

El perfil `sys-bench-qwen38-kvstream-24gb-131k` se agrega como registro
experimental posterior; no altera el conteo ni el orden de la campaña 86
perfiles del 2026-08-28. Su identidad queda congelada en
`assets/system_profiles.json`: engine `kv-streaming`, Qwen3.8 UD-Q4, K/V
`q8_0/q4_0`, ctx 131072, B/U 256/256, una GPU, un slot y pool de 2048 MiB.

| Evidencia | Resultado | Decisión |
|---|---|---|
| Benchmark sintético del fork, 8k → 131k | 16/16 puntos OK; 131k con 617,1 tok/s de prefill, 5,25 tok/s de decode y 19.621 MiB de VRAM usada | **Superior en ejecución/capacidad de contexto largo; mantener experimental** |
| Probe NIAH/passkey | Sin salida exacta en el A/B; no es score de calidad válido para este GGUF | **No promocionar** |
| Control `stage=0` del mismo fork | Cuelga al iniciar el prompt sintético de 8k | **Inferior / no usar** |

Los recibos completos quedan en la estación de prueba bajo
`D:\Models\llamacpp\qwen38-adaptive-kv-sweep-20260829-131k-clean` y
`D:\Models\llamacpp\qwen38-adaptive-kv-ab-20260829-ub64-result.json`. El
runner A/B fue ampliado para conservar `serverExe` por variante, y la matriz
LID recibió `--startup-only`; ambas mejoras sí quedan versionadas en el repo.

## Inventario de modelos, quantizaciones y binarios

| Familia | Archivo/piezas relevantes | Quant de pesos | Proyector/drafter | Build mínima declarada | Observación |
|---|---|---|---|---:|---|
| DeepSeek Fusion / antirez | `DeepSeek-V4-Flash-Layers37-42Q4KExperts-OtherExpertLayersIQ2XXSGateUp-Q2KDown-AProjQ8-SExpQ8-OutQ8-chat-v2-imatrix-fixed-0731.gguf` | Híbrido Q2/Q4 imatrix, ~90,89 GiB | Sin mmproj ni drafter | b10228 | MoE offloaded: capas de expertos selectas en CUDA1 y resto CPU; `tensor-split 1,0`. |
| DeepSeek ULTRA-Q | `DeepSeek-V4-Flash-0731-UD-IQ3_S-00001-of-00004.gguf` más shards `00002`–`00004` | `UD-IQ3_S` | Sin mmproj; sin DSpark en las filas auditadas | b10228 | El archivo es 1/4; no basta verificar sólo el primer shard. La carga debe validar el conjunto completo. |
| Qwen3.8 Dynamic V3 | `Qwen3.8-27B-UD-Q4_K_XL.gguf` | `UD-Q4_K_XL` | `mmproj-BF16.gguf`; MTP embebido o draft MTP separado según fila | b10331 | Es la familia con mejores referencias 8/8; cada cambio de MTP, KV, contexto o harness tiene huella propia. |
| Qwen3.8 Q4/Q5 | `Qwen3.8-27B-Q4_K_M.gguf` / `Qwen3.8-27B-Q5_K_M.gguf` | `Q4_K_M` / `Q5_K_M` | `mmproj-BF16.gguf`; MTP | b10331 | En esta cadena Q5 no mejoró Q4: 4/8 frente a 6/8 BCB. |
| ThinkingCap / MAX-Q | `ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf` | `Q4_K_M` | `mmproj-ThinkingCap-27B-f16.gguf`; MTP4 | b10228 | El beneficio de 48GB se usa sobre todo para KV q8 y contexto; cambiar a KV q4 altera velocidad/calidad. |
| KAT Coder control | GGUF KAT-Coder V2.5-Dev Q4_K_M | `Q4_K_M` | Sin mmproj en el control; template `kat-coder-tools.jinja` | b10228 | Texto/tools; el control 262k/B512/U64 es rápido pero BCB parcial. |
| KAT APEX | `KAT-Coder-V2.5-Dev-MTP-APEX-i-quality-v2.gguf` | APEX I-Quality-v2 | `mmproj-F16.gguf`; MTP2/MTP3 | b10331 | Artefactos verificados con SHA-256 en la sección APEX de esta tabla; visión y XML KAT confirmados. |
| Laguna S 2.1 | `Laguna-S-2.1-UD-Q2_K_XL.gguf` | `UD-Q2_K_XL` | Sin mmproj | b10087 | El template v24, Flash, fit y batch modifican mucho el resultado; no mezclar las variantes. |

Los perfiles de usuario además conservan un `binaryId` dentro de su backend. En
los resultados recientes la resolución efectiva observada apunta a
`D:\Models\llamacpp\llama.cpp-b10331-cuda12.4\llama-server.exe`, pero la
declaración del catálogo sigue siendo la fuente del mínimo compatible por
familia. Por eso la tabla conserva simultáneamente `binaryId`, build mínima y
la ruta observada: no son sinónimos y no deben colapsarse en una sola etiqueta.

## Índice de artefactos nuevos del corte 2026-08-28

Los nombres de carpeta son relativos a `%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-runs` y contienen `metadata.json`, el JSON de resultados, workspace y, cuando corresponde, el log del harness. Los tres perfiles de abajo son los resultados que faltaban en la matriz y quedaron persistidos durante el corte oficial.

| Perfil | HE0 | HE20 | BCB | Resultado |
|---|---|---|---|---|
| 43 · KAT APEX MTP3 | `HumanEval_1_tems__20260828_123336` | `HumanEval_20_tems__20260828_123721` | `BigCodeBench-Hard_8_tems__20260828_124543` | 1/1, 18/20, 5/8; huella `d129fd9df9f6`. |
| 44 · KAT APEX sin MTP | `HumanEval_1_tems__20260828_125509` | `HumanEval_20_tems__20260828_125706` | `BigCodeBench-Hard_8_tems__20260828_131007` | 1/1, 19/20, 1/8; huella `7285ccddb373`. |
| 55 · antirez stress 64k KV q8 | `HumanEval_1_tems__20260828_171744` | `HumanEval_20_tems__20260828_172159` | `BigCodeBench-Hard_8_tems__20260828_174241` | 1/1, 19/20, 3/8; huella `2dc877404a16`. |
| 58 · antirez reasoning low | `HumanEval_1_tems__20260828_193117` | `HumanEval_20_tems__20260828_193722` | No iniciado | HE0 1/1; cancelado durante HE20 en prompt 5/20. El JSON auxiliar de HE20 no se usa como score oficial. |

El resto de los resultados históricos puede localizarse por `profileId`,
`timestamp`, `runDir`, `profileConfigFingerprint` y
`benchmarkEffectiveArgs` dentro de cada JSON. No se copiaron los JSON al repo:
el ledger mantiene el índice y el repositorio conserva la configuración y la
decisión, mientras que `%LOCALAPPDATA%` conserva la evidencia pesada de cada
ejecución.

## Criterio de decisión

| Estado | Significado operativo |
|---|---|
| Promocionable | HE0 y HE20 válidos, BCB 8/8 evaluable y velocidad/estabilidad razonables para el tier. Puede ser una referencia histórica si la huella actual cambió. |
| Referencia de calidad | BCB 8/8 válido, pero con coste alto, harness específico, contexto distinto o una corrida posterior degradada. No reemplaza automáticamente SOL/TERRA/LUNA. |
| Parcial / no promover | Carga y/o HE20 válidos, pero BCB menor que 8/8 o inconsistente. Sirve para aprender; no debe aparecer como ganador. |
| Infraestructura / bloqueado | No hubo pasada evaluable por carga, timeout, transporte, servidor o agente. Requiere corregir la causa y repetir desde HE0; no es un juicio sobre el modelo. |
| Retirado | Se quitó de la cola activa (`benchmark=false`) por evidencia suficiente de que no justifica el coste de continuar. Se preservan JSON y logs. |
| No alcanzado | El runner se detuvo antes de iniciar el perfil. No hay evidencia para promover ni descartar. |

## Perfiles 1–10: base, DeepSeek Fusion y Laguna

| # | ID / fuente | Modelo, quant y configuración congelada | Binario / agente | Campaña | HE0 — último persistido | HE20 — último persistido | BCB — último persistido | FP `HE0/HE20/BCB` | Lectura y decisión |
|---:|---|---|---|---|---|---|---|---|---|
| 1 | `4f5cc556-333d-4310-955e-15042cd874d6` · launch | DeepSeek Fusion basado en `DeepSeek-V4` antirez Q2/Q4 imatrix; ctx 131k; B4096/U1024; KV q4; `fit off`; layer `tensor-split 1,0`; expertos 37–42 en CUDA1 y resto CPU; CPU-MoE; sin visión/spec. | `binaryId 6372ba0d-b1ef-4341-9c79-f79b308336af`; mínimo histórico b10228+; `agent-maximo`; sampling base. | `complete` | 1/1 · 160,536 s · 0,293 t/s | 20/20 · 1232,312 s · 8,695 t/s | 3/8 · 1678,949 s · 8,233 t/s; `quality/acceptance` | `8dc16054d127 / 8dc16054d127 / 8dc16054d127` | No promover: calidad BCB parcial y throughput muy bajo para un perfil QUALITY. Conservado como control Fusion. |
| 2 | `9e26ee20-85fd-4a4d-a58c-45844e072689` · launch | Laguna S 2.1 118B-A8B `UD-Q2_K_XL`; ctx 100k; B512/U64; KV q4; `fit on`; layer `tensor-split 1,1`; Flash; template Laguna v24; agent máximo explícito. | `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; ficha legacy b10228+; `agent-maximo`; sampling base. | `incomplete`; HE0 bloqueado | 0/1 · 368,697 s · 35,558 t/s; `quality/acceptance` | 20/20 · 658,860 s · 46,573 t/s | 0/8 · 66,484 s · 0,214 t/s; `quality/acceptance` | `13e23b60cbf2 / 089b1dabd803 / 089b1dabd803` | Bloqueado por HE0 en la campaña; el BCB histórico 0/8 tampoco habilita promoción. El HE20 válido no rescata un gate HE0 fallido. |
| 3 | `8d0dd2e0-c6c6-41ef-81d6-893c20d2f621` · launch | Laguna S 2.1 `UD-Q2_K_XL`; ctx 100k; B512/U64 en el launch; KV q4; `fit on`; `tensor-split 1,1`; Flash; template v24; agente por defecto. | `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; ficha b10228+; agente default. | `complete` | 1/1 · 140,140 s · 28,898 t/s | 20/20 · 529,608 s · 32,483 t/s | 8/8 · 1253,510 s · 32,417 t/s | `759aefed56a2 / 759aefed56a2 / 759aefed56a2` | Mejor Laguna medido en esta familia; referencia de calidad completa, pero lenta y con coste E2E alto. No desplaza Dynamic/Qwen para uso general. |
| 4 | `9dda6bf4-7aae-4806-ba3a-8466bf41e702` · launch | KAT-Coder V2.5-Dev Q4_K_M; ctx 262k; B512/U64; KV q8; `fit off`; layer `tensor-split 1,1`; Flash; sin visión/spec; template `kat-coder-tools.jinja`; reasoning off. | `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; ficha KAT b10228+; `agent-maximo`. | `complete` | 1/1 · 30,163 s · 1,387 t/s | 20/20 · 280,542 s · 67,165 t/s | 3/8 · 602,898 s · 65,980 t/s; `quality/acceptance` | `e143b98e12c0 / e143b98e12c0 / e143b98e12c0` | FAST/text-only: muy veloz, pero BCB 3/8. Mantener como referencia de latencia, no como perfil de calidad. |
| 5 | `a03e65f5-2f2c-4d45-b67b-4b1270fa2a6c` · launch | ThinkingCap Qwen3.6-27B Q4_K_M; mmproj F16; ctx 131k; B512/U64; KV q4; MTP4; visión; mlock on/mmap off en la receta histórica; sampling base. | `binaryId 90e90d91-ee33-49d1-963a-fd7c30017e64`; backend histórico “KAT vs Qwen 35B Q4KM · CUDA b9763”; agente default. | `complete` | 1/1 · 39,744 s · 1,386 t/s | 20/20 · 254,024 s · 40,628 t/s | 5/8 · 215,717 s · 33,520 t/s; `quality/acceptance` | `9d2c8cbe4ed9 / 9d2c8cbe4ed9 / 9d2c8cbe4ed9` | Candidato LUNA por latencia; calidad parcial. La corrida corta 5/5 en 86,262 s es auxiliar y no sustituye BCB. |
| 6 | `cbff7c85-2116-4b42-b1b9-485dd33384cc` · launch | BigBang-v1 Q4_K_M; mmproj BF16; MTP embebido n=5; runtime efectivo 64k/B256/U64/KV q8; Flash on; reasoning on; sampling agresivo `0.70/0.08`. | `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; ficha BigBang requiere b10262+; `agent-maximo`. | `complete` | 1/1 · 41,520 s · 11,696 t/s | 20/20 · 227,398 s · 38,072 t/s | 4/8 · 653,426 s · 33,727 t/s; `quality/acceptance` | `f64bbe113d2d / f64bbe113d2d / f64bbe113d2d` | Velocidad útil, pero calidad incompleta y receta sensible a contexto/batch. No confundir con el repair METEOR histórico. |
| 7 | `392ea030-059e-4f69-86c6-81d3fa31acbc` · launch | DeepSeek Fusion Q2/Q4 imatrix; ctx 131k; B4096/U1024; KV q4; `fit off`; expertos 0–2 en CUDA0, 37–42 en CUDA1 y resto CPU; CPU-MoE; sin visión/spec. | `binaryId 6372ba0d-b1ef-4341-9c79-f79b308336af`; mínimo b10228+; `agent-maximo`. | `incomplete`; BCB pendiente en cierre | 1/1 · 161,554 s · 0,293 t/s | 20/20 · 1300,738 s · 8,485 t/s | 3/8 · 1263,873 s · 8,714 t/s histórico; `quality/acceptance` | `0697e515ebfb / 0697e515ebfb / 96bc6bd48671` | Mover expertos 0–2 a CUDA0 no produjo una mejora de calidad/velocidad que justifique la complejidad. No promover. |
| 8 | `8dd3325d-8658-45ca-9aad-ad80d301b4e9` · launch | Laguna S 2.1 Q2; ctx 32k; B128/U32; KV q4; `fit off`; Flash; `tensor-split 1,1`; template v24; `n-predict 512`; agent máximo. | `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; ficha b10228+; `agent-maximo`. | `complete`; último intento HE20 parcial | 1/1 · 77,724 s · 1,227 t/s | 11/20 · 1064,067 s · 38,795 t/s; `quality/acceptance`; histórico 20/20 | 1/8 · 839,827 s · 35,285 t/s; histórico 4/8 | `cba95b13eb31 / cba95b13eb31 / cba95b13eb31` | Evidencia inconsistente entre intentos; no tratar la corrida histórica 4/8 como promoción. Repetir sólo si se necesita estudiar contexto corto Laguna. |
| 9 | `807c23f8-442c-4303-b96a-e1d0481eaf69` · launch | Laguna S 2.1 Q2; ctx 65k; B256/U32; KV q4; `fit off`; Flash off; `tensor-split 1,1`; template v24. | `binaryId c243506b-f4bc-4a2c-b3a7-6f946f3ab77a`; backend nombrado b10228 safe CUDA; agente default. | `incomplete`; HE0 bloqueado | 0/1 · 746,361 s · 34,892 t/s; `quality/acceptance` | 20/20 · 231,125 s · 55,306 t/s | 4/8 · 911,053 s · 50,539 t/s; `quality/acceptance` | `f30dfa2fe84d / 7d2f8d9ea4da / 7d2f8d9ea4da` | No promover: el HE0 actual falla y el nombre “safe CUDA” no garantiza calidad. Conservar como diagnóstico de Flash off/batch reducido. |
| 10 | `6b3bf7bd-0889-491a-9b6d-b12128478a5f` · launch | DeepSeek Fusion Q2/Q4; ctx 131k; B4096/U1024; KV q4; `fit off`; expertos 0–1 CUDA0, 37–42 CUDA1 y resto CPU; CPU-MoE; Flash; cache RAM 32768. | `binaryId 6372ba0d-b1ef-4341-9c79-f79b308336af`; mínimo b10228+; `agent-maximo`. | `incomplete`; BCB `infra-timeout` tras 3 intentos | 1/1 · 164,092 s · 0,281 t/s | 20/20 · 1216,849 s · 8,967 t/s | 5/8 · 1290,250 s · 7,527 t/s; `quality/acceptance` en intento persistido | `0182d705fa56 / 0182d705fa56 / 0182d705fa56` | Hay un BCB parcial, pero el cierre oficial fue infraestructura/timeout. Se conserva, no se promociona ni se usa para comparar contra Qwen sin misma huella. |

## Perfiles 11–22: Qwen3.8, MTP, DSH y agentes Browser

| # | ID / fuente | Modelo, quant y configuración congelada | Binario / agente | Campaña | HE0 — último persistido | HE20 — último persistido | BCB — último persistido | FP `HE0/HE20/BCB` | Lectura y decisión |
|---:|---|---|---|---|---|---|---|---|---|
| 11 | `sys-qwen38-27b-q4km-131k` · system | Qwen3.8-27B Q4_K_M; `mmproj-BF16.gguf`; ctx 131k; B512/U64; KV q4; MTP; visión; reasoning on en la ficha. | `official`, mínimo b10331; `agent-maximo`. | `complete` | 1/1 · 39,121 s · 1,487 t/s | 20/20 · 378,664 s · 34,858 t/s | 6/8 · 723,087 s · 33,927 t/s; `quality/acceptance` | `8eac3495b056 / 8eac3495b056 / 8eac3495b056` | Q4 rápido y funcional, pero pierde calidad frente a los perfiles Dynamic 8/8. |
| 12 | `sys-qwen38-27b-q5km-131k` · system | Qwen3.8-27B Q5_K_M; `mmproj-BF16.gguf`; ctx 131k; B512/U64; KV q4; MTP; visión; reasoning on. | `official`, mínimo b10331; `agent-maximo`. | `complete` | 1/1 · 38,877 s · 1,495 t/s | 20/20 · 385,110 s · 35,714 t/s | 4/8 · 581,714 s · 33,315 t/s; `quality/acceptance` | `a2a559090a60 / a2a559090a60 / a2a559090a60` | La quant más pesada no ganó calidad en este harness; no justificar memoria/coste adicional con esta evidencia. |
| 13 | `sys-48-dsv4-nospec` · system | DeepSeek V4 Flash UD-IQ3_S en 4 shards; ctx 131k; B4096/U1024; KV q4; `fit off`; `tensor-split 1,0`; expertos 29–36 CUDA1/resto CPU; sin speculative; agent máximo. | `official`, mínimo b10228; path de modelo `DeepSeek-V4-Flash-0731-UD-IQ3_S-00001-of-00004.gguf`. | `incomplete`; HE0 en `server-load` | 0/0 · 4,440 s · 0 t/s; `server-load` | 0/0 · 204,061 s · 0 t/s; `hard-timeout` | 0/8 · 27,952 s; `request`, no evaluable | `6537405afd02 / f84556ab98eb / —` | Infraestructura, no calidad cero. El servidor no llegó a dejar un HE0 evaluable; investigar carga/shards/binario antes de opinar sobre DeepSeek. |
| 14 | `51d46758-fd7c-4d3c-8018-23154a2e0062` · launch | KAT-Coder Q4_K_M; ctx 262k; B512/U64; KV q8; `fit off`; Flash; `tensor-split 1,1`; sin visión/spec; reasoning off; A/B `temp 0.30/top-p 0.90/min-p 0.05`. | `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; `agent-maximo`; template KAT. | `complete` | 1/1 · 41,317 s · 129 t/s | 20/20 · 260,082 s · 108,067 t/s | 3/8 · 557,542 s · 112,355 t/s; `quality/acceptance` | `1831be11c92a / 1831be11c92a / 1831be11c92a` | El sampling conservador A/B pasó HE20, pero BCB sigue 3/8. No atribuir el cambio de sampling como mejora global sin repetición apareada. |
| 15 | `ee97cec0-3915-46eb-ab41-59ff362a8f63` · launch | Qwen3.8 Dynamic V3 UD-Q4_K_XL; ctx 131k; B512/U64; KV q8; MTP2; visión/plantilla Qwen3.8; Flash; reasoning off. | Backend `Dynamic v3 · llama.cpp b10331 CUDA`; `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; `agent-maximo`. | `complete` | 1/1 · 38,715 s · 1,394 t/s | 20/20 · 378,906 s · 35,058 t/s | 6/8 · 841,819 s · 32,497 t/s; `quality/acceptance` | `323e275970e2 / 323e275970e2 / 323e275970e2` | MTP2/KV q8 estable, pero no supera el umbral de calidad. |
| 16 | `ec212f51-730e-4456-a673-0aba1d1818a8` · launch | Qwen3.8 Dynamic V3 UD-Q4_K_XL; ctx 131k; B512/U64; KV q8; MTP2 embebido; visión; Flash; reasoning off. | Backend Dynamic V3 b10331 CUDA; `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; `agent-maximo`. | `complete` | 1/1 · 38,732 s · 1,381 t/s | 20/20 · 380,763 s · 34,875 t/s | 8/8 · 512,519 s · 33,891 t/s | `ea8d5167b170 / ea8d5167b170 / ea8d5167b170` | Referencia de calidad completa de la campaña: consistente y simple. No es la más rápida, pero sí una base sólida para comparar cambios. |
| 17 | `37269d11-26db-4fd0-ade3-3c595f70e4cd` · launch | Qwen3.8 Dynamic V3 UD-Q4_K_XL; ctx 64k; B512/U64; KV q8; MTP2 embebido; visión; Flash; reasoning off. | Backend Dynamic V3 b10331 CUDA; `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; `agent-maximo`. | `complete` | 1/1 · 38,761 s · 1,394 t/s | 20/20 · 378,868 s · 35,359 t/s | 6/8 · 844,742 s · 33,841 t/s; `quality/acceptance` | `148a75023cc0 / 148a75023cc0 / 148a75023cc0` | Reducir contexto no produjo el salto de calidad esperado; mantener como control de memoria, no como ganador. |
| 18 | `8797a8cf-fea9-46cb-934a-0d62f3ee8ca7` · launch | Qwen3.8 Dynamic V3; ctx 160k; B512/U64; KV q8; MTP2; `mmproj` en RAM; DSH medium; Flash; temp 1.0; agent-browser. | Backend Dynamic V3 b10331 CUDA; `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; `agent-browser`. | `complete` | 1/1 · 38,709 s · 1,458 t/s | 20/20 · 378,486 s · 35,263 t/s | 7/8 · 734,164 s · 33,862 t/s; histórico 8/8 | `469b9bc0e52d / 469b9bc0e52d / 469b9bc0e52d` | Referencia SOL histórica: 8/8, 54,74 tok/s y 890,1 s E2E con otra corrida. El último persistido 7/8 impide tratarla como garantía automática; repetir antes de cambiar la promoción. |
| 19 | `7d54c7f2-47dd-43df-a608-f67e4d4b027d` · launch | Qwen3.8 Dynamic V3 UD-Q4; ctx 131k; B512/U64; KV q4; MTP3/separado; visión; Browser Agent; temp 1.0; reasoning medium; Flash. | Backend Dynamic V3 b10331 CUDA; `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; `agent-browser`. | `complete` | 1/1 · 38,064 s · 1,568 t/s | 20/20 · 367,020 s · 36,336 t/s | 7/8 · 590,357 s · 32,328 t/s; histórico 8/8 | `8ac81c224c09 / 8ac81c224c09 / 8ac81c224c09` | Referencia TERRA histórica: 8/8 y 592,5 s E2E; el último BCB 7/8 queda registrado como degradación posterior, no se oculta. |
| 20 | `abc1df7a-2af1-4957-9d12-dbe2d01988aa` · launch | Qwen3.8 Dynamic V3; ctx 192k/196608; B512/U64; KV q8; MTP2; `mmproj` RAM; DSH medium; Flash; temp 1.0; agent-browser. | Backend Dynamic V3 b10331 CUDA; `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; `agent-browser`. | `complete` | 1/1 · 38,712 s · 1,413 t/s | 20/20 · 378,726 s · 35,115 t/s | 7/8 · 599,181 s · 33,613 t/s; histórico 8/8 | `34abef073866 / 34abef073866 / 34abef073866` | Mejor contexto de la familia; histórico 8/8 y 55,11 tok/s, pero el último intento 7/8 exige repetir antes de presentar estabilidad absoluta. |
| 21 | `2c25280b-819e-411c-92fc-c5127cb3b900` · launch | Qwen3.8 Dynamic V3; ctx 131k; B512/U64; KV q4; MTP3; visión; Browser Agent; temp 1.0; reasoning xhigh; Flash. | Backend Dynamic V3 b10331 CUDA; `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; `agent-browser`. | `complete` | 1/1 · 37,736 s · 1,586 t/s | 20/20 · 368,109 s · 36,398 t/s | 8/8 · 650,847 s · 34,407 t/s | `a7684e6a3c32 / a7684e6a3c32 / a7684e6a3c32` | Calidad completa con reasoning xhigh; más lento que el Browser medium histórico, pero útil como referencia de robustez del agente. |
| 22 | `d9df2b65-41b2-43bb-97b6-5fe6f2760dc4` · launch | Qwen3.8 Dynamic V3 UD-Q4; ctx 131k; B512/U64; KV q4; MTP3 separado; visión; Flash; reasoning off; agent máximo. | Backend Dynamic V3 b10331 CUDA; `binaryId ead8f9b5-c918-4252-b7df-247e058f6f42`; `agent-maximo`. | `complete` | 1/1 · 39,482 s · 1,421 t/s | 20/20 · 397,193 s · 36,439 t/s | 7/8 · 500,153 s · 32,049 t/s; histórico 8/8 | `aab88ded9deb / aab88ded9deb / aab88ded9deb` | Control MTP separado: el BCB histórico 8/8 lo mantiene como referencia, pero el último 7/8 no justifica promoción automática. |

## Perfiles 23–38: ULTRA-Q y DeepSeek IQ3_S

Familia común: `DeepSeek-V4-Flash-0731-UD-IQ3_S-00001-of-00004.gguf` y shards asociados, quant `UD-IQ3_S`, sin DSpark, agente `agent-maximo`, mínimo de catálogo b10228, `tensor-split 1,0`, expertos alineados por `-ot`, CPU-MoE y KV q4/q8 según fila. En la variante 48GB se conserva el reparto de expertos a CUDA1 y resto CPU. Los 17 HE0 `0/0` de esta familia (13 y 23–38) son `server-load`; no son diecisiete ceros de calidad.

| # | ID / fuente | Configuración diferencial | Binario / agente | Campaña | HE0 — último persistido | HE20 — último persistido | BCB — último persistido | FP `HE0/HE20/BCB` | Lectura y decisión |
|---:|---|---|---|---|---|---|---|---|---|
| 23 | `sys-bench-ultraq-b4096-u1024-nospec` | ctx 131k; B4096/U1024; KV q4; MoE base 39; sin DSpark. | `official` mínimo b10228; `agent-maximo`. | `incomplete` | 0/0 · 5,653 s; `server-load` | — | — | `fa7630b36bf2 / — / —` | Bloqueado antes de HE0 evaluable; investigar servidor/shards. |
| 24 | `sys-bench-ultraq-b8192-u2048-nospec` | ctx 131k; B8192/U2048; KV q4; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` | 0/0 · 5,033 s; `server-load` | — | — | `8b99359c0e52 / — / —` | Bloqueado por carga; el batch alto no llegó a medirse. |
| 25 | `sys-bench-ultraq-b4096-u1024-moe43-nospec` | ctx 131k; B4096/U1024; KV q4; `n-cpu-moe 43`; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` | 0/0 · 4,473 s; `server-load` | — | — | `54cb21ed19f1 / — / —` | Infraestructura; no concluir que MoE43 sea peor en calidad. |
| 26 | `sys-bench-ultraq-b8192-u2048-kv8-nospec` | ctx 131k; B8192/U2048; KV q8; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` | 0/0 · 4,425 s; `server-load` | — | — | `85e9cecd02ce / — / —` | Infraestructura; comparar KV q8 sólo después de una carga limpia. |
| 27 | `sys-bench-ultraq-b8192-u2048-kv-k8v4` | ctx 131k; B8192/U2048; K q8/V q4; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` | 0/0 · 16,681 s; `server-load` | — | — | `5c68dc26b0f2 / — / —` | Infraestructura; la carga tardó más, pero no fue una medición de velocidad del modelo. |
| 28 | `sys-bench-ultraq-64k-nospec` | ctx 64k; B2048/U512; KV q4; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` | 0/0 · 4,420 s; `server-load` | — | — | `01af93a65cc1 / — / —` | Infraestructura; contexto corto no validado. |
| 29 | `sys-bench-ultraq-64k-kv8-nospec` | ctx 64k; B2048/U512; KV q8; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` | 0/0 · 4,430 s; `server-load` | — | — | `2c178359267f / — / —` | Infraestructura; no comparar con 28 por ausencia de HE0 válido. |
| 30 | `sys-bench-ultraq-reasoning-low` | ctx 131k; B1024/U512 heredado; KV q4; reasoning low; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` | 0/0 · 4,407 s; `server-load` | 0/20 · 11,750 s; `infrastructure/agent` | — | `54b89f215541 / cd978e0e2758 / —` | Doble bloqueo: no hay HE0 y el intento HE20 fue de transporte/agente. Sin veredicto de razonamiento low. |
| 31 | `sys-bench-ultraq-reasoning-medium` | ctx 131k; KV q4; reasoning medium; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` | 0/0 · 36,204 s; `server-load` | — | — | `418461b1ea32 / — / —` | Infraestructura; el tiempo de carga no equivale a TTFT ni throughput. |
| 32 | `sys-bench-ultraq-reasoning-high` | ctx 131k; KV q4; reasoning high; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` | 0/0 · 4,422 s; `server-load` | 0/0 · 213,160 s; `hard-timeout` | — | `98272d10cfc8 / 986403915442 / —` | Infraestructura; no usar como evidencia de que reasoning high sea inviable. |
| 33 | `sys-ultraq-dsv4-0731-iq3s-48gb` · system | ctx 131k; B4096/U1024; KV q4; 48GB; expertos 29–36 CUDA1/resto CPU; sin DSpark. Modelo en 4 shards IQ3_S. | `official` mínimo b10228; `agent-maximo`. | `incomplete` por HE0 actual | 0/0 · 4,461 s; `server-load` | 20/20 · 1557,752 s · 10,038 t/s histórico | 8/8 · 2830,574 s · 9,645 t/s histórico | `7ae71fb2c90 / b0358bbdd88c / b0358bbdd88c` | Único DeepSeek/ULTRA con BCB completo histórico, pero extremadamente lento y con HE0 actual fallido. Referencia de capacidad, no perfil práctico. |
| 34 | `sys-bench-ultraq-48gb-64k-nospec` | Variante 48GB; ctx 64k; B4096/U1024 heredado; KV q4; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` | 0/0 · 4,436 s; `server-load` | 0/0 · 1801,172 s; `hard-timeout` | — | `782dd64f65b4 / 1fae71bbde05 / —` | Carga/timeout; no evidencia de calidad. |
| 35 | `sys-bench-ultraq-48gb-64k-kv8` | Variante 48GB; ctx 64k; B4096/U1024; KV q8; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` | 0/0 · 5,797 s; `server-load` | 0/0 · 1801,178 s; `hard-timeout` | — | `641c8fe26c1c / 4a51987ef573 / —` | KV q8 no puede juzgarse porque el arranque/HE20 no dejó una pasada comparable. |
| 36 | `sys-bench-ultraq-48gb-reasoning-off` | Variante 48GB; ctx 131k; B4096/U1024; KV q4; reasoning off; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` por HE0 actual | 0/0 · 4,409 s; `server-load` | 20/20 · 1519,384 s · 10,145 t/s histórico | 4/8 · 1537,609 s · 9,825 t/s; `quality/acceptance` histórico | `9b63c35370fc / bd3958d210ca / bd3958d210ca` | Hold: HE20 sí, pero HE0 actual falla y BCB es parcial. No promover. |
| 37 | `sys-bench-ultraq-48gb-reasoning-medium` | Variante 48GB; ctx 131k; B4096/U1024; KV q4; reasoning medium; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` por HE0 actual | 0/0 · 5,143 s; `server-load` | 20/20 · 1500,984 s · 10,093 t/s histórico | 5/8 · 1946,960 s · 9,899 t/s; `quality/acceptance` histórico | `566250ad32c3 / 351f8d7a48c5 / 351f8d7a48c5` | Hold experimental; muy lento y BCB parcial. |
| 38 | `sys-bench-ultraq-48gb-reasoning-high` | Variante 48GB; ctx 131k; B4096/U1024; KV q4; reasoning high; sin DSpark. | `official` b10228+; `agent-maximo`. | `incomplete` por HE0 actual | 0/0 · 8,820 s; `server-load` | 20/20 · 1507,520 s · 10,159 t/s histórico | 5/8 · 1961,085 s · 10,079 t/s; `quality/acceptance` histórico | `0688f047feb2 / 9c2e2af9c8e5 / 9c2e2af9c8e5` | Hold experimental; no compensa el coste frente a Qwen 8/8 muchas veces más rápido. |

## Perfiles 39–47: MAX-Q, KAT APEX y Laguna 48GB

Artefactos relevantes de APEX: `KAT-Coder-V2.5-Dev-MTP-APEX-i-quality-v2.gguf`, quant APEX I-Quality-v2, `mmproj-F16.gguf`, SHA-256 del modelo `F4501B4F54577DA43A1D088BD3B81CE4DF900A553F5407BC5D29BCBE54AF66EF` y del mmproj `71F3CBC1F7CC0F30D09D41CFA924C0060827EBC33BF15ACE7E86661E856F0160`. La familia MAX-Q usa `ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf` con `mmproj-ThinkingCap-27B-f16.gguf`.

| # | ID / fuente | Modelo, quant y configuración congelada | Binario / agente | Campaña | HE0 — último persistido | HE20 — último persistido | BCB — último persistido | FP `HE0/HE20/BCB` | Lectura y decisión |
|---:|---|---|---|---|---|---|---|---|---|
| 39 | `sys-bench-48-tc-mtp` · system | ThinkingCap Qwen3.6-27B Q4_K_M; mmproj F16; ctx 196k; B2048/U512; KV q8; MTP4; visión; cache prompt; layer split. | `official` mínimo b10228; `agent-maximo`. | `complete` | 1/1 · 25,845 s · 1,301 t/s | 20/20 · 222,017 s · 36,001 t/s | 6/8 · 231,400 s · 28,718 t/s; `quality/acceptance` | `894fbc615475 / 894fbc615475 / 894fbc615475` | Contexto largo y buena latencia, pero calidad parcial. |
| 40 | `sys-bench-48-tc-mtp-131k` · system | ThinkingCap Q4_K_M; ctx 131k; B2048/U512; KV q8; MTP4; visión; mmproj F16. | `official` b10228+; `agent-maximo`. | `complete` | 1/1 · 53,725 s · 28,568 t/s | 20/20 · 282,125 s · 37,908 t/s | 6/8 · 291,147 s · 34,761 t/s; `quality/acceptance` | `28aba95d76d4 / 28aba95d76d4 / 28aba95d76d4` | La variante 131k no mejora la calidad y es menos atractiva que 196k en esta tanda. |
| 41 | `sys-bench-48-tc-kv4` · system | ThinkingCap Q4_K_M; ctx 196k; B2048/U512; KV q4; MTP4; visión; mmproj F16. | `official` b10228+; `agent-maximo`. | `complete` | 1/1 · 24,440 s · 0,771 t/s | 20/20 · 364,957 s · 21,324 t/s | 5/8 · 294,319 s · 21,973 t/s; `quality/acceptance` | `f5d907f799d5 / f5d907f799d5 / f5d907f799d5` | KV q4 reduce velocidad/calidad respecto de MTP4/KV q8. |
| 42 | `sys-bench-48-tc-b4096` · system | ThinkingCap Q4_K_M; ctx 196k; B4096/U1024; KV q8; MTP4; visión; mmproj F16. | `official` b10228+; `agent-maximo`. | `complete` | 1/1 · 24,531 s · 0,782 t/s | 20/20 · 383,975 s · 23,147 t/s | 5/8 · 259,900 s · 22,722 t/s; `quality/acceptance` | `dda051d7a6df / dda051d7a6df / dda051d7a6df` | Batch mayor no compensa la pérdida de calidad; no promover. |
| 43 | `sys-bench-48-kat-mtp-vision-mtp3` · system | KAT APEX I-Quality-v2; mmproj F16; ctx 32k; B512/U64; KV q8; MTP3; visión; Flash; layer split 1,1; skip-chat-parsing; reasoning off. | `official` mínimo b10331; `agent-maximo`. | `complete` | 1/1 · 46,149 s · 138,956 t/s | 18/20 · 429,338 s · 930,181 t/s; `quality/acceptance` | 5/8 · 463,354 s · 155,397 t/s; `quality/acceptance` | `d129fd9df9f6 / d129fd9df9f6 / d129fd9df9f6` | Candidato experimental de visión/tools; calidad parcial, no desplaza Dynamic. |
| 44 | `sys-bench-48-kat-mtp-vision-nospec` · system | Mismo GGUF/mmproj/KV/contexto de APEX; sin MTP; visión; resto de flags igual a 43. | `official` b10331+; `agent-maximo`. | `complete` | 1/1 · 43,946 s · 121,765 t/s | 19/20 · 702,375 s · 112,482 t/s; `quality/acceptance` | 1/8 · 342,839 s · 116,761 t/s; `quality/acceptance` | `7285ccddb373 / 7285ccddb373 / 7285ccddb373` | El control sin MTP empeora claramente el BCB; descartar como candidato de calidad, conservar como A/B. |
| 45 | `sys-bench-laguna-s-2-1-q2-48gb-64k-b1024` · system | Laguna S 2.1 `UD-Q2_K_XL`; ctx 64k; B1024/U256; KV q4; Flash según base; 48GB; agente del sistema. | `official` mínimo b10087; agente default. | `complete` | 1/1 · 26,565 s · 32,086 t/s | 20/20 · 407,635 s · 35,897 t/s | 4/8 · 1300,133 s · 30,137 t/s; `quality/acceptance` | `3cd7ca48be61 / 3cd7ca48be61 / 3cd7ca48be61` | Candidato de contexto/memoria, no de calidad; muy lento. |
| 46 | `sys-bench-laguna-s-2-1-q2-48gb-32k-official` · system | Laguna S 2.1 Q2; ctx 32k; B512/U64; KV q4; template oficial; thinking off; 48GB. | `official` b10087+; agente default. | `complete` | 1/1 · 135,768 s · 36,923 t/s | 12/20 · 1086,275 s · 40,058 t/s; `quality/acceptance` | 1/8 · 756,053 s · 36,451 t/s; `quality/acceptance` | `2355155f0c50 / 2355155f0c50 / 2355155f0c50` | Template oficial de Laguna no resultó competitivo en calidad. |
| 47 | `sys-laguna-s-2-1-q2-48gb-safe` · system | Laguna S Q2; ctx 64k; B256/U64; KV q4; Flash on; `fit off`; `tensor-split 1,1`; CPU-MoE32; agente default. | `official` mínimo b10087; binario seguro CUDA de la ficha. | `complete` | 1/1 · 72,798 s · 0,969 t/s | 20/20 · 800,694 s · 33,819 t/s | 7/8 · 1597,481 s · 32,321 t/s; `quality/acceptance` | `8753bcdfa063 / 8753bcdfa063 / 8753bcdfa063` | Casi completo, pero lento y aún debajo del umbral 8/8. |

## Perfiles 48–58: antirez Q2/Q4 imatrix

Familia común: `DeepSeek-V4-Flash-Layers37-42Q4KExperts-OtherExpertLayersIQ2XXSGateUp-Q2KDown-AProjQ8-SExpQ8-OutQ8-chat-v2-imatrix-fixed-0731.gguf`, quant híbrido Q2/Q4 imatrix, aproximadamente 90,89 GiB, `tensor-split 1,0`, expertos de capas 37–42 en CUDA1 y resto CPU, `--fit off`, Flash, KV q4 o q8 según fila, sin visión ni MTP, mínimo de catálogo b10228, agente máximo y sampling base salvo reasoning. El cuello observado es el tiempo de prefill/agent y los hard timeouts de HE20, no una falta de memoria simple.

| # | ID / fuente | Configuración diferencial | Binario / agente | Campaña | HE0 — último persistido | HE20 — último persistido | BCB — último persistido | FP `HE0/HE20/BCB` | Lectura y decisión |
|---:|---|---|---|---|---|---|---|---|---|
| 48 | `sys-48-antirez-dsv4-q2q4-0731-16k` | ctx 16k; B2048/U256; KV q4. | `official` mínimo b10228; `agent-maximo`. | `incomplete` | 1/1 · 350,622 s · 0,315 t/s | 0/0 · 1800,417 s; `hard-timeout` | — | `0a02f53d4fbc / 0a02f53d4fbc / —` | El contexto corto no evita el timeout del agente; no seguir escalando sin corregir el cuello. |
| 49 | `sys-48-antirez-dsv4-q2q4-0731-32k-b4096` | ctx 32k; B4096/U512; KV q4. | `official` b10228+; `agent-maximo`. | `incomplete` | 1/1 · 233,675 s · 0,312 t/s | 0/0 · 1800,939 s; `hard-timeout` | — | `564092bc2ac0 / 564092bc2ac0 / —` | No escala a HE20 dentro del límite; descartado para la cola práctica. |
| 50 | `sys-48-antirez-dsv4-q2q4-0731-32k-b8192` | ctx 32k; B8192/U1024; KV q4; stress batch. | `official` b10228+; `agent-maximo`. | `incomplete` | 1/1 · 169,913 s · 0,292 t/s | 0/0 · 1801,113 s; `hard-timeout` | — | `e0fd38794350 / e0fd38794350 / —` | Batch alto no resolvió el cuello; no continuar sin hipótesis nueva. |
| 51 | `sys-48-antirez-dsv4-q2q4-64k` | ctx 64k; B4096/U512; KV q4. | `official` b10228+; `agent-maximo`. | `incomplete`; BCB pendiente | 1/1 · 259,491 s · 0,229 t/s | 20/20 · 1238,143 s · 10,404 t/s | — | `063c025e569d / 063c025e569d / —` | HE20 válido pero no existe BCB comparable en este corte; mantener pendiente, no promover. |
| 52 | `sys-48-antirez-dsv4-q2q4-131k` | ctx 131k; B4096/U1024; KV q4; stress. | `official` b10228+; `agent-maximo`. | `incomplete`; BCB pendiente en campaña | 1/1 · 169,121 s · 0,304 t/s | 20/20 · 1189,709 s · 9,615 t/s | 8/8 · 3107,978 s · 10,548 t/s histórico | `6f655f84b007 / 6f655f84b007 / 95acb74fa0eb` | BCB completo histórico, pero >3100 s y la campaña no repitió BCB con la huella actual. Referencia de límite, no candidato práctico. |
| 53 | `sys-48-antirez-dsv4-q2q4-kv8` | ctx 32k; B4096/U512; KV q8. | `official` b10228+; `agent-maximo`. | `incomplete` | 1/1 · 245,948 s · 0,327 t/s | 0/0 · 1801,501 s; `hard-timeout` | 0/8 · 11,407 s; `infrastructure/agent`, no calidad | `626d98fbdc6f / 626d98fbdc6f / 1ae587eee728` | El BCB 0/8 es transporte; el cuello real sigue siendo HE20 timeout. No promover. |
| 54 | `sys-48-antirez-dsv4-q2q4-kvf16` | ctx 32k; B4096/U512; quant histórico f16 capado por política a KV q8; stress. | `official` b10228+; `agent-maximo`. | `incomplete` | 1/1 · 223,243 s · 0,362 t/s | 0/0 · 1801,066 s; `hard-timeout` | — | `ca93716cc204 / ca93716cc204 / —` | La fila ya no representa KV f16. Timeout persistente; conservar sólo como evidencia de la política de cap. |
| 55 | `sys-48-antirez-dsv4-q2q4-64k-kv8` | ctx 64k; B4096/U1024; KV q8. | `official` b10228+; `agent-maximo`. | `complete` | 1/1 · 156,513 s · 0,331 t/s | 19/20 · 1150,040 s · 10,393 t/s; `quality/acceptance` | 3/8 · 2049,730 s · 9,792 t/s; `quality/acceptance`; histórico 8/8 | `2dc877404a16 / 2dc877404a16 / 2dc877404a16` | La campaña cerró, pero el intento actual es parcial. El 8/8 viejo queda como referencia histórica, no como promoción automática. |
| 56 | `sys-48-antirez-dsv4-q2q4-prefill` | ctx 32k; B8192/U2048; KV q4; variante prefill. | `official` b10228+; `agent-maximo`. | `incomplete` | 1/1 · 124,843 s · 0,272 t/s | 0/0 · 1801,205 s; `hard-timeout` | — | `ef89532f1359 / ef89532f1359 / —` | Optimizar prefill no alcanzó calidad de agente; no avanzar a BCB. |
| 57 | `sys-48-antirez-dsv4-q2q4-32k-reasoning-off` | ctx 32k; B4096/U512; KV q4; reasoning off; resto antirez común. | `official` b10228+; `agent-maximo`; ahora `benchmark=false`, nombre `[retirado 2026-08-28]`. | `incomplete`; retirado | 1/1 · 217,863 s · 0,311 t/s | 0/0 · 1801,300 s; `hard-timeout` | — | `ae9704af5595 / ae9704af5595 / —` | **Descartado y retirado**: HE20 no terminó en 1801,3 s; no ofrece una ruta competitiva de calidad sostenida. |
| 58 | `sys-48-antirez-dsv4-q2q4-32k-reasoning-low` | ctx 32k; B4096/U512; KV q4; reasoning low; resto antirez común. | `official` b10228+; `agent-maximo`; ahora `benchmark=false`, nombre `[retirado 2026-08-28]`. | Cancelado en prompt 5/20; sin cierre de runner; retirado | 1/1 · 270,169 s · 0,356 t/s | Sin score oficial: cancelado tras ~14 min en prompt 5/20. Un JSON persistido auxiliar marca 0/20 `infrastructure/agent` en 23,503 s; no es un score de calidad. | — | `4014a6ac4adb / 49248659f563 / —` | **Descartado y retirado**: incluso HE0 tardó 270 s y HE20 no llegó a terminar; no se justificaba continuar hacia BCB. |

## Perfiles 59–86: no alcanzados

| Rango | Estado | Qué significa |
|---:|---|---|
| 59–86 | `no alcanzado` | La campaña se detuvo intencionalmente después del retiro de 57/58. No se inició servidor, HE0, HE20 ni BCB para estos slots. No hay métrica, hash de corrida ni motivo de descarte por mérito. Deben reaparecer como `pendientes/no ejecutados` en la próxima campaña, salvo que una decisión posterior los retire con evidencia propia. |

No se inventan nombres ni IDs para esos 28 slots: el log fuente sólo registra un inicio de perfil 58 y no emite la expansión de la cola siguiente. La identidad debe tomarse del `launchMenu()` de la misma build en la próxima campaña, porque el catálogo puede cambiar su orden al promocionar, retirar o agregar variantes.

## Mejores perfiles y referencias útiles

Esta tabla separa el mejor resultado histórico de la decisión de promoción actual. Un perfil puede tener BCB 8/8 y aun así ser demasiado lento, depender de un harness específico o mostrar una repetición posterior inferior.

| Rol | Perfil / ID | Modelo, quant y binario | Configuración | Métrica principal | Decisión |
|---|---|---|---|---|---|
| SOL — calidad/contexto | Dynamic V3 DSH medium 160k · `8797a8cf-fea9-46cb-934a-0d62f3ee8ca7` | Qwen3.8 UD-Q4_K_XL, KV q8, llama.cpp b10331 CUDA, MTP2, mmproj RAM. | ctx 160k; B512/U64; temp1; DSH medium; agent-browser. | Histórico BCB 8/8, 54,74 tok/s, E2E 890,1 s; último persistido 7/8. | Mejor referencia de contexto largo, pendiente de repetición apareada antes de elevarla. |
| TERRA — balance | Browser Agent medium · `7d54c7f2-47dd-43df-a608-f67e4d4b027d` | Qwen3.8 UD-Q4_K_XL, MTP3, llama.cpp b10331 CUDA. | ctx131k; B512/U64; KV q4; temp1; Browser Agent; reasoning medium. | Histórico BCB 8/8, 71,51 tok/s, E2E 592,5 s; último persistido 7/8. | Mejor compromiso histórico; la regresión posterior debe quedar visible. |
| LUNA — rápido y usable | ThinkingCap Qwen3.6 MTP4 · `a03e65f5-2f2c-4d45-b67b-4b1270fa2a6c` | Qwen3.6 Q4_K_M, mmproj F16, binaryId `90e90d91-ee33-49d1-963a-fd7c30017e64`. | ctx131k; B512/U64; KV q4; MTP4; visión. | BCB 6/8, 56,84 tok/s y 298,8 s E2E. | Perfil rápido con calidad parcial; conservar como tier de velocidad, no de calidad máxima. |
| Calidad completa simple | Dynamic V3 MTP embebido 131k · `ec212f51-730e-4456-a673-0aba1d1818a8` | Qwen3.8 UD-Q4_K_XL, KV q8, b10331 CUDA. | ctx131k; B512/U64; MTP2 embebido; visión; reasoning off. | BCB 8/8 en 512,519 s; 33,891 tok/s en último persistido. | Mejor ancla reproducible de la campaña para A/B de runtime y sampling. |
| BEST calidad DeepSeek, referencia | `sys-48-dsv4-nospec` / ULTRA-Q 48GB · `sys-ultraq-dsv4-0731-iq3s-48gb` | DeepSeek V4 Flash UD-IQ3_S en 4 shards; official mínimo b10228. | ctx131k; B4096/U1024; KV q4; expertos CPU/CUDA; sin DSpark. | Histórico BCB 8/8, 9,645 tok/s, 2830,574 s; HE0 actual `server-load`; smoke nativo 5,764–6,171 tok/s. | **BEST dentro de la familia por calidad** frente a Fusion (2–4/8); no ganador universal de velocidad y no sustituye la evidencia HE0 pendiente. |
| Mejor Laguna | Laguna S 2.1 100k · `8d0dd2e0-c6c6-41ef-81d6-893c20d2f621` | Laguna Q2_K_XL; official/b10228+; 48GB. | ctx100k; B512/U64; KV q4; fit on; template v24. | BCB 8/8, 32,417 tok/s, 1253,510 s. | Referencia Laguna completa; coste demasiado alto frente a Qwen/Dynamic. |
| Velocidad extrema, no calidad | BigBang MTP · `cbff7c85-2116-4b42-b1b9-485dd33384cc` | BigBang Q4_K_M, mmproj BF16, MTP5; ficha b10262+. | ctx64k; B256/U64; KV q8; temp.70/top-p.08. | HE20 20/20 en 227,398 s; BCB 4/8; 33,727 tok/s. | Útil para throughput y comparación de speculative decoding; no promover como calidad. |

## Descartes, bloqueos y aprendizajes acumulados

| Familia / perfiles | Tipo | Motivo documentado | Acción futura |
|---|---|---|---|
| Antirez reasoning off/low, 57–58 | Retiro por competitividad | HE0 de 217,863/270,169 s; HE20 timeout o cancelación en prompt 5/20; no había calidad HE20 demostrada ni una ruta de BCB razonable. | Quedan fuera de cola activa; conservar evidencia. Sólo reabrir con cambio medible de binario, quant, offload o harness. |
| Antirez q4/q8, 48–56 | Parcial/timeout | Varias configuraciones pasan HE0, pero HE20 cae en hard timeout; 52 logró BCB 8/8 histórico a más de 3100 s y 55 tuvo 3/8 actual frente a 8/8 viejo. | No promover. Reintentar únicamente con hipótesis concreta y timeout/coste aceptable. |
| DeepSeek V4 y ULTRA-Q HE0, 13 y 23–38 | Infraestructura | 17 intentos `0/0` `server-load`; el servidor no dejó una pasada evaluable. Algunas variantes tienen HE20/BCB históricos, pero con huellas diferentes. | Diagnosticar carga, shards, binary mínimo y colocación antes de sacar conclusión de calidad. |
| DeepSeek Fusion, 1/7/10 | Parcial + lento | 8–9 tok/s en HE20/BCB y BCB 3–5/8; perfil 10 además cerró `infra-timeout`. | Mantener como investigación, no como QUALITY práctica. |
| Laguna, 2/8/9/45–47 | Bloqueado o parcial | Sensible a fit, Flash, template y batch; hay HE0 bloqueados o BCB de 1–7/8; sólo el launch 3 completó 8/8, con alto coste. | Usar sólo como referencia de memoria/contexto; no multiplicar variantes sin hipótesis. |
| Qwen Q4/Q5, 11–12 | Calidad parcial | Ambas pasan HE0/HE20, pero BCB 6/8 y 4/8; Q5 no mejora Q4 en esta cadena. | Mantener Q4 como control 24GB; no subir quant por intuición. |
| Dynamic V3, 15–22 | Mejores candidatos | Varias huellas históricas logran 8/8; las últimas persistidas muestran 6–8/8 según harness/contexto. | Repetir ganadores con la misma huella antes de cambiar ranking o promover nuevos perfiles. |
| KAT / APEX, 4/14/43–44 | Velocidad o visión parcial | KAT text-only es rápido con BCB 3/8; APEX valida visión/tools pero queda en 5/8 o 1/8 y HE20 parcial. | Conservar como laboratorio multimodal; falta una promoción de calidad. |

## Qué debe preservarse en cada nueva corrida

Antes de ejecutar un perfil nuevo o reintentar uno de esta tabla, registrar en el JSON y en la fila correspondiente:

1. ID del perfil, modelo, quant, archivo GGUF y hash del modelo/mmproj si existe.
2. `binaryId`, ruta/version de `llama-server`, build de LlamaCode y commit.
3. Contexto, batch, ubatch, threads, GPU layers, `tensor-split`, overrides de expertos/CPU-MoE, Flash, mmap/mlock y KV K/V.
4. MTP/drafter, n máximo aceptado, visión/mmproj, template, reasoning y sampling completo.
5. Agente/harness, suite, semilla, timeout, número de pasada y workspace limpio.
6. `profileConfigFingerprint`, `benchmarkEffectiveArgs`, `vramGpu0Mb`, `vramGpu1Mb`, `vramMb`, `ramMb`, TTFT, TPS y tiempo E2E.
7. `failureKind`, `failureStage`, mensaje de error y si el resultado es evaluable.
8. Decisión: promover, mantener como control, repetir, bloquear por infraestructura, retirar o dejar pendiente.

Un resultado nuevo no sobrescribe el histórico: agrega una huella y una fecha. La promoción sólo cambia cuando el resultado apareado supera el gate y la repetición no revela una regresión como las observadas en varios perfiles Dynamic y antirez.
