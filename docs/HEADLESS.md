# Operación headless

LlamaCode puede ejecutarse sin QML con `--agent-daemon` (alias `--headless`).
La API local se documenta en [`control-api.md`](control-api.md).

## Smoke test de perfiles de personalidad y estilo

El test completamente offline es:

```powershell
.\tests.bat Debug
```

Para aislarlo durante desarrollo:

```powershell
cmake --build build_tests --config Debug --target test_agent_profiles
ctest --test-dir build_tests -C Debug -R test_agent_profiles --output-on-failure
```

Ese test no requiere GUI, llama-server, modelo, red, GPU ni escritorio visible.
Usa `QTemporaryDir` para que la persistencia no toque los perfiles del usuario.

La prueba HTTP equivalente arranca el daemon con un puerto localhost y un
`LLAMACODE_PROFILES_DIR` temporal, consulta `profileManager` mediante
`/methods?target=profileManager` y ejecuta sus `Q_INVOKABLE` con `/invoke`.

Para exportar o importar perfiles completos sin QML, usar los invocables
`exportProfilesBundle()` e `importProfilesBundle(json)`. El bundle incluye sólo
perfiles de usuario, conserva ids y referencias, y devuelve la cantidad de
entradas importadas (`-1` ante JSON o schema inválido).
El procedimiento específico está en
[`personality-style-profiles.md`](personality-style-profiles.md).

La extracción asistida se dispara sobre `AppController` y es asíncrona:

```powershell
$body = @{ method = "analyzePersonaStyleProfile"; args = @("<profileId>", "Mi muestra...") } |
  ConvertTo-Json -Compress
Invoke-RestMethod "http://127.0.0.1:8876/invoke" -Method Post `
  -ContentType application/json -Body $body
Invoke-RestMethod "http://127.0.0.1:8876/prop?name=personaStyleAnalysisStatus"
Invoke-RestMethod "http://127.0.0.1:8876/prop?name=personaStyleAnalysisError"
```

El estado esperado es `running` → `ready` o `error`. El test headless no debe
exigir un modelo real para validar parsing: para eso usa la respuesta JSON
directamente con `applyPersonaStyleAnalysis`. La llamada al backend es un smoke
test opcional y requiere que el perfil activo tenga server o backend cloud.

Los tests que necesiten escritorio, navegador headed, audio físico o interacción
visual no son parte de este smoke test: deben estar separados y marcados como
QA opt-in, nunca ser un requisito del gate headless.

El smoke también valida hardware/performance sin GPU real:

```powershell
.\tests\headless_smoke.ps1 -Exe .\build\Debug\LlamaCode.exe
```

Comprueba `runStartupScan`, `hardwareSummary`, `performanceRecommendation` y
el circuito de matriz (`performanceMatrixCandidates`,
`annotatePerformanceMatrix`, `rankPerformanceMatrix`) con una muestra
sintética compatible con los artefactos reales. Si `nvidia-smi` no existe, el
caso sigue siendo válido: debe publicar fingerprint, fallback CPU y
`splitMode=layer`.

El script arranca el daemon con `--agent-daemon --handoff-ui` para que una
instancia Debug abierta no interfiera con la prueba; `--handoff-ui` no carga
QML. Si se usa otro ejecutable, debe tener su runtime Qt desplegado junto al
binario: el ejecutable crudo de `build_tests/Debug` puede fallar por DLL
faltantes; usar `build/Debug` o configurar `PATH`/`QT_PLUGIN_PATH`.

## Qué debe probarse headless

La matriz mínima para Tasks, loops y workflows es:

| Capa | Cómo se ejecuta | Requiere modelo | Gate |
|---|---|---:|---:|
| `TaskStore` / `WorkflowEngine` | `test_tasks`, `test_agent_efficiency` | No | Sí |
| ciclo de `AppController` | `test_appcontroller` con `FakeAgentBackend` | No | Sí |
| hardware/topología/matriz de rendimiento | `test_hardware_diagnostics` + `headless_smoke.ps1` | No | Sí |
| API HTTP reflexiva | `test_control_api` | No | Sí |
| daemon real + ControlApi | smoke manual/CI separado | No para CRUD/validación | Recomendado |
| loop con inferencia real | daemon + modelo local + `llama-server` | Sí | Opt-in |
| desktop/OCR/audio/headed browser | probes QA específicos | Sí o hardware | Nunca |

Los tests unitarios y de `AppController` no deben abrir ventanas ni depender de
QML, GPU, red o un modelo descargado. `runTaskBodyForTest()` existe únicamente
para ese propósito: inyecta un backend falso y permite probar iteraciones,
`GOAL_MET`, `GOAL_NOT_MET`, reanudación, aprobación, errores y límites sin
inferencia real.

## Auditoría read-only de frugalidad

`review_overengineering` se prueba completamente headless desde
`test_agent_tools`. El caso usa un repositorio Git temporal dentro de
`QTemporaryDir`, ejecuta Git con `QProcess::setWorkingDirectory()` y nunca
cambia el directorio global del proceso. Por eso es seguro con CTest paralelo.

Ejecutar sólo este bloque durante desarrollo:

```powershell
cmake --build build_tests --config Debug --target test_agent_tools
ctest --test-dir build_tests -C Debug -R test_agent_tools --output-on-failure
```

La cobertura incluye:

- diff normal y repositorio sin cambios;
- diff `staged`;
- archivos no rastreados;
- scope inválido;
- truncamiento por `max_diff_chars`;
- garantía de salida `readOnly` y ausencia de escritura;
- repositorio sin `.git` y error controlado.

No requiere QML, modelo, llama-server, GPU, red, navegador ni escritorio
visible. Requiere `git` en `PATH`, igual que la capacidad productiva que audita.
La tool no incluye automáticamente el contenido de archivos no rastreados:
los informa mediante `untrackedPresent` para que el usuario decida si debe
incorporarlos a una revisión posterior.

## Cobertura de UI respaldada por APIs headless

Las funciones visuales de historial y diagnóstico también tienen contrato
headless: `searchChatHistory(query)` devuelve resultados con snippet para que un
cliente pueda reproducir la búsqueda sin QML; `serverLogByLevel(level)` filtra
el log por `all`, `error`, `warn`, `stderr`, `stdout`, `lifecycle`, `health` o
`diag`; y `serverDiagnostic(level,message)` conserva los diagnósticos que la
UI muestra como aviso no bloqueante. El build QML valida que ChatPage y
LaunchPage consuman esos contratos; las pruebas de datos viven en
`test_backends_net` y `test_appcontroller`.

El backend raw también acepta sampling por sesión (`temperature`, `topP`,
`topK`, `minP` y `repeatPenalty`), lo persiste en el JSON de la sesión y lo
envía en cada request sólo cuando está configurado. Cada respuesta registra
`firstTokenMs`. `test_backends_net` verifica payload, persistencia y medición
contra un stub HTTP local, sin modelo ni GPU.

La búsqueda de perfiles también es headless: `ProfileManager::launchProfilesForProfilesPage(query)`
filtra por nombre, alias, id o etiquetas sin depender de QML. `setLaunchTags`
normaliza etiquetas (trim, elimina vacías y duplicadas sin distinguir mayúsculas)
y `markLaunchUsed` persiste el epoch en milisegundos; los menús ordenan por
BEST, favorito, último uso y número incremental. La regresión
`manager_profileSearchFiltersNameAliasAndId` cubre mayúsculas/minúsculas, alias,
id y resultado vacío; `manager_tagsAndLastUsed` cubre normalización, búsqueda por
etiqueta, persistencia del último uso y round-trip JSON. Se puede ejecutar sin
GUI con:

```powershell
cmake --build build_tests --config Debug --target test_profiles
ctest --test-dir build_tests -C Debug -R test_profiles --output-on-failure
```

## Benchmark Honey A/B headless

El benchmark de agente se ejecuta siempre en workspaces temporales y headless.
Seleccioná dos perfiles equivalentes, uno con la directiva `honey` y otro sin
ella, y ejecutá la misma suite con el mismo modelo, hardware, seed y cantidad
de pasadas. Cada resultado persiste:

- `agentVariant`: `baseline` o `honey`;
- `honeyEnabled`;
- `complexityMetrics.filesChanged`;
- `complexityMetrics.filesCreated` y `filesDeleted`;
- `complexityMetrics.addedLines` y `removedLines`.

`comparison.json` agrega medianas por perfil y deltas de complejidad entre
variantes. No se necesita una ventana gráfica. La suite puede iniciarse por
ControlApi con `startCustomBenchmark(...)`; consultar `benchmarkRunning`,
`benchmarkProgress` y `benchmarkResults` por polling. Honey sólo se considera
mejor si no reduce calidad/éxito ni aumenta regresiones.

Las sesiones de chat también exponen sampling reproducible sin GUI:
`temperature`, `topP`, `topK`, `minP` y `repeatPenalty` se guardan en el JSON de
la sesión y se envían al request `/v1/chat/completions` sólo cuando están
configurados. Cada respuesta registra `firstTokenMs`; `test_backends_net`
verifica payload, reinicio de sesión y medición contra un stub HTTP local.

## Aislamiento de un smoke que levanta el binario

`LLAMACODE_PROFILES_DIR` redirige los perfiles, pero **no** el resto: memoria,
catálogo, chat y directivas cuelgan de `QStandardPaths::AppLocalDataLocation`,
que en Windows Qt resuelve por la API de shell — pisar `%LOCALAPPDATA%` no hace
nada. Para eso está `LLAMACODE_TEST_MODE=1`: `main.cpp` llama a
`QStandardPaths::setTestModeEnabled(true)` y todo cae en
`AppData/Local/qttest/...`, igual que en los tests C++.

Sin esa variable, un smoke escribe en la instalación real del usuario.

## A/B de harness headless

Comparar dos configuraciones de harness (mismo modelo, distinto `HarnessSpec`)
no necesita GUI: `startBenchmark`/`startCustomBenchmark` aceptan `agentProfileId`,
y `compareHarnessBenchmarks(agentProfileIds, runDir)` agrupa las corridas
guardadas por perfil de AGENTE en vez de por modelo. `tools\harness_ab.ps1`
orquesta las dos corridas y escribe el informe:

```powershell
powershell -File tools\harness_ab.ps1 -LaunchProfileId <launch> `
  -AgentProfileIds agent-intermedio,agent-minimal -Passes 3 -Port 8877
```

Los verbos del harness modular (`harnessPackCatalog`, `agentProfileSpec`,
`setAgentProfileSpec`, `agentProfileDiff`, `harnessSpecSummary`,
`harnessDirectiveCatalog`) viven en el target `profileManager`.

## Smoke real del daemon y una Loop vía ControlApi

Este procedimiento valida el contrato que usarían CI o un cliente externo. No
usa QML. Para CRUD, validación y persistencia no necesita modelo; para ejecutar
el cuerpo de una Task sí necesita un agente local activo.

```powershell
$env:LLAMACODE_CONTROL_PORT = "8877"
$env:LLAMACODE_PROFILES_DIR = Join-Path $pwd "headless-profile-test"
New-Item -ItemType Directory -Force $env:LLAMACODE_PROFILES_DIR | Out-Null
Start-Process -FilePath ".\build\Debug\LlamaCode.exe" -ArgumentList "--agent-daemon" `
  -WindowStyle Hidden

$base = "http://127.0.0.1:8877"
function Inv($target, $method, $args) {
  $body = @{ method = $method; args = $args } | ConvertTo-Json -Depth 12 -Compress
  Invoke-RestMethod "$base/invoke?target=$target" -Method Post `
    -ContentType "application/json" -Body $body
}
function Prop($target, $name) {
  (Invoke-RestMethod "$base/prop?target=$target&name=$name").value
}

Invoke-RestMethod "$base/health"
Inv "taskStore" "save" @("headless-loop", @{
  name = "Headless loop smoke"
  description = "Verificar un objetivo local"
  loopEnabled = $true
  loopGoal = "el agente confirmó la verificación"
  loopMaxIterations = 3
  loopMaxSeconds = 1800
})

# Con un agente local ya iniciado:
Inv "" "runTask" @("headless-loop")
while ([bool](Prop "" "taskRunning")) {
  "phase=$((Prop "" "runningTaskPhase"))"
  Start-Sleep 2
}
Prop "taskStore" "count"
Inv "taskStore" "get" @("headless-loop")
```

El smoke debe comprobar `lastRunStatus`, `lastRunSummary`, el número de
iteraciones y el estado persistido. Si se prueba sin modelo, limitarse a
`save`, `get`, `validateWorkflow`, `engineeringWorkflows` y la persistencia;
no interpretar una ejecución sin agente como un test de calidad del loop.

Al terminar, detener el daemon y borrar el directorio temporal de perfiles. No
usar el perfil de producción ni el puerto habitual si puede existir una GUI.

El smoke reproducible está disponible en `tests/headless_smoke.ps1`:

```powershell
.\tests\headless_smoke.ps1 -Exe .\build\Debug\LlamaCode.exe
```

Ese modo no carga un modelo y valida daemon, hardware/topología, matriz de
rendimiento, API, CRUD, persistencia y
validación de workflow. Para ejecutar además el cuerpo del loop contra un
modelo local ya configurado:

```powershell
.\tests\headless_smoke.ps1 -RunModelLoop -LaunchId "<launchId>"
```

El segundo modo es optativo y no forma parte del gate porque depende de modelo,
VRAM, tiempos de inferencia y disponibilidad del perfil local.

La auditoría de perfiles también es headless: `ProfileManager.profileChangeHistory`
lee snapshots JSONL append-only por entidad/id. `test_profiles` cubre alta,
actualización, lectura del historial y persistencia sin QML ni GUI.

Las plantillas de launch también son headless: `saveLaunchAsTemplate` guarda
referencias, args, variables de entorno y etiquetas; `createLaunchFromTemplate`
crea una copia editable y `removeProfileTemplate` la elimina. El round-trip
está cubierto por `manager_profileTemplatesRoundTrip` en `test_profiles`.

Para verificar que la persistencia sobrevive a un reinicio del daemon:

```powershell
.\tests\headless_smoke.ps1 -TestRestartPersistence
```

Este smoke no ejecuta inferencia: sólo comprueba que el proceso pueda detenerse,
volver a iniciarse y recuperar la Task desde el store persistente.

Para probar el proceso companion del scheduler sin tocar el registro de inicio
del sistema ni requerir que la programación global esté activada:

```powershell
.\tests\headless_smoke.ps1 -TestSchedulerDaemon
```

El script usa `LLAMACODE_SCHEDULER_SMOKE=1`, espera el heartbeat mediante
`schedulerDaemonStatus` y mata el proceso al finalizar.

## Smoke de workflows de ingeniería

```powershell
powershell -ExecutionPolicy Bypass -File .\tests\headless_engineering_workflows.ps1
```

El script usa un puerto localhost efímero y un directorio temporal de perfiles.
Sin QML ni modelo consulta el catálogo por `/invoke`, valida `qa`, instala la
Task, comprueba su persistencia y cierra el daemon incluso ante errores. La
política de seguridad se prueba además con `test_task_security_policy` y la
barra de presets mediante el contrato headless de `AppController`; el panel se
valida además durante el build QML.

Para ejecutar el gate compuesto de workflows, sin depender de QML, modelo ni
GPU:

```powershell
.\tests\headless_engineering_gate.ps1 -Build
```

El gate compila y ejecuta `test_engineering_workflows`,
`test_task_security_policy`, `test_tasks`, `test_appcontroller` y
`test_control_api`, y luego ejecuta el smoke HTTP del daemon. Sin `-Build`,
reutiliza los binarios Debug ya compilados. Todos los procesos usan
`QT_QPA_PLATFORM=offscreen` en los tests Qt/QML; el daemon se ejecuta sin esa
variable porque no carga QML. Ambos usan perfiles temporales y un puerto
localhost efímero; no modifican el perfil del usuario.
