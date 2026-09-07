#include "EvidenceBundle.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace {
QByteArray canonicalRun(const QVariantMap &run)
{
    return QJsonDocument(QJsonObject::fromVariantMap(run)).toJson(QJsonDocument::Compact);
}
}

QJsonObject EvidenceBundle::build(const QString &ownerId, const QVariantList &runs,
                                  const QString &productVersion)
{
    QJsonArray exportedRuns;
    for (const QVariant &value : runs) {
        const QVariantMap run = value.toMap();
        QJsonObject item = QJsonObject::fromVariantMap(run);
        item.insert(QStringLiteral("evidenceSha256"),
                    QString::fromLatin1(QCryptographicHash::hash(
                        canonicalRun(run), QCryptographicHash::Sha256).toHex()));
        exportedRuns.append(item);
    }

    QJsonObject bundle;
    bundle.insert(QStringLiteral("schema"), QStringLiteral("llamacode.evidence.v1"));
    bundle.insert(QStringLiteral("exportedAt"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    bundle.insert(QStringLiteral("product"), QStringLiteral("UNLZ_LlamaCode"));
    bundle.insert(QStringLiteral("productVersion"), productVersion);
    bundle.insert(QStringLiteral("ownerId"), ownerId);
    bundle.insert(QStringLiteral("runCount"), exportedRuns.size());
    bundle.insert(QStringLiteral("runs"), exportedRuns);
    return bundle;
}

bool EvidenceBundle::write(const QString &path, const QJsonObject &bundle, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(bundle).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
