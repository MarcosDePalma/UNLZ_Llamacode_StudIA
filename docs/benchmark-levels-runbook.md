# Runbook — Comparativa de los 5 NIVELES de agente (handoff)

Handoff para continuar (IA o persona) la medición **tiempo + calidad** de las
respuestas del agente a través de los 5 niveles, sobre una tarea autocontenida.
Todo se maneja **headless** vía ControlApi (ver `docs/control-api.md`).

## Objetivo

Medir, para un mismo modelo (perfil de inferencia **MAX-Q**), cómo varían **tiempo**
y **calidad** según el **NIVEL de agente** (capacidades + directivas + MCP):

| Nivel (agentProfileId) | tools | directivas | MCP |
|---|---|---|---|
| `agent-chat` (Chat liviano) | 5 (read/list/grep/write/edit) | — | off |
| `agent-basico` | core 7 | — | on |
| `agent-intermedio` | 10 | discipline | on |
| `agent-avanzado` | 16 | discipline/testNet/projectContext/efficiency/style | on |
| `agent-maximo` | todas (`*`) | todas menos opt-in puras | on |

**Hipótesis a validar/refutar:** en una tarea AUTOCONTENIDA (un HTML single-file,
sin repo/búsqueda/web), el nivel mínimo **Chat liviano gana en tiempo y calidad**;
los niveles altos agregan latencia (más contexto/atención) y se distraen
sobre-ingenierizando (testNet→escribe tests, projectContext→lee memoria, tools/MCP
irrelevantes), sin upside de calidad. Ver el presupuesto de contexto por nivel en
`tests/test_appcontroller.cpp::agentLevels_contextBudgetLadder` y `[[agent-levels-context-budget]]`.

## Tarea (EvalSuite)

`assets/eval/snake_retro_singlefile.json` — “Snake retro, un HTML autocontenido con
CSS+JS, estética NES/8-bit”. Es el prompt real del usuario.

> ⚠️ **Acceptance demasiado estricta (corregir).** La acceptance actual exige varios
> substrings A LA VEZ (incluye `requestAnimationFrame`). Un Snake válido que use
> `setInterval` “falla” → el benchmark dispara su **bucle de reparación** (gen + hasta
> 2 repairs), triplicando el tiempo por nivel y metiendo falsos negativos. **Antes de
> correr, aflojá la acceptance** a marcadores mínimos que cualquier Snake jugable
> cumpla (p.ej. `<canvas`, `getContext`, `keydown`, `score`) — sin exigir el método de
> loop concreto. Objetivo: que un buen Snake pase en la primera, sin repair.

## Identificadores (verificar, pueden cambiar)

- **MAX-Q launchId**: `5c3d9bda-8810-4331-a770-1c981461fe17`
  (alias “MAX Q QWEN”, `1_Llama.cpp_Qwen_27b_Q4XS_262k_Q4kv_MTP_NGRAM_MMPROJ`).
  Reconfirmá con `POST /invoke {"method":"launchMenu","args":[]}`.
- **Modelo**: Qwen3.6-27B-MTP-IQ4_XS, **ctx 262144**, KV q4, MTP+ngram spec,
  mmproj (visión). GGUF en `D:/Models/llamacpp/Qwen3.6-27B-MTP-IQ4_XS-GGUF/`.
- **Niveles**: `agent-chat`, `agent-basico`, `agent-intermedio`, `agent-avanzado`, `agent-maximo`.
- ControlApi en `http://127.0.0.1:8765` (env `LLAMACODE_CONTROL_PORT`).

## ⛔ Consideración #1 — VRAM (esto te va a frenar)

El 27B con **262k de contexto** entra **justo** en una 3090 de 24 GB **solo si la GPU
está vacía** (~50 t/s). Cualquier otra app que use VRAM (navegadores, EdgeWebView,
Claude/Codex/Antigravity desktop, Steam/Epic/Xbox, etc.) empuja el KV/compute a la
**“shared GPU memory” de Windows (RAM por PCIe)** → CUDA no da OOM, **decodifica a ~1 t/s**
(15× más lento, y peor a medida que crece el KV).

**Antes de cargar el modelo:**
```bash
nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader
```
Querés **>20 GB libres** (used < ~3–4 GB). Si no, cerrá apps que usen GPU:
```bash
nvidia-smi --query-compute-apps=pid,process_name --format=csv,noheader
```
Baseline sano confirmado: corridas viejas del mismo setup dieron **avgTps 56–70 t/s**.
Si tras cargar medís ~3 t/s o menos, **es VRAM** (no el harness): liberá y reintentá.

## Otras consideraciones / gotchas

- **`startServer abort: No model selected`** (en `logs/server.log`, `serverState` queda
  `stopped`): el modelo no está catalogado esta sesión →
  `POST /invoke?target=rootRegistry {"method":"scanAll","args":[]}`, esperar `scanning=false`, reintentar.
- **El agente no arranca solo** si usaste `startServer` suelto: llamá `startAgent(launchId)`
  cuando `serverState=="running"` y `agentRunning` siga `false`.
- **`/prop` devuelve `value`**, `/invoke`/`/setprop` devuelven `result`/`ok`. No confundir.
- **Benchmark de agente sin server+agente cargado** → corre en ~0s con `qualityScore 0`
  (no hay agente). Cargá TODO antes.
- Quedó un resultado basura (Snake `qualityScore 0/0`, ~0.17s) de un intento sin server:
  borralo con `removeBenchmarkResultById(id)` o filtralo por timestamp.

## Procedimiento (PowerShell)

```powershell
$base="http://127.0.0.1:8765"; $id="5c3d9bda-8810-4331-a770-1c981461fe17"
function Inv($m,$a){ Invoke-RestMethod "$base/invoke" -Method Post -ContentType application/json -Body (@{method=$m;args=$a}|ConvertTo-Json -Compress) }
function Prop($n){ (Invoke-RestMethod "$base/prop?name=$n").value }   # .value !

# 0) VRAM > 20 GB libre (ver arriba). Si no, cerrar apps.

# 1) cargar modelo + agente
Inv "startServerAndAgent" @($id)
while ((Prop "serverState") -ne "running") { Start-Sleep 3 }   # ~30-60s
if (-not (Prop "agentRunning")) { Inv "startAgent" @($id); Start-Sleep 3 }

# 2) PROBE de velocidad ANTES de gastar horas: querés ~40-60 t/s
#    (POST directo al server llama.cpp, lee timings.predicted_per_second)
$probe = Invoke-RestMethod "http://127.0.0.1:8021/v1/chat/completions" -Method Post `
  -ContentType application/json -Body '{"messages":[{"role":"user","content":"Conta del 1 al 50 separados por coma."}],"max_tokens":120,"stream":false,"cache_prompt":false}'
$probe.timings.predicted_per_second    # < ~10 => abortar, es VRAM

# 3) importar la suite (con acceptance YA aflojada)
$custom = (Inv "importEvalSuite" @("C:/Users/cristian/Documents/LlamaCode/assets/eval/snake_retro_singlefile.json")).result

# 4) correr los 5 niveles; capturar el resultado más nuevo por timestamp
$levels = "agent-chat","agent-basico","agent-intermedio","agent-avanzado","agent-maximo"
$out = @()
foreach ($lvl in $levels) {
  Inv "startCustomBenchmark" @(@($id), $custom, 1, "agent", 1800, $lvl)
  Start-Sleep 5
  while ((Prop "benchmarkRunning")) { Start-Sleep 15 }   # ~4-6 min/nivel a 50 t/s sin repair
  $r = (Prop "benchmarkResults" | Where-Object { $_.runLabel -like "*Snake*" } | Sort-Object timestamp -Desc)[0]
  $out += [ordered]@{ level=$lvl; q="$($r.qualityScore)/$($r.qualityTotal)"; sec=$r.elapsedSec; tps=$r.avgTps; ttft=$r.avgTtftMs; files=$r.agentFiles; runDir=$r.runDir }
}
$out | ConvertTo-Json -Depth 6
```

## Qué reportar (por nivel)

`qualityScore/qualityTotal`, `elapsedSec`, `avgTps`, `avgTtftMs`, `agentFiles` (¿escribió
1 HTML o se fue a tests/archivos extra?), y abrir el HTML de `runDir` para juzgar calidad
real (jugable, retro, single-file). Armar tabla y contrastar con la hipótesis: ¿Chat
ganó tiempo y calidad? ¿los niveles altos sobre-ingenierizaron / tardaron más?

## Resultado 2026-06-29

Corrida completa con acceptance aflojada en `assets/eval/snake_retro_singlefile.json`
(`requestAnimationFrame` removido; se aceptan `<canvas`, `<style`, `<script`,
`getContext`, `addEventListener`, `keydown`, `score`). VRAM inicial: **494 MiB /
24 GB**. Probe previo directo a llama.cpp: **69.96 t/s** de generación, sano.

| Nivel | Score original | Score corregido esperado | Tiempo | avgTps | TTFT | Archivos relevantes |
|---|---:|---:|---:|---:|---:|---|
| `agent-chat` | 7/7 | 7/7 | 233.537 s | 84.612 | 4397 ms | `snake.html` |
| `agent-basico` | 7/7 | 7/7 | 249.878 s | 46.052 | 4548 ms | `snake_retro.html` |
| `agent-intermedio` | 7/7 | 7/7 | 517.606 s | 45.694 | 2876 ms | `snake_retro.html` + artefactos Playwright |
| `agent-avanzado` | 7/7 | 7/7 | 222.241 s | 41.995 | 4061 ms | `snake_retro.html` |
| `agent-maximo` | 1/7 | 7/7 | 270.074 s | 42.656 | 7482 ms | `snake_retro.html` |

HTMLs generados revisados en los `*_ws` de cada `runDir`: todos son single-file,
sin dependencias externas detectadas, y todos contienen los marcadores mínimos de
acceptance. En `agent-maximo`, el archivo generado también contiene todos los
marcadores; el `1/7` fue un falso negativo del evaluador porque los substrings se
buscaron en la respuesta final del agente tras el repair, no en el archivo escrito.
La traza muestra que el agente verificó el archivo con `findstr`, pero respondió
sólo texto explicativo y no repitió todos los marcadores.

Análisis de métricas: el `avgTps` reportado no es `tokens / elapsedSec`; es el
promedio de TPS de mensajes del backend. El tiempo total incluye carga de agente,
tool calls, escrituras, verificaciones y repairs. Por eso `agent-chat` puede mostrar
casi el doble de `avgTps` que `agent-basico` pero tiempos parecidos: chat generó
~7844 tokens en ~90 s de generación efectiva, mientras básico generó ~4472 tokens
en ~79 s de generación efectiva y el resto fue overhead/repair. Además, todos los
niveles tuvieron `firstAttemptScore=1/7` por el bug de scoring contra la respuesta;
los tiempos de esta corrida están inflados por reparaciones que no correspondían.

Artefactos para revisión manual: `docs/benchmark-levels-artifacts/2026-06-29/`,
con `agent-chat/`, `agent-basico/`, `agent-intermedio/`, `agent-avanzado/` y
`agent-maximo/`. Cada carpeta contiene `snake.html`, `result.json` y `metadata.json`.

Veredicto ajustado: con scoring corregido, los cinco niveles generaron un HTML
válido y autocontenido para la acceptance mínima. La hipótesis de que `agent-chat`
gana siempre en tiempo/calidad **no queda validada** por esta corrida: `agent-avanzado`
fue el menor elapsed y todos los HTML pasan los checks mínimos. Sí queda evidencia
de sobre-uso de herramientas en `agent-intermedio`, que tardó más del doble y dejó
artefactos Playwright para una tarea autocontenida.
