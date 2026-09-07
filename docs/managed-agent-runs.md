# Corridas administradas de Claude Code y Codex

LlamaCode puede iniciar una corrida larga de un CLI externo desde **Agente →
🚀 Corridas** o mediante `ControlApi`. La corrida no depende de que una ventana
de terminal permanezca abierta: `ManagedAgentRunStore` conserva el proceso,
observa stdout/stderr y publica el mismo estado en QML y en la API headless.

Esto complementa, no reemplaza, el `AgentRunStore` del backend nativo: el
primero supervisa procesos Claude/Codex; el segundo mantiene leases y journal
de los turnos del agente local.

## Contrato

La entrada mínima es:

```json
{
  "runtime": "claude",
  "prompt": "Revisá el diff y devolvé riesgos priorizados.",
  "workspace": "C:/proyectos/app"
}
```

También acepta `codex`, `cliPath`, `ownerId`, `taskId`, `agentProfileId`,
`model`, `applyEdits`, `approvalMode`, `timeoutSec`, `idleTimeoutSec`,
`maxLogBytes`, `verifyProgram`, `verifyArgs` y `presentation`. El runtime sólo
puede ser `claude` o `codex`; el workspace debe existir y el prompt tiene un
límite de 200000 caracteres.

Cada corrida queda en:

```text
<AppLocalData>/managed-agent-runs/<runId>/
  prompt.md
  manifest.json
  stdout.log
  stderr.log
  verification.log
```

El manifiesto no duplica el prompt: guarda su ruta, tamaño y SHA-256. Al
cerrar la corrida se agrega un registro a `RunHistoryStore`, con metadata de
runtime, workspace, postura de permisos y rutas de artefactos. Por eso una
corrida puede exportarse luego como parte de `llamacode.evidence.v1`.

## Seguridad y visibilidad

- El default es `plan` para Claude y una intención de lectura para Codex.
- `applyEdits` es explícito. Claude usa `acceptEdits`; no se activa
  `bypassPermissions` automáticamente.
- Codex sólo recibe `--full-auto` si además se envían explícitamente
  `approvalMode: "super"` y `allowDangerous: true`.
- El prompt viaja por `stdin` por defecto y no queda expuesto en la línea de
  comandos. `promptTransport: "argv"` existe sólo como compatibilidad explícita
  y queda marcado en el manifiesto.
- Las corridas que editan reservan el workspace completo en `WorkRegistry`,
  mantienen heartbeat y se detienen si pierden el claim. Así no pisan una sesión
  nativa que ya reclama archivos.
- `captureDeliverables` toma un snapshot antes/después mediante
  `AgentDeliverableStore`. Un `exit code 0` se informa como
  `completed_unverified`, `artifacts_captured` o `verified`; sólo un verificador
  explícito (`verifyProgram` + `verifyArgs`) produce `verified`.
- `timeoutSec`, `idleTimeoutSec` y `maxLogBytes` limitan procesos y artefactos.
  La retención por defecto conserva 40 corridas terminadas; puede cambiarse con
  `LLAMACODE_MANAGED_RUN_RETENTION`.
- `visibleProof` distingue `managed_panel` de una ventana de escritorio. Ver
  un proceso en LlamaCode no se presenta como prueba de que el CLI tenga una
  ventana visible. En Windows se puede pedir `presentation: "console"` para
  crear una consola separada; el manifiesto lo marca como
  `console_requested` (la prueba sigue siendo la corrida y sus artefactos).
- Si LlamaCode se cierra, las corridas que estaban activas se recuperan como
  `stale`, liberan su claim y dejan un registro de interrupción. Desde la UI o
  `retryRun` se puede reintentarlas con el prompt durable.

## Delegación desde el agente

Cuando el backend nativo usa `ask_teacher` con un CLI Claude/Codex, el handoff
usa este mismo store: AppController arranca la corrida en el hilo de UI, el
worker espera un evento acotado y recibe el closeout sin mantener un `QProcess`
cruzado entre hilos. Un timeout del handoff solicita la cancelación de la corrida.
El detector `MasterCli` expone versión, `probeOk` y capacidades observadas por
`--help` para que una UI o integración pueda advertir un wrapper incompatible.

La implementación sigue el patrón útil de Hermes Valkyrie —prompt durable,
manifiesto, watcher y closeout— pero usa `QProcess`, `RunHistoryStore` y las
políticas de aprobación propias de LlamaCode, sin depender de tmux ni de
flags de bypass.
