# Validación de visión auxiliar local — 2026-08-29

## Alcance y configuración

- Fecha de campaña: 2026-08-29 (turno disparado por la automatización de validación).
- Proyecto: `C:\Users\cristian\Documents\LlamaCode`.
- Configuración: Debug candidato; ejecutable validado en `build\Debug\LlamaCode.exe`.
- Toolchain utilizada por el proyecto: Qt `6.8.3\msvc2022_64`.
- Hardware detectado: 2 × NVIDIA GeForce RTX 3090; `hardwareFingerprint=hw-3051c56204610e6e`.
- Las pruebas headless fijaron `QT_QPA_PLATFORM=offscreen`, `QT_PLUGIN_PATH` al Qt del proyecto y un puerto de ControlApi aislado por ejecución.
- No se ejecutaron modelos reales ni benchmarks largos.

## Estado previo y coordinación

- Se leyeron `AGENTS.md` y `README.md` de la raíz antes de validar.
- `.llamacode\active_work.json`: ausente (`none`).
- `build_coord.ps1`: libre antes de iniciar cada cola; los locks temporales fueron tomados y liberados correctamente.
- No se detectaron otros procesos de Codex trabajando sobre este repositorio.
- Se preservaron los cambios ajenos ya presentes en el working tree; no se modificó ninguno de ellos.

## Ejecuciones

| Comando | Resultado | Tiempo / observaciones |
|---|---|---|
| `cmd /c tests.bat Debug` | PASS | CTest: 73/73, `Total Test time (real) = 95.56 sec`; suites Python: 6/6 y 8/8 (`0.519 s` y `0.011 s`); salida final `=== All tests passed ===`. Incluyó agent, Tasks, perfiles, OCR locator, VisualMatcher, QML, automatización y guards de probes OCR. |
| `cmd /c build.bat Debug NOPAUSE` | PASS | Generó/actualizó `build\Debug\LlamaCode.exe`. Advertencias no bloqueantes: no se encontraron `dxcompiler.dll`/`dxil.dll` y `VCINSTALLDIR` no estaba definido. |
| `tests\headless_smoke.ps1 -Exe build\Debug\LlamaCode.exe -Port 18877` | PASS | Smoke de hardware/topología, matriz de rendimiento, ControlApi, TaskStore CRUD, persistencia y validación headless; sin modelo. |
| `tests\headless_smoke.ps1 -Exe build\Debug\LlamaCode.exe -Port 18878 -TestRestartPersistence` | PASS | Reinicio aislado del daemon y persistencia de Task verificadas. |
| `tests\headless_smoke.ps1 -Exe build\Debug\LlamaCode.exe -Port 18880 -TestSchedulerDaemon` | PASS | Heartbeat del scheduler daemon verificado. |
| `tests\headless_smoke.ps1 -Exe build\Debug\LlamaCode.exe -Port 18882 -TestRestartPersistence -TestSchedulerDaemon` | PASS | Combinación de reinicio y scheduler verificada en un directorio temporal aislado. |
| `tests\headless_harness_smoke.ps1 -Exe build\Debug\LlamaCode.exe -Port 18879` | PASS | 16 checks: health, packs, facts, agent profile, resumen, CRUD de directivas, aislamiento AppData y comparación de benchmarks. |
| `tests\headless_engineering_gate.ps1` | PASS | Workflows 6, task security 6, Tasks 22, AppController 108 y ControlApi 24 casos; smoke HTTP de catálogo/validación/instalación/persistencia OK. Hubo un `QWARN` benigno por intento de escuchar en `0.0.0.0:8088`; el caso pasó. |
| `tools\harness_sdk_smoke.py` | PASS | `NODE_SDK_OK` y `PYTHON_SDK_OK`. |
| `tests\dual_gpu_voice_smoke.ps1 -RequireRtx3090 -Exe build\Debug\LlamaCode.exe -Port 18897` | PASS | 2 GPUs físicas; voz en GPU 0 (RTX 3090), split `1,1.109`; memoria física libre reportada: `23.171 MiB`. |
| `tests\headless_persona_styles.ps1 -Exe build\Debug\LlamaCode.exe -Port 18896` | PASS | CRUD de estilos/personas, análisis JSON, asociación, ranking, preview e import/export. |

## Incidencia observada y repetición

Una primera ejecución de `headless_smoke.ps1` con `-TestRestartPersistence -TestSchedulerDaemon` informó `El daemon no volvió después del reinicio` y terminó con código 1. No se modificó código por ese único resultado. Al repetir las pruebas en puertos y directorios temporales aislados:

- reinicio solo: PASS;
- scheduler solo: PASS;
- combinación reinicio + scheduler: PASS.

Se conserva el primer fallo como resultado intermitente de la campaña; no queda reproducido en la repetición aislada y no se abrió una corrección especulativa.

## Omisiones deliberadas

- `tests\qa_visual_automation` y probes de pantalla real: omitidos porque pueden abrir ventanas, robar foco o mover mouse/teclado.
- `qa_ocr_probe --self-contained` sobre pantalla real: omitido por la misma condición; quedaron cubiertos `test_ocr_locator`, `qa_ocr_probe_help` y `qa_ocr_probe_offscreen_guard` dentro de `tests.bat Debug`.
- `qa_auxiliary_concurrency`, `qa_kv_cache` y probes de proveedores web: omitidos porque requieren servidor/modelo real o endpoints externos.
- `headless_smoke.ps1 -RunModelLoop`: omitido porque requiere modelo real.
- No se ejecutaron benchmarks extensos.

## Artefacto final y limpieza

Después del build se verificó:

`C:\Users\cristian\Documents\LlamaCode\build\Debug\LlamaCode.exe` — existente, tamaño observado `26,113,024` bytes.

Los daemons y probes propios fueron detenidos por sus bloques de limpieza; los directorios temporales de las ejecuciones fueron eliminados. No se dejó ningún proceso, servidor ni probe propio activo al finalizar la campaña.

## Revalidación posterior con PC libre — 2026-08-29 03:27–03:39

- Antes de actuar se volvieron a leer `AGENTS.md` y `README.md`; `.llamacode\\active_work.json` quedó en `none`.
- `build_coord.ps1 -Lane build/tests -Action status`: ambas lanes `FREE` antes y después de la campaña.
- La única tarea de Codex activa era esta validación; las demás tareas del repo estaban `idle` o `notLoaded`. RAM usada: `21,7%` (101.979 MB libres de 130.214 MB); las dos RTX 3090 estaban prácticamente ociosas (`0%` y `1%`, 1.064/839 MiB usados).
- Working tree: se preservaron sin tocar las modificaciones ajenas preexistentes (`.gitignore`, `CMakeLists.txt`, `README.md`, `assets/update/latest.json`, documentación, perfiles, `src/AppController.h`, `src/main.cpp`, `tests/test_tuner.cpp`, `artifacts/`, `build_tests_aux/`, `build_tests_final/` y `tools/run_qwen38_flash_next_campaign.ps1`).

| Comando | Resultado | Tiempo / observaciones |
|---|---|---|
| `cmd /c tests.bat Debug` | PASS | CTest: 73/73, `Total Test time (real) = 101.84 sec`; suites Python: 6/6 y 8/8. Incluyó agent, Tasks, perfiles, OCR locator, VisualMatcher, QML, automatización y guards de probes OCR. |
| `cmd /c build.bat Debug NOPAUSE` | PASS | Compilación incremental completa; generó `build\\Debug\\LlamaCode.exe`, actualizó el acceso directo Debug y liberó la lane. Persistieron advertencias conocidas de `dxcompiler.dll`/`dxil.dll` y `VCINSTALLDIR`; además aparecieron mensajes no fatales de cmd (`"M"` y `"EM"` no se reconocen) antes del deploy, con exit code 0. |
| `tests\\headless_smoke.ps1` | PASS | Hardware/topología, matriz de rendimiento, ControlApi, TaskStore CRUD, persistencia y validación headless. |
| `tests\\headless_smoke.ps1 -TestRestartPersistence` | PASS | Reinicio aislado y persistencia verificados. |
| `tests\\headless_smoke.ps1 -TestSchedulerDaemon` | PASS | Scheduler daemon verificado. |
| `tests\\headless_harness_smoke.ps1` | PASS | Health, catálogo/directivas, perfiles de agente, resumen, CRUD y aislamiento AppData. |
| `tests\\headless_engineering_gate.ps1` | PASS | Workflows 6, task security 6, Tasks 22, AppController 108 y ControlApi 24; smoke HTTP OK. Un `QWARN` benigno por `0.0.0.0:8088` no impidió el PASS. |
| `tests\\headless_persona_styles.ps1` | PASS | CRUD, análisis JSON, asociación, ranking, preview e import/export. |
| `tools\\harness_sdk_smoke.py` | PASS | `NODE_SDK_OK` y `PYTHON_SDK_OK`. |
| `tests\\dual_gpu_voice_smoke.ps1 -RequireRtx3090` | PASS | 2 RTX 3090; voz en GPU 0; split `1,1.107`; memoria física libre reportada: `23.259 MiB`. |

No se ejecutaron modelos reales, benchmarks largos ni probes interactivos que pudieran robar foco o mover mouse/teclado. No se detectó un bug propio reproducible que justificara modificar C++/QML/core. La revalidación dejó libres las lanes y no dejó `LlamaCode`, `llama-server`, `ctest`, `cmake`, `MSBuild`, servidores ni probes propios activos. No se actualizó `README.md` porque no cambió ninguna capacidad ni recomendación.
