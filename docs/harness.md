# El harness de agentes de LlamaCode

Documento de referencia del **harness**: qué es, cómo está armado, qué garantías
da y dónde tocar cada cosa. Refleja el código real (`src/core/agent/`), no el plan
histórico.

Documentos relacionados (y su estado):
- `docs/plan_harness.md` — **histórico**. Plan por etapas de 2025/26; nombra
  `CustomBackend`, que hoy se llama `LlamaAgentBackend`. Útil para el porqué de
  las decisiones (p.ej. descartar Goose), no para el estado actual.
- `docs/agent.md` — descripción del modo Agente; también usa el nombre viejo
  `CustomBackend` en varias secciones.
- `docs/plan-harness-modular.md` — diseño del harness modular (implementado).
- `docs/plan-harness-cierre.md` — plan de cierre (F0–F5), **implementado**.
- `docs/codehamr_harness_review.md` — revisión externa que originó
  `sanitizeApiMessagesForWire`, el idle-watchdog de SSE y el kill de árbol de shell.
- `docs/agent-efficiency.md`, `docs/agent-workflows.md`, `docs/skills.md`,
  `docs/evidence.md`, `docs/HEADLESS.md`, `docs/control-api.md`.
- `docs/managed-agent-runs.md` — corridas largas administradas de Claude Code y
  Codex, con prompt durable, manifiesto, logs y cierre auditable.

---

## 0. Harness modular (`HarnessSpec`)

Desde la implementación del plan modular, un perfil de agente es una
**composición declarativa de módulos**, no un preset cerrado.
`src/core/profiles/HarnessSpec.h` define once piezas:

| Módulo | Gobierna |
|---|---|
| `tools` | packs + include/exclude + gating de tools MCP |
| `skills` | allowlist/denylist de skills portables por slug |
| `prompt` | directivas built-in + directivas propias (.md) + `systemExtra` + tope de tamaño |
| `loop` | créditos del governor, `sameCallLimit`, `failureSpiral`, `transportRetries`, watchdogs, idle de stream |
| `context` | compactación (on/off + umbral + cola), poda, `keepLastImages`, read-dedup, preflight, warmup |
| `permissions` | `approvalMode`, reglas por patrón, scope de FS, guardrails |
| `escalation` | cap de sub-agentes, aislamiento en worktree, cadena y gatillo del maestro, umbrales del `DifficultyRouter` |
| `protocol` | `auto` / `native` / `text`, leak-guard de thinking, temperatura, reasoning |
| `memory` | hechos de `MemoryStore` inyectados, memoria de proyecto (+ tope), consolidación al salir |
| `knowledge` | paquete de memoria + grafo, preflight, citas y límites de hechos/edges/caracteres |
| `chat` | modo Chat sin tools: sampling, thinking, persona diseñadora, instrucciones persistentes |
| `runtime` | contrato de ejecución: `legacy` o `next`, versión, fallback y flag experimental |
| `worker` | lane Node/Python, entrypoint, límites, capacidades solicitadas y sandbox OS |
| `phases` | overrides por fase: `plan`, `exec`, `verify`, `goalCheck` |

Tres invariantes, todas testeadas:

El módulo runtime agrega una selección explícita y reversible. Si falta, el
motor efectivo es legacy (versión 1) y conserva las rutas anteriores. El perfil
agent-intermedio-next declara next (versión 2), mantiene las mismas capacidades
del perfil Intermedio y usa el namespace de almacenamiento agent_harness_next.
Cambiar de motor recrea el backend; nunca se reutiliza una instancia con una
sesión del otro contrato.

1. **Módulo ausente = heredado**, nunca vacío. Cada módulo lleva un flag `set`
   que distingue "no lo declaré" de "lo declaré vacío a propósito".
2. **Los defaults reproducen el comportamiento histórico.** Un perfil que no
   declara nada se comporta exactamente como antes de la feature.
3. **Un perfil nunca sube privilegios**: su scope de filesystem se *intersecta*
   con el de la Task (`HarnessPolicy::narrowerScope`), y `hitlDestructive:false`
   sólo tiene efecto si el perfil se declara en `approvalMode: "super"` — en
   cualquier otro modo `fromJson` lo vuelve a poner en `true`.

Resolución en capas: `defaults → cadena de extends (padre→hijo) → override de
fase`. `ProfileManager::resolveHarnessSpec` sube por `extends` hasta la raíz y
aplica de padre a hijo; un ciclo corta en el primer repetido (el perfil degrada,
no cuelga el arranque).

`AgentProfile` sigue siendo lo que se persiste: guarda `spec{}` + `extends`, y
`toSpec()` deriva un spec equivalente para los perfiles viejos que no lo tienen.
Al guardar un spec, los campos legacy se espejan (`applySpecToLegacyFields`) para
que un binario anterior lea el archivo sin romperse.

Headless (contrato obligatorio, ver `docs/HEADLESS.md`) vía `/invoke` sobre el
target `profileManager`: `harnessPackCatalog`, `harnessDirectiveCatalog`,
`agentProfileSpec`, `setAgentProfileSpec`, `agentProfileDiff`, `harnessSpecSummary`.

`memory` y `chat` cubren dos cosas que el spec no tocaba: cuánta memoria entra
al system prompt (eran 12 hechos y 64 KB de archivo, fijos y sin apagar — lo
primero que uno recorta en un perfil al límite de contexto) y el modo Chat, que
seguía usando los ajustes globales aunque el perfil dijera otra cosa. El `chat`
se aplica al `RawChatBackend` al crearlo y en cada cambio de perfil activo, y
suma `systemExtra`: instrucciones persistentes que van **primeras** en el
preamble, antes de las notas de formato.

`knowledge` agrega una capa opt-in sobre esas dos fuentes: `KnowledgePacket` reúne
hechos relevantes de `MemoryStore` y relaciones del `GraphStore`, conserva un
recibo y emite citas de archivo/rango cuando el grafo tiene evidencia. Sus edges
se etiquetan como verificados o no revisados; el prompt prohíbe convertir una
inferencia no revisada en un hecho. `preflight` controla si el paquete entra antes
del primer request y los topes del módulo protegen el presupuesto de contexto.

El backend también emite `agentLifecycleEvent` con eventos normalizados de sesión,
prompt, tools y sincronización de contexto. Después de una mutación nativa, un
`run_shell` que haya cambiado archivos o una tool externa que declare rutas, el
backend refresca los índices afectados; para shell compara snapshots de
`ProjectBrain` en vez de interpretar comandos específicos.

El editor vive en `qml/components/LcHarnessEditor.qml`: no conoce a `App` (todo
entra por propiedades y sale por señales), y por eso su lógica de edición —la
parte donde una regresión cuesta un perfil— se testea sola.

Los hechos que entiende el gate `when` salen de
`LlamaAgentBackend::directiveFactKeys()` y la UI los enumera
(`App.harnessDirectiveFacts()`): un test fija que el catálogo y los hechos que
realmente se evalúan no diverjan, porque un `when` con un nombre inexistente
deja la directiva fuera **en silencio**.

Directivas propias: se crean, editan y borran desde el editor
(`HarnessDirectiveStore::save`/`remove`, expuestas como `saveHarnessDirective` /
`removeHarnessDirective`). Hay una bundleada de ejemplo
(`assets/harness/directives/commit-conventions.md`) que se copia a la carpeta del
usuario la primera vez: sirve de plantilla y de smoke del descubrimiento.

Presets de sistema: los cinco niveles históricos más **`agent-minimal`**
(local-first duro: pocas tools, sin MCP, prompt ≤8000 chars, `sameCallLimit=2`,
sin capturas), **`agent-rpa`** (packs `core`+`rpa`, watchdog de 30 s, dos
capturas de contexto, guardrail firme) y **`agent-browser`** (packs
`core`+`web`+`browser`, timeout web de 300 s, dos imágenes retenidas y skills
portables apagadas por defecto).

`agent-browser` es una composición nativa inspirada en Browser Agent, no una
instalación del proyecto externo. Sirve para comparar el perímetro del
harness con el mismo modelo/runtime; una afirmación sobre BU Bench o
BrowseWebApp Bench requiere ejecutar esas suites de navegador por separado.

El inventario estable de perfiles, roles de sala, motores, adapters y skills
está en [`docs/agent-harness-inventory.md`](agent-harness-inventory.md).

---

## 1. Qué es "el harness"

Todo lo que rodea al modelo para que pueda **trabajar**: armado del contexto,
protocolo de tool-calling, ejecución de tools, permisos, presupuesto de
iteraciones, persistencia y telemetría. El modelo es intercambiable; el harness
es el producto.

Principio rector (heredado del plan original y todavía vigente): **la app no se
casa con un runtime de agente**. Todo lo valioso (aprobación, diffs, memoria,
compactación, robustez de protocolo) vive del lado harness-agnóstico.

```
QML (AgentPage / ChatPage / TasksPage)
   ↕ propiedades + señales
AppController                       fachada: espeja estado, resuelve perfil/secretos
   ↕ IAgentBackend                  interfaz común (src/core/agent/IAgentBackend.h)
LlamaAgentBackend | OpencodeBackend | RawChatBackend
   ↕ HTTP OpenAI-compat (/v1/chat/completions, /props, /v1/embeddings)
llama-server (local) o provider cloud
   ↕ señales en cola (QThread)
AgentToolRunner                     ejecuta tools nativas + MCP (worker thread)
   ↕
filesystem / shell / UIA-escritorio / browser / email / MCP stdio
```

---

## 2. Backends

`ensureAgentBackend(adapter)` en `AppController.cpp:4043` construye uno solo a la vez:

| adapter | clase | rol |
|---|---|---|
| `llamaagent` | `LlamaAgentBackend` | agente nativo. Loop ReAct propio, tools, HITL, subagentes. **El harness real.** |
| `opencode` | `OpencodeBackend` | harness externo (`opencode serve` + SSE). Legacy. |
| — | `RawChatBackend` | chat directo sin tools; sólo la página Chat, no seleccionable como agente. |

`IAgentBackend` es un `QObject` abstracto: la UI nunca conoce el backend concreto.
Superficie (ver el header para la lista completa):

- **Vida**: `start(AgentContext)`, `stop()`, `running()`.
- **Conversación**: `sendMessage`, `cancelGeneration`, `steerMessage` (interrumpe
  el turno y manda uno nuevo), `queueMessage` + edición/borrado de la cola,
  `prefillWarmup` (precalienta el prompt-cache mientras el usuario habla en Charla).
- **Sesiones**: new / switch / rename / delete / fork / refresh / `deleteProject`.
- **Permisos**: `approveTool(id, always)`, `rejectTool`, `setApprovalPolicy`,
  `setPermissionRules`, `revertEdit`.
- **Señales**: `runningChanged`, `turnFinished`, `messagesChanged`,
  `streamingText(index, content)` (update incremental de una burbuja, evita
  reconstruir la lista por token), `queueChanged`, `sessionsChanged`,
  `logAppended`, `toolApprovalNeeded`, `desktopActivityChanged`, `errorOccurred`,
  `contextUsage`, `contextManaged`, `gitRequired`, `chatTemplateDetected`.

`AgentContext` (`AgentTypes.h`) es lo que el backend recibe al arrancar: adapter,
cwd, `serverBaseUrl`, `modelId`, `apiKey` + `ctxOverride` (provider cloud),
`parallelSlots`, VRAM total/libre. Los dos últimos alimentan el cap adaptativo de
sub-agentes.

---

## 3. El loop ReAct (`LlamaAgentBackend`)

```
sendMessage(text)
 └─ pushCheckpoint()               rollback por turno de usuario
 └─ runCompletion()  ── POST /v1/chat/completions (stream:true, tools, tool_choice:auto)
      ├─ handleStreamData()        acumula content / reasoning_content / tool_calls[index]
      └─ handleStreamFinished()
           ├─ sin tool_calls → finishTurn()
           └─ con tool_calls → processPendingCalls()
                ├─ validación (JSON, requiredArgs, tool conocida, anti-loop, destructiva)
                ├─ aprobación (reglas por patrón → política auto|ask|manual)
                ├─ executeTool() en el worker thread → toolExecuted
                ├─ AgentProgressGovernor decide Continue | Replan | Stop
                └─ runCompletion()  con el tool result en contexto
```

Puntos que importan:

- **Streaming SSE**. Los `tool_calls` llegan fragmentados por `index`;
  `mergeToolCallDelta()` (pura, testeada) mergea `id`/`name` y concatena
  `arguments` hasta el final del stream.
- **Watchdog de inactividad de stream**, no de duración total: `m_streamIdleTimer`
  se reinicia con cada frame SSE. Configurable con `LLAMACODE_STREAM_IDLE_TIMEOUT`
  (segundos, default 3600). Un prefill lento y legítimo no se mata; un stream
  muerto sí.
- **Watchdog por tool**: `toolWatchdogSeconds(tool, args, quickTimeoutSec)` da un
  presupuesto distinto según la tool (una lectura no merece lo mismo que un
  `run_shell`). Al vencer, se reinicia el worker (`restartWorkerAfterTimeout`).
- **Tres modos de completion** (`enum CompletionMode`): `NativeFull`,
  `NativeCompat`, `TextTools`. Si el server rechaza tools nativas con 400 se cae a
  protocolo textual (`m_textToolFallback`); `setForceTextTools(true)` lo activa
  desde el primer request cuando el chat-template del GGUF no soporta tools (caso
  Gemma). El parseo textual es `textToolCallFromContent()` +
  `secondTextToolCallStart()` (corta ráfagas de varios TOOL_CALL).
- **Reintentos clasificados**: `classifyCompletionError(httpStatus, text)` →
  `RetryNone` / `RetryTransient` (hasta `kMaxTransportRetries`=60, ~5 min: cubre un
  reinicio de llama-server) / `RetryContextOverflow` (dispara recuperación de
  contexto en vez de reintentar igual).
- **Límites**: `kMaxTurnIters`=1000 (tope de seguridad, deliberadamente altísimo —
  no cortar trabajo legítimo) y `kMaxSameCall`=3 (la tercera llamada idéntica se
  bloquea y fuerza replanteo). La firma la calcula `toolCallSignature()`, que
  normaliza el JSON (orden de claves y espacios) para que un mismo pedido no se
  disfrace de distinto.
- **Espirales de fallo**: `failureFingerprint(tool, result)` normaliza el error;
  `kFailureSpiralThreshold`=3 fallos equivalentes consecutivos cambia la
  estrategia (replan / escalado al maestro).
- **Repetición textual**: `repeatedSuffixStart()` detecta el bucle de generación
  (mismo bloque repetido) y corta el stream.

### AgentProgressGovernor

Presupuesto **elástico** de créditos por turno, no un contador de pasos.
`Policy{initialCredits=8, maxCredits=16, replanAfter=3, stopAfterReplan=5,
maxDistinctWrites=24}`. Decide sólo al terminar una acción: la evidencia nueva
premia créditos, la repetición de la misma *familia semántica* (aunque cambien
los argumentos superficiales) los quema. `maxDistinctWrites` es el techo duro que
mata el patrón `prime_checker.py`, `prime_checker_v2.py`, …`_v10.py`, donde cada
escritura parece "nueva" y el presupuesto elástico nunca cerraría.

---

## 4. Contexto: transcript vs memoria de trabajo

Dos arrays separados, y esa separación es la garantía central:

- `m_transcriptMessages` — **fuente de verdad inmutable** de la sesión.
- `m_apiMessages` — memoria de trabajo que se le manda al modelo. Se poda y se
  compacta libremente.

Mecanismos sobre la memoria de trabajo:

| Mecanismo | Función | Qué hace |
|---|---|---|
| System prompt | `buildSystemPrompt()` | base + directivas + memoria del proyecto (`.llamacode/memory.md`, fallback `AGENTS.md`) |
| Refresco de contexto | `refreshSystemPromptContext()` | reconstruye el system cuando cambia cwd/modo; sin esto el prompt persistido sigue anunciando permisos viejos |
| Poda | `pruneWorkingContext()` | descarta lo prescindible; respeta `isProtectedContextMessage()` |
| Compactación | `planCompaction` → `startCompaction` → `applyCompaction` | resume el tramo medio con el propio LLM; conserva system + objetivo inicial + cola reciente. `m_compactStall` corta el bucle si compactar deja de bajar tokens |
| Saneo de wire | `sanitizeApiMessagesForWire(msgs, modelId)` | antes de cada request: elimina `tool` huérfanos, `assistant.tool_calls` colgantes, conserva el user de anclaje, degrada system no-inicial |
| Rol de notas | `midConversationNoteRole(modelId)` | `latest_reminder` en deepseek-v4, `user` en el resto — **nunca** `system`, porque varios templates hoistean los system al tope y rompen posición + prompt-cache |
| Imágenes | `trimStaleImages(msgs, keepLast)` | deja capturas sólo en los últimos N mensajes; el resto pasa a `[captura omitida]` (con mmproj cada screenshot son miles de tokens de prefill) |
| Warmup | `buildWarmupPayload()` | mismo prefijo, `max_tokens=1`, `stream=false`, `cache_prompt=true` |
| Read-dedup | `m_readFingerprints` | releer el mismo archivo sin cambios devuelve un stub en vez de re-inyectar el contenido |
| Preflight | `ContextPreflight::build(root, request, maxFiles, tokenBudget, expandGraph)` | scout inicial con handles, recibo y vecinos relevantes |

`contextUsage(used, limit)` y `contextManaged(working, transcript, pruned, events)`
son lo que ve la UI (barra "ctx N/M" y las métricas de poda). El límite sale de
`/props` (`n_ctx`) salvo `ctxOverride` de un provider cloud, que no expone `/props`.

### Thinking

`visibleAnswer(content, thinkingEnabled, leakGuard)` decide qué se muestra. Con
Pensar OFF quita los `<think>…</think>`, **pero** si el modelo metió toda la
respuesta adentro (Qwen lo hace) rescata el texto interno en vez de dejar la
burbuja vacía. Al historial de API el `<think>` no viaja nunca. Estrategia por
binario/modelo: `--reasoning on/off`, `--reasoning-budget`, o
`chat_template_kwargs.enable_thinking` y `reasoning_effort` (`low`, `medium`,
`high`, `xhigh` o el alias histórico `max`) mediante `thinkingTemplateKwargs()`.

---

## 5. Tools

Catálogo built-in: `LlamaAgentBackend::toolCatalog()` — `{name, group,
description, approxTokens}`. Los `approxTokens` son lo que hace que apagar tools
sea una decisión de **presupuesto de contexto** y no de gusto.

| Grupo | Tools |
|---|---|
| Archivos | `read_file`, `list_dir`, `glob` |
| Búsqueda | `grep`, `code_hotspots`, `search_docs`, `semantic_search`, `hybrid_search`, `repo_slice`, `context_status`, `context_scout`, `context_fetch` |
| Código | `write_file`, `edit_file`, `run_shell` |
| Revisión | `review_overengineering` |
| Conocimiento | `project_brain`, `memory`, `graph`, `verify_claims`, `recent_actions`, `context_checkpoint` |
| Web | `web_search`, `web_fetch`, `deep_research` |
| Multi-Agente | `ask_teacher`, `task` |
| Habilidades | `skill_list`, `skill_load` |
| Browser | `browser_skill_list`, `browser_skill_replay`, `browser_network_discover` |
| Escritorio | `desktop_snapshot`, `desktop_windows`, `desktop_controls`, `desktop_click_element`, `desktop_control_action`, `desktop_find_image` / `click_image` / `wait_image` / `assert_image`, `desktop_observe`, `desktop_click`, `desktop_stroke`, `desktop_type`, `desktop_key`, `desktop_scroll`, `desktop_focus`, `desktop_resize`, `desktop_wait`, `desktop_wait_for`, `desktop_assert`, `desktop_launch` |
| Correo | `email_accounts`, `email_send`, `email_list`, `email_read` |

- `setDisabledTools(names)` las saca de `buildToolSchemas()` → no se ofrecen al
  modelo, no gastan contexto. Acepta built-ins y `mcp__server__tool`.
- `toolKind(name)` → `read | write | shell | mcp`, base de la política de permisos.
- `requiredArgs(name)` se valida **antes** de ejecutar; faltante = tool result de
  error, no crash del turno.

### Ejecución: worker thread

El loop coordina en el hilo principal (red asíncrona); la ejecución vive en
`AgentToolRunner` sobre un `QThread` dedicado, porque `run_shell`, el handshake
MCP (el primer `npx` baja el paquete) y `tools/call` son bloqueantes y congelarían
la UI. Los `McpClient` viven en el worker por afinidad de `QProcess`.

Flujo: `approveAndContinue` → `invokeMethod` en cola → `toolExecuted(QVariantMap)`
→ `onToolExecuted` en el hilo UI (snapshot+diff si es write, tool result, seguir el
loop). Una tool a la vez; `m_execCallId` descarta resultados tardíos tras
cancelar. `run_shell` es async con tarjeta en vivo: `toolStarted` la crea,
`toolOutputChunk` la va llenando (con throttle), `finishShell` la cierra. La
cancelación mata el **árbol** de procesos (`taskkill /T /F` en Windows).

### MCP

`McpClient` = JSON-RPC 2.0 sobre stdio: `initialize` → `notifications/initialized`
→ `tools/list` → `tools/call`. Las tools se inyectan con prefijo
`mcp__<server>__<tool>` y se rutean por prefijo. Config = global + proyecto
(el de proyecto pisa por nombre), más el server de browser inyectado según perfil.

Dos gates independientes:
- `setMcpServers(...)` — qué servers **corren**.
- `setMcpToolsEnabled(bool)` — si sus tools se **inyectan** en el contexto. Las
  tools MCP no están en `toolCatalog()`, así que `disabledTools` no las apaga;
  esta es la perilla del perfil de agente (`mcpEnabled`).

`ToolExecutionSafety` da el contrato uniforme para tools externas: cuando un
server MCP no declara annotations, el default es **conservador**
(`effect=external_write`, `approvalRequired=true`, `openWorld=true`). Aporta
además `canonicalJson`, `payloadHash(server, tool, args)`, `idempotencyKey` y
`resultHash` — lo que liga aprobación, idempotencia y recibos al payload exacto
(por eso la aprobación guarda `m_awaitPayloadHash`: aprobar una llamada no
aprueba otra parecida).

---

## 6. Permisos y guardrails

Orden de evaluación:

1. **Reglas por patrón** (`setPermissionRules`, una por línea:
   `allow|deny|ask [kind:]<glob>`).
2. **Política global** `agent/approvalMode`: `auto` (todo), `ask` (auto lectura;
   pide write + shell + mcp), `manual` (pide todo).
3. **Overrides de Task**: `setTaskAutoApprove(true)` auto-aprueba durante una Task
   sin tocar la preferencia persistida.
4. **Guardrail Zero-Autonomy** (`setHitlDestructive`, ON por defecto): si
   `isDestructiveAction(name, args, desktopControlsText)` da true (borrado
   recursivo, format, drop de DB, `memory forget/prune`, click sobre un control
   cuyo nombre es delete/eliminar/format), **fuerza aprobación humana aunque el
   modo sea `auto` o haya auto-approve de Task**. Sólo el modo "super" lo saltea.
   Mismo mecanismo que el gate de `email_send` (enviar correo es acción externa
   irreversible; `setMailAutoSend` es el opt-in explícito).
5. En **sub-agentes**, que corren headless y no pueden pedir aprobación, el
   guardrail **rechaza de plano** la acción destructiva y le dice al sub-agente
   que la difiera al agente principal.

Aprobación en vuelo: una tool a la vez (`m_awaitId` + `m_awaitCall` +
`m_awaitPayloadHash`); `toolApprovalNeeded({tool, kind, detail, diff})` a la UI;
"Siempre" agrega el `kind` a `m_alwaysAllowed` para la sesión.

### Alcance de filesystem

`setTaskScope(scope, folders)`:
- `project` — confinado al cwd (default).
- `folder` — cwd + carpetas listadas.
- `full` — sin confinamiento ("Super Agente").

`safeProjectDir(dir)` rechaza el home del usuario, su carpeta padre y la raíz de
una unidad: ahí las tools con auto-aprobación escribirían sobre `.ssh`, `.claude`
o los perfiles. Si no hay proyecto válido se usa `fallbackWorkspaceDir()`, que
nunca es el home.

### Diffs y rollback

- `write_file` toma snapshot previo (`m_editSnapshots`) y genera un diff unificado
  simple (`makeDiff`, prefijo/sufijo común + bloque +/-) que se muestra en la card
  de aprobación **antes** de aplicar.
- `revertEdit(path)` restaura o borra desde el snapshot.
- `pushCheckpoint()` por turno de usuario guarda longitudes de mensajes +
  qué archivos ya estaban editados; `rollbackToMessage(i)` rebobina la
  conversación y revierte sólo lo editado después.
- `editMessage(i, texto)` reescribe un mensaje y descarta lo posterior
  (rebuildea `m_apiMessages` como turnos de texto: pierde la estructura de
  tool_calls pero deja el contexto válido).
- `forkSessionAtMessage(i)` bifurca la sesión en un punto.

---

## 7. Sesiones y persistencia

- Store en `AppLocalData/agent_custom/`: `index.json` (lista) +
  `<sessionId>.json` con `{messages (UI), api, transcript, checkpoints}`. Se
  persiste el historial de API completo (roles tool / assistant+tool_calls) para
  reanudar el contexto real, no sólo el texto.
- **Concurrencia real**: `m_sessionRuntimes` mantiene **una instancia completa de
  `LlamaAgentBackend` por conversación activa** (`m_isSessionRuntime=true`),
  aislando request, stream, contexto, tools, aprobaciones, compactación y
  subagentes. La "vista" está desacoplada del runtime que ejecuta
  (`m_viewSessionId`): se puede mirar otra sesión mientras la primera termina su
  turno, sin mezclar historiales. `isBusy()` vs `selectedSessionBusy()`.
- Sesiones de Task: `newTaskSession()` / `endTaskSession()` son efímeras y
  restauran la sesión previa del usuario al terminar.
- `pruneEmptySessions(keepId)` limpia las creadas y abandonadas.
- `suggestSessionTitle(firstPrompt)` da un título corto y determinista (≤3
  palabras) sin consultar al server.

---

## 8. Multi-agente

- **`task` → `SubAgentRunner`**: sub-agente headless con su propio loop ReAct y su
  propio worker de tools, confinado a una **git worktree aislada**
  (`createWorktree` / `mergeAndCleanupWorktree`). Sin git no hay subagentes → señal
  `gitRequired`. El cap de paralelismo es adaptativo:
  `adaptiveSubagentLimit(parallelSlots, ctxTokens, vramTotal, vramFree)`, con
  `kAbsoluteMaxParallelSubs`=5 como techo duro. Los que no entran esperan en
  `m_subQueue`; el loop principal no cierra el turno hasta que terminan todos.
- **`ask_teacher` → maestro**: un modelo más capaz, por HTTP OpenAI-compat o por
  CLI (`claude` / `codex`, detectados por `MasterCli`). `setMasterChain` define una
  cadena de fallbacks ordenada; `escalation` es `manual` | `auto` | `both`, y
  `autoAfterFails` dispara el escalado automático. `m_escalatedSigs` evita
  re-escalar la misma firma (anti-recursión). Con la directiva `honey` el maestro
  responde en formato denso clave:valor (`masterSystemPrompt(honey)`).
- **`DifficultyRouter`**: clase pura que evalúa archivos afectados, tokens de
  contexto, fallos repetidos, ciclos y confianza → `Low | Medium | High`, para
  decidir si escalar a un modelo más capaz.
- **`HybridPlanning`**: plan explícito antes de ejecutar (`parsePlan` /
  `normalizePlan` — tolera las formas de más que devuelven los modelos —
  `validatePlan`, `executorPrompt`, `cacheKey`).
- **`AgentRoomStore`**: salas donde humanos y agentes comparten timeline, con
  identidad, grants y evidencia. No ejecuta modelos.

---

## 9. Conocimiento persistente

| Componente | Dónde | Qué guarda |
|---|---|---|
| `MemoryStore` | tool `memory` | hechos durables por capas (save/recall/forget) |
| `GraphStore` + `CodeGraphIndexer` | `.llamacode/graph.jsonl` | knowledge graph `archivo -[defines]-> símbolo`, `archivo -[imports]-> archivo`, con `SourceRef` (ruta/rango/hash), citas, revisión y `doctor`. Indexado **sin LLM**, idempotente, con reindexado incremental por git-diff/mtime |
| `KnowledgePacket` | efímero, dentro del prompt/tool | unión acotada de memoria + grafo, con nodos, edges, fuentes y `knowledge-receipt`; no agrega otro almacenamiento |
| `ProjectBrain` | cache por root | índice de estructura y metadata del workspace; sólo huellas, nunca copia fuente afuera |
| `AgentEventLog` | `.llamacode/agent_events.jsonl` | bitácora append-only de lo que el agente intentó/rechazó/ejecutó. La tool `recent_actions` la relee para auto-corregirse |
| `PortableSkillStore` | `<AppLocalData>/skills/<slug>/SKILL.md` y `<ws>/.llamacode/skills/…` | habilidades portables; expone sólo metadata hasta que `skill_load` pide el cuerpo |
| Consolidación | `consolidateMemory()` | corre una completion en background sobre el transcript al dejar una sesión y extrae hechos durables (`source="consolidation"`, deduplicado por `m_consolidatedLen`) |

`StructuredSourceView` es la vista efímera de un archivo: comprime manteniendo el
rango original de cada token, para poder proyectar la evidencia de vuelta al
archivo real y **rechazar rangos ambiguos**. Nunca escribe el archivo compacto.

---

## 10. Directivas del system prompt

`directiveCatalog()` es la fuente de verdad para la UI de toggles y para
`buildSystemPrompt()`. Cada una es una función estática pura → unit-testeable.
Sin setear = **todas activas** (no regresiona el comportamiento histórico).

| key | Sección | Qué impone |
|---|---|---|
| `discipline` | `developmentDisciplineSection()` | anti-regresión: blast radius, cambio mínimo, preservar contratos, verificar |
| `testNet` | `testSafetyNetSection()` | detectar el runner del proyecto, un test caja-negra por cambio, correr el suite |
| `projectContext` | `projectContextSection()` | entender el porqué antes de tocar (no romper workarounds deliberados), co-cambios por git, dejar lo durable en memoria |
| `efficiency` | `efficiencySection()` | menos pasos y tool calls |
| `style` | `styleSection()` | respuestas concisas |
| `honey` | `honeySection()` | **off por defecto**. Reduce lo que el modelo *emite*: código YAGNI sin scaffolding, respuesta-primero, handoffs densos |
| `antiBias` | `antiBiasSection()` | **off por defecto**. "usual/estándar/típico" es señal de sesgo a re-examinar; responder al cumplir la premisa sin re-derivar |

Aparte, `desktopPlaybookSection(visionReady)` se inyecta **sólo** si las tools de
escritorio están habilitadas: camino rápido por teclado, verificación por texto
(UIA) cuando no hay visión, clic semántico por nombre.

Cambiar directivas con sesión viva reemplaza el mensaje system en vivo
(`replaceSystemMessage`).

---

## 11. Telemetría

`AgentEfficiency` normaliza lo que devuelven llama.cpp y los endpoints
OpenAI-compat, **sin inventar**: los campos que el server no reporta quedan en
cero. Por request: `phase`, prompt/generated tokens, prompt/generated ms, wall ms,
tool calls, tool bytes. `summarize()`, `compare(baseline, candidate)` y
`benchmarkComparison(runs)` (agrupa por perfil con estadísticos robustos; las
filas fallidas cuentan para estabilidad pero no contaminan las medianas).

El backend además guarda las métricas **reales** de generación del server
(`timings.predicted_n` / `predicted_ms`) en vez de estimar chars/4 + wall clock,
y expone `efficiencySummary()` y `progressSummary()` (`progressEvents`,
`stagnationEvents`, `replanEvents`). `setDeterministicSeed(n)` fija la semilla
para benchmarks reproducibles.

---

## 12. Cobertura de tests

| Área del harness | Test |
|---|---|
| System prompt (directivas, disciplina, test-net, contexto), `mergeToolCallDelta`, `planCompaction` | `tests/test_agent_wire.cpp` |
| Tools nativas, `executeTool`/`toolExecuted`, path de error de escritorio | `tests/test_agent_tools.cpp` |
| Perfiles de agente (capacidades + directivas, presets, gating) | `tests/test_agent_profiles.cpp` |
| Presupuesto de contexto | `tests/test_agent_context_budget.cpp` |
| Loop de Loops (body → goal-check → repeat) con `FakeAgentBackend` | `tests/test_appcontroller.cpp` |
| SSE stub de `/v1/chat/completions` | `tests/test_backends_net.cpp` (`SseStubServer`) |
| Memoria + grafo | `tests/test_memory_graph.cpp` |
| `code_hotspots` | `tests/test_hotspots.cpp` |
| `MasterCli` | `tests/test_master_cli.cpp` |
| `HarnessSpec` (herencia, packs, permisos, fases, migración) | `tests/test_harness_spec.cpp` |
| Cableado de módulos al backend + directivas .md (CRUD incluido) | `tests/test_harness_modules.cpp` |
| Verbos de harness headless | `tests/test_control_api.cpp` |
| Editor del harness (QML): edición del spec, diff, import/export | `tests/qml/tst_harness_editor.qml` (ctest: `qml_harness_editor`) |
| Fases aplicadas por el runner de Tasks | `tests/test_appcontroller.cpp` |
| Barrido A/B (`tools/harness_ab.ps1`) | `tests/test_harness_ab.ps1` (stub HTTP; fuera de ctest) |
| Verbos del harness contra el daemon REAL | `tests/headless_harness_smoke.ps1` (fuera de ctest, necesita el exe) |
| `EvalSuite` | `tests/test_eval.cpp` |

Gate: `tests.bat` + ctest en verde antes de commitear (`/gate`). Si el
coordinador imprime `[WARN] ... DIRTY`, los tests **no** corrieron sobre tu
fuente: el resultado no vale.

Sin cobertura automatizada (QA manual, requiere escritorio vivo): UI Automation
(`desktop_controls` / `desktop_click_element`), `desktop_observe` → visión con
`--mmproj`, teach persistente de browser, y el swap de modelo de verify-phase
end-to-end. Detalle en `CLAUDE.md`.

---

La regresión dedicada de la frontera nueva cubre engine/fingerprint, event log,
capabilities fail-closed, ledger de efectos y framing/autenticación de workers.

## 12b. A/B de harness (medir antes de decidir)

Personalizar sin medir es adivinar. El ciclo cerrado:

```bash
powershell -File tools\harness_ab.ps1 -LaunchProfileId <launch> -AgentProfileIds agent-intermedio,agent-minimal -Passes 3
```

Corre el **mismo** benchmark sobre el **mismo** launch con cada perfil de agente
(`startBenchmark`/`startCustomBenchmark` ya aceptan `agentProfileId`), y después
llama a `compareHarnessBenchmarks`, que agrupa las corridas por
`agentProfileId` en vez de por modelo — `AgentEfficiency::benchmarkComparison`
toma un `groupBy` para eso. Sale un JSON con medianas de calidad, tiempo, tasa de
éxito y complejidad por perfil, más los deltas entre pares.

Necesita un daemon headless arriba (`--agent-daemon`, ver `docs/HEADLESS.md`).
Antes de correr valida que los perfiles existan e imprime su costo de contexto y
sus advertencias de dependencias: un id mal escrito correría con el nivel por
defecto y la comparación sería una mentira.

Regla de lectura: un harness sólo es mejor si **no** baja calidad ni tasa de éxito.

Para comparar harnesses que viven fuera de LlamaCode existe además
`tools/harness_matrix.py`. Usa el contrato JSONL `llamacode-harness-v1`, ejecuta
el mismo producto adapter × tarea con orden intercalado y semilla reproducible,
y guarda deltas pareados de éxito, latencia, F1 de tools y redundancias. Los IDs
de OpenClaw, Hermes, Pi y Nanobot se incluyen como configuración de ejemplo, pero
los comandos concretos se inyectan por ambiente para no acoplarse a una CLI
particular. La suite bundleada multi-dominio es
`assets/benchmarks/custom/harness_multidomain_v1.json`; el contrato mínimo de
secuencia de herramientas está separado en
`assets/benchmarks/custom/harness_tool_contract_v1.json`.

---

## 12c. Contrato Next y lanes de workers

La primera versión de Next no reemplaza el loop estable: agrega una frontera
operativa que se puede medir y apagar. Sus sesiones viven en
AppLocalData/agent_harness_next, con eventos JSONL por sesión y un ledger
append-only de efectos. Un efecto puede quedar prepared, dispatching, settled,
uncertain o parked; uncertain es información útil, no un error que se oculte.

HarnessCapabilitySnapshot admite pedidos contra una lista de policy grants y
entrega handles opacos sólo para los grants admitidos. Revocar o cambiar la
generación invalida el handle; una tool no debe interpretar la existencia del
nombre como autoridad.

HarnessWorkerProtocol define framing binario con longitud, límite de bytes,
protocolo versionado y autenticación por nonce. `HarnessWorkerDriver` supervisa
un proceso externo, impone timeout, evita reutilizar call IDs, permite
cancelación y enruta llamadas de capacidades sólo después de validar el handle
contra el snapshot de la activación. `HarnessWorkerFactory` traduce el módulo
de perfil a Node o CPython y prepara el snapshot fail-closed.

Las SDK ya están implementadas en `sdk/node` (ESM, Node >=20, con tipos
`index.d.ts` para TypeScript) y
`sdk/python` (stdlib, Python >=3.10). Ambas hablan exactamente el mismo wire
protocol, reservan stdout para frames, exponen `CapabilityBroker` y devuelven
rechazos nombrados (`capability_denied`, `capability_revoked`) sin convertirlos
en traps. Si el host desaparece mientras una capability está pendiente, ambas
despiertan la operación con `worker_disconnected`.

El sandbox OS también es opt-in por perfil: `process` usa Job Objects en Windows
(kill-on-close, límite de procesos, memoria y CPU cuando se declara) y grupos de procesos Unix;
`strong` usa bubblewrap en Unix con root de sólo lectura, workspace writable,
PID/IPC/UTS aislados y red descompartida. En Windows `strong` se rechaza
explícitamente: un Job Object contiene procesos pero no es un boundary de red.
Si el backend solicitado no existe, el worker no arranca y el perfil no cae
silenciosamente a `none`.

### Integración con el loop del agente

El módulo `worker` ya no es sólo una frontera de proceso: al arrancar
`LlamaAgentBackend`, un `lane: node|python` crea y autentica su
`HarnessWorkerDriver`. Una vez autenticado, el schema de `worker_call` aparece
en el próximo request del modelo. La llamada atraviesa el mismo circuito de
aprobación que las demás acciones externas y queda registrada como efecto; un
perfil sin `worker` no crea procesos ni agrega ese schema, por lo que conserva
el camino legacy.

El host admite por defecto únicamente capacidades de lectura y sólo si el
perfil las pidió: `fs.read` (`read`/`read_file`), `memory.read` (`recall`),
`graph.read` (`query`) y `context.read` (`status`). Cualquier otra operación
recibe `capability_operation_unsupported`; la red, si se habilita, es una
perilla independiente del sandbox y no una capability brokered. Un fallo o
salida del worker devuelve el `tool_result` correspondiente y no deja el turno
del modelo esperando indefinidamente.

En la UI del editor de HarnessSpec, elegir Node/Python habilita el entrypoint,
sandbox, red, timeout y capabilities solicitadas. `worker_call` se habilita
automáticamente para esa lane salvo que el perfil lo incluya en
`tools.exclude`; volver a `builtin` es reversible y no modifica las sesiones ni
la configuración histórica.

## 13. Deudas conocidas

- `docs/plan_harness.md` y `docs/agent.md` siguen nombrando `CustomBackend`
  (hoy `LlamaAgentBackend`) y describen `OpencodeBackend` como default.
- `OpencodeBackend` no tiene diffs/revert ni la robustez de protocolo del backend
  nativo (maneja su propio loop), y no consume el `HarnessSpec`.
- `run_shell` continúa usando su propio lifecycle histórico; las lanes externas
  de plugins usan el sandbox declarativo del módulo `worker`.
- Los presets de sistema son specs construidos en código (`systemPresets()`), no
  JSON bundleado: quedan auditables y componibles, pero agregar un preset sigue
  siendo una recompilación.
- **`OpencodeBackend` no consume el `HarnessSpec`** y no va a hacerlo: es el
  backend legacy con su propio loop, y respetar `loop`/`context` ahí sería
  reimplementar el harness. Decisión tomada, no deuda: el editor muestra un aviso
  cuando el agente activo no es el nativo, para que la UI no prometa lo que no se
  aplica.
- **Presets de sistema en código** (`systemPresets()`), no JSON bundleado. Se
  revisará sólo si aparece la necesidad de agregar un preset fuera de una release;
  hoy el costo (cargador + validación + fallback) no se paga solo.
