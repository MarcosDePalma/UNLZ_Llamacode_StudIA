# Qwen3.8-Flash-Next en LlamaCode

Modelo MoE de 125B (arquitectura Qwen4) con 262k de contexto. Lo particular no es
el tamano sino que una parte grande del modelo son tensores **Ngram / PLE**: una
lookup table de acceso aleatorio, no compute.

## Por que rompe los supuestos del proyecto

1. **`size_total` deja de predecir la VRAM necesaria.** El peso residente y el
   peso total divergen: unsloth reporta que el 1-bit corre en 75 GB de RAM *sin
   VRAM*. El calculo de tier de `EffectiveProfileBuilder` asume archivo ~= VRAM.
2. **RAM vs VRAM importa mucho menos que en un MoE normal.** Los Ngram se pueden
   dejar en RAM o incluso en SSD con mmap y la perdida de t/s es chica.
3. **No se cuantizan agresivamente.** Minimo 4-bit, porque el acceso aleatorio se
   degrada mal. Por eso el 1-bit pesa 75 GB en vez de los ~30 GB que uno
   esperaria de 125B a 1 bpw.

## Engine

Mainline todavia no mergeo la arquitectura: hace falta el PR
[ggml-org/llama.cpp#27742](https://github.com/ggml-org/llama.cpp/pull/27742).

El catalogo tiene la entry `qwen38-next` (`src/core/EngineCatalog.cpp`), que
compila desde ese pull request. Para eso hubo que agregar soporte de **PR refs**:
`git clone --branch pull/27742/head` no funciona (un PR no es una rama), asi que
el script de build detecta la forma `pull/<N>/head` y hace
`git fetch origin pull/<N>/head:pr-<N>` + checkout.

- `EngineCatalog::parsePullRequestRef()` / `localBranchForRef()`
- Tests: `pullRequestRefIsParsedAsBranch`, `qwen38NextEntryBuildsFromPullRequest`
  en `tests/test_engine_catalog.cpp`.

El arbol del PR vive en `tools/llama.cpp-qwen38-next/` (slug distinto del
llama.cpp oficial, para no pisarse).

## Eleccion de quant

KLD publicado por unsloth (top-1% = cuanto coincide con BF16):

| quant | GB | mean KLD | top-1 % |
|---|---|---|---|
| UD-Q4_K_XL | 111.3 | 0.0447 | 93.5 |
| UD-IQ4_XS | 93.7 | 0.0792 | 91.1 |
| UD-Q3_K_XL | 90 | 0.0997 | 90.4 |
| UD-IQ3_XXS | 82 | 0.1565 | 87.6 |
| UD-Q2_K_XL | 78.9 | 0.2133 | 85.2 |
| UD-IQ1_M | 74.5 | 0.3022 | 82.4 |
| UD-IQ1_S | 72.5 | 0.3751 | 80.2 |

Los requisitos se cuentan como **RAM + VRAM sumadas**. En la maquina de
desarrollo (2x RTX 3090 = 48 GB VRAM + 127 GB RAM = 175 GB) entra UD-Q4_K_XL con
aire para KV cache; el salto de IQ4_XS a Q4_K_XL casi duplica la fidelidad por
17.6 GB, y es el mejor de la tabla.

## Sampling

Son dos perfiles distintos, no uno con variantes:

| | thinking | instruct |
|---|---|---|
| temperature | 1.0 | 0.7 |
| top_p | 0.95 | 0.80 |
| top_k | 20 | 20 |
| min_p | 0.0 | 0.0 |
| presence_penalty | 0.0 | 1.5 |

`reasoning_effort` (`xhigh` por defecto, `medium`, `low`, `none`) va por
`--chat-template-kwargs`. En PowerShell hay que escapar las comillas:

```
--chat-template-kwargs "{\"reasoning_effort\":\"medium\"}"
```

## Benchmark

`tools/benchmark_qwen38_flash_next.ps1` barre las tres colocaciones de los Ngram
(VRAM / RAM / SSD+mmap) contra los dos perfiles de sampling, y reporta t/s y
accuracy. El regex de tensores Ngram se **deriva del GGUF**, no se adivina.

```
powershell -File tools\benchmark_qwen38_flash_next.ps1 -Passes 2 -Output run.json
```

## Estado real: el engine produce salida corrupta

Lo mas importante que salio de correrlo: **el PR 27742 genera texto corrupto de
forma no reproducible**. Se le caen digitos y caracteres:

```
"2^100 mod 125"   ->  el modelo razona sobre "2^18 mod "
"coprime"         ->  "copr"
"doesn't"         ->  "doesn"
"Count: 121 122 123 ..."  ->  "1211 1211 1212" en vez de "130 131 132"
```

Con `temperature 0` y `seed` fijo la salida deberia ser identica siempre. No lo
es: cambia entre instancias del server y entre modos de prefill.

### Lo que SI se pudo aislar

| condicion | resultado |
|---|---|
| 10 peticiones identicas, mismo estado | 10/10 identicas (estable DENTRO de un estado) |
| `cache_prompt=false` (prefill fresco) | corrupto, consistente |
| `cache_prompt=true` (prefijo cacheado) | correcto, consistente |
| `--ubatch-size 1` | arregla algunos prompts, no todos |

O sea: la generacion desde un prefijo ya cacheado esta bien; el **prefill** es
donde se rompe. Encaja con el commit del propio PR ("hold the qwen4exp indexer
cache in a new llama_memory_hybrid_idx"): hay un cache nuevo especifico de esta
arquitectura.

### Caracterizado: se caen tokens de UN caracter

Probe con 5 continuaciones inequivocas x 3 repeticiones x 4 tamanos de ubatch
(`tools/` no lo incluye; fue un harness descartable). Resultado:

| tarea | prompt | ubatch1 | ubatch32 | ubatch128 | default |
|---|---|---|---|---|---|
| letters | `A B C D E F G H I` | 3/3 | 3/3 | 3/3 | 3/3 |
| evens | `2 4 6 8 10 12 14 16` | 0/3 | 3/3 | 3/3 | 3/3 |
| count | `121 122 ... 129` | 3/3 | 0/3 | 1/3 | 1/3 |
| numbers | `55 56 ... 63` | 0/3 | 0/3 | 0/3 | 0/3 |
| days | `Monday Tuesday ...` | 0/3 | 0/3 | 0/3 | 0/3 |

**`letters` es el unico que sale perfecto siempre.** Y las fallas tienen forma:

```
"55 56 57 ... 63"   -> " 6 6 6 6 6 6"      (el 2do digito de cada numero, ausente)
"2 4 6 ... 16"      -> " 1820222426283"    (los numeros correctos, SIN espacios)
```

El tokenizer NO es el culpable: `/tokenize` + `/detokenize` da round-trip
perfecto en 8/8 casos, incluidos `"Count: 121 122 123"` y `"2^100 mod 125"`.

Lo que si muestra el tokenizer es la clave: `"55 56 57"` son **8 tokens** -- los
digitos van de a uno. `"J K L M"` son 4. O sea que el modelo genera digito por
digito, y lo que se pierde son **tokens de un solo caracter** (digitos sueltos y
espacios). Las letras zafan porque cada "letra + espacio" es un token unico.

Es corrupcion en el forward pass a nivel de token individual, no en el
tokenizer ni en el sampler. `--ubatch-size` cambia CUALES prompts fallan, lo que
apunta al batching del prefill, pero ningun valor los arregla a todos.

### Correccion de una conclusion anterior

En una primera pasada se atribuyo la corrupcion a `-ot` de los Ngram y a
`--cpu-moe`. **Eso estaba mal.** Esas mediciones eran de UNA sola peticion por
config, y justo esa peticion cae en la loteria del prefill. Repitiendo con
warmup y varias peticiones, ambas configs alternan entre salida buena y mala:

| | req 1 | req 2 | req 3 |
|---|---|---|---|
| sin `-ot` | correcto | corrupto | corrupto |
| con `-ot` | corrupto | correcto | correcto |

No hay causalidad a nivel de flag. Por eso NO se agrego ningun health check que
culpe a `-ot` o a `--cpu-moe`: seria codificar una conclusion que los datos no
sostienen.

## El offload de Ngram no sirve (por otro motivo)

Independientemente de la corrupcion, el `-ot` de los Ngram **no ahorra memoria**
en CUDA. Medido en el sweep completo:

| config | VRAM |
|---|---|
| `--n-cpu-moe 40` sin `-ot` | 24168 MiB |
| `--n-cpu-moe 40` con `-ot` de Ngram | 24136 MiB |

32 MiB de diferencia sobre 26.85 GB de tensores, y ~0.06 t/s mas lento. El
reporte original que motivo la idea era de **Metal**; en CUDA el override no
hace lo que promete. La variable util para dimensionar es `--n-cpu-moe`.

## Implicancia para el calculo de tier

`EffectiveProfileBuilder` asume `tamano de archivo ~= memoria necesaria`. Para
esta arquitectura eso sigue siendo falso, pero **no** por la razon que se
esperaba: como el offload de Ngram no funciona en CUDA, los 26.85 GB de lookup
tables SI tienen que estar en memoria. Lo que si cambia el calculo es el MoE:
con `--n-cpu-moe 36` el modelo de 103.68 GB corre con 30.1 GB de VRAM.

O sea que la variable de dimensionamiento util es `--n-cpu-moe`, no el offload de
Ngram. `GGUFScanner::ngramElements` sigue siendo el dato correcto para saber
cuanto del peso es lookup, pero hoy no se traduce en memoria ahorrada.

## Licencia

No es Apache/MIT. Permisiva para uso local, interno, fine-tuning y derivados,
pero MaaS comercial o un asistente de codigo/oficina standalone requieren una
licencia aparte de Qwen. Relevante si LlamaCode alguna vez lo bundlea o lo
ofrece como servicio; para uso local no cambia nada.

## Medido en la maquina de desarrollo (2x RTX 3090 + 127 GB RAM)

Composicion real del UD-Q4_K_XL, leida de los 4 shards:

| | |
|---|---|
| Total | 103.68 GB |
| Ngram/PLE | 26.85 GB (**25.9%**) |
| Backbone residente | 76.82 GB |

La regla del 75% se confirma con el modelo en la mano. Layout: el **shard 1 no
tiene tensores** (solo metadata) y **todos** los Ngram estan en el shard 2, que
ademas mezcla backbone (46.4 GB totales, 26.9 de Ngram). Por eso
`readComposition` de un solo shard no sirve para este modelo y hay
`readCompositionAllShards`.

### Cosas que rompen, encontradas al correrlo

1. **KV cache cuantizado: ARREGLADO upstream (2026-08-27).** Hasta el commit
   `035e22731` abortaba en `qwen4exp.cpp:544` (`GGML_ASSERT(inp->self_k_rot ==
   nullptr)`). El commit `0ac4b1802` ("support a quantized KV cache in the QSA
   attention path") lo resolvio: verificado contra `ef6876693`, q8_0 y q4_0
   levantan y responden bien.
   LlamaCode llego a tener un workaround que descartaba el KV quant para esta
   arquitectura; **se revirtio**, porque una vez arreglado el engine ese dropeo
   pasaba a recortar contexto en silencio. Moraleja para forks en desarrollo
   activo: un workaround contra un bug transitorio caduca, y hay que re-testear
   antes de dejarlo.

2. **`-ot` desactiva `--fit`.** El loader avisa `tensor_buft_overrides already
   set by user, abort` y entonces mete todas las capas a GPU -> OOM. `--tensor-split`
   lo desactiva igual. Con `-ot` hay que dimensionar a mano.
3. **`--n-gpu-layers 999` solo no alcanza.** El backbone son 77 GB contra 48 GB
   de VRAM. Hace falta `--n-cpu-moe N`.
4. **`--mmap` / `--no-mmap` / `--mlock` estan DEPRECADOS** en este build, en favor
   de `-lm/--load-mode {auto|none|mmap|mlock|mmap+mlock}` (auto = mmap). Ademas el
   loader sugiere `--load-mode none` cuando hay `-ot ...=CPU`, o sea que hay un
   trade-off real entre RAM y page faults, no una opcion obviamente mejor.

### VRAM por configuracion (deterministico)

| `--n-cpu-moe` | VRAM total | resultado |
|---|---|---|
| 26 | — | **OOM**: pide 36 GB en una sola placa |
| 32 | — | carga |
| 36 | — | carga |
| 40 | 22.8 GB | carga |
| 48 (`--cpu-moe`) | 10.2 GB | carga |

Con 48 capas de expertos en CPU sobran ~38 GB de VRAM: hay lugar de sobra para
subir expertos, el limite lo pone el reparto desigual entre las dos placas.

**Los t/s medidos hasta ahora NO son confiables**: se tomaron con otra sesion
compilando en paralelo (7 procesos de MSVC), y dieron entre 2.4 y 8.2 t/s para la
misma clase de configuracion. Para numeros publicables hay que correr
`benchmark_qwen38_flash_next.ps1` con la maquina quieta.

## Benchmark medido (maquina libre, 2x RTX 3090 + 127 GB)

`tools/benchmark_qwen38_flash_next.ps1 -Passes 1 -Mode thinking -IncludeNgramOffload`

| `--n-cpu-moe` | ngram `-ot` | t/s | VRAM | working set | accuracy |
|---|---|---|---|---|---|
| 36 | no | **9.96** | 30168 MiB | 25.4 GB | 0% |
| 40 | no | 9.41 | 24168 MiB | 19.6 GB | 0% |
| 40 | si | 9.35 | 24136 MiB | 19.6 GB | 0% |
| 32 / 34 | — | OOM (pide ~28 GB en una placa de 24) | | | |

**La accuracy de 0% NO mide calidad del modelo**: casi todas las tareas agotan el
presupuesto de 4096 tokens sin emitir su linea `FINAL`, porque el razonamiento se
va corrompiendo (ver la seccion de arriba). Mientras el prefill este roto, la
accuracy de este harness no dice nada sobre el modelo.

Lo que si es solido de esta tabla: **throughput (~9.4-10 t/s) y VRAM**, que no
dependen de que el texto sea correcto.

## Re-test contra `ef6876693` (2026-08-27)

El PR **no** esta mergeado, pero sumo 27 commits en un dia. Recompilado y
re-medido con el mismo probe:

| ubatch | build `035e22731` | build `ef6876693` |
|---|---|---|
| 1 | 40% | 40% |
| 32 | 40% | 53% |
| 128 | 47% | 60% |
| default | 47% | 60% |

Mejoro, y `count` paso de 1/3 a 3/3. Siguen fallando 0/3 `numbers`
("55 56 ... 63" -> " 6 6 6 6") y `days` en todas las configuraciones.

### Descartado como causa (verificado, no supuesto)

Se comparo `qwen4exp.cpp` contra la implementacion de referencia
(`transformers/models/qwen4_exp/modeling_qwen4_exp.py`):

- el mixing del hash n-gram (XOR de `token * multiplicador` por posicion) es
  identico;
- el corte de ventana por EOS es identico, incluida la sutileza de que el EOS
  del propio token no corta su contexto;
- las constantes horneadas en el GGUF (`layer_multipliers`, `head_vocab_sizes`,
  `head_offsets`) se reprodujeron en Python desde el config de referencia
  (splitmix64 + primos + offsets acumulados) y **coinciden exactamente**.

O sea que ni el convert ni la matematica del hash son el problema. Lo que queda
es la obtencion de predecesores desde las celdas KV o el grafo de computo -- que
es justo donde estan iterando los autores (`llama: give the qwen4exp full memory
context its indexer cache`, `keep the indexer cache in step across server
slots`, `fix the PLE history seq_rm(-1) iterator invalidation`).

Siguiente paso si hace falta localizarlo: instrumentar `llm_graph_input_ple::
set_input` para volcar los indices calculados y diffearlos contra el calculo en
Python sobre los mismos token ids.
