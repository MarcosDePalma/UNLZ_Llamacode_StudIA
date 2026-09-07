#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Contrato de ejecución. Un spec que no declara este módulo es siempre
// compatible con el harness histórico: no cambia backend, rutas ni sesiones.
struct HarnessRuntimeModule {
    QString engine = QStringLiteral("legacy");
    int version = 1;
    QString fallbackEngine = QStringLiteral("legacy");
    bool experimental = false;
    bool set = false;

    QJsonObject toJson() const;
    static HarnessRuntimeModule fromJson(const QJsonObject &o);
};

// External worker lane. Absent means the historical in-process path. A worker
// asks for capabilities but never grants itself authority; the host admits the
// request and snapshots the result for the activation.
struct HarnessWorkerModule {
    QString lane = QStringLiteral("builtin"); // builtin|node|python
    QString entrypoint;
    QStringList arguments;
    QString sandbox = QStringLiteral("none"); // none|process|strong
    QString workingDirectory;
    bool allowNetwork = false;
    int maxFrameBytes = 1024 * 1024;
    int startupTimeoutMs = 10000;
    int callTimeoutMs = 120000;
    int memoryLimitMb = 512;
    int processLimit = 32;
    int cpuTimeLimitSec = 0;
    QStringList requestedCapabilities;
    bool set = false;

    QJsonObject toJson() const;
    static HarnessWorkerModule fromJson(const QJsonObject &o);
};

// HARNESS MODULAR: un perfil de agente deja de ser un preset cerrado y pasa a
// ser una COMPOSICION declarativa de modulos (HarnessSpec). Cada modulo tiene
// defaults identicos al comportamiento historico del backend, asi que un spec
// vacio == lo que hacia LlamaCode antes de esta feature.
//
// INVARIANTE CENTRAL: "modulo ausente = HEREDADO, nunca vacio". Por eso cada
// modulo lleva un flag `set`: distingue "no lo declare" (heredar del padre) de
// "lo declare vacio" (lista vacia a proposito). Sin eso la herencia miente y un
// perfil hijo apagaria todas las tools sin pedirlo.
//
// La resolucion es por MODULO (no por campo): si el hijo declara `tools`, su
// modulo `tools` reemplaza entero al del padre. Es la regla que se documenta al
// usuario y la que testea test_harness_spec.
//
// Ver docs/plan-harness-modular.md y docs/harness.md.

// ---------------------------------------------------------------------------
// Modulos
// ---------------------------------------------------------------------------

// Tools ofrecidas al modelo. Resolucion: packs -> expandir -> include suma ->
// exclude resta. Determinista y ordenada por el catalogo.
struct HarnessToolsModule {
    QStringList packs;                 // claves de HarnessTools::packCatalog()
    QStringList include;               // nombres sueltos, o "*" = todo
    QStringList exclude;               // se restan al final
    bool mcpToolsEnabled = true;       // inyectar tools MCP descubiertas
    bool adaptiveRouting = false;      // recortar tools en el primer turno
    bool set = false;

    QJsonObject toJson() const;
    static HarnessToolsModule fromJson(const QJsonObject &o);
};

// Skills portables disponibles para el agente. `include` vacío en un módulo
// declarado significa "ninguna"; `*` habilita todas las descubiertas. `exclude`
// siempre gana. Módulo ausente = todas, para conservar perfiles legacy.
struct HarnessSkillsModule {
    QStringList include;              // slugs o "*"
    QStringList exclude;              // slugs bloqueados
    bool set = false;

    QJsonObject toJson() const;
    static HarnessSkillsModule fromJson(const QJsonObject &o);
};

// Composicion del system prompt: directivas built-in + packs de usuario (.md).
struct HarnessPromptModule {
    QStringList builtin;               // keys de directiveCatalog(), o "*"
    QStringList custom;                // slugs de directivas de usuario
    QString systemExtra;
    int maxChars = 24000;              // tope del prompt compuesto (aviso, no crash)
    bool set = false;

    QJsonObject toJson() const;
    static HarnessPromptModule fromJson(const QJsonObject &o);
};

// Presupuesto y frenos del loop ReAct. Los defaults son las constantes que
// hasta ahora vivian compiladas en LlamaAgentBackend.
struct HarnessLoopModule {
    int credits = 8;                   // AgentProgressGovernor::Policy
    int maxCredits = 16;
    int replanAfter = 3;
    int stopAfter = 5;
    int maxDistinctWrites = 24;
    int sameCallLimit = 3;             // kMaxSameCall
    int failureSpiral = 3;             // kFailureSpiralThreshold
    int transportRetries = 60;         // kMaxTransportRetries
    int quickToolTimeoutSec = 15;      // watchdog de tools locales rapidas
    int webToolTimeoutSec = 180;       // watchdog de web_*/mcp/research
    int streamIdleTimeoutSec = 0;      // 0 = env LLAMACODE_STREAM_IDLE_TIMEOUT / 3600
    bool set = false;

    QJsonObject toJson() const;
    static HarnessLoopModule fromJson(const QJsonObject &o);
};

// Politica de contexto (compactacion, poda, imagenes, dedup, warmup).
struct HarnessContextModule {
    bool compaction = true;
    double compactionTrigger = 0.90;   // fraccion de n_ctx que dispara el plan
    double tailRatio = 0.60;           // parte del budget reservada a la cola
    bool prune = true;                 // poda determinista de resultados repetidos
    int keepLastImages = 1;            // trimStaleImages
    bool readDedup = true;             // stub al releer el mismo archivo
    bool preflight = false;            // ContextPreflight al abrir un objetivo
    QString indexPolicy = QStringLiteral("lazy"); // off|lazy|eager
    int scoutBudget = 700;             // presupuesto del scout/preflight
    int scoutK = 8;                    // cantidad máxima de rangos iniciales
    bool graphExpansion = true;        // vecinos estructurales del contexto
    bool warmup = true;                // prefill del prompt-cache (Charla)
    bool set = false;

    QJsonObject toJson() const;
    static HarnessContextModule fromJson(const QJsonObject &o);
};

// Permisos: politica + reglas por patron + alcance de filesystem + guardrails.
struct HarnessPermissionsModule {
    QString approvalMode = QStringLiteral("ask");   // auto|ask|manual|super|plan
    QStringList rules;                 // "allow|deny|ask [kind:]<glob>"
    QString fsScope = QStringLiteral("project");    // project|folder|full
    QStringList folders;               // scope folder
    // true sólo si el JSON trae `fsScope` explícito. Un perfil legacy (spec
    // derivado) NO declara alcance, y por eso no debe pisar el de una Task en
    // curso: sin este flag, actualizar a esta versión estrecharía a "project"
    // toda Task con permisos de carpeta o disco completo.
    bool fsScopeDeclared = false;
    bool hitlDestructive = true;       // guardrail Zero-Autonomy
    bool mailAutoSend = false;
    bool set = false;

    QJsonObject toJson() const;
    static HarnessPermissionsModule fromJson(const QJsonObject &o);
};

// Escalacion: sub-agentes + umbrales del DifficultyRouter + gatillo del maestro.
struct HarnessEscalationModule {
    int maxParallelSubagents = 5;      // techo propio (el absoluto sigue mandando)
    bool isolateSubagents = true;      // git worktree por sub-agente
    QString masterEscalation;          // "" = heredar del LaunchProfile
    int masterAutoAfterFails = 0;      // 0 = heredar
    // Cadena de maestros del PERFIL DE AGENTE. Misma forma que MasterFallback
    // (type/cliName/httpUrl/httpModel/httpKeyRef/profileId/applyEdits/timeoutSec).
    // Vacía = usar la del LaunchProfile activo. Se guarda como JSON crudo para no
    // arrastrar ProfileTypes acá: AppController la convierte con
    // MasterFallback::fromJson y la resuelve con buildMasterChain (secretos y
    // cliPath incluidos).
    QJsonArray masterFallbacks;
    int routerFilesAffected = 8;
    int routerContextTokens = 24000;
    int routerRepeatedFailures = 3;
    int routerAgentCycles = 5;
    double routerConfidenceFloor = 0.3;
    bool set = false;

    QJsonObject toJson() const;
    static HarnessEscalationModule fromJson(const QJsonObject &o);
};

// Memoria inyectada en el system prompt del agente: hechos estructurados
// (MemoryStore) + memoria de proyecto (.llamacode/memory.md o AGENTS.md).
// Todo esto estaba hardcodeado: 12 hechos y 64 KB de archivo, sin forma de
// apagarlo. En un perfil al limite de contexto es lo primero que uno querria
// recortar, y en uno de investigacion lo primero que querria ampliar.
struct HarnessMemoryModule {
    bool structuredEnabled = true;     // hechos de MemoryStore
    int  structuredFacts = 12;         // cuantos hechos vigentes recuperar
    bool projectMemory = true;         // .llamacode/memory.md / AGENTS.md
    int  projectMemoryMaxChars = 65536;
    bool consolidateOnLeave = true;    // consolidar al dejar la sesion
    bool set = false;

    QJsonObject toJson() const;
    static HarnessMemoryModule fromJson(const QJsonObject &o);
};

// Ensamblado opcional de hechos y relaciones con evidencia para el contexto
// del agente. `enabled=false` conserva el comportamiento histórico: las tools
// memory/graph siguen disponibles según el pack, pero no se inyecta un paquete
// automático en cada turno.
struct HarnessKnowledgeModule {
    bool enabled = false;
    bool preflight = false;          // agregarlo al preflight inicial
    bool citeSources = true;         // pedir citas path:line al modelo
    int maxFacts = 8;
    int maxEdges = 12;
    int maxChars = 12000;
    bool set = false;

    QJsonObject toJson() const;
    static HarnessKnowledgeModule fromJson(const QJsonObject &o);
};

// Modo Chat (RawChatBackend): hasta ahora el spec no lo tocaba en absoluto, asi
// que un perfil "minimal" seguia chateando con los defaults globales. No lleva
// tools ni loop: sampling, razonamiento e instrucciones persistentes.
struct HarnessChatModule {
    bool thinking = false;
    QString reasoningEffort;           // "" | low | medium | high | xhigh | max
    double temperature = -1.0;         // <0 = no enviar (default del server)
    double topP = -1.0;
    int topK = -1;
    bool designerPersona = false;      // preamble de diagramas/mockups
    QString systemExtra;               // instrucciones persistentes del perfil
    bool set = false;

    QJsonObject toJson() const;
    static HarnessChatModule fromJson(const QJsonObject &o);
};

// Protocolo de wire + razonamiento.
struct HarnessProtocolModule {
    QString toolProtocol = QStringLiteral("auto");  // auto|native|text
    bool thinking = false;
    bool thinkingLeakGuard = false;
    double temperature = -1.0;         // <0 = heredar del modelo/perfil
    QString reasoningEffort;           // "" | low | medium | high | xhigh | max
    int reasoningBudget = -1;
    bool set = false;

    QJsonObject toJson() const;
    static HarnessProtocolModule fromJson(const QJsonObject &o);
};

// ---------------------------------------------------------------------------
// Spec
// ---------------------------------------------------------------------------

struct HarnessSpec {
    QString extends;                   // id del spec padre ("" = defaults)
    HarnessRuntimeModule runtime;
    HarnessWorkerModule worker;
    HarnessToolsModule tools;
    HarnessSkillsModule skills;
    HarnessPromptModule prompt;
    HarnessLoopModule loop;
    HarnessContextModule context;
    HarnessPermissionsModule permissions;
    HarnessEscalationModule escalation;
    HarnessProtocolModule protocol;
    HarnessMemoryModule memory;
    HarnessKnowledgeModule knowledge;
    HarnessChatModule chat;
    // Overrides por fase: "plan" | "exec" | "verify" | "goalCheck" -> patch JSON
    // con la misma forma que el spec (solo los modulos que pisa). Se guardan sin
    // resolver para que forPhase() aplique el patch sobre el spec YA resuelto.
    QMap<QString, QJsonObject> phases;

    bool isEmpty() const;

    QJsonObject toJson() const;
    static HarnessSpec fromJson(const QJsonObject &o);

    // Resolucion en capas: devuelve `base` con los modulos que `override`
    // declaro (set=true) reemplazados. Los `phases` se mergean por clave.
    static HarnessSpec resolve(const HarnessSpec &base, const HarnessSpec &override);

    // Aplica el patch de una fase sobre un spec ya resuelto. Fase inexistente =
    // devuelve `spec` igual (comportamiento historico: una sola configuracion).
    static HarnessSpec forPhase(const HarnessSpec &spec, const QString &phase);

    // Diferencias legibles contra `base`: lista de {module, field, base, value}.
    // Es lo que hace mantenible un perfil propio ("cambia 6 cosas vs Avanzado").
    QVariantList diff(const HarnessSpec &base) const;
};

// ---------------------------------------------------------------------------
// Packs de tools + validacion de entorno
// ---------------------------------------------------------------------------

namespace HarnessTools {

// {key, name, description, tools:[...]}. Incluye packs compuestos (core, rag,
// web, rpa, ...) y un pack por grupo del toolCatalog().
QVariantList packCatalog();

// Expande un modulo a la lista final de tools habilitadas, en el orden del
// catalogo. Vacio => se interpreta como "sin tools" solo si el modulo fue
// declarado; el caller decide (ver AgentProfile::toSpec).
QStringList resolve(const HarnessToolsModule &module);

// Nombres del catalogo que NO estan en `enabled` (lo que espera setDisabledTools).
    QStringList disabledFrom(const QStringList &enabled);

// Costo aproximado en tokens de un set de tools (suma de approxTokens).
int approxTokens(const QStringList &enabled);

// Entorno para el preflight de dependencias.
struct Environment {
    bool hasGit = true;
    bool hasEmbeddings = true;
    bool hasDesktop = true;
    bool hasMailAccount = true;
    bool hasMcpServers = true;
    bool hasBrowser = true;
};

// Advertencias por tools habilitadas cuya dependencia falta. No bloquea: el
// runtime ya devuelve error por tool; esto es para el editor y headless.
// `mcpToolsEnabled` es el gate del módulo `tools`: sólo tiene sentido avisar por
// servers MCP faltantes si el perfil efectivamente los quiere inyectar.
QStringList dependencyWarnings(const QStringList &enabled, const Environment &env,
                               bool mcpToolsEnabled = false);

}  // namespace HarnessTools

namespace HarnessSkills {

// Decide si un slug puede ser descubierto/cargado por el harness efectivo.
bool allows(const HarnessSkillsModule &module, const QString &slug);

}  // namespace HarnessSkills

namespace HarnessPolicy {

// Interseccion de alcances de filesystem: nunca amplia. Devuelve el mas
// restrictivo de los dos (project < folder < full) y, en folder, la
// interseccion de carpetas.
QString narrowerScope(const QString &a, const QString &b);
QStringList intersectFolders(const QString &scopeA, const QStringList &a,
                             const QString &scopeB, const QStringList &b);

}  // namespace HarnessPolicy
