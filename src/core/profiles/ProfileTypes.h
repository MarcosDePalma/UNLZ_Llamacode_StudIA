#pragma once
#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include "core/voice/VoiceTypes.h"
#include "HarnessSpec.h"

// Flag runtime-only en cada struct: perfil de SISTEMA (bundled desde
// assets/system_profiles.json). Inmutable (no se borra/edita/renombra) y NO se
// persiste a disco — se reconstruye del bundle en cada arranque. Default false
// (todo lo que viene del disco es de usuario). No se serializa en toJson/fromJson.
struct BackendProfile {
    QString id;
    bool system = false;
    QString name;
    QString binaryId;
    QString host = "127.0.0.1";
    int port = 8080;
    QStringList baseArgs;
    QMap<QString, QString> envOverrides;

    // Provider del backend. "local" = llama-server propio (binaryId/host/port).
    // "cloud" = endpoint OpenAI-compat externo (OpenAI/OpenRouter/Groq/DeepSeek…);
    // no lanza proceso ni binario, el agente pega directo al cloudBaseUrl.
    QString kind = "local";    // local | cloud
    QString cloudBaseUrl;      // sin /v1, ej https://api.openai.com
    // Nombre de la referencia al secreto (NO el secreto). El valor se resuelve vía
    // SecretStore (env var o store en disco fuera del repo). Nunca se serializa la key.
    QString cloudKeyRef;
    QString cloudModel;        // nombre de modelo a enviar (ej gpt-4o, anthropic/claude-...)
    int     cloudCtx = 0;      // n_ctx fallback (cloud no expone /props); 0 = default

    bool isCloud() const { return kind == QLatin1String("cloud"); }

    QJsonObject toJson() const;
    static BackendProfile fromJson(const QJsonObject &obj);
    static QString generateId();
};

struct ModelProfile {
    QString id;
    bool system = false;
    QString name;
    QString modelId;
    QString mmprojId;
    QString draftModelId;
    // Ancla estable al archivo, en paralelo a los ids de catálogo de arriba. Esos
    // se derivan de la ruta: mover el gguf, o un cambio en cómo el scanner los
    // mintea, los invalida y el perfil queda apuntando a la nada ("No model
    // selected"). El stable id se asigna una vez por archivo y no cambia, así que
    // sirve de respaldo para volver a encontrarlo. 0 = perfil viejo, sin ancla.
    qint64 modelStableId = 0;
    qint64 mmprojStableId = 0;
    qint64 draftStableId = 0;
    // Speculative decoding / MTP. draft-mtp también puede usar el cabezal embebido
    // del GGUF principal cuando su filename lo identifica como MTP.
    QString specType;          // "" | "draft-mtp" | "draft-dspark"
    int     specDraftNMax = 0; // --spec-draft-n-max (0 = no emitir)
    int     specDraftNMin = 0; // --spec-draft-n-min (0 = default; adaptive)
    bool    specDraftAdaptive = false; // --spec-draft-adaptive (opt-in)
    double  specDraftConfMin = 0.0; // --spec-draft-conf-min (0 = desactivado)
    QString specDraftNgl;      // --spec-draft-ngl  ("" | "all" | número de capas)
    QString specDraftTypeK;    // --spec-draft-type-k ("" | "q8_0" | "q4_0"...)
    QString specDraftTypeV;    // --spec-draft-type-v

    QJsonObject toJson() const;
    static ModelProfile fromJson(const QJsonObject &obj);
    static QString generateId();
};

struct RuntimePreset {
    QString id;
    bool system = false;
    QString name;
    int ctx = 4096;
    int batch = 512;
    int ubatch = 512;
    int threads = -1;
    int gpuLayers = -1;
    bool flashAttention = false;
    bool mmap = true;
    bool mlock = false;
    bool contBatching = true;
    // Política de perfiles: KV K/V puede ser q8_0 o menor; no ofrecer f16
    // como valor predeterminado para una candidata nueva.
    QString cacheType = "q8_0";
    int parallelSlots = 1;
    // Role-aware per-tensor quant. Cada entry = un spec de --override-tensor de
    // llama.cpp ("<regex>=<type>", ej "ffn_.*=Q4_K"). Mantener attention/output
    // en alta precisión y bajar sólo MLP → mejor cosine a igual tamaño que un
    // quant uniforme. Vacío = sin overrides.
    QStringList tensorOverrides;

    QJsonObject toJson() const;
    static RuntimePreset fromJson(const QJsonObject &obj);
    static QString generateId();
};

struct HarnessProfile {
    QString id;
    bool system = false;
    QString name;
    QString adapter;  // "none", "opencode", "aider", "llamaagent"
    QStringList args;
    QMap<QString, QString> env;

    QJsonObject toJson() const;
    static HarnessProfile fromJson(const QJsonObject &obj);
    static QString generateId();
};

struct WorkspaceProfile {
    QString id;
    bool system = false;
    QString name;
    QString cwd;
    QStringList allowedPaths;
    QStringList blockedPaths;
    bool allowShellCommands = false;

    QJsonObject toJson() const;
    static WorkspaceProfile fromJson(const QJsonObject &obj);
    static QString generateId();
};

// Perfil reutilizable de personalidad o estilo. Es una preferencia de expresión,
// nunca una fuente de permisos: no puede alterar tools, aprobaciones ni guardrails.
struct PersonaStyleProfile {
    QString id;
    bool system = false;
    QString name;
    QString kind = "writing-style"; // personality | writing-style
    QString description;
    QString styleCard;
    QStringList examples;
    bool enabled = true;
    int maxExamples = 2;
    int maxChars = 6000;

    QJsonObject toJson() const;
    static PersonaStyleProfile fromJson(const QJsonObject &obj);
    static QString generateId();
};

// Perfil de Agente: set de capacidades (tools) + directivas (secciones del system
// prompt) + ajustes (approval/thinking/temperatura/instrucciones extra). Registro
// global; los LaunchProfile y el modo agente lo referencian por id. Los 4 presets
// (Básico/Intermedio/Avanzado/Máximo) son de sistema (system=true, inmutables).
struct AgentProfile {
    QString id;
    bool system = false;
    QString name;
    QStringList enabledTools;       // nombres de LlamaAgentBackend::toolCatalog() ON
    QStringList directives;         // claves de directiveCatalog() ON
    QString approvalMode = "ask";   // auto | ask | manual | super | plan
    bool thinking = false;
    double temperature = -1.0;      // -1 = heredar del modelo/perfil
    QString systemExtra;            // instrucciones extra opcionales
    QStringList personalityProfileIds;
    QStringList styleProfileIds;
    bool injectStyleExamples = true;
    int styleExampleLimit = 2;
    int styleContextLimit = 6000;
    bool mcpEnabled = true;         // false = no inyectar tools MCP (ahorra contexto;
                                    // las tools MCP NO están en toolCatalog, así que
                                    // enabledTools no las puede apagar — esto sí)
    bool thinkingLeakGuard = false; // compatibilidad opt-in: no preservar thinking
                                    // previo y cortar colas tras </think> huérfano
    int progressCredits = 8;        // presupuesto elástico inicial de acciones
    int progressMaxCredits = 16;    // techo al renovar por evidencia nueva
    int progressReplanAfter = 3;    // acciones estancadas antes de replantear
    int progressStopAfter = 5;      // estancadas posteriores antes de cerrar
    int quickToolTimeoutSec = 15;   // watchdog para tools locales rápidas

    // HARNESS MODULAR: composición declarativa (ver HarnessSpec.h). Opcional:
    // un perfil sin spec (todos los guardados antes de la feature) se sigue
    // leyendo igual y toSpec() lo deriva de los campos de arriba. Cuando hay
    // spec, los campos legacy se siguen escribiendo derivados de él para que una
    // versión anterior del app pueda leer el archivo sin romperse.
    HarnessSpec spec;
    bool hasSpec = false;
    QString extendsId;              // preset/perfil base del que hereda el spec

    // Spec efectivo de este perfil: el declarado, o el derivado de los campos
    // legacy. Puro → testeable sin ProfileManager.
    HarnessSpec toSpec() const;
    // Vuelca un spec a los campos legacy (compatibilidad de lectura hacia atrás).
    void applySpecToLegacyFields(const HarnessSpec &resolved);

    QJsonObject toJson() const;
    static AgentProfile fromJson(const QJsonObject &obj);
    static QString generateId();
    // Los presets de sistema (orden: Chat liviano, Básico, Intermedio, Avanzado,
    // Máximo). ids estables ("agent-basico"…) para que los launch los referencien.
    static QList<AgentProfile> systemPresets();
    static QString defaultPresetId();   // "agent-intermedio"
};

// Un nivel de la cadena de fallbacks del maestro. El agente local escala el
// problema al primero; si ese también falla, al siguiente, y así hasta agotar
// la lista (ver tool ask_teacher). Ordenados de primero a último.
struct MasterFallback {
    QString type = "http";           // profile | http | cli
    QString label;                   // nombre opcional para la UI
    // type==profile: referencia a otro LaunchProfile del mismo LlamaCode.
    QString profileId;
    // type==http: endpoint OpenAI-compatible. httpKeyRef es una *referencia*
    // a SecretStore (nunca la key en claro en el JSON del perfil).
    QString httpUrl;
    QString httpModel;
    QString httpKeyRef;
    // type==cli: claude | codex.
    QString cliName;
    // Overrides por nivel.
    bool    applyEdits = true;       // CLI edita archivos directo vs sólo plan
    int     timeoutSec = 300;

    QJsonObject toJson() const;
    static MasterFallback fromJson(const QJsonObject &obj);
};

// Config del "maestro" (supervisor): cadena de modelos/CLIs más capaces a los
// que el agente local escala un sub-problema que no resuelve. Vive por
// LaunchProfile; si la cadena está vacía se usa el fallback global (Ajustes).
// Los campos legacy (kind/cliName/http*) se migran a un fallback único al leer.
struct MasterConfig {
    QList<MasterFallback> fallbacks;  // cadena ordenada (primero → último)
    QString escalation = "manual";   // manual | auto | both
    int     autoAfterFails = 3;      // gatillo auto: N fallos de la misma tool/firma

    // --- Legacy (un solo maestro). Se mantienen para migración/lectura vieja. ---
    QString kind = "none";           // none | http | cli
    QString cliName;                 // claude | codex   (kind==cli)
    QString httpUrl;                 // endpoint OpenAI-compat (kind==http)
    QString httpModel;
    QString httpKey;
    bool    applyEdits = true;
    int     timeoutSec = 300;

    bool isConfigured() const { return !fallbacks.isEmpty(); }

    QJsonObject toJson() const;
    static MasterConfig fromJson(const QJsonObject &obj);
};

struct LaunchProfile {
    QString id;
    bool    system = false;   // perfil de sistema (bundled, inmutable, no persistido)
    QString name;
    QString alias;            // opcional; tiene prioridad sobre name en la UI
    bool    best = false;     // perfil recomendado (rayo) y ordenado primero
    bool    favorite = false; // marcados con estrella y ordenados arriba
    QStringList tags;          // etiquetas libres para filtrar perfiles
    qint64 lastUsed = 0;       // epoch ms del último arranque exitoso
    bool    benchmark = false; // candidato pendiente para la cola de benchmark
    bool    systemBadge = false; // ícono de sistema; distinto de la inmutabilidad interna
    bool    deprecated = false; // visible sólo en Perfiles; excluido de uso operativo
    QString backendProfileId;
    QString modelProfileId;
    QString runtimePresetId;
    QString harnessProfileId;
    QString workspaceProfileId;
    // Perfil de agente por defecto al iniciar el modo agente con este launch.
    // Vacío = usar el preset por defecto (AgentProfile::defaultPresetId()).
    QString agentProfileId;
    // Política de razonamiento por request. Vacío/-1 = comportamiento heredado.
    QString reasoningEffort;       // "" | low | medium | high | xhigh | max
    int reasoningBudget = -1;      // -1 = ilimitado/heredado; 0 = sin thinking
    QStringList extraArgs;
    QMap<QString, QString> envOverrides;
    MasterConfig master;      // supervisor opcional (maestro CLI/HTTP)
    // Orquestación híbrida por turno. Cuando plannerProfileId no está vacío,
    // el request se planifica con ese LaunchProfile y se ejecuta con éste.
    // "sequential" permite compartir GPU/puerto descargando un modelo antes de
    // cargar el siguiente; "concurrent" queda reservado para endpoints distintos.
    QString plannerProfileId;
    QString hybridMode = QStringLiteral("off"); // off | sequential | concurrent
    // Límite de potencia de GPU (W) aplicado vía nvidia-smi al arrancar el server
    // de este perfil. 0 = sin override (usa el global de Ajustes, si hay).
    int powerLimitW = 0;
    // Override del toggle global de automatización de browser (MCP Playwright).
    // "inherit" = usar el global de Ajustes; "on"/"off" = forzar por perfil.
    QString browserAutomation = QStringLiteral("inherit");
    // Config del modo Charla (voz-a-voz) de este perfil: STT/TTS, servidores
    // gestionados, VAD. La Charla usa la del perfil activo.
    VoiceConfig voice;

    QJsonObject toJson() const;
    static LaunchProfile fromJson(const QJsonObject &obj);
    static QString generateId();
};

struct EffectiveProfile {
    QStringList effectiveArgs;
    QMap<QString, QString> effectiveEnv;
    QStringList warnings;
    QStringList blockingErrors;
    QString binaryPath;
    QString commandLine;

    bool isValid() const { return blockingErrors.isEmpty(); }
};
