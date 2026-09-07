# QA de automatización foreground

Estos probes son opt-in porque necesitan una sesión de escritorio interactiva y no
deben mover el mouse ni cubrir pantallas durante `ctest`.

## Matriz visual

`build_tests/Release/qa_visual_automation.exe --matrix` recorre todos los monitores,
dos objetivos y temas claro/oscuro. Agregar `--execute-click` valida también el clic
real sobre las ventanas propias del probe.

## OCR autocontenido

`build_tests/Release/qa_ocr_probe.exe --self-contained` crea temporalmente controles
Qt accesibles a pantalla completa en cada monitor. Cruza cada palabra reconocida por
Windows OCR con el control reportado por UI Automation en el mismo punto físico. Sale
con código 0 si el acuerdo mínimo es al menos 70%, 1 ante discrepancias y 2 cuando no
hay motor OCR o muestra suficiente.

El modo autocontenido evita depender de aplicaciones, idioma de interfaz, ventanas
preexistentes o preparación manual. Las superficies se destruyen al terminar.

## Registro de disponibilidad y corrida compartida

### 2026-08-27 — no se inicia una segunda corrida

La comprobación de disponibilidad detectó actividad interactiva reciente y una
corrida existente de `tests.bat Debug` (PID propietario 60104, `ctest` hijo
30824). El build de Debug ya había publicado `OK` para el fingerprint
`DB20E0A28A6564A0EA43F35D1DFFD91E`; la suite de tests todavía estaba ejecutándose
al momento de la comprobación. CPU y memoria no eran un problema, pero la PC y la
lane de tests no estaban libres. Por eso no se lanzó otra compilación, no se
robaron locks y no se ejecutaron probes foreground que pudieran mover el mouse o
tomar el foco.

Observación de coordinación: `build_coord.ps1 -Lane tests -Action status` informó
`STALE` mientras el propietario seguía vivo. La causa observada es que
`ConvertFrom-Json` convierte `ownerStartedUtc` en `DateTime` y al convertirlo de
nuevo a string usa el formato local, mientras `Get-Process.StartTime` se compara
como ISO 8601. Esto queda pendiente como corrección separada del coordinador; no
se modificó durante esta verificación.

### Cierre de la verificación — 2026-08-27

La corrida compartida terminó con `tests.DB20E0A28A6564A0EA43F35D1DFFD91E.txt`
en estado `OK`. Luego se ejecutó `build.bat Debug NOPAUSE`, también con estado
`OK` (`build.DB20E0A28A6564A0EA43F35D1DFFD91E.txt`), y se confirmó la existencia
de `build/Debug/LlamaCode.exe` (26.113.024 bytes). El build fue incremental: el
ejecutable ya era posterior al archivo fuente más nuevo, por lo que su timestamp
no tuvo que avanzar; el acceso directo Debug sí fue actualizado.

Al quedar libres las corridas y con LlamaCode cerrado, sí se ejecutaron los probes
autocontenidos. `qa_visual_automation --matrix` terminó `summary cases=12
failures=0`: cubrió los 3 monitores, temas claro/oscuro, DPI 100% y 125%, usando
el backend `qt-sampled` y sin `--execute-click`. `qa_ocr_probe --self-contained`
terminó con código 0: motor OCR disponible en `es-MX`, 3 pantallas, 100% de
acuerdo OCR/UIA en cada pantalla y 100% de acuerdo también en los monitores
escalados al 125%. Las superficies de prueba fueron destruidas al terminar y los
locks de build/tests quedaron libres al cierre.

### 2026-08-27 — seguimiento diferido

Una comprobación posterior encontró la entrada de usuario activa y la ventana
`Seguridad de Windows` en primer plano. Aunque no había una compilación o suite
registrada en los locks, la PC no se consideró libre; no se iniciaron tests, build
ni probes y no se tocó ninguna aplicación. El seguimiento automático queda
programado para volver a comprobar dentro de 10 minutos.

### 2026-08-27 — validación ampliada

Con la lane de tests libre y `LlamaCode` cerrado se repitió la matriz visual
en el candidato Debug. `qa_visual_automation --matrix` terminó con
`summary cases=12 failures=0` sin ejecutar clicks. Luego
`qa_visual_automation --matrix --execute-click` ejecutó los 12 clicks sobre
las ventanas propias del fixture y terminó también con `exit=0`.

La matriz cubrió los tres monitores, temas claro/oscuro y escalado 100/125 %.
El resultado queda consolidado junto con el OCR y el build en
[`validation-run-2026-08-27.md`](validation-run-2026-08-27.md).

### 2026-08-28 — validación coordinada completa

La comprobación inicial encontró las lanes `tests` y `build` libres, sin procesos
de LlamaCode, `llama-server`, `cmake`, `MSBuild`, `ctest` ni probes QA, y sin
interacción reciente. Se leyeron `AGENTS.md` y `README.md` antes de iniciar la
corrida.

`tests.bat Debug` terminó con resultado compartido
`OK|2026-08-28T23:51:11.5025490Z|20940`: CTest pasó 73/73 pruebas, y las dos
suites auxiliares también terminaron en `OK` (6 y 8 casos). Luego
`build.bat Debug NOPAUSE` terminó con
`OK|2026-08-28T23:51:53.2025411Z|46280`; se verificó
`build/Debug/LlamaCode.exe` (26.113.024 bytes, modificado a las 20:51:47
`-03:00`) y el acceso directo conservó `assets/debug_icon.ico`.

El build mostró los warnings ya observados de despliegue (`dxcompiler.dll`/`dxil.dll`
y `VCINSTALLDIR`) y los mensajes espurios de cmd `"M"`/`"EM"`; no alteraron el
resultado `OK`. Al cierre, ambas lanes reportaron `FREE`. Quedaron únicamente
workers persistentes de MSBuild con `/nodeReuse:true`, sin `ctest` ni ejecución de
build activa.

Los probes foreground se ejecutaron sin `--execute-click`:

- `build/Debug/qa_visual_automation.exe --matrix`: `summary cases=12 failures=0`,
  cubriendo 3 monitores, temas claro/oscuro y DPI 100/125 %, con backend
  `qt-sampled`.
- `build/Debug/qa_ocr_probe.exe --self-contained`: código 0, OCR `es-MX`, tres
  pantallas (2560x1440 al 100 % y dos 1920x1080 al 125 %, incluida una con
  coordenadas negativas), 100 % de acuerdo OCR/UIA y coordenadas físicas
  correctas.

Las superficies temporales de los probes se destruyeron al terminar y no se
realizaron clicks sobre aplicaciones del usuario.
