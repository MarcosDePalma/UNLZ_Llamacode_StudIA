#include "ClaudeDesktopIntegration.h"

#include <QDir>
#include <QRegularExpression>

QString ClaudeDesktopIntegration::configId()
{
    // ID estable usado por la biblioteca de configuraciones 3P de Desktop.
    return QStringLiteral("00000000-0000-4000-8000-000000000114");
}

QString ClaudeDesktopIntegration::modelAlias(const QString &launchProfileId)
{
    QString id = launchProfileId.trimmed();
    if (id.isEmpty()) return {};
    // Desktop valida el nombre como si fuera un modelo Claude. El sufijo
    // conserva una referencia estable al launch real que el gateway resolverá.
    id.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")),
               QStringLiteral("-"));
    return QStringLiteral("claude-llamacode-") + id;
}

QJsonObject ClaudeDesktopIntegration::gatewayConfig(const QString &baseUrl,
                                                    const QString &apiKey,
                                                    const QJsonArray &models,
                                                    const QString &selectedModelId)
{
    QString normalizedUrl = baseUrl.trimmed();
    while (normalizedUrl.endsWith(QLatin1Char('/')))
        normalizedUrl.chop(1);

    QJsonArray inferenceModels;
    auto appendModel = [&inferenceModels](const QJsonObject &entry) {
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString alias = modelAlias(id);
        if (id.isEmpty() || alias.isEmpty()) return;
        const QString displayName = entry.value(QStringLiteral("name"))
                                        .toString(id).trimmed();
        inferenceModels.append(QJsonObject{
            {QStringLiteral("name"), alias},
            {QStringLiteral("labelOverride"),
             QStringLiteral("LlamaCode · %1").arg(displayName)}
        });
    };

    // El perfil seleccionado queda primero para que Desktop lo ofrezca como
    // opción inicial; el resto sigue disponible en el selector.
    for (const QJsonValue &value : models) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("id")).toString() == selectedModelId)
            appendModel(entry);
    }
    for (const QJsonValue &value : models) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("id")).toString() != selectedModelId)
            appendModel(entry);
    }

    return QJsonObject{
        {QStringLiteral("inferenceProvider"), QStringLiteral("gateway")},
        {QStringLiteral("inferenceCredentialKind"), QStringLiteral("static")},
        {QStringLiteral("inferenceGatewayApiKey"), apiKey},
        {QStringLiteral("inferenceGatewayAuthScheme"), QStringLiteral("bearer")},
        {QStringLiteral("inferenceGatewayBaseUrl"), normalizedUrl},
        {QStringLiteral("inferenceModels"), inferenceModels}
    };
}

QJsonObject ClaudeDesktopIntegration::metaConfig(const QJsonArray &models,
                                                 const QString &selectedModelId)
{
    QString name = modelAlias(selectedModelId);
    if (name.isEmpty() && !models.isEmpty())
        name = modelAlias(models.first().toObject().value(QStringLiteral("id")).toString());
    if (name.isEmpty()) name = QStringLiteral("claude-llamacode");

    return QJsonObject{
        {QStringLiteral("appliedId"), configId()},
        {QStringLiteral("entries"), QJsonArray{QJsonObject{
            {QStringLiteral("id"), configId()},
            {QStringLiteral("name"), name}
        }}}
    };
}

QJsonObject ClaudeDesktopIntegration::withDeploymentMode(const QJsonObject &existing,
                                                         const QString &mode)
{
    QJsonObject result = existing;
    result.insert(QStringLiteral("deploymentMode"), mode);
    return result;
}

QStringList ClaudeDesktopIntegration::windowsDataDirectories(const QString &appData,
                                                             const QString &localAppData,
                                                             const QStringList &msixDirs)
{
    QStringList result;
    auto addUnique = [&result](const QString &path) {
        const QString clean = QDir::cleanPath(path.trimmed());
        if (!clean.isEmpty() && !result.contains(clean)) result.append(clean);
    };

    // El perfil estándar es el que usan las instalaciones Electron actuales.
    if (!appData.trimmed().isEmpty())
        addUnique(QDir(appData).filePath(QStringLiteral("Claude")));
    // Compatibilidad con el layout 3P de versiones anteriores y con MSIX.
    for (const QString &path : msixDirs) addUnique(path);
    if (!localAppData.trimmed().isEmpty())
        addUnique(QDir(localAppData).filePath(QStringLiteral("Claude-3p")));
    if (result.isEmpty() && !localAppData.trimmed().isEmpty())
        addUnique(QDir(localAppData).filePath(QStringLiteral("Claude")));
    return result;
}
