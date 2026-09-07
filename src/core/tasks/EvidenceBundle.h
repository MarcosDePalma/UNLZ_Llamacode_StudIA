#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>

// EvidenceBundle v1: export reproducible de corridas ya persistidas.
// No ejecuta herramientas ni incluye secretos: sólo captura el registro de
// RunHistoryStore, su procedencia y hashes para detectar modificaciones.
class EvidenceBundle
{
public:
    static QJsonObject build(const QString &ownerId, const QVariantList &runs,
                             const QString &productVersion);
    static bool write(const QString &path, const QJsonObject &bundle,
                      QString *error = nullptr);
};
