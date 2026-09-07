# Assistant Runtime y auxiliares

LlamaCode tiene un canal estrecho para un asistente siempre disponible. El
runtime no expone `ControlApi` ni invocación reflectiva: sólo recibe texto,
mantiene una cola acotada, deduplica por `id` y publica respuestas/eventos.

## Activación

Desde la aplicación se puede llamar `startAssistantGateway(port, token, lan)`.
Si el token está vacío, LlamaCode reutiliza o genera uno en `SecretStore` bajo
`assistant/gateway`; nunca se imprime ni se incluye en discovery. El bind
predeterminado es `127.0.0.1`. El bind LAN requiere token y debe reservarse
para una red mesh o privada confiable.

También puede activarse al iniciar el ejecutable con:

```text
LLAMACODE_ASSISTANT_TOKEN=<token>
LLAMACODE_ASSISTANT_PORT=8787
LLAMACODE_ASSISTANT_LAN=0
```

## Contrato HTTP

Todos los endpoints requieren `Authorization: Bearer <token>` o `X-Api-Key`.

```text
GET  /health
GET  /v1/assistant/messages
GET  /v1/assistant/events?after=<eventId>
POST /v1/assistant/messages
```

El POST acepta `{ "id": "idempotente", "channel": "telegram", "sender":
"...", "text": "..." }`. Los adaptadores de Telegram, Discord, voz u otro
transporte deben traducir su entrada a este contrato y sus respuestas a su
canal. La cola tiene límite, el cuerpo HTTP está limitado y los eventos se
persisten en `AppLocalData/assistant-events.json`; el cursor `after` evita
reenviar eventos ya confirmados.

## Memoria personal

Los hechos con `scope=personal` se guardan en
`AppLocalData/memory/personal.jsonl`, separados de
`<proyecto>/.llamacode/memory.jsonl`. El agente recibe sólo un bloque acotado de
hechos personales estructurados además de la memoria del proyecto. `forget`
sigue permitiendo borrar o marcar obsoleto por scope.

## Routing adaptativo de tools

Los perfiles de agente avanzado, máximo, browser y RPA pueden activar
`HarnessSpec.tools.adaptiveRouting`. En el primer turno se seleccionan grupos
semánticos —archivos, búsqueda, código, web, browser, escritorio, correo,
multiagente, skills y plugins— y se mantiene la superficie completa en turnos
posteriores. Una intención ambigua falla abierta, evitando bloquear una tool
válida. El router no depende de nombres de aplicaciones, coordenadas ni layouts.

## Roles y fallbacks

`ModelRoleRegistry` expone roles configurables para `primary_agent`,
`fast_agent`, `planner`, `verifier`, `embedding`, `reranker`, `stt`, `tts` y
`vision`. Cada rol guarda modelo preferido, fallback, endpoint, clase de cola,
recurso, prioridad y concurrencia en `AppLocalData/model_roles.json`.
`enqueueModelRoleJob()` usa esa ficha para alimentar el scheduler auxiliar sin
interrumpir el turno interactivo. La ejecución del worker sigue siendo
explícita y observable en `auxiliaryScheduler.jobs`.
