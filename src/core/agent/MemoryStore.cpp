#include "MemoryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QPair>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace {

QString normScope(const QString &s)
{
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("session") || v == QLatin1String("project")
        || v == QLatin1String("personal"))
        return v;
    return QStringLiteral("project");   // default: lo más útil para coding
}

QString normType(const QString &t)
{
    const QString v = t.trimmed().toLower();
    static const QSet<QString> ok{QStringLiteral("preference"), QStringLiteral("decision"),
                                  QStringLiteral("fact"), QStringLiteral("bug"),
                                  QStringLiteral("skill"),
                                  QStringLiteral("other")};
    return ok.contains(v) ? v : QStringLiteral("fact");
}

// Tokens útiles (lowercase, >=2 chars, únicos) para scoring de keywords.
QStringList terms(const QString &s)
{
    QStringList out;
    const auto parts = s.toLower().split(
        QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")), Qt::SkipEmptyParts);
    for (const QString &t : parts)
        if (t.size() >= 2 && !out.contains(t)) out << t;
    return out;
}

// id corto y estable a partir de content+ts (suficiente para 'forget'/dedupe).
QString makeId(const QString &content, const QString &ts)
{
    const QByteArray h = QCryptographicHash::hash(
        (content + QLatin1Char('|') + ts).toUtf8(), QCryptographicHash::Sha1);
    return QString::fromLatin1(h.toHex().left(8));
}

bool ignoredDir(const QString &name)
{
    static const QSet<QString> ignored{
        QStringLiteral("node_modules"), QStringLiteral(".git"), QStringLiteral("build"),
        QStringLiteral("build2"), QStringLiteral("dist"), QStringLiteral(".venv"),
        QStringLiteral("venv"), QStringLiteral("__pycache__"), QStringLiteral(".next"),
        QStringLiteral(".turbo"), QStringLiteral("coverage"), QStringLiteral("target"),
        QStringLiteral(".cache"), QStringLiteral(".llamacode"), QStringLiteral(".idea"),
        QStringLiteral(".vs"),
        QStringLiteral(".gradle"), QStringLiteral("bin"), QStringLiteral("obj")};
    return ignored.contains(name);
}

QVector<QPair<QString, QString>> textCorpus(const QString &root, int maxFiles)
{
    QVector<QPair<QString, QString>> out;
    QStringList stack{root};
    const QDir base(root);
    while (!stack.isEmpty() && out.size() < maxFiles) {
        const QDir dir(stack.takeLast());
        const QFileInfoList entries = dir.entryInfoList(
            QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs, QDir::Name);
        for (const QFileInfo &info : entries) {
            if (info.isDir()) {
                if (!ignoredDir(info.fileName())) stack << info.absoluteFilePath();
                continue;
            }
            if (info.size() > 1024 * 1024) continue;
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) continue;
            const QByteArray raw = file.read(1024 * 1024);
            if (raw.contains('\0')) continue;
            out.append({base.relativeFilePath(info.absoluteFilePath()),
                        QString::fromUtf8(raw)});
        }
    }
    return out;
}

}  // namespace

namespace MemoryStore {

QString jsonlPath(const QString &cwd)
{
    return QDir::cleanPath(cwd + QStringLiteral("/.llamacode/memory.jsonl"));
}

QString personalJsonlPath()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) root = QDir::homePath() + QStringLiteral("/.llamacode");
    return QDir::cleanPath(root + QStringLiteral("/memory/personal.jsonl"));
}

static QString pathForScope(const QString &cwd, const QString &scope)
{
    return normScope(scope) == QLatin1String("personal")
        ? personalJsonlPath() : jsonlPath(cwd);
}

QString save(const QString &cwd, const QString &content, const QString &scope,
             const QString &type, double confidence, const QString &source,
             double importance, double surprise, const QString &verification,
             const QString &supersedes)
{
    if (cwd.trimmed().isEmpty()) return QStringLiteral("[memory save: cwd vacío]");
    const QString text = content.trimmed();
    if (text.isEmpty()) return QStringLiteral("[memory save: 'content' vacío]");

    const QString path = pathForScope(cwd, scope);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(10 * 60 * 1000);
    if (!lock.tryLock(5000))
        return QStringLiteral("[memory save: memoria ocupada por otra sesión]");

    const QString ts = QDateTime::currentDateTime().toString(Qt::ISODate);
    const QString id = makeId(text, ts);
    QJsonObject fact{
        {QStringLiteral("id"), id},
        {QStringLiteral("content"), text},
        {QStringLiteral("scope"), normScope(scope)},
        {QStringLiteral("type"), normType(type)},
        {QStringLiteral("confidence"), qBound(0.0, confidence <= 0.0 ? 0.8 : confidence, 1.0)},
        {QStringLiteral("source"), source.trimmed().isEmpty() ? QStringLiteral("agent")
                                                              : source.trimmed()},
        {QStringLiteral("importance"), qBound(0.0, importance, 1.0)},
        {QStringLiteral("surprise"), qBound(0.0, surprise, 1.0)},
        {QStringLiteral("verification"), verification.trimmed().isEmpty()
             ? QStringLiteral("inferred") : verification.trimmed().toLower()},
        {QStringLiteral("useCount"), 0},
        {QStringLiteral("ts"), ts}};
    if (!supersedes.trimmed().isEmpty())
        fact.insert(QStringLiteral("supersedes"), supersedes.trimmed());

    QFile f(path);
    // Supersesión explícita: conservar audit trail, pero impedir que la versión
    // reemplazada vuelva a recuperarse junto con la corrección.
    if (!supersedes.trimmed().isEmpty() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QVector<QJsonObject> rows;
        bool changed = false;
        while (!f.atEnd()) {
            const QJsonObject row = QJsonDocument::fromJson(f.readLine().trimmed()).object();
            if (row.isEmpty()) continue;
            QJsonObject updated = row;
            if (row.value(QStringLiteral("id")).toString() == supersedes.trimmed()
                && !row.value(QStringLiteral("stale")).toBool(false)) {
                updated.insert(QStringLiteral("stale"), true);
                updated.insert(QStringLiteral("supersededBy"), id);
                changed = true;
            }
            rows.append(updated);
        }
        f.close();
        if (changed) {
            QSaveFile rewritten(path);
            if (!rewritten.open(QIODevice::WriteOnly | QIODevice::Text))
                return QStringLiteral("[no se pudo reescribir la memoria: %1]").arg(path);
            for (const QJsonObject &row : std::as_const(rows)) {
                rewritten.write(QJsonDocument(row).toJson(QJsonDocument::Compact));
                rewritten.write("\n");
            }
            if (!rewritten.commit())
                return QStringLiteral("[no se pudo confirmar la memoria: %1]").arg(path);
        }
    }
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return QStringLiteral("[no se pudo escribir la memoria: %1]").arg(path);
    f.write(QJsonDocument(fact).toJson(QJsonDocument::Compact));
    f.write("\n");
    f.close();
    return QStringLiteral("[memoria guardada · id=%1 scope=%2 type=%3]")
        .arg(id, fact.value(QStringLiteral("scope")).toString(),
             fact.value(QStringLiteral("type")).toString());
}

QJsonArray recallFacts(const QString &cwd, const QString &query,
                       const QString &scope, int k)
{
    if (cwd.trimmed().isEmpty()) return {};
    if (k <= 0) k = 8;
    k = qBound(1, k, 30);
    // Mantenimiento conservador y amortizado: evita que el histórico stale o
    // de muy bajo valor domine el contexto sin convertir cada recall en una
    // reescritura del JSONL.
    maintain(cwd, scope, 24);
    const QString path = pathForScope(cwd, scope);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};

    const QString scopeFilter = scope.trimmed().toLower();
    const QStringList qterms = terms(query);

    struct Row { QJsonObject obj; double score; };
    QVector<Row> rows;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (o.isEmpty()) continue;
        if (o.value(QStringLiteral("stale")).toBool(false)) continue;   // olvidado
        if (!scopeFilter.isEmpty()
            && o.value(QStringLiteral("scope")).toString() != scopeFilter)
            continue;

        // Score: solapamiento de keywords + sesgo por confianza y recencia.
        double score = 0.0;
        if (!qterms.isEmpty()) {
            const QString hay = o.value(QStringLiteral("content")).toString().toLower();
            int hits = 0;
            for (const QString &t : qterms)
                if (hay.contains(t)) ++hits;
            if (hits == 0) continue;                    // sin query-match → descartar
            score = double(hits) / qterms.size();
        }
        score += 0.08 * o.value(QStringLiteral("confidence")).toDouble(0.8);
        score += 0.14 * o.value(QStringLiteral("importance")).toDouble(0.0);
        score += 0.10 * o.value(QStringLiteral("surprise")).toDouble(0.0);
        const QString verification = o.value(QStringLiteral("verification")).toString();
        if (verification == QLatin1String("user") || verification == QLatin1String("test")
            || verification == QLatin1String("tool"))
            score += 0.08;
        // Recencia: decae con la antigüedad (media vida ~30 días).
        const QDateTime ts = QDateTime::fromString(
            o.value(QStringLiteral("ts")).toString(), Qt::ISODate);
        if (ts.isValid()) {
            const double days = ts.daysTo(QDateTime::currentDateTime());
            score += 0.10 * std::pow(0.5, qMax(0.0, days) / 30.0);
        }
        rows.append({o, score});
    }
    f.close();

    std::stable_sort(rows.begin(), rows.end(),
                     [](const Row &a, const Row &b) { return a.score > b.score; });

    QJsonArray out;
    for (int i = 0; i < rows.size() && i < k; ++i) {
        QJsonObject o = rows[i].obj;
        o.insert(QStringLiteral("_score"), rows[i].score);
        out.append(o);
    }
    return out;
}

QString recall(const QString &cwd, const QString &query, const QString &scope, int k)
{
    const QJsonArray rows = recallFacts(cwd, query, scope, k);
    if (rows.isEmpty())
        return query.isEmpty() ? QStringLiteral("[memoria estructurada vacía]")
                               : QStringLiteral("[sin hechos para: %1]").arg(query);

    QStringList out;
    for (const QJsonValue &value : rows) {
        const QJsonObject o = value.toObject();
        const QString src = o.value(QStringLiteral("source")).toString();
        const QString id = o.value(QStringLiteral("id")).toString();
        QString line = QStringLiteral("- [%1/%2] %3")
                   .arg(o.value(QStringLiteral("scope")).toString(),
                        o.value(QStringLiteral("type")).toString(),
                        o.value(QStringLiteral("content")).toString());
        // Provenance compacta para trazabilidad (id + fuente).
        QStringList meta;
        if (!id.isEmpty()) meta << QStringLiteral("id=%1").arg(id);
        if (!src.isEmpty()) meta << QStringLiteral("src=%1").arg(src);
        if (!meta.isEmpty()) line += QStringLiteral("  (%1)").arg(meta.join(QLatin1String(", ")));
        out << line;
    }
    return out.join(QLatin1Char('\n'));
}

QVector<ClaimEvidence> verifyClaims(const QString &cwd, const QStringList &claims,
                                    const QString &root, int maxFiles)
{
    const QString rootAbs = root.trimmed().isEmpty() ? cwd : root;
    const QVector<QPair<QString, QString>> files = textCorpus(rootAbs, qBound(1, maxFiles, 20000));
    QVector<QPair<QString, QString>> corpus = files;
    const QString memory = recall(cwd, QString(), QString(), 30);
    if (!memory.isEmpty() && !memory.startsWith(QLatin1String("[memoria estructurada vacía]")))
        corpus.prepend({QStringLiteral("memoria"), memory});

    QVector<ClaimEvidence> out;
    out.reserve(claims.size());
    for (const QString &claim : claims) {
        ClaimEvidence evidence;
        evidence.claim = claim.trimmed();
        QStringList claimTerms;
        for (const QString &term : evidence.claim.toLower().split(
                 QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")), Qt::SkipEmptyParts)) {
            if (term.size() >= 3 && !claimTerms.contains(term)) claimTerms << term;
        }
        if (claimTerms.isEmpty()) {
            evidence.status = QStringLiteral("unaccredited");
            out.append(evidence);
            continue;
        }

        for (const auto &fragment : corpus) {
            const QString haystack = fragment.second.toLower();
            int hits = 0;
            for (const QString &term : claimTerms)
                if (haystack.contains(term)) ++hits;
            const double coverage = double(hits) / claimTerms.size();
            if (coverage > evidence.coverage) {
                evidence.coverage = coverage;
                evidence.where = fragment.first;
            }
            if (evidence.coverage >= 0.99) break;
        }
        if (evidence.coverage >= 0.8)
            evidence.status = QStringLiteral("accredited");
        else if (evidence.coverage >= 0.4)
            evidence.status = QStringLiteral("partial");
        else
            evidence.status = QStringLiteral("unaccredited");
        out.append(evidence);
    }
    return out;
}

QString forget(const QString &cwd, const QString &query, const QString &scope,
               const QString &mode)
{
    if (cwd.trimmed().isEmpty()) return QStringLiteral("[forget: cwd vacío]");
    const QString path = pathForScope(cwd, scope);
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(10 * 60 * 1000);
    if (!lock.tryLock(5000))
        return QStringLiteral("[forget: memoria ocupada por otra sesión]");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("[memoria estructurada vacía]");

    const QString scopeFilter = scope.trimmed().toLower();
    const QStringList qterms = terms(query);
    if (qterms.isEmpty() && scopeFilter.isEmpty())
        return QStringLiteral("[forget: pasá 'query' y/o 'scope' para no borrar todo]");

    const bool del = mode.trimmed().toLower() == QLatin1String("delete");

    QVector<QJsonObject> kept;     // lo que sobrevive (para mode=delete)
    QVector<QJsonObject> all;      // todo (para mode=stale, reescribe con flag)
    int matched = 0;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.isEmpty()) continue;
        QJsonObject o = QJsonDocument::fromJson(l).object();
        if (o.isEmpty()) continue;

        bool hit = true;
        if (!scopeFilter.isEmpty()
            && o.value(QStringLiteral("scope")).toString() != scopeFilter)
            hit = false;
        if (hit && !qterms.isEmpty()) {
            const QString hay = o.value(QStringLiteral("content")).toString().toLower();
            hit = std::any_of(qterms.cbegin(), qterms.cend(),
                              [&](const QString &t) { return hay.contains(t); });
        }
        if (hit && !o.value(QStringLiteral("stale")).toBool(false)) ++matched;
        if (hit) {
            if (del) continue;                              // delete: lo dropeamos
            o.insert(QStringLiteral("stale"), true);        // stale: lo marcamos
        }
        kept.append(o);
        all.append(o);
    }
    f.close();

    if (matched == 0) return QStringLiteral("[forget: nada coincide]");

    const QVector<QJsonObject> &outRows = del ? kept : all;
    QSaveFile outFile(path);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("[forget: no se pudo reescribir %1]").arg(path);
    for (const QJsonObject &o : outRows) {
        outFile.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        outFile.write("\n");
    }
    if (!outFile.commit())
        return QStringLiteral("[forget: no se pudo confirmar %1]").arg(path);
    return QStringLiteral("[olvidados %1 hecho(s) · modo=%2]")
        .arg(matched).arg(del ? QStringLiteral("delete") : QStringLiteral("stale"));
}

namespace {

// Peso por tipo: las decisiones/preferencias valen más que un 'fact' suelto.
double typeWeight(const QString &t)
{
    if (t == QLatin1String("decision") || t == QLatin1String("preference")) return 1.0;
    if (t == QLatin1String("skill")) return 0.95;
    if (t == QLatin1String("bug")) return 0.9;
    if (t == QLatin1String("fact")) return 0.7;
    return 0.5;   // other
}

// VALOR de un hecho en [0..~1]: tipo · confianza · recencia. Sube con utilidad.
double factValue(const QJsonObject &o)
{
    const double conf = o.value(QStringLiteral("confidence")).toDouble(0.8);
    const double importance = o.value(QStringLiteral("importance")).toDouble(0.0);
    const double surprise = o.value(QStringLiteral("surprise")).toDouble(0.0);
    double rec = 0.5;
    const QDateTime ts = QDateTime::fromString(
        o.value(QStringLiteral("ts")).toString(), Qt::ISODate);
    if (ts.isValid()) {
        const double days = ts.daysTo(QDateTime::currentDateTime());
        rec = std::pow(0.5, qMax(0.0, days) / 30.0);   // media vida 30 días
    }
    const QString type = o.value(QStringLiteral("type")).toString();
    const QString verification = o.value(QStringLiteral("verification")).toString();
    if ((type == QLatin1String("preference") || type == QLatin1String("decision")
         || type == QLatin1String("skill"))
        && (verification == QLatin1String("user") || verification == QLatin1String("test")
            || verification == QLatin1String("tool")))
        rec = qMax(rec, 0.85);
    return typeWeight(type) * (0.4 + 0.6 * conf) * (0.4 + 0.6 * rec)
           + 0.25 * importance + 0.15 * surprise;
}

// COSTO MDL: largo del content normalizado a un "presupuesto" de ~240 chars.
// Texto corto ~0; un párrafo largo penaliza fuerte (paga sus bits).
double lenCost(const QJsonObject &o)
{
    const int len = o.value(QStringLiteral("content")).toString().size();
    return len / 240.0;
}

// Jaccard de keywords entre dos hechos (para detectar casi-duplicados).
double jaccard(const QSet<QString> &a, const QSet<QString> &b)
{
    if (a.isEmpty() || b.isEmpty()) return 0.0;
    int inter = 0;
    for (const QString &t : a) if (b.contains(t)) ++inter;
    const int uni = a.size() + b.size() - inter;
    return uni > 0 ? double(inter) / uni : 0.0;
}

}  // namespace

QString prune(const QString &cwd, const QString &scope, int maxKeep,
              const QString &mode, bool dryRun)
{
    if (cwd.trimmed().isEmpty()) return QStringLiteral("[prune: cwd vacío]");
    if (maxKeep <= 0) maxKeep = 50;
    maxKeep = qBound(1, maxKeep, 1000);

    const QString path = pathForScope(cwd, scope);
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(10 * 60 * 1000);
    if (!lock.tryLock(5000))
        return QStringLiteral("[prune: memoria ocupada por otra sesión]");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("[memoria estructurada vacía]");

    const QString scopeFilter = scope.trimmed().toLower();

    struct Row {
        QJsonObject obj;
        QSet<QString> kw;
        double net = 0.0;       // valor - costo - redundancia
        bool inScope = false;   // candidato a poda
        QString why;            // motivo si se evicta
        bool evict = false;
    };
    QVector<Row> rows;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.isEmpty()) continue;
        QJsonObject o = QJsonDocument::fromJson(l).object();
        if (o.isEmpty()) continue;
        Row r;
        r.obj = o;
        const bool stale = o.value(QStringLiteral("stale")).toBool(false);
        r.inScope = !stale
            && (scopeFilter.isEmpty()
                || o.value(QStringLiteral("scope")).toString() == scopeFilter);
        if (r.inScope) {
            const auto ts = terms(o.value(QStringLiteral("content")).toString());
            r.kw = QSet<QString>(ts.cbegin(), ts.cend());
            r.net = factValue(o) - 0.15 * lenCost(o);
        }
        rows.append(r);
    }
    f.close();

    // Redundancia: entre casi-duplicados (jaccard alto) penalizamos al de menor
    // valor → conserva el que "comprime mejor" la misma info.
    for (int i = 0; i < rows.size(); ++i) {
        if (!rows[i].inScope) continue;
        for (int j = i + 1; j < rows.size(); ++j) {
            if (!rows[j].inScope) continue;
            if (jaccard(rows[i].kw, rows[j].kw) < 0.6) continue;
            Row &lo = rows[i].net <= rows[j].net ? rows[i] : rows[j];
            lo.evict = true;
            lo.why = QStringLiteral("redundante");
        }
    }

    // Orden por valor neto desc; lo que cae bajo el presupuesto maxKeep se evicta.
    QVector<int> idx;
    for (int i = 0; i < rows.size(); ++i) if (rows[i].inScope) idx.append(i);
    std::stable_sort(idx.begin(), idx.end(),
                     [&](int a, int b) { return rows[a].net > rows[b].net; });
    int kept = 0;
    for (int rank = 0; rank < idx.size(); ++rank) {
        Row &r = rows[idx[rank]];
        if (r.evict) continue;                    // ya marcado por redundancia
        if (kept < maxKeep) { ++kept; continue; }
        r.evict = true;
        r.why = QStringLiteral("excede presupuesto");
    }

    int evicted = 0;
    QStringList sample;
    for (const Row &r : rows) if (r.evict) {
        ++evicted;
        if (sample.size() < 5)
            sample << QStringLiteral("  ✗ %1 (%2)")
                .arg(r.obj.value(QStringLiteral("content")).toString().left(60), r.why);
    }

    if (evicted == 0)
        return QStringLiteral("[prune: nada que podar · %1 hecho(s) dentro del presupuesto]")
            .arg(kept);

    const QString report = QStringLiteral("[prune%1: %2 evicto(s), %3 conservado(s)]\n")
        .arg(dryRun ? QStringLiteral(" dry-run") : QString()).arg(evicted).arg(kept)
        + sample.join(QLatin1Char('\n'));
    if (dryRun) return report;

    // Aplicar: borrar o marcar stale los evictos, reescribiendo el JSONL.
    const bool del = mode.trimmed().toLower() == QLatin1String("delete");
    QSaveFile outFile(path);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("[prune: no se pudo reescribir %1]").arg(path);
    for (Row &r : rows) {
        if (r.evict) {
            if (del) continue;
            r.obj.insert(QStringLiteral("stale"), true);
        }
        outFile.write(QJsonDocument(r.obj).toJson(QJsonDocument::Compact));
        outFile.write("\n");
    }
    if (!outFile.commit())
        return QStringLiteral("[prune: no se pudo confirmar %1]").arg(path);
    return report + QStringLiteral("\n· modo=%1")
        .arg(del ? QStringLiteral("delete") : QStringLiteral("stale"));
}

QString decay(const QString &cwd, const QString &scope, int maxAgeDays,
              double minValue, bool dryRun)
{
    if (cwd.trimmed().isEmpty()) return QStringLiteral("[decay: cwd vacío]");
    maxAgeDays = qBound(30, maxAgeDays, 3650);
    minValue = qBound(0.05, minValue, 0.75);

    const QString path = pathForScope(cwd, scope);
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(10 * 60 * 1000);
    if (!lock.tryLock(5000))
        return QStringLiteral("[decay: memoria ocupada por otra sesión]");

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("[decay: memoria estructurada vacía]");

    const QString scopeFilter = scope.trimmed().toLower();
    const QDateTime now = QDateTime::currentDateTime();
    QVector<QJsonObject> rows;
    QStringList sample;
    int candidates = 0;
    int active = 0;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty()) continue;
        QJsonObject row = QJsonDocument::fromJson(line).object();
        if (row.isEmpty()) continue;
        const bool stale = row.value(QStringLiteral("stale")).toBool(false);
        const bool inScope = scopeFilter.isEmpty()
            || row.value(QStringLiteral("scope")).toString() == scopeFilter;
        const QString type = row.value(QStringLiteral("type")).toString().toLower();
        const QString verification = row.value(QStringLiteral("verification")).toString().toLower();
        const bool protectedFact = row.value(QStringLiteral("importance")).toDouble(0.0) >= 0.75
            || row.value(QStringLiteral("useCount")).toInt(0) >= 3
            || ((type == QLatin1String("decision") || type == QLatin1String("preference"))
                && (verification == QLatin1String("user")
                    || verification == QLatin1String("test")
                    || verification == QLatin1String("tool")));
        const QDateTime ts = QDateTime::fromString(
            row.value(QStringLiteral("ts")).toString(), Qt::ISODate);
        const double ageDays = ts.isValid()
            ? qMax(0.0, double(ts.daysTo(now)))
            : double(maxAgeDays + 1);
        const bool candidate = !stale && inScope && !protectedFact
            && ageDays >= maxAgeDays && factValue(row) <= minValue;
        if (!stale && inScope) ++active;
        if (candidate) {
            ++candidates;
            if (sample.size() < 5)
                sample << QStringLiteral("  ✗ %1 (%2d, value=%3)")
                    .arg(row.value(QStringLiteral("content")).toString().left(60))
                    .arg(qRound(ageDays)).arg(factValue(row), 0, 'f', 2);
            if (!dryRun) row.insert(QStringLiteral("stale"), true);
        }
        rows.append(row);
    }
    f.close();

    const QString report = QStringLiteral("[decay%1: %2 candidato(s), %3 activo(s)]\n")
        .arg(dryRun ? QStringLiteral(" dry-run") : QString())
        .arg(candidates).arg(active)
        + sample.join(QLatin1Char('\n'));
    if (dryRun || candidates == 0) return report;

    QSaveFile outFile(path);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return QStringLiteral("[decay: no se pudo reescribir %1]").arg(path);
    for (const QJsonObject &row : std::as_const(rows)) {
        outFile.write(QJsonDocument(row).toJson(QJsonDocument::Compact));
        outFile.write("\n");
    }
    if (!outFile.commit())
        return QStringLiteral("[decay: no se pudo confirmar %1]").arg(path);
    return report + QStringLiteral("· modo=stale");
}

QString maintain(const QString &cwd, const QString &scope, int intervalHours)
{
    if (cwd.trimmed().isEmpty()) return QStringLiteral("[memory maintenance: cwd vacío]");
    intervalHours = qBound(1, intervalHours, 24 * 30);
    const QString marker = QDir::cleanPath(
        cwd + QStringLiteral("/.llamacode/memory_maintenance.json"));
    QDir().mkpath(QFileInfo(marker).absolutePath());
    QLockFile gate(marker + QStringLiteral(".lock"));
    gate.setStaleLockTime(10 * 60 * 1000);
    if (!gate.tryLock(100))
        return QStringLiteral("[memory maintenance: otra sesión ya lo ejecuta]");

    QFile previous(marker);
    if (previous.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonObject state = QJsonDocument::fromJson(previous.readAll()).object();
        const QDateTime last = QDateTime::fromString(
            state.value(QStringLiteral("lastRun")).toString(), Qt::ISODate);
        if (last.isValid() && last.secsTo(QDateTime::currentDateTime())
                < intervalHours * 3600)
            return QStringLiteral("[memory maintenance: no requerido todavía]");
    }

    const QString result = decay(cwd, scope, 90, 0.28, false);
    QSaveFile out(marker);
    if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        const QJsonObject state{
            {QStringLiteral("lastRun"), QDateTime::currentDateTime().toString(Qt::ISODate)},
            {QStringLiteral("scope"), scope.trimmed().toLower()},
            {QStringLiteral("result"), result.left(512)}};
        out.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
        out.commit();
    }
    return QStringLiteral("[memory maintenance: ejecutado] ") + result;
}

}  // namespace MemoryStore
