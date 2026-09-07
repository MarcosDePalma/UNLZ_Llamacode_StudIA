#pragma once
#include "IAgentBackend.h"
#include "AgentProgressGovernor.h"
#include "HarnessEventLog.h"
#include "HarnessEffectLedger.h"
#include "HarnessCapabilitySnapshot.h"
#include "AgentLifecycle.h"
#include "AgentRunStore.h"
#include "AgentDeliverableStore.h"
#include "core/profiles/HarnessEngine.h"
#include "core/profiles/HarnessSpec.h"
#include "ToolSurfaceRouter.h"
#include <QHash>
#include <QList>
#include <QSet>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSharedPointer>

struct AgentDurableRunState;

class QThread;
class QTimer;
class AgentToolRunner;
class SubAgentRunner;
class HarnessWorkerDriver;

// Backend propio ("llamaagent"): loop ReAct con tool-calling OpenAI contra
// llama-server. No lanza proceso externo; ejecuta tools nativas (lectura/
// escritura/shell/grep) con aprobación human-in-the-loop vía IAgentBackend.
class LlamaAgentBackend : public IAgentBackend
{
    Q_OBJECT
public:
    explicit LlamaAgentBackend(QObject *parent = nullptr);
    ~LlamaAgentBackend() override;

    QString adapter() const override { return QStringLiteral("llamaagent"); }
    bool running() const override { return m_running; }

    void start(const AgentContext &ctx) override;
    void stop() override;
    void sendMessage(const QString &text) override;
    // Envía un payload enriquecido al modelo, pero conserva en conversación,
    // título y auditoría únicamente el texto que realmente escribió el usuario.
    void sendMessageWithVisibleText(const QString &apiText, const QString &visibleText);
    static QString visibleUserTextForTest(const QString &apiText, const QString &visibleText);
    void prefillWarmup() override;
    void cancelGeneration() override;
    void steerMessage(const QString &text) override;
    void queueMessage(const QString &text) override;
    int queuedCount() const override;
    QStringList queuedMessages() const override;
    bool updateQueuedMessage(int index, const QString &text) override;
    bool removeQueuedMessage(int index) override;
    void clearQueue() override;

    // Rebobinar la conversación al estado previo a un mensaje de usuario (índice
    // en la lista de UI). Trunca mensajes + contexto y revierte edits posteriores.
    void rollbackToMessage(int msgIndex);
    // Editar el texto de un mensaje (user o IA) en msgIndex y descartar todo lo
    // posterior. Rebuildea m_apiMessages (system + turnos de texto) → pierde la
    // estructura de tool_calls pero deja el contexto válido para continuar.
    void editMessage(int msgIndex, const QString &newText);

    void newSession() override;
    void newTaskSession();
    // Cierra la sesión efímera de la Task y restaura la sesión previa del usuario.
    void endTaskSession();
    void newSessionInProject(const QString &projectDir) override;
    void switchSession(const QString &sessionId) override;
    void renameSession(const QString &sessionId, const QString &title) override;
    void deleteSession(const QString &sessionId) override;
    void deleteProject(const QString &projectDir) override;
    void forkSession(const QString &sessionId) override;
    void forkSessionAtMessage(int msgIndex);
    void refreshSessions() override;
    // Descarta sesiones sin ningún mensaje (creadas y abandonadas). `keepId`
    // nunca se toca (la sesión recién creada / recién abierta).
    void pruneEmptySessions(const QString &keepId);
    bool sessionHasNoMessages(const QString &sessionId) const;

    void approveTool(const QString &id, bool always = false) override;
    void rejectTool(const QString &id) override;
    void setApprovalPolicy(const QString &mode) override;
    void setPermissionRules(const QString &rules) override;
    void revertEdit(const QString &path) override;
    void setAgentTuning(const QString &systemExtra, double temperature) override;
    void setProgressPolicy(const AgentProgressGovernor::Policy &policy,
                           int quickToolTimeoutSec = 15);
    // HARNESS MODULAR: políticas que antes eran constantes compiladas. Los
    // defaults de los módulos reproducen exactamente el comportamiento previo,
    // así que no llamarlos = no regresionar.
    void setLoopPolicy(const HarnessLoopModule &loop);
    void setContextPolicy(const HarnessContextModule &context);
    void setEscalationPolicy(const HarnessEscalationModule &escalation);
    // Memoria inyectada en el system prompt (hechos + memoria de proyecto) y
    // gate de la consolidacion automatica. Ver modulo `memory` del HarnessSpec.
    void setMemoryPolicy(const HarnessMemoryModule &memory);
    HarnessMemoryModule memoryPolicyForTest() const { return m_memoryPolicy; }
    void setKnowledgePolicy(const HarnessKnowledgeModule &knowledge);
    HarnessKnowledgeModule knowledgePolicyForTest() const { return m_knowledgePolicy; }
    HarnessEscalationModule escalationPolicyForTest() const { return m_escalationPolicy; }
    HarnessLoopModule loopPolicyForTest() const { return m_loopPolicy; }
    HarnessContextModule contextPolicyForTest() const { return m_contextPolicy; }
    // Directivas de usuario (packs .md) a inyectar tras las built-in, en orden.
    // Cada entrada: {slug, body, when}. `when` gatea la inyección por contexto
    // (tools.desktop / vision / project.hasGit); vacío = siempre.
    void setCustomDirectives(const QVariantList &directives);
    QVariantList customDirectivesForTest() const { return m_customDirectives; }
    // Tope de tamaño del system prompt compuesto (0 = sin tope). No trunca:
    // avisa por log — cortar una instrucción al medio es peor que un prompt largo.
    void setPromptMaxChars(int chars) { m_promptMaxChars = qMax(0, chars); }
    // Evaluación pura del gate `when` de una directiva de usuario.
    static bool directiveConditionMet(const QString &when, const QVariantMap &facts);
    // Claves de los hechos que entiende `when` (para la UI y el editor). Ver el
    // .cpp: son la única fuente, y un test fija que coincidan con directiveFacts.
    static QStringList directiveFactKeys();
    QVariantMap directiveFactsForTest() const { return directiveFacts(false); }
    void setDeterministicSeed(int seed) { m_seed = seed; }

    // El backend sigue siendo el mismo loop ReAct en ambos perfiles durante la
    // primera etapa, pero el contrato, las sesiones y la evidencia quedan
    // explícitamente aislados. Los getters son para QA/benchmarks y no forman
    // parte del flujo normal de la UI.
    QString harnessEngineIdForTest() const { return m_harnessEngineId; }
    int harnessEngineVersionForTest() const { return m_harnessEngineVersion; }
    QString harnessStorageDirForTest() const { return storageDir(); }
    QString harnessEventLogPathForTest() const { return m_harnessEventLog.path(); }
    HarnessCapabilitySnapshot harnessCapabilitiesForTest() const { return m_harnessCapabilities; }
    bool harnessWorkerConfiguredForTest() const;
    bool harnessWorkerReadyForTest() const { return m_harnessWorkerReady; }

    // Razonamiento (Qwen3): on por defecto para que el agente piense las tools.
    void setThinkingEnabled(bool enabled);
    void setReasoningPolicy(const QString &effort, int budget);
    void setThinkingLeakGuard(bool enabled) { m_thinkingLeakGuard = enabled; }
    void setStablePhasePrefix(bool enabled) { m_stablePhasePrefix = enabled; }
    bool stablePhasePrefixForTest() const { return m_stablePhasePrefix; }
    QVariantMap efficiencySummary() const;
    QVariantMap progressSummary() const {
        return {{QStringLiteral("progressEvents"), m_progressEvents},
                {QStringLiteral("stagnationEvents"), m_stagnationEvents},
                {QStringLiteral("replanEvents"), m_replanEvents}};
    }
    void setEphemeralSessions(bool enabled) { m_ephemeralSessions = enabled; }
    // Título breve y determinista a partir del primer objetivo del usuario.
    // Público para cubrir el límite de tres palabras sin requerir un servidor.
    static QString suggestSessionTitle(const QString &firstPrompt);

    // Adjuntos (imágenes/docs) a incluir en el PRÓXIMO sendMessage (modelo VL).
    void setPendingAttachments(const QStringList &paths) { m_pendingAttachments = paths; }

    // Capacidad de visión del server activo (mmproj cargado). Cuando una tool
    // devuelve una captura (desktop_observe / browser screenshot), si hay visión
    // la imagen se inyecta en el contexto como mensaje user con image_url, así el
    // modelo VE el resultado que pidió observar (loop de debug visual recursivo).
    void setVisionAvailable(bool v) { m_visionReady = v; }
    // Captura opt-in para el inspector de Tasks; no afecta el modo headless
    // normal ni la conversación de Chat.
    void setLivePreviewEnabled(bool enabled);
    // Pausa la planificación de nuevas tools sin matar la tool que ya está en
    // vuelo. stepExecution() permite liberar exactamente una acción.
    void setExecutionPaused(bool paused);
    void stepExecution();
    bool executionPaused() const { return m_executionPaused; }

    // Forzar el protocolo textual de tools (TOOL_CALL por texto) desde el primer
    // request, sin esperar el 400 del server. Lo activa AppController cuando el
    // tool-calling del modelo activo es "unsupported" (chat-template del GGUF sin
    // tools, ej. Gemma): así el modelo igual puede operar tools vía texto.
    void setForceTextTools(bool v) { m_forceTextTools = v; }

    // Protocolo declarado por el perfil (módulo `protocol` del HarnessSpec):
    //   "auto"   → como siempre: nativo, con fallback a texto si el server da 400
    //   "text"   → texto desde el primer request (= setForceTextTools(true))
    //   "native" → nativo y NO caer a texto aunque el server rechace (útil para
    //              detectar un perfil mal armado en vez de degradar en silencio)
    void setToolProtocol(const QString &mode);
    QString toolProtocolForTest() const { return m_toolProtocol; }

    // Servers MCP (stdio) a usar. Cada entrada: {name, command, type, enabled}.
    // Se relanzan en start(). Sus tools se inyectan con prefijo mcp__<server>__<tool>.
    void setMcpServers(const QVariantList &servers);

    // Gating de las tools MCP en el contexto (independiente de qué servers corren).
    // false = buildToolSchemas NO inyecta las tools MCP descubiertas → ahorra el
    // payload de schemas MCP por request. Lo setea el perfil de agente (mcpEnabled):
    // las tools MCP no están en toolCatalog(), así que enabledTools no las apaga.
    void setMcpToolsEnabled(bool on) { m_mcpToolsEnabled = on; }

    // Cuentas de correo (con password resuelto) para las tools email_*. Se
    // reenvían al worker. mailAutoSend=true permite que email_send NO pida
    // aprobación (default false: enviar correo es acción externa irreversible).
    void setMailAccounts(const QVariantList &accounts);
    void setWebProviders(const QVariantList &providers);
    // Política de skills portables proveniente del HarnessSpec.
    void setPortableSkillPolicy(const HarnessSkillsModule &policy);
    HarnessSkillsModule portableSkillPolicyForTest() const { return m_skillPolicy; }
    void setMailAutoSend(bool on) { m_mailAutoSend = on; }

    // Guardrail "Zero-Autonomy": si una tool es destructiva/irreversible (borrado
    // recursivo, format, drop de DB, memory forget/prune, click de escritorio sobre
    // un control delete/eliminar/format), fuerza aprobación humana AUNQUE el modo sea
    // auto o haya Task auto-approve. Excepción: modo "super" (autonomía total). ON por
    // defecto. Es el mismo mecanismo que el gate de email_send.
    void setHitlDestructive(bool on) { m_hitlDestructive = on; }
    // Clasificador puro (testeable): true si (name,args) representa una acción
    // destructiva/irreversible según las heurísticas de shell/desktop/memory.
    // desktopControlsText = salida cacheada del último desktop_controls (formato
    // 'controlId=<id> [role]... "<name>"' por línea); se usa para resolver el
    // control_id de un desktop_click_element a su nombre y decidir si es destructivo.
    static bool isDestructiveAction(const QString &name, const QJsonObject &args,
                                    const QString &desktopControlsText = QString());

    // Diff unificado simple (prefijo/sufijo común; líneas +/-/ ).
    static QString makeDiff(const QString &oldText, const QString &newText);

    // Sección del system prompt con la disciplina anti-regresión (blast radius,
    // cambios mínimos, correr tests al tocar código existente). Pública y pura →
    // unit-testeable y reusable. La consume buildSystemPrompt().
    static QString developmentDisciplineSection();

    // Sección del system prompt con la "red de tests": detectar el runner del
    // proyecto, escribir un test caja-negra por cada feature/cambio, registrarlo y
    // correr el suite. Pura y testeable. La consume buildSystemPrompt().
    static QString testSafetyNetSection();

    enum RetryClass { RetryNone, RetryTransient, RetryContextOverflow };
    static RetryClass classifyCompletionError(int httpStatus, const QString &errorText);

    // Sección del system prompt sobre contexto del proyecto: entender el PORQUÉ
    // antes de tocar (no romper workarounds deliberados), revisar co-cambios por
    // git, y dejar las decisiones durables en .llamacode/memory.md. Pura y
    // testeable. La consume buildSystemPrompt().
    static QString projectContextSection();

    // Secciones de eficiencia (menos pasos/tool calls) y estilo (respuestas
    // concisas). Antes inline en buildSystemPrompt; extraídas para poder
    // habilitarlas/deshabilitarlas por directiva. Puras y testeables.
    static QString efficiencySection();
    static QString styleSection();

    // Playbook de AUTOMATIZACIÓN DE ESCRITORIO: guía concreta para operar apps
    // nativas de Windows con las tools desktop_* sin flailar. Camino rápido por
    // teclado (calc/notepad), verificación por TEXTO (desktop_controls/UIA) en vez
    // de capturas cuando no hay visión, y clic semántico por nombre. 'visionReady'
    // adapta si puede usar desktop_observe para VER. Pura y testeable. La consume
    // buildSystemPrompt() sólo si las tools de escritorio están habilitadas.
    static QString desktopPlaybookSection(bool visionReady);
    static bool redundantDesktopConfirmKey(const QString &previousTool,
                                           const QString &previousTypeText,
                                           const QString &key);

    // Sección "Frugalidad (Honey)": reduce lo que el modelo EMITE. Código
    // YAGNI-first (parar en el primer escalón que funciona, sin scaffolding
    // especulativo), respuesta-primero sin narrar, y handoffs agente↔agente
    // densos (clave:valor compacto, no JSON pretty). Pura y testeable. Apaga por
    // defecto vía catálogo (off-by-default), distinta de style/efficiency. La
    // consume buildSystemPrompt().
    static QString honeySection();

    // Sección "Anti-sesgo": endurece el razonamiento contra pattern-matching.
    // Basar la respuesta en las premisas DADAS; "usual/standard/typical/classical"
    // es señal de sesgo (re-examinar); responder una vez cumplida la premisa
    // primaria sin re-derivar checks ya pasados. Opt-in puro (off-by-default),
    // distinta de efficiency (que ataca el sobre-pensar por el lado de pasos).
    // La consume buildSystemPrompt().
    static QString antiBiasSection();

    // Catálogo de DIRECTIVAS del system prompt para la UI de toggles: lista de
    // {key, name, description}. Las keys (discipline/testNet/projectContext/
    // efficiency/style) son la fuente de verdad para buildSystemPrompt y el editor
    // de perfiles de agente. El orden define el de la UI.
    static QVariantList directiveCatalog();

    // Directivas habilitadas (keys de directiveCatalog). Vacío/sin setear =
    // TODAS activas (comportamiento histórico, no regresiona). Refresca el system
    // prompt si ya hay sesión.
    void setDirectives(const QStringList &keys);

    // Hook de test: expone el system prompt construido (buildSystemPrompt es
    // privada). Permite verificar el gating por directiva sin arrancar una sesión.
    QString systemPromptForTest() const { return buildSystemPrompt(); }

    // Hooks de test para medir el PRESUPUESTO DE CONTEXTO que el harness inyecta
    // (system prompt + schemas de tools con MCP + memoria de proyecto) sin una
    // sesión viva. Sirven para bisecar qué toggle infla el contexto (lo que en un
    // perfil al límite de VRAM como MAX-Q tira el decode a swap). No tocan estado
    // de runtime: setean caches/campos que normalmente llena el worker.
    QJsonArray toolSchemasForTest() const { return buildToolSchemas(); }
    void setMcpToolsForTest(const QVariantList &tools) { m_mcpTools = tools; }
    void setCwdForTest(const QString &dir) { m_cwd = dir; }
    // Hooks para testear planCompaction (decide si compactar según budget). El
    // budget depende de si las tools viajan como payload nativo (reservan tokens)
    // o embebidas en texto (modo text-tools → no reservan). Ver test_agent_wire.
    void setCtxLimitForTest(int n) { m_ctxLimit = n; }
    void setApiMessagesForTest(const QJsonArray &m) {
        m_apiMessages = m;
        m_transcriptMessages = m;
    }
    QJsonArray apiMessagesForTest() const { return m_apiMessages; }
    QJsonArray transcriptMessagesForTest() const { return m_transcriptMessages; }
    int pruneWorkingContextForTest() { return pruneWorkingContext(); }
    bool planCompactionForTest(int &head, int &keepFrom) const {
        return planCompaction(head, keepFrom);
    }
    // Anti-loop de compactación: aplicar el tramo y leer el contador de estancamiento.
    void applyCompactionForTest(int head, int keepFrom, const QString &summary) {
        applyCompaction(head, keepFrom, summary);
    }
    int compactStallForTest() const { return m_compactStall; }
    static QString failureFingerprint(const QString &tool, const QString &result);
    // Firma semántica estable para anti-loop: normaliza JSON equivalente
    // (espacios y orden de claves) antes de comparar llamadas consecutivas.
    static QString toolCallSignature(const QString &tool, const QString &arguments);
    static int toolWatchdogSeconds(const QString &tool, const QJsonObject &arguments,
                                   int quickTimeoutSec = 15, int webTimeoutSec = 180);
    static int repeatedSuffixStart(const QString &text, int repeats = 3,
                                   int minBlockChars = 80);
    bool recordToolOutcomeForTest(const QString &tool, bool ok, bool isWrite,
                                  const QString &result) {
        return recordToolOutcome(tool, ok, isWrite, result);
    }
    bool recoveryLearningEligibleForTest() const {
        return m_turnHadDifficulty && m_turnRecovered;
    }
    QJsonObject buildTextToolPayloadForTest(const QJsonObject &nativePayload) const {
        return buildTextToolPayload(nativePayload);
    }

    // Schemas de las tools built-in (sin MCP). Público para reusar en sub-agentes.
    static QJsonArray toolSchemas();
    static QJsonObject textToolCallFromContent(const QString &content);
    // Offset donde arranca el SEGUNDO TOOL_CALL de una ráfaga (-1 si no hay).
    static int secondTextToolCallStart(const QString &content);
    // Señal de tool-calling nativo del /props del server (caps > regex jinja).
    static bool toolSupportFromProps(const QJsonObject &props, bool *haveTemplate = nullptr);

    // Texto VISIBLE de una respuesta del modelo según "Pensar". Con Pensar ON deja
    // el content tal cual (la UI muestra <think>). Con Pensar OFF quita los bloques
    // <think>…</think>; pero si el modelo metió TODA la respuesta dentro de <think>
    // (Qwen con thinking off suele hacerlo) y al quitar quedaría vacío, RESCATA el
    // texto interno en vez de mostrar una burbuja vacía: silencio es peor que
    // verboso, y era la causa de los saludos "sin respuesta". PURA → unit-testeable.
    static QString visibleAnswer(const QString &content, bool thinkingEnabled,
                                 bool thinkingLeakGuard = false);
    static QJsonObject thinkingTemplateKwargs(bool thinkingEnabled,
                                              bool thinkingLeakGuard,
                                              const QString &reasoningEffort = QString());

    // Une los deltas incrementales de tool_calls (streaming OpenAI) en el
    // acumulador `acc` (index → {id,name,arguments}). id/name se setean cuando
    // llegan; arguments se concatena chunk a chunk. PURA → unit-testeable.
    static void mergeToolCallDelta(QHash<int, QJsonObject> &acc,
                                   const QJsonArray &deltaToolCalls);

    // Arma el mensaje user multimodal con las capturas observadas por las tools
    // (data-URIs ya codificadas). Devuelve un QJsonObject role=user con content =
    // [text, image_url...]. Objeto vacío si no hay imágenes. PURA → unit-testeable.
    static QJsonObject buildObservationMessage(const QStringList &imageDataUris);

    // Rol para notas inyectadas EN MEDIO de la conversación (hoy: el resumen de
    // compactación). "system" es el rol equivocado: varios chat-templates
    // (DeepSeek-V4 y sus ports jinja) HOISTEAN todos los mensajes system al tope
    // del prompt, así que la nota pierde su posición (el modelo no sabe dónde se
    // cortó el hilo) y encima corre el prefijo → invalida el prompt-cache de todo
    // lo que sigue, en cada turno.
    //   deepseek-v4 → "latest_reminder" (el rol que DS entrenó para esto)
    //   resto       → "user" (aceptado por todo server OpenAI-compat y
    //                 posicionalmente fiel; un rol inventado da 400 en cloud)
    // PURA → unit-testeable.
    static QString midConversationNoteRole(const QString &modelId);

    // Cap adaptativo para sub-agentes: combina slots reales del perfil, contexto
    // por secuencia y VRAM. Público/puro para pruebas y UI futura.
    static int adaptiveSubagentLimit(int parallelSlots, int ctxTokens,
                                     double vramTotalMb, double vramFreeMb = 0.0);

    // Catálogo de tools built-in con metadata para la UI de habilitar/deshabilitar:
    // lista de {name, group, description, approxTokens}. El orden define el de la UI.
    static QVariantList toolCatalog();

    // Tools deshabilitadas por el usuario (nombres built-in y/o mcp__server__tool).
    // Se excluyen de buildToolSchemas() → no se ofrecen al modelo (ahorra contexto).
    void setDisabledTools(const QStringList &names);
    // Recorte adaptativo de la superficie inicial. No modifica permisos: sólo
    // decide qué schemas se anuncian en el primer request.
    void setAdaptiveToolRouting(bool enabled);
    QVariantMap toolSurfaceDecisionForTest() const
    { return m_toolSurfaceDecision.toVariantMap(); }

    // Permisos de filesystem por Task (no persisten; aplican a la corrida actual).
    //   scope = "project" → confinado al cwd (default)
    //           "folder"  → confinado al cwd + las `folders` indicadas
    //           "full"    → sin confinamiento (todo el disco)
    void setTaskScope(const QString &scope, const QStringList &folders);
    // Restaura el confinamiento según el modo de aprobación persistido y limpia roots.
    void clearTaskScope();
    // Auto-aprueba todas las tools (salvo email_send gateado) durante una Task, sin
    // tocar la preferencia persistida (m_approvalMode).
    void setTaskAutoApprove(bool on);

    // Config del modelo maestro (tool ask_teacher). Se reenvía al worker.
    void setTeacherConfig(const QString &url, const QString &model, const QString &key);
    // Endpoint/modelos opcionales para embeddings y rerank. No altera el chat
    // ni el endpoint del maestro.
    void setAuxiliaryServerConfig(const QString &url, const QString &embeddingModel,
                                  const QString &rerankModel, const QString &bearer);
    // Config de maestro tipo CLI (claude-code / codex). Se reenvía al worker.
    // escalation: "manual"|"auto"|"both"; autoAfterFails dispara el escalado auto.
    void setMasterCli(const QString &kind, const QString &cliName, const QString &cliPath,
                      const QString &escalation, int autoAfterFails,
                      bool applyEdits, int timeoutSec);
    // Cadena de fallbacks del maestro (resuelta por AppController: keys y cliPath
    // ya resueltos). Se reenvía al worker. Tiene prioridad sobre setMasterCli.
    void setMasterChain(const QVariantList &chain, const QString &escalation,
                        int autoAfterFails);
    void completeManagedAgentRun(const QString &requestId,
                                 const QVariantMap &run) override;
    // Escalado manual: el usuario pide pasar el problema actual al maestro.
    // Envía un turno que instruye al agente a usar ask_teacher. Devuelve false si
    // no hay maestro configurado.
    bool escalateToMaster(const QString &problem);
    bool masterConfigured() const {
        return !m_masterChain.isEmpty() || m_masterKind != QLatin1String("none");
    }

    // Memoria por proyecto: ruta del archivo de memoria dentro de un cwd.
    static QString memoryFilePath(const QString &cwd);

    // Raíz de trabajo utilizable, o vacío si `dir` no sirve como proyecto.
    // Rechaza el home del usuario, su carpeta padre y la raíz de una unidad:
    // ahí las tools (con auto-aprobación) escribirían sobre .ssh/.claude/perfiles.
    static QString safeProjectDir(const QString &dir);
    // Workspace aislado que se usa cuando no hay proyecto válido. Nunca el home.
    static QString fallbackWorkspaceDir();

    // Normaliza el historial antes de enviarlo a backends OpenAI-compatible:
    // elimina pares assistant/tool incompletos, conserva un user de anclaje y
    // evita system messages no iniciales (degradados con midConversationNoteRole,
    // que depende del modelo).
    static QJsonArray sanitizeApiMessagesForWire(const QJsonArray &messages,
                                                 const QString &modelId = QString());

    // Poda de capturas viejas (pura, testeable): conserva las imágenes solo en los
    // últimos `keepLast` mensajes que las tengan; en los anteriores reemplaza cada
    // image_url por un texto "[captura omitida]". Cada screenshot son miles de
    // tokens de prefill con mmproj — una sesión con varias desktop_observe
    // acumulaba prompts de 50k+ tokens (minutos de prefill en frío).
    static QJsonArray trimStaleImages(const QJsonArray &messages, int keepLast = 1);

    // Payload de precalentamiento del prompt-cache (pura, testeable): mismo
    // prefijo que un turno real pero max_tokens=1, stream=false, cache_prompt=true.
    static QJsonObject buildWarmupPayload(const QJsonArray &wireMessages,
                                          const QJsonArray &tools,
                                          const QString &modelId,
                                          double temperature,
                                          bool thinkingEnabled,
                                          const QString &reasoningEffort = QString());

    // Consolidación de memoria (background): corre 1 completion sobre el transcript
    // actual y extrae hechos durables → MemoryStore (source="consolidation"). Async,
    // fire-and-forget. Se dispara solo al dejar una sesión y puede invocarse manual.
    void consolidateMemory(bool recoveredSkill = false);

    QString currentSessionId() const override {
        return m_viewSessionId.isEmpty() ? m_sessionId : m_viewSessionId;
    }
    QString currentSessionTitle() const override {
        return m_viewSessionId.isEmpty() ? m_sessionTitle : m_viewSessionTitle;
    }
    QString currentProjectDir() const override {
        return m_viewSessionId.isEmpty() ? m_cwd : m_viewProjectDir;
    }
    QVariantList messages() const override {
        return m_viewSessionId.isEmpty() ? m_messages : m_viewMessages;
    }
    QVariantList sessions() const override;
    bool isBusy() const;               // turno/tool/compactación en curso
    bool selectedSessionBusy() const;  // runtime de la sesión visible

private:
    void sendMessageImpl(const QString &apiText, const QString &visibleText);
    enum CompletionMode { NativeFull, NativeCompat, TextTools };

    // Loop
    void runCompletion();
    void postCompletionRequest(QJsonObject payload, CompletionMode mode);
    QJsonObject buildTextToolPayload(const QJsonObject &nativePayload) const;
    void processPendingCalls();     // procesa m_pendingCalls (approval/exec)
    void finishTurn(const QString &finalText, bool persistFinalToApi = true,
                    const QString &terminalStatus = QStringLiteral("completed"));
    void appendAssistantText(const QString &text);
    void setTyping(bool typing);

    // Tools
    QJsonArray buildToolSchemas() const;             // built-in + MCP (cache)
    static QString toolKind(const QString &name);    // read | write | shell | mcp
    static QStringList requiredArgs(const QString &name);

    // Worker de ejecución de tools (hilo aparte; no bloquea UI).
    void ensureWorker();
    void configureWorker();
    void restartWorkerAfterTimeout();
    void teardownWorker();
    void startHarnessWorker();
    void stopHarnessWorker();
    void failExternalWorkerCalls(const QString &reason);
    void failWorkerCapabilities(const QString &reason);
    void dispatchWorkerCapability(const QString &requestId, const QString &capability,
                                  const QString &operation, const QJsonObject &payload);
    void finishWorkerCapability(const QVariantMap &result);

private slots:
    void onServersReady(const QVariantList &toolDefs);
    void onToolExecuted(const QVariantMap &result);
    void onToolStarted(const QVariantMap &info);          // run_shell async: tarjeta en vivo
    void onToolOutputChunk(const QString &callId, const QString &chunk);
    void onSubFinished(const QString &id, const QString &result, bool ok);
    void onSubProgress(const QString &id, const QString &note);
    void onHarnessWorkerAuthenticated(bool authenticated);
    void onHarnessWorkerCallResult(const QString &callId, const QJsonObject &payload);
    void onHarnessWorkerCapabilityCall(const QString &requestId, const QString &capability,
                                       const QString &operation, const QJsonObject &payload);
    void onHarnessWorkerError(const QString &message);
    void onHarnessWorkerExited(int exitCode);
    void flushQueue();                 // envía el próximo mensaje encolado (si lo hay)

private:
    // Subagents (tool `task`): spawn paralelo en git worktrees aisladas.
    void spawnTasks(const QJsonArray &taskCalls);
    void pumpSubs();                   // lanza subs de la cola hasta el cap
    void launchSub(const QJsonObject &call);
    bool subsActive() const { return !m_subQueue.isEmpty() || !m_subs.isEmpty(); }
    QString createWorktree(const QString &callId, bool &isolated);
    QString mergeAndCleanupWorktree(const QString &callId, bool ok, bool isolated);
    void cancelAllSubs();
    int subagentLimit() const;
    static constexpr int kAbsoluteMaxParallelSubs = 5;

    void interruptForSteer();          // aborta y deja m_apiMessages consistente
    void repairDanglingToolCalls();    // cierra tool_calls sin respuesta tras abortar
    void finalizeLiveToolCard(bool cancelled);  // cierra tarjeta run_shell en vivo

private:
    void approveAndContinue(const QString &id, const QString &response); // once|always|reject
    void emitToolLifecycleFinish(const QString &id, const QString &name, bool ok,
                                 bool isWrite, bool externalWrite,
                                 const QString &content, const QStringList &paths = {});
    void appendToolResult(const QString &id, const QString &name, const QString &content,
                          bool ok = false, bool isWrite = false,
                          bool externalWrite = false, const QStringList &paths = {});
    bool recordToolOutcome(const QString &tool, bool ok, bool isWrite,
                           const QString &result);
    // Burbuja de asistente: crear/cerrar por iteración para no apilar texto LLM
    // con las tarjetas de tools.
    void ensureAssistantBubble();
    void closeAssistantBubble();
    // Tarjeta separada para una ejecución de tool (nombre, comando, salida).
    void appendToolCard(const QString &name, const QString &kind, bool ok,
                        const QString &command, const QString &output);
    void setAssistantStatus(const QString &status);
    static QString toolStatusText(const QString &name, const QString &kind,
                                  const QString &detail = QString());

    // Streaming SSE (igual patrón que RawChatBackend)
    void handleStreamData();             // parsea m_sseBuf incremental
    void handleStreamFinished(bool ok, const QString &err);
    void resetStreamState();
    void resetStreamIdleWatchdog();
    int streamIdleTimeoutMs() const;

    // Sesión + persistencia a disco (patrón RawChatBackend)
    void ensureSession();
    void releaseWorkClaim();
    void configureHarnessEventLog();
    void configureDurableRunStore();
    void beginDurableRun(const QString &objective);
    void finishDurableRun(const QString &status, const QString &detail);
    void scheduleDurableCapture(const QSharedPointer<AgentDurableRunState> &state);
    QString buildSystemPrompt() const;   // prompt base + memoria del proyecto
    void logFromConst(const QString &text) const;  // log desde métodos const
    QVariantMap directiveFacts(bool super) const;  // hechos para el gate `when`
    void fetchContextLimit();            // n_ctx desde /props
    // Aplica Content-Type + Authorization Bearer (si hay provider cloud con apiKey).
    void applyHeaders(QNetworkRequest &req) const;
    // Auto-compactación vía modelo: cuando el historial se acerca al n_ctx del
    // perfil, resume el tramo intermedio con el propio LLM y lo reemplaza por un
    // mensaje de resumen, conservando system + objetivo inicial + cola reciente.
    int  estimateApiTokens() const;      // estimación de tokens del historial API
    int  estimateMessageTokens(const QJsonArray &messages) const;
    bool planCompaction(int &head, int &keepFrom) const;  // ¿hay tramo a compactar?
    void startCompaction(int head, int keepFrom);         // dispara resumen (async)
    void applyCompaction(int head, int keepFrom, const QString &summary); // reemplaza tramo
    void appendApiMessage(const QJsonObject &message);
    void replaceSystemMessage(const QJsonObject &message);
    // Reconstruye el system prompt cuando cambió el contexto (cwd, modo). Sin
    // esto el prompt persistido sigue anunciando el proyecto y los permisos de
    // cuando se creó la sesión, y el modelo pide rutas que el runner deniega.
    void refreshSystemPromptContext();
    void resyncContextAfterMutation(const QString &reason,
                                    const QStringList &knownPaths = {});
    int pruneWorkingContext();
    bool isProtectedContextMessage(const QJsonObject &message) const;
    QString normalizeCompactionSummary(const QString &summary) const;
    QString storageDir() const;
    QString sessionFilePath(const QString &sessionId) const;
    void loadFromDisk();
    void persistIndex() const;
    void persistSession(const QString &sessionId) const;
    void persistAll() const;
    void removeSessionFile(const QString &sessionId) const;
    void saveCurrentSession();           // vuelca m_messages+m_apiMessages al store
    void autoTitleCurrentSession(const QString &firstPrompt);
    void setCurrentSession(const QString &sessionId);
    void showSessionWhileTurnRuns(const QString &sessionId);
    void activateViewedSessionIfIdle();
    void forkSessionImpl(const QString &sessionId, int msgIndex);
    LlamaAgentBackend *viewRuntime() const;
    LlamaAgentBackend *ensureSessionRuntime(const QString &sessionId);
    void copyRuntimeConfigurationTo(LlamaAgentBackend *runtime) const;
    int activeRuntimeCount() const;
    void pumpSessionRuntimes();
    void syncRuntimeSession(const QString &sessionId, LlamaAgentBackend *runtime);

    AgentContext m_ctx;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_compactReply = nullptr;   // request de resumen (compactación)
    QNetworkReply *m_warmupReply = nullptr;    // prefill del prompt-cache (charla)
    bool m_compacting = false;                 // compactación en curso
    int  m_compactStall = 0;                   // compactaciones consecutivas sin reducir tokens
                                               // (anti-loop: si no baja, dejar de compactar)
    int  m_contextPrunedMessages = 0;
    qint64 m_contextPrunedTokens = 0;
    int  m_lastPromptTokens = 0;
    QNetworkReply *m_consolidateReply = nullptr;     // request de consolidación de memoria
    QHash<QString, int> m_consolidatedLen;     // sessionId → nº de msgs ya consolidados (dedupe)
    QNetworkReply *m_reply = nullptr;
    QTimer *m_streamIdleTimer = nullptr;
    bool m_streamIdleTimedOut = false;
    int m_transportRetries = 0;
    int m_contextRecoveries = 0;
    bool m_running = false;
    bool m_textToolFallback = false;       // server rechazó OpenAI tools nativo (400)
    bool m_forceTextTools = false;         // modelo sin tool-template → texto desde el inicio
    QString m_toolProtocol = QStringLiteral("auto");  // auto | native | text (spec)
    bool m_visionReady = false;            // server cargó mmproj (ve imágenes)
    bool m_livePreviewEnabled = false;     // screenshots post-acción, sólo Tasks
    bool m_executionPaused = false;
    bool m_executionStep = false;
    // Verdadero si el turno debe usar el protocolo textual de tools (por 400 del
    // server o por gating proactivo de AppController para modelos "unsupported").
    bool usingTextTools() const { return m_textToolFallback || m_forceTextTools; }
    // Capturas devueltas por tools en el turno de tools en curso (data-URIs). Se
    // vuelcan como mensaje user multimodal cuando se resuelven TODAS las tools del
    // turno (no interleavear entre tool_results → rompería el contrato OpenAI).
    QStringList m_pendingObservations;
    QString m_cwd;
    QString m_harnessEngineId = QStringLiteral("legacy");
    int m_harnessEngineVersion = 1;
    QString m_harnessProfileId;
    QString m_harnessSpecHash;
    HarnessEventLog m_harnessEventLog;
    HarnessEffectLedger m_harnessEffectLedger;
    AgentRunStore m_agentRunStore;
    QTimer *m_runLeaseTimer = nullptr;
    QString m_runId;
    QString m_runLeaseToken;
    QString m_runOwnerId;
    QSharedPointer<AgentDurableRunState> m_runAsyncState;
    static constexpr qint64 kRunLeaseMs = 120000;
    QString m_harnessActivationId;
    HarnessCapabilitySnapshot m_harnessCapabilities;
    HarnessWorkerModule m_harnessWorkerModule;
    HarnessWorkerDriver *m_harnessWorker = nullptr;
    bool m_harnessWorkerReady = false;
    QHash<QString, QString> m_externalWorkerCalls; // driver call id → model tool-call id
    QHash<QString, QString> m_externalWorkerDescriptions;
    QHash<QString, QString> m_workerCapabilityCalls; // native call id → broker request id
    QHash<QString, QString> m_workerCapabilityNames;
    QString m_approvalMode = QStringLiteral("ask");
    bool    m_taskAutoApprove = false;   // override temporal durante una Task
    QString m_systemExtra;          // instrucciones extra del usuario (perfil de agente)
    double  m_temperature = -1.0;   // <0 = no enviar (default del server)
    bool    m_thinkingEnabled = false;
    QString m_reasoningEffort;
    int     m_reasoningBudget = -1;
    bool    m_thinkingLeakGuard = false;
    bool    m_stablePhasePrefix = true;
    QVariantList m_efficiencyRequests;
    // Directivas del system prompt ON (keys de directiveCatalog). Sin setear
    // (m_directivesSet=false) = todas activas (no regresiona el comportamiento).
    QSet<QString> m_directives;
    bool    m_directivesSet = false;

    QStringList m_pendingAttachments;  // adjuntos para el próximo sendMessage

    // Permisos por patrón (evaluados antes de la política global).
    enum PermAction { PermDeny, PermAllow, PermAsk };
    struct PermRule { PermAction action; QString kind; QRegularExpression rx; QString glob; };
    QList<PermRule> m_permRules;

    QSet<QString> m_disabledTools;   // tools off por el usuario (built-in y MCP)
    bool m_adaptiveToolRouting = false;
    ToolSurfaceRouter::Decision m_toolSurfaceDecision;
    QString m_teacherUrl, m_teacherModel, m_teacherKey;  // ask_teacher (config UI)
    QString m_auxiliaryUrl, m_auxiliaryEmbeddingModel, m_auxiliaryRerankModel,
        m_auxiliaryBearer;
    // Maestro CLI (claude-code / codex) por perfil.
    QString m_masterKind = QStringLiteral("none");
    QString m_masterCliName, m_masterCliPath, m_masterEscalation = QStringLiteral("manual");
    int     m_masterAutoAfterFails = 3;
    bool    m_masterApplyEdits = true;
    int     m_masterTimeoutS = 300;
    QVariantList m_masterChain;      // cadena de fallbacks resuelta (ver setMasterChain)
    QSet<QString> m_escalatedSigs;   // firmas ya escaladas al maestro (anti-recursión)
    bool masterAutoEnabled() const {
        return (!m_masterChain.isEmpty() || m_masterKind != QLatin1String("none"))
            && (m_masterEscalation == QLatin1String("auto")
                || m_masterEscalation == QLatin1String("both"));
    }

    QVariantList m_mcpConfig;        // config de servers MCP (de AppController)
    QVariantList m_mailAccounts;     // cuentas de correo (password resuelto)
    QVariantList m_webProviders;     // proveedores web externos opt-in
    HarnessSkillsModule m_skillPolicy; // ausente = todas las skills portables
    bool         m_mailAutoSend = false; // permitir email_send sin aprobación
    bool         m_hitlDestructive = true; // guardrail Zero-Autonomy (ver setHitlDestructive)
    QVariantList m_mcpTools;         // cache de tool-defs MCP del worker {server,name,description,schema}
    bool         m_mcpToolsEnabled = true; // false = no inyectar tools MCP (perfil mcpEnabled)

    // Worker thread para ejecución de tools (nativas + MCP).
    QThread *m_workerThread = nullptr;
    AgentToolRunner *m_worker = nullptr;
    QString m_execCallId;            // tool_call en ejecución ("" = ninguno)
    QString m_execCommand;           // comando/ruta del tool en ejecución (para la tarjeta)
    QString m_execToolName;
    QString m_execArguments;
    QTimer *m_toolWatchdog = nullptr;
    int m_quickToolTimeoutSec = 15;
    int m_execWatchdogSec = 15;
    AgentProgressGovernor m_progressGovernor;
    // Políticas del harness modular (defaults == comportamiento histórico).
    HarnessLoopModule m_loopPolicy;
    HarnessContextModule m_contextPolicy;
    HarnessEscalationModule m_escalationPolicy;
    HarnessMemoryModule m_memoryPolicy;
    HarnessKnowledgeModule m_knowledgePolicy;
    QVariantList m_customDirectives;   // {slug, body, when} del perfil
    int m_promptMaxChars = 0;          // 0 = sin tope (default histórico)
    int m_progressEvents = 0;
    int m_stagnationEvents = 0;
    int m_replanEvents = 0;
    qint64  m_lastUiEmitMs = 0;       // throttle de messagesChanged durante streaming
    // run_shell async: tarjeta de tool "en vivo" (creada al arrancar, actualizada
    // con chunks de salida, finalizada al terminar).
    QString m_liveToolCallId;
    int     m_liveToolMsgIdx = -1;
    qint64  m_lastToolEmitMs = 0;     // throttle de chunks de salida del shell
    // Métricas REALES de generación del server (no estimadas chars/4 + wall):
    // del objeto `timings`/`usage` del último completion. predicted_n tokens en
    // predicted_ms ms → tps de generación pura (sin prompt-processing/TTFT).
    int     m_genTokens = 0;
    double  m_genMs = 0.0;
    int     m_seed = -1;             // >=0: reproducibilidad de benchmark

    QString m_sessionId;
    QString m_sessionTitle;
    // Vista desacoplada del contexto que está ejecutando el turno. Permite mirar
    // otra sesión mientras el agente termina la original sin mezclar historiales.
    QString m_viewSessionId;
    QString m_viewSessionTitle;
    QString m_viewProjectDir;
    QVariantList m_viewMessages;
    // Una instancia completa por conversación concurrente: aísla request,
    // stream, contexto, tools, aprobaciones, compactación y subagentes.
    QHash<QString, LlamaAgentBackend *> m_sessionRuntimes;
    bool m_isSessionRuntime = false;
    QVariantList m_sessions;
    bool m_ephemeralSessions = false;
    // Estado para restaurar la sesión del usuario tras una Task efímera.
    QString m_preTaskSessionId;
    bool m_preTaskEphemeral = false;
    QVariantList m_messages;        // para UI: {role, content, typing}
    int m_curAsstIdx = -1;

    // Estado de streaming del turno en curso.
    QByteArray m_sseBuf;
    QByteArray m_streamErrBody;     // body crudo de una respuesta 4xx/5xx (no SSE) para diagnóstico
    QString    m_streamBase;        // contenido del bubble al iniciar el stream (se le concatena)
    QString    m_streamContent;     // delta.content acumulado
    QString    m_streamReason;      // delta.reasoning_content acumulado
    bool m_streamRepetitionDetected = false; // loop textual cortado durante generación
    bool m_streamToolCallCut = false;        // ráfaga de TOOL_CALL cortada en el segundo
    QHash<int, QJsonObject> m_streamToolCalls; // index → {id,name,arguments} mergeado

    // El transcript es la fuente de verdad inmutable de la sesión. m_apiMessages
    // es sólo la memoria de trabajo enviada al modelo y puede podarse/compactarse.
    QJsonArray m_transcriptMessages;
    QJsonArray m_apiMessages;
    QJsonArray m_pendingCalls;      // tool_calls restantes del turno actual
    QSet<QString> m_alwaysAllowed;  // kinds aprobados con "Siempre"

    // Robustez (Etapa 7)
    int m_turnIters = 0;                 // completions consumidas en el turno actual
    int m_emptyTextRetries = 0;          // reintentos por turno text-tools vacío (nudge)
    QString m_lastCallSignature;         // firma de la última tool_call del turno
    int m_sameCallStreak = 0;            // repeticiones consecutivas de esa firma
    QSet<QString> m_replannedCallSigs;    // firmas ya devueltas al modelo para cambiar estrategia
    QString m_failureFingerprint;        // error normalizado de la racha actual
    int m_equivalentFailures = 0;        // fallos consecutivos equivalentes
    bool m_turnHadDifficulty = false;    // hubo al menos una tool fallida en este turno
    bool m_turnRecovered = false;        // luego hubo progreso exitoso comprobable
    int m_toolOk = 0;                    // salud: tools exitosas en la sesión
    int m_toolFail = 0;                  // salud: tools con error/inválidas
    int m_ctxLimit = -1;                 // n_ctx del server (vía /props)
    QString m_lastDesktopTool;           // última tool desktop_* ejecutada (nudge text-tools)
    QString m_lastDesktopResult;
    QString m_lastDesktopTypeText;        // última entrada por desktop_type (guardrail teclado)
    QSet<QString> m_desktopLaunchApps;   // apps ya lanzadas en la sesión/Task actual
    QSet<QString> m_lifecycleFinishedToolIds;
    // Tope de seguridad MUY alto: no cortar trabajo legítimo. El loop infinito
    // real lo frena m_loopPolicy.sameCallLimit (misma tool + mismos args
    // repetidos). Que el agente haga tantas iteraciones como necesite. Este
    // fusible NO es configurable a propósito: es el último freno del harness.
    static constexpr int kMaxTurnIters = 1000;
    static constexpr int kMaxTransportRetryDelayMs = 5000;
    // sameCallLimit / transportRetries / failureSpiral viven ahora en
    // m_loopPolicy (HarnessLoopModule), con los mismos defaults 3 / 60 / 3.

    // Aprobación en curso (1 tool a la vez)
    QString m_awaitId;              // id del tool_call esperando respuesta ("" = ninguno)
    QJsonObject m_awaitCall;
    QString m_awaitPayloadHash;     // aprobación ligada al payload MCP exacto
    QString m_correlationId;        // un ID por turno, propagado a tools/recibos
    QString m_workClaimId;          // claim expirable del objetivo actual en el proyecto

    // Snapshots para revertir ediciones: path absoluto → {existía, contenido viejo}
    struct EditSnapshot { bool existed = false; QByteArray oldContent; };
    QHash<QString, EditSnapshot> m_editSnapshots;

    // Read-dedup: ruta relativa → huella (md5) del último contenido leído en la
    // sesión. Re-lectura con la misma huella → stub en el contexto. Se limpia al
    // cambiar/crear sesión.
    QHash<QString, QString> m_readFingerprints;

    // Mensajes encolados (modo "encolar"): se envían uno por uno al terminar cada
    // turno. Se limpia al cambiar/crear sesión.
    QStringList m_msgQueue;

    // Checkpoints para rollback: uno por turno de usuario (antes de enviar). Guarda
    // longitudes de m_messages/m_apiMessages y qué archivos ya estaban editados
    // (para revertir solo los editados DESPUÉS al rebobinar).
    struct Checkpoint { int apiLen; int transcriptLen; int msgLen; QStringList editKeys; };
    QList<Checkpoint> m_checkpoints;
    void pushCheckpoint();
    QJsonArray checkpointsToJson() const;
    void restoreCheckpoints(const QJsonArray &saved);

    // Sub-agentes en vuelo (callId → runner/worktree/branch/tarjeta). Mientras
    // m_subsOutstanding>0 el loop principal espera a que terminen todos.
    QHash<QString, SubAgentRunner *> m_subs;   // corriendo (callId → runner)
    QJsonArray              m_subQueue;        // task calls esperando un slot
    QHash<QString, QString> m_subWorktree;
    QHash<QString, QString> m_subBranch;
    QHash<QString, bool>    m_subIsolated;
    QHash<QString, int>     m_subMsgIdx;
};
