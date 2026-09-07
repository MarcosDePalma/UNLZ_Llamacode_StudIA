#include "WorkRegistry.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <utility>

namespace {

qint64 currentMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString lockPath(const QString &root)
{
    return WorkRegistry::storePath(root) + QStringLiteral(".lock");
}

bool openLock(const QString &root, QLockFile &lock)
{
    const QString path = WorkRegistry::storePath(root);
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    lock.setStaleLockTime(30000);
    return lock.tryLock(1000);
}

bool loadClaims(const QString &path, QJsonArray *claims)
{
    if (!claims) return false;
    *claims = QJsonArray{};
    QFile file(path);
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
    *claims = doc.object().value(QStringLiteral("claims")).toArray();
    return true;
}

bool saveClaims(const QString &path, const QJsonArray &claims)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    const QJsonObject root{{QStringLiteral("schemaVersion"), 1},
                           {QStringLiteral("claims"), claims}};
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

QStringList normalizePaths(const QString &root, const QStringList &paths)
{
    QStringList out;
    const QDir base(root);
    for (QString raw : paths) {
        raw = raw.trimmed();
        if (raw.isEmpty()) continue;
        raw.replace(QLatin1Char('\\'), QLatin1Char('/'));
        QString rel = QDir::isAbsolutePath(raw)
            ? base.relativeFilePath(QDir::cleanPath(raw))
            : QDir::cleanPath(raw);
        rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
        while (rel.startsWith(QStringLiteral("./"))) rel.remove(0, 2);
        if (rel.isEmpty() || rel == QLatin1String("..")
            || rel.startsWith(QStringLiteral("../")))
            continue;
        if (!out.contains(rel)) out << rel;
    }
    return out;
}

bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

bool pathMatches(const QString &claimed, const QString &requested)
{
    // `.` es una claim de workspace completo. Permite que un proceso externo
    // que edita el proyecto reserve el mismo ámbito que una lista de archivos
    // descubierta por un agente nativo.
    if (claimed == QLatin1String(".") || requested == QLatin1String(".")) return true;
    if (samePath(claimed, requested)) return true;
    const QString c = claimed.endsWith(QLatin1Char('/')) ? claimed
                                                           : claimed + QLatin1Char('/');
    const QString r = requested.endsWith(QLatin1Char('/')) ? requested
                                                             : requested + QLatin1Char('/');
    return r.startsWith(c, 
#ifdef Q_OS_WIN
                        Qt::CaseInsensitive
#else
                        Qt::CaseSensitive
#endif
    ) || c.startsWith(r,
#ifdef Q_OS_WIN
                      Qt::CaseInsensitive
#else
                      Qt::CaseSensitive
#endif
    );
}

QString formatConflictRows(const QJsonArray &rows, int maxClaims = 5)
{
    if (rows.isEmpty()) return {};
    QStringList lines{QStringLiteral("[work_conflict: otra sesión reclama rutas solapadas]")};
    const int limit = qBound(1, maxClaims, 20);
    for (int i = 0; i < rows.size() && i < limit; ++i) {
        const QJsonObject claim = rows.at(i).toObject();
        QStringList claimed;
        for (const QJsonValue &value : claim.value(QStringLiteral("paths")).toArray())
            claimed << value.toString();
        lines << QStringLiteral("- %1 (%2): %3")
                     .arg(claim.value(QStringLiteral("agentId")).toString(),
                          claim.value(QStringLiteral("sessionId")).toString().left(8),
                          claimed.join(", "));
    }
    if (rows.size() > limit)
        lines << QStringLiteral("- … %1 conflicto(s) adicional(es) omitido(s)")
                     .arg(rows.size() - limit);
    return lines.join(QLatin1Char('\n'));
}

bool isActive(const QJsonObject &claim, qint64 now)
{
    if (claim.value(QStringLiteral("status")).toString() != QLatin1String("active"))
        return false;
    return claim.value(QStringLiteral("expiresAt")).toVariant().toLongLong() > now;
}

bool pruneExpired(QJsonArray *claims, qint64 now)
{
    if (!claims) return false;
    QJsonArray kept;
    bool changed = false;
    for (const QJsonValue &value : std::as_const(*claims)) {
        const QJsonObject claim = value.toObject();
        if (isActive(claim, now)) kept.append(claim);
        else changed = true;
    }
    if (changed) *claims = kept;
    return changed;
}

QJsonObject withExpiry(QJsonObject claim, qint64 now, int ttlSec)
{
    claim[QStringLiteral("heartbeatAt")] = now;
    claim[QStringLiteral("expiresAt")] = now + qBound(30, ttlSec, 24 * 60 * 60) * 1000LL;
    return claim;
}

} // namespace

namespace WorkRegistry {

QString storePath(const QString &root)
{
    if (root.trimmed().isEmpty()) return {};
    return QDir::cleanPath(root + QStringLiteral("/.llamacode/active_work.json"));
}

QString acquire(const QString &root, const QString &sessionId,
                const QString &agentId, const QString &goal,
                const QStringList &paths, const QString &branch,
                const QString &worktree, int ttlSec)
{
    if (storePath(root).isEmpty() || sessionId.trimmed().isEmpty()) return {};
    QLockFile lock{lockPath(root)};
    if (!openLock(root, lock)) return {};
    QJsonArray claims;
    const QString path = storePath(root);
    if (!loadClaims(path, &claims)) return {};
    const qint64 now = currentMs();
    pruneExpired(&claims, now);
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject claim{
        {QStringLiteral("id"), id},
        {QStringLiteral("sessionId"), sessionId.trimmed()},
        {QStringLiteral("agentId"), agentId.trimmed().isEmpty()
             ? QStringLiteral("llamacode-agent") : agentId.trimmed()},
        {QStringLiteral("goal"), goal.trimmed().left(4096)},
        {QStringLiteral("paths"), QJsonArray::fromStringList(normalizePaths(root, paths))},
        {QStringLiteral("branch"), branch.trimmed()},
        {QStringLiteral("worktree"), worktree.trimmed()},
        {QStringLiteral("status"), QStringLiteral("active")},
        {QStringLiteral("createdAt"), now}};
    claims.append(withExpiry(claim, now, ttlSec));
    if (!saveClaims(path, claims)) return {};
    return id;
}

bool heartbeat(const QString &root, const QString &claimId,
               const QString &sessionId, int ttlSec)
{
    if (claimId.trimmed().isEmpty() || sessionId.trimmed().isEmpty()) return false;
    QLockFile lock{lockPath(root)};
    if (!openLock(root, lock)) return false;
    QJsonArray claims;
    const QString path = storePath(root);
    if (!loadClaims(path, &claims)) return false;
    const qint64 now = currentMs();
    bool changed = pruneExpired(&claims, now);
    for (int i = 0; i < claims.size(); ++i) {
        QJsonObject claim = claims.at(i).toObject();
        if (claim.value(QStringLiteral("id")).toString() != claimId
            || claim.value(QStringLiteral("sessionId")).toString() != sessionId)
            continue;
        claims.replace(i, withExpiry(claim, now, ttlSec));
        changed = true;
        break;
    }
    return changed && saveClaims(path, claims);
}

bool addPaths(const QString &root, const QString &claimId,
              const QString &sessionId, const QStringList &paths, int ttlSec)
{
    return claimPaths(root, claimId, sessionId, paths, nullptr, ttlSec);
}

bool claimPaths(const QString &root, const QString &claimId,
                const QString &sessionId, const QStringList &paths,
                QString *conflictMessage, int ttlSec)
{
    if (conflictMessage) conflictMessage->clear();
    if (claimId.trimmed().isEmpty() || sessionId.trimmed().isEmpty()) return false;
    QLockFile lock{lockPath(root)};
    if (!openLock(root, lock)) return false;
    QJsonArray claims;
    const QString path = storePath(root);
    if (!loadClaims(path, &claims)) return false;
    const qint64 now = currentMs();
    bool changed = pruneExpired(&claims, now);
    const QStringList normalized = normalizePaths(root, paths);
    QJsonArray conflicts;
    for (int i = 0; i < claims.size(); ++i) {
        QJsonObject claim = claims.at(i).toObject();
        const QString id = claim.value(QStringLiteral("id")).toString();
        const QString owner = claim.value(QStringLiteral("sessionId")).toString();
        if (id == claimId && owner != sessionId) return false;
        if (id != claimId || owner != sessionId)
            continue;
        for (const QJsonValue &otherValue : std::as_const(claims)) {
            const QJsonObject other = otherValue.toObject();
            if (other.value(QStringLiteral("id")).toString() == claimId
                || other.value(QStringLiteral("sessionId")).toString() == sessionId
                || !isActive(other, now))
                continue;
            bool match = false;
            for (const QJsonValue &claimed : other.value(QStringLiteral("paths")).toArray()) {
                for (const QString &requested : normalized) {
                    if (pathMatches(claimed.toString(), requested)) {
                        match = true;
                        break;
                    }
                }
                if (match) break;
            }
            if (match) conflicts.append(other);
        }
        if (!conflicts.isEmpty()) {
            if (conflictMessage) *conflictMessage = formatConflictRows(conflicts);
            return false;
        }
        QStringList merged;
        for (const QJsonValue &value : claim.value(QStringLiteral("paths")).toArray())
            merged << value.toString();
        for (const QString &item : normalized)
            if (!merged.contains(item)) merged << item;
        claim[QStringLiteral("paths")] = QJsonArray::fromStringList(merged);
        claims.replace(i, withExpiry(claim, now, ttlSec));
        changed = true;
        break;
    }
    return changed && saveClaims(path, claims);
}

bool release(const QString &root, const QString &claimId,
             const QString &sessionId, const QString &status)
{
    if (claimId.trimmed().isEmpty() || sessionId.trimmed().isEmpty()) return false;
    QLockFile lock{lockPath(root)};
    if (!openLock(root, lock)) return false;
    QJsonArray claims;
    const QString path = storePath(root);
    if (!loadClaims(path, &claims)) return false;
    QJsonArray kept;
    bool found = false;
    for (const QJsonValue &value : std::as_const(claims)) {
        const QJsonObject claim = value.toObject();
        if (claim.value(QStringLiteral("id")).toString() == claimId
            && claim.value(QStringLiteral("sessionId")).toString() == sessionId) {
            found = true;
            continue;
        }
        kept.append(claim);
    }
    Q_UNUSED(status); // El historial completo vive en AgentEventLog.
    return found && saveClaims(path, kept);
}

QJsonArray active(const QString &root, const QString &excludeSessionId, qint64 nowMs)
{
    QLockFile lock{lockPath(root)};
    if (!openLock(root, lock)) return {};
    QJsonArray claims;
    const QString path = storePath(root);
    if (!loadClaims(path, &claims)) return {};
    const qint64 now = nowMs > 0 ? nowMs : currentMs();
    const bool changed = pruneExpired(&claims, now);
    if (changed) saveClaims(path, claims);
    QJsonArray result;
    for (const QJsonValue &value : std::as_const(claims)) {
        const QJsonObject claim = value.toObject();
        if (!isActive(claim, now)) continue;
        if (!excludeSessionId.isEmpty()
            && claim.value(QStringLiteral("sessionId")).toString() == excludeSessionId)
            continue;
        result.append(claim);
    }
    return result;
}

QJsonArray conflicts(const QString &root, const QString &sessionId,
                     const QStringList &paths, qint64 nowMs)
{
    const QStringList normalized = normalizePaths(root, paths);
    if (normalized.isEmpty()) return {};
    QJsonArray result;
    for (const QJsonValue &value : active(root, sessionId, nowMs)) {
        const QJsonObject claim = value.toObject();
        bool match = false;
        for (const QJsonValue &claimed : claim.value(QStringLiteral("paths")).toArray()) {
            for (const QString &requested : normalized) {
                if (pathMatches(claimed.toString(), requested)) {
                    match = true;
                    break;
                }
            }
            if (match) break;
        }
        if (match) result.append(claim);
    }
    return result;
}

QString formatActive(const QString &root, const QString &excludeSessionId, int maxClaims)
{
    const QJsonArray rows = active(root, excludeSessionId);
    if (rows.isEmpty()) return QStringLiteral("[active_work: sin claims activos]");
    QStringList lines{QStringLiteral("[active_work · estado del proyecto]")};
    const int limit = qBound(1, maxClaims, 50);
    for (int i = 0; i < rows.size() && i < limit; ++i) {
        const QJsonObject claim = rows.at(i).toObject();
        QStringList paths;
        for (const QJsonValue &value : claim.value(QStringLiteral("paths")).toArray())
            paths << value.toString();
        const QString owner = claim.value(QStringLiteral("agentId")).toString()
            + QLatin1Char('/') + claim.value(QStringLiteral("sessionId")).toString().left(8);
        QString line = QStringLiteral("- %1: %2").arg(owner,
            claim.value(QStringLiteral("goal")).toString().left(240));
        if (!paths.isEmpty()) line += QStringLiteral(" [paths: %1]").arg(paths.join(", "));
        line += QStringLiteral(" [claim=%1]").arg(claim.value(QStringLiteral("id")).toString().left(8));
        lines << line;
    }
    if (rows.size() > limit)
        lines << QStringLiteral("- … %1 claim(s) adicional(es) omitida(s) por presupuesto")
                     .arg(rows.size() - limit);
    return lines.join(QLatin1Char('\n'));
}

QString formatConflicts(const QString &root, const QString &sessionId,
                        const QStringList &paths, int maxClaims)
{
    return formatConflictRows(conflicts(root, sessionId, paths), maxClaims);
}

} // namespace WorkRegistry
