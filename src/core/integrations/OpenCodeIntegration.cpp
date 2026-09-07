#include "OpenCodeIntegration.h"

#include <QDir>

QString OpenCodeIntegration::modelRef(const QString &modelId)
{
    return QStringLiteral("llamacode/") + modelId;
}

QJsonObject OpenCodeIntegration::buildConfig(const QString &gatewayV1Url,
                                             const QJsonArray &models,
                                             const QString &selectedModelId)
{
    QJsonObject modelMap;
    for (const QJsonValue &value : models) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;

        QJsonObject model{
            {QStringLiteral("name"), entry.value(QStringLiteral("name")).toString(id)},
            {QStringLiteral("tool_call"), true}
        };
        const int context = entry.value(QStringLiteral("context")).toInt();
        const int output = entry.value(QStringLiteral("output")).toInt();
        if (context > 0 || output > 0) {
            QJsonObject limit;
            if (context > 0) limit.insert(QStringLiteral("context"), context);
            if (output > 0) limit.insert(QStringLiteral("output"), output);
            model.insert(QStringLiteral("limit"), limit);
        }
        modelMap.insert(id, model);
    }

    const QJsonObject provider{
        {QStringLiteral("npm"), QStringLiteral("@ai-sdk/openai-compatible")},
        {QStringLiteral("name"), QStringLiteral("LlamaCode local")},
        {QStringLiteral("options"), QJsonObject{
            {QStringLiteral("baseURL"), gatewayV1Url},
            {QStringLiteral("apiKey"), QStringLiteral("{env:LLAMACODE_GATEWAY_API_KEY}")}
        }},
        {QStringLiteral("models"), modelMap}
    };

    QJsonObject root{
        {QStringLiteral("$schema"), QStringLiteral("https://opencode.ai/config.json")},
        {QStringLiteral("enabled_providers"), QJsonArray{QStringLiteral("llamacode")}},
        {QStringLiteral("provider"), QJsonObject{{QStringLiteral("llamacode"), provider}}}
    };
    if (!selectedModelId.isEmpty())
        root.insert(QStringLiteral("model"), modelRef(selectedModelId));
    return root;
}

QStringList OpenCodeIntegration::windowsDesktopCandidates(const QString &localAppData)
{
    if (localAppData.isEmpty()) return {};
    const QDir root(localAppData);
    return {
        root.filePath(QStringLiteral("Programs/@opencode-aidesktop/OpenCode.exe")),
        root.filePath(QStringLiteral("Programs/OpenCode/OpenCode.exe")),
        root.filePath(QStringLiteral("Microsoft/WindowsApps/OpenCode.exe"))
    };
}
