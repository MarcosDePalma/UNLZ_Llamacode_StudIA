#include "AgentDeliverableStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {

constexpr qint64 kMaxSnapshotFiles = 50000;
constexpr qint64 kMaxStoredFileBytes = 16 * 1024 * 1024;
constexpr qint64 kMaxStoredRunBytes = 128 * 1024 * 1024;

QString safeRunId(const QString &value)
{
    const QString id = value.trimmed();
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9._-]{1,128}$"));
    return valid.match(id).hasMatch() ? id : QString();
}

QString cleanWorkspace(const QString &workspace)
{
    const QString absolute = QFileInfo(workspace).absoluteFilePath();
    const QString canonical = QFileInfo(absolute).canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? absolute : canonical);
}

bool isInside(const QString &root, const QString &path, QString *relative = nullptr)
{
    const QString rel = QDir(root).relativeFilePath(path);
    if (rel.isEmpty() || QDir::isAbsolutePath(rel) || rel == QLatin1String("..")
        || rel.startsWith(QStringLiteral("../")))
        return false;
    if (relative) *relative = QDir::fromNativeSeparators(rel);
    return true;
}

bool skipRelativePath(const QString &relative)
{
    const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return true;
    // Metadata is not a deliverable and may contain credentials or enormous
    // object databases. These names are project/runtime boundaries, not app UI
    // heuristics.
    return parts.first() == QLatin1String(".git")
        || parts.first() == QLatin1String(".llamacode");
}

QString hashFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && !file.atEnd()) {
            if (error) *error = file.errorString();
            return {};
        }
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool writeJson(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool copyAtomic(const QString &source, const QString &destination, bool overwrite,
                QString *error)
{
    if (!overwrite && QFile::exists(destination)) {
        if (error) *error = QStringLiteral("el destino ya existe");
        return false;
    }
    QDir().mkpath(QFileInfo(destination).absolutePath());
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) {
        if (error) *error = input.errorString();
        return false;
    }
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) *error = output.errorString();
        return false;
    }
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(1024 * 1024);
        if (chunk.isEmpty() && !input.atEnd()) {
            if (error) *error = input.errorString();
            return false;
        }
        if (output.write(chunk) != chunk.size()) {
            if (error) *error = output.errorString();
            return false;
        }
    }
    if (!output.commit()) {
        if (error) *error = output.errorString();
        return false;
    }
    return true;
}

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    return parseError.error == QJsonParseError::NoError && doc.isObject()
        ? doc.object() : QJsonObject{};
}

QString runDir(const QString &runId)
{
    return QDir(AgentDeliverableStore::rootDir()).filePath(safeRunId(runId));
}

} // namespace

QString AgentDeliverableStore::rootDir()
{
    const QByteArray overridePath = qgetenv("LLAMACODE_DELIVERABLES_DIR");
    const QString dir = overridePath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
              + QStringLiteral("/agent_deliverables")
        : QString::fromLocal8Bit(overridePath);
    QDir().mkpath(dir);
    return QDir::cleanPath(dir);
}

QJsonObject AgentDeliverableStore::snapshot(const QString &workspace, QString *error)
{
    const QString root = cleanWorkspace(workspace);
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        if (error) *error = QStringLiteral("workspace inválido");
        return {};
    }

    QJsonObject files;
    bool truncated = false;
    QDirIterator iterator(root, QDir::Files | QDir::NoDotAndDotDot | QDir::Readable
                              | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    qint64 count = 0;
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QFileInfo info(path);
        QString relative;
        if (info.isSymLink() || !isInside(root, info.absoluteFilePath(), &relative)
            || skipRelativePath(relative))
            continue;
        if (++count > kMaxSnapshotFiles) {
            truncated = true;
            break;
        }
        QString hashError;
        const QString hash = hashFile(info.absoluteFilePath(), &hashError);
        if (hash.isEmpty()) {
            if (error && error->isEmpty())
                *error = QStringLiteral("no se pudo hashear %1: %2").arg(relative, hashError);
            continue;
        }
        files[relative] = QJsonObject{
            {QStringLiteral("hash"), hash},
            {QStringLiteral("size"), static_cast<double>(info.size())},
            {QStringLiteral("mtime"), static_cast<double>(info.lastModified().toMSecsSinceEpoch())}};
    }
    return QJsonObject{
        {QStringLiteral("schemaVersion"), FormatVersion},
        {QStringLiteral("fileCount"), static_cast<double>(files.size())},
        {QStringLiteral("truncated"), truncated},
        {QStringLiteral("files"), files}};
}

QJsonObject AgentDeliverableStore::capture(const QString &runId, const QString &workspace,
                                           const QJsonObject &beforeSnapshot,
                                           QString *error)
{
    const QString id = safeRunId(runId);
    if (id.isEmpty()) {
        if (error) *error = QStringLiteral("runId inválido");
        return {};
    }
    const QString dir = runDir(id);
    QDir().mkpath(dir);
    QLockFile runLock(QDir(dir).filePath(QStringLiteral(".capture.lock")));
    runLock.setStaleLockTime(30000);
    if (!runLock.tryLock(5000)) {
        if (error) *error = QStringLiteral("captura ocupada por otra sesión");
        return {};
    }
    const QString manifestPath = QDir(dir).filePath(QStringLiteral("manifest.json"));
    if (QFile::exists(manifestPath)) return readJson(manifestPath);

    QString snapshotError;
    const QJsonObject afterSnapshot = snapshot(workspace, &snapshotError);
    if (afterSnapshot.isEmpty()) {
        if (error) *error = snapshotError.isEmpty() ? QStringLiteral("snapshot vacío") : snapshotError;
        return {};
    }
    const QJsonObject beforeFiles = beforeSnapshot.value(QStringLiteral("files")).toObject();
    const QJsonObject afterFiles = afterSnapshot.value(QStringLiteral("files")).toObject();
    QSet<QString> paths;
    for (const QString &path : beforeFiles.keys()) paths.insert(path);
    for (const QString &path : afterFiles.keys()) paths.insert(path);
    QStringList sorted = paths.values();
    std::sort(sorted.begin(), sorted.end());

    QJsonArray entries;
    qint64 storedBytes = 0;
    for (const QString &relative : sorted) {
        const QJsonObject before = beforeFiles.value(relative).toObject();
        const QJsonObject after = afterFiles.value(relative).toObject();
        const QString beforeHash = before.value(QStringLiteral("hash")).toString();
        const QString afterHash = after.value(QStringLiteral("hash")).toString();
        if (beforeHash == afterHash) continue;

        QJsonObject entry{{QStringLiteral("path"), relative},
                          {QStringLiteral("status"), before.isEmpty()
                              ? QStringLiteral("created")
                              : after.isEmpty() ? QStringLiteral("deleted")
                                                : QStringLiteral("modified")},
                          {QStringLiteral("beforeHash"), beforeHash},
                          {QStringLiteral("afterHash"), afterHash},
                          {QStringLiteral("size"), after.value(QStringLiteral("size"))}};
        if (!after.isEmpty()) {
            const qint64 size = static_cast<qint64>(after.value(QStringLiteral("size")).toDouble());
            const QString workspaceRoot = cleanWorkspace(workspace);
            const QString source = QDir(workspaceRoot).filePath(relative);
            const QString stored = QDir(dir).filePath(QStringLiteral("files/%1").arg(relative));
            const QFileInfo sourceInfo(source);
            QString sourceRelative;
            const QString canonicalSource = sourceInfo.canonicalFilePath();
            const bool sourceIsSafe = !sourceInfo.isSymLink()
                && !canonicalSource.isEmpty()
                && isInside(workspaceRoot, canonicalSource, &sourceRelative)
                && sourceRelative == relative;
            if (!sourceIsSafe) {
                entry[QStringLiteral("stored")] = false;
                entry[QStringLiteral("copyError")] = QStringLiteral(
                    "la ruta de origen dejó de estar dentro del workspace");
            } else if (size > kMaxStoredFileBytes || storedBytes + size > kMaxStoredRunBytes) {
                entry[QStringLiteral("stored")] = false;
                entry[QStringLiteral("copyError")] = QStringLiteral("límite de tamaño de entregables");
            } else {
                QString copyError;
                if (copyAtomic(source, stored, false, &copyError)) {
                    storedBytes += size;
                    entry[QStringLiteral("stored")] = true;
                    entry[QStringLiteral("storedPath")] = QDir(dir).relativeFilePath(stored);
                } else {
                    entry[QStringLiteral("stored")] = false;
                    entry[QStringLiteral("copyError")] = copyError;
                    if (error && error->isEmpty()) *error = copyError;
                }
            }
        }
        entries.append(entry);
    }

    const QJsonObject result{
        {QStringLiteral("schemaVersion"), FormatVersion},
        {QStringLiteral("runId"), id},
        {QStringLiteral("workspace"), cleanWorkspace(workspace)},
        {QStringLiteral("capturedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("beforeTruncated"), beforeSnapshot.value(QStringLiteral("truncated")).toBool()},
        {QStringLiteral("afterTruncated"), afterSnapshot.value(QStringLiteral("truncated")).toBool()},
        {QStringLiteral("entries"), entries},
        {QStringLiteral("changedCount"), entries.size()},
        {QStringLiteral("storedBytes"), static_cast<double>(storedBytes)}};
    if (!writeJson(manifestPath, result, error)) return {};

    const QString storeRoot = rootDir();
    QLockFile indexLock(QDir(storeRoot).filePath(QStringLiteral(".index.lock")));
    indexLock.setStaleLockTime(30000);
    if (indexLock.tryLock(5000)) {
        QFile index(QDir(storeRoot).filePath(QStringLiteral("index.jsonl")));
        if (index.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            index.write(QJsonDocument(QJsonObject{
                {QStringLiteral("runId"), id},
                {QStringLiteral("capturedAt"), result.value(QStringLiteral("capturedAt"))},
                {QStringLiteral("workspace"), result.value(QStringLiteral("workspace"))},
                {QStringLiteral("changedCount"), result.value(QStringLiteral("changedCount"))},
                {QStringLiteral("manifest"), QDir(storeRoot).relativeFilePath(manifestPath)}})
                             .toJson(QJsonDocument::Compact));
            index.write("\n");
            index.flush();
        }
    } else if (error && error->isEmpty()) {
        *error = QStringLiteral("manifiesto guardado pero no se pudo actualizar el índice global");
    }
    return result;
}

QJsonObject AgentDeliverableStore::manifest(const QString &runId)
{
    if (safeRunId(runId).isEmpty()) return {};
    return readJson(QDir(runDir(runId)).filePath(QStringLiteral("manifest.json")));
}

bool AgentDeliverableStore::remove(const QString &runId)
{
    const QString dir = runDir(runId);
    return !dir.isEmpty() && QDir(dir).removeRecursively();
}

bool AgentDeliverableStore::saveAs(const QString &runId, const QString &relativePath,
                                   const QString &destination, bool overwrite, QString *error)
{
    if (destination.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("destino vacío");
        return false;
    }
    const QJsonObject m = manifest(runId);
    if (m.isEmpty()) {
        if (error) *error = QStringLiteral("manifiesto de entregables inexistente");
        return false;
    }
    const QString relative = QDir::fromNativeSeparators(relativePath.trimmed());
    QJsonObject selected;
    for (const QJsonValue &value : m.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject entry = value.toObject();
        if (entry.value(QStringLiteral("path")).toString() == relative) {
            selected = entry;
            break;
        }
    }
    const QString storedRel = selected.value(QStringLiteral("storedPath")).toString();
    if (selected.isEmpty() || !selected.value(QStringLiteral("stored")).toBool() || storedRel.isEmpty()) {
        if (error) *error = QStringLiteral("el archivo no tiene una copia restaurable");
        return false;
    }
    const QString source = QDir(runDir(runId)).filePath(storedRel);
    if (!QFileInfo::exists(source)) {
        if (error) *error = QStringLiteral("la copia del entregable no existe");
        return false;
    }
    const QString runRoot = QFileInfo(runDir(runId)).canonicalFilePath();
    const QString sourceCanonical = QFileInfo(source).canonicalFilePath();
    QString sourceRelative;
    if (runRoot.isEmpty() || sourceCanonical.isEmpty()
        || !isInside(runRoot, sourceCanonical, &sourceRelative)) {
        if (error) *error = QStringLiteral("la copia del entregable sale de su corrida");
        return false;
    }
    return copyAtomic(sourceCanonical, QFileInfo(destination).absoluteFilePath(), overwrite, error);
}
