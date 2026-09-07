#include "HarnessEngine.h"

#include <QCryptographicHash>
#include <QJsonDocument>

namespace {

QString normalized(const QString &id)
{
    return id.trimmed().toLower();
}

HarnessEngine::Descriptor legacyDescriptor()
{
    return {QStringLiteral("legacy"), QStringLiteral("Legacy"),
            QStringLiteral("Harness histórico de LlamaCode; contrato estable y rutas existentes."),
            1, false, QStringLiteral("agent_llamaagent"), QStringLiteral("legacy")};
}

HarnessEngine::Descriptor nextDescriptor()
{
    return {QStringLiteral("next"), QStringLiteral("Next (experimental)"),
            QStringLiteral("Perfil modular reversible con eventos y almacenamiento aislados."),
            2, true, QStringLiteral("agent_harness_next"), QStringLiteral("legacy")};
}

QVariantMap asVariant(const HarnessEngine::Descriptor &d)
{
    return {{QStringLiteral("id"), d.id},
            {QStringLiteral("name"), d.name},
            {QStringLiteral("description"), d.description},
            {QStringLiteral("version"), d.version},
            {QStringLiteral("experimental"), d.experimental},
            {QStringLiteral("storageNamespace"), d.storageNamespace},
            {QStringLiteral("fallbackId"), d.fallbackId}};
}

}  // namespace

namespace HarnessEngine {

QString effectiveId(const HarnessRuntimeModule &runtime)
{
    const QString id = normalized(runtime.engine);
    if (id.isEmpty()) return QStringLiteral("legacy");
    return isKnown(id) ? id : QStringLiteral("legacy");
}

int effectiveVersion(const HarnessRuntimeModule &runtime)
{
    if (effectiveId(runtime) == QStringLiteral("legacy")) return 1;
    return qMax(1, runtime.version);
}

bool isKnown(const QString &id)
{
    const QString n = normalized(id);
    return n == QLatin1String("legacy") || n == QLatin1String("next");
}

QString storageNamespace(const QString &id)
{
    return normalized(id) == QLatin1String("next") ? QStringLiteral("agent_harness_next")
                                                     : QStringLiteral("agent_llamaagent");
}

QString fingerprint(const HarnessSpec &spec)
{
    const QByteArray bytes = QJsonDocument(spec.toJson()).toJson(QJsonDocument::Compact);
    return QStringLiteral("sha256:%1")
        .arg(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                                     .toHex()));
}

QVariantList catalog()
{
    return {asVariant(legacyDescriptor()), asVariant(nextDescriptor())};
}

}  // namespace HarnessEngine
