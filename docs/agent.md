# Modo Agente — Documentación

> Nota: este documento usa el nombre viejo `CustomBackend`; la clase hoy se llama
> `LlamaAgentBackend` (adapter `llamaagent`) y es el backend por defecto. La
> referencia completa y al día del harness es `docs/harness.md`.

Cómo funciona el agente de LlamaCode: arquitectura, ciclo de vida, loop ReAct,
tools (nativas + MCP), aprobación human-in-the-loop, diffs/revert, memoria/contexto,
robustez y persistencia. Complementa `plan_harness.md` (estado/etapas).

Compatibilidad: el comportamiento histórico se identifica como engine legacy.
El perfil agent-intermedio-next usa el mismo loop durante la etapa experimental,
pero recrea el backend y aísla sesiones, eventos y efectos bajo
agent_harness_next. Cambiar de perfil permite volver a legacy sin migración.

---

## 1. Arquitectura

```
QML (AgentPage / ChatPage)
   ↕  propiedades + señales
AppController          ← fachada; espeja estado del backend a QML
   ↕  IAgentBackend (interfaz común)
Backend concreto: OpencodeBackend | CustomBackend | RawChatBackend
   ↕  HTTP OpenAI-compatible (/v1/chat/completions, /props)
llama-server (llama.cpp)
   ↕  tools nativas + MCP (stdio)
filesystem / shell / grep / servers MCP
```

- **`IAgentBackend`** (`src/core/agent/IAgentBackend.h`): QObject abstracto. La UI nunca
  conoce el backend concreto. Métodos clave: `start/stop`, `sendMessage`, sesiones,
  `approveTool/rejectTool`, `revertEdit`, `setAgentTuning`. Señales: `runningChanged`,
  `messagesChanged`, `sessionsChanged`, `logAppended`, `toolApprovalNeeded`,
  `errorOccurred`, `contextUsage`.
- **`AppController`** crea el backend en `ensureAgentBackend(adapter)` y conecta sus señales
  a mirrors expuestos a QML (`agentMessages`, `agentSessions`, `agentPendingTool`,
  `agentContextUsed/Limit`, …).
- Backends vivos:
  - **`OpencodeBackend`** — harness externo `opencode serve` (default histórico).
  - **`CustomBackend`** — agente **nativo** (loop ReAct propio). Este documento.
  - **`RawChatBackend`** — chat directo, sin tools. Solo modo Chat.

---

## 2. CustomBackend — ciclo de vida

`src/core/agent/CustomBackend.{h,cpp}`

- `start(ctx)`: fija `m_cwd` (dir del proyecto), `loadFromDisk()` recupera sesiones previas
  y activa la primera, `ensureSession()` crea una si no había, `fetchContextLimit()` lee
  `n_ctx` del server vía `/props`.
- `stop()`: aborta el reply en curso, persiste la sesión + índice, baja `m_running`.
- `sendMessage(text)`: agrega el mensaje user a `m_messages` (UI) y `m_apiMessages` (API),
  crea el bubble assistant (typing + `status` visible), resetea contadores de turno y arranca `runCompletion()`.

---

## 3. Loop ReAct (tool-calling)

Flujo de un turno:

```
sendMessage
  └─ runCompletion()  ──POST /v1/chat/completions (stream:true, tools, tool_choice:auto)
        │  (SSE incremental)
        ├─ handleStreamData()      acumula content / reasoning_content / tool_calls[index]
        └─ handleStreamFinished()
              ├─ sin tool_calls → finishTurn()   (fin del turno)
              └─ con tool_calls → processPendingCalls()
                       ├─ valida (JSON, args requeridos, tool conocida, anti-loop)
                       ├─ aprobación (auto | ask | manual)  → toolApprovalNeeded / approveTool
                       ├─ executeTool()  → resultado como mensaje role:"tool"
                       └─ runCompletion()  (vuelve a consultar al modelo con el resultado)
```

- La burbuja activa mantiene un `status` visible mientras no hay tokens de texto:
  `Pensando...`, `Revisando resultados...`, ejecución de tool, lectura/escritura o
  aprobación pendiente. Así el usuario ve qué está pasando durante acciones largas.
- **Streaming** (`stream:true`): el parser SSE (igual patrón que `RawChatBackend`) lee
  `data: {…}` por línea. Los `tool_calls` llegan en **fragmentos por `index`**: se mergean
  `id`, `function.name` y se concatena `function.arguments` hasta el `finished`, donde se
  reensamblan ordenados por index.
- **Sin `max_tokens`**: se eliminó el cap de 256 que truncaba el JSON de tool_calls (causa
  principal de las fallas previas).
- **`m_apiMessages`** es la conversación real para la API: incluye `system`, `user`,
  `assistant` (con `tool_calls`) y `tool` (resultados con `tool_call_id`).
- Límites del turno: `kMaxTurnIters=12` completions; `kMaxSameCall=3` repeticiones de la
  misma firma `name|args` (anti-loop).

---

## 4. Razonamiento (thinking)

- El modo `Pensar` se decide en el perfil efectivo del servidor con la mejor
  estrategia compatible con el binario/modelo: `--reasoning on/off` cuando existe,
  `--reasoning-budget` como fallback, o `--chat-template-kwargs.enable_thinking`
  para templates Qwen/QwQ antiguos. El payload mantiene hints per-request
  (`reasoning_budget`, `chat_template_kwargs`) sólo como compatibilidad. El
  esfuerzo se reenvía como `chat_template_kwargs.reasoning_effort` y acepta
  `low`, `medium`, `high`, `xhigh` y el alias histórico `max`; los valores
  desconocidos se omiten. El warmup usa los mismos kwargs que el turno real.
- El `reasoning_content` del stream se acumula aparte y se muestra envuelto en
  `<think>…</think>` en el bubble.
- **Al historial de API NO va el `<think>`**: `stripThinkForContext()` lo quita antes de
  guardar el mensaje assistant en `m_apiMessages` (si no, el modelo se "ceba" razonando y
  rompe el tool-calling).
- Toggle: `setThinkingEnabled(bool)` (default off). `AppController` lo lee de
  `agent/thinkingEnabled` y lo trata como fuente de verdad: los perfiles de agente
  no fuerzan thinking si el checkbox está apagado.

---

## 5. Tools

### Nativas (`toolSchemas` / `executeTool`)
| Tool         | Kind  | Args                | Notas |
|--------------|-------|---------------------|-------|
| `read_file`  | read  | path                | cap 256 KB |
| `list_dir`   | read  | path (opcional)     | |
| `grep`       | read  | pattern, path?      | scan ≤2000 archivos, ≤100 hits |
| `write_file` | write | path, content       | snapshot para revert + diff |
| `run_shell`  | shell | command             | cmd/sh en cwd, timeout 30 s |

- Rutas confinadas al proyecto (`inProject()` evita escapar con `../`).
- Args validados (`requiredArgs`) antes de ejecutar.

### MCP (Fase 2)
- Servers MCP locales (stdio) declarados en config `mcp{}` (mismo formato que opencode:
  `listMcpServers`/`setMcpServer` en `AppController`).
- `McpClient` lanza el server por `QProcess` y habla **JSON-RPC 2.0** por stdin/stdout:
  `initialize` → `tools/list` → `tools/call`.
- Las tools MCP se inyectan en `buildToolSchemas()` con prefijo **`mcp__<server>__<tool>`**
  (cache `m_mcpTools`); el ruteo por prefijo ocurre en el worker. Aprobación: kind `mcp`.

### Ejecución en worker thread (no bloquea UI)
- El loop ReAct coordina en el hilo principal (network async). La **ejecución de tools**
  (nativas + MCP) corre en `AgentToolRunner` sobre un `QThread` dedicado, porque
  `run_shell`, el handshake MCP (primer `npx` descarga el paquete) y `tools/call` son
  bloqueantes y congelarían la UI.
- Flujo async: `approveAndContinue` despacha `executeTool` al worker
  (`QMetaObject::invokeMethod` en cola); el worker emite `toolExecuted(QVariantMap)` →
  `onToolExecuted` (hilo UI) reanuda el loop (snapshot+diff de `write_file`, tool result,
  `processPendingCalls`). Los `McpClient` viven en el hilo worker (afinidad de `QProcess`).
- Una tool a la vez; `m_execCallId` descarta resultados tardíos tras cancelar/cerrar turno.

---

## 6. Aprobación human-in-the-loop

- Política `agent/approvalMode`: `auto` (todo), `ask` (auto lectura; pide write+shell+mcp),
  `manual` (pide todo). `setApprovalPolicy`.
- Una tool a la vez: se emite `toolApprovalNeeded(map)` con `tool/kind/detail/diff`, se
  guarda `m_awaitId` y se espera `approveTool(id, always)` / `rejectTool(id)`.
- "Siempre" agrega el `kind` a `m_alwaysAllowed` (no vuelve a preguntar ese tipo en la
  sesión).

---

## 7. Diffs + revert

- `write_file` toma snapshot del estado previo (`m_editSnapshots`), genera diff unificado
  simple (`makeDiff`, prefijo/sufijo común + bloque +/-) y agrega una entrada `role:"diff"`
  al chat con `path`/`diff`.
- Preview del diff en la card de aprobación (antes de aplicar).
- `revertEdit(path)` restaura/borra desde el snapshot y marca la entrada como revertida.

---

## 8. Memoria + contexto

- **Project memory**: `buildSystemPrompt()` inyecta `.llamacode/memory.md` (fallback
  `AGENTS.md`) en el system prompt. Editable desde la UI
  (`readAgentMemory`/`writeAgentMemory`).
- **Personal memory**: los hechos `scope=personal` se almacenan globalmente en
  `AppLocalData/memory/personal.jsonl`, no dentro de un proyecto, y el prompt
  recibe sólo una selección acotada. Esto permite continuidad entre repos sin
  mezclar reglas privadas con la memoria del código.
- **Adaptive tool surface**: los perfiles que lo habilitan reducen las tools del
  primer turno por grupos semánticos; una intención ambigua conserva la
  superficie completa y los turnos siguientes vuelven a exponerla. El router
  usa categorías generales, no nombres de apps ni coordenadas.
- **Persistencia de sesiones** (= memoria de conversación): cada sesión se guarda en
  `AppLocalData/agent_custom/`:
  - `index.json` — lista de sesiones (id, title, projectDir…).
  - `<sessionId>.json` — `{ messages (UI), api (m_apiMessages) }`. Se persiste el historial
    de API completo (con roles tool/assistant+tool_calls) para reanudar el contexto real.
  - `switchSession` vuelca la actual y carga la destino; `newSession`/`deleteSession`
    mantienen índice + archivos.
- **Contexto visible**: `fetchContextLimit()` lee `n_ctx`; `usage.total_tokens` de cada
  respuesta → señal `contextUsage(used, limit)` → barra "ctx N/M" en el header.

---

## 9. Robustez (modelo local)

- Args JSON malformado → no ejecuta; devuelve `tool` result con error y pide reintentar.
- Args requeridos faltantes → error como tool result (no crashea el turno).
- Tool desconocida → tool result de error.
- Anti-loop (`kMaxSameCall`) y cap de iteraciones (`kMaxTurnIters`).
- Salud por sesión: contador ok/fail + log `[salud: X/Y tools ok (Z%)]`.

---

## 10. Chat vs Agente

| | Chat (`RawChatBackend`) | Agente (`CustomBackend`) |
|---|---|---|
| Tools | No | Sí (nativas + MCP) |
| Loop | 1 respuesta | ReAct multi-paso |
| Streaming | Sí | Sí (alineado en Fase 1) |
| Thinking | Sí (separa reasoning) | Sí (idem) |
| Aprobación | — | human-in-the-loop |
| Persistencia | `chat_raw/` | `agent_custom/` |
| Selección | página Chat | tarjeta en Profiles |

El trabajo de estabilización de Fase 1 portó al agente los patrones probados del Chat:
streaming SSE, sin cap de tokens, control de thinking + strip-think, y persistencia a disco.
