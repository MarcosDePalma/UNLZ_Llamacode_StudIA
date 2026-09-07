# Eficiencia, contexto estructurado y workflows

## Frugalidad y revisión de sobre-ingeniería

La directiva opt-in `honey` aplica una escalera YAGNI después de entender la
tarea: comprobar si hace falta crear algo, reutilizar lo que ya existe, preferir
stdlib/API nativa/dependencias instaladas y detenerse en el primer bloque mínimo
correcto. La frugalidad sólo reduce scaffolding, pasos y texto; nunca permite
quitar validaciones, seguridad, tests, accesibilidad, manejo de errores,
permisos, aprobación humana o verificación del resultado.

La tool read-only `review_overengineering` audita el diff de Git y devuelve:

- archivos y líneas agregadas/eliminadas;
- tamaño y truncamiento del diff;
- candidatos explicables para una delete-list;
- cambios no rastreados detectados por `git status`;
- guardrails que la revisión humana debe conservar.

No modifica archivos ni ejecuta tests. El botón **Revisar frugalidad** de la
barra del Agente solicita esta auditoría de manera opt-in. Sus sugerencias son
indicadores, no instrucciones automáticas: una abstracción puede ser necesaria
por seguridad, validación, accesibilidad, rendimiento o un contrato existente.

Para una comparación A/B reproducible, ejecutar la misma suite con el mismo
modelo, perfil, hardware y cantidad de pasadas, alternando sólo la directiva
`honey`. Cada resultado de agente persiste `agentVariant`, `honeyEnabled` y
`complexityMetrics` (`filesChanged`, `filesCreated`, `filesDeleted`,
`addedLines`, `removedLines`); `comparison.json` calcula sus medianas y deltas.
Tool calls, tokens, tiempo, éxito funcional, tests y reparaciones continúan en
la telemetría existente. Un resultado sólo es mejor si reduce complejidad o
costo sin bajar éxito ni aumentar regresiones.

LlamaCode implementa estas capacidades de forma propia y desacoplada del agente:

- `AgentEfficiency`: normaliza telemetría `timings` de llama.cpp y `usage` de
  proveedores OpenAI-compatible, agrega por fase y calcula comparaciones A/B.
  También conserva `draftTokens`, `draftAcceptedTokens` y el porcentaje de
  aceptación speculative cuando llama.cpp entrega `timings.draft_n` y
  `timings.draft_n_accepted`; si el backend no los entrega, informa `-1` en el
  porcentaje en vez de inventar una aceptación.
- Prefijo estable: el system prompt contiene un protocolo fijo y los cambios
  Plan/Ejecución se agregan como controles cortos al final del historial. El
  runner continúa aplicando el bloqueo efectivo de tools en Plan.
- Checkpoints de sesión versionados: cada turno persiste longitudes UI/API y
  restaura snapshots al abrir. Las sesiones antiguas reconstruyen checkpoints
  conservadores sin inventar snapshots de archivos.
- `StructuredSourceView`: produce una vista efímera compacta con mapeo byte a
  byte al original. Nunca escribe la proyección; rechaza sintaxis dudosa y
  lenguajes sensibles a indentación, donde se usa la fuente exacta.
- `WorkflowEngine`: máquina de estados determinista con pasos agent/tool,
  aprobación, condiciones/paralelo/verificación como tipos validados,
  reparación acotada, gates estructurados, presupuestos y snapshots JSON
  reanudables. La ejecución real debe pasar por los runners existentes para
  conservar permisos y límites de concurrencia.

## Esquema de workflow v1

```json
{
  "schemaVersion": 1,
  "entry": "explore",
  "budget": { "maxIterations": 12, "maxSeconds": 1800 },
  "steps": {
    "explore": { "type": "agent", "next": "review" },
    "review": { "type": "approval", "accept": "execute", "reject": "stop" },
    "execute": { "type": "tool", "onSuccess": "verify", "onFailure": "stop" },
    "verify": { "type": "verify", "onSuccess": "finish", "onFailure": "execute" },
    "finish": { "type": "finish" }
  }
}
```

Tasks y Automations preservan un objeto `workflow` opcional. Los registros de
historial preservan `workflowState` y `metrics`. Los registros legacy siguen
siendo válidos.

El preset `autoprompt` usa `LC_GATE: PASS|FAIL|BLOCKED` como recibo de cada
fase crítica. Las ramas de revisión/verificación se lanzan read-only; sólo el
verificador puede habilitar shell para tests y siempre queda confinado al
workspace. Las reparaciones tienen un presupuesto explícito para impedir loops
infinitos.

## Loops locales

Las Tasks pueden repetir su cuerpo hasta cumplir un objetivo. Además del límite
de iteraciones (`loopMaxIterations`, hasta 1000), admiten un límite operativo de
tiempo (`loopMaxSeconds`, hasta 24 horas; `0` = sin límite). Este límite no es
económico: protege la disponibilidad de GPU/CPU y evita relanzar una iteración
cuando el loop ya agotó su ventana de ejecución. La decisión se evalúa antes de
cada relanzamiento y queda registrada en el motivo de finalización.

La vista visual es una proyección editable y sin pérdida: conserva campos avanzados,
ramas, argumentos, presupuestos y metadata que no representa gráficamente, y al
guardar modifica únicamente los campos visibles de cada nodo.

## Benchmark A/B

Comparar con el mismo modelo, quant, contexto, prompt, temperatura y hardware:

1. Prefijo estable apagado/encendido.
2. Fuente exacta/vista estructurada.
3. Caché fría y caliente por separado.
4. Cinco pasadas como mínimo y orden aleatorio.

Registrar tokens de prompt/generados, tiempos de prefill/generación/pared,
tool calls, bytes de tools, éxito, reparaciones, RAM y VRAM. Cuando la tarea
declara `acceptance.toolCalls`, registrar además la corrección de la secuencia
(éxito, fallos, incompletas, redundancias, precision/recall/F1 y llamadas
inesperadas). Sin esa expectativa la métrica de corrección queda explícitamente
como "sin dato", no como cero. Una variante pasa
el gate si reduce al menos 15% tokens o 10% tiempo sin reducir éxito ni producir
ediciones incorrectas.

El primer resultado exitoso de cada combinación suite/perfil/target queda marcado
automáticamente como baseline. Las corridas posteriores persisten su ID y los deltas
de tiempo y calidad, para evitar comparaciones manuales ambiguas.

## Límites de seguridad

- Un workflow no ejecuta shell directamente: solicita una tool al runner.
- Cambiar de fase no modifica permisos.
- Una proyección compacta nunca es destino de edición.
- Un hash/cambio de archivo debe invalidar cualquier proyección cacheada.
- Acciones destructivas conservan aprobación humana incluso dentro de jobs.

### Suite E2E fija

`assets/benchmarks/custom/agent_efficiency_e2e_v1.json` se distribuye con la app.
Sus IDs, prompts, límites y comandos de aceptación están versionados; una comparación
válida usa la misma revisión de la suite, modelo, perfil, hardware y cantidad de
pasadas. La suite recomienda tres pasadas: al seleccionarla la UI aplica ese valor,
que el usuario todavía puede modificar. Para barridos A/B de harness,
`tools/harness_ab.ps1` usa cinco pasadas por defecto, intercala aleatoriamente
los perfiles con una semilla persistible y aborta si el informe final queda
desbalanceado; menos de cinco queda reservado para smoke/A-B rapido. Cada
carpeta aislada mantiene además
`comparison.json`, actualizado después de cada muestra, con tasa de éxito,
estabilidad del resultado, rango y mediana de calidad, medianas de tiempo y
reparaciones, y deltas pareados entre los perfiles seleccionados. Las pasadas
fallidas cuentan para éxito/estabilidad pero no introducen ceros artificiales en
las medianas. El resultado individual persistido aporta calidad, tiempo y uso de
tools junto con `toolCallQuality`.

## Project Brain incremental

El manifiesto schema v2 persiste únicamente metadata y SHA-256. Un refresh completo
reutiliza el hash cuando tamaño y mtime coinciden. Después de una escritura del agente,
o al recibir `changed_paths`, actualiza sólo esos archivos/subárboles y reporta
`scanMode=events` y `changes.added/updated/removed/reused`. El cache es regenerable y
no guarda contenido fuente.

## Parser estructural opcional

`read_file(compact=true)` intenta `tree-sitter parse --xml` si encuentra el CLI (o
`LLAMACODE_TREE_SITTER`) y el archivo existe. La vista se construye con las hojas del
árbol y conserva el mapeo exacto al byte original. Sólo marca
`parserBackend=tree-sitter-ast` ante un árbol completo sin `ERROR`/`MISSING`; cualquier
ausencia, timeout, gramática no instalada o mapeo dudoso cae automáticamente al
validador lexical conservador.

## Scheduler persistente

Al habilitar Tasks programadas se registra el companion `--scheduler-daemon` en el
inicio de sesión del sistema operativo. Un heartbeat permite distinguir registro y
proceso activo; deshabilitar el scheduler elimina el registro. El daemon conserva el
lock de instancia e IPC existentes.
