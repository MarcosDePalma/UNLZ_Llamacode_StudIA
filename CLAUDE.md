# LlamaCode — guía para Claude

## Testing policy (OBLIGATORIA)

Toda feature nueva o cambio de comportamiento DEBE llegar con su test (unit +
integration cuando aplique). Sin test = incompleto.

Reglas:
- Agregar/actualizar el test en `tests/` y registrarlo en `CMakeLists.txt` con
  `add_lc_test(<area> tests/test_<area>.cpp)` (helper ya definido, sección
  `if (BUILD_TESTS)`).
- Antes de commitear: correr `tests.bat` (configura `BUILD_TESTS=ON`, compila en
  `build_tests/`, corre `ctest`). Build + todos los tests verdes = gate (hoy 42;
  ver `add_lc_test` en `CMakeLists.txt`). No commitear en rojo. Atajo: `/gate`.
  Verde NO alcanza: si el coordinador imprime `[WARN] ... DIRTY`, los tests NO
  corrieron sobre tu fuente y el resultado no vale. Volvé a correrlo.
- Un executable por subsistema (QtTest = 1 `QTEST_MAIN` por binario).

Las herramientas de StudIA son Python (ingestor, vectorizador, graficador), así
que sus suites son `unittest` y se enganchan a `ctest` como `test_studia_ingest`,
`test_studia_vectorizar` y `test_studia_graficar`. Si no hay Python en el
sistema, CMake omite esos tests y el resto sigue corriendo; los que además
necesitan numpy/matplotlib se saltean solos con `skipUnless`.

### Convenciones de tests
- Aislamiento de disco: `QStandardPaths::setTestModeEnabled(true)` redirige
  AppData/AppLocalData a una ubicación de test (registries, catalog, chat_raw,
  cache de DocumentExtractor). Perfiles: env var `LLAMACODE_PROFILES_DIR`
  (setear en `initTestCase` ANTES de construir el primer `ProfileManager`: la
  raíz se cachea en un `static`).
- DB de catálogo persiste entre corridas: borrarla en `initTestCase` si el test
  necesita estado limpio.
- ControlApi / backends de red: server y client en el mismo hilo → NO usar
  `waitForReadyRead`; bombear el event loop (`QCoreApplication::processEvents`).
- AgentToolRunner: `executeTool` emite `toolExecuted`; capturar con `QSignalSpy`.
  `run_shell` es async (esperar el spy).
- Los test exes se linkean contra la lib `llamacode_core` (mismos objetos que el
  app) y se fuerzan a subsistema consola para que QtTest imprima a stdout.

### Mapa módulo → archivo de test
| Subsistema | Test |
|---|---|
| GGUFScanner, EffectiveProfileBuilder | `tests/test_gguf_profiles.cpp` |
| ProfileTypes, ProfileManager | `tests/test_profiles.cpp` |
| AgentProfile (perfiles de agente: capacidades+directivas, presets, gating system prompt) | `tests/test_agent_profiles.cpp` |
| HarnessSpec (harness modular: herencia por módulo, packs, permisos, fases, migración legacy) | `tests/test_harness_spec.cpp` |
| Módulos del harness cableados al backend + directivas propias (.md) | `tests/test_harness_modules.cpp` |
| LlamaBinary, ModelRoot, BinaryRegistry, ModelRootRegistry | `tests/test_registries.cpp` |
| CatalogModel, ModelCatalog | `tests/test_catalog.cpp` |
| CapabilityDetector | `tests/test_capability.cpp` |
| DocumentExtractor | `tests/test_document_extractor.cpp` |
| MemoryStore, GraphStore | `tests/test_memory_graph.cpp` |
| AutoTuner / tuner | `tests/test_tuner.cpp` |
| EvalSuite | `tests/test_eval.cpp` |
| ControlApi | `tests/test_control_api.cpp` |
| AgentToolRunner (tools nativas) | `tests/test_agent_tools.cpp` |
| LlamaAgentBackend (system prompt: discipline/test-net/contexto) | `tests/test_agent_wire.cpp` |
| HotspotAnalyzer (archivos riesgosos: churn+autores+sin test) | `tests/test_hotspots.cpp` |
| MasterCli | `tests/test_master_cli.cpp` |
| RawChatBackend (sesiones/persistencia) | `tests/test_backends_net.cpp` |
| LogTriage (barrido de errores) | `tests/test_logtriage.cpp` |
| DownloadHistoryStore (historial de descargas) | `tests/test_download_history.cpp` |
| FuzzyMatch (matching difuso de nombres de control) | `tests/test_fuzzy_match.cpp` |
| OcrTextLocator (ubicar texto OCR en pantalla) | `tests/test_ocr_locator.cpp` |
| VoiceCursorCommand (parseo de órdenes de cursor por voz) | `tests/test_voice.cpp` |
| ThemeProvider (temas custom, normalización de hex) | `tests/test_theme.cpp` |
| LcColorRow + LcColorPicker (editor de tema, lado QML) | `tests/qml/tst_color_picker.qml` |
| StudiaIndex, StudiaPrompt, StudiaSessionStore, StudiaController | `tests/test_studia.cpp` |
| Ingestor de StudIA (Python) | `tools/studia/test_ingest.py` |
| Vectorizador de StudIA (Python) | `tools/studia/test_vectorizar.py` |
| Graficador de StudIA (Python) | `tools/studia/test_graficar.py` |

Los componentes QML sin C++ se testean con el runtime `qml` en offscreen
(`add_test(qml_color_picker ...)`): los archivos reales se copian a
`build_tests/qml_harness/` junto a stubs de `Theme`/`App` (que en el app son
context properties de C++). Un archivo QML resuelve los nombres sin calificar en
SU directorio, por eso conviven copiados en vez de importarse.

### Prompts: verificar contra el modelo, no contra el texto
Los tests de C++ comprueban que la instrucción esté en el prompt; no dicen nada
sobre si el modelo la cumple. Para eso está `tools/studia/probar_modos.py`, que
manda los prompts reales al servidor y revisa el formato que vuelve. No se
engancha a `ctest` porque necesita un llama-server vivo.

    tests.bat Release                 # vuelca los prompts a %TEMP%
    python tools/studia/probar_modos.py "Redes" "modelo OSI capas" "el modelo OSI"

Al tocar los modos con formato estricto (autoevaluación, flashcards, plan),
correrlo sobre 3 o 4 materias distintas antes de dar el cambio por bueno. Dos
cosas se descubrieron así y conviene no volver a intentarlas: el formato puesto
sólo en el prompt de sistema se ignora —va al final del mensaje de usuario— y
pedir una estructura global ("primero las 10 preguntas, después las 10
respuestas") no la sostiene; los pares `P:`/`R:` sí.

### Pendiente de cobertura
Stub HTTP ya disponible: `SseStubServer` en `tests/test_backends_net.cpp` cubre
el stream SSE de `/v1/chat/completions` para RawChatBackend (acumulación de deltas
+ error HTTP). El ciclo del bucle de Loops (sin swap) se cubre en
`tests/test_appcontroller.cpp` con `FakeAgentBackend` + `setTestAgentBackend` +
`runTaskBodyForTest` (body→goal-check→repeat→GOAL_MET / corte por maxIter). El
ensamblado de tool_calls en streaming se cubre en `tests/test_agent_wire.cpp`
(`LlamaAgentBackend::mergeToolCallDelta`, pura). Falta: `/props` (chat-template),
`/v1/embeddings`, y el swap de modelo verify-phase end-to-end (el swap recrea el
backend vía `ensureAgentBackend`, así que no es stubbeable sin un harness cloud
completo; queda como QA manual). Reusar `SseStubServer`.

### Chat-template de Gemma4 (`chat-templates/gemma4-tools-fixed.jinja`)
Es el `chat_template.jinja` **oficial de Google** (no un template propio): al
actualizarlo, copiarlo tal cual de `google/gemma-4-31B-it` en HF. Está duplicado
(bundle qrc en `assets/` + copia del repo root que usan los perfiles de usuario
vía `--chat-template-file`); `bundle_gemma4TemplateKeepsLlamaCppMarkers`
(`tests/test_system_profiles.cpp`) exige que no diverjan y que sobrevivan los dos
marcadores por los que **llama.cpp clasifica el template leyendo su texto**:
`'<|tool_call>call:'` (si falta, no toma el path nativo `peg-gemma4`) y el
comentario `OpenAI Chat Completions:` (si falta, lo trata como outdated y le
aplica workarounds). Perderlos degrada el tool-calling en silencio: el server
levanta y responde 200 igual.

Dos cosas que parecen bugs del template y NO lo son (medidas contra server real,
llama.cpp mainline y beellama v0.3.2):
- **`arguments` como string** (spec OpenAI): el template hace `raise_exception`
  si recibe un string, lo que invita a mandar un objeto. Sería un error: los
  perfiles cloud exigen string, y llama.cpp ya lo deserializa antes de renderizar
  (`func_args_not_string`). Lo fija `parsesTextToolCallFallback`.
- **La rama `image`/`image_url` del template es código muerto** bajo llama.cpp:
  convierte las partes de imagen en un `media_marker` propio (`<__media_XXX__>`)
  ANTES del jinja (`common/chat.cpp`). No "arreglarla": no se ejecuta. Sin
  `--mmproj` el server corta con 500 antes de llegar.

### QA manual pendiente (sin test automatizado)
Necesitan una sesión de escritorio interactiva con ventanas vivas (no
reproducible en headless/CI), así que sólo se cubre el path de error en
`tests/test_agent_tools.cpp`. La lógica real es **QA manual**:
- **Abrir la app después de tocar QML** (no alcanza con que compile): una página
  en `qml/pages/` que usa componentes `Lc*` necesita `import LlamaCode 1.0` —
  dentro del qrc la resolución por directorio NO cruza carpetas. Sin ese import
  el módulo entero falla al cargar (`LcButton is not a type`) y la app no abre
  ninguna ventana, con el build en verde. Pasó con `DataLabPage.qml`.
- **Editor del harness modular** (Ajustes → Perfiles de agente): la lógica está
  cubierta por `qml_harness_editor`; lo que falta es **verlo**. Duplicar un preset
  → cambiar herencia, tocar dos packs, bajar "Capturas a conservar" a 0 → Guardar
  harness → cambiar de perfil y volver: los cambios siguen y el diff muestra
  exactamente lo tocado. Con la ventana angosta la grilla de 4 columnas no se
  rompe y los chips hacen wrap. Con el agente corriendo, cambiar de perfil rehace
  el system prompt sin reiniciar (mirar el log). Exportar el spec, romperlo a mano
  e importarlo: deja el mensaje de error y NO pisa el perfil.
- **UI Automation** (`DesktopAutomationBackend::controls` / `clickElement`, tools
  `desktop_controls` / `desktop_click_element`): abrir Notepad → `desktop_windows`
  para el id de ventana → `desktop_controls` lista los controles → tomar el
  `controlId` de un botón (ej. menú "Archivo"/"Guardar") → `desktop_click_element`
  lo invoca. Verificar que el clic semántico (patrón Invoke) acciona el control.
- **desktop_observe → visión**: con un perfil con `--mmproj`, confirmar que la
  captura se inyecta como `image_url` y el modelo la describe (el agente VE lo que
  observa). Sin mmproj no se inyecta (gateado por `setVisionAvailable`).
- **Browser teach persistente**: grabar un skill con login (`--user-data-dir` en
  `browser_skills/profiles/<slug>`), cerrar, reproducir → la sesión sigue logueada
  (no re-pide credenciales).
- **Recetas de Teach viejas en monitor escalado**: las grabadas ANTES de `ef70ea3`
  en un monitor a 125/150% quedaron con datos malos horneados (ancla UIA equivocada
  en scope=screen; coords normalizadas mal en scope=window, que mezclaba el bounds
  físico de GetWindowRect con un cursor lógico). El fix de coords no las repara:
  **regrabarlas**. Las grabadas a 100% están bien. Toda coordenada del backend es
  FISICA — ver el comentario de `physicalRectForOrigin` en
  `DesktopAutomationBackend.cpp`.
- **OCR / coordenadas** — hay harness, no hace falta ojo humano:
  `build_tests\Release\qa_ocr_probe.exe [texto]` (se compila con `tests.bat` pero
  NO está en ctest: necesita escritorio vivo + paquete de idioma OCR). Cruza
  `readText()` contra UIA: si el OCR ubica "Archivo" en (x,y) y
  `controlAtPoint(x,y)` dice "Archivo", dos fuentes independientes coinciden.
  Imprime el acuerdo por pantalla y sale 0 si todas dan >=70%.
  **Correrlo con una app con menús abierta en un monitor ESCALADO (125/150%)**: a
  100% lógico y físico coinciden y taparían cualquier error de espacio de coords.
  Así se cazó el bug de `targetBounds` (acuerdo 10% en el monitor al 150%, hoy 97%).
- **Cursor por voz** (`cursorOcr` en Charla, off por defecto): activarlo en
  Charla → decir "clic en Guardar" mueve/clickea; decir "no sé si hacer clic en
  Guardar" NO actúa y va al LLM. Con dos textos iguales en pantalla debe negarse
  por ambigüedad en vez de adivinar.

## Build
- **Política actual (desde 2026-06-18): build Release + tests, sin Debug.**
  Compilar solo Release (no Debug) y correr `tests.bat` + el gate de ctest antes
  de commitear. La sección "Testing policy (OBLIGATORIA)" de arriba está vigente.
- App: `build.bat [Debug|Release|Both]` (tiene `pause`; correr con `< nul` para no colgar).
- Tests: `tests.bat [Debug|Release]` (sin `pause`).
- La lógica core vive en la lib estática `llamacode_core`; el app y los tests linkean contra ella.

### Sesiones paralelas: worktree por tarea (preferido)
Si vas a hacer **más de una mejora a la vez**, no compartas el working tree:
```
powershell -File worktree.ps1 -Action new    -Name <tarea>   # ../LlamaCode-<tarea>, rama session/<tarea>
powershell -File worktree.ps1 -Action list
powershell -File worktree.ps1 -Action remove -Name <tarea> [-Force]
```
Cada worktree tiene su propio `build/`, `build_tests/` y `.buildlock/` (gitignored
root-anchored) → compilan en paralelo sin pisarse, y `git add` nunca mezcla hunks
de otra tarea. Comparte el object store: crearlo son segundos. Costo: el primer
build del worktree es full. `remove` sin `-Force` frena si hay cambios sin commitear.

**Por qué importa:** `build_coord.ps1` serializa *quién compila*, NO *qué fuente hay
en disco*. Con un tree compartido, la otra sesión edita `src/` mientras vos compilás
→ "error de compilación que no es mío", hunks ajenos en `CMakeLists.txt`, gate verde
que no corrió sobre tu fuente. Ningún lock lo arregla: la edición pasa fuera del lock.

### Hooks de convivencia (`tools/session_guard.ps1`)
`worktree.ps1` y `build_coord.ps1` son opt-in — hay que *acordarse*. Los hooks los
corre el harness solo, así que son el único punto donde esto se aplica sin
disciplina. Enganchados en `.claude/settings.json` (checkeado al repo):
- **SessionStart** → si el tree ya tiene trabajo sin commitear, lo lista y sugiere
  el worktree. Informativo.
- **PreToolUse(Edit|Write|apply_patch)** → claim por archivo en
  `.buildlock/claims/`. Si otra sesión viva (claim < 90 min) tocó ese archivo,
  **avisa** — no bloquea: un claim stale no debe trabar trabajo legítimo, y un
  choque de edición se arregla con un merge. Un patch multiarchivo reclama todos
  sus encabezados `Update/Add/Delete File`.
- **PreToolUse(Bash|PowerShell)** → **BLOQUEA** git de alcance global
  (`checkout/restore .`, `reset --hard`, `clean -fd`, `stash`, `add -A`/`.`) si hay
  trabajo sin commitear. Es el único caso irreversible. La forma **por path**
  (`git checkout -- src/foo.cpp`, `git add <path>`) pasa siempre: es la salida
  recomendada, no puede caer en el guard.

Ver quién está tocando qué **ahora** (mismo registry, para humanos):
```
powershell -File tools\session_guard.ps1 -Mode status
```
Lista sesiones activas con sus archivos, el estado de las lanes, y lo único
accionable: los **solapamientos** (mismo archivo en dos sesiones vivas). Exit 1 si
hay solapamientos, 0 si no.

Test: `powershell -File tests\test_session_guard.ps1` (fuera de ctest, infra PS).

**Regla: los scripts de infra PS son ASCII puro** (`build_coord.ps1`,
`worktree.ps1`, `tools/session_guard.ps1`). Windows PowerShell 5.1 lee un `.ps1`
sin BOM como ANSI; un carácter UTF-8 dentro de un **string** se decodifica mal y
puede terminarlo (el em-dash `—` = `E2 80 94` → en CP1252 el `0x94` es `”`, que PS
toma como comilla de cierre → ParserError y el hook muere entero). En comentarios
sólo se ve feo; en strings rompe. El test 0 de `test_session_guard.ps1` lo fija.

### Encolamiento inteligente de builds (sesiones paralelas)
Varias IAs/CI pueden correr `build.bat` / `build_auto.bat` / `tests.bat` a la vez.
`build_coord.ps1` serializa por **lane** (`build` y `tests` tienen locks separados,
corren en paralelo entre sí) con un lock atómico (`.buildlock/`, gitignored):
- Si NO hay build en curso → tomás el lock (OWNER), bumpeás versión y compilás.
- Si ya hay uno en curso con la **misma fuente** (fingerprint = hash de
  `src/ qml/ tests/ CMakeLists.txt` con los triples semver neutralizados, para
  que el auto-bump no cuente) → esperás y **adoptás su resultado** (REUSE, no
  recompilás). Si ese build compartido falló, reintentás propio.
- Si es **otra fuente** → esperás tu turno (QUEUE).
- Locks muertos (guardian PID caído) o vencidos (`StaleSec`) se roban solos.
- **Tree compartido en movimiento** (defensa, no cura — la cura es el worktree):
  `acquire` re-fingerprintea tras `-MutationCheckMs` (1200ms, 0 = off) y avisa si
  la fuente está cambiando *antes* de gastar minutos de MSBuild; `release`
  revalida el fingerprint y, si cambió durante el build, publica **DIRTY** en vez
  de OK (exit 12) → nadie adopta por REUSE un artefacto de otra fuente, y los
  `.bat` gritan que el binario/gate no es confiable.
- El "owner" real es un proceso *guardian* oculto (su vida == el lock); `release`
  lo mata. Así el PID sí prueba vida (el powershell que hace `acquire` muere ya).
- Test: `powershell -File tests\test_build_coord.ps1` (fuera de ctest; infra PS/bat).

### Releases y detector de updates
`AppController::checkForUpdates()` pide `/releases/latest` a la API de GitHub y
compara `tag_name` (sin la `v`) contra la versión propia. **Sin releases
publicados el endpoint da 404** y el app cae al `assets/update/latest.json`
bundleado, que trae `newVersion:false` → nunca avisa nada. El detector no está
roto: falta publicar.

Publicar (el tag DEBE ser `vX.Y.Z` o `QVersionNumber` no lo parsea):
```
release.bat                                  # dry run: muestra el plan
release.bat -Publish                         # taggea vX.Y.Z, pushea y crea el release
powershell -File tools\release.ps1 -Version 0.2.0 -Publish
```
Ojo: **en la máquina de desarrollo el aviso casi nunca aparece**, porque cada
build corre `bump-patch.bat` y deja la versión local por encima del último tag.
Para probar el popup hace falta un binario con versión menor a la publicada.

**"Actualizar ahora"** lanza `scripts/bootstrap.ps1` en una consola aparte
(`-NoExit`: el update tarda minutos y si falla el error tiene que quedar a la
vista). El app le pasa `LC_DIR` con **la instalación que está corriendo**
(`installRootForExePath`: sube desde el exe hasta el checkout con
`CMakeLists.txt` + `scripts/bootstrap.ps1`). Sin eso el bootstrap clona en
`%USERPROFILE%\LlamaCode`, mata la app y actualiza *otra* copia — el síntoma era
"se cierra y no actualiza".

El bootstrap hace `git reset --hard origin/main` sobre `LC_DIR`, así que **aborta
si el destino tiene cambios sin commitear** (forzar con `LC_FORCE=1`), y recién
cierra la app justo antes del build: si algo falla antes, el usuario se queda con
su versión vieja andando.

Tests: `powershell -File tests\test_release_script.ps1` y
`powershell -File tests\test_bootstrap_script.ps1` (fuera de ctest, infra PS).
En ctest: `githubReleaseToUpdateFlag` e `installRootForExePath`
(`tests/test_appcontroller.cpp`).
## Entrega (instaladores)
`installer\compilar.bat` arma con Inno Setup lo que se le manda a otra persona.
Sale a `dist\` (ignorado por git).

- **App** (`StudIA.iss`) — un solo `.exe` de 1,21 GB: app + `studia.db` + bge-m3.
  Instala sin pedir administrador (`PrivilegesRequired=lowest`) y deja en
  `HKA\Software\StudIA\InstallDir` dónde quedó.
- **Documentos** (`copiar_documentos.ps1`) — los 7,7 GB del corpus, aparte y en
  pendrive. No es un instalador y no puede serlo, por dos límites de Windows:
  un `.exe` no pasa de 4,2 GB, y las rutas del corpus superan los 260
  caracteres (máximo relativo: 261, o sea que no entran ni en la raíz del
  disco). Usa `robocopy`, que sí las maneja, y por eso el destino por defecto
  es corto (`C:\StudIA_Docs`): metido dentro de la carpeta de instalación se
  perderían 76 de 2937 archivos; ahí afuera, 2.

Tres cosas que hay que mantener y no son obvias:
- `compilar.ps1` copia el runtime de MSVC al lado del ejecutable. Sin eso la app
  no abre en una PC sin Visual Studio. `llama-server.exe` vive en otra carpeta y
  no las alcanza: de ese se ocupa `instalar_dependencias.ps1` vía winget.
- `compilar.ps1` también copia los scripts del repo a `build\Release\StudIA`
  antes de empaquetar, para no publicar la versión vieja de uno que se tocó
  después del último build.
- El corpus **no** hace falta para responder: el texto está dentro de
  `studia.db`. Sólo habilita abrir el PDF desde una cita. `carpetaCorpus()`
  busca en cascada: la carpeta empaquetada, la que dejó el copiador en
  `studia/carpetaDocumentos`, `C:\StudIA_Docs`, y por último la raíz con la que
  se indexó.

`installer\PROBAR_EN_PC_LIMPIA.md` documenta cómo verificarlo de verdad. Lo que
no se puede probar en la máquina de desarrollo son las ramas de *instalación*
del script de dependencias: acá ya está todo instalado.
