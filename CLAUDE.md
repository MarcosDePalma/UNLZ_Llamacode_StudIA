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

El ingestor de StudIA es Python (usa pypdf / python-docx / python-pptx / openpyxl),
así que su suite es `unittest` y se engancha a `ctest` como `test_studia_ingest`.
Si no hay Python en el sistema, CMake omite ese test y el resto sigue corriendo.

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
| StudiaIndex, StudiaController (módulo StudIA) | `tests/test_studia.cpp` |
| Ingestor de StudIA (Python) | `tools/studia/test_ingest.py` |

### Pendiente de cobertura
Los backends de red con stream SSE real (RawChatBackend/LlamaAgentBackend/
OpencodeBackend/McpClient sendMessage, tool-call extraction) necesitan un stub
HTTP de `/v1/chat/completions` y `/v1/embeddings`. Hoy se cubre el ciclo de
sesiones/persistencia sin red. Al tocar esos paths, agregar el stub.

## Build
- App: `build.bat [Debug|Release|Both]` (tiene `pause`; correr con `< nul` para no colgar).
- Tests: `tests.bat [Debug|Release]` (sin `pause`).
- La lógica core vive en la lib estática `llamacode_core`; el app y los tests linkean contra ella.
