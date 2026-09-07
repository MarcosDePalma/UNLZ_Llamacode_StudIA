# Workflows de ingeniería

LlamaCode incluye presets declarativos sobre el motor de Tasks para ordenar el
trabajo del agente sin hardcodear una aplicación, una resolución o coordenadas.

Presets disponibles:

- `investigate`: inspección, hipótesis, aprobación y validación de causa raíz.
- `qa`: preflight, reproducción, corrección, prueba de regresión y verificación.
- `document-audit`: detección de documentación obsoleta, faltante o contradictoria.
- `review`: revisión funcional y revisión de alcance en paralelo.
- `autoprompt`: alcance, roadmap, implementación, revisión/verificación
  independiente, reparación acotada y gate final.
- `release-check`: revisión del estado, `tests.bat Debug`, `build.bat Debug NOPAUSE`
  y aprobación final.

Los presets se instalan desde Tasks como procesos normales. Cada ejecución usa
los snapshots, aprobaciones, permisos y reanudación del motor existente. El
workflow sólo describe intención y pasos; la resolución concreta queda a cargo
del agente y de las tools autorizadas por el workspace.

## Seguridad

Los perfiles disponibles son `normal`, `investigation`, `guarded` y `production`.
En particular, `release-check` nunca hace commit ni push automáticamente: deja
la aprobación final en la interfaz para que el usuario revise el resultado.

Los workflows no deben asumir nombres de aplicaciones, botones, colores,
layouts ni coordenadas. Para browser y escritorio deben usar las capacidades
semánticas, OCR, evidencia y templates de Teach ya existentes.

## Contrato Autoprompt

`autoprompt` adapta el loop planificar → construir → probar → revisar → reparar
sin depender de una skill externa. Cada fase marcada `verdictRequired` debe
comenzar su respuesta con exactamente una de estas líneas:

```text
LC_GATE: PASS
LC_GATE: FAIL
LC_GATE: BLOCKED
```

El runner no toma una respuesta sin gate como éxito. `FAIL` vuelve a `repair` y
`BLOCKED` detiene el workflow para que el usuario resuelva la dependencia. La
entrada a `repair` se cuenta en `repairAttempts` y el preset permite como máximo
tres reparaciones; el snapshot persiste el contador, el último veredicto y los
resultados para reanudar sin perder contexto.

La revisión y la verificación corren como ramas independientes read-only. El
revisor no recibe escritura, desktop, correo, MCP con efectos ni shell; el
verificador puede ejecutar tests dentro del workspace confinado, pero tampoco
recibe escritura directa ni acciones externas. El shell no es una garantía de
inmutabilidad: las instrucciones de la rama prohíben modificar desde comandos y
la evidencia debe hacer visible cualquier desviación.

## Evidencia y pruebas

Un workflow exitoso debe dejar un resumen de pasos, resultados, herramientas y
aprobaciones en el historial de la Task. `qa` debe agregar o justificar una
prueba de regresión, `autoprompt` debe conservar los gates y reparaciones, y
`release-check` debe verificar el ejecutable Debug según las instrucciones de
`AGENTS.md`.

## Validación headless

La cobertura de catálogo, persistencia, políticas de seguridad, AppController,
ControlApi y el contrato de la barra QML puede correr sin modelo ni escritorio:

```powershell
cmake --build build_tests --config Debug --target test_engineering_workflows test_task_security_policy
ctest --test-dir build_tests -C Debug -R "test_engineering_workflows|test_task_security_policy|test_tasks|test_appcontroller|test_control_api" --output-on-failure
powershell -ExecutionPolicy Bypass -File .\tests\headless_engineering_workflows.ps1
```

El panel QML se valida por compilación y por el contrato headless de
`AppController`; los loops con `FakeAgentBackend` tampoco requieren inferencia.
Desktop/OCR/audio/browser real e inferencia local son QA optativo, no parte del
gate headless.
