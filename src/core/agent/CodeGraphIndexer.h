#pragma once
#include <QString>
#include <QStringList>

// INDEXADOR DETERMINISTA repo→[[GraphStore]]. Pasada estilo "Graphify pass-1":
// recorre el árbol del proyecto y, por archivo, extrae sus SÍMBOLOS (clases/
// funciones, vía regex por lenguaje) y sus IMPORTS/INCLUDES, volcándolos como
// entidades + relaciones tipadas:
//   <archivo> -[defines]-> <símbolo>
//   <archivo> -[imports]-> <archivo>   (resuelto por basename dentro del repo)
//
// SIN LLM, idempotente (GraphStore deduplica por nombre normalizado), barato:
// usa GraphStore::addBatch para escribir todo en una sola pasada. Después el
// agente consulta el mapa con la tool 'graph' (action='query') en vez de
// re-leer archivos → contexto barato para el loop ReAct. Complementa la carga
// MANUAL del grafo (link/decide); esto la siembra automáticamente.
//
// Reindexado INCREMENTAL (buildIncremental / reindexFiles): re-procesa sólo los
// archivos cambiados desde la última pasada (git diff vs el HEAD guardado, o
// mtime si no hay git), borrando primero los edges viejos de cada archivo (un
// símbolo/import eliminado desaparece del grafo). Equivale al "hook post-commit"
// de Graphify: mantener el mapa vivo sin re-escanear todo.
namespace CodeGraphIndexer {

struct Stats {
    int files = 0;     // archivos indexados (con extensión soportada)
    int symbols = 0;   // símbolos distintos definidos
    int edges = 0;     // relaciones nuevas escritas (defines + imports)
};

// Indexa TODO 'rootCwd'. 'langs' filtra por familia (vacío = cpp/qml/js/ts/py).
// Escribe en GraphStore::jsonlPath(rootCwd) y deja un marcador de estado
// (.llamacode/graph.state) para el reindexado incremental. 'report' opcional.
Stats build(const QString &rootCwd, const QStringList &langs, QString *report);

// Reindexa SÓLO los archivos cambiados desde la última pasada: detecta cambios
// por git (diff contra el HEAD guardado + working tree) y cae a mtime si no hay
// git/baseline. Borra los edges viejos de cada archivo tocado y purga los de
// archivos borrados. Si no hay estado previo, hace un build completo.
Stats buildIncremental(const QString &rootCwd, const QStringList &langs, QString *report);

// Reindexa una lista EXPLÍCITA de archivos (rutas relativas al repo). Por cada
// uno: borra sus relaciones previas y re-extrae símbolos+imports. Resuelve
// imports contra todo el repo. Para el flujo incremental y para forzar refresco
// puntual desde la tool. Ignora rutas inexistentes o de extensión no soportada.
Stats reindexFiles(const QString &rootCwd, const QStringList &relFiles,
                   const QStringList &langs, QString *report);

}  // namespace CodeGraphIndexer
