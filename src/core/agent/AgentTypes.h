#pragma once
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QProcessEnvironment>

#include "core/profiles/HarnessSpec.h"

// Contexto para arrancar un backend de agente.
struct AgentContext {
    QString adapter;          // "opencode" | "goose" | "raw" | ...
    QString launchProfileId;
    QString exePath;          // binario del harness (vacío para raw)
    QString cwd;              // carpeta del proyecto
    QString serverBaseUrl;    // URL del llama-server (OpenAI-compatible), sin /v1
    QString modelId;          // modelo servido / alias
    QProcessEnvironment env;  // env ya armado para el proceso del harness
    // Provider cloud (OpenAI-compat): si apiKey != "" se manda Authorization Bearer
    // y se omite /props (el endpoint cloud no lo expone). ctxOverride fija n_ctx.
    QString apiKey;
    int     ctxOverride = 0;
    // Presupuesto de concurrencia del runtime local activo. El agente no intenta
    // crear más requests simultáneos que los slots realmente abiertos por server.
    int     parallelSlots = 1;
    double  vramTotalMb = 0.0;
    double  vramFreeMb = 0.0;
    // Contrato del harness. Vacío/legacy conserva exactamente el backend y el
    // almacenamiento históricos; los perfiles nuevos se aíslan por engine.
    QString harnessEngineId = QStringLiteral("legacy");
    int harnessEngineVersion = 1;
    QString harnessProfileId;
    QString harnessSpecHash;
    // Optional external plugin lane. The default is unset/builtin, which keeps
    // the historical in-process execution path unchanged.
    HarnessWorkerModule harnessWorker;
};

// Mensaje de chat del agente (rol + contenido + estado).
struct AgentMessage {
    QString role;             // "user" | "assistant" | "system"
    QString content;
    bool    typing = false;

    QVariantMap toMap() const {
        return { {"role", role}, {"content", content}, {"typing", typing} };
    }
};

// Sesión de agente (chat persistente, opcionalmente ligada a un proyecto).
struct AgentSession {
    QString id;
    QString title;
    double  created = 0;
    QString projectId;
    QString projectName;
    QString projectDir;
    QString parentSessionId;
    int     forkMessageIndex = -1;
    int     depth = 0;
    QString harnessEngineId = QStringLiteral("legacy");
    int     harnessEngineVersion = 1;
    QString harnessProfileId;
    QString harnessSpecHash;

    QVariantMap toMap() const {
        return {
            {"id", id}, {"title", title}, {"created", created},
            {"projectId", projectId}, {"projectName", projectName},
            {"projectDir", projectDir}, {"parentSessionId", parentSessionId},
            {"forkMessageIndex", forkMessageIndex}, {"depth", depth},
            {"harnessEngineId", harnessEngineId},
            {"harnessEngineVersion", harnessEngineVersion},
            {"harnessProfileId", harnessProfileId}, {"harnessSpecHash", harnessSpecHash}
        };
    }
};

// Pedido de aprobación de una tool (human-in-the-loop).
struct ToolCall {
    QString id;
    QString sessionId;
    QString name;             // "bash" | "edit" | "write" | ...
    QString summary;          // comando / archivo / descripción corta
    QString detail;           // diff o cuerpo completo

    QVariantMap toMap() const {
        return { {"id", id}, {"sessionId", sessionId}, {"name", name},
                 {"summary", summary}, {"detail", detail} };
    }
};
