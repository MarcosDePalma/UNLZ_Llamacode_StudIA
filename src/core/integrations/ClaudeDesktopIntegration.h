#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// Construcción pura del perfil de Claude Desktop para Third-Party Inference.
// La escritura de archivos y la selección de rutas quedan en AppController para
// poder preservar la configuración existente y dejar respaldos antes de editar.
class ClaudeDesktopIntegration
{
public:
    static QString configId();
    static QString modelAlias(const QString &launchProfileId);
    static QJsonObject gatewayConfig(const QString &baseUrl,
                                     const QString &apiKey,
                                     const QJsonArray &models,
                                     const QString &selectedModelId);
    static QJsonObject metaConfig(const QJsonArray &models,
                                  const QString &selectedModelId);
    static QJsonObject withDeploymentMode(const QJsonObject &existing,
                                          const QString &mode);

    // Devuelve rutas en orden de preferencia. `msixDirs` debe contener sólo
    // carpetas detectadas, no patrones ni rutas inventadas.
    static QStringList windowsDataDirectories(const QString &appData,
                                              const QString &localAppData,
                                              const QStringList &msixDirs);
};
