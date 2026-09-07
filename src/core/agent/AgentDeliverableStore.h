#pragma once

#include <QJsonObject>
#include <QString>

// Captura los archivos creados o modificados por una corrida sin copiar el
// workspace completo. El manifiesto es local, versionado por run y permite
// restaurar/guardar una copia sólo cuando el caller lo solicita explícitamente.
class AgentDeliverableStore final
{
public:
    static constexpr int FormatVersion = 1;

    static QString rootDir();

    // Devuelve un snapshot de metadatos (hash SHA-256, tamaño y mtime) del
    // workspace. Nunca guarda el contenido de los archivos.
    static QJsonObject snapshot(const QString &workspace, QString *error = nullptr);

    // Compara beforeSnapshot con el estado actual y guarda únicamente archivos
    // creados/modificados. La operación es idempotente por runId.
    static QJsonObject capture(const QString &runId, const QString &workspace,
                               const QJsonObject &beforeSnapshot,
                               QString *error = nullptr);

    static QJsonObject manifest(const QString &runId);
    static bool remove(const QString &runId);

    // `relativePath` debe ser una entrada del manifiesto. Save As no pisa por
    // defecto; `overwrite=true` sólo debe usarse después de una aprobación UI.
    static bool saveAs(const QString &runId, const QString &relativePath,
                       const QString &destination, bool overwrite = false,
                       QString *error = nullptr);
    static bool restore(const QString &runId, const QString &relativePath,
                        const QString &destination, bool overwrite = false,
                        QString *error = nullptr)
    {
        return saveAs(runId, relativePath, destination, overwrite, error);
    }
};
