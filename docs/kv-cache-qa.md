# QA de KV cache y contexto largo

LlamaCode incluye un fixture de recuperación larga y un probe opt-in para
comparar `q4_0`, `q8_0`, `f16` o un runtime especializado contra el mismo
`llama-server`. El probe no forma parte de `ctest`: necesita un modelo cargado,
puede enviar prompts de 131K y su duración depende del hardware.

## Qué mide

- Recuperación de una passkey única en siete profundidades: 5%, 15%, 25%, 50%,
  75%, 90% y 95%.
- Salida exacta, no sólo una coincidencia parcial.
- Prefill y decode por separado usando los `timings` de `llama-server`.
- Latencia por request, errores HTTP, timeouts y throughput agregado.
- Con `--users N`, N requests concurrentes por profundidad, cada una con una
  passkey y un stream distintos para comprobar aislamiento.

El relleno usa una aproximación de cuatro caracteres por token. Sirve para
comparar variantes con la misma carga, no para afirmar una cantidad exacta de
tokens del tokenizer del modelo.

## Uso

Compilar el candidato Debug y localizar el probe:

```powershell
build.bat Debug NOPAUSE
```

Con un `llama-server` ya levantado:

```powershell
.\build\Debug\qa_kv_cache.exe http://127.0.0.1:8080 `
  --contexts 8192,32768,131072 `
  --depths 0.05,0.15,0.25,0.50,0.75,0.90,0.95 `
  --users 1 `
  --n-predict 32 `
  --timeout-ms 120000 | Tee-Object kv-cache-qa.json
```

Para comprobar concurrencia, repetir con el mismo modelo, contexto y flags,
cambiando sólo `--users`:

```powershell
.\build\Debug\qa_kv_cache.exe http://127.0.0.1:8080 --contexts 32768 --users 4 |
  Tee-Object kv-cache-qa-users4.json
```

Para una comparación válida, detener y volver a levantar el servidor entre
variantes, mantener fijo el modelo, `ctx-size`, batch, ubatch, GPU layers,
flash-attention y número de usuarios, y guardar cada recibo por separado. Un
resultado sólo es candidato si todas las passkeys pasan exactamente y no hay
requests sin timings, timeout u OOM.

## Runtime especializado

Un runtime que necesite sidecars o variables propias puede probarse con el mismo
probe siempre que conserve el endpoint `/v1/chat/completions` y el formato de
timings de llama-server. El probe usa el chat template del modelo y muestreo
determinista para no confundir el razonamiento/formato de salida con un fallo de
recuperación. Para un servidor que sólo exponga `/completion`, hay que adaptar
el probe explícitamente antes de comparar resultados. Los argumentos y el
entorno deben quedar explícitos en el perfil; no se deben promover a default
hasta repetir la prueba en el modelo y hardware objetivo.

La prueba unitaria de `tests/long_context_probe.cpp` garantiza que el fixture es
determinista, que cada passkey aparece una sola vez y que las profundidades se
mantienen ordenadas. La prueba no valida la calidad general del modelo: para
eso hay que combinarla con HumanEval/BCB, tool-calling y una corrida normal del
benchmark de LlamaCode.

## A/B completo con dos runtimes

Para comparar dos configuraciones que requieren reiniciar el servidor —por
ejemplo `q8_0` contra un runtime con sidecars— usar
[`tools/kv_cache_ab.py`](../tools/kv_cache_ab.py). El runner es portable entre
Windows y Linux, arranca y detiene cada `llama-server`, espera `/health`, corre
el mismo probe en ambos lados y guarda los comandos, logs, recibos y deltas
pareados. No descarga modelos ni sidecars.

Si un servidor termina durante el arranque o antes de responder `/health`, el
runner conserva también las últimas líneas de su log dentro del campo `error`
del registro de esa variante, además de `logPath`. Esto permite diagnosticar un
fallo de runtime desde el JSON sin perder la evidencia original.

Copiar y editar
[`assets/benchmarks/kv_cache_ab.example.json`](../assets/benchmarks/kv_cache_ab.example.json)
con las rutas reales. Después:

La ruta común `serverExe` se conserva por compatibilidad; cada variante puede
sobrescribirla con su propio binario. Esto permite comparar el oficial contra
un fork especializado sin cambiar el modelo ni el resto de la receta.

```powershell
python tools/kv_cache_ab.py `
  --config assets/benchmarks/kv_cache_ab.example.json `
  --probe build/Debug/qa_kv_cache.exe `
  --passes 3 `
  --include-aa `
  --out kv-cache-ab.json
```

En Linux se puede usar `python3` y el binario `qa_kv_cache` correspondiente.
`--include-aa` repite el baseline como control de ruido; si el control resulta
estadísticamente inestable, `validForPromotion` queda en `false`. El proceso
termina con código distinto de cero ante fallos de arranque, timeouts, pérdida
de passkeys, filas no pareadas o resultados que no pasan el control de calidad.

## Matriz de contextos (LID y otros runtimes)

Para comparar presets que requieren una reserva KV distinta, usar
[`tools/long_context_matrix.py`](../tools/long_context_matrix.py). A diferencia
del probe directo, reinicia `llama-server` para cada contexto y reescribe tanto
`--ctx-size` como `--fit-ctx` cuando aparecen en la receta. Cada fila conserva
su comando, log y recibo; `verifiedContexts` sólo incluye contextos que
arrancaron y pasaron todas las passkeys exactamente.

El ejemplo para la rama DeepSeek LID CUDA está en
[`assets/benchmarks/deepseek_lid_context_matrix.example.json`](../assets/benchmarks/deepseek_lid_context_matrix.example.json):

```powershell
python tools/long_context_matrix.py `
  --config assets/benchmarks/deepseek_lid_context_matrix.example.json `
  --probe build/Debug/qa_kv_cache.exe `
  --out deepseek-lid-context-matrix.json
```

Se puede hacer una corrida acotada antes de lanzar toda la matriz:

```powershell
python tools/long_context_matrix.py `
  --config assets/benchmarks/deepseek_lid_context_matrix.example.json `
  --contexts 131072 `
  --probe build/Debug/qa_kv_cache.exe `
  --out deepseek-lid-131k.json
```

`--dry-run` valida la configuración y muestra los comandos finales sin cargar
el modelo. Los presets 256K, 512K y 1M siguen siendo experimentales: que estén
en el perfil sólo los ofrece en la UI; deben aparecer en `verifiedContexts` de
un recibo real antes de considerarse capacidad confirmada.

Para separar capacidad de calidad, `--startup-only` reinicia el servidor por
contexto y sólo exige que `/health` responda; no ejecuta `qa_kv_cache`. Sirve
para diagnosticar reservas diferidas o límites de VRAM, pero no confirma
recuperación ni calidad del modelo.

### Windows con WSL2

El runner admite `launcher.kind = "wsl"`: el probe sigue ejecutándose en
Windows y `wsl.exe` inicia cada servidor dentro de la distribución indicada. El
runtime fraQtl debe estar en rutas Linux absolutas; usar como base
[`assets/benchmarks/kv_cache_ab.wsl.example.json`](../assets/benchmarks/kv_cache_ab.wsl.example.json),
reemplazar `/home/your-user` y ejecutar desde PowerShell:

```powershell
python tools/kv_cache_ab.py `
  --config assets/benchmarks/kv_cache_ab.wsl.example.json `
  --probe build/Debug/qa_kv_cache.exe `
  --passes 1 `
  --out kv-cache-ab-wsl.json
```

El runtime publicado por fraQtl es Linux x86_64/CUDA y requiere una GPU de las
arquitecturas que declara el paquete; WSL2 no convierte ese binario en un
runtime Win32 nativo. `FRAQTL_MEMBRANE_EXCLUSIVE=1` debe mantenerse durante la
validación: ante un dispatch no soportado el proceso debe fallar, no continuar
con una ruta de cache que ya no representa la variante candidata.

## Registro de disponibilidad — 2026-08-27

Se verificó el contrato local de `qa_kv_cache` y se imprimió su ayuda. El
ejecutable devuelve código 2 cuando se invoca sin el `base-url` posicional,
aunque la ayuda se muestra; esto es una observación de CLI y no un resultado de
KV cache.

No se ejecutó la carga real. La PC tenía modelos GGUF locales disponibles,
pero durante la revisión apareció un `llama-server` preexistente de otra
instalación en `127.0.0.1:8033` (Qwen3.5-9B, contexto 32768). El probe puede
enviar prompts grandes y no se debe usar contra un servidor que no fue lanzado
por esta corrida. Para preservar la sesión existente no se inició otro server,
no se enviaron requests al PID 34264 y no se generó un recibo de benchmark.
