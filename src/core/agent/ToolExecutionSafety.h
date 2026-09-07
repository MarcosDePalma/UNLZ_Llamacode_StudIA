#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

// Contrato uniforme para tools externas. MCP annotations es opcional; cuando un
// server no declara efectos, la politica es deliberadamente conservadora.
namespace ToolExecutionSafety {

struct Contract {
    QString effect = QStringLiteral("external_write"); // read|proposal|external_write
    bool approvalRequired = true;
    bool destructive = false;
    bool idempotent = false;
    bool openWorld = true;
    QString receipt = QStringLiteral("result_hash");
    QString source = QStringLiteral("conservative_default");
};

Contract fromMcpTool(const QString &name, const QString &description,
                     const QJsonObject &annotations);
QVariantMap toVariantMap(const Contract &contract);

// JSON canonico (claves ordenadas recursivamente) para ligar aprobación,
// idempotencia y recibos al payload exacto.
QByteArray canonicalJson(const QJsonObject &object);
QString payloadHash(const QString &server, const QString &tool,
                    const QJsonObject &arguments);
QString idempotencyKey(const QString &correlationId, const QString &payloadHash);
QString resultHash(const QString &result);

} // namespace ToolExecutionSafety
