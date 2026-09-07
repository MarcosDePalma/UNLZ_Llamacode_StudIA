# Validación de Qwen3.8-Flash-Next — 2026-08-28

## Alcance y estado del entorno

Se leyeron `AGENTS.md` y `README.md` antes de trabajar. La validación se
realizó sobre `main`, HEAD `512da0eca8ee923576a319af89fbcaa20c653891`, con
`origin` en `https://github.com/cristianlukas/UNLZ_Llamacode.git`.
La campaña fresca se lanzó sobre ese HEAD; durante su ejecución otra
automatización publicó el commit ajeno
`fedac2eadf8e9e2e2bdf6d97ed5ae31a466250d0` (`audit partial benchmark campaign
history`), que quedó tanto en `main` como en `origin/main`. No se modificó ni
se incluyó ese commit en este informe.

La ventana libre se confirmó después de que no hubiera `llama-server`,
`llama-cli`, CMake ni una compilación activa. Los tres procesos MSBuild
`/nodeReuse` que quedaban no acumularon CPU durante una muestra de cuatro
segundos y no había lock de `build_coord.ps1` ni `active_work`. En ese momento
había aproximadamente 97,45 GB de RAM libres y ambas GPU estaban en 0% de uso
(724/595 MiB ocupados).

## Tests y build del candidato Debug

Comandos ejecutados:

```text
tests.bat Debug
build.bat Debug NOPAUSE
```

Resultados:

- `tests.bat Debug`: **73/73 tests CTest aprobados**, 92,30 s de tiempo total;
  además, las dos suites Python del arnés terminaron `OK` (6 y 8 casos).
- `build.bat Debug NOPAUSE`: **éxito**; se actualizó
  `build/Debug/LlamaCode.exe` y se regeneró el acceso directo Debug con
  `assets/debug_icon.ico`.
- Ejecutable verificado: 26.113.024 bytes, SHA-256
  `C5F9EBA8F8BC164FD9227D92F0B1B872E61A9F103CD7B8FBD9785D3B31574510`.
- Advertencias no bloqueantes del script: no se encontraron `dxcompiler.dll` /
  `dxil.dll` y `VCINSTALLDIR` no estaba definido. También aparecieron las
  líneas espurias `"M"`/`"EM"` del script, pero el build terminó con
  `=== Build complete ===` y liberó el lock con `OK`.

## Artefactos Qwen disponibles

El runtime experimental disponible es
`%APPDATA%/LlamaCode/LlamaCode/tools/llama.cpp-qwen38-next/build/bin/llama-server.exe`,
de 208.480.768 bytes, SHA-256
`8EF3B44A183975E1148A884E3B9752C071A9B2F42E718A29EBFA79F0B589F4CA`, reportado
como `0.3.0-dev (build 92, commit ef6876693)`.

Los cuatro shards `UD-Q4_K_XL` están presentes y completos:

| shard | bytes |
|---|---:|
| `00001-of-00004.gguf` | 10.946.624 |
| `00002-of-00004.gguf` | 49.859.583.136 |
| `00003-of-00004.gguf` | 49.376.141.504 |
| `00004-of-00004.gguf` | 12.087.983.520 |

El conjunto suma aproximadamente 103,68 GB. El commit del engine reconoce
`qwen4exp`, `--n-cpu-moe` y `--override-tensor`; no reconoce
`--tensor-read-lazy`.

## Campaña Qwen y no duplicación

Ya existía una campaña reproducible completada el mismo día, documentada en
`docs/qwen38-flash-next.md` y con logs en
`build_tests_aux/qwen38-campaign-20260828/`. Incluye colocación normal frente a
override PLE, contextos de 16k/32k/64k y mediciones de carga, request,
prefill/decode, working set y VRAM. Sus cuatro casos OK reportados fueron:

| caso | carga s | request s | prefill tok/s | decode tok/s | VRAM MiB |
|---|---:|---:|---:|---:|---:|
| 16k, `n-cpu-moe=40`, KV q8/q8 | 24,59 | 12,52 | 2,46 | 3,88 | 22.690 |
| 16k, `n-cpu-moe=40`, PLE `-ot` a CPU | 27,58 | 7,99 | 5,92 | 4,41 | 22.902 |
| 32k, `n-cpu-moe=40`, KV f16/f16 | 27,86 | 9,69 | 5,02 | 4,14 | 23.894 |
| 64k, `n-cpu-moe=40`, KV f16/f16 | 57,98 | 32,78 | 0,78 | 1,66 | 25.813 |

La conclusión respaldada por esos datos es que `-ot` no ahorra VRAM de forma
material en CUDA; la variable útil de dimensionamiento es `--n-cpu-moe`. El
salto de 32k a 64k elevó la carga aproximadamente 2,1x y bajó el decode a
aproximadamente 40%. También se conservaron fallos de `n-cpu-moe=36`,
`n-cpu-moe=48`, KV q4 y un arranque transitorio, sin tratarlos como fallos de
calidad del modelo.

Se ejecutó además, en una ventana libre posterior, el arnés dedicado:

```text
tools/benchmark_qwen38_flash_next.ps1 -Passes 1 -CtxSize 16384
  -NCpuMoeSweep 40 -ReasoningEffort low -MaxTokens 256 -Mode both
  -IncludeNgramOffload -Port 18127
```

La ejecución completó las 16 corridas (thinking/instruct, normal/PLE, cuatro
tareas por combinación) y produjo
`docs/benchmark-artifacts/qwen38-flash-next-20260828-mode-both-16k.json`.
El escaneo detectó 12 tensores Ngram/PLE, todos en el shard 2, y advirtió que
el layout intercalado mezcla Ngram con el backbone. La tabla siguiente resume
el JSON; `tok/s` es throughput de generación y `correct` fue falso en las 16
corridas según el criterio estricto del smoke del arnés:

| modo | `ngram-ot` | n-cpu-moe | corridas | tokens generados | tok/s promedio | working set MiB | VRAM MiB |
|---|---:|---:|---:|---:|---:|---:|---:|
| thinking | no | 40 | 4 | 1.024 | 9,33 | 19.679 | 22.503 |
| thinking | sí | 40 | 4 | 1.024 | 5,50 | 19.696 | 22.474 |
| instruct | no | 40 | 4 | 901 | 5,29 | 19.693 | 22.502 |
| instruct | sí | 40 | 4 | 942 | 4,96 | 19.695 | 22.484 |

Los logs del `llama-server` registraron también prefill por request. La media
ponderada por tokens de prompt fue 15,91 tok/s (thinking normal), 3,27 tok/s
(thinking PLE), 4,63 tok/s (instruct normal) y 4,64 tok/s (instruct PLE); el
JSON conserva los valores de generación por tarea y los tiempos de pared.
El working set y la VRAM se mantuvieron prácticamente iguales entre cada par,
mientras que el override PLE redujo el decode promedio en esta muestra. Esto
es una medición del engine experimental y no una recomendación de calidad:
`correct=false` indica que el smoke no alcanzó sus respuestas esperadas, pero
no diagnostica por sí solo corrupción del modelo. Los logs crudos quedaron en
`%TEMP%/llamacode-q38fn-thinking-n40*.log` y
`%TEMP%/llamacode-q38fn-instruct-n40*.log`.

La tentativa anterior en el puerto 18117 sí fue abortada: durante el escaneo
automático apareció un `llama-server` externo de DeepSeek. Para no competir
por GPU/RAM ni interferir con la sesión ajena, se detuvieron únicamente el
arnés propio y su proceso `llama-gguf` hijo, antes de cargar Qwen. No se usan
datos de esa tentativa abortada.

## Calidad y limitaciones

Los resultados existentes siguen mostrando corrupción de prefill en el engine
experimental; el documento principal ya separa esa limitación de los datos de
memoria y throughput. El smoke de 32 tokens de la campaña completada no
equivale a recuperar prompts de 32k/64k. No se construyó Release ni se hicieron
cambios de producto para forzar la prueba.

Al finalizar esta pasada no quedaron procesos propios de tests, build o del
arnés Qwen activos. La cuenta GitHub activa fue verificada como `guideahon` y
el remoto como el `origin` esperado; los cambios preexistentes del working tree
se dejaron sin tocar.
