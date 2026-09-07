#include "ContextIndex.h"

#include <QAtomicInt>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>

#include <algorithm>

namespace {

QAtomicInt g_connectionCounter;

struct DbHandle {
    QSqlDatabase db;
    QString name;

    ~DbHandle()
    {
        if (db.isValid()) {
            db.close();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(name);
        }
    }
};

QString normalizedRoot(const QString &root)
{
    return QDir(root).absolutePath();
}

QString connectionName()
{
    const quintptr thread = reinterpret_cast<quintptr>(QThread::currentThreadId());
    return QStringLiteral("llamacode_context_%1_%2")
        .arg(QString::number(thread, 16))
        .arg(g_connectionCounter.fetchAndAddRelaxed(1));
}

QString sha256(const QByteArray &data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString sha256File(const QString &path, QByteArray *raw = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = f.read(4 * 1024 * 1024 + 1);
    if (raw) *raw = bytes;
    if (bytes.size() > 4 * 1024 * 1024) return {};
    return sha256(bytes);
}

bool ignoredDir(const QString &name)
{
    static const QSet<QString> ignored{
        QStringLiteral(".git"), QStringLiteral("build"),
        QStringLiteral("build_tests"), QStringLiteral("node_modules"),
        QStringLiteral("dist"), QStringLiteral("target"),
        QStringLiteral(".venv"), QStringLiteral("venv"),
        QStringLiteral("__pycache__"), QStringLiteral(".llamacode"),
        QStringLiteral(".idea"), QStringLiteral(".vs"),
        QStringLiteral("coverage"), QStringLiteral("bin"),
        QStringLiteral("obj")};
    return ignored.contains(name);
}

QString languageFor(const QString &suffix)
{
    const QString e = suffix.toLower();
    if (QStringList{QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"),
                    QStringLiteral("cxx"), QStringLiteral("h"), QStringLiteral("hh"),
                    QStringLiteral("hpp"), QStringLiteral("hxx")}.contains(e)) return QStringLiteral("cpp");
    if (e == QLatin1String("qml")) return QStringLiteral("qml");
    if (QStringList{QStringLiteral("js"), QStringLiteral("jsx"), QStringLiteral("mjs")}.contains(e)) return QStringLiteral("javascript");
    if (QStringList{QStringLiteral("ts"), QStringLiteral("tsx")}.contains(e)) return QStringLiteral("typescript");
    if (e == QLatin1String("py")) return QStringLiteral("python");
    if (e == QLatin1String("java")) return QStringLiteral("java");
    if (e == QLatin1String("rs")) return QStringLiteral("rust");
    if (QStringList{QStringLiteral("cmake"), QStringLiteral("txt"), QStringLiteral("md"),
                    QStringLiteral("json"), QStringLiteral("yaml"), QStringLiteral("yml"),
                    QStringLiteral("toml"), QStringLiteral("ini"), QStringLiteral("xml")}.contains(e))
        return QStringLiteral("text");
    return {};
}

struct SourceFile {
    QString rel;
    QString abs;
    QString language;
    qint64 bytes = 0;
    qint64 modifiedMs = 0;
};

QList<SourceFile> collectFiles(const QString &root, int maxFiles)
{
    QList<SourceFile> result;
    const QDir base(root);
    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext() && result.size() < qBound(1, maxFiles, 20000)) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const QString rel = QDir::fromNativeSeparators(base.relativeFilePath(info.absoluteFilePath()));
        bool skip = info.size() > 4 * 1024 * 1024;
        for (const QString &part : rel.split(QLatin1Char('/'))) {
            if (ignoredDir(part)) { skip = true; break; }
        }
        const QString language = languageFor(info.suffix());
        if (skip || language.isEmpty()) continue;
        result.append({rel, info.absoluteFilePath(), language, info.size(),
                       info.lastModified().toMSecsSinceEpoch()});
    }
    std::sort(result.begin(), result.end(), [](const SourceFile &a, const SourceFile &b) {
        return a.rel < b.rel;
    });
    return result;
}

bool openDb(const QString &root, DbHandle *handle, QString *error = nullptr)
{
    handle->name = connectionName();
    handle->db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), handle->name);
    handle->db.setDatabaseName(ContextIndex::cachePath(root));
    if (!handle->db.open()) {
        if (error) *error = handle->db.lastError().text();
        return false;
    }
    QSqlQuery q(handle->db);
    const QStringList ddl{
        QStringLiteral("PRAGMA journal_mode=WAL"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS files (path TEXT PRIMARY KEY, language TEXT, bytes INTEGER, modified_ms INTEGER, sha256 TEXT)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS chunks (id TEXT PRIMARY KEY, path TEXT, start_line INTEGER, end_line INTEGER, text_hash TEXT, text TEXT, token_est INTEGER)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS edges (source TEXT, target TEXT, type TEXT, reason TEXT, confidence REAL, PRIMARY KEY(source, target, type))"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS chunks_path_idx ON chunks(path)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS edges_source_idx ON edges(source)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS edges_target_idx ON edges(target)")};
    for (const QString &sql : ddl) {
        if (!q.exec(sql)) {
            if (error) *error = q.lastError().text();
            return false;
        }
    }
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO meta(key,value) VALUES('schemaVersion','1')"));
    q.exec();
    return true;
}

QStringList importRefs(const QString &text)
{
    QSet<QString> refs;
    static const QRegularExpression includeRe(QStringLiteral("#\\s*include\\s*[\\\"<]([^\\\">]+)[\\\">]"));
    static const QRegularExpression importRe(QStringLiteral("(?:from|import|require)\\s*\\(?\\s*[\\\"']([^\\\"']+)[\\\"']"));
    static const QRegularExpression qmlRe(QStringLiteral("^\\s*import\\s+([A-Za-z0-9_.]+)"),
                                           QRegularExpression::MultilineOption);
    for (auto it = includeRe.globalMatch(text); it.hasNext();) refs.insert(it.next().captured(1));
    for (auto it = importRe.globalMatch(text); it.hasNext();) refs.insert(it.next().captured(1));
    for (auto it = qmlRe.globalMatch(text); it.hasNext();) refs.insert(it.next().captured(1));
    QStringList result = refs.values();
    for (QString &ref : result) {
        ref = QFileInfo(ref).completeBaseName().toLower();
    }
    return result;
}

QString basename(const QString &path)
{
    return QFileInfo(path).completeBaseName().toLower();
}

QString handleFor(const QString &rel, int start, int end, const QString &fileHash)
{
    const QJsonObject payload{{QStringLiteral("path"), rel},
                              {QStringLiteral("start"), start},
                              {QStringLiteral("end"), end},
                              {QStringLiteral("sha256"), fileHash}};
    return QStringLiteral("ctx:") + QJsonDocument(payload).toJson(QJsonDocument::Compact)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QJsonObject asJson(const QVariantMap &map)
{
    return QJsonObject::fromVariantMap(map);
}

void addEdge(QSqlDatabase db, const QString &source, const QString &target,
             const QString &type, const QString &reason, double confidence)
{
    if (source.isEmpty() || target.isEmpty() || source == target) return;
    QSqlQuery edge(db);
    edge.prepare(QStringLiteral("INSERT OR REPLACE INTO edges(source,target,type,reason,confidence) VALUES(?,?,?,?,?)"));
    edge.addBindValue(source); edge.addBindValue(target); edge.addBindValue(type);
    edge.addBindValue(reason); edge.addBindValue(confidence); edge.exec();
}

} // namespace

namespace ContextIndex {

QString cachePath(const QString &root)
{
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(normalizedRoot(root).toUtf8(), QCryptographicHash::Sha256)
            .toHex().left(24));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/context");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + key + QStringLiteral(".sqlite");
}

QVariantMap status(const QString &root)
{
    DbHandle handle;
    QString error;
    if (!openDb(root, &handle, &error))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("freshness"), QStringLiteral("absent")},
                {QStringLiteral("error"), error}};
    QSqlQuery q(handle.db);
    q.prepare(QStringLiteral("SELECT key,value FROM meta"));
    QVariantMap meta;
    if (q.exec()) while (q.next()) meta.insert(q.value(0).toString(), q.value(1).toString());
    QSqlQuery count(handle.db);
    int files = 0, chunks = 0, edges = 0;
    if (count.exec(QStringLiteral("SELECT COUNT(*) FROM files")) && count.next()) files = count.value(0).toInt();
    if (count.exec(QStringLiteral("SELECT COUNT(*) FROM chunks")) && count.next()) chunks = count.value(0).toInt();
    if (count.exec(QStringLiteral("SELECT COUNT(*) FROM edges")) && count.next()) edges = count.value(0).toInt();
    if (files == 0)
        return {{QStringLiteral("ok"), true}, {QStringLiteral("freshness"), QStringLiteral("absent")},
                {QStringLiteral("files"), files}, {QStringLiteral("chunks"), chunks},
                {QStringLiteral("edges"), edges}, {QStringLiteral("cachePath"), cachePath(root)}};
    QHash<QString, QPair<qint64, qint64>> indexedFiles;
    QSqlQuery liveQuery(handle.db);
    if (liveQuery.exec(QStringLiteral("SELECT path,bytes,modified_ms FROM files"))) {
        while (liveQuery.next()) indexedFiles.insert(liveQuery.value(0).toString(),
            {liveQuery.value(1).toLongLong(), liveQuery.value(2).toLongLong()});
    }
    bool dirty = indexedFiles.size() != files;
    if (!dirty) {
        for (const SourceFile &file : collectFiles(root, 8000)) {
            const auto known = indexedFiles.value(file.rel, QPair<qint64, qint64>{-1, -1});
            if (known.first != file.bytes || known.second != file.modifiedMs) {
                dirty = true;
                break;
            }
        }
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("freshness"), dirty ? QStringLiteral("dirty") : QStringLiteral("clean")},
            {QStringLiteral("files"), files}, {QStringLiteral("chunks"), chunks},
            {QStringLiteral("edges"), edges}, {QStringLiteral("revision"), meta.value(QStringLiteral("revision"))},
            {QStringLiteral("indexedAt"), meta.value(QStringLiteral("indexedAt"))},
            {QStringLiteral("cachePath"), cachePath(root)}};
}

QVariantMap refresh(const QString &root, const QStringList &changedPaths, int maxFiles)
{
    const QDir base(root);
    if (!base.exists()) return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("workspace inexistente")}};
    DbHandle handle;
    QString error;
    if (!openDb(root, &handle, &error))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};

    const QList<SourceFile> files = collectFiles(root, maxFiles);
    QHash<QString, SourceFile> current;
    QHash<QString, QString> byBase;
    for (const SourceFile &file : files) {
        current.insert(file.rel, file);
        if (!byBase.contains(basename(file.rel))) byBase.insert(basename(file.rel), file.rel);
    }

    QSet<QString> filter;
    for (const QString &raw : changedPaths) {
        const QString rel = QDir::fromNativeSeparators(base.relativeFilePath(
            QFileInfo(raw).isAbsolute() ? raw : base.absoluteFilePath(raw)));
        if (!rel.isEmpty() && rel != QLatin1String(".")) filter.insert(rel);
    }

    QHash<QString, QVariantMap> old;
    QSqlQuery read(handle.db);
    if (read.exec(QStringLiteral("SELECT path,bytes,modified_ms,sha256 FROM files"))) {
        while (read.next()) old.insert(read.value(0).toString(),
            {{QStringLiteral("bytes"), read.value(1)}, {QStringLiteral("modifiedMs"), read.value(2)},
             {QStringLiteral("sha256"), read.value(3)}});
    }

    if (!handle.db.transaction())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), handle.db.lastError().text()}};

    int added = 0, updated = 0, removed = 0, reused = 0;
    QSqlQuery q(handle.db);
    q.prepare(QStringLiteral("DELETE FROM files WHERE path=?"));
    for (auto it = old.cbegin(); it != old.cend(); ++it) {
        if (!current.contains(it.key())) {
            q.bindValue(0, it.key()); q.exec();
            QSqlQuery del(handle.db);
            del.prepare(QStringLiteral("DELETE FROM chunks WHERE path=? OR id LIKE ?"));
            del.bindValue(0, it.key()); del.bindValue(1, it.key() + QStringLiteral("|%")); del.exec();
            del.prepare(QStringLiteral("DELETE FROM edges WHERE source=? OR target=?"));
            del.bindValue(0, it.key()); del.bindValue(1, it.key()); del.exec();
            ++removed;
        }
    }

    for (const SourceFile &file : files) {
        const QVariantMap previous = old.value(file.rel);
        const bool explicitlyChanged = !filter.isEmpty() && filter.contains(file.rel);
        const bool same = !previous.isEmpty()
            && previous.value(QStringLiteral("bytes")).toLongLong() == file.bytes
            && previous.value(QStringLiteral("modifiedMs")).toLongLong() == file.modifiedMs
            && !explicitlyChanged;
        if (same) { ++reused; continue; }

        QByteArray raw;
        const QString hash = sha256File(file.abs, &raw);
        if (hash.isEmpty() || raw.contains('\0')) continue;
        const bool existed = !previous.isEmpty();
        QSqlQuery del(handle.db);
        del.prepare(QStringLiteral("DELETE FROM chunks WHERE path=?"));
        del.bindValue(0, file.rel); del.exec();
        del.prepare(QStringLiteral("DELETE FROM edges WHERE source=?"));
        del.bindValue(0, file.rel); del.exec();

        QSqlQuery up(handle.db);
        up.prepare(QStringLiteral("INSERT OR REPLACE INTO files(path,language,bytes,modified_ms,sha256) VALUES(?,?,?,?,?)"));
        up.addBindValue(file.rel); up.addBindValue(file.language); up.addBindValue(file.bytes);
        up.addBindValue(file.modifiedMs); up.addBindValue(hash); up.exec();

        const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
        for (int start = 0; start < lines.size(); start += 40) {
            const int count = qMin(40, lines.size() - start);
            const QString text = lines.mid(start, count).join(QLatin1Char('\n')).trimmed();
            if (text.isEmpty()) continue;
            const int lineStart = start + 1;
            const int lineEnd = start + count;
            const QString id = file.rel + QLatin1Char('|') + QString::number(lineStart);
            QSqlQuery chunk(handle.db);
            chunk.prepare(QStringLiteral("INSERT OR REPLACE INTO chunks(id,path,start_line,end_line,text_hash,text,token_est) VALUES(?,?,?,?,?,?,?)"));
            chunk.addBindValue(id); chunk.addBindValue(file.rel); chunk.addBindValue(lineStart);
            chunk.addBindValue(lineEnd); chunk.addBindValue(sha256(text.toUtf8()));
            chunk.addBindValue(text); chunk.addBindValue(text.size() / 4 + 8); chunk.exec();
        }

        const QStringList refs = importRefs(QString::fromUtf8(raw));
        for (const QString &ref : refs) {
            const QString target = byBase.value(ref);
            if (target.isEmpty() || target == file.rel) continue;
            addEdge(handle.db, file.rel, target,
                    file.language == QLatin1String("qml") ? QStringLiteral("qml-import") : QStringLiteral("imports"),
                    QStringLiteral("portable lexical parser"), 0.75);
        }

        // Relaciones baratas pero útiles para priorizar evidencia: un test cuyo
        // basename coincide con un módulo y las fuentes declaradas por CMake.
        const QString lowerRel = file.rel.toLower();
        if (lowerRel.contains(QStringLiteral("/test"))
            || lowerRel.startsWith(QStringLiteral("test"))) {
            QString candidate = basename(file.rel);
            if (candidate.startsWith(QStringLiteral("test_"))) candidate.remove(0, 5);
            if (candidate.endsWith(QStringLiteral("_test"))) candidate.chop(5);
            addEdge(handle.db, file.rel, byBase.value(candidate), QStringLiteral("tests"),
                    QStringLiteral("test basename heuristic"), 0.65);
        }
        if (QFileInfo(file.rel).fileName().compare(QStringLiteral("CMakeLists.txt"),
                                                    Qt::CaseInsensitive) == 0) {
            static const QRegularExpression cmakeSourceRe(
                QStringLiteral("[\\\"']([^\\\"']+\\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx|qml|js|ts|py|java|rs))[\\\"']"),
                QRegularExpression::CaseInsensitiveOption);
            for (auto it = cmakeSourceRe.globalMatch(QString::fromUtf8(raw)); it.hasNext();) {
                const QString ref = QDir::cleanPath(QDir::fromNativeSeparators(it.next().captured(1)));
                QString target = current.value(ref).rel;
                if (target.isEmpty()) {
                    const QString local = QDir::cleanPath(QFileInfo(file.rel).path()
                                                           + QLatin1Char('/') + ref);
                    target = current.value(local).rel;
                }
                addEdge(handle.db, file.rel, target, QStringLiteral("build-manifest"),
                        QStringLiteral("CMake source declaration"), 0.80);
            }
        }
        if (existed) ++updated; else ++added;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QSqlQuery meta(handle.db);
    meta.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key,value) VALUES(?,?)"));
    meta.addBindValue(QStringLiteral("indexedAt")); meta.addBindValue(now); meta.exec();
    meta.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key,value) VALUES(?,?)"));
    meta.addBindValue(QStringLiteral("revision")); meta.addBindValue(now); meta.exec();
    meta.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key,value) VALUES(?,?)"));
    meta.addBindValue(QStringLiteral("parserVersion")); meta.addBindValue(QStringLiteral("context-index-v1")); meta.exec();
    if (!handle.db.commit())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), handle.db.lastError().text()}};

    QVariantMap result = status(root);
    result.insert(QStringLiteral("added"), added);
    result.insert(QStringLiteral("updated"), updated);
    result.insert(QStringLiteral("removed"), removed);
    result.insert(QStringLiteral("reused"), reused);
    result.insert(QStringLiteral("freshness"), QStringLiteral("clean"));
    return result;
}

QVariantMap scout(const QString &root, const QString &query, int tokenBudget,
                  int k, bool expandGraph, const QString &path)
{
    QVariantMap sync = refresh(root);
    if (!sync.value(QStringLiteral("ok")).toBool()) return sync;
    DbHandle handle;
    QString error;
    if (!openDb(root, &handle, &error)) return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};

    QStringList terms;
    for (const QString &term : query.toLower().split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")), Qt::SkipEmptyParts))
        if (term.size() >= 2 && !terms.contains(term)) terms << term;
    if (terms.isEmpty()) return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("query vacía")}};

    struct Hit { QString id, rel, hash, text, preview; int start = 0, end = 0; int score = 0; int tokens = 0; };
    QList<Hit> hits;
    QSqlQuery q(handle.db);
    q.prepare(QStringLiteral("SELECT c.id,c.path,c.start_line,c.end_line,c.text_hash,c.text,c.token_est,f.sha256 FROM chunks c JOIN files f ON f.path=c.path"));
    q.exec();
    const QString scope = QDir::fromNativeSeparators(path.trimmed());
    while (q.next()) {
        const QString rel = q.value(1).toString();
        if (!scope.isEmpty() && rel != scope && !rel.startsWith(scope + QLatin1Char('/'))) continue;
        const QString text = q.value(5).toString();
        const QString low = (rel + QLatin1Char(' ') + text).toLower();
        int score = 0;
        for (const QString &term : terms) {
            const int count = low.count(term);
            if (count > 0) score += 2 + qMin(4, count);
            if (rel.toLower().contains(term)) score += 3;
        }
        if (score <= 0) continue;
        QString preview;
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            if (!line.trimmed().isEmpty()) { preview = line.trimmed().left(120); break; }
        }
        hits.append({q.value(0).toString(), rel, q.value(7).toString(), text, preview,
                     q.value(2).toInt(), q.value(3).toInt(), score, q.value(6).toInt()});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        return a.score != b.score ? a.score > b.score : a.rel < b.rel;
    });

    const int effectiveK = qBound(1, k, 15);
    int used = 0;
    bool cut = false;
    QVariantList returned;
    QStringList selectedFiles;
    QVariantList skipped;
    for (const Hit &hit : hits) {
        if (returned.size() >= effectiveK) { skipped.append(QVariantMap{{QStringLiteral("path"), hit.rel}, {QStringLiteral("reason"), QStringLiteral("k_limit")}}); break; }
        const int cost = qMax(1, hit.tokens);
        if (tokenBudget > 0 && !returned.isEmpty() && used + cost > tokenBudget) {
            cut = true;
            skipped.append(QVariantMap{{QStringLiteral("path"), hit.rel}, {QStringLiteral("reason"), QStringLiteral("token_budget")}});
            continue;
        }
        used += cost;
        returned.append(QVariantMap{{QStringLiteral("path"), hit.rel},
                                    {QStringLiteral("startLine"), hit.start},
                                    {QStringLiteral("endLine"), hit.end},
                                    {QStringLiteral("score"), hit.score},
                                    {QStringLiteral("source"), QStringLiteral("bm25")},
                                    {QStringLiteral("preview"), hit.preview},
                                    {QStringLiteral("handle"), handleFor(hit.rel, hit.start, hit.end, hit.hash)}});
        if (!selectedFiles.contains(hit.rel)) selectedFiles << hit.rel;
    }

    QVariantList neighbors;
    QVariantList graphOmitted;
    if (expandGraph && !selectedFiles.isEmpty()) {
        QSet<QString> selected(selectedFiles.cbegin(), selectedFiles.cend());
        // SQLite no soporta cómodamente un IN dinámico con la lista anterior;
        // consultar cada archivo mantiene el contrato simple y el límite pequeño.
        for (const QString &source : selectedFiles) {
            QSqlQuery one(handle.db);
            one.prepare(QStringLiteral("SELECT target,type,reason,confidence FROM edges WHERE source=?"));
            one.addBindValue(source);
            if (!one.exec()) continue;
            while (one.next()) {
                const QString target = one.value(0).toString();
                if (selected.contains(target)) continue;
                const QVariantMap item{{QStringLiteral("path"), target},
                                       {QStringLiteral("type"), one.value(1)},
                                       {QStringLiteral("reason"), one.value(2)},
                                       {QStringLiteral("confidence"), one.value(3)}};
                if (neighbors.size() < 12) neighbors.append(item);
                else graphOmitted.append(item);
            }
        }
    }

    const QVariantMap receipt{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("freshness"), QStringLiteral("clean")},
        {QStringLiteral("backend"), QStringLiteral("builtin-bm25")},
        {QStringLiteral("returned"), returned},
        {QStringLiteral("skipped"), skipped},
        {QStringLiteral("graphOmitted"), graphOmitted},
        {QStringLiteral("usedTokensEst"), used},
        {QStringLiteral("remainingBudgetEst"), qMax(0, tokenBudget - used)},
        {QStringLiteral("budgetCut"), cut},
        {QStringLiteral("recommendedNextAction"), returned.isEmpty() ? QStringLiteral("hybrid_search") : QStringLiteral("read_file")},
        {QStringLiteral("indexRevision"), sync.value(QStringLiteral("revision"))}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("receipt"), receipt},
            {QStringLiteral("neighbors"), neighbors}, {QStringLiteral("index"), sync}};
}

QString fetch(const QString &root, const QString &handle, QVariantMap *meta)
{
    if (!handle.startsWith(QStringLiteral("ctx:"))) return QStringLiteral("[context_fetch: handle inválido]");
    const QByteArray encoded = handle.mid(4).toUtf8();
    const QJsonObject obj = QJsonDocument::fromJson(
        QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding)).object();
    const QString rel = QDir::fromNativeSeparators(
        QDir::cleanPath(obj.value(QStringLiteral("path")).toString()));
    const int start = obj.value(QStringLiteral("start")).toInt();
    const int end = obj.value(QStringLiteral("end")).toInt();
    const QString expected = obj.value(QStringLiteral("sha256")).toString();
    const QDir base(root);
    const QString abs = QDir::cleanPath(base.absoluteFilePath(rel));
    const QString roundTrip = QDir::fromNativeSeparators(base.relativeFilePath(abs));
    if (rel.isEmpty() || rel == QLatin1String(".") || rel.startsWith(QStringLiteral("../"))
        || roundTrip != rel || start < 1 || end < start)
        return QStringLiteral("[context_fetch: ruta o rango inválido]");
    QByteArray raw;
    const QString actual = sha256File(abs, &raw);
    if (actual.isEmpty() || actual != expected)
        return QStringLiteral("[context_fetch: handle obsoleto; el archivo cambió, ejecutá repo_slice nuevamente]");
    const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
    const int first = qMin(start - 1, lines.size());
    const int count = qMin(end - start + 1, lines.size() - first);
    if (meta) *meta = {{QStringLiteral("path"), rel}, {QStringLiteral("startLine"), start},
                       {QStringLiteral("endLine"), end}, {QStringLiteral("sha256"), actual}};
    return lines.mid(first, count).join(QLatin1Char('\n'));
}

QString formatScout(const QVariantMap &result)
{
    if (!result.value(QStringLiteral("ok")).toBool())
        return QStringLiteral("[context_scout: %1]").arg(result.value(QStringLiteral("error")).toString());
    QStringList lines{QStringLiteral("[context_scout · evidencia compacta]")};
    for (const QVariant &v : result.value(QStringLiteral("receipt")).toMap()
                                      .value(QStringLiteral("returned")).toList()) {
        const QVariantMap hit = v.toMap();
        lines << QStringLiteral("- %1:%2-%3 [%4] handle=%5 · %6")
            .arg(hit.value(QStringLiteral("path")).toString())
            .arg(hit.value(QStringLiteral("startLine")).toInt())
            .arg(hit.value(QStringLiteral("endLine")).toInt())
            .arg(hit.value(QStringLiteral("score")).toInt())
            .arg(hit.value(QStringLiteral("handle")).toString())
            .arg(hit.value(QStringLiteral("preview")).toString());
    }
    const QVariantList neighbors = result.value(QStringLiteral("neighbors")).toList();
    if (!neighbors.isEmpty()) {
        lines << QStringLiteral("Vecinos:");
        for (const QVariant &v : neighbors) lines << QStringLiteral("- %1 (%2)")
            .arg(v.toMap().value(QStringLiteral("path")).toString(),
                 v.toMap().value(QStringLiteral("type")).toString());
    }
    lines << QStringLiteral("── context-receipt ──");
    lines << QString::fromUtf8(QJsonDocument(asJson(result.value(QStringLiteral("receipt")).toMap()))
                                   .toJson(QJsonDocument::Compact));
    return lines.join(QLatin1Char('\n'));
}

} // namespace ContextIndex
