#include <QtTest>
#include "core/EngineCatalog.h"

class EngineCatalogTests : public QObject
{
    Q_OBJECT
private slots:
    void catalogIncludesSourceForks();
    void variantCompatibilityReportsReason();
    void versionComparisonHandlesBuildTagsAndSemver();
    void sourceUpdateComparesShortSha();
    void repoNormalizationAndBuildDirAreStable();
    void pullRequestRefIsParsedAsBranch();
    void qwen38NextEntryBuildsFromPullRequest();
};

void EngineCatalogTests::catalogIncludesSourceForks()
{
    const EngineCatalogEntry ik = EngineCatalog::entry(QStringLiteral("ik_llama.cpp"));
    QCOMPARE(ik.repo, QStringLiteral("ikawrakow/ik_llama.cpp"));
    QVERIFY(!ik.variants.isEmpty());
    QVERIFY(ik.variants.first().buildFromSource);

    const EngineCatalogEntry adaptive = EngineCatalog::entry(QStringLiteral("llama.cpp-adaptive"));
    QCOMPARE(adaptive.repo, QStringLiteral("LaurentZuijdwijk/llama.cpp"));
    QCOMPARE(adaptive.flavor, QStringLiteral("mtp-fork"));
    QVERIFY(!adaptive.variants.isEmpty());
    QVERIFY(adaptive.variants.first().buildFromSource);
    QCOMPARE(adaptive.variants.first().gpuVendors, QStringList{QStringLiteral("nvidia")});
    QVERIFY(EngineCatalog::sourceBuildDirName(adaptive).contains(QStringLiteral("adaptive")));
    QVERIFY(EngineCatalog::sourceBuildDirName(adaptive) != QStringLiteral("llama.cpp"));

    const EngineCatalogEntry kvStreaming =
        EngineCatalog::entry(QStringLiteral("llama.cpp-kv-streaming"));
    QCOMPARE(kvStreaming.repo,
             QStringLiteral("sachin-detrax/llama.cpp-adaptive-kv-streaming"));
    QCOMPARE(kvStreaming.sourceBranch, QStringLiteral("feature/adaptive-kv-stream"));
    QCOMPARE(kvStreaming.flavor, QStringLiteral("kv-streaming"));
    QVERIFY(!kvStreaming.variants.isEmpty());
    QVERIFY(kvStreaming.variants.first().buildFromSource);
    QCOMPARE(kvStreaming.variants.first().gpuVendors,
             QStringList{QStringLiteral("nvidia")});
    QVERIFY(kvStreaming.sourceCMakeArgs.contains(QStringLiteral("-DGGML_CUDA_FA_ALL_QUANTS=ON")));
    QCOMPARE(kvStreaming.sourceBuildTarget, QStringLiteral("llama-server"));
    QVERIFY(EngineCatalog::sourceBuildDirName(kvStreaming).contains(QStringLiteral("kv-streaming")));

    const EngineCatalogEntry lid =
        EngineCatalog::entry(QStringLiteral("llama.cpp-deepseek-lid-cuda"));
    QCOMPARE(lid.repo, QStringLiteral("spencer-zaid/llama.cpp"));
    QCOMPARE(lid.sourceBranch, QStringLiteral("deepseek-lid-cuda"));
    QCOMPARE(lid.flavor, QStringLiteral("deepseek-lid-cuda"));
    QVERIFY(!lid.variants.isEmpty());
    QVERIFY(lid.variants.first().buildFromSource);
    QCOMPARE(lid.sourceBuildTarget, QStringLiteral("llama-server"));
    QVERIFY(lid.sourceCMakeArgs.contains(QStringLiteral("-DLLAMA_BUILD_UI=OFF")));
    QVERIFY(lid.sourceCMakeArgs.contains(QStringLiteral("-DLLAMA_USE_PREBUILT_UI=ON")));
    QVERIFY(EngineCatalog::sourceBuildDirName(lid).contains(QStringLiteral("lid-cuda")));

    const EngineCatalogEntry official = EngineCatalog::entry(QStringLiteral("llama.cpp"));
    QVERIFY(official.variants.size() >= 3);

    const EngineCatalogEntry nanbeige = EngineCatalog::entry(QStringLiteral("nanbeige42"));
    QCOMPARE(nanbeige.repo, QStringLiteral("Nanbeige/llama.cpp"));
    QCOMPARE(nanbeige.sourceBranch, QStringLiteral("nanbeige42"));
    QVERIFY(!nanbeige.variants.isEmpty());
    QVERIFY(nanbeige.variants.first().buildFromSource);
    QCOMPARE(nanbeige.variants.first().gpuVendors, QStringList{QStringLiteral("nvidia")});
}

void EngineCatalogTests::variantCompatibilityReportsReason()
{
    const EngineCatalogEntry beellama = EngineCatalog::entry(QStringLiteral("beellama"));
    HardwareSignals hw;
    hw.platform = QStringLiteral("windows");
    hw.arch = QStringLiteral("x64");
    hw.gpuVendor = QStringLiteral("unknown");

    QString reason;
    QVERIFY(!EngineCatalog::isVariantCompatible(beellama.variants.first(), hw, &reason));
    QVERIFY(reason.contains(QStringLiteral("nvidia"), Qt::CaseInsensitive));
}

void EngineCatalogTests::versionComparisonHandlesBuildTagsAndSemver()
{
    QCOMPARE(EngineCatalog::parseBuildTag(QStringLiteral("b9608")), 9608);
    QVERIFY(EngineCatalog::compareVersions(QStringLiteral("b9608"), QStringLiteral("b9761")) < 0);
    QVERIFY(EngineCatalog::compareVersions(QStringLiteral("v1.115.1"), QStringLiteral("v1.115.2")) < 0);
    QCOMPARE(EngineCatalog::compareVersions(QStringLiteral("0.11"), QStringLiteral("0.11.0")), 0);
}

void EngineCatalogTests::sourceUpdateComparesShortSha()
{
    EngineUpdateStatus st = EngineCatalog::computeUpdateStatus(
        QStringLiteral("0a635dc"), QStringLiteral("9f42aaa123456"), true);
    QVERIFY(st.rebuild);
    QVERIFY(st.comparable);
    QVERIFY(st.hasUpdate);

    st = EngineCatalog::computeUpdateStatus(QString(), QStringLiteral("9f42aaa"), true);
    QVERIFY(st.rebuild);
    QVERIFY(!st.comparable);
    QCOMPARE(st.error, QStringLiteral("no_source"));
}

void EngineCatalogTests::repoNormalizationAndBuildDirAreStable()
{
    QCOMPARE(EngineCatalog::normalizeRepo(QStringLiteral("https://github.com/ikawrakow/ik_llama.cpp.git")),
             QStringLiteral("ikawrakow/ik_llama.cpp"));
    QCOMPARE(EngineCatalog::buildDirName(QStringLiteral("https://github.com/a/repo.git"), QStringLiteral("feature/x")),
             QStringLiteral("repo-feature-x"));
}

void EngineCatalogTests::pullRequestRefIsParsedAsBranch()
{
    QCOMPARE(EngineCatalog::parsePullRequestRef(QStringLiteral("pull/27742/head")), 27742);
    QCOMPARE(EngineCatalog::localBranchForRef(QStringLiteral("pull/27742/head")),
             QStringLiteral("pr-27742"));

    // Una rama comun no debe confundirse con un PR: se clona con --branch.
    QCOMPARE(EngineCatalog::parsePullRequestRef(QStringLiteral("master")), 0);
    QCOMPARE(EngineCatalog::parsePullRequestRef(QStringLiteral("pull/abc/head")), 0);
    QCOMPARE(EngineCatalog::parsePullRequestRef(QStringLiteral("pull/27742/merge")), 0);
    QCOMPARE(EngineCatalog::parsePullRequestRef(QString()), 0);
    QCOMPARE(EngineCatalog::localBranchForRef(QStringLiteral("nanbeige42")),
             QStringLiteral("nanbeige42"));
}

void EngineCatalogTests::qwen38NextEntryBuildsFromPullRequest()
{
    const EngineCatalogEntry e = EngineCatalog::entry(QStringLiteral("qwen38-next"));
    QCOMPARE(e.repo, QStringLiteral("ggml-org/llama.cpp"));
    QCOMPARE(e.sourceBranch, QStringLiteral("pull/27742/head"));
    QVERIFY(EngineCatalog::parsePullRequestRef(e.sourceBranch) > 0);
    QCOMPARE(e.sourceBuildTarget, QStringLiteral("llama-server"));
    QVERIFY(!e.variants.isEmpty());
    QVERIFY(e.variants.first().buildFromSource);
    QVERIFY(!e.variants.first().hasPrebuilt);

    // El arbol del PR no debe pisar el build del llama.cpp oficial.
    const EngineCatalogEntry official = EngineCatalog::entry(QStringLiteral("llama.cpp"));
    QVERIFY(EngineCatalog::sourceBuildDirName(e)
            != EngineCatalog::sourceBuildDirName(official));
    QCOMPARE(EngineCatalog::sourceBuildDirName(e), QStringLiteral("llama.cpp-qwen38-next"));
}

QTEST_MAIN(EngineCatalogTests)
#include "test_engine_catalog.moc"
