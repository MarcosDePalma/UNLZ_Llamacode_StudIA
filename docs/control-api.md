# Control API (headless)

API HTTP que **espeja todo `AppController`** para manejar LlamaCode sin GUI: tests
automatizados, scripting, CI, o un agente (IA) operando la app. Reflexión automática
vía meta-object de Qt — expone los mismos `Q_INVOKABLE` y `Q_PROPERTY` que usa el
QML, sin código por función.

Implementación: `src/core/ControlApi.{h,cpp}`, instanciado en `src/main.cpp`.

Para ejecutarla sin interfaz:

```powershell
$env:LLAMACODE_CONTROL_PORT = 8765
.\LlamaCode.exe --agent-daemon
```

`--headless` es alias. En este modo no se carga QML y el núcleo realiza el escaneo
de arranque. La instancia única evita que una GUI y un daemon muten
simultáneamente los mismos stores.

> **Empezá por acá:** `GET /help` (índice autodescriptivo) y `GET /methods`
> (métodos + propiedades + sub-targets del momento). Ante un nombre desconocido,
> el error te devuelve la lista `available` de nombres válidos — no hace falta leer
> el código para descubrir la superficie.

## Habilitar / puerto
- Escucha en **`http://127.0.0.1:<port>`** — **solo localhost**, no exponer.
- Puerto: env **`LLAMACODE_CONTROL_PORT`** (default **8765**). `0` = desactivado.
- Se levanta al iniciar la app (la GUI puede estar abierta; la API funciona igual).

```powershell
$env:LLAMACODE_CONTROL_PORT = "8765"   # o dejar default
.\build\Release\LlamaCode.exe
```

## Endpoints

| Método | Ruta | Body | Devuelve |
|--------|------|------|----------|
| GET  | `/` o `/help` | — | índice autodescriptivo (endpoints + ejemplo de flujo) |
| GET  | `/health` | — | `{"ok":true}` |
| GET  | `/methods` | — | `{methods, properties, targets}` del target (`?target=...`) |
| GET  | `/prop?name=X` | — | `{name, value}` |
| POST | `/setprop` | `{name, value}` | `{ok:true}` |
| POST | `/invoke` | `{method, args:[...]}` | `{ok:true, result?}` |

- `Content-Type: application/json`. CORS abierto (`Access-Control-Allow-Origin: *`).
- Toda respuesta incluye `reqId`. El cliente puede enviarlo como `?reqId=...`,
  campo JSON `reqId`, header `x-req-id` o header `reqid`; si falta, la app genera
  uno y lo devuelve. Los errores agregan `ok:false` sin perder `error`/`available`.
- `args` se convierten al tipo de cada parámetro (QString/bool/int/double/QStringList/QVariantMap…).
  Para un parámetro `QStringList`, pasá un array JSON: `"args":[["a","b"]]`.
- El match de método es por **nombre + aridad** (cantidad de args).
- Acciones de la GUI que son **setters de propiedad** (no `Q_INVOKABLE`) se cambian con
  `/setprop`, no con `/invoke` (ej. `agentApprovalMode`, `agentThinkingEnabled`).
- Todo corre en el **hilo de `AppController`** → seguro y síncrono. Las operaciones de
  por sí asíncronas (cargar modelo, stream del agente, benchmark) siguen siendo
  asíncronas: hay que **pollear propiedades** para ver el resultado.

### Respuestas y errores (forma estable)
- `/prop` → `{"name":"X","value":...}`  ⟵ el valor está en **`value`**, no en `result`.
- `/invoke`, `/setprop` → `{"ok":true, "result":...}` (result solo si el método devuelve algo).
- Error → `{"error":"...", "available":[...]}`. `available` lista los nombres válidos
  (propiedades, métodos `nombre/aridad`, o sub-targets según el caso). Aridad equivocada
  de un método existente → `{"error":"aridad incorrecta...", "validArgCounts":[...]}`.

## Sub-targets (navegar registries/sub-objetos)

Todo endpoint acepta `target` (query en GET, campo JSON en POST): una ruta de
propiedades `QObject*` separadas por punto. Vacío = `AppController` raíz. Listá los
hijos con `GET /methods` (campo `targets`). Ejemplo — re-escanear modelos del disco:

```bash
curl "localhost:8765/prop?target=rootRegistry&name=count"
curl -XPOST "localhost:8765/invoke?target=rootRegistry" -d '{"method":"scanAll","args":[]}'
curl "localhost:8765/prop?target=rootRegistry&name=scanning"   # esperar false
```

## Cheat-sheet (agente / servidor / benchmark)

Invocables (`/invoke`) — verificados en uso:
- `launchMenu()` → lista de perfiles de lanzamiento `{id, alias, name, ready, ...}`.
- `startServerAndAgent(launchId)` — carga el modelo **y** programa el agente al estar listo.
- `startServer(launchId)` / `stopServer()` — solo el llama-server.
- `startAgent(launchId)` / `stopAgent()` — solo el backend de agente (requiere server arriba).
- `sendToAgent(text)` / `sendToAgentWithAttachments(text, [paths])`.
- `importEvalSuite(absPath)` → id de custom benchmark.
- `startCustomBenchmark([launchIds], customId, passes, target, timeoutSec, agentProfileId)`
  — `target="agent"`, `agentProfileId` fija el **NIVEL** (`agent-chat|agent-basico|
  agent-intermedio|agent-avanzado|agent-maximo`). Requiere el agente corriendo.
- `startNextPendingBenchmark(profileId, passes, target, timeoutSec, agentProfileId)`
  — selecciona y ejecuta únicamente la siguiente etapa faltante de ese perfil
  elegible, en orden HE0 → HE20 → BCB. Rechaza perfiles retirados/no listos o
  bloqueados, y limita cada etapa a 1800 segundos (30 minutos).
- `cancelBenchmark()`.
- `runStartupScan()` — re-escaneo general (hardware + modelos).
- `configureClaudeDesktop(launchProfileId)` — levanta el gateway si hace falta y
  escribe el perfil local Third-Party Inference de Claude Desktop, con aliases
  `claude-llamacode-*`; crea respaldos antes de modificar los JSON.

Propiedades (`/prop`, y `/setprop` si son escribibles):
- `serverState` (read: `"stopped"|"running"|...`), `serverRunning` (bool).
- `agentRunning`, `agentStarting`, `agentMessages`, `agentStreamingText`, `agentLog`.
- `agentContextUsed`, `agentContextLimit`.
- `agentApprovalMode` (read/write: `auto|ask|manual|super|plan`).
- `agentThinkingEnabled` (read/write: bool).
- `activeAgentProfileId` (read/write: fija el nivel del agente fuera del benchmark).
- `benchmarkRunning`, `benchmarkProgress`, `benchmarkStatus`, `benchmarkResults`.
- `benchmarkCoverage` — matriz viva por perfil del catálogo, incluyendo
  `benchmarkEligible`, `coverageState` (`complete`, `incomplete`, `not-ready`,
  `retired`), `stageStates` (`valid`, `pending`, `blocked`, `infra-timeout`),
  `pendingStages`, `nextStage` y los códigos de salud. Se calcula contra la
  huella efectiva actual; un resultado histórico con otra configuración no
  cierra la etapa.

Sub-targets útiles: `rootRegistry` (modelos en disco: `add/remove/scan/scanAll/refresh`,
prop `count`/`scanning`), `modelCatalog`, `profileManager`, `binaryRegistry` y
`auxiliaryScheduler`.

### Scheduler de operaciones auxiliares

`auxiliaryScheduler` es una cola separada del scheduler cron de Tasks. Permite que
un sidecar de embeddings, rerank, STT u otra operación CPU tenga límite por clase,
exclusión por recurso, prioridad y estado observable:

```powershell
$aux = "$base/invoke?target=auxiliaryScheduler"
$jobBody = @{method="enqueue"; args=@("retrieval", "cpu-embed", 5, "RAG")} |
  ConvertTo-Json -Compress
$job = (Invoke-RestMethod $aux -Method Post -ContentType application/json -Body $jobBody).result
Invoke-RestMethod $aux -Method Post -ContentType application/json `
  -Body (@{method="startNextJob"; args=@()} | ConvertTo-Json -Compress)
Invoke-RestMethod $aux -Method Post -ContentType application/json `
  -Body (@{method="complete"; args=@($job, $true, "ok")} | ConvertTo-Json -Compress)
Invoke-RestMethod "$base/prop?target=auxiliaryScheduler&name=jobs"
```

Las llamadas anteriores llevan `target=auxiliaryScheduler`; `auxiliaryJobs` en la
raíz de `AppController` ofrece el mismo snapshot para QML o polling. El scheduler
no ejecuta procesos por sí solo: el worker/sidecar que realiza el trabajo debe
avanzarlo con `startNextJob()` y cerrarlo con `complete(id, ok, detail)` o
`cancel(id, detail)`.

## Ciclo de vida del agente (importante)

`startServerAndAgent(launchId)` deja el agente **programado** para arrancar cuando el
server esté listo. Pero si arrancás el server por otro camino (o reintentás
`startServer` solo), el agente **no** se inicia solo: hay que llamar `startAgent`
explícitamente una vez que `serverState == "running"`.

```
startServerAndAgent(id)           # camino feliz
  └─ poll serverState == running
  └─ poll agentRunning == true
# si agentRunning no llega a true (p.ej. usaste startServer suelto):
startAgent(id)                    # arranca el agente contra el server ya cargado
```

## Ejemplo 1 — chatear con el agente (PowerShell)

```powershell
$base = "http://127.0.0.1:8765"
function Inv($m,$a){ Invoke-RestMethod "$base/invoke" -Method Post -ContentType application/json -Body (@{method=$m;args=$a}|ConvertTo-Json -Compress) }
function SetP($n,$v){ Invoke-RestMethod "$base/setprop" -Method Post -ContentType application/json -Body (@{name=$n;value=$v}|ConvertTo-Json -Compress) }
function Prop($n){ (Invoke-RestMethod "$base/prop?name=$n").value }   # OJO: .value, no .result

$id = (Inv "launchMenu" @()).result[0].id          # o el id que quieras
Inv "startServerAndAgent" @($id)
while ((Prop "serverState") -ne "running") { Start-Sleep 2 }
if (-not (Prop "agentRunning")) { Inv "startAgent" @($id); Start-Sleep 3 }

SetP "agentApprovalMode" "super"                   # acceso total + sin prompts
Inv "sendToAgent" @("Leé README.md y resumilo")
do { Start-Sleep 2; $m = Prop "agentMessages" } while ($m[-1].typing)
$m[-1].content
```

## Ejemplo 2 — benchmark por NIVEL de agente

Compara los 5 niveles sobre una EvalSuite (mismo modelo, distinto perfil de agente).

```powershell
$id = "<launchId>"                                  # de launchMenu()
Inv "startServerAndAgent" @($id)
while ((Prop "serverState") -ne "running") { Start-Sleep 2 }
if (-not (Prop "agentRunning")) { Inv "startAgent" @($id) }

$custom = (Inv "importEvalSuite" @("C:/.../assets/eval/snake_retro_singlefile.json")).result

foreach ($lvl in "agent-chat","agent-basico","agent-intermedio","agent-avanzado","agent-maximo") {
    Inv "startCustomBenchmark" @(@($id), $custom, 1, "agent", 2400, $lvl)
    while ((Prop "benchmarkRunning")) { Start-Sleep 15; (Prop "benchmarkStatus") }
}
(Prop "benchmarkResults")        # qualityScore, elapsedSec, avgTps, agentFiles, response...
```

## Gotchas (probados en vivo)

- **`startServer abort: No model selected`** (en `logs/server.log`, mientras `serverState`
  queda `stopped`): el modelo del perfil no está en el catálogo de esta sesión. Re-escaneá
  los roots de modelos y reintentá:
  `POST /invoke?target=rootRegistry {"method":"scanAll","args":[]}` → esperar `scanning=false`.
- **El agente no arranca solo**: ver "Ciclo de vida del agente" arriba — `startAgent` tras
  `serverState=running`.
- **Benchmark de agente con `target="agent"` y nada cargado** → corre en ~0s con
  `qualityScore 0` (no hay agente). Cargá server + agente **antes**.
- **`/prop` devuelve `value`**, no `result`. Confundirlos es el bug más común al scriptear.

## `custom` vs `opencode`

`startAgent(launchId)` usa el adapter del harness del perfil: `custom` (backend nativo
interno, sin proceso externo) u `opencode` (`opencode serve`). Para forzar el nativo, usá
un `launchId` con harness `custom`.

## Troubleshooting

- `agentLog` con `mcp 0 tool(s) descubiertas`: revisá `listMcpServers("global","")` /
  `listMcpServers("project","<dir>")` y que el comando MCP exista en PATH.
- `serverState` vuelve a `stopped` durante un turno: ver
  `%LOCALAPPDATA%\LlamaCode\LlamaCode\logs\server.log`; relanzar `startServer` y luego `startAgent`.
- Durante un benchmark, un crash de `llama-server` no dispara el watchdog: el benchmark conserva el fallo como infraestructura y evita que un segundo server se mezcle con la pasada. En una ejecución manual sí se mantiene el reinicio automático con backoff. Revisá `server.log` y `benchmarkStatus` antes de reintentar.
- Contexto roto por intentos previos: crear sesión nueva antes de reenviar el prompt.
- Diagnóstico: server llama.cpp → `logs/server.log`; agente → `logs/agent.md`/`agent.log`.

## Notas
- Solo localhost; pensado para dev/test/scripting, no producción.
- Parsing HTTP mínimo (headers + `Content-Length`); una request por conexión.
- La superficie es **viva**: lo expuesto = los `Q_INVOKABLE`/`Q_PROPERTY` de `AppController`
  y sus sub-objetos en esta build. Si un método/propiedad no aparece en `/methods`, no existe
  en esta versión.
# Salas multiagente

La API introspectiva expone `agentRoomStore` como sub-target:

```powershell
curl "localhost:8765/methods?target=agentRoomStore"
curl "localhost:8765/prop?target=agentRoomStore&name=rooms"
curl -X POST localhost:8765/invoke -d '{"method":"createAgentRoom","args":["Revisión","C:/repo"]}'
curl -X POST localhost:8765/invoke -d '{"method":"runAgentRoomPreset","args":["<roomId>","review","Implementar y validar el cambio"]}'
```

También se puede invocar `postEvent`, `participants`, `events`,
`updateGrant` y `compactContext` directamente con
`"target":"agentRoomStore"`. El servidor continúa limitado a localhost. Los
grants de participantes existentes son monotónicos: una actualización puede
revocar capacidades, pero no añadir autoridad.
