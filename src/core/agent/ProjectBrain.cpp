#include "ProjectBrain.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace {
QByteArray contentHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result().toHex();
}
}

QString ProjectBrain::cachePath(const QString &root)
{
    const QString key = QString::fromLatin1(QCryptographicHash::hash(
        QDir(root).absolutePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/project_brain");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + key + QStringLiteral(".json");
}

QVariantMap ProjectBrain::load(const QString &root)
{
    QFile file(cachePath(root));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

QStringList ProjectBrain::changedPaths(const QVariantMap &before, const QVariantMap &after)
{
    QHash<QString, QString> oldHashes;
    for (const QVariant &value : before.value(QStringLiteral("files")).toList()) {
        const QVariantMap item = value.toMap();
        const QString path = QDir::fromNativeSeparators(
            item.value(QStringLiteral("path")).toString());
        if (!path.isEmpty()) oldHashes.insert(path, item.value(QStringLiteral("sha256")).toString());
    }

    QSet<QString> changed;
    for (const QVariant &value : after.value(QStringLiteral("files")).toList()) {
        const QVariantMap item = value.toMap();
        const QString path = QDir::fromNativeSeparators(
            item.value(QStringLiteral("path")).toString());
        if (path.isEmpty()) continue;
        const QString hash = item.value(QStringLiteral("sha256")).toString();
        if (!oldHashes.contains(path) || oldHashes.value(path) != hash)
            changed.insert(path);
        oldHashes.remove(path);
    }
    for (auto it = oldHashes.cbegin(); it != oldHashes.cend(); ++it)
        changed.insert(it.key());

    QStringList result = changed.values();
    result.sort(Qt::CaseInsensitive);
    return result;
}

QVariantMap ProjectBrain::refresh(const QString &root, int maxFiles)
{
    const QDir base(root);
    if (!base.exists()) return {{QStringLiteral("error"), QStringLiteral("workspace inexistente")}};
    const int effectiveMax = qBound(1, maxFiles, 20000);
    static const QSet<QString> ignored{QStringLiteral(".git"), QStringLiteral("build"),
        QStringLiteral("build_tests"), QStringLiteral("node_modules"), QStringLiteral("dist"),
        QStringLiteral("target"), QStringLiteral(".venv"), QStringLiteral("venv")};
    QVariantList files;
    QVariantMap extensions;
    qint64 bytes = 0;
    const QVariantMap previous = load(root);
    QHash<QString, QVariantMap> previousFiles;
    for (const QVariant &value : previous.value(QStringLiteral("files")).toList()) {
        const QVariantMap item = value.toMap();
        previousFiles.insert(item.value(QStringLiteral("path")).toString(), item);
    }
    int added = 0, updated = 0, reused = 0;
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && files.size() < effectiveMax) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const QString rel = base.relativeFilePath(info.absoluteFilePath());
        bool skip = false;
        for (const QString &part : rel.split(QLatin1Char('/')))
            if (ignored.contains(part)) { skip = true; break; }
        if (skip || info.size() > 4 * 1024 * 1024) continue;
        const QString ext = info.suffix().toLower();
        extensions[ext.isEmpty() ? QStringLiteral("(none)") : ext]
            = extensions.value(ext.isEmpty() ? QStringLiteral("(none)") : ext).toInt() + 1;
        bytes += info.size();
        const qint64 modifiedMs = info.lastModified().toMSecsSinceEpoch();
        const QVariantMap old = previousFiles.take(rel);
        QByteArray hash;
        if (!old.isEmpty() && old.value(QStringLiteral("bytes")).toLongLong() == info.size()
            && old.value(QStringLiteral("modifiedMs")).toLongLong() == modifiedMs
            && !old.value(QStringLiteral("sha256")).toString().isEmpty()) {
            hash = old.value(QStringLiteral("sha256")).toString().toLatin1();
            ++reused;
        } else {
            hash = contentHash(info.absoluteFilePath());
            old.isEmpty() ? ++added : ++updated;
        }
        files.append(QVariantMap{{QStringLiteral("path"), rel},
            {QStringLiteral("bytes"), info.size()},
            {QStringLiteral("modifiedMs"), modifiedMs},
            {QStringLiteral("sha256"), QString::fromLatin1(hash)}});
    }
    const int removed = previousFiles.size();
    QVariantMap brain{{QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("root"), base.absolutePath()},
        {QStringLiteral("refreshedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("fileCount"), files.size()}, {QStringLiteral("bytes"), bytes},
        {QStringLiteral("extensions"), extensions}, {QStringLiteral("files"), files},
        {QStringLiteral("changes"), QVariantMap{{QStringLiteral("added"), added},
            {QStringLiteral("updated"), updated}, {QStringLiteral("removed"), removed},
            {QStringLiteral("reused"), reused}}},
        {QStringLiteral("truncated"), files.size() >= effectiveMax}};
    QSaveFile file(cachePath(root));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject::fromVariantMap(brain)).toJson(QJsonDocument::Compact));
        file.commit();
    }
    return brain;
}

QVariantMap ProjectBrain::update(const QString &root, const QStringList &changedPaths, int maxFiles)
{
    const QDir base(root);
    QVariantMap previous = load(root);
    if (!base.exists() || previous.value(QStringLiteral("schemaVersion")).toInt() < 2)
        return refresh(root, maxFiles);
    QHash<QString, QVariantMap> entries;
    for (const QVariant &value : previous.value(QStringLiteral("files")).toList()) {
        const QVariantMap item = value.toMap();
        entries.insert(item.value(QStringLiteral("path")).toString(), item);
    }
    int added = 0, updated = 0, removed = 0;
    auto removePrefix = [&](const QString &prefix) {
        const QString slash = prefix.isEmpty() ? QString() : prefix + QLatin1Char('/');
        for (auto it = entries.begin(); it != entries.end();) {
            if (it.key() == prefix || (!slash.isEmpty() && it.key().startsWith(slash))) {
                it = entries.erase(it); ++removed;
            } else ++it;
        }
    };
    auto addFile = [&](const QFileInfo &info) {
        if (!info.exists() || !info.isFile() || info.size() > 4 * 1024 * 1024) return;
        const QString rel = QDir::fromNativeSeparators(base.relativeFilePath(info.absoluteFilePath()));
        const bool existed = entries.contains(rel);
        entries[rel] = QVariantMap{{QStringLiteral("path"), rel}, {QStringLiteral("bytes"), info.size()},
            {QStringLiteral("modifiedMs"), info.lastModified().toMSecsSinceEpoch()},
            {QStringLiteral("sha256"), QString::fromLatin1(contentHash(info.absoluteFilePath()))}};
        existed ? ++updated : ++added;
    };
    for (const QString &path : changedPaths) {
        const QString abs = QFileInfo(path).isAbsolute() ? QFileInfo(path).absoluteFilePath()
                                                         : base.absoluteFilePath(path);
        const QFileInfo info(abs);
        const QString rel = QDir::fromNativeSeparators(base.relativeFilePath(abs));
        if (rel == QLatin1String("..") || rel.startsWith(QStringLiteral("../"))) continue;
        if (!info.exists()) { removePrefix(rel); continue; }
        if (info.isFile()) {
            const bool existed = entries.contains(rel);
            removePrefix(rel); addFile(info);
            if (existed) { --added; --removed; ++updated; }
            continue;
        }
        removePrefix(rel);
        QDirIterator it(abs, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext() && entries.size() < qBound(1, maxFiles, 20000)) {
            it.next(); addFile(it.fileInfo());
        }
    }
    QStringList keys = entries.keys();
    keys.sort(Qt::CaseInsensitive);
    QVariantList files; QVariantMap extensions; qint64 bytes = 0;
    const int limit = qBound(1, maxFiles, 20000);
    for (const QString &key : keys.mid(0, limit)) {
        const QVariantMap item = entries.value(key);
        files.append(item); bytes += item.value(QStringLiteral("bytes")).toLongLong();
        const QString ext = QFileInfo(key).suffix().toLower();
        const QString extKey = ext.isEmpty() ? QStringLiteral("(none)") : ext;
        extensions[extKey] = extensions.value(extKey).toInt() + 1;
    }
    QVariantMap brain{{QStringLiteral("schemaVersion"), 2}, {QStringLiteral("root"), base.absolutePath()},
        {QStringLiteral("refreshedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("scanMode"), QStringLiteral("events")},
        {QStringLiteral("fileCount"), files.size()}, {QStringLiteral("bytes"), bytes},
        {QStringLiteral("extensions"), extensions}, {QStringLiteral("files"), files},
        {QStringLiteral("changes"), QVariantMap{{QStringLiteral("added"), added},
            {QStringLiteral("updated"), updated}, {QStringLiteral("removed"), removed},
            {QStringLiteral("reused"), qMax(0, entries.size() - added - updated)}}},
        {QStringLiteral("truncated"), entries.size() > limit}};
    QSaveFile file(cachePath(root));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject::fromVariantMap(brain)).toJson(QJsonDocument::Compact));
        file.commit();
    }
    return brain;
}
