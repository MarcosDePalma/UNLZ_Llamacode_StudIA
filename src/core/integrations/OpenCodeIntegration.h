#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// Construcción pura de la configuración efímera que LlamaCode inyecta al
// proceso OpenCode. No modifica la configuración global ni la del proyecto.
class OpenCodeIntegration
{
public:
    static QJsonObject buildConfig(const QString &gatewayV1Url,
                                   const QJsonArray &models,
                                   const QString &selectedModelId);
    static QString modelRef(const QString &modelId);
    static QStringList windowsDesktopCandidates(const QString &localAppData);
};
