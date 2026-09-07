#include "ToolExecutionSafety.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <algorithm>

namespace {

QJsonValue canonicalValue(const QJsonValue &value)
{
    if (value.isArray()) {
        QJsonArray out;
        for (const QJsonValue &item : value.toArray())
            out.append(canonicalValue(item));
        return out;
    }
    if (!value.isObject()) return value;

    const QJsonObject object = value.toObject();
    QStringList keys = object.keys();
    std::sort(keys.begin(), keys.end());
    QJsonObject out;
    for (const QString &key : keys)
        out.insert(key, canonicalValue(object.value(key)));
    return out;
}

bool looksReadOnly(const QString &text)
{
    static const QStringList verbs{
        QStringLiteral("get"), QStringLiteral("list"), QStringLiteral("read"),
        QStringLiteral("search"), QStringLiteral("find"), QStringLiteral("fetch"),
        QStringLiteral("query"), QStringLiteral("inspect"), QStringLiteral("describe")
    };
    for (const QString &verb : verbs)
        if (text.startsWith(verb + QLatin1Char('_')) || text == verb) return true;
    return false;
}

} // namespace

namespace ToolExecutionSafety {

Contract fromMcpTool(const QString &name, const QString &description,
                     const QJsonObject &annotations)
{
    Contract c;
    const QJsonObject lc = annotations.value(QStringLiteral("llamacode")).toObject();
    const QString explicitEffect = lc.value(QStringLiteral("effect")).toString();
    if (explicitEffect == QLatin1String("read")
        || explicitEffect == QLatin1String("proposal")
        || explicitEffect == QLatin1String("external_write")) {
        c.effect = explicitEffect;
        c.source = QStringLiteral("llamacode_annotation");
    } else if (annotations.value(QStringLiteral("readOnlyHint")).toBool(false)) {
        c.effect = QStringLiteral("read");
        c.source = QStringLiteral("mcp_annotation");
    } else if (looksReadOnly(name.trimmed().toLower())
               && description.contains(QStringLiteral("read-only"), Qt::CaseInsensitive)) {
        // La heurística sólo relaja permisos cuando nombre Y descripción coinciden.
        c.effect = QStringLiteral("read");
        c.source = QStringLiteral("strong_heuristic");
    }

    c.destructive = annotations.value(QStringLiteral("destructiveHint")).toBool(false)
                    || lc.value(QStringLiteral("destructive")).toBool(false);
    c.idempotent = annotations.value(QStringLiteral("idempotentHint")).toBool(false)
                   || lc.value(QStringLiteral("idempotent")).toBool(false);
    c.openWorld = annotations.value(QStringLiteral("openWorldHint")).toBool(true);
    c.approvalRequired = c.effect != QLatin1String("read");
    if (lc.contains(QStringLiteral("approvalRequired")))
        c.approvalRequired = lc.value(QStringLiteral("approvalRequired")).toBool(true);
    if (c.destructive) c.approvalRequired = true;
    const QString receipt = lc.value(QStringLiteral("receipt")).toString();
    if (!receipt.isEmpty()) c.receipt = receipt;
    return c;
}

QVariantMap toVariantMap(const Contract &c)
{
    return {
        {QStringLiteral("effect"), c.effect},
        {QStringLiteral("approvalRequired"), c.approvalRequired},
        {QStringLiteral("destructive"), c.destructive},
        {QStringLiteral("idempotent"), c.idempotent},
        {QStringLiteral("openWorld"), c.openWorld},
        {QStringLiteral("receipt"), c.receipt},
        {QStringLiteral("source"), c.source}
    };
}

QByteArray canonicalJson(const QJsonObject &object)
{
    return QJsonDocument(canonicalValue(object).toObject()).toJson(QJsonDocument::Compact);
}

QString payloadHash(const QString &server, const QString &tool,
                    const QJsonObject &arguments)
{
    const QByteArray payload = server.toUtf8() + '\0' + tool.toUtf8() + '\0'
                               + canonicalJson(arguments);
    return QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

QString idempotencyKey(const QString &correlationId, const QString &hash)
{
    return QStringLiteral("lc_%1").arg(QString::fromLatin1(QCryptographicHash::hash(
        correlationId.toUtf8() + '\0' + hash.toUtf8(),
        QCryptographicHash::Sha256).toHex()));
}

QString resultHash(const QString &result)
{
    return QString::fromLatin1(QCryptographicHash::hash(result.toUtf8(),
                                                         QCryptographicHash::Sha256).toHex());
}

} // namespace ToolExecutionSafety
