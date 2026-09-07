#include <QtTest>
#include "core/agent/HotspotAnalyzer.h"

using Churn = HotspotAnalyzer::Churn;
using Hotspot = HotspotAnalyzer::Hotspot;
using Options = HotspotAnalyzer::Options;

class HotspotTests : public QObject
{
    Q_OBJECT
private slots:
    void parseGitLog_aggregatesCommitsAndAuthors();
    void parseGitLog_emptyInput();
    void classifies_testPaths();
    void classifies_sourcePaths();
    void rank_filtersByMinCommitsTestsAndNonSource();
    void rank_untestedHotspotOutranksTestedSameChurn();
    void rank_authorCongestionRaisesScore();
    void rank_topNTruncatesByScore();
    void formatReport_listsPathsOrEmptyNote();
};

// Helper: arma una salida sintética de `git log --pretty=format:@@%an --name-only`.
static QString gitLog(const QStringList &commits)
{
    // Cada "commit" string: "Autor|file1,file2,..."
    QStringList blocks;
    for (const QString &c : commits) {
        const QString author = c.section(QLatin1Char('|'), 0, 0);
        const QStringList files = c.section(QLatin1Char('|'), 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        QString b = QStringLiteral("@@") + author + QLatin1Char('\n');
        b += files.join(QLatin1Char('\n'));
        blocks << b;
    }
    return blocks.join(QStringLiteral("\n\n"));   // git separa commits con línea en blanco
}

static Churn churn(const QString &path, int commits, const QStringList &authors)
{
    Churn c;
    c.path = path;
    c.commits = commits;
    for (const QString &a : authors) c.authors.insert(a);
    return c;
}

static Hotspot find(const QList<Hotspot> &hs, const QString &path)
{
    for (const Hotspot &h : hs) if (h.path == path) return h;
    return Hotspot{};   // path vacío → no encontrado
}

void HotspotTests::parseGitLog_aggregatesCommitsAndAuthors()
{
    const QString log = gitLog({
        QStringLiteral("ana|src/Foo.cpp,src/Bar.cpp"),
        QStringLiteral("ana|src/Foo.cpp"),
        QStringLiteral("beto|src/Foo.cpp,README.md"),
    });
    const auto churns = HotspotAnalyzer::parseGitLog(log);

    Churn foo, bar, readme;
    for (const Churn &c : churns) {
        if (c.path == QLatin1String("src/Foo.cpp")) foo = c;
        if (c.path == QLatin1String("src/Bar.cpp")) bar = c;
        if (c.path == QLatin1String("README.md"))   readme = c;
    }
    QCOMPARE(foo.commits, 3);
    QCOMPARE(foo.authors.size(), 2);          // ana + beto
    QVERIFY(foo.authors.contains(QStringLiteral("ana")));
    QVERIFY(foo.authors.contains(QStringLiteral("beto")));
    QCOMPARE(bar.commits, 1);
    QCOMPARE(bar.authors.size(), 1);
    QCOMPARE(readme.commits, 1);
}

void HotspotTests::parseGitLog_emptyInput()
{
    QVERIFY(HotspotAnalyzer::parseGitLog(QString()).isEmpty());
    QVERIFY(HotspotAnalyzer::parseGitLog(QStringLiteral("\n\n  \n")).isEmpty());
}

void HotspotTests::classifies_testPaths()
{
    QVERIFY(HotspotAnalyzer::isTestPath(QStringLiteral("tests/test_foo.cpp")));
    QVERIFY(HotspotAnalyzer::isTestPath(QStringLiteral("src/foo/__tests__/foo.js")));
    QVERIFY(HotspotAnalyzer::isTestPath(QStringLiteral("app/Bar.spec.ts")));
    QVERIFY(HotspotAnalyzer::isTestPath(QStringLiteral("pkg/foo_test.go")));
    QVERIFY(!HotspotAnalyzer::isTestPath(QStringLiteral("src/Foo.cpp")));
    QVERIFY(!HotspotAnalyzer::isTestPath(QStringLiteral("src/latest/Thing.cpp")));   // "test" no como token
}

void HotspotTests::classifies_sourcePaths()
{
    QVERIFY(HotspotAnalyzer::isSourcePath(QStringLiteral("src/Foo.cpp")));
    QVERIFY(HotspotAnalyzer::isSourcePath(QStringLiteral("ui/Page.qml")));
    QVERIFY(HotspotAnalyzer::isSourcePath(QStringLiteral("a/b/c.py")));
    QVERIFY(!HotspotAnalyzer::isSourcePath(QStringLiteral("README.md")));
    QVERIFY(!HotspotAnalyzer::isSourcePath(QStringLiteral("assets/logo.png")));
    QCOMPARE(HotspotAnalyzer::stemOf(QStringLiteral("src/a/Foo.cpp")), QStringLiteral("Foo"));
}

void HotspotTests::rank_filtersByMinCommitsTestsAndNonSource()
{
    const QList<Churn> in{
        churn(QStringLiteral("src/Hot.cpp"), 5, {QStringLiteral("a")}),
        churn(QStringLiteral("src/Cold.cpp"), 1, {QStringLiteral("a")}),     // < minCommits
        churn(QStringLiteral("README.md"), 9, {QStringLiteral("a")}),        // no es source
        churn(QStringLiteral("tests/test_hot.cpp"), 9, {QStringLiteral("a")}), // es test
    };
    Options o; o.minCommits = 2; o.topN = 0;
    const auto hs = HotspotAnalyzer::rank(in, {}, o);

    QCOMPARE(hs.size(), 1);
    QCOMPARE(hs.first().path, QStringLiteral("src/Hot.cpp"));
}

void HotspotTests::rank_untestedHotspotOutranksTestedSameChurn()
{
    // Mismo churn y autores; el ÚNICO diferencial es la cobertura de test.
    // El untested debe puntuar estrictamente más alto (hallazgo central).
    const QList<Churn> in{
        churn(QStringLiteral("src/Untested.cpp"), 10, {QStringLiteral("a"), QStringLiteral("b")}),
        churn(QStringLiteral("src/Tested.cpp"),   10, {QStringLiteral("a"), QStringLiteral("b")}),
    };
    QSet<QString> tested{QStringLiteral("src/Tested.cpp")};
    Options o; o.minCommits = 2; o.topN = 0;
    const auto hs = HotspotAnalyzer::rank(in, tested, o);

    const Hotspot un = find(hs, QStringLiteral("src/Untested.cpp"));
    const Hotspot te = find(hs, QStringLiteral("src/Tested.cpp"));
    QVERIFY(un.score > te.score);
    QVERIFY(!un.hasTest);
    QVERIFY(te.hasTest);
    QVERIFY(un.reasons.contains(QStringLiteral("sin test")));
    QVERIFY(!te.reasons.contains(QStringLiteral("sin test")));
    // El más riesgoso aparece primero (orden por score desc).
    QCOMPARE(hs.first().path, QStringLiteral("src/Untested.cpp"));
}

void HotspotTests::rank_authorCongestionRaisesScore()
{
    // Mismo churn y misma (falta de) cobertura; más autores = más riesgo.
    const QList<Churn> in{
        churn(QStringLiteral("src/Crowded.cpp"), 8, {QStringLiteral("a"), QStringLiteral("b"),
                                                     QStringLiteral("c"), QStringLiteral("d")}),
        churn(QStringLiteral("src/Lonely.cpp"),  8, {QStringLiteral("a")}),
    };
    Options o; o.minCommits = 2; o.topN = 0;
    const auto hs = HotspotAnalyzer::rank(in, {}, o);

    const Hotspot crowded = find(hs, QStringLiteral("src/Crowded.cpp"));
    const Hotspot lonely  = find(hs, QStringLiteral("src/Lonely.cpp"));
    QVERIFY(crowded.score >= lonely.score);
    QVERIFY(crowded.reasons.contains(QStringLiteral("congestión (4 autores)")));
}

void HotspotTests::rank_topNTruncatesByScore()
{
    QList<Churn> in;
    for (int i = 0; i < 10; ++i)
        in << churn(QStringLiteral("src/F%1.cpp").arg(i), 2 + i, {QStringLiteral("a")});
    Options o; o.minCommits = 2; o.topN = 3;
    const auto hs = HotspotAnalyzer::rank(in, {}, o);

    QCOMPARE(hs.size(), 3);
    // Top = los de mayor churn (F9, F8, F7), no cualquiera.
    QCOMPARE(hs.first().path, QStringLiteral("src/F9.cpp"));
    // Score monótono no creciente.
    for (int i = 1; i < hs.size(); ++i)
        QVERIFY(hs[i - 1].score >= hs[i].score);
}

void HotspotTests::formatReport_listsPathsOrEmptyNote()
{
    QVERIFY(HotspotAnalyzer::formatReport({}).contains(QStringLiteral("Sin hotspots")));

    Options o; o.topN = 0;
    const auto hs = HotspotAnalyzer::rank(
        {churn(QStringLiteral("src/Risky.cpp"), 7, {QStringLiteral("a")})}, {}, o);
    const QString rep = HotspotAnalyzer::formatReport(hs);
    QVERIFY(rep.contains(QStringLiteral("src/Risky.cpp")));
    QVERIFY(rep.contains(QStringLiteral("sin test")));
}

QTEST_MAIN(HotspotTests)
#include "test_hotspots.moc"
