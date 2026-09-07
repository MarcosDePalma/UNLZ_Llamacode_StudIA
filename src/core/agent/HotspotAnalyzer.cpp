#include "HotspotAnalyzer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

// --- Núcleo puro ---

QList<HotspotAnalyzer::Churn> HotspotAnalyzer::parseGitLog(const QString &gitLogOutput)
{
    QHash<QString, Churn> byPath;
    QString author;
    const QStringList lines = gitLogOutput.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("@@"))) {
            author = line.mid(2);   // resto = nombre del autor del commit
            continue;
        }
        if (line.isEmpty()) continue;
        // Normalizá separadores a '/' (git ya usa '/', pero por las dudas).
        const QString path = QString(line).replace(QLatin1Char('\\'), QLatin1Char('/'));
        Churn &c = byPath[path];
        c.path = path;
        c.commits += 1;
        if (!author.isEmpty()) c.authors.insert(author);
    }
    return byPath.values();
}

bool HotspotAnalyzer::isTestPath(const QString &path)
{
    const QString p = path.toLower();
    if (p.contains(QLatin1String("/tests/")) || p.startsWith(QLatin1String("tests/"))
        || p.contains(QLatin1String("/test/")) || p.startsWith(QLatin1String("test/"))
        || p.contains(QLatin1String("__tests__/")) || p.contains(QLatin1String("/spec/")))
        return true;
    const QString name = QFileInfo(p).fileName();
    return name.startsWith(QLatin1String("test_")) || name.startsWith(QLatin1String("test-"))
        || name.contains(QLatin1String("_test."))  || name.contains(QLatin1String(".test."))
        || name.contains(QLatin1String(".spec."))  || name.contains(QLatin1String("_spec."));
}

bool HotspotAnalyzer::isSourcePath(const QString &path)
{
    static const QStringList exts = {
        QStringLiteral(".cpp"), QStringLiteral(".cc"), QStringLiteral(".cxx"),
        QStringLiteral(".c"),   QStringLiteral(".h"),  QStringLiteral(".hpp"),
        QStringLiteral(".hh"),  QStringLiteral(".qml"),
        QStringLiteral(".js"),  QStringLiteral(".jsx"), QStringLiteral(".ts"),
        QStringLiteral(".tsx"), QStringLiteral(".py"),  QStringLiteral(".go"),
        QStringLiteral(".rs"),  QStringLiteral(".java"), QStringLiteral(".kt"),
        QStringLiteral(".cs"),  QStringLiteral(".rb"),  QStringLiteral(".php"),
        QStringLiteral(".swift"), QStringLiteral(".m"), QStringLiteral(".mm"),
        QStringLiteral(".scala"), QStringLiteral(".dart")
    };
    const QString p = path.toLower();
    for (const QString &e : exts)
        if (p.endsWith(e)) return true;
    return false;
}

QString HotspotAnalyzer::stemOf(const QString &path)
{
    return QFileInfo(path).completeBaseName();
}

QList<HotspotAnalyzer::Hotspot> HotspotAnalyzer::rank(const QList<Churn> &churn,
                                                      const QSet<QString> &testedPaths,
                                                      const Options &opts)
{
    // Máximos para normalizar (sobre los que pasan el filtro de minCommits).
    int maxCommits = 0, maxAuthors = 0;
    for (const Churn &c : churn) {
        if (c.commits < opts.minCommits) continue;
        if (isTestPath(c.path) || !isSourcePath(c.path)) continue;
        maxCommits = std::max(maxCommits, c.commits);
        maxAuthors = std::max(maxAuthors, int(c.authors.size()));
    }

    QList<Hotspot> out;
    for (const Churn &c : churn) {
        if (c.commits < opts.minCommits) continue;
        if (isTestPath(c.path) || !isSourcePath(c.path)) continue;

        const double churnNorm  = maxCommits > 0 ? double(c.commits) / maxCommits : 0.0;
        const double authorNorm = maxAuthors > 0 ? double(c.authors.size()) / maxAuthors : 0.0;
        // "Hotness": cuánta actividad/congestión tiene el archivo (0..1).
        const double hotness = 0.6 * churnNorm + 0.4 * authorNorm;

        const bool hasTest = testedPaths.contains(c.path);
        // Sin test eleva el piso: tested → riesgo a lo sumo 0.5; untested → 0.5..1.0.
        const double risk01 = hasTest ? hotness * 0.5 : 0.5 + hotness * 0.5;

        Hotspot h;
        h.path = c.path;
        h.commits = c.commits;
        h.authors = int(c.authors.size());
        h.hasTest = hasTest;
        h.score = std::clamp(int(std::lround(1.0 + risk01 * 9.0)), 1, 10);

        if (!hasTest) h.reasons << QStringLiteral("sin test");
        if (churnNorm >= 0.66) h.reasons << QStringLiteral("alto churn (%1 commits)").arg(c.commits);
        else if (c.commits >= opts.minCommits) h.reasons << QStringLiteral("%1 commits").arg(c.commits);
        if (c.authors.size() >= 3) h.reasons << QStringLiteral("congestión (%1 autores)").arg(c.authors.size());
        out << h;
    }

    std::sort(out.begin(), out.end(), [](const Hotspot &a, const Hotspot &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.commits != b.commits) return a.commits > b.commits;
        return a.path < b.path;
    });

    if (opts.topN > 0 && out.size() > opts.topN)
        out = out.mid(0, opts.topN);
    return out;
}

// --- Driver con I/O ---

QList<HotspotAnalyzer::Hotspot> HotspotAnalyzer::analyzeRepo(const QString &repoPath,
                                                            const Options &opts,
                                                            QString *error)
{
    auto fail = [&](const QString &msg) {
        if (error) *error = msg;
        return QList<Hotspot>{};
    };

    const QDir repo(repoPath);
    if (!repo.exists())
        return fail(QStringLiteral("ruta inexistente: %1").arg(repoPath));

    QStringList gitArgs{QStringLiteral("-C"), repo.absolutePath(),
                        QStringLiteral("log"), QStringLiteral("--no-merges"),
                        QStringLiteral("--pretty=format:@@%an"),
                        QStringLiteral("--name-only")};
    if (opts.sinceDays > 0)
        gitArgs << QStringLiteral("--since=%1.days.ago").arg(opts.sinceDays);

    QProcess git;
    git.start(QStringLiteral("git"), gitArgs);
    if (!git.waitForStarted(5000))
        return fail(QStringLiteral("no se pudo ejecutar git (¿instalado/en PATH?)"));
    if (!git.waitForFinished(30000)) {
        git.kill();
        return fail(QStringLiteral("git log no terminó a tiempo"));
    }
    if (git.exitStatus() != QProcess::NormalExit || git.exitCode() != 0)
        return fail(QStringLiteral("git log falló (¿es un repo git?): %1")
                        .arg(QString::fromUtf8(git.readAllStandardError()).trimmed()));

    const QString out = QString::fromUtf8(git.readAllStandardOutput());
    const QList<Churn> churn = parseGitLog(out);

    // Cobertura: junta el texto de todos los archivos de test del repo y marca
    // como "tested" a los sources cuyo stem aparece mencionado en algún test.
    QString allTestsText;
    QSet<QString> testStems;
    for (const Churn &c : churn) {
        if (!isTestPath(c.path)) continue;
        QFile f(repo.absoluteFilePath(c.path));
        if (!f.open(QIODevice::ReadOnly)) continue;
        allTestsText += QString::fromUtf8(f.read(512 * 1024));
        allTestsText += QLatin1Char('\n');
        testStems.insert(stemOf(c.path));
    }

    QSet<QString> testedPaths;
    for (const Churn &c : churn) {
        if (isTestPath(c.path) || !isSourcePath(c.path)) continue;
        const QString stem = stemOf(c.path);
        if (stem.isEmpty()) continue;
        // Mención del stem como token (evita matches parciales tipo "Foo" en "FooBar").
        const QRegularExpression re(QStringLiteral("\\b") + QRegularExpression::escape(stem) + QStringLiteral("\\b"));
        if (allTestsText.contains(re))
            testedPaths.insert(c.path);
    }

    if (error) error->clear();
    return rank(churn, testedPaths, opts);
}

QString HotspotAnalyzer::formatReport(const QList<Hotspot> &hotspots)
{
    if (hotspots.isEmpty())
        return QStringLiteral("[code_hotspots] Sin hotspots: no hay archivos de código con "
                              "churn suficiente (o el repo no tiene historial git).");
    QStringList lines;
    lines << QStringLiteral("Archivos riesgosos (score 1-10, 10 = más riesgo; churn git + "
                            "autores + cobertura de test):");
    for (const Hotspot &h : hotspots) {
        lines << QStringLiteral("  [%1] %2 — %3")
                     .arg(h.score, 2)
                     .arg(h.path,
                          h.reasons.isEmpty() ? QStringLiteral("ok") : h.reasons.join(QStringLiteral(", ")));
    }
    lines << QStringLiteral("Prioridad: agregá tests a los de score alto SIN test antes de "
                            "tocarlos; son los que más probablemente escondan regresiones.");
    return lines.join(QLatin1Char('\n'));
}
