# TODO - LlamaCode

## P0 - Núcleo multi-llama.cpp ✅

- [x] Crear `BinaryRegistry` (CRUD)
- [x] Entidad `LlamaBinary`
- [x] Registrar múltiples `llama-server.exe`
- [x] Detectar capacidades por `--help` (async, timeout 15s)
- [x] Persistir capabilities cacheadas por hash SHA256 del binario
- [x] UI para elegir binario activo por perfil (`BinariesPage.qml`)
- [x] Estado de binario inválido/no encontrado (`pathValid`)

## P0 - Núcleo multi-GGUF roots ✅

- [x] Crear `ModelRootRegistry` (CRUD)
- [x] Agregar múltiples carpetas de modelos
- [x] Prioridades y tags por root (`main`, `vision`, `draft`)
- [x] Escaneo manual + al inicio (`scanMode`: manual/startup/watch)
- [x] `GGUFScanner` async via `QtConcurrent` + inferencia familia/quant
- [x] Persistir catálogo en SQLite (`ModelCatalog`)
- [x] Estado root offline/unavailable sin perder historial (`isAvailable`)

## P0 - Perfiles compuestos ✅

- [x] Separar entidades: `BackendProfile`, `ModelProfile`, `RuntimePreset`, `HarnessProfile`, `WorkspaceProfile`, `LaunchProfile`
- [x] Implementar `EffectiveProfileBuilder`
- [x] Resolver merge + overrides
- [x] Salida: `effectiveArgs`, `effectiveEnv`, `warnings`, `blockingErrors`
- [x] Command Preview exacto y copiable (`CommandPreview.qml`)

## P0 - UI base ✅

- [x] App Qt Quick arranca sin errores (`WIN32_EXECUTABLE`)
- [x] NavBar + páginas (Binaries, Model Roots, Profiles, Launch, Chat, Agente)
- [x] Tema Catppuccin Mocha
- [x] `LcButton`, `LcTextField`, `LcDialog`, `NavBar`, `PageHeader`, `CommandPreview`

## P1 - Validación y compatibilidad ✅

- [x] Matriz flag/capability por binario (`EffectiveProfileBuilder.addFlag`)
- [x] Reglas de degradación (warning) vs bloqueo (error)
- [x] Validar modelo/mmproj/draft antes de start
- [x] Validar colisión de puerto antes de start (server: `QTcpServer::listen` probe en `startServer`; opencode: pre-kill puerto 4096)
- [x] Diagnóstico temprano: verificar que el binario existe y es ejecutable (`QFileInfo` exists/isFile/isExecutable en `startServer`)

## P1 - Ejecución y observabilidad (parcial)

- [ ] `LlamaProcessManager` dedicado (extraer de `AppController`)
- [x] Logs en vivo stdout/stderr (en AppController + AgentPage Vista terminal)
- [x] Filtros de log por nivel (`serverLogByLevel(level)` invokable: all/error/warn/stderr/stdout/lifecycle/health/diag + combo en UI)
- [x] Detecciones automáticas por regex en log (`detectServerLogPatterns`: OOM, port busy, modelo cargado, arg inválido, load fail, context-shift → señal `serverDiagnostic(level,msg)`)
- [x] Botón copiar comando

## P1 - Process lifecycle ✅

- [x] Windows Job Object: hijos mueren al cerrar LlamaCode
- [x] Env vars de trazabilidad: `LLAMACODE_MANAGED`, `LLAMACODE_ROLE`, `LLAMACODE_APP_PID`
- [x] PID state file (`services.json`): detecta y mata orphans al iniciar
- [x] Pre-kill de puerto 4096 al levantar opencode
- [x] Stop asíncrono: `serverStopping` property + botón "Deteniendo..." + kill fallback 5s (UI no se congela)

## P1 - Endpoint health

- [x] `GET /health` polling post-start (`startHealthPolling`, intervalo 2s, set `serverReady` al 200)
- [x] `POST /v1/chat/completions` test prompt mínimo (smoke HTTP aislado en `test_backends_net`)
- [x] Medir latencia first-token (campo `firstTokenMs` por respuesta de chat raw)
- [x] UI de estado: iniciando / listo / error (`serverReady`/`serverStopping`/`serverError`)

## P2 - UX de perfiles (parcial)

- [x] Duplicar perfil
- [x] Renombrar perfil
- [x] Eliminar perfil
- [x] **Importar perfil desde argumentos CLI** (parsea --host, --port, --model, --ctx-size, --batch-size, --ubatch-size, --threads, --n-gpu-layers, --flash-attn, --no-mmap, --mlock, --parallel, --cache-type-k)
- [x] Plantillas de perfil reutilizables (guardar/aplicar/eliminar vía `ProfileManager`, con referencias, args, env y tags)
- [x] Búsqueda por nombre, alias, id o etiquetas en la pantalla de perfiles
- [x] Favoritos y orden de favoritos en menús; último usado se conserva mediante el perfil activo persistido
- [x] Export/Import de perfiles completos (bundle JSON, ids y referencias preservados)
- [x] Historial de cambios por perfil (`profileChangeHistory`, JSONL append-only y test headless)

## P2 - Model Catalog avanzado

- [x] Dedupe por hash SHA256 para binarios; los modelos conservan ids estables y metadatos cacheados
- [x] Filtros por familia/vision/root (UI); cuantización y tamaño quedan visibles en las tarjetas del catálogo
- [x] Marcar compatibilidad vision/draft manualmente (UI + API headless, persistido fuera del scanner)
- [x] Asociación rápida modelo → perfil desde ProfilesPage

## P3 - Harness opencode ✅

- [x] Integración HTTP API nativa (POST `/session`, `/session/{id}/prompt_async`, GET `/event` SSE)
- [x] Eliminado conflicto de DB SQLite (no más subproceso `opencode run`)
- [x] Vista Agente: chat bubbles con streaming en tiempo real (`message.part.delta` SSE)
- [x] Vista terminal: log raw para debug
- [x] Indicador "⏳ Procesando..." + cursor `▌` durante streaming
- [x] Panel lateral de sesiones con agrupación por proyecto (directorio)
- [x] Resume automático de última sesión (QSettings `opencode/lastSessionId`)
- [x] Creación de nueva sesión desde UI
- [x] Switch entre sesiones con carga de historial (`GET /session/{id}/message`)
- [x] Actualización de título de sesión en tiempo real vía SSE `session.updated`
- [x] Limpieza de sesión/SSE al detener agente
- [ ] `AiderCliAdapter`
- [x] Templates args/env por harness (persistidos en `HarnessProfile` y expuestos por `ProfileManager`)
- [x] Adjuntar archivos al mensaje (texto, documentos e imágenes con filtro de visión)

## P4 - Chat integrado ✅

- [x] Chat streaming directo a `llama-server` vía `/v1/chat/completions` SSE
- [x] Estado gestionado en `AppController` (no en QML): `chatMessages`, `chatGenerating`, `sendChatMessage`, `stopChatGeneration`
- [x] Sesiones persistidas en JSON (`AppLocalData/LlamaCode/chat/{id}.json`)
- [x] Índice de sesiones (`index.json`) con título, fecha, projectId
- [x] Auto-título desde primer mensaje del usuario
- [x] Panel lateral de chats agrupado por proyecto (launch profile)
- [x] Switch entre chats con carga de historial
- [x] Nueva sesión desde UI
- [x] Stop de generación con guardado
- [x] Indicador "⏳ Procesando..." + cursor `▌`
- [x] Sampling configurable por sesión (temperature/top-p/top-k, persistencia JSON, payload y panel ChatPage)
- [x] Export conversación (Markdown/JSON) (`exportChatSession(id,format)` + items en menú contextual de ChatPage)
- [x] Búsqueda en historial (`searchChatHistory(query)` invokable: matchea título + contenido, devuelve snippet + panel de búsqueda en UI)

## P5 - Built-in agent nativo ✅ (`LlamaAgentBackend`, loop ReAct)

- [x] `read_file` (offset/limit, dedup por huella)
- [x] `write_file` (aprobación + diff + snapshot/revert)
- [x] `edit_file` (reemplazo exacto, aprobación)
- [x] `list_dir` (recursivo opcional)
- [x] `grep` (regex) + `glob`
- [x] `run_shell` (async, output en vivo, timeout, cancelación)
- [x] `web_fetch`
- [x] `task` (subagents paralelos en git worktrees)
- [x] MCP stdio (tools `mcp__server__tool`)
- [x] Bloqueos de seguridad por workspace (confinamiento cwd, permisos por patrón, plan mode)
- [x] Checkpoint/rollback de conversación
- [x] Imágenes al agente (detección de visión por `--mmproj`)

## P6 - Benchmarking

- [x] `BenchmarkRunner`: `runBenchmarkInternal` lanza perfiles en secuencia vía `AppController`
- [x] `BenchmarkSession`: resultados JSON por perfil con RAM/VRAM, t/s, tiempos y scores
- [x] Modo **Corta** y **Completa** con suites estándar y scoring de aceptación
- [x] Editor/importador de prompts personalizados y suites por categoría
- [x] Scoring post-corrida y re-scoring sin repetir inferencia
- [x] Persistencia en JSON (`benchmarks/{timestamp}/...`)
- [x] `BenchmarkPage.qml`: tabla comparativa con columnas ordenables y filtros
- [x] Exportar resultados a CSV desde la UI y `AppController::exportBenchmarkResultsCsv`
- [x] Selección multi-perfil y cola de benchmarks personalizados
- [x] Calidad relativa y comparación contra baseline en `comparison.json`

## Calidad

Target de tests: `cmake -B build_tests -DBUILD_TESTS=ON` → `LlamaCodeTests` (Qt Test). `tests/test_core.cpp`. 17/17 pasan.

- [x] Tests `BinaryRegistry` (add/get/update/remove, hash y persistencia en `test_registries`)
- [x] Tests `ModelRootRegistry`
- [x] Tests `EffectiveProfileBuilder` (host/port, drop flag no soportado, modelo faltante = blocking)
- [x] Tests `GGUFScanner` (inferencia familia/quant/vision/draft)
- [x] Tests `AppController` chat session CRUD

## Pendientes deferidos (jun-2026) — backend/infra listo, falta lo anotado

- [ ] **LlamaProcessManager dedicado** — extraer ciclo de vida de proceso de `AppController` a clase propia. Refactor arquitectónico grande, alto riesgo, bajo ROI ahora. No empezado.
- [x] **ControlApi `reqId` estable** — acepta `reqId` por body/query/header (`x-req-id`/`reqid`), lo genera si falta y lo devuelve en respuestas/errores. Falta propagar ese id a logs de Tasks/agente/benchmark.
- [x] **Scheduler de operaciones auxiliares** — `AuxiliaryJobScheduler` separa del `TaskScheduler` cron una cola interna por clases de trabajo, prioridad, recurso ocupado, cancelación y snapshot consultable. Está expuesto como subobjeto observable de `AppController` y es invocable por `ControlApi` para encolar, iniciar, completar/cancelar y consultar trabajos reales.
- [x] **Sampling por sesión (chat)** — temperature/top-p/top-k/min-p/repeat penalty, persistencia JSON, payload, medición first-token y panel en ChatPage.
- [x] **Panel UI de búsqueda en historial** — campo de búsqueda + resultados (snippet y `switchChatSession`) en ChatPage.
- [x] **Combo UI de filtro de log por nivel** — selector all/error/warn/stderr/stdout/lifecycle/health/diag en LaunchPage.
- [x] **Banner UI de `serverDiagnostic`** — aviso no bloqueante en la vista de log del servidor.
- [ ] **Verificación GUI e2e de subagents con LLM vivo** — requiere server+modelo corriendo. Plumbing git/worktree/merge/abort ya validado; falta corrida real con el modelo manejando `task`.
- [x] **Tests `BinaryRegistry`** — add/get/update/remove, hash y persistencia en `test_registries`.
- [x] **Tests `ModelRootRegistry`** — add/remove, escaneo GGUF, persistencia y Ollama en `test_registries`.
- [x] **Tests `AppController` chat session CRUD** — sesiones raw, cola, rename/delete/move y persistencia en `test_backends_net`/`test_appcontroller`.

## Memoria estilo Thoth/GraphRAG (provenance+forget+grafo+consolidación ✅, commits c099c4d/42b1084/76c1003)

- [x] **Evidencia durable del grafo** — `SourceRef` con ruta/rango/SHA-256,
  `query packet`, `graph doctor`, `KnowledgePacket` acotado y política opt-in del
  `HarnessSpec`; el reindexado post-escritura funciona también fuera de Debug.
- [x] **Gate de calidad sobre lo consolidado** — `consolidateMemory()` reutiliza la verificación de `verify_claims`: descarta inferencias sin respaldo y baja confidence cuando la evidencia es parcial antes de persistir.
- [x] **Grafo inferido automático** — los `write_file`/`edit_file` exitosos registran módulo→archivo con provenance de tool/sesión; la consolidación conecta decisión→bug sólo cuando comparten vocabulario y mantiene ambos edges como unreviewed.

## Backend RAG compacto opcional (LEANN)

- [ ] **Evaluar LEANN como sidecar CLI/MCP opcional** — no reemplazar el buscador integrado. Mantener `hybrid_search` actual (BM25 + embeddings + RRF, presupuesto de tokens y expansión por dep-graph) como backend default y fallback sin dependencias externas.
- [ ] **Abstracción de backend de retrieval** — permitir seleccionar por workspace `builtin` o `leann`, conservando una salida normalizada para que el agente y el empaquetado de contexto no dependan del proveedor del índice.
- [ ] **Fusión híbrida** — combinar resultados LEANN con BM25 local mediante RRF; no depender exclusivamente del índice vectorial para símbolos, errores y frases exactas.
- [x] **Indexación incremental por hash** — Project Brain persiste SHA-256 por archivo, reutiliza entradas cuyo tamaño+mtime no cambió y reporta agregados/modificados/eliminados/reutilizados. La invalidación de chunks SQLite sigue siendo una optimización independiente futura.
- [ ] **Chunking AST ampliado** — evaluar el chunking de LEANN para Python, Java, C# y TypeScript y extenderlo a los lenguajes relevantes de LlamaCode. Preservar metadata de archivo, símbolo y rango de líneas para filtros y citas.
- [ ] **Criterio de activación** — ofrecer LEANN principalmente para repositorios o colecciones grandes (documentos, chats e historiales), donde el ahorro del índice compense la latencia de recomputación y las dependencias Python/HuggingFace.
- [ ] **Benchmark antes de integrar** — medir con 10k, 100k y 1M chunks: tamaño del índice, tiempo de build/actualización, búsqueda fría/caliente, recall@k, RAM/VRAM, latencia y comportamiento tras altas/bajas/modificaciones.
- [ ] **Validación Windows y distribución** — comprobar instalación y ejecución nativa en las versiones soportadas de Windows antes de exponerlo en UI; definir instalación aislada, detección de capacidades, health check y mensajes de fallback.
- [ ] **Licencia y atribución** — LEANN es MIT. Si se incorpora código o datos, registrar versión/fuente y agregar la atribución correspondiente en `README.md`; si sólo se integra como proceso externo, documentar la dependencia opcional.

## Definition of Done MVP real

- [ ] 3+ binarios registrados y seleccionables
- [ ] 3+ roots GGUF escaneadas y navegables
- [ ] 10+ perfiles compuestos persistidos y recargables
- [x] Start/Stop con comando reproducible
- [x] Command Preview exacto y copiable
- [x] Chat streaming funcionando con historial
- [x] Harness opencode con sesiones y proyectos
- [ ] Test API exitoso desde perfil activo (health check automático)
- [x] Reapertura de app sin pérdida de estado (chat + sesiones opencode)
- [x] Subprocesos orphan limpiados al reiniciar
