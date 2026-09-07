# Plan — Cierre del harness modular

> **IMPLEMENTADO** (F0–F5). El editor se extrajo a `LcHarnessEditor` con su test
> QML, `harness_ab.ps1` tiene `tests/test_harness_ab.ps1`, las fases se verifican
> de punta a punta en `test_appcontroller`, las directivas se crean desde la app
> y F5 quedó decidido: `OpencodeBackend` NO consume el spec (se avisa en la UI) y
> los presets siguen en código. La referencia viva es `docs/harness.md`.

Estado de partida: el harness modular (`HarnessSpec`) está implementado y en
`main` (ver `docs/harness.md`). El gate está verde y la app compila. Lo que
falta **no es funcionalidad nueva**: es cobertura de lo que se entregó sin
verificar y un par de asperezas de uso.

Principio de este plan: *cada etapa entrega su test*. Lo que genuinamente no se
puede automatizar (ver algo con los ojos) va como checklist a la sección "QA
manual pendiente" de `CLAUDE.md`, no como intención.

Orden por riesgo, no por tamaño: primero lo que se entregó **sin ejecutar nunca**.

---

## Inventario de lo que falta

| # | Hueco | Riesgo |
|---|---|---|
| F0 | El editor del harness (QML) nunca se abrió. Compila y pasa qmllint; nada más. | **Alto** |
| F1 | `tools/harness_ab.ps1` nunca se ejecutó y no tiene test. | **Alto** |
| F2 | Las fases se verificaron puras (`forPhase`), no de punta a punta. | Medio |
| F3 | Directivas `.md`: se eligen, no se crean. Sección vacía sin explicación. | Medio |
| F4 | Asperezas: auto-herencia ofrecida, `hasMcpServers` hardcodeado. | Bajo |
| F5 | `OpencodeBackend` sin spec; presets en código y no en JSON. | Bajo (decisión) |

---

## F0 — Editor del harness: extraer y testear

**Problema.** El bloque nuevo vive dentro de `qml/pages/SettingsPage.qml`, que
ya es enorme y depende de `App`/`Theme` como context properties de C++. Ahí
adentro no se puede testear nada: por eso quedó sin una sola verificación.

**Trabajo.**
1. Extraer el bloque a `qml/components/LcHarnessEditor.qml`, con una API chica y
   explícita en vez de tocar `agentProfilesSection.*` por alcance léxico:
   - `property string profileId`, `property bool readOnly`
   - `property var spec` (el JSON editable), `property var summary`,
     `property var diff`, `property var packs`, `property var directives`
   - `signal specChanged(var spec)` / `signal saveRequested()`
   - Los helpers puros (`specSet`, `specValue`, `specToggleListItem`,
     `specHasListItem`) se mueven con el componente: son la lógica real y hoy
     están sueltos en la página.
2. `SettingsPage.qml` pasa a instanciarlo y cablear `App.profileManager`.
3. Test QML con el harness que ya existe (`tests/qml/tst_*.qml`, runtime `qml`
   en offscreen): `tests/qml/tst_harness_editor.qml` + stubs. Cubre:
   - toggle de pack: agrega/saca de `spec.tools.packs` sin tocar otros módulos;
   - editar un número de `loop` no borra `context` (regresión típica del
     "copiar el objeto entero");
   - `readOnly` (preset de sistema) no emite `specChanged`;
   - el diff se renderiza con N filas y el contador coincide;
   - import de JSON inválido no rompe el componente ni pisa el spec bueno.
4. Registrar en `CMakeLists.txt`: sumar los archivos a `LC_QML_HARNESS_SOURCES`
   y `add_test(NAME qml_harness_editor ...)` con el mismo
   `FAIL_REGULAR_EXPRESSION` que los otros.

**Hecho cuando** `ctest -R qml_harness_editor` pasa y `SettingsPage.qml` no
contiene lógica del harness, sólo el `LcHarnessEditor { ... }`.

**Lo que igual queda manual** (va a `CLAUDE.md`): que se *vea* bien. Ver la
sección "QA manual" al final.

---

## F1 — `harness_ab.ps1`: test y primera corrida real

**Problema.** Es el único artefacto entregado sin ejecutarse una vez. Los otros
scripts de infra PS del repo (`build_coord`, `session_guard`, `release`,
`bootstrap`) tienen su `tests/test_*.ps1`; éste no.

**Trabajo.**
1. `tests/test_harness_ab.ps1` (fuera de ctest, como los demás): levanta un
   **stub HTTP** con `HttpListener` que emula el ControlApi —
   `/health`, `/prop?name=benchmarkRunning`, `/invoke` — y verifica:
   - test 0: el `.ps1` es **ASCII puro** (la trampa de PS 5.1 documentada en
     `CLAUDE.md`; mismo test que abre `test_session_guard.ps1`);
   - falla temprano y con mensaje claro si no hay daemon (`/health` caído);
   - falla si un `agentProfileId` no existe, **antes** de correr nada;
   - corre una vez por perfil y espera a `benchmarkRunning=false` entre corridas;
   - `-CustomBenchmarkId` usa `startCustomBenchmark`, sin él usa `startBenchmark`;
   - escribe el JSON de salida y devuelve exit 0.
2. Correrlo **de verdad** una vez contra un daemon con un modelo chico y un
   benchmark de pocos ítems, y pegar el `comparison` resultante en
   `docs/benchmark-results-history.md` como primera muestra (aunque sea ruido:
   sirve para ver que el informe es legible).

**Hecho cuando** `powershell -File tests\test_harness_ab.ps1` pasa y hay una
corrida real registrada.

---

## F2 — Fases de punta a punta

**Problema.** `HarnessSpec::forPhase` está testeada como función pura. Que
`applyHarnessPhase` efectivamente cambie el backend en el momento correcto del
runner de Tasks, no.

**Trabajo.** Extender `tests/test_appcontroller.cpp`, que ya tiene el harness
(`FakeAgentBackend` + `setTestAgentBackend` + `runTaskBodyForTest`):
- un perfil con `phases.verify` que cambia `approvalMode` y apaga `run_shell`;
- correr el ciclo body → goal-check y verificar que el backend recibió el cambio
  en la fase de verificación y **volvió** al spec base en la siguiente body;
- que un perfil **sin** `phases` no genera ninguna llamada extra (no-op real, no
  no-op de palabra).

Requiere exponer en `FakeAgentBackend` lo que hoy no registra (`setDirectives`,
`setDisabledTools`, `setApprovalPolicy`, `setAgentTuning`): guardar la última
llamada de cada uno.

**Hecho cuando** el test falla si se borra la línea `applyHarnessPhase(...)` del
runner. Ese es el criterio, no que pase.

---

## F3 — Directivas propias: crear, no sólo elegir

**Problema.** El editor lista las directivas `.md` que ya existen en disco. No
hay forma de crear una desde la app y no hay ninguna de ejemplo → la sección se
ve vacía y el usuario no sabe qué se espera ahí.

**Trabajo.**
1. `HarnessDirectiveStore`: sumar `save(name, description, when, body, scope)` y
   `remove(name, scope)`, con las mismas validaciones que el parseo (slug
   kebab-case, `description` obligatoria, tope de bytes) y confinamiento a la
   raíz correcta (global vs proyecto). Puras y testeables.
2. `ProfileManager` (o `AppController`, según dónde caiga el workspace):
   `Q_INVOKABLE` de alta/baja/edición → headless por contrato.
3. UI en `LcHarnessEditor`: crear / editar / borrar, con preview del cuerpo y el
   campo `when` con los hechos disponibles listados (`tools.desktop`, `vision`,
   `project.hasGit`, …). Hoy el usuario tiene que adivinarlos.
4. Una directiva **bundleada de ejemplo** (`assets/harness/directives/`), con un
   caso real y chico — p.ej. `commit-conventions.md` con `when:` vacío. Sirve de
   plantilla y de smoke: si el catálogo la lista, el descubrimiento funciona.
5. Tests en `test_harness_modules.cpp`: alta→listado→carga→borrado, rechazo de
   slug inválido y de cuerpo sobredimensionado, y que una directiva de proyecto
   sigue pisando a la global después de guardar.

**Hecho cuando** se puede crear una directiva desde la app, aparece en el
catálogo, se inyecta en el prompt y sobrevive a reiniciar.

---

## F4 — Asperezas

Cada una es chica; juntas son la diferencia entre "configurable" y "usable".

1. **Auto-herencia**: el combo de `extends` lista el propio perfil. El ciclo se
   maneja (no cuelga), pero no debería ofrecerse. Filtrar el `profileId` actual
   y los descendientes. Test: un helper puro
   `ProfileManager::eligibleParents(id)` que excluye el propio id y su subárbol.
2. **`hasMcpServers` hardcodeado en `true`** en el preflight de dependencias:
   pasarle el conteo real de servers MCP habilitados (global + proyecto), que
   `AppController` ya sabe calcular para `ensureAgentBackend`.
3. **Warnings accionables**: hoy dicen qué falta, no qué hacer. Sumar la acción
   ("instalá git", "activá el server para embeddings", "configurá una cuenta en
   Integraciones"). Cambio en `HarnessTools::dependencyWarnings`, ya testeada.
4. **Costo de contexto**: mostrar también el tamaño del system prompt compuesto
   junto al de los schemas de tools. Hoy sólo se ven los tokens de tools, y el
   prompt es la otra mitad del presupuesto.

---

## F5 — Decisiones pendientes (no trabajo ciego)

Dos cosas quedaron fuera a propósito. Este plan las deja explícitas para
decidirlas, no para arrastrarlas:

- **`OpencodeBackend` no consume el spec.** Es el backend legacy, con su propio
  loop; hacerlo respetar `loop`/`context` sería reimplementar el harness ahí.
  Propuesta: **no hacerlo** y marcarlo en la UI ("los módulos del harness aplican
  al agente nativo"), para que la promesa del editor no mienta según el backend.
- **Presets de sistema en código.** Hoy `systemPresets()` los construye en C++.
  Pasarlos a JSON bundleado (como `assets/system_profiles.json`) los haría
  editables sin recompilar y agregables por el usuario avanzado. Cuesta un
  cargador + validación + fallback si el JSON está roto. Propuesta: hacerlo
  **sólo si** aparece la necesidad de un preset nuevo por fuera de una release.

---

## QA manual (lo que no se automatiza)

Va a la sección "QA manual pendiente" de `CLAUDE.md`. Necesita la app abierta:

- **Recorrido del editor**: Ajustes → Perfiles de agente → duplicar un preset →
  cambiar herencia, tocar dos packs, bajar `keepLastImages` a 0 → Guardar
  harness → cambiar de perfil y volver: los cambios siguen ahí y el diff muestra
  exactamente lo tocado.
- **Layout**: con la ventana angosta la grilla de 4 columnas no se rompe, los
  chips de packs hacen wrap, y el bloque de warnings no empuja el resto.
- **Aplicación en vivo**: con el agente corriendo, cambiar de perfil y confirmar
  en el log que el system prompt se rehizo (directivas nuevas) sin reiniciar.
- **Import/export**: exportar el spec, editarlo a mano, importarlo; un JSON roto
  deja el mensaje de error y **no** pisa el perfil.

---

## Orden y criterio de corte

```
F0 (editor: extraer + test)  →  F1 (A/B: test + corrida real)
        ↓
F2 (fases e2e)  →  F3 (directivas CRUD)  →  F4 (asperezas)
        ↓
F5 = decisión, no implementación
```

F0 y F1 son el núcleo: cubren lo entregado sin verificar. F2–F4 son mejoras
incrementales con test propio. Si hay que cortar por tiempo, se corta en F4 —
nunca antes de F1.

Gate en cada etapa: `tests.bat` verde (hoy 59 tests + los que sume este plan) y
la app compilando. El árbol quieto durante el build, o worktree propia: el gate
DIRTY ya costó una corrida entera en esta feature.
