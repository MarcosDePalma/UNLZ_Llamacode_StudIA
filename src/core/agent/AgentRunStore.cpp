#include "AgentRunStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace {

QString readRecordFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}

bool writeAtomic(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

QJsonObject loadObject(const QString &path)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(readRecordFile(path).toUtf8(), &parseError);
    return parseError.error == QJsonParseError::NoError && doc.isObject()
        ? doc.object() : QJsonObject{};
}

bool isPending(const QString &status)
{
    return status == QLatin1String("queued") || status == QLatin1String("running")
        || status == QLatin1String("waiting_approval")
        || status == QLatin1String("waiting_user")
        || status == QLatin1String("uncertain");
}

bool isClaimable(const QString &status)
{
    // `uncertain` is intentionally visible to recovery UI, but never becomes
    // executable again implicitly: its previous owner may have performed an
    // external effect after the lease boundary.
    return status == QLatin1String("queued")
        || status == QLatin1String("waiting_approval")
        || status == QLatin1String("waiting_user");
}

} // namespace

QJsonObject AgentRunRecord::toJson(bool includeLease) const
{
    QJsonObject object{
        {QStringLiteral("schemaVersion"), AgentRunStore::FormatVersion},
        {QStringLiteral("runId"), runId},
        {QStringLiteral("requestHash"), requestHash},
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("correlationId"), correlationId},
        {QStringLiteral("workspace"), workspace},
        {QStringLiteral("objective"), objective.left(4096)},
        {QStringLiteral("status"), status},
        {QStringLiteral("detail"), detail.left(4096)},
        {QStringLiteral("createdAt"), static_cast<double>(createdAt)},
        {QStringLiteral("updatedAt"), static_cast<double>(updatedAt)},
        {QStringLiteral("finishedAt"), static_cast<double>(finishedAt)},
        {QStringLiteral("leaseExpiresAt"), static_cast<double>(leaseExpiresAt)},
        {QStringLiteral("eventSequence"), static_cast<double>(eventSequence)},
        {QStringLiteral("attempt"), attempt},
        {QStringLiteral("metadata"), metadata}};
    if (includeLease) {
        object[QStringLiteral("ownerId")] = ownerId;
        object[QStringLiteral("leaseToken")] = leaseToken;
        object[QStringLiteral("beforeSnapshot")] = beforeSnapshot;
    }
    return object;
}

AgentRunRecord AgentRunRecord::fromJson(const QJsonObject &object)
{
    AgentRunRecord record;
    record.runId = object.value(QStringLiteral("runId")).toString();
    record.requestHash = object.value(QStringLiteral("requestHash")).toString();
    record.sessionId = object.value(QStringLiteral("sessionId")).toString();
    record.correlationId = object.value(QStringLiteral("correlationId")).toString();
    record.workspace = object.value(QStringLiteral("workspace")).toString();
    record.objective = object.value(QStringLiteral("objective")).toString();
    record.status = object.value(QStringLiteral("status")).toString();
    record.detail = object.value(QStringLiteral("detail")).toString();
    record.ownerId = object.value(QStringLiteral("ownerId")).toString();
    record.leaseToken = object.value(QStringLiteral("leaseToken")).toString();
    record.createdAt = static_cast<qint64>(object.value(QStringLiteral("createdAt")).toDouble());
    record.updatedAt = static_cast<qint64>(object.value(QStringLiteral("updatedAt")).toDouble());
    record.finishedAt = static_cast<qint64>(object.value(QStringLiteral("finishedAt")).toDouble());
    record.leaseExpiresAt = static_cast<qint64>(object.value(QStringLiteral("leaseExpiresAt")).toDouble());
    record.eventSequence = static_cast<qint64>(object.value(QStringLiteral("eventSequence")).toDouble());
    record.attempt = object.value(QStringLiteral("attempt")).toInt();
    record.beforeSnapshot = object.value(QStringLiteral("beforeSnapshot")).toObject();
    record.metadata = object.value(QStringLiteral("metadata")).toObject();
    return record;
}

bool AgentRunRecord::isTerminal() const
{
    return status == QLatin1String("completed") || status == QLatin1String("failed")
        || status == QLatin1String("cancelled") || status == QLatin1String("interrupted");
}

QString AgentRunStore::safeId(const QString &value)
{
    const QString id = value.trimmed();
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9._-]{1,128}$"));
    return valid.match(id).hasMatch() ? id : QString();
}

QString AgentRunStore::requestHash(const QString &sessionId, const QString &workspace,
                                   const QString &objective)
{
    const QByteArray canonical = sessionId.toUtf8() + '\n' + workspace.toUtf8() + '\n'
        + objective.left(4096).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

bool AgentRunStore::validStatus(const QString &status)
{
    static const QSet<QString> states{
        QStringLiteral("queued"), QStringLiteral("running"),
        QStringLiteral("waiting_approval"), QStringLiteral("waiting_user"),
        QStringLiteral("completed"), QStringLiteral("failed"),
        QStringLiteral("cancelled"), QStringLiteral("interrupted"),
        QStringLiteral("uncertain")};
    return states.contains(status);
}

bool AgentRunStore::open(const QString &root, QString *error)
{
    m_root = QDir::cleanPath(root.trimmed());
    if (m_root.isEmpty()) {
        if (error) *error = QStringLiteral("root vacío");
        return false;
    }
    if (!QDir().mkpath(QDir(m_root).filePath(QStringLiteral("runs")))
        || !QDir().mkpath(QDir(m_root).filePath(QStringLiteral("events")))) {
        if (error) *error = QStringLiteral("no se pudo crear el almacenamiento de runs");
        return false;
    }
    return true;
}

QString AgentRunStore::recordPath(const QString &runId) const
{
    return QDir(m_root).filePath(QStringLiteral("runs/%1.json").arg(safeId(runId)));
}

QString AgentRunStore::eventPath(const QString &runId) const
{
    return QDir(m_root).filePath(QStringLiteral("events/%1.jsonl").arg(safeId(runId)));
}

AgentRunRecord AgentRunStore::record(const QString &runId) const
{
    QLockFile storeLock(QDir(m_root).filePath(QStringLiteral(".store.lock")));
    if (!acquireLock(storeLock)) return {};
    return recordUnlocked(runId);
}

AgentRunRecord AgentRunStore::recordUnlocked(const QString &runId) const
{
    if (m_root.isEmpty() || safeId(runId).isEmpty()) return {};
    return AgentRunRecord::fromJson(loadObject(recordPath(runId)));
}

bool AgentRunStore::acquireLock(QLockFile &file, QString *error) const
{
    if (m_root.isEmpty()) {
        if (error) *error = QStringLiteral("store no abierto");
        return false;
    }
    file.setStaleLockTime(30000);
    if (!file.tryLock(5000)) {
        if (error) *error = QStringLiteral("store ocupado (%1)")
            .arg(static_cast<int>(file.error()));
        return false;
    }
    return true;
}

bool AgentRunStore::writeRecord(const AgentRunRecord &value, QString *error) const
{
    if (m_root.isEmpty() || safeId(value.runId).isEmpty()) {
        if (error) *error = QStringLiteral("run inválido");
        return false;
    }
    return writeAtomic(recordPath(value.runId),
                       QJsonDocument(value.toJson()).toJson(QJsonDocument::Indented), error);
}

QJsonArray AgentRunStore::events(const QString &runId) const
{
    QLockFile storeLock(QDir(m_root).filePath(QStringLiteral(".store.lock")));
    if (!acquireLock(storeLock)) return {};
    return eventsUnlocked(runId);
}

QJsonArray AgentRunStore::eventsUnlocked(const QString &runId) const
{
    QJsonArray out;
    if (m_root.isEmpty() || safeId(runId).isEmpty()) return out;
    QFile file(eventPath(runId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    while (!file.atEnd()) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readLine().trimmed(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject())
            out.append(doc.object());
    }
    return out;
}

bool AgentRunStore::appendEvent(AgentRunRecord &value, const QString &kind,
                                const QJsonObject &payload, QString *error) const
{
    const QString normalizedKind = kind.trimmed();
    if (normalizedKind.isEmpty() || safeId(value.runId).isEmpty()) {
        if (error) *error = QStringLiteral("evento inválido");
        return false;
    }
    qint64 next = value.eventSequence + 1;
    const QJsonArray previous = eventsUnlocked(value.runId);
    if (!previous.isEmpty())
        next = qMax(next, static_cast<qint64>(previous.last().toObject()
            .value(QStringLiteral("seq")).toDouble()) + 1);
    const QJsonObject event{
        {QStringLiteral("schemaVersion"), FormatVersion},
        {QStringLiteral("seq"), static_cast<double>(next)},
        {QStringLiteral("eventId"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("runId"), value.runId},
        {QStringLiteral("ts"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("kind"), normalizedKind},
        {QStringLiteral("payload"), payload}};
    QFile file(eventPath(value.runId));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    if (file.write(line) != line.size() || !file.flush()) {
        if (error) *error = file.errorString();
        return false;
    }
    value.eventSequence = next;
    value.updatedAt = QDateTime::currentMSecsSinceEpoch();
    return writeRecord(value, error);
}

QString AgentRunStore::accept(const QString &runId, const QString &sessionId,
                              const QString &correlationId, const QString &workspace,
                              const QString &objective, const QJsonObject &beforeSnapshot,
                              QString *error)
{
    if (m_root.isEmpty()) {
        if (error) *error = QStringLiteral("store no abierto");
        return {};
    }
    QLockFile storeLock(QDir(m_root).filePath(QStringLiteral(".store.lock")));
    if (!acquireLock(storeLock, error)) return {};
    QString id = safeId(runId);
    if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString hash = requestHash(sessionId, workspace, objective);
    const AgentRunRecord existing = recordUnlocked(id);
    if (!existing.runId.isEmpty()) {
        if (existing.requestHash != hash) {
            if (error) *error = QStringLiteral("runId reutilizado con otro payload");
            return {};
        }
        return id;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    AgentRunRecord value;
    value.runId = id;
    value.requestHash = hash;
    value.sessionId = sessionId;
    value.correlationId = correlationId;
    value.workspace = workspace;
    value.objective = objective.left(4096);
    value.status = QStringLiteral("queued");
    value.createdAt = now;
    value.updatedAt = now;
    value.beforeSnapshot = beforeSnapshot;
    if (!writeRecord(value, error)) return {};
    if (!appendEvent(value, QStringLiteral("run.accepted"),
                     QJsonObject{{QStringLiteral("status"), value.status}}, error))
        return {};
    return id;
}

bool AgentRunStore::claim(const QString &runId, const QString &ownerId, qint64 leaseMs,
                          QString *leaseToken, QString *error)
{
    if (leaseToken) leaseToken->clear();
    QLockFile storeLock(QDir(m_root).filePath(QStringLiteral(".store.lock")));
    if (!acquireLock(storeLock, error)) return false;
    AgentRunRecord value = recordUnlocked(runId);
    if (value.runId.isEmpty() || ownerId.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("run u owner inválido");
        return false;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (value.isTerminal()) {
        if (error) *error = QStringLiteral("run terminal");
        return false;
    }
    if (value.status == QLatin1String("running") && value.leaseExpiresAt > now) {
        if (value.ownerId != ownerId) {
            if (error) *error = QStringLiteral("run retenido por otro owner");
            return false;
        }
        if (leaseToken) *leaseToken = value.leaseToken;
        return true;
    }
    if (!isClaimable(value.status)) {
        if (error) *error = QStringLiteral("estado no reclamable: %1").arg(value.status);
        return false;
    }
    value.ownerId = ownerId;
    value.leaseToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    value.leaseExpiresAt = now + qMax<qint64>(1000, leaseMs);
    value.status = QStringLiteral("running");
    ++value.attempt;
    const bool ok = appendEvent(value, QStringLiteral("run.claimed"),
                                QJsonObject{{QStringLiteral("ownerId"), ownerId},
                                            {QStringLiteral("attempt"), value.attempt}}, error);
    if (ok && leaseToken) *leaseToken = value.leaseToken;
    return ok;
}

bool AgentRunStore::heartbeat(const QString &runId, const QString &leaseToken,
                              qint64 leaseMs, QString *error)
{
    QLockFile storeLock(QDir(m_root).filePath(QStringLiteral(".store.lock")));
    if (!acquireLock(storeLock, error)) return false;
    AgentRunRecord value = recordUnlocked(runId);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (value.runId.isEmpty() || value.leaseToken.isEmpty() || value.leaseToken != leaseToken
        || value.leaseExpiresAt <= now) {
        if (error) *error = QStringLiteral("lease inválido o expirado");
        return false;
    }
    value.leaseExpiresAt = now + qMax<qint64>(1000, leaseMs);
    value.updatedAt = now;
    return writeRecord(value, error);
}

bool AgentRunStore::transition(const QString &runId, const QString &leaseToken,
                               const QString &status, const QString &detail,
                               const QJsonObject &metadata, QString *error)
{
    const QString normalized = status.trimmed().toLower();
    if (!validStatus(normalized) || normalized == QLatin1String("queued")) {
        if (error) *error = QStringLiteral("estado inválido");
        return false;
    }
    QLockFile storeLock(QDir(m_root).filePath(QStringLiteral(".store.lock")));
    if (!acquireLock(storeLock, error)) return false;
    AgentRunRecord value = recordUnlocked(runId);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (value.runId.isEmpty() || value.leaseToken.isEmpty() || value.leaseToken != leaseToken
        || value.leaseExpiresAt <= now) {
        if (error) *error = QStringLiteral("la transición no pertenece al lease activo");
        return false;
    }
    if (value.isTerminal()) {
        if (error) *error = QStringLiteral("run terminal");
        return false;
    }
    value.status = normalized;
    value.detail = detail.left(4096);
    if (!metadata.isEmpty()) {
        for (auto it = metadata.begin(); it != metadata.end(); ++it)
            value.metadata[it.key()] = it.value();
    }
    if (value.isTerminal()) {
        value.finishedAt = now;
        value.ownerId.clear();
        value.leaseToken.clear();
        value.leaseExpiresAt = 0;
    }
    return appendEvent(value, QStringLiteral("run.%1").arg(normalized),
                       QJsonObject{{QStringLiteral("status"), normalized},
                                   {QStringLiteral("detail"), value.detail},
                                   {QStringLiteral("metadata"), metadata}}, error);
}

int AgentRunStore::recoverStaleRuns(qint64 nowMs, QString *error)
{
    if (m_root.isEmpty()) return 0;
    QLockFile storeLock(QDir(m_root).filePath(QStringLiteral(".store.lock")));
    if (!acquireLock(storeLock, error)) return 0;
    if (nowMs <= 0) nowMs = QDateTime::currentMSecsSinceEpoch();
    int recovered = 0;
    const QFileInfoList files = QDir(QDir(m_root).filePath(QStringLiteral("runs")))
        .entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo &file : files) {
        AgentRunRecord value = AgentRunRecord::fromJson(loadObject(file.absoluteFilePath()));
        if (value.runId.isEmpty() || value.leaseExpiresAt <= 0 || value.leaseExpiresAt > nowMs
            || (value.status != QLatin1String("running")
                && value.status != QLatin1String("waiting_approval")
                && value.status != QLatin1String("waiting_user")))
            continue;
        value.status = QStringLiteral("uncertain");
        value.detail = QStringLiteral("lease expirado; no se reejecutan efectos automáticamente");
        value.ownerId.clear();
        value.leaseToken.clear();
        value.leaseExpiresAt = 0;
        value.finishedAt = 0;
        if (!appendEvent(value, QStringLiteral("run.recovered_uncertain"),
                         QJsonObject{{QStringLiteral("reason"), QStringLiteral("lease_expired")}},
                         error))
            continue;
        ++recovered;
    }
    return recovered;
}

bool AgentRunStore::resolveUncertain(const QString &runId, const QString &status,
                                     const QString &detail, QString *error)
{
    const QString normalized = status.trimmed().toLower();
    if (normalized != QLatin1String("cancelled")
        && normalized != QLatin1String("failed")) {
        if (error) *error = QStringLiteral("una corrida uncertain sólo puede cerrarse como failed o cancelled");
        return false;
    }
    QLockFile storeLock(QDir(m_root).filePath(QStringLiteral(".store.lock")));
    if (!acquireLock(storeLock, error)) return false;
    AgentRunRecord value = recordUnlocked(runId);
    if (value.runId.isEmpty() || value.status != QLatin1String("uncertain")) {
        if (error) *error = QStringLiteral("la corrida no está uncertain");
        return false;
    }
    value.status = normalized;
    value.detail = detail.trimmed().left(4096);
    value.finishedAt = QDateTime::currentMSecsSinceEpoch();
    value.ownerId.clear();
    value.leaseToken.clear();
    value.leaseExpiresAt = 0;
    return appendEvent(value, QStringLiteral("run.uncertain_resolved"),
                       QJsonObject{{QStringLiteral("status"), normalized},
                                   {QStringLiteral("detail"), value.detail},
                                   {QStringLiteral("humanDecision"), true}}, error);
}

bool AgentRunStore::mergeTerminalMetadata(const QString &runId,
                                           const QJsonObject &metadata,
                                           QString *error)
{
    if (metadata.isEmpty()) return true;
    QLockFile storeLock(QDir(m_root).filePath(QStringLiteral(".store.lock")));
    if (!acquireLock(storeLock, error)) return false;
    AgentRunRecord value = recordUnlocked(runId);
    if (value.runId.isEmpty() || !value.isTerminal()) {
        if (error) *error = QStringLiteral("só se puede enriquecer una corrida terminal");
        return false;
    }
    for (auto it = metadata.begin(); it != metadata.end(); ++it)
        value.metadata[it.key()] = it.value();
    return appendEvent(value, QStringLiteral("run.metadata_merged"),
                       QJsonObject{{QStringLiteral("metadata"), metadata}}, error);
}

QJsonArray AgentRunStore::pending(int limit) const
{
    QLockFile storeLock(QDir(m_root).filePath(QStringLiteral(".store.lock")));
    if (!acquireLock(storeLock)) return {};
    QJsonArray out;
    if (m_root.isEmpty()) return out;
    limit = qBound(1, limit, 500);
    QList<AgentRunRecord> rows;
    const QFileInfoList files = QDir(QDir(m_root).filePath(QStringLiteral("runs")))
        .entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    for (const QFileInfo &file : files) {
        const AgentRunRecord value = AgentRunRecord::fromJson(loadObject(file.absoluteFilePath()));
        if (value.runId.isEmpty() || !isPending(value.status)) continue;
        rows.append(value);
    }
    std::sort(rows.begin(), rows.end(), [](const AgentRunRecord &a, const AgentRunRecord &b) {
        return a.updatedAt > b.updatedAt;
    });
    for (int i = 0; i < rows.size() && i < limit; ++i)
        out.append(rows.at(i).toJson(false));
    return out;
}

QJsonArray AgentRunStore::all(int limit) const
{
    QLockFile storeLock(QDir(m_root).filePath(QStringLiteral(".store.lock")));
    if (!acquireLock(storeLock)) return {};
    QJsonArray out;
    if (m_root.isEmpty()) return out;
    limit = qBound(1, limit, 500);
    QList<AgentRunRecord> rows;
    const QFileInfoList files = QDir(QDir(m_root).filePath(QStringLiteral("runs")))
        .entryInfoList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    for (const QFileInfo &file : files) {
        const AgentRunRecord value = AgentRunRecord::fromJson(loadObject(file.absoluteFilePath()));
        if (value.runId.isEmpty()) continue;
        rows.append(value);
    }
    std::sort(rows.begin(), rows.end(), [](const AgentRunRecord &a, const AgentRunRecord &b) {
        return a.updatedAt > b.updatedAt;
    });
    for (int i = 0; i < rows.size() && i < limit; ++i)
        out.append(rows.at(i).toJson(false));
    return out;
}
