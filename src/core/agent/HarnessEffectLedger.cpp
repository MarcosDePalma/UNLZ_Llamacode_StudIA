#include "HarnessEffectLedger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

QJsonObject HarnessEffectRecord::toJson() const
{
    return {{QStringLiteral("effectId"), effectId},
            {QStringLiteral("state"), state},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("sessionId"), sessionId},
            {QStringLiteral("correlationId"), correlationId},
            {QStringLiteral("payloadHash"), payloadHash},
            {QStringLiteral("resultHash"), resultHash},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("updatedAt"), static_cast<double>(updatedAt)}};
}

HarnessEffectRecord HarnessEffectRecord::fromJson(const QJsonObject &object)
{
    HarnessEffectRecord record;
    record.effectId = object.value(QStringLiteral("effectId")).toString();
    record.state = object.value(QStringLiteral("state")).toString();
    record.kind = object.value(QStringLiteral("kind")).toString();
    record.sessionId = object.value(QStringLiteral("sessionId")).toString();
    record.correlationId = object.value(QStringLiteral("correlationId")).toString();
    record.payloadHash = object.value(QStringLiteral("payloadHash")).toString();
    record.resultHash = object.value(QStringLiteral("resultHash")).toString();
    record.detail = object.value(QStringLiteral("detail")).toString();
    record.updatedAt = static_cast<qint64>(object.value(QStringLiteral("updatedAt")).toDouble());
    return record;
}

bool HarnessEffectLedger::open(const QString &path, QString *error)
{
    m_path = QDir::cleanPath(path.trimmed());
    if (m_path.isEmpty()) return false;
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    return load(error);
}

bool HarnessEffectLedger::load(QString *error)
{
    m_records = {};
    if (!QFile::exists(m_path)) return true;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    while (!file.atEnd()) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readLine().trimmed(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            if (error) *error = parseError.errorString();
            return false;
        }
        m_records.append(doc.object());
    }
    return true;
}

bool HarnessEffectLedger::append(const HarnessEffectRecord &record)
{
    if (m_path.isEmpty()) return false;
    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return false;
    if (file.write(QJsonDocument(record.toJson()).toJson(QJsonDocument::Compact)) < 0
        || file.write("\n") < 0 || !file.flush())
        return false;
    m_records.append(record.toJson());
    return true;
}

bool HarnessEffectLedger::prepare(const HarnessEffectRecord &input)
{
    if (input.effectId.trimmed().isEmpty() || !record(input.effectId).effectId.isEmpty())
        return false;
    HarnessEffectRecord prepared = input;
    prepared.state = QStringLiteral("prepared");
    prepared.updatedAt = QDateTime::currentMSecsSinceEpoch();
    return append(prepared);
}

HarnessEffectRecord HarnessEffectLedger::record(const QString &effectId) const
{
    for (int i = m_records.size() - 1; i >= 0; --i) {
        const HarnessEffectRecord candidate = HarnessEffectRecord::fromJson(
            m_records.at(i).toObject());
        if (candidate.effectId == effectId) return candidate;
    }
    return {};
}

bool HarnessEffectLedger::transition(const QString &effectId, const QString &state,
                                     const QString &detail, const QString &resultHash)
{
    HarnessEffectRecord next = record(effectId);
    if (next.effectId.isEmpty() || state.trimmed().isEmpty()) return false;
    next.state = state.trimmed().toLower();
    next.detail = detail;
    if (!resultHash.isEmpty()) next.resultHash = resultHash;
    next.updatedAt = QDateTime::currentMSecsSinceEpoch();
    return append(next);
}

QJsonArray HarnessEffectLedger::records() const
{
    return m_records;
}
