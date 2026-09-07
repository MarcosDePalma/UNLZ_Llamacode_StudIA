# AGENTS.md

Instrucciones para agentes que trabajen en este repo.

- Antes de editar, leer este archivo y el `README.md` de la raiz. Si el cambio altera comportamiento, build, arquitectura o flujo de trabajo, actualizar la documentacion correspondiente.
- No leer todo el proyecto por defecto. Buscar con `rg`, abrir solo los archivos relevantes y seguir los limites de modulo existentes (`src/core`, `src/core/agent`, `src/core/profiles`, `qml/pages`, etc.).
- Todo bug arreglado debe incluir una prueba de regresion cuando sea viable. Toda feature nueva debe cubrir al menos el camino feliz y los bordes principales.
- Antes de terminar, correr `tests.bat Debug` cuando se toque C++/QML/core. Si no se puede correr, dejar el motivo concreto.
- Los builds normales son Debug, que funciona como release candidate: usar `build.bat Debug NOPAUSE` y verificar `build/Debug/LlamaCode.exe`. Release es el canal estable y sólo debe generarse explícitamente (`build.bat Release NOPAUSE`) después de integrar y validar varias versiones candidatas.
- En esta notebook, la app que usa el usuario es el Debug candidato de este proyecto:
  `C:\Users\cristian\Documents\LlamaCode\build\Debug\LlamaCode.exe`. Cuando se
  compile para validar cambios, verificar que ese ejecutable se actualizó; sólo una
  promoción estable debe verificar `build\Release\LlamaCode.exe`.
- Mantener sincronizada la identidad visual por configuración: Debug debe usar `assets/debug_icon.ico` (llama roja) tanto en el `.exe`, acceso directo, ventana principal y splash; Release debe usar `assets/app_icon.ico`. La selección en C++ y recursos debe depender de `LC_DEBUG_ICON`, no de `QT_DEBUG`.
- Para perfiles locales Qwen/coding, preferir sampling conservador: `--temp 0.6 --top-p 0.95 --top-k 20 --min-p 0.0 --repeat-penalty 1.0 --presence-penalty 0.0`. No subir creatividad sin justificarlo.
- Mantener la entrada `nav.tasks` de `qml/components/NavBar.qml` con `serverOnly: true`; Tasks requiere un servidor activo y no debe mostrarse como navegación disponible sin servidor.
- No revertir cambios ajenos. Si el working tree ya esta sucio, trabajar alrededor de esos cambios y reportar que archivos se tocaron.
- Para operaciones de GitHub en este proyecto, antes de hacer commit, pull, push,
  abrir PR, consultar issues o cualquier operación contra GitHub, cambiar/verificar
  que la cuenta activa sea `guideahon`. Usar siempre `guideahon` y pushear a
  `origin main`. El remoto `origin` debe apuntar a
  `https://github.com/cristianlukas/UNLZ_Llamacode.git`, branch `main`. Si las
  credenciales activas no corresponden a `guideahon`, no continuar con la operación
  GitHub y reportar el bloqueo.
- Si hay repo git disponible, al terminar hacer commit y push de los cambios propios, salvo que el usuario indique lo contrario o haya un bloqueo real.
- Mantener los cambios acotados. Evitar refactors amplios si no son necesarios para la tarea.
- Diseñar **Automatizaciones/Teach como control general de la PC**, capaz de operar
  cualquier aplicación. No hardcodear nombres de apps, colores, botones, layouts,
  coordenadas, textos ni heurísticas excesivamente específicas de un caso observado
  cuando la mejora pertenece al motor general. Preferir UI Automation, targets
  semánticos, visión, evidencia, contexto de la aplicación y adaptación mediante
  tools. Un caso concreto (Paint, Calculadora, navegador, etc.) puede y debe usarse
  como prueba de regresión, pero no debe definir la implementación ni perjudicar
  otras aplicaciones. Antes de aceptar una solución, preguntarse si sigue siendo
  válida al cambiar de app, resolución, idioma, tema y distribución de controles.
