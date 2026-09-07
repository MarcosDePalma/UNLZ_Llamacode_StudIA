# Computer Use nativo

LlamaCode controla el escritorio mediante un pipeline local y genérico:

```text
snapshot → resolver → stale guard → política → acción → assert → receipt
```

## Snapshot y referencias

`desktop_snapshot`, `desktop_controls` y `desktop_observe` comparten un
`snapshotId` determinista. El snapshot incluye alcance, ventana, PID, geometría,
DPI disponible, árbol UI Automation, `automationId`, rol, patrones soportados,
estado y fingerprint.

Las acciones semánticas pueden recibir `snapshot_id`. Si la ventana o el árbol
cambiaron, el backend rechaza la acción y obliga a observar y resolver de nuevo.
No se elige silenciosamente entre candidatos ambiguos.

## Acciones UIA

`desktop_control_action` soporta:

- `invoke`
- `toggle`
- `set_value`
- `select`
- `expand`
- `collapse`
- `range_set`
- `scroll_into_view`
- `read`

Cuando existe un patrón UIA, la operación no roba foco. El fallback de pointer
se reserva para `invoke` cuando el control no expone una operación semántica.

## Captura

En Windows, la captura de una ventana intenta primero un render offscreen del
HWND mediante `PrintWindow(PW_RENDERFULLCONTENT)`. Si la aplicación no soporta
ese render, se conserva el fallback de captura del monitor. El resultado indica
el proveedor usado; esto evita presentar una captura de pantalla como si fuera
siempre una captura independiente de la ventana.

Mouse, strokes y teclas siguen requiriendo una sesión interactiva y foco. El
modo semántico no promete mouse físico en background.

## Receipts y seguridad

Toda tool `desktop_*` produce un receipt estructurado con:

- estado `settled` o `failed`;
- tool y estrategia usada;
- hash de payload y resultado;
- snapshot asociado;
- sesión y correlación;
- target y detalle redactado.

Los tools de lectura conservan el tipo `read`. Las acciones físicas y semánticas
usan el tipo `desktop` para que los perfiles de aprobación puedan distinguirlas.
Campos de contraseña, tokens y secretos se redactan en evidencia.

Los artifacts Teach nuevos usan formato v3. Se conservan `precondition`,
`postcondition` y `repair` aunque el evento provenga de un Teach legacy. Los
artifacts v2 siguen siendo legibles.

## Presupuesto del prompt de Teach

Al inyectar una receta larga en el prompt del agente, las intenciones de cada paso
se resumen a 120 caracteres. Targets, ventanas, puntos de strokes y aserciones se
conservan como datos estructurados aparte. Así las recetas extensas no desplazan
las instrucciones operativas ni empujan el contexto fuera del límite del perfil.

## QA

Los tests unitarios cubren hashes, stale guard, receipts, redacción y clasificación
genérica. Los probes que interactúan con el escritorio siguen siendo opt-in para
no mover el mouse ni cambiar el foco durante `ctest`.

El diagnóstico y el uso de la superficie nativa se mantienen documentados en
[`docs/harness.md`](harness.md) y
[`docs/qa_foreground_automation.md`](qa_foreground_automation.md).
