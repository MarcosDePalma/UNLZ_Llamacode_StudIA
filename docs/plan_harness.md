# Plan de implementación — Multi-backend de agentes (Harness)

> **DOCUMENTO HISTÓRICO.** Registra el plan por etapas y el porqué de las
> decisiones (p.ej. descartar Goose). No refleja el estado actual: el
> `CustomBackend` que se nombra acá hoy es `LlamaAgentBackend`, y el backend
> nativo —no `opencode`— es el default. Referencia vigente: `docs/harness.md`.

Arquitectura objetivo: LlamaCode (Qt/QML) habla con un **runtime de agente** intercambiable
vía una interfaz común `IAgentBackend`. El runtime usa el `llama-server` local
(OpenAI-compatible) y herramientas vía MCP.

```
App Qt/QML
   ↓  IAgentBackend (start/stop/sendMessage/sesiones/tools/approval)
Backend: opencode | custom | raw | ...
   ↓  OpenAI-compatible API
llama.cpp server
   ↓  tools / MCP
filesystem, shell, git, ripgrep, tests, browser, etc.
```

Principio rector: **la app no se casa con un harness**. El valor real (control de
contexto, tool-calling estable, diffs, aprobación, memoria por proyecto, perfiles por
modelo, fallback de JSON roto) es **harness-agnóstico**.

---

## Leyenda de estado
- ✅ HECHO
- 🚧 EN PROGRESO
- ⬜ PENDIENTE

---

## Etapa 0 — Contrato / interfaz  ✅ HECHO
**Meta:** definir la interfaz y los tipos antes de mover lógica.

- ✅ `src/core/agent/AgentTypes.h` — `AgentContext`, `AgentMessage`, `AgentSession`, `ToolCall`.
- ✅ `src/core/agent/IAgentBackend.h` — QObject abstracto:
  - ciclo de vida: `start(ctx)`, `stop()`
  - conversación: `sendMessage(text)`
  - sesiones: `newSession`, `newSessionInProject`, `switchSession`, `renameSession`,
    `deleteSession`, `forkSession`, `refreshSessions`
  - tools: `approveTool(id, always)`, `rejectTool(id)`
  - getters: `currentSessionId/Title/ProjectDir`, `messages()`, `sessions()`
  - señales: `runningChanged`, `messagesChanged`, `sessionsChanged`, `logAppended`,
    `toolApprovalNeeded`, `errorOccurred`
- ✅ Registrado en CMake. Compila.

---

## Etapa 1 — Extraer OpencodeBackend  ✅ HECHO
**Meta:** mover toda la lógica opencode de `AppController` a un backend. Comportamiento idéntico.

- ✅ `src/core/agent/OpencodeBackend.{h,cpp}` autocontenido: dueño de su `QProcess`
  (`opencode serve`) + `QNetworkAccessManager` + stream SSE.
  - sesiones (crear/listar/mensajes/rename/delete/fork)
  - eventos SSE (deltas, idle, session.updated, errores)
  - reconexión del stream (server opencode se apaga sin suscriptores)
  - kill del árbol de procesos al detener (libera puerto 4096)
  - auto-approve de permisos (provisional — lo reemplaza Etapa 4)
- ✅ `AppController` convertido en **fachada**: `ensureAgentBackend(adapter)` crea el
  backend y conecta señales → mirrors/señales QML existentes.
- ✅ `startAgent/stopAgent/sendToAgent` + `*OpencodeSession*` + `changeAgentProject` +
  `currentAgentProjectDir` delegan al backend.
- ✅ QML sin cambios (mismas propiedades/señales).
- ✅ **Limpieza hecha:** removido el path opencode legacy de `AppController`
  (HTTP/SSE/sesiones directas: `initOpencodeSession`, `loadOpencodeSessionList`,
  `resumeOrCreateOpencodeSession`, `doCreateOpencodeSession`, `loadOpencodeSessionMessages`,
  `subscribeOpencodeEvents`, `respondOpencodePermission` + miembros `m_opencodeAttachUrl`,
  `m_opencodeEventReply`, `m_forceNewOpencodeSession`). Los `*OpencodeSession*` ahora solo
  delegan a `IAgentBackend`; el path `QProcess` genérico quedó solo para adapters stdin
  (smallcode). `OpencodeBackend` es el único dueño de sesiones/stream/proceso.
- ⬜ **Falta test en vivo:** iniciar/chatear/sesiones/restart/nuevo-proyecto.

---

## Etapa 2 — RawChatBackend (modo Chat)  ✅ HECHO
**Meta:** chat directo contra `llama-server` `/v1/chat/completions` (sin harness de agente).
`raw` es exclusivo de **Chat**; no debe ser seleccionable en perfiles de **Agente**.

- ✅ `RawChatBackend` implementa `IAgentBackend` con streaming SSE de OpenAI (`stream:true`).
- ✅ Definición de producto: `raw` fuera del pipeline de Agente (solo Chat).
- ✅ Sesiones locales en memoria (crear/cambiar/renombrar/borrar/fork).
- ✅ Persistencia local de sesiones/mensajes de `RawChatBackend` (JSON en AppLocalData).
- ✅ Integración en Chat para usar `RawChatBackend` como runtime único de chat directo.
- ✅ Extras agregados: adjuntos (imágenes vía mmproj + docs de texto inline),
  pegar imagen del portapapeles, toggle de thinking con detección de soporte
  (`chatThinkingSupported` vía `/props`), control per-turn de reasoning (`reasoning_budget`).
- ✅ Separación Chat/Agente: páginas distintas en nav + `raw` oculto del selector de
  Agente (no es tool-calling). El "selector de modo" se cumple estructuralmente, no como toggle.
- **Entregable:** chat directo funcional + interfaz validada con 3 backends reales
  (opencode, raw, custom).

---

## Etapa 3 — CustomBackend (segundo backend real)  🚧 EN PROGRESO
**Decisión:** en vez de Goose (API `goosed` poco documentada → reverse-engineering),
se implementa un backend propio. Cero dependencias externas, controla el contrato,
y reusa al 100% el approval/diff harness-agnóstico. Goose queda diferido/descartado.

- ✅ `CustomBackend` implementa `IAgentBackend`: loop ReAct con tool-calling OpenAI
  (`/v1/chat/completions`, `tools` + `tool_choice:auto`, non-stream) contra `llama-server`.
- ✅ Tools nativas: `read_file`, `list_dir`, `grep`, `write_file`, `run_shell`
  (sandbox simple: rutas confinadas a cwd del proyecto).
- ✅ Aprobación integrada: clasifica tool (read/write/shell), respeta `agent/approvalMode`,
  emite `toolApprovalNeeded` y espera; `approveTool`/`rejectTool` reanudan el turno.
- ✅ Sin binario externo: `startAgent` rama `custom` (no `findExecutable`),
  `isHarnessInstalled("custom")=true`, tarjeta "Custom (nativo)" en selector.
- ✅ Registrado en CMake + `ensureAgentBackend`.
- ⬜ Sesiones: v1 en memoria (1 sesión). Falta persistencia a disco (reusar patrón Raw).
- ⬜ Streaming de respuesta (hoy non-stream por simplicidad de parseo de tool_calls).
- ⬜ MCP para tools externas (hoy solo built-in).
- ⬜ Test en vivo: pedir leer/escribir/ejecutar y verificar loop + approval.
- **Entregable:** segundo agente real seleccionable (chat + tools + approval). opencode intacto.

---

## Etapa 4 — Aprobación de herramientas visible  🚧 EN PROGRESO
**Meta:** human-in-the-loop real (hoy opencode auto-aprueba TODO — peligroso).

- ✅ Señal `toolApprovalNeeded(QVariantMap)` en `IAgentBackend` → `AppController`
  espeja en `agentPendingTool` → UI: card con tool/título/comando-archivo +
  botones Rechazar / Aprobar / Siempre (`approveAgentTool`/`rejectAgentTool`).
- ✅ `OpencodeBackend` espera respuesta (no auto-`always`): clasifica tool por tipo
  (`toolKind`: read/write/shell), guarda pendiente en `m_pendingPerm`, responde
  `once`/`always`/`reject` vía `/permissions/`.
- ✅ Setting global `agent/approvalMode` (auto | ask | manual) + selector en header
  de Agente. "ask" = auto-aprueba lectura, pide escritura+shell.
- ⬜ Scope por proyecto (hoy solo global).
- ⬜ Test en vivo: gatillar un write/bash y verificar card + respuesta opencode.
- **Entregable:** ninguna escritura/exec sin OK (configurable). Diferenciador clave.

---

## Etapa 5 — Diffs visibles + control de cambios  🚧 EN PROGRESO
**Meta:** ver qué tocó el agente.

- ✅ `CustomBackend` intercepta `write_file`: snapshot del estado previo, genera diff
  unificado simple (`makeDiff`, prefijo/sufijo común + bloque +/-) y agrega una entrada
  `role:"diff"` al chat con path + diff.
- ✅ Preview de diff en la card de aprobación (antes de aplicar, scroll).
- ✅ Botón "Revertir" por archivo (snapshot propio `m_editSnapshots`):
  `IAgentBackend::revertEdit` → `CustomBackend` restaura/borra; marca la entrada como
  revertida. Invokable `revertAgentEdit`.
- ⬜ Soporte para `OpencodeBackend` (hoy solo custom; opencode no expone old/new directo).
- ⬜ Coloreado +/- (verde/rojo) en el render del diff (hoy mono monocolor).
- ⬜ Revert vía git como alternativa al snapshot.
- **Entregable:** panel de diffs por mensaje. Integra con Etapa 4.

---

## Etapa 6 — Memoria + contexto por proyecto  🚧 EN PROGRESO
**Meta:** lo que mueve la aguja en LLM local.

- ✅ Memoria por proyecto: `.llamacode/memory.md` (fallback `AGENTS.md`), editable.
  `CustomBackend::buildSystemPrompt` la inyecta en el system prompt al crear sesión.
  Invokables `readAgentMemory`/`writeAgentMemory` + editor (botón "🧠 Memoria" en `AgentPage`).
- ✅ Control de contexto visible (tokens): `CustomBackend` lee `n_ctx` de `/props` y
  `usage.total_tokens` de cada respuesta → señal `contextUsage` → `AppController`
  (`agentContextUsed`/`agentContextLimit`) → barra "ctx N/M" en header de `AgentPage`
  (color por % de uso).
- ⬜ Archivos en contexto + pin/exclusión (no aplica al loop actual; futuro).
- ⬜ Compactación configurable (ya hay flags en perfiles opencode).
- **Entregable:** barra de contexto + editor de memoria por proyecto. ✅ (lo central)

---

## Etapa 7 — Robustez con modelo local  🚧 EN PROGRESO
**Meta:** que no explote cuando Qwen rompe JSON/tool calls.

- ✅ Fallback: tool-call con JSON de args malformado → no ejecuta; devuelve `tool`
  result con el error y pide reintentar con JSON válido (`CustomBackend`).
- ✅ Validación de args requeridos por tool (`requiredArgs`) antes de ejecutar;
  faltantes → tool result de error, sin crashear el turno.
- ✅ Tool desconocida → tool result de error (no aborta).
- ✅ Detección de loops: firma `name|args` contada por turno; >3 iguales → corta el turno.
- ✅ Límite de iteraciones por turno (`kMaxTurnIters=12`) → corta y avisa.
- ✅ Indicador de salud: contador ok/fail por sesión, log `[salud: X/Y tools ok (Z%)]`.
- ⬜ Equivalente para `OpencodeBackend` (hoy solo custom; opencode maneja su propio loop).
- ⬜ Exponer salud en UI (hoy solo en log).
- **Entregable:** sesiones no se cuelgan por JSON roto + logs de fallos.

---

## Etapa 8 — Perfiles de agente por modelo + UX final  🚧 EN PROGRESO
**Meta:** ajuste fino local-first.

- ✅ Ajuste del agente (`CustomBackend`): system prompt extra + temperatura para
  tool-calling. `IAgentBackend::setAgentTuning`; `AppController` persiste en
  `agent/systemPrompt` + `agent/temperature` y aplica en vivo. Dialog "⚙ Agente"
  en `AgentPage`. `--jinja` ya se fuerza en `EffectiveProfileBuilder` (Etapa 2).
- ⬜ Tuning ligado al perfil de modelo concreto (hoy global, no por modelo/perfil).
- ⬜ Config por proyecto del agente (hoy global).
- ⬜ Pulido: selector de backend por proyecto, estados/errores.
- **Entregable:** v1 multi-backend completa.

---

## Dependencias
```
0 ✅ → 1 ✅ → 2 ✅ (valida interfaz)
              ↓
              3 (Custom) ✅core ── paralelo ── 4 ✅core → 5 (diffs)
                                                ↓
                                              6 → 7 → 8
```
Etapas 4–8 son **harness-agnósticas** (sirven a cualquier backend).
Estado actual: 0-1-2 cerradas; 3 y 4 con su core implementado (falta test en vivo +
pulidos); próximo foco natural = **Etapa 5 (diffs)**, que se apoya en el approval ya hecho.

## Estimación
MVP útil ALCANZADO en core: opencode + custom + raw, con aprobación visible.
Resto (5–8) son mejoras incrementales harness-agnósticas.

---

## Decisión de segundo backend (Goose vs propio)
Se evaluó sumar un segundo agente externo. Goose tiene buena integración pero su API
(`goosed`) está poco documentada → costo alto de reverse-engineering del event stream.
Alternativas externas (Pydantic AI, OpenHands) suman runtime Python/Docker.
**Se eligió `CustomBackend` propio**: cero deps externas, contrato bajo control, y
reuso total del approval/diff harness-agnóstico. Goose queda diferido; revisitar solo
si se necesita un harness externo adicional. Ranking de referencia (si se retoma externo):
Goose > Pydantic AI > OpenHands SDK > Aider > LangGraph.

Backends detrás de `IAgentBackend`:
- `OpencodeBackend` ✅ (default, harness externo)
- `RawChatBackend` ✅ (chat directo, solo Chat)
- `CustomBackend` 🚧 (ReAct propio + tools nativas — segundo agente real, reemplaza Goose)
- `GooseBackend` ⬜ (diferido: API poco documentada; revisitar si hace falta harness externo)

---

## Notas de estado actual del repo
- pi / smallcode: ocultos del selector (UI comentada), lógica aún en `AppController`
  (pi por print-mode, smallcode por stdin path genérico). Migrar a backends o eliminar.
- Editor de config opencode (`OpencodeConfigDialog.qml`) + MCP + skills/commands ya
  implementados (backend en `AppController`: `readOpencodeConfig/writeOpencodeConfig`,
  `listMcpServers/setMcpServer/...`, `listOpencodeCommands/...`). Específico de opencode;
  generalizar al integrar MCP en `CustomBackend` (Etapa 3 pendiente).
- Backends vivos: `OpencodeBackend` (externo, default), `RawChatBackend` (Chat),
  `CustomBackend` (agente nativo). Selección por tarjetas en `ProfilesPage`.
- Aprobación: `agent/approvalMode` (auto|ask|manual), `agentPendingTool` +
  `approveAgentTool`/`rejectAgentTool`; card en `AgentPage`.
- Build: `build.bat` o `cmake --build build --config Debug --target LlamaCode`.
  QML y C++ embebidos (qt_add_qml_module / qrc) → requiere recompilar para ver cambios.
  Agregar fuentes nuevas a `CMakeLists.txt` (SOURCES/HEADERS).
