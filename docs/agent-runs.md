# Corridas durables y entregables

El backend nativo asigna una corrida durable a cada turno de una sesión
persistente. La corrida no depende sólo de la memoria de `LlamaAgentBackend`:
queda registrada en el namespace del harness activo, bajo
`AppLocalData/LlamaCode/<harness>/agent_runs/`.

## Contrato de una corrida

`AgentRunStore` persiste:

- una identidad estable (`runId`/`correlationId`) y un hash del pedido lógico;
- sesión, workspace, objetivo acotado y snapshot de metadatos inicial;
- estado, intento, timestamps, owner y vencimiento del lease;
- un journal JSONL con secuencia por corrida;
- metadata final, incluyendo el resumen del entregable capturado.

Los estados terminales son `completed`, `failed`, `cancelled` e `interrupted`.
`queued`, `running`, `waiting_approval` y `waiting_user` son estados activos.
`uncertain` significa que venció el lease mientras un efecto podía estar en
vuelo; el backend nunca lo reejecuta automáticamente.

El flujo normal es:

```text
accept → queued → claim → running → completed|failed|cancelled|interrupted
                                  └→ uncertain (lease vencido)
```

El token del lease sólo se entrega al owner que reclama la corrida. Las
transiciones y heartbeats que no presentan el token vigente son rechazados.
Las notificaciones o señales de UI no son la fuente de verdad: el registro y
el journal se pueden leer nuevamente después de reiniciar.

## Entregables

Al terminar una corrida, `AgentDeliverableStore` compara snapshots de hashes del
workspace y conserva sólo archivos creados o modificados. No copia `.git` ni
`.llamacode`, limita el tamaño de cada archivo y el total por corrida, y genera
un manifiesto en:

```text
AppLocalData/LlamaCode/agent_deliverables/<runId>/manifest.json
```

La captura es idempotente. `saveAs`/`restore` nunca sobrescribe por defecto;
el caller debe pasar `overwrite=true` después de obtener una aprobación de la
UI. El manifiesto conserva el hash anterior y posterior, el estado
`created|modified|deleted`, el tamaño y si existe una copia restaurable.

Para tests o herramientas headless se pueden redirigir ambos stores con
`LLAMACODE_DELIVERABLES_DIR` y una raíz de `AgentRunStore` explícita.

La captura es una evidencia de archivos, no un commit automático: los cambios
siguen perteneciendo al workspace y el usuario decide si los restaura, guarda
en otro destino o los integra en Git.

## Inbox y recuperación

La página Agente ofrece `📦 Inbox` para leer todas las corridas de los namespaces
del harness, mostrar el estado `uncertain`, abrir el almacenamiento local y
examinar el manifiesto. `Guardar como` usa `saveAs` con `overwrite=false`; la UI
no pisa un destino existente de forma implícita.

Cerrar una corrida `uncertain` es una decisión humana explícita. El contrato sólo
acepta `cancelled` o `failed`, registra `run.uncertain_resolved` y no vuelve a
reclamar ni ejecutar el objetivo original. Los metadatos de entregables pueden
adjuntarse después de la transición terminal mediante `run.metadata_merged`,
porque el snapshot y la captura se realizan en tareas concurrentes para no
bloquear la interfaz.

El journal y el registro se protegen con `.store.lock`; cada captura usa además
un lock por corrida y el índice global usa `.index.lock`. Los locks abandonados
se consideran stale después de 30 segundos. Las lecturas que llegan a QML usan
una representación segura: no incluyen `leaseToken` ni `beforeSnapshot`.
