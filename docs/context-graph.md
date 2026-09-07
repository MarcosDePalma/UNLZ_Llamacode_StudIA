# Índice y grafo de contexto del agente

LlamaCode mantiene un índice local, descartable y regenerable para que el agente
pueda explorar un repositorio por evidencia antes de editarlo. Está inspirado en
las ideas de Graft y archex, pero forma parte del backend nativo Qt/C++ y no
requiere Node, Python, ONNX ni un servicio externo.

## Capas

- `ProjectBrain`: inventario liviano de rutas, tamaños, fechas y hashes.
- `ContextIndex`: chunks persistentes, frescura, handles y edges de imports.
- `CodeGraphIndexer` + `GraphStore`: grafo estructural y conocimiento persistente
  existente, que no se reemplaza.
- `KnowledgePacket`: une hechos de `MemoryStore` con el vecindario relevante del
  grafo y devuelve un recibo compacto para el prompt o la tool.
- `WorkRegistry`: estado efímero y expirable de claims activas por proyecto; no
  reemplaza `AgentEventLog`, que conserva el historial.
- `hybrid_search` / `repo_slice`: búsqueda BM25 + embeddings opcionales + RRF y
  reranking cuando el server los expone.
- `context_scout`: exploración compacta con presupuesto y recibo.
- `context_fetch`: lectura exacta de un rango, validada contra el hash del archivo.

El índice de chunks vive en `AppLocalData/LlamaCode/context/<repo-hash>.sqlite`.
Puede borrarse sin perder código, sesiones ni memoria; la siguiente consulta lo
reconstruye. Los chunks no se envían a ningún proveedor salvo que el resultado se
inyecte en una petición del modelo configurado.

## Grafo con evidencia

Cada edge generado por `CodeGraphIndexer` conserva `SourceRef` con ruta relativa,
rango de líneas y SHA-256 del archivo. Las relaciones creadas por el agente
pueden agregar las mismas referencias mediante la API C++. `graph query` mantiene
la salida Markdown para compatibilidad y acepta `format: "packet"` para devolver
`nodes`, `edges`, `sources` y un `receipt`; cada edge incluye `verified` o
`unreviewed` y sus citas `ruta:Línea-Línea`. Una relación no revisada no debe
presentarse como un hecho confirmado.

`graph doctor` valida el JSONL, detecta edges huérfanos, líneas inválidas y
fuentes cuyo hash ya no coincide. Es una comprobación de salud, no una migración:
el JSONL append-only sigue siendo legible por versiones anteriores. Después de
`write_file` y `edit_file`, el backend actualiza ProjectBrain, ContextIndex y
CodeGraphIndexer también en Release; la selección de icono Debug no participa en
esa ruta. Un `run_shell` exitoso compara el snapshot de ProjectBrain antes/después
y sólo si detecta rutas agregadas, eliminadas o modificadas refresca los chunks y
el grafo. Las tools externas pueden aportar `path`, `file_path`, `paths` o campos
anidados equivalentes; las rutas fuera del workspace se descartan antes de indexar.

`KnowledgePacket::build()` combina el recall de memoria con edges cuyo nombre
coincide con la consulta. Las decisiones aparecen separadas de los hechos de
apoyo y el paquete declara la precedencia `código/tests actuales → decisión
vigente → memoria verificada → inferencia no revisada → historial`. Sus límites (`maxFacts`, `maxEdges`, `maxChars`) son
parte del módulo `knowledge` de `HarnessSpec`; está apagado por defecto para
conservar el presupuesto histórico. Al activarlo, el system prompt instruye al
modelo a distinguir evidencia verificada de inferencias y a citar las fuentes.

## Hardening de coordinación y memoria

Las mutaciones no pueden esconderse detrás del nombre de una tool. El backend
clasifica comandos shell con redirecciones, verbos de filesystem y ediciones
embebidas, y consulta el contrato de seguridad de cada tool MCP. Si una llamada
puede escribir y no declara `changed_paths`, se rechaza antes de ejecutarse. Las
tools internas `memory` y `graph` usan sus rutas persistentes conocidas. Cuando
hay rutas declaradas, se reclaman atómicamente en `WorkRegistry`; una claim viva
de otra sesión produce un rechazo explícito.

`MemoryStore` protege el JSONL con lock y reescrituras atómicas. Además, cada
recall puede disparar mantenimiento como máximo una vez por día: `decay` marca
stale sólo hechos antiguos y de bajo valor, preservando decisiones/preferencias
verificadas, recuerdos importantes y hechos reutilizados. El mantenimiento deja
un recibo en `.llamacode/memory_maintenance.json`; `memory action=decay` permite
un `dry_run` auditable antes de forzar otros umbrales.

Las corridas durables aplican el mismo cierre estricto: una respuesta no pasa a
`completed` hasta que el snapshot inicial y el manifiesto de entregables están
disponibles. Si el lease vence durante la captura, la corrida queda `uncertain`
para resolución humana; no se reejecuta ni se publica un éxito sin evidencia.

El preflight también adjunta las claims de `WorkRegistry` de otras sesiones. Antes
de cada escritura, el backend vuelve a comprobar las rutas y rechaza una edición
que se solape con una claim viva de otro agente. Las claims se guardan en
`.llamacode/active_work.json`, tienen heartbeat/TTL y se eliminan al cerrar el
turno; si un proceso muere, la expiración evita dejar el proyecto bloqueado.

La consolidación de memoria reutiliza el mismo verificador que expone la tool
`verify_claims`: una inferencia sin cobertura suficiente se descarta y una
afirmación parcialmente respaldada se conserva con confidence máxima 0.55. Las
afirmaciones explícitas del usuario, tests o tools mantienen su provenance. Los
`write_file`/`edit_file` exitosos agregan edges inferidos `module:<directorio>`
→ `archivo` con `prov=tool`, `sessionId` y `correlationId`; además, decisiones y
bugs consolidados que comparten vocabulario se relacionan con `prov=consolidation`.
Todos esos edges quedan `unreviewed` hasta una revisión explícita.

## Contrato de exploración

`context_scout` devuelve rangos, previews, handles y un `context-receipt` con:

- `freshness` y `indexRevision`;
- archivos devueltos y omitidos;
- vecinos del grafo y vecinos cortados;
- `usedTokensEst`, `remainingBudgetEst` y `budgetCut`;
- `recommendedNextAction`.

Un handle `ctx:...` sólo sirve mientras el hash del archivo siga coincidiendo.
Si el archivo cambió, `context_fetch` lo rechaza y pide ejecutar nuevamente el
scout. Así los rangos no se convierten en evidencia silenciosamente obsoleta.

## Modos del harness

El módulo `context` del `HarnessSpec` conserva los defaults históricos y permite:

- `preflight`: prepara contexto antes del primer turno;
- `indexPolicy`: `off`, `lazy` o `eager`; `off` evita la indexación automática y el
  preflight, pero las tools explícitas pueden construir una caché bajo demanda;
- `scoutBudget`: presupuesto inicial del scout;
- `scoutK`: cantidad máxima de rangos;
- `graphExpansion`: incluir vecinos estructurales.

El módulo `knowledge` agrega:

- `enabled`: habilita el paquete durable en el harness;
- `preflight`: lo incluye antes del primer request;
- `citeSources`: exige citas de las fuentes disponibles;
- `maxFacts`, `maxEdges`, `maxChars`: límites explícitos del paquete.

El preflight está desactivado por defecto hasta completar el benchmark A/B. El
editor QML permite activarlo por perfil. Los presets `agent-avanzado` y
`agent-maximo` lo habilitan; `agent-intermedio-next` queda sin él para conservar su
comparabilidad con Intermedio. Release mantiene el flujo histórico de los demás
perfiles hasta que las métricas demuestren una mejora sin regresiones.

## Eventos normalizados del ciclo

`IAgentBackend` expone `agentLifecycleEvent` con un mapa estable, independiente del
harness externo. Los eventos actuales son `session.start`, `prompt.submit`,
`context.preflight`, `tool.request`, `tool.start`, `tool.finish` y
`context.resync`. Incluyen sesión, workspace, correlación, tool y rutas afectadas
cuando están disponibles. Esto permite agregar observadores o políticas sin
depender de que un proveedor use `file_path`, `path` o un patch en `command`.

El contrato se emite también desde `RawChatBackend` y `OpencodeBackend`, no sólo
desde el loop nativo. OpenCode adapta `message.part.updated` y
`permission.asked`; si una tool queda pendiente cuando la sesión pasa a `idle` o
el proceso termina, se emite igualmente un `tool.finish` fallido para que ningún
observador quede esperando un cierre. En el backend nativo, los rechazos por
permisos, anti-loop, argumentos inválidos, cancelación, timeout y workers externos
usan la misma regla de finalización única.

## Invariantes

1. El código fuente real es la fuente de verdad; el índice es sólo una caché.
2. El retrieval debe respetar el presupuesto o declarar el corte.
3. La búsqueda estructural funciona sin embeddings ni red.
4. Toda expansión debe conservar procedencia y tipo de relación.
5. Las actualizaciones posteriores a `write_file` y `edit_file` invalidan el
   contexto afectado y refrescan el grafo estructural.
6. Las citas sólo son frescas mientras el SHA-256 de su archivo coincida.
7. El índice no concede permisos adicionales ni permite salir del workspace.
8. La inferencia automática nunca se presenta como decisión confirmada: conserva
   provenance de sesión y estado `unreviewed`.
9. Una claim de trabajo no concede permisos: sólo coordina escrituras y siempre
   puede vencer o ser liberada.
10. Toda mutación shell/MCP debe declarar rutas afectadas o se bloquea de forma
    conservadora; las lecturas y builds sin efectos de filesystem no se frenan.
11. El decaimiento automático nunca borra historial: marca `stale`, protege
    evidencia verificada y tiene intervalo amortizado.
12. Una corrida durable sólo publica `completed` junto con su evidencia de
    entregables; un cierre ambiguo queda `uncertain`.

## Validación

`tests/test_context_index.cpp` cubre persistencia, chunks, edges, recibos, handles
y rechazo de handles obsoletos. `tests/test_memory_graph.cpp` cubre citas, paquetes,
`doctor`, niveles de evidencia y edges inferidos; `tests/test_code_graph.cpp`
verifica rangos y hashes producidos por el indexador. `tests/test_work_registry.cpp`
cubre claims, solapamiento de rutas, expiración y liberación. `tests/test_agent_tools.cpp`
cubre la exposición de `query packet`, `doctor` y detección de mutaciones opacas
por la tool; `tests/test_memory_graph.cpp` también cubre el decaimiento selectivo
y el resguardo de decisiones verificadas. La comparación de
calidad debe medir recall de archivos requeridos, tokens, latencia cold/warm,
cortes por presupuesto y éxito de las pruebas de la tarea.
