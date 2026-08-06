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
#include <QTemporaryDir>
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
    void masterConfig_jsonRoundTrip();
    void masterConfig_legacyMigration();

    void manager_addGetRemoveBackend();
    void manager_updateBackendPort();
    void manager_setBackendCloud();
    void manager_addModelProfile();
    void manager_favoriteAndAlias();
    void manager_browserAutomationOverride();
    void manager_persistsAcrossInstances();

    void migracion_copiaLosPerfilesSiElDestinoEstaVacio();
    void migracion_noPisaLoQueYaHayEnElDestino();
    void migracion_sinOrigenNoHaceNada();
    void rutaDePerfiles_respetaLaVariableDeEntorno();

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
    m.specType = "draft-mtp"; m.specDraftNMax = 3; m.specDraftNgl = "all";
    m.specDraftTypeK = "q8_0"; m.specDraftTypeV = "q8_0";
    const ModelProfile r = ModelProfile::fromJson(m.toJson());
    QCOMPARE(r.name, m.name);
    QCOMPARE(r.modelId, m.modelId);
    QCOMPARE(r.mmprojId, m.mmprojId);
    QCOMPARE(r.draftModelId, m.draftModelId);
    QCOMPARE(r.specType, m.specType);
    QCOMPARE(r.specDraftNMax, m.specDraftNMax);
    QCOMPARE(r.specDraftNgl, m.specDraftNgl);
    QCOMPARE(r.specDraftTypeK, m.specDraftTypeK);
    QCOMPARE(r.specDraftTypeV, m.specDraftTypeV);
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
    l.id = "l1"; l.name = "prod"; l.alias = "P"; l.favorite = true;
    l.backendProfileId = "b1"; l.modelProfileId = "m1"; l.runtimePresetId = "r1";
    l.extraArgs = QStringList{"--verbose"};
    MasterFallback mf; mf.type = "cli"; mf.cliName = "claude";
    l.master.fallbacks.append(mf);
    l.powerLimitW = 280;
    l.browserAutomation = "on";
    const LaunchProfile r = LaunchProfile::fromJson(l.toJson());
    QCOMPARE(r.browserAutomation, QStringLiteral("on"));
    QCOMPARE(r.name, l.name);
    QCOMPARE(r.alias, l.alias);
    QCOMPARE(r.favorite, l.favorite);
    QCOMPARE(r.backendProfileId, l.backendProfileId);
    QCOMPARE(r.modelProfileId, l.modelProfileId);
    QCOMPARE(r.extraArgs, l.extraArgs);
    QCOMPARE(r.master.fallbacks.size(), 1);
    QCOMPARE(r.master.fallbacks.first().type, QStringLiteral("cli"));
    QCOMPARE(r.master.fallbacks.first().cliName, QStringLiteral("claude"));
    QCOMPARE(r.powerLimitW, 280);
    // Default (campo ausente) → 0 = sin override.
    LaunchProfile empty;
    QCOMPARE(LaunchProfile::fromJson(empty.toJson()).powerLimitW, 0);
    // browserAutomation ausente → default "inherit".
    QCOMPARE(LaunchProfile::fromJson(empty.toJson()).browserAutomation,
             QStringLiteral("inherit"));
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

    // setModelSpec persiste la config MTP y getModelProfile la expone.
    QVERIFY(pm.setModelSpec(id, "draft-mtp", 3, "all", "q8_0", "q8_0"));
    const QVariantMap m = pm.getModelProfile(id);
    QCOMPARE(m.value("specType").toString(), QStringLiteral("draft-mtp"));
    QCOMPARE(m.value("specDraftNMax").toInt(), 3);
    QCOMPARE(m.value("specDraftTypeK").toString(), QStringLiteral("q8_0"));

    QVERIFY(pm.removeModelProfile(id));
}

void ProfilesTests::manager_favoriteAndAlias()
{
    ProfileManager pm;
    const QString id = pm.addLaunchProfile("L", "b", "m", "r");
    QVERIFY(!id.isEmpty());
    pm.setLaunchFavorite(id, true);
    pm.setLaunchAlias(id, "Alias");
    const QVariantList menu = pm.launchProfilesForMenu();
    QVERIFY(!menu.isEmpty());
    const QVariantMap top = menu.first().toMap();
    QCOMPARE(top.value("alias").toString(), QStringLiteral("Alias"));
    QVERIFY(top.value("favorite").toBool());
    // displayName antepone la estrella a los favoritos y muestra alias + nombre.
    QCOMPARE(top.value("displayName").toString(), QStringLiteral("★ Alias - 1_L"));
    QCOMPARE(pm.getLaunchProfile(id).value("displayName").toString(),
             QStringLiteral("Alias - 1_L"));
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

QTEST_MAIN(ProfilesTests)
#include "test_profiles.moc"
