# Rendimiento de arranque

LlamaCode mantiene todas las páginas QML disponibles desde el inicio para que
entrar a una sección no provoque una pausa por carga diferida. La optimización
se hace reduciendo el trabajo que cada página realiza al construirse y
separando el arranque en fases.

## Fases

1. Se crea y muestra la ventana.
2. Se actualizan registros livianos de binarios y roots.
3. La detección de GPU (`nvidia-smi`) corre en un worker.
4. El catálogo se diagnostica y los roots se escanean después del primer frame.
5. Benchmark, Research y recomendaciones se preparan en la fase tardía.

La UI expone `App.startupBusy`, `App.startupStatus` y
`App.startupTimings`. El log de la aplicación registra también los tiempos
hasta `QApplication ready`, carga de `Main.qml`, primera ventana visible y
entrada al event loop. Esto permite comparar `LlamaCode` y
`LlamaCode-debug` sin inferir el origen de una demora.

## Catálogo incremental

El escáner conserva la metadata previamente catalogada cuando coinciden ruta,
tamaño y fecha de modificación. Sólo los archivos nuevos o modificados vuelven
a leer el header GGUF y a calcular composición, quant real y arquitectura.

Los roots de inicio ya no se escanean durante la construcción de
`AppController`; se programan desde la fase de startup posterior al primer
pintado. Los roots manuales mantienen su comportamiento y sólo se escanean al
pedir un rescan.
