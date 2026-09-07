# Rendimiento de la interfaz

## Carga diferida de páginas

`qml/Main.qml` no construye todas las páginas al iniciar. Cada sección se
define como un `Component` y se instancia mediante un `Loader` cuando el
usuario la visita por primera vez. Una vez creada, la página permanece viva
para conservar el estado temporal de sus formularios y evitar reconstrucciones
innecesarias.

La página de inicio es la única que se carga durante el arranque. Esto reduce
el trabajo inicial del hilo GUI y permite que el menú de la bandeja y la
ventana respondan mientras el resto de la aplicación queda disponible bajo
demanda.

## Diagnóstico del event loop

El build de escritorio registra una advertencia cuando el hilo GUI deja de
procesar eventos durante 250 ms o más:

```text
GUI event-loop pause ms=...
```

Estas pausas son evidencia de trabajo síncrono en el hilo principal. Deben
correlacionarse con la acción que el usuario estaba realizando antes de mover
operaciones de disco o CPU a tareas asíncronas.

Las cargas de historial, benchmarks y reportes del arranque se ejecutan en
turnos separados del event loop. Esto no paraleliza todavía el parseo interno,
pero evita encadenar varias lecturas grandes sin devolver el control a la GUI.

## Bandeja de notificación

El menú de la bandeja usa `QSystemTrayIcon` y `QMenu` nativos. QML conserva la
sincronización de visibilidad, idioma y estado de Teach, pero la apertura del
menú y sus acciones ya no dependen de instanciar páginas QML ni de los menús de
`Qt.labs.platform`.

## Validación

Después de cambios en QML o C++ de la interfaz:

```bat
tests.bat Debug
build.bat Debug NOPAUSE
```

Además de la suite automática, comprobar manualmente el click derecho del tray,
la restauración de la ventana, el cambio entre páginas y las acciones de
agregar binarios/modelos desde el asistente inicial.

## Pendientes anotados

- Regenerar el banco `build_tests` en un entorno aislado cuando no queden
  procesos huérfanos de CMake/MSBuild y completar `tests.bat Debug`.
- Hacer una prueba manual de 30–50 clicks derechos del tray mientras se cargan
  benchmarks, se cambia de página y se ejecuta Teach.
- Si las pausas del event loop siguen superando 250 ms, convertir el parseo de
  historial/catálogo en trabajo `QtConcurrent` con resultados aplicados en el
  hilo GUI; la separación actual sólo divide las fases entre turnos.
