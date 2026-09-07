# Validación local — 2026-08-27

## Alcance

Se comprobó la disponibilidad de la PC antes de ejecutar. En el primer
chequeo no había una compilación, suite de tests, `LlamaCode.exe` ni
`llama-server` activos. Había aproximadamente 93,3 GB de RAM libre y dos
RTX 3090 con baja utilización; se dejaron intactos los procesos interactivos
ajenos al proyecto.

La corrida usó el candidato Debug del proyecto y datos de test cuando el
runbook lo permitía. No se modificaron perfiles de producción ni se tomó el
control de aplicaciones del usuario.

## Resultados

| Área | Ejecución | Resultado |
| --- | --- | --- |
| Build | `build.bat Debug NOPAUSE` | OK; se verificó `build/Debug/LlamaCode.exe` (26.113.024 bytes) y el acceso directo Debug con `assets/debug_icon.ico`. |
| Suite oficial | `tests.bat Debug` | OK; reutilizó el fingerprint `DB20E0A28A6564A0EA43F35D1DFFD91E`. |
| CTest independiente | rangos 1–35 y 36–73 con `--output-on-failure` | 73/73 tests OK; el segundo rango tardó 100,23 s. |
| Python | `test_harness_matrix` y `test_kv_cache_ab` | 6/6 y 8/8 OK. |
| Headless | restart/persistencia + scheduler, harness, persona/styles y engineering gate | OK en las cuatro corridas. |
| Automatización visual | matriz sin click y matriz con `--execute-click` | 12/12 casos OK en ambos modos: 3 monitores, claro/oscuro y DPI 100/125 %. |
| OCR | `qa_ocr_probe --self-contained Archivo` | OK; 3 pantallas, OCR `es-MX`, acuerdo OCR/UIA del 100 % en todas. |

Los smokes headless se ejecutaron con `LLAMACODE_TEST_MODE=1`; los daemons
temporales fueron detenidos al terminar. La matriz visual y el probe OCR
destruyeron sus propias superficies de prueba.

## Pruebas opt-in no ejecutadas

- `qa_kv_cache` y `qa_auxiliary_concurrency`: requieren enviar carga real a un
  servidor/modelo elegido. El chequeo posterior detectó un `llama-server`
  preexistente, PID 34264, de `PeritoSoft`, en `127.0.0.1:8033` con Qwen3.5-9B;
  no se lo interrumpió ni se le enviaron prompts de carga.
- Loop headless con inferencia real: no se lanzó otro servidor para evitar
  competir con el proceso anterior y no se alteró el perfil de producción.
- `qa_web_providers`: no había un proveedor Camofox/Playwright configurado y
  la prueba implicaría red externa; sólo se verificó su contrato de uso.

Las comprobaciones de CLI dieron `--help` correcto para los probes visual y
auxiliar. `qa_kv_cache --help` imprime la ayuda pero devuelve código 2 si se
omite el `base-url` obligatorio; es una observación de interfaz, no un fallo
de la suite. `qa_web_providers` sin argumentos devuelve código 2 por uso
incompleto, como corresponde.

## Observaciones

El build mostró avisos de que no encontró `dxcompiler.dll`/`dxil.dll` y que
`VCINSTALLDIR` no estaba definido; aun así completó correctamente y publicó
el ejecutable Debug. También se observó un mensaje ruidoso del script sobre
`M`/`EM`, sin impacto en el código de salida ni en los artefactos verificados.

No se actualiza `README.md`: esta anotación registra una corrida puntual y no
altera comportamiento, build ni arquitectura.
