// Unit tests de perfiles:
//   - ProfileTypes: serialización JSON ida/vuelta de cada struct de perfil.
//   - ProfileManager: CRUD de backends/modelProfiles/runtimes/harness/launch,
//     getters, favoritos/alias y persistencia a disco (aislada vía
//     LLAMACODE_PROFILES_DIR).
//
// NOTA: ProfileManager::storagePath cachea la raíz en un 'static' la primera
// vez que se llama, así que el env var DEBE setearse antes de construir el
// primer ProfileManager del proceso (lo hacemos en initTestCase).

#include <QtTest>
#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>
#include "core/profiles/ProfileTypes.h"
#include "core/profiles/ProfileManager.h"

class ProfilesTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void backendProfile_jsonRoundTrip();
    void modelProfile_jsonRoundTrip();
    void runtimePreset_jsonRoundTrip();
    void launchProfile_jsonRoundTrip();
    void agentProfile_thinkingLeakGuardRoundTripAndDefault();
    void masterConfig_jsonRoundTrip();
    void masterConfig_legacyMigration();

    void manager_addGetRemoveBackend();
    void manager_updateBackendPort();
    void manager_setBackendCloud();
    void manager_addModelProfile();
    void manager_favoriteAndAlias();
    void manager_benchmarkQueue();
    void manager_profileSearchFiltersNameAliasAndId();
    void manager_tagsAndLastUsed();
    void manager_profileTemplatesRoundTrip();
    void manager_harnessArgsEnvRoundTrip();
    void manager_deprecatedIsProfilesOnly();
    void manager_browserAutomationOverride();
    void manager_systemProfilesReloadIdempotent();
    void manager_persistsAcrossInstances();
    void manager_exportImportBundle();
    void manager_profileChangeHistory();

    void migracion_copiaLosPerfilesSiElDestinoEstaVacio();
    void migracion_noPisaLoQueYaHayEnElDestino();
    void migracion_sinOrigenNoHaceNada();
    void rutaDePerfiles_respetaLaVariableDeEntorno();
    void rutaDePerfiles_bajoTestNuncaEsLaDelUsuario();
    void rutaDePerfiles_imprimeLaRaiz();

private:
    QTemporaryDir m_dir;
};

void ProfilesTests::initTestCase()
{
    QVERIFY(m_dir.isValid());
    qputenv("LLAMACODE_PROFILES_DIR", m_dir.path().toLocal8Bit());
}

void ProfilesTests::backendProfile_jsonRoundTrip()
{
    BackendProfile b;
    b.id = "b1"; b.name = "cuda-8080"; b.binaryId = "bin1";
    b.host = "0.0.0.0"; b.port = 9090;
    b.baseArgs = QStringList{"--ctx", "8192"};
    b.envOverrides.insert("CUDA_VISIBLE_DEVICES", "0");
    const BackendProfile r = BackendProfile::fromJson(b.toJson());
    QCOMPARE(r.id, b.id);
    QCOMPARE(r.name, b.name);
    QCOMPARE(r.binaryId, b.binaryId);
    QCOMPARE(r.host, b.host);
    QCOMPARE(r.port, b.port);
    QCOMPARE(r.baseArgs, b.baseArgs);
    QCOMPARE(r.envOverrides.value("CUDA_VISIBLE_DEVICES"), QStringLiteral("0"));
    // default kind = local, sin campos cloud
    QCOMPARE(r.kind, QStringLiteral("local"));
    QVERIFY(!r.isCloud());

    // provider cloud round-trip
    BackendProfile c;
    c.id = "c1"; c.kind = "cloud";
    c.cloudBaseUrl = "https://api.openai.com";
    c.cloudKeyRef = "OPENAI_API_KEY"; c.cloudModel = "gpt-4o"; c.cloudCtx = 16384;
    const QJsonObject cj = c.toJson();
    // El secreto NUNCA se serializa: sólo la referencia (nombre).
    QVERIFY(!cj.contains("cloudApiKey"));
    QCOMPARE(cj.value("cloudKeyRef").toString(), QStringLiteral("OPENAI_API_KEY"));
    const BackendProfile rc = BackendProfile::fromJson(cj);
    QVERIFY(rc.isCloud());
    QCOMPARE(rc.cloudBaseUrl, c.cloudBaseUrl);
    QCOMPARE(rc.cloudKeyRef, c.cloudKeyRef);
    QCOMPARE(rc.cloudModel, c.cloudModel);
    QCOMPARE(rc.cloudCtx, 16384);
}

void ProfilesTests::modelProfile_jsonRoundTrip()
{
    ModelProfile m;
    m.id = "m1"; m.name = "qwen"; m.modelId = "cat1";
    m.mmprojId = "mm1"; m.draftModelId = "d1";
    m.specType = "draft-dspark"; m.specDraftNMax = 3; m.specDraftNMin = 2;
    m.specDraftAdaptive = true; m.specDraftConfMin = 0.6;
    m.specDraftNgl = "all";
    m.specDraftTypeK = "q8_0"; m.specDraftTypeV = "q8_0";
    const ModelProfile r = ModelProfile::fromJson(m.toJson());
    QCOMPARE(r.name, m.name);
    QCOMPARE(r.modelId, m.modelId);
    QCOMPARE(r.mmprojId, m.mmprojId);
    QCOMPARE(r.draftModelId, m.draftModelId);
    QCOMPARE(r.specType, m.specType);
    QCOMPARE(r.specDraftNMax, m.specDraftNMax);
    QCOMPARE(r.specDraftNMin, m.specDraftNMin);
    QCOMPARE(r.specDraftAdaptive, m.specDraftAdaptive);
    QCOMPARE(r.specDraftConfMin, m.specDraftConfMin);
    QCOMPARE(r.specDraftNgl, m.specDraftNgl);
    QCOMPARE(r.specDraftTypeK, m.specDraftTypeK);
    QCOMPARE(r.specDraftTypeV, m.specDraftTypeV);

    // Perfiles anteriores no tienen los campos nuevos y deben seguir siendo
    // speculative fijo, sin activar adaptive por defecto.
    const ModelProfile legacy = ModelProfile::fromJson(QJsonObject{
        {QStringLiteral("specType"), QStringLiteral("draft-mtp")},
        {QStringLiteral("specDraftNMax"), 3}
    });
    QCOMPARE(legacy.specDraftNMin, 0);
    QVERIFY(!legacy.specDraftAdaptive);
}

void ProfilesTests::runtimePreset_jsonRoundTrip()
{
    RuntimePreset p;
    p.id = "r1"; p.name = "fast"; p.ctx = 16384; p.batch = 1024; p.ubatch = 256;
    p.threads = 8; p.gpuLayers = 99; p.flashAttention = true; p.mmap = false;
    p.mlock = true; p.contBatching = false; p.cacheType = "q8_0"; p.parallelSlots = 4;
    const RuntimePreset r = RuntimePreset::fromJson(p.toJson());
    QCOMPARE(r.ctx, p.ctx);
    QCOMPARE(r.batch, p.batch);
    QCOMPARE(r.ubatch, p.ubatch);
    QCOMPARE(r.threads, p.threads);
    QCOMPARE(r.gpuLayers, p.gpuLayers);
    QCOMPARE(r.flashAttention, p.flashAttention);
    QCOMPARE(r.mmap, p.mmap);
    QCOMPARE(r.mlock, p.mlock);
    QCOMPARE(r.contBatching, p.contBatching);
    QCOMPARE(r.cacheType, p.cacheType);
    QCOMPARE(r.parallelSlots, p.parallelSlots);
}

void ProfilesTests::launchProfile_jsonRoundTrip()
{
    LaunchProfile l;
    l.id = "l1"; l.name = "prod"; l.alias = "P"; l.best = true; l.favorite = true;
    l.systemBadge = true; l.benchmark = true; l.deprecated = true;
    l.backendProfileId = "b1"; l.modelProfileId = "m1"; l.runtimePresetId = "r1";
    l.extraArgs = QStringList{"--verbose"};
    MasterFallback mf; mf.type = "cli"; mf.cliName = "claude";
    l.master.fallbacks.append(mf);
    l.powerLimitW = 280;
    l.browserAutomation = "on";
    l.tags = QStringList{"coding", "local"};
    l.lastUsed = 123456789;
    l.plannerProfileId = "planner-maxq";
    l.hybridMode = "sequential";
    const LaunchProfile r = LaunchProfile::fromJson(l.toJson());
    QCOMPARE(r.browserAutomation, QStringLiteral("on"));
    QCOMPARE(r.name, l.name);
    QCOMPARE(r.alias, l.alias);
    QCOMPARE(r.best, l.best);
    QCOMPARE(r.favorite, l.favorite);
    QCOMPARE(r.systemBadge, l.systemBadge);
    QCOMPARE(r.benchmark, l.benchmark);
    QCOMPARE(r.deprecated, l.deprecated);
    QCOMPARE(r.backendProfileId, l.backendProfileId);
    QCOMPARE(r.modelProfileId, l.modelProfileId);
    QCOMPARE(r.extraArgs, l.extraArgs);
    QCOMPARE(r.master.fallbacks.size(), 1);
    QCOMPARE(r.master.fallbacks.first().type, QStringLiteral("cli"));
    QCOMPARE(r.master.fallbacks.first().cliName, QStringLiteral("claude"));
    QCOMPARE(r.powerLimitW, 280);
    QCOMPARE(r.plannerProfileId, QStringLiteral("planner-maxq"));
    QCOMPARE(r.hybridMode, QStringLiteral("sequential"));
    QCOMPARE(r.tags, l.tags);
    QCOMPARE(r.lastUsed, l.lastUsed);
    // Default (campo ausente) → 0 = sin override.
    LaunchProfile empty;
    QCOMPARE(LaunchProfile::fromJson(empty.toJson()).powerLimitW, 0);
    // browserAutomation ausente → default "inherit".
    QCOMPARE(LaunchProfile::fromJson(empty.toJson()).browserAutomation,
             QStringLiteral("inherit"));
    QCOMPARE(LaunchProfile::fromJson(QJsonObject{}).hybridMode,
             QStringLiteral("off"));
}

void ProfilesTests::agentProfile_thinkingLeakGuardRoundTripAndDefault()
{
    AgentProfile profile;
    profile.id = QStringLiteral("nanbeige-agent");
    profile.name = QStringLiteral("Nanbeige compat");
    profile.thinkingLeakGuard = true;
    const AgentProfile restored = AgentProfile::fromJson(profile.toJson());
    QVERIFY(restored.thinkingLeakGuard);

    // Perfiles viejos y perfiles nuevos sin opt-in conservan el comportamiento
    // estándar del template/modelo.
    QVERIFY(!AgentProfile::fromJson(QJsonObject{}).thinkingLeakGuard);
    for (const AgentProfile &preset : AgentProfile::systemPresets())
        QVERIFY(!preset.thinkingLeakGuard);
}

void ProfilesTests::masterConfig_jsonRoundTrip()
{
    MasterConfig m;
    m.escalation = "auto"; m.autoAfterFails = 5;
    // Cadena de 3 fallbacks ordenados: http → profile → cli.
    MasterFallback f1; f1.type = "http"; f1.label = "GPT";
    f1.httpUrl = "https://api.openai.com"; f1.httpModel = "gpt-4o";
    f1.httpKeyRef = "master/openai"; f1.timeoutSec = 600;
    MasterFallback f2; f2.type = "profile"; f2.profileId = "lp-big"; f2.label = "Cloud profile";
    MasterFallback f3; f3.type = "cli"; f3.cliName = "claude"; f3.applyEdits = false;
    m.fallbacks << f1 << f2 << f3;
    QVERIFY(m.isConfigured());

    const MasterConfig r = MasterConfig::fromJson(m.toJson());
    QCOMPARE(r.escalation, QStringLiteral("auto"));
    QCOMPARE(r.autoAfterFails, 5);
    QCOMPARE(r.fallbacks.size(), 3);
    // Orden preservado.
    QCOMPARE(r.fallbacks[0].type, QStringLiteral("http"));
    QCOMPARE(r.fallbacks[0].httpKeyRef, QStringLiteral("master/openai"));
    QCOMPARE(r.fallbacks[0].timeoutSec, 600);
    QCOMPARE(r.fallbacks[1].type, QStringLiteral("profile"));
    QCOMPARE(r.fallbacks[1].profileId, QStringLiteral("lp-big"));
    QCOMPARE(r.fallbacks[2].type, QStringLiteral("cli"));
    QCOMPARE(r.fallbacks[2].cliName, QStringLiteral("claude"));
    QCOMPARE(r.fallbacks[2].applyEdits, false);

    QVERIFY(!MasterConfig{}.isConfigured());  // default: cadena vacía
}

void ProfilesTests::masterConfig_legacyMigration()
{
    // Perfil viejo: un solo maestro http con key en claro, sin array fallbacks.
    QJsonObject o;
    o["kind"] = "http"; o["httpUrl"] = "http://x"; o["httpModel"] = "big";
    o["httpKey"] = "secret-k"; o["escalation"] = "both"; o["autoAfterFails"] = 4;
    const MasterConfig r = MasterConfig::fromJson(o);
    QCOMPARE(r.fallbacks.size(), 1);
    QCOMPARE(r.fallbacks.first().type, QStringLiteral("http"));
    QCOMPARE(r.fallbacks.first().httpUrl, QStringLiteral("http://x"));
    QCOMPARE(r.fallbacks.first().httpKeyRef, QStringLiteral("secret-k"));
    QCOMPARE(r.escalation, QStringLiteral("both"));
    QCOMPARE(r.autoAfterFails, 4);

    // Legacy CLI.
    QJsonObject c; c["kind"] = "cli"; c["cliName"] = "codex";
    const MasterConfig rc = MasterConfig::fromJson(c);
    QCOMPARE(rc.fallbacks.size(), 1);
    QCOMPARE(rc.fallbacks.first().type, QStringLiteral("cli"));
    QCOMPARE(rc.fallbacks.first().cliName, QStringLiteral("codex"));

    // Legacy none → cadena vacía.
    QJsonObject n; n["kind"] = "none";
    QVERIFY(!MasterConfig::fromJson(n).isConfigured());
}

void ProfilesTests::manager_addGetRemoveBackend()
{
    ProfileManager pm;
    const QString id = pm.addBackend("be", "bin1", "127.0.0.1", 8080);
    QVERIFY(!id.isEmpty());
    const QVariantMap got = pm.getBackend(id);
    QCOMPARE(got.value("name").toString(), QStringLiteral("be"));
    QCOMPARE(got.value("port").toInt(), 8080);
    QVERIFY(pm.updateBackend(id, "be2", "bin1", "0.0.0.0", 9000, QStringList{"--x"}));
    QCOMPARE(pm.getBackend(id).value("name").toString(), QStringLiteral("be2"));
    QVERIFY(pm.removeBackend(id));
    QVERIFY(pm.getBackend(id).isEmpty());
    QVERIFY(!pm.removeBackend("nope"));
}

void ProfilesTests::manager_updateBackendPort()
{
    ProfileManager pm;
    const QString id = pm.addBackend("be", "bin1", "127.0.0.1", 8080);
    QVERIFY(!id.isEmpty());
    QVERIFY(pm.updateBackendPort(id, 8082));
    QCOMPARE(pm.getBackend(id).value("port").toInt(), 8082);
    QCOMPARE(pm.resolveBackend(id).port, 8082);
    QVERIFY(!pm.updateBackendPort(id, 0));
    QVERIFY(!pm.updateBackendPort(id, 70000));
    QVERIFY(!pm.updateBackendPort("nope", 8083));
}

void ProfilesTests::manager_setBackendCloud()
{
    ProfileManager pm;
    const QString id = pm.addBackend("be", "bin1", "127.0.0.1", 8080);
    QVERIFY(pm.setBackendCloud(id, "cloud", "https://openrouter.ai/api", "OPENROUTER_KEY",
                               "anthropic/claude-sonnet-4", 200000));
    const QVariantMap got = pm.getBackend(id);
    QCOMPARE(got.value("kind").toString(), QStringLiteral("cloud"));
    QCOMPARE(got.value("cloudKeyRef").toString(), QStringLiteral("OPENROUTER_KEY"));
    QCOMPARE(got.value("cloudBaseUrl").toString(), QStringLiteral("https://openrouter.ai/api"));
    QCOMPARE(got.value("cloudModel").toString(), QStringLiteral("anthropic/claude-sonnet-4"));
    QCOMPARE(got.value("cloudCtx").toInt(), 200000);
    // volver a local limpia el flag
    QVERIFY(pm.setBackendCloud(id, "local", "", "", "", 0));
    QCOMPARE(pm.getBackend(id).value("kind").toString(), QStringLiteral("local"));
    QVERIFY(!pm.setBackendCloud("nope", "cloud", "", "", "", 0));
}

void ProfilesTests::manager_addModelProfile()
{
    ProfileManager pm;
    const QString id = pm.addModelProfile("qwen", "cat1", "", "");
    QVERIFY(!id.isEmpty());
    QCOMPARE(pm.getModelProfile(id).value("modelId").toString(), QStringLiteral("cat1"));

    // setModelSpec persiste la configuración DSpark y getModelProfile la expone.
    QVERIFY(pm.setModelSpec(id, "draft-dspark", 3, "all", "q8_0", "q8_0", 0.6, 2, true));
    const QVariantMap m = pm.getModelProfile(id);
    QCOMPARE(m.value("specType").toString(), QStringLiteral("draft-dspark"));
    QCOMPARE(m.value("specDraftNMax").toInt(), 3);
    QCOMPARE(m.value("specDraftNMin").toInt(), 2);
    QVERIFY(m.value("specDraftAdaptive").toBool());
    QCOMPARE(m.value("specDraftConfMin").toDouble(), 0.6);
    QCOMPARE(m.value("specDraftTypeK").toString(), QStringLiteral("q8_0"));

    QVERIFY(pm.removeModelProfile(id));
}

void ProfilesTests::manager_favoriteAndAlias()
{
    ProfileManager pm;
    const QString id = pm.addLaunchProfile("L", "b", "m", "r");
    QVERIFY(!id.isEmpty());
    pm.setLaunchFavorite(id, true);
    QVERIFY(pm.updateLaunchProfile(QVariantMap{{"id", id}, {"best", true}}));
    pm.setLaunchAlias(id, "Alias");
    const QVariantList menu = pm.launchProfilesForMenu();
    QVERIFY(!menu.isEmpty());
    const auto it = std::find_if(menu.cbegin(), menu.cend(), [&](const QVariant &value) {
        return value.toMap().value("id").toString() == id;
    });
    QVERIFY(it != menu.cend());
    const QVariantMap row = it->toMap();
    QCOMPARE(row.value("alias").toString(), QStringLiteral("Alias"));
    QVERIFY(row.value("favorite").toBool());
    // displayName antepone la estrella a los favoritos y muestra alias + nombre.
    QCOMPARE(row.value("displayName").toString(), QStringLiteral("⚡ ★ Alias - 1_L"));
    QVERIFY(row.value("best").toBool());
    QCOMPARE(pm.getLaunchProfile(id).value("displayName").toString(),
             QStringLiteral("Alias - 1_L"));
}

void ProfilesTests::manager_benchmarkQueue()
{
    ProfileManager pm;
    const QString id = pm.addLaunchProfile(QStringLiteral("Pendiente"), "b", "m", "r");
    QVERIFY(!id.isEmpty());

    pm.setLaunchBenchmark(id, true);
    const QVariantMap queued = pm.getLaunchProfile(id);
    QVERIFY(queued.value(QStringLiteral("benchmark")).toBool());

    const QVariantList menu = pm.launchProfilesForMenu();
    QVERIFY(std::any_of(menu.cbegin(), menu.cend(), [&](const QVariant &value) {
        const QVariantMap row = value.toMap();
        return row.value(QStringLiteral("id")).toString() == id
            && row.value(QStringLiteral("benchmark")).toBool()
            && row.value(QStringLiteral("displayName")).toString().contains(QStringLiteral("🏆"));
    }));

    {
        ProfileManager reloaded;
        QVERIFY(reloaded.getLaunchProfile(id).value(QStringLiteral("benchmark")).toBool());
    }

    pm.setLaunchBenchmark(id, false);
    QVERIFY(!pm.getLaunchProfile(id).value(QStringLiteral("benchmark")).toBool());
}

void ProfilesTests::manager_profileSearchFiltersNameAliasAndId()
{
    ProfileManager pm;
    const QString id = pm.addLaunchProfile(QStringLiteral("Qwen análisis"), {}, {}, {});
    QVERIFY(!id.isEmpty());
    pm.setLaunchAlias(id, QStringLiteral("Documentos"));

    const QVariantList byName = pm.launchProfilesForProfilesPage(QStringLiteral("ANÁLISIS"));
    QVERIFY(std::any_of(byName.cbegin(), byName.cend(), [&](const QVariant &v) {
        return v.toMap().value(QStringLiteral("id")).toString() == id;
    }));
    const QVariantList byAlias = pm.launchProfilesForProfilesPage(QStringLiteral("documentos"));
    QCOMPARE(byAlias.size(), 1);
    QCOMPARE(byAlias.first().toMap().value(QStringLiteral("id")).toString(), id);
    const QVariantList byId = pm.launchProfilesForProfilesPage(id.left(8));
    QVERIFY(std::any_of(byId.cbegin(), byId.cend(), [&](const QVariant &v) {
        return v.toMap().value(QStringLiteral("id")).toString() == id;
    }));
    QVERIFY(pm.launchProfilesForProfilesPage(QStringLiteral("no existe")).isEmpty());
}

void ProfilesTests::manager_tagsAndLastUsed()
{
    ProfileManager pm;
    const QString id = pm.addLaunchProfile(QStringLiteral("Tagged"), {}, {}, {});
    QVERIFY(!id.isEmpty());
    QVERIFY(pm.updateLaunchProfile(QVariantMap{{"id", id}, {"tags", QStringList{" coding ", "local", "CODING"}}}));
    const QVariantMap before = pm.getLaunchProfile(id);
    QCOMPARE(before.value("tags").toStringList(), QStringList({"coding", "local"}));
    QCOMPARE(before.value("lastUsed").toLongLong(), 0);

    pm.markLaunchUsed(id);
    const QVariantMap after = pm.getLaunchProfile(id);
    QVERIFY(after.value("lastUsed").toLongLong() > 0);
    const QVariantList byTag = pm.launchProfilesForProfilesPage(QStringLiteral("LOCAL"));
    QVERIFY(std::any_of(byTag.cbegin(), byTag.cend(), [&](const QVariant &value) {
        return value.toMap().value("id").toString() == id;
    }));
}

void ProfilesTests::manager_profileTemplatesRoundTrip()
{
    ProfileManager pm;
    const QString launch = pm.addLaunchProfile(QStringLiteral("Template source"), "be", "mo", "rt");
    QVERIFY(!launch.isEmpty());
    QVERIFY(pm.updateLaunchProfile(QVariantMap{{"id", launch}, {"tags", QStringList{"coding"}},
                                               {"extraArgs", QStringList{"--temp", "0.6"}},
                                               {"envOverrides", QVariantMap{{"TEST_TEMPLATE", "1"}}}}));
    const QString tid = pm.saveLaunchAsTemplate(launch, QStringLiteral("Coding local"));
    QVERIFY(!tid.isEmpty());
    const QVariantList templates = pm.profileTemplates();
    QVERIFY(std::any_of(templates.cbegin(), templates.cend(), [&](const QVariant &v) {
        return v.toMap().value(QStringLiteral("id")).toString() == tid;
    }));
    const QString copy = pm.createLaunchFromTemplate(tid, QStringLiteral("From template"));
    QVERIFY(!copy.isEmpty());
    const QVariantMap restored = pm.getLaunchProfile(copy);
    QCOMPARE(restored.value(QStringLiteral("tags")).toStringList(), QStringList{"coding"});
    const QStringList expectedArgs{"--temp", "0.6"};
    QCOMPARE(restored.value(QStringLiteral("extraArgs")).toStringList(), expectedArgs);
    QCOMPARE(restored.value(QStringLiteral("envOverrides")).toMap().value(QStringLiteral("TEST_TEMPLATE")).toString(), QStringLiteral("1"));
    QVERIFY(pm.removeProfileTemplate(tid));
}

void ProfilesTests::manager_harnessArgsEnvRoundTrip()
{
    ProfileManager pm;
    const QString id = pm.addHarness(QStringLiteral("Harness"), QStringLiteral("llamaagent"));
    QVERIFY(pm.updateHarness(QVariantMap{{"id", id}, {"args", QStringList{"--quiet"}},
                                         {"env", QVariantMap{{"HARNESS_MODE", "test"}}}}));
    const QVariantMap got = pm.getHarness(id);
    QCOMPARE(got.value(QStringLiteral("args")).toStringList(), QStringList{"--quiet"});
    QCOMPARE(got.value(QStringLiteral("env")).toMap().value(QStringLiteral("HARNESS_MODE")).toString(), QStringLiteral("test"));
}

void ProfilesTests::manager_browserAutomationOverride()
{
    ProfileManager pm;
    const QString id = pm.addLaunchProfile("BA", "b", "m", "r");
    QVERIFY(!id.isEmpty());
    // Default: campo ausente → "inherit".
    QCOMPARE(pm.getLaunchProfile(id).value("browserAutomation").toString(),
             QStringLiteral("inherit"));
    // update/get round-trip del override.
    QVERIFY(pm.updateLaunchProfile(QVariantMap{
        {"id", id}, {"browserAutomation", "off"}}));
    QCOMPARE(pm.getLaunchProfile(id).value("browserAutomation").toString(),
             QStringLiteral("off"));
}

void ProfilesTests::manager_deprecatedIsProfilesOnly()
{
    ProfileManager pm;
    const QString id = pm.addLaunchProfile("Deprecated", "b", "m", "r");
    QVERIFY(!id.isEmpty());
    QVERIFY(pm.updateLaunchProfile(QVariantMap{{"id", id}, {"deprecated", true},
                                                {"systemBadge", true}, {"benchmark", true}}));
    QCOMPARE(pm.getLaunchProfile(id).value("deprecated").toBool(), true);
    QCOMPARE(pm.getLaunchProfile(id).value("systemBadge").toBool(), true);
    QCOMPARE(pm.getLaunchProfile(id).value("benchmark").toBool(), true);
    const QVariantList menu = pm.launchProfilesForMenu();
    for (const QVariant &v : menu)
        QVERIFY(v.toMap().value("id").toString() != id);
    const QVariantList profiles = pm.launchProfilesForProfilesPage();
    bool found = false;
    for (const QVariant &v : profiles) {
        if (v.toMap().value("id").toString() == id) {
            found = true;
            QCOMPARE(v.toMap().value("deprecated").toBool(), true);
            QVERIFY(v.toMap().value("displayName").toString().startsWith(QStringLiteral("⚙ 🏆 ⚠ ")));
        }
    }
    QVERIFY(found);
}

void ProfilesTests::manager_systemProfilesReloadIdempotent()
{
    ProfileManager pm;
    const int launchCount = pm.launchProfiles()->rowCount();
    const int modelCount = pm.modelProfiles()->rowCount();
    const int runtimeCount = pm.runtimePresets()->rowCount();
    const int backendCount = pm.backendProfiles()->rowCount();
    const int agentCount = pm.agentProfiles()->rowCount();

    pm.reloadFromDisk();
    QCOMPARE(pm.launchProfiles()->rowCount(), launchCount);
    QCOMPARE(pm.modelProfiles()->rowCount(), modelCount);
    QCOMPARE(pm.runtimePresets()->rowCount(), runtimeCount);
    QCOMPARE(pm.backendProfiles()->rowCount(), backendCount);
    QCOMPARE(pm.agentProfiles()->rowCount(), agentCount);
}

void ProfilesTests::manager_persistsAcrossInstances()
{
    QString id;
    {
        ProfileManager pm;
        id = pm.addBackend("persist", "bin1", "127.0.0.1", 7777);
        pm.saveProfiles();
    }
    {
        ProfileManager pm2;  // recarga de disco en el ctor
        QCOMPARE(pm2.getBackend(id).value("name").toString(), QStringLiteral("persist"));
    }
}

// ── Migración de la ruta de perfiles ─────────────────────────────────────────
// Antes la carpeta estaba escrita a mano y apuntaba al usuario del
// desarrollador original; ahora usa Documentos del usuario actual. La migración
// tiene que traer los perfiles existentes sin pisar los del destino.

void ProfilesTests::migracion_copiaLosPerfilesSiElDestinoEstaVacio()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString origen  = tmp.filePath(QStringLiteral("viejo"));
    const QString destino = tmp.filePath(QStringLiteral("nuevo"));
    QVERIFY(QDir().mkpath(origen));

    for (const QString &e : {QStringLiteral("backends"), QStringLiteral("launches")}) {
        QFile f(origen + QLatin1Char('/') + e + QStringLiteral(".json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[]");
    }
    QCOMPARE(ProfileManager::migrarPerfiles(destino, origen), 2);
    QVERIFY(QFile::exists(destino + QStringLiteral("/backends.json")));
    QVERIFY(QFile::exists(destino + QStringLiteral("/launches.json")));
    // El origen no se toca: la migración copia, no mueve.
    QVERIFY(QFile::exists(origen + QStringLiteral("/backends.json")));
}

void ProfilesTests::migracion_noPisaLoQueYaHayEnElDestino()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString origen  = tmp.filePath(QStringLiteral("viejo"));
    const QString destino = tmp.filePath(QStringLiteral("nuevo"));
    QVERIFY(QDir().mkpath(origen));
    QVERIFY(QDir().mkpath(destino));

    QFile viejo(origen + QStringLiteral("/backends.json"));
    QVERIFY(viejo.open(QIODevice::WriteOnly));
    viejo.write("[\"del origen\"]");
    viejo.close();

    QFile nuevo(destino + QStringLiteral("/backends.json"));
    QVERIFY(nuevo.open(QIODevice::WriteOnly));
    nuevo.write("[\"del usuario\"]");
    nuevo.close();

    QCOMPARE(ProfileManager::migrarPerfiles(destino, origen), 0);
    QFile chequeo(destino + QStringLiteral("/backends.json"));
    QVERIFY(chequeo.open(QIODevice::ReadOnly));
    QCOMPARE(chequeo.readAll(), QByteArrayLiteral("[\"del usuario\"]"));
}

void ProfilesTests::migracion_sinOrigenNoHaceNada()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QCOMPARE(ProfileManager::migrarPerfiles(tmp.filePath(QStringLiteral("nuevo")),
                                            tmp.filePath(QStringLiteral("no-existe"))), 0);
    // Y si origen y destino son el mismo, tampoco.
    const QString mismo = tmp.filePath(QStringLiteral("igual"));
    QVERIFY(QDir().mkpath(mismo));
    QCOMPARE(ProfileManager::migrarPerfiles(mismo, mismo), 0);
}

void ProfilesTests::rutaDePerfiles_respetaLaVariableDeEntorno()
{
    // initTestCase la setea; la raíz se cachea, así que debe coincidir.
    QCOMPARE(QDir::cleanPath(ProfileManager::profilesRoot()),
             QDir::cleanPath(QString::fromLocal8Bit(qgetenv("LLAMACODE_PROFILES_DIR"))));
    // Y la ruta vieja ya no se usa como default.
    QVERIFY(!ProfileManager::profilesRoot().contains(QStringLiteral("cristian")));
}

void ProfilesTests::rutaDePerfiles_bajoTestNuncaEsLaDelUsuario()
{
    // Los perfiles viven en Documentos, y setTestModeEnabled() NO redirige esa
    // carpeta: sólo AppData y AppLocalData. Un test que se olvide de setear
    // LLAMACODE_PROFILES_DIR escribía perfiles de verdad en la máquina de quien
    // corre la suite —pasó: se acumularon 29 perfiles basura antes de notarlo—.
    //
    // Se comprueba en un proceso aparte porque profilesRoot() cachea su
    // resultado en un static: dentro de este binario ya quedó fijado por la
    // variable de entorno de initTestCase.
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("LLAMACODE_PROFILES_DIR"));
    p.setProcessEnvironment(env);
    p.start(QCoreApplication::applicationFilePath(),
            {QStringLiteral("rutaDePerfiles_imprimeLaRaiz")});
    QVERIFY(p.waitForFinished(30000));
    const QString salida = QString::fromLocal8Bit(p.readAllStandardOutput());

    const QString docs =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QVERIFY2(!docs.isEmpty(), "sin carpeta de Documentos no se puede comprobar");
    const QString raiz = salida.section(QStringLiteral("RAIZ="), 1).section('\n', 0, 0).trimmed();
    QVERIFY2(!raiz.isEmpty(), qPrintable(salida));
    QVERIFY2(!QDir::cleanPath(raiz).startsWith(QDir::cleanPath(docs)),
             qPrintable(QStringLiteral("los tests escribirían en %1").arg(raiz)));
}

void ProfilesTests::rutaDePerfiles_imprimeLaRaiz()
{
    // Caso auxiliar del anterior: corre en el subproceso, sin la variable de
    // entorno, y sólo informa dónde caerían los perfiles.
    QStandardPaths::setTestModeEnabled(true);
    printf("RAIZ=%s\n", qPrintable(ProfileManager::profilesRoot()));
    fflush(stdout);
}

void ProfilesTests::manager_exportImportBundle()
{
    ProfileManager pm;
    const QString backendId = pm.addBackend("bundle-backend", "bin-bundle",
                                            "127.0.0.1", 9191);
    QVERIFY(!backendId.isEmpty());
    const QString launchId = pm.addLaunchProfile("bundle-launch", backendId, "model-missing",
                                                 "runtime-missing");
    QVERIFY(!launchId.isEmpty());
    const QString json = pm.exportProfilesBundle();
    QVERIFY(json.contains(QStringLiteral("schemaVersion")));
    QVERIFY(json.contains(QStringLiteral("bundle-backend")));

    QVERIFY(pm.removeBackend(backendId));
    QVERIFY(pm.removeLaunchProfile(launchId));
    const int imported = pm.importProfilesBundle(json);
    QVERIFY(imported >= 2);
    QCOMPARE(pm.getBackend(backendId).value(QStringLiteral("name")).toString(),
             QStringLiteral("bundle-backend"));
    QVERIFY(pm.getLaunchProfile(launchId).value(QStringLiteral("name")).toString()
            .endsWith(QStringLiteral("_bundle-launch")));
    QCOMPARE(pm.importProfilesBundle(QStringLiteral("not-json")), -1);
    QCOMPARE(pm.importProfilesBundle(QStringLiteral("{\"schemaVersion\":99}")), -1);
}

void ProfilesTests::manager_profileChangeHistory()
{
    ProfileManager pm;
    const QString id = pm.addBackend(QStringLiteral("history-a"), QStringLiteral("bin"),
                                     QStringLiteral("127.0.0.1"), 9911);
    QVERIFY(!id.isEmpty());
    QVERIFY(pm.updateBackendPort(id, 9912));
    const QVariantList history = pm.profileChangeHistory(QStringLiteral("backend"), id, 10);
    QVERIFY(history.size() >= 2);
    QCOMPARE(history.first().toMap().value(QStringLiteral("id")).toString(), id);
    QVERIFY(history.first().toMap().value(QStringLiteral("snapshot")).toMap()
                .value(QStringLiteral("name")).toString().contains(QStringLiteral("history-a")));
}

QTEST_MAIN(ProfilesTests)
#include "test_profiles.moc"
