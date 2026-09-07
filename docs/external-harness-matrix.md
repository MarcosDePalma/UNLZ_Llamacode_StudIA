# Matriz externa de harnesses

LlamaCode puede comparar harnesses externos sin asumir que sus CLIs son iguales.
El runner `tools/harness_matrix.py` ejecuta una suite versionada contra procesos
adaptadores y persiste una matriz reproducible con éxito, latencia y calidad de
tools.

## Contrato `llamacode-harness-v1`

El runner inicia el comando configurado una vez por tarea y pasada. Le envía una
línea JSON por `stdin`:

```json
{
  "protocol": "llamacode-harness-v1",
  "requestId": "4242-1-0-openclaw-task-001",
  "suite": {"id": "harness_multidomain_v1", "name": "Harness Multi-domain v1"},
  "pass": 1,
  "task": {
    "id": "task-001",
    "category": "coding",
    "prompt": "...",
    "maxTokens": 4096,
    "acceptance": {}
  }
}
```

El adaptador debe emitir exactamente una línea JSON por `stdout`, conservando
`protocol` y `requestId`:

```json
{
  "protocol": "llamacode-harness-v1",
  "requestId": "4242-1-0-openclaw-task-001",
  "ok": true,
  "passed": true,
  "elapsedMs": 1234,
  "response": "...",
  "toolCalls": [
    {"tool": "read_file", "arguments": {"path": "README.md"}, "completed": true, "ok": true}
  ]
}
```

Los logs deben ir a `stderr`. El runner no usa shell para ejecutar comandos y
rechaza respuestas truncadas, JSON inválido, protocolo incorrecto, `requestId`
incorrecto, timeouts y comandos con formato ambiguo.

## OpenClaw, Hermes, Pi y Nanobot

No se hardcodean comandos de terceros: sus versiones y modos de ejecución
cambian. El archivo [harness_adapters.example.json](../assets/benchmarks/harness_adapters.example.json)
declara los cuatro IDs y deja el comando en variables de entorno. Cada wrapper
local debe traducir su CLI al contrato anterior.

```powershell
$env:LLAMACODE_HARNESS_OPENCLAW_CMD = '["openclaw","llamacode-adapter"]'
$env:LLAMACODE_HARNESS_HERMES_CMD = '["hermes","llamacode-adapter"]'
python tools\harness_matrix.py `
  --suite assets\benchmarks\custom\harness_multidomain_v1.json `
  --manifest assets\benchmarks\harness_adapters.example.json `
  --adapters openclaw,hermes `
  --passes 5 --seed 4242 --out harness-matrix.json
```

El adaptador debe ser el único lugar que conozca cómo pedirle una tarea al
harness externo. Así se pueden comparar versiones sin mezclar diferencias de
CLI con diferencias de suite.

## Lectura del resultado

El runner baraja el producto adapter × tarea dentro de cada pasada, pero conserva
la misma cantidad de muestras por adapter. `comparisons` informa deltas pareados
de éxito, latencia, F1 de tools y redundancias. `null` significa que no hubo
datos comparables; no se convierte en cero.

Para corrección de tools una tarea puede declarar `acceptance.toolCalls`. El
runner y LlamaCode calculan el mismo contrato: llamadas válidas, completas,
exitosas, fallidas, redundantes, inesperadas, precision/recall/F1 y
`sequenceExact`. Si una suite no declara expectativas, la corrección queda
desconocida aunque la telemetría de fallos y redundancias siga disponible.
