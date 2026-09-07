#pragma once
#include "core/BinaryRegistry.h"
#include "core/EngineCatalog.h"
#include "core/HardwareDiagnostics.h"
#include "core/PerformanceMatrix.h"
#include "core/ModelRootRegistry.h"
#include "core/ModelCatalog.h"
#include "core/profiles/ProfileManager.h"
#include "core/profiles/EffectiveProfileBuilder.h"
#include "core/profiles/ProfileHealthChecker.h"
#include "core/profiles/HarnessEngine.h"
#include "core/tasks/TaskStore.h"
#include "core/tasks/AutomationStore.h"
#include "core/tasks/RunHistoryStore.h"
#include "core/tasks/EvidenceBundle.h"
#include "core/downloads/DownloadHistoryStore.h"
#include "core/tasks/TaskScheduler.h"
#include "core/AuxiliaryJobScheduler.h"
#include "core/ModelRoleRegistry.h"
#include "core/assistant/AssistantRuntime.h"
#include "core/agents/AgentDefinitionStore.h"
#include "core/agents/TriggerManager.h"
#include "core/tasks/WorkflowRunner.h"
#include "core/tasks/EngineeringWorkflowCatalog.h"
#include "core/agent/IAgentBackend.h"
#include "core/agent/MasterCli.h"
#include "core/agent/ManagedAgentRunStore.h"
#include "core/agent/AgentRunStore.h"
#include "core/agent/AgentDeliverableStore.h"
#include "core/agent/AgentRoomStore.h"
#include "core/SecretStore.h"
#include "core/gateway/LlmGateway.h"
#include "core/voice/VoiceServerManager.h"
#include "core/voice/VoiceTypes.h"
#include "core/tuner/TunerWorker.h"
#include "core/automation/TeachSessionRecorder.h"
#include "core/data/DataLab.h"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QVariantMap>
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFile>
#include <QSettings>
#include <QUrl>
class QUdpSocket;

class QThread;
class QFileSystemWatcher;
class QWidget;
class AgentToolRunner;
class SubAgentRunner;

class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(BinaryRegistry*     binaryRegistry  READ binaryRegistry  CONSTANT)
    Q_PROPERTY(ModelRootRegistry*  rootRegistry    READ rootRegistry    CONSTANT)
    Q_PROPERTY(ModelCatalog*       modelCatalog    READ modelCatalog    CONSTANT)
    Q_PROPERTY(ProfileManager*     profileManager  READ profileManager  CONSTANT)
    Q_PROPERTY(TaskStore*          taskStore       READ taskStore       CONSTANT)
    Q_PROPERTY(AgentDefinitionStore* agentDefinitions READ agentDefinitions CONSTANT)
    Q_PROPERTY(TriggerManager* triggerManager READ triggerManager CONSTANT)
    Q_PROPERTY(AgentRoomStore* agentRoomStore READ agentRoomStore CONSTANT)
    Q_PROPERTY(ManagedAgentRunStore* managedAgentRunStore READ managedAgentRunStore CONSTANT)
    Q_PROPERTY(QVariantList nativeAgentRuns READ nativeAgentRuns NOTIFY nativeAgentRunsChanged)
    Q_PROPERTY(int nativeUncertainRunCount READ nativeUncertainRunCount NOTIFY nativeAgentRunsChanged)
    Q_PROPERTY(QString activeAgentDefinitionId READ activeAgentDefinitionId
               NOTIFY activeAgentDefinitionChanged)
    Q_PROPERTY(AutomationStore*    automationStore READ automationStore CONSTANT)
    Q_PROPERTY(DataLabStore*      dataLab         READ dataLab         CONSTANT)
    Q_PROPERTY(AuxiliaryJobScheduler* auxiliaryScheduler READ auxiliaryScheduler CONSTANT)
    Q_PROPERTY(ModelRoleRegistry* modelRoles READ modelRoles CONSTANT)
    Q_PROPERTY(QVariantList auxiliaryJobs READ auxiliaryJobs NOTIFY auxiliaryJobsChanged)
    Q_PROPERTY(AssistantRuntime* assistantRuntime READ assistantRuntime CONSTANT)
    Q_PROPERTY(bool tasksSchedulerEnabled READ tasksSchedulerEnabled WRITE setTasksSchedulerEnabled NOTIFY tasksSchedulerChanged)
    Q_PROPERTY(bool taskRunning READ taskRunning NOTIFY taskRunStateChanged)
    Q_PROPERTY(bool canRunTask READ canRunTask NOTIFY taskRunAvailabilityChanged)
    Q_PROPERTY(QString runningTaskId READ runningTaskId NOTIFY taskRunStateChanged)
    Q_PROPERTY(QString runningTaskName READ runningTaskName NOTIFY taskRunStateChanged)
    Q_PROPERTY(QString runningTaskPhase READ runningTaskPhase NOTIFY taskRunStateChanged)
    Q_PROPERTY(QVariantList taskRunTimeline READ taskRunTimeline NOTIFY taskRunTraceChanged)
    Q_PROPERTY(QVariantMap taskRunPreview READ taskRunPreview NOTIFY taskRunTraceChanged)
    Q_PROPERTY(bool taskLivePreviewEnabled READ taskLivePreviewEnabled
               WRITE setTaskLivePreviewEnabled NOTIFY taskLivePreviewChanged)
    Q_PROPERTY(bool taskPaused READ taskPaused NOTIFY taskRunStateChanged)
    Q_PROPERTY(QVariantMap runningWorkflowState READ runningWorkflowState NOTIFY taskRunStateChanged)
    Q_PROPERTY(QVariantMap workflowApproval READ workflowApproval NOTIFY taskRunStateChanged)
    Q_PROPERTY(bool taskAbRunning READ taskAbRunning NOTIFY taskAbChanged)
    Q_PROPERTY(QString taskAbStatus READ taskAbStatus NOTIFY taskAbChanged)
    Q_PROPERTY(QVariantList chatSessions    READ chatSessions    NOTIFY chatSessionsChanged)
    Q_PROPERTY(QVariantList chatMessages    READ chatMessages    NOTIFY chatMessagesChanged)
    Q_PROPERTY(QString      chatSessionId   READ chatSessionId   NOTIFY chatSessionsChanged)
    Q_PROPERTY(QString      chatSessionTitle READ chatSessionTitle NOTIFY chatSessionsChanged)
    Q_PROPERTY(bool         chatGenerating  READ chatGenerating  NOTIFY chatGeneratingChanged)
    Q_PROPERTY(bool         chatThinkingSupported READ chatThinkingSupported NOTIFY chatThinkingSupportedChanged)
    // Streaming incremental del chat: refresca sólo la burbuja activa, igual que Agente.
    Q_PROPERTY(int chatStreamingIndex READ chatStreamingIndex NOTIFY chatStreamingChanged)
    Q_PROPERTY(QString chatStreamingText READ chatStreamingText NOTIFY chatStreamingChanged)
    Q_PROPERTY(bool chatThinkingEnabled READ chatThinkingEnabled WRITE setChatThinkingEnabled NOTIFY chatThinkingChanged)
    Q_PROPERTY(bool chatPersonaDesigner READ chatPersonaDesigner WRITE setChatPersonaDesigner NOTIFY chatPersonaDesignerChanged)
    Q_PROPERTY(double chatTemperature READ chatTemperature WRITE setChatTemperature NOTIFY chatSamplingChanged)
    Q_PROPERTY(double chatTopP READ chatTopP WRITE setChatTopP NOTIFY chatSamplingChanged)
    Q_PROPERTY(int chatTopK READ chatTopK WRITE setChatTopK NOTIFY chatSamplingChanged)
    Q_PROPERTY(double chatMinP READ chatMinP WRITE setChatMinP NOTIFY chatSamplingChanged)
    Q_PROPERTY(double chatRepeatPenalty READ chatRepeatPenalty WRITE setChatRepeatPenalty NOTIFY chatSamplingChanged)
    Q_PROPERTY(bool thinkingEnabled READ thinkingEnabled WRITE setThinkingEnabled NOTIFY thinkingChanged)
    // Render de diagramas Mermaid en el chat (requiere sidecar mermaid-cli).
    Q_PROPERTY(bool mermaidEnabled READ mermaidEnabled WRITE setMermaidEnabled NOTIFY mermaidEnabledChanged)
    Q_PROPERTY(bool   serverRunning   READ serverRunning   NOTIFY serverRunningChanged)
    // Disponibilidad del backend para la UI: proceso local o perfil remoto/cloud
    // activo. Un cliente LAN no crea un QProcess local.
    Q_PROPERTY(bool   backendAvailable READ backendAvailable NOTIFY backendAvailableChanged)
    Q_PROPERTY(bool   serverStopping  READ serverStopping  NOTIFY serverRunningChanged)
    Q_PROPERTY(bool   serverReady     READ serverReady     NOTIFY serverReadyChanged)
    Q_PROPERTY(QString serverState    READ serverState     NOTIFY serverStateChanged)
    // Reinicio intencional al cambiar thinking: conserva la superficie actual
    // mientras el proceso baja, vuelve a cargar el modelo y el agente se reconecta.
    Q_PROPERTY(bool thinkingRestarting READ thinkingRestarting NOTIFY thinkingRestartingChanged)
    Q_PROPERTY(QVariantMap serverStats READ serverStats    NOTIFY serverStatsChanged)
    Q_PROPERTY(QString serverLog      READ serverLog       NOTIFY serverLogChanged)
    Q_PROPERTY(QString activeLaunchId READ activeLaunchId  NOTIFY activeLaunchIdChanged)
    Q_PROPERTY(QVariantMap effectiveProfile READ effectiveProfile NOTIFY effectiveProfileChanged)
    Q_PROPERTY(bool needsSetup READ needsSetup NOTIFY setupStateChanged)
    Q_PROPERTY(bool hasAnyBinary READ hasAnyBinary NOTIFY setupStateChanged)
    Q_PROPERTY(bool hasAnyModel  READ hasAnyModel  NOTIFY setupStateChanged)
    Q_PROPERTY(bool hasAnyLaunch READ hasAnyLaunch NOTIFY setupStateChanged)
    Q_PROPERTY(QString serverBaseUrl READ serverBaseUrl NOTIFY serverRunningChanged)
    // Capacidades del modelo activo. Vision = el server se lanzó con --mmproj.
    Q_PROPERTY(bool serverHasVision READ serverHasVision NOTIFY serverHasVisionChanged)
    // git instalado (requerido por subagents para aislar en worktrees).
    Q_PROPERTY(bool gitAvailable READ gitAvailable NOTIFY gitAvailableChanged)
    Q_PROPERTY(bool installingGit READ installingGit NOTIFY gitAvailableChanged)
    Q_PROPERTY(bool installingOfficialBinary READ installingOfficialBinary NOTIFY installingOfficialBinaryChanged)
    Q_PROPERTY(QString officialBinaryInstallStatus READ officialBinaryInstallStatus NOTIFY officialBinaryInstallStatusChanged)
    Q_PROPERTY(QString officialBinaryInstallLog READ officialBinaryInstallLog NOTIFY officialBinaryInstallLogChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(int langV READ langV NOTIFY languageChanged)
    Q_PROPERTY(bool agentRunning      READ agentRunning      NOTIFY agentRunningChanged)
    // Soporte de tool-calling del perfil activo: "supported"|"unsupported"|"unknown".
    // Derivado del cookbook (hf_models.json) + chat-template del GGUF (/props).
    Q_PROPERTY(QString activeProfileToolSupport READ activeProfileToolSupport NOTIFY activeProfileToolSupportChanged)
    Q_PROPERTY(QString agentLog       READ agentLog          NOTIFY agentLogChanged)
    Q_PROPERTY(bool agentStarting     READ agentStarting     NOTIFY agentStartingChanged)
    Q_PROPERTY(QString hybridStatus   READ hybridStatus      NOTIFY agentStartingChanged)
    Q_PROPERTY(QVariantList agentMessages  READ agentMessages  NOTIFY agentMessagesChanged)
    // Streaming incremental: índice del mensaje en streaming (-1 = ninguno) y su
    // texto en vivo. La UI override sólo esa burbuja sin re-bindear toda la lista.
    Q_PROPERTY(int agentStreamingIndex READ agentStreamingIndex NOTIFY agentStreamingChanged)
    Q_PROPERTY(QString agentStreamingText READ agentStreamingText NOTIFY agentStreamingChanged)
    Q_PROPERTY(QString personaStyleAnalysisStatus READ personaStyleAnalysisStatus NOTIFY personaStyleAnalysisChanged)
    Q_PROPERTY(QString personaStyleAnalysisError READ personaStyleAnalysisError NOTIFY personaStyleAnalysisChanged)
    // Mensajes encolados pendientes (modo "encolar"), agente y chat.
    Q_PROPERTY(int agentQueuedCount READ agentQueuedCount NOTIFY agentQueueChanged)
    Q_PROPERTY(int chatQueuedCount READ chatQueuedCount NOTIFY chatQueueChanged)
    Q_PROPERTY(QStringList agentQueuedMessages READ agentQueuedMessages NOTIFY agentQueueChanged)
    Q_PROPERTY(QStringList chatQueuedMessages READ chatQueuedMessages NOTIFY chatQueueChanged)
    Q_PROPERTY(QVariantList agentSessions  READ agentSessions  NOTIFY agentSessionsChanged)
    Q_PROPERTY(QString opencodeSessionId   READ opencodeSessionId   NOTIFY agentSessionsChanged)
    Q_PROPERTY(QString opencodeSessionTitle READ opencodeSessionTitle NOTIFY agentSessionsChanged)
    Q_PROPERTY(QVariantMap agentPendingTool READ agentPendingTool NOTIFY agentPendingToolChanged)
    Q_PROPERTY(QString agentApprovalMode READ agentApprovalMode WRITE setAgentApprovalMode NOTIFY agentApprovalModeChanged)
    Q_PROPERTY(bool agentThinkingEnabled READ agentThinkingEnabled WRITE setAgentThinkingEnabled NOTIFY agentThinkingChanged)
    Q_PROPERTY(QString activeAgentProfileId READ activeAgentProfileId WRITE setActiveAgentProfileId NOTIFY activeAgentProfileChanged)
    Q_PROPERTY(bool browserAutomationEnabled READ browserAutomationEnabled WRITE setBrowserAutomationEnabled NOTIFY browserAutomationChanged)
    Q_PROPERTY(QString browserMcpCommand READ browserMcpCommand WRITE setBrowserMcpCommand NOTIFY browserAutomationChanged)
    Q_PROPERTY(QString teachState READ teachState NOTIFY teachChanged)
    Q_PROPERTY(QString teachError READ teachError NOTIFY teachChanged)
    Q_PROPERTY(QVariantList teachTimeline READ teachTimeline NOTIFY teachChanged)
    Q_PROPERTY(QString agentTeacherUrl   READ agentTeacherUrl   WRITE setAgentTeacherUrl   NOTIFY agentTeacherChanged)
    Q_PROPERTY(QString agentTeacherModel READ agentTeacherModel WRITE setAgentTeacherModel NOTIFY agentTeacherChanged)
    Q_PROPERTY(QString agentTeacherKey   READ agentTeacherKey   WRITE setAgentTeacherKey   NOTIFY agentTeacherChanged)
    Q_PROPERTY(QString agentAuxiliaryUrl READ agentAuxiliaryUrl WRITE setAgentAuxiliaryUrl NOTIFY agentAuxiliaryChanged)
    Q_PROPERTY(QString agentAuxiliaryEmbeddingModel READ agentAuxiliaryEmbeddingModel WRITE setAgentAuxiliaryEmbeddingModel NOTIFY agentAuxiliaryChanged)
    Q_PROPERTY(QString agentAuxiliaryRerankModel READ agentAuxiliaryRerankModel WRITE setAgentAuxiliaryRerankModel NOTIFY agentAuxiliaryChanged)
    Q_PROPERTY(QString agentAuxiliaryKey READ agentAuxiliaryKey WRITE setAgentAuxiliaryKey NOTIFY agentAuxiliaryChanged)
    Q_PROPERTY(bool mailAutoSend READ mailAutoSend WRITE setMailAutoSend NOTIFY mailAutoSendChanged)
    Q_PROPERTY(bool hitlDestructive READ hitlDestructive WRITE setHitlDestructive NOTIFY hitlDestructiveChanged)
    Q_PROPERTY(bool desktopIndicatorVisible READ desktopIndicatorVisible WRITE setDesktopIndicatorVisible NOTIFY desktopIndicatorChanged)
    Q_PROPERTY(bool desktopAgentActive READ desktopAgentActive NOTIFY desktopIndicatorChanged)
    Q_PROPERTY(QString desktopAgentAction READ desktopAgentAction NOTIFY desktopIndicatorChanged)
    Q_PROPERTY(bool autoStartAgentOnLaunch READ autoStartAgentOnLaunch WRITE setAutoStartAgentOnLaunch NOTIFY autoStartAgentOnLaunchChanged)
    // Gateway (proxy Anthropic/OpenAI + auto-load por modelo).
    Q_PROPERTY(bool    gatewayEnabled  READ gatewayEnabled  WRITE setGatewayEnabled  NOTIFY gatewayChanged)
    Q_PROPERTY(bool    gatewayRunning  READ gatewayRunning  NOTIFY gatewayChanged)
    Q_PROPERTY(int     gatewayPort     READ gatewayPort     WRITE setGatewayPort     NOTIFY gatewayChanged)
    Q_PROPERTY(QString gatewayApiKey   READ gatewayApiKey   WRITE setGatewayApiKey   NOTIFY gatewayChanged)
    Q_PROPERTY(int     gatewayKeepN    READ gatewayKeepN    WRITE setGatewayKeepN    NOTIFY gatewayChanged)
    Q_PROPERTY(bool    gatewayAutoSwap READ gatewayAutoSwap WRITE setGatewayAutoSwap NOTIFY gatewayChanged)
    Q_PROPERTY(bool    gatewayLanEnabled READ gatewayLanEnabled WRITE setGatewayLanEnabled NOTIFY gatewayChanged)
    Q_PROPERTY(QVariantList lanServers READ lanServers NOTIFY lanServersChanged)
    Q_PROPERTY(bool lanDiscoveryActive READ lanDiscoveryActive NOTIFY lanServersChanged)
    // Idle auto-stop del server (libera VRAM tras N minutos sin uso; 0 = off).
    Q_PROPERTY(int     idleAutoStopMin READ idleAutoStopMin WRITE setIdleAutoStopMin NOTIFY idleAutoStopChanged)
    Q_PROPERTY(int agentContextUsed READ agentContextUsed NOTIFY agentContextChanged)
    Q_PROPERTY(int agentContextLimit READ agentContextLimit NOTIFY agentContextChanged)
    Q_PROPERTY(int agentContextTranscript READ agentContextTranscript NOTIFY agentContextChanged)
    Q_PROPERTY(qint64 agentContextPruned READ agentContextPruned NOTIFY agentContextChanged)
    Q_PROPERTY(int agentContextPruneEvents READ agentContextPruneEvents NOTIFY agentContextChanged)
    Q_PROPERTY(QString agentSystemPrompt READ agentSystemPrompt WRITE setAgentSystemPrompt NOTIFY agentTuningChanged)
    Q_PROPERTY(double agentTemperature READ agentTemperature WRITE setAgentTemperature NOTIFY agentTuningChanged)
    Q_PROPERTY(QString agentPermRules READ agentPermRules WRITE setAgentPermRules NOTIFY agentTuningChanged)
    Q_PROPERTY(QString activeAgentAdapter READ activeAgentAdapter NOTIFY agentRunningChanged)
    Q_PROPERTY(QString activeHarnessEngineId READ activeHarnessEngineId NOTIFY agentRunningChanged)
    Q_PROPERTY(bool agentInTerminal   READ agentInTerminal   NOTIFY agentRunningChanged)
    Q_PROPERTY(bool installingHarness READ installingHarness NOTIFY harnessStatusChanged)
    Q_PROPERTY(QString harnessInstallStatus READ harnessInstallStatus NOTIFY harnessStatusChanged)
    Q_PROPERTY(int harnessCheckV READ harnessCheckV NOTIFY harnessStatusChanged)
    Q_PROPERTY(bool benchmarkRunning READ benchmarkRunning NOTIFY benchmarkRunningChanged)
    Q_PROPERTY(int benchmarkProgress READ benchmarkProgress NOTIFY benchmarkProgressChanged)
    Q_PROPERTY(QString benchmarkStatus READ benchmarkStatus NOTIFY benchmarkStatusChanged)
    Q_PROPERTY(QVariantList benchmarkResults READ benchmarkResults NOTIFY benchmarkResultsChanged)
    Q_PROPERTY(QVariantList benchmarkCoverage READ benchmarkCoverage NOTIFY benchmarkResultsChanged)
    Q_PROPERTY(QVariantList benchmarkRanking READ benchmarkRanking NOTIFY benchmarkResultsChanged)
    Q_PROPERTY(QVariantList benchmarkBest25 READ benchmarkBest25 NOTIFY benchmarkResultsChanged)
    Q_PROPERTY(QVariantList benchmarkBestModelosSpeed READ benchmarkBestModelosSpeed NOTIFY benchmarkResultsChanged)
    Q_PROPERTY(QVariantList benchmarkHumanEval20Candidates READ benchmarkHumanEval20Candidates NOTIFY benchmarkResultsChanged)
    Q_PROPERTY(QVariantList benchmarkBestModelosQuality READ benchmarkBestModelosQuality NOTIFY benchmarkResultsChanged)
    Q_PROPERTY(QVariantList customBenchmarks READ customBenchmarks NOTIFY customBenchmarksChanged)
    Q_PROPERTY(bool autoTuneRunning READ autoTuneRunning NOTIFY autoTuneChanged)
    Q_PROPERTY(int autoTuneProgress READ autoTuneProgress NOTIFY autoTuneChanged)
    Q_PROPERTY(QString autoTuneStatus READ autoTuneStatus NOTIFY autoTuneChanged)
    // Propiedades (no Q_INVOKABLE) para que la sección Tuner se re-evalúe sola
    // en cada trial: un método invocable no dispara re-binding en QML.
    Q_PROPERTY(QVariantList autoTuneTrials READ autoTuneTrials NOTIFY autoTuneChanged)
    Q_PROPERTY(QVariantMap autoTuneResult READ autoTuneResult NOTIFY autoTuneChanged)
    Q_PROPERTY(bool researchRunning READ researchRunning NOTIFY researchChanged)
    Q_PROPERTY(int researchProgress READ researchProgress NOTIFY researchChanged)
    Q_PROPERTY(QString researchStatus READ researchStatus NOTIFY researchChanged)
    Q_PROPERTY(QVariantList researchReports READ researchReports NOTIFY researchReportsChanged)
    Q_PROPERTY(QVariantMap hardwareSummary READ hardwareSummary NOTIFY hardwareSummaryChanged)
    Q_PROPERTY(bool startupBusy READ startupBusy NOTIFY startupChanged)
    Q_PROPERTY(QString startupStatus READ startupStatus NOTIFY startupChanged)
    Q_PROPERTY(QVariantMap startupTimings READ startupTimings NOTIFY startupChanged)
    // El modo normal evita muestreo periódico y escrituras de telemetría. El
    // modo dev se activa explícitamente para investigar lentitud de la GUI.
    Q_PROPERTY(bool devMode READ devMode WRITE setDevMode NOTIFY devModeChanged)
    Q_PROPERTY(QVariantMap performanceSnapshot READ performanceSnapshot NOTIFY performanceChanged)
    Q_PROPERTY(QVariantList engineCatalog READ engineCatalog NOTIFY hardwareSummaryChanged)
    Q_PROPERTY(QVariantList modelRecommendations READ modelRecommendations NOTIFY modelRecommendationsChanged)
    Q_PROPERTY(bool modelDownloadRunning READ modelDownloadRunning NOTIFY modelDownloadChanged)
    Q_PROPERTY(int modelDownloadProgress READ modelDownloadProgress NOTIFY modelDownloadChanged)
    Q_PROPERTY(QString modelDownloadStatus READ modelDownloadStatus NOTIFY modelDownloadChanged)
    Q_PROPERTY(QVariantList modelDownloadQueue READ modelDownloadQueue NOTIFY modelDownloadChanged)
    Q_PROPERTY(QVariantList downloadHistory READ downloadHistory NOTIFY downloadHistoryChanged)
    // ── Modo Charla (voz-a-voz) ──
    Q_PROPERTY(QString voiceState READ voiceState NOTIFY voiceStateChanged)
    Q_PROPERTY(bool    voiceActive READ voiceActive NOTIFY voiceStateChanged)
    Q_PROPERTY(double  voiceLevel READ voiceLevel NOTIFY voiceLevelChanged)
    Q_PROPERTY(QString voiceError READ voiceError NOTIFY voiceStateChanged)
    Q_PROPERTY(QString voicePartial READ voicePartial NOTIFY voicePartialChanged)
    Q_PROPERTY(QVariantMap voiceLatencyStats READ voiceLatencyStats NOTIFY voiceLatencyStatsChanged)
    Q_PROPERTY(bool dictationActive READ dictationActive NOTIFY dictationChanged)
    Q_PROPERTY(QString dictationText READ dictationText NOTIFY dictationChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateCheckChanged)
    Q_PROPERTY(QVariantMap updateInfo READ updateInfo NOTIFY updateCheckChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    BinaryRegistry    *binaryRegistry()  { return &m_binaries; }
    ModelRootRegistry *rootRegistry()    { return &m_roots; }
    ModelCatalog      *modelCatalog()    { return &m_catalog; }
    ProfileManager    *profileManager()  { return &m_profiles; }
    TaskStore         *taskStore()       { return &m_tasks; }
    AgentDefinitionStore *agentDefinitions() { return &m_agentDefinitions; }
    TriggerManager *triggerManager() { return &m_triggerManager; }
    ManagedAgentRunStore *managedAgentRunStore() { return &m_managedAgentRuns; }
    QVariantList nativeAgentRuns() const { return m_nativeAgentRuns; }
    int nativeUncertainRunCount() const;
    Q_INVOKABLE QVariantMap agentDefinitionMetrics(const QString &agentId) const;
    QString activeAgentDefinitionId() const { return m_activeAgentDefinitionId; }
    Q_INVOKABLE bool activateAgentDefinition(const QString &agentId);
    AutomationStore   *automationStore() { return &m_automations; }
    DataLabStore       *dataLab()        { return &m_dataLab; }
    AuxiliaryJobScheduler *auxiliaryScheduler() const { return m_auxiliaryScheduler; }
    ModelRoleRegistry *modelRoles() { return &m_modelRoles; }
    AssistantRuntime *assistantRuntime() { return &m_assistantRuntime; }
    QVariantList auxiliaryJobs() const
    { return m_auxiliaryScheduler ? m_auxiliaryScheduler->snapshot() : QVariantList{}; }
    bool tasksSchedulerEnabled() const
    { return QSettings().value(QStringLiteral("tasks/schedulerEnabled"), false).toBool(); }
    void setTasksSchedulerEnabled(bool on);
    // Reconstruye el QFileSystemWatcher desde las Tasks con triggerType=fileWatch.
    // Idempotente; llamar tras cambios en las Tasks. Expuesto para tests.
    Q_INVOKABLE void rebuildTaskTriggers();
    Q_INVOKABLE QStringList watchedTriggerPaths() const;
    bool taskRunning() const { return !m_runningTaskId.isEmpty(); }
    bool canRunTask() const;
    QString runningTaskId() const { return m_runningTaskId; }
    QString runningTaskName() const { return m_runningTaskName; }
    QString runningTaskPhase() const { return m_runningTaskPhase; }
    QVariantList taskRunTimeline() const { return m_taskRunTimeline; }
    QVariantMap taskRunPreview() const { return m_taskRunPreview; }
    bool taskLivePreviewEnabled() const { return m_taskLivePreviewEnabled; }
    bool taskPaused() const { return m_taskPaused; }
    void setTaskLivePreviewEnabled(bool enabled);
    Q_INVOKABLE void pauseTask(bool paused = true);
    Q_INVOKABLE void stepTask();
    // Normaliza las tarjetas del backend a una traza segura y estable para QML,
    // historial y pruebas. No expone el razonamiento privado del modelo.
    static QVariantList taskRunTimelineFromMessagesForTest(const QVariantList &messages);
    QVariantMap runningWorkflowState() const;
    QVariantMap workflowApproval() const { return m_workflowApproval; }
    bool taskAbRunning() const { return !m_taskAbId.isEmpty(); }
    QString taskAbStatus() const { return m_taskAbStatus; }

    QVariantList chatSessions()     const { return m_chatSessions; }
    QVariantList chatMessages()     const { return m_chatMessages; }
    QString      chatSessionId()    const { return m_chatSessionId; }
    QString      chatSessionTitle() const { return m_chatSessionTitle; }
    bool         chatGenerating()   const { return m_chatGenerating; }
    bool         chatThinkingSupported() const { return m_chatThinkingSupported; }
    bool   backendAvailable() const;
    static bool backendAvailability(bool localServerRunning, bool activeRemoteProfile)
    {
        return localServerRunning || activeRemoteProfile;
    }
    static bool isRemoteHost(const QString &host) {
        const QString h = host.trimmed().toLower();
        return !h.isEmpty() && h != QLatin1String("127.0.0.1") && h != QLatin1String("localhost") && h != QLatin1String("::1") && h != QLatin1String("0.0.0.0");
    }
    static bool isLoopbackCloudUrl(const QString &url) {
        const QUrl parsed(url.trimmed());
        if (!parsed.isValid()
            || (parsed.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0
                && parsed.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0))
            return false;
        const QString host = parsed.host().trimmed().toLower();
        return host == QLatin1String("127.0.0.1")
            || host == QLatin1String("localhost")
            || host == QLatin1String("::1");
    }
    bool   serverRunning()   const { return (m_proc && m_proc->state() != QProcess::NotRunning) || m_remoteServerActive; }
    bool   serverStopping()  const { return m_serverStopping; }
    bool   serverReady()     const { return m_serverReady; }
    QString serverState()    const { return m_serverState; }
    bool thinkingRestarting() const { return m_thinkingRestarting; }
    QVariantMap serverStats() const { return m_serverStats; }
    QString serverLog()      const { return m_log; }
    QString activeLaunchId() const { return m_activeLaunchId; }
    QVariantMap effectiveProfile() const { return m_effectiveProfile; }
    bool needsSetup() const { return !hasAnyBinary() || !hasAnyModel() || !hasAnyLaunch(); }
    bool hasAnyBinary() const { return m_binaries.count() > 0; }
    bool hasAnyModel()  const { return m_catalog.count()  > 0; }
    bool hasAnyLaunch() const { return !m_profiles.launchProfilesForMenu().isEmpty(); }
    // Salud de perfiles: lista de issues (severity/entity/code/message/fix) de todos
    // los launches. Vacío = todo sano. Expuesto a QML y headless (ControlApi).
    Q_INVOKABLE QVariantList profileHealth();
    // Resumen para badges: {errors, warnings} (conteo de issues por severity).
    Q_INVOKABLE QVariantMap profileHealthSummary();
    QString serverBaseUrl() const {
        if (!m_activeLaunchId.isEmpty()) {
            const auto ctx = const_cast<AppController*>(this)->buildContext(m_activeLaunchId);
            if (ctx.backend.isCloud()) {
                return ctx.backend.cloudBaseUrl.trimmed();
            }
            if (isRemoteHost(ctx.backend.host)) {
                const int port = ctx.backend.port > 0 ? ctx.backend.port : 8080;
                return QStringLiteral("http://%1:%2").arg(ctx.backend.host.trimmed()).arg(port);
            }
        }
        const QStringList args = m_effectiveProfile.value("effectiveArgs").toStringList();
        QString host = QStringLiteral("127.0.0.1");
        int port = 8080;
        for (int i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == "--host") host = args[i + 1];
            else if (args[i] == "--port") port = args[i + 1].toInt();
        }
        return QStringLiteral("http://%1:%2").arg(host).arg(port);
    }
    bool installingOfficialBinary() const { return m_installingOfficialBinary; }
    QString officialBinaryInstallStatus() const { return m_officialBinaryInstallStatus; }
    QString officialBinaryInstallLog() const { return m_officialBinaryInstallLog; }
    QString language() const { return m_language; }
    void setLanguage(const QString &lang);
    int langV() const { return 0; }
    bool agentRunning() const {
        if (m_agentBackend && m_agentBackend->running()) return true;
        if (m_piActive) return true;
        return m_agentInTerminal ? (m_agentPid != 0) : (m_agentProc && m_agentProc->state() != QProcess::NotRunning);
    }
    bool agentStarting() const { return m_agentStarting || !m_pendingAutoAgentLaunchId.isEmpty()
                                       || !m_hybridPhase.isEmpty(); }
    QString hybridStatus() const;
    QString activeProfileToolSupport() const { return m_activeProfileToolSupport; }
    QString agentLog() const { return m_agentLog; }
    QVariantList agentMessages()  const { return m_agentMessages; }
    AgentRoomStore *agentRoomStore() { return m_agentRoomStore; }
    int agentStreamingIndex() const { return m_agentStreamingIndex; }
    QString agentStreamingText() const { return m_agentStreamingText; }
    QString personaStyleAnalysisStatus() const { return m_personaStyleAnalysisStatus; }
    QString personaStyleAnalysisError() const { return m_personaStyleAnalysisError; }
    int chatStreamingIndex() const { return m_chatStreamingIndex; }
    QString chatStreamingText() const { return m_chatStreamingText; }
    int agentQueuedCount() const { return m_agentQueuedCount; }
    int chatQueuedCount() const { return m_chatQueuedCount; }
    QStringList agentQueuedMessages() const;
    QStringList chatQueuedMessages() const;
    QVariantList agentSessions()  const { return m_agentSessions; }
    QString opencodeSessionId()   const { return m_opencodeSessionId; }
    QString opencodeSessionTitle() const { return m_opencodeSessionTitle; }
    QVariantMap agentPendingTool() const { return m_agentPendingTool; }
    int agentContextUsed() const { return m_agentContextUsed; }
    int agentContextLimit() const { return m_agentContextLimit; }
    int agentContextTranscript() const { return m_agentContextTranscript; }
    qint64 agentContextPruned() const { return m_agentContextPruned; }
    int agentContextPruneEvents() const { return m_agentContextPruneEvents; }
    QString agentSystemPrompt() const { return m_agentSystemPrompt; }
    double agentTemperature() const { return m_agentTemperature; }
    QString agentPermRules() const { return m_agentPermRules; }
    void setAgentSystemPrompt(const QString &p);
    void setAgentTemperature(double t);
    void setAgentPermRules(const QString &rules);
    QString agentApprovalMode() const { return m_agentApprovalMode; }
    void setAgentApprovalMode(const QString &mode);
    bool agentThinkingEnabled() const { return m_agentThinkingEnabled; }
    void setAgentThinkingEnabled(bool enabled);
    bool thinkingEnabled() const { return m_agentThinkingEnabled; }
    void setThinkingEnabled(bool enabled);
    bool chatThinkingEnabled() const { return m_chatThinkingEnabled; }
    void setChatThinkingEnabled(bool enabled);
    bool chatPersonaDesigner() const { return m_chatPersonaDesigner; }
    void setChatPersonaDesigner(bool enabled);
    double chatTemperature() const { return m_chatTemperature; }
    double chatTopP() const { return m_chatTopP; }
    int chatTopK() const { return m_chatTopK; }
    double chatMinP() const { return m_chatMinP; }
    double chatRepeatPenalty() const { return m_chatRepeatPenalty; }
    void setChatTemperature(double value);
    void setChatTopP(double value);
    void setChatTopK(int value);
    void setChatMinP(double value);
    void setChatRepeatPenalty(double value);
    // Automatización de browser vía MCP Playwright (toggle global; override por perfil).
    bool browserAutomationEnabled() const { return m_browserAutomationEnabled; }
    void setBrowserAutomationEnabled(bool enabled);
    QString browserMcpCommand() const { return m_browserMcpCommand; }
    void setBrowserMcpCommand(const QString &cmd);
    // ── Browser teach: grabar/listar/borrar skills reproducibles ──
    Q_INVOKABLE QStringList listBrowserSkills() const;
    Q_INVOKABLE bool removeBrowserSkill(const QString &name);
    Q_INVOKABLE bool browserSkillExists(const QString &name) const;
    // Lanza Playwright codegen: el usuario graba acciones; al cerrar el inspector
    // se guarda el skill. "" = ok (grabación lanzada); si no, mensaje de error.
    Q_INVOKABLE QString recordBrowserSkill(const QString &name, const QString &url);
    Q_INVOKABLE bool browserRecording() const { return m_browserRecordProc != nullptr; }
    bool mermaidEnabled() const { return m_mermaidEnabled; }
    void setMermaidEnabled(bool enabled);
    QString agentTeacherUrl()   const { return m_agentTeacherUrl; }
    QString agentTeacherModel() const { return m_agentTeacherModel; }
    QString agentTeacherKey()   const { return m_agentTeacherKey; }
    void setAgentTeacherUrl(const QString &url);
    void setAgentTeacherModel(const QString &model);
    void setAgentTeacherKey(const QString &key);
    QString agentAuxiliaryUrl() const { return m_agentAuxiliaryUrl; }
    QString agentAuxiliaryEmbeddingModel() const { return m_agentAuxiliaryEmbeddingModel; }
    QString agentAuxiliaryRerankModel() const { return m_agentAuxiliaryRerankModel; }
    QString agentAuxiliaryKey() const { return m_agentAuxiliaryKey; }
    void setAgentAuxiliaryUrl(const QString &url);
    void setAgentAuxiliaryEmbeddingModel(const QString &model);
    void setAgentAuxiliaryRerankModel(const QString &model);
    void setAgentAuxiliaryKey(const QString &key);
    QString activeAgentAdapter() const { return m_activeAgentAdapter; }
    QString activeHarnessEngineId() const { return m_activeHarnessEngineId; }
    bool agentInTerminal() const { return m_agentInTerminal; }
    bool installingHarness() const { return m_installingHarness; }
    QString harnessInstallStatus() const { return m_harnessInstallStatus; }
    int harnessCheckV() const { return 0; }
    bool benchmarkRunning() const { return m_benchmarkRunning; }
    int benchmarkProgress() const { return m_benchmarkProgress; }
    QString benchmarkStatus() const { return m_benchmarkStatus; }
    QVariantList benchmarkResults() const { return m_benchmarkResults; }
    QVariantList benchmarkCoverage() const;
    QVariantList benchmarkRanking() const;
    QVariantList benchmarkBest25() const;
    QVariantList benchmarkBestModelosSpeed() const;
    QVariantList benchmarkHumanEval20Candidates() const;
    QVariantList benchmarkBestModelosQuality() const;
    QVariantList customBenchmarks() const { return m_customBenchmarks; }
    bool researchRunning() const { return m_researchRunning; }
    int researchProgress() const { return m_researchProgress; }
    QString researchStatus() const { return m_researchStatus; }
    QVariantList researchReports() const { return m_researchReports; }
    QVariantMap hardwareSummary() const { return m_hardwareSummary; }
    bool startupBusy() const { return m_startupBusy; }
    QString startupStatus() const { return m_startupStatus; }
    QVariantMap startupTimings() const { return m_startupTimings; }
    bool devMode() const { return m_devMode; }
    void setDevMode(bool enabled);
    QVariantMap performanceSnapshot() const { return m_performanceSnapshot; }
    Q_INVOKABLE QString performanceLogPath() const;
    Q_INVOKABLE void clearPerformanceLog();
    Q_INVOKABLE void recordPerformanceSample(const QString &label);
    QVariantList engineCatalog() const { return EngineCatalog::toVariantList(EngineCatalog::detectHardware()); }
    QVariantList modelRecommendations() const { return m_modelRecommendations; }
    bool modelDownloadRunning() const { return m_modelDownloadReply != nullptr; }
    int modelDownloadProgress() const { return m_modelDownloadProgress; }
    QString modelDownloadStatus() const { return m_modelDownloadStatus; }
    QVariantList modelDownloadQueue() const;
    QVariantList downloadHistory() const { return m_downloadHistory.history(); }
    Q_INVOKABLE void clearDownloadHistory() { m_downloadHistory.clear(); }

    Q_INVOKABLE void newChatSession();
    Q_INVOKABLE void newChatSessionInProject(const QString &projectId, const QString &projectName);
    Q_INVOKABLE void switchChatSession(const QString &id);
    Q_INVOKABLE void deleteChatSession(const QString &id);
    Q_INVOKABLE void deleteChatProject(const QString &projectName);
    Q_INVOKABLE void moveChatToProject(const QString &id, const QString &projectId, const QString &projectName);
    Q_INVOKABLE QVariantList chatProjects() const;
    Q_INVOKABLE void renameChatSession(const QString &id, const QString &title);
    Q_INVOKABLE void renameChatProject(const QString &oldName, const QString &newName);
    Q_INVOKABLE void sendChatMessage(const QString &text);
    Q_INVOKABLE void sendChatMessageWithAttachments(const QString &text, const QStringList &paths);
    Q_INVOKABLE void steerChat(const QString &text);
    Q_INVOKABLE void queueChat(const QString &text);
    Q_INVOKABLE bool updateChatQueuedMessage(int index, const QString &text);
    Q_INVOKABLE bool removeChatQueuedMessage(int index);
    Q_INVOKABLE void clearChatQueue();
    Q_INVOKABLE QStringList pickChatAttachments();
    // Si el portapapeles tiene una imagen, la guarda a temp y devuelve la ruta; "" si no.
    Q_INVOKABLE QString pasteClipboardImage();
    Q_INVOKABLE void stopChatGeneration();
    Q_INVOKABLE void startServer(const QString &launchProfileId);
    Q_INVOKABLE void startServerAndAgent(const QString &launchProfileId);
    Q_INVOKABLE bool useSuggestedServerPort(const QString &launchProfileId, int port,
                                            bool startAgent);
    Q_INVOKABLE void stopServer();
    Q_INVOKABLE void applyThinkingChange(bool enabled,
                                         const QString &surface,
                                         const QString &restartMode);
    Q_INVOKABLE void computeEffectiveProfile(const QString &launchProfileId);
    Q_INVOKABLE QVariantMap launchPortStatus(const QString &launchProfileId);
    Q_INVOKABLE QVariantMap launchVramFitStatus(const QString &launchProfileId);
    Q_INVOKABLE bool setLaunchBackendPort(const QString &launchProfileId, int port);
    // Recalcula la vista previa desde valores en memoria del editor, sin persistir.
    Q_INVOKABLE void computeEffectiveProfilePreview(const QString &launchProfileId,
                                                    const QVariantMap &overrides);
    // --- Router mode (hot-swap) -----------------------------------------
    // Genera un .ini de presets desde varios launch profiles y lo escribe en disco.
    // Devuelve la ruta del .ini, o cadena vacía + serverError() si algo falla.
    Q_INVOKABLE QString generateRouterPreset(const QStringList &launchProfileIds);
    // Arranca un único llama-server en modo router con el preset generado.
    // El swap entre modelos se hace por el campo "model" del request (nombre de sección).
    Q_INVOKABLE void startRouter(const QStringList &launchProfileIds, int modelsMax = 1);
    // Nombres de sección (modelos) cargados en el router activo.
    Q_INVOKABLE QStringList routerModelNames() const { return m_routerModelNames; }
    // Modelo (sección) activo: chat/agente mandan este nombre como campo "model".
    Q_INVOKABLE QString routerActiveModel() const { return m_routerActiveModel; }
    Q_INVOKABLE void setRouterActiveModel(const QString &name);
    Q_INVOKABLE bool serverIsRouter() const { return m_serverIsRouter; }
    Q_INVOKABLE void clearLog();
    // Filtra el log del server por nivel/fuente. level: "all"|"error"|"warn"|"stderr"|
    // "stdout"|"lifecycle"|"health"|"diag". Devuelve sólo las líneas que matchean.
    Q_INVOKABLE QString serverLogByLevel(const QString &level) const;
    // Barrido de errores: agrupa las líneas de error del log del server + del
    // agente en firmas distintas con su conteo (ver LogTriage). Resumen corto
    // para alimentar un Proceso "barrido de errores" en bucle. Vacío = sin
    // errores. Reachable headless vía ControlApi.
    Q_INVOKABLE QString triageServerLog(int maxGroups = 10) const;
    // Exporta una sesión de chat a archivo (Markdown o JSON). format: "md"|"json".
    // Abre diálogo de guardado; devuelve la ruta escrita ("" si cancelado/error).
    Q_INVOKABLE QString exportChatSession(const QString &id, const QString &format);
    // Variante headless: escribe directo a `path` (sin diálogo). Devuelve la ruta
    // escrita, o "" + serverError() si falla.
    Q_INVOKABLE QString exportChatSessionTo(const QString &id, const QString &format,
                                            const QString &path);
    // Busca texto en títulos y contenido de todas las sesiones de chat. Devuelve
    // lista de {id,title,projectName,snippet} de sesiones que matchean.
    Q_INVOKABLE QVariantList searchChatHistory(const QString &query) const;
    Q_INVOKABLE void copyToClipboard(const QString &text);
    // Diálogo "Guardar como": devuelve la ruta elegida (sugerencia en Documentos)
    // o "" si se canceló. filter estilo Qt: "PNG (*.png)".
    Q_INVOKABLE QString pickSavePath(const QString &suggestedName, const QString &filter);
    // Abre el explorador en la carpeta contenedora del archivo (y lo selecciona en Windows).
    Q_INVOKABLE void openContainingFolder(const QString &path);
    Q_INVOKABLE void installOfficialBinary();
    Q_INVOKABLE void installRequiredBinaryForProfile(const QString &launchProfileId);
    // Instala el build MTP (Anbeeld/beellama.cpp, DFlash/MTP) — solo Windows CUDA.
    // Para perfiles de sistema en máquinas NVIDIA: habilita los flags MTP.
    Q_INVOKABLE void installMtpBinary();
    Q_INVOKABLE void installCatalogEngine(const QString &engineId);
    Q_INVOKABLE void cancelOfficialBinaryInstall();
    Q_INVOKABLE void smokeTestServer(const QString &launchProfileId);
    Q_INVOKABLE bool smokeTestRunning() const { return m_smokeTestProc != nullptr; }
    Q_INVOKABLE QString resolveFlag(const QString &binaryId, const QString &flag) const;
    Q_INVOKABLE QString version() const { return QStringLiteral("0.1.116"); }
    // Convierte la respuesta de /repos/.../releases/latest al formato interno
    // del popup. Público para poder validar el contrato sin hacer red en tests.
    static QJsonObject githubReleaseToUpdateFlag(const QJsonObject &release);
    static bool shouldReplaceBundledBenchmarkForTest(const QJsonObject &source,
                                                     const QJsonObject &destination);
    static QVariantList benchmarkBest25ForTest(const QVariantList &results);
    static QVariantList benchmarkRankingForTest(const QVariantList &results);
    static QVariantList benchmarkBestModelosSpeedForTest(const QVariantList &candidates);
    static QVariantList benchmarkHumanEval20CandidatesForTest(const QVariantList &speedCandidates,
                                                              const QVariantList &bestControls);
    static QVariantList benchmarkBestModelosQualityForTest(const QVariantList &results,
                                                            const QVariantList &speedCandidates);
    static bool benchmarkErrorIsInfrastructureForTest(const QString &message);
    static bool benchmarkTransportAfterEvaluationForTest(int evaluatedTaskCount,
                                                          int declaredTaskCount,
                                                          bool transportFailure);
    // Compuerta de la escalera HE0 -> HE20 -> BCB. HE0 exige 1/1 y transporte
    // válido; HE20 puede ser calidad parcial, pero nunca infraestructura rota.
    static bool benchmarkResultPassesGateForTest(const QVariantMap &result,
                                                  const QString &stage,
                                                  const QString &profileFingerprint);
    static QString benchmarkStageCoverageStateForTest(const QVariantMap &result,
                                                       const QString &stage,
                                                       const QString &profileFingerprint);
    static QString customBenchmarkStageForTest(const QString &label, int taskCount);
    static QVariantList benchmarkDocumentRowsForTest(const QString &markdown,
                                                      const QString &sourceName = QString());
    // Checkout del que cuelga el exe (lo consume el bootstrap via LC_DIR).
    static QString installRootForExePath(const QString &exePath);
    // Diagnóstico consolidado (estilo `om doctor`): estado de binarios, roots,
    // catálogo, hardware, git, gateway y server en un solo QVariantMap, más una
    // lista `issues` de problemas accionables. Reachable headless vía ControlApi
    // (GET /invoke?method=doctor). Pensado para triage rápido sin abrir la GUI.
    Q_INVOKABLE QVariantMap doctor() const;
    // ── Ingesta de modelos de Ollama ──
    // Store de Ollama por defecto ($OLLAMA_MODELS o ~/.ollama/models).
    Q_INVOKABLE QString ollamaDefaultStore() const;
    // ¿Hay un store de Ollama detectable en el dir por defecto? (gate del botón UI).
    Q_INVOKABLE bool ollamaStoreAvailable() const;
    // Registra un ModelRoot kind="ollama" (dir vacío = store por defecto) y lo
    // escanea. Devuelve el rootId, o "" si no hay store en esa ruta. Headless.
    Q_INVOKABLE QString importOllamaModels(const QString &dir = QString());
    bool updateAvailable() const { return m_updateAvailable; }
    QVariantMap updateInfo() const { return m_updateInfo; }
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void handleUpdateDecision(const QString &decision);
    Q_INVOKABLE QString l(const QString &key) const;
    Q_INVOKABLE QString lf(const QString &key, const QString &arg1) const { return l(key).arg(arg1); }
    Q_INVOKABLE QVariant readSetting(const QString &key, const QVariant &defaultValue = QVariant()) const;
    Q_INVOKABLE void writeSetting(const QString &key, const QVariant &value);
    // Perfil efectivo del selector Agente: cloud mantiene su preferencia;
    // un launch local activo prevalece sobre una preferencia local anterior.
    Q_INVOKABLE QString preferredAgentLaunchId() const;
    static QString choosePreferredAgentLaunchId(const QString &agentId,
                                                bool agentIsCloud,
                                                const QString &activeId,
                                                const QString &globalId);
    Q_INVOKABLE bool startWithWindowsEnabled() const;
    Q_INVOKABLE QString setStartWithWindowsEnabled(bool enabled);
    static QString windowsStartupCommand(const QString &executablePath);
    static bool shouldStartHidden(bool startedWithWindows, bool minimizeToTray);
    // Normaliza el adapter de harness: "none"/""/"opencode" → "llamaagent" (todo
    // perfil usa el agente nativo). "raw" (Chat) se respeta.
    static QString normalizeHarnessAdapter(const QString &adapter);
    Q_INVOKABLE QString exportUserData();
    Q_INVOKABLE QString importUserData();
    // Variantes headless: ruta explícita, sin diálogo.
    Q_INVOKABLE QString exportUserDataTo(const QString &path);
    Q_INVOKABLE QString importUserDataFrom(const QString &path);
    Q_INVOKABLE bool wipeUserData(const QString &kind, const QString &confirmation);
    Q_INVOKABLE QVariantList wipeCategories() const;
    Q_INVOKABLE bool isHarnessInstalled(const QString &adapter) const;
    Q_INVOKABLE void installHarness(const QString &adapter);
    // Maestro CLI (supervisor): detección + metadata para la UI de Perfiles.
    Q_INVOKABLE QStringList masterCliList() const;
    Q_INVOKABLE QVariantMap masterCliStatus(const QString &name, bool force = false);
    Q_INVOKABLE QString masterCliInstallCommand(const QString &name) const;
    // Corridas largas de Claude Code/Codex con prompt y artefactos durables.
    // El modo seguro por defecto es de planificación; la escritura requiere
    // applyEdits explícito y nunca activa bypass/full-auto implícitamente.
    Q_INVOKABLE QString startManagedAgentRun(const QVariantMap &request);
    Q_INVOKABLE bool stopManagedAgentRun(const QString &runId);
    Q_INVOKABLE QVariantMap managedAgentRun(const QString &runId) const;
    Q_INVOKABLE QString managedAgentRunLog(const QString &runId) const;
    Q_INVOKABLE bool removeManagedAgentRun(const QString &runId);
    Q_INVOKABLE void openManagedAgentRunDirectory(const QString &runId);
    Q_INVOKABLE void refreshNativeAgentRuns();
    Q_INVOKABLE QVariantMap nativeAgentRun(const QString &runId) const;
    Q_INVOKABLE QVariantList nativeAgentRunEvents(const QString &runId) const;
    Q_INVOKABLE QVariantMap nativeAgentDeliverableManifest(const QString &runId) const;
    Q_INVOKABLE bool saveNativeAgentDeliverable(const QString &runId,
                                                const QString &relativePath,
                                                const QString &destination,
                                                bool overwrite = false);
    Q_INVOKABLE void openNativeAgentRunDirectory(const QString &runId);
    Q_INVOKABLE bool resolveNativeAgentRun(const QString &runId,
                                           const QString &status = QStringLiteral("cancelled"),
                                           const QString &detail = QString());
    Q_INVOKABLE void startAgent(const QString &launchProfileId);
    Q_INVOKABLE void stopAgent();

    // ── Tasks (macros semánticas) ──
    // Ejecuta una Task (botón manual o scheduler): compone el prompt-objetivo y lo
    // resuelve con el agente de forma adaptativa. Si el agente ya corre lo usa; si
    // no, auto-inicia server+agente (perfil de la Task o el activo), ejecuta al
    // quedar listo y lo apaga al terminar. Marca lastRun en el TaskStore.
    Q_INVOKABLE void runTask(const QString &id);
    Q_INVOKABLE void approveTaskWorkflow(const QString &choice,
                                         const QString &userText = QString());
    Q_INVOKABLE void runTaskAB(const QString &id);
    Q_INVOKABLE QString validateWorkflow(const QVariantMap &definition) const;
    Q_INVOKABLE QVariantList workflowVisualRows(const QVariantMap &definition) const;
    Q_INVOKABLE QVariantMap mergeWorkflowVisual(const QVariantMap &definition,
                                                 const QVariantList &rows) const;
    // Presets de ingeniería declarativos, reutilizables desde Tasks y QML.
    Q_INVOKABLE QVariantList engineeringWorkflows() const;
    Q_INVOKABLE QVariantList engineeringSafetyProfiles() const;
    Q_INVOKABLE QString installEngineeringWorkflow(const QString &workflowId);
    // Test seams (solo para tests; no usar desde la app). Permiten ejercitar el
    // ciclo del bucle de Tasks sin un llama-server real: inyectar un backend de
    // agente fake y arrancar el cuerpo de la Task salteando el gating de server.
    void setTestAgentBackend(IAgentBackend *b);
    Q_INVOKABLE void runTaskBodyForTest(const QString &id);
    // binaryPin del perfil de sistema (del bundle): substring de nombre/ruta del
    // build a fijar (ej. "b9842"). Vacío = sin pin. Público (headless/test).
    Q_INVOKABLE QString systemProfileBinaryPin(const QString &launchId) const;
    Q_INVOKABLE int systemProfileMinimumBinaryBuild(const QString &launchId) const;
    Q_INVOKABLE QVariantList systemProfileContextPresets(const QString &launchId) const;
    Q_INVOKABLE QString createSystemProfileContextVariant(const QString &launchId, int ctx);
    static int llamaCppBuildNumber(const QString &text);
    // Test seam para la huella del workspace del benchmark: los eventos internos
    // del agente no cuentan como progreso de una reparación.
    static bool benchmarkWorkspacePathIsInternalForTest(const QString &relativePath);
    // Aplica las capacidades de un perfil de agente al backend (tools/directivas/
    // MCP; thinking viene del checkbox global) usando la MISMA traducción que la app. Test seam para medir el
    // presupuesto de contexto por NIVEL sin duplicar la lógica de mapeo.
    void applyAgentProfileCapsForTest(class LlamaAgentBackend *cb, const AgentProfile &ap)
    { applyAgentProfileCaps(cb, ap); }
    // Ingi Charla: rutea un transcript de voz. Si hay agente corriendo lo manda al
    // agente (computer-use/visión) y devuelve true; si no, al chat backend (false).
    // Excepción: si es una orden de cursor por voz (ver tryVoiceCursorCommand) se
    // resuelve local y devuelve false SIN mandar nada a ningún backend — el bool
    // sólo distingue agente de chat, así que acá "false" es "no fue al agente".
    // Público para tests (el lambda de transcriptReady delega acá).
    bool dispatchCharlaTranscript(const QString &text);
    // Cursor por voz vía OCR (VoiceConfig::cursorOcr, off por defecto). Si el
    // transcript es una orden de cursor la ejecuta y devuelve true (el turno NO
    // llega al LLM); si no, false y sigue el ruteo normal. Público para tests.
    bool tryVoiceCursorCommand(const QString &text);
    void setVoiceCursorOcrForTest(bool on) { m_voiceCursorOcr = on; }
    // Estado del OCR de Windows para la UI: {available, language, detail}. Sin
    // esto, prender el toggle de cursor por voz en una máquina sin paquete de
    // idioma OCR falla mudo — el usuario sólo se entera al hablarle y recibir un
    // error. Q_INVOKABLE (no property): levantar el motor no es gratis, así que se
    // consulta cuando la UI lo necesita, no en cada repintado.
    Q_INVOKABLE QVariantMap ocrStatus() const;
    bool charlaUseAgentForTest() const { return m_charlaUseAgent; }
    // Regresión "Iniciando agente" trabado tras swap/restart de server: arma el
    // estado previo (pending + starting) y dispara el ready-branch. Con un agente
    // ya corriendo debe bajar el flag sin relanzar.
    void setPendingAutoAgentForTest(const QString &launchId)
    { m_pendingAutoAgentLaunchId = launchId; m_agentStarting = true; }
    void triggerPendingAgentForTest() { maybeStartPendingAgentOnReady(); }
    bool agentStartingFlagForTest() const { return m_agentStarting; }
    QString pendingAutoAgentForTest() const { return m_pendingAutoAgentLaunchId; }
    static QString composeHybridExecutionPromptForTest(const QString &request,
                                                        const QString &plan);
    static QVariantMap parseHybridPlanForTest(const QString &text, QString *error = nullptr);
    static QString parseHybridStreamLineForTest(const QByteArray &line, bool *done);
    static QString hybridStatusTextForTest(const QString &phase,
                                           const QString &plannerName,
                                           const QString &executorName);
    void setHybridPhaseForTest(const QString &phase) { setHybridPhase(phase); }
    QVariantMap resolvedSystemBinaryForTest(const QString &launchId)
    {
        const auto ctx = buildContext(launchId);
        return {{QStringLiteral("id"), ctx.binary.id},
                {QStringLiteral("backend"), ctx.binary.backend},
                {QStringLiteral("path"), ctx.binary.path}};
    }
    // Ejecuta la Automatización `automationId`: resuelve el proceso enlazado y lo
    // corre vía runTask, marcando el resultado también en el AutomationStore.
    Q_INVOKABLE void runAutomation(const QString &automationId);
    Q_INVOKABLE QVariantMap schedulerDaemonStatus() const;
    Q_INVOKABLE QString taskRunWorkLog(const QString &id) const;
    Q_INVOKABLE QVariantMap captureTaskPreview();
    Q_INVOKABLE bool clearTaskPreviewArtifacts();
    // Historial de corridas de un Proceso o Programación (más nuevo primero).
    Q_INVOKABLE QVariantList runHistory(const QString &ownerId) const;
    // Exporta el historial como paquete de evidencia versionado con hashes.
    Q_INVOKABLE QString exportRunEvidence(const QString &ownerId);
    Q_INVOKABLE QString exportRunEvidenceTo(const QString &ownerId, const QString &path);
    // Compara telemetría de dos corridas (índices del historial, 0=más nueva).
    // Permite A/B reproducible sin mezclar los scores de calidad del benchmark.
    Q_INVOKABLE QVariantMap compareTaskRunMetrics(const QString &ownerId,
                                                  int baselineIndex,
                                                  int candidateIndex) const;
    Q_INVOKABLE QVariantMap currentAgentEfficiency() const;
    static bool taskFinalTextIndicatesFailure(const QString &text);
    static bool taskRequiresToolEvidence(const QVariantMap &task);
    // Una reproducción determinista exitosa también es evidencia de operación:
    // el verificador visual no necesita llamar otra tool si ambas imágenes cumplen.
    static bool taskHasToolEvidence(const QString &workLog, const QVariantList &replayReport);
    static bool taskHasVisualComparison(const QVariantList &replayReport);
    // Previsualiza el prompt que recibiría el agente (para el editor de Tasks).
    Q_INVOKABLE QString previewTaskPrompt(const QString &id) const;
    // Graba un paso de browser (Playwright codegen) y devuelve el nombre del skill
    // generado para referenciarlo en un paso de Task. "" + serverError si falla.
    Q_INVOKABLE QString recordTaskBrowserStep(const QString &skillName, const QString &url);
    Q_INVOKABLE QVariantList automationScreens() const;
    Q_INVOKABLE QVariantList automationWindows() const;
    Q_INVOKABLE QString startDesktopTeach(const QString &taskId, const QString &scopeKind,
                                          const QString &scopeTargetId);
    Q_INVOKABLE QString startBrowserTeach(const QString &taskId, const QString &url,
                                          bool discoverNetwork = true);
    Q_INVOKABLE void pauseTeach(bool paused);
    Q_INVOKABLE void addTeachNote(const QString &note);
    Q_INVOKABLE QVariantMap captureTeachVisualReference(int size = 72);
    Q_INVOKABLE bool armTeachVisualRegionSelection();
    Q_INVOKABLE QString finishTeach();
    Q_INVOKABLE void cancelTeach();
    Q_INVOKABLE QVariantList automationTimeline(const QString &artifactId) const;
    Q_INVOKABLE QVariantList automationTemplates(const QString &artifactId) const;
    Q_INVOKABLE QVariantList automationNetworkDiscoveries(const QString &artifactId) const;
    Q_INVOKABLE bool reviewAutomationNetworkDiscovery(const QString &artifactId,
                                                      const QString &signature,
                                                      const QString &status);
    Q_INVOKABLE bool clearAutomationNetworkDiscoveries(const QString &artifactId);
    Q_INVOKABLE QVariantMap testAutomationTemplate(const QString &artifactId,
                                                   const QString &fileName) const;
    Q_INVOKABLE bool removeAutomationTemplate(const QString &artifactId,
                                              const QString &fileName);
    Q_INVOKABLE bool replaceAutomationTemplate(const QString &artifactId,
                                               const QString &fileName,
                                               const QString &sourcePath);
    Q_INVOKABLE bool addAutomationTemplateVariant(const QString &artifactId,
                                                  const QString &fileName,
                                                  const QString &sourcePath);
    Q_INVOKABLE QString importBrowserSkillAsTask(const QString &skillName);
    Q_INVOKABLE bool removeAutomationEvidence(const QString &artifactId,
                                              const QString &fileName);
    Q_INVOKABLE void stopAutomation();
    QString teachState() const { return m_teachRecorder.state(); }
    QString teachError() const { return m_teachRecorder.error(); }
    QVariantList teachTimeline() const { return m_teachRecorder.timeline(); }
    // --- Secretos cloud (API keys fuera del repo) ---
    // ¿Hay key resoluble (env var o store) para esa ref?
    Q_INVOKABLE bool hasSecret(const QString &keyRef) const { return m_secrets.has(keyRef); }
    // Guarda/actualiza la key en el store en disco (no toca env vars). value vacío = borra.
    Q_INVOKABLE void setSecret(const QString &keyRef, const QString &value) { m_secrets.set(keyRef, value); }
    // keyRef del backend cloud de un perfil ("" si el backend no es cloud).
    Q_INVOKABLE QString cloudKeyRefForProfile(const QString &launchProfileId);
    Q_INVOKABLE void sendToAgent(const QString &text);
    // Arranca el canal estrecho del asistente. Devuelve el token efectivo para
    // mostrarlo una sola vez; si token está vacío se genera y guarda cifrado.
    Q_INVOKABLE QString startAssistantGateway(int port = 8787,
                                              const QString &token = QString(),
                                              bool lan = false);
    Q_INVOKABLE void stopAssistantGateway();
    // Encola una capacidad auxiliar con la clase, recurso, prioridad y fallback
    // definidos por el rol; la ejecución efectiva queda a cargo del worker.
    Q_INVOKABLE QString enqueueModelRoleJob(const QString &roleId,
                                             const QString &detail = {});
    // Analiza una muestra con el backend activo y aplica una ficha JSON validada
    // al perfil indicado. No usa tools ni modifica el historial del agente.
    Q_INVOKABLE bool analyzePersonaStyleProfile(const QString &profileId,
                                                const QString &sample);
    Q_INVOKABLE QString createAgentRoom(const QString &title,
                                        const QString &projectDir = QString());
    Q_INVOKABLE bool sendAgentRoomMessage(const QString &roomId, const QString &text,
                                          const QStringList &audience = {});
    Q_INVOKABLE bool runAgentRoomPreset(const QString &roomId, const QString &preset,
                                        const QString &goal);
    // Escalado manual al maestro del perfil activo. Devuelve false si no hay maestro
    // configurado o el agente no está corriendo.
    Q_INVOKABLE bool escalateToMaster(const QString &problem = QString());
    // ¿El agente activo tiene un maestro configurado? (gate del botón en la UI).
    Q_INVOKABLE bool agentMasterConfigured() const;
    Q_INVOKABLE void sendToAgentWithAttachments(const QString &text, const QStringList &paths);
    // Diálogo de adjuntos para el agente; filtra imágenes si el modelo no tiene visión.
    Q_INVOKABLE QStringList pickAgentAttachments();
    // Archivos del proyecto del agente que matchean `query` (para @-mentions). Rutas
    // relativas, salta dirs ignorados, cap 50.
    Q_INVOKABLE QStringList agentProjectFiles(const QString &query) const;
    bool serverHasVision() const { return m_serverHasVision; }
    bool gitAvailable() const { return m_gitAvailable; }
    bool installingGit() const { return m_gitInstallProc != nullptr; }
    Q_INVOKABLE void installGit();
    Q_INVOKABLE void recheckGit();
    // Steering (interrumpe el turno y manda ya) / cola (manda al terminar).
    Q_INVOKABLE void steerAgent(const QString &text);
    Q_INVOKABLE void queueAgent(const QString &text);
    Q_INVOKABLE bool updateAgentQueuedMessage(int index, const QString &text);
    Q_INVOKABLE bool removeAgentQueuedMessage(int index);
    Q_INVOKABLE void clearAgentQueue();
    // Rebobinar la conversación del agente al estado previo a un mensaje de usuario.
    Q_INVOKABLE void rollbackAgentToMessage(int msgIndex);
    Q_INVOKABLE void forkAgentAtMessage(int msgIndex);
    // Igual para el chat (RawChatBackend): trunca msgs desde ese índice.
    Q_INVOKABLE void rollbackChatToMessage(int msgIndex);
    // Editar el texto de un mensaje (user o IA) y descartar lo posterior.
    Q_INVOKABLE void editAgentMessage(int msgIndex, const QString &newText);
    Q_INVOKABLE void editChatMessage(int msgIndex, const QString &newText);
    // Aborta la generación/turno en curso sin matar el backend (botón PARAR).
    Q_INVOKABLE void cancelAgentGeneration();
    Q_INVOKABLE void approveAgentTool(const QString &id, bool always = false);
    Q_INVOKABLE void rejectAgentTool(const QString &id);
    Q_INVOKABLE void revertAgentEdit(const QString &path);
    Q_INVOKABLE QString readAgentMemory(const QString &projectDir) const;
    Q_INVOKABLE bool writeAgentMemory(const QString &projectDir, const QString &text);
    Q_INVOKABLE void clearAgentLog();
    Q_INVOKABLE QString agentNativeLogDir(const QString &adapter) const;
    Q_INVOKABLE void openAgentLogDir(const QString &adapter);
    Q_INVOKABLE void openRuntimeLogDir();
    // Diagnóstico de movimientos inesperados del viewport QML. Persiste en
    // runtime/agent.log junto a las acciones del backend para correlacionarlos.
    Q_INVOKABLE void logAgentUiScroll(const QString &event, const QString &state);
    Q_INVOKABLE void newOpencodeSession();
    Q_INVOKABLE void switchOpencodeSession(const QString &sessionId);
    Q_INVOKABLE void refreshOpencodeSessionList();
    Q_INVOKABLE void renameOpencodeSession(const QString &sessionId, const QString &title);
    Q_INVOKABLE void deleteOpencodeSession(const QString &sessionId);
    Q_INVOKABLE void deleteOpencodeProject(const QString &projectDir);
    Q_INVOKABLE void newOpencodeSessionInProject(const QString &projectDir);
    Q_INVOKABLE void forkOpencodeSession(const QString &sessionId);
    // ── Tools del agente (habilitar/deshabilitar built-in) ──
    // Catálogo con metadata + estado enabled, para la UI de toggles.
    Q_INVOKABLE QVariantList agentToolCatalog() const;
    // Resumen del harness de un perfil con el ENTORNO REAL (embeddings del server
    // activo, escritorio, cuentas de correo, browser). ProfileManager solo puede
    // detectar git; sin esto el preflight de dependencias avisaba de menos.
    Q_INVOKABLE QVariantMap harnessSpecSummary(const QString &agentProfileId) const;
    Q_INVOKABLE QVariantList harnessEngineCatalog() const;
    // Servers MCP habilitados (global + proyecto). Alimenta el preflight.
    int enabledMcpServerCount() const;
    // Directivas propias del harness (.md): alta/edición y baja. Pasan por
    // AppController porque necesitan el workspace del agente para el scope
    // "project". Devuelven {ok} o {ok:false, error}.
    Q_INVOKABLE QVariantMap saveHarnessDirective(const QString &name, const QString &description,
                                                 const QString &when, const QString &body,
                                                 const QString &scope = QStringLiteral("global"));
    Q_INVOKABLE QVariantMap removeHarnessDirective(const QString &name,
                                                   const QString &scope = QStringLiteral("global"));
    Q_INVOKABLE QVariantMap harnessDirective(const QString &name) const;
    // Hechos que entiende el gate `when` de una directiva. Salen del backend
    // (única fuente); la UI los enumera para que no haya que adivinarlos.
    Q_INVOKABLE QStringList harnessDirectiveFacts() const;
    // A/B de HARNESS: agrupa las corridas de benchmark ya guardadas por perfil de
    // AGENTE (mismo modelo, distinto HarnessSpec) y devuelve el mismo informe que
    // usa comparison.json. `runDir` vacío = todas las corridas cargadas.
    // `sinceEpochMs` acota a las corridas de ESTA sesión de barrido: sin ese
    // filtro el informe mezcla el histórico del usuario (un perfil con 30 corridas
    // viejas contra uno recién creado con 1) y la comparación es una mentira.
    Q_INVOKABLE QVariantMap compareHarnessBenchmarks(const QStringList &agentProfileIds,
                                                     const QString &runDir = QString(),
                                                     double sinceEpochMs = 0) const;
    Q_INVOKABLE QVariantMap agentSandboxStatus() const;
    Q_INVOKABLE QVariantList portableSkills() const;
    Q_INVOKABLE QVariantMap portableSkill(const QString &name) const;
    Q_INVOKABLE void setAgentToolEnabled(const QString &name, bool enabled);
    // ── Perfiles de agente (capacidades + directivas como toggles) ──
    // Catálogo de directivas del system prompt para la UI (passthrough al backend).
    Q_INVOKABLE QVariantList agentDirectiveCatalog() const;
    // id del perfil de agente activo (override vivo del modo agente). Resolución:
    // override de sesión → agentProfileId del launch activo → preset por defecto.
    QString activeAgentProfileId() const;
    void setActiveAgentProfileId(const QString &id);
    Q_INVOKABLE QString pickDirectory(const QString &title = QString());
    Q_INVOKABLE void changeAgentProject(const QString &directory);
    Q_INVOKABLE QString currentAgentProjectDir() const;

    // ── Opencode config (opencode.json global / por proyecto) ──
    Q_INVOKABLE QString opencodeConfigPath(const QString &scope, const QString &projectDir) const;
    Q_INVOKABLE QString readOpencodeConfig(const QString &scope, const QString &projectDir) const;
    Q_INVOKABLE bool    writeOpencodeConfig(const QString &scope, const QString &projectDir, const QString &jsonText);

    // ── MCP servers (sobre el bloque "mcp" del config) ──
    Q_INVOKABLE QVariantList listMcpServers(const QString &scope, const QString &projectDir) const;
    // Inserta el server MCP de Playwright en el mapa mergeado si la automatización
    // de browser está efectivamente activa: override del LaunchProfile
    // (browserAutomation "on"/"off") pisa el toggle global; "inherit" usa el global.
    void injectBrowserMcp(QMap<QString, QVariant> &merged, const QString &launchId,
                          bool foreground = false, bool taskNeedsBrowser = true) const;
    // Decisión pura de activación: override del perfil ("on"/"off"/"inherit") sobre
    // el toggle global. Expuesta para test unitario.
    static bool browserMcpEffective(const QString &override, bool globalEnabled);
    Q_INVOKABLE bool setMcpServer(const QString &scope, const QString &projectDir,
                                  const QString &name, const QVariantMap &def);
    Q_INVOKABLE bool removeMcpServer(const QString &scope, const QString &projectDir, const QString &name);
    Q_INVOKABLE bool toggleMcpServer(const QString &scope, const QString &projectDir,
                                     const QString &name, bool enabled);

    // ── Cuentas de correo (globales). El password va a SecretStore (ref
    // "mail/<name>"), nunca al JSON. listMailAccounts NO incluye el password.
    Q_INVOKABLE QVariantList listMailAccounts() const;
    Q_INVOKABLE bool setMailAccount(const QString &name, const QVariantMap &def);
    Q_INVOKABLE bool removeMailAccount(const QString &name);
    // Prueba la cuenta (login SMTP + recepción). "" = OK; si no, mensaje de error.
    Q_INVOKABLE QString testMailAccount(const QString &name) const;
    bool mailAutoSend() const { return m_mailAutoSend; }
    void setMailAutoSend(bool on);
    bool hitlDestructive() const { return m_hitlDestructive; }
    void setHitlDestructive(bool on);
    bool desktopIndicatorVisible() const { return m_desktopIndicatorVisible; }
    void setDesktopIndicatorVisible(bool on);
    bool desktopAgentActive() const { return m_desktopAgentActive; }
    QString desktopAgentAction() const { return m_desktopAgentAction; }
    Q_INVOKABLE QVariantMap desktopCursorState() const;

    bool autoStartAgentOnLaunch() const { return m_autoStartAgentOnLaunch; }
    void setAutoStartAgentOnLaunch(bool on);

    // Gateway
    bool    gatewayEnabled() const { return m_gatewayEnabled; }
    void    setGatewayEnabled(bool on);
    bool    gatewayRunning() const { return m_gateway && m_gateway->listening(); }
    int     gatewayPort() const { return m_gatewayPort; }
    void    setGatewayPort(int p);
    QString gatewayApiKey() const { return m_gatewayApiKey; }
    void    setGatewayApiKey(const QString &k);
    int     gatewayKeepN() const { return m_gatewayKeepN; }
    void    setGatewayKeepN(int n);
    bool    gatewayAutoSwap() const { return m_gatewayAutoSwap; }
    void    setGatewayAutoSwap(bool on);
    bool    gatewayLanEnabled() const { return m_gatewayLanEnabled; }
    void    setGatewayLanEnabled(bool on);
    Q_INVOKABLE void startGateway();
    Q_INVOKABLE void stopGateway();
    Q_INVOKABLE QString gatewayBaseUrl() const;
    Q_INVOKABLE QString gatewayLanBaseUrl() const;
    Q_INVOKABLE QString gatewayLanOpenCodeConfig(const QString &launchProfileId) const;
    QVariantList lanServers() const { return m_lanServers; }
    bool lanDiscoveryActive() const { return m_lanDiscoverySocket != nullptr; }
    Q_INVOKABLE void discoverLanServers();
    Q_INVOKABLE void useLanServer(const QString &baseUrl, const QString &apiKey,
                                  const QString &profileId, const QString &profileName,
                                  int context);
    Q_INVOKABLE QString launchClaudeCode();   // exec `claude` apuntando al gateway
    // Configura Claude Desktop para Third-Party Inference usando este gateway.
    // Devuelve un mensaje de error o el resumen de las rutas configuradas.
    Q_INVOKABLE QString configureClaudeDesktop(const QString &launchProfileId);
    Q_INVOKABLE QString launchOpenCode(const QString &projectDir,
                                       const QString &launchProfileId);

    // Idle auto-stop
    int  idleAutoStopMin() const { return m_idleAutoStopMin; }
    void setIdleAutoStopMin(int minutes);
    void bumpActivity();   // marca actividad → reinicia el watchdog idle
    // Decisión pura (testeable): ¿parar por inactividad?
    static bool shouldIdleStop(bool serverRunning, bool busy, int idleMin,
                               qint64 idleElapsedMs);

    // Salida estructurada del chat (GBNF / JSON schema).
    Q_INVOKABLE void chatSetStructuredOutput(const QString &grammar, const QString &jsonSchema);
    // Cuentas con el password resuelto (para inyectar al backend del agente).
    QVariantList mailAccountsResolved() const;

    // ── Integrations (registro unificado: MCP global + API services) ──
    // Lista agregada de conexiones externas. Cada item: {id,type,name,enabled,
    // summary,config{}}. type = "mcp" | "api_service".
    Q_INVOKABLE QVariantList integrations() const;
    // Alta/edición de un MCP Tool Server (escribe en el config opencode global).
    Q_INVOKABLE bool saveMcpIntegration(const QString &name, const QString &type,
                                        const QString &commandOrUrl);
    // Alta/edición de un API Service genérico (endpoint HTTP + key). id vacío = crear.
    Q_INVOKABLE bool saveApiService(const QString &id, const QString &name,
                                    const QString &baseUrl, const QString &apiKey, bool enabled,
                                    const QString &provider);
    Q_INVOKABLE bool removeIntegration(const QString &id);
    Q_INVOKABLE bool setIntegrationEnabled(const QString &id, bool enabled);
    // Test asíncrono. Emite integrationTestResult(id, ok, message).
    Q_INVOKABLE void testIntegration(const QString &id);

    // ── Skills / comandos (.opencode/command/*.md) ──
    Q_INVOKABLE QVariantList listOpencodeCommands(const QString &scope, const QString &projectDir) const;
    Q_INVOKABLE QString readOpencodeCommand(const QString &scope, const QString &projectDir, const QString &name) const;
    Q_INVOKABLE bool writeOpencodeCommand(const QString &scope, const QString &projectDir,
                                          const QString &name, const QString &content);
    Q_INVOKABLE bool deleteOpencodeCommand(const QString &scope, const QString &projectDir, const QString &name);
    // agentProfileId: solo aplica al target "agent" — fija el NIVEL del agente
    // (capacidades + directivas) para una comparación justa. Vacío = todas las
    // tools (comportamiento histórico del benchmark).
    Q_INVOKABLE void startBenchmark(const QStringList &profileIds, const QString &mode, int passes = 1,
                                    const QString &target = QStringLiteral("model"), int timeoutSec = 0,
                                    const QString &agentProfileId = QString());
    Q_INVOKABLE void cancelBenchmark();
    // Una serie son horas: si la app muere en el perfil 8 de 13, esto dice qué
    // faltaba y lo retoma sin repetir lo ya medido.
    Q_INVOKABLE QVariantMap pendingBenchmark() const;
    Q_INVOKABLE bool resumeBenchmark();
    // Re-puntúa las corridas ya guardadas con los evaluadores actuales, usando el
    // workspace que quedó en disco. Cuando el bug está en el evaluador y no en la
    // medición, esto evita repetir horas de GPU. Devuelve cuántas cambiaron.
    Q_INVOKABLE int rescoreBenchmarkResults();
    Q_INVOKABLE void openBenchmarkFolder(const QString &path);
    Q_INVOKABLE void clearBenchmarkResults();
    Q_INVOKABLE void removeBenchmarkResult(int index);
    Q_INVOKABLE void removeBenchmarkResultById(const QString &id);
    Q_INVOKABLE bool exportBenchmarkResultsCsv(const QString &path) const;
    Q_INVOKABLE void loadBenchmarkResults();
    // Custom benchmarks (user-defined prompt sets)
    Q_INVOKABLE void loadCustomBenchmarks();
    Q_INVOKABLE QString saveCustomBenchmark(const QVariantMap &def); // returns id
    Q_INVOKABLE void deleteCustomBenchmark(const QString &id);
    // Importa una EvalSuite JSON (src/core/eval) como custom benchmark. Devuelve
    // el id creado, o "" si falla (motivo en lastEvalImportError()).
    Q_INVOKABLE QString importEvalSuite(const QString &path);
    // Importa un benchmark PÚBLICO estándar (GSM8K, HumanEval, MMLU) desde el
    // JSONL tal cual se descarga. Detecta el formato solo. `limit` corta la
    // cantidad de ítems: HumanEval son 164 y GSM8K 1319, y a 7 t/s eso son días.
    // Devuelve el id del benchmark creado, o vacío (ver lastEvalImportError).
    Q_INVOKABLE QString importBenchmarkPack(const QString &path, int limit = 0);
    Q_INVOKABLE QString lastEvalImportError() const { return m_lastEvalImportError; }
    Q_INVOKABLE void startCustomBenchmark(const QStringList &profileIds, const QString &customId, int passes = 1,
                                          const QString &target = QStringLiteral("model"), int timeoutSec = 0,
                                          const QString &agentProfileId = QString());
    // Ejecuta sólo la siguiente etapa faltante de un perfil benchmark=true.
    // La selección HE0 -> HE20 -> BCB y la huella efectiva se validan aquí,
    // evitando que un caller repita etapas ya cerradas o reactive retirados.
    Q_INVOKABLE QVariantMap startNextPendingBenchmark(const QString &profileId,
                                                       int passes = 1,
                                                       const QString &target = QStringLiteral("agent"),
                                                       int timeoutSec = 1800,
                                                       const QString &agentProfileId = QString());
    Q_INVOKABLE void startProBenchmarks(const QStringList &profileIds, const QStringList &customIds,
                                        int passes = 1, const QString &target = QStringLiteral("model"),
                                        int timeoutSec = 0, const QString &agentProfileId = QString());
    // Ejecuta una escalera explícita HE0 → HE20 → BCB con tres definiciones
    // custom. Cada etapa se serializa y conserva la compuerta de calidad.
    Q_INVOKABLE void startThreeStageBenchmark(const QStringList &profileIds,
                                              const QString &he0Id,
                                              const QString &he20Id,
                                              const QString &bcbId,
                                              int passes = 1,
                                              const QString &target = QStringLiteral("model"),
                                              int timeoutSec = 0,
                                              const QString &agentProfileId = QString());
    // Compara concurrencia real del llama-server. Para cada valor de slots crea
    // una copia editable del launch, abre varias requests simultáneas y guarda
    // throughput agregado, TTFT, latencia, RAM/VRAM y fallos en benchmarks.
    Q_INVOKABLE void startConcurrencyBenchmark(const QString &profileId,
                                               int minSlots = 1, int maxSlots = 3,
                                               int requests = 4, int maxTokens = 128,
                                               const QString &prompt = QStringLiteral(
                                                   "Respondé con una lista numerada de 5 puntos sobre buenas prácticas de programación."));
    // Benchmark nativo del servidor, separado del score E2E del agente. Mide
    // corpus versionado por categoría, PP/TG/TTFT/ITL, distribuciones,
    // cold/warm, sweep de prefill y curva de concurrencia. El límite de ocho
    // argumentos mantiene compatibilidad con ControlApi.
    Q_INVOKABLE void startServerSpeedBenchmark(const QStringList &profileIds,
                                               int passes = 5, int warmup = 1,
                                               bool includePrefill = true,
                                               bool includeConcurrency = true,
                                               int maxPrefillTokens = 65536,
                                               int maxSlots = 4, int requests = 4);
    // A/B pareado para dos perfiles. Cada par usa el mismo prompt y alterna el
    // orden A/B; al final guarda el intervalo de confianza y un test A/A.
    Q_INVOKABLE void startServerSpeedABBenchmark(const QString &profileAId,
                                                 const QString &profileBId,
                                                 int pairs = 10, int warmup = 1);
    // Auto-tuning de parámetros de inferencia (AutoTuner TPE-lite + gate de
    // calidad). Lanza llama-server por candidato en un puerto scratch, mide
    // tok/s y calidad, y al terminar fusiona la mejor config en extraArgs del
    // launch profile. maxTrials/qualityGate/nPredict son opcionales.
    // ppWeight [0,1]: cuánto pesa el prefill (PP) contra la generación (TG) en el
    // objetivo. prefillTokens: largo del prompt de medición; con un prompt corto
    // el PP no es medible y -b/-ub quedan sin señal que optimizar.
    // OJO: 8 parámetros = el techo que puede invocar ControlApi (QGenericArgument
    // ga[8]). Un 9º haría que el arg extra no se pase headless. Si hace falta más
    // configuración, agrupar en un QVariantMap en vez de sumar parámetros.
    Q_INVOKABLE void startAutoTune(const QString &launchProfileId, int maxTrials = 24,
                                   double qualityGate = 0.0, int nPredict = 256,
                                   const QString &mode = QStringLiteral("auto"),
                                   double ppWeight = 0.0, int prefillTokens = 0,
                                   bool measureBaseline = false);
    Q_INVOKABLE void cancelAutoTune();
    // Nombre del perfil que produce el tuner a partir del original. Prefijo
    // "Opti - " (contrato visible al usuario); no re-prefija un perfil que ya
    // salió del tuner, para no encadenar "Opti - Opti - …" al re-optimizar.
    static QString optimizedProfileName(const QString &sourceName);
    // Mejora relativa (%) de una medición contra su baseline. 0 si falta
    // cualquiera de las dos: sin "antes" no hay mejora que reportar.
    static double tuneGainPct(double after, double before);
    bool autoTuneRunning() const { return m_autoTuneRunning; }
    int autoTuneProgress() const { return m_autoTuneProgress; }
    QString autoTuneStatus() const { return m_autoTuneStatus; }
    // Filas de la tabla de trials de la sección Tuner. index 0 = baseline.
    QVariantList autoTuneTrials() const { return m_autoTuneTrials; }
    // Resultado final: bestArgs, pp/tg de la mejor config y del baseline,
    // mejora %, y el perfil "Opti - " creado.
    QVariantMap autoTuneResult() const { return m_autoTuneResult; }
    Q_INVOKABLE void clearAutoTuneResults();

    Q_INVOKABLE void startResearch(const QString &topic, const QString &mode, int maxPages,
                                   const QString &workspaceId = QString(),
                                   const QString &workspaceName = QString());
    Q_INVOKABLE void cancelResearch();
    Q_INVOKABLE void refreshResearchReports();
    Q_INVOKABLE QString readResearchReport(const QString &id) const;
    Q_INVOKABLE void openResearchReport(const QString &id);
    Q_INVOKABLE void deleteResearchReport(const QString &id);
    // Exporta un workspace (proyecto compatible) como JSON portable con manifiesto,
    // chats e investigaciones. No incluye secretos ni embeddings regenerables.
    Q_INVOKABLE QString exportWorkspace(const QString &workspaceId,
                                        const QString &workspaceName);
    Q_INVOKABLE QString exportWorkspaceTo(const QString &workspaceId,
                                          const QString &workspaceName,
                                          const QString &path);
    Q_INVOKABLE void rescanHardware();
    // Re-escanea TODOS los roots de modelos, incluidos los de scanMode "manual"
    // (que el arranque no toca). Un root manual con modelos nuevos en disco los
    // deja invisibles para los perfiles hasta que alguien fuerza esto, y el
    // síntoma que ve el usuario es un opaco "No model selected".
    Q_INVOKABLE void rescanModelRoots();
    // ── GPU power limit (nvidia-smi) ──
    // Estado actual por GPU: {available:bool, gpus:[{index,name,currentW,defaultW,
    // minW,maxW,drawW}]}. Lectura síncrona (rápida, usada on-demand desde Ajustes).
    Q_INVOKABLE QVariantMap gpuPowerInfo() const;
    // GPUs NVIDIA visibles para llama.cpp: {available:bool, gpus:[{index,name,
    // totalMb,driver}]}. Se obtiene bajo demanda para no bloquear el arranque.
    Q_INVOKABLE QVariantMap gpuInventory() const;
    // Recomendación explicable basada en la topología GPU/PCIe detectada.
    Q_INVOKABLE QVariantMap performanceRecommendation(const QString &target = {}) const;
    Q_INVOKABLE QVariantList performanceMatrixCandidates(const QString &target = {},
                                                         bool withVision = false) const;
    Q_INVOKABLE QVariantList rankPerformanceMatrix(const QVariantList &samples,
                                                   const QString &target = {}) const;
    Q_INVOKABLE QVariantMap annotatePerformanceMatrix(const QVariantMap &sample,
                                                      const QVariantMap &candidate) const;
    // Fija el power limit (W) en una GPU (gpuIndex<0 = todas). En Windows requiere
    // elevación → se relanza nvidia-smi vía powershell RunAs. Devuelve "" si OK o
    // un mensaje de error. Persiste el valor como setting global "gpuPowerLimitW".
    Q_INVOKABLE QString setGpuPowerLimit(int watts, int gpuIndex = -1);
    // Parser de la salida CSV de nvidia-smi (--query-gpu power.*). Estático para
    // testear sin GPU. Devuelve lista de QVariantMap por GPU.
    static QVariantList parseGpuPowerCsv(const QString &csv);
    static QVariantList parseGpuInventoryCsv(const QString &csv);
    // Reglas determinísticas para impedir que Deep Research publique errores
    // técnicos/comerciales conocidos aunque el modelo los redacte con confianza.
    static QStringList researchReportGuardrailIssues(const QString &report);
    // Filtro puro para el cookbook: la app descarga/lanza GGUF con llama.cpp, no
    // repos MLX/AWQ/GPTQ que requieren otros runtimes.
    static bool isGgufRecommendationCandidate(const QString &name, bool isGguf,
                                              bool hasGgufSources);
    static QString recommendedGgufFileName(const QString &repo, const QString &modelName,
                                           const QString &quant);
    static QString resolveRecommendedGgufFileName(const QStringList &siblings,
                                                  const QString &requestedFileName);
    // Escaneo pesado de arranque (binaries/roots/hardware/catálogo + migraciones).
    // Diferido fuera del constructor; QML lo invoca tras pintar el popup de carga.
    Q_INVOKABLE void runStartupScan();
    Q_INVOKABLE QString createRecommendedLaunchProfile();
    // Duplica un launch profile. Envuelve ProfileManager::duplicateLaunchProfile
    // para cerrar el agujero de los perfiles de sistema: esos resuelven en CADA
    // arranque, y sólo por ser system, tanto el binario (binaryPin /
    // minimumBinaryBuild / binaryKind) como el gguf (religado por nombre de archivo
    // contra los roots escaneados). La copia NO es system, así que ambas
    // resoluciones dejan de correr: hay que hornear sus resultados en la copia.
    Q_INVOKABLE QString duplicateLaunchProfile(const QString &launchId);
    Q_INVOKABLE void downloadRecommendedModel(const QString &repo, const QString &fileName);
    Q_INVOKABLE void pauseModelDownload(const QString &id);
    Q_INVOKABLE void resumeModelDownload(const QString &id);
    Q_INVOKABLE void cancelModelDownload(const QString &id);
    Q_INVOKABLE void moveModelDownload(const QString &id, int delta);
    Q_INVOKABLE void openModelRecommendation(const QString &repo);
    // Re-mostrar el asistente inicial (setup) desde la UI.
    Q_INVOKABLE void requestShowSetup() { emit showSetupRequested(); }
    // Emite secondInstanceLaunched (lo llama main.cpp al detectar otra instancia).
    Q_INVOKABLE void notifySecondInstance() { emit secondInstanceLaunched(); }

    // ── Perfiles de sistema (fast start por hardware) ──
    // Perfil de sistema recomendado: el tier más cercano ≤ hardware (VRAM/RAM).
    // Devuelve {} si no hay perfiles de sistema. Incluye campos del modelo a bajar.
    Q_INVOKABLE QVariantMap recommendedSystemProfile() const;
    // Acepta un perfil de sistema: asegura binario (MTP si hay NVIDIA, si no
    // official), baja el modelo (+mmproj) al dir gestionado, escanea y lo activa.
    Q_INVOKABLE void acceptSystemProfile(const QString &launchId);
    // Igual que acceptSystemProfile, pero al quedar listo selecciona y arranca
    // servidor + agente. Usado por botones "Instalar y usar".
    Q_INVOKABLE void installAndUseSystemProfile(const QString &launchId);
    // Showcase de tope (24GB): perfiles "extra" (MAX-Q coding + FAST-GEMMA general)
    // que el hardware puede correr. Vacío si la VRAM no alcanza.
    Q_INVOKABLE QVariantList recommendedShowcase() const;
    // Menú de lanzamiento para la UI: como ProfileManager::launchProfilesForMenu pero
    // (a) oculta perfiles de sistema cuya VRAM mínima supera la del equipo, y
    // (b) agrega "ready" (modelo+binario presentes) y "minVram" a cada item.
    Q_INVOKABLE QVariantList launchMenu();
    // True si el perfil de sistema tiene modelo y binario listos para lanzar.
    Q_INVOKABLE bool systemProfileReady(const QString &launchId);
    // "Instalar ambos": baja binarios+modelos+mmproj+drafters de todos los extras
    // y deja activo el primero (coding).
    Q_INVOKABLE void acceptShowcase();
    // Instala UN solo perfil del showcase (coding o general) sin bajar el otro.
    Q_INVOKABLE void acceptShowcaseOne(const QString &launchId);
    // Inyecta el resumen de hardware (solo para tests headless).
    // vramTotalGb <= 0 significa "una sola placa" (total = vramGb).
    Q_INVOKABLE void setHardwareSummaryForTest(double vramGb, double ramGb,
                                               const QString &gpuName,
                                               double vramTotalGb = 0.0,
                                               int gpuCount = 0);

    // ── Modo Charla (voz-a-voz) ──
    QString voiceState() const;
    bool    voiceActive() const;
    double  voiceLevel() const;
    QString voiceError() const;
    QString voicePartial() const { return m_voicePartial; }
    QVariantMap voiceLatencyStats() const;
    // Config de Charla POR LaunchProfile (vive en el perfil; la Charla usa la del
    // perfil activo). El setter persiste en el perfil y, si es el activo, la aplica
    // al controller vivo.
    Q_INVOKABLE QVariantMap voiceConfig(const QString &profileId) const;
    Q_INVOKABLE void setVoiceConfig(const QString &profileId, const QVariantMap &cfg);
    Q_INVOKABLE QVariantMap recommendedVoiceTts(const QString &profileId) const;
    // Plan automático de recursos para Charla: GPU más débil para voz/auxiliares
    // y reparto de la VRAM restante entre las GPU visibles por el LLM.
    Q_INVOKABLE QVariantMap voiceGpuPlan() const;
    Q_INVOKABLE QVariantMap charlaAgentCapability() const;
    Q_INVOKABLE void startCharla();   // arranca la sesión de voz (usa el backend de chat)
    Q_INVOKABLE void stopCharla();
    Q_INVOKABLE void toggleDictation();
    bool dictationActive() const { return m_dictationActive; }
    QString dictationText() const { return m_dictationText; }
    // Tuning del perfil activo para voz: cambios recomendados (vacío = perfil ya
    // óptimo), aplicar+relanzar el server con overrides temporales (NO persiste
    // el perfil) y arrancar la charla al quedar listo. charlaAutoTune = aplicar
    // siempre sin preguntar (checkbox del popup).
    Q_INVOKABLE QVariantList charlaTuneRecommendations() const;
    Q_INVOKABLE void applyCharlaTuneAndStartCharla();
    Q_INVOKABLE bool charlaAutoTune() const;
    Q_INVOKABLE void setCharlaAutoTune(bool on);
    Q_INVOKABLE void charlaListen();  // fuerza escucha (corta el TTS si suena)
    Q_INVOKABLE void charlaPushToTalkStart(); // abre el micrófono al pulsar
    Q_INVOKABLE void charlaPushToTalkStop();  // cierra el turno al soltar
    // Micrófonos disponibles: [{id,name,isDefault}].
    Q_INVOKABLE QVariantList audioInputDevices() const;
    // Micrófono elegido (persistido en setting "voiceInputDevice"; "" = default).
    Q_INVOKABLE QString voiceInputDevice() const;
    Q_INVOKABLE void setVoiceInputDevice(const QString &id);
    // Prueba de micrófono: captura y muestra nivel sin STT/chat (ver voiceLevel).
    Q_INVOKABLE void startMicTest();
    Q_INVOKABLE void stopMicTest();
    // ── STT gestionado (Whisper HTTP o Parakeet nativo) ──
    Q_INVOKABLE QVariantList voiceSttCatalog() const;
    Q_INVOKABLE bool voiceModelInstalled(const QString &engineId) const;
    Q_INVOKABLE bool voiceSttBinaryAvailable(const QString &engineId) const;
    Q_INVOKABLE void installVoiceModel(const QString &engineId);
    Q_INVOKABLE void installVoicePrerequisites(const QString &engineId);
    Q_INVOKABLE void cancelVoiceModelInstall();
    // Ruta del binario whisper-server (setting global; "" = buscar en PATH).
    Q_INVOKABLE QString voiceWhisperServerPath() const;
    Q_INVOKABLE bool voiceWhisperServerAvailable() const;
    Q_INVOKABLE void setVoiceWhisperServerPath(const QString &path);
    Q_INVOKABLE QString pickVoiceWhisperServer();   // diálogo de archivo; devuelve la ruta
    // ── TTS gestionado (piper, process-mode) ──
    Q_INVOKABLE QVariantList voiceTtsCatalog() const;
    Q_INVOKABLE bool voiceTtsVoiceInstalled(const QString &voiceId) const;
    Q_INVOKABLE void installVoiceTts(const QString &voiceId);
    Q_INVOKABLE QString voicePiperPath() const;
    Q_INVOKABLE bool voicePiperAvailable() const;
    Q_INVOKABLE void setVoicePiperPath(const QString &path);
    Q_INVOKABLE QString pickVoicePiper();
    // Auto-descarga de binarios (kind: "whisper-server"|"piper"). Al terminar OK
    // fija la ruta correspondiente. urlOverride vacío = URL por defecto del SO.
    Q_INVOKABLE void installVoiceBinary(const QString &kind, const QString &urlOverride = QString());
    Q_INVOKABLE QString voiceBinaryDefaultUrl(const QString &kind) const;
    // ── Pocket TTS (Kyutai, CPU/offline, sidecar residente) ──
    Q_INVOKABLE QVariantMap voicePocketStatus(const QString &profileId = QString()) const;
    Q_INVOKABLE bool voicePocketAvailable() const;
    Q_INVOKABLE QString voicePocketPythonPath() const;
    Q_INVOKABLE void setVoicePocketPythonPath(const QString &path);
    Q_INVOKABLE QString pickVoicePocketPython();
    Q_INVOKABLE void installVoicePocket();
    Q_INVOKABLE void cancelVoicePocketInstall();

    static QVariantMap scoreAgentBenchmarkAcceptanceForTest(const QString &workspace,
                                                            const QString &finalText,
                                                            const QVariantList &benchTasks,
                                                            const QStringList &files);
    static QString benchmarkTaskArtifactNameForTest(const QString &taskId);
    static int benchmarkStreamingDeltaForTest(QString *previous, const QString &current);
    static bool benchmarkTurnBusyForTest(const QString &message);
    static bool benchmarkRepairStagnationCheckForTest(bool agentBusy,
                                                       bool workspaceChanged);
    static bool benchmarkLogIndicatesOutOfMemoryForTest(const QString &message);
    static QStringList benchmarkMemoryPolicyArgsForTest(const QStringList &baseArgs,
                                                        int attempt);

    // Puntúa las respuestas de texto del agente con el evaluador que ya trae cada
    // tarea del benchmark. Sin esto, una suite cuyas tareas se contestan en el chat
    // (la "Corta": "responde YES o NO", "devolvé sólo JSON") queda sin score: el
    // modo agente sólo sabía puntuar archivos producidos en el workspace.
    static QVariantMap scoreBenchTextResponsesForTest(const QString &mode,
                                                      const QVariantList &benchTasks,
                                                      const QVariantList &messages,
                                                      const QString &extraText = QString());

    // Corre el evaluador de UNA tarea del benchmark sobre un texto. Existe para
    // poder testear los evaluadores con respuestas reales de modelos: son texto
    // libre de un LLM y se rompen con el formato (markdown, <think>, espaciado).
    static bool evalBenchTaskForTest(const QString &mode, const QString &taskId,
                                     const QString &text);
    // Todo lo que el agente escribió en el workspace, como un solo texto: en modo
    // agente el mensaje final suele venir vacío porque el trabajo quedó en los
    // archivos, y sin esto las tareas de código puntúan 0 aunque estén bien.
    static QString benchWorkspaceText(const QString &workspace, const QStringList &files);

    // Un score de calidad parcial (3/5) NO es una corrida fallada; que falte un
    // archivo pedido, sí. Separa una cosa de la otra.
    static bool benchHardCriteriaFailed(const QVariantList &acceptanceRows);

signals:
    void voiceStateChanged();
    void voiceLevelChanged();
    void voicePartialChanged();
    void voiceLatencyStatsChanged();
    void dictationChanged();
    void voiceInstallProgress(const QString &engineId, int pct, const QString &status);
    void voiceInstallFinished(const QString &engineId, bool ok, const QString &message);
    void voiceBinaryInstalled(const QString &kind, bool ok, const QString &message);
    void updateCheckChanged();
    // Un perfil cloud necesita su API key y no se pudo resolver (ni env var ni store):
    // la UI debe pedirla y llamar setSecret(keyRef, value) antes de reintentar.
    void cloudSecretRequired(const QString &launchProfileId, const QString &keyRef);
    void serverRunningChanged();
    void backendAvailableChanged();
    // Otra instancia intentó abrirse (single-instance): la UI restaura/enfoca la
    // ventana existente en vez de abrir una nueva.
    void secondInstanceLaunched();
    // Pedido de re-mostrar el asistente inicial (botón "Repetir asistente").
    void showSetupRequested();
    void serverReadyChanged();
    void serverStateChanged();
    void thinkingRestartingChanged();
    void serverStatsChanged();
    void serverHasVisionChanged();
    void gitAvailableChanged();
    // El agente pidió subagents pero falta git → la UI ofrece instalarlo.
    void gitRequiredForSubagents();
    void serverLogChanged();
    void activeLaunchIdChanged();
    void browserAutomationChanged();
    void browserSkillsChanged();
    void teachChanged();
    void effectiveProfileChanged();
    void routerStateChanged();
    void setupStateChanged();
    void installingOfficialBinaryChanged();
    void officialBinaryInstallStatusChanged();
    void officialBinaryInstallLogChanged();
    void officialBinaryInstallFinished(bool success, const QString &message, const QString &binaryPath);
    void integrationsChanged();
    void integrationTestResult(const QString &id, bool ok, const QString &message);
    void serverError(const QString &message);
    void serverPortCollision(const QString &launchProfileId, const QString &host,
                             int requestedPort, int suggestedPort, bool startAgent);
    // Diagnóstico detectado por regex en el log del server (OOM, puerto, modelo cargado…).
    // level: "error" | "warn" | "info".
    void serverDiagnostic(const QString &level, const QString &message);
    void smokeTestFinished(bool passed, const QString &output);
    void languageChanged();
    void harnessStatusChanged();
    void harnessInstallFinished(bool success, const QString &adapter, const QString &message);
    void chatSessionsChanged();
    void chatMessagesChanged();
    void chatStreamingChanged();
    void chatGeneratingChanged();
    void chatThinkingSupportedChanged();
    void chatThinkingChanged();
    void chatPersonaDesignerChanged();
    void chatSamplingChanged();
    void thinkingChanged();
    void mermaidEnabledChanged();
    void agentRunningChanged();
    void nativeAgentRunsChanged();
    void agentStartingChanged();
    void activeProfileToolSupportChanged();
    void agentLogChanged();
    // Evento normalizado del ciclo del backend (no depende de Claude/Codex/
    // OpenCode); sirve para integraciones y diagnóstico sin parsear texto de log.
    void agentLifecycleEvent(const QVariantMap &event);
    void agentMessagesChanged();
    void agentStreamingChanged();
    void personaStyleAnalysisChanged();
    void agentQueueChanged();
    void chatQueueChanged();
    void agentSessionsChanged();
    void agentPendingToolChanged();
    void agentApprovalModeChanged();
    void agentThinkingChanged();
    void agentToolsChanged();
    void activeAgentProfileChanged();
    void activeAgentDefinitionChanged();
    void agentTeacherChanged();
    void agentAuxiliaryChanged();
    void auxiliaryJobsChanged();
    void mailAutoSendChanged();
    void hitlDestructiveChanged();
    void desktopIndicatorChanged();
    void autoStartAgentOnLaunchChanged();
    void gatewayChanged();
    void lanServersChanged();
    void lanProfileReady(const QString &launchProfileId, const QString &error);
    void idleAutoStopChanged();
    void agentContextChanged();
    void agentTuningChanged();
    void benchmarkRunningChanged();
    void benchmarkProgressChanged();
    void benchmarkStatusChanged();
    void benchmarkResultsChanged();
    void customBenchmarksChanged();
    void autoTuneChanged();
    // Cada trial evaluado: índice/total, score mezclado, calidad [0,1], resumen
    // de flags, y el desglose pp/tg (-1 = no medido).
    void autoTuneTrial(int index, int total, double throughput, double quality,
                       const QString &summary, double promptTps, double genTps);
    // Fin del tuning: ok=true si encontró config válida; bestArgs ya fusionados.
    // newProfileId: perfil "Opti - " creado (vacío si no se creó ninguno).
    void autoTuneFinished(bool ok, const QString &bestArgs, double throughput,
                          double quality, const QString &newProfileId);
    void researchChanged();
    void researchReportsChanged();
    void researchFinished(const QString &id, const QString &title);
    void hardwareSummaryChanged();
    void modelRecommendationsChanged();
    void startupChanged();
    void devModeChanged();
    void performanceChanged();
    void modelDownloadChanged();
    void downloadHistoryChanged();
    void launchProfileSelected(const QString &launchProfileId);
    // Pedido a la UI de abrir la sección "Descargas" (p.ej. al instalar deps).
    void navigateToDownloads();
    void tasksSchedulerChanged();
    void taskRunStateChanged();
    void taskAbChanged();
    void taskAbFinished(const QString &id, const QVariantMap &comparison);
    void taskRunAvailabilityChanged();
    void taskRunFinished(const QString &id, const QString &name, const QString &status,
                         const QString &summary, bool silentUnlessError);
    void taskRunTraceChanged();
    void taskLivePreviewChanged();

private:
    void cleanupDuplicateInitialLaunchProfiles();
    void appendLog(const QString &text);
    void appendServerEvent(const QString &source, const QString &text);
    // Escanea líneas del server por patrones conocidos y emite serverDiagnostic.
    void detectServerLogPatterns(const QString &text);
    void launchTaskBody(const QString &id, const QVariantMap &task);
    void onTriggerPathChanged(const QString &path);
    // Reproducción fiel: arranca/avanza el player determinista de pasos de escritorio.
    // Devuelve false si no hay pasos mecánicos (→ el caller usa el replay adaptativo).
    bool startDesktopReplay(const QString &id, const QString &artifactId);
    void playNextReplayStep();
    void finishDesktopReplay();
    void updateTeachStopOverlay();
    void registerHotkeys();          // (re)registra los atajos globales de las Tasks
public:
    void onHotkeyPressed(int hotkeyId);   // llamado por el filtro de eventos nativos
private:
    void appendAgentEvent(const QString &source, const QString &text);
    // Verify-phase swap: manda `prompt` al agente, cambiando antes el modelo a
    // `targetLaunchId` si difiere del activo (reinicia server+agente; sesión
    // nueva). `freshSession` limpia la sesión cuando NO hay swap. Si targetLaunchId
    // está vacío o coincide con el activo, manda directo.
    void sendForPhaseProfile(const QString &targetLaunchId, const QString &prompt,
                             bool freshSession);
    QString taskVerificationProfile(const QVariantMap &task);
    QString runtimeLogDir() const;
    void rotateLogIfNeeded(const QString &path) const;
    void appendFileLog(const QString &path, const QString &line) const;
    void finishSmokeTest(bool passed, const QString &output);
    QVariantMap voiceGpuPlanForLaunch(const QString &launchId,
                                      const VoiceConfig &config) const;
    QVariantMap voiceGpuPlanForConfig(const VoiceConfig &config) const;
    EffectiveProfileBuilder::Context buildContext(const QString &launchProfileId);
    // Resuelve la cadena de fallbacks del maestro (keys/cliPath/profile→http) a
    // una QVariantList lista para el worker del agente.
    QVariantList buildMasterChain(const MasterConfig &mc);

    BinaryRegistry    m_binaries;
    ModelCatalog      m_catalog;
    ModelRootRegistry m_roots{&m_catalog};
    ProfileManager    m_profiles;
    AgentDefinitionStore m_agentDefinitions;
    TriggerManager m_triggerManager;
    QString m_activeAgentDefinitionId;
    QStringList m_pendingTriggeredTasks;
    TaskStore         m_tasks;
    // Watcher de triggers fileWatch: mapea cada path vigilado → ids de Task a correr,
    // con debounce por Task (evita ráfagas de eventos de guardado).
    QFileSystemWatcher *m_taskWatcher = nullptr;
    QHash<QString, qint64> m_triggerLastFire;   // id de Task → epoch ms del último disparo
    // Atajos globales (RegisterHotKey): id de atajo Win32 → id de Task. El filtro de
    // eventos nativos (opaco para no meter windows.h en el header) traduce WM_HOTKEY.
    QHash<int, QString> m_hotkeyTaskIds;
    void *m_hotkeyFilter = nullptr;
    // Reproducción fiel (determinista) de un Teach de escritorio: se ejecutan los
    // pasos grabados (key/type/click/stroke) tal cual con DesktopAutomationBackend,
    // sin pasar por el modelo → un dibujo sale igual. El agente sólo verifica al final.
    QVariantList m_replaySteps;
    int          m_replayIndex = 0;
    QString      m_replayScopeKind;
    QString      m_replayScopeId;
    QString      m_replayTaskId;
    QString      m_replayArtifactId;
    QVariantList m_replayTemplateRows;
    QVariantList m_replayReport;   // {n,tool,ok,summary} por paso (auditoría honesta)
    int          m_replayErrors = 0;
    bool         m_replaySingleStep = false;
    int          m_visualVerificationDesktopActions = 0;
    AutomationStore   m_automations;
    DataLabStore      m_dataLab;
    TaskScheduler    *m_scheduler = nullptr;
    AuxiliaryJobScheduler *m_auxiliaryScheduler = nullptr;
    ModelRoleRegistry m_modelRoles;
    AssistantRuntime m_assistantRuntime;
    QString m_activeAssistantMessageId;
    // Task en ejecución (para marcar lastRun ok al terminar el turno).
    QString  m_runningTaskId;
    // Automatización que disparó la corrida actual (si vino del scheduler/UI de
    // automatizaciones). Permite marcar su lastRun al terminar. Vacío = corrida
    // directa de un proceso.
    QString  m_runningAutomationId;
    QString  m_runningTaskName;
    QString  m_runningTaskPhase;
    QVariantList m_taskRunTimeline;
    QVariantList m_taskRunExtraTimeline;
    QVariantMap m_taskRunPreview;
    bool m_taskLivePreviewEnabled = false;
    bool m_taskPaused = false;
    QString  m_runningTaskPostPrompt;
    WorkflowRunner *m_workflowRunner = nullptr;
    QJsonObject m_runningWorkflowDefinition;
    QVariantMap m_workflowApproval;
    bool m_workflowStepInFlight = false;
    QVariantMap m_runningTaskMetricsBaseline;
    AgentToolRunner *m_workflowToolRunner = nullptr;
    QVariantMap m_pendingDirectTool;
    QHash<QString, SubAgentRunner *> m_workflowBranches;
    QVariantMap m_workflowBranchResults;
    bool m_workflowBranchFailed = false;
    QString m_taskAbId;
    QString m_taskAbStatus;
    int m_taskAbStage = 0;
    qsizetype m_runningTaskLogStart = 0;
    bool     m_runningTaskSilentUnlessError = false;
    // Estado del bucle "correr hasta cumplir objetivo" (feature Loops). El
    // contador cuenta corridas del cuerpo ya completadas; el goal-check corre
    // entre iteraciones y decide si re-disparar el cuerpo (ver TaskStore::decideLoop).
    bool     m_runningTaskLoopEnabled = false;
    int      m_runningTaskLoopIteration = 0;
    int      m_runningTaskLoopMaxSeconds = 0;
    qint64   m_runningTaskLoopStartedAtMs = 0;
    // Data-driven (RPA por lote): mismo flujo, una corrida del cuerpo por fila del
    // dataset. m_dataTaskId marca qué Task está iterando (evita re-resolver el
    // dataset en cada relanzamiento); m_dataRows son las filas resueltas; el índice
    // apunta a la fila en curso. La fila actual se sustituye ({{var}}) en el prompt.
    QString      m_dataTaskId;
    QVariantList m_dataRows;
    int          m_dataIndex = 0;
    QVariantMap  m_runningTaskRow;   // fila en curso (para expandir postprompt/loop)
    // On-error/reintentos (RPA robusto): reintenta el cuerpo ante fallo hasta
    // maxRetries antes de darlo por perdido; el lote data-driven sigue o corta según
    // datasetOnError. m_pendingRetry marca que el próximo launch es un reintento (no
    // resetea el contador ni avanza de fila).
    int          m_attemptRetry = 0;
    int          m_attemptRetryMax = 0;
    bool         m_pendingRetry = false;
    QString      m_datasetOnError = QStringLiteral("continue");
    // Routing multi-modelo (verify-phase swap): perfil de ejecución vs perfil de
    // verificación del goal-check. Si difieren, el goal-check del bucle corre en
    // el modelo de verificación (sesión nueva: se auto-verifica con herramientas)
    // y el cuerpo vuelve al de ejecución. Vacío m_runningTaskVerifyLaunchId = sin
    // routing (comportamiento normal).
    QString  m_runningTaskExecLaunchId;
    QString  m_runningTaskVerifyLaunchId;
    QString  m_pendingSwapPrompt;        // prompt a mandar cuando el swap termina
    bool     m_pendingSwapFreshSession = false;  // limpiar sesión antes de mandar
    QHash<QString, QString> m_taskWorkLogs;
    RunHistoryStore  m_runHistory;
    ManagedAgentRunStore m_managedAgentRuns{&m_runHistory};
    QHash<QString, QPointer<IAgentBackend>> m_managedDelegationBackends;
    QVariantList m_nativeAgentRuns;
    QStringList nativeAgentRunRoots() const;
    QString nativeAgentRunRoot(const QString &runId) const;
    DownloadHistoryStore m_downloadHistory;
    // Inicio de la corrida actual (para registrar el historial al terminar).
    QString  m_runningTaskStartedAt;
    // Task programada esperando que el agente auto-iniciado quede listo.
    QString  m_pendingScheduledTaskId;
    QString  m_pendingScheduledLaunchId;
    QString  m_pendingAutomationStartupId;
    // El agente fue auto-iniciado por el scheduler → apagarlo al terminar el turno.
    bool     m_scheduledAutoStop = false;
    void dispatchPendingScheduledTask();
    // Maneja el fin de turno del agente (marca lastRun, apaga si fue auto-iniciado).
    void onAgentTurnFinished();
    void startOrRestoreTaskWorkflow(const QVariantMap &task);
    QString workflowStepPrompt(const QString &stepId, const QString &type,
                               const QVariantMap &step, const QVariantMap &context) const;
    void finishRunningTask(const QString &status, const QString &summary);
    void refreshTaskRunTrace();
    void appendTaskTraceResult(const QVariantMap &result);
    // Registra en el historial una corrida que falló ANTES de arrancar el cuerpo
    // (gating/validación). finishRunningTask sólo graba lo que llegó a correr, así
    // que sin esto los errores tempranos no aparecían en Historial. Marca estado
    // de error en el Proceso (y la Programación si vino de una) y appendea el
    // registro a ambos owners.
    void recordEarlyFailure(const QString &processId, const QString &summary);
    // Una vez al arranque: por cada Proceso con scheduleEnabled (modelo viejo
    // pre-split) crea una Automatización enlazada si aún no existe ninguna.
    void migrateLegacySchedulesToAutomations();
    QString latestAgentAssistantText() const;
    bool agentBackendBusy() const;
    void prepareTaskAgentSession();
    // Aplica/limpia permisos de filesystem + auto-aprobación de tools de una Task.
    void applyTaskAgentPermissions(const QVariantMap &task);
    void clearTaskAgentPermissions();

    QProcess *m_proc = nullptr;
    QProcess *m_installerProc = nullptr;
    // Fuente del instalador de binarios (parametriza installOfficialBinary):
    // repo GitHub + etiqueta (kind en el registro) + si exige CUDA (MTP build).
    QString m_installSourceRepo  = QStringLiteral("ggml-org/llama.cpp");
    QString m_installSourceLabel = QStringLiteral("official");
    QString m_installReleaseTag;
    bool    m_installRequireCuda = false;
    bool    m_installRequireCpu = false;
    void startBinaryInstall();   // cuerpo común (antes en installOfficialBinary)
    void startSourceBuildInstall(const EngineCatalogEntry &entry);
    QProcess *m_smokeTestProc = nullptr;
    QTimer   *m_smokeTestTimer = nullptr;
    QString   m_smokeTestLog;
    bool      m_smokeTestDone = false;
    QString   m_log;
    QString   m_serverLogFilePath;
    QString   m_activeLaunchId;
    QString   m_pendingAutoAgentLaunchId;
    QString   m_hybridExecutorLaunchId;
    QString   m_hybridPlannerLaunchId;
    QString   m_hybridUserRequest;
    QString   m_hybridPlan;
    QString   m_hybridPlanningContext;
    QString   m_hybridPlanCacheKey;
    QString   m_hybridFailure;
    QStringList m_hybridAttachments;
    QString   m_hybridPhase;
    QPointer<QNetworkReply> m_hybridReply;
    QTimer   *m_hybridProgressWatchdog = nullptr;
    QByteArray m_hybridStreamBuffer;
    QString   m_hybridStreamPlan;
    bool      m_hybridStreamDone = false;
    bool      m_hybridStalled = false;
    bool      m_hybridDispatching = false;
    bool      m_serverStopping = false;
    bool      m_serverReady    = false;
    bool      m_remoteServerActive = false;
    bool      m_serverHasVision = false;
    bool      m_serverGpuRequested = false;
    bool      m_serverGpuDeviceSeen = false;
    bool      m_serverCpuDeviceSeen = false;
    bool      m_serverGpuFallbackWarned = false;
    bool      m_gitAvailable = false;
    QProcess *m_gitInstallProc = nullptr;
    QTimer   *m_stopKillTimer  = nullptr;
    QTimer   *m_healthPollTimer = nullptr;
    void startHealthPolling();
    void stopHealthPolling();
    // Arranque diferido del agente cuando el server queda listo. Si quedó un
    // agente vivo (teardown async no observado) NO relanza, pero baja
    // m_agentStarting para no dejar "Iniciando agente" trabado. Compartido por
    // el ready-branch del health-poll y el arranque síncrono de startServerAndAgent.
    void maybeStartPendingAgentOnReady();
    void startSequentialHybrid(const QString &text, const LaunchProfile &executor);
    void requestHybridPlan();
    QString buildHybridPlanningContext() const;
    QString hybridPlanCachePath(const QString &key) const;
    bool loadHybridPlanCache();
    void saveHybridPlanCache() const;
    void persistHybridJournal() const;
    void finishHybridPlanning(const QString &plan, const QString &error = QString());
    void startHybridExecutor();
    void dispatchHybridRequest();
    void resetHybridRun();
    void setHybridPhase(const QString &phase);
    // Watchdog auto-restart on crash
    QString   m_serverState = QStringLiteral("stopped"); // stopped|running|restarting|failed
    int       m_serverRestartCount = 0;
    QTimer   *m_serverRestartTimer = nullptr;
    static constexpr int kMaxServerRestarts = 3;
    void setServerState(const QString &s);
    // Live VRAM/stats meter (nvidia-smi async poll while server runs)
    QTimer   *m_vramPollTimer = nullptr;
    QProcess *m_vramProc = nullptr;
    QVariantMap m_serverStats;
    // Última muestra de nvidia-smi, conservada aunque el server se detenga
    // durante un rebalanceo. Así el próximo plan no confunde VRAM ocupada por
    // otras aplicaciones con VRAM libre.
    QVariantList m_lastLiveGpus;
    QString m_charlaGpuRebalanceCandidate;
    int m_charlaGpuRebalanceSamples = 0;
    bool m_charlaGpuRebalancePending = false;
    bool m_charlaGpuRebalanceWarned = false;
    void startVramPolling();
    void stopVramPolling();
    void pollServerStats();
    void maybeRebalanceCharlaGpuPlan();
    // Aplica el power limit configurado al arrancar un server: usa el override del
    // launch profile (powerLimitW>0) o, si no, el global "gpuPowerLimitW". No-op si
    // ambos son 0 o no hay nvidia-smi. Loguea a eventos del server.
    void applyConfiguredPowerLimit(const LaunchProfile &launch);
    QVariantMap m_effectiveProfile;
    QStringList m_routerModelNames;   // secciones cargadas en el router activo
    QString m_routerActiveModel;      // sección activa (campo "model" de los requests)
    int m_benchHardTimeoutSec = 0;    // 0 = sin límite; timeout duro (wall-clock) por corrida
    bool m_serverIsRouter = false;    // el m_proc actual es un router
    // Devuelve la sección activa del router si corresponde, sino el fallback.
    QString routedModelId(const QString &fallback) const {
        return (m_serverIsRouter && !m_routerActiveModel.isEmpty()) ? m_routerActiveModel : fallback;
    }
    bool m_installingOfficialBinary = false;
    QString m_officialBinaryInstallStatus;
    QString m_officialBinaryInstallLog;
    bool m_cancelingOfficialBinaryInstall = false;
    bool m_timeoutOfficialBinaryInstall = false;
    QDateTime m_lastInstallProgressAt;
    QTimer *m_installWatchdog = nullptr;
    QString m_language;
    bool m_installingHarness = false;
    QString m_harnessInstallStatus;
    QProcess *m_harnessProc = nullptr;
    QProcess *m_agentProc = nullptr;
    qint64    m_agentPid = 0;
    bool      m_agentInTerminal = false;
    QTimer   *m_agentPollTimer = nullptr;
    QString   m_agentLog;
    QString   m_agentLogFilePath;
    bool      m_agentStarting = false;
    // Soporte de tool-calling del perfil activo (cookbook + chat-template).
    QString   m_activeProfileToolSupport = QStringLiteral("unknown");
    bool      m_toolTemplateHave = false;       // /props respondió con chat_template
    bool      m_toolTemplateSupports = false;   // y ese template menciona tools
    void recomputeToolSupport();
    QString   m_activeAgentAdapter;
    QString   m_activeHarnessEngineId = QStringLiteral("legacy");
    QString   m_agentCwdOverride;   // directory for next/current agent start
    QString   m_pendingAgentLaunchId; // used when restarting for project change
    // pi harness: modo print por-mensaje (sin proceso persistente)
    bool                m_piActive = false;
    QProcess           *m_piMsgProc = nullptr;
    QProcessEnvironment m_piEnv;
    QString             m_piExe;
    QString             m_piCwd;
    QString             m_piSessionPath;
    // Backend de agente activo (opencode/goose/raw). Dueño de su proceso/conexión.
    IAgentBackend      *m_agentBackend = nullptr;
    // Crear una sesión desde un delegate QML muta el modelo del ListView. Se
    // difiere al próximo ciclo para no destruir ese delegate dentro de su click.
    bool                m_agentSessionCreateQueued = false;
    // Backend de chat directo (raw), separado del modo Agente.
    IAgentBackend      *m_chatBackend = nullptr;
    // Modo Charla (voz-a-voz): orquestador STT→chat→TTS. Reusa m_chatBackend.
    class VoiceController *m_voice = nullptr;
    VoiceServerManager m_voiceServers;  // catálogo + descarga de modelos STT
    QProcess *m_sttProc = nullptr;      // server STT gestionado (whisper.cpp)
    QProcess *m_externalSttProc = nullptr; // endpoint STT local configurado por el usuario
    QProcess *m_externalTtsProc = nullptr; // endpoint TTS local configurado por el usuario
    QProcess *m_pocketInstallProc = nullptr; // venv + paquete + cache inicial
    QString m_pocketInstallBasePython;
    int m_pocketInstallStep = 0;
    bool m_pocketInstallFinishing = false;
    QString m_pendingVoicePrerequisitesEngine;
    bool m_charlaActive = false;
    bool m_charlaTuneOnNextLaunch = false;  // startServer aplica overrides de voz
    bool m_charlaStartAfterRelaunch = false; // arrancar charla al serverReady
    bool m_charlaGpuPlanActive = false;      // auto reparto durante una sesión de voz
    bool m_serverUsesVoiceGpuPlan = false;   // reparto aplicado al server actual
    QString m_serverVoiceGpuPlanSignature;
    QVariantMap m_serverVoiceGpuPlan;
    bool m_charlaHasVoiceConfigOverride = false;
    VoiceConfig m_charlaVoiceConfigOverride;
    // Ingi Charla: el turno actual se ruteó al agente (computer-use/visión) en vez
    // del chat backend. Decide quién habla la respuesta final (onAgentTurnFinished).
    bool m_charlaUseAgent = false;
    bool m_voiceCursorOcr = false;   // espejo de VoiceConfig::cursorOcr (opt-in)
    // Burbuja del agente que se está hablando en vivo (streaming incremental de TTS
    // en Charla). -1 = ninguna. Se pasa a VoiceController::speakFlush al cerrar.
    int m_charlaStreamBubble = -1;
    bool m_chatWasGenerating = false;
    QString m_voicePartial;
    bool m_dictationActive = false;
    QString m_dictationText;
    void ensureVoice();                 // crea + configura el controller (lazy)
    void applyVoiceConfig();            // empuja config + keys resueltas al controller
    // Fuerza STT (whisper) y voz piper (TTS) al idioma de la app (m_language), así
    // Ingi Charla no mezcla idiomas por mala detección de whisper en auto.
    void applyAppLanguageToVoice(VoiceConfig &c) const;
    bool startManagedStt(const VoiceConfig &c);  // lanza el STT gestionado del perfil activo
    bool startManagedExternalVoice(const VoiceConfig &c, bool stt);
    void stopManagedExternalVoice();
    void continueVoicePrerequisitesInstall();
    void runPocketInstallStep();
    void finishPocketInstall(bool ok, const QString &message);
    void stopManagedStt();
    QString voiceConfigPath() const;
    // Chat session state
    QString       m_chatProjectIdOverride;
    QString       m_chatProjectNameOverride;
    QVariantList  m_chatSessions;
    QVariantList  m_chatMessages;
    QString       m_chatSessionId;
    QString       m_chatSessionTitle;
    bool          m_chatGenerating = false;
    bool          m_chatThinkingSupported = false;
    bool          m_chatThinkingEnabled = false;
    QString       m_chatReasoningEffort;
    bool          m_chatPersonaDesigner = false;
    double        m_chatTemperature = -1.0;
    double        m_chatTopP = -1.0;
    int           m_chatTopK = -1;
    double        m_chatMinP = -1.0;
    double        m_chatRepeatPenalty = -1.0;
    int           m_chatStreamingIndex = -1;
    QString       m_chatStreamingText;
    void fetchChatThinkingSupport();
    QNetworkReply *m_chatReply = nullptr;
    int           m_chatAssistantIdx = -1;
    QString       chatStorageDir() const;
    void          loadChatSessions();
    // Agrega la sesión activa aún no persistida (sin mensajes) al vector ANTES de
    // agrupar/ordenar, para que caiga dentro del grupo de su proyecto (no como
    // sección duplicada al tope).
    void          injectDraftSession(QVector<QVariantMap> &sessions);
    void          saveChatSession();
    void          loadChatSessionMessages(const QString &id);

    QNetworkAccessManager *m_nam = nullptr;
    // Espejos del backend de agente activo (poblados desde IAgentBackend).
    QString   m_opencodeSessionId;
    QString   m_opencodeSessionTitle;
    bool           m_agentStopping = false;   // reservado para path genérico (stdin)
    QVariantList m_agentMessages;
    AgentRoomStore *m_agentRoomStore = nullptr;
    QString m_activeRoomId;
    QString m_activeRoomCorrelationId;
    QString m_activeRoomPreset;
    int       m_agentStreamingIndex = -1;
    QString   m_agentStreamingText;
    int       m_agentQueuedCount = 0;
    int       m_chatQueuedCount = 0;
    int       m_currentAssistantIdx = -1;
    QVariantList m_agentSessions;
    QVariantMap m_agentPendingTool;   // tool esperando aprobación ({} si ninguna)
    QString   m_agentApprovalMode = QStringLiteral("ask");  // auto | ask | manual | super
    bool      m_agentThinkingEnabled = false;   // razonamiento agente/benchmark/research
    bool      m_launchThinkingEnabled = false;  // razonamiento duro del próximo llama-server
    bool      m_restartThinkingAfterResponse = false;
    bool      m_restartThinkingWithAgent = false;
    bool      m_thinkingRestarting = false;
    void      restartActiveLaunchForThinking(bool withAgent, bool cancelActiveGeneration);
    // Automatización de browser (MCP Playwright). Toggle global; cada LaunchProfile
    // puede forzar on/off con browserAutomation ("inherit"|"on"|"off").
    bool      m_browserAutomationEnabled = false;
    QString   m_browserMcpCommand = QStringLiteral("npx @playwright/mcp@latest");
    QProcess *m_browserRecordProc = nullptr;   // codegen en curso (modo teach)
    TeachSessionRecorder m_teachRecorder;
    QPointer<QWidget> m_teachStopOverlay;
    QPointer<QWidget> m_teachRegionOverlay;
    bool      m_mermaidEnabled = true;          // render de diagramas mermaid en el chat
    QStringList m_agentDisabledTools;           // tools built-in apagadas por el usuario
    // Perfil de agente activo (override vivo del modo agente). Vacío = resolver
    // desde el launch activo / preset por defecto. No se persiste (es de sesión).
    QString   m_activeAgentProfileId;
    // Resuelve el id efectivo (override → launch activo → preset por defecto) y
    // aplica capacidades+directivas+ajustes del perfil al backend del agente.
    QString   resolveAgentProfileId() const;
    void      applyActiveAgentProfile();
    // Aplica solo capacidades (enabledTools→disabledTools, expandiendo "*") +
    // directivas de un perfil a un backend dado. NO toca approval ni
    // tuning (los maneja el caller). Reusado por el agente vivo y el benchmark.
    void      applyAgentProfileCaps(class LlamaAgentBackend *cb, const AgentProfile &ap);
    // Baja los módulos del HarnessSpec (loop/contexto/permisos/escalación/
    // protocolo/directivas de usuario) al backend. Ver docs/harness.md.
    void      applyHarnessSpec(class LlamaAgentBackend *cb, const HarnessSpec &spec);
    // Expande el sentinel "*" del catálogo de directivas (sin las opt-in puras).
    static QStringList expandDirectiveSentinel(const QStringList &keys);
    // Aplica el override de una fase declarada en el spec ("plan"|"exec"|
    // "verify"|"goalCheck"). Sin fases declaradas es un no-op.
    void      applyHarnessPhase(const QString &phase);
    // Modulo `chat` del spec al RawChatBackend (sampling, thinking, persona,
    // instrucciones persistentes). No-op si el modulo no fue declarado.
    void      applyChatHarnessSpec(const HarnessSpec &spec);
    // Tuning efectivo (systemExtra + temperatura) de un perfil bajo un spec dado
    // (el resuelto, o el de una fase). Ver la definición para la precedencia.
    void      resolveAgentTuning(const AgentProfile &ap, const HarnessSpec &spec,
                                 QString *systemExtra, double *temperature) const;
    // Alcance de filesystem de la Task en curso ("" = ninguna). El spec del
    // perfil se intersecta con esto: un perfil no puede ampliar permisos.
    QString     m_agentTaskScope;
    QStringList m_agentTaskFolders;
    QString   m_agentTeacherUrl;                // ask_teacher: endpoint OpenAI-compat
    QString   m_agentTeacherModel;
    QString   m_agentTeacherKey;
    QString   m_agentAuxiliaryUrl;              // RAG sidecar: embeddings/rerank
    QString   m_agentAuxiliaryEmbeddingModel;
    QString   m_agentAuxiliaryRerankModel;
    QString   m_agentAuxiliaryKey;
    MasterCli m_masterCli;                      // detección de CLIs maestro (claude/codex)
    SecretStore m_secrets;                       // API keys cloud (fuera del repo)
    bool        m_mailAutoSend = false;          // permitir email_send sin aprobación
    bool        m_hitlDestructive = true;        // guardrail Zero-Autonomy (destructivas → aprobación salvo super)
    bool        m_desktopIndicatorVisible = true;
    bool        m_desktopAgentActive = false;
    bool        m_desktopTaskIndicatorActive = false;
    QString     m_desktopAgentAction;
    bool        m_autoStartAgentOnLaunch = false; // arrancar agente al abrir la app (tasks por horario)

    // Gateway (proxy Anthropic/OpenAI + auto-load)
    LlmGateway *m_gateway = nullptr;
    bool        m_gatewayEnabled = false;
    int         m_gatewayPort = 8088;
    QString     m_gatewayApiKey;
    int         m_gatewayKeepN = 4;
    bool        m_gatewayAutoSwap = true;
    bool        m_gatewayLanEnabled = false;
    QVariantList m_lanServers;
    QUdpSocket  *m_lanDiscoverySocket = nullptr;

    // Idle auto-stop
    int         m_idleAutoStopMin = 0;        // 0 = desactivado
    QTimer     *m_idleTimer = nullptr;
    QElapsedTimer m_lastActivity;
    void        startIdleWatchdog();
    void        stopIdleWatchdog();
    void        wireGatewayHooks();
    void        gatewayEnsureModel(const QString &name);
    QJsonArray  gatewayModelCatalog() const;
    int       m_agentContextUsed = 0;
    int       m_agentContextLimit = -1;
    int       m_agentContextTranscript = 0;
    qint64    m_agentContextPruned = 0;
    int       m_agentContextPruneEvents = 0;
    QString   m_agentSystemPrompt;
    QString   m_agentStyleQuery;
    QString   m_personaStyleAnalysisStatus;
    QString   m_personaStyleAnalysisError;
    QString   m_agentPermRules;
    double    m_agentTemperature = -1.0;
    double    m_resolvedProfileTemperature = -1.0;

    // Managed-process lifecycle
#ifdef Q_OS_WIN
    void *m_jobObject = nullptr;   // HANDLE — typed as void* to avoid pulling windows.h into header
#endif
    void createJobObject();
    void assignToJobObject(qint64 pid);
    // Reenvía las cuentas de correo (password resuelto) + flag auto-send al
    // backend del agente activo, si lo hay.
    void pushMailAccountsToAgent();
    void killManagedOrphans();
    void writeServiceState(const QString &role, qint64 pid, const QVariantMap &extra = {});
    void ensurePiConfig(const QString &openaiBaseUrl);
    void sendPiMessage(const QString &text);
    // Crea/asegura el backend para el adapter dado y conecta señales→QML.
    IAgentBackend *ensureAgentBackend(const QString &adapter,
                                      const QString &harnessEngineId = QStringLiteral("legacy"));
    IAgentBackend *ensureChatBackend();
    // Helpers de config opencode
    QString ocGlobalConfigDir() const;
    QString ocConfigFilePath(const QString &scope, const QString &projectDir) const;
    QString ocCommandDir(const QString &scope, const QString &projectDir) const;
    QJsonObject ocReadConfigObj(const QString &scope, const QString &projectDir) const;
    bool ocWriteConfigObj(const QString &scope, const QString &projectDir, const QJsonObject &obj);
    void clearServiceState(const QString &role);
    QString serviceStatePath() const;

    // Integrations: API services persistidos en JSON propio.
    QString integrationsFilePath() const;
    QJsonArray readApiServices() const;
    bool writeApiServices(const QJsonArray &arr);
    QVariantList webProviderConfigs() const;
    void migrateIntegrationSecrets();
    QJsonObject exportFileSet(const QString &root, const QStringList &relativePaths) const;
    bool importFileSet(const QString &root, const QJsonObject &set, QStringList *written);
    bool removePathForWipe(const QString &path);
    void reloadPersistentStateAfterImportOrWipe();

    static const QHash<QString, QHash<QString, QString>> &translations();

    // Benchmark
    bool         m_benchmarkRunning  = false;
    bool         m_benchmarkCanceled = false;
    QPointer<QNetworkReply> m_benchmarkActiveReply; // in-flight req, aborted on cancel
    QList<QPointer<QNetworkReply>> m_benchmarkReplies; // requests concurrentes en vuelo
    QPointer<IAgentBackend> m_benchmarkAgent;       // dedicated headless agent (agent target)
    int          m_benchmarkProgress = 0;
    QString      m_benchmarkStatus;
    // Perfil de agente elegido para el target "agent" (NIVEL del agente). Vacío =
    // todas las tools. El nombre se guarda en cada resultado para el historial.
    QString      m_benchmarkAgentProfileId;
    QString      m_benchmarkAgentProfileName;
    void         setBenchmarkAgentProfile(const QString &agentProfileId);
    void         importBundledBenchmarkDocuments();
    QVariantList m_benchmarkResults;
    QVariantList m_customBenchmarks;
    QVariantList m_proBenchmarkQueue;
    QStringList m_proBenchmarkProfiles;
    int m_proBenchmarkPasses = 1;
    QString m_proBenchmarkTarget;
    int m_proBenchmarkTimeout = 0;
    QString m_proBenchmarkAgent;
    QString      m_lastEvalImportError;
    // Benchmark-only VRAM escalation. A fresh profile starts at the most
    // aggressive placement and moves down the ladder only after a load OOM.
    // Manual launches keep their declared arguments unchanged.
    int          m_benchmarkMemoryAttempt = -1;
    QStringList  m_benchmarkEffectiveArgs;
    static constexpr int kBenchmarkMaxMemoryAttempts = 7;
    // Auto-tuning
    bool         m_autoTuneRunning = false;
    int          m_autoTuneProgress = 0;
    QString      m_autoTuneStatus;
    QString      m_autoTuneLaunchId;     // perfil objetivo del tuning en curso
    QVariantList m_autoTuneTrials;       // filas para la tabla de la sección Tuner
    QVariantMap  m_autoTuneResult;       // resumen final + perfil creado
    QThread     *m_tuneThread = nullptr;
    TunerWorker *m_tuneWorker = nullptr;
    void onAutoTuneFinished(bool ok, const QStringList &bestArgs,
                            double throughput, double quality,
                            double promptTps, double genTps,
                            double basePromptTps, double baseGenTps,
                            double draftAcceptancePct,
                            double baseDraftAcceptancePct);
    QVariantMap  m_hardwareSummary;
    bool m_startupBusy = false;
    QString m_startupStatus;
    QVariantMap m_startupTimings;
    QElapsedTimer m_startupTimer;
    bool m_startupScanStarted = false;
    bool m_hardwareScanInFlight = false;
    QFutureWatcher<QVariantMap> m_hardwareWatcher;
    bool m_devMode = false;
    QVariantMap m_performanceSnapshot;
    QTimer m_performanceTimer;
    QElapsedTimer m_performanceClock;
    qint64 m_performanceLastWallMs = 0;
    qint64 m_performanceLastCpuMs = -1;
    void capturePerformanceSample(const QString &label);
    void applyHardwareSummary(const QVariantMap &hardware);
    QVariantList m_modelRecommendations;
    struct ModelDownloadItem {
        QString id;
        QString repo;
        QString fileName;
        QString outPath;
        QString partPath;
        QString state;   // queued | resolving | verifying | downloading | paused | done | error
        QString status;
        int progress = 0;
        qint64 received = 0;
        qint64 total = 0;
        qint64 resumeOffset = 0;
        bool resolvedFileName = false;
        bool pauseRequested = false;
        bool cancelRequested = false;
    };
    QVector<ModelDownloadItem> m_modelDownloadQueue;
    QString m_activeModelDownloadId;
    QNetworkReply *m_modelDownloadReply = nullptr;
    QFile *m_modelDownloadFile = nullptr;
    QString m_modelDownloadPath;
    int m_modelDownloadProgress = 0;
    QString m_modelDownloadStatus;
    QVariantMap modelDownloadItemToMap(const ModelDownloadItem &item) const;
    int modelDownloadIndexById(const QString &id) const;
    void emitModelDownloadChanged();
    void startNextModelDownload();
    void startModelDownload(int index);
    void finishModelDownloadItem(const QString &id, const QString &state, const QString &status,
                                 int progress = -1, bool removePart = false);
    void scanModelDownloadRoot();
    void acceptSystemProfileImpl(const QString &launchId, bool startWhenReady);
    QString benchmarkStorageDir() const;
    QString customBenchmarkDir() const;   // dir holding custom benchmark definitions
    void seedBundledCustomBenchmarks() const;
    QString benchmarkRunsDir() const;     // root for isolated timestamped run folders
    void saveBenchmarkResult(const QVariantMap &result);
    void decorateBenchmarkBaseline(QVariantMap *result) const;
    QString benchmarkProfileConfigFingerprint(const QString &profileId);
    QStringList benchmarkProfilesAllowedForStage(const QStringList &profileIds,
                                                  const QString &stage,
                                                  QStringList *blockedProfiles = nullptr);
    QString benchmarkServerLogTail(int maxBytes = 24000) const;
    void decorateBenchmarkMemory(QVariantMap *result) const;
    void saveBenchmarkFailureResult(const QString &profileId, const QString &profileName,
                                    int pass, int passes, const QString &mode,
                                    const QString &target, const QString &benchmarkName,
                                    const QString &runLabel, const QString &runDir,
                                    const QString &stage, const QString &message,
                                    const QString &detail, double elapsedSec = 0.0);
    QString benchmarkResumeFile() const;
    void saveBenchmarkResumePoint(const QStringList &pending, const QString &mode,
                                  int passes, const QString &target,
                                  const QString &agentProfileId,
                                  const QString &runDir, const QString &runLabel);
    void clearBenchmarkResumePoint();
    void runBenchmarkInternal(const QStringList &profileIds, const QString &mode,
                              const QVariantList &customTasks, const QString &runLabel,
                              int passes, const QString &target = QStringLiteral("model"));
    // Agent-target benchmark: drives a dedicated headless LlamaAgentBackend so the
    // model uses tools and writes real files into an isolated per-profile workspace.
    void runAgentBenchmark(const QString &profileId, const QString &profName, int idx, int total,
                           const QVariantList &tasks, int passes, const QString &mode,
                           const QString &runLabel, const QString &runDir,
                           std::function<void()> onProfileDone);
    void benchmarkWaitServerReady(int attemptsLeft, int totalAttempts, const QString &url,
                                  const QString &statusPrefix,
                                  std::function<void(bool)> onResult,
                                  qint64 waitStartMs = 0,
                                  qint64 lastActivityMs = 0,
                                  qint64 lastLogSize = -1);
    void benchmarkWaitServerStopped(int remainingMs, std::function<void()> onStopped);
    void benchmarkEnsureServerStopped(int budgetMs, std::function<void()> onStopped);

public:
    // Qué hacer mientras se espera que el server anterior muera, según si sigue
    // vivo y cuánto presupuesto queda. Pura para poder testearla sin proceso.
    enum class BenchStopStep { Proceed, Wait, Kill };
    static BenchStopStep benchmarkStopStep(bool stillRunning, int budgetLeftMs);
    // ¿El server que ya corre sirve para benchmarkear este perfil?
    static bool benchmarkCanReuseServer(const QString &activeLaunchId,
                                        const QString &wantedLaunchId,
                                        bool running, bool ready);
    // Normaliza los límites del benchmark de concurrencia; puro para probar los
    // bordes sin lanzar un servidor real.
    static QVariantMap concurrencyBenchmarkSettingsForTest(int minSlots, int maxSlots,
                                                            int requests, int maxTokens);

private:
    void benchmarkKillStrayServers();
    void benchmarkRequest(const QString &url, const QString &prompt,
                          int maxTokens, bool streaming,
                          std::function<void(QVariantMap)> onDone,
                          const QString &resultType = QString(),
                          bool cachePrompt = false, int seed = -1);
    struct BenchmarkResources {
        double ramMb = 0.0;
        double ramTotalMb = 0.0;
        double ramFreeMb = 0.0;
        double vramMb = 0.0;
        double vramGpu0Mb = 0.0;
        double vramGpu1Mb = 0.0;
        double vramGpu0TotalMb = 0.0;
        double vramGpu1TotalMb = 0.0;
        double vramGpu0FreeMb = 0.0;
        double vramGpu1FreeMb = 0.0;
        bool vramProcessScoped = false;
    };
    BenchmarkResources benchmarkMeasureResourcesNow() const;
    void benchmarkMeasureResources(std::function<void(BenchmarkResources)> onDone);
    void decorateBenchmarkResourceMetrics(QVariantMap *result,
                                          const BenchmarkResources &resources) const;
    QVariantMap serverBenchmarkCorpus(QString *contentHash = nullptr) const;
    static QString serverBenchmarkPadPrompt(const QString &prompt, int targetTokens);
    static QString serverBenchmarkNonce(int sequence);
    QString modelDownloadDir() const;
    void rebuildModelRecommendations();
    // kind: "cpu" | "beellama" (ngram-mod / Qwen NextN MTP) | "official"/""
    // (gemma4-assistant y el resto). Elige un binario válido del tipo pedido;
    // cpu no cae a GPU, para evitar crashear perfiles 0GB con builds CUDA.
    QString resolveSystemBinaryId(const QString &kind = QString()) const;
    // binaryKind del perfil de sistema (del bundle); "official" si no se especifica.
    QString systemProfileBinaryKind(const QString &launchId) const;
    int systemProfileMinimumBuild(const QString &launchId) const;
    // Id del binario instalado que matchea el binaryPin del perfil (primer válido
    // cuyo nombre+ruta contiene el pin). Vacío si no hay pin o no hay match válido
    // → el caller cae al resolveSystemBinaryId por kind.
    QString pinnedSystemBinaryId(const QString &launchId) const;
    QString minimumSystemBinaryId(const QString &launchId) const;
    QList<HealthIssue> resolvedProfileHealth();
    // Instala el binario del tipo pedido si falta (cpu/official→installOfficialBinary,
    // beellama→installMtpBinary). beellama es CUDA-only: sin NVIDIA cae a official.
    void ensureSystemBinary(const QString &kind);
    // Igual que ensureSystemBinary, pero respeta binaryPin del perfil de sistema:
    // si el pin falta, instala exactamente ese release aunque exista otro official.
    void ensureSystemProfileBinary(const QJsonObject &entry);
    // Para acceptSystemProfile: perfil de sistema pendiente de bind tras descarga.
    QString m_pendingSystemLaunchId;
    bool    m_pendingSystemStartAgent = false;
    // Tras un scan: si el modelo del perfil de sistema pendiente ya está en el
    // catálogo, lo activa (lastLaunchId + effective) y limpia el pendiente.
    void maybeActivatePendingSystemProfile();
    // Encola una descarga de modelo en un subdir opcional de modelDownloadDir
    // (evita colisión de nombres genéricos como mmproj-F16.gguf entre repos).
    void enqueueModelDownload(const QString &repo, const QString &fileName,
                              const QString &subdir);
    // Encola modelo + mmproj + draft de un perfil de sistema (cada uno a su subdir).
    void enqueueSystemProfileAssets(const class QJsonObject &entry);

    // Model quality benchmarks (Artificial Analysis Intelligence Index).
    // Bundled table is the offline fallback; a weekly live fetch overlays it.
    QHash<QString, double> m_benchmarkQuality;
    QHash<QString, double> m_cookbookPriority;
    bool m_benchmarkLoaded = false;
    QNetworkReply *m_benchmarkFetchReply = nullptr;
    QString benchmarkCachePath() const;       // writable cache for fetched data
    void loadBenchmarkScores();               // populate m_benchmarkQuality (cache→bundled)
    void maybeFetchBenchmarks();              // weekly live refresh, async, best-effort

    // Deep Research
    bool m_researchRunning = false;
    int m_researchProgress = 0;
    QString m_researchStatus;
    QVariantList m_researchReports;
    QNetworkReply *m_researchReply = nullptr;
    QString researchStorageDir() const;
    void setResearchState(bool running, int progress, const QString &status);
    void saveResearchReport(const QVariantMap &summary, const QString &markdown,
                            const QJsonObject &full);

    bool m_updateAvailable = false;
    QVariantMap m_updateInfo;
    QNetworkReply *m_updateReply = nullptr;
    void applyUpdateFlag(const QJsonObject &flag);
};
