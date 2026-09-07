#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

// Índice local, descartable y regenerable para recuperar contexto de código.
// No reemplaza MemoryStore/GraphStore: guarda archivos, chunks y relaciones
// estructurales para que el agente pueda explorar por evidencia antes de editar.
namespace ContextIndex {

QString cachePath(const QString &root);

// Devuelve el estado conocido sin forzar un barrido completo.
QVariantMap status(const QString &root);

// Actualiza el índice por mtime/hash. changedPaths puede acotar la pasada tras
// una escritura del agente; vacío significa comprobar todo el workspace.
QVariantMap refresh(const QString &root, const QStringList &changedPaths = {},
                    int maxFiles = 8000);

// Recuperación compacta en dos pasos: candidatos + handles + recibo.
QVariantMap scout(const QString &root, const QString &query, int tokenBudget = 700,
                  int k = 8, bool expandGraph = true, const QString &path = {});

// Resuelve un handle de scout, valida el hash actual y devuelve sólo el rango
// exacto. Un archivo modificado invalida el handle en vez de devolver evidencia
// potencialmente obsoleta.
QString fetch(const QString &root, const QString &handle, QVariantMap *meta = nullptr);

// Presentación estable para el modelo; el recibo sigue siendo JSON parseable al
// final del bloque y permite depurar el retrieval sin depender de la UI.
QString formatScout(const QVariantMap &result);

} // namespace ContextIndex

