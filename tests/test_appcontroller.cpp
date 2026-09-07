// Tests de las variantes headless (sin diálogo) de AppController: export/import
// de datos de usuario y export de sesión de chat a ruta explícita. Garantizan
// que toda feature con diálogo tenga un camino api/headless equivalente.

#include <QtTest>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QTimer>
#include <QSignalSpy>
#include <QSet>
#include <QSettings>
#include "AppController.h"
#include "core/agent/BrowserTeach.h"
#include "core/agent/IAgentBackend.h"
#include "core/agent/LlamaAgentBackend.h"
#include "core/profiles/HarnessSpec.h"
#include "core/profiles/ProfileManager.h"
#include "core/profiles/ProfileTypes.h"
#include "core/tasks/TaskStore.h"
#include "core/automation/AutomationRunner.h"
#include "core/CatalogModel.h"
#include "core/ModelCatalog.h"
#include "core/ModelRootRegistry.h"
#include "core/OllamaImporter.h"
#include <QJsonArray>

static QString systemProfilesBundlePath()
{
    const QStringList candidates = {
        QDir::current().absoluteFilePath(QStringLiteral("assets/system_profiles.json")),
        QDir::current().absoluteFilePath(QStringLiteral("../assets/system_profiles.json")),
        QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(QStringLiteral("../../assets/system_profiles.json")),
    };
    for (const QString &candidate : candidates)
        if (QFile::exists(candidate))
            return candidate;
    return candidates.first();
}

// Backend de agente fake para ejercitar el ciclo del bucle de Tasks sin un
// llama-server real. Cada sendMessage responde un texto scripteado y, async,
// emite messagesChanged + turnFinished (async para no recursar dentro de
// sendToAgent). El goal-check se detecta por el prompt; devuelve veredictos en
// secuencia (GOAL_NOT_MET → otra iteración; GOAL_MET → corta).
class FakeAgentBackend : public IAgentBackend
{
    Q_OBJECT
public:
    explicit FakeAgentBackend(QObject *parent = nullptr) : IAgentBackend(parent) {}

    QString adapter() const override { return QStringLiteral("llamaagent"); }
    bool running() const override { return m_running; }
    void start(const AgentContext &) override { m_running = true; emit runningChanged(); }
    void stop() override { m_running = false; emit runningChanged(); }
    void newSession() override { m_msgs.clear(); }   // sesión limpia por iteración
    QVariantList messages() const override { return m_msgs; }

    void sendMessage(const QString &text) override
    {
        QString reply;
        if (text.contains(QStringLiteral("objetivo del bucle"))) {
            reply = m_verdicts.isEmpty() ? QStringLiteral("GOAL_MET") : m_verdicts.takeFirst();
        } else {
            ++m_bodyRuns;
            m_lastBodyPrompt = text;
            reply = m_bodyReplies.isEmpty() ? QStringLiteral("trabajo realizado %1").arg(m_bodyRuns)
                                            : m_bodyReplies.takeFirst();
        }
        m_msgs.append(QVariantMap{{QStringLiteral("role"), QStringLiteral("assistant")},
                                  {QStringLiteral("content"), reply}});
        emit messagesChanged();
        QTimer::singleShot(m_replyDelayMs, this, [this]() { emit turnFinished(); });
    }

    // Registro de lo que el harness le baja al backend por la interfaz comun.
    // Sin esto una fase podia ser un no-op y el test no se enteraba.
    void setApprovalPolicy(const QString &mode) override
    {
        m_approvalModes.append(mode);
        m_approvalAtBodyRun.insert(m_bodyRuns, mode);
    }
    void setPermissionRules(const QString &rules) override { m_permRules = rules; }
    void setAgentTuning(const QString &systemExtra, double temperature) override
    {
        m_systemExtra = systemExtra;
        m_temperature = temperature;
        m_tuningCalls++;
    }
    QStringList approvalModes() const { return m_approvalModes; }
    QString approvalAtBodyRun(int run) const { return m_approvalAtBodyRun.value(run); }
    QString permissionRules() const { return m_permRules; }
    QString systemExtra() const { return m_systemExtra; }
    double temperature() const { return m_temperature; }
    int tuningCalls() const { return m_tuningCalls; }

    void setVerdicts(const QStringList &v) { m_verdicts = v; }
    void setBodyReplies(const QStringList &r) { m_bodyReplies = r; }
    void setReplyDelayMs(int ms) { m_replyDelayMs = qMax(0, ms); }
    int bodyRuns() const { return m_bodyRuns; }
    QString lastBodyPrompt() const { return m_lastBodyPrompt; }

private:
    bool m_running = false;
    int m_bodyRuns = 0;
    QStringList m_verdicts;
    QStringList m_bodyReplies;
    QString m_lastBodyPrompt;
    int m_replyDelayMs = 0;
    QVariantList m_msgs;
    QStringList m_approvalModes;
    QHash<int, QString> m_approvalAtBodyRun;
    QString m_permRules;
    QString m_systemExtra;
    double m_temperature = -1.0;
    int m_tuningCalls = 0;
};

class AppControllerTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void exportUserDataToWritesBackup();
    void githubReleaseIsConvertedToUpdateFlag();
    void githubPrereleaseIsIgnored();
    void installRootIsDerivedFromExeLocation();
    void importUserDataFromRoundTrips();
    void exportUserDataToEmptyPathErrors();
    void exportChatSessionToMissingSessionErrors();
    void parseGpuPowerCsvParses();
    void parseGpuPowerCsvTolerant();
    void parseGpuInventoryCsvParses();
    void researchReportGuardrailsRejectKnownErrors();
    void researchReportGuardrailsAcceptCorrectedClaims();
    void ggufRecommendationCandidateFilter();
    void modelRecommendationsUseResolvableGgufNames();
    void createRecommendedLaunchProfileBuildsProfile();
    void createRecommendedLaunchProfileReusesExistingMenuProfile();
    void preferredAgentLaunchSelection();
    void browserMcpEffectiveResolves();
    void integrationSecretsMigrateOutOfJson();
    void pendingAgentClearsStartingWhenAlreadyRunning();
    void benchmarkStopStepKillsWhenBudgetRunsOut();
    void benchmarkRestartErrorsAreInfrastructure();
    void benchmarkBusyTurnIsRetryableNotInfrastructure();
    void benchmarkRepairPrefillIsNotStagnation();
    void benchmarkMemoryPressureIsRecognized();
    void benchmarkUsesMaxVramBeforeReducingPressure();
    void benchmarkPreservesScoreAfterTransportTail();
    void benchmarkGateRejectsBrokenOrStaleHe0();
    void benchmarkGateAcceptsValidHe20QualityResult();
    void benchmarkCoverageClassifiesStages();
    void benchmarkCustomStageClassification();
    void benchmarkRankingAggregatesLatestStages();
    void benchmarkDocumentRowsParseScores();
    void benchmarkUsesOneArtifactPerTask();
    void benchmarkStreamingCountsSnapshotsOnce();
    void concurrencyBenchmarkSettingsClampBounds();
    void benchmarkReusesServerAlreadyLoadedWithSameProfile();
    void benchmarkBest25ClassifiesExclusiveSpeedTiers();
    void benchmarkBest25IncludesValidRowsWithoutTps();
    void benchmarkBestModelosSpeedCapsProfilesPerGguf();
    void benchmarkHumanEval20CandidatesUseTopThreeAndBestControls();
    void benchmarkBestModelosQualityUsesTwentyItemResults();
    void benchmarkScoresChatAnswersWhenAgentWritesNoFiles();
    void benchmarkResumesWhereItDiedInsteadOfLosingTheSeries();
    void benchmarkWorkspaceFingerprintIgnoresInternalAgentFiles();
    void benchmarkEvaluatorsToleratePresentationNotContent();
    void hybridExecutionPromptPreservesRequestAndPlan();
    void hybridVisibleMessagePreservesOnlyOriginalRequest();
    void hybridStreamParserDetectsProgressAndCompletion();
    void hybridStructuredPlanValidatesAndRejectsUnsafeShape();
    void hybridPlanNormalizesScalarLists();
    void hybridStatusUsesSelectedPlannerAndExecutorNames();
    void hybridSwapRemainsStartingUntilPipelineEnds();
    void voiceWhisperServerAvailabilityUsesConfiguredPath();
    void legacyVoiceConfigDefaultsToManagedPiper();
    void hardwareRecommendationIsHeadless();
    void voiceGpuPlanIsExposedHeadless();
    void performanceMatrixIsHeadless();
    void devModeIsOptInAndRecordsPerformance();
    void browserTeachSkillsLifecycle();
    void taskFailureTextDetected();
    void taskRequiresToolEvidenceForWebObjective();
    void deterministicReplayCountsAsToolEvidence();
    void visualComparisonIsRecognizedSeparately();
    void readResearchReportPrependsLegacyTopic();
    void researchReportsExposeFormattedDate();
    void exportWorkspaceIncludesChatsAndResearch();
    void autoStartAgentOnLaunchPersists();
    void remoteBackendEnablesServerDependentUi();
    void windowsStartupCommandQuotesExecutable();
    void startupHiddenRequiresBothFlags();
    void loopTaskRunsBodyUntilGoalMet();
    void loopTaskAppliesVerifyPhaseAndRestoresIt();
    void profileWithoutPhasesDoesNotReapplyHarness();
    void loopTaskStopsAtMaxIterations();
    void loopTaskStopsAtMaxSeconds();
    void dataDrivenTaskRunsBodyPerRow();
    void taskRetriesBodyOnFailure();
    void datasetAbortStopsOnError();
    void fileWatchTriggerRegistersPath();
    void taskRunTimelineNormalizesToolCards();
    void earlyFailureRecordedInHistory();
    void workflowTaskPausesApprovesAndPersistsSnapshot();
    void workflowDirectToolCompletesWithoutModelTurn();
    void workflowValidationIsAvailableToVisualEditor();
    void engineeringCatalogIsExposedHeadless();
    void engineeringPresetInstallsPersistsAndRestores();
    void harnessAdapterNormalizesToLlamaAgent();
    void systemProfileBinaryPinReadsBundle();
    void systemProfileMinimumBuildSelectsNewestCompatible();
    void cpuSystemProfileRequiresCpuBinary();
    void charlaTranscriptRoutesToAgentWhenRunning();
    void charlaCursorOcrIsOptInAndDoesNotHijackChat();
    void ocrStatusAlwaysExplainsItself();
    void agentLevels_contextBudgetLadder();
    void doctorReportsStructureAndIssues();
    void importOllamaModelsIngestsStore();
    void bundledCustomBenchmarkUpgradePreservesPersonalFiles();
    void bundledOneShottingBenchmarksAreSeparate();
    void bundledHarnessContextAbBenchmarkIsValid();
    void bundledArtifactLifecycleBenchmarkIsValid();
    void importedBenchmarkNamesDescribeSubset();
    void tunerProfileNameUsesOptiPrefixWithoutChaining();
    void tunerGainPctNeedsBothLegs();
    void isRemoteHostDetectsLanHosts();
    void isLoopbackCloudUrlRequiresLocalHttpEndpoint();

private:
    QTemporaryDir m_tmp;
    QString makeLoopTask(AppController &app, const QString &name, int maxIter);
    // Perfil de agente con override por FASE, activo en `app`. Devuelve su id.
    QString makePhasedAgentProfile(AppController &app, const QJsonObject &phases);
};

void AppControllerTests::githubReleaseIsConvertedToUpdateFlag()
{
    const QJsonObject release{
        {QStringLiteral("tag_name"), QStringLiteral("v0.2.0")},
        {QStringLiteral("name"), QStringLiteral("LlamaCode 0.2.0")},
        {QStringLiteral("html_url"), QStringLiteral("https://example.test/release")},
        {QStringLiteral("body"), QStringLiteral("Mejoras generales\n- Inicio más rápido\n- Corrección importante")},
        {QStringLiteral("draft"), false},
        {QStringLiteral("prerelease"), false},
    };

    const QJsonObject flag = AppController::githubReleaseToUpdateFlag(release);
    QCOMPARE(flag.value(QStringLiteral("version")).toString(), QStringLiteral("0.2.0"));
    QCOMPARE(flag.value(QStringLiteral("newVersion")).toBool(), true);
    QCOMPARE(flag.value(QStringLiteral("releaseUrl")).toString(),
             QStringLiteral("https://example.test/release"));
    QCOMPARE(flag.value(QStringLiteral("changelog")).toArray().size(), 3);
}

// "Actualizar ahora" le pasa esta ruta al bootstrap via LC_DIR. Si sale vacia,
// el script clona en %USERPROFILE%\LlamaCode y actualiza OTRA copia: la app se
// cierra (el bootstrap la mata) y la instalacion real queda igual.
void AppControllerTests::installRootIsDerivedFromExeLocation()
{
    QTemporaryDir checkout;
    QVERIFY(checkout.isValid());
    const QDir root(checkout.path());
    QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("scripts"))));
    QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("build/Release"))));

    auto touch = [](const QString &path) {
        QFile f(path);
        return f.open(QIODevice::WriteOnly) ? (f.write("x") > 0) : false;
    };
    QVERIFY(touch(root.filePath(QStringLiteral("CMakeLists.txt"))));
    QVERIFY(touch(root.filePath(QStringLiteral("scripts/bootstrap.ps1"))));

    const QString exe = root.filePath(QStringLiteral("build/Release/LlamaCode.exe"));
    QCOMPARE(AppController::installRootForExePath(exe),
             QDir::toNativeSeparators(root.absolutePath()));

    // Un exe suelto (sin checkout arriba) no inventa una raiz: el bootstrap
    // usa su default en vez de resetear un directorio cualquiera.
    QTemporaryDir loose;
    QVERIFY(loose.isValid());
    QVERIFY(AppController::installRootForExePath(
                QDir(loose.path()).filePath(QStringLiteral("LlamaCode.exe"))).isEmpty());
}

void AppControllerTests::githubPrereleaseIsIgnored()
{
    const QJsonObject release{
        {QStringLiteral("tag_name"), QStringLiteral("v9.0.0-beta")},
        {QStringLiteral("prerelease"), true},
    };
    QVERIFY(AppController::githubReleaseToUpdateFlag(release).isEmpty());
}

void AppControllerTests::voiceWhisperServerAvailabilityUsesConfiguredPath()
{
    AppController app;
    const QString missing = QDir::temp().filePath(QStringLiteral("missing-whisper-server.exe"));
    QFile::remove(missing);
    app.setVoiceWhisperServerPath(missing);
    QVERIFY(!app.voiceWhisperServerAvailable());

    QTemporaryFile executable(QDir::temp().filePath(QStringLiteral("whisper-server-XXXXXX.exe")));
    QVERIFY(executable.open());
    app.setVoiceWhisperServerPath(executable.fileName());
    QVERIFY(app.voiceWhisperServerAvailable());
    app.setVoiceWhisperServerPath(QString());
}

void AppControllerTests::legacyVoiceConfigDefaultsToManagedPiper()
{
    AppController app;
    const QVariantMap cfg = app.voiceConfig(QStringLiteral("missing-profile"));
    QCOMPARE(cfg.value(QStringLiteral("ttsMode")).toString(), QStringLiteral("auto"));
    QCOMPARE(cfg.value(QStringLiteral("ttsManagedVoice")).toString(),
             QStringLiteral("es_ES-davefx-medium"));
}

QString AppControllerTests::makeLoopTask(AppController &app, const QString &name, int maxIter)
{
    // Task local (sin keywords web → no exige evidencia de tool), en bucle, sin
    // postprompt ni verifyProfile (sin swap de modelo).
    const QVariantMap def{
        {QStringLiteral("name"), name},
        {QStringLiteral("description"), QStringLiteral("Escribí un resumen del texto dado")},
        {QStringLiteral("executionMode"), QStringLiteral("auto")},
        {QStringLiteral("loopEnabled"), true},
        {QStringLiteral("loopGoal"), QStringLiteral("el resumen quedó completo")},
        {QStringLiteral("loopMaxIterations"), maxIter}
    };
    return app.taskStore()->save(QString(), def);
}

QString AppControllerTests::makePhasedAgentProfile(AppController &app,
                                                  const QJsonObject &phases)
{
    ProfileManager *pm = app.profileManager();
    const QString id = pm->addAgentProfile(QStringLiteral("Con fases"));
    HarnessSpec spec;
    spec.permissions.set = true;
    spec.permissions.approvalMode = QStringLiteral("auto");
    for (auto it = phases.constBegin(); it != phases.constEnd(); ++it)
        spec.phases.insert(it.key(), it.value().toObject());
    pm->setAgentProfileSpec(id, spec.toJson().toVariantMap());
    app.setActiveAgentProfileId(id);
    return id;
}

// Las fases del HarnessSpec no valen nada si el runner no las aplica en el
// momento correcto. Esto es lo que faltaba: forPhase estaba testeada pura, el
// cableado no. El test tiene que FALLAR si se borra applyHarnessPhase del runner.
void AppControllerTests::loopTaskAppliesVerifyPhaseAndRestoresIt()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});
    fake->setVerdicts({QStringLiteral("GOAL_NOT_MET falta"), QStringLiteral("GOAL_MET listo")});
    app.setTestAgentBackend(fake);

    // Fase de verificación en modo plan (sólo lectura) sobre un perfil que en
    // ejecución aprueba todo: si la fase se aplica, el modo cambia y vuelve.
    QJsonObject verifyPatch;
    verifyPatch[QStringLiteral("permissions")] =
        QJsonObject{{QStringLiteral("approvalMode"), QStringLiteral("plan")}};
    QJsonObject phases;
    phases[QStringLiteral("verify")] = verifyPatch;
    makePhasedAgentProfile(app, phases);

    const QString id = makeLoopTask(app, QStringLiteral("Loop con fases"), 5);
    QSignalSpy fin(&app, &AppController::taskRunFinished);
    app.runTaskBodyForTest(id);
    QTRY_VERIFY_WITH_TIMEOUT(!fin.isEmpty(), 5000);

    const QStringList modes = fake->approvalModes();
    QVERIFY2(modes.contains(QStringLiteral("plan")),
             qPrintable(QStringLiteral("la fase verify debe bajar approvalMode=plan; se vio: ")
                        + modes.join(QLatin1Char(','))));
    // Y la iteración siguiente vuelve al modo del spec base: una fase es una
    // vista temporal, no una mutación del perfil. Se mira desde el PRIMER plan:
    // el último es el goal-check final, después del cual la Task ya terminó.
    const int planAt = modes.indexOf(QStringLiteral("plan"));
    QVERIFY2(modes.mid(planAt + 1).contains(QStringLiteral("auto")),
             qPrintable(QStringLiteral("tras verificar, el modo base tiene que volver; se vio: ")
                        + modes.join(QLatin1Char(','))));
}

// El contrapeso del test anterior: un perfil SIN fases no debe generar ninguna
// llamada extra. Un no-op de palabra (que igual reaplica todo) ensuciaría el
// prompt-cache en cada verificación.
void AppControllerTests::profileWithoutPhasesDoesNotReapplyHarness()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});
    fake->setVerdicts({QStringLiteral("GOAL_MET listo")});
    app.setTestAgentBackend(fake);

    ProfileManager *pm = app.profileManager();
    const QString id = pm->addAgentProfile(QStringLiteral("Sin fases"));
    HarnessSpec spec;
    spec.permissions.set = true;
    spec.permissions.approvalMode = QStringLiteral("ask");
    pm->setAgentProfileSpec(id, spec.toJson().toVariantMap());
    app.setActiveAgentProfileId(id);

    const int tuningBefore = fake->tuningCalls();
    const QString task = makeLoopTask(app, QStringLiteral("Loop sin fases"), 3);
    QSignalSpy fin(&app, &AppController::taskRunFinished);
    app.runTaskBodyForTest(task);
    QTRY_VERIFY_WITH_TIMEOUT(!fin.isEmpty(), 5000);

    QCOMPARE(fake->tuningCalls(), tuningBefore);
    QVERIFY2(!fake->approvalModes().contains(QStringLiteral("plan")),
             "sin fases declaradas no hay cambio de modo");
}

void AppControllerTests::hybridExecutionPromptPreservesRequestAndPlan()
{
    const QString prompt = AppController::composeHybridExecutionPromptForTest(
        QStringLiteral("  corregí el bug  "), QStringLiteral("  1. leer\n2. probar  "));
    QVERIFY(prompt.contains(QStringLiteral("REQUEST ORIGINAL:\ncorregí el bug")));
    QVERIFY(prompt.contains(QStringLiteral("1. leer\n2. probar")));
    QVERIFY(prompt.contains(QStringLiteral("verificá el resultado")));
}

void AppControllerTests::hybridVisibleMessagePreservesOnlyOriginalRequest()
{
    const QString original = QStringLiteral(
        "revisá la documentación y actualizala toda a fondo; sólo documentación");
    const QString internal = AppController::composeHybridExecutionPromptForTest(
        original, QStringLiteral("1. leer README\n2. crear RECOMMENDATIONS.md"));

    QCOMPARE(LlamaAgentBackend::visibleUserTextForTest(internal, original), original);
    QVERIFY(!LlamaAgentBackend::visibleUserTextForTest(internal, original)
                 .contains(QStringLiteral("PLAN DEL MODELO")));
    // Los envíos normales mantienen el comportamiento anterior.
    QCOMPARE(LlamaAgentBackend::visibleUserTextForTest(original, QString()), original);
}

void AppControllerTests::hybridStreamParserDetectsProgressAndCompletion()
{
    bool done = false;
    QCOMPARE(AppController::parseHybridStreamLineForTest(
                 QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"Paso 1\"}}]}"), &done),
             QStringLiteral("Paso 1"));
    QVERIFY(!done);
    QCOMPARE(AppController::parseHybridStreamLineForTest(QByteArrayLiteral("data: [DONE]"), &done),
             QString());
    QVERIFY(done);
}

void AppControllerTests::hybridStructuredPlanValidatesAndRejectsUnsafeShape()
{
    const QString valid = QStringLiteral(R"(```json
{"schemaVersion":1,"goal":"arreglar","understanding":"bug","assumptions":[],
 "files":["src/a.cpp"],"steps":["inspeccionar","editar"],"tests":["ctest"],
 "risks":["regresión"],"doneWhen":["tests pasan"]}
```)" );
    QString error;
    const QVariantMap plan = AppController::parseHybridPlanForTest(valid, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(plan.value("schemaVersion").toInt(), 1);
    QCOMPARE(plan.value("goal").toString(), QStringLiteral("arreglar"));
    const QString prompt = AppController::composeHybridExecutionPromptForTest(
        QStringLiteral("pedido"), QString::fromUtf8(QJsonDocument::fromVariant(plan).toJson()));
    QVERIFY(prompt.contains(QStringLiteral("PLAN ESTRUCTURADO")));
    QVERIFY(prompt.contains(QStringLiteral("pedido")));

    const QVariantMap invalid = AppController::parseHybridPlanForTest(
        QStringLiteral(R"({"schemaVersion":1,"goal":"x","steps":[],"tests":[],"risks":[],"doneWhen":[]})"),
        &error);
    QVERIFY(invalid.isEmpty());
    QVERIFY(!error.isEmpty());
}

// El planificador suele mandar doneWhen/steps como texto o con viñetas: eso no
// puede tumbar el request (antes daba "doneWhen debe ser un array").
void AppControllerTests::hybridPlanNormalizesScalarLists()
{
    QString error;
    const QVariantMap plan = AppController::parseHybridPlanForTest(
        QStringLiteral(R"({"schemaVersion":"1","goal":"documentar",)"
                       R"("steps":"- leer README\n2) actualizar docs",)"
                       R"("doneWhen":"docs actualizadas","risks":null})"),
        &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(plan.value("schemaVersion").toInt(), 1);
    QCOMPARE(plan.value("steps").toStringList(),
             QStringList({QStringLiteral("leer README"), QStringLiteral("actualizar docs")}));
    QCOMPARE(plan.value("doneWhen").toStringList(),
             QStringList({QStringLiteral("docs actualizadas")}));
    QVERIFY(plan.value("risks").toList().isEmpty());
    QVERIFY(plan.contains("tests"));
}

void AppControllerTests::hybridSwapRemainsStartingUntilPipelineEnds()
{
    AppController app;
    QSignalSpy startingChanged(&app, &AppController::agentStartingChanged);

    QVERIFY(!app.agentStarting());
    app.setHybridPhaseForTest(QStringLiteral("preparing"));
    QVERIFY(app.agentStarting());
    QCOMPARE(startingChanged.count(), 1);

    // Las fases internas no deben crear un instante observable como detenido.
    app.setHybridPhaseForTest(QStringLiteral("stopping-executor"));
    app.setHybridPhaseForTest(QStringLiteral("planning"));
    app.setHybridPhaseForTest(QStringLiteral("executor-start"));
    QVERIFY(app.agentStarting());
    QCOMPARE(startingChanged.count(), 1);

    app.setHybridPhaseForTest(QString());
    QVERIFY(!app.agentStarting());
    QCOMPARE(startingChanged.count(), 2);
}

void AppControllerTests::hybridStatusUsesSelectedPlannerAndExecutorNames()
{
    const QString planner = QStringLiteral("MAX-Q planner");
    const QString executor = QStringLiteral("KAT-Coder executor");
    QCOMPARE(AppController::hybridStatusTextForTest(QStringLiteral("preparing"), planner, executor),
             QStringLiteral("Preparando contexto para MAX-Q planner…"));
    QCOMPARE(AppController::hybridStatusTextForTest(QStringLiteral("planning"), planner, executor),
             QStringLiteral("MAX-Q planner está planificando…"));
    QCOMPARE(AppController::hybridStatusTextForTest(QStringLiteral("executor-start"), planner, executor),
             QStringLiteral("Restaurando KAT-Coder executor…"));
    QCOMPARE(AppController::hybridStatusTextForTest(QStringLiteral("dispatching"), planner, executor),
             QStringLiteral("Entregando el plan validado a KAT-Coder executor…"));
}

void AppControllerTests::initTestCase()
{
    // Aísla AppData/AppLocalData a una ubicación de test.
    QStandardPaths::setTestModeEnabled(true);
    // QTEST_MAIN no setea org/app name; sin ellos QSettings no persiste. Igualamos
    // a lo que usa la app (main.cpp) para que el round-trip de settings funcione.
    QCoreApplication::setOrganizationName(QStringLiteral("LlamaCode"));
    QCoreApplication::setApplicationName(QStringLiteral("LlamaCode"));
    QVERIFY(m_tmp.isValid());
    // Y los perfiles, que viven en Documentos y NO los redirige el modo test.
    // Sin esto, createRecommendedLaunchProfileBuildsProfile() creaba un perfil
    // real por cada corrida de la suite en la máquina de quien la ejecuta.
    qputenv("LLAMACODE_PROFILES_DIR", m_tmp.filePath(QStringLiteral("profiles")).toLocal8Bit());
    qputenv("LLAMACODE_RUN_HISTORY_DIR",
            QFile::encodeName(QDir(m_tmp.path()).filePath(QStringLiteral("run_history"))));
}

void AppControllerTests::bundledCustomBenchmarkUpgradePreservesPersonalFiles()
{
    const QJsonObject oldBundled{
        {QStringLiteral("id"), QStringLiteral("stress-largo-dificil-python-v1")},
        {QStringLiteral("bundledVersion"), 1},
        {QStringLiteral("name"), QStringLiteral("copia vieja")}
    };
    const QJsonObject newBundled{
        {QStringLiteral("id"), QStringLiteral("stress-largo-dificil-python-v1")},
        {QStringLiteral("bundledVersion"), 2}
    };
    const QJsonObject personal{
        {QStringLiteral("id"), QStringLiteral("personal")},
        {QStringLiteral("bundledVersion"), 0}
    };

    QVERIFY(AppController::shouldReplaceBundledBenchmarkForTest(newBundled, oldBundled));
    QVERIFY(!AppController::shouldReplaceBundledBenchmarkForTest(oldBundled, newBundled));
    QVERIFY(!AppController::shouldReplaceBundledBenchmarkForTest(newBundled, personal));
    QVERIFY(!AppController::shouldReplaceBundledBenchmarkForTest(personal, oldBundled));
}

void AppControllerTests::bundledOneShottingBenchmarksAreSeparate()
{
    const QString root = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("../../assets/benchmarks/custom"));
    const QStringList names = {
        QStringLiteral("one_shotting_bakery_ecommerce_v1.json"),
        QStringLiteral("one_shotting_space_shooter_v1.json")
    };
    for (const QString &name : names) {
        QFile f(QDir(root).filePath(name));
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(name));
        const QJsonObject suite = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY2(!suite.isEmpty(), qPrintable(name));
        QCOMPARE(suite.value(QStringLiteral("bundledVersion")).toInt(), 1);
        QCOMPARE(suite.value(QStringLiteral("evaluation")).toObject()
                     .value(QStringLiteral("kind")).toString(),
                 QStringLiteral("one-shotting"));
        QCOMPARE(suite.value(QStringLiteral("prompts")).toArray().size(), 1);
    }
}

void AppControllerTests::bundledHarnessContextAbBenchmarkIsValid()
{
    const QString root = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("../../assets/benchmarks/custom"));
    QFile f(QDir(root).filePath(QStringLiteral("harness_context_tools_ab_v1.json")));
    QVERIFY(f.open(QIODevice::ReadOnly));

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    const QJsonObject suite = doc.object();
    QVERIFY(!suite.isEmpty());
    QCOMPARE(suite.value(QStringLiteral("id")).toString(),
             QStringLiteral("harness_context_tools_ab_v1"));
    QCOMPARE(suite.value(QStringLiteral("bundledVersion")).toInt(), 1);
    QCOMPARE(suite.value(QStringLiteral("recommendedPasses")).toInt(), 3);

    const QJsonObject evaluation = suite.value(QStringLiteral("evaluation")).toObject();
    QCOMPARE(evaluation.value(QStringLiteral("kind")).toString(),
             QStringLiteral("harness-context-ab"));
    QCOMPARE(evaluation.value(QStringLiteral("target")).toString(),
             QStringLiteral("agent"));
    const QJsonArray profiles = evaluation.value(QStringLiteral("compareAgentProfiles"))
                                    .toArray();
    QCOMPARE(profiles.size(), 3);
    QCOMPARE(profiles.at(0).toString(), QStringLiteral("agent-chat"));
    QCOMPARE(profiles.at(1).toString(), QStringLiteral("agent-intermedio"));
    QCOMPARE(profiles.at(2).toString(), QStringLiteral("agent-maximo"));

    const QJsonArray prompts = suite.value(QStringLiteral("prompts")).toArray();
    QCOMPARE(prompts.size(), 3);
    QSet<QString> ids;
    for (const QJsonValue &value : prompts) {
        const QJsonObject prompt = value.toObject();
        const QString id = prompt.value(QStringLiteral("id")).toString();
        QVERIFY(!id.isEmpty());
        QVERIFY(!ids.contains(id));
        ids.insert(id);
        QVERIFY(!prompt.value(QStringLiteral("prompt")).toString().isEmpty());
        const QJsonObject acceptance = prompt.value(QStringLiteral("acceptance")).toObject();
        QCOMPARE(acceptance.value(QStringLiteral("graderType")).toString(),
                 QStringLiteral("code_tests"));
        QVERIFY(!acceptance.value(QStringLiteral("entryPoint")).toString().isEmpty());
        QVERIFY(!acceptance.value(QStringLiteral("tests")).toString().isEmpty());
    }
}

void AppControllerTests::bundledArtifactLifecycleBenchmarkIsValid()
{
    const QString root = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("../../assets/benchmarks/custom"));
    QFile f(QDir(root).filePath(QStringLiteral("artifact_lifecycle_v1.json")));
    QVERIFY(f.open(QIODevice::ReadOnly));

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    const QJsonObject suite = doc.object();
    QVERIFY(!suite.isEmpty());
    QCOMPARE(suite.value(QStringLiteral("id")).toString(),
             QStringLiteral("artifact_lifecycle_v1"));
    QCOMPARE(suite.value(QStringLiteral("bundledVersion")).toInt(), 1);
    QCOMPARE(suite.value(QStringLiteral("recommendedPasses")).toInt(), 1);

    const QJsonObject evaluation = suite.value(QStringLiteral("evaluation")).toObject();
    QCOMPARE(evaluation.value(QStringLiteral("kind")).toString(),
             QStringLiteral("artifact-lifecycle"));
    QCOMPARE(evaluation.value(QStringLiteral("target")).toString(),
             QStringLiteral("agent"));
    const QJsonArray expectedAgentProfiles{
        QStringLiteral("agent-artifact-local"),
        QStringLiteral("agent-artifact-publisher")};
    QCOMPARE(evaluation.value(QStringLiteral("compareAgentProfiles")).toArray(),
             expectedAgentProfiles);

    const QJsonArray prompts = suite.value(QStringLiteral("prompts")).toArray();
    QCOMPARE(prompts.size(), 2);
    QSet<QString> ids;
    for (const QJsonValue &value : prompts) {
        const QJsonObject prompt = value.toObject();
        const QString id = prompt.value(QStringLiteral("id")).toString();
        QVERIFY(!id.isEmpty());
        QVERIFY(!ids.contains(id));
        ids.insert(id);
        QVERIFY(!prompt.value(QStringLiteral("prompt")).toString().isEmpty());
        const QJsonObject acceptance = prompt.value(QStringLiteral("acceptance")).toObject();
        QVERIFY(!acceptance.value(QStringLiteral("files")).toArray().isEmpty());
        QVERIFY(!acceptance.value(QStringLiteral("expectSubstrings")).toArray().isEmpty());
    }
}

void AppControllerTests::importedBenchmarkNamesDescribeSubset()
{
    AppController app;
    QTemporaryFile input(m_tmp.filePath(QStringLiteral("benchmark-XXXXXX.jsonl")));
    QVERIFY(input.open());
    input.write("{\"task_id\":\"HumanEval/0\",\"prompt\":\"def add(a,b):\\n\","
                "\"test\":\"def check(f):\\n    assert f(1,2)==3\\n\",\"entry_point\":\"add\"}\n");
    input.close();

    const QString id = app.importBenchmarkPack(input.fileName());
    QVERIFY(!id.isEmpty());
    QVariantMap imported;
    for (const QVariant &value : app.customBenchmarks()) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("id")).toString() == id) {
            imported = candidate;
            break;
        }
    }
    QCOMPARE(imported.value(QStringLiteral("name")).toString(),
             QStringLiteral("HumanEval · 1 ítem"));
    QVERIFY(imported.value(QStringLiteral("description")).toString().contains(
        QStringLiteral("Tarea: HumanEval/0")));
}

void AppControllerTests::exportUserDataToWritesBackup()
{
    AppController app;
    const QString path = m_tmp.filePath(QStringLiteral("backup.json"));
    const QString written = app.exportUserDataTo(path);
    QCOMPARE(written, path);

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("app")).toString(), QStringLiteral("LlamaCode"));
}

void AppControllerTests::importUserDataFromRoundTrips()
{
    AppController app;
    const QString path = m_tmp.filePath(QStringLiteral("backup_rt.json"));
    QVERIFY(!app.exportUserDataTo(path).isEmpty());
    // Re-importar el backup recién escrito devuelve la ruta (no vacío).
    QCOMPARE(app.importUserDataFrom(path), path);
}

void AppControllerTests::exportUserDataToEmptyPathErrors()
{
    AppController app;
    QVERIFY(app.exportUserDataTo(QString()).isEmpty());
}

void AppControllerTests::exportChatSessionToMissingSessionErrors()
{
    AppController app;
    // Sesión inexistente → "" (sin colgar en diálogo).
    const QString out = app.exportChatSessionTo(QStringLiteral("no-such-id"),
                                                QStringLiteral("md"),
                                                m_tmp.filePath(QStringLiteral("c.md")));
    QVERIFY(out.isEmpty());
}

void AppControllerTests::readResearchReportPrependsLegacyTopic()
{
    AppController app;
    const QString id = QStringLiteral("legacy-research-report");
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/research");
    QVERIFY(QDir().mkpath(dir));

    QFile md(dir + QLatin1Char('/') + id + QStringLiteral(".md"));
    QVERIFY(md.open(QIODevice::WriteOnly | QIODevice::Truncate));
    md.write("# Reporte\n\nContenido");
    md.close();

    QFile json(dir + QLatin1Char('/') + id + QStringLiteral(".json"));
    QVERIFY(json.open(QIODevice::WriteOnly | QIODevice::Truncate));
    json.write(QJsonDocument(QJsonObject{
        {QStringLiteral("topic"), QStringLiteral("Consulta completa del usuario")}
    }).toJson());
    json.close();

    const QString report = app.readResearchReport(id);
    QVERIFY(report.startsWith(QStringLiteral(
        "# Consulta original\n\nConsulta completa del usuario\n\n---\n\n# Reporte")));
}

void AppControllerTests::researchReportsExposeFormattedDate()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/research");
    QVERIFY(QDir().mkpath(dir));
    const qint64 timestamp =
        QDateTime(QDate(2026, 6, 18), QTime(11, 30)).toMSecsSinceEpoch();

    QFile index(dir + QStringLiteral("/index.json"));
    QVERIFY(index.open(QIODevice::WriteOnly | QIODevice::Truncate));
    index.write(QJsonDocument(QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("dated-report")},
            {QStringLiteral("title"), QStringLiteral("Reporte con fecha")},
            {QStringLiteral("timestamp"), static_cast<double>(timestamp)}
        }
    }).toJson());
    index.close();

    AppController app;
    app.refreshResearchReports();
    const QVariantList reports = app.researchReports();
    QVERIFY(!reports.isEmpty());
    QCOMPARE(reports.first().toMap().value(QStringLiteral("dateLabel")).toString(),
             QStringLiteral("18/06/2026 11:30"));
}

void AppControllerTests::exportWorkspaceIncludesChatsAndResearch()
{
    const QString appLocal =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString chatDir = appLocal + QStringLiteral("/chat");
    const QString researchDir = appLocal + QStringLiteral("/research");
    QVERIFY(QDir().mkpath(chatDir));
    QVERIFY(QDir().mkpath(researchDir));

    const QString workspaceId = QStringLiteral("workspace-export-test");
    QFile chatIndex(chatDir + QStringLiteral("/index.json"));
    QVERIFY(chatIndex.open(QIODevice::WriteOnly | QIODevice::Truncate));
    chatIndex.write(QJsonDocument(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("workspace-chat")},
        {QStringLiteral("title"), QStringLiteral("Chat incluido")},
        {QStringLiteral("projectId"), workspaceId},
        {QStringLiteral("projectName"), QStringLiteral("Tesis")}}}).toJson());
    chatIndex.close();
    QFile chat(chatDir + QStringLiteral("/workspace-chat.json"));
    QVERIFY(chat.open(QIODevice::WriteOnly | QIODevice::Truncate));
    chat.write(QJsonDocument(QJsonObject{{QStringLiteral("messages"), QJsonArray{}}}).toJson());
    chat.close();

    QFile researchIndex(researchDir + QStringLiteral("/index.json"));
    QVERIFY(researchIndex.open(QIODevice::WriteOnly | QIODevice::Truncate));
    researchIndex.write(QJsonDocument(QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("workspace-report")},
        {QStringLiteral("title"), QStringLiteral("Reporte incluido")},
        {QStringLiteral("workspaceId"), workspaceId},
        {QStringLiteral("workspaceName"), QStringLiteral("Tesis")}}}).toJson());
    researchIndex.close();
    QFile report(researchDir + QStringLiteral("/workspace-report.md"));
    QVERIFY(report.open(QIODevice::WriteOnly | QIODevice::Truncate));
    report.write("# Reporte");
    report.close();

    AppController app;
    const QString path = m_tmp.filePath(QStringLiteral("workspace.json"));
    QCOMPARE(app.exportWorkspaceTo(workspaceId, QStringLiteral("Tesis"), path), path);
    QFile bundle(path);
    QVERIFY(bundle.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(bundle.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("format")).toString(),
             QStringLiteral("llamacode-workspace"));
    const QJsonObject contents = root.value(QStringLiteral("contents")).toObject();
    QCOMPARE(contents.value(QStringLiteral("chats")).toArray().size(), 1);
    QCOMPARE(contents.value(QStringLiteral("researchReports")).toArray().size(), 1);
    QVERIFY(root.value(QStringLiteral("excluded")).toArray()
                .contains(QStringLiteral("secrets")));
}

void AppControllerTests::autoStartAgentOnLaunchPersists()
{
    {
        AppController app;
        QVERIFY(!app.autoStartAgentOnLaunch());   // default off
        QSignalSpy spy(&app, &AppController::autoStartAgentOnLaunchChanged);
        app.setAutoStartAgentOnLaunch(true);
        QCOMPARE(spy.count(), 1);
        QVERIFY(app.autoStartAgentOnLaunch());
        // set idéntico no re-emite.
        app.setAutoStartAgentOnLaunch(true);
        QCOMPARE(spy.count(), 1);
    }
    // Persiste entre instancias (QSettings).
    AppController app2;
    QVERIFY(app2.autoStartAgentOnLaunch());
    app2.setAutoStartAgentOnLaunch(false);   // restaurar para no contaminar otros tests
}

void AppControllerTests::taskFailureTextDetected()
{
    QVERIFY(AppController::taskFinalTextIndicatesFailure(
        QStringLiteral("No puedo acceder a sitios web desde este entorno.")));
    QVERIFY(AppController::taskFinalTextIndicatesFailure(
        QStringLiteral("I can't browse the website without tools.")));
    QVERIFY(AppController::taskFinalTextIndicatesFailure(
        QStringLiteral("[error: Error transferring http://127.0.0.1:8081/v1/chat/completions - server replied: Bad Request]")));
    QVERIFY(!AppController::taskFinalTextIndicatesFailure(
        QStringLiteral("Compra: 1230. Venta: 1250. Fuente consultada correctamente.")));
}

void AppControllerTests::taskRequiresToolEvidenceForWebObjective()
{
    const QVariantMap webTask{
        {QStringLiteral("name"), QStringLiteral("Extraer cotización")},
        {QStringLiteral("description"), QStringLiteral("Entrá a https://dolarhoy.com/ y traé compra y venta")}
    };
    QVERIFY(AppController::taskRequiresToolEvidence(webTask));

    const QVariantMap localTask{
        {QStringLiteral("name"), QStringLiteral("Resumir nota")},
        {QStringLiteral("description"), QStringLiteral("Escribí un resumen corto del texto dado")}
    };
    QVERIFY(!AppController::taskRequiresToolEvidence(localTask));
}

void AppControllerTests::deterministicReplayCountsAsToolEvidence()
{
    QVERIFY(!AppController::taskHasToolEvidence(QString(), {}));
    QVERIFY(!AppController::taskHasToolEvidence(QString(), QVariantList{
        QVariantMap{{"tool", "verificación visual por IA"}, {"ok", true}}}));
    QVERIFY(!AppController::taskHasToolEvidence(QString(), QVariantList{
        QVariantMap{{"tool", "stroke 10 pts"}, {"ok", false}}}));
    QVERIFY(AppController::taskHasToolEvidence(QString(), QVariantList{
        QVariantMap{{"tool", "stroke 10 pts"}, {"ok", true}}}));
    QVERIFY(AppController::taskHasToolEvidence(QStringLiteral("[tool] desktop_controls"), {}));
}

void AppControllerTests::visualComparisonIsRecognizedSeparately()
{
    QVERIFY(!AppController::taskHasVisualComparison({}));
    QVERIFY(!AppController::taskHasVisualComparison(QVariantList{
        QVariantMap{{"tool", "verificación visual por IA"}, {"ok", false}}}));
    QVERIFY(!AppController::taskHasVisualComparison(QVariantList{
        QVariantMap{{"tool", "desktop_controls"}, {"ok", true}}}));
    QVERIFY(AppController::taskHasVisualComparison(QVariantList{
        QVariantMap{{"tool", "verificación visual por IA"}, {"ok", true}}}));
}

void AppControllerTests::parseGpuPowerCsvParses()
{
    // index, name, limit, default, min, max, draw
    const QString csv =
        QStringLiteral("0, NVIDIA GeForce RTX 3090, 280.00, 350.00, 100.00, 350.00, 142.50\n");
    const QVariantList gpus = AppController::parseGpuPowerCsv(csv);
    QCOMPARE(gpus.size(), 1);
    const QVariantMap g = gpus.first().toMap();
    QCOMPARE(g.value("index").toInt(), 0);
    QCOMPARE(g.value("name").toString(), QStringLiteral("NVIDIA GeForce RTX 3090"));
    QCOMPARE(g.value("currentW").toDouble(), 280.0);
    QCOMPARE(g.value("defaultW").toDouble(), 350.0);
    QCOMPARE(g.value("minW").toDouble(), 100.0);
    QCOMPARE(g.value("maxW").toDouble(), 350.0);
    QCOMPARE(g.value("drawW").toDouble(), 142.5);
}

void AppControllerTests::parseGpuPowerCsvTolerant()
{
    // Línea basura (sin index numérico) y campo faltante → se ignoran sin romper.
    const QString csv = QStringLiteral("garbage line\n1, GPU B, 200, 250, 90, 250\n");
    const QVariantList gpus = AppController::parseGpuPowerCsv(csv);
    QCOMPARE(gpus.size(), 1);
    const QVariantMap g = gpus.first().toMap();
    QCOMPARE(g.value("index").toInt(), 1);
    QCOMPARE(g.value("drawW").toDouble(), 0.0);   // power.draw ausente
}

void AppControllerTests::researchReportGuardrailsRejectKnownErrors()
{
    const QString report = QStringLiteral(
        "La RTX 3090 no soporta NVLink. La ProArt Z790 trabaja x16+x8 y el segundo "
        "slot viene del chipset. Su VRM alimenta las dos GPU. El anuncio activo "
        "confirma stock. Precio estimado: $1.500.000 ARS.");
    const QStringList issues = AppController::researchReportGuardrailIssues(report);
    QVERIFY(issues.size() >= 5);
}

void AppControllerTests::researchReportGuardrailsAcceptCorrectedClaims()
{
    const QString report = QStringLiteral(
        "La RTX 3090 soporta NVLink sujeto al modelo, puente y software. En la "
        "ASUS ProArt Z790-CREATOR los dos slots principales usan líneas del CPU "
        "en x8/x8. La PSU alimenta las GPU. La publicación no confirma stock ni "
        "precio, por lo que se marca como no verificado.");
    QVERIFY(AppController::researchReportGuardrailIssues(report).isEmpty());
}

void AppControllerTests::ggufRecommendationCandidateFilter()
{
    QVERIFY(!AppController::isGgufRecommendationCandidate(
        QStringLiteral("lmstudio-community/Qwen3-14B-MLX-4bit"), false, false));
    QVERIFY(!AppController::isGgufRecommendationCandidate(
        QStringLiteral("cyankiwi/Qwen3.5-9B-AWQ-BF16-INT4"), false, false));
    QVERIFY(AppController::isGgufRecommendationCandidate(
        QStringLiteral("Qwen/Qwen3.5-9B-MTP"), false, true));
    QVERIFY(AppController::isGgufRecommendationCandidate(
        QStringLiteral("unsloth/Qwen3.5-9B-GGUF"), false, false));
}

void AppControllerTests::modelRecommendationsUseResolvableGgufNames()
{
    QCOMPARE(AppController::recommendedGgufFileName(
                 QStringLiteral("unsloth/Qwen3.6-35B-A3B-MTP-GGUF"),
                 QStringLiteral("Qwen/Qwen3.6-35B-A3B-MTP"),
                 QStringLiteral("Q4_K_M")),
             QStringLiteral("Qwen3.6-35B-A3B-UD-Q4_K_M.gguf"));
    QCOMPARE(AppController::recommendedGgufFileName(
                 QStringLiteral("unsloth/Qwen3.5-2B-MTP-GGUF"),
                 QStringLiteral("Qwen/Qwen3.5-2B-MTP"),
                 QStringLiteral("Q4_K_M")),
             QStringLiteral("Qwen3.5-2B-Q4_K_M.gguf"));
    QCOMPARE(AppController::recommendedGgufFileName(
                 QStringLiteral("bartowski/Qwen2.5-7B-Instruct-GGUF"),
                 QStringLiteral("Qwen/Qwen2.5-7B-Instruct"),
                 QStringLiteral("Q4_K_M")),
             QStringLiteral("Qwen2.5-7B-Instruct-Q4_K_M.gguf"));

    const QStringList siblings = {
        QStringLiteral("Qwen3.6-35B-A3B-UD-IQ3_S.gguf"),
        QStringLiteral("Qwen3.6-35B-A3B-UD-Q4_K_M.gguf"),
        QStringLiteral("Qwen3.6-35B-A3B-Q8_0.gguf")
    };
    QCOMPARE(AppController::resolveRecommendedGgufFileName(
                 siblings,
                 QStringLiteral("Qwen3.6-35B-A3B-MTP-Q4_K_M.gguf")),
             QStringLiteral("Qwen3.6-35B-A3B-UD-Q4_K_M.gguf"));
    const QStringList hermesSiblings = {
        QStringLiteral("Hermes-3-Llama-3.1-8B-Q3_K_M.gguf"),
        QStringLiteral("Hermes-3-Llama-3.1-8B-IQ4_XS.gguf"),
        QStringLiteral("Hermes-3-Llama-3.1-8B-Q8_0.gguf")
    };
    QCOMPARE(AppController::resolveRecommendedGgufFileName(
                 hermesSiblings,
                 QStringLiteral("Hermes-3-Llama-3.1-8B-IQ4_XS.gguf")),
             QStringLiteral("Hermes-3-Llama-3.1-8B-IQ4_XS.gguf"));

    const QStringList catalogCandidates = {
        QStringLiteral(":/assets/hwfit/hf_models.json"),
        QDir::current().absoluteFilePath(QStringLiteral("assets/hwfit/hf_models.json")),
        QDir::current().absoluteFilePath(QStringLiteral("../assets/hwfit/hf_models.json")),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../assets/hwfit/hf_models.json"))
    };
    QFile catalog;
    for (const QString &path : catalogCandidates) {
        catalog.setFileName(path);
        if (catalog.exists())
            break;
    }
    QVERIFY(catalog.open(QIODevice::ReadOnly));
    const QJsonArray rows = QJsonDocument::fromJson(catalog.readAll()).array();
    bool sawTinyQwen = false;
    bool sawNanbeige = false;
    for (const QJsonValue &v : rows) {
        const QJsonObject row = v.toObject();
        if (row.value(QStringLiteral("name")).toString() == QLatin1String("Nanbeige/Nanbeige4.2-3B")) {
            QCOMPARE(row.value(QStringLiteral("architecture")).toString(), QStringLiteral("nanbeige"));
            QCOMPARE(row.value(QStringLiteral("required_engine")).toString(), QStringLiteral("nanbeige42"));
            const QJsonArray sources = row.value(QStringLiteral("gguf_sources")).toArray();
            QVERIFY(!sources.isEmpty());
            QCOMPARE(sources.first().toObject().value(QStringLiteral("file")).toString(),
                     QStringLiteral("nanbeige4.2-3b-Q4_K_M.gguf"));
            sawNanbeige = true;
        }
        if (row.value(QStringLiteral("name")).toString() == QLatin1String("Qwen/Qwen3.5-2B-MTP")) {
            const QJsonArray sources = row.value(QStringLiteral("gguf_sources")).toArray();
            QVERIFY(!sources.isEmpty());
            QCOMPARE(sources.first().toObject().value(QStringLiteral("repo")).toString(),
                     QStringLiteral("unsloth/Qwen3.5-2B-MTP-GGUF"));
            sawTinyQwen = true;
        }
    }
    QVERIFY(sawTinyQwen);
    QVERIFY(sawNanbeige);
}

void AppControllerTests::createRecommendedLaunchProfileBuildsProfile()
{
    const QByteArray oldSystemProfiles = qgetenv("LLAMACODE_SYSTEM_PROFILES");
    const QString emptySystemProfiles = m_tmp.filePath(QStringLiteral("empty-system-profiles.json"));
    {
        QFile f(emptySystemProfiles);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("[]");
    }
    qputenv("LLAMACODE_SYSTEM_PROFILES", QFile::encodeName(emptySystemProfiles));
    AppController app;

    const QString exePath = m_tmp.filePath(QStringLiteral("llama-server.exe"));
    QFile exe(exePath);
    QVERIFY(exe.open(QIODevice::WriteOnly));
    exe.write("fake-binary");
    exe.close();
    const QString binaryId = app.binaryRegistry()->add(
        exePath, QStringLiteral("fake llama-server"), QStringLiteral("custom"),
        QStringLiteral("cpu"), QString());
    QVERIFY(!binaryId.isEmpty());

    const QString modelPath = m_tmp.filePath(QStringLiteral("Qwen3.5-9B-Q4_K_M.gguf"));
    QFile modelFile(modelPath);
    QVERIFY(modelFile.open(QIODevice::WriteOnly));
    modelFile.write("GGUF");
    modelFile.close();

    CatalogModel model;
    model.id = QStringLiteral("setup-model");
    model.rootId = QStringLiteral("setup-root");
    model.absolutePath = modelPath;
    model.fileName = QFileInfo(modelPath).fileName();
    model.sizeBytes = QFileInfo(modelPath).size();
    model.mtime = QFileInfo(modelPath).lastModified();
    model.familyHint = QStringLiteral("qwen");
    model.quantHint = QStringLiteral("Q4_K_M");
    model.isAvailable = true;
    app.modelCatalog()->addOrUpdate(model);

    const QString launchId = app.createRecommendedLaunchProfile();
    QVERIFY(!launchId.isEmpty());
    QCOMPARE(app.createRecommendedLaunchProfile(), launchId);
    QVERIFY(app.hasAnyLaunch());
    QVERIFY(!app.needsSetup());
    QCOMPARE(app.profileManager()->getLaunchProfile(launchId).value("id").toString(), launchId);
    if (oldSystemProfiles.isEmpty())
        qunsetenv("LLAMACODE_SYSTEM_PROFILES");
    else
        qputenv("LLAMACODE_SYSTEM_PROFILES", oldSystemProfiles);
}

void AppControllerTests::createRecommendedLaunchProfileReusesExistingMenuProfile()
{
    AppController app;
    const int before = app.profileManager()->launchProfiles()->rowCount();
    QVERIFY(before > 0);

    const QString launchId = app.createRecommendedLaunchProfile();
    QVERIFY(!launchId.isEmpty());
    QCOMPARE(app.profileManager()->launchProfiles()->rowCount(), before);
    QCOMPARE(app.readSetting(QStringLiteral("lastLaunchId"), QString()).toString(), launchId);
}

void AppControllerTests::browserMcpEffectiveResolves()
{
    // Override "on"/"off" pisa el toggle global; "inherit" (u otro) lo hereda.
    QVERIFY( AppController::browserMcpEffective(QStringLiteral("on"),  false));
    QVERIFY( AppController::browserMcpEffective(QStringLiteral("on"),  true));
    QVERIFY(!AppController::browserMcpEffective(QStringLiteral("off"), true));
    QVERIFY(!AppController::browserMcpEffective(QStringLiteral("off"), false));
    QVERIFY( AppController::browserMcpEffective(QStringLiteral("inherit"), true));
    QVERIFY(!AppController::browserMcpEffective(QStringLiteral("inherit"), false));
    QVERIFY( AppController::browserMcpEffective(QString(), true));   // vacío → hereda
}

void AppControllerTests::integrationSecretsMigrateOutOfJson()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/integrations.json");
    const QString secret = QStringLiteral("legacy-camofox-secret-DO-NOT-PERSIST");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(QJsonArray{QJsonObject{
            {QStringLiteral("id"), QStringLiteral("legacy-camofox")},
            {QStringLiteral("name"), QStringLiteral("Camofox")},
            {QStringLiteral("provider"), QStringLiteral("camofox")},
            {QStringLiteral("baseUrl"), QStringLiteral("http://127.0.0.1:9377")},
            {QStringLiteral("apiKey"), secret},
            {QStringLiteral("enabled"), true}}}).toJson());
    }

    AppController controller; // constructor ejecuta la migración
    QFile migrated(path);
    QVERIFY(migrated.open(QIODevice::ReadOnly));
    const QByteArray json = migrated.readAll();
    QVERIFY(!json.contains(secret.toUtf8()));
    const QJsonObject stored = QJsonDocument::fromJson(json).array().first().toObject();
    QCOMPARE(stored.value(QStringLiteral("apiKeyRef")).toString(),
             QStringLiteral("integration/legacy-camofox"));
    QVERIFY(!stored.contains(QStringLiteral("apiKey")));
    const QVariantList integrations = controller.integrations();
    bool migratedHasKey = false;
    for (const QVariant &entry : integrations) {
        const QVariantMap integration = entry.toMap();
        if (integration.value(QStringLiteral("id")).toString()
            == QLatin1String("api:legacy-camofox")) {
            migratedHasKey = integration.value(QStringLiteral("config")).toMap()
                                 .value(QStringLiteral("hasKey")).toBool();
            break;
        }
    }
    QVERIFY(migratedHasKey);
    QVERIFY(controller.removeIntegration(QStringLiteral("api:legacy-camofox")));
}

void AppControllerTests::browserTeachSkillsLifecycle()
{
    // sanitize: slug seguro para filename.
    QCOMPARE(BrowserTeach::sanitize(QStringLiteral("Login   Banco!!")),
             QStringLiteral("login-banco"));
    QCOMPARE(BrowserTeach::sanitize(QStringLiteral("a.b/c")), QStringLiteral("a-b-c"));

    // skillPath usa el slug + .mjs dentro de skillsDir.
    const QString path = BrowserTeach::skillPath(QStringLiteral("My Skill"));
    QVERIFY(path.endsWith(QStringLiteral("/my-skill.mjs")));
    QVERIFY(path.startsWith(BrowserTeach::skillsDir()));

    // recordCommand: codegen con -o al path; agrega url http válida.
    const QString rc = BrowserTeach::recordCommand(QStringLiteral("My Skill"),
                                                   QStringLiteral("https://x.com"));
    QVERIFY(rc.contains(QStringLiteral("playwright codegen")));
    QVERIFY(rc.contains(QStringLiteral("my-skill.mjs")));
    QVERIFY(rc.endsWith(QStringLiteral("https://x.com")));
    // Perfil persistente: codegen graba con --user-data-dir al dir del skill, así el
    // replay reusa el login. El dir vive bajo skillsDir()/profiles/<slug>.
    const QString prof = BrowserTeach::profileDir(QStringLiteral("My Skill"));
    QVERIFY(prof.endsWith(QStringLiteral("/profiles/my-skill")));
    QVERIFY(prof.startsWith(BrowserTeach::skillsDir()));
    QVERIFY(rc.contains(QStringLiteral("--user-data-dir=\"") + prof + QLatin1Char('"')));
    QVERIFY(BrowserTeach::profileDir(QStringLiteral("!!!")).isEmpty());   // sin slug → vacío
    // url no-http se ignora.
    QVERIFY(!BrowserTeach::recordCommand(QStringLiteral("s"), QStringLiteral("ftp://x"))
                 .contains(QStringLiteral("ftp://")));

    // replayProgramArgs: {node, path}.
    const QStringList pa = BrowserTeach::replayProgramArgs(QStringLiteral("My Skill"));
    QCOMPARE(pa.size(), 2);
    QCOMPARE(pa.first(), QStringLiteral("node"));
    QVERIFY(pa.at(1).endsWith(QStringLiteral("my-skill.mjs")));

    // list/has/remove sobre un .mjs real escrito a disco (skillsDir aislado por
    // setTestModeEnabled en initTestCase).
    QVERIFY(!BrowserTeach::hasSkill(QStringLiteral("My Skill")));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("// dummy\n");
    f.close();
    QVERIFY(BrowserTeach::hasSkill(QStringLiteral("My Skill")));
    QVERIFY(BrowserTeach::listSkills().contains(QStringLiteral("my-skill")));
    QVERIFY(BrowserTeach::removeSkill(QStringLiteral("My Skill")));
    QVERIFY(!BrowserTeach::hasSkill(QStringLiteral("My Skill")));
}

void AppControllerTests::preferredAgentLaunchSelection()
{
    QCOMPARE(AppController::choosePreferredAgentLaunchId("agent-local", false,
                                                         "active-local", "global"),
             QStringLiteral("active-local"));
    QCOMPARE(AppController::choosePreferredAgentLaunchId("agent-cloud", true,
                                                         "active-local", "global"),
             QStringLiteral("agent-cloud"));
    QCOMPARE(AppController::choosePreferredAgentLaunchId("agent-local", false, {}, "global"),
             QStringLiteral("agent-local"));
    QCOMPARE(AppController::choosePreferredAgentLaunchId({}, false, {}, "global"),
             QStringLiteral("global"));

    AppController app;
    const QVariantList launches = app.profileManager()->launchProfilesForMenu();
    QVERIFY(launches.size() >= 2);
    const QString agentId = launches.at(0).toMap().value(QStringLiteral("id")).toString();
    const QString swappedId = launches.at(1).toMap().value(QStringLiteral("id")).toString();
    QVERIFY(agentId != swappedId);
    app.writeSetting(QStringLiteral("lastAgentLaunchId"), agentId);
    app.writeSetting(QStringLiteral("lastLaunchId"), swappedId);
    QCOMPARE(app.preferredAgentLaunchId(), agentId);
    app.writeSetting(QStringLiteral("lastAgentLaunchId"), QStringLiteral("missing-profile"));
    QCOMPARE(app.preferredAgentLaunchId(), swappedId);
}

void AppControllerTests::remoteBackendEnablesServerDependentUi()
{
    QVERIFY(!AppController::backendAvailability(false, false));
    QVERIFY(AppController::backendAvailability(true, false));
    QVERIFY(AppController::backendAvailability(false, true));
}

void AppControllerTests::windowsStartupCommandQuotesExecutable()
{
    QCOMPARE(AppController::windowsStartupCommand(
                 QStringLiteral("C:/Program Files/LlamaCode/LlamaCode.exe")),
             QStringLiteral("\"C:\\Program Files\\LlamaCode\\LlamaCode.exe\" --startup"));
}

void AppControllerTests::startupHiddenRequiresBothFlags()
{
    QVERIFY(AppController::shouldStartHidden(true, true));
    QVERIFY(!AppController::shouldStartHidden(true, false));
    QVERIFY(!AppController::shouldStartHidden(false, true));
    QVERIFY(!AppController::shouldStartHidden(false, false));
}

void AppControllerTests::loopTaskRunsBodyUntilGoalMet()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});
    fake->setVerdicts({QStringLiteral("GOAL_NOT_MET falta"), QStringLiteral("GOAL_MET listo")});
    app.setTestAgentBackend(fake);

    const QString id = makeLoopTask(app, QStringLiteral("Loop hasta goal"), 5);
    QSignalSpy fin(&app, &AppController::taskRunFinished);
    app.runTaskBodyForTest(id);

    QTRY_VERIFY_WITH_TIMEOUT(!fin.isEmpty(), 5000);
    const QList<QVariant> args = fin.takeFirst();
    QCOMPARE(args.at(2).toString(), QStringLiteral("ok"));   // status final
    // Cuerpo corrió 2 veces (iter1 GOAL_NOT_MET → iter2 GOAL_MET).
    QCOMPARE(fake->bodyRuns(), 2);
    // Checkpoint/resume: el cuerpo de la iteración 2 recibió el progreso del
    // veredicto previo ("falta") para retomar desde ahí, no arrancar de cero.
    QVERIFY(fake->lastBodyPrompt().contains(QStringLiteral("Progreso acumulado")));
    QVERIFY(fake->lastBodyPrompt().contains(QStringLiteral("falta")));
}

void AppControllerTests::loopTaskStopsAtMaxIterations()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});
    // Nunca cumple → debe cortar por techo de iteraciones.
    fake->setVerdicts({QStringLiteral("GOAL_NOT_MET"), QStringLiteral("GOAL_NOT_MET"),
                       QStringLiteral("GOAL_NOT_MET"), QStringLiteral("GOAL_NOT_MET")});
    app.setTestAgentBackend(fake);

    const QString id = makeLoopTask(app, QStringLiteral("Loop sin fin"), 3);
    QSignalSpy fin(&app, &AppController::taskRunFinished);
    app.runTaskBodyForTest(id);

    QTRY_VERIFY_WITH_TIMEOUT(!fin.isEmpty(), 5000);
    QCOMPARE(fake->bodyRuns(), 3);   // exactamente maxIter corridas del cuerpo
}

void AppControllerTests::loopTaskStopsAtMaxSeconds()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});
    fake->setReplyDelayMs(1100);
    fake->setVerdicts({QStringLiteral("GOAL_NOT_MET sigue faltando"),
                       QStringLiteral("GOAL_NOT_MET no debería ejecutarse")});
    app.setTestAgentBackend(fake);

    const QString id = makeLoopTask(app, QStringLiteral("Loop con timeout"), 100);
    QCOMPARE(app.taskStore()->save(id, {{QStringLiteral("loopMaxSeconds"), 1}}), id);
    QSignalSpy fin(&app, &AppController::taskRunFinished);
    app.runTaskBodyForTest(id);

    QTRY_VERIFY_WITH_TIMEOUT(!fin.isEmpty(), 5000);
    QCOMPARE(fin.first().at(2).toString(), QStringLiteral("ok"));
    QCOMPARE(fake->bodyRuns(), 1);
    QVERIFY(fin.first().at(3).toString().contains(QStringLiteral("tiempo")));
}

void AppControllerTests::dataDrivenTaskRunsBodyPerRow()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});
    app.setTestAgentBackend(fake);

    // Task con dataset CSV de 2 filas y {{var}} en la descripción: el cuerpo corre
    // una vez por fila y el prompt de cada corrida trae los valores sustituidos.
    const QVariantMap def{
        {QStringLiteral("name"), QStringLiteral("Saludo por lote")},
        {QStringLiteral("description"), QStringLiteral("Saludá a {{nombre}} de {{edad}} años")},
        {QStringLiteral("executionMode"), QStringLiteral("auto")},
        {QStringLiteral("datasetInline"), QStringLiteral("nombre,edad\nAna,30\nBeto,40")},
        {QStringLiteral("datasetFormat"), QStringLiteral("csv")}};
    const QString id = app.taskStore()->save(QString(), def);

    QSignalSpy fin(&app, &AppController::taskRunFinished);
    app.runTaskBodyForTest(id);

    QTRY_COMPARE_WITH_TIMEOUT(fin.count(), 2, 5000);
    QCOMPARE(fake->bodyRuns(), 2);              // una corrida del cuerpo por fila
    QCOMPARE(fin.count(), 2);                   // cada fila = un registro/finished
    // La última corrida sustituyó la 2da fila (Beto/40), sin dejar el placeholder.
    QVERIFY(fake->lastBodyPrompt().contains(QStringLiteral("Beto")));
    QVERIFY(fake->lastBodyPrompt().contains(QStringLiteral("40")));
    QVERIFY(!fake->lastBodyPrompt().contains(QStringLiteral("{{nombre}}")));
}

void AppControllerTests::taskRetriesBodyOnFailure()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});
    // 1er cuerpo declara fallo → reintento → 2do cuerpo ok.
    fake->setBodyReplies({QStringLiteral("no pude completar la tarea"),
                          QStringLiteral("listo, hecho")});
    app.setTestAgentBackend(fake);

    const QVariantMap def{
        {QStringLiteral("name"), QStringLiteral("Con reintento")},
        {QStringLiteral("description"), QStringLiteral("Tarea local")},
        {QStringLiteral("executionMode"), QStringLiteral("auto")},
        {QStringLiteral("maxRetries"), 2}};
    const QString id = app.taskStore()->save(QString(), def);

    QSignalSpy fin(&app, &AppController::taskRunFinished);
    app.runTaskBodyForTest(id);
    // Reintento asíncrono: esperar los 2 finished (1er fallo + relanzamiento ok).
    QTRY_COMPARE_WITH_TIMEOUT(fin.count(), 2, 8000);

    QCOMPARE(fake->bodyRuns(), 2);                       // falló, reintentó
    QCOMPARE(fin.count(), 2);
    QCOMPARE(fin.at(0).at(2).toString(), QStringLiteral("error"));   // 1er intento falló
    QCOMPARE(fin.at(1).at(2).toString(), QStringLiteral("ok"));      // reintento ok
}

void AppControllerTests::datasetAbortStopsOnError()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});
    app.setTestAgentBackend(fake);   // sin bodyReplies → siempre "trabajo realizado N"

    // 2 filas, sin reintentos, política abort. Forzamos fallo con un objetivo que
    // exige herramientas web (taskRequiresToolEvidence) y el fake no ejecuta ninguna
    // → la fila 1 falla → el lote se corta (fila 2 no corre).
    const QVariantMap def{
        {QStringLiteral("name"), QStringLiteral("Lote abortable")},
        {QStringLiteral("description"), QStringLiteral("Buscá en internet {{q}} y resumí")},
        {QStringLiteral("executionMode"), QStringLiteral("auto")},
        {QStringLiteral("maxRetries"), 0},
        {QStringLiteral("datasetInline"), QStringLiteral("q\nuno\ndos")},
        {QStringLiteral("datasetFormat"), QStringLiteral("csv")},
        {QStringLiteral("datasetOnError"), QStringLiteral("abort")}};
    const QString id = app.taskStore()->save(QString(), def);

    QSignalSpy fin(&app, &AppController::taskRunFinished);
    app.runTaskBodyForTest(id);
    QTRY_VERIFY_WITH_TIMEOUT(!fin.isEmpty(), 5000);
    QTest::qWait(60);   // dar chance a un (indebido) avance de fila

    QVERIFY(!fin.isEmpty());
    QCOMPARE(fin.first().at(2).toString(), QStringLiteral("error"));
    QCOMPARE(fake->bodyRuns(), 1);   // abort: la 2da fila NO se ejecuta
}

void AppControllerTests::fileWatchTriggerRegistersPath()
{
    AppController app;
    // Archivo real a vigilar (el watcher sólo agrega paths existentes).
    QTemporaryFile f(QDir::temp().filePath(QStringLiteral("trigger-XXXXXX.txt")));
    QVERIFY(f.open());
    f.write("x"); f.flush();
    const QString path = QFileInfo(f.fileName()).absoluteFilePath();

    const QVariantMap def{
        {QStringLiteral("name"), QStringLiteral("Watch")},
        {QStringLiteral("description"), QStringLiteral("Corre al cambiar el archivo")},
        {QStringLiteral("triggerType"), QStringLiteral("fileWatch")},
        {QStringLiteral("triggerPath"), path}};
    app.taskStore()->save(QString(), def);
    app.rebuildTaskTriggers();

    QVERIFY(app.watchedTriggerPaths().contains(path));

    // Pura: fileWatchTriggers filtra por triggerType y path no vacío. (La DB de tasks
    // puede tener triggers de corridas previas → verificamos que EL NUESTRO esté.)
    const QVariantList trg = AutomationRunner::fileWatchTriggers(app.taskStore()->all());
    QVERIFY(!trg.isEmpty());
    bool found = false;
    for (const QVariant &v : trg)
        if (v.toMap().value("path").toString() == path) found = true;
    QVERIFY(found);
}

void AppControllerTests::charlaTranscriptRoutesToAgentWhenRunning()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});
    app.setTestAgentBackend(fake);

    // Ingi Charla con agente corriendo: el transcript de voz va al agente
    // (computer-use/visión), no al chat backend.
    QVERIFY(app.dispatchCharlaTranscript(QStringLiteral("abrí el navegador")));
    QVERIFY(app.charlaUseAgentForTest());
    QCOMPARE(fake->bodyRuns(), 1);   // el agente recibió el mensaje

    // Agente detenido: NO rutea al agente (no más mensajes al fake). La rama de
    // fallback a chat se ejercita en QA manual (requiere server real).
    fake->stop();
    QCOMPARE(fake->bodyRuns(), 1);
    QVERIFY(!(app.charlaUseAgentForTest() && fake->running()));
}

void AppControllerTests::charlaCursorOcrIsOptInAndDoesNotHijackChat()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});
    app.setTestAgentBackend(fake);

    // Apagado (default): ni siquiera una orden de cursor explícita se intercepta.
    // Es la garantía de que actualizar la app no estrena la captura de pantalla.
    QVERIFY(!app.tryVoiceCursorCommand(QStringLiteral("clic en Guardar")));
    QVERIFY(app.dispatchCharlaTranscript(QStringLiteral("clic en Guardar")));
    QCOMPARE(fake->bodyRuns(), 1);   // fue al agente, como cualquier frase

    // Encendido: una frase que NO es orden de cursor sigue yendo al LLM. Charla es
    // una conversación; mencionar un clic no puede secuestrar el turno.
    app.setVoiceCursorOcrForTest(true);
    QVERIFY(!app.tryVoiceCursorCommand(QStringLiteral("¿tendría que hacer clic en Guardar?")));
    QVERIFY(!app.tryVoiceCursorCommand(QStringLiteral("hola, ¿cómo andás?")));
    QVERIFY(app.dispatchCharlaTranscript(QStringLiteral("hola, ¿cómo andás?")));
    QCOMPARE(fake->bodyRuns(), 2);
    // La ejecución real de una orden (OCR + clic) es QA manual: necesita pantalla
    // viva y paquete de idioma OCR. Ver CLAUDE.md.
}

void AppControllerTests::ocrStatusAlwaysExplainsItself()
{
    AppController app;
    const QVariantMap st = app.ocrStatus();
    // El contrato vale en cualquier máquina, haya OCR o no: la UI SIEMPRE tiene
    // algo que mostrar. Un `detail` vacío dejaría el toggle fallando mudo, que es
    // justo lo que este map existe para evitar.
    QVERIFY(st.contains(QStringLiteral("available")));
    QVERIFY(st.contains(QStringLiteral("detail")));
    QVERIFY(!st.value(QStringLiteral("detail")).toString().isEmpty());
    // Con OCR se nombra el idioma (si el motor quedó en otro idioma que la UI, los
    // labels con tildes se leen mal: hay que poder verlo). Sin OCR, el mensaje
    // tiene que decir cómo resolverlo, no sólo que no hay.
    if (st.value(QStringLiteral("available")).toBool())
        QVERIFY(!st.value(QStringLiteral("language")).toString().isEmpty());
    else
        QVERIFY(st.value(QStringLiteral("detail")).toString().contains(QStringLiteral("OCR")));
}

// Regresión "el perfil falla en 4 s con failureStage=server-load": el benchmark
// esperaba 8 s a que muriera el server anterior y, si seguía vivo, arrancaba
// igual. startServer abortaba con "servidor ya en ejecución" y la pasada entera
// se anotaba como fallo. Con DeepSeek V4 (116 GB mapeados, ~40 GB de VRAM) el
// cierre tarda más que eso, así que agotado el presupuesto hay que MATAR, no
// seguir de largo.
void AppControllerTests::benchmarkStopStepKillsWhenBudgetRunsOut()
{
    using Step = AppController::BenchStopStep;
    // Ya murió: arrancar, sobre presupuesto o no.
    QCOMPARE(AppController::benchmarkStopStep(false, 45000), Step::Proceed);
    QCOMPARE(AppController::benchmarkStopStep(false, 0), Step::Proceed);
    QCOMPARE(AppController::benchmarkStopStep(false, -300), Step::Proceed);
    // Sigue vivo y queda tiempo: seguir esperando.
    QCOMPARE(AppController::benchmarkStopStep(true, 45000), Step::Wait);
    QCOMPARE(AppController::benchmarkStopStep(true, 300), Step::Wait);
    // Sigue vivo y se acabó el tiempo: matar (antes: arrancaba igual).
    QCOMPARE(AppController::benchmarkStopStep(true, 0), Step::Kill);
    QCOMPARE(AppController::benchmarkStopStep(true, -300), Step::Kill);
}

// Los evaluadores miran texto libre de un LLM, así que no pueden puntuar el
// FORMATO. En la primera corrida real, python_prime y code_refactor dieron False
// en las 4 pasadas de todos los modelos: exigían el literal "n <= 1" y "**2", y
// cualquier función correcta escrita "n<=1" o "x ** 2" contaba como error.
void AppControllerTests::benchmarkEvaluatorsToleratePresentationNotContent()
{
    const QString m = QStringLiteral("short");
    auto ok = [&](const char *id, const QString &text) {
        return AppController::evalBenchTaskForTest(m, QLatin1String(id), text);
    };

    // is_prime: la guarda del caso borde vale en cualquier estilo.
    QVERIFY(ok("python_prime", "def is_prime(n: int) -> bool:\n    if n <= 1:\n        return False\n    return True"));
    QVERIFY(ok("python_prime", "def is_prime(n:int)->bool:\n if n<=1: return False\n return True"));
    QVERIFY(ok("python_prime", "def is_prime(n: int) -> bool:\n    if n < 2:\n        return False\n    return True"));
    QVERIFY(ok("python_prime", "```python\ndef is_prime(n: int) -> bool:\n    if n<2: return False\n    return True\n```"));
    QVERIFY(ok("python_prime", "<think>me piden una función</think>\ndef is_prime(n):\n if n<=1: return False\n return True"));
    QVERIFY(!ok("python_prime", "No sé cómo hacerlo."));
    QVERIFY(!ok("python_prime", "def is_even(n):\n    return n % 2 == 0"));   // otra función

    // Refactor a one-liner: la potencia puede venir espaciada o como x*x.
    QVERIFY(ok("code_refactor", "result = [x**2 for x in range(10) if x % 2 == 0]"));
    QVERIFY(ok("code_refactor", "result = [x ** 2 for x in range(10) if x % 2 == 0]"));
    QVERIFY(ok("code_refactor", "[x*x for x in range(10) if x % 2 == 0]"));
    QVERIFY(ok("code_refactor", "```python\n[pow(x, 2) for x in range(10) if x % 2 == 0]\n```"));
    QVERIFY(!ok("code_refactor", "for x in range(10): result.append(x**2)"));  // no es comprehension

    // YES: markdown, prefijos y castellano.
    QVERIFY(ok("reasoning_logic", "YES, por transitividad."));
    QVERIFY(ok("reasoning_logic", "**YES** — la relación es transitiva."));
    QVERIFY(ok("reasoning_logic", "Answer: YES. Todo A es C."));
    QVERIFY(ok("reasoning_logic", "Sí, porque la relación es transitiva."));
    QVERIFY(!ok("reasoning_logic", "NO, no se sigue."));

    // JSON: aunque venga con reasoning o texto alrededor.
    QVERIFY(ok("json_output", "{\"name\":\"Alice\",\"age\":30,\"active\":true}"));
    QVERIFY(ok("json_output", "```json\n{\"name\":\"Alice\",\"age\":30,\"active\":true}\n```"));
    QVERIFY(ok("json_output", "Acá va: {\"name\":\"Alice\",\"age\":30,\"active\":true} listo."));
    QVERIFY(!ok("json_output", "{\"nombre\":\"Alice\"}"));

    // La cuenta, con separadores o sin ellos.
    QVERIFY(ok("math_arithmetic", "El resultado final es 436."));
    QVERIFY(ok("math_arithmetic", "**436**"));
    QVERIFY(!ok("math_arithmetic", "El resultado es 999."));

    // El caso que rompía todo en la corrida real: el agente NO contesta por chat,
    // escribe el archivo y su mensaje final viene vacío. El contenido del
    // workspace tiene que llegar al evaluador.
    QTemporaryDir ws;
    QVERIFY(ws.isValid());
    QFile py(ws.filePath(QStringLiteral("is_prime.py")));
    QVERIFY(py.open(QIODevice::WriteOnly | QIODevice::Text));
    py.write("def is_prime(n: int) -> bool:\n    if n <= 1:\n        return False\n    return True\n");
    py.close();
    QDir(ws.path()).mkpath(QStringLiteral(".llamacode"));
    QFile log(ws.filePath(QStringLiteral(".llamacode/agent_events.jsonl")));
    QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::Text));
    log.write("{\"kind\":\"observation\"}\n");
    log.close();

    const QString wsText = AppController::benchWorkspaceText(
        ws.path(), QStringList{QStringLiteral("is_prime.py"),
                               QStringLiteral(".llamacode/agent_events.jsonl")});
    QVERIFY(wsText.contains(QStringLiteral("def is_prime")));
    QVERIFY(!wsText.contains(QStringLiteral("agent_events")));   // el log no se puntúa
    QVERIFY(AppController::evalBenchTaskForTest(m, QStringLiteral("python_prime"),
                                                QString() + QLatin1Char('\n') + wsText));
}

// Una serie de benchmarks son horas. Si la app se cae en el perfil 8 de 13, lo que
// faltaba tiene que poder retomarse: un crash del proceso no se puede atrapar desde
// adentro, así que el punto de reanudación se escribe en disco antes de cada perfil.
void AppControllerTests::benchmarkResumesWhereItDiedInsteadOfLosingTheSeries()
{
    AppController app;
    // Sin serie previa no hay nada que reanudar, y resume no inventa una corrida.
    QVERIFY(app.pendingBenchmark().isEmpty());
    QVERIFY(!app.resumeBenchmark());

    // Simular lo que deja el bucle antes de tocar el perfil 3 de 5.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/benchmark-runs");
    QDir().mkpath(dir);
    QJsonObject o;
    o["pending"] = QJsonArray{"sys-48-katcoder-262k", "sys-bench-48-kat-f16", "sys-48-katcoder-131k"};
    o["mode"] = "short";
    o["passes"] = 2;
    o["target"] = "agent";
    o["agentProfileId"] = "agent-chat";
    o["runLabel"] = "standard";
    QFile f(dir + QStringLiteral("/.resume.json"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.close();

    const QVariantMap pending = app.pendingBenchmark();
    QCOMPARE(pending.value("pending").toStringList().size(), 3);
    QCOMPARE(pending.value("pending").toStringList().first(),
             QStringLiteral("sys-48-katcoder-262k"));
    QCOMPARE(pending.value("passes").toInt(), 2);
    QCOMPARE(pending.value("target").toString(), QStringLiteral("agent"));
    QCOMPARE(pending.value("agentProfileId").toString(), QStringLiteral("agent-chat"));

    // Una serie terminada no deja nada pendiente: el archivo con lista vacía no
    // debe hacer que la app crea que quedó trabajo a medias.
    o["pending"] = QJsonArray{};
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.close();
    QVERIFY(app.pendingBenchmark().isEmpty());
    QVERIFY(!app.resumeBenchmark());
    QFile::remove(f.fileName());
}

// Regresión "el benchmark Corta termina sin score": en modo agente el puntaje
// salía sólo de los archivos que el agente dejara en el workspace (acceptance o
// el fallback py_compile). Una suite que se contesta en el chat — "respondé YES o
// NO", "devolvé sólo JSON" — no produce archivos, así que quedaba 0/0 y la tabla
// mostraba un guion como si hubiera fallado. Las tareas YA traen su evaluador de
// texto; sólo faltaba usarlo.
void AppControllerTests::benchmarkScoresChatAnswersWhenAgentWritesNoFiles()
{
    const QVariantList tasks{
        QVariantMap{{"id", "reasoning_logic"},
                    {"prompt", "All A are B. All B are C. Is all A are C? Answer YES or NO, then explain in one sentence."}},
        QVariantMap{{"id", "math_arithmetic"},
                    {"prompt", "Calculate: (17 * 23) + (456 / 8) - 12. Show each step. Give the final numeric answer."}},
        // Las de velocidad no se puntúan: sólo miden TPS.
        QVariantMap{{"id", "speed_short"},
                    {"prompt", "Write a Python function to check if a number is prime."}},
    };
    auto msg = [](const char *role, const QString &content) {
        return QVariant(QVariantMap{{"role", role}, {"content", content}});
    };

    // Una respuesta correcta y una incorrecta, sin un solo archivo de por medio.
    QVariantList messages{
        msg("user", tasks.at(0).toMap().value("prompt").toString()),
        msg("assistant", QStringLiteral("YES, porque la relación es transitiva.")),
        msg("user", tasks.at(1).toMap().value("prompt").toString()),
        msg("assistant", QStringLiteral("El resultado es 999.")),
        msg("user", tasks.at(2).toMap().value("prompt").toString()),
        msg("assistant", QStringLiteral("def is_prime(n): ...")),
    };
    const QVariantMap scored =
        AppController::scoreBenchTextResponsesForTest(QStringLiteral("short"), tasks, messages);
    QCOMPARE(scored.value("total").toInt(), 2);   // speed_short NO cuenta
    QCOMPARE(scored.value("score").toInt(), 1);   // sólo la de lógica pasa
    const QVariantList rows = scored.value("rows").toList();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).toMap().value("taskId").toString(), QStringLiteral("reasoning_logic"));
    QVERIFY(rows.at(0).toMap().value("passed").toBool());
    QVERIFY(!rows.at(1).toMap().value("passed").toBool());

    // El agente puede intercalar mensajes de tool antes de contestar.
    QVariantList withTools{
        msg("user", tasks.at(1).toMap().value("prompt").toString()),
        msg("tool", QStringLiteral("{\"result\": 436}")),
        msg("assistant", QString()),                       // vacío: se saltea
        msg("assistant", QStringLiteral("La cuenta da 436.")),
    };
    const QVariantMap viaTools = AppController::scoreBenchTextResponsesForTest(
        QStringLiteral("short"), QVariantList{tasks.at(1)}, withTools);
    QCOMPARE(viaTools.value("total").toInt(), 1);
    QCOMPARE(viaTools.value("score").toInt(), 1);

    // Una tarea que nunca llegó a correr (timeout) no se cuenta como fallada:
    // sumaría un 0/1 que no refleja la calidad del modelo.
    const QVariantMap corte = AppController::scoreBenchTextResponsesForTest(
        QStringLiteral("short"), tasks, QVariantList{});
    QCOMPARE(corte.value("total").toInt(), 0);

    // Un puntaje de calidad parcial NO es una corrida fallada: 3/5 en una suite de
    // preguntas es el resultado. Si eso marcara failed, la tabla taparía el score
    // con un badge rojo y se dispararían reparaciones inútiles.
    const QVariantList soloTexto{
        QVariantMap{{"type", "text"}, {"taskId", "a"}, {"passed", true}},
        QVariantMap{{"type", "text"}, {"taskId", "b"}, {"passed", false}},
    };
    QVERIFY(!AppController::benchHardCriteriaFailed(soloTexto));
    // Pero un archivo que el agente no dejó sí es un fallo de ejecución.
    QVariantList conArchivo = soloTexto;
    conArchivo.append(QVariantMap{{"type", "file"}, {"taskId", "c"}, {"passed", false}});
    QVERIFY(AppController::benchHardCriteriaFailed(conArchivo));
    QVariantList archivoOk{QVariantMap{{"type", "file"}, {"taskId", "c"}, {"passed", true}}};
    QVERIFY(!AppController::benchHardCriteriaFailed(archivoOk));
    QVERIFY(!AppController::benchHardCriteriaFailed(QVariantList{}));

    // Con criterios declarativos propios NO se duplica el puntaje.
    QVariantList conAcceptance{
        QVariantMap{{"id", "reasoning_logic"},
                    {"prompt", tasks.at(0).toMap().value("prompt")},
                    {"acceptance", QVariantMap{{"files", QVariantList{"x.py"}}}}},
    };
    const QVariantMap noDup = AppController::scoreBenchTextResponsesForTest(
        QStringLiteral("short"), conAcceptance, messages);
    QCOMPARE(noDup.value("total").toInt(), 0);
}

void AppControllerTests::benchmarkRestartErrorsAreInfrastructure()
{
    QVERIFY(AppController::benchmarkErrorIsInfrastructureForTest(
        QStringLiteral("[error: el servidor o backend se reinició durante la respuesta; el turno fue interrumpido.]")));
    QVERIFY(AppController::benchmarkErrorIsInfrastructureForTest(
        QStringLiteral("connection closed by peer")));
    QVERIFY(!AppController::benchmarkErrorIsInfrastructureForTest(
        QStringLiteral("Fallaron criterios de aceptacion.")));
}

void AppControllerTests::benchmarkBusyTurnIsRetryableNotInfrastructure()
{
    QVERIFY(AppController::benchmarkTurnBusyForTest(
        QStringLiteral("Hay un turno en curso.")));
    QVERIFY(AppController::benchmarkTurnBusyForTest(
        QStringLiteral("No se puede editar con un turno en curso.")));
    QVERIFY(!AppController::benchmarkTurnBusyForTest(
        QStringLiteral("connection closed by peer")));
    QVERIFY(!AppController::benchmarkErrorIsInfrastructureForTest(
        QStringLiteral("Hay un turno en curso.")));
}

void AppControllerTests::benchmarkRepairPrefillIsNotStagnation()
{
    QVERIFY(!AppController::benchmarkRepairStagnationCheckForTest(true, false));
    QVERIFY(!AppController::benchmarkRepairStagnationCheckForTest(true, true));
    QVERIFY(!AppController::benchmarkRepairStagnationCheckForTest(false, true));
    QVERIFY(AppController::benchmarkRepairStagnationCheckForTest(false, false));
}

void AppControllerTests::benchmarkMemoryPressureIsRecognized()
{
    QVERIFY(AppController::benchmarkLogIndicatesOutOfMemoryForTest(
        QStringLiteral("cudaMalloc failed: out of memory")));
    QVERIFY(AppController::benchmarkLogIndicatesOutOfMemoryForTest(
        QStringLiteral("Memoria insuficiente (OOM) al cargar el modelo.")));
    QVERIFY(AppController::benchmarkLogIndicatesOutOfMemoryForTest(
        QStringLiteral("unable to allocate CUDA buffer")));
    QVERIFY(!AppController::benchmarkLogIndicatesOutOfMemoryForTest(
        QStringLiteral("server ready - model loaded")));
    QVERIFY(!AppController::benchmarkLogIndicatesOutOfMemoryForTest(
        QStringLiteral("El agente quedó pensando durante el prefill.")));
}

void AppControllerTests::benchmarkUsesMaxVramBeforeReducingPressure()
{
    const QStringList base{
        QStringLiteral("--fit"), QStringLiteral("off"),
        QStringLiteral("--n-gpu-layers"), QStringLiteral("999"),
        QStringLiteral("--tensor-split"), QStringLiteral("1,0"),
        QStringLiteral("-ot"), QStringLiteral("experts=CUDA1,other=CPU"),
        QStringLiteral("--n-cpu-moe"), QStringLiteral("32"),
        QStringLiteral("--ctx-size"), QStringLiteral("131072"),
        QStringLiteral("--batch-size"), QStringLiteral("4096"),
        QStringLiteral("--ubatch-size"), QStringLiteral("1024")};

    const QStringList maxArgs = AppController::benchmarkMemoryPolicyArgsForTest(base, 0);
    QCOMPARE(maxArgs.at(maxArgs.indexOf(QStringLiteral("--fit")) + 1),
             QStringLiteral("on"));
    QCOMPARE(maxArgs.at(maxArgs.indexOf(QStringLiteral("--fit-target")) + 1),
             QStringLiteral("128"));
    QVERIFY(!maxArgs.contains(QStringLiteral("--n-cpu-moe")));
    QVERIFY(!maxArgs.contains(QStringLiteral("-ot")));
    QVERIFY(!maxArgs.contains(QStringLiteral("--n-gpu-layers")));
    QVERIFY(!maxArgs.contains(QStringLiteral("--tensor-split")));

    const QStringList stableArgs = AppController::benchmarkMemoryPolicyArgsForTest(base, 3);
    QCOMPARE(stableArgs.at(stableArgs.indexOf(QStringLiteral("--fit-target")) + 1),
             QStringLiteral("1024"));
    QVERIFY(!stableArgs.contains(QStringLiteral("--n-cpu-moe")));
    QVERIFY(!stableArgs.contains(QStringLiteral("-ot")));
    QVERIFY(!stableArgs.contains(QStringLiteral("--n-gpu-layers")));
    QVERIFY(!stableArgs.contains(QStringLiteral("--tensor-split")));

    const QStringList lastResort = AppController::benchmarkMemoryPolicyArgsForTest(base, 5);
    QVERIFY(lastResort.at(lastResort.indexOf(QStringLiteral("--ctx-size")) + 1).toInt() < 131072);
    QVERIFY(lastResort.at(lastResort.indexOf(QStringLiteral("--batch-size")) + 1).toInt() < 4096);
    QVERIFY(lastResort.at(lastResort.indexOf(QStringLiteral("--ubatch-size")) + 1).toInt() < 1024);
    QVERIFY(!lastResort.contains(QStringLiteral("--n-gpu-layers")));
    QVERIFY(!lastResort.contains(QStringLiteral("--tensor-split")));
}

void AppControllerTests::benchmarkPreservesScoreAfterTransportTail()
{
    QVERIFY(AppController::benchmarkTransportAfterEvaluationForTest(8, 8, true));
    QVERIFY(AppController::benchmarkTransportAfterEvaluationForTest(9, 8, true));
    QVERIFY(!AppController::benchmarkTransportAfterEvaluationForTest(7, 8, true));
    QVERIFY(!AppController::benchmarkTransportAfterEvaluationForTest(8, 8, false));
    QVERIFY(!AppController::benchmarkTransportAfterEvaluationForTest(0, 8, true));
}

void AppControllerTests::benchmarkGateRejectsBrokenOrStaleHe0()
{
    const QVariantMap valid{
        {"benchmarkName", "HumanEval (1 ítems)"},
        {"profileConfigFingerprint", "fp-current"},
        {"qualityScore", 1}, {"qualityTotal", 1},
        {"failed", false}, {"failureKind", "none"},
        {"invalid", false}, {"timedOut", false},
        {"transportAfterEvaluation", false}};
    QVERIFY(AppController::benchmarkResultPassesGateForTest(
        valid, QStringLiteral("he0"), QStringLiteral("fp-current")));

    QVariantMap stale = valid;
    stale[QStringLiteral("profileConfigFingerprint")] = QStringLiteral("fp-old");
    QVERIFY(!AppController::benchmarkResultPassesGateForTest(
        stale, QStringLiteral("he0"), QStringLiteral("fp-current")));

    QVariantMap legacy = valid;
    legacy.remove(QStringLiteral("profileConfigFingerprint"));
    QVERIFY(!AppController::benchmarkResultPassesGateForTest(
        legacy, QStringLiteral("he0"), QStringLiteral("fp-current")));

    QVariantMap broken = valid;
    broken[QStringLiteral("failed")] = true;
    broken[QStringLiteral("failureKind")] = QStringLiteral("infrastructure");
    broken[QStringLiteral("transportAfterEvaluation")] = true;
    QVERIFY(!AppController::benchmarkResultPassesGateForTest(
        broken, QStringLiteral("he0"), QStringLiteral("fp-current")));
}

void AppControllerTests::benchmarkGateAcceptsValidHe20QualityResult()
{
    const QVariantMap partial{
        {"benchmarkName", "HumanEval (20 ítems)"},
        {"profileConfigFingerprint", "fp-current"},
        {"qualityScore", 19}, {"qualityTotal", 20},
        {"failed", true}, {"failureKind", "quality"},
        {"invalid", false}, {"timedOut", false},
        {"transportAfterEvaluation", false}};
    QVERIFY(AppController::benchmarkResultPassesGateForTest(
        partial, QStringLiteral("he20"), QStringLiteral("fp-current")));

    QVariantMap transport = partial;
    transport[QStringLiteral("failureKind")] = QStringLiteral("infrastructure");
    transport[QStringLiteral("transportAfterEvaluation")] = true;
    QVERIFY(!AppController::benchmarkResultPassesGateForTest(
        transport, QStringLiteral("he20"), QStringLiteral("fp-current")));
}

void AppControllerTests::benchmarkCoverageClassifiesStages()
{
    const QString fp = QStringLiteral("fp-current");
    QVariantMap he0{{"benchmarkName", "HumanEval (1 ítems)"},
                    {"profileConfigFingerprint", fp}, {"qualityScore", 1},
                    {"qualityTotal", 1}, {"failureKind", "none"}};
    QVariantMap he20{{"benchmarkName", "HumanEval (20 ítems)"},
                     {"profileConfigFingerprint", fp}, {"qualityScore", 18},
                     {"qualityTotal", 20}, {"failureKind", "quality"}};
    QVariantMap bcb{{"benchmarkName", "BigCodeBench (8 ítems)"},
                    {"profileConfigFingerprint", fp}, {"qualityScore", 6},
                    {"qualityTotal", 8}, {"failureKind", "quality"}};

    QCOMPARE(AppController::benchmarkStageCoverageStateForTest(he0, "he0", fp),
             QStringLiteral("valid"));
    QCOMPARE(AppController::benchmarkStageCoverageStateForTest(he20, "he20", fp),
             QStringLiteral("valid"));
    QCOMPARE(AppController::benchmarkStageCoverageStateForTest(bcb, "bcb", fp),
             QStringLiteral("valid"));

    QVariantMap timeout = he20;
    timeout[QStringLiteral("timedOut")] = true;
    timeout[QStringLiteral("failureKind")] = QStringLiteral("timeout");
    QCOMPARE(AppController::benchmarkStageCoverageStateForTest(timeout, "he20", fp),
             QStringLiteral("infra-timeout"));
    QCOMPARE(AppController::benchmarkStageCoverageStateForTest({}, "bcb", fp),
             QStringLiteral("pending"));
    QCOMPARE(AppController::benchmarkStageCoverageStateForTest(he0, "he0", "fp-old"),
             QStringLiteral("pending"));
}

void AppControllerTests::benchmarkCustomStageClassification()
{
    QCOMPARE(AppController::customBenchmarkStageForTest(
        QStringLiteral("HumanEval (1 ítems)"), 1), QStringLiteral("he0"));
    QCOMPARE(AppController::customBenchmarkStageForTest(
        QStringLiteral("HumanEval (20 ítems)"), 20), QStringLiteral("he20"));
    QCOMPARE(AppController::customBenchmarkStageForTest(
        QStringLiteral("BCB · suite custom"), 8), QStringLiteral("bcb"));
    QCOMPARE(AppController::customBenchmarkStageForTest(
        QStringLiteral("Mi benchmark"), 8), QString());
}

void AppControllerTests::benchmarkRankingAggregatesLatestStages()
{
    QVariantList rows;
    auto add = [&](const QString &profileId, const QString &target,
                   const QString &agentId, const QString &agentName,
                   const QString &benchmarkName, qint64 timestamp,
                   int score, int total, double elapsed, double tps) {
        rows.append(QVariantMap{
            {QStringLiteral("profileId"), profileId},
            {QStringLiteral("profileName"), profileId + QStringLiteral(" · pasada 1/1")},
            {QStringLiteral("target"), target},
            {QStringLiteral("agentProfileId"), agentId},
            {QStringLiteral("agentProfileName"), agentName},
            {QStringLiteral("thinkingEnabled"), false},
            {QStringLiteral("benchmarkName"), benchmarkName},
            {QStringLiteral("timestamp"), timestamp},
            {QStringLiteral("qualityScore"), score},
            {QStringLiteral("qualityTotal"), total},
            {QStringLiteral("elapsedSec"), elapsed},
            {QStringLiteral("avgTps"), tps},
            {QStringLiteral("ramMb"), 100.0},
            {QStringLiteral("vramMb"), 200.0},
            {QStringLiteral("failed"), false},
            {QStringLiteral("failureKind"), QStringLiteral("none")}
        });
    };

    add(QStringLiteral("profile-a"), QStringLiteral("agent"), QStringLiteral("agent-1"),
        QStringLiteral("Agente"), QStringLiteral("HumanEval (1 ítems)"), 1, 0, 1, 90.0, 10.0);
    add(QStringLiteral("profile-a"), QStringLiteral("agent"), QStringLiteral("agent-1"),
        QStringLiteral("Agente"), QStringLiteral("HumanEval (1 ítems)"), 2, 1, 1, 80.0, 11.0);
    add(QStringLiteral("profile-a"), QStringLiteral("agent"), QStringLiteral("agent-1"),
        QStringLiteral("Agente"), QStringLiteral("HumanEval (20 ítems)"), 3, 18, 20, 300.0, 9.0);
    add(QStringLiteral("profile-a"), QStringLiteral("agent"), QStringLiteral("agent-1"),
        QStringLiteral("Agente"), QStringLiteral("BigCodeBench-Hard (8 ítems)"), 4, 4, 8, 500.0, 8.0);
    // The same launch with another target/harness is a separate ranking row.
    add(QStringLiteral("profile-a"), QStringLiteral("model"), QString(), QString(),
        QStringLiteral("HumanEval (1 ítems)"), 5, 1, 1, 40.0, 20.0);

    const QVariantList ranking = AppController::benchmarkRankingForTest(rows);
    QCOMPARE(ranking.size(), 2);
    QVariantMap agentRow;
    for (const QVariant &value : ranking) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("target")).toString() == QStringLiteral("agent")) {
            agentRow = row;
            break;
        }
    }
    QVERIFY(!agentRow.isEmpty());
    QCOMPARE(agentRow.value(QStringLiteral("profileName")).toString(), QStringLiteral("profile-a"));
    QCOMPARE(agentRow.value(QStringLiteral("he0Score")).toInt(), 1);
    QCOMPARE(agentRow.value(QStringLiteral("he20Score")).toInt(), 18);
    QCOMPARE(agentRow.value(QStringLiteral("bcbScore")).toInt(), 4);
    QCOMPARE(agentRow.value(QStringLiteral("stageCount")).toInt(), 3);
    QVERIFY(agentRow.value(QStringLiteral("complete")).toBool());
    QCOMPARE(agentRow.value(QStringLiteral("he0Tps")).toDouble(), 11.0);
}

void AppControllerTests::benchmarkDocumentRowsParseScores()
{
    const QString markdown = QStringLiteral(
        "# 2026-08-18 — import\n"
        "| ID | Perfil | Agente | HE0 | HE20 | BCB | Tiempo HE0 | Tiempo HE20 | Tiempo BCB | TPS HE0 | TPS HE20 | TPS BCB | VRAM total | Estado |\n"
        "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|\n"
        "| `profile-a` | Perfil A | chat | 1/1 | 18/20 | 4/8 | 11,288 s | 237,507 s | 1430,390 s | 39,22 | 60,50 | 65,21 | 24.569 MB | BCB calidad |\n"
        "| `profile-pending` | Pendiente | chat | Pendiente | Pendiente | Pendiente | — | — | — | — | — | — | — | Pendiente |\n");

    const QVariantList rows = AppController::benchmarkDocumentRowsForTest(
        markdown, QStringLiteral("docs/benchmark-results.md"));
    QCOMPARE(rows.size(), 3);

    QVariantMap he0;
    QVariantMap bcb;
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("benchmarkName")).toString()
                == QStringLiteral("HumanEval (1 ítems)"))
            he0 = row;
        if (row.value(QStringLiteral("benchmarkName")).toString()
                == QStringLiteral("BigCodeBench-Hard (8 ítems)"))
            bcb = row;
    }
    QVERIFY(!he0.isEmpty());
    QVERIFY(!bcb.isEmpty());
    QCOMPARE(he0.value(QStringLiteral("qualityScore")).toInt(), 1);
    QCOMPARE(he0.value(QStringLiteral("qualityTotal")).toInt(), 1);
    QCOMPARE(he0.value(QStringLiteral("elapsedSec")).toDouble(), 11.288);
    QCOMPARE(he0.value(QStringLiteral("avgTps")).toDouble(), 39.22);
    QCOMPARE(he0.value(QStringLiteral("vramMb")).toDouble(), 24569.0);
    QCOMPARE(bcb.value(QStringLiteral("qualityScore")).toInt(), 4);
    QCOMPARE(bcb.value(QStringLiteral("qualityTotal")).toInt(), 8);
    QVERIFY(!bcb.value(QStringLiteral("failed")).toBool());
    QVERIFY(bcb.value(QStringLiteral("importedFromDocs")).toBool());
    QVERIFY(bcb.value(QStringLiteral("timestamp")).toLongLong() > 0);
}

void AppControllerTests::benchmarkUsesOneArtifactPerTask()
{
    QCOMPARE(AppController::benchmarkTaskArtifactNameForTest(QStringLiteral("HumanEval/0")),
             QStringLiteral("solution_HumanEval_0.py"));

    QTemporaryDir ws;
    QVERIFY(ws.isValid());
    const QString addFile = QStringLiteral("solution_HumanEval_0.py");
    const QString subFile = QStringLiteral("solution_HumanEval_1.py");
    auto write = [&](const QString &name, const QByteArray &content) {
        QFile f(ws.filePath(name));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return false;
        return f.write(content) == content.size();
    };
    QVERIFY(write(addFile, "def add(a, b):\n    return a + b\n"));
    QVERIFY(write(subFile, "def subtract(a, b):\n    return a - b\n"));

    const QVariantMap addAcceptance{
        {QStringLiteral("graderType"), QStringLiteral("code_tests")},
        {QStringLiteral("entryPoint"), QStringLiteral("add")},
        {QStringLiteral("preamble"), QStringLiteral("def add(a, b):\n")},
        {QStringLiteral("tests"), QStringLiteral(
            "\ndef check(candidate):\n    assert candidate(2, 3) == 5\n\ncheck(add)\n")}};
    const QVariantMap subAcceptance{
        {QStringLiteral("graderType"), QStringLiteral("code_tests")},
        {QStringLiteral("entryPoint"), QStringLiteral("subtract")},
        {QStringLiteral("preamble"), QStringLiteral("def subtract(a, b):\n")},
        {QStringLiteral("tests"), QStringLiteral(
            "\ndef check(candidate):\n    assert candidate(7, 2) == 5\n\ncheck(subtract)\n")}};
    const QVariantList tasks{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("HumanEval/0")},
                    {QStringLiteral("artifactFile"), addFile},
                    {QStringLiteral("acceptance"), addAcceptance}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("HumanEval/1")},
                    {QStringLiteral("artifactFile"), subFile},
                    {QStringLiteral("acceptance"), subAcceptance}}};
    const QStringList files{addFile, subFile};

    QVariantMap score = AppController::scoreAgentBenchmarkAcceptanceForTest(
        ws.path(), QString(), tasks, files);
    QCOMPARE(score.value(QStringLiteral("score")).toInt(), 2);
    QCOMPARE(score.value(QStringLiteral("total")).toInt(), 2);

    // Aunque ambas soluciones existan, no se pueden acreditar desde el archivo
    // de la otra tarea: ése era el bug que ocultaba las sobrescrituras.
    QVERIFY(write(addFile, "def subtract(a, b):\n    return a - b\n"));
    QVERIFY(write(subFile, "def add(a, b):\n    return a + b\n"));
    score = AppController::scoreAgentBenchmarkAcceptanceForTest(
        ws.path(), QString(), tasks, files);
    QCOMPARE(score.value(QStringLiteral("score")).toInt(), 0);
    QCOMPARE(score.value(QStringLiteral("total")).toInt(), 2);

    // BCB models occasionally emit the unambiguous Bench/Benchmark spelling
    // typo. It must remain gradeable without allowing another task's artifact
    // to satisfy this task.
    const QString aliasFile = QStringLiteral("solution_BigCodeBenchmark_928.py");
    QVERIFY(write(aliasFile, "def task_func(word):\n    return {}\n"));
    const QVariantMap aliasAcceptance{
        {QStringLiteral("graderType"), QStringLiteral("code_tests")},
        {QStringLiteral("entryPoint"), QStringLiteral("task_func")},
        {QStringLiteral("preamble"), QStringLiteral("def task_func(word):\n")},
        {QStringLiteral("tests"), QStringLiteral(
            "\ndef check(candidate):\n    assert candidate('x') == {}\n\ncheck(task_func)\n")}};
    const QVariantList aliasTasks{
        QVariantMap{{QStringLiteral("id"), QStringLiteral("BigCodeBench/928")},
                    {QStringLiteral("artifactFile"), QStringLiteral("solution_BigCodeBench_928.py")},
                    {QStringLiteral("acceptance"), aliasAcceptance}}};
    score = AppController::scoreAgentBenchmarkAcceptanceForTest(
        ws.path(), QString(), aliasTasks, QStringList{aliasFile});
    QCOMPARE(score.value(QStringLiteral("score")).toInt(), 1);
    QCOMPARE(score.value(QStringLiteral("total")).toInt(), 1);
}

void AppControllerTests::benchmarkStreamingCountsSnapshotsOnce()
{
    QString previous;
    QCOMPARE(AppController::benchmarkStreamingDeltaForTest(&previous, QStringLiteral("abc")), 3);
    QCOMPARE(AppController::benchmarkStreamingDeltaForTest(&previous, QStringLiteral("abcdef")), 3);
    QCOMPARE(AppController::benchmarkStreamingDeltaForTest(&previous, QStringLiteral("abcdef")), 0);
    // Un backend que emite chunks, no snapshots, sigue sumando el chunk completo.
    QCOMPARE(AppController::benchmarkStreamingDeltaForTest(&previous, QStringLiteral("ghi")), 3);
}

void AppControllerTests::benchmarkBest25ClassifiesExclusiveSpeedTiers()
{
    QVariantList rows;
    qint64 timestamp = 1;
    auto add = [&](const QString &id, double tps, int score = 1) {
        rows.append(QVariantMap{
            {QStringLiteral("profileId"), id},
            {QStringLiteral("profileName"), id + QStringLiteral(" · pasada 1/1")},
            {QStringLiteral("target"), QStringLiteral("agent")},
            {QStringLiteral("benchmarkName"), QStringLiteral("HumanEval (1 ítems)")},
            {QStringLiteral("failed"), false},
            {QStringLiteral("failureKind"), QStringLiteral("none")},
            {QStringLiteral("avgTps"), tps},
            {QStringLiteral("qualityScore"), score},
            {QStringLiteral("qualityTotal"), 1},
            {QStringLiteral("timestamp"), timestamp++},
        });
    };
    for (int i = 0; i < 12; ++i) add(QStringLiteral("fast-%1").arg(i), 61.0 + i);
    for (int i = 0; i < 12; ++i) add(QStringLiteral("balanced-%1").arg(i), 41.0 + i);
    for (int i = 0; i < 7; ++i) add(QStringLiteral("quality-%1").arg(i), 6.0 + i);
    add(QStringLiteral("excluded-60"), 60.0);
    add(QStringLiteral("excluded-40"), 40.0);
    add(QStringLiteral("excluded-5"), 5.0);

    const QVariantList best = AppController::benchmarkBest25ForTest(rows);
    QCOMPARE(best.size(), 25);
    int fast = 0, balanced = 0, quality = 0;
    for (const QVariant &value : best) {
        const QVariantMap row = value.toMap();
        const QString category = row.value(QStringLiteral("best25Category")).toString();
        const double tps = row.value(QStringLiteral("avgTps")).toDouble();
        if (category == QStringLiteral("Fast")) { ++fast; QVERIFY(tps > 60.0); }
        if (category == QStringLiteral("Balanced")) { ++balanced; QVERIFY(tps > 40.0 && tps <= 60.0); }
        if (category == QStringLiteral("Quality")) { ++quality; QVERIFY(tps > 5.0 && tps <= 40.0); }
    }
    QCOMPARE(fast, 10);
    QCOMPARE(balanced, 10);
    QCOMPARE(quality, 5);
}

void AppControllerTests::concurrencyBenchmarkSettingsClampBounds()
{
    const QVariantMap low = AppController::concurrencyBenchmarkSettingsForTest(
        -4, -2, 0, 0);
    QCOMPARE(low.value(QStringLiteral("minSlots")).toInt(), 1);
    QCOMPARE(low.value(QStringLiteral("maxSlots")).toInt(), 1);
    QCOMPARE(low.value(QStringLiteral("requests")).toInt(), 2);
    QCOMPARE(low.value(QStringLiteral("maxTokens")).toInt(), 1);

    const QVariantMap high = AppController::concurrencyBenchmarkSettingsForTest(
        99, 2, 99, 99999);
    QCOMPARE(high.value(QStringLiteral("minSlots")).toInt(), 16);
    QCOMPARE(high.value(QStringLiteral("maxSlots")).toInt(), 16);
    QCOMPARE(high.value(QStringLiteral("requests")).toInt(), 32);
    QCOMPARE(high.value(QStringLiteral("maxTokens")).toInt(), 4096);
}

void AppControllerTests::benchmarkBest25IncludesValidRowsWithoutTps()
{
    const QVariantList rows{
        QVariantMap{{QStringLiteral("profileId"), QStringLiteral("sys-48-dsv4-nospec")},
                    {QStringLiteral("profileName"), QStringLiteral("DeepSeek")},
                    {QStringLiteral("target"), QStringLiteral("agent")},
                    {QStringLiteral("benchmarkName"), QStringLiteral("HumanEval (1 ítems)")},
                    {QStringLiteral("failed"), false},
                    {QStringLiteral("failureKind"), QStringLiteral("none")},
                    {QStringLiteral("avgTps"), 0.0},
                    {QStringLiteral("qualityScore"), 1},
                    {QStringLiteral("qualityTotal"), 1},
                    {QStringLiteral("timestamp"), 1}}};
    const QVariantList best = AppController::benchmarkBest25ForTest(rows);
    QCOMPARE(best.size(), 1);
    const QVariantMap row = best.first().toMap();
    QCOMPARE(row.value(QStringLiteral("best25Category")).toString(), QStringLiteral("Quality"));
    QVERIFY(row.value(QStringLiteral("best25TpsPending")).toBool());
}

void AppControllerTests::parseGpuInventoryCsvParses()
{
    const QVariantList gpus = AppController::parseGpuInventoryCsv(
        QStringLiteral("0, NVIDIA RTX 3090, 24576, 552.22\n"
                       "1, NVIDIA RTX 3060, 12288, 552.22\n"));
    QCOMPARE(gpus.size(), 2);
    QCOMPARE(gpus.at(0).toMap().value(QStringLiteral("index")).toInt(), 0);
    QCOMPARE(gpus.at(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("NVIDIA RTX 3090"));
    QCOMPARE(gpus.at(1).toMap().value(QStringLiteral("totalMb")).toDouble(), 12288.0);
}

void AppControllerTests::benchmarkBestModelosSpeedCapsProfilesPerGguf()
{
    QVariantList rows;
    for (int i = 0; i < 11; ++i) {
        rows.append(QVariantMap{
            {QStringLiteral("profileId"), QStringLiteral("qwen-%1").arg(i)},
            {QStringLiteral("profileName"), QStringLiteral("Qwen %1").arg(i)},
            {QStringLiteral("ggufKey"), QStringLiteral("qwen.gguf")},
            {QStringLiteral("ggufName"), QStringLiteral("qwen.gguf")},
            {QStringLiteral("avgTps"), 100.0 - i},
            {QStringLiteral("best25QualityRatio"), 1.0},
        });
    }
    rows.append(QVariantMap{
        {QStringLiteral("profileId"), QStringLiteral("gemma")},
        {QStringLiteral("profileName"), QStringLiteral("Gemma")},
        {QStringLiteral("ggufKey"), QStringLiteral("gemma.gguf")},
        {QStringLiteral("ggufName"), QStringLiteral("gemma.gguf")},
        {QStringLiteral("avgTps"), 50.0},
        {QStringLiteral("best25QualityRatio"), 1.0},
    });

    const QVariantList best = AppController::benchmarkBestModelosSpeedForTest(rows);
    QCOMPARE(best.size(), 11);
    int qwen = 0;
    for (const QVariant &value : best)
        if (value.toMap().value(QStringLiteral("ggufKey")).toString() == QStringLiteral("qwen.gguf"))
            ++qwen;
    QCOMPARE(qwen, 10);
}

void AppControllerTests::benchmarkHumanEval20CandidatesUseTopThreeAndBestControls()
{
    QVariantList speed;
    for (int rank = 1; rank <= 4; ++rank) {
        speed.append(QVariantMap{
            {QStringLiteral("profileId"), QStringLiteral("qwen-%1").arg(rank)},
            {QStringLiteral("ggufKey"), QStringLiteral("qwen.gguf")},
            {QStringLiteral("bestModelosSpeedRank"), rank}});
        speed.append(QVariantMap{
            {QStringLiteral("profileId"), QStringLiteral("gemma-%1").arg(rank)},
            {QStringLiteral("ggufKey"), QStringLiteral("gemma.gguf")},
            {QStringLiteral("bestModelosSpeedRank"), rank}});
    }
    const QVariantList controls = {
        QVariantMap{{QStringLiteral("profileId"), QStringLiteral("qwen-1")}}, // duplicado
        QVariantMap{{QStringLiteral("profileId"), QStringLiteral("kat-best")},
                    {QStringLiteral("humanEval20Control"), true}},
        QVariantMap{{QStringLiteral("profileId"), QStringLiteral("qwen-bench")},
                    {QStringLiteral("benchmarkControl"), true}}
    };

    const QVariantList selected =
        AppController::benchmarkHumanEval20CandidatesForTest(speed, controls);
    QCOMPARE(selected.size(), 8); // 3 Qwen + 3 Gemma + control BEST + candidato BENCH
    QSet<QString> ids;
    for (const QVariant &value : selected)
        ids.insert(value.toMap().value(QStringLiteral("profileId")).toString());
    QVERIFY(ids.contains(QStringLiteral("qwen-1")));
    QVERIFY(ids.contains(QStringLiteral("qwen-3")));
    QVERIFY(!ids.contains(QStringLiteral("qwen-4")));
    QVERIFY(ids.contains(QStringLiteral("gemma-3")));
    QVERIFY(!ids.contains(QStringLiteral("gemma-4")));
    QVERIFY(ids.contains(QStringLiteral("kat-best")));
    QVERIFY(ids.contains(QStringLiteral("qwen-bench")));
    QVERIFY(selected.last().toMap().value(QStringLiteral("humanEval20Control")).toBool());
}

void AppControllerTests::benchmarkBestModelosQualityUsesTwentyItemResults()
{
    QVariantList speed = {
        QVariantMap{{QStringLiteral("profileId"), QStringLiteral("p1")},
                    {QStringLiteral("profileName"), QStringLiteral("P1")},
                    {QStringLiteral("ggufName"), QStringLiteral("same.gguf")},
                    {QStringLiteral("ggufKey"), QStringLiteral("same.gguf")},
                    {QStringLiteral("best25Category"), QStringLiteral("Fast")}},
        QVariantMap{{QStringLiteral("profileId"), QStringLiteral("p2")},
                    {QStringLiteral("profileName"), QStringLiteral("P2")},
                    {QStringLiteral("ggufName"), QStringLiteral("same.gguf")},
                    {QStringLiteral("ggufKey"), QStringLiteral("same.gguf")},
                    {QStringLiteral("best25Category"), QStringLiteral("Balanced")}},
        QVariantMap{{QStringLiteral("profileId"), QStringLiteral("p4")},
                    {QStringLiteral("profileName"), QStringLiteral("P4")},
                    {QStringLiteral("ggufName"), QStringLiteral("same.gguf")},
                    {QStringLiteral("ggufKey"), QStringLiteral("same.gguf")},
                    {QStringLiteral("best25Category"), QStringLiteral("Fast")}},
        QVariantMap{{QStringLiteral("profileId"), QStringLiteral("p5")},
                    {QStringLiteral("profileName"), QStringLiteral("P5")},
                    {QStringLiteral("ggufName"), QStringLiteral("same.gguf")},
                    {QStringLiteral("ggufKey"), QStringLiteral("same.gguf")},
                    {QStringLiteral("best25Category"), QStringLiteral("Fast")}},
        QVariantMap{{QStringLiteral("profileId"), QStringLiteral("p3")},
                    {QStringLiteral("profileName"), QStringLiteral("P3")},
                    {QStringLiteral("ggufName"), QStringLiteral("other.gguf")},
                    {QStringLiteral("ggufKey"), QStringLiteral("other.gguf")},
                    {QStringLiteral("best25Category"), QStringLiteral("Quality")}}
    };
    QVariantList results = {
        QVariantMap{{QStringLiteral("target"), QStringLiteral("agent")},
                    {QStringLiteral("benchmarkName"), QStringLiteral("HumanEval (20 ítems)")},
                    {QStringLiteral("profileId"), QStringLiteral("p1")},
                    {QStringLiteral("failed"), true},
                    {QStringLiteral("failureKind"), QStringLiteral("quality")},
                    {QStringLiteral("qualityScore"), 18}, {QStringLiteral("qualityTotal"), 20},
                    {QStringLiteral("firstAttemptScore"), 15}, {QStringLiteral("timeToFirstAttempt"), 30.0},
                    {QStringLiteral("timestamp"), 1LL}},
        QVariantMap{{QStringLiteral("target"), QStringLiteral("agent")},
                    {QStringLiteral("benchmarkName"), QStringLiteral("HumanEval (20 ítems)")},
                    {QStringLiteral("failureKind"), QStringLiteral("none")},
                    {QStringLiteral("profileId"), QStringLiteral("p2")},
                    {QStringLiteral("qualityScore"), 20}, {QStringLiteral("qualityTotal"), 20},
                    {QStringLiteral("firstAttemptScore"), 20}, {QStringLiteral("timeToFirstAttempt"), 40.0},
                    {QStringLiteral("avgTps"), 20.0}, {QStringLiteral("totalTime"), 40.0},
                    {QStringLiteral("timestamp"), 2LL}},
        QVariantMap{{QStringLiteral("target"), QStringLiteral("agent")},
                    {QStringLiteral("benchmarkName"), QStringLiteral("HumanEval (20 ítems)")},
                    {QStringLiteral("failureKind"), QStringLiteral("none")},
                    {QStringLiteral("profileId"), QStringLiteral("p4")},
                    {QStringLiteral("qualityScore"), 20}, {QStringLiteral("qualityTotal"), 20},
                    {QStringLiteral("firstAttemptScore"), 20}, {QStringLiteral("timeToFirstAttempt"), 30.0},
                    {QStringLiteral("avgTps"), 50.0}, {QStringLiteral("totalTime"), 30.0},
                    {QStringLiteral("timestamp"), 4LL}},
        QVariantMap{{QStringLiteral("target"), QStringLiteral("agent")},
                    {QStringLiteral("benchmarkName"), QStringLiteral("HumanEval (20 ítems)")},
                    {QStringLiteral("failureKind"), QStringLiteral("none")},
                    {QStringLiteral("profileId"), QStringLiteral("p5")},
                    {QStringLiteral("qualityScore"), 20}, {QStringLiteral("qualityTotal"), 20},
                    {QStringLiteral("firstAttemptScore"), 20}, {QStringLiteral("timeToFirstAttempt"), 20.0},
                    {QStringLiteral("avgTps"), 50.0}, {QStringLiteral("totalTime"), 10.0},
                    {QStringLiteral("timestamp"), 5LL}},
        QVariantMap{{QStringLiteral("target"), QStringLiteral("agent")},
                    {QStringLiteral("benchmarkName"), QStringLiteral("HumanEval (20 ítems)")},
                    {QStringLiteral("failureKind"), QStringLiteral("none")},
                    {QStringLiteral("profileId"), QStringLiteral("p3")},
                    {QStringLiteral("qualityScore"), 19}, {QStringLiteral("qualityTotal"), 20},
                    {QStringLiteral("firstAttemptScore"), 19}, {QStringLiteral("timeToFirstAttempt"), 25.0},
                    {QStringLiteral("timestamp"), 3LL}}
    };
    const QVariantList best = AppController::benchmarkBestModelosQualityForTest(results, speed);
    QCOMPARE(best.size(), 5);
    QCOMPARE(best.at(0).toMap().value(QStringLiteral("profileId")).toString(), QStringLiteral("p5"));
    QCOMPARE(best.at(1).toMap().value(QStringLiteral("profileId")).toString(), QStringLiteral("p4"));
    QCOMPARE(best.at(2).toMap().value(QStringLiteral("profileId")).toString(), QStringLiteral("p2"));
    QCOMPARE(best.at(3).toMap().value(QStringLiteral("profileId")).toString(), QStringLiteral("p3"));
    QCOMPARE(best.at(4).toMap().value(QStringLiteral("profileId")).toString(), QStringLiteral("p1"));
}

// Si el server ya está sirviendo el perfil que se va a benchmarkear, el modelo ya
// está en VRAM y descargarlo para recargar lo mismo son minutos por pasada.
void AppControllerTests::benchmarkReusesServerAlreadyLoadedWithSameProfile()
{
    const QString a = QStringLiteral("sys-ultraq-dsv4-0731-iq3s-48gb");
    const QString b = QStringLiteral("sys-ultraq-dsv4-0731-iq3s");
    // Mismo perfil, corriendo y listo → reusar.
    QVERIFY(AppController::benchmarkCanReuseServer(a, a, true, true));
    // Otro perfil: puede compartir el .gguf pero diferir en ctx/KV/batch/offload,
    // así que el server cargado no sirve para medirlo.
    QVERIFY(!AppController::benchmarkCanReuseServer(b, a, true, true));
    // Cargando todavía, o sin server, o sin perfil activo → arrancar normal.
    QVERIFY(!AppController::benchmarkCanReuseServer(a, a, true, false));
    QVERIFY(!AppController::benchmarkCanReuseServer(a, a, false, true));
    QVERIFY(!AppController::benchmarkCanReuseServer(QString(), a, true, true));
    QVERIFY(!AppController::benchmarkCanReuseServer(a, QString(), true, true));
}

// Regresión "16GB trabado en Iniciando agente": tras un swap/restart de server
// puede quedar un agente vivo. El ready-branch NO debe relanzarlo, pero SÍ bajar
// m_agentStarting; si no, el popup "Iniciando agente" queda trabado para siempre.
void AppControllerTests::pendingAgentClearsStartingWhenAlreadyRunning()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});          // agente ya corriendo (sobrevivió al swap)
    app.setTestAgentBackend(fake);
    QVERIFY(app.agentRunning());

    // Estado previo: startServerAndAgent dejó el arranque del agente pendiente
    // y prendió el flag "Iniciando agente".
    app.setPendingAutoAgentForTest(QStringLiteral("sys-vram-16"));
    QVERIFY(app.agentStartingFlagForTest());

    // Server queda listo → dispara el ready-branch.
    app.triggerPendingAgentForTest();

    // No se relanza (mismo agente sigue vivo) pero el flag baja y el pending se
    // consume: nada queda trabado.
    QVERIFY(!app.agentStartingFlagForTest());
    QVERIFY(app.pendingAutoAgentForTest().isEmpty());
    QVERIFY(app.agentRunning());
}

void AppControllerTests::taskRunTimelineNormalizesToolCards()
{
    const QVariantList messages{
        QVariantMap{{QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), QStringLiteral("objetivo")}},
        QVariantMap{{QStringLiteral("role"), QStringLiteral("toolcall")},
                    {QStringLiteral("name"), QStringLiteral("desktop_click")},
                    {QStringLiteral("command"), QStringLiteral("click password=secret")},
                    {QStringLiteral("output"), QStringLiteral("ok")},
                    {QStringLiteral("ok"), true},
                    {QStringLiteral("createdAt"), 1234},
                    {QStringLiteral("elapsedMs"), 42},
                    {QStringLiteral("imagePath"), QStringLiteral("C:/tmp/preview.jpg")},
                    {QStringLiteral("beforeImagePath"), QStringLiteral("C:/tmp/before.jpg")},
                    {QStringLiteral("arguments"), QStringLiteral("{\"password\":\"secret\",\"x\":1}")}},
        QVariantMap{{QStringLiteral("role"), QStringLiteral("toolcall")},
                    {QStringLiteral("name"), QStringLiteral("browser_snapshot")},
                    {QStringLiteral("typing"), true},
                    {QStringLiteral("ok"), true}},
        QVariantMap{{QStringLiteral("role"), QStringLiteral("diff")},
                    {QStringLiteral("path"), QStringLiteral("src/main.cpp")},
                    {QStringLiteral("diff"), QStringLiteral("+changed")},
                    {QStringLiteral("ok"), true}}
    };
    const QVariantList timeline = AppController::taskRunTimelineFromMessagesForTest(messages);
    QCOMPARE(timeline.size(), 3);
    QCOMPARE(timeline.at(0).toMap().value(QStringLiteral("n")).toInt(), 1);
    QCOMPARE(timeline.at(0).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("ok"));
    QVERIFY(timeline.at(0).toMap().value(QStringLiteral("imageSource")).toString()
                .startsWith(QStringLiteral("file:")));
    QVERIFY(!timeline.at(0).toMap().value(QStringLiteral("detail")).toString()
                .contains(QStringLiteral("secret")));
    QVERIFY(!timeline.at(0).toMap().value(QStringLiteral("arguments")).toString()
                .contains(QStringLiteral("secret")));
    QVERIFY(timeline.at(0).toMap().value(QStringLiteral("beforeImageSource")).toString()
                .startsWith(QStringLiteral("file:")));
    QCOMPARE(timeline.at(1).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("running"));
    QCOMPARE(timeline.at(2).toMap().value(QStringLiteral("tool")).toString(),
             QStringLiteral("file_change"));
}

void AppControllerTests::earlyFailureRecordedInHistory()
{
    AppController app;
    // Sin agente: canRunTask() es false → runTask falla en el gating ANTES de
    // arrancar el cuerpo. Antes esto no dejaba rastro; ahora debe quedar una
    // corrida con estado error en el historial del Proceso.
    const QString id = makeLoopTask(app, QStringLiteral("Falla temprana"), 3);
    QSignalSpy fin(&app, &AppController::taskRunFinished);

    app.runTask(id);

    QVERIFY(!fin.isEmpty());
    QCOMPARE(fin.takeFirst().at(2).toString(), QStringLiteral("error"));

    const QVariantList hist = app.runHistory(id);
    QCOMPARE(hist.size(), 1);
    QCOMPARE(hist.first().toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("error"));
}

void AppControllerTests::workflowTaskPausesApprovesAndPersistsSnapshot()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});
    app.setTestAgentBackend(fake);
    const QVariantMap workflow{
        {"schemaVersion", 1}, {"entry", "work"},
        {"steps", QVariantMap{
            {"work", QVariantMap{{"type", "agent"}, {"prompt", "hacé el paso"}, {"next", "gate"}}},
            {"gate", QVariantMap{{"type", "approval"}, {"prompt", "¿continuar?"}, {"accept", "done"}}},
            {"done", QVariantMap{{"type", "finish"}}}}}};
    const QString id = app.taskStore()->save({}, {
        {"name", "Workflow test"}, {"description", "local"},
        {"executionMode", "auto"}, {"workflow", workflow}});
    QSignalSpy finished(&app, &AppController::taskRunFinished);

    app.runTaskBodyForTest(id);
    QTRY_VERIFY_WITH_TIMEOUT(!app.workflowApproval().isEmpty(), 2000);
    QCOMPARE(app.runningTaskPhase(), QStringLiteral("aprobación"));
    QCOMPARE(app.workflowApproval().value("stepId").toString(), QStringLiteral("gate"));
    const QVariantMap paused = app.taskStore()->get(id).value("workflowState").toMap();
    QCOMPARE(paused.value("status").toString(), QStringLiteral("waiting_approval"));

    app.approveTaskWorkflow(QStringLiteral("accept"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 2000);
    QCOMPARE(finished.first().at(2).toString(), QStringLiteral("ok"));
    const QVariantList history = app.runHistory(id);
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.first().toMap().value("workflowState").toMap()
                 .value("status").toString(), QStringLiteral("completed"));
}

void AppControllerTests::workflowDirectToolCompletesWithoutModelTurn()
{
    AppController app;
    auto *fake = new FakeAgentBackend(&app);
    fake->start(AgentContext{});
    app.setTestAgentBackend(fake);
    const QVariantMap workflow{{"schemaVersion", 1}, {"entry", "list"},
        {"steps", QVariantMap{
            {"list", QVariantMap{{"type", "tool"}, {"tool", "list_dir"},
                                  {"arguments", QVariantMap{{"path", "."}}}, {"next", "done"}}},
            {"done", QVariantMap{{"type", "finish"}}}}}};
    const QString id = app.taskStore()->save({}, {{"name", "Direct tool"},
        {"description", "local"}, {"executionMode", "auto"}, {"workflow", workflow}});
    QSignalSpy finished(&app, &AppController::taskRunFinished);
    app.runTaskBodyForTest(id);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 2000);
    QCOMPARE(finished.first().at(2).toString(), QStringLiteral("ok"));
    QCOMPARE(fake->bodyRuns(), 0);
}

void AppControllerTests::workflowValidationIsAvailableToVisualEditor()
{
    AppController app;
    const QVariantMap valid{{"schemaVersion", 1}, {"entry", "start"},
        {"steps", QVariantMap{{"start", QVariantMap{{"type", "finish"}}}}}};
    QVERIFY(app.validateWorkflow(valid).isEmpty());
    QVariantMap broken = valid;
    broken["entry"] = QStringLiteral("missing");
    QVERIFY(app.validateWorkflow(broken).contains(QStringLiteral("entry")));
}

void AppControllerTests::engineeringCatalogIsExposedHeadless()
{
    AppController app;
    const QVariantList workflows = app.engineeringWorkflows();
    QCOMPARE(workflows.size(), 6);
    QVERIFY(app.engineeringSafetyProfiles().size() >= 4);
    for (const QVariant &value : workflows) {
        const QVariantMap workflow = value.toMap();
        QVERIFY(!workflow.value(QStringLiteral("id")).toString().isEmpty());
        QVERIFY(app.validateWorkflow(workflow).isEmpty());
    }
    QVERIFY(app.installEngineeringWorkflow(QStringLiteral("missing")).isEmpty());
}

void AppControllerTests::engineeringPresetInstallsPersistsAndRestores()
{
    const QStringList ids{QStringLiteral("investigate"), QStringLiteral("qa"),
                          QStringLiteral("document-audit"), QStringLiteral("review"),
                          QStringLiteral("autoprompt"),
                          QStringLiteral("release-check")};
    AppController first;
    QStringList installed;
    for (const QString &workflowId : ids) {
        const QString taskId = first.installEngineeringWorkflow(workflowId);
        QVERIFY2(!taskId.isEmpty(), qPrintable(workflowId));
        const QVariantMap task = first.taskStore()->get(taskId);
        QCOMPARE(task.value(QStringLiteral("workflow")).toMap()
                     .value(QStringLiteral("id")).toString(), workflowId);
        QVERIFY(!task.value(QStringLiteral("safetyProfile")).toString().isEmpty());
        QVERIFY(!task.value(QStringLiteral("approvalPolicy")).toString().isEmpty());
        installed.append(taskId);
    }

    // Una segunda instancia fuerza el camino real de carga desde tasks.json.
    AppController restored;
    for (const QString &taskId : installed) {
        const QVariantMap task = restored.taskStore()->get(taskId);
        QVERIFY2(!task.isEmpty(), qPrintable(taskId));
        QVERIFY(!task.value(QStringLiteral("workflow")).toMap().isEmpty());
        QVERIFY(!task.value(QStringLiteral("safetyProfile")).toString().isEmpty());
    }
}

void AppControllerTests::harnessAdapterNormalizesToLlamaAgent()
{
    // Política: todo perfil usa LlamaAgent. "none"/vacío/"opencode" → "llamaagent".
    QCOMPARE(AppController::normalizeHarnessAdapter(QString()), QStringLiteral("llamaagent"));
    QCOMPARE(AppController::normalizeHarnessAdapter(QStringLiteral("  ")), QStringLiteral("llamaagent"));
    QCOMPARE(AppController::normalizeHarnessAdapter(QStringLiteral("none")), QStringLiteral("llamaagent"));
    QCOMPARE(AppController::normalizeHarnessAdapter(QStringLiteral("opencode")), QStringLiteral("llamaagent"));
    // Respeta llamaagent y raw (modo Chat).
    QCOMPARE(AppController::normalizeHarnessAdapter(QStringLiteral("llamaagent")), QStringLiteral("llamaagent"));
    QCOMPARE(AppController::normalizeHarnessAdapter(QStringLiteral("raw")), QStringLiteral("raw"));
}

void AppControllerTests::benchmarkWorkspaceFingerprintIgnoresInternalAgentFiles()
{
    QVERIFY(AppController::benchmarkWorkspacePathIsInternalForTest(QStringLiteral(".llamacode/agent_events.jsonl")));
    QVERIFY(AppController::benchmarkWorkspacePathIsInternalForTest(QStringLiteral(".llamacode\\agent_events.jsonl")));
    QVERIFY(!AppController::benchmarkWorkspacePathIsInternalForTest(QStringLiteral("solution_BigCodeBench_928.py")));
}

// Niveles de agente = escalera de presupuesto de contexto. Ejercita los 5 presets
// con la traducción REAL (applyAgentProfileCaps) sobre un LlamaAgentBackend con 37
// tools MCP inyectadas, y mide el contexto efectivo (system prompt + tool schemas)
// por nivel. Garantías:
//   - escalera monótona: Chat < Básico < Intermedio < Avanzado < Máximo.
//   - Chat es el único con MCP off → sin tools mcp__ en el schema.
//   - Máximo ("*") NO arrastra las opt-in puras (honey/antiBias): regresión que se
//     coló al sumar antiBias al catálogo; acá queda pinchada.
void AppControllerTests::agentLevels_contextBudgetLadder()
{
    AppController app;

    // 37 tools MCP sintéticas (14 filesystem + 23 playwright), como en producción.
    QVariantList mcp;
    auto mkTool = [](const QString &server, const QString &name) {
        return QVariant(QVariantMap{
            {"server", server}, {"name", name},
            {"description", QStringLiteral("Tool %1 del MCP %2.").arg(name, server)},
            {"schema", QStringLiteral("{\"type\":\"object\",\"properties\":"
                "{\"path\":{\"type\":\"string\"},\"opts\":{\"type\":\"object\"}}}")}});
    };
    for (int i = 0; i < 14; ++i) mcp << mkTool("filesystem", QStringLiteral("fs_%1").arg(i));
    for (int i = 0; i < 23; ++i) mcp << mkTool("playwright", QStringLiteral("pw_%1").arg(i));

    auto budgetFor = [&](const AgentProfile &ap, bool *hasMcp, QString *sysOut) {
        LlamaAgentBackend be;
        be.setMcpToolsForTest(mcp);
        app.applyAgentProfileCapsForTest(&be, ap);   // traducción REAL de la app
        const QString sys = be.systemPromptForTest();
        const QJsonArray tools = be.toolSchemasForTest();
        if (sysOut) *sysOut = sys;
        if (hasMcp) {
            *hasMcp = false;
            for (const QJsonValue &v : tools) {
                const QString n = v.toObject().value("function").toObject().value("name").toString();
                // Lazy discovery expone las meta-tools mcp_search_tools / mcp_call_tool
                // (prefijo "mcp_"); las tools crudas serían mcp__<server>__<tool>. Ambas
                // matchean "mcp_" → basta para detectar que MCP está inyectado.
                if (n.startsWith(QStringLiteral("mcp_"))) { *hasMcp = true; break; }
            }
        }
        const int toolBytes = QJsonDocument(tools).toJson(QJsonDocument::Compact).size();
        return sys.toUtf8().size() + toolBytes;
    };

    const QList<AgentProfile> ps = AgentProfile::systemPresets();
    auto byId = [&](const QString &id) {
        for (const AgentProfile &p : ps) if (p.id == id) return p;
        return AgentProfile{};
    };

    bool chatMcp = true, maxMcp = false;
    QString chatSys, maxSys;
    const int chat   = budgetFor(byId(QStringLiteral("agent-chat")),       &chatMcp, &chatSys);
    const int basico = budgetFor(byId(QStringLiteral("agent-basico")),     nullptr, nullptr);
    const int inter  = budgetFor(byId(QStringLiteral("agent-intermedio")), nullptr, nullptr);
    const int avanz  = budgetFor(byId(QStringLiteral("agent-avanzado")),   nullptr, nullptr);
    const int maximo = budgetFor(byId(QStringLiteral("agent-maximo")),     &maxMcp, &maxSys);

    qInfo() << "presupuesto por nivel (system+tools, bytes):";
    qInfo() << "  Chat liviano:" << chat;
    qInfo() << "  Básico:" << basico;
    qInfo() << "  Intermedio:" << inter;
    qInfo() << "  Avanzado:" << avanz;
    qInfo() << "  Máximo (+37 MCP):" << maximo;

    // Escalera monótona estricta.
    QVERIFY2(chat < basico,   qPrintable(QStringLiteral("Chat %1 !< Básico %2").arg(chat).arg(basico)));
    QVERIFY2(basico < inter,  qPrintable(QStringLiteral("Básico %1 !< Intermedio %2").arg(basico).arg(inter)));
    QVERIFY2(inter < avanz,   qPrintable(QStringLiteral("Intermedio %1 !< Avanzado %2").arg(inter).arg(avanz)));
    QVERIFY2(avanz < maximo,  qPrintable(QStringLiteral("Avanzado %1 !< Máximo %2").arg(avanz).arg(maximo)));

    // Chat: único sin MCP. Máximo: con MCP.
    QVERIFY2(!chatMcp, "Chat liviano no debería inyectar tools MCP");
    QVERIFY2(maxMcp, "Máximo debería inyectar tools MCP");

    // Opt-in puras NO entran por el sentinel "*" de Máximo.
    QVERIFY2(!maxSys.contains(QStringLiteral("ANTI-SESGO")),
             "Máximo arrastró antiBias por '*' (debe ser opt-in puro)");
    QVERIFY2(!maxSys.contains(QStringLiteral("FRUGALIDAD (Honey)")),
             "Máximo arrastró honey por '*' (debe ser opt-in puro)");
}

// binaryPin del bundle de perfiles de sistema: permite fijar UN perfil a un
// build concreto (substring de nombre/ruta) sin tocar el resto. Acá se ejercita
// la lectura del campo desde el bundle (vía override LLAMACODE_SYSTEM_PROFILES);
// el match contra el registro de binarios reusa el mismo loop que resolveSystemBinaryId.
void AppControllerTests::systemProfileBinaryPinReadsBundle()
{
    const QString bundle = m_tmp.filePath(QStringLiteral("sysprof_pin.json"));
    QFile f(bundle);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"([
      {"id":"pinned","binaryKind":"official","binaryPin":"b9842"},
      {"id":"nopin","binaryKind":"official"}
    ])");
    f.close();
    qputenv("LLAMACODE_SYSTEM_PROFILES", bundle.toLocal8Bit());

    AppController app;
    QCOMPARE(app.systemProfileBinaryPin(QStringLiteral("pinned")), QStringLiteral("b9842"));
    QVERIFY(app.systemProfileBinaryPin(QStringLiteral("nopin")).isEmpty());
    QVERIFY(app.systemProfileBinaryPin(QStringLiteral("unknown")).isEmpty());

    qunsetenv("LLAMACODE_SYSTEM_PROFILES");
}

void AppControllerTests::systemProfileMinimumBuildSelectsNewestCompatible()
{
    QCOMPARE(AppController::llamaCppBuildNumber(QStringLiteral("llama.cpp b10217")), 10217);
    QCOMPARE(AppController::llamaCppBuildNumber(QStringLiteral("build: 10221 (abc)")), 10221);
    QCOMPARE(AppController::llamaCppBuildNumber(QStringLiteral("unknown")), 0);

    const QString bundle = m_tmp.filePath(QStringLiteral("sysprof_min_build.json"));
    QFile f(bundle);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(R"([{"id":"deepseek","binaryKind":"official","minimumBinaryBuild":10217}])");
    f.close();
    qputenv("LLAMACODE_SYSTEM_PROFILES", bundle.toLocal8Bit());

    AppController app;
    while (app.binaryRegistry()->rowCount() > 0) {
        const QString id = app.binaryRegistry()->data(
            app.binaryRegistry()->index(0, 0), BinaryRegistry::IdRole).toString();
        QVERIFY(app.binaryRegistry()->remove(id));
    }
    auto addFake = [&](const QString &file, const QString &version) {
        const QString path = m_tmp.filePath(file);
        QFile exe(path);
        if (!exe.open(QIODevice::WriteOnly)) return QString();
        exe.write("fake"); exe.close();
        return app.binaryRegistry()->add(path, QStringLiteral("llama-server ") + version,
                                         QStringLiteral("official"), QStringLiteral("cuda"), version);
    };
    addFake(QStringLiteral("old.exe"), QStringLiteral("b10216"));
    addFake(QStringLiteral("compatible.exe"), QStringLiteral("b10217"));
    const QString newest = addFake(QStringLiteral("newest.exe"), QStringLiteral("b10221"));
    QVERIFY(!newest.isEmpty());
    QCOMPARE(app.systemProfileMinimumBinaryBuild(QStringLiteral("deepseek")), 10217);
    QCOMPARE(app.resolvedSystemBinaryForTest(QStringLiteral("deepseek")).value("id").toString(), newest);
    qunsetenv("LLAMACODE_SYSTEM_PROFILES");
}

void AppControllerTests::cpuSystemProfileRequiresCpuBinary()
{
    const QByteArray oldSystemProfiles = qgetenv("LLAMACODE_SYSTEM_PROFILES");
    const QString bundle = systemProfilesBundlePath();
    QVERIFY2(QFile::exists(bundle), qPrintable(bundle));
    qputenv("LLAMACODE_SYSTEM_PROFILES", QFile::encodeName(bundle));

    AppController app;
    while (app.binaryRegistry()->rowCount() > 0) {
        const QString id = app.binaryRegistry()
                               ->data(app.binaryRegistry()->index(0, 0), BinaryRegistry::IdRole)
                               .toString();
        QVERIFY(app.binaryRegistry()->remove(id));
    }

    const QString cudaPath = m_tmp.filePath(QStringLiteral("llama-cuda.exe"));
    QFile cuda(cudaPath);
    QVERIFY(cuda.open(QIODevice::WriteOnly));
    cuda.write("fake-cuda");
    cuda.close();
    const QString cudaId = app.binaryRegistry()->add(
        cudaPath, QStringLiteral("llama-server cuda b9045"),
        QStringLiteral("official"), QStringLiteral("cuda"), QStringLiteral("b9045"));
    QVERIFY(!cudaId.isEmpty());

    const QVariantMap cudaOnly = app.resolvedSystemBinaryForTest(QStringLiteral("sys-vram-0"));
    QVERIFY2(cudaOnly.value(QStringLiteral("id")).toString().isEmpty(),
             "El perfil 0GB CPU no debe caer a un binario CUDA si falta CPU.");

    const QString cpuPath = m_tmp.filePath(QStringLiteral("llama-cpu.exe"));
    QFile cpu(cpuPath);
    QVERIFY(cpu.open(QIODevice::WriteOnly));
    cpu.write("fake-cpu");
    cpu.close();
    const QString cpuId = app.binaryRegistry()->add(
        cpuPath, QStringLiteral("llama-server cpu"),
        QStringLiteral("official"), QStringLiteral("cpu"), QStringLiteral("cpu"));
    QVERIFY(!cpuId.isEmpty());

    const QVariantMap withCpu = app.resolvedSystemBinaryForTest(QStringLiteral("sys-vram-0"));
    QCOMPARE(withCpu.value(QStringLiteral("id")).toString(), cpuId);
    QCOMPARE(withCpu.value(QStringLiteral("backend")).toString(), QStringLiteral("cpu"));

    if (oldSystemProfiles.isEmpty())
        qunsetenv("LLAMACODE_SYSTEM_PROFILES");
    else
        qputenv("LLAMACODE_SYSTEM_PROFILES", oldSystemProfiles);
}

void AppControllerTests::doctorReportsStructureAndIssues()
{
    AppController app;
    const QVariantMap d = app.doctor();

    // Claves consolidadas presentes.
    for (const char *k : {"version", "binaries", "roots", "modelCount",
                          "hardware", "gitAvailable", "gateway", "server",
                          "issues", "ok"})
        QVERIFY2(d.contains(QString::fromLatin1(k)), k);

    QCOMPARE(d.value(QStringLiteral("version")).toString(), app.version());
    QVERIFY(d.value(QStringLiteral("issues")).toList().size() >= 0);
    // ok == (sin issues): coherencia interna del reporte.
    QCOMPARE(d.value(QStringLiteral("ok")).toBool(),
             d.value(QStringLiteral("issues")).toList().isEmpty());
    // Entorno de test limpio: sin binarios ni modelos → debe reportar issues.
    if (app.binaryRegistry()->count() == 0)
        QVERIFY(!d.value(QStringLiteral("ok")).toBool());
}

void AppControllerTests::hardwareRecommendationIsHeadless()
{
    AppController app;
    app.setHardwareSummaryForTest(48.0, 64.0, QStringLiteral("RTX 3090"), 48.0, 2);
    const QVariantMap summary = app.hardwareSummary();
    QVERIFY(summary.value(QStringLiteral("hardwareFingerprint")).toString().startsWith("hw-"));
    QCOMPARE(summary.value(QStringLiteral("recommendedSplitMode")).toString(), QStringLiteral("layer"));
    const QVariantMap recommendation = app.performanceRecommendation(QStringLiteral("balanced"));
    QCOMPARE(recommendation.value(QStringLiteral("splitMode")).toString(), QStringLiteral("layer"));
    QCOMPARE(recommendation.value(QStringLiteral("kvCache")).toString(), QStringLiteral("q8_0"));
}

void AppControllerTests::voiceGpuPlanIsExposedHeadless()
{
    AppController app;
    app.setHardwareSummaryForTest(36.0, 64.0, QStringLiteral("dual test"), 36.0, 2);
    const QVariantMap plan = app.voiceGpuPlan();
    QVERIFY(plan.value(QStringLiteral("enabled")).toBool());
    QCOMPARE(plan.value(QStringLiteral("voiceGpuIndex")).toInt(), 0);
    QCOMPARE(plan.value(QStringLiteral("modelGpuMask")).toString(), QStringLiteral("0,1"));
    QVERIFY(plan.value(QStringLiteral("modelAvailableGb")).toDouble() > 0.0);
}

void AppControllerTests::performanceMatrixIsHeadless()
{
    AppController app;
    app.setHardwareSummaryForTest(48.0, 64.0, QStringLiteral("RTX 3090"), 48.0, 2);
    const QVariantList candidates = app.performanceMatrixCandidates(QStringLiteral("decode"), false);
    QCOMPARE(candidates.size(), 6);
    for (const QVariant &value : candidates) {
        const QVariantMap row = value.toMap();
        QCOMPARE(row.value(QStringLiteral("status")).toString(), QStringLiteral("pending"));
        QCOMPARE(row.value(QStringLiteral("splitMode")).toString(), QStringLiteral("layer"));
    }
    QVariantList measured;
    QVariantMap sample = candidates.first().toMap();
    sample[QStringLiteral("performanceCandidate")] = candidates.first();
    sample[QStringLiteral("promptTps")] = 100.0;
    sample[QStringLiteral("generationTps")] = 30.0;
    sample[QStringLiteral("quality")] = 1.0;
    sample[QStringLiteral("stable")] = true;
    measured.append(sample);
    const QVariantList ranked = app.rankPerformanceMatrix(measured, QStringLiteral("decode"));
    QCOMPARE(ranked.first().toMap().value(QStringLiteral("rank")).toInt(), 1);
    QVERIFY(ranked.first().toMap().value(QStringLiteral("performanceScore")).toDouble() > 0.0);
    const QVariantMap annotated = app.annotatePerformanceMatrix(
        sample, candidates.first().toMap());
    QCOMPARE(annotated.value(QStringLiteral("measurementStatus")).toString(),
             QStringLiteral("measured"));
}

void AppControllerTests::devModeIsOptInAndRecordsPerformance()
{
    QSettings settings;
    settings.remove(QStringLiteral("app/devMode"));
    AppController app;

    QVERIFY(!app.devMode());
    app.recordPerformanceSample(QStringLiteral("normal_mode"));
    QVERIFY(app.performanceSnapshot().isEmpty());

    app.setDevMode(true);
    QVERIFY(app.devMode());
    QVERIFY(!app.performanceSnapshot().isEmpty());
    QVERIFY(app.performanceSnapshot().contains(QStringLiteral("rssMb"))
            || app.performanceSnapshot().contains(QStringLiteral("virtualMb")));
    QVERIFY(QFile::exists(app.performanceLogPath()));

    app.setDevMode(false);
    QVERIFY(!app.devMode());
    QVERIFY(app.performanceSnapshot().isEmpty());
    app.clearPerformanceLog();
    settings.remove(QStringLiteral("app/devMode"));
}

void AppControllerTests::importOllamaModelsIngestsStore()
{
    // Store de Ollama falso: manifest + blob presente.
    QTemporaryDir store;
    const QString digestHex(64, QLatin1Char('a'));
    const QString blob = store.path() + "/blobs/sha256-" + digestHex;
    QDir().mkpath(QFileInfo(blob).absolutePath());
    { QFile f(blob); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("GGUF-fake"); }
    const QString manifest = store.path()
        + "/manifests/registry.ollama.ai/library/phi3/mini";
    QDir().mkpath(QFileInfo(manifest).absolutePath());
    {
        QJsonObject layer{{"mediaType", "application/vnd.ollama.image.model"},
                          {"digest", "sha256:" + digestHex}, {"size", 9}};
        QJsonObject m{{"layers", QJsonArray{layer}}};
        QFile f(manifest); QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(m).toJson());
    }

    AppController app;
    const int before = app.modelCatalog()->count();
    const QString id = app.importOllamaModels(store.path());
    QVERIFY(!id.isEmpty());
    QCOMPARE(app.rootRegistry()->get(id).value("kind").toString(),
             QStringLiteral("ollama"));

    // add() escanea sincrónicamente en un thread; bombear hasta que aparezca.
    QSignalSpy spy(app.rootRegistry(), &ModelRootRegistry::scanFinished);
    if (app.modelCatalog()->count() == before)
        spy.wait(5000);
    QVERIFY(app.modelCatalog()->count() > before);

    // Ruta inexistente → sin ingesta.
    QVERIFY(app.importOllamaModels(QDir::temp().filePath("no-such-ollama-xyz")).isEmpty());
}

// El tuner nunca pisa el perfil original: clona a "Opti - <nombre>". Es un
// contrato visible al usuario, y re-optimizar no debe encadenar prefijos.
void AppControllerTests::tunerProfileNameUsesOptiPrefixWithoutChaining()
{
    QCOMPARE(AppController::optimizedProfileName(QStringLiteral("MAX-Q coding")),
             QStringLiteral("Opti - MAX-Q coding"));
    // Re-optimizar un perfil ya optimizado deja el nombre igual.
    QCOMPARE(AppController::optimizedProfileName(QStringLiteral("Opti - MAX-Q coding")),
             QStringLiteral("Opti - MAX-Q coding"));
}

void AppControllerTests::tunerGainPctNeedsBothLegs()
{
    QCOMPARE(AppController::tuneGainPct(120.0, 100.0), 20.0);
    QCOMPARE(AppController::tuneGainPct(80.0, 100.0), -20.0);
    // Sin baseline medido (-1 o 0) no se inventa una mejora.
    QCOMPARE(AppController::tuneGainPct(120.0, -1.0), 0.0);
    QCOMPARE(AppController::tuneGainPct(120.0, 0.0), 0.0);
    QCOMPARE(AppController::tuneGainPct(-1.0, 100.0), 0.0);
}

void AppControllerTests::isRemoteHostDetectsLanHosts()
{
    QVERIFY(!AppController::isRemoteHost(QStringLiteral("127.0.0.1")));
    QVERIFY(!AppController::isRemoteHost(QStringLiteral("localhost")));
    QVERIFY(!AppController::isRemoteHost(QStringLiteral("0.0.0.0")));
    QVERIFY(!AppController::isRemoteHost(QStringLiteral("::1")));
    QVERIFY(!AppController::isRemoteHost(QStringLiteral("")));

    QVERIFY(AppController::isRemoteHost(QStringLiteral("192.168.1.50")));
    QVERIFY(AppController::isRemoteHost(QStringLiteral("10.0.0.15")));
    QVERIFY(AppController::isRemoteHost(QStringLiteral("pc-potente.local")));
}

void AppControllerTests::isLoopbackCloudUrlRequiresLocalHttpEndpoint()
{
    QVERIFY(AppController::isLoopbackCloudUrl(QStringLiteral("http://127.0.0.1:8000")));
    QVERIFY(AppController::isLoopbackCloudUrl(QStringLiteral("https://localhost/v1")));
    QVERIFY(AppController::isLoopbackCloudUrl(QStringLiteral("http://[::1]:8001")));
    QVERIFY(!AppController::isLoopbackCloudUrl(QStringLiteral("http://192.168.1.50:8000")));
    QVERIFY(!AppController::isLoopbackCloudUrl(QStringLiteral("https://api.example.com")));
    QVERIFY(!AppController::isLoopbackCloudUrl(QStringLiteral("127.0.0.1:8000")));
}

QTEST_MAIN(AppControllerTests)
#include "test_appcontroller.moc"
