// Tests de ProfileHealthChecker: núcleo puro checkLaunch(Refs) — detecta
// perfiles de lanzamiento rotos/degradados y explica severity + code + fix.
//
// Build: cmake -DBUILD_TESTS=ON ...   Run: ctest --test-dir build_tests.

#include <QtTest>
#include <QTemporaryDir>
#include "core/profiles/ProfileHealthChecker.h"
#include "core/profiles/ProfileManager.h"

class ProfileHealthTests : public QObject
{
    Q_OBJECT

private:
    // Launch local completo y sano (todas las refs resueltas + archivos presentes).
    static ProfileHealthChecker::Refs healthyLocal()
    {
        ProfileHealthChecker::Refs r;
        r.launch.id = "L1";
        r.launch.backendProfileId = "B1";
        r.launch.modelProfileId   = "M1";

        r.backendFound = true;
        r.backend.id = "B1";
        r.backend.kind = "local";
        r.backend.binaryId = "BIN1";

        r.binaryFound = true;
        r.binary.id = "BIN1";
        r.binary.pathValid = true;

        r.modelRefFound = true;
        r.model.id = "M1";
        r.model.modelId = "gguf-1";
        r.modelFileExists = true;
        return r;
    }

    static bool hasCode(const QList<HealthIssue> &v, const QString &code)
    {
        for (const auto &i : v) if (i.code == code) return true;
        return false;
    }
    static QString severityOf(const QList<HealthIssue> &v, const QString &code)
    {
        for (const auto &i : v) if (i.code == code) return i.severity;
        return {};
    }

private:
    QTemporaryDir m_dir;

private slots:
    void initTestCase();
    void checkAllResolvesMissingBackend();
    void healthyLocalIsClean();
    void backendUnset();
    void backendMissing();
    void binaryMissing();
    void resolvedSystemBinaryIsAccepted();
    void binaryPathInvalid();
    void modelFileMissing();
    void mmprojMissingIsWarning();
    void specWithoutDraft();
    void adaptiveCapabilityUnknownIsWarning();
    void adaptiveCapabilityMissingIsError();
    void adaptiveCapabilityAvailableIsClean();
    void adaptiveRawLaunchArgIsValidated();
    void runtimeAndAgentMissingAreWarnings();
    void cloudSkipsBinaryAndModel();
    void cloudMissingUrlIsError();
    void hybridPlannerValidation();
};

void ProfileHealthTests::initTestCase()
{
    QVERIFY(m_dir.isValid());
    qputenv("LLAMACODE_PROFILES_DIR", m_dir.path().toLocal8Bit());
}

// Integración: resolver checkAll contra un ProfileManager real. Un launch que
// apunta a un backend inexistente debe producir backend-missing. binaries/catalog
// nulos: checkAll debe tolerarlos.
void ProfileHealthTests::checkAllResolvesMissingBackend()
{
    ProfileManager pm;
    const QString lid = pm.addLaunchProfile("Roto", "no-existe-backend", "", "");
    QVERIFY(!lid.isEmpty());

    const auto issues = ProfileHealthChecker::checkAll(&pm, nullptr, nullptr);
    bool found = false;
    for (const auto &i : issues)
        if (i.launchId == lid && i.code == "backend-missing") found = true;
    QVERIFY(found);
}

void ProfileHealthTests::healthyLocalIsClean()
{
    QVERIFY(ProfileHealthChecker::checkLaunch(healthyLocal()).isEmpty());
}

void ProfileHealthTests::backendUnset()
{
    auto r = healthyLocal();
    r.launch.backendProfileId.clear();
    r.backendFound = false;
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QVERIFY(hasCode(v, "backend-unset"));
    QCOMPARE(severityOf(v, "backend-unset"), QStringLiteral("error"));
}

void ProfileHealthTests::backendMissing()
{
    auto r = healthyLocal();
    r.backendFound = false;
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QVERIFY(hasCode(v, "backend-missing"));
    QCOMPARE(severityOf(v, "backend-missing"), QStringLiteral("error"));
}

void ProfileHealthTests::binaryMissing()
{
    auto r = healthyLocal();
    r.binaryFound = false;
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QVERIFY(hasCode(v, "binary-missing"));
}

void ProfileHealthTests::resolvedSystemBinaryIsAccepted()
{
    auto r = healthyLocal();
    r.launch.system = true;
    r.backend.binaryId.clear();
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QVERIFY(!hasCode(v, "binary-unset"));
    QVERIFY(!hasCode(v, "binary-missing"));
}

void ProfileHealthTests::binaryPathInvalid()
{
    auto r = healthyLocal();
    r.binary.pathValid = false;
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QVERIFY(hasCode(v, "binary-path-invalid"));
    QCOMPARE(severityOf(v, "binary-path-invalid"), QStringLiteral("error"));
}

void ProfileHealthTests::modelFileMissing()
{
    auto r = healthyLocal();
    r.modelFileExists = false;
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QVERIFY(hasCode(v, "model-file-missing"));
    QCOMPARE(severityOf(v, "model-file-missing"), QStringLiteral("error"));
}

void ProfileHealthTests::mmprojMissingIsWarning()
{
    auto r = healthyLocal();
    r.model.mmprojId = "mmproj-1";
    r.mmprojFileExists = false;
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QVERIFY(hasCode(v, "mmproj-file-missing"));
    QCOMPARE(severityOf(v, "mmproj-file-missing"), QStringLiteral("warning"));
}

void ProfileHealthTests::specWithoutDraft()
{
    auto r = healthyLocal();
    r.model.specType = "draft-mtp";
    r.model.draftModelId.clear();
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QVERIFY(hasCode(v, "spec-without-draft"));
    QCOMPARE(severityOf(v, "spec-without-draft"), QStringLiteral("warning"));
}

void ProfileHealthTests::adaptiveCapabilityUnknownIsWarning()
{
    auto r = healthyLocal();
    r.model.specType = "draft-mtp";
    r.model.specDraftAdaptive = true;
    // supportedFlags vacío significa que todavía no se ejecutó --help.
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QCOMPARE(severityOf(v, "adaptive-capability-unknown"), QStringLiteral("warning"));
    QVERIFY(!hasCode(v, "adaptive-capability-missing"));
}

void ProfileHealthTests::adaptiveCapabilityMissingIsError()
{
    auto r = healthyLocal();
    r.model.specType = "draft-mtp";
    r.model.specDraftAdaptive = true;
    r.binary.supportedFlags = {"--spec-type", "--spec-draft-n-max"};
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QCOMPARE(severityOf(v, "adaptive-capability-missing"), QStringLiteral("error"));
}

void ProfileHealthTests::adaptiveCapabilityAvailableIsClean()
{
    auto r = healthyLocal();
    r.model.specType = "draft-mtp";
    r.model.specDraftAdaptive = true;
    r.binary.supportedFlags = {"--spec-type", "--spec-draft-n-max",
                               "--spec-draft-adaptive", "--spec-draft-n-min"};
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QVERIFY(!hasCode(v, "adaptive-capability-unknown"));
    QVERIFY(!hasCode(v, "adaptive-capability-missing"));
}

void ProfileHealthTests::adaptiveRawLaunchArgIsValidated()
{
    auto r = healthyLocal();
    r.launch.extraArgs = {QStringLiteral("--spec-type"), QStringLiteral("draft-mtp"),
                          QStringLiteral("--spec-draft-adaptive")};
    r.binary.supportedFlags = {QStringLiteral("--spec-type"),
                               QStringLiteral("--spec-draft-n-max")};
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QCOMPARE(severityOf(v, "adaptive-capability-missing"), QStringLiteral("error"));
}

void ProfileHealthTests::runtimeAndAgentMissingAreWarnings()
{
    auto r = healthyLocal();
    r.launch.runtimePresetId = "RT1";
    r.runtimeFound = false;
    r.launch.agentProfileId = "AG1";
    r.agentRefFound = false;
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QCOMPARE(severityOf(v, "runtime-missing"), QStringLiteral("warning"));
    QCOMPARE(severityOf(v, "agent-missing"), QStringLiteral("warning"));
}

void ProfileHealthTests::cloudSkipsBinaryAndModel()
{
    ProfileHealthChecker::Refs r;
    r.launch.id = "L1";
    r.launch.backendProfileId = "B1";
    // sin modelo ni binario: al ser cloud, no debe reclamarlos.
    r.backendFound = true;
    r.backend.id = "B1";
    r.backend.kind = "cloud";
    r.backend.cloudBaseUrl = "https://api.openai.com";
    r.backend.cloudKeyRef = "OPENAI_KEY";
    r.backend.cloudModel = "gpt-4o";
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QVERIFY(!hasCode(v, "model-unset"));
    QVERIFY(!hasCode(v, "binary-unset"));
    QVERIFY(v.isEmpty());
}

void ProfileHealthTests::cloudMissingUrlIsError()
{
    ProfileHealthChecker::Refs r;
    r.launch.id = "L1";
    r.launch.backendProfileId = "B1";
    r.backendFound = true;
    r.backend.id = "B1";
    r.backend.kind = "cloud";
    // sin url/key/model
    const auto v = ProfileHealthChecker::checkLaunch(r);
    QCOMPARE(severityOf(v, "cloud-url-missing"), QStringLiteral("error"));
    QVERIFY(hasCode(v, "cloud-key-unset"));
    QVERIFY(hasCode(v, "cloud-model-unset"));
}

void ProfileHealthTests::hybridPlannerValidation()
{
    ProfileManager pm;
    const QString executor = pm.addLaunchProfile("Executor", "", "", "");
    QVERIFY(pm.updateLaunchProfile(QVariantMap{{"id", executor},
                                               {"hybridMode", "sequential"},
                                               {"plannerProfileId", "missing"}}));
    auto issues = ProfileHealthChecker::checkAll(&pm, nullptr, nullptr);
    QVERIFY(hasCode(issues, "hybrid-planner-missing"));

    QVERIFY(pm.updateLaunchProfile(QVariantMap{{"id", executor},
                                               {"plannerProfileId", executor}}));
    issues = ProfileHealthChecker::checkAll(&pm, nullptr, nullptr);
    QVERIFY(hasCode(issues, "hybrid-self-reference"));
}

QTEST_MAIN(ProfileHealthTests)
#include "test_profile_health.moc"
