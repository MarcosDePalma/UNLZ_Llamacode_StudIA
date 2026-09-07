# Revision de Omnix para LlamaCode

Fuente revisada: https://github.com/LoanLemon/Omnix, README y `package.json`
publicos de la rama `main` al 2026-07-01.

## Resumen

Omnix apunta a ser un estudio local multimodal basado en Electron/React,
Transformers.js, ONNX, WebGPU/WASM y una API local propia. LlamaCode tiene otro
nucleo: app nativa Qt/QML, orquestacion de `llama.cpp`/GGUF, perfiles compuestos,
agente de codigo, Tasks, voz, vision y ControlApi headless. Por eso Omnix no
conviene como dependencia ni como base de codigo, pero si deja patrones utiles
para endurecer automatizacion local y multitarea.

## Matriz

| Decision | Idea | Aplicacion en LlamaCode |
|---|---|---|
| Adoptar | `reqId`/correlation id aceptado por body, query o header | Agregar un id estable por request/corrida a ControlApi, Tasks, agente, logs y eventos. Si falta, generarlo en la app. Debe viajar en respuestas y errores. |
| Adoptar | Modo headless documentado como producto | Consolidar `docs/control-api.md` como contrato publico: versionado, health enriquecido, ciclo de vida y ejemplos para CI/agentes externos. |
| Adaptar | API local multimodal simple (`/api/text`, `/api/vision`, `/api/stt`, `/api/tts`) | No duplicar OpenAI-compatible ni la reflexion Qt actual. Agregar wrappers estables de alto nivel sobre ControlApi para operaciones frecuentes cuando haga falta compatibilidad externa. |
| Adaptar | Separacion entre worker de texto y worker de operaciones auxiliares | Implementar un scheduler interno por clases de trabajo, no Web Workers. Texto/agente debe tener prioridad; STT/TTS, extraccion de documentos, RAG, verificacion, imagen y benchmarks no deben bloquear el turno interactivo. |
| Adaptar | `--silent`, `--dependent-pid`, `PORT`, singleton | LlamaCode ya tiene Job Object, PID state y `LLAMACODE_CONTROL_PORT`. Puede sumar modo engine/headless formal y attachment a un proceso padre para integraciones. |
| Descartar | Electron/React como superficie de escritorio | No aporta: LlamaCode es Qt/QML nativo y ya tiene distribucion/build propios. |
| Descartar | Transformers.js/WebGPU como runtime principal | Choca con el foco `llama.cpp`/GGUF, perfiles, hardware-fit y agente local. Podria integrarse solo como servicio externo opcional. |
| Descartar | Generacion de musica como prioridad de producto | Es lateral al objetivo de estacion local de agentes/codigo. Si se agrega, deberia entrar como API service externo y no como nucleo. |

## Prioridad recomendada

1. **ControlApi con `reqId` estable**.
   - Entrada: body JSON `reqId`, query `?reqId=...`, headers `x-req-id` o `reqid`.
   - Salida: incluir `reqId` en toda respuesta, error, log estructurado y evento asociado.
   - Si el cliente no lo envia, generar uno (`QUuid`) y devolverlo.
   - Usarlo como correlacion para Tasks, benchmark, agente, browser, mail, voz y operaciones largas.

2. **Scheduler interno para operaciones auxiliares**.
   - Separar clases de trabajo: `interactive_text`, `agent_tool`, `voice`, `document`,
     `retrieval`, `verification`, `benchmark`, `background_maintenance`.
   - Limitar concurrencia por clase y por recurso: modelo principal, GPU/VRAM,
     red, disco y procesos externos.
   - Mantener prioridad para chat/agente; las operaciones pesadas deben pausar,
     encolarse o degradar antes de bloquear el turno del usuario.
   - Exponer estado por ControlApi para que Tasks y scripts sepan si una operacion
     esta `queued`, `running`, `completed`, `failed` o `cancelled`.

## Encaje con la arquitectura actual

- `docs/control-api.md` ya cubre la API headless reflexiva. El cambio natural es
  estabilizar un pequeno contrato encima, no reemplazar la introspeccion Qt.
- `TaskScheduler` hoy dispara Tasks por cron; no deberia convertirse en scheduler
  global de recursos. Conviene una pieza separada para trabajos auxiliares y dejar
  `TaskScheduler` enfocado en calendario/automatizaciones.
- `AppController` ya concentra demasiadas rutas de ejecucion. Si se implementa el
  scheduler auxiliar, deberia entrar como clase de `src/core`, con senales Qt y
  pruebas unitarias de prioridad/cancelacion.
- Cualquier integracion con Omnix deberia ser via perfil de API service externo:
  endpoint, health check y capabilities. No incorporar Node/Electron al binario
  principal.

## Pendientes concretos

- [x] Definir formato minimo de respuesta ControlApi v1:
  `{"ok":true,"reqId":"...","result":...}` y
  `{"ok":false,"reqId":"...","error":"...","available":[...]}`.
- Agregar `reqId` a los logs de ejecucion de Tasks y agente.
- [x] Disenar `AuxiliaryJobScheduler` con colas, prioridad, cancelacion y estado
  consultable por ControlApi.
- Documentar compatibilidad de API services externos para endpoints tipo Omnix,
  sin prometer soporte nativo de Transformers.js/WebGPU.
