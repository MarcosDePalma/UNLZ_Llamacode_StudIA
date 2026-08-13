# LlamaCode — guía para Claude

## Testing policy (OBLIGATORIA)

Toda feature nueva o cambio de comportamiento DEBE llegar con su test (unit +
integration cuando aplique). Sin test = incompleto.

Reglas:
- Agregar/actualizar el test en `tests/` y registrarlo en `CMakeLists.txt` con
  `add_lc_test(<area> tests/test_<area>.cpp)` (helper ya definido, sección
  `if (BUILD_TESTS)`).
- Antes de commitear: correr `tests.bat` (configura `BUILD_TESTS=ON`, compila en
  `build_tests/`, corre `ctest`). Build + 13 tests verdes = gate. No commitear en rojo.
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
| LlamaBinary, ModelRoot, BinaryRegistry, ModelRootRegistry | `tests/test_registries.cpp` |
| CatalogModel, ModelCatalog | `tests/test_catalog.cpp` |
| CapabilityDetector | `tests/test_capability.cpp` |
| DocumentExtractor | `tests/test_document_extractor.cpp` |
| MemoryStore, GraphStore | `tests/test_memory_graph.cpp` |
| AutoTuner / tuner | `tests/test_tuner.cpp` |
| EvalSuite | `tests/test_eval.cpp` |
| ControlApi | `tests/test_control_api.cpp` |
| AgentToolRunner (tools nativas) | `tests/test_agent_tools.cpp` |
| MasterCli | `tests/test_master_cli.cpp` |
| RawChatBackend (sesiones/persistencia) | `tests/test_backends_net.cpp` |
| StudiaIndex, StudiaPrompt, StudiaSessionStore, StudiaController | `tests/test_studia.cpp` |
| Ingestor de StudIA (Python) | `tools/studia/test_ingest.py` |
| Vectorizador de StudIA (Python) | `tools/studia/test_vectorizar.py` |
| Graficador de StudIA (Python) | `tools/studia/test_graficar.py` |

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
Los backends de red con stream SSE real (RawChatBackend/LlamaAgentBackend/
OpencodeBackend/McpClient sendMessage, tool-call extraction) necesitan un stub
HTTP de `/v1/chat/completions` y `/v1/embeddings`. Hoy se cubre el ciclo de
sesiones/persistencia sin red. Al tocar esos paths, agregar el stub.

## Build
- App: `build.bat [Debug|Release|Both]` (tiene `pause`; correr con `< nul` para no colgar).
- Tests: `tests.bat [Debug|Release]` (sin `pause`).
- La lógica core vive en la lib estática `llamacode_core`; el app y los tests linkean contra ella.

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
