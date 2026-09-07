// Tests de perfiles de SISTEMA (bundled, fast-start por hardware):
//   - ProfileManager: carga del bundle, inmutabilidad (no borrar/editar/fav),
//     no-persistencia, duplicar a copia editable, modelId determinista por ruta.
//   - AppController: recommendedSystemProfile elige el tier ≤ hardware correcto.
//
// Aislamiento: LLAMACODE_PROFILES_DIR (temp) + LLAMACODE_SYSTEM_PROFILES (bundle
// del repo) seteados en initTestCase ANTES de construir el primer ProfileManager
// (storagePath cachea la raíz en un static). QStandardPaths en modo test.

#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QUuid>
#include <QCoreApplication>
#include <algorithm>
#include "core/profiles/ProfileManager.h"
#include "core/profiles/SystemProfileVariants.h"
#include "core/profiles/MtpDetection.h"
#include "AppController.h"

// Bundle resuelto relativo al repo (ctest corre con WORKING_DIRECTORY = source dir).
static QString bundlePath()
{
    const QStringList candidates = {
        QDir::current().absoluteFilePath(QStringLiteral("assets/system_profiles.json")),
        QDir::current().absoluteFilePath(QStringLiteral("../assets/system_profiles.json")),
        QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(QStringLiteral("../../assets/system_profiles.json")),
    };
    for (const QString &c : candidates)
        if (QFile::exists(c))
            return c;
    return candidates.first();
}

class SystemProfilesTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void manager_loadsSystemProfiles();
    void manager_ninferProfilesAreBenchmarkCandidates();
    void manager_vllmDflashProfilesAreExternalBenchmarks();
    void manager_16gbQwen38CandidatesPreservePostTuning();
    void manager_lingProfilesAreBenchmarkCandidates();
    void manager_systemNotPersisted();
    void manager_immutable();
    void manager_duplicateMakesEditableCopy();
    void manager_duplicatePreservesLaunchLinkage();
    void manager_modelIdIsDeterministic();
    void manager_fastGemmaDflashWired();
    void manager_systemProfilesAvoidAccidentalVisionAndMtp();
    void bundle_draftMtpAlwaysDeclaresDraftModel();
    void bundle_gemma4TemplateKeepsLlamaCppMarkers();
    void bundle_lagunaTemplateIsAppliedToBothProfiles();
    void manager_smallProfilesAreConservative();
    void manager_defaultCodingProfileUsesKatCoder();
    void manager_16gbCodingProfileUsesBenchmarkedKatCoder();
    void manager_24gbPremiumPromotesThinkingCapAndKeepsMaxCtx();
    void bundle_qwen38VariantsAreMtpVisionAndTemplated();
    void bundle_qwen38Turing24gbControlsAreColdAndSeparated();
    void bundle_qwen38Q6BenchmarkFamilyIsGatedAndExpanded();
    void bundle_qwen38Q8BenchmarkFamilyIsGatedAndExpanded();
    void bundle_bigBangDisablesCrashyFlashAttention();
    void bundle_quantizationPolicyCapsKvAtQ8();
    void bundle_bestProfilesUseRequestedCategoryNames();

    void controller_recommendsClosestTier();
    void controller_recommendedTierIncludesDisplayName();
    void controller_recommendsCpuWhenNoGpu();
    void controller_noneWhenBelowMinimum();
    void controller_showcase8gbOffersGemmaAndQwen();
    void controller_showcase24gbUnchanged();
    void controller_showcaseEmptyWhenNoSiblings();
    void bundle_lagunaIsOptInAndHardwareGated();
    void bundle_ultraQAndHybridAreWiredAndOptIn();
    void bundle_deepSeekLidUsesDedicatedExperimentalBinaryAndF16Kv();
    void bundle_katApexMtpVisionIsOptInAndWired();
    void bundle_miniMaxIsOptInAndMemoryGated();
    void bundle_ultraQ48gbIsDualGpuVariantOfUltraQ();
    void controller_launchMenuGatesByTotalVramAcrossGpus();
    void controller_launchMenuAnnotatesGpuAffinity();
    void bundle_48gbFamilyIsBenchmarkableAndDualGpu();
    void controller_duplicateBakesResolvedBinary();

private:
    QTemporaryDir m_dir;
};

void SystemProfilesTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("LlamaCode"));
    QCoreApplication::setApplicationName(QStringLiteral("LlamaCode"));
    QVERIFY(m_dir.isValid());
    qputenv("LLAMACODE_PROFILES_DIR", m_dir.path().toLocal8Bit());
    // ProfileManager acepta LLAMACODE_MODELS_DIR como override de runtime,
    // pero este test verifica los IDs contra QStandardPaths. No dejar que una
    // variable del entorno del proceso vuelva no determinista la expectativa.
    qunsetenv("LLAMACODE_MODELS_DIR");
    const QString bundle = bundlePath();
    QVERIFY2(QFile::exists(bundle), "falta assets/system_profiles.json");
    qputenv("LLAMACODE_SYSTEM_PROFILES", bundle.toLocal8Bit());
}

void SystemProfilesTests::manager_loadsSystemProfiles()
{
    ProfileManager pm;
    auto *m = pm.launchProfiles();
    int sys = 0;
    QString anySysId;
    for (int r = 0; r < m->rowCount(); ++r) {
        if (m->data(m->index(r), ProfileListModel<LaunchProfile>::SystemRole).toBool()) {
            ++sys;
            anySysId = m->data(m->index(r), ProfileListModel<LaunchProfile>::IdRole).toString();
        }
    }
    QVERIFY2(sys >= 68, "el bundle debe conservar al menos los perfiles base y variantes existentes");
    QVERIFY(pm.isSystemLaunch("sys-vram-16"));
    QVERIFY(!anySysId.isEmpty());
    // Visión: solo los perfiles Gemma vision dedicados llevan mmproj. Los perfiles
    // Qwen/coding y Gemma chicos no deben cargar projector para una automatización
    // textual: aumenta memoria/prompt y no ayuda a desktop_controls.
    const QString mp16 = pm.getLaunchProfile("sys-vram-16").value("modelProfileId").toString();
    QVERIFY(pm.getModelProfile(mp16).value("mmprojId").toString().isEmpty());
    const QString mp4 = pm.getLaunchProfile("sys-vram-4").value("modelProfileId").toString();
    QVERIFY(pm.getModelProfile(mp4).value("mmprojId").toString().isEmpty());
    // El tier 8GB Gemma tiene visión (gemma4uv): mmproj presente, offload a CPU via
    // --no-mmproj-offload. Requiere llama-server b9496+ en runtime. Q3_K_XL deja
    // margen para MTP self-draft (mtp-gemma-4-12b-it.gguf, --spec-type draft-mtp).
    const QString mp8 = pm.getLaunchProfile("sys-vram-8-gemma").value("modelProfileId").toString();
    const QVariantMap m8 = pm.getModelProfile(mp8);
    QVERIFY(!m8.value("mmprojId").toString().isEmpty());
    QCOMPARE(m8.value("specType").toString(), QStringLiteral("draft-mtp"));
    QVERIFY(!m8.value("draftModelId").toString().isEmpty());
}

void SystemProfilesTests::manager_ninferProfilesAreBenchmarkCandidates()
{
    ProfileManager pm;
    const QStringList ids = {
        QStringLiteral("sys-ninfer3090-qwen27"),
        QStringLiteral("sys-ninfer3090-qwen35"),
        QStringLiteral("sys-ninfer3090-qwen38")};
    const QVariantList menu = pm.launchProfilesForMenu();

    for (const QString &id : ids) {
        const QVariantMap launch = pm.getLaunchProfile(id);
        QVERIFY2(!launch.isEmpty(), qPrintable(id));
        QVERIFY2(!launch.value(QStringLiteral("benchmark")).toBool(), qPrintable(id));

        bool found = false;
        for (const QVariant &value : menu) {
            const QVariantMap item = value.toMap();
            if (item.value(QStringLiteral("id")).toString() != id)
                continue;
            found = true;
            QVERIFY2(!item.value(QStringLiteral("displayName")).toString().contains(QStringLiteral("🏆")),
                     qPrintable(id));
            break;
        }
        QVERIFY2(found, qPrintable(id));
    }

    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    QJsonObject qwen38;
    for (const QJsonValue &value : profiles) {
        if (value.toObject().value(QStringLiteral("id"))
                .toString() == QStringLiteral("sys-ninfer3090-qwen38")) {
            qwen38 = value.toObject();
            break;
        }
    }
    QVERIFY(!qwen38.isEmpty());
    QCOMPARE(qwen38.value(QStringLiteral("binaryKind")).toString(),
             QStringLiteral("ninfer3090"));
    QCOMPARE(qwen38.value(QStringLiteral("model")).toObject()
                 .value(QStringLiteral("file")).toString(),
             QStringLiteral("qwen3_8_27b.ninfer"));
    QVERIFY(qwen38.value(QStringLiteral("comment")).toString()
                .contains(QStringLiteral("no ejecuta tool calls")));
}

void SystemProfilesTests::manager_vllmDflashProfilesAreExternalBenchmarks()
{
    const QStringList ids = {
        QStringLiteral("sys-bench-qwen38-dflash2-vllm-262k"),
        QStringLiteral("sys-bench-qwen38-dflash2-vllm-ar-262k")};
    ProfileManager pm;

    for (const QString &id : ids) {
        const QVariantMap launch = pm.getLaunchProfile(id);
        QVERIFY2(!launch.isEmpty(), qPrintable(id));
        QVERIFY2(launch.value(QStringLiteral("system")).toBool(), qPrintable(id));
        QVERIFY2(!launch.value(QStringLiteral("benchmark")).toBool(), qPrintable(id));
        QVERIFY2(launch.value(QStringLiteral("modelProfileId")).toString().isEmpty(),
                 qPrintable(id));
        QVERIFY2(launch.value(QStringLiteral("runtimePresetId")).toString().isEmpty(),
                 qPrintable(id));

        const QVariantMap backend = pm.getBackend(
            launch.value(QStringLiteral("backendProfileId")).toString());
        QCOMPARE(backend.value(QStringLiteral("kind")).toString(), QStringLiteral("cloud"));
        QCOMPARE(backend.value(QStringLiteral("cloudBaseUrl")).toString(),
                 id.endsWith(QStringLiteral("-ar-262k"))
                     ? QStringLiteral("http://127.0.0.1:8001")
                     : QStringLiteral("http://127.0.0.1:8000"));
        QCOMPARE(backend.value(QStringLiteral("cloudModel")).toString(),
                 QStringLiteral("lued/Qwen3.8-27B-INT8-W8A16-DFlash2"));
        QCOMPARE(backend.value(QStringLiteral("cloudCtx")).toInt(), 262144);
    }

    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    QJsonObject dflash;
    QJsonObject autoregressive;
    for (const QJsonValue &value : profiles) {
        const QJsonObject profile = value.toObject();
        const QString id = profile.value(QStringLiteral("id")).toString();
        if (id == ids.at(0)) dflash = profile;
        if (id == ids.at(1)) autoregressive = profile;
    }
    QVERIFY(!dflash.isEmpty());
    QVERIFY(!autoregressive.isEmpty());
    QCOMPARE(dflash.value(QStringLiteral("backend")).toObject()
                 .value(QStringLiteral("kind")).toString(), QStringLiteral("cloud"));
    const QJsonObject external = dflash.value(QStringLiteral("external")).toObject();
    QCOMPARE(external.value(QStringLiteral("targetModel")).toString(),
             QStringLiteral("lued/Qwen3.8-27B-INT8-W8A16-DFlash2"));
    QCOMPARE(external.value(QStringLiteral("drafterModel")).toString(),
             QStringLiteral("lued/Qwen3.8-27B-DFlash2-W8"));
    QCOMPARE(external.value(QStringLiteral("speculativeMethod")).toString(),
             QStringLiteral("dflash"));
    QCOMPARE(external.value(QStringLiteral("numSpeculativeTokens")).toInt(), 7);
    QCOMPARE(external.value(QStringLiteral("tensorParallelSize")).toInt(), 2);
    QCOMPARE(external.value(QStringLiteral("maxModelLen")).toInt(), 262144);
    QCOMPARE(external.value(QStringLiteral("kvCacheDtype")).toString(),
             QStringLiteral("fp8_e4m3"));
    QCOMPARE(external.value(QStringLiteral("patchRepo")).toString(),
             QStringLiteral("noonghunna/club-3090"));
    QCOMPARE(autoregressive.value(QStringLiteral("external")).toObject()
                 .value(QStringLiteral("speculativeMethod")).toString(),
             QStringLiteral("none"));
}

void SystemProfilesTests::manager_16gbQwen38CandidatesPreservePostTuning()
{
    const QStringList ids = {
        QStringLiteral("sys-bench-16-qwen38-rvn-iq3xxs-ngram-131k"),
        QStringLiteral("sys-bench-16-qwen38-rvn-iq3xxs-dflash2-ngram-105k"),
        QStringLiteral("sys-bench-16-qwen38-rvn-iq3xxs-mtp-ngram-105k")};

    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    ProfileManager pm;

    for (const QString &id : ids) {
        QJsonObject entry;
        for (const QJsonValue &value : profiles) {
            if (value.toObject().value(QStringLiteral("id")).toString() == id) {
                entry = value.toObject();
                break;
            }
        }
        QVERIFY2(!entry.isEmpty(), qPrintable(id));
        QVERIFY(!entry.value(QStringLiteral("benchmark")).toBool());
        QCOMPARE(entry.value(QStringLiteral("minVramGb")).toInt(), 16);
        QCOMPARE(entry.value(QStringLiteral("minRamGb")).toInt(), 32);
        QCOMPARE(entry.value(QStringLiteral("binaryKind")).toString(), QStringLiteral("beellama"));
        QCOMPARE(entry.value(QStringLiteral("reasoningEffort")).toString(), QStringLiteral("medium"));
        QCOMPARE(entry.value(QStringLiteral("chatTemplate")).toString(),
                 QStringLiteral("qwen38-tools-fixed.jinja"));
        QVERIFY(entry.value(QStringLiteral("model")).toObject()
                    .value(QStringLiteral("mmprojFile")).toString().isEmpty());

        const QJsonObject runtime = entry.value(QStringLiteral("runtime")).toObject();
        QCOMPARE(runtime.value(QStringLiteral("ctx")).toInt(),
                 id.endsWith(QStringLiteral("131k")) ? 131072 : 105000);
        QCOMPARE(runtime.value(QStringLiteral("batch")).toInt(), 512);
        QCOMPARE(runtime.value(QStringLiteral("ubatch")).toInt(), 512);
        QCOMPARE(runtime.value(QStringLiteral("kv")).toString(), QStringLiteral("q5_1"));

        const QVariantMap launch = pm.getLaunchProfile(id);
        QVERIFY2(!launch.isEmpty(), qPrintable(id));
        QVERIFY(!launch.value(QStringLiteral("benchmark")).toBool());
        QCOMPARE(launch.value(QStringLiteral("agentProfileId")).toString(),
                 QStringLiteral("agent-maximo"));
        QCOMPARE(launch.value(QStringLiteral("reasoningEffort")).toString(),
                 QStringLiteral("medium"));
        const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
        const auto valueAfter = [&args](const QString &flag) {
            const int index = args.indexOf(flag);
            return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : QString();
        };
        for (const QString &flag : {QStringLiteral("--cache-type-k"),
                                    QStringLiteral("--cache-type-v"),
                                    QStringLiteral("--fit"), QStringLiteral("--temp"),
                                    QStringLiteral("--top-p"), QStringLiteral("--top-k"),
                                    QStringLiteral("--min-p"),
                                    QStringLiteral("--repeat-penalty"),
                                    QStringLiteral("--presence-penalty"),
                                    QStringLiteral("--parallel")})
            QVERIFY2(args.contains(flag), qPrintable(id + " falta " + flag));
        QCOMPARE(valueAfter(QStringLiteral("--temp")), QStringLiteral("0.60"));
        QCOMPARE(valueAfter(QStringLiteral("--top-p")), QStringLiteral("0.95"));
        QCOMPARE(valueAfter(QStringLiteral("--top-k")), QStringLiteral("20"));
        QCOMPARE(valueAfter(QStringLiteral("--min-p")), QStringLiteral("0.0"));
        QCOMPARE(valueAfter(QStringLiteral("--repeat-penalty")), QStringLiteral("1.0"));
        QCOMPARE(valueAfter(QStringLiteral("--presence-penalty")), QStringLiteral("0.0"));
        QCOMPARE(valueAfter(QStringLiteral("--cache-type-k")), QStringLiteral("q5_1"));
        QCOMPARE(valueAfter(QStringLiteral("--cache-type-v")), QStringLiteral("q5_1"));
        QCOMPARE(valueAfter(QStringLiteral("--parallel")), QStringLiteral("1"));
        QCOMPARE(valueAfter(QStringLiteral("--spec-ngram-mod-n-min")), QStringLiteral("4"));
        QCOMPARE(valueAfter(QStringLiteral("--spec-ngram-mod-n-max")), QStringLiteral("8"));
        QCOMPARE(valueAfter(QStringLiteral("--spec-ngram-mod-n-match")), QStringLiteral("32"));

        const QVariantMap model = pm.getModelProfile(
            launch.value(QStringLiteral("modelProfileId")).toString());
        if (id.contains(QStringLiteral("dflash2"))) {
            QVERIFY(!model.value(QStringLiteral("draftModelId")).toString().isEmpty());
            QCOMPARE(model.value(QStringLiteral("specType")).toString(),
                     QStringLiteral("draft-dflash"));
            QCOMPARE(valueAfter(QStringLiteral("--spec-type")),
                     QStringLiteral("draft-dflash,ngram-mod"));
            QCOMPARE(entry.value(QStringLiteral("draftModel")).toObject()
                         .value(QStringLiteral("file")).toString(),
                     QStringLiteral("Qwen3.8-27B-DFlash2-Q4_K_M.gguf"));
        } else if (!id.contains(QStringLiteral("mtp-ngram"))) {
            QVERIFY(model.value(QStringLiteral("draftModelId")).toString().isEmpty());
            QVERIFY(args.contains(QStringLiteral("ngram-mod")));
        }
        if (id.contains(QStringLiteral("mtp-ngram"))) {
            QVERIFY(entry.value(QStringLiteral("manualOnly")).toBool());
            QVERIFY(entry.value(QStringLiteral("mtp")).toObject()
                        .value(QStringLiteral("selfContained")).toBool());
            QVERIFY(args.contains(QStringLiteral("draft-mtp,ngram-mod")));
            QVERIFY(args.contains(QStringLiteral("--spec-draft-n-max")));
        }
    }
}

void SystemProfilesTests::manager_lingProfilesAreBenchmarkCandidates()
{
    ProfileManager pm;
    const QStringList ids = {
        QStringLiteral("sys-ling30-tiny-q6-131k"),
        QStringLiteral("sys-bench-ling30-tiny-q6-64k"),
        QStringLiteral("sys-bench-ling30-tiny-q6-thinking-131k"),
        QStringLiteral("sys-bench-ling30-tiny-q6-kv4-131k"),
        QStringLiteral("sys-bench-ling30-tiny-udq4-64k"),
        QStringLiteral("sys-hybrid-ling30-qwen38")};

    for (const QString &id : ids) {
        const QVariantMap launch = pm.getLaunchProfile(id);
        QVERIFY2(!launch.isEmpty(), qPrintable(id));
        QVERIFY2(launch.value(QStringLiteral("system")).toBool(), qPrintable(id));
        QVERIFY2(!launch.value(QStringLiteral("benchmark")).toBool(), qPrintable(id));
    }

    const QVariantMap ling = pm.getLaunchProfile(QStringLiteral("sys-ling30-tiny-q6-131k"));
    const QVariantMap lingModel = pm.getModelProfile(
        ling.value(QStringLiteral("modelProfileId")).toString());
    QVERIFY(lingModel.value(QStringLiteral("mmprojId")).toString().isEmpty());
    const QVariantMap lingRuntime = pm.getRuntimePreset(
        ling.value(QStringLiteral("runtimePresetId")).toString());
    QCOMPARE(lingRuntime.value(QStringLiteral("ctx")).toInt(), 131072);
    QCOMPARE(lingRuntime.value(QStringLiteral("cacheType")).toString(), QStringLiteral("q8_0"));
    const QStringList lingArgs = ling.value(QStringLiteral("extraArgs")).toStringList();
    const int kwargs = lingArgs.indexOf(QStringLiteral("--chat-template-kwargs"));
    QVERIFY(kwargs >= 0 && kwargs + 1 < lingArgs.size());
    QCOMPARE(lingArgs.at(kwargs + 1), QStringLiteral("{\"enable_thinking\":false}"));

    const QVariantMap shortLing = pm.getLaunchProfile(
        QStringLiteral("sys-bench-ling30-tiny-q6-64k"));
    QCOMPARE(pm.getRuntimePreset(shortLing.value(QStringLiteral("runtimePresetId")).toString())
                 .value(QStringLiteral("ctx")).toInt(), 65536);

    const QVariantMap thinkingLing = pm.getLaunchProfile(
        QStringLiteral("sys-bench-ling30-tiny-q6-thinking-131k"));
    const QStringList thinkingArgs = thinkingLing.value(QStringLiteral("extraArgs")).toStringList();
    const int thinkingKwargs = thinkingArgs.indexOf(QStringLiteral("--chat-template-kwargs"));
    QVERIFY(thinkingKwargs >= 0 && thinkingKwargs + 1 < thinkingArgs.size());
    QCOMPARE(thinkingArgs.at(thinkingKwargs + 1), QStringLiteral("{\"enable_thinking\":true}"));

    const QVariantMap kv4Ling = pm.getLaunchProfile(
        QStringLiteral("sys-bench-ling30-tiny-q6-kv4-131k"));
    QCOMPARE(pm.getRuntimePreset(kv4Ling.value(QStringLiteral("runtimePresetId")).toString())
                 .value(QStringLiteral("cacheType")).toString(), QStringLiteral("q4_0"));

    const QVariantMap hybrid = pm.getLaunchProfile(QStringLiteral("sys-hybrid-ling30-qwen38"));
    QCOMPARE(hybrid.value(QStringLiteral("plannerProfileId")).toString(),
             QStringLiteral("sys-ling30-tiny-q6-131k"));
    QCOMPARE(hybrid.value(QStringLiteral("hybridMode")).toString(), QStringLiteral("sequential"));
    QVERIFY(!hybrid.value(QStringLiteral("modelProfileId")).toString().isEmpty());
    QVERIFY(hybrid.value(QStringLiteral("modelProfileId")).toString()
                != ling.value(QStringLiteral("modelProfileId")).toString());

    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    QJsonObject q6;
    QJsonObject q4;
    QJsonObject hybridJson;
    for (const QJsonValue &value : profiles) {
        const QJsonObject profile = value.toObject();
        const QString id = profile.value(QStringLiteral("id")).toString();
        if (id == QStringLiteral("sys-ling30-tiny-q6-131k")) q6 = profile;
        if (id == QStringLiteral("sys-bench-ling30-tiny-udq4-64k")) q4 = profile;
        if (id == QStringLiteral("sys-hybrid-ling30-qwen38")) hybridJson = profile;
    }
    QVERIFY(!q6.isEmpty());
    QVERIFY(!q4.isEmpty());
    QVERIFY(!hybridJson.isEmpty());
    QCOMPARE(q6.value(QStringLiteral("model")).toObject().value(QStringLiteral("repo")).toString(),
             QStringLiteral("bloomer010/Ling-3.0-tiny-GGUF"));
    QCOMPARE(q6.value(QStringLiteral("model")).toObject().value(QStringLiteral("file")).toString(),
             QStringLiteral("Ling-3.0-tiny-Q6_K.gguf"));
    QCOMPARE(q4.value(QStringLiteral("model")).toObject().value(QStringLiteral("quant")).toString(),
             QStringLiteral("UD-Q4_K_XL"));
    QCOMPARE(hybridJson.value(QStringLiteral("plannerProfileId")).toString(),
             QStringLiteral("sys-ling30-tiny-q6-131k"));
    QCOMPARE(hybridJson.value(QStringLiteral("hybridMode")).toString(),
             QStringLiteral("sequential"));
    QVERIFY(hybridJson.value(QStringLiteral("comment")).toString()
                .contains(QStringLiteral("No implementa todavía routing automático")));
}

void SystemProfilesTests::manager_systemNotPersisted()
{
    { ProfileManager pm; pm.saveProfiles(); }   // fuerza save con system en memoria
    // El launches.json en disco NO debe contener perfiles de sistema.
    QFile f(m_dir.path() + "/launches.json");
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray raw = f.readAll();
        QVERIFY(!raw.contains("sys-vram-16"));
    }
    // Y al reconstruir, los de sistema reaparecen del bundle.
    ProfileManager pm2;
    QVERIFY(pm2.isSystemLaunch("sys-vram-16"));
}

void SystemProfilesTests::manager_immutable()
{
    ProfileManager pm;
    QVERIFY(!pm.removeLaunchProfile("sys-vram-12-moe"));
    QVERIFY(!pm.updateLaunchProfile(QVariantMap{{"id", "sys-vram-12-moe"}, {"name", "hack"}}));
    pm.setLaunchFavorite("sys-vram-12-moe", true);
    QVERIFY(!pm.getLaunchProfile("sys-vram-12-moe").value("favorite").toBool());
    pm.setLaunchAlias("sys-vram-12-moe", "hack");
    QCOMPARE(pm.getLaunchProfile("sys-vram-12-moe").value("alias").toString(), QStringLiteral("12GB"));
}

void SystemProfilesTests::manager_duplicateMakesEditableCopy()
{
    ProfileManager pm;
    const QString dup = pm.duplicateLaunchProfile("sys-vram-12-moe");
    QVERIFY(!dup.isEmpty());
    QVERIFY(!pm.isSystemLaunch(dup));
    // La copia ES editable (rename/fav OK).
    QVERIFY(pm.updateLaunchProfile(QVariantMap{{"id", dup}, {"name", "mio"}}));
    // backing clonado: ids distintos a los del perfil de sistema.
    const QVariantMap src = pm.getLaunchProfile("sys-vram-12-moe");
    const QVariantMap cp = pm.getLaunchProfile(dup);
    QVERIFY(cp.value("modelProfileId").toString() != src.value("modelProfileId").toString());
    QVERIFY(cp.value("runtimePresetId").toString() != src.value("runtimePresetId").toString());
}

void SystemProfilesTests::manager_duplicatePreservesLaunchLinkage()
{
    ProfileManager pm;
    const QVariantMap src = pm.getLaunchProfile("sys-vram-12-moe");
    const QString dup = pm.duplicateLaunchProfile("sys-vram-12-moe");
    QVERIFY(!dup.isEmpty());
    const QVariantMap cp = pm.getLaunchProfile(dup);
    QVERIFY(!cp.value("backendProfileId").toString().isEmpty());
    QVERIFY(!cp.value("modelProfileId").toString().isEmpty());
    QVERIFY(!cp.value("runtimePresetId").toString().isEmpty());
    QVERIFY(!pm.getBackend(cp.value("backendProfileId").toString()).isEmpty());
    QVERIFY(!pm.getModelProfile(cp.value("modelProfileId").toString()).isEmpty());
    QVERIFY(!pm.getRuntimePreset(cp.value("runtimePresetId").toString()).isEmpty());
    QVERIFY(cp.value("backendProfileId").toString() != src.value("backendProfileId").toString());
    QVERIFY(cp.value("modelProfileId").toString() != src.value("modelProfileId").toString());
    QVERIFY(cp.value("runtimePresetId").toString() != src.value("runtimePresetId").toString());
}

void SystemProfilesTests::manager_modelIdIsDeterministic()
{
    ProfileManager pm;
    const QString mpId = pm.getLaunchProfile("sys-vram-4").value("modelProfileId").toString();
    const QString modelId = pm.getModelProfile(mpId).value("modelId").toString();
    const QString modelsDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/models";
    const QUuid ns(QStringLiteral("a1b2c3d4-e5f6-4a5b-8c7d-0e1f2a3b4c5d"));
    const QString expect = QUuid::createUuidV5(
        ns, QString(modelsDir + "/Qwen3.5-4B/Qwen3.5-4B-Q4_K_M.gguf").toUtf8()).toString(QUuid::WithoutBraces);
    QCOMPARE(modelId, expect);
}

void SystemProfilesTests::manager_fastGemmaDflashWired()
{
    ProfileManager pm;
    // FAST GEMMA: DFlash = target + draft + specType "dflash" (inmutable).
    const QString mpId = pm.getLaunchProfile("sys-fastgemma").value("modelProfileId").toString();
    const QVariantMap mp = pm.getModelProfile(mpId);
    QCOMPARE(mp.value("specType").toString(), QStringLiteral("draft-mtp"));
    QVERIFY(!mp.value("draftModelId").toString().isEmpty());
    QVERIFY(!mp.value("modelId").toString().isEmpty());
    // No se puede editar el original; sí su duplicado (copia editable).
    QVERIFY(!pm.updateLaunchProfile(QVariantMap{{"id","sys-fastgemma"},{"name","x"}}));
    const QString dup = pm.duplicateLaunchProfile("sys-fastgemma");
    QVERIFY(!dup.isEmpty());
    QVERIFY(!pm.isSystemLaunch(dup));
    QVERIFY(pm.updateLaunchProfile(QVariantMap{{"id",dup},{"name","mi-gemma"}}));
}

void SystemProfilesTests::manager_systemProfilesAvoidAccidentalVisionAndMtp()
{
    // La visión intencional se declara con "vision": true en el bundle, no por el
    // texto del displayName: un perfil puede llamarse como el usuario quiera
    // ("ThinkingCap+MTP-7-8-26") y seguir cargando mmproj a propósito. El chequeo
    // sigue existiendo para lo que importa — que nadie arrastre un mmproj sin
    // querer, porque cuesta VRAM.
    QSet<QString> declaresVision;
    QHash<QString, QString> modelFilesByLaunchId;
    {
        QFile bundle(bundlePath());
        QVERIFY(bundle.open(QIODevice::ReadOnly));
        for (const QJsonValue &v : QJsonDocument::fromJson(bundle.readAll()).array()) {
            const QJsonObject o = v.toObject();
            const QString id = o.value(QStringLiteral("id")).toString();
            const QString file = o.value(QStringLiteral("model")).toObject()
                                     .value(QStringLiteral("file")).toString();
            if (!id.isEmpty() && !file.isEmpty())
                modelFilesByLaunchId.insert(id, file);
            if (o.value(QStringLiteral("vision")).toBool()) {
                declaresVision.insert(id);
                // Las variantes declarativas heredan mmproj/vision del perfil base
                // al expandirse en ProfileManager; reconocer también sus IDs.
                for (const QJsonValue &variant :
                     o.value(QStringLiteral("benchmarkVariants")).toArray()) {
                    const QString variantId =
                        variant.toObject().value(QStringLiteral("id")).toString();
                    declaresVision.insert(variantId);
                    if (!variantId.isEmpty() && !file.isEmpty())
                        modelFilesByLaunchId.insert(variantId, file);
                }
            }
        }
    }

    ProfileManager pm;
    auto *m = pm.launchProfiles();
    for (int r = 0; r < m->rowCount(); ++r) {
        const QModelIndex idx = m->index(r);
        if (!m->data(idx, ProfileListModel<LaunchProfile>::SystemRole).toBool())
            continue;
        const QString launchId = m->data(idx, ProfileListModel<LaunchProfile>::IdRole).toString();
        const QVariantMap launch = pm.getLaunchProfile(launchId);
        const QVariantMap model = pm.getModelProfile(launch.value("modelProfileId").toString());
        const QString name = launch.value("name").toString().toLower();
        const bool isVisionProfile = name.contains(QStringLiteral("visión"))
                                     || name.contains(QStringLiteral("vision"))
                                     || declaresVision.contains(launchId);
        if (!isVisionProfile) {
            QVERIFY2(model.value("mmprojId").toString().isEmpty(),
                     qPrintable(QStringLiteral("%1 carga mmproj sin ser perfil de visión")
                                    .arg(launchId)));
        }

        const bool hasSpec = !model.value("specType").toString().isEmpty()
                             || model.value("specDraftNMax").toInt() > 0;
        if (hasSpec) {
            // MTP puede ir integrado en el GGUF (por ejemplo KAT/APEX-MTP),
            // por lo que no corresponde exigir un segundo archivo draft.
            const bool selfContainedMtp =
                model.value("specType").toString() == QStringLiteral("draft-mtp")
                && model.value("draftModelId").toString().isEmpty()
                && MtpDetection::isSelfContained(modelFilesByLaunchId.value(launchId));
            QVERIFY2(!model.value("draftModelId").toString().isEmpty() || selfContainedMtp,
                     qPrintable(QStringLiteral("%1 declara speculative/MTP sin draftModel")
                                    .arg(launchId)));
        }
    }
}

void SystemProfilesTests::bundle_draftMtpAlwaysDeclaresDraftModel()
{
    QFile f(bundlePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    QVERIFY(!arr.isEmpty());
    for (const QJsonValue &value : arr) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value(QStringLiteral("id")).toString();
        QStringList specTokens;
        const QJsonObject mtp = entry.value(QStringLiteral("mtp")).toObject();
        if (mtp.value(QStringLiteral("enabled")).toBool()) {
            for (const QJsonValue &arg : mtp.value(QStringLiteral("args")).toArray())
                specTokens << arg.toString();
        }
        for (const QJsonValue &arg : entry.value(QStringLiteral("extraArgs")).toArray())
            specTokens << arg.toString();
        const QJsonObject spec = entry.value(QStringLiteral("spec")).toObject();
        const bool declaresDraftMtp =
            spec.value(QStringLiteral("type")).toString().contains(QStringLiteral("draft"), Qt::CaseInsensitive)
            || specTokens.contains(QStringLiteral("draft-mtp"), Qt::CaseInsensitive);
        if (!declaresDraftMtp)
            continue;
        const QJsonObject draft = entry.value(QStringLiteral("draftModel")).toObject();
        const QString modelFile =
            entry.value(QStringLiteral("model")).toObject().value(QStringLiteral("file")).toString();
        const bool selfContained = MtpDetection::isSelfContained(modelFile)
            || mtp.value(QStringLiteral("selfContained")).toBool();
        QVERIFY2(selfContained
                     || (!draft.value(QStringLiteral("repo")).toString().isEmpty()
                         && !draft.value(QStringLiteral("file")).toString().isEmpty()),
                 qPrintable(QStringLiteral("%1 declara draft-mtp pero no draftModel repo/file")
                                .arg(id)));
    }
}

void SystemProfilesTests::manager_smallProfilesAreConservative()
{
    ProfileManager pm;
    const auto assertRt = [&](const QString &launchId, int expectedCtx, int maxBatch, int maxLayers) {
        const QVariantMap launch = pm.getLaunchProfile(launchId);
        const QVariantMap rt = pm.getRuntimePreset(launch.value("runtimePresetId").toString());
        QCOMPARE(rt.value("ctx").toInt(), expectedCtx);
        QVERIFY2(rt.value("ctx").toInt() >= 8192,
                 qPrintable(QStringLiteral("%1 ctx=%2").arg(launchId).arg(rt.value("ctx").toInt())));
        QVERIFY2(rt.value("batch").toInt() <= maxBatch,
                 qPrintable(QStringLiteral("%1 batch=%2").arg(launchId).arg(rt.value("batch").toInt())));
        QVERIFY2(rt.value("ubatch").toInt() <= maxBatch,
                 qPrintable(QStringLiteral("%1 ubatch=%2").arg(launchId).arg(rt.value("ubatch").toInt())));
        QVERIFY2(rt.value("gpuLayers").toInt() <= maxLayers,
                 qPrintable(QStringLiteral("%1 gpuLayers=%2")
                                .arg(launchId).arg(rt.value("gpuLayers").toInt())));
    };
    assertRt(QStringLiteral("sys-vram-4-gemma"), 8192, 128, 12);
    assertRt(QStringLiteral("sys-vram-2-gemma"), 8192, 64, 8);
    assertRt(QStringLiteral("sys-vram-2"), 8192, 64, 8);
    assertRt(QStringLiteral("sys-vram-0"), 8192, 128, 0);

    const QVariantMap cpuLaunch = pm.getLaunchProfile(QStringLiteral("sys-vram-0"));
    const QVariantMap cpuRt = pm.getRuntimePreset(cpuLaunch.value("runtimePresetId").toString());
    const QVariantMap cpuModel = pm.getModelProfile(cpuLaunch.value("modelProfileId").toString());
    const QString modelsDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/models";
    const QUuid ns(QStringLiteral("a1b2c3d4-e5f6-4a5b-8c7d-0e1f2a3b4c5d"));
    const QString expect = QUuid::createUuidV5(
        ns, QString(modelsDir + "/Qwen3.5-4B/Qwen3.5-4B-Q4_K_M.gguf").toUtf8()).toString(QUuid::WithoutBraces);
    QCOMPARE(cpuModel.value("modelId").toString(), expect);
    QCOMPARE(cpuRt.value("gpuLayers").toInt(), 0);
    QVERIFY2(!cpuRt.value("flashAttention").toBool(),
             "El fallback CPU no debe depender de flash-attn");

    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    bool sawCpuKind = false;
    for (const QJsonValue &v : QJsonDocument::fromJson(bundle.readAll()).array()) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("id")).toString() != QStringLiteral("sys-vram-0"))
            continue;
        sawCpuKind = true;
        QCOMPARE(o.value(QStringLiteral("binaryKind")).toString(), QStringLiteral("cpu"));
    }
    QVERIFY(sawCpuKind);
}

void SystemProfilesTests::manager_defaultCodingProfileUsesKatCoder()
{
    ProfileManager pm;
    const QVariantMap launch = pm.getLaunchProfile(QStringLiteral("sys-vram-20"));
    QCOMPARE(launch.value(QStringLiteral("name")).toString(),
             QStringLiteral("[coding] 20GB · KAT Coder 2.5 35B-A3B Q4_K_M"));

    const QVariantMap model =
        pm.getModelProfile(launch.value(QStringLiteral("modelProfileId")).toString());
    const QString modelsDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/models";
    const QUuid ns(QStringLiteral("a1b2c3d4-e5f6-4a5b-8c7d-0e1f2a3b4c5d"));
    const QString expectedModelId = QUuid::createUuidV5(
        ns, QString(modelsDir
                    + "/KAT-Coder-V2.5-Dev-Q4_K_M-GGUF/"
                      "Kwaipilot_KAT-Coder-V2.5-Dev-Q4_K_M.gguf").toUtf8())
                                        .toString(QUuid::WithoutBraces);
    QCOMPARE(model.value(QStringLiteral("modelId")).toString(), expectedModelId);

    const QVariantMap runtime =
        pm.getRuntimePreset(launch.value(QStringLiteral("runtimePresetId")).toString());
    QCOMPARE(runtime.value(QStringLiteral("ctx")).toInt(), 32768);
    QCOMPARE(runtime.value(QStringLiteral("gpuLayers")).toInt(), 30);
    QCOMPARE(runtime.value(QStringLiteral("batch")).toInt(), 2048);
    QCOMPARE(runtime.value(QStringLiteral("ubatch")).toInt(), 512);
    QCOMPARE(runtime.value(QStringLiteral("cacheType")).toString(), QStringLiteral("q4_0"));

    const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
    const auto valueAfter = [&args](const QString &flag) {
        const int index = args.indexOf(flag);
        return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : QString();
    };
    QCOMPARE(valueAfter(QStringLiteral("--temp")), QStringLiteral("0.60"));
    QCOMPARE(valueAfter(QStringLiteral("--top-p")), QStringLiteral("0.95"));
    QCOMPARE(valueAfter(QStringLiteral("--top-k")), QStringLiteral("20"));
    QCOMPARE(valueAfter(QStringLiteral("--repeat-penalty")), QStringLiteral("1.0"));
    QCOMPARE(valueAfter(QStringLiteral("--presence-penalty")), QStringLiteral("0.0"));
    QCOMPARE(valueAfter(QStringLiteral("--reasoning")), QStringLiteral("on"));
}

void SystemProfilesTests::manager_16gbCodingProfileUsesBenchmarkedKatCoder()
{
    ProfileManager pm;
    const QVariantMap launch = pm.getLaunchProfile(QStringLiteral("sys-vram-16"));
    QCOMPARE(launch.value(QStringLiteral("name")).toString(),
             QStringLiteral("[coding] 16GB · KAT Coder 2.5 35B-A3B Q4_K_M"));

    const QVariantMap model =
        pm.getModelProfile(launch.value(QStringLiteral("modelProfileId")).toString());
    const QString modelsDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/models";
    const QUuid ns(QStringLiteral("a1b2c3d4-e5f6-4a5b-8c7d-0e1f2a3b4c5d"));
    const QString expectedModelId = QUuid::createUuidV5(
        ns, QString(modelsDir
                    + "/KAT-Coder-V2.5-Dev-Q4_K_M-GGUF/"
                      "Kwaipilot_KAT-Coder-V2.5-Dev-Q4_K_M.gguf").toUtf8())
                                        .toString(QUuid::WithoutBraces);
    QCOMPARE(model.value(QStringLiteral("modelId")).toString(), expectedModelId);

    const QVariantMap runtime =
        pm.getRuntimePreset(launch.value(QStringLiteral("runtimePresetId")).toString());
    QCOMPARE(runtime.value(QStringLiteral("ctx")).toInt(), 32768);
    QCOMPARE(runtime.value(QStringLiteral("gpuLayers")).toInt(), 999);
    QCOMPARE(runtime.value(QStringLiteral("batch")).toInt(), 512);
    QCOMPARE(runtime.value(QStringLiteral("cacheType")).toString(), QStringLiteral("q4_0"));

    const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
    QVERIFY(args.contains(QStringLiteral("--n-cpu-moe")));
    QCOMPARE(args.value(args.indexOf(QStringLiteral("--n-cpu-moe")) + 1),
             QStringLiteral("18"));
    QCOMPARE(args.value(args.indexOf(QStringLiteral("--reasoning")) + 1),
             QStringLiteral("on"));
}

void SystemProfilesTests::manager_24gbPremiumPromotesThinkingCapAndKeepsMaxCtx()
{
    ProfileManager pm;
    const QVariantMap launch = pm.getLaunchProfile(QStringLiteral("sys-maxq"));
    QCOMPARE(launch.value(QStringLiteral("name")).toString(),
             QStringLiteral("[coding] MAX-Q · ThinkingCap Qwen3.6-27B 131k (visión)"));

    const QVariantMap model =
        pm.getModelProfile(launch.value(QStringLiteral("modelProfileId")).toString());
    const QString modelsDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/models";
    const QUuid ns(QStringLiteral("a1b2c3d4-e5f6-4a5b-8c7d-0e1f2a3b4c5d"));
    const QString modelBase = modelsDir + "/ThinkingCap-Qwen3.6-27B-GGUF/";
    QCOMPARE(model.value(QStringLiteral("modelId")).toString(),
             QUuid::createUuidV5(
                 ns, QString(modelBase + "ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf").toUtf8())
                 .toString(QUuid::WithoutBraces));
    QVERIFY(!model.value(QStringLiteral("mmprojId")).toString().isEmpty());

    const QVariantMap runtime =
        pm.getRuntimePreset(launch.value(QStringLiteral("runtimePresetId")).toString());
    QCOMPARE(runtime.value(QStringLiteral("ctx")).toInt(), 131000);
    QCOMPARE(runtime.value(QStringLiteral("gpuLayers")).toInt(), 999);
    QCOMPARE(runtime.value(QStringLiteral("batch")).toInt(), 512);
    QCOMPARE(runtime.value(QStringLiteral("ubatch")).toInt(), 64);
    QCOMPARE(runtime.value(QStringLiteral("cacheType")).toString(), QStringLiteral("q4_0"));

    const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
    const auto valueAfter = [&args](const QString &flag) {
        const int index = args.indexOf(flag);
        return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : QString();
    };
    QCOMPARE(valueAfter(QStringLiteral("--spec-type")), QStringLiteral("draft-mtp"));
    QCOMPARE(valueAfter(QStringLiteral("--spec-draft-n-max")), QStringLiteral("4"));
    QCOMPARE(valueAfter(QStringLiteral("--reasoning")), QStringLiteral("off"));

    const QVariantMap maxCtx = pm.getLaunchProfile(QStringLiteral("sys-maxctx"));
    QCOMPARE(maxCtx.value(QStringLiteral("name")).toString(),
             QStringLiteral("[coding] MAX-CTX · Qwen3.6-27B 262k"));
    const QVariantMap maxCtxRuntime =
        pm.getRuntimePreset(maxCtx.value(QStringLiteral("runtimePresetId")).toString());
    QCOMPARE(maxCtxRuntime.value(QStringLiteral("ctx")).toInt(), 262000);
    const QVariantMap maxCtxModel =
        pm.getModelProfile(maxCtx.value(QStringLiteral("modelProfileId")).toString());
    QVERIFY(maxCtxModel.value(QStringLiteral("mmprojId")).toString().isEmpty());
}

void SystemProfilesTests::controller_recommendsClosestTier()
{
    AppController app;
    // 24GB: maxq/fastgemma son extra (showcase), así que el mejor tier no-extra
    // ≤VRAM es el default coding KAT de 20GB.
    app.setHardwareSummaryForTest(24.0, 128.0, QStringLiteral("NVIDIA GeForce RTX 3090"));
    QCOMPARE(app.recommendedSystemProfile().value("launchId").toString(),
             QStringLiteral("sys-vram-20"));
    // RTX 3080 20GB: mismo tier KAT con offload parcial validado.
    app.setHardwareSummaryForTest(20.0, 64.0, QStringLiteral("NVIDIA GeForce RTX 3080"));
    QCOMPARE(app.recommendedSystemProfile().value("launchId").toString(),
             QStringLiteral("sys-vram-20"));
    app.setHardwareSummaryForTest(10.0, 32.0, QStringLiteral("NVIDIA"));
    QCOMPARE(app.recommendedSystemProfile().value("launchId").toString(),
             QStringLiteral("sys-vram-8-gemma"));
    app.setHardwareSummaryForTest(5.0, 16.0, QStringLiteral("NVIDIA"));
    QCOMPARE(app.recommendedSystemProfile().value("launchId").toString(),
             QStringLiteral("sys-vram-4"));
}

void SystemProfilesTests::controller_recommendedTierIncludesDisplayName()
{
    AppController app;
    app.setHardwareSummaryForTest(8.0, 32.0, QStringLiteral("NVIDIA GeForce RTX 3070"));
    const QVariantMap pick = app.recommendedSystemProfile();
    QCOMPARE(pick.value("launchId").toString(), QStringLiteral("sys-vram-8-gemma"));
    QCOMPARE(pick.value("displayName").toString(),
             QStringLiteral("[general] 8GB · Gemma 4 12B Q3 (visión, MTP)"));
}

void SystemProfilesTests::controller_recommendsCpuWhenNoGpu()
{
    AppController app;
    app.setHardwareSummaryForTest(0.0, 64.0, QStringLiteral("sin GPU"));
    QCOMPARE(app.recommendedSystemProfile().value("launchId").toString(),
             QStringLiteral("sys-vram-0"));
}

void SystemProfilesTests::controller_noneWhenBelowMinimum()
{
    AppController app;
    app.setHardwareSummaryForTest(0.0, 4.0, QStringLiteral("sin GPU"));   // 4GB RAM < 16 del CPU tier
    QVERIFY(app.recommendedSystemProfile().isEmpty());
}

// A 8GB el showcase ofrece elegir: Gemma 12B (visión) vs Qwen3.5 9B (agente),
// o ambos. recommendedSystemProfile sigue siendo el Gemma (default por orden).
void SystemProfilesTests::controller_showcase8gbOffersGemmaAndQwen()
{
    AppController app;
    app.setHardwareSummaryForTest(8.0, 32.0, QStringLiteral("NVIDIA GeForce RTX 3070"));

    // El recomendado único (tier ≤VRAM, no-extra, primero por orden) = Gemma.
    QCOMPARE(app.recommendedSystemProfile().value("launchId").toString(),
             QStringLiteral("sys-vram-8-gemma"));

    const QVariantList sc = app.recommendedShowcase();
    QCOMPARE(sc.size(), 2);
    QStringList ids, labels;
    for (const QVariant &v : sc) {
        ids   << v.toMap().value("launchId").toString();
        labels << v.toMap().value("label").toString();
    }
    QVERIFY(ids.contains("sys-vram-8-gemma"));
    QVERIFY(ids.contains("sys-vram-8-qwen-agent"));
    QVERIFY(labels.contains("Visión"));
    QVERIFY(labels.contains("Agente"));
}

// El showcase de 24GB (MAX-Q coding + FAST-GEMMA general) sigue intacto tras
// generalizar el mecanismo por showcaseGroup.
void SystemProfilesTests::controller_showcase24gbUnchanged()
{
    AppController app;
    app.setHardwareSummaryForTest(24.0, 64.0, QStringLiteral("NVIDIA GeForce RTX 4090"));
    const QVariantList sc = app.recommendedShowcase();
    QCOMPARE(sc.size(), 2);
    QStringList ids;
    for (const QVariant &v : sc) ids << v.toMap().value("launchId").toString();
    QVERIFY(ids.contains("sys-maxq"));
    QVERIFY(ids.contains("sys-fastgemma"));
}

void SystemProfilesTests::bundle_lagunaIsOptInAndHardwareGated()
{
    QFile f(bundlePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    QJsonObject laguna, dual, safe;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("id")).toString()
            == QStringLiteral("sys-laguna-s-2-1-q2")) {
            laguna = o;
        } else if (o.value(QStringLiteral("id")).toString()
                   == QStringLiteral("sys-laguna-s-2-1-q2-48gb")) {
            dual = o;
        } else if (o.value(QStringLiteral("id")).toString()
                   == QStringLiteral("sys-laguna-s-2-1-q2-48gb-safe")) {
            safe = o;
        }
    }
    QVERIFY2(!laguna.isEmpty(), "falta el perfil experimental Laguna");
    QVERIFY(laguna.value(QStringLiteral("extra")).toBool());
    QVERIFY(!laguna.value(QStringLiteral("autoCompanion")).toBool());
    QCOMPARE(laguna.value(QStringLiteral("minVramGb")).toInt(), 24);
    QCOMPARE(laguna.value(QStringLiteral("minRamGb")).toInt(), 120);
    QVERIFY(laguna.value(QStringLiteral("binaryPin")).toString().isEmpty());
    QCOMPARE(laguna.value(QStringLiteral("minimumBinaryBuild")).toInt(), 10087);

    const QJsonObject model = laguna.value(QStringLiteral("model")).toObject();
    QCOMPARE(model.value(QStringLiteral("file")).toString(),
             QStringLiteral("Laguna-S-2.1-UD-Q2_K_XL.gguf"));
    const QJsonObject runtime = laguna.value(QStringLiteral("runtime")).toObject();
    QCOMPARE(runtime.value(QStringLiteral("ctx")).toInt(), 100000);
    QCOMPARE(runtime.value(QStringLiteral("ubatch")).toInt(), 768);
    const QJsonArray args = laguna.value(QStringLiteral("extraArgs")).toArray();
    QStringList tokens;
    for (const QJsonValue &arg : args)
        tokens << arg.toString();
    const int cpuMoe = tokens.indexOf(QStringLiteral("--n-cpu-moe"));
    QVERIFY(cpuMoe >= 0 && cpuMoe + 1 < tokens.size());
    QCOMPARE(tokens.at(cpuMoe + 1), QStringLiteral("32"));
    QVERIFY(tokens.contains(QStringLiteral("--reasoning-preserve")));

    QVERIFY2(!dual.isEmpty(), "falta la variante Laguna medida para 2x3090");
    QVERIFY(dual.value(QStringLiteral("extra")).toBool());
    QVERIFY(!dual.value(QStringLiteral("autoCompanion")).toBool());
    QVERIFY(dual.value(QStringLiteral("favorite")).toBool());
    QVERIFY(!dual.value(QStringLiteral("benchmark")).toBool());
    QCOMPARE(dual.value(QStringLiteral("minVramGb")).toInt(), 48);
    QCOMPARE(dual.value(QStringLiteral("minRamGb")).toInt(), 64);
    QCOMPARE(dual.value(QStringLiteral("minimumBinaryBuild")).toInt(), 10087);
    QCOMPARE(dual.value(QStringLiteral("model")).toObject(), model);
    const QJsonObject dualRuntime = dual.value(QStringLiteral("runtime")).toObject();
    QCOMPARE(dualRuntime.value(QStringLiteral("ctx")).toInt(), 100000);
    QVERIFY(dualRuntime.value(QStringLiteral("mmap")).toBool());
    QStringList dualArgs;
    for (const QJsonValue &arg : dual.value(QStringLiteral("extraArgs")).toArray())
        dualArgs << arg.toString();
    QVERIFY(!dualArgs.contains(QStringLiteral("--n-cpu-moe")));
    QCOMPARE(dualArgs.value(dualArgs.indexOf(QStringLiteral("--fit")) + 1),
             QStringLiteral("on"));
    QCOMPARE(dualArgs.value(dualArgs.indexOf(QStringLiteral("--split-mode")) + 1),
             QStringLiteral("layer"));
    QCOMPARE(dualArgs.value(dualArgs.indexOf(QStringLiteral("--tensor-split")) + 1),
             QStringLiteral("1,1"));
    QVERIFY(dualArgs.contains(QStringLiteral("--reasoning-preserve")));

    QVERIFY2(!safe.isEmpty(), "falta la variante Laguna CUDA safe");
    QVERIFY(safe.value(QStringLiteral("extra")).toBool());
    QVERIFY(!safe.value(QStringLiteral("autoCompanion")).toBool());
    QVERIFY(safe.value(QStringLiteral("benchmark")).toBool());
    QCOMPARE(safe.value(QStringLiteral("minVramGb")).toInt(), 48);
    QCOMPARE(safe.value(QStringLiteral("minimumBinaryBuild")).toInt(), 10087);
    QCOMPARE(safe.value(QStringLiteral("model")).toObject(), model);
    const QJsonObject safeRuntime = safe.value(QStringLiteral("runtime")).toObject();
    QCOMPARE(safeRuntime.value(QStringLiteral("ctx")).toInt(), 65536);
    QCOMPARE(safeRuntime.value(QStringLiteral("batch")).toInt(), 256);
    QCOMPARE(safeRuntime.value(QStringLiteral("ubatch")).toInt(), 64);
    QVERIFY(safeRuntime.value(QStringLiteral("flashAttn")).toBool());
    QStringList safeArgs;
    for (const QJsonValue &arg : safe.value(QStringLiteral("extraArgs")).toArray())
        safeArgs << arg.toString();
    QCOMPARE(safeArgs.value(safeArgs.indexOf(QStringLiteral("--fit")) + 1),
             QStringLiteral("off"));
    QCOMPARE(safeArgs.value(safeArgs.indexOf(QStringLiteral("--flash-attn")) + 1),
             QStringLiteral("on"));
    QCOMPARE(safeArgs.value(safeArgs.indexOf(QStringLiteral("--n-cpu-moe")) + 1),
             QStringLiteral("32"));

    // Sigue fuera del recomendado y del showcase premium: es opt-in incluso en
    // la máquina objetivo de 24GB VRAM + 128GB RAM.
    AppController app;
    app.setHardwareSummaryForTest(24.0, 128.0,
                                  QStringLiteral("NVIDIA GeForce RTX 3090"));
    QVERIFY(app.recommendedSystemProfile().value(QStringLiteral("launchId")).toString()
            != QStringLiteral("sys-laguna-s-2-1-q2"));
    const QVariantList showcase = app.recommendedShowcase();
    for (const QVariant &item : showcase)
        QVERIFY(item.toMap().value(QStringLiteral("launchId")).toString()
                != QStringLiteral("sys-laguna-s-2-1-q2"));

}

void SystemProfilesTests::bundle_miniMaxIsOptInAndMemoryGated()
{
    QFile f(bundlePath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    QJsonObject profile;
    for (const QJsonValue &v : arr) {
        if (v.toObject().value(QStringLiteral("id")).toString()
            == QStringLiteral("sys-experimental-minimax-m27-q3ks")) {
            profile = v.toObject();
            break;
        }
    }
    QVERIFY2(!profile.isEmpty(), "falta el perfil experimental MiniMax M2.7");
    QVERIFY(profile.value(QStringLiteral("extra")).toBool());
    QVERIFY(!profile.value(QStringLiteral("autoCompanion")).toBool());
    QCOMPARE(profile.value(QStringLiteral("minRamGb")).toInt(), 112);
    QCOMPARE(profile.value(QStringLiteral("minVramGb")).toInt(), 48);
    QVERIFY(profile.value(QStringLiteral("favorite")).toBool());
    QVERIFY(!profile.value(QStringLiteral("benchmark")).toBool());
    QCOMPARE(profile.value(QStringLiteral("agentProfileId")).toString(),
             QStringLiteral("agent-maximo"));
    QCOMPARE(profile.value(QStringLiteral("minimumBinaryBuild")).toInt(), 10331);
    QCOMPARE(profile.value(QStringLiteral("model")).toObject().value(QStringLiteral("quant")).toString(),
             QStringLiteral("Q3_K_S"));
    QCOMPARE(profile.value(QStringLiteral("model")).toObject().value(QStringLiteral("file")).toString(),
             QStringLiteral("MiniMax-M2.7.Q3_K_S.gguf"));
    QCOMPARE(profile.value(QStringLiteral("runtime")).toObject().value(QStringLiteral("ctx")).toInt(), 32768);

    QStringList args;
    for (const QJsonValue &arg : profile.value(QStringLiteral("extraArgs")).toArray())
        args << arg.toString();
    QCOMPARE(args.value(args.indexOf(QStringLiteral("--n-cpu-moe")) + 1), QStringLiteral("45"));
    QCOMPARE(args.value(args.indexOf(QStringLiteral("--split-mode")) + 1), QStringLiteral("layer"));
    QCOMPARE(args.value(args.indexOf(QStringLiteral("--tensor-split")) + 1), QStringLiteral("3,1"));

    AppController app;
    app.setHardwareSummaryForTest(48.0, 128.0, QStringLiteral("2x NVIDIA GeForce RTX 3090"));
    QVERIFY(app.recommendedSystemProfile().value(QStringLiteral("launchId")).toString()
            != QStringLiteral("sys-experimental-minimax-m27-q3ks"));
    for (const QVariant &item : app.recommendedShowcase())
        QVERIFY(item.toMap().value(QStringLiteral("launchId")).toString()
                != QStringLiteral("sys-experimental-minimax-m27-q3ks"));
}

// Un tier sin grupo de showcase (ej. 4GB) no ofrece "uno/otro/ambos".
void SystemProfilesTests::controller_showcaseEmptyWhenNoSiblings()
{
    AppController app;
    app.setHardwareSummaryForTest(5.0, 16.0, QStringLiteral("NVIDIA"));
    QVERIFY(app.recommendedShowcase().isEmpty());
}

// El chat-template de Gemma4 no es un archivo cualquiera: llama.cpp lo clasifica
// leyendo su TEXTO. Busca "'<|tool_call>call:'" para tomar el path nativo
// (peg-gemma4) en vez del parseo genérico, y el comentario "OpenAI Chat
// Completions:" para decidir que NO es una versión vieja que necesite
// workarounds de compatibilidad. Un reemplazo desde upstream que pierda
// cualquiera de los dos degrada el tool-calling en silencio: el server arranca,
// responde 200, y sólo se nota porque el modelo llama peor a las tools.
// Además el archivo está duplicado (qrc bundle + copia versionada que usan los
// perfiles de usuario vía --chat-template-file): deben ser idénticos.
void SystemProfilesTests::bundle_gemma4TemplateKeepsLlamaCppMarkers()
{
    const QDir repo = QFileInfo(bundlePath()).dir();   // .../assets
    const QString bundled = repo.absoluteFilePath(
        QStringLiteral("chat-templates/gemma4-tools-fixed.jinja"));
    QFile f(bundled);
    QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("no se pudo abrir %1").arg(bundled)));
    const QString tpl = QString::fromUtf8(f.readAll());
    f.close();

    QVERIFY2(tpl.contains(QStringLiteral("'<|tool_call>call:'")),
             "sin este literal llama.cpp no toma el path nativo peg-gemma4");
    QVERIFY2(tpl.contains(QStringLiteral("OpenAI Chat Completions:")),
             "sin este comentario llama.cpp trata el template como outdated");

    // La copia del repo root (a la que apuntan los perfiles de usuario) no puede
    // divergir de la bundleada en el qrc.
    const QString rootCopy = QDir(repo.absoluteFilePath(QStringLiteral("..")))
                                 .absoluteFilePath(QStringLiteral("chat-templates/gemma4-tools-fixed.jinja"));
    QFile g(rootCopy);
    QVERIFY2(g.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("no se pudo abrir %1").arg(rootCopy)));
    const QString rootTpl = QString::fromUtf8(g.readAll());
    g.close();
    QCOMPARE(rootTpl, tpl);

    // Todos los perfiles Gemma 4 deben forzar la plantilla canónica. Dejar que
    // alguno use sólo el metadata embebido hace que un GGUF descargado antes de
    // la corrección de Google conserve silenciosamente el tool-calling viejo.
    QFile bundle(bundlePath());
    QVERIFY2(bundle.open(QIODevice::ReadOnly), "no se pudo abrir system_profiles.json");
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    int gemmaProfiles = 0;
    bool promotedHeretic = false;
    for (const QJsonValue &value : profiles) {
        const QJsonObject profile = value.toObject();
        const QJsonObject model = profile.value(QStringLiteral("model")).toObject();
        const QString identity = model.value(QStringLiteral("repo")).toString()
                                 + QLatin1Char('/')
                                 + model.value(QStringLiteral("file")).toString();
        if (!identity.contains(QStringLiteral("gemma-4"), Qt::CaseInsensitive))
            continue;
        ++gemmaProfiles;
        QCOMPARE(profile.value(QStringLiteral("chatTemplate")).toString(),
                 QStringLiteral("gemma4-tools-fixed.jinja"));
        if (profile.value(QStringLiteral("id")).toString() == QStringLiteral("sys-vram-4-gemma")) {
            QCOMPARE(model.value(QStringLiteral("repo")).toString(),
                     QStringLiteral("SC117/gemma-4-E4B-it-heretic-QAT-GGUF"));
            QCOMPARE(model.value(QStringLiteral("file")).toString(),
                     QStringLiteral("gemma-4-E4B-it-heretic-QAT-UD-Q4_K_XL.gguf"));
            promotedHeretic = true;
        }
    }
    QCOMPARE(gemmaProfiles, 4);
    QVERIFY(promotedHeretic);
}

void SystemProfilesTests::bundle_lagunaTemplateIsAppliedToBothProfiles()
{
    const QDir repo = QFileInfo(bundlePath()).dir();
    const QString path = repo.absoluteFilePath(
        QStringLiteral("chat-templates/laguna-tools-v24.jinja"));
    QFile templateFile(path);
    QVERIFY2(templateFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("no se pudo abrir %1").arg(path)));
    const QString tpl = QString::fromUtf8(templateFile.readAll());
    QVERIFY(tpl.contains(QStringLiteral("laguna-s21-froggeric-v24.0-loopguard")));
    QVERIFY(tpl.contains(QStringLiteral("<tool_call>")));
    QVERIFY(tpl.contains(QStringLiteral("<tool_response>")));

    const QString officialPath = repo.absoluteFilePath(
        QStringLiteral("chat-templates/poolside-Laguna-S-2.1.jinja"));
    QFile officialFile(officialPath);
    QVERIFY2(officialFile.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(QStringLiteral("no se pudo abrir %1").arg(officialPath)));
    const QString official = QString::fromUtf8(officialFile.readAll());
    QVERIFY(official.contains(QStringLiteral("No formatting instructions")));
    QVERIFY(official.contains(QStringLiteral("<available_tools>")));
    QVERIFY(official.contains(QStringLiteral("<tool_response>")));

    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    int lagunaProfiles = 0;
    QJsonObject single;
    QJsonObject dual;
    for (const QJsonValue &value : profiles) {
        const QJsonObject profile = value.toObject();
        const QString id = profile.value(QStringLiteral("id")).toString();
        if (!id.startsWith(QStringLiteral("sys-laguna")))
            continue;
        ++lagunaProfiles;
        QCOMPARE(profile.value(QStringLiteral("chatTemplate")).toString(),
                 QStringLiteral("laguna-tools-v24.jinja"));
        if (id == QStringLiteral("sys-laguna-s-2-1-q2")) single = profile;
        if (id == QStringLiteral("sys-laguna-s-2-1-q2-48gb")) dual = profile;
    }
    QCOMPARE(lagunaProfiles, 3);

    QVERIFY(!single.isEmpty());
    QVERIFY(!dual.isEmpty());
    QCOMPARE(single.value(QStringLiteral("benchmarkVariants")).toArray().size(), 2);
    QCOMPARE(dual.value(QStringLiteral("benchmarkVariants")).toArray().size(), 4);

    const QJsonArray expandedOfficial = expandSystemProfileVariants(QJsonArray{dual});
    QJsonObject officialVariant;
    for (const QJsonValue &value : expandedOfficial) {
        if (value.toObject().value(QStringLiteral("id")).toString()
            == QStringLiteral("sys-bench-laguna-s-2-1-q2-48gb-32k-official")) {
            officialVariant = value.toObject();
            break;
        }
    }
    QVERIFY(!officialVariant.isEmpty());
    QCOMPARE(officialVariant.value(QStringLiteral("chatTemplate")).toString(),
             QStringLiteral("poolside-Laguna-S-2.1.jinja"));

    ProfileManager pm;
    const QStringList variantIds = {
        QStringLiteral("sys-bench-laguna-s-2-1-q2-24gb-32k-official"),
        QStringLiteral("sys-bench-laguna-s-2-1-q2-24gb-32k-v24"),
        QStringLiteral("sys-bench-laguna-s-2-1-q2-48gb-32k-official"),
        QStringLiteral("sys-bench-laguna-s-2-1-q2-48gb-32k-v24")};
    for (const QString &id : variantIds) {
        const QVariantMap launch = pm.getLaunchProfile(id);
        QVERIFY2(!launch.isEmpty(), qPrintable(id));
        const bool retired = id.contains(QStringLiteral("24gb-32k"))
            || id.endsWith(QStringLiteral("48gb-32k-v24"));
        QCOMPARE(launch.value(QStringLiteral("benchmark")).toBool(), !retired);
        const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
        QVERIFY(!args.contains(QStringLiteral("--reasoning-format")));
        QVERIFY(!args.contains(QStringLiteral("--reasoning-preserve")));
    }
}

// El perfil de 48 GB (2x RTX 3090) reusa los mismos shards que ULTRA-Q y solo
// cambia lo que la VRAM extra habilita: KV q8_0, menos expertos en RAM, batch
// ganador del barrido y reparto explicito entre las dos placas.
void SystemProfilesTests::bundle_ultraQ48gbIsDualGpuVariantOfUltraQ()
{
    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    QJsonObject ultra, dual;
    for (const QJsonValue &value : profiles) {
        const QJsonObject profile = value.toObject();
        const QString id = profile.value("id").toString();
        if (id == QLatin1String("sys-ultraq-dsv4-0731-iq3s")) ultra = profile;
        if (id == QLatin1String("sys-ultraq-dsv4-0731-iq3s-48gb")) dual = profile;
    }
    QVERIFY(!dual.isEmpty());
    QVERIFY(dual.value("extra").toBool());
    QVERIFY(!dual.value("autoCompanion").toBool());
    QCOMPARE(dual.value("minVramGb").toInt(), 48);
    QCOMPARE(dual.value("minRamGb").toInt(), 120);
    QCOMPARE(dual.value("minimumBinaryBuild").toInt(), 10228);
    QCOMPARE(dual.value("model").toObject(), ultra.value("model").toObject());

    // Valores medidos contra el server real: ver el comment del perfil. Cambiarlos
    // sin volver a medir es lo que hacia crashear al server (OOM o illegal memory
    // access), asi que el test los clava.
    const QJsonObject rt = dual.value("runtime").toObject();
    QCOMPARE(rt.value("batch").toInt(), 4096);
    QCOMPARE(rt.value("ubatch").toInt(), 1024);
    QCOMPARE(rt.value("kv").toString(), QStringLiteral("q4_0")); // q8_0 deja CUDA0 al borde
    QVERIFY(rt.value("mmap").toBool());     // --no-mmap => OOM (116 GB en 128 de RAM)
    QVERIFY(!rt.value("mlock").toBool());   // mlock crashea

    QStringList args;
    for (const QJsonValue &v : dual.value("extraArgs").toArray()) args << v.toString();
    QCOMPARE(args.value(args.indexOf("--cache-type-k") + 1), QStringLiteral("q4_0"));
    QCOMPARE(args.value(args.indexOf("--cache-type-v") + 1), QStringLiteral("q4_0"));
    QCOMPARE(args.value(args.indexOf("--threads-batch") + 1), QStringLiteral("16"));
    // DSpark is retired for llama.cpp on Windows.  The dual profile is the
    // validated no-spec control; speculative decoding gets its own explicit
    // experiments when a backend supports it.
    QVERIFY(!args.contains(QStringLiteral("--spec-type")));
    QVERIFY(!args.contains(QStringLiteral("--spec-draft-n-max")));

    // El reparto va con -ot explicito, NO con --n-cpu-moe: en multi-GPU el
    // n-cpu-moe manda todas las capas pesadas a una placa (OOM) y, aun
    // balanceado, mata la inferencia con illegal memory access.
    QVERIFY(!args.contains(QStringLiteral("--n-cpu-moe")));
    QCOMPARE(args.value(args.indexOf("--split-mode") + 1), QStringLiteral("layer"));
    // --fit off, no --fit-target: con el target el server muere en el primer prefill.
    QCOMPARE(args.value(args.indexOf("--fit") + 1), QStringLiteral("off"));
    QVERIFY(!args.contains(QStringLiteral("--fit-target")));

    // CLAVADO A PROPOSITO: --tensor-split 1,0, o sea las 44 capas base enteras en
    // CUDA0 y sólo expertos en CUDA1. Con 1,1 (repartir también las capas base)
    // este modelo devuelve TEXTO CORRUPTO — a "cuál es la capital de Francia"
    // contesta ".#/!)". No crashea: responde basura con métricas perfectas, así que
    // ningún test de arranque lo agarra. Sólo pasa con DeepSeek (expertos en RAM +
    // capas base repartidas); ThinkingCap y KAT con 1,1 responden bien.
    QCOMPARE(args.value(args.indexOf("--tensor-split") + 1), QStringLiteral("1,0"));
    const QStringList ot = args.filter(QStringLiteral("_exps"));
    QCOMPARE(ot.size(), 1);
    const QStringList otRules = ot.at(0).split(',');
    QCOMPARE(otRules.size(), 2);
    QVERIFY(otRules.at(0).endsWith(QStringLiteral("=CUDA1"))); // sólo expertos cruzan
    QVERIFY(!otRules.at(0).contains(QStringLiteral("=CUDA0")));
    QVERIFY(otRules.at(1).endsWith(QStringLiteral("=CPU")));   // -ot es first-match-wins:
                                                               // el catch-all va ULTIMO
    // El regex tiene que nombrar el tensor exacto: con ffn_.*_exps arrastra tensores
    // que no deben moverse y el server muere con illegal memory access.
    for (const QString &rule : otRules)
        QVERIFY(rule.contains(QStringLiteral("ffn_(gate|up|down)_exps\\.weight")));

    // El launch derivado tiene que llegar con esos valores, no solo el bundle.
    ProfileManager pm;
    const QVariantMap launch =
        pm.getLaunchProfile(QStringLiteral("sys-ultraq-dsv4-0731-iq3s-48gb"));
    QVERIFY(launch.value("system").toBool());
    const QVariantMap preset = pm.getRuntimePreset(launch.value("runtimePresetId").toString());
    QCOMPARE(preset.value("batch").toInt(), 4096);
    QCOMPARE(preset.value("ubatch").toInt(), 1024);
    const QStringList launchArgs = launch.value("extraArgs").toStringList();
    QCOMPARE(launchArgs.filter(QStringLiteral("_exps")).size(), 1);
    QVERIFY(!launchArgs.contains(QStringLiteral("--n-cpu-moe")));
    QCOMPARE(launchArgs.value(launchArgs.indexOf("--tensor-split") + 1), QStringLiteral("1,0"));

    // Mismo modelo fisico que ULTRA-Q: no re-descarga los 116 GB de shards.
    const QVariantMap ultraLaunch =
        pm.getLaunchProfile(QStringLiteral("sys-ultraq-dsv4-0731-iq3s"));
    QCOMPARE(pm.getModelProfile(launch.value("modelProfileId").toString()).value("modelId"),
             pm.getModelProfile(ultraLaunch.value("modelProfileId").toString()).value("modelId"));
}

// La familia 48GB son pares comparables para benchmarkear (DeepSeek con y sin
// DSpark, ThinkingCap 131k/196k, KAT 131k/262k). Todos reparten entre las dos
// placas y todos declaran 48 GB, así que el menú los muestra o los esconde juntos.
void SystemProfilesTests::bundle_48gbFamilyIsBenchmarkableAndDualGpu()
{
    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();

    const QStringList expected{
        QStringLiteral("sys-ultraq-dsv4-0731-iq3s-48gb"),   // DeepSeek + DSpark
        QStringLiteral("sys-48-dsv4-nospec"),               // DeepSeek sin DSpark
        QStringLiteral("sys-48-dsv4-iq2m"),                 // quant chico: mas expertos en VRAM
        QStringLiteral("sys-48-thinkingcap-131k"),
        QStringLiteral("sys-48-thinkingcap-196k"),
        QStringLiteral("sys-48-katcoder-262k"),
        QStringLiteral("sys-48-katcoder-131k"),
        QStringLiteral("sys-48-katcoder-393k-nographs"),
        QStringLiteral("sys-48-thinkingcap-mtp"),      // el ganador del barrido
        QStringLiteral("sys-48-fablefusion-q6-mtp"),   // comparativa Q6 MTP + visión
        QStringLiteral("sys-48-hybrid-tc-kat"),        // planner TC+MTP, ejecutor KAT
    };
    QHash<QString, QJsonObject> found;
    for (const QJsonValue &v : profiles) {
        const QJsonObject o = v.toObject();
        if (expected.contains(o.value("id").toString()))
            found.insert(o.value("id").toString(), o);
    }
    QCOMPARE(found.size(), expected.size());

    for (const QString &id : expected) {
        const QJsonObject o = found.value(id);
        QCOMPARE(o.value("minVramGb").toInt(), 48);
        QVERIFY(o.value("extra").toBool());              // opt-in, no auto-recomendados
        QVERIFY(!o.value("autoCompanion").toBool());     // no arrastran descargas
        QVERIFY(!o.value("comment").toString().isEmpty());
        QStringList args;
        for (const QJsonValue &a : o.value("extraArgs").toArray()) args << a.toString();
        // Reparto entre las dos placas + el --fit off que evita el crash de prefill.
        QCOMPARE(args.value(args.indexOf("--split-mode") + 1), QStringLiteral("layer"));
        QCOMPARE(args.value(args.indexOf("--fit") + 1), QStringLiteral("off"));
        // DeepSeek va 1,0 (capas base enteras en CUDA0): con 1,1 devuelve texto
        // corrupto sin crashear. El resto entra entero en VRAM y reparte 1,1.
        const bool deepSeek = id.contains(QStringLiteral("dsv4"))
                           || id.contains(QStringLiteral("ultraq"));
        QCOMPARE(args.value(args.indexOf("--tensor-split") + 1),
                 deepSeek ? QStringLiteral("1,0") : QStringLiteral("1,1"));
        QVERIFY(o.value("runtime").toObject().value("flashAttn").toBool());
    }

    // Los que entran enteros en VRAM no llevan reparto de expertos (no hay offload
    // a RAM); los de DeepSeek sí, porque el modelo son ~116 GB.
    for (const QString &id : expected) {
        QStringList args;
        for (const QJsonValue &a : found.value(id).value("extraArgs").toArray())
            args << a.toString();
        const bool isDeepSeek = id.contains(QStringLiteral("dsv4"))
                             || id.contains(QStringLiteral("ultraq"));
        // Las dos reglas DeepSeek viajan comma-separated en una unica ocurrencia:
        // repetir -ot hace que llama.cpp conserve solo la ultima.
        QCOMPARE(args.filter(QStringLiteral("_exps")).size(), isDeepSeek ? 1 : 0);
    }

    // ThinkingCap conserva la visión (mmproj) y KAT usa el KV fino que habilitan
    // los 48 GB — es justamente lo que el tier de 24 GB no puede pagar.
    QVERIFY(!found.value(QStringLiteral("sys-48-thinkingcap-196k"))
                 .value("model").toObject().value("mmprojFile").toString().isEmpty());
    QCOMPARE(found.value(QStringLiteral("sys-48-katcoder-262k"))
                 .value("runtime").toObject().value("kv").toString(),
             QStringLiteral("q8_0"));
    // 196k es el techo medido de ThinkingCap: a 262144 el server crashea.
    QCOMPARE(found.value(QStringLiteral("sys-48-thinkingcap-196k"))
                 .value("runtime").toObject().value("ctx").toInt(), 196608);

    // Ningún perfil de DeepSeek puede mandar expertos a CUDA0: con --tensor-split
    // 1,0 esa placa tiene las 44 capas base y el KV, y agregarle expertos la
    // desborda. CUDA1 recibe sólo expertos, y por eso sus capas van >= 22 (las de
    // la mitad de arriba), que es donde el reparto quedó verificado.
    for (const QString &id : expected) {
        if (!id.contains(QStringLiteral("dsv4")) && !id.contains(QStringLiteral("ultraq")))
            continue;
        QStringList args;
        for (const QJsonValue &a : found.value(id).value("extraArgs").toArray())
            args << a.toString();
        QStringList rules;
        for (const QString &joined : args.filter(QStringLiteral("_exps")))
            rules.append(joined.split(','));
        for (const QString &rule : rules) {
            QVERIFY2(!rule.endsWith(QStringLiteral("=CUDA0")), qPrintable(id));
            if (!rule.endsWith(QStringLiteral("=CUDA1"))) continue;  // catch-all a CPU
            // Sólo los números del selector de capas: el "=CUDA0"/"=CUDA1" del final
            // también tiene dígitos y no es una capa.
            const QString layers = rule.left(rule.indexOf(QStringLiteral(".ffn_")));
            static const QRegularExpression num(QStringLiteral("\\d+"));
            auto it = num.globalMatch(layers);
            while (it.hasNext()) {
                const int layer = it.next().captured().toInt();
                QVERIFY2(layer >= 22, qPrintable(id + QStringLiteral(": ") + rule));
            }
        }
    }

    // El perfil IQ2_M apunta a OTRO quant (90,9 GB contra 116), que es lo único que
    // puede bajar el tráfico a RAM — el cuello del decode. Si alguien lo hace
    // apuntar a los shards del IQ3_S deja de tener sentido: sería el perfil base
    // con más residencia de la que entra.
    const QJsonObject iq2 = found.value(QStringLiteral("sys-48-dsv4-iq2m"));
    QCOMPARE(iq2.value("model").toObject().value("quant").toString(),
             QStringLiteral("UD-IQ2_M"));
    QCOMPARE(iq2.value("model").toObject().value("files").toArray().size(), 3);
    QVERIFY(iq2.value("folder").toString().contains(QStringLiteral("IQ2_M")));
    // Y aprovecha el quant chico para residir más expertos que el base (16 vs 12).
    QStringList iq2Args;
    for (const QJsonValue &a : iq2.value("extraArgs").toArray()) iq2Args << a.toString();
    const QStringList iq2Ot = iq2Args.filter(QStringLiteral("=CUDA1"));
    QCOMPARE(iq2Ot.size(), 1);
    QStringList baseArgs;
    for (const QJsonValue &a : found.value(QStringLiteral("sys-ultraq-dsv4-0731-iq3s-48gb"))
                                  .value("extraArgs").toArray())
        baseArgs << a.toString();
    const QStringList baseOt = baseArgs.filter(QStringLiteral("=CUDA1"));
    QVERIFY(iq2Ot.at(0).count(u'|') > baseOt.at(0).count(u'|'));

    // Cada modelo trae su barrido de variantes para benchmarkear (heredan del base
    // y sólo cambian una palanca). Sin ellas el usuario no puede comparar nada.
    QCOMPARE(found.value(QStringLiteral("sys-48-thinkingcap-196k"))
                 .value("benchmarkVariants").toArray().size(), 4);
    QCOMPARE(found.value(QStringLiteral("sys-48-katcoder-262k"))
                 .value("benchmarkVariants").toArray().size(), 5);
    QCOMPARE(found.value(QStringLiteral("sys-48-fablefusion-q6-mtp"))
                 .value("benchmarkVariants").toArray().size(), 4);

    // Todos tienen que llegar al menú como perfiles de sistema lanzables, incluidas
    // las variantes expandidas.
    ProfileManager pm;
    QStringList launchable = expected;
    launchable << QStringLiteral("sys-bench-48-tc-mtp")
               << QStringLiteral("sys-bench-48-kat-f16")
               << QStringLiteral("sys-bench-48-kat-noreason");
    for (const QString &id : launchable) {
        const QVariantMap launch = pm.getLaunchProfile(id);
        QVERIFY2(launch.value("system").toBool(), qPrintable(id));
        QVERIFY2(!launch.value("modelProfileId").toString().isEmpty(), qPrintable(id));
    }

    // Los controles que agotaron BCB, fallaron al cargar o quedaron duplicados
    // permanecen en la matriz histórica, pero no deben volver a entrar en la cola.
    const QStringList retiredBenchmarkVariants = {
        QStringLiteral("sys-bench-48-kat-f16"),
        QStringLiteral("sys-bench-laguna-s-2-1-q2-24gb-32k-official"),
        QStringLiteral("sys-bench-laguna-s-2-1-q2-24gb-32k-v24"),
        QStringLiteral("sys-bench-laguna-s-2-1-q2-48gb-100k-b1024"),
        QStringLiteral("sys-48-antirez-dsv4-q2q4-0731-32k-b2048"),
        QStringLiteral("sys-bench-48-bigbang-base"),
        QStringLiteral("sys-bench-48-bigbang-fast"),
        QStringLiteral("sys-bench-qwen38-udq4-post-262k-kv8"),
        QStringLiteral("sys-bench-qwen38-q5km-mtp3-64k-kv8"),
        QStringLiteral("sys-bench-qwen38-q5km-post-mirror-160k"),
        QStringLiteral("sys-bench-qwen38-q4km-24gb-tg128")};
    for (const QString &id : retiredBenchmarkVariants) {
        const QVariantMap launch = pm.getLaunchProfile(id);
        QVERIFY2(!launch.isEmpty(), qPrintable(id));
        QVERIFY2(!launch.value(QStringLiteral("benchmark")).toBool(), qPrintable(id));
    }

    // Los tres modelos medidos el 2026-08-07 quedan con nombre propio y fecha para
    // poder identificarlos después, y el híbrido junta a los dos ganadores:
    // ThinkingCap+MTP (10/10) planifica y KAT (9/10 pero a 110 t/s) ejecuta.
    QCOMPARE(found.value(QStringLiteral("sys-48-thinkingcap-mtp")).value("displayName").toString(),
             QStringLiteral("BALANCE - ThinkingCap+MTP-7-8-26"));
    QCOMPARE(found.value(QStringLiteral("sys-48-katcoder-262k")).value("displayName").toString(),
             QStringLiteral("FAST - KAT2-Coder-7-8-26"));
    QVERIFY(found.value(QStringLiteral("sys-48-katcoder-262k")).value("favorite").toBool());
    QVERIFY(!found.value(QStringLiteral("sys-48-katcoder-262k")).value("benchmark").toBool());
    QVERIFY(found.value(QStringLiteral("sys-48-thinkingcap-mtp")).value("favorite").toBool());
    QVERIFY(!found.value(QStringLiteral("sys-48-thinkingcap-mtp")).value("benchmark").toBool());
    QVERIFY(found.value(QStringLiteral("sys-48-dsv4-nospec")).value("favorite").toBool());
    QVERIFY(found.value(QStringLiteral("sys-48-dsv4-nospec")).value("benchmark").toBool());
    QVERIFY(found.value(QStringLiteral("sys-48-fablefusion-q6-mtp")).value("favorite").toBool());
    QVERIFY(!found.value(QStringLiteral("sys-48-fablefusion-q6-mtp")).value("benchmark").toBool());
    int marked = 0;
    for (auto it = found.cbegin(); it != found.cend(); ++it)
        marked += it.value().value("favorite").toBool() ? 1 : 0;
    QCOMPARE(marked, 4);
    QCOMPARE(found.value(QStringLiteral("sys-48-dsv4-nospec")).value("displayName").toString(),
             QStringLiteral("DeepSeek V4-7-8-26"));

    const QJsonObject hyb = found.value(QStringLiteral("sys-48-hybrid-tc-kat"));
    QCOMPARE(hyb.value("plannerProfileId").toString(), QStringLiteral("sys-48-thinkingcap-mtp"));
    QCOMPARE(hyb.value("hybridMode").toString(), QStringLiteral("sequential"));
    // El ejecutor es KAT: mismo modelo que el perfil de KAT, no el de ThinkingCap.
    QCOMPARE(hyb.value("model").toObject().value("file").toString(),
             found.value(QStringLiteral("sys-48-katcoder-262k"))
                 .value("model").toObject().value("file").toString());
    // El planner tiene que existir y traer MTP, que es lo que lo hace rendir.
    QStringList mtpArgs2;
    for (const QJsonValue &a : found.value(QStringLiteral("sys-48-thinkingcap-mtp"))
                                   .value("extraArgs").toArray())
        mtpArgs2 << a.toString();
    QCOMPARE(mtpArgs2.value(mtpArgs2.indexOf("--spec-type") + 1), QStringLiteral("draft-mtp"));
    QVERIFY(!found.value(QStringLiteral("sys-48-thinkingcap-mtp"))
                 .value("model").toObject().value("mmprojFile").toString().isEmpty());

    const QJsonObject fable = found.value(QStringLiteral("sys-48-fablefusion-q6-mtp"));
    QCOMPARE(fable.value("model").toObject().value("quant").toString(),
             QStringLiteral("Q6_K"));
    QVERIFY(!fable.value("model").toObject().value("mmprojFile").toString().isEmpty());
    QCOMPARE(fable.value("runtime").toObject().value("ctx").toInt(), 32768);
    QCOMPARE(fable.value("minimumBinaryBuild").toInt(), 10331);
    QStringList fableArgs;
    for (const QJsonValue &a : fable.value("extraArgs").toArray())
        fableArgs << a.toString();
    QCOMPARE(fableArgs.value(fableArgs.indexOf("--cache-type-k") + 1),
             QStringLiteral("q8_0"));
    QCOMPARE(fableArgs.value(fableArgs.indexOf("--cache-type-v") + 1),
             QStringLiteral("q8_0"));
    QCOMPARE(fableArgs.value(fableArgs.indexOf("--spec-type") + 1),
             QStringLiteral("draft-mtp"));
    QCOMPARE(fableArgs.value(fableArgs.indexOf("--spec-draft-n-max") + 1),
             QStringLiteral("3"));
    const QVariantMap fableLaunch = pm.getLaunchProfile(QStringLiteral("sys-48-fablefusion-q6-mtp"));
    QVERIFY(fableLaunch.value("favorite").toBool());
    QVERIFY(!fableLaunch.value("benchmark").toBool());
    const QVariantMap fableVariant = pm.getLaunchProfile(QStringLiteral("sys-bench-48-fable-mtp3"));
    QVERIFY(!fableVariant.value("favorite").toBool());
    QVERIFY(!fableVariant.value("benchmark").toBool());

    // 393k sólo arranca con los CUDA graphs apagados, y eso viaja por env: si el
    // env se pierde, el server muere con "invalid program counter" en el primer
    // prompt. Es la única palanca del tier que no es un flag de línea de comandos.
    const QJsonObject k393 = found.value(QStringLiteral("sys-48-katcoder-393k-nographs"));
    QCOMPARE(k393.value("runtime").toObject().value("ctx").toInt(), 393216);
    QCOMPARE(k393.value("env").toObject().value("GGML_CUDA_DISABLE_GRAPHS").toString(),
             QStringLiteral("1"));
    const QVariantMap k393Launch =
        pm.getLaunchProfile(QStringLiteral("sys-48-katcoder-393k-nographs"));
    QCOMPARE(k393Launch.value("envOverrides").toMap()
                 .value(QStringLiteral("GGML_CUDA_DISABLE_GRAPHS")).toString(),
             QStringLiteral("1"));
    // Y nadie más lo lleva: apagar los graphs cuesta ~21% de decode.
    for (const QString &id : expected) {
        if (id == QStringLiteral("sys-48-katcoder-393k-nographs")) continue;
        QVERIFY2(found.value(id).value("env").toObject().isEmpty(), qPrintable(id));
    }

    // Las variantes tienen que llegar con la palanca aplicada, no sólo declarada.
    const QStringList mtpArgs =
        pm.getLaunchProfile(QStringLiteral("sys-bench-48-tc-mtp")).value("extraArgs").toStringList();
    QCOMPARE(mtpArgs.value(mtpArgs.indexOf("--spec-type") + 1), QStringLiteral("draft-mtp"));
    const QVariantMap katKv8 = pm.getLaunchProfile(QStringLiteral("sys-bench-48-kat-f16"));
    QCOMPARE(pm.getRuntimePreset(katKv8.value("runtimePresetId").toString())
                 .value("cacheType").toString(),
             QStringLiteral("q8_0"));
    const QStringList katKv8Args = katKv8.value("extraArgs").toStringList();
    QCOMPARE(katKv8Args.value(katKv8Args.indexOf("--cache-type-k") + 1), QStringLiteral("q8_0"));
}

// El menú de Lanzar gatea por VRAM TOTAL, no por la placa más grande: llama.cpp
// reparte por capas, asi que 2x24 GB habilita un perfil que pide 48.
void SystemProfilesTests::controller_launchMenuGatesByTotalVramAcrossGpus()
{
    const QString id = QStringLiteral("sys-ultraq-dsv4-0731-iq3s-48gb");
    const QString lagunaId = QStringLiteral("sys-laguna-s-2-1-q2-48gb");
    auto idsOf = [](const QVariantList &menu) {
        QStringList out;
        for (const QVariant &v : menu) out << v.toMap().value("id").toString();
        return out;
    };

    AppController single;
    single.setHardwareSummaryForTest(24.0, 128.0, QStringLiteral("NVIDIA GeForce RTX 3090"));
    const QStringList singleIds = idsOf(single.launchMenu());
    QVERIFY(singleIds.contains(QStringLiteral("sys-ultraq-dsv4-0731-iq3s")));
    QVERIFY(!singleIds.contains(id));
    QVERIFY(!singleIds.contains(lagunaId));

    AppController dual;
    dual.setHardwareSummaryForTest(24.0, 128.0, QStringLiteral("NVIDIA GeForce RTX 3090"), 48.0, 2);
    const QVariantList dualMenu = dual.launchMenu();
    QVERIFY(idsOf(dualMenu).contains(id));
    QVERIFY(idsOf(dualMenu).contains(lagunaId));
    for (const QVariant &v : dualMenu) {
        const QVariantMap m = v.toMap();
        if (m.value("id").toString() != id) continue;
        QCOMPARE(m.value("minVram").toDouble(), 48.0);
    }
}

void SystemProfilesTests::controller_launchMenuAnnotatesGpuAffinity()
{
    AppController app;
    app.setHardwareSummaryForTest(24.0, 128.0,
                                  QStringLiteral("NVIDIA GeForce RTX 3090"), 48.0, 2);
    const QVariantList menu = app.launchMenu();
    bool dualMarked = false;
    bool ninferMarked = false;
    for (const QVariant &value : menu) {
        const QVariantMap item = value.toMap();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (id == QStringLiteral("sys-48-thinkingcap-131k")) {
            dualMarked = item.value(QStringLiteral("gpuAffinityMatched")).toBool();
            QVERIFY(item.value(QStringLiteral("displayName")).toString().startsWith(QStringLiteral("🎯 ")));
            QVERIFY(item.value(QStringLiteral("gpuAffinityLabel")).toString().contains(QStringLiteral("48")));
        }
        if (id == QStringLiteral("sys-ninfer3090-qwen38")) {
            ninferMarked = item.value(QStringLiteral("gpuAffinityMatched")).toBool();
            QVERIFY(item.value(QStringLiteral("gpuAffinityLabel")).toString()
                        .contains(QStringLiteral("RTX 3090")));
        }
    }
    QVERIFY(dualMarked);
    QVERIFY(ninferMarked);
}

void SystemProfilesTests::bundle_ultraQAndHybridAreWiredAndOptIn()
{
    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    ProfileManager pm;
    QJsonObject ultra, ultraExternal, hybrid;
    for (const QJsonValue &value : profiles) {
        const QJsonObject profile = value.toObject();
        if (profile.value("id").toString() == QLatin1String("sys-ultraq-dsv4-0731-iq3s")) ultra = profile;
        if (profile.value("id").toString()
            == QLatin1String("sys-ultraq-dsv4-0731-iq3s-dspark-external"))
            ultraExternal = profile;
        if (profile.value("id").toString() == QLatin1String("sys-hybrid-ultraq-maxq")) hybrid = profile;
    }
    QVERIFY(!ultra.isEmpty());
    QVERIFY(ultra.value("extra").toBool());
    QVERIFY(!ultra.value("autoCompanion").toBool());
    QCOMPARE(ultra.value("minimumBinaryBuild").toInt(), 10228);
    QCOMPARE(ultra.value("reasoningEffort").toString(), QStringLiteral("high"));
    QCOMPARE(ultra.value("reasoningBudget").toInt(), 8192);
    QCOMPARE(ultra.value("contextPresets").toArray().size(), 5);
    QCOMPARE(ultra.value("minRamGb").toInt(), 120);
    const QJsonObject model = ultra.value("model").toObject();
    QCOMPARE(model.value("repo").toString(), QStringLiteral("unsloth/DeepSeek-V4-Flash-0731-GGUF"));
    QCOMPARE(model.value("files").toArray().size(), 4);
    QCOMPARE(model.value("file").toString(),
             QStringLiteral("DeepSeek-V4-Flash-0731-UD-IQ3_S-00001-of-00004.gguf"));
    QVERIFY(ultra.value("runtime").toObject().value("mmap").toBool());
    QStringList args;
    for (const QJsonValue &v : ultra.value("extraArgs").toArray()) args << v.toString();
    QCOMPARE(args.value(args.indexOf("--n-cpu-moe") + 1), QStringLiteral("39"));
    QCOMPARE(args.value(args.indexOf("--fit-target") + 1), QStringLiteral("512"));
    QCOMPARE(args.value(args.indexOf("--temp") + 1), QStringLiteral("0.60"));
    QCOMPARE(args.value(args.indexOf("--top-p") + 1), QStringLiteral("0.95"));
    QCOMPARE(args.value(args.indexOf("--top-k") + 1), QStringLiteral("20"));
    QCOMPARE(args.value(args.indexOf("--min-p") + 1), QStringLiteral("0.0"));
    QCOMPARE(args.value(args.indexOf("--repeat-penalty") + 1), QStringLiteral("1.0"));
    QCOMPARE(args.value(args.indexOf("--presence-penalty") + 1), QStringLiteral("0.0"));
    QVERIFY(args.contains(QStringLiteral("--no-warmup")));
    QVERIFY(!args.contains(QStringLiteral("--spec-type")));
    QVERIFY(!args.contains(QStringLiteral("--spec-draft-n-max")));
    QCOMPARE(ultra.value("benchmarkVariants").toArray().size(), 31);

    QVERIFY(!ultraExternal.isEmpty());
    QVERIFY(ultraExternal.value("extra").toBool());
    QVERIFY(!ultraExternal.value("autoCompanion").toBool());
    QCOMPARE(ultraExternal.value("minimumBinaryBuild").toInt(), 10228);
    QCOMPARE(ultraExternal.value("model").toObject().value("file").toString(),
             model.value("file").toString());
    const QJsonObject externalDraft = ultraExternal.value("draftModel").toObject();
    QCOMPARE(externalDraft.value("repo").toString(),
             QStringLiteral("am17an/DeepseekV4-Flash-20260731-DSpark"));
    QCOMPARE(externalDraft.value("file").toString(),
             QStringLiteral("DeepseekV4-Flash-20260731-DSpark.gguf"));
    const QJsonObject externalSpec = ultraExternal.value("spec").toObject();
    QCOMPARE(externalSpec.value("type").toString(), QStringLiteral("draft-dspark"));
    QCOMPARE(externalSpec.value("draftNgl").toString(), QStringLiteral("auto"));
    QCOMPARE(externalSpec.value("draftNMax").toInt(), 5);
    QStringList externalArgs;
    for (const QJsonValue &v : ultraExternal.value("extraArgs").toArray())
        externalArgs << v.toString();
    QCOMPARE(externalArgs.value(externalArgs.indexOf("--spec-type") + 1),
             QStringLiteral("draft-dspark"));
    QVERIFY(!externalArgs.contains(QStringLiteral("--spec-draft-model")));

    const QVariantMap externalLaunch =
        pm.getLaunchProfile(QStringLiteral("sys-ultraq-dsv4-0731-iq3s-dspark-external"));
    const QVariantMap externalModel =
        pm.getModelProfile(externalLaunch.value("modelProfileId").toString());
    QVERIFY(!externalModel.value("draftModelId").toString().isEmpty());
    QCOMPARE(externalModel.value("specType").toString(), QStringLiteral("draft-dspark"));
    QCOMPARE(externalModel.value("specDraftNgl").toString(), QStringLiteral("auto"));
    QCOMPARE(externalModel.value("specDraftNMax").toInt(), 5);

    QVERIFY(!hybrid.isEmpty());
    QCOMPARE(hybrid.value("plannerProfileId").toString(),
             QStringLiteral("sys-ultraq-dsv4-0731-iq3s"));
    QCOMPARE(hybrid.value("hybridMode").toString(), QStringLiteral("sequential"));
    QCOMPARE(hybrid.value("binaryKind").toString(), QStringLiteral("official"));
    const QVariantMap launch = pm.getLaunchProfile(QStringLiteral("sys-hybrid-ultraq-maxq"));
    QCOMPARE(launch.value("plannerProfileId").toString(),
             QStringLiteral("sys-ultraq-dsv4-0731-iq3s"));
    QCOMPARE(launch.value("hybridMode").toString(), QStringLiteral("sequential"));

    const QVariantMap balanced =
        pm.getLaunchProfile(QStringLiteral("sys-bench-ultraq-b8192-u2048-ds5"));
    QVERIFY(balanced.value("system").toBool());
    const QVariantMap balancedRt =
        pm.getRuntimePreset(balanced.value("runtimePresetId").toString());
    QCOMPARE(balancedRt.value("batch").toInt(), 8192);
    QCOMPARE(balancedRt.value("ubatch").toInt(), 2048);
    QCOMPARE(balanced.value("modelProfileId").toString(),
             QStringLiteral("sysmodel-sys-bench-ultraq-b8192-u2048-ds5"));
    const QVariantMap balancedModel =
        pm.getModelProfile(balanced.value("modelProfileId").toString());
    const QVariantMap ultraLaunch =
        pm.getLaunchProfile(QStringLiteral("sys-ultraq-dsv4-0731-iq3s"));
    const QVariantMap ultraModel =
        pm.getModelProfile(ultraLaunch.value("modelProfileId").toString());
    QCOMPARE(balancedModel.value("modelId").toString(),
             ultraModel.value("modelId").toString());
    // DSpark queda fuera de la cola activa en llama.cpp/Windows. El perfil
    // histórico conserva su ID para no romper referencias, pero no debe
    // reintroducir flags speculative al materializarse.
    QVERIFY(!balanced.value("extraArgs").toStringList().contains(
        QStringLiteral("--spec-type")));
    QVERIFY(!balanced.value("extraArgs").toStringList().contains(
        QStringLiteral("--spec-draft-n-max")));

    const QVariantMap noSpec =
        pm.getLaunchProfile(QStringLiteral("sys-bench-ultraq-b4096-u1024-nospec"));
    QVERIFY(!noSpec.value("extraArgs").toStringList().contains(QStringLiteral("--spec-type")));
    QVERIFY(!noSpec.value("extraArgs").toStringList().contains(
        QStringLiteral("--spec-draft-n-max")));

    const QVariantMap moe43 =
        pm.getLaunchProfile(QStringLiteral("sys-bench-ultraq-b4096-u1024-moe43"));
    const QStringList moeArgs = moe43.value("extraArgs").toStringList();
    QCOMPARE(moeArgs.value(moeArgs.indexOf("--n-cpu-moe") + 1), QStringLiteral("43"));

    // Variantes históricas de DSpark: se conservan para reproducibilidad, pero
    // ninguna debe quedar disponible para benchmarking activo en Windows.
    auto argsOf = [&pm](const char *id) {
        return pm.getLaunchProfile(QString::fromLatin1(id)).value("extraArgs").toStringList();
    };
    // Control: el batch ganador debe quedar SIN speculative, o no mide nada.
    const QStringList wideNoSpec = argsOf("sys-bench-ultraq-b8192-u2048-nospec");
    QVERIFY(!wideNoSpec.isEmpty());
    QVERIFY(!wideNoSpec.contains(QStringLiteral("--spec-type")));
    QVERIFY(!wideNoSpec.contains(QStringLiteral("--spec-draft-n-max")));
    // La única variante speculative que queda en este grupo es ngram-mod,
    // porque no depende del drafter DSpark externo.
    const QStringList ngram = argsOf("sys-bench-ultraq-b8192-u2048-ngrammod");
    QCOMPARE(ngram.count(QStringLiteral("--spec-type")), 1);
    QCOMPARE(ngram.value(ngram.indexOf("--spec-type") + 1), QStringLiteral("ngram-mod"));

    // El control del hallazgo KV q8_0 debe quedar sin speculative, o no controla nada.
    const QStringList kv8NoSpec = argsOf("sys-bench-ultraq-b8192-u2048-kv8-nospec");
    QVERIFY(!kv8NoSpec.isEmpty());
    QVERIFY(!kv8NoSpec.contains(QStringLiteral("--spec-type")));
    QCOMPARE(kv8NoSpec.value(kv8NoSpec.indexOf("--cache-type-k") + 1), QStringLiteral("q8_0"));
    // Asimétrico: K en q8_0 y V en q4_0, no los dos iguales.
    const QStringList k8v4 = argsOf("sys-bench-ultraq-b8192-u2048-kv-k8v4");
    QCOMPARE(k8v4.value(k8v4.indexOf("--cache-type-k") + 1), QStringLiteral("q8_0"));
    QCOMPARE(k8v4.value(k8v4.indexOf("--cache-type-v") + 1), QStringLiteral("q4_0"));

    // Greedy: temp/top-k/top-p/min-p en 0. Con sampling estocástico el verificador
    // rechaza drafts que el greedy aceptaría, y el draft se paga igual.
    const QStringList greedy = argsOf("sys-bench-ultraq-b8192-u2048-ds5-temp0");
    QCOMPARE(greedy.count(QStringLiteral("--temp")), 1);
    QCOMPARE(greedy.value(greedy.indexOf("--temp") + 1), QStringLiteral("0"));
    QCOMPARE(greedy.value(greedy.indexOf("--top-k") + 1), QStringLiteral("0"));
    QCOMPARE(greedy.value(greedy.indexOf("--top-p") + 1), QStringLiteral("0"));
    // Pinear el modelo sólo funciona con archivo de paginación: sin él el commit
    // limit de Windows es la RAM física, VirtualLock falla a mitad de camino y el
    // server muere con GGML_ASSERT(ctx->mem_buffer != NULL). Los perfiles que lo
    // usan tienen que avisarlo en el NOMBRE, que es lo único que se ve al elegir
    // qué correr en el benchmark.
    int pinning = 0;
    auto *launches = pm.launchProfiles();
    for (int r = 0; r < launches->rowCount(); ++r) {
        const QModelIndex idx = launches->index(r);
        if (!launches->data(idx, ProfileListModel<LaunchProfile>::SystemRole).toBool()) continue;
        const QString id = launches->data(idx, ProfileListModel<LaunchProfile>::IdRole).toString();
        if (!id.startsWith(QStringLiteral("sys-bench-ultraq"))) continue;
        const QVariantMap lp = pm.getLaunchProfile(id);
        const QStringList a = lp.value("extraArgs").toStringList();
        const int lm = a.indexOf(QStringLiteral("--load-mode"));
        const bool pins = a.contains(QStringLiteral("--mlock"))
                          || (lm >= 0 && a.value(lm + 1).contains(QStringLiteral("mlock")));
        if (!pins) continue;
        ++pinning;
        QVERIFY2(lp.value("name").toString().contains(QStringLiteral("REQUIERE PAGEFILE")),
                 qPrintable(QStringLiteral("%1 pinea el modelo pero su nombre no avisa").arg(id)));
    }
    QCOMPARE(pinning, 2);
    // ngram-mod trae su propia ventana y no arrastra el n-max de DSpark.
    const QStringList ngramMod = argsOf("sys-bench-ultraq-b8192-u2048-ngrammod");
    QCOMPARE(ngramMod.value(ngramMod.indexOf("--spec-type") + 1), QStringLiteral("ngram-mod"));
    QVERIFY(!ngramMod.contains(QStringLiteral("--spec-draft-n-max")));
    QCOMPARE(ngramMod.value(ngramMod.indexOf("--spec-ngram-mod-n-match") + 1), QStringLiteral("32"));
    QCOMPARE(ngramMod.value(ngramMod.indexOf("--spec-ngram-mod-n-max") + 1), QStringLiteral("64"));
    // El KV del perfil se sube sin duplicar el flag.
    const QStringList kv8 = argsOf("sys-bench-ultraq-b8192-u2048-ds5-kv8");
    QCOMPARE(kv8.count(QStringLiteral("--cache-type-k")), 1);
    QCOMPARE(kv8.value(kv8.indexOf("--cache-type-k") + 1), QStringLiteral("q8_0"));
    QCOMPARE(kv8.value(kv8.indexOf("--cache-type-v") + 1), QStringLiteral("q8_0"));

    // Las variantes descartadas por resultados ya no se ofrecen.
    for (const char *gone : {"sys-bench-ultraq-b2048-u512-ds5", "sys-bench-ultraq-b8192-u512-ds5",
                             "sys-bench-ultraq-b4096-u1024-moe35", "sys-bench-ultraq-b4096-u1024-ds1",
                             "sys-bench-ultraq-b4096-u1024-ds3"})
        QVERIFY2(pm.getLaunchProfile(QString::fromLatin1(gone)).isEmpty(), gone);
}

void SystemProfilesTests::bundle_deepSeekLidUsesDedicatedExperimentalBinaryAndF16Kv()
{
    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    QJsonObject lid;
    for (const QJsonValue &value : profiles) {
        const QJsonObject profile = value.toObject();
        if (profile.value(QStringLiteral("id")).toString()
            == QLatin1String("sys-ultraq-dsv4-0731-lid-cuda")) {
            lid = profile;
            break;
        }
    }

    QVERIFY(!lid.isEmpty());
    QVERIFY(lid.value(QStringLiteral("extra")).toBool());
    QVERIFY(!lid.value(QStringLiteral("autoCompanion")).toBool());
    QCOMPARE(lid.value(QStringLiteral("binaryKind")).toString(),
             QStringLiteral("llama.cpp-deepseek-lid-cuda"));
    QCOMPARE(lid.value(QStringLiteral("runtime")).toObject().value(QStringLiteral("kv"))
                 .toString(), QStringLiteral("f16"));
    QCOMPARE(lid.value(QStringLiteral("contextPresets")).toArray().size(), 4);
    QCOMPARE(lid.value(QStringLiteral("env")).toObject()
                 .value(QStringLiteral("GGML_CUDA_NO_PINNED")).toString(), QStringLiteral("1"));

    QStringList args;
    for (const QJsonValue &value : lid.value(QStringLiteral("extraArgs")).toArray())
        args << value.toString();
    QVERIFY(!args.contains(QStringLiteral("--cache-type-k")));
    QVERIFY(!args.contains(QStringLiteral("--cache-type-v")));
    QCOMPARE(args.value(args.indexOf(QStringLiteral("--fit-ctx")) + 1),
             QStringLiteral("131072"));
    QCOMPARE(args.value(args.indexOf(QStringLiteral("--fit-target")) + 1),
             QStringLiteral("512"));

    ProfileManager pm;
    const QVariantMap launch =
        pm.getLaunchProfile(QStringLiteral("sys-ultraq-dsv4-0731-lid-cuda"));
    QVERIFY(launch.value(QStringLiteral("system")).toBool());
    const QVariantMap runtime = pm.getRuntimePreset(
        launch.value(QStringLiteral("runtimePresetId")).toString());
    QCOMPARE(runtime.value(QStringLiteral("cacheType")).toString(), QStringLiteral("f16"));
    const QVariantMap backend = pm.getBackend(
        launch.value(QStringLiteral("backendProfileId")).toString());
    QVERIFY(backend.value(QStringLiteral("binaryId")).toString().isEmpty());
}

void SystemProfilesTests::bundle_katApexMtpVisionIsOptInAndWired()
{
    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    QJsonObject kat;
    for (const QJsonValue &value : profiles) {
        const QJsonObject profile = value.toObject();
        if (profile.value(QStringLiteral("id")).toString()
            == QLatin1String("sys-48-katcoder-mtp-vision")) {
            kat = profile;
            break;
        }
    }

    QVERIFY(!kat.isEmpty());
    QVERIFY(kat.value(QStringLiteral("extra")).toBool());
    QVERIFY(!kat.value(QStringLiteral("benchmark")).toBool());
    QVERIFY(!kat.value(QStringLiteral("autoCompanion")).toBool());
    QVERIFY(kat.value(QStringLiteral("vision")).toBool());
    QCOMPARE(kat.value(QStringLiteral("minVramGb")).toInt(), 48);
    QCOMPARE(kat.value(QStringLiteral("minimumBinaryBuild")).toInt(), 10331);

    const QJsonObject model = kat.value(QStringLiteral("model")).toObject();
    QCOMPARE(model.value(QStringLiteral("repo")).toString(),
             QStringLiteral("ursb01/KAT-Coder-V2.5-Dev-MTP-APEX-GGUF"));
    QCOMPARE(model.value(QStringLiteral("file")).toString(),
             QStringLiteral("KAT-Coder-V2.5-Dev-MTP-APEX-i-quality-v2.gguf"));
    QCOMPARE(model.value(QStringLiteral("mmprojRepo")).toString(),
             QStringLiteral("unsloth/Qwen3.6-35B-A3B-MTP-GGUF"));
    QCOMPARE(model.value(QStringLiteral("mmprojFile")).toString(),
             QStringLiteral("mmproj-F16.gguf"));

    const QJsonObject runtime = kat.value(QStringLiteral("runtime")).toObject();
    QCOMPARE(runtime.value(QStringLiteral("ctx")).toInt(), 32768);
    QCOMPARE(runtime.value(QStringLiteral("batch")).toInt(), 512);
    QCOMPARE(runtime.value(QStringLiteral("ubatch")).toInt(), 64);
    QCOMPARE(runtime.value(QStringLiteral("kv")).toString(), QStringLiteral("q8_0"));

    const QJsonObject spec = kat.value(QStringLiteral("spec")).toObject();
    QCOMPARE(spec.value(QStringLiteral("type")).toString(), QStringLiteral("draft-mtp"));
    QCOMPARE(spec.value(QStringLiteral("draftNMax")).toInt(), 2);

    const QJsonArray variants = kat.value(QStringLiteral("benchmarkVariants")).toArray();
    QCOMPARE(variants.size(), 2);
    const QJsonArray expanded = expandSystemProfileVariants(QJsonArray{kat});
    QHash<QString, QJsonObject> expandedById;
    for (const QJsonValue &value : expanded)
        expandedById.insert(value.toObject().value(QStringLiteral("id")).toString(),
                            value.toObject());
    const QJsonObject mtp3 = expandedById.value(
        QStringLiteral("sys-bench-48-kat-mtp-vision-mtp3"));
    QVERIFY(!mtp3.isEmpty());
    QCOMPARE(mtp3.value(QStringLiteral("spec")).toObject()
                 .value(QStringLiteral("draftNMax")).toInt(), 3);
    const QJsonObject noSpec = expandedById.value(
        QStringLiteral("sys-bench-48-kat-mtp-vision-nospec"));
    QVERIFY(!noSpec.isEmpty());
    QVERIFY(noSpec.value(QStringLiteral("spec")).isNull());

    ProfileManager pm;
    const QVariantMap launch = pm.getLaunchProfile(
        QStringLiteral("sys-48-katcoder-mtp-vision"));
    QVERIFY(!launch.isEmpty());
    const QVariantMap modelProfile = pm.getModelProfile(
        launch.value(QStringLiteral("modelProfileId")).toString());
    const QString modelsDir =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/models/KAT-Coder-V2.5-Dev-MTP-APEX-GGUF/");
    const QUuid ns(QStringLiteral("a1b2c3d4-e5f6-4a5b-8c7d-0e1f2a3b4c5d"));
    QCOMPARE(modelProfile.value(QStringLiteral("modelId")).toString(),
             QUuid::createUuidV5(
                 ns, (modelsDir
                      + QStringLiteral("KAT-Coder-V2.5-Dev-MTP-APEX-i-quality-v2.gguf"))
                         .toUtf8())
                 .toString(QUuid::WithoutBraces));
    QCOMPARE(modelProfile.value(QStringLiteral("mmprojId")).toString(),
             QUuid::createUuidV5(ns, (modelsDir + QStringLiteral("mmproj-F16.gguf"))
                                     .toUtf8())
                 .toString(QUuid::WithoutBraces));
    QCOMPARE(modelProfile.value(QStringLiteral("specType")).toString(),
             QStringLiteral("draft-mtp"));
    QCOMPARE(modelProfile.value(QStringLiteral("specDraftNMax")).toInt(), 2);
    QVERIFY(!modelProfile.value(QStringLiteral("mmprojId")).toString().isEmpty());
    QVERIFY(modelProfile.value(QStringLiteral("draftModelId")).toString().isEmpty());

    const QVariantMap mtp3Launch = pm.getLaunchProfile(
        QStringLiteral("sys-bench-48-kat-mtp-vision-mtp3"));
    QVERIFY(!mtp3Launch.isEmpty());
    const QVariantMap mtp3Model = pm.getModelProfile(
        mtp3Launch.value(QStringLiteral("modelProfileId")).toString());
    QCOMPARE(mtp3Model.value(QStringLiteral("specType")).toString(),
             QStringLiteral("draft-mtp"));
    QCOMPARE(mtp3Model.value(QStringLiteral("specDraftNMax")).toInt(), 3);

    const QVariantMap noSpecLaunch = pm.getLaunchProfile(
        QStringLiteral("sys-bench-48-kat-mtp-vision-nospec"));
    QVERIFY(!noSpecLaunch.isEmpty());
    const QVariantMap noSpecModel = pm.getModelProfile(
        noSpecLaunch.value(QStringLiteral("modelProfileId")).toString());
    QVERIFY(noSpecModel.value(QStringLiteral("specType")).toString().isEmpty());
    QCOMPARE(noSpecModel.value(QStringLiteral("specDraftNMax")).toInt(), 0);
    QVERIFY(!noSpecModel.value(QStringLiteral("mmprojId")).toString().isEmpty());
}

// Regresión: duplicar un perfil de sistema debe FIJAR en la copia lo que el
// original resolvía dinámicamente sólo por ser system.
//   - binario por minimumBinaryBuild: antes la copia quedaba con backend.binaryId
//     vacío y la UI la ataba al primer binario de la lista — que puede ser un build
//     viejo sin los flags del perfil (p.ej. --spec-type draft-dspark) y el server
//     moría al arrancar.
//   - modelo: el ModelProfile de sistema lleva un id determinista por la ruta
//     administrada; el religado por nombre de archivo también es system-only.
void SystemProfilesTests::controller_duplicateBakesResolvedBinary()
{
    // Dos binarios "instalados": uno viejo (primero de la lista, el que se colaba)
    // y uno que cumple el mínimo del perfil ULTRA-Q (build 10228).
    const QString oldExe = m_dir.path() + QStringLiteral("/b9045/llama-server.exe");
    const QString newExe = m_dir.path() + QStringLiteral("/b10228-cuda12.4/llama-server.exe");
    for (const QString &p : {oldExe, newExe}) {
        QVERIFY(QDir().mkpath(QFileInfo(p).absolutePath()));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("stub");
    }

    AppController app;
    const QString oldId = app.binaryRegistry()->add(oldExe, QStringLiteral("b9045 (cuda)"),
                                                    QStringLiteral("official"),
                                                    QStringLiteral("cuda"), QStringLiteral("b9045"));
    const QString newId = app.binaryRegistry()->add(newExe, QStringLiteral("b10228 CUDA 12.4"),
                                                    QStringLiteral("official"),
                                                    QStringLiteral("cuda"), QStringLiteral("b10228"));
    QVERIFY(!oldId.isEmpty() && !newId.isEmpty());

    const QString sysId = QStringLiteral("sys-bench-ultraq-b8192-u2048-ds5");
    QCOMPARE(app.systemProfileMinimumBinaryBuild(sysId), 10228);

    const QString dup = app.duplicateLaunchProfile(sysId);
    QVERIFY(!dup.isEmpty());

    ProfileManager *pm = app.profileManager();
    QVERIFY(!pm->isSystemLaunch(dup));
    const QVariantMap backend =
        pm->getBackend(pm->getLaunchProfile(dup).value("backendProfileId").toString());
    // Fijado, y al binario correcto — no al primero de la lista.
    QCOMPARE(backend.value("binaryId").toString(), newId);
    QVERIFY(backend.value("binaryId").toString() != oldId);

    // El original sigue sin binario fijado: resuelve en cada arranque.
    const QVariantMap sysBackend =
        pm->getBackend(pm->getLaunchProfile(sysId).value("backendProfileId").toString());
    QVERIFY(sysBackend.value("binaryId").toString().isEmpty());

    // El modelo de la copia nunca queda vacío: o mantiene el id determinista del
    // original, o el que el religado del original resolvió contra el catálogo.
    const QVariantMap sysModel =
        pm->getModelProfile(pm->getLaunchProfile(sysId).value("modelProfileId").toString());
    const QVariantMap dupModel =
        pm->getModelProfile(pm->getLaunchProfile(dup).value("modelProfileId").toString());
    QVERIFY(!dupModel.value("modelId").toString().isEmpty());
    // Sin catálogo escaneado en el test no hay religado, así que debe coincidir.
    QCOMPARE(dupModel.value("modelId").toString(), sysModel.value("modelId").toString());
}

void SystemProfilesTests::bundle_qwen38VariantsAreMtpVisionAndTemplated()
{
    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    const QStringList ids = {QStringLiteral("sys-qwen38-27b-udq4-131k"),
                             QStringLiteral("sys-qwen38-27b-q4km-131k"),
                             QStringLiteral("sys-qwen38-27b-q5km-131k")};
    ProfileManager pm;
    for (const QString &id : ids) {
        QJsonObject found;
        for (const QJsonValue &value : profiles)
            if (value.toObject().value("id").toString() == id) found = value.toObject();
        QVERIFY2(!found.isEmpty(), qPrintable(id));
        QVERIFY(found.value("extra").toBool());
        QVERIFY(found.value("vision").toBool());
        if (id == QStringLiteral("sys-qwen38-27b-q4km-131k") ||
            id == QStringLiteral("sys-qwen38-27b-q5km-131k"))
            QVERIFY(found.value("benchmark").toBool());
        QCOMPARE(found.value("chatTemplate").toString(), QStringLiteral("qwen38-tools-fixed.jinja"));
        const QJsonObject model = found.value("model").toObject();
        QCOMPARE(model.value("repo").toString(), QStringLiteral("unsloth/Qwen3.8-27B-GGUF"));
        QCOMPARE(model.value("mmprojFile").toString(), QStringLiteral("mmproj-BF16.gguf"));
        const QJsonObject mtp = found.value("mtp").toObject();
        QVERIFY(mtp.value("enabled").toBool());
        QVERIFY(mtp.value("args").toArray().contains(QStringLiteral("draft-mtp")));
        const QJsonArray variants = found.value(QStringLiteral("benchmarkVariants")).toArray();
            const int expectedVariantCount = id == QStringLiteral("sys-qwen38-27b-udq4-131k")
                ? 32
                : id == QStringLiteral("sys-qwen38-27b-q5km-131k") ? 17 : 16;
        QCOMPARE(variants.size(), expectedVariantCount); // variantes base, controles del post, espejo y MTP+ngram
        if (id == QStringLiteral("sys-qwen38-27b-udq4-131k")) {
            bool ngramQueued = false;
            for (const QJsonValue &variant : variants) {
                const QJsonObject variantObject = variant.toObject();
                if (variantObject.value(QStringLiteral("id")).toString() ==
                    QStringLiteral("sys-bench-qwen38-udq4-mtp3-ngram")) {
                    ngramQueued = true;
                    QVERIFY(variantObject.value(QStringLiteral("benchmark")).toBool());
                    break;
                }
            }
            QVERIFY(ngramQueued);
            const QStringList reasoningIds = {
                QStringLiteral("sys-bench-qwen38-udq4-reasoning-low"),
                QStringLiteral("sys-bench-qwen38-udq4-reasoning-medium"),
                QStringLiteral("sys-bench-qwen38-udq4-reasoning-xhigh")};
            const QStringList reasoningLevels = {
                QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("xhigh")};
            for (int i = 0; i < reasoningIds.size(); ++i) {
                const QJsonObject variant = [&]() {
                    for (const QJsonValue &value : variants)
                        if (value.toObject().value(QStringLiteral("id")).toString()
                            == reasoningIds.at(i))
                            return value.toObject();
                    return QJsonObject{};
                }();
                QVERIFY2(!variant.isEmpty(), qPrintable(reasoningIds.at(i)));
                QVERIFY(variant.value(QStringLiteral("benchmark")).toBool());
                QCOMPARE(variant.value(QStringLiteral("extraArgOverrides")).toObject()
                             .value(QStringLiteral("--reasoning")).toString(),
                         reasoningLevels.at(i));
                const QVariantMap launch = pm.getLaunchProfile(reasoningIds.at(i));
                QVERIFY2(!launch.isEmpty(), qPrintable(reasoningIds.at(i)));
                const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
                QCOMPARE(args.value(args.indexOf(QStringLiteral("--reasoning")) + 1),
                         reasoningLevels.at(i));
            }
            const QStringList browserIds = {
                QStringLiteral("sys-bench-qwen38-udq4-browser-agent-off"),
                QStringLiteral("sys-bench-qwen38-udq4-browser-agent-low"),
                QStringLiteral("sys-bench-qwen38-udq4-browser-agent-medium"),
                QStringLiteral("sys-bench-qwen38-udq4-browser-agent-xhigh")};
            const QStringList browserLevels = {
                QStringLiteral("off"), QStringLiteral("low"),
                QStringLiteral("medium"), QStringLiteral("xhigh")};
            for (int i = 0; i < browserIds.size(); ++i) {
                const QJsonObject variant = [&]() {
                    for (const QJsonValue &value : variants)
                        if (value.toObject().value(QStringLiteral("id")).toString()
                            == browserIds.at(i))
                            return value.toObject();
                    return QJsonObject{};
                }();
                QVERIFY2(!variant.isEmpty(), qPrintable(browserIds.at(i)));
                QVERIFY(variant.value(QStringLiteral("benchmark")).toBool());
                QCOMPARE(variant.value(QStringLiteral("agentProfileId")).toString(),
                         QStringLiteral("agent-browser"));
                const QVariantMap launch = pm.getLaunchProfile(browserIds.at(i));
                QVERIFY2(!launch.isEmpty(), qPrintable(browserIds.at(i)));
                QCOMPARE(launch.value(QStringLiteral("agentProfileId")).toString(),
                         QStringLiteral("agent-browser"));
                const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
                const int reasoning = args.indexOf(QStringLiteral("--reasoning"));
                QVERIFY(reasoning >= 0 && reasoning + 1 < args.size());
                QCOMPARE(args.at(reasoning + 1), browserLevels.at(i));
            }
            const QStringList artifactIds = {
                QStringLiteral("sys-bench-qwen38-udq4-artifact-local"),
                QStringLiteral("sys-bench-qwen38-udq4-artifact-publisher")};
            const QStringList artifactProfiles = {
                QStringLiteral("agent-artifact-local"),
                QStringLiteral("agent-artifact-publisher")};
            for (int i = 0; i < artifactIds.size(); ++i) {
                const QJsonObject variant = [&]() {
                    for (const QJsonValue &value : variants)
                        if (value.toObject().value(QStringLiteral("id")).toString()
                            == artifactIds.at(i))
                            return value.toObject();
                    return QJsonObject{};
                }();
                QVERIFY2(!variant.isEmpty(), qPrintable(artifactIds.at(i)));
                QVERIFY(variant.value(QStringLiteral("benchmark")).toBool());
                QCOMPARE(variant.value(QStringLiteral("agentProfileId")).toString(),
                         artifactProfiles.at(i));
                const QVariantMap launch = pm.getLaunchProfile(artifactIds.at(i));
                QVERIFY2(!launch.isEmpty(), qPrintable(artifactIds.at(i)));
                QCOMPARE(launch.value(QStringLiteral("agentProfileId")).toString(),
                         artifactProfiles.at(i));
            }
            const QStringList memoryIds = {
                QStringLiteral("sys-bench-qwen38-udq4-24gb-fast-mtp4-64k"),
                QStringLiteral("sys-bench-qwen38-udq4-24gb-lookup-64k"),
                QStringLiteral("sys-bench-qwen38-udq4-24gb-prefix-cache-64k")};
            for (const QString &memoryId : memoryIds) {
                const QJsonObject variant = [&]() {
                    for (const QJsonValue &value : variants)
                        if (value.toObject().value(QStringLiteral("id")).toString() == memoryId)
                            return value.toObject();
                    return QJsonObject{};
                }();
                QVERIFY2(!variant.isEmpty(), qPrintable(memoryId));
                QCOMPARE(variant.value(QStringLiteral("benchmark")).toBool(),
                         !memoryId.endsWith(QStringLiteral("lookup-64k")));
                QCOMPARE(variant.value(QStringLiteral("runtime")).toObject()
                             .value(QStringLiteral("ctx")).toInt(), 65536);
                const QVariantMap launch = pm.getLaunchProfile(memoryId);
                QVERIFY2(!launch.isEmpty(), qPrintable(memoryId));
                const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
                QVERIFY(args.contains(QStringLiteral("--spec-draft-n-max"))
                        || args.contains(QStringLiteral("--spec-type"))
                        || args.contains(QStringLiteral("--cache-prompt")));
            }
        }
        QSet<QString> variantIds;
        const QVariantMap baseLaunch = pm.getLaunchProfile(id);
        QVERIFY2(!baseLaunch.isEmpty(), qPrintable(id));
        const QString baseModelId = pm.getModelProfile(
            baseLaunch.value(QStringLiteral("modelProfileId")).toString())
            .value(QStringLiteral("modelId")).toString();
        QVERIFY(!baseModelId.isEmpty());
        for (const QJsonValue &variantValue : variants) {
            const QString variantId = variantValue.toObject().value(QStringLiteral("id")).toString();
            QVERIFY(!variantId.isEmpty());
            QVERIFY(!variantIds.contains(variantId));
            variantIds.insert(variantId);
            const QVariantMap launch = pm.getLaunchProfile(variantId);
            QVERIFY2(!launch.isEmpty(), qPrintable(variantId));
            const QVariantMap variantModel = pm.getModelProfile(
                launch.value(QStringLiteral("modelProfileId")).toString());
            QCOMPARE(variantModel.value(QStringLiteral("modelId")).toString(), baseModelId);
            const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
            const int parallel = args.indexOf(QStringLiteral("--parallel"));
            QVERIFY(parallel >= 0 && parallel + 1 < args.size());
            QCOMPARE(args.at(parallel + 1), QStringLiteral("1"));
            if (variantId.endsWith(QStringLiteral("-ngram"))) {
                QVERIFY(args.contains(QStringLiteral("--spec-type")));
                QVERIFY(args.contains(QStringLiteral("draft-mtp,ngram-mod")));
                QVERIFY(args.contains(QStringLiteral("--spec-ngram-mod-n-match")));
                QVERIFY(args.contains(QStringLiteral("--spec-ngram-mod-n-min")));
                QVERIFY(args.contains(QStringLiteral("--spec-ngram-mod-n-max")));
            }
        }
        const auto findVariant = [&](const QString &variantId) {
            for (const QJsonValue &value : variants)
                if (value.toObject().value(QStringLiteral("id")).toString() == variantId)
                    return value.toObject();
            return QJsonObject{};
        };
        // Las variantes llevan el quant del PADRE en el id (udq4 / q4km / q5km);
        // con el prefijo fijo "udq4" este bloque pasaba para el primer perfil y
        // fallaba en el segundo, aunque el bundle estuviera bien.
        const QString quant = id.section(QLatin1Char('-'), 3, 3);   // sys-qwen38-27b-<quant>-131k
        QVERIFY2(!quant.isEmpty(), qPrintable(id));
        const QString variantPrefix = QStringLiteral("sys-bench-qwen38-%1-").arg(quant);
        for (const QString &suffix : {QStringLiteral("post-parallel2"), QStringLiteral("post-parallel4"), QStringLiteral("post-parallel6")}) {
            const QJsonObject variant = findVariant(variantPrefix + suffix);
            QVERIFY2(!variant.isEmpty(), qPrintable(variantPrefix + suffix));
            const int expectedSlots = suffix.endsWith(QStringLiteral("2")) ? 2 : suffix.endsWith(QStringLiteral("4")) ? 4 : 6;
            QCOMPARE(variant.value(QStringLiteral("runtime")).toObject().value(QStringLiteral("parallelSlots")).toInt(), expectedSlots);
        }
        const QJsonObject longVariant = findVariant(variantPrefix + QStringLiteral("post-262k-kv8"));
        QVERIFY2(!longVariant.isEmpty(), qPrintable(variantPrefix + QStringLiteral("post-262k-kv8")));
        QCOMPARE(longVariant.value(QStringLiteral("runtime")).toObject().value(QStringLiteral("ctx")).toInt(), 262144);
        QCOMPARE(longVariant.value(QStringLiteral("runtime")).toObject().value(QStringLiteral("kv")).toString(), QStringLiteral("q8_0"));
        const QJsonObject mirrorVariant = findVariant(variantPrefix + QStringLiteral("post-mirror-160k"));
        QVERIFY2(!mirrorVariant.isEmpty(), qPrintable(variantPrefix + QStringLiteral("post-mirror-160k")));
        const QJsonObject mirrorRuntime = mirrorVariant.value(QStringLiteral("runtime")).toObject();
        QCOMPARE(mirrorRuntime.value(QStringLiteral("ctx")).toInt(), 160927);
        QCOMPARE(mirrorRuntime.value(QStringLiteral("batch")).toInt(), 2048);
        QCOMPARE(mirrorRuntime.value(QStringLiteral("ubatch")).toInt(), 512);
        QCOMPARE(mirrorRuntime.value(QStringLiteral("kv")).toString(), QStringLiteral("q4_0"));
            const QJsonObject mirrorOverrides =
                mirrorVariant.value(QStringLiteral("extraArgOverrides")).toObject();
            QCOMPARE(mirrorOverrides.value(QStringLiteral("--spec-draft-n-max")).toString(),
                     QStringLiteral("2"));
        const QString improvedId = id == QStringLiteral("sys-qwen38-27b-udq4-131k")
            ? QStringLiteral("sys-bench-qwen38-udq4-48gb-196k-mtp2-kv8-mmproj-ram")
            : id == QStringLiteral("sys-qwen38-27b-q5km-131k")
                ? QStringLiteral("sys-bench-qwen38-q5km-48gb-196k-mtp2-kv8-mmproj-ram")
                : QString();
        if (!improvedId.isEmpty()) {
            const QJsonObject improved = findVariant(improvedId);
            QVERIFY2(!improved.isEmpty(), qPrintable(improvedId));
            QCOMPARE(improved.value(QStringLiteral("runtime")).toObject()
                         .value(QStringLiteral("ctx")).toInt(), 196608);
            QCOMPARE(improved.value(QStringLiteral("runtime")).toObject()
                         .value(QStringLiteral("batch")).toInt(), 512);
            QCOMPARE(improved.value(QStringLiteral("runtime")).toObject()
                         .value(QStringLiteral("ubatch")).toInt(), 64);
            const QJsonObject mtp = improved.value(QStringLiteral("mtp")).toObject();
            QCOMPARE(mtp.value(QStringLiteral("args")).toArray().at(3).toString(),
                     QStringLiteral("2"));
            const QVariantMap launch = pm.getLaunchProfile(improvedId);
            QVERIFY2(!launch.isEmpty(), qPrintable(improvedId));
            const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
            QCOMPARE(args.count(QStringLiteral("--spec-draft-n-max")), 1);
            QCOMPARE(args.value(args.indexOf(QStringLiteral("--spec-draft-n-max")) + 1),
                     QStringLiteral("2"));
        }
        if (id == QStringLiteral("sys-qwen38-27b-udq4-131k")) {
            const QStringList postVariantIds = {
                QStringLiteral("sys-bench-qwen38-udq4-post-tensor"),
                QStringLiteral("sys-bench-qwen38-udq4-post-mmproj-cpu"),
                QStringLiteral("sys-bench-qwen38-udq4-post-cache-warm")};
            for (const QString &variantId : postVariantIds) {
                const QVariantMap launch = pm.getLaunchProfile(variantId);
                QVERIFY2(!launch.isEmpty(), qPrintable(variantId));
                QVERIFY(launch.value(QStringLiteral("benchmark")).toBool());
                const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
                if (variantId.endsWith(QStringLiteral("-tensor"))) {
                    QCOMPARE(args.value(args.indexOf(QStringLiteral("--split-mode")) + 1),
                             QStringLiteral("tensor"));
                    QCOMPARE(args.value(args.indexOf(QStringLiteral("--tensor-split")) + 1),
                             QStringLiteral("1,1"));
                } else if (variantId.endsWith(QStringLiteral("-mmproj-cpu"))) {
                    QVERIFY(args.contains(QStringLiteral("--no-mmproj-offload")));
                } else {
                    QVERIFY(args.contains(QStringLiteral("--cache-prompt")));
                    QCOMPARE(args.value(args.indexOf(QStringLiteral("--cache-reuse")) + 1),
                             QStringLiteral("512"));
                }
            }
        }
        // El mmproj se resuelve desde model.mmprojFile al escanear el catálogo;
        // no debe exigirse como ruta absoluta en el bundle declarativo.
    }
}

void SystemProfilesTests::bundle_qwen38Turing24gbControlsAreColdAndSeparated()
{
    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    const QStringList ids = {
        QStringLiteral("sys-qwen38-27b-q4km-24gb-32k"),
        QStringLiteral("sys-qwen38-27b-q6k-24gb-32k")};

    ProfileManager pm;
    for (const QString &id : ids) {
        QJsonObject base;
        for (const QJsonValue &value : profiles) {
            if (value.toObject().value(QStringLiteral("id")).toString() == id) {
                base = value.toObject();
                break;
            }
        }
        QVERIFY2(!base.isEmpty(), qPrintable(id));
        QVERIFY(!base.value(QStringLiteral("benchmark")).toBool());
        QCOMPARE(base.value(QStringLiteral("minVramGb")).toInt(), 24);
        QCOMPARE(base.value(QStringLiteral("minRamGb")).toInt(), 32);
        QVERIFY(!base.value(QStringLiteral("vision")).toBool());
        QVERIFY(!base.contains(QStringLiteral("mtp")));
        QCOMPARE(base.value(QStringLiteral("chatTemplate")).toString(),
                 QStringLiteral("qwen38-tools-fixed.jinja"));

        const QJsonObject model = base.value(QStringLiteral("model")).toObject();
        QCOMPARE(model.value(QStringLiteral("repo")).toString(),
                 QStringLiteral("unsloth/Qwen3.8-27B-GGUF"));
        QVERIFY(model.value(QStringLiteral("file")).toString().contains(
            id.contains(QStringLiteral("q4km")) ? QStringLiteral("Q4_K_M")
                                                   : QStringLiteral("Q6_K")));

        const QJsonObject runtime = base.value(QStringLiteral("runtime")).toObject();
        QCOMPARE(runtime.value(QStringLiteral("ctx")).toInt(), 32768);
        QCOMPARE(runtime.value(QStringLiteral("batch")).toInt(), 512);
        QCOMPARE(runtime.value(QStringLiteral("ubatch")).toInt(), 512);
        QCOMPARE(runtime.value(QStringLiteral("gpuLayers")).toInt(), 999);
        QCOMPARE(runtime.value(QStringLiteral("parallelSlots")).toInt(), 1);
        QCOMPARE(runtime.value(QStringLiteral("kv")).toString(), QStringLiteral("q4_0"));
        QVERIFY(runtime.value(QStringLiteral("flashAttn")).toBool());
        QVERIFY(runtime.value(QStringLiteral("mmap")).toBool());
        QVERIFY(!runtime.value(QStringLiteral("mlock")).toBool());

        const QVariantMap baseLaunch = pm.getLaunchProfile(id);
        QVERIFY2(!baseLaunch.isEmpty(), qPrintable(id));
        const QStringList baseArgs = baseLaunch.value(QStringLiteral("extraArgs")).toStringList();
        QCOMPARE(baseArgs.value(baseArgs.indexOf(QStringLiteral("--reasoning")) + 1),
                 QStringLiteral("off"));
        QVERIFY(!baseArgs.contains(QStringLiteral("--spec-type")));
        QVERIFY(!baseArgs.contains(QStringLiteral("--cache-prompt")));
        QVERIFY(!baseArgs.contains(QStringLiteral("--cache-reuse")));

        const QJsonArray variants = base.value(QStringLiteral("benchmarkVariants")).toArray();
        QCOMPARE(variants.size(), 3);
        const QString prefix = id.contains(QStringLiteral("q4km"))
            ? QStringLiteral("sys-bench-qwen38-q4km-24gb-")
            : QStringLiteral("sys-bench-qwen38-q6k-24gb-");
        const QStringList variantIds = {
            prefix + QStringLiteral("tg128"),
            prefix + QStringLiteral("ngram-diagnostic"),
            prefix + QStringLiteral("prefix-warm")};
        for (const QString &variantId : variantIds) {
            QJsonObject variant;
            for (const QJsonValue &value : variants) {
                if (value.toObject().value(QStringLiteral("id")).toString() == variantId) {
                    variant = value.toObject();
                    break;
                }
            }
            QVERIFY2(!variant.isEmpty(), qPrintable(variantId));
            const bool expectedBenchmark = id.contains(QStringLiteral("q4km"))
                && !variantId.endsWith(QStringLiteral("tg128"));
            QCOMPARE(variant.value(QStringLiteral("benchmark")).toBool(), expectedBenchmark);
            const QVariantMap launch = pm.getLaunchProfile(variantId);
            QVERIFY2(!launch.isEmpty(), qPrintable(variantId));
            const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
            QCOMPARE(args.value(args.indexOf(QStringLiteral("--parallel")) + 1),
                     QStringLiteral("1"));
            if (variantId.endsWith(QStringLiteral("tg128"))) {
                QVERIFY(!args.contains(QStringLiteral("--spec-type")));
                QVERIFY(!args.contains(QStringLiteral("--cache-prompt")));
            } else if (variantId.endsWith(QStringLiteral("ngram-diagnostic"))) {
                QCOMPARE(args.value(args.indexOf(QStringLiteral("--spec-type")) + 1),
                         QStringLiteral("ngram-mod"));
                QVERIFY(!args.contains(QStringLiteral("--cache-prompt")));
            } else {
                QVERIFY(args.contains(QStringLiteral("--cache-prompt")));
                QCOMPARE(args.value(args.indexOf(QStringLiteral("--cache-reuse")) + 1),
                         QStringLiteral("512"));
            }
        }
    }
}

void SystemProfilesTests::bundle_qwen38Q6BenchmarkFamilyIsGatedAndExpanded()
{
    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    const QString baseId = QStringLiteral("sys-48-qwen38-27b-q6-96k");
    QJsonObject base;
    for (const QJsonValue &value : profiles) {
        if (value.toObject().value(QStringLiteral("id")).toString() == baseId) {
            base = value.toObject();
            break;
        }
    }
    QVERIFY2(!base.isEmpty(), qPrintable(baseId));
    QVERIFY(base.value(QStringLiteral("extra")).toBool());
    QVERIFY(!base.value(QStringLiteral("benchmark")).toBool());
    QCOMPARE(base.value(QStringLiteral("minVramGb")).toInt(), 48);
    QCOMPARE(base.value(QStringLiteral("minRamGb")).toInt(), 64);
    QVERIFY(base.value(QStringLiteral("vision")).toBool());
    QCOMPARE(base.value(QStringLiteral("chatTemplate")).toString(),
             QStringLiteral("qwen38-tools-fixed.jinja"));

    const QJsonObject model = base.value(QStringLiteral("model")).toObject();
    QCOMPARE(model.value(QStringLiteral("repo")).toString(),
             QStringLiteral("unsloth/Qwen3.8-27B-GGUF"));
    QCOMPARE(model.value(QStringLiteral("file")).toString(),
             QStringLiteral("Qwen3.8-27B-UD-Q6_K_XL.gguf"));
    QCOMPARE(model.value(QStringLiteral("mmprojFile")).toString(),
             QStringLiteral("mmproj-BF16.gguf"));
    QCOMPARE(model.value(QStringLiteral("quant")).toString(),
             QStringLiteral("UD-Q6_K_XL"));

    const QJsonObject runtime = base.value(QStringLiteral("runtime")).toObject();
    QCOMPARE(runtime.value(QStringLiteral("ctx")).toInt(), 98304);
    QCOMPARE(runtime.value(QStringLiteral("batch")).toInt(), 512);
    QCOMPARE(runtime.value(QStringLiteral("ubatch")).toInt(), 64);
    QCOMPARE(runtime.value(QStringLiteral("kv")).toString(), QStringLiteral("q4_0"));
    QCOMPARE(base.value(QStringLiteral("minimumBinaryBuild")).toInt(), 10331);
    const QJsonArray presets = base.value(QStringLiteral("contextPresets")).toArray();
    QVERIFY(presets.contains(65536));
    QVERIFY(presets.contains(98304));
    QVERIFY(presets.contains(131072));

    ProfileManager pm;
    const QVariantMap baseLaunch = pm.getLaunchProfile(baseId);
    QVERIFY2(!baseLaunch.isEmpty(), qPrintable(baseId));
    const QStringList baseArgs = baseLaunch.value(QStringLiteral("extraArgs")).toStringList();
    QVERIFY(baseArgs.contains(QStringLiteral("--fit")));
    QCOMPARE(baseArgs.value(baseArgs.indexOf(QStringLiteral("--fit")) + 1), QStringLiteral("off"));
    QCOMPARE(baseArgs.value(baseArgs.indexOf(QStringLiteral("--spec-draft-n-max")) + 1),
             QStringLiteral("2"));
    QCOMPARE(baseArgs.value(baseArgs.indexOf(QStringLiteral("--reasoning")) + 1),
             QStringLiteral("off"));
    QCOMPARE(baseArgs.value(baseArgs.indexOf(QStringLiteral("--split-mode")) + 1),
             QStringLiteral("layer"));
    QCOMPARE(baseArgs.value(baseArgs.indexOf(QStringLiteral("--tensor-split")) + 1),
             QStringLiteral("1,1"));

    const QJsonArray variants = base.value(QStringLiteral("benchmarkVariants")).toArray();
    QCOMPARE(variants.size(), 11);
    QSet<QString> ids;
    for (const QJsonValue &value : variants) {
        const QString id = value.toObject().value(QStringLiteral("id")).toString();
        QVERIFY(!id.isEmpty());
        QVERIFY(!ids.contains(id));
        ids.insert(id);
        const QVariantMap launch = pm.getLaunchProfile(id);
        QVERIFY2(!launch.isEmpty(), qPrintable(id));
        QVERIFY(!launch.value(QStringLiteral("benchmark")).toBool());
        const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
        QCOMPARE(args.value(args.indexOf(QStringLiteral("--parallel")) + 1), QStringLiteral("1"));
    }

    const auto findLaunch = [&pm](const QString &id) {
        return pm.getLaunchProfile(id);
    };
    const auto argValue = [](const QStringList &args, const QString &flag) {
        const int index = args.indexOf(flag);
        return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : QString();
    };

    const QVariantMap mirror = findLaunch(QStringLiteral("sys-bench-48-qwen38-q6-post-mirror-96k"));
    QVERIFY(!mirror.isEmpty());
    const QVariantMap mirrorRuntime = pm.getRuntimePreset(
        mirror.value(QStringLiteral("runtimePresetId")).toString());
    QCOMPARE(mirrorRuntime.value(QStringLiteral("ctx")).toInt(), 98304);
    QCOMPARE(mirrorRuntime.value(QStringLiteral("batch")).toInt(), 2048);
    QCOMPARE(mirrorRuntime.value(QStringLiteral("ubatch")).toInt(), 512);
    const QStringList mirrorArgs = mirror.value(QStringLiteral("extraArgs")).toStringList();
    QVERIFY(mirrorArgs.contains(QStringLiteral("--no-mmproj-offload")));
    QCOMPARE(argValue(mirrorArgs, QStringLiteral("--spec-draft-n-max")), QStringLiteral("2"));

    const QVariantMap kv8 = findLaunch(QStringLiteral("sys-bench-48-qwen38-q6-mtp2-kv8"));
    QVERIFY(!kv8.isEmpty());
    const QVariantMap kv8Runtime = pm.getRuntimePreset(
        kv8.value(QStringLiteral("runtimePresetId")).toString());
    QCOMPARE(kv8Runtime.value(QStringLiteral("cacheType")).toString(), QStringLiteral("q8_0"));
    const QStringList kv8Args = kv8.value(QStringLiteral("extraArgs")).toStringList();
    QCOMPARE(argValue(kv8Args, QStringLiteral("--cache-type-k")), QStringLiteral("q8_0"));
    QCOMPARE(argValue(kv8Args, QStringLiteral("--cache-type-v")), QStringLiteral("q8_0"));

    const QVariantMap tensor = findLaunch(QStringLiteral("sys-bench-48-qwen38-q6-tensor"));
    QVERIFY(!tensor.isEmpty());
    const QStringList tensorArgs = tensor.value(QStringLiteral("extraArgs")).toStringList();
    QCOMPARE(argValue(tensorArgs, QStringLiteral("--split-mode")), QStringLiteral("tensor"));
    QCOMPARE(argValue(tensorArgs, QStringLiteral("--tensor-split")), QStringLiteral("1,1"));

    const QVariantMap reasoning = findLaunch(QStringLiteral("sys-bench-48-qwen38-q6-reasoning-on"));
    QVERIFY(!reasoning.isEmpty());
    QCOMPARE(argValue(reasoning.value(QStringLiteral("extraArgs")).toStringList(),
                      QStringLiteral("--reasoning")), QStringLiteral("on"));
}

void SystemProfilesTests::bundle_qwen38Q8BenchmarkFamilyIsGatedAndExpanded()
{
    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    QJsonObject found;
    for (const QJsonValue &value : profiles) {
        if (value.toObject().value(QStringLiteral("id"))
                == QStringLiteral("sys-48-qwen38-27b-q8-196k")) {
            found = value.toObject();
            break;
        }
    }
    QVERIFY(!found.isEmpty());
    QCOMPARE(found.value(QStringLiteral("minVramGb")).toInt(), 48);
    QCOMPARE(found.value(QStringLiteral("minRamGb")).toInt(), 64);
    QVERIFY(!found.value(QStringLiteral("benchmark")).toBool());
    const QJsonObject model = found.value(QStringLiteral("model")).toObject();
    QCOMPARE(model.value(QStringLiteral("repo")).toString(),
             QStringLiteral("JonathanColetti/Qwen3.8-27B-Uncensored-GGUF"));
    QCOMPARE(model.value(QStringLiteral("file")).toString(),
             QStringLiteral("Qwen3.8-27B-Uncensored-Q8_0.gguf"));
    QCOMPARE(model.value(QStringLiteral("mmprojFile")).toString(),
             QStringLiteral("Qwen3.8-27B-Uncensored-vision-f16.gguf"));
    QCOMPARE(found.value(QStringLiteral("runtime")).toObject()
                 .value(QStringLiteral("ctx")).toInt(), 196608);
    QCOMPARE(found.value(QStringLiteral("runtime")).toObject()
                 .value(QStringLiteral("ubatch")).toInt(), 256);

    const QJsonArray variants = found.value(QStringLiteral("benchmarkVariants")).toArray();
    QCOMPARE(variants.size(), 3);
    ProfileManager pm;
    const QStringList ids = {
        QStringLiteral("sys-bench-48-qwen38-q8-tensor"),
        QStringLiteral("sys-bench-48-qwen38-q8-mmproj-cpu"),
        QStringLiteral("sys-bench-48-qwen38-q8-cache-warm")};
    for (const QString &id : ids) {
        const QVariantMap launch = pm.getLaunchProfile(id);
        QVERIFY2(!launch.isEmpty(), qPrintable(id));
        QVERIFY(!launch.value(QStringLiteral("benchmark")).toBool());
        const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
        if (id.endsWith(QStringLiteral("-tensor"))) {
            QCOMPARE(args.value(args.indexOf(QStringLiteral("--split-mode")) + 1),
                     QStringLiteral("tensor"));
        } else if (id.endsWith(QStringLiteral("-mmproj-cpu"))) {
            QVERIFY(args.contains(QStringLiteral("--no-mmproj-offload")));
        } else {
            QVERIFY(args.contains(QStringLiteral("--cache-prompt")));
            QCOMPARE(args.value(args.indexOf(QStringLiteral("--cache-reuse")) + 1),
                     QStringLiteral("512"));
        }
    }
}

void SystemProfilesTests::bundle_bigBangDisablesCrashyFlashAttention()
{
    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();

    QJsonObject bigBang;
    for (const QJsonValue &value : profiles) {
        const QJsonObject profile = value.toObject();
        if (profile.value("id").toString() == QStringLiteral("sys-48-bigbang-v1-q4km")) {
            bigBang = profile;
            break;
        }
    }

    QVERIFY2(!bigBang.isEmpty(), "falta el perfil base BigBang");
    QVERIFY2(!bigBang.value("runtime").toObject().value("flashAttn").toBool(),
             "BigBang no debe reactivar el kernel Flash Attention que crashea en CUDA");
    QCOMPARE(bigBang.value("runtime").toObject().value("kv").toString(),
             QStringLiteral("q8_0"));
    QCOMPARE(bigBang.value("runtime").toObject().value("batch").toInt(), 512);
    QCOMPARE(bigBang.value("runtime").toObject().value("ubatch").toInt(), 128);
    QStringList args;
    for (const QJsonValue &value : bigBang.value("extraArgs").toArray())
        args << value.toString();
    const int flashArg = args.indexOf(QStringLiteral("--flash-attn"));
    QVERIFY2(flashArg >= 0 && args.value(flashArg + 1) == QStringLiteral("off"),
             "llama.cpp b10331 requiere --flash-attn off explicito; omitirlo usa auto");
    const QString comment = bigBang.value("comment").toString();
    QVERIFY(comment.contains(QStringLiteral("Flash Attention")));

    bool foundBestVariant = false;
    for (const QJsonValue &value : bigBang.value("benchmarkVariants").toArray()) {
        if (value.toObject().value("id").toString()
            == QStringLiteral("sys-bench-48-bigbang-post")) {
            foundBestVariant = true;
            QVERIFY2(!value.toObject().value("runtime").toObject().contains("flashAttn"),
                     "la variante debe heredar Flash Attention desactivado del perfil base");
            const QJsonObject overrides = value.toObject().value("extraArgOverrides").toObject();
            QCOMPARE(overrides.value(QStringLiteral("--spec-type")).toString(),
                     QStringLiteral("draft-mtp"));
            QCOMPARE(overrides.value(QStringLiteral("--spec-draft-n-max")).toString(),
                     QStringLiteral("5"));
            const QJsonObject runtime = value.toObject().value("runtime").toObject();
            QCOMPARE(runtime.value(QStringLiteral("ctx")).toInt(), 65536);
            QCOMPARE(runtime.value(QStringLiteral("batch")).toInt(), 256);
            QCOMPARE(runtime.value(QStringLiteral("ubatch")).toInt(), 64);
        }
    }
    QVERIFY(foundBestVariant);

    // Las reparaciones de BigBang son copias explícitas: no cambian el perfil
    // histórico con Flash Attention desactivado, pero sí deben evitar la
    // combinación 131k/B512+ que produjo illegal memory access en CUDA.
    ProfileManager pm;
    const QHash<QString, bool> repairMtp = {
        {QStringLiteral("sys-repair-48-bigbang-mtp"), true},
        {QStringLiteral("sys-repair-48-bigbang-mtp-balance"), true},
        {QStringLiteral("sys-repair-48-bigbang-base"), false},
    };
    for (auto it = repairMtp.cbegin(); it != repairMtp.cend(); ++it) {
        const QVariantMap launch = pm.getLaunchProfile(it.key());
        QVERIFY2(!launch.isEmpty(), qPrintable(it.key()));
        const QVariantMap runtime = pm.getRuntimePreset(
            launch.value(QStringLiteral("runtimePresetId")).toString());
        QCOMPARE(runtime.value(QStringLiteral("ctx")).toInt(), 65536);
        QCOMPARE(runtime.value(QStringLiteral("batch")).toInt(), 256);
        QCOMPARE(runtime.value(QStringLiteral("ubatch")).toInt(), 64);
        QCOMPARE(runtime.value(QStringLiteral("cacheType")).toString(), QStringLiteral("q8_0"));
        QVERIFY(runtime.value(QStringLiteral("flashAttention")).toBool());
        const QStringList args = launch.value(QStringLiteral("extraArgs")).toStringList();
        QCOMPARE(args.value(args.indexOf(QStringLiteral("--flash-attn")) + 1), QStringLiteral("on"));
        QCOMPARE(args.value(args.indexOf(QStringLiteral("--cache-type-k")) + 1), QStringLiteral("q8_0"));
        QCOMPARE(args.value(args.indexOf(QStringLiteral("--cache-type-v")) + 1), QStringLiteral("q8_0"));
        const bool hasMtp = args.contains(QStringLiteral("--spec-type"));
        QCOMPARE(hasMtp, it.value());
    }
}

void SystemProfilesTests::bundle_quantizationPolicyCapsKvAtQ8()
{
    QFile bundle(bundlePath());
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonArray profiles = QJsonDocument::fromJson(bundle.readAll()).array();
    const auto allowed = [](const QString &value) {
        return value.isEmpty() || value.compare(QStringLiteral("q8_0"), Qt::CaseInsensitive) == 0
            || value.compare(QStringLiteral("q4_0"), Qt::CaseInsensitive) == 0
            || value.compare(QStringLiteral("q5_1"), Qt::CaseInsensitive) == 0
            || value.compare(QStringLiteral("q6_k"), Qt::CaseInsensitive) == 0;
    };
    const auto checkCacheArgs = [&allowed](const QJsonObject &object) {
        const QJsonArray raw = object.value(QStringLiteral("extraArgs")).toArray();
        for (int i = 0; i + 1 < raw.size(); ++i) {
            const QString flag = raw.at(i).toString();
            if (flag == QStringLiteral("--cache-type-k")
                || flag == QStringLiteral("--cache-type-v"))
                QVERIFY2(allowed(raw.at(i + 1).toString()), qPrintable(flag));
        }
    };

    for (const QJsonValue &value : profiles) {
        const QJsonObject profile = value.toObject();
        const QString id = profile.value(QStringLiteral("id")).toString();
        const QJsonObject runtime = profile.value(QStringLiteral("runtime")).toObject();
        const QString kv = runtime.value(QStringLiteral("kv")).toString();
        const bool lidF16 = id == QStringLiteral("sys-ultraq-dsv4-0731-lid-cuda");
        QVERIFY2(lidF16 || allowed(kv), qPrintable(id));
        checkCacheArgs(profile);
        for (const QJsonValue &variantValue : profile.value(QStringLiteral("benchmarkVariants")).toArray()) {
            const QJsonObject variant = variantValue.toObject();
            const QString variantId = variant.value(QStringLiteral("id")).toString();
            if (variantId.isEmpty()) continue;
            const QJsonObject variantRuntime = variant.value(QStringLiteral("runtime")).toObject();
            QVERIFY2(allowed(variantRuntime.value(QStringLiteral("kv")).toString()),
                     qPrintable(variantId));
            const QJsonObject overrides = variant.value(QStringLiteral("extraArgOverrides")).toObject();
            for (const QString &flag : {QStringLiteral("--cache-type-k"), QStringLiteral("--cache-type-v")})
                QVERIFY2(allowed(overrides.value(flag).toString()), qPrintable(variantId));
        }
    }
}

void SystemProfilesTests::bundle_bestProfilesUseRequestedCategoryNames()
{
    ProfileManager pm;
    const QHash<QString, QString> expected{
        {QStringLiteral("sys-qwen38-27b-udq4-131k"),
         QStringLiteral("BALANCE - Qwen3.8 UD-Q4 visión")},
        {QStringLiteral("sys-bench-qwen38-udq4-mtp4"),
         QStringLiteral("BALANCE - Qwen3.8 UD-Q4 MTP4")},
        {QStringLiteral("sys-48-katcoder-262k"),
         QStringLiteral("FAST - KAT2-Coder-7-8-26")},
        {QStringLiteral("sys-bench-48-bigbang-post"),
         QStringLiteral("FAST - BigBang · MTP · top-p 0.08")},
        {QStringLiteral("sys-48-thinkingcap-mtp"),
         QStringLiteral("BALANCE - ThinkingCap+MTP-7-8-26")},
        {QStringLiteral("sys-laguna-s-2-1-q2-48gb"),
         QStringLiteral("BALANCE - Laguna S 2.1 118B-A8B Q2")},
    };
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        const QVariantMap launch = pm.getLaunchProfile(it.key());
        QVERIFY2(!launch.isEmpty(), qPrintable(it.key()));
        const QString displayName =
            launch.value(QStringLiteral("displayName")).toString();
        QVERIFY2(displayName.endsWith(it.value()), qPrintable(displayName));
    }
}

QTEST_MAIN(SystemProfilesTests)
#include "test_system_profiles.moc"
