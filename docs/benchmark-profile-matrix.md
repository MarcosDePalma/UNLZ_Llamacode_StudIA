# Matriz de perfiles para benchmarks

Snapshot de revisión: 2026-08-28; anexo de campaña DeepSeek nativa: 2026-08-30. Este archivo conserva la identidad y la configuración efectiva de los perfiles medidos, además de los candidatos derivados del catálogo. Los cambios de perfiles deben hacerse con LlamaCode cerrada; luego hay que volver a abrir la app headless y verificar que los argumentos efectivos coincidan con esta captura.

El procedimiento reusable para agregar modelos, binarios, perfiles o harnesses está documentado en el [Manual de benchmarking](benchmark-manual.md). Esta matriz resume resultados; el manual define las condiciones de validez, el orden HE0 → HE20 → BCB y las reglas de promoción para FAST, BALANCED y QUALITY. HE0 es una compuerta dura: si falla, el perfil queda bloqueado para HE20 y BCB hasta investigar la causa raíz y repetir HE0 con resultado válido.

La tabla consolidada vigente de todos los perfiles activos, junto con las
recomendaciones SOL/TERRA/LUNA/METEOR y los casos de uso, está en
[Tabla final de benchmarks y recomendaciones](benchmark-final-table.md).

El [registro detallado de perfiles](benchmark-profile-ledger-2026-08.md)
contiene la tabla fila por fila de la campaña de 86 candidatos: descartes,
motivos, binarios, quantizaciones, configuración efectiva, huellas y métricas
por etapa. Esta matriz conserva el resumen comparativo y no reemplaza ese
inventario.

Política vigente: los pesos del modelo principal y el KV K/V deben ser `q8_0` o
menor. Las variantes históricas con KV `f16` fueron reemplazadas por copias
limitadas a `q8_0`; sus tiempos y scores anteriores no se mezclan con los nuevos.
Los `mmproj` `F16/BF16` se conservan únicamente como proyectores auxiliares de
visión y no representan el quant de los pesos ni del KV.

## Validación KAT Q4/A-B y APEX — 2026-08-28

Esta actualización agrega corridas headless nuevas sin reemplazar las filas
históricas. El control Q4 y su clon A/B usaron el mismo modelo, KV, contexto,
seed, agente y harness; sólo cambiaron `temp`, `top-p` y `min-p`:

| Perfil | Receta efectiva | HE0 (3 pasadas) | HE20 (3 pasadas) | BCB | Lectura |
|---|---|---:|---:|---:|---|
| `sys-48-katcoder-262k` | Q4_K_M, K/V `q8_0`, ctx 262k, B512/U64, fit on, reasoning off, `0.60/0.95/top-k20/min-p0.0` | 3/3, 100%, 0 reparaciones | 2/3 corridas completas; 18/20 primer intento; una corrida terminó 19/20 con `failureKind=quality` | 3/8 en primera pasada, 315,509 s, 84,18 tok/s | Control rápido; no estable como calidad consolidada en esta repetición |
| `51d46758-fd7c-4d3c-8018-23154a2e0062` | Misma huella; `0.30/0.90/top-k20/min-p0.05` | 3/3, 100%, 0 reparaciones | 3/3 finales, 18/20 primer intento en cada una, 1 reparación por corrida | No evaluable: el BCB fue cancelado por contención externa antes de completar el A/B | Mejor cobertura final tras reparación, pero no candidato de calidad sin BCB |

En HE0 el A/B tuvo una mediana warm de `29,08 s` frente a `30,66 s` del
control (aprox. 5,4% mejor). En HE20, en cambio, el control fue aprox. 29,3%
más rápido en la métrica warm disponible (`179,42 s` frente a `253,81 s` del
A/B), con una corrida fallida y dispersión de resultados; por eso no se trata
como una victoria definitiva de velocidad. La huella verificable del control
incluye `--model .../Kwaipilot_KAT-Coder-V2.5-Dev-Q4_K_M.gguf`,
`--ctx-size 262144`, `--batch-size 512`, `--ubatch-size 64`,
`--cache-type-k q8_0 --cache-type-v q8_0`, `--fit on`,
`--temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0`, `--jinja`,
`--skip-chat-parsing`, el template `kat-coder-tools.jinja` y
`--reasoning off`; el clon sólo sustituyó los tres valores de sampling.
La telemetría de las corridas HE20 fue aproximadamente 13,5–14,2 GiB en GPU0,
12,9–13,1 GiB en GPU1 y 26,5–27,2 GiB de RAM.

El BCB del control es un resultado de calidad parcial, no una repetición final:
`BigCodeBench-Hard_8_tems__20260827_225118` produjo 3/8 en primer intento,
sin reparaciones, `failureKind=quality`, 315,509 s y 84,18 tok/s. El A/B no
obtuvo BCB: durante la segunda etapa apareció un `llama-server` ajeno de
DeepSeek en el puerto 8021, con ambas RTX 3090 ocupadas, y se canceló la
corrida propia sin cerrar ni tocar ese proceso. Las filas antiguas que dicen
“BCB infraestructura” corresponden a conexiones cerradas/transportes rotos y
no deben convertirse en cero; la fila histórica 3/8 y este 3/8 parcial son
scores evaluables, pero ninguno habilita promoción frente a los perfiles 8/8.

El perfil APEX exacto sí tiene los artefactos locales
`D:\Models\llamacpp\KAT-Coder-V2.5-Dev-MTP-APEX-GGUF\KAT-Coder-V2.5-Dev-MTP-APEX-i-quality-v2.gguf`
(SHA-256 `F4501B4F54577DA43A1D088BD3B81CE4DF900A553F5407BC5D29BCBE54AF66EF`)
y `mmproj-F16.gguf` (SHA-256
`71F3CBC1F7CC0F30D09D41CFA924C0060827EBC33BF15ACE7E86661E856F0160`). El
smoke confirmó texto, XML KAT (`read_file` sobre `README.md`) e imagen; HE0
fue 1/1, 100%, sin reparaciones, en 90,747 s con MTP2, y el smoke directo
midió 17/22 tokens MTP aceptados (77,27%) y 113,01 tok/s de decode.

| Contexto configurado | Prompt efectivo | Resultado del smoke APEX + mmproj, KV `q8_0` |
|---:|---:|---|
| 32k | 23.508 tokens | estable; PP 960,64; decode 113,88 tok/s; MTP 8/8 |
| 64k | 47.622 tokens | marcador exacto; PP 903,50; decode 103,08 tok/s; MTP 6/8 |
| 131k | 99.371 tokens | marcador exacto; PP 801,32; decode 98,78 tok/s; MTP 8/8 |
| 262k | 199.856 tokens | cargó sin OOM; PP 633,02; decode 73,47 tok/s; MTP 7/8 |
| cerca del límite 262k | 244.505 tokens | sin OOM, pero marcador truncado; decode 39,89 tok/s; MTP 4/4 |

Los 64k y 131k quedan como candidatos experimentales de visión/herramientas;
262k demuestra capacidad de carga, no recuperación de calidad al límite. MTP3
no superó a MTP2 en el smoke agregado (56,9% frente a 63,5% de aceptación y
115,53 frente a 123,62 tok/s predichos). APEX no entra todavía en la tabla de
calidad: faltan HE20 y BCB válidos bajo la misma huella.

## Matriz calidad / velocidad — corte 2026-08-21

Esta matriz usa el último BCB evaluable de cada huella. Calidad es `BCB/8` y
velocidad es el TPS medio de esa etapa. Los `failureKind=infrastructure`,
timeouts, conexiones cerradas o “Hay un turno en curso” no se convierten en
cero de calidad. La cola pendiente reintentada el 2026-08-21 volvió a encontrar
la colisión de turno en los dos primeros perfiles y se canceló antes de
contaminar los otros once resultados.

| Perfil representativo | GGUF / receta | Calidad BCB | Velocidad BCB | Tiempo total HE0+HE20+BCB | Lectura |
| --- | --- | ---: | ---: | ---: | --- |
| Dynamic V3 Browser Agent · medium · 131k | Qwen3.8 UD-Q4, Browser Agent | 8/8 | 71,51 tok/s | 592,5 s | Frontera calidad/velocidad; harness específico |
| Qwen3.8 UD-Q4 · prefix cache · 64k | Qwen3.8 UD-Q4_K_XL | 8/8 | 59,96 tok/s | 934,3 s | Depende de prefijo/cache caliente |
| Dynamic V3 DSH medium · 192k · MTP2 | Qwen3.8 UD-Q4, KV q8 | 8/8 | 55,11 tok/s | 644,7 s | Mejor candidato general de contexto largo |
| Dynamic V3 MTP separado · 131k | Qwen3.8 UD-Q4, MTP | 8/8 | 55,17 tok/s | 655,4 s | Calidad máxima medida y buena velocidad |
| Dynamic V3 DSH medium · 160k · MTP2 | Qwen3.8 UD-Q4, KV q8 | 8/8 | 54,74 tok/s | 890,1 s | Muy equilibrado; menor contexto que 192k |
| Dynamic V3 MTP embebido · 64k | Qwen3.8 UD-Q4, KV q8 | 8/8 | 52,58 tok/s | 1.120,6 s | Calidad máxima con receta local simple |
| ThinkingCap Qwen3.6 · MTP4 | ThinkingCap Q4_K_M | 6/8 | 56,84 tok/s | 298,8 s | Rápido y útil; debajo de Dynamic en calidad |
| Qwen3.8 UD-Q4 · control 131k | Qwen3.8 UD-Q4_K_XL | 7/8 | 39,53 tok/s | 984,9 s | Control conservador; calidad alta, velocidad menor |
| Qwen3.8 UD-Q4 · 48GB · 196k · MTP2 | Qwen3.8 UD-Q4, KV q8 | 8/8 | 43,71 tok/s | 941,9 s | Calidad máxima y contexto largo; más lento |
| Qwen3.8 Q4_K_M · 131k | Qwen3.8 Q4_K_M | 5/8 | 53,83 tok/s | 874,7 s | Control rápido con pérdida de calidad |
| Qwen3.8 Q5_K_M · 131k | Qwen3.8 Q5_K_M | 3/8 | 47,13 tok/s | 635,8 s | La quant más pesada no ganó calidad aquí |
| KAT Coder Q4 | KAT-Coder Q4_K_M | 3/8 | 116,83 tok/s | 600,9 s | Speed-first/text-only; sin visión |
| BigBang MTP reparado | BigBang Q4_K_M | 3/8 | 211,18 tok/s | 670,8 s | Muy veloz; calidad parcial y estabilidad delicada |
| Laguna S dual GPU · 32k | Laguna S Q2_K_XL | 4/8 | 44,33 tok/s | 1.324,6 s | Modelo grande, lento y de calidad parcial |
| DeepSeek Fusion · leloch | DeepSeek V4 Q2/Q4 imatrix | 1–2/8 | 8,57–9,45 tok/s | 1.720–7.169 s | Investigación; no candidato práctico |

### Candidatos mejorados

Son etiquetas operativas, no una afirmación de que todos los harnesses sean
idénticos.

| Tier | Perfil recomendado | Por qué |
| --- | --- | --- |
| **SOL — calidad** | Dynamic V3 DSH medium · 160k · MTP2 | 8/8 BCB, 54,74 tok/s y 890,1 s totales; candidato de calidad/contexto |
| **TERRA — balanceado** | Dynamic V3 Browser Agent medium · 131k | 8/8 BCB, 71,51 tok/s y 592,5 s totales; mejor compromiso general medido |
| **LUNA — velocidad** | ThinkingCap Qwen3.6 · MTP4 | 6/8 BCB, 56,84 tok/s y 298,8 s totales; termina rápido sin ser malo |
| **METEOR — velocidad extrema** | BigBang MTP reparado | 3/8 BCB, 211,18 tok/s y 670,8 s totales; máximo throughput medido, con calidad parcial |

El conjunto oficial de candidatos mejorados queda formado por `Dynamic V3 DSH
medium 160k MTP2` (SOL), `Dynamic V3 Browser Agent medium 131k` (TERRA),
`ThinkingCap Qwen3.6 MTP4` (LUNA) y `BigBang MTP reparado` (METEOR). Los
perfiles secundarios permanecen en la tabla de referencia y no desplazan estos
cuatro nombres en la promoción visible.

## Auditoría por GGUF y variantes 48GB — 2026-08-21

La auditoría del catálogo partió de 81 candidatos `benchmark=true`. Se retiraron
50 variantes de sistema por ausencia de GGUF/runtime/binario, incompatibilidad
repetida, CPU-only, timeout o estancamiento; se conservan IDs, configuraciones y
resultados históricos. Quedan 33 variantes de sistema y 18 perfiles de usuario
con `benchmark=true`; otros 34 perfiles de usuario quedaron anotados con
`benchmark=false`. La clasificación por
velocidad, calidad, tamaño, visión, contexto y caso de uso está en
[`benchmark-ranking-and-use-cases.md`](benchmark-ranking-and-use-cases.md).

La variante `sys-bench-qwen38-udq4-48gb-196k-mtp2-kv8-mmproj-ram` completó
HE0 1/1 en 34,12 s, HE20 20/20 en 370,38 s y BCB 8/8 en 537,39 s, todos con
`failureKind=none` y `timedOut=false`. Usa UD-Q4, visión, MTP2, contexto 196k,
KV q8, batch 512/ubatch 64 y mmproj en RAM; queda promovida como referencia de
contexto largo con visión para 2×RTX 3090.

En Laguna Q2, el control 48 GB con template v24 (`sys-bench-laguna-s-2-1-q2-48gb-32k-v24`)
queda retirado por 1/8 en BCB; se conserva el control oficial y el resultado
histórico para A/B, pero no se vuelve a encolar automáticamente.

La variante `sys-bench-qwen38-q5km-48gb-196k-mtp2-kv8-mmproj-ram` falló durante
HE0 con acceso ilegal de CUDA al cargar el modelo (`HumanEval_1_tems__20260821_110312`)
y quedó `benchmark=false`. La referencia Q5 131k conserva HE0 1/1, HE20 20/20 y
BCB 3/8 con fallo de calidad.

Los renglones históricos de KAT f16, Laguna 24GB sin etapa evaluable, Laguna 100k,
antirez B2048, BigBang base/fast, Qwen3.8 UD-Q4 262k y Qwen3.8 Q5 64k/espejo
quedaron `benchmark=false` en el catálogo. La tabla se conserva para auditoría;
la cola viva debe reconstruirse desde `launchProfilesForMenu()` y la API, no desde
estos renglones históricos.

La prueba adicional de `sys-bench-48-bigbang-fast` se hizo bajo la huella actual:
HE0 cerró 1/1 en 22,34 s (`HumanEval_1_tems__20260821_114336`). HE20 llegó a
`prompt 2/20`, pero `agent_events.jsonl` registró estancamiento semántico y
`hardFailed=true`; se canceló antes de 1.800 s y no se inventó score. BCB quedó
bloqueado por la compuerta HE20 y el perfil pasó a `benchmark=false`. La evidencia
queda en `HumanEval_20_tems__20260821_114525`.

## Inventario vigente por GGUF — 2026-08-21

La cola activa se agrupa por el GGUF lógico, no por el nombre visible del perfil
ni por si la entrada vive en `system_profiles.json` o en `profiles/launches.json`.
El conteo incluye sólo entradas con `benchmark=true`; los perfiles retirados se
conservan en una sección histórica separada.

| GGUF lógico | Sistema activos | Usuario activos | Total activo |
|---|---:|---:|---:|
| `Qwen3.8-27B-UD-Q4_K_XL.gguf` | 22 | 8 Dynamic V3 | 30 |
| `Laguna-S-2.1-UD-Q2_K_XL.gguf` | 3 | 4 | 7 |
| `Qwen3.8-27B-Q4_K_M.gguf` | 5 | 0 | 5 |
| `DeepSeek-V4 Q2/Q4 imatrix` | 1 | 3 | 4 |
| `ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf` | 1 | 1 | 2 |
| `Qwen3.8-27B-Q5_K_M.gguf` | 1 | 0 | 1 |
| `Kwaipilot_KAT-Coder-V2.5-Dev-Q4_K_M.gguf` | 0 | 1 KAT Coder | 1 |
| `endless-frontier_BigBang-v1-Q4_K_M.gguf` | 0 | 1 BigBang | 1 |
| **Total** | **33** | **18** | **51 perfiles activos** |

En los 18 perfiles de usuario están incluidos: KAT Coder, BigBang, cuatro
variantes Laguna, tres variantes DeepSeek Q2/Q4 y ocho variantes Dynamic V3
(MTP separado, MTP2/KV q8, Browser Agent, DSH 160/192k y MTP embebido 64/131k).
El inventario detallado de IDs bundled sigue en el catálogo; los nombres de
usuario se conservan en `profiles/launches.json`.

### Familias conservadas pero fuera de la cola activa

Estas familias existen y deben permanecer en el inventario histórico, aunque no
se suman a los 51 activos:

| Familia | Motivo de quedar fuera de la cola activa |
|---|---|
| KAT APEX-MTP | Experimental; el perfil opt-in `sys-48-katcoder-mtp-vision` está cableado con `mmproj-F16` y los dos GGUF exactos ya están disponibles. HE0 1/1 y los smokes de texto, XML e imagen son válidos; HE20/BCB siguen pendientes. Queda fuera de la cola automática, no descartado por calidad. |
| Dynamic V3 DFlash2 local | El loader falla antes de inferir con `wrong number of tensors; expected 81, got 58` y errores `FGDN_AR`. Es incompatibilidad de arquitectura/backend, no una mala puntuación del modelo. |
| Dynamic V3 DFlash2 vLLM | No es un benchmark local utilizable en Windows nativo: necesita vLLM parcheado, drafter externo y un endpoint Linux/WSL o remoto preparado. Se conserva como experimento externo, separado de llama.cpp. |
| BigBang base | HE0 no produjo un resultado evaluable y no llegó a BCB. |
| BigBang fast | Tuvo un crash CUDA histórico y luego estancamiento en HE20. La reparación 64k/B256/U64 queda como control experimental. |
| DeepSeek Q2/Q4 antirez B2048 | BCB terminó `0/0` por timeout; se conserva el control KV q8 menos agresivo. |
| DeepSeek Q3 / IQ3_S | Las variantes con DSpark quedan retiradas porque no son una ruta confiable en llama.cpp/Windows. Se recuperan los controles sin speculative, con KV/offload separados para volver a medir HE0. |
| Variantes DeepSeek de reparto de expertos | Varias terminaron con OOM, crashes del backend o `0/0`; otras duplican la misma matriz sin una etapa evaluable adicional. |

La exclusión significa `benchmark=false`, no borrado del GGUF ni de sus
resultados. DFlash2-vLLM queda fuera específicamente de la ejecución local en
Windows; podría reactivarse si se ofrece un endpoint externo/Linux reproducible.

Los controles 24 GB Q4/Q6 quedan explícitamente separados: el Q4 cold y las
tres variantes Q6 están retirados del benchmark activo; sólo siguen activos los
dos diagnósticos Q4 de ngram y prefix-cache. No deben interpretarse como
mediciones de throughput comparable.

## Tabla de resultados

Los valores históricos de la tabla siguiente quedan como referencia. La tabla
vigente de la corrida corregida del 2026-08-17 aparece inmediatamente después;
no se mezclan resultados tomados con otra huella de configuración.

## Investigación de estabilidad Laguna/DeepSeek — 2026-08-17

La repetición de Laguna confirmó que el resultado histórico no se puede
reproducir en el estado actual del runtime. El perfil original pasó HE0 1/1 a
las 02:50 con el mismo fingerprint `0ba62d48...`; después de una prueba
experimental con `thinking on`, el servidor comenzó a terminar con
`CUDA error: an illegal memory access` en GPU1. Se repitió con `thinking off`,
daemon reiniciado, limpieza del límite de perfil, `fit off`, Flash Attention
apagado, continuous batching apagado, contexto 65k, b10228 y offload parcial.
El error continuó en Laguna (GPU1, y GPU0 en el offload parcial). Como control,
Qwen3.8 con la misma infraestructura limpia pasó HE0 1/1 en 13,812 s; por eso
la evidencia actual apunta a la combinación Laguna/GGUF/binario CUDA, no a una
contaminación general de la GPU.

La app ahora limpia el límite entre perfiles: detiene el servidor, espera su
salida, mata cualquier `llama-server.exe` residual y deja un margen antes de
cargar el siguiente modelo. Esto evita heredar procesos o VRAM de otra corrida,
pero no oculta un acceso ilegal reproducible. Las copias experimentales de
Laguna se conservan fuera de la fila histórica y permanecen bloqueadas para
HE20/BCB hasta que HE0 vuelva a ser válido. Se agregó la copia
`sys-laguna-s-2-1-q2-48gb-safe`, con ctx=65k, batch/ubatch=256/64, Flash
Attention y fit desactivados, tensor-split 1,1 y 32 expertos en CPU; debe
validarse con HE0 antes de habilitar las etapas siguientes.

La repetición headless de DeepSeek después de una limpieza completa volvió a
pasar HE0 (`1/1`, `69,45 s`, sin crash). En BCB cargó el modelo correctamente,
procesó las ocho tareas y creó los ocho artefactos con el nombre canónico
`solution_BigCodeBench_<id>.py`. La primera repetición obtuvo `1/8` y se
canceló al quedar la reparación sin cambios de workspace. Con el watchdog
nuevo, la segunda repetición obtuvo `2/8` en la primera aceptación y registró
`repair-stagnation` tras `184,996 ms` sin cambios; el resultado persistido es
`2/8`, `failureKind=infrastructure`, `failureStage=agent`, no un BCB final
comparable. Las corridas se conservan en
`benchmark-runs/BigCodeBench-Hard_8_tems__20260817_113521` y
`benchmark-runs/BigCodeBench-Hard_8_tems__20260817_115030`.

### DeepSeek antirez con recetas derivadas de SOL/TERRA/LUNA/METEOR — 2026-08-21

Se probaron dos variantes sobre el GGUF antirez disponible, manteniendo el
reparto de expertos alineado (`tensor-split 1,0`), KV `q8_0`, sampling
conservador y `reasoning off`:

| Variante | Resultado | Tiempo | Diagnóstico |
| --- | --- | ---: | --- |
| B512/U64 | HE0 `0/0` | 225,9 s | `idle-timeout`; prefill observado ~3–5 tok/s, sin primer turno |
| B2048/U256 | HE0 `0/0` | 202,8 s | `idle-timeout`, sin primer turno; cargó sin OOM/crash |

Las dos recetas se conservan como controles experimentales fuera de la cola
activa. La conclusión es negativa pero útil: KV q8 y `reasoning off` no
compensan el coste del offload de expertos; B512/U64 estrangula el prefill y
B2048/U256 tampoco alcanza una respuesta dentro del watchdog. No se asigna
calidad BCB a estos `0/0` porque son timeouts de infraestructura. El GGUF
DeepSeek IQ3_S sí está presente localmente en cuatro shards dentro de
`D:\\Models\\llamacpp\\DeepSeek-V4-Flash-0731-UD-IQ3_S\\UD-IQ3_S`; no se
había ejecutado porque sus perfiles seguían retirados/experimentales, no porque
faltaran los archivos. En esta revisión se habilitaron tres controles IQ3_S:
B8192/U2048 sin speculative, B4096/U1024 sin speculative y la receta dual 48 GB.
Los tres cargaron el GGUF correctamente; el primer lote histórico cayó en `failureKind=infrastructure`
por la carrera de reparación `Hay un turno en curso`. Se corrigió el harness para
usar steering headless al iniciar reparaciones. El control B8192/U2048 se repitió
con la corrección: HE0 llegó a reparación sin el error de turno, pero terminó
`0/1`, 200,693 s, `failureKind=infrastructure`, porque el agente no cambió los
archivos durante el watchdog. Por eso todavía no hay calidad comparable ni se
promueven a SOL/TERRA/LUNA; los perfiles quedan como candidatos experimentales,
no como resultados de calidad.

### Recuperación sin DSpark/DFlash2 — 2026-08-21

Se reactivaron para nueva cola HE0 los perfiles llama.cpp que no dependen de
DFlash2 ni DSpark:

| Familia | Perfiles recuperados | Excepciones mantenidas fuera |
|---|---|---|
| DeepSeek IQ3_S | B8192/U2048 sin speculative, B4096/U1024 sin speculative, MoE43 sin speculative, KV q8 y control K q8/V q4; además el control dual 48 GB y `DeepSeek V4-7-8-26` | `tensor-split 1,1` queda como diagnóstico: corrupción histórica con b10228, pero respuesta exacta en smoke b10331; DSpark por incompatibilidad/resultado no confiable |
| DeepSeek antirez Q2/Q4 | 16k, 32k B4096, 32k B8192, 64k, 131k, KV q8, 64k KV q8 y prefill B8192 | 32k B2048 y los controles B512/B2048 que ya agotaron timeout sin primer turno |

La siguiente tanda debe empezar por HE0 y sólo promover a HE20/BCB los perfiles
que entreguen un resultado evaluable.

Actualización 2026-08-28: las variantes A/B de 32k con `reasoning off` y
`reasoning low` se retiraron de la cola activa. `reasoning off` agotó HE20 por
timeout (1801,3 s, sin score) y `reasoning low` pasó HE0 1/1 pero fue
cancelado en HE20 por latencia no competitiva antes de BCB; sus artefactos se
conservan para auditoría.

### Control de perfiles de agente alternativos

Para separar modelo de agente/harness se repitieron corridas headless sin
modificar los perfiles de inferencia, pasando sólo otro `agentProfileId` al
benchmark. Todas usaron el mismo harness `HumanEval (1 ítems)` para HE0 y
`BigCodeBench-Hard (8 ítems)` para BCB:

| Perfil de inferencia | Agente | HE0 | BCB inicial/final | Tiempo BCB | Diagnóstico |
|---|---|---:|---:|---:|---|
| QUALITY - DeepSeek Fusion leloch | `agent-intermedio` | 1/1, 85,601 s | 2/8 → 2/8 | 997,436 s | Reparó archivos puntuales, pero terminó en `failureKind=infrastructure`, `failureStage=agent`; no es una mejora final comparable. |
| QUALITY - DeepSeek Fusion leloch · VRAM balance | `agent-intermedio` | 1/1, 83,489 s | 1/8 → 1/8 | 1048,769 s | Sin crash CUDA; la reparación quedó estancada. |
| QUALITY - DeepSeek Fusion leloch | `agent-basico` | 1/1, 74,651 s | 1/8 → 1/8 | 1013,900 s | `run_shell` no corrigió los fallos funcionales; la reparación quedó estancada. |
| QUALITY - DeepSeek Fusion leloch | `agent-basico` post-fix | 1/1, 74,715 s | 2/8 → 2/8 | 830,127 s | El watchdog portable cortó la reparación a los 180 s sin cambios reales; ignoró correctamente `agent_events.jsonl`. |
| QUALITY - DeepSeek Fusion leloch | `agent-avanzado` | 1/1, 112,497 s | 3/8 → 4/8 | 1396,871 s total (682,967 s primer pase) | Sin crash CUDA ni cierre de transporte. Reparó 583 y recuperó cuatro tareas evaluables en total; 771, 1019, 139 y 360 siguieron fallando. El watchdog cortó el intento 2/2 tras 180 s sin cambios reales. Resultado no comparable como BCB final. |
| QUALITY - DeepSeek Fusion leloch | `agent-maximo` | No repetido (HE0 ya válido) | 1/8 → 1/8 | 983,893 s total (788,396 s primer pase) | Sin crash CUDA. Sólo aprobó 906; la reparación 1/2 no modificó archivos durante 180 s y fue cortada por el watchdog. Peor que `agent-avanzado`; no es una mejora final comparable. |
| BALANCE - Laguna S 2.1 | `agent-intermedio` | 0/0 | No ejecutado | — | El servidor terminó con `CUDA error: an illegal memory access` en GPU0 durante la carga; HE0 bloquea BCB. |
| BALANCE - Laguna S 2.1 | `agent-avanzado` | 0/0 | No ejecutado | — | El servidor volvió a fallar en `server-load`, antes de iniciar el agente/harness, con `CUDA error: an illegal memory access` en GPU0 usando ctx=100k, batch=512, ubatch=64, tensor-split=1,1; HE0 bloquea BCB. |

La evidencia separa tres capas: (1) Laguna tiene una falla de infraestructura/CUDA
antes del harness; (2) DeepSeek sí produce soluciones evaluables, pero conserva
fallos funcionales concretos en BCB; (3) el agente puede mejorar o reparar
archivos puntuales, pero cambiar `agent-chat` por `agent-intermedio` o
`agent-basico` no elevó el score final. `agent-avanzado` elevó el primer pase
de DeepSeek de 3/8 a 4/8, pero tampoco resolvió todos los contratos funcionales
y terminó en el watchdog de reparación; por lo tanto no convierte el resultado
en un 8/8 ni demuestra una mejora estable del modelo. `agent-maximo` fue peor:
1/8 y también quedó detenido por el watchdog, con mayor latencia. Durante estas
pruebas se detectó y
corrigió además un defecto portable del watchdog: en Windows debía ignorar
`.llamacode\\agent_events.jsonl`, no sólo la variante con `/`.

| Perfil | HumanEval/0 | HumanEval/20 | BigCodeBench/8 | Tiempo HE0 | Tiempo HE20 | Tiempo BCB | TPS HE0 | TPS HE20 | TPS BCB | Visión | Drafter | Quant | Parámetros (B) | Contexto | Configuración | Estado |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|---|---|---|---|
| BALANCE - Qwen3.8 UD-Q4 visión | 1/1 | 20/20 | 7/8 | 12,997 s | 269,96 s | 736,07 s | 56,89 | — | 39,53 | Sí | MTP3 | UD-Q4_K_XL | 27B | 131072 | `launch=sys-qwen38-27b-udq4-131k; backend=sysbe-sys-qwen38-27b-udq4-131k; modelProfile=sysmodel-sys-qwen38-27b-udq4-131k; runtimePreset=sysrt-sys-qwen38-27b-udq4-131k; model=Qwen3.8-27B-UD-Q4_K_XL.gguf; mmproj=mmproj-BF16.gguf; agent=agent-maximo; binary=official,b10331+; runtime=ctx=131072,batch=512,ubatch=64,threads=8,gpuLayers=999,slots=1,cache=q4_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q4_0 --cache-type-v q4_0 --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --predict 4096 --parallel 1 --reasoning off --spec-type draft-mtp --spec-draft-n-max 3 --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/qwen38-tools-fixed.jinja` | HE0 válido; HE20 histórico; BCB válido |
| BALANCE - Qwen3.8 UD-Q4 MTP4 | 1/1 | 20/20 | 3/8 | 13,132 s | 332,12 s | 585,12 s | 57,06 | — | 54,85 | Sí | MTP4 | UD-Q4_K_XL | 27B | 131072 | `launch=sys-bench-qwen38-udq4-mtp4; backend=sysbe-sys-bench-qwen38-udq4-mtp4; modelProfile=sysmodel-sys-bench-qwen38-udq4-mtp4; runtimePreset=sysrt-sys-bench-qwen38-udq4-mtp4; model=Qwen3.8-27B-UD-Q4_K_XL.gguf; mmproj=mmproj-BF16.gguf; agent=agent-maximo; binary=official,b10331+; runtime=ctx=131072,batch=512,ubatch=64,threads=8,gpuLayers=999,slots=1,cache=q4_0,flash=on,cont=on,mmap=on,mlock=off; override=MTP n-max 4 sobre padre MTP3; args=--cache-type-k q4_0 --cache-type-v q4_0 --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --predict 4096 --parallel 1 --reasoning off --spec-type draft-mtp --spec-draft-n-max 4 --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/qwen38-tools-fixed.jinja` | HE0 válido; HE20 válido; BCB calidad |
| FAST - KAT2-Coder-7-8-26 | 1/1 | 20/20 | — | 16,267 s | 307,78 s | 20,87 s | 103,93* | — | 0,00 | No | — | Q4_K_M | 35B-A3B (≈3B activos) | 262144 | `launch=sys-48-katcoder-262k; backend=sysbe-sys-48-katcoder-262k; modelProfile=sysmodel-sys-48-katcoder-262k; runtimePreset=sysrt-sys-48-katcoder-262k; model=Kwaipilot_KAT-Coder-V2.5-Dev-Q4_K_M.gguf; mmproj=ninguno; agent=agent-maximo; binary=official,b10228+; runtime=ctx=262144,batch=2048,ubatch=512,threads=8,gpuLayers=999,slots=1,cache=q8_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q8_0 --cache-type-v q8_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 8192 --parallel 1 --reasoning off --cache-prompt --cache-reuse 512 --split-mode layer --tensor-split 1,1 --skip-chat-parsing --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/kat-coder-tools.jinja` | HE0 revalidado 3/3; BCB infraestructura (`Connection closed`); repetir |
| FAST - KAT-Coder-7-8-26 | 1/1 | 20/20 | — | 13,963 s | 212,69 s | 20,60 s | 113,03 | — | 0,00 | No | — | Q4_K_M | 35B-A3B (≈3B activos) | 262144 | `launch=9dda6bf4-7aae-4806-ba3a-8466bf41e702; backend=d4e4ab3d-1188-444e-b971-5f86fe683eab; modelProfile=b933d0b2-014e-45c0-9558-3936d310e0bb; runtimePreset=8378307f-290b-4d7f-a345-1aef49db938b; model=Kwaipilot_KAT-Coder-V2.5-Dev-Q4_K_M.gguf; mmproj=ninguno; agent=agent-maximo; binary=official,b10228+; runtime=ctx=262144,batch=2048,ubatch=512,threads=8,gpuLayers=999,slots=1,cache=q8_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q8_0 --cache-type-v q8_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 8192 --parallel 1 --reasoning off --cache-prompt --cache-reuse 512 --split-mode layer --tensor-split 1,1 --skip-chat-parsing --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/kat-coder-tools.jinja` | HE0 válido; BCB infraestructura (`Connection closed`); repetir |
| FAST - BigBang · MTP · top-p 0.08 | 1/1 | 20/20 | — | 10,428 s | 136,84 s | 41,42 s | 165,87 | 107,56 | 0,00 | Sí | MTP embebido | Q4_K_M | 35B-A3B (≈3B activos) | 131072 | `launch=cbff7c85-2116-4b42-b1b9-485dd33384cc; backend=b5acf97e-a091-4925-837a-99270c093b38; modelProfile=ae10f4c4-2d39-4fc1-acdc-f16b9c75b0bc; runtimePreset=83cf0d96-d531-43e0-9fed-6a1c407047d0; model=endless-frontier_BigBang-v1-Q4_K_M.gguf; mmproj=mmproj-endless-frontier_BigBang-v1-bf16.gguf; agent=agent-maximo; binary=official,b10262+; runtime=ctx=131072,batch=4096,ubatch=1024,threads=0,gpuLayers=999,slots=1,cache=q8_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q8_0 --cache-type-v q8_0 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --no-context-shift --metrics --no-warmup --jinja --parallel 1 --reasoning on --spec-draft-n-max 5 --spec-type draft-mtp --temp 0.70 --top-p 0.08` | HE0 válido; BCB infraestructura; repetir |
| BALANCE - BigBang MTP | 1/1 | 20/20† | 2/8† | 13,365 s | 207,55 s† | 464,06 s† | 10,72 | 117,58† | 107,45† | Sí | MTP embebido | Q4_K_M | 35B-A3B (≈3B activos) | 65536 | `launch=sys-repair-48-bigbang-mtp-balance; display=24GB - BALANCE - BigBang MTP; backend=sysbe-sys-repair-48-bigbang-mtp-balance; modelProfile=sysmodel-sys-repair-48-bigbang-mtp-balance; runtimePreset=sysrt-sys-repair-48-bigbang-mtp-balance; model=endless-frontier_BigBang-v1-Q4_K_M.gguf; mmproj=24152073-986a-5470-b717-a70861d14883 (heredado); agent=agent-maximo (benchmark agent-chat); binary=official,b10262+; runtime=ctx=65536,batch=256,ubatch=64,threads=0,gpuLayers=999,slots=1,cache=q8_0,flash=on,cont=on,mmap=on,mlock=off; args=--top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --parallel 1 --cache-type-k q8_0 --cache-type-v q8_0 --flash-attn on --reasoning off --spec-draft-n-max 5 --spec-type draft-mtp --temp 0.60 --top-p 0.95` | HE0 corregido y válido sin reparación; HE20/BCB históricos del perfil 131k/Flash off, repetir |
| BALANCE - ThinkingCap Qwen3.6-27B MTP4 | 1/1 | 20/20 | — | 12,922 s | 174,96 s | — | 61,62 | — | — | Sí | MTP4 | Q4_K_M | 27B | 131000 | `launch=a03e65f5-2f2c-4d45-b67b-4b1270fa2a6c; backend=1cd00b04-02bd-41bf-be45-49eb35b0c3cf; modelProfile=423c82fd-50c6-4a2d-b7b4-5c2d168dbd1c; runtimePreset=12b64031-d497-44ad-af3b-6fd2d451ce91; model=ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf; mmproj=8572fc2c-29cd-5caf-b702-4e2b71fb5de3; agent=default del launch; binary=official; runtime=ctx=131000,batch=512,ubatch=64,threads=8,gpuLayers=-1,slots=1,cache=q4_0,flash=on,cont=on,mmap=off,mlock=on; args=--alias thinkingcap-qwen36-27b-q4km-mtp4 --cache-type-k q4_0 --cache-type-v q4_0 --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --cache-ram 32768 --cache-reuse 512 --jinja --threads-batch 8 --predict 4096 --parallel 1 --flash-attn on --ctx-size 131000 --reasoning off --spec-type draft-mtp --spec-draft-n-max 4` | HE0 válido; BCB bloqueado durante reparación; repetir |
| BALANCE - ThinkingCap+MTP-7-8-26 | 1/1 | 20/20 | — | 11,435 s | 197,10 s | 38,24 s | 63,90 | — | 0,00 | Sí | MTP4 | Q4_K_M | 27B | 196608 | `launch=sys-48-thinkingcap-mtp; backend=sysbe-sys-48-thinkingcap-mtp; modelProfile=sysmodel-sys-48-thinkingcap-mtp; runtimePreset=sysrt-sys-48-thinkingcap-mtp; model=ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf; mmproj=fe4ff7eb-f122-53a6-bdb4-fde28253c875; agent=agent-maximo; binary=official,b10228+; runtime=ctx=196608,batch=2048,ubatch=512,threads=8,gpuLayers=999,slots=1,cache=q8_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q8_0 --cache-type-v q8_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 8192 --parallel 1 --reasoning-format auto --cache-prompt --cache-reuse 512 --split-mode layer --tensor-split 1,1 --spec-type draft-mtp --spec-draft-n-max 4` | HE0 válido; BCB infraestructura; repetir |
| BALANCE - Laguna S 2.1 118B-A8B Q2 | 1/1 | 20/20 | — | 16,980 s | 204,16 s | 56,93 s | 53,34 | — | 0,00 | No | — | UD-Q2_K_XL | 118B-A8B (≈8B activos) | 100000 | `launch=8d0dd2e0-c6c6-41ef-81d6-893c20d2f621; backend=b53df8bb-16b9-413d-8649-813e0a70d080; modelProfile=358edb77-0667-4190-b0e1-08654cb13864; runtimePreset=1b670632-3987-4047-be78-3efc93bb60d6; model=Laguna-S-2.1-UD-Q2_K_XL.gguf; mmproj=ninguno; agent=default del launch; binary=official,b10087+; runtime=ctx=100000,batch=2048,ubatch=768,threads=8,gpuLayers=999,slots=1,cache=q4_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q4_0 --cache-type-v q4_0 --fit on --split-mode layer --tensor-split 1,1 --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 4096 --parallel 1 --reasoning-format auto --reasoning-preserve --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/laguna-tools-v24.jinja` | HE0 válido; BCB infraestructura (`Connection closed`); repetir |
| QUALITY - DeepSeek Fusion leloch | 1/1 | 20/20 | — | 70,903 s | 852,31 s | 716,23 s† | 8,53 | 9,15 | 0† | No | — | Q2/Q4 imatrix | 284B (≈13B activos) | 131072 | `launch=4f5cc556-333d-4310-955e-15042cd874d6; backend=1485cb47-757a-4a01-9f71-832567d01973; modelProfile=6ab3222e-5f71-442d-9eb9-7e895520befc; runtimePreset=20d4e6e6-9240-4926-9ea6-5bcea0eb2c50; model=DeepSeek-V4-Flash-Layers37-42Q4KExperts-OtherExpertLayersIQ2XXS...gguf; mmproj=ninguno; agent=agent-maximo (HE20 histórico agent-chat); binary=official,b10228+; runtime=ctx=131072,batch=4096,ubatch=1024,threads=8,gpuLayers=999,slots=1,cache=q4_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q4_0 --cache-type-v q4_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 16384 --parallel 1 --reasoning-format auto --cache-prompt --split-mode layer --tensor-split 1,0 --override-tensor blk\.(37\|38\|39\|40\|41\|42)\.ffn_(gate\|up\|down)_exps\.weight=CUDA1,blk\.[0-9]+\.ffn_(gate\|up\|down)_exps\.weight=CPU --repeat-last-n 64 --flash-attn on --cpu-moe --cache-ram 32768` | HE0 válido; BCB sin cierre evaluable, repetir |
| QUALITY - DeepSeek Fusion leloch · VRAM balance | — | — | — | — | — | — | 8,81 | — | — | No | — | Q2/Q4 imatrix | 284B (≈13B activos) | 131072 | `launch=6b3bf7bd-0889-491a-9b6d-b12128478a5f; backend=07bf242d-0685-45d1-a752-11ddec6ef6df; modelProfile=0985be04-d2bc-455d-a3a5-e5fc19795e5d; runtimePreset=fdd4ca0a-3b8d-43a2-924b-092327aca314; model=DeepSeek-V4-Flash-Layers37-42Q4KExperts-OtherExpertLayersIQ2XXS...gguf; mmproj=ninguno; agent=agent-maximo (HE0 histórico agent-chat); binary=official,b10228+; runtime=ctx=131072,batch=4096,ubatch=1024,threads=8,gpuLayers=999,slots=1,cache=q4_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q4_0 --cache-type-v q4_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 16384 --parallel 1 --reasoning-format auto --cache-prompt --split-mode layer --tensor-split 1,0 --override-tensor blk\.(0\|1)\.ffn_(gate\|up\|down)_exps\.weight=CUDA0,blk\.(37\|38\|39\|40\|41\|42)\.ffn_(gate\|up\|down)_exps\.weight=CUDA1,blk\.[0-9]+\.ffn_(gate\|up\|down)_exps\.weight=CPU --repeat-last-n 64 --flash-attn on --cpu-moe --cache-ram 32768` | Variante conservadora; HE0 histórico válido (67,039 s), requiere repetición con huella actual |

## Corrida vigente headless — 2026-08-17

Esta es la tabla espejo completa de la corrida headless comparable. Los tiempos
están en segundos, los TPS son los reportados por el harness y `Configuración`
conserva IDs, modelo, binario, runtime, flags, harness, suite y huella SHA-256.

| Perfil | HE0 | HE20 | BCB | Tiempo HE0 | Tiempo HE20 | Tiempo BCB | TPS HE0 | TPS HE20 | TPS BCB | Visión | Drafter | Quant | Parámetros (B) | Contexto | Estado | Configuración |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|---|---:|---|---|
| BALANCE - Qwen3.8 UD-Q4 visión | 1/1 | 20/20 | 5/8 | 11,288 s | 237,507 s | 1430,390 s | 39,22 | 60,50 | 65,21 | Sí | MTP3 | UD-Q4_K_XL | 27B | 131072 | BCB calidad | `launch=sys-qwen38-27b-udq4-131k; backend=sysbe-sys-qwen38-27b-udq4-131k; modelProfile=sysmodel-sys-qwen38-27b-udq4-131k; runtimePreset=sysrt-sys-qwen38-27b-udq4-131k; model=Qwen3.8-27B-UD-Q4_K_XL.gguf; mmproj=mmproj-BF16.gguf; binary=official,b10331+; agent=agent-chat; harness=HumanEval/0, HumanEval/20, BigCodeBench-Hard/8; runtime=ctx=131072,batch=512,ubatch=64,threads=8,gpuLayers=999,slots=1,cache=q4_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q4_0 --cache-type-v q4_0 --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --predict 4096 --parallel 1 --reasoning off --spec-type draft-mtp --spec-draft-n-max 3 --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/qwen38-tools-fixed.jinja; fp=0a61a1a7984b74b29076cb919fe8d9b8e96aee70a5bf39aea490e0d5a43065f4` |
| BALANCE - Qwen3.8 UD-Q4 MTP4 | 1/1 | 20/20 | 4/8 | 10,750 s | 262,737 s | 1507,487 s | — | 56,83 | 60,28 | Sí | MTP4 | UD-Q4_K_XL | 27B | 131072 | BCB calidad | `launch=sys-bench-qwen38-udq4-mtp4; backend=sysbe-sys-bench-qwen38-udq4-mtp4; modelProfile=sysmodel-sys-bench-qwen38-udq4-mtp4; runtimePreset=sysrt-sys-bench-qwen38-udq4-mtp4; model=Qwen3.8-27B-UD-Q4_K_XL.gguf; mmproj=mmproj-BF16.gguf; binary=official,b10331+; agent=agent-chat; harness=HumanEval/0, HumanEval/20, BigCodeBench-Hard/8; runtime=ctx=131072,batch=512,ubatch=64,threads=8,gpuLayers=999,slots=1,cache=q4_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q4_0 --cache-type-v q4_0 --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --predict 4096 --parallel 1 --reasoning off --spec-type draft-mtp --spec-draft-n-max 4 --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/qwen38-tools-fixed.jinja; fp=804b8b7813fd9ded9653458d7c4100a0c3132388aa2809c2e810dc3b804f1f74` |
| FAST - KAT2-Coder-7-8-26 | 1/1 | 20/20 | 3/8 | 15,817 s | 183,904 s | 401,169 s | 124,24 | 108,45 | 116,83 | No | — | Q4_K_M | 35B-A3B | 262144 | Reparado; BCB calidad | `launch=sys-48-katcoder-262k; backend=sysbe-sys-48-katcoder-262k; modelProfile=sysmodel-sys-48-katcoder-262k; runtimePreset=sysrt-sys-48-katcoder-262k; model=Kwaipilot_KAT-Coder-V2.5-Dev-Q4_K_M.gguf; mmproj=ninguno; binary=official,b10228+; agent=agent-chat; harness=HumanEval/0, HumanEval/20, BigCodeBench-Hard/8; runtime=ctx=262144,batch=512,ubatch=64,threads=8,gpuLayers=999,slots=1,cache=q8_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q8_0 --cache-type-v q8_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 8192 --parallel 1 --reasoning off --cache-prompt --cache-reuse 512 --split-mode layer --tensor-split 1,1 --skip-chat-parsing --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/kat-coder-tools.jinja; fp=cf312bcfe413161dc8eaf9bdbfa1d979140deaa3ba7cb381cba209747eb501e0` |
| FAST - KAT-Coder-7-8-26 | 1/1 | 20/20 | 3/8 | 17,102 s | 273,407 s | 639,302 s | — | 107,14 | 111,77 | No | — | Q4_K_M | 35B-A3B | 262144 | Reparado; BCB calidad | `launch=9dda6bf4-7aae-4806-ba3a-8466bf41e702; backend=d4e4ab3d-1188-444e-b971-5f86fe683eab; modelProfile=b933d0b2-014e-45c0-9558-3936d310e0bb; runtimePreset=8378307f-290b-4d7f-a345-1aef49db938b; model=Kwaipilot_KAT-Coder-V2.5-Dev-Q4_K_M.gguf; mmproj=ninguno; binary=official,b10228+; agent=agent-chat; harness=HumanEval/0, HumanEval/20, BigCodeBench-Hard/8; runtime=ctx=262144,batch=512,ubatch=64,threads=8,gpuLayers=999,slots=1,cache=q8_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q8_0 --cache-type-v q8_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 8192 --parallel 1 --reasoning off --cache-prompt --cache-reuse 512 --split-mode layer --tensor-split 1,1 --skip-chat-parsing --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/kat-coder-tools.jinja; fp=c8b2aa409c73f03cfcd4a4c524a576453687ca37634e458248018b319deee583` |
| FAST - BigBang MTP | 1/1 | 20/20 | 3/8 | 11,289 s | 222,185 s | 1055,693 s | — | 204,42 | 207,33 | Sí | MTP embebido | Q4_K_M | 35B-A3B | 65536 | BCB calidad | `launch=cbff7c85-2116-4b42-b1b9-485dd33384cc; backend=b5acf97e-a091-4925-837a-99270c093b38; modelProfile=ae10f4c4-2d39-4fc1-acdc-f16b9c75b0bc; runtimePreset=83cf0d96-d531-43e0-9fed-6a1c407047d0; model=endless-frontier_BigBang-v1-Q4_K_M.gguf; mmproj=mmproj-endless-frontier_BigBang-v1-bf16.gguf; binary=official,b10262+; agent=agent-chat; harness=HumanEval/0, HumanEval/20, BigCodeBench-Hard/8; runtime=ctx=65536,batch=256,ubatch=64,threads=0,gpuLayers=999,slots=1,cache=q8_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q8_0 --cache-type-v q8_0 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --parallel 1 --reasoning on --spec-draft-n-max 5 --spec-type draft-mtp --temp 0.70 --top-p 0.08; fp=2b070c48ac1481cffc071ccbd76daa5fcea26d32fb6d55565e2610e7e8d548a6` |
| BALANCE - BigBang MTP (reparado) | 1/1 | 20/20 | 3/8 | 11,266 s | 253,067 s | 406,496 s | — | 206,53 | 211,18 | Sí | MTP embebido | Q4_K_M | 35B-A3B | 65536 | Reparado; BCB calidad | `launch=sys-repair-48-bigbang-mtp-balance; display=24GB - BALANCE - BigBang MTP; backend=sysbe-sys-repair-48-bigbang-mtp-balance; modelProfile=sysmodel-sys-repair-48-bigbang-mtp-balance; runtimePreset=sysrt-sys-repair-48-bigbang-mtp-balance; model=endless-frontier_BigBang-v1-Q4_K_M.gguf; mmproj=24152073-986a-5470-b717-a70861d14883 (heredado); binary=official,b10262+; agent=agent-chat; harness=HumanEval/0, HumanEval/20, BigCodeBench-Hard/8; runtime=ctx=65536,batch=256,ubatch=64,threads=0,gpuLayers=999,slots=1,cache=q8_0,flash=on,cont=on,mmap=on,mlock=off; args=--top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --parallel 1 --cache-type-k q8_0 --cache-type-v q8_0 --flash-attn on --reasoning off --spec-draft-n-max 5 --spec-type draft-mtp --temp 0.60 --top-p 0.95 --presence-penalty 0.0; fp=612645291d06fad1d0773356ab331a8cff651e2406d1edc069f889a674cbc993` |
| BALANCE - ThinkingCap Qwen3.6 MTP4 | 1/1 | 20/20 | 3/8 | 11,288 s | 118,098 s | 169,431 s | 48,21 | 61,96 | 52,04 | Sí | MTP4 | Q4_K_M | 27B | 131000 | BCB calidad | `launch=a03e65f5-2f2c-4d45-b67b-4b1270fa2a6c; backend=1cd00b04-02bd-41bf-be45-49eb35b0c3cf; modelProfile=423c82fd-50c6-4a2d-b7b4-5c2d168dbd1c; runtimePreset=12b64031-d497-44ad-af3b-6fd2d451ce91; model=ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf; mmproj=8572fc2c-29cd-5caf-b702-4e2b71fb5de3; binary=official; agent=agent-chat; harness=HumanEval/0, HumanEval/20, BigCodeBench-Hard/8; runtime=ctx=131000,batch=512,ubatch=64,threads=8,gpuLayers=-1,slots=1,cache=q4_0,flash=on,cont=on,mmap=off,mlock=on; args=--alias thinkingcap-qwen36-27b-q4km-mtp4 --cache-type-k q4_0 --cache-type-v q4_0 --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --cache-ram 32768 --cache-reuse 512 --jinja --threads-batch 8 --predict 4096 --parallel 1 --flash-attn on --ctx-size 131000 --reasoning off --spec-type draft-mtp --spec-draft-n-max 4; fp=3becfb2ddabdb7ea870b02b1106818925cacef6fc74f3bd77bb7dc63148cb3c9` |
| BALANCE - ThinkingCap+MTP | 1/1 | 20/20 | 3/8 | 10,265 s | 217,926 s | 401,922 s | 7,46 | 58,64 | 58,15 | Sí | MTP4 | Q4_K_M | 27B | 131072 | BCB calidad | `launch=sys-48-thinkingcap-mtp; backend=sysbe-sys-48-thinkingcap-mtp; modelProfile=sysmodel-sys-48-thinkingcap-mtp; runtimePreset=sysrt-sys-48-thinkingcap-mtp; model=ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf; mmproj=fe4ff7eb-f122-53a6-bdb4-fde28253c875; binary=official,b10228+; agent=agent-chat; harness=HumanEval/0, HumanEval/20, BigCodeBench-Hard/8; runtime=ctx=131072,batch=512,ubatch=64,threads=8,gpuLayers=999,slots=1,cache=q8_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q8_0 --cache-type-v q8_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 8192 --parallel 1 --reasoning-format auto --cache-prompt --cache-reuse 512 --split-mode layer --tensor-split 1,1 --spec-type draft-mtp --spec-draft-n-max 4; fp=7dcc0f0530b30f8a50a62313df2f58d0fc741c14ba869b1726a7b1e87109089c` |
| BALANCE - Laguna S 2.1 | 1/1 | 20/20 | 0/8 | 13,921 s | 341,545 s | 1041,378 s | 47,84 | 55,35 | 52,62 | No | — | UD-Q2_K_XL | 118B-A8B | 100000 | Reparado; BCB calidad | `launch=8d0dd2e0-c6c6-41ef-81d6-893c20d2f621; backend=b53df8bb-16b9-413d-8649-813e0a70d080; modelProfile=358edb77-0667-4190-b0e1-08654cb13864; runtimePreset=1b670632-3987-4047-be78-3efc93bb60d6; model=Laguna-S-2.1-UD-Q2_K_XL.gguf; mmproj=ninguno; binary=official,b10087+; agent=agent-chat; harness=HumanEval/0, HumanEval/20, BigCodeBench-Hard/8; runtime=ctx=100000,batch=512,ubatch=64,threads=8,gpuLayers=999,slots=1,cache=q4_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q4_0 --cache-type-v q4_0 --fit on --split-mode layer --tensor-split 1,1 --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 4096 --parallel 1 --reasoning-format auto --reasoning-preserve --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/laguna-tools-v24.jinja; fp=0ba62d48dad96884f8953a0499df3ef818b0a63a8285c8501ca1522b6a444d0c` |
| QUALITY - DeepSeek Fusion leloch | 1/1 | 20/20 | 1/8 | 69,794 s | 802,656 s | 2397,063 s | — | 10,35 | 8,57 | No | — | Q2/Q4 imatrix | 284B (≈13B activos) | 131072 | BCB calidad | `launch=4f5cc556-333d-4310-955e-15042cd874d6; backend=1485cb47-757a-4a01-9f71-832567d01973; modelProfile=6ab3222e-5f71-442d-9eb9-7e895520befc; runtimePreset=20d4e6e6-9240-4926-9ea6-5bcea0eb2c50; model=DeepSeek-V4-Flash-Layers37-42Q4KExperts-OtherExpertLayersIQ2XXS...gguf; mmproj=ninguno; binary=official,b10228+; agent=agent-chat; harness=HumanEval/0, HumanEval/20, BigCodeBench-Hard/8; runtime=ctx=131072,batch=4096,ubatch=1024,threads=8,gpuLayers=999,slots=1,cache=q4_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q4_0 --cache-type-v q4_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 16384 --parallel 1 --reasoning-format auto --cache-prompt --split-mode layer --tensor-split 1,0 --override-tensor blk\.(37\|38\|39\|40\|41\|42)\.ffn_(gate\|up\|down)_exps\.weight=CUDA1,blk\.[0-9]+\.ffn_(gate\|up\|down)_exps\.weight=CPU --repeat-last-n 64 --flash-attn on --cpu-moe --cache-ram 32768; fp=63dfc6a4f05df0270ad2935da4a43721b7eb1e8adbdf7c3e2d7bb0e2ccf3225b` |
| QUALITY - DeepSeek Fusion leloch · VRAM balance | 1/1 | 20/20 | 2/8 | 65,622 s | 775,223 s | 6328,761 s | — | 10,76 | 9,45 | No | — | Q2/Q4 imatrix | 284B (≈13B activos) | 131072 | BCB calidad; 2 reparaciones internas | `launch=6b3bf7bd-0889-491a-9b6d-b12128478a5f; backend=07bf242d-0685-45d1-a752-11ddec6ef6df; modelProfile=0985be04-d2bc-455d-a3a5-e5fc19795e5d; runtimePreset=fdd4ca0a-3b8d-43a2-924b-092327aca314; model=DeepSeek-V4-Flash-Layers37-42Q4KExperts-OtherExpertLayersIQ2XXS...gguf; mmproj=ninguno; binary=official,b10228+; agent=agent-chat; harness=HumanEval/0, HumanEval/20, BigCodeBench-Hard/8; runtime=ctx=131072,batch=4096,ubatch=1024,threads=8,gpuLayers=999,slots=1,cache=q4_0,flash=on,cont=on,mmap=on,mlock=off; args=--cache-type-k q4_0 --cache-type-v q4_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 16384 --parallel 1 --reasoning-format auto --cache-prompt --split-mode layer --tensor-split 1,0 --override-tensor blk\.(0\|1)\.ffn_(gate\|up\|down)_exps\.weight=CUDA0,blk\.(37\|38\|39\|40\|41\|42)\.ffn_(gate\|up\|down)_exps\.weight=CUDA1,blk\.[0-9]+\.ffn_(gate\|up\|down)_exps\.weight=CPU --repeat-last-n 64 --flash-attn on --cpu-moe --cache-ram 32768; fp=fa3fbb3f6ef70d2405eb64cc7de9e3737d2c7f47be1c0334e2b52f16b296d789` |
| [CANDIDATO] NInfer-3090 · Qwen3.6-27B · texto | Pendiente | Pendiente | Pendiente | — | — | — | — | — | — | No (app text-only) | MTP2 | groupwise-int | 27B | 4096 | Pendiente HE0 | `launch=sys-ninfer3090-qwen27; backend=sysbe-sys-ninfer3090-qwen27; modelProfile=sysmodel-sys-ninfer3090-qwen27; runtimePreset=sysrt-sys-ninfer3090-qwen27; model=qwen3_6_27b.ninfer; binary=ninfer-serve/flavor ninfer-3090; agent=agent-intermedio; runtime=ctx=4096,batch=512,ubatch=128,kv=int8; args=--model-id qwen3.6-27b --mtp-draft-tokens 2 --lm-head-draft --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --reasoning on` |
| [CANDIDATO] NInfer-3090 · Qwen3.6-35B-A3B · coding | Pendiente | Pendiente | Pendiente | — | — | — | — | — | — | No (app text-only) | MTP2 + prompt-lookup | groupwise-int | 35B-A3B | 4096 | Pendiente HE0 | `launch=sys-ninfer3090-qwen35; backend=sysbe-sys-ninfer3090-qwen35; modelProfile=sysmodel-sys-ninfer3090-qwen35; runtimePreset=sysrt-sys-ninfer3090-qwen35; model=qwen3_6_35b_a3b.ninfer; binary=ninfer-serve/flavor ninfer-3090; agent=agent-avanzado; runtime=ctx=4096,batch=512,ubatch=128,kv=int8; args=--model-id qwen3.6-35b-a3b --mtp-draft-tokens 2 --lm-head-draft --prompt-lookup-auto --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --reasoning on` |
| [CANDIDATO] NInfer-3090 · Qwen3.8-27B · coding | Pendiente | Pendiente | Pendiente | — | — | — | — | — | — | No (app text-only; artefacto con visión) | MTP3 | groupwise-int | 27B | 4096 | Pendiente HE0; BCB requiere validar tool-calls | `launch=sys-ninfer3090-qwen38; backend=sysbe-sys-ninfer3090-qwen38; modelProfile=sysmodel-sys-ninfer3090-qwen38; runtimePreset=sysrt-sys-ninfer3090-qwen38; model=qwen3_8_27b.ninfer; binary=ninfer-serve/flavor ninfer-3090; agent=agent-avanzado; runtime=ctx=4096,batch=512,ubatch=128,kv=int8; args=--model-id qwen3.8-27b --spec mtp --draft-tokens 3 --lm-head-draft --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --reasoning on` |

Los scores BCB menores que 8/8 son resultados válidos de calidad del modelo:
no se repiten ni se “arreglan” cambiando parámetros cuando el transporte, el
grader y el servidor terminaron correctamente. En cambio, las dos fallas HE20
iniciales de KAT y Laguna sí fueron de CUDA/infraestructura; se corrigieron los
presets a `batch=512, ubatch=64`, se repitió HE0 y luego HE20, y ambas quedaron
20/20. DeepSeek VRAM terminó `2/8` tras dos reparaciones internas; el límite de
generación fue del modelo durante una ejecución transportada, no un crash ni un
acceso ilegal de CUDA, por lo que se conserva como calidad.

Auditoría de cobertura de los perfiles cerrados: **11/11 perfiles con HE0, 11/11
con HE20 y 11/11 con BCB** bajo su huella efectiva actual. Los tres candidatos
NInfer incorporados el 2026-08-18 quedan fuera de ese conteo hasta completar
HE0 → HE20 → BCB; no se presentan como resultados medidos. Los scores BCB
parciales se conservan como calidad del modelo y no como infraestructura.

La corrección de la fila BALANCE - BigBang se validó nuevamente en modo headless
después de recompilar el catálogo el 2026-08-17 con
`sys-repair-48-bigbang-mtp-balance`, corrida
`HumanEval_1_tems__20260817_001311`: **1/1 en 13,365 s**, `TPS HE0=10,72`, sin
reparación, crash ni cierre de transporte. La copia conserva MTP embebido, pero
usa `ctx=65536`, `batch=256`, `ubatch=64`, `flash-attn=on`, `KV=q8_0` y
sampling conservador; el perfil histórico 131k/Flash off queda archivado y no
se mezcla con esta medición.

HE0 de la variante actual: `HumanEval_1_tems__20260816_131714`, `1/1`, `67,039 s`, sin reparación ni fallo de infraestructura; `TPS HE0=8,81` es el timing nativo de `llama-server`. La corrida descartada anterior `HumanEval_1_tems__20260816_122210` usó `tensor-split=1,1` y falló al cargar por OOM en CUDA1; no se cuenta como calidad. La corrida histórica `HumanEval_1_tems__20260816_122508` también fue válida (`1/1`, `67,308 s`, `9,20 t/s`).

## Depuración del daemon y reparaciones BigBang (2026-08-16)

La intermitencia tenía dos causas independientes. Primero, las variantes BigBang originales combinaban contexto de 131k, batches altos y Flash Attention desactivado; en el equipo dual RTX 3090 se observaron `resource allocation failed` e `illegal memory access` dentro de CUDA. Segundo, los callbacks de `QProcess` de server/router consultaban el miembro global `m_proc`: durante un crash, una recarga o el teardown del benchmark ese miembro podía ya apuntar a otro proceso o ser nulo. El watchdog también podía relanzar el server mientras el benchmark todavía estaba cerrando la pasada, mezclando dos ciclos de vida.

La corrección en `AppController` captura el `QProcess` concreto con `QPointer` en cada callback, ignora señales tardías de procesos reemplazados y suprime el auto-restart del watchdog mientras el benchmark es dueño del ciclo de vida. En una ejecución manual el watchdog conserva la recuperación automática; durante un benchmark, el crash queda registrado como infraestructura y la pasada no se maquilla con un segundo server.

Los perfiles históricos no se sobrescribieron. Se agregaron copias de reparación con `ctx=65536`, `batch=256`, `ubatch=64`, `cache K/V=q8_0` y Flash Attention activado explícitamente. Las dos variantes MTP conservan `--spec-type draft-mtp --spec-draft-n-max 5`; la variante sin MTP elimina ambos argumentos. Sus IDs y nombres son:

| Perfil de reparación | ID | HE0 pasada 1 / 2 / 3 | Resultado |
|---|---|---:|---|
| REPAIR - BigBang · MTP · 64k · B256/U64 | `sys-repair-48-bigbang-mtp` | 19,673 / 13,090 / 11,463 s | 1/1 en 3/3; sin crash ni reparación |
| BALANCE - BigBang MTP | `sys-repair-48-bigbang-mtp-balance` | 11,480 / 13,056 / 11,468 s | 1/1 en 3/3; sin crash ni reparación |
| REPAIR - BigBang · sin MTP · 64k · B256/U64 | `sys-repair-48-bigbang-base` | 13,047 / 13,045 / 21,641 s | 1/1 en 3/3; sin crash ni reparación |
| FAST - KAT2-Coder-7-8-26 | `sys-48-katcoder-262k` | 19,606 / 14,993 / 16,058 s | 1/1 en 3/3; sin crash del daemon |

La evidencia de esta validación fría está en `benchmark-runs/HumanEval_1_tems__20260816_140058`. La primera pasada de KAT2 necesitó dos reparaciones del agente, pero las tres pasadas cerraron con `1/1` y no hubo `Connection closed`, crash nativo ni nuevo `APPCRASH` de LlamaCode. El último `APPCRASH` de `LlamaCode.exe` observado en Event Viewer corresponde a la ejecución previa, antes del binario recompilado y de la corrección de callbacks.

La reparación de BigBang es deliberadamente conservadora: primero demuestra estabilidad en HE0. Después de esa promoción, corresponde repetir HE20 y recién entonces BCB; no se deben mezclar los scores históricos de los perfiles originales con los de estas copias.

## Procedimiento de benchmarking

El orden es deliberado y se aplica a cada perfil base o candidato, siempre en modo headless y con el mismo harness, agente, semilla y criterios de reparación:

1. **HumanEval/0 (smoketest):** ejecutar una sola tarea. Verifica que el modelo, backend, plantilla, MTP/mmproj y transporte funcionen; registra `Calidad HE0` y `TPS HE0`. Un `server-load`, `server-crash`, `timeout`, conexión cerrada o respuesta sin cierre es un fallo de infraestructura, no calidad cero, y bloquea las etapas siguientes de ese perfil.
2. **HumanEval/20:** sólo después de HE0 válido para la misma configuración efectiva. Ejecutar las 20 tareas para medir calidad del perfil y del harness; registrar score, tiempo total y TPS. Si HE0 falló, no se ejecuta HE20: se investiga, se corrige y se repite HE0.
3. **BigCodeBench/8:** sólo después de HE0 y HE20 válidos —o como repetición explícita de una fila ya marcada— ejecutar las 8 tareas difíciles para medir tool-calls, reparaciones, loops y estabilidad sostenida; registrar score, tiempo total y TPS. Un score bajo con transporte, harness y grader funcionando es una medición válida del modelo y no obliga a cambiar ni repetir el perfil. Sólo un fallo de harness/infraestructura, carga, timeout sin progreso, conexión, crash o `CUDA illegal memory access` obliga a investigar, corregir y repetir BCB después de HE0.

La promoción de un perfil requiere pasar HE0. Un fallo de HE0 exige diagnóstico antes de cualquier HE20/BCB. En HE20 y BCB se separan los resultados válidos de calidad del modelo de las fallas de harness/infraestructura: las primeras se conservan aunque sean bajas; las segundas se corrigen y se repiten. Por eso no se mezclan `0/0` de infraestructura con una puntuación de inteligencia, y todo resultado queda anotado junto con la configuración efectiva usada.

La aplicación aplica esta compuerta en tiempo de ejecución mediante una huella
SHA-256 del comando efectivo: los HE0 históricos sin huella no habilitan HE20;
deben repetirse con el binario/harness actual. La verificación headless quedó
probada el 2026-08-16: solicitar HE20 para `sys-bench-48-kat-f16` sin HE0
compatible devolvió estado bloqueado y no arrancó ningún servidor.

## Revalidación de perfiles modificados por la política q8 — HE0 (2026-08-16)

Después de limitar los pesos y el KV K/V del catálogo a `q8_0` o menor, se
repitió el smoketest en modo headless para los 16 perfiles afectados, incluidos
los perfiles derivados que heredan el runtime del padre. La evidencia queda en
`benchmark-runs/HumanEval_1_tems__20260816_162242`. Estos resultados son una
nueva línea base: no se mezclan con tiempos tomados cuando el perfil usaba
`f16`.

| Perfil | ID | HumanEval/0 | Tiempo HE0 | TPS HE0 del agente | Estado |
|---|---|---:|---:|---:|---|
| `[bench 48GB KAT] KV q8_0 · 262k` | `sys-bench-48-kat-f16` | 1/1 | 16,267 s | 127,43 | Válido |
| `[bench antirez stress] 32k · B4096 · U512 · KV q8_0` | `sys-48-antirez-dsv4-q2q4-kvf16` | 1/1 | 75,781 s | — | Válido; sin tokens medibles del agente |
| `Fable Fusion Qwen3.6-27B Q6 · MTP · visión` | `sys-48-fablefusion-q6-mtp` | 1/1 | 11,419 s | 8,62 | Válido |
| `Fable Q6 · MTP 1 · 120k` | `sys-bench-48-fable-mtp1` | 1/1 | 12,871 s | 2,91 | Válido |
| `Fable Q6 · MTP 3 · 120k` | `sys-bench-48-fable-mtp3` | 1/1 | 10,315 s | — | Válido; sin tokens medibles del agente |
| `Fable Q6 · KV q8/q8 · MTP 3` | `sys-bench-48-fable-kv-q8` | 1/1 | 11,342 s | 35,37 | Válido |
| `Fable Q6 · sin MTP · 120k` | `sys-bench-48-fable-nospec` | 1/1 | 15,261 s | 16,39 | Válido |
| `BigBang-v1 35B-A3B Q4_K_M · visión` | `sys-48-bigbang-v1-q4km` | 1/1 | 10,349 s | 29,02 | Válido |
| `BigBang · 131k · sin MTP · KV q8_0` | `sys-bench-48-bigbang-base` | 1/1 | 11,761 s | — | Válido; sin tokens medibles del agente |
| `BigBang · 131k · MTP embebido · KV q8_0` | `sys-bench-48-bigbang-mtp` | 1/1 | 11,313 s | — | Válido; sin tokens medibles del agente |
| `BigBang · 131k · MTP · B1024/U256` | `sys-bench-48-bigbang-fast` | 1/1 | 11,291 s | — | Válido; sin tokens medibles del agente |
| `BigBang · 196k · MTP · KV q8_0` | `sys-bench-48-bigbang-long` | 0/1 | 15,260 s | 0,66 | Infraestructura: `Connection closed` y transporte sin cierre evaluable; repetir |
| `FAST - BigBang · MTP · top-p 0.08` | `sys-bench-48-bigbang-post` | 1/1 | 11,306 s | — | Válido; sin tokens medibles del agente |
| `REPAIR - BigBang · MTP · 64k · B256/U64` | `sys-repair-48-bigbang-mtp` | 1/1 | 11,283 s | 46,43 | Válido |
| `REPAIR - BigBang · sin MTP · 64k · B256/U64` | `sys-repair-48-bigbang-base` | 1/1 | 18,379 s | — | Válido; sin tokens medibles del agente |
| `BALANCE - BigBang MTP` | `sys-repair-48-bigbang-mtp-balance` | 1/1 | 12,851 s | 16,46 | Válido |

`TPS HE0 del agente` es el `avgTps` reportado por el harness para esta tanda;
cuando la respuesta fue evaluable pero no incluyó tokens de generación, se
deja `—` y no se inventa un throughput. La variante BigBang de 196k queda
fuera de la promoción hasta repetirla con transporte completamente cerrado.

La tanda HE20 posterior se canceló a pedido antes de iniciar BCB. En
`benchmark-runs/HumanEval_20_tems__20260816_163945` sólo alcanzó a completarse
`sys-bench-48-kat-f16`: `20/20` en `137,29 s`; los otros 15 perfiles quedaron
pendientes. Este resultado HE20 es válido como medición del perfil, pero no
promueve por sí solo a los restantes ni reemplaza sus resultados históricos.

## Candidatos derivados para medir

No se duplican manualmente los perfiles base. Se incorporan al plan las variantes ya existentes en el catálogo y tres variantes conservadoras de Laguna, incluida la copia CUDA safe. Cada fila sigue HE0 → HE20 → BCB; el HE0 de esta tanda ya fue ejecutado en modo headless y sus resultados están debajo.

| Candidato | Derivado de | Hipótesis | Cambio controlado | Orden |
|---|---|---|---|---|
| `[bench 16GB] Qwen3.8 Heretic RVN-IQ3_XXS · ngram-mod · 131k` (`sys-bench-16-qwen38-rvn-iq3xxs-ngram-131k`) | Post LocalLLaMA adjunto · RTX 4080 16GB | Medir el control de contexto largo sin drafter separado | RVN-IQ3_XXS; ctx 131k; KV q5_1; ngram 4/8/32; `reasoningEffort=medium` | HE0 → HE20 → BCB |
| `[bench 16GB] Qwen3.8 Heretic RVN-IQ3_XXS · DFlash2+ngram · 105k` (`sys-bench-16-qwen38-rvn-iq3xxs-dflash2-ngram-105k`) | Post LocalLLaMA adjunto · RTX 4080 16GB | Medir si el draft DFlash2 compensa su costo a contexto largo | draft Q4_K_M; 5 tokens; ngram 4/8/32; ctx 105k; build beellama/dflash2 | HE0 → HE20 → BCB |
| `[bench 16GB] Qwen3.8 Heretic RVN-IQ3_XXS-MTP · MTP+ngram · 105k` (`sys-bench-16-qwen38-rvn-iq3xxs-mtp-ngram-105k`) | Post LocalLLaMA adjunto · RTX 4080 16GB | Comparar el MTP fusionado contra DFlash2 y ngram-only | GGUF MTP fusionado manualmente; 2 tokens; ngram 4/8/32; ctx 105k; `manualOnly` | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · MTP2 · 64k · mmproj` (`sys-bench-qwen38-udq4-mtp2-64k`) | BALANCE - Qwen3.8 UD-Q4 visión | Menos contexto y MTP pueden mejorar velocidad/estabilidad | ctx 65k; MTP2; B512/U64 | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · MTP3 · B1024 · mmproj` (`sys-bench-qwen38-udq4-mtp3-b1024`) | BALANCE - Qwen3.8 UD-Q4 visión | Menor batch puede evitar fallos de infraestructura | B1024/U128; MTP3 | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · MTP3 · 131k · KV q8 · mmproj` (`sys-bench-qwen38-udq4-mtp3-kv8`) | BALANCE - Qwen3.8 UD-Q4 visión | KV q8 puede sostener mejor contexto y calidad | KV K/V q8_0 | HE0 → HE20 → BCB |
| `[bench Qwen3.8] Q4_K_M · MTP4 · 131k · mmproj` (`sys-bench-qwen38-q4km-mtp4`) | Qwen3.8-27B Q4_K_M visión | Comparar MTP4 sin cambiar quant/contexto | MTP4 | HE0 → HE20 → BCB |
| `[bench Qwen3.8] Q5_K_M · MTP3 · 64k · KV q8 · mmproj` (`sys-bench-qwen38-q5km-mtp3-64k-kv8`) | Qwen3.8-27B Q5_K_M visión | Más precisión/KV puede mejorar BCB a costa de velocidad | ctx 65k; KV q8_0 | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · MTP3 + ngram · 131k · mmproj` (`sys-bench-qwen38-udq4-mtp3-ngram`) | BALANCE - Qwen3.8 UD-Q4 visión | Medir si ngram mejora repetición/TPS sin cambiar modelo, quant ni contexto | `draft-mtp,ngram-mod`; match 24; min 16; max 64 | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · espejo post · 160,9k · MTP2 · B2048/U512` (`sys-bench-qwen38-udq4-post-mirror-160k`) | BALANCE - Qwen3.8 UD-Q4 visión | Reproducir el load screen del post en llama.cpp manteniendo el mismo modelo/template | ctx 160927; MTP2; B2048/U512; KV q4_0 | HE0 → HE20 → BCB |
| `[bench Qwen3.8] Q4_K_M · espejo post · 160,9k · MTP2 · B2048/U512` (`sys-bench-qwen38-q4km-post-mirror-160k`) | Qwen3.8-27B Q4_K_M visión | Separar el efecto de la quant del modelo del resto de la receta espejo | ctx 160927; MTP2; B2048/U512; KV q4_0 | HE0 → HE20 → BCB |
| `[bench Qwen3.8] Q5_K_M · espejo post · 160,9k · MTP2 · B2048/U512` (`sys-bench-qwen38-q5km-post-mirror-160k`) | Qwen3.8-27B Q5_K_M visión | Medir si el margen de calidad de Q5 compensa el costo de VRAM a igual receta | ctx 160927; MTP2; B2048/U512; KV q4_0 | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · MTP3 · 262k · B2048/U512 · KV q8` (`sys-bench-qwen38-udq4-post-262k-kv8`) | BALANCE - Qwen3.8 UD-Q4 visión | Medir el extremo de contexto largo con KV q8, equivalente al control de comentarios del post | ctx 262k; MTP3; B2048/U512; KV q8_0 | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · MTP3 · B8192/U512 · 131k` (`sys-bench-qwen38-udq4-post-b8192`) | BALANCE - Qwen3.8 UD-Q4 visión | Aislar el batch total alto del post sin cambiar modelo ni contexto | ctx 131k; MTP3; B8192/U512; KV q4_0 | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · MTP3 · 2 slots · 131k` (`sys-bench-qwen38-udq4-post-parallel2`) | BALANCE - Qwen3.8 UD-Q4 visión | Medir throughput agregado con dos solicitudes concurrentes; no usar para comparar decode de una sola solicitud | ctx 131k; MTP3; 2 slots; KV q4_0 | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · tensor split · 131k · MTP3` (`sys-bench-qwen38-udq4-post-tensor`) | BALANCE - Qwen3.8 UD-Q4 visión | Medir `split-mode tensor` contra el control `layer` en dos GPUs | `--split-mode tensor`; `--tensor-split 1,1` | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · mmproj en RAM · 131k` (`sys-bench-qwen38-udq4-post-mmproj-cpu`) | BALANCE - Qwen3.8 UD-Q4 visión | Liberar VRAM del proyector sin cambiar los pesos | `--no-mmproj-offload`; validar visión aparte | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · cache warm · 131k · MTP3` (`sys-bench-qwen38-udq4-post-cache-warm`) | BALANCE - Qwen3.8 UD-Q4 visión | Medir prefijos repetidos con prompt cache | `--cache-prompt`; `--cache-reuse 512`; no mezclar con cold-cache | HE0 → HE20 → BCB |
| `[bench 48GB] Qwen3.8 Uncensored Q8 · 196k · visión` (`sys-48-qwen38-27b-q8-196k`) | Fuente JonathanColetti Qwen3.8 Uncensored | Comparar calidad Q8 en 48 GB bajo la política reproducible del repo | Q8_0; ctx 196k; B2048/U256; KV q8_0; MTP3 | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q8] tensor split · 196k · MTP3` (`sys-bench-48-qwen38-q8-tensor`) | Qwen3.8 Uncensored Q8 | Reproducir el eje multi-GPU del post | `--split-mode tensor`; `--tensor-split 1,1` | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q8] mmproj en RAM · 196k` (`sys-bench-48-qwen38-q8-mmproj-cpu`) | Qwen3.8 Uncensored Q8 | Medir margen de VRAM con visión auxiliar en RAM | `--no-mmproj-offload`; validar visión aparte | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q8] cache warm · 196k · MTP3` (`sys-bench-48-qwen38-q8-cache-warm`) | Qwen3.8 Uncensored Q8 | Medir el escenario de prefijo repetido del post | `--cache-prompt`; `--cache-reuse 512`; sólo warm-cache | HE0 → HE20 → BCB |
| `[bench 48GB] Qwen3.8 UD-Q6_K_XL · 96k · MTP2 · visión` (`sys-48-qwen38-27b-q6-96k`) | Qwen3.8 UD-Q6_K_XL de Unsloth | Control base de la receta Q6 del post en llama.cpp | ctx 96k; MTP2; B512/U64; KV q4_0; layer 1,1 | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q6] MTP3 · 96k · visión` (`sys-bench-48-qwen38-q6-mtp3-96k`) | Qwen3.8 UD-Q6_K_XL | Comparar MTP3 contra MTP2 | MTP3 | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q6] MTP4 · 96k · visión` (`sys-bench-48-qwen38-q6-mtp4-96k`) | Qwen3.8 UD-Q6_K_XL | Medir el techo de MTP antes de promoverlo | MTP4 | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q6] MTP2 · 64k · visión` (`sys-bench-48-qwen38-q6-mtp2-64k`) | Qwen3.8 UD-Q6_K_XL | Separar presión de KV/prefill | ctx 65k; MTP2 | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q6] MTP2 · 131k · visión` (`sys-bench-48-qwen38-q6-mtp2-131k`) | Qwen3.8 UD-Q6_K_XL | Medir contexto largo con la misma quant | ctx 131k; MTP2 | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q6] MTP2 · 96k · KV q8` (`sys-bench-48-qwen38-q6-mtp2-kv8`) | Qwen3.8 UD-Q6_K_XL | Medir calidad/estabilidad de KV q8 | KV K/V q8_0 | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q6] MTP2 · 96k · B2048/U512` (`sys-bench-48-qwen38-q6-mtp2-b2048`) | Qwen3.8 UD-Q6_K_XL | Aislar el batch alto del post | B2048/U512 | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q6] espejo post · 96k · MTP2 · B2048/U512 · mmproj RAM` (`sys-bench-48-qwen38-q6-post-mirror-96k`) | Qwen3.8 UD-Q6_K_XL | Reproducir la receta del post con equivalentes llama.cpp | ctx 96k; MTP2; B2048/U512; `--no-mmproj-offload` | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q6] mmproj en RAM · 96k · MTP2` (`sys-bench-48-qwen38-q6-mmproj-cpu`) | Qwen3.8 UD-Q6_K_XL | Medir margen de VRAM del proyector | `--no-mmproj-offload`; validar visión aparte | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q6] tensor split · 96k · MTP2` (`sys-bench-48-qwen38-q6-tensor`) | Qwen3.8 UD-Q6_K_XL | Comparar reparto tensor contra layer | `--split-mode tensor`; `--tensor-split 1,1` | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q6] cache warm · 96k · MTP2` (`sys-bench-48-qwen38-q6-cache-warm`) | Qwen3.8 UD-Q6_K_XL | Medir prefijos repetidos | `--cache-prompt`; `--cache-reuse 512`; sólo warm-cache | HE0 → HE20 → BCB |
| `[bench 48GB Qwen3.8 Q6] reasoning on · 96k · MTP2` (`sys-bench-48-qwen38-q6-reasoning-on`) | Qwen3.8 UD-Q6_K_XL | Medir efecto de thinking en tool-calls | `--reasoning on` | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · thinking low · 131k · MTP3 · visión` (`sys-bench-qwen38-udq4-reasoning-low`) | BALANCE - Qwen3.8 UD-Q4 visión | A/B exacto del nivel mínimo de thinking contra el control `reasoning off` | `--reasoning low`; resto idéntico al control | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · thinking medium · 131k · MTP3 · visión` (`sys-bench-qwen38-udq4-reasoning-medium`) | BALANCE - Qwen3.8 UD-Q4 visión | A/B exacto del nivel medio de thinking contra el control `reasoning off` | `--reasoning medium`; resto idéntico al control | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · thinking xhigh · 131k · MTP3 · visión` (`sys-bench-qwen38-udq4-reasoning-xhigh`) | BALANCE - Qwen3.8 UD-Q4 visión | A/B exacto del nivel máximo disponible de thinking contra el control `reasoning off` | `--reasoning xhigh`; resto idéntico al control | HE0 → HE20 → BCB |
| `[bench Qwen3.8] UD-Q4 · Browser Agent · thinking off · 131k` (`sys-bench-qwen38-udq4-browser-agent-off`) | BALANCE - Qwen3.8 UD-Q4 visión | Control del harness Browser Agent nativo contra Máximo/off | `agent-browser`; `core + web + browser`; skills portables off | HE0 → HE20 → BCB + suite browser |
| `[bench Qwen3.8] UD-Q4 · Browser Agent · thinking low · 131k` (`sys-bench-qwen38-udq4-browser-agent-low`) | BALANCE - Qwen3.8 UD-Q4 visión | Medir el primer nivel de thinking dentro del harness web | `agent-browser`; `--reasoning low` | HE0 → HE20 → BCB + suite browser |
| `[bench Qwen3.8] UD-Q4 · Browser Agent · thinking medium · 131k` (`sys-bench-qwen38-udq4-browser-agent-medium`) | BALANCE - Qwen3.8 UD-Q4 visión | Medir el punto medio de calidad/latencia en navegación | `agent-browser`; `--reasoning medium` | HE0 → HE20 → BCB + suite browser |
| `[bench Qwen3.8] UD-Q4 · Browser Agent · thinking xhigh · 131k` (`sys-bench-qwen38-udq4-browser-agent-xhigh`) | BALANCE - Qwen3.8 UD-Q4 visión | Contrastar la configuración prioritaria del reporte externo | `agent-browser`; `--reasoning xhigh` | HE0 → HE20 → BCB + suite browser |
| `[bench Qwen3.8] UD-Q4 · Artifacts local/privado · 131k` (`sys-bench-qwen38-udq4-artifact-local`) | BALANCE - Qwen3.8 UD-Q4 visión | Medir creación, validación y stash local de un artefacto sin red | `agent-artifact-local`; pack core; MCP/web/browser off | `artifact_lifecycle_v1` |
| `[bench Qwen3.8] UD-Q4 · Artifacts publicación aprobada · 131k` (`sys-bench-qwen38-udq4-artifact-publisher`) | BALANCE - Qwen3.8 UD-Q4 visión | Medir el mismo flujo con web/browser disponibles sin publicar durante la preparación | `agent-artifact-publisher`; core + web + browser; approval ask | `artifact_lifecycle_v1` |
| `[bench 24GB Qwen3.8] UD-Q4 · fast · MTP4 · 64k · mmproj RAM` (`sys-bench-qwen38-udq4-24gb-fast-mtp4-64k`) | BALANCE - Qwen3.8 UD-Q4 visión | Variante de baja presión para una sola RTX 3090/24GB | ctx 65k; MTP4; `--no-mmproj-offload`; KV q4_0 | HE0 → HE20 → BCB |
| `[bench 24GB Qwen3.8] UD-Q4 · lookup aproximado · 64k · MTP3` (`sys-bench-qwen38-udq4-24gb-lookup-64k`) | BALANCE - Qwen3.8 UD-Q4 visión | Aproximar lookup-augmented drafting con la tool disponible en llama.cpp | ctx 65k; MTP3 + `ngram-mod`; KV q4_0 | HE0 → HE20 → BCB |
| `[bench 24GB Qwen3.8] UD-Q4 · prefix cache · 64k · MTP3` (`sys-bench-qwen38-udq4-24gb-prefix-cache-64k`) | BALANCE - Qwen3.8 UD-Q4 visión | Medir turnos repetidos con cache de prefijo en un perfil 24GB | ctx 65k; MTP3; `--cache-prompt`; `--cache-reuse 512` | HE0 → HE20 → BCB |
| `[bench 24GB] Q4_K_M · tg128 · cold · sin speculative` (`sys-bench-qwen38-q4km-24gb-tg128`) | Control Qwen3.8 Q4_K_M 24GB Turing | Reproducir la receta del reporte sin MTP, ngram, cache de prefijo ni mmproj | `-ngl 99`; Flash on; ctx 32k; B512/U512; KV q4_0; `parallel=1`; thinking off | `llama-bench tg128` / HE0 separado |
| `[bench 24GB] Q6_K · tg128 · cold · sin speculative` (`sys-bench-qwen38-q6k-24gb-tg128`) | Control Qwen3.8 Q6_K 24GB Turing | Medir el costo de subir Q4 a Q6 manteniendo la misma receta | `-ngl 99`; Flash on; ctx 32k; B512/U512; KV q4_0; `parallel=1`; thinking off | `llama-bench tg128` / HE0 separado |
| `[diagnóstico 24GB] Q4/Q6 · ngram o prefix warm` | Controles Q4/Q6 24GB Turing | Detectar el tipo de falso throughput descrito en el reporte; nunca rankear contra cold | Variantes `*-ngram-diagnostic` y `*-prefix-warm`; reportar como grupo separado | Diagnóstico, no score comparable |
| `[bench 12GB] Ling 3.0 Tiny Q6 · auxiliar · 131k` (`sys-ling30-tiny-q6-131k`) | Ling 3.0 Tiny Q6 | Medir el auxiliar thinking-off para compresión, resumen y tareas no críticas | Q6_K; ctx 131k; KV q8_0; `enable_thinking=false` | HE0 + suite auxiliar |
| `[bench Ling 3.0 Tiny] Q6 · auxiliar · 64k` (`sys-bench-ling30-tiny-q6-64k`) | Ling 3.0 Tiny Q6 | Separar coste de KV/contexto del coste de la arquitectura | ctx 65k; KV q8_0; `enable_thinking=false` | HE0 + suite auxiliar |
| `[bench Ling 3.0 Tiny] Q6 · thinking on · 131k` (`sys-bench-ling30-tiny-q6-thinking-131k`) | Ling 3.0 Tiny Q6 | A/B del razonamiento nativo frente al uso auxiliar rápido | Q6_K; ctx 131k; KV q8_0; `enable_thinking=true` | HE0 + suite auxiliar |
| `[bench Ling 3.0 Tiny] Q6 · auxiliar · KV q4 · 131k` (`sys-bench-ling30-tiny-q6-kv4-131k`) | Ling 3.0 Tiny Q6 | Medir ahorro de memoria contra posible pérdida semántica | Q6_K; ctx 131k; KV q4_0; `enable_thinking=false` | HE0 + suite auxiliar |
| `[bench 8GB] Ling 3.0 Tiny UD-Q4 · auxiliar · 64k` (`sys-bench-ling30-tiny-udq4-64k`) | Ling 3.0 Tiny UD-Q4 | Punto de baja memoria para compresión/resumen | UD-Q4_K_XL; ctx 65k; KV q4_0; `enable_thinking=false` | HE0 + suite auxiliar |
| `[bench hybrid 24GB] Ling Tiny planifica → Qwen3.8 ejecuta` (`sys-hybrid-ling30-qwen38`) | Qwen3.8 UD-Q4 visión | Medir si el planificador ligero reduce wall-time sin bajar calidad | Ling Q6 planifica sin tools; Qwen3.8 UD-Q4/MTP3 ejecuta; swap secuencial | HE0 → HE20 → BCB |
| `[bench 48GB KAT] KV q8_0 · 262k (cap de política)` (`sys-bench-48-kat-f16`) | FAST - KAT-Coder-7-8-26 | Repetir la variante histórica con la cota vigente | KV K/V q8_0 | HE0 → HE20 → BCB |
| `EXPERIMENTAL - KAT3-Coder-7-8-26 · APEX-MTP · 262k` (`sys-kat3-mtp-262k`) | FAST - KAT2-Coder-7-8-26 | Probar MTP embebido APEX sin alterar KAT2; texto-only | APEX I-Compact; ctx 262k; B512/U64; KV q8_0; MTP4; requiere descargar el GGUF | HE0 → HE20 → BCB |
| `[experimental 48GB] KAT APEX-MTP + Qwen mmproj · 32k` (`sys-48-katcoder-mtp-vision`) | KAT APEX-MTP + mmproj Qwen3.6 | Separar compatibilidad de visión y aceleración MTP sobre el trunk fine-tuneado | GGUF comunitario I-Quality-v2 + `mmproj-F16`; ctx 32k; KV q8_0; MTP2; b10331+ | Texto → imagen sin MTP → imagen + MTP2/MTP3 |
| `[bench 48GB] KAT APEX-MTP + visión · MTP3 · 32k` (`sys-bench-48-kat-mtp-vision-mtp3`) | KAT APEX-MTP + mmproj Qwen3.6 | Medir si MTP3 supera a MTP2 sin alterar la huella multimodal | Misma huella; MTP3; `parallel=1` | HE0 → HE20 → BCB |
| `[bench 48GB] KAT APEX-MTP + visión · sin MTP · 32k` (`sys-bench-48-kat-mtp-vision-nospec`) | KAT APEX-MTP + mmproj Qwen3.6 | Controlar visión con el cabezal MTP desactivado | Misma huella; sin speculative; `parallel=1` | Smoke de imagen → HE0 |
| `[bench BigBang] 131k · MTP · batch 1024 · ubatch 256` (`sys-bench-48-bigbang-fast`) | FAST - BigBang MTP | Mantener MTP y bajar presión de prefill para corregir `Connection closed` | B1024/U256; MTP5 | HE0 → HE20 → BCB |
| `[bench BigBang] 131k · sin MTP · KV q8_0` (`sys-bench-48-bigbang-base`) | BALANCE - BigBang MTP | Aislar si el fallo pertenece al MTP o al harness/modelo | MTP desactivado; KV q8_0 | HE0 → HE20 → BCB |
| `[bench 48GB MAX-Q] MTP4 · 131k · visión` (`sys-bench-48-tc-mtp-131k`) | BALANCE - ThinkingCap Qwen3.6 MTP4 | Mantener MTP4 y reducir contexto para evitar bloqueo sostenido | ctx 131k; MTP4 | HE0 → HE20 → BCB |
| `[bench Laguna] Q2 · 64k · B1024 · U256` (`sys-bench-laguna-s-2-1-q2-48gb-64k-b1024`) | BALANCE - Laguna S 2.1 | Reducir KV y batch para salir del bucle de evaluación | ctx 65k; B1024/U256 | HE0 → HE20 → BCB |
| `[bench Laguna] Q2 · 100k · B1024 · U256` (`sys-bench-laguna-s-2-1-q2-48gb-100k-b1024`) | BALANCE - Laguna S 2.1 | Aislar batch como causa manteniendo el contexto histórico | B1024/U256; ctx 100k | HE0 → HE20 → BCB |
| `BALANCE - Laguna S 2.1 · CUDA safe 64k` (`sys-laguna-s-2-1-q2-48gb-safe`) | BALANCE - Laguna S 2.1 | Evitar la ruta inestable de fit y reducir presión de KV/prefill; Flash on es obligatorio para V-cache q4_0 | ctx 65k; B256/U64; fit off; Flash on; tensor-split 1,1; 32 expertos CPU | HE0 → HE20 → BCB |
| `[bench Laguna 24GB] 32k · template oficial · thinking OFF` (`sys-bench-laguna-s-2-1-q2-24gb-32k-official`) | Laguna S 2.1 Q2 | A/B contra el template v24 en GPU+RAM; aislar template con contexto y batch conservadores | ctx 32k; B512/U128; template oficial; thinking OFF | HE0 → HE20 → BCB |
| `[bench Laguna 24GB] 32k · template v24 · thinking OFF` (`sys-bench-laguna-s-2-1-q2-24gb-32k-v24`) | Laguna S 2.1 Q2 | Control del A/B de template en GPU+RAM | ctx 32k; B512/U128; template v24; thinking OFF | HE0 → HE20 → BCB |
| `[bench Laguna 48GB] 32k · template oficial · thinking OFF` (`sys-bench-laguna-s-2-1-q2-48gb-32k-official`) | Laguna S 2.1 Q2 | A/B principal contra v24 en 2× RTX 3090; aislar template con la misma quant y reparto | ctx 32k; B512/U64; template oficial; thinking OFF; split 1,1 | HE0 → HE20 → BCB |
| `[bench Laguna 48GB] 32k · template v24 · thinking OFF` (`sys-bench-laguna-s-2-1-q2-48gb-32k-v24`) | Laguna S 2.1 Q2 | Control del A/B principal con template v24 | ctx 32k; B512/U64; template v24; thinking OFF; split 1,1 | HE0 → HE20 → BCB |
| `[bench antirez] 32k · B2048 · U256 · KV q4_0` (`sys-48-antirez-dsv4-q2q4-0731-32k-b2048`) | QUALITY - DeepSeek Fusion leloch | Bajar contexto/batch para mejorar estabilidad sin repartir capas base | ctx 32k; B2048/U256; `tensor-split 1,0` | HE0 → HE20 → BCB |
| `[bench antirez] 32k · B4096 · U512 · KV q8_0` (`sys-48-antirez-dsv4-q2q4-kv8`) | QUALITY - DeepSeek Fusion leloch | KV q8 puede mejorar calidad sostenida; medir coste real | ctx 32k; KV K/V q8_0 | HE0 → HE20 → BCB |
| `[bench NInfer] Qwen3.6-27B · texto` (`sys-ninfer3090-qwen27`) | NInfer-3090 · Qwen3.6-27B · texto | Comparar backend nativo contra los candidatos llama.cpp | ctx 4k; B512/U128; KV int8; MTP2 | HE0 → HE20 → BCB |
| `[bench NInfer] Qwen3.6-35B-A3B · coding` (`sys-ninfer3090-qwen35`) | NInfer-3090 · Qwen3.6-35B-A3B · coding | Comparar el backend nativo en el perfil coder | ctx 4k; B512/U128; KV int8; MTP2 + prompt-lookup | HE0 → HE20 → BCB |
| `[bench NInfer] Qwen3.8-27B · coding` (`sys-ninfer3090-qwen38`) | NInfer-3090 · Qwen3.8-27B · coding | Verificar el artefacto Qwen3.8 en el flujo local; BCB condicionado a tool-calls | ctx 4k; B512/U128; KV int8; MTP3 | HE0 → HE20 → BCB |
| **[SUPERIOR CONTEXTO LARGO] Qwen3.8 KV streaming · 131k · K8/V4 · B256/U256** (`sys-bench-qwen38-kvstream-24gb-131k`) | Fork `sachin-detrax/llama.cpp-adaptive-kv-streaming`, commit `11a01c8` | Ejecutar Qwen3.8 dense con KV autoritativo en RAM pinned y pool CUDA acotado en una sola RTX 3090 de 24GB | `--kv-stream-stage-mib 2048`; ctx 131k; K q8_0/V q4_0; B256/U256; `split-mode none`; `parallel=1`; texto-only | **Historial de infraestructura: 8k..131k sintético OK; no promocionar calidad** |

### Resultado directo del perfil KV streaming — 2026-08-29

Se construyó el fork Windows desde `feature/adaptive-kv-stream`, commit
`11a01c8`, con MSVC/CUDA y `GGML_CUDA_FA_ALL_QUANTS=ON`. En la RTX 3090 de
24 GiB se ejecutó el sweep sintético del fork con Qwen3.8 UD-Q4, Flash on,
K=`q8_0`, V=`q4_0`, B/U=`256/256`, `parallel=1`, una GPU visible y pool fijo
de 2048 MiB. Cada punto llenó `context-8` y generó 8 tokens:

| Contexto | Prefill tok/s | Decode tok/s | VRAM usada después | Resultado |
|---:|---:|---:|---:|---|
| 8k | 1226,2 | 36,02 | 19.065 MiB | OK |
| 32k | 1074,5 | 31,09 | 19.173 MiB | OK |
| 64k | 858,7 | 25,22 | 19.316 MiB | OK |
| 131k | 617,1 | 5,25 | 19.621 MiB | OK |

La clasificación es **SUPERIOR en ejecución sintética larga con B256/U256**:
el fork completó 131k manteniendo ~4,7 GiB libres. Es **INFERIOR en latencia
de contexto largo** a medida que crece la transferencia KV; no es un perfil
speed-first. El A/B de passkey no se promociona: con este GGUF ambos runtimes
fallaron la salida exacta, aunque el oficial b10331 además cayó con `ubatch=256`
y con `ubatch=64` resultó mucho más lento (44,5 tok/s de prefill y 12,0 tok/s
de decode, frente a 870,1 y 38,3 del candidato en una pasada). La diferencia
de calidad queda **sin validar**.

## Resultados HE0 de los candidatos

TPS es el decode nativo informado por `llama-server` en `eval time`, no el `avgTps` del agente cuando el harness no recibió tokens de generación. Un `—` indica que no hubo timing evaluable por fallo de carga o cierre de conexión.

| Candidato | HumanEval/0 | Tiempo HE0 | TPS HE0 | Estado |
|---|---:|---:|---:|---|
| `[bench 16GB] Qwen3.8 Heretic RVN-IQ3_XXS · ngram-mod · 131k` | Pendiente | — | — | Pendiente de HE0; requiere build beellama/dflash2-capable y GGUF RVN-IQ3_XXS |
| `[bench 16GB] Qwen3.8 Heretic RVN-IQ3_XXS · DFlash2+ngram · 105k` | Pendiente | — | — | Pendiente de HE0; requiere drafter DFlash2 y registrar fork/build |
| `[bench 16GB] Qwen3.8 Heretic RVN-IQ3_XXS-MTP · MTP+ngram · 105k` | Pendiente | — | — | Pendiente de HE0; GGUF fusionado manual; no descargable automáticamente |
| `[bench NInfer] Qwen3.6-27B · texto` | Pendiente | — | — | Pendiente de HE0 |
| `[bench NInfer] Qwen3.6-35B-A3B · coding` | Pendiente | — | — | Pendiente de HE0 |
| `[bench NInfer] Qwen3.8-27B · coding` | Pendiente | — | — | Pendiente de HE0; BCB requiere validar tool-calls |
| `[bench Qwen3.8] UD-Q4 · MTP2 · 64k · mmproj` | 1/1 | 12,926 s | 51,68 | Válido |
| `[bench Qwen3.8] UD-Q4 · MTP3 · B1024 · mmproj` | 1/1 | 10,378 s | 64,66 | Válido |
| `[bench Qwen3.8] UD-Q4 · MTP3 · 131k · KV q8 · mmproj` | 1/1 | 12,925 s | 57,87 | Válido |
| `[bench Qwen3.8] Q4_K_M · MTP4 · 131k · mmproj` | 1/1 | 12,968 s | 45,38 | Válido |
| `[bench Qwen3.8] Q5_K_M · MTP3 · 64k · KV q8 · mmproj` | 1/1 | 14,982 s | 52,48 | Válido |
| `[bench Qwen3.8] UD-Q4 · thinking low · 131k · MTP3 · visión` | Pendiente | — | — | Pendiente de HE0; comparar sólo contra el control UD-Q4 `reasoning off` |
| `[bench Qwen3.8] UD-Q4 · thinking medium · 131k · MTP3 · visión` | Pendiente | — | — | Pendiente de HE0; comparar sólo contra el control UD-Q4 `reasoning off` |
| `[bench Qwen3.8] UD-Q4 · thinking xhigh · 131k · MTP3 · visión` | Pendiente | — | — | Pendiente de HE0; comparar sólo contra el control UD-Q4 `reasoning off` |
| `[bench Qwen3.8] UD-Q4 · Browser Agent · thinking off · 131k` | Pendiente | — | — | Pendiente de HE0; luego requiere suite browser real |
| `[bench Qwen3.8] UD-Q4 · Browser Agent · thinking low · 131k` | Pendiente | — | — | Pendiente de HE0; luego requiere suite browser real |
| `[bench Qwen3.8] UD-Q4 · Browser Agent · thinking medium · 131k` | Pendiente | — | — | Pendiente de HE0; luego requiere suite browser real |
| `[bench Qwen3.8] UD-Q4 · Browser Agent · thinking xhigh · 131k` | Pendiente | — | — | Pendiente de HE0; luego requiere suite browser real |
| `[bench 24GB Qwen3.8] UD-Q4 · fast · MTP4 · 64k · mmproj RAM` | Pendiente | — | — | Pendiente de HE0; análogo llama.cpp, no DFlash2 |
| `[bench 24GB Qwen3.8] UD-Q4 · lookup aproximado · 64k · MTP3` | Pendiente | — | — | Pendiente de HE0; ngram-mod no equivale al lookup DFlash2 |
| `[bench 24GB Qwen3.8] UD-Q4 · prefix cache · 64k · MTP3` | Pendiente | — | — | Pendiente de HE0; comparar sólo con warm-cache |
| `[bench 24GB] Q4_K_M · tg128 · cold · sin speculative` | Pendiente | — | — | Pendiente de HE0; primero verificar que Q4 queda completamente offloaded |
| `[bench 24GB] Q6_K · tg128 · cold · sin speculative` | Pendiente | — | — | Pendiente de HE0; registrar OOM/offload si la máquina no deja margen |
| `[diagnóstico 24GB] Q4/Q6 · ngram o prefix warm` | No rankear | — | — | Medir sólo para demostrar la diferencia entre cold decode, ngram-hit y warm-cache |
| `[bench 12GB] Ling 3.0 Tiny Q6 · auxiliar · 131k` | Pendiente | — | — | GGUF no descargado; HE0 + suite semántica auxiliar |
| `[bench Ling 3.0 Tiny] Q6 · auxiliar · 64k` | Pendiente | — | — | GGUF no descargado; HE0 + suite semántica auxiliar |
| `[bench Ling 3.0 Tiny] Q6 · thinking on · 131k` | Pendiente | — | — | GGUF no descargado; comparar sólo contra Q6 thinking-off |
| `[bench Ling 3.0 Tiny] Q6 · auxiliar · KV q4 · 131k` | Pendiente | — | — | GGUF no descargado; validar fidelidad antes de promover |
| `[bench 8GB] Ling 3.0 Tiny UD-Q4 · auxiliar · 64k` | Pendiente | — | — | GGUF no descargado; HE0 + suite semántica auxiliar |
| `[bench hybrid 24GB] Ling Tiny planifica → Qwen3.8 ejecuta` | Pendiente | — | — | Comparar contra Qwen3.8 directo; separar planificación y ejecución |
| `[bench Qwen3.8] UD-Q4 · espejo post · 160,9k · MTP2 · B2048/U512` | Pendiente | — | — | Pendiente de HE0 |
| `[bench Qwen3.8] Q4_K_M · espejo post · 160,9k · MTP2 · B2048/U512` | Pendiente | — | — | Pendiente de HE0 |
| `[bench Qwen3.8] Q5_K_M · espejo post · 160,9k · MTP2 · B2048/U512` | Pendiente | — | — | Pendiente de HE0 |
| `[bench Qwen3.8] UD-Q4 · MTP3 · 262k · B2048/U512 · KV q8` | Pendiente | — | — | Pendiente de HE0 |
| `[bench Qwen3.8] UD-Q4 · MTP3 · B8192/U512 · 131k` | Pendiente | — | — | Pendiente de HE0 |
| `[bench Qwen3.8] UD-Q4 · MTP3 · 2 slots · 131k` | Pendiente | — | — | Pendiente de HE0 |
| `[bench Qwen3.8] UD-Q4 · tensor split · 131k · MTP3` | Pendiente | — | — | Pendiente de HE0 |
| `[bench Qwen3.8] UD-Q4 · mmproj en RAM · 131k` | Pendiente | — | — | Pendiente de HE0 |
| `[bench Qwen3.8] UD-Q4 · cache warm · 131k · MTP3` | Pendiente | — | — | Pendiente de HE0; comparar sólo contra warm-cache |
| `[bench 48GB] Qwen3.8 Uncensored Q8 · 196k · visión` | Pendiente | — | — | Pendiente de HE0; requiere descargar GGUF Q8 + vision |
| `[bench 48GB Qwen3.8 Q8] tensor split · 196k · MTP3` | Pendiente | — | — | Pendiente de HE0; requiere dos GPUs |
| `[bench 48GB Qwen3.8 Q8] mmproj en RAM · 196k` | Pendiente | — | — | Pendiente de HE0; validar visión aparte |
| `[bench 48GB Qwen3.8 Q8] cache warm · 196k · MTP3` | Pendiente | — | — | Pendiente de HE0; comparar sólo contra warm-cache |
| `[bench 48GB] Qwen3.8 UD-Q6_K_XL · 96k · MTP2 · visión` | Pendiente | — | — | Pendiente de HE0; requiere descargar GGUF Q6 + mmproj |
| `[bench 48GB Qwen3.8 Q6] MTP3 · 96k · visión` | Pendiente | — | — | Pendiente de HE0 |
| `[bench 48GB Qwen3.8 Q6] MTP4 · 96k · visión` | Pendiente | — | — | Pendiente de HE0 |
| `[bench 48GB Qwen3.8 Q6] MTP2 · 64k · visión` | Pendiente | — | — | Pendiente de HE0 |
| `[bench 48GB Qwen3.8 Q6] MTP2 · 131k · visión` | Pendiente | — | — | Pendiente de HE0 |
| `[bench 48GB Qwen3.8 Q6] MTP2 · 96k · KV q8` | Pendiente | — | — | Pendiente de HE0 |
| `[bench 48GB Qwen3.8 Q6] MTP2 · 96k · B2048/U512` | Pendiente | — | — | Pendiente de HE0 |
| `[bench 48GB Qwen3.8 Q6] espejo post · 96k · MTP2 · B2048/U512 · mmproj RAM` | Pendiente | — | — | Pendiente de HE0; comparar con otras recetas espejo |
| `[bench 48GB Qwen3.8 Q6] mmproj en RAM · 96k · MTP2` | Pendiente | — | — | Pendiente de HE0; validar visión aparte |
| `[bench 48GB Qwen3.8 Q6] tensor split · 96k · MTP2` | Pendiente | — | — | Pendiente de HE0; requiere dos GPUs |
| `[bench 48GB Qwen3.8 Q6] cache warm · 96k · MTP2` | Pendiente | — | — | Pendiente de HE0; comparar sólo contra warm-cache |
| `[bench 48GB Qwen3.8 Q6] reasoning on · 96k · MTP2` | Pendiente | — | — | Pendiente de HE0; validar primera tool-call |
| `[bench 48GB KAT] KV q8_0 · 262k (cap de política)` | Pendiente | — | — | Resultado histórico con f16 archivado; repetir con q8_0 |
| `[bench BigBang] 131k · MTP · batch 1024 · ubatch 256` | — | 58,638 s | — | Infraestructura: server-load |
| `[bench BigBang] 131k · sin MTP · KV q8_0` | Pendiente | — | — | Resultado histórico con f16 archivado; repetir con q8_0 |
| `[bench 48GB MAX-Q] MTP4 · 131k · visión` | 1/1 | 11,498 s | 58,02 | Válido |
| `[bench Laguna 24GB] 32k · template oficial · thinking OFF` | Pendiente | — | — | Pendiente de HE0; comparar contra la variante v24 de 24 GB |
| `[bench Laguna 24GB] 32k · template v24 · thinking OFF` | Pendiente | — | — | Pendiente de HE0; control de 24 GB |
| `[bench Laguna 48GB] 32k · template oficial · thinking OFF` | Pendiente | — | — | Pendiente de HE0; comparar contra la variante v24 de 48 GB |
| `[bench Laguna 48GB] 32k · template v24 · thinking OFF` | Pendiente | — | — | Pendiente de HE0; control de 48 GB |
| `[bench Laguna] Q2 · 64k · B1024 · U256` | 1/1 | 15,004 s | 51,46 | Válido |
| `[bench Laguna] Q2 · 100k · B1024 · U256` | 1/1 | 13,961 s | 51,99 | Válido |
| `BALANCE - Laguna S 2.1 · CUDA safe 64k` | 1/1 | 150,127 s | — | Válido; carga estable sin illegal access; el tiempo incluye cold start del agente |
| `[bench antirez] 32k · B2048 · U256 · KV q4_0` | 1/1 | 93,540 s | 10,00 | Válido |
| `[bench antirez] 32k · B4096 · U512 · KV q8_0` | 1/1 | 76,189 s | 10,00 | Válido |

Evidencia de la tanda: `benchmark-runs/HumanEval_1_tems__20260816_130505` a `HumanEval_1_tems__20260816_133559`. La variante segura de Laguna se ejecutó en `benchmark-runs/HumanEval_1_tems__20260817_203002` con `agent-basico`: `1/1`, `150,127 s`, `failed=false`, `failureKind=none`, `vramMb=18891`, `ramMb=38050`. Los 24 IDs fueron ejecutados aisladamente. KAT2 (`sys-48-katcoder-262k`) cargó y llegó a timing nativo de `103,93 t/s`, pero el daemon terminó en `APPCRASH` de `LlamaCode.exe` dentro de `Qt6Core.dll` antes de persistir el JSON; queda como `HE0 daemon-crash`, no como calidad.

Las variantes DeepSeek mantienen la regla de seguridad del perfil histórico: `--tensor-split 1,0`, expertos residentes alineados con sus capas y resto en CPU. `tensor-split 1,1` queda sólo como diagnóstico: con b10331 generó “París” correctamente, pero fue 6,6% más lento en el smoke; requiere repetir HE0/HE20/BCB antes de cambiar la receta.

`*` En KAT2, `103,93 t/s` es timing nativo observado antes del `APPCRASH`; no hay JSON evaluable y debe repetirse.

`†` En DeepSeek, el tiempo/TPS de BCB es el último intento observado antes de cerrar la serie; la respuesta no tuvo cierre evaluable. Debe repetirse después de HE0/HE20 válidos del perfil o variante correspondiente.

La nota histórica de KAT2 que sigue a esta sección describe el fallo anterior; no invalida la repetición 3/3 documentada arriba.

## Captura completa de configuración

La configuración se separa en: launch profile, runtime preset y argumentos adicionales. Los IDs son importantes: si cambia uno, el perfil puede apuntar silenciosamente a otro modelo, backend o runtime.

### BALANCE - Qwen3.8 UD-Q4 visión

```text
launchId: sys-qwen38-27b-udq4-131k
backendProfileId: sysbe-sys-qwen38-27b-udq4-131k
modelProfileId: sysmodel-sys-qwen38-27b-udq4-131k
runtimePresetId: sysrt-sys-qwen38-27b-udq4-131k
agentProfileId: agent-maximo
runtime: ctx=131072, batch=512, ubatch=64, threads=8, gpuLayers=999, parallelSlots=1, cache=q4_0, flashAttention=on, contBatching=on, mmap=on, mlock=off
mmprojId: 4eff47c9-3c8c-5163-aa3a-b7262ff17af7
extraArgs: --cache-type-k q4_0 --cache-type-v q4_0 --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --predict 4096 --parallel 1 --reasoning off --spec-type draft-mtp --spec-draft-n-max 3 --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/qwen38-tools-fixed.jinja
```

### BALANCE - Qwen3.8 UD-Q4 MTP4

```text
launchId: sys-bench-qwen38-udq4-mtp4
backendProfileId: sysbe-sys-bench-qwen38-udq4-mtp4
modelProfileId: sysmodel-sys-bench-qwen38-udq4-mtp4
runtimePresetId: sysrt-sys-bench-qwen38-udq4-mtp4
agentProfileId: agent-maximo
runtime: ctx=131072, batch=512, ubatch=64, threads=8, gpuLayers=999, parallelSlots=1, cache=q4_0, flashAttention=on, contBatching=on, mmap=on, mlock=off
mmprojId: 4eff47c9-3c8c-5163-aa3a-b7262ff17af7
extraArgs: --cache-type-k q4_0 --cache-type-v q4_0 --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --predict 4096 --parallel 1 --reasoning off --spec-draft-n-max 4 --spec-type draft-mtp --spec-draft-n-max 3 --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/qwen38-tools-fixed.jinja
```

### FAST - KAT2-Coder-7-8-26

```text
launchId: sys-48-katcoder-262k
backendProfileId: sysbe-sys-48-katcoder-262k
modelProfileId: sysmodel-sys-48-katcoder-262k
runtimePresetId: sysrt-sys-48-katcoder-262k
agentProfileId: agent-maximo
runtime: ctx=262144, batch=2048, ubatch=512, threads=8, gpuLayers=999, parallelSlots=1, cache=q8_0, flashAttention=on, contBatching=on, mmap=on, mlock=off
extraArgs: --cache-type-k q8_0 --cache-type-v q8_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 8192 --parallel 1 --reasoning off --cache-prompt --cache-reuse 512 --split-mode layer --tensor-split 1,1 --skip-chat-parsing --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/kat-coder-tools.jinja
```

### KAT APEX-MTP + Qwen mmproj — experimental

```text
launchId: sys-48-katcoder-mtp-vision
modelProfileId: sysmodel-sys-48-katcoder-mtp-vision
runtimePresetId: sysrt-sys-48-katcoder-mtp-vision
runtime: ctx=32768, batch=512, ubatch=64, threads=8, gpuLayers=999, parallelSlots=1, cache=q8_0, flashAttention=on, contBatching=on, mmap=on, mlock=off
model: KAT-Coder-V2.5-Dev-MTP-APEX-i-quality-v2.gguf
mmproj: mmproj-F16.gguf (unsloth/Qwen3.6-35B-A3B-MTP-GGUF)
spec: draft-mtp, n-max=2 (variante MTP3; variante nospec)
minimumBinaryBuild: 10331
```

La combinación no está validada automáticamente por tener nombres de familia
compatibles: hay que comprobar que el `mmproj` produzca respuestas útiles y que
MTP no provoque crash en la build instalada. El APEX MTP es un GGUF de una sola
pieza; no se debe combinar un cabezal Qwen externo con el KAT Q4 normal.

El template bundleado `kat-coder-tools.jinja` conserva el formato de tool-calling
de KAT y convierte el contenido OpenAI multimodal (`image_url`) en los marcadores
`<|vision_start|><|image_pad|><|vision_end|>` que consume `llama-server` con
`--mmproj`. Por eso el smoke debe probar también una imagen real, no sólo que el
proyector aparezca en la línea de comandos.

### FAST - KAT-Coder-7-8-26

```text
launchId: 9dda6bf4-7aae-4806-ba3a-8466bf41e702
backendProfileId: d4e4ab3d-1188-444e-b971-5f86fe683eab
modelProfileId: b933d0b2-014e-45c0-9558-3936d310e0bb
runtimePresetId: 8378307f-290b-4d7f-a345-1aef49db938b
agentProfileId: agent-maximo
runtime: ctx=262144, batch=2048, ubatch=512, threads=8, gpuLayers=999, parallelSlots=1, cache=q8_0, flashAttention=on, contBatching=on, mmap=on, mlock=off
extraArgs: --cache-type-k q8_0 --cache-type-v q8_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 8192 --parallel 1 --reasoning off --cache-prompt --cache-reuse 512 --split-mode layer --tensor-split 1,1 --skip-chat-parsing --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/kat-coder-tools.jinja
```

### FAST - BigBang · MTP · top-p 0.08

```text
launchId: cbff7c85-2116-4b42-b1b9-485dd33384cc
backendProfileId: b5acf97e-a091-4925-837a-99270c093b38
modelProfileId: ae10f4c4-2d39-4fc1-acdc-f16b9c75b0bc
runtimePresetId: 83cf0d96-d531-43e0-9fed-6a1c407047d0
agentProfileId: agent-maximo
runtime: ctx=131072, batch=4096, ubatch=1024, threads=0, gpuLayers=999, parallelSlots=1, cache=q8_0, flashAttention=on, contBatching=on, mmap=on, mlock=off
mmprojId: debe4716-71b7-5cfa-9d7a-045546810eda
extraArgs: --cache-type-k q8_0 --cache-type-v q8_0 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --no-context-shift --metrics --no-warmup --jinja --parallel 1 --reasoning on --spec-draft-n-max 5 --spec-type draft-mtp --temp 0.70 --top-p 0.08
```

### BALANCE - BigBang · MTP · top-p 0.08 (histórico)

```text
tablaName: BALANCE - BigBang · MTP · top-p 0.08
launchId: sys-bench-48-bigbang-mtp
internalName: [bench BigBang] 131k · MTP embebido · KV q8_0
backendProfileId: sysbe-sys-bench-48-bigbang-mtp
modelProfileId: sysmodel-sys-bench-48-bigbang-mtp
runtimePresetId: sysrt-sys-bench-48-bigbang-mtp
agentProfileId: agent-maximo
runtime: ctx=131072, batch=512, ubatch=128, threads=0, gpuLayers=999, parallelSlots=1, cache=q8_0, flashAttention=off, contBatching=on, mmap=on, mlock=off
mmprojId: 24152073-986a-5470-b717-a70861d14883
extraArgs: --cache-type-k q8_0 --cache-type-v q8_0 --flash-attn off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --parallel 1 --reasoning on --spec-draft-n-max 5 --spec-type draft-mtp
WARNING: el nombre visible de la tabla no coincide con el nombre interno ni con top-p 0.08. No publicar esta fila como definitiva hasta resolver la identidad.
```

### BALANCE - BigBang MTP

```text
launchId: sys-repair-48-bigbang-mtp-balance
internalName: BALANCE - BigBang MTP
agentProfileId: agent-chat
runtime: ctx=65536, batch=256, ubatch=64, threads=0, gpuLayers=999, parallelSlots=1, cache=q8_0, flashAttention=on, contBatching=on, mmap=on, mlock=off
mmprojId: heredado del BigBang-v1; visión disponible
extraArgs: --cache-type-k q8_0 --cache-type-v q8_0 --flash-attn on --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --parallel 1 --reasoning off --spec-draft-n-max 5 --spec-type draft-mtp
HE0: 1/1, 13,365 s, TPS 10,72; sin reparación del agente, crash ni transporte truncado
```

La fila histórica no se sobrescribe: la copia reparada es la que queda
habilitada para repetir HE20 y luego BCB, siempre respetando la compuerta HE0.

### BALANCE - ThinkingCap Qwen3.6-27B MTP4

```text
launchId: a03e65f5-2f2c-4d45-b67b-4b1270fa2a6c
backendProfileId: 1cd00b04-02bd-41bf-be45-49eb35b0c3cf
modelProfileId: 423c82fd-50c6-4a2d-b7b4-5c2d168dbd1c
runtimePresetId: 12b64031-d497-44ad-af3b-6fd2d451ce91
agentProfileId: (vacío; usa el default del launch)
runtime: ctx=131000, batch=512, ubatch=64, threads=8, gpuLayers=-1, parallelSlots=1, cache=q4_0, flashAttention=on, contBatching=on, mmap=off, mlock=on
mmprojId: 8572fc2c-29cd-5caf-b702-4e2b71fb5de3
extraArgs: --alias thinkingcap-qwen36-27b-q4km-mtp4 --cache-type-k q4_0 --cache-type-v q4_0 --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --cache-ram 32768 --cache-reuse 512 --jinja --threads-batch 8 --predict 4096 --parallel 1 --flash-attn on --ctx-size 131000 --reasoning off
```

### BALANCE - ThinkingCap+MTP-7-8-26

```text
launchId: sys-48-thinkingcap-mtp
backendProfileId: sysbe-sys-48-thinkingcap-mtp
modelProfileId: sysmodel-sys-48-thinkingcap-mtp
runtimePresetId: sysrt-sys-48-thinkingcap-mtp
agentProfileId: agent-maximo
runtime: ctx=196608, batch=2048, ubatch=512, threads=8, gpuLayers=999, parallelSlots=1, cache=q8_0, flashAttention=on, contBatching=on, mmap=on, mlock=off
mmprojId: fe4ff7eb-f122-53a6-bdb4-fde28253c875
extraArgs: --cache-type-k q8_0 --cache-type-v q8_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 8192 --parallel 1 --reasoning-format auto --cache-prompt --cache-reuse 512 --split-mode layer --tensor-split 1,1 --spec-type draft-mtp --spec-draft-n-max 4
```

### BALANCE - Laguna S 2.1 118B-A8B Q2

```text
launchId: 8d0dd2e0-c6c6-41ef-81d6-893c20d2f621
backendProfileId: b53df8bb-16b9-413d-8649-813e0a70d080
modelProfileId: 358edb77-0667-4190-b0e1-08654cb13864
runtimePresetId: 1b670632-3987-4047-be78-3efc93bb60d6
agentProfileId: (vacío; usa el default del launch)
runtime: ctx=100000, batch=2048, ubatch=768, threads=8, gpuLayers=999, parallelSlots=1, cache=q4_0, flashAttention=on, contBatching=on, mmap=on, mlock=off
extraArgs: --cache-type-k q4_0 --cache-type-v q4_0 --fit on --split-mode layer --tensor-split 1,1 --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 4096 --parallel 1 --reasoning-format auto --reasoning-preserve --chat-template-file %LOCALAPPDATA%/LlamaCode/LlamaCode/chat-templates/laguna-tools-v24.jinja
```

### QUALITY - DeepSeek Fusion leloch

```text
launchId: 4f5cc556-333d-4310-955e-15042cd874d6
backendProfileId: 1485cb47-757a-4a01-9f71-832567d01973
modelProfileId: 6ab3222e-5f71-442d-9eb9-7e895520befc
runtimePresetId: 20d4e6e6-9240-4926-9ea6-5bcea0eb2c50
agentProfileId: agent-maximo (para reproducir el histórico HE20 usar agent-chat)
runtime: ctx=131072, batch=4096, ubatch=1024, threads=8, gpuLayers=999, parallelSlots=1, cache=q4_0, flashAttention=on, contBatching=on, mmap=on, mlock=off
extraArgs: --cache-type-k q4_0 --cache-type-v q4_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 16384 --parallel 1 --reasoning-format auto --cache-prompt --split-mode layer --tensor-split 1,0 --override-tensor blk\.(37|38|39|40|41|42)\.ffn_(gate|up|down)_exps\.weight=CUDA1,blk\.[0-9]+\.ffn_(gate|up|down)_exps\.weight=CPU --repeat-last-n 64 --flash-attn on --cpu-moe --cache-ram 32768
historical HE20 agent: agent-chat; thinkingEnabled=false
```

### QUALITY - DeepSeek Fusion leloch · VRAM balance

Variante duplicada mientras LlamaCode estaba cerrada. El perfil original no comparte backend, modelo ni runtime persistidos con esta copia; sólo comparten el mismo archivo de modelo identificado por `modelId`.

```text
launchId: 6b3bf7bd-0889-491a-9b6d-b12128478a5f
backendProfileId: 07bf242d-0685-45d1-a752-11ddec6ef6df
modelProfileId: 0985be04-d2bc-455d-a3a5-e5fc19795e5d
runtimePresetId: fdd4ca0a-3b8d-43a2-924b-092327aca314
agentProfileId: agent-maximo (el smoketest HE0 se ejecutó con agent-chat)
runtime: ctx=131072, batch=4096, ubatch=1024, threads=8, gpuLayers=999, parallelSlots=1, cache=q4_0, flashAttention=on, contBatching=on, mmap=on, mlock=off
mmprojId: (vacío)
extraArgs: --cache-type-k q4_0 --cache-type-v q4_0 --fit off --temp 0.60 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0 --no-context-shift --metrics --no-warmup --jinja --threads-batch 16 --predict 16384 --parallel 1 --reasoning-format auto --cache-prompt --split-mode layer --tensor-split 1,0 --override-tensor blk\.(0|1)\.ffn_(gate|up|down)_exps\.weight=CUDA0,blk\.(37|38|39|40|41|42)\.ffn_(gate|up|down)_exps\.weight=CUDA1,blk\.[0-9]+\.ffn_(gate|up|down)_exps\.weight=CPU --repeat-last-n 64 --flash-attn on --cpu-moe --cache-ram 32768
```

La variante conserva `--tensor-split 1,0` para no desalinear las capas base de los expertos. Su única diferencia funcional respecto del original es el primer tramo de `-ot`, que coloca los expertos de los bloques 0 y 1 en CUDA0; los bloques 37–42 siguen en CUDA1 y el resto sigue en CPU. La vista previa efectiva verificada antes del benchmark resolvió `--override-tensor` con esa misma regla.

Backup de los cuatro archivos antes de la edición: `profiles/{launches,backends,models,runtimes}.json.bak.vram-variant.20260816_121921`.

## Corte auditado de campaña post-corrección — 2026-08-28

La campaña serial de 86 perfiles no quedó documentada sólo como “en curso”:
el log persistente registra cierre explícito para los perfiles 1–57 (27
`complete`, 30 `incomplete`) y cancelación controlada al comenzar el perfil
58. `complete` describe el estado de etapas del runner y no significa BCB 8/8.
La lista completa por número y el inventario de artefactos están en
[`benchmark-results-history.md`](benchmark-results-history.md).

En esta campaña se persistieron los siguientes resultados que antes figuraban
como pendientes en la matriz:

| Perfil | HE0 | HE20 | BCB | Lectura |
|---|---:|---:|---:|---|
| `[bench 48GB] KAT APEX-MTP + visión · MTP3 · 32k` | 1/1 · 46,149 s | 18/20 · 429,338 s | 5/8 · 463,354 s | Calidad parcial; no promover |
| `[bench 48GB] KAT APEX-MTP + visión · sin MTP · 32k` | 1/1 · 43,946 s | 19/20 · 702,375 s | 1/8 · 342,839 s | Control válido; calidad insuficiente |
| `[bench antirez stress] 64k · B4096 · U1024 · KV q8_0` | 1/1 · 156,513 s | 19/20 · 1150,040 s | 3/8 · 2049,730 s | Calidad parcial; no desplaza candidatos |

Los 17 intentos HE0 `0/0` de `DeepSeek V4-7-8-26` y ULTRA-Q quedaron
clasificados como `server-load`, no como calidad cero. Los archivos JSON y sus
metadatos se conservan bajo `%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-runs`.

## Regla de edición y verificación

1. Cerrar LlamaCode antes de editar cualquier perfil o runtime desde la UI/archivo de perfiles.
2. Guardar una copia de esta matriz antes de modificar.
3. Abrir LlamaCode headless y verificar `getLaunchProfile`, `getRuntimePreset` y la línea de comandos efectiva de `llama-server`.
4. No aceptar un resultado 0/0 como calidad: clasificarlo como `Infraestructura`, `server-load`, `server-crash` o `timeout`.
5. Actualizar primero la fila de resultados y luego el bloque de configuración, manteniendo el ID histórico de la corrida.

## Experimentos Qwen3.6 cache híbrido — 2026-08-18

Copias opt-in de MAX-Q ejecutadas exclusivamente desde `build/Debug/LlamaCode.exe`.
No modifican el control `a03e65f5-2f2c-4d45-b67b-4b1270fa2a6c`.

| Perfil | ID | Suite | Resultado | Tiempo | VRAM | RAM | Estado |
|---|---|---|---:|---:|---:|---:|---|
| Control MAX-Q MTP4 repetido | `a03e65f5-2f2c-4d45-b67b-4b1270fa2a6c` | Corta/7 | 5/5 | 86,262 s | 23.849 MB | 26.408 MB | control |
| Cache híbrido MTP2 | `sys-experiment-qwen36-cache-mtp2` | Corta/7 | 5/5 | 147,013 s | 23.576 MB | 25.065 MB | estable; lento |
| Cache híbrido MTP4 | `sys-experiment-qwen36-cache-mtp4` | Corta/7 | 5/5 | 111,520 s | 23.867 MB | 25.371 MB | estable; sin promoción |
| Cache híbrido MTP6/p-min 0.5 | `sys-experiment-qwen36-cache-mtp6` | Corta/7 | 5/5 | 86,551 s | 24.159 MB | 25.672 MB | repetir |
| Texto-only cache híbrido MTP4 | `sys-experiment-qwen36-cache-text-mtp4` | Corta/7 | 5/5 | 91,671 s | 22.975 MB | 24.305 MB | menor memoria |

Configuración común de las copias: `ctx=131000`, `batch=512`, `ubatch=64`,
`parallel=1`, KV `q4_0`, Flash Attention, sampling conservador,
`--cache-ram 32768 --ctx-checkpoints 8 --checkpoint-min-step 4096
--kv-unified --cache-idle-slots`. La copia texto-only agrega `--no-mmproj`.
El log de b10331 informa que `cache-reuse` no está soportado por el contexto MTP
(y también queda deshabilitado en la variante multimodal), por lo que la mejora
de cache propuesta por el texto no quedó validada completamente. La corrección
híbrida de PR #25592 debe repetirse con un binario que la incluya.
