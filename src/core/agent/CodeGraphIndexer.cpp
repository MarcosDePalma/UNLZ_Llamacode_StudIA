#include "CodeGraphIndexer.h"

#include "GraphStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

namespace {

// Carpetas que NO recorremos (ruido + lentitud). Mismo criterio que el walker de
// AgentToolRunner (no parsea .gitignore completo).
bool isIgnoredDir(const QString &name)
{
    static const QSet<QString> ig{
        QStringLiteral("node_modules"), QStringLiteral(".git"), QStringLiteral("build"),
        QStringLiteral("build2"), QStringLiteral("build_tests"), QStringLiteral("dist"),
        QStringLiteral(".venv"), QStringLiteral("venv"), QStringLiteral("__pycache__"),
        QStringLiteral(".next"), QStringLiteral(".turbo"), QStringLiteral("coverage"),
        QStringLiteral("target"), QStringLiteral(".cache"), QStringLiteral(".idea"),
        QStringLiteral(".vs"), QStringLiteral(".gradle"), QStringLiteral("bin"),
        QStringLiteral("obj"), QStringLiteral(".llamacode")};
    return ig.contains(name);
}

// Familia de lenguaje por extensión. "" = no soportado (lo salteamos).
QString langOf(const QString &ext)
{
    const QString e = ext.toLower();
    if (e == QLatin1String("h") || e == QLatin1String("hpp") || e == QLatin1String("hh")
        || e == QLatin1String("hxx") || e == QLatin1String("c") || e == QLatin1String("cc")
        || e == QLatin1String("cpp") || e == QLatin1String("cxx"))
        return QStringLiteral("cpp");
    if (e == QLatin1String("qml")) return QStringLiteral("qml");
    if (e == QLatin1String("js") || e == QLatin1String("jsx") || e == QLatin1String("mjs"))
        return QStringLiteral("js");
    if (e == QLatin1String("ts") || e == QLatin1String("tsx")) return QStringLiteral("ts");
    if (e == QLatin1String("py")) return QStringLiteral("py");
    return QString();
}

// Palabras que NO son nombres de símbolo (keywords/control que matchean los
// patrones tipo 'foo(' en C++). Filtra el ruido más común.
bool isNoiseSymbol(const QString &s)
{
    static const QSet<QString> kw{
        QStringLiteral("if"), QStringLiteral("for"), QStringLiteral("while"),
        QStringLiteral("switch"), QStringLiteral("catch"), QStringLiteral("return"),
        QStringLiteral("sizeof"), QStringLiteral("do"), QStringLiteral("else"),
        QStringLiteral("case"), QStringLiteral("new"), QStringLiteral("delete"),
        QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("not"),
        QStringLiteral("typedef"), QStringLiteral("template")};
    return s.size() < 2 || kw.contains(s.toLower());
}

struct LocatedRef {
    QString name;
    int startLine = 0;
    int endLine = 0;
};

int lineAt(const QString &text, int position)
{
    if (position < 0) return 0;
    return text.left(position).count(QLatin1Char('\n')) + 1;
}

// Extrae los símbolos (clases/funciones) de 'text' según la familia 'lang',
// conservando la línea que respalda cada relación defines.
QVector<LocatedRef> extractSymbols(const QString &lang, const QString &text, int maxSyms)
{
    QVector<LocatedRef> out;
    QSet<QString> seen;
    auto push = [&](const QString &raw, int position) {
        const QString s = raw.trimmed();
        if (isNoiseSymbol(s) || seen.contains(s)) return;
        if (out.size() >= maxSyms) return;
        seen.insert(s);
        const int line = lineAt(text, position);
        out.append({s, line, line});
    };
    auto runRe = [&](const QRegularExpression &re) {
        auto it = re.globalMatch(text);
        while (it.hasNext() && out.size() < maxSyms) {
            const QRegularExpressionMatch match = it.next();
            push(match.captured(1), match.capturedStart(1));
        }
    };

    if (lang == QLatin1String("cpp")) {
        static const QRegularExpression reType(
            QStringLiteral("\\b(?:class|struct)\\s+(\\w+)"));
        static const QRegularExpression reFunc(
            QStringLiteral("(?m)^[A-Za-z_][\\w:\\*&<>,\\s]*?\\b(\\w+)\\s*\\([^;{}]*\\)\\s*"
                           "(?:const\\s*)?(?:noexcept\\s*)?(?:override\\s*)?\\{"));
        runRe(reType);
        runRe(reFunc);
    } else if (lang == QLatin1String("py")) {
        static const QRegularExpression rePy(
            QStringLiteral("(?m)^\\s*(?:def|class)\\s+(\\w+)"));
        runRe(rePy);
    } else if (lang == QLatin1String("js") || lang == QLatin1String("ts")) {
        static const QRegularExpression reFn(QStringLiteral("\\bfunction\\s+(\\w+)"));
        static const QRegularExpression reClass(QStringLiteral("\\bclass\\s+(\\w+)"));
        static const QRegularExpression reArrow(
            QStringLiteral("(?m)^\\s*(?:export\\s+)?(?:const|let|var)\\s+(\\w+)\\s*="
                           "\\s*(?:async\\s*)?\\([^)]*\\)\\s*=>"));
        runRe(reFn);
        runRe(reClass);
        runRe(reArrow);
    } else if (lang == QLatin1String("qml")) {
        static const QRegularExpression reFn(QStringLiteral("\\bfunction\\s+(\\w+)"));
        static const QRegularExpression reId(
            QStringLiteral("(?m)^\\s*id\\s*:\\s*(\\w+)"));
        runRe(reFn);
        runRe(reId);
    }
    return out;
}

// Extrae referencias de import/include como basenames normalizados (sin ext,
// minúscula), conservando la primera línea que respalda cada relación imports.
QVector<LocatedRef> extractImportRefs(const QString &text)
{
    QHash<QString, int> refs;
    static const QRegularExpression reC(
        QStringLiteral("#\\s*include\\s*[\"<]([^\">]+)[\">]"));
    static const QRegularExpression reFrom(
        QStringLiteral("(?:from|import|require)\\s*\\(?\\s*[\"']([^\"']+)[\"']"));
    static const QRegularExpression rePy(
        QStringLiteral("^\\s*(?:from|import)\\s+([\\w.]+)"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression reQml(
        QStringLiteral("^\\s*import\\s+([\\w.]+)"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression reCodeExt(
        QStringLiteral("\\.(h|hpp|hh|hxx|c|cc|cpp|cxx|js|jsx|mjs|ts|tsx|py|qml|java|go|rs|kt)$"),
        QRegularExpression::CaseInsensitiveOption);
    auto add = [&](const QString &raw, int position) {
        QString s = raw;
        s.replace(QLatin1Char('\\'), QLatin1Char('/'));
        s = s.section(QLatin1Char('/'), -1).trimmed();
        if (reCodeExt.match(s).hasMatch())
            s = s.section(QLatin1Char('.'), 0, -2);
        else if (s.contains(QLatin1Char('.')))
            s = s.section(QLatin1Char('.'), -1);
        s = s.toLower();
        if (s.size() >= 2 && !refs.contains(s)) refs.insert(s, lineAt(text, position));
    };
    for (const QRegularExpression *re : {&reC, &reFrom, &rePy, &reQml}) {
        auto it = re->globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            add(match.captured(1), match.capturedStart(1));
        }
    }
    QVector<LocatedRef> out;
    for (auto it = refs.cbegin(); it != refs.cend(); ++it)
        out.append({it.key(), it.value(), it.value()});
    return out;
}

constexpr int kMaxFiles = 5000;
constexpr int kMaxSymsPerFile = 80;

QSet<QString> wantSet(const QStringList &langs)
{
    QSet<QString> w;
    for (const QString &l : langs) {
        const QString v = l.trimmed().toLower();
        if (!v.isEmpty()) w.insert(v);
    }
    return w;
}

// Índice basename(sin ext, minúscula) → rel de TODOS los archivos soportados del
// repo. Sólo nombres (no lee contenido): resuelve los imports por basename.
void collectByBase(const QString &rootAbs, const QSet<QString> &wantLang,
                   QHash<QString, QString> &byBase)
{
    QDir base(rootAbs);
    QStringList stack{rootAbs};
    int n = 0;
    while (!stack.isEmpty() && n < kMaxFiles) {
        QDir d(stack.takeLast());
        const auto entries = d.entryInfoList(
            QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs, QDir::Name);
        for (const QFileInfo &fi : entries) {
            if (fi.isDir()) {
                if (!isIgnoredDir(fi.fileName())) stack << fi.absoluteFilePath();
                continue;
            }
            const QString lang = langOf(fi.suffix());
            if (lang.isEmpty()) continue;
            if (!wantLang.isEmpty() && !wantLang.contains(lang)) continue;
            const QString b = fi.completeBaseName().toLower();
            const QString rel = base.relativeFilePath(fi.absoluteFilePath());
            if (!b.isEmpty() && !byBase.contains(b)) byBase.insert(b, rel);
            if (++n >= kMaxFiles) break;
        }
    }
}

// Acumula entidades + relaciones de UN archivo (ya leído). Devuelve cuántos
// símbolos distintos aportó (para stats).
void emitDoc(const QString &rel, const QString &lang, const QString &text,
             const QHash<QString, QString> &byBase,
             QVector<QPair<QString, QString>> &entities,
             QVector<GraphStore::Triple> &relations,
             QSet<QString> &distinctSyms)
{
    const QString sha256 = QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
    entities.append({rel, QStringLiteral("file")});
    for (const LocatedRef &sym : extractSymbols(lang, text, kMaxSymsPerFile)) {
        entities.append({sym.name, QStringLiteral("concept")});
        relations.append({rel, QStringLiteral("defines"), sym.name,
                          GraphStore::SourceRefs{{rel, sym.startLine, sym.endLine,
                                                  sha256, QStringLiteral("code")}}});
        distinctSyms.insert(sym.name);
    }
    const QString selfBase = QFileInfo(rel).completeBaseName().toLower();
    for (const LocatedRef &ref : extractImportRefs(text)) {
        if (ref.name == selfBase) continue;
        const QString target = byBase.value(ref.name);
        if (target.isEmpty() || target == rel) continue;
        relations.append({rel, QStringLiteral("imports"), target,
                          GraphStore::SourceRefs{{rel, ref.startLine, ref.endLine,
                                                  sha256, QStringLiteral("code")}}});
    }
}

// --- Estado para el incremental (.llamacode/graph.state) -------------------
QString statePath(const QString &cwd)
{
    return QDir::cleanPath(cwd + QStringLiteral("/.llamacode/graph.state"));
}

void readState(const QString &cwd, QString *head, QDateTime *ts)
{
    QFile f(statePath(cwd));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    if (head) *head = o.value(QStringLiteral("head")).toString();
    if (ts) *ts = QDateTime::fromString(o.value(QStringLiteral("ts")).toString(), Qt::ISODate);
}

void writeState(const QString &cwd, const QString &head)
{
    const QString path = statePath(cwd);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    const QJsonObject o{
        {QStringLiteral("head"), head},
        {QStringLiteral("ts"), QDateTime::currentDateTime().toString(Qt::ISODate)}};
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.close();
}

// --- git (best-effort; vacío si no es repo git) ----------------------------
QString runGit(const QString &rootAbs, const QStringList &args, bool *ok)
{
    QProcess p;
    p.setWorkingDirectory(rootAbs);
    p.start(QStringLiteral("git"), args);
    if (!p.waitForStarted(3000) || !p.waitForFinished(8000)) {
        if (ok) *ok = false;
        return QString();
    }
    if (ok) *ok = (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0);
    return QString::fromUtf8(p.readAllStandardOutput());
}

QString gitHead(const QString &rootAbs)
{
    bool ok = false;
    const QString out = runGit(rootAbs, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, &ok);
    return ok ? out.trimmed() : QString();
}

// HEAD sólo si rootAbs ES el toplevel del repo. Si rootAbs es un subdirectorio
// de un repo más grande (p.ej. un cwd dentro de otro checkout, o un temp dir
// bajo $HOME que resulta ser un repo), `git diff/status` devuelven rutas
// relativas al toplevel —no a rootCwd—, que no matchean el grafo (indexado
// relativo a rootCwd) y dejarían cambios sin reindexar. Devolver vacío en ese
// caso fuerza el camino mtime, que sí es relativo a rootCwd.
QString gitHeadIfToplevel(const QString &rootAbs)
{
    bool ok = false;
    const QString top = runGit(rootAbs,
        {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")}, &ok);
    if (!ok) return QString();
    if (QDir::cleanPath(top.trimmed()).compare(QDir::cleanPath(rootAbs),
                                               Qt::CaseInsensitive) != 0)
        return QString();
    return gitHead(rootAbs);
}

// Cambios desde 'sinceHead' (diff de commits) + working tree (status). Llena
// 'changed' (A/M/R) y 'deleted' (D). Devuelve false si git no está disponible.
bool gitChanges(const QString &rootAbs, const QString &sinceHead,
                QSet<QString> &changed, QSet<QString> &deleted)
{
    bool ok = false;
    auto ingest = [&](const QString &out) {
        const auto lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &ln : lines) {
            const QString l = ln.trimmed();
            if (l.isEmpty()) continue;
            const QChar st = l.at(0);
            // path = último campo separado por whitespace (R: "old new").
            const QString path = l.section(QRegularExpression(QStringLiteral("\\s+")), -1);
            if (path.isEmpty()) continue;
            if (st == QLatin1Char('D')) deleted.insert(path);
            else changed.insert(path);
        }
    };
    if (!sinceHead.isEmpty()) {
        const QString d = runGit(rootAbs,
            {QStringLiteral("diff"), QStringLiteral("--name-status"),
             sinceHead, QStringLiteral("HEAD")}, &ok);
        if (!ok) return false;
        ingest(d);
    }
    const QString s = runGit(rootAbs,
        {QStringLiteral("status"), QStringLiteral("--porcelain")}, &ok);
    if (!ok) return false;
    ingest(s);
    return true;
}

}  // namespace

namespace CodeGraphIndexer {

Stats build(const QString &rootCwd, const QStringList &langs, QString *report)
{
    Stats st;
    const QString rootAbs = QDir(rootCwd).absolutePath();
    QDir base(rootAbs);
    const QSet<QString> wantLang = wantSet(langs);

    // 1. Recolectar archivos soportados (cap defensivo) + índice de basenames.
    struct Doc { QString rel, lang, text; };
    QList<Doc> docs;
    QHash<QString, QString> byBase;

    QStringList stack{rootAbs};
    while (!stack.isEmpty() && docs.size() < kMaxFiles) {
        QDir d(stack.takeLast());
        const auto entries = d.entryInfoList(
            QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs, QDir::Name);
        for (const QFileInfo &fi : entries) {
            if (fi.isDir()) {
                if (!isIgnoredDir(fi.fileName())) stack << fi.absoluteFilePath();
                continue;
            }
            const QString lang = langOf(fi.suffix());
            if (lang.isEmpty()) continue;
            if (!wantLang.isEmpty() && !wantLang.contains(lang)) continue;

            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QString text = QString::fromUtf8(f.read(512 * 1024));
            f.close();

            const QString rel = base.relativeFilePath(fi.absoluteFilePath());
            docs.append({rel, lang, text});
            const QString b = fi.completeBaseName().toLower();
            if (!b.isEmpty() && !byBase.contains(b)) byBase.insert(b, rel);
            if (docs.size() >= kMaxFiles) break;
        }
    }

    // 2. Construir entidades + relaciones.
    QVector<QPair<QString, QString>> entities;
    QVector<GraphStore::Triple> relations;
    QSet<QString> distinctSyms;
    for (const Doc &doc : docs)
        emitDoc(doc.rel, doc.lang, doc.text, byBase, entities, relations, distinctSyms);

    st.files = docs.size();
    st.symbols = distinctSyms.size();

    // 3. Volcado masivo (una sola pasada de escritura) + estado para incremental.
    int addedEnt = 0, addedRel = 0;
    GraphStore::addBatch(rootCwd, entities, relations, &addedEnt, &addedRel);
    st.edges = addedRel;
    writeState(rootCwd, gitHeadIfToplevel(rootAbs));

    if (report) {
        *report = QStringLiteral(
            "[code_graph: %1 archivos indexados · %2 símbolos · +%3 entidades · "
            "+%4 relaciones (defines/imports) en .llamacode/graph.jsonl]\n"
            "Consultá el mapa con la tool 'graph' (action='query', name=<archivo o símbolo>).")
            .arg(st.files).arg(st.symbols).arg(addedEnt).arg(addedRel);
    }
    return st;
}

Stats reindexFiles(const QString &rootCwd, const QStringList &relFiles,
                   const QStringList &langs, QString *report)
{
    Stats st;
    const QString rootAbs = QDir(rootCwd).absolutePath();
    QDir base(rootAbs);
    const QSet<QString> wantLang = wantSet(langs);

    QHash<QString, QString> byBase;
    collectByBase(rootAbs, wantLang, byBase);

    QVector<QPair<QString, QString>> entities;
    QVector<GraphStore::Triple> relations;
    QSet<QString> distinctSyms;

    for (const QString &relRaw : relFiles) {
        const QString rel = relRaw.trimmed();
        if (rel.isEmpty()) continue;
        const QString abs = base.absoluteFilePath(rel);
        const QFileInfo fi(abs);
        const QString lang = langOf(fi.suffix());
        if (lang.isEmpty()) continue;
        if (!wantLang.isEmpty() && !wantLang.contains(lang)) continue;
        if (!fi.exists()) continue;

        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString text = QString::fromUtf8(f.read(512 * 1024));
        f.close();

        // Borrar edges viejos de este archivo ANTES de re-extraer (los símbolos/
        // imports eliminados desaparecen; el resto del grafo es append-only).
        GraphStore::removeRelationsBySubject(rootCwd, rel);
        emitDoc(rel, lang, text, byBase, entities, relations, distinctSyms);
        st.files++;
    }

    st.symbols = distinctSyms.size();
    int addedEnt = 0, addedRel = 0;
    if (!entities.isEmpty() || !relations.isEmpty())
        GraphStore::addBatch(rootCwd, entities, relations, &addedEnt, &addedRel);
    st.edges = addedRel;

    if (report) {
        *report = QStringLiteral(
            "[code_graph reindex: %1 archivos · %2 símbolos · +%3 relaciones nuevas]")
            .arg(st.files).arg(st.symbols).arg(addedRel);
    }
    return st;
}

Stats buildIncremental(const QString &rootCwd, const QStringList &langs, QString *report)
{
    const QString rootAbs = QDir(rootCwd).absolutePath();

    QString prevHead;
    QDateTime prevTs;
    readState(rootCwd, &prevHead, &prevTs);

    // Sin baseline → pasada completa (que además deja el estado).
    if (!QFile::exists(statePath(rootCwd)) || (prevHead.isEmpty() && !prevTs.isValid()))
        return build(rootCwd, langs, report);

    const QSet<QString> wantLang = wantSet(langs);
    QSet<QString> changed, deleted;

    const QString headNow = gitHeadIfToplevel(rootAbs);
    bool usedGit = false;
    if (!headNow.isEmpty()) {
        usedGit = gitChanges(rootAbs, prevHead, changed, deleted);
    }
    if (!usedGit) {
        // Fallback mtime: archivos soportados modificados después del último índice.
        QDir base(rootAbs);
        QStringList stack{rootAbs};
        int n = 0;
        while (!stack.isEmpty() && n < kMaxFiles) {
            QDir d(stack.takeLast());
            const auto entries = d.entryInfoList(
                QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs, QDir::Name);
            for (const QFileInfo &fi : entries) {
                if (fi.isDir()) {
                    if (!isIgnoredDir(fi.fileName())) stack << fi.absoluteFilePath();
                    continue;
                }
                const QString lang = langOf(fi.suffix());
                if (lang.isEmpty()) continue;
                if (!wantLang.isEmpty() && !wantLang.contains(lang)) continue;
                ++n;
                if (prevTs.isValid() && fi.lastModified() <= prevTs) continue;
                changed.insert(base.relativeFilePath(fi.absoluteFilePath()));
            }
        }
        // Archivos borrados: entidades 'file' cuyo archivo ya no existe.
        for (const QString &fileRel : GraphStore::entityNames(rootCwd, QStringLiteral("file"))) {
            if (!QFileInfo::exists(base.absoluteFilePath(fileRel)))
                deleted.insert(fileRel);
        }
    }

    // Purgar edges de archivos borrados.
    for (const QString &del : deleted)
        GraphStore::removeRelationsBySubject(rootCwd, del);

    Stats st = reindexFiles(rootCwd, QStringList(changed.cbegin(), changed.cend()),
                            langs, nullptr);
    writeState(rootCwd, headNow);

    if (report) {
        *report = QStringLiteral(
            "[code_graph incremental (%1): %2 archivos reindexados · %3 borrados · "
            "%4 símbolos · +%5 relaciones nuevas]")
            .arg(usedGit ? QStringLiteral("git") : QStringLiteral("mtime"))
            .arg(st.files).arg(deleted.size()).arg(st.symbols).arg(st.edges);
    }
    return st;
}

}  // namespace CodeGraphIndexer
