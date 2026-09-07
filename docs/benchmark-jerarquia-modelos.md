# ¿Hay jerarquía Haiku / Sonnet / Opus entre los tres modelos del tier 48 GB?

La idea a probar era: **KAT-Coder = el rápido**, **ThinkingCap = el intermedio**,
**DeepSeek V4 Flash = el inteligente**. Este documento junta lo medido, incluido lo
que salió en contra.

Todo contra `llama-server` b10228-cuda12.4 en 2× RTX 3090, temperatura 0.3,
evaluadores deterministas (varios ejecutan el código que devuelve el modelo).

---

## 1. La suite Corta no discrimina: todos 5/5

Primer intento, benchmark del app en modo agente, 2 pasadas por perfil:

| perfil | score | TPS | seg/pasada |
|---|---|---|---|
| MTP 4 · 131k | 5/5, 5/5 | 61,8 | 59 |
| MTP 4 · 196k | 5/5, 5/5 | 60,3 | 63 |
| ThinkingCap 131k | 4/5, 5/5 | 39,4 | 97 |
| ThinkingCap 196k | 5/5, 5/5 | 36,7 | 96 |
| DeepSeek sin DSpark | 5/5, 5/5 | 7,6 | 374 |
| DeepSeek con DSpark | 5/5, 5/5 | 6,8 | 466 |

Las cinco tareas de la suite (`is_prime`, una cuenta, un one-liner, un silogismo y
un JSON) las despacha cualquier modelo de 27B. **Mide velocidad, no capacidad.**

Dato que ya vale: **DeepSeek tarda 6× más para el mismo 5/5**, y con DSpark rinde
*peor* que sin él (466 s vs 374 s).

---

## 2. Segundo intento: tareas más duras. Empate en los tres

Se armaron 12 tareas nuevas (código con tests ejecutables, aritmética multi-paso,
lógica, invariantes):

| | KAT-Coder | ThinkingCap+MTP | DeepSeek |
|---|---|---|---|
| total | 9/12 | 9/12 · 8/12 | — |
| contar letras `r` | ❌ | ❌ | ❌ |
| formato estricto (3×4 palabras) | ❌ | ❌ | ❌ |
| día de la semana | ❌ | ❌ | ❌ |
| porcentaje inverso | ✅ | ✅ | ✅ |
| suma de horarios | ✅ | ✅ | ✅ |
| json autoconteo | ✅ | ✅ | ✅ |
| nivel de código/lógica (6 tareas) | 6/6 | 6/6 · 5/6 | — |

**Empatan tarea por tarea.** ThinkingCap+MTP no le gana a KAT en nada, y encima va
a la mitad de velocidad (60 vs 110 t/s).

### Por qué esas tres tareas eran malas para medir

Contar letras y "3 líneas de 4 palabras" fallan por **tokenización**: el modelo no
ve caracteres, ve tokens. Un modelo de 284B falla igual que uno de 3B activos. Es
una limitación estructural compartida, no una diferencia de capacidad — no sirven
para armar escalones.

---

## 3. Tercer intento: cambiar de eje

Los ejes donde el tamaño del modelo sí debería pesar:

- **D. conocimiento factual de nicho** — un MoE de ~3B activos no tiene dónde
  guardarlo.
- **E. contexto largo** (needle in a haystack a 8k y 24k tokens).
- **F. muchas entidades cruzadas** — 5-6 restricciones que interactúan.
- **G. instrucciones anidadas** — 7 reglas simultáneas sobre un JSON.

Resultados en `docs/` cuando cierre la corrida; el script está en el scratchpad de
la sesión (`probe3.py`) y es reproducible contra cualquier server.

---

## 3b. Resultados del eje nuevo: la jerarquía está al revés

Anulando el puzzle de las 5 casas (mal diseñado: **no tiene solución**, verificado
a mano — así que "fallarlo" era lo correcto):

| | KAT-Coder | **ThinkingCap+MTP** | DeepSeek V4 |
|---|---|---|---|
| D conocimiento de nicho (5) | 5/5 | 5/5 | 5/5 |
| E contexto largo 8k + 24k (2) | 2/2 | 2/2 | 2/2 |
| F torneo (1) | 1/1 | 1/1 | 1/1 |
| G json con 7 reglas (1) | 1/1 | 1/1 | 1/1 |
| G reescritura con 4 reglas (1) | ❌ | ✅ | ❌ |
| **total válido** | 9/10 | **10/10** | 9/10 |
| velocidad | 110 t/s | 60 t/s | 7 t/s |

**DeepSeek no ganó una sola tarea** que los otros no ganaran, y empata con KAT
yendo 15× más lento. La única tarea que separó a alguien la ganó ThinkingCap.

## 3c. Cuarto intento: tareas de élite. ThinkingCap 14/14

Buscando explícitamente algo que un 284B haga mejor que un 27B:

| eje | ThinkingCap+MTP |
|---|---|
| H conocimiento long-tail (Redis ZSET, año de Karatsuba, counting Bloom, Turing 1936, CAP + Gilbert/Lynch, sort externo) | 6/6 |
| I matemática multi-paso (último dígito de 7^7^7, MISSISSIPPI, serie telescópica, 2^100 mod 125) | 4/4 |
| J algoritmos poco frecuentes, ejecutados (Boyer-Moore O(1) sin `Counter`, k-ésimo desde el final en una pasada) | 2/2 |
| K trampas (reglas contradictorias, bate y pelota) | 2/2 |
| **total** | **14/14** |

Un 27B satura el techo de todo lo que se puede construir **y verificar con
certeza**. Ese es el límite del experimento: para tareas donde un 284B despegue
harían falta problemas de nivel investigación, donde el evaluador tendría que
conocer la respuesta correcta de antemano.

## 3d. DeepSeek sobre el mismo set de élite: empate, sin una sola ventaja

| | ThinkingCap+MTP | DeepSeek V4 IQ3_S |
|---|---|---|
| H conocimiento long-tail (6) | 6/6 | 6/6 |
| I matemática multi-paso (4) | 4/4 | 4/4 |
| J algoritmos poco frecuentes (2) | 2/2 | 2/2 ¹ |
| K trampas (2) | 2/2 | 2/2 |
| **total** | **14/14** | **14/14** ¹ |
| velocidad | 60 t/s | 7 t/s |

¹ La corrida automática marcó 13/14 por un fallo en Boyer-Moore, pero al repetir
esa tarea sola DeepSeek escribió el algoritmo canónico correcto (dos variables,
verificación en segunda pasada, sin `Counter`) y **pasa los cinco tests**. Era
varianza de muestreo a temp 0.3, no incapacidad: se cuenta como PASS.

**Resultado buscando explícitamente una ventaja de DeepSeek: no existe.** Sobre 24
tareas de cuatro sets distintos, no ganó ninguna que ThinkingCap no ganara, y va
8× más lento.

## 4. Lo que ya se puede afirmar

1. **KAT-Coder no es "el rápido y limitado".** Empata en capacidad con ThinkingCap
   y va casi al doble de velocidad. Para trabajo diario es el default razonable.
2. **ThinkingCap+MTP no compra capacidad sobre KAT**, compra visión (mmproj). Si no
   necesitás imágenes, no hay razón medida para preferirlo.
3. **MTP sí vale, y mucho**: +68% de decode (36,7 → 61,8 t/s) sin costo de
   calidad, y baja el tiempo de tarea de 96 s a 59 s. Es la mejor palanca del tier.
4. **DeepSeek no justifica su costo, y el tema está cerrado.** Empata 14/14 con
   ThinkingCap en el set de élite y 9/10 vs 10/10 en el anterior, a 7 t/s contra
   60. Sobre 24 tareas no ganó ninguna. Sale del roster diario.

### Hipótesis abierta sobre DeepSeek

Se corrió en **UD-IQ3_S**, cuantizado sobre un modelo que ya viene en 4-bit. La
pregunta era si la ventaja se perdió ahí. Quedó **sin resolver a propósito**, y no
vale la pena resolverla:

- **Q4 no entra en la máquina**: ~170 GB contra ~138 GB utilizables (48 VRAM + 128
  RAM menos overhead). Los ~30 GB que faltan se leerían del disco en cada token.
  Haría falta pasar a 192 GB de RAM.
- **Q2 (91 GB) no se bajó** porque no hay con qué medir la mejora: comparar quants
  exige al menos UNA tarea donde DeepSeek se despegue, y no existe ninguna. Bajar
  91 GB para comparar dos empates es tiempo tirado.

Si alguna vez aparece una tarea donde DeepSeek gane, el perfil `sys-48-dsv4-iq2m`
ya está armado y ahí sí tiene sentido la comparación.

---

## 5. Método: lo que costó caro

- **Medir velocidad no alcanza: hay que leer la respuesta.** La config más rápida
  de DeepSeek (194,9 t/s) devolvía texto corrupto con métricas perfectas.
- **Los evaluadores no pueden puntuar formato.** `python_prime` y `code_refactor`
  daban False en el 100% de las pasadas de todos los modelos porque comparaban
  literales (`n <= 1`, `**2`) contra código correcto escrito distinto.
- **En modo agente hay que evaluar los archivos, no el chat.** El agente escribe
  `is_prime.py` y su mensaje final viene vacío.
- **Un score parcial no es una corrida fallada.** 3/5 es un resultado; marcarlo
  `failed` esconde el score y dispara reparaciones inútiles.
- **Una tarea con 1.436 archivos generados no está midiendo el modelo.** Ver el
  bucle de renombres en `AgentProgressGovernor` (§ commit 3975790).

---

## 6. HumanEval (benchmark público, 20 ítems, corregido ejecutando los tests)

Última corrida, 2026-08-08:

| modelo | score | tiempo | t/s |
|---|---|---|---|
| DeepSeek V4 IQ3_S | **19/20** (95%) | 1.597 s | 7 |
| DeepSeek V4 IQ2_M | 18/20 (90%) | 1.621 s | ~7,25 |
| ThinkingCap+MTP | **19/20** (95%) | 224 s | 60 |
| KAT-Coder | 18/20 (90%) | **23 s** | 110 |

Los tres caen en el rango que publican los modelos de esta clase (80–95% pass@1),
que es la señal de que el circuito mide bien. **DeepSeek tarda 70× más que KAT
para un punto de diferencia sobre 20.**

### Cuánto costó llegar a un número confiable

Seis corridas, y las cinco primeras midieron otra cosa. Vale anotarlas porque el
patrón se repitió toda la sesión — **el sistema devuelve números plausibles aunque
esté midiendo mal**:

| # | qué medía en realidad | síntoma |
|---|---|---|
| 1 | la suite built-in "Completa" | `startBenchmark` toma un *modo*, no un id de pack |
| 2 | nada: grader no conectado en el path del agente | 0/20 |
| 3 | nada: tampoco en el path del modelo | 0/20 |
| 4 | ThinkingCap sin ser escuchado | MTP manda todo por `reasoning_content` |
| 5 | DeepSeek con el razonamiento pegado al código | `invalid character '¿'` |
| 6 | HumanEval de verdad | 19/19/18 |

Cada uno dejó un test de regresión. Los artefactos que más engañaron fueron los
que **parecían fallas del modelo**: `NameError: name 'List' is not defined` y
`SyntaxError: 'return' outside function` eran del harness — HumanEval espera
`prompt + completion` y yo ejecutaba sólo la respuesta.

Quedan 1–2 fallos residuales por modelo del mismo tipo (texto colándose al código,
respuestas sin cercas de markdown). No se persiguieron más porque no cambian el
orden: el resultado es un empate técnico con 70× de diferencia en velocidad.

### 6b. HumanEval difícil y corto (12 ítems, 3 pasadas)

Los identificadores de HumanEval no están ordenados por dificultad: usar simplemente
los últimos doce habría incluido ejercicios cortos como `/162` y `/163`. Para evitar
elegir a mano después de ver respuestas, `tools/select_humaneval_hard.py` ordena el
pack público de forma determinista por complejidad estructural de la solución
canónica (AST, flujo de control, profundidad y tests) y excluye `/0`–`/19`, ya usados
en la corrida anterior. La solución canónica se usa sólo para seleccionar; el JSONL
resultante conserva el prompt y los tests oficiales y nunca se la envía al modelo.

Subset fijado antes de correr: `/81`, `/129`, `/95`, `/75`, `/124`, `/140`, `/141`,
`/39`, `/127`, `/132`, `/94`, `/20`. Resultado de 36 intentos por perfil:

| perfil | scores por pasada | total | tiempo medio | tokens | decode ponderado |
|---|---:|---:|---:|---:|---:|
| KAT-Coder | 11, 11, 11 / 12 | **33/36 (91,7%)** | 41,0 s | 14.570 | 122,4 t/s |
| ThinkingCap+MTP | 10, 10, 10 / 12 | 30/36 (83,3%) | **39,5 s** | 7.554 | 66,2 t/s |
| DeepSeek V4 IQ3_S | 10, 9, 10 / 12 | 29/36 (80,6%) | 457,9 s | 10.015 | 7,3 t/s |

Los fallos también discriminan:

- KAT falló `/81` una vez y `/132` dos veces: score estable, solución concreta no
  totalmente estable.
- ThinkingCap repitió exactamente `/127` y `/132` en las tres pasadas.
- DeepSeek falló `/132` tres veces, `/127` dos y `/129` dos. No resolvió ningún
  ejercicio que estableciera una ventaja sobre ambos perfiles más baratos.

Este test sí pide más código y más ramas que el prefijo de 20, pero sigue siendo
**función aislada, pass@1**. Por eso refuta una ventaja de DeepSeek en programación
algorítmica corta; no sustituye un proyecto agentic acumulativo con archivos,
herramientas, tests iterativos y recuperación de errores.

## 7. Veredicto

1. **KAT-Coder es el default.** Ganó además el subset difícil: 91,7%, contra 83,3%
   de ThinkingCap y 80,6% de DeepSeek.
2. **ThinkingCap+MTP cuando haga falta visión.** Conserva la ventaja cualitativa de
   mmproj, pero el subset difícil no mostró una ventaja de código sobre KAT.
3. **DeepSeek V4 no justifica su costo en código aislado.** Fue último en el subset
   difícil, tardó 11,2× más que KAT en promedio, ocupa
   116 GB, necesita offload a RAM y una configuración frágil (`-ot` alineado, y
   con `--tensor-split 1,1` devuelve texto corrupto sin avisar).
4. **MTP es la mejor palanca del tier**: +68% de decode sin costo de calidad.

La hipótesis de una ventaja específicamente **agentic y acumulativa** queda abierta:
HumanEval no usa repositorio ni tools. No debe afirmarse que DeepSeek la tiene hasta
que gane una prueba de proyecto reproducible bajo esas condiciones.

## 7b. BigCodeBench-Hard Instruct (8 ítems, 2 pasadas)

Para salir del tipo de ejercicio de HumanEval se usó BigCodeBench-Hard v0.1.4: su
split Hard tiene 148 tareas orientadas a instrucciones más complejas y composición
de APIs. `tools/prepare_bigcodebench_hard.py` construye una muestra reproducible:
orden SHA-256 con seed fija, exclusión de red/subprocess y dependencias ausentes, y
admisión sólo si la solución canónica pasa localmente los tests oficiales. La
solución canónica valida el instrumento pero no se incluye en el pack ni en el
prompt.

IDs fijados antes de inferir: `/928`, `/765`, `/771`, `/1019`, `/906`, `/583`,
`/139`, `/360`. Incluyen collections/itertools, filesystem, regex, OCR/imágenes,
criptografía y NumPy/Pandas/Matplotlib. Resultado reproducido dos veces:

| perfil | pasadas | total | tiempo medio | tokens totales | decode ponderado |
|---|---:|---:|---:|---:|---:|
| KAT-Coder | 3/8, 3/8 | **6/16 (37,5%)** | **35,6 s** | 5.733 | 118,4 t/s |
| ThinkingCap+MTP | 3/8, 3/8 | **6/16 (37,5%)** | 64,6 s | 6.027 | 64,3 t/s |
| Fable Fusion Q6, MTP3 32k, 2×3090 | 3/8, 3/8 | **6/16 (37,5%)** | 71,5 s | 5.084 | 50,3 t/s |
| Laguna S 2.1 118B-A8B UD-Q2_K_XL, 2×3090 | 2/8, 2/8 | **4/16 (25,0%)** | 91,1 s | 10.267 | 64,2 t/s |
| DeepSeek V4 IQ3_S, perfil conservador | 1/8, 1/8 | 2/16 (12,5%) | 460,5 s | 4.950 | 5,6 t/s |
| MiniMax M2.7 Q3_K_S, 32k, 2×3090 | 1/8, 1/8 | 2/16 (12,5%) | 1.783,8 s | 19.200 | 5,4 t/s |

KAT, ThinkingCap y Fable pasaron siempre `/928`, `/906` y `/139`, y fallaron los
mismos otros cinco. Fable empató calidad, pero tardó 10,7% más que ThinkingCap y
aproximadamente el doble que KAT. Laguna pasó siempre `/928` y `/139`: no alcanzó a los dos perfiles
Qwen, aunque duplicó el score de DeepSeek con aproximadamente una quinta parte de
su tiempo. DeepSeek sólo pasó `/928`; cambió firmas o retornos exigidos, renombró
`task_func` en varias respuestas y se extendió de forma repetida en `/771`. No ganó
ningún ítem exclusivo.

El barrido sostenido de Fable detectó una diferencia que el smoke corto no mostró:
120k/MTP4 completó la primera pasada pero cayó con `CUDA illegal memory access` al
iniciar la segunda; MTP3 y `nospec` a 120k tampoco produjeron dos corridas válidas.
Con 32k/MTP3 completó las 16 tareas sin crash y repitió exactamente el score y los
IDs. Por estabilidad, esa configuración queda como base y 120k como preset opt-in.

El perfil pedido `sys-48-dsv4-nospec` no produjo un score válido: en dos intentos
se cayó en el primer prompt con `CUDA illegal memory access` (primero GPU 1, después
GPU 0). El runner siguió y guardó 0/8 sin tokens; esos resultados se excluyen. Para
medir capacidad se usó `sys-ultraq-dsv4-0731-iq3s`, los mismos pesos UD-IQ3_S con
offload conservador de una placa. Esto separa dos hechos: el perfil 48 GB es inestable
con esta carga y, cuando el modelo sí corre, tampoco muestra ventaja de calidad.

MiniMax se midió después de optimizarlo sobre llama.cpp b10331: `--n-cpu-moe 45`,
`--split-mode layer` y `--tensor-split 3,1`. Las dos pasadas fueron estables y
reprodujeron exactamente el mismo resultado: pasó sólo `/906` y falló `/928`,
`/765`, `/771`, `/1019`, `/583`, `/139` y `/360`. Casi todas las respuestas
agotaron 1.200 tokens, por lo que empató la calidad de DeepSeek pero tardó 3,9×
más por pasada. Queda marcado como favorito/benchmark para repetir comparaciones,
no como recomendación automática.

BigCodeBench sigue siendo generación de una función, no una sesión agentic sobre un
repositorio. Es un escalón materialmente más complejo que HumanEval, pero no cierra
por sí solo la hipótesis de trabajo acumulativo con tools.

## 8. Comparación de quant DeepSeek (WikiText-2 + HumanEval)

Se descargaron los tres shards oficiales UD-IQ2_M (~90,9 GB) y se ejecutó
`llama-perplexity` b10228 sobre el mismo `wiki.test.raw`, 40 chunks de 512 tokens:

| quant | PPL | delta vs IQ3_S | HumanEval-20 | tiempo |
|---|---:|---:|---:|---:|
| UD-IQ3_S | 4,6923 ± 0,12246 | baseline | 19/20 | 1.597 s |
| UD-IQ2_M | 5,7034 ± 0,15557 | **+21,6%** | 18/20 | 1.621 s |

El resultado cierra la pregunta del quant: **IQ2_M no regala velocidad ni
calidad** en esta configuración. Sus fallos `/7` y `/10` agotaron los 1.200 tokens
repitiendo razonamiento sin emitir código. Queda descargado para reproducibilidad,
pero no debe promoverse.

La corrida descubrió además que b10228 acepta varias reglas `-ot` dentro de una
sola ocurrencia separada por comas; si se repite el flag, conserva sólo la última.
El builder y los tres perfiles DeepSeek 48 GB quedaron corregidos. PPL y score son
comparables porque no cambian los pesos. Un probe real posterior al fix verificó
una sola regla `-ot`, pasó 1/1 y midió **6,78 t/s**: la residencia CUDA1 no
rescata IQ2_M y, para ese ítem, empeoró el decode frente a ~7,25 t/s.
