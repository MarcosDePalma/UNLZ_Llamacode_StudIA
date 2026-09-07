# Repetición headless de HumanEval/20 — 2026-08-15

## Alcance

Se cancelaron las corridas interrumpidas y se intentó repetir únicamente la
selección de perfiles mostrada en las capturas del usuario. La ejecución se
hizo contra el build Debug actualizado, sin abrir ni controlar la interfaz:

- ejecutable: `build/Debug/LlamaCode.exe`
- modo: `--headless`
- ControlApi: `http://127.0.0.1:8876`
- suite: `HumanEval (20 ítems)`
- benchmark custom: `267bb33d-4510-417b-ae3c-6dbbfb2cb08d`
- objetivo: `agent`
- agente: `agent-chat` / `Chat liviano`
- pasadas: 1 por perfil
- límite duro: 1200 segundos por perfil en la última tanda

La orden lógica enviada al daemon fue equivalente a:

```powershell
$body = @{
  method = 'startCustomBenchmark'
  args = @($profileIds, '267bb33d-4510-417b-ae3c-6dbbfb2cb08d', 1,
           'agent', 1200, 'agent-chat')
} | ConvertTo-Json -Depth 8
Invoke-RestMethod -Method Post -Uri 'http://127.0.0.1:8876/invoke' `
  -ContentType 'application/json' -Body $body
```

La lista seleccionada fue:

```text
sys-48-katcoder-262k
sys-48-thinkingcap-mtp
sys-laguna-s-2-1-q2-48gb
sys-qwen38-27b-udq4-131k
9dda6bf4-7aae-4806-ba3a-8466bf41e702
cbff7c85-2116-4b42-b1b9-485dd33384cc
a03e65f5-2f2c-4d45-b67b-4b1270fa2a6c
8d0dd2e0-c6c6-41ef-81d6-893c20d2f621
4f5cc556-333d-4310-955e-15042cd874d6
sys-bench-48-bigbang-post
sys-bench-qwen38-udq4-mtp4
sys-qwen38-27b-q4km-131k
sys-qwen38-27b-q5km-131k
```

## Última medición válida disponible por perfil

Estos son los últimos registros con una medición cuantificable. Un resultado
de calidad inferior a 20/20 se conserva como dato válido; los fallos de carga,
crash o timeout no se consideran mediciones de calidad.

| Perfil | HumanEval/20 | Tiempo |
|---|---:|---:|
| FAST - KAT2-Coder-7-8-26 | 20/20 | 307,78 s |
| BALANCE - ThinkingCap+MTP-7-8-26 | 20/20 | 197,10 s |
| BALANCE - Laguna S 2.1 118B-A8B Q2 | 20/20 | 204,16 s |
| BALANCE - Qwen3.8 UD-Q4 visión | 20/20 | 269,96 s |
| FAST - KAT-Coder-7-8-26 | 20/20 | 212,69 s |
| FAST-BigBang MTP top-p 0.08 (copia local antigua) | — | No comparable: conserva la configuración anterior |
| BALANCE - ThinkingCap Qwen3.6-27B MTP4 | 20/20 | 174,96 s |
| QUALITY - DeepSeek Fusion leloch | 20/20 | 817,61 s |
| FAST - BigBang · MTP · top-p 0.08 | 20/20 | 277,00 s |
| BALANCE - Qwen3.8 UD-Q4 MTP4 | 20/20 | 332,12 s |
| Qwen3.8-27B Q4_K_M visión | — | Sin medición válida |
| Qwen3.8-27B Q5_K_M visión | — | Sin medición válida |

## Resultado de la última tanda headless

La última tanda quedó registrada en:

```text
%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-runs\HumanEval_20_tems__20260815_193802
```

Resultados nuevos confirmados:

- `sys-48-katcoder-262k`: **20/20**, 307,78 s.
- `sys-48-thinkingcap-mtp`: fallo de infraestructura `server-crash`, sin score.
- `sys-laguna-s-2-1-q2-48gb`: comenzó, pero quedó atascado en el primer prompt y fue cancelado.

Los perfiles posteriores de la cola no se ejecutaron en esa tanda porque se
canceló la serie al detectar el atasco de Laguna. El daemon terminó con:

```text
benchmarkRunning=False
serverRunning=False
serverState=stopped
benchmarkStatus=Cancelado.
```

## Diagnóstico de los fallos

### Resultados 1/20 falsamente bajos

La causa principal quedó reproducida en el harness. Las 20 tareas pedían
escribir siempre `solution.py` dentro del mismo workspace. Los modelos que
obedecían esa instrucción sobrescribían la solución anterior y al final sólo
quedaba el ejercicio 20: de ahí el patrón exacto 1/20 de BigBang y DeepSeek.
Los 20/20 anteriores dependían accidentalmente de que ciertos modelos crearan
archivos adicionales.

El harness corregido asigna un archivo determinista por tarea, por ejemplo
`solution_HumanEval_0.py`, y el grader sólo acredita ese artefacto. La prueba de
regresión intercambia deliberadamente dos soluciones y exige 0/2, evitando
crédito cruzado entre ejercicios.

### Bucle de Laguna

En Laguna y en el intento anterior con Qwen visión se observaron entradas
repetidas en `.llamacode/agent_events.jsonl`:

```text
kind=benchmark_acceptance_probe
score=0
total=20
hardFailed=true
```

Se generaba aproximadamente una entrada por segundo sin completar el turno ni
avanzar de prompt. `streamingText` entrega snapshots acumulativos en algunos
backends, pero el benchmark sumaba el largo completo en cada callback. Así podía
alcanzar falsamente el límite de 16000 caracteres con apenas ~100 tokens,
cancelar la generación y quedar esperando un cierre que nunca llegaba.

Ahora sólo se cuenta el delta nuevo de cada snapshot; se excluyen los eventos
internos `.llamacode/` del grader, se reevalúa únicamente cuando cambia un
artefacto del usuario y hay watchdogs de 15 s para cancelaciones y 180 s de
inactividad. Esto elimina tanto el consumo continuo de procesos Python como el
bucle headless.

### Crash de BigBang

La prueba corta reprodujo un acceso ilegal CUDA en
`ggml_cuda_flash_attn_ext_mma_f16_case`. En llama.cpp b10331, omitir la opción
no desactiva el modo automático; se requiere `--flash-attn off`. A su vez, una
caché KV cuantizada exige Flash Attention, por lo que el perfil estable usa KV
f16. El batch 4096/ubatch 1024 reservaba otros ~10,9 GB y agotaba CUDA al crear
el contexto MTP; quedó reducido a B512/U128 (la variante fast, a B1024/U256).

En los logs del servidor también aparecieron reinicios/watchdog y procesos que
terminaron con código `62097`. Esos casos se clasifican como infraestructura,
no como “el modelo obtuvo 0/20”.

El arreglo incluido en el commit `7ba1956` mantiene el aislamiento entre
pasadas: detiene y vuelve a cargar el servidor antes de cada pasada repetida, y
reconoce marcadores de reinicio del backend como errores de infraestructura.
La validación del código fue `tests.bat Debug`: **52/52 pruebas aprobadas**.

## Validación posterior al arreglo

Se ejecutó `HumanEval (1 ítems)` de forma headless contra el Debug corregido:

| Perfil | Resultado | Tiempo | Reparaciones |
|---|---:|---:|---:|
| QUALITY - DeepSeek Fusion leloch | 1/1 | 70,08 s | 0 |
| BALANCE - Laguna S 2.1 118B-A8B Q2 | 1/1 | 11,38 s | 0 |
| FAST - BigBang · MTP · top-p 0.08 (Flash off, KV f16, B512/U128) | 1/1 | 10,49 s | 0 |

DeepSeek cargó y respondió correctamente. Laguna completó sin entrar en el
bucle. BigBang cargó y ejecutó sin el crash CUDA después de aplicar la pareja
compatible Flash off/KV f16 y reducir el batch.

La suite automatizada final fue `tests.bat Debug`: **52/52 pruebas aprobadas**.
También se generó y verificó `build/Debug/LlamaCode.exe`.

## HumanEval/20 definitivo con el harness corregido

El 2026-08-15 se repitieron aisladamente, en modo `--headless`, los dos perfiles
canónicos cuyos 1/20 habían sido producidos por la sobrescritura de
`solution.py`. Cada perfil tuvo su propio daemon, servidor y workspace.

| Perfil | HumanEval/20 | Tiempo | Reparaciones | Estabilidad |
|---|---:|---:|---:|---:|
| FAST - BigBang · MTP · top-p 0.08 | 20/20 | 277,00 s | 0 | 100% |
| QUALITY - DeepSeek Fusion leloch | 20/20 | 817,61 s | 0 | 100% |

Evidencia persistida:

```text
%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-runs\HumanEval_20_tems__20260815_205335
%LOCALAPPDATA%\LlamaCode\LlamaCode\benchmark-runs\HumanEval_20_tems__20260815_205909
```

El duplicado local `FAST-BigBang-131k-MTP-top-p-0.08` no reemplaza esta medición:
conserva Flash Attention, KV q8 y B4096/U1024 de la configuración antigua. Debe
considerarse obsoleto frente al perfil de sistema canónico ya corregido, no una
tercera variante comparable.

## Pendientes

- Repetir Qwen3.8-27B Q4_K_M visión y Q5_K_M visión en una tanda aislada.
- Repetir los perfiles que quedaron después de Laguna, preferentemente uno por
  uno para que un crash no cancele la cola completa.
