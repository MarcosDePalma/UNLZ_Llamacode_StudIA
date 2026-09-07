#include "HarnessEventLog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>

bool HarnessEventLog::open(const QString &path, QString *error)
{
    m_path = QDir::cleanPath(path.trimmed());
    if (m_path.isEmpty()) return false;
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    return load(error);
}

bool HarnessEventLog::load(QString *error)
{
    m_events = {};
    m_nextSequence = 1;
    if (m_path.isEmpty()) return false;

    QFile file(m_path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }

    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            if (error) *error = QStringLiteral("JSONL inválido en %1: %2")
                                   .arg(m_path, parseError.errorString());
            return false;
        }
        const QJsonObject event = doc.object();
        const qint64 seq = static_cast<qint64>(event.value(QStringLiteral("seq")).toDouble(0));
        if (seq != m_nextSequence || event.value(QStringLiteral("kind")).toString().isEmpty()) {
            if (error) *error = QStringLiteral("secuencia o kind inválido en %1").arg(m_path);
            return false;
        }
        m_events.append(event);
        ++m_nextSequence;
    }
    return true;
}

bool HarnessEventLog::append(const QString &kind, QJsonObject payload)
{
    const QString normalizedKind = kind.trimmed();
    if (m_path.isEmpty() || normalizedKind.isEmpty()) return false;

    QJsonObject event{
        {QStringLiteral("seq"), static_cast<double>(m_nextSequence)},
        {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("ts"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("kind"), normalizedKind},
        {QStringLiteral("payload"), payload}};

    QFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return false;
    if (file.write(QJsonDocument(event).toJson(QJsonDocument::Compact)) < 0
        || file.write("\n") < 0 || !file.flush())
        return false;
    m_events.append(event);
    ++m_nextSequence;
    return true;
}
