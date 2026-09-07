#pragma once
#include "IAgentBackend.h"
#include <QHash>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// Backend "raw": chat directo a llama-server /v1/chat/completions (stream SSE).
// No lanza proceso externo; usa sesiones locales en memoria.
class RawChatBackend : public IAgentBackend
{
    Q_OBJECT
public:
    explicit RawChatBackend(QObject *parent = nullptr);
    ~RawChatBackend() override;

    QString adapter() const override { return QStringLiteral("raw"); }
    bool running() const override { return m_running; }

    void start(const AgentContext &ctx) override;
    void stop() override;
    void sendMessage(const QString &text) override;
    void cancelGeneration() override;
    void steerMessage(const QString &text) override;
    void queueMessage(const QString &text) override;
    int queuedCount() const override { return m_msgQueue.size(); }
    QStringList queuedMessages() const override { return m_msgQueue; }
    bool updateQueuedMessage(int index, const QString &text) override;
    bool removeQueuedMessage(int index) override;
    void clearQueue() override;

    void newSession() override;
    void newSessionInProject(const QString &projectDir) override;
    void switchSession(const QString &sessionId) override;
    void renameSession(const QString &sessionId, const QString &title) override;
    // Rebobinar: descarta el mensaje de usuario en msgIndex y todo lo posterior.
    void rollbackToMessage(int msgIndex);
    // Editar: reescribe el contenido del mensaje en msgIndex (user o IA) y
    // descarta todo lo posterior.
    void editMessage(int msgIndex, const QString &newText);
    void deleteSession(const QString &sessionId) override;
    void forkSession(const QString &sessionId) override;
    void refreshSessions() override;

    QString currentSessionId() const override { return m_sessionId; }
    QString currentSessionTitle() const override { return m_sessionTitle; }
    QString currentProjectDir() const override { return m_projectDir; }
    QVariantList messages() const override { return m_messages; }
    QVariantList sessions() const override { return m_sessions; }
    bool updateSessionProject(const QString &sessionId, const QString &projectId,
                              const QString &projectName, const QString &projectDir = QString());
    bool renameProject(const QString &oldName, const QString &newName);
    void setThinkingEnabled(bool enabled) { m_thinkingEnabled = enabled; }
    void setReasoningEffort(const QString &effort) { m_reasoningEffort = effort; }
    // Sampling por sesión. Un valor negativo significa heredar el default del
    // servidor; los valores se persisten junto con la sesión activa.
    QVariantMap sampling() const;
    void setSampling(const QVariantMap &sampling);
    void setSampling(double temperature, double topP, int topK);
    double temperature() const { return sampling().value(QStringLiteral("temperature"), -1.0).toDouble(); }
    double topP() const { return sampling().value(QStringLiteral("topP"), -1.0).toDouble(); }
    int topK() const { return sampling().value(QStringLiteral("topK"), -1).toInt(); }
    void setPendingAttachments(const QStringList &paths) { m_pendingAttachments = paths; }
    // Persona "Diseño": sesga al modelo a responder con artifacts visuales
    // (bloques ```mermaid / ```svg) que la UI rinde inline.
    void setPersonaDesigner(bool enabled) { m_personaDesigner = enabled; }


    // System prompt de la persona Diseño (público para test).
    static QString designerSystemPrompt();
    // Mensajes de sistema a prepender al request, según flags. Pura/testeable.
    // `systemExtra`: instrucciones persistentes del perfil para el modo Chat
    // (modulo `chat` del HarnessSpec). Van PRIMERAS: son las del usuario, no un
    // detalle de formato. Pura y estatica -> unit-testeable.
    static QJsonArray buildSystemPreamble(bool thinkingEnabled, bool designerPersona,
                                          const QString &systemExtra = QString());
    void setSystemExtra(const QString &extra) { m_systemExtra = extra; }
    QString systemExtra() const { return m_systemExtra; }
    // Salida estructurada: grammar GBNF o JSON schema (string JSON). Vacío = libre.
    void setStructuredOutput(const QString &grammar, const QString &jsonSchema) {
        m_grammar = grammar; m_jsonSchema = jsonSchema;
    }
    // Descarta sesiones sin ningún mensaje (creadas y abandonadas). `keepId`
    // nunca se toca (la sesión recién creada / recién abierta).
    void pruneEmptySessions(const QString &keepId);

private:
    void emitSessionLifecycle();
    QString storageDir() const;
    QString sessionFilePath(const QString &sessionId) const;
    void loadFromDisk();
    void persistIndex() const;
    void persistSession(const QString &sessionId) const;
    void removeSessionFile(const QString &sessionId) const;
    void persistAll() const;
    void createSession(const QString &projectDir);
    void createSession(const QString &projectId, const QString &projectName, const QString &projectDir);
    void setCurrentSession(const QString &sessionId);
    void saveCurrentMessages();
    QVariantList loadMessagesForSession(const QString &sessionId) const;

    AgentContext m_ctx;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    bool m_running = false;
    bool m_stopping = false;
    bool m_thinkingEnabled = false;
    QString m_reasoningEffort;
    bool m_personaDesigner = false;
    QString m_systemExtra;              // instrucciones del perfil (modulo `chat`)
    QString m_grammar;       // GBNF (passthrough a llama-server)
    QString m_jsonSchema;    // JSON schema (string) → response_format

    QString m_sessionId;
    QString m_correlationId;
    QString m_lifecycleSessionId;
    QString m_sessionTitle;
    QString m_projectDir;
    QVariantList m_messages;
    QVariantList m_sessions;
    QHash<QString, QVariantList> m_sessionMessages;
    QHash<QString, QVariantMap> m_sessionSampling;
    int m_curAsstIdx = -1;
    QByteArray m_sseBuf;
    QString m_reasonBuf;   // reasoning_content acumulado (thinking)
    QString m_answerBuf;   // content acumulado (respuesta)
    QStringList m_pendingAttachments;  // rutas a adjuntar en el próximo envío
    QStringList m_msgQueue;            // mensajes encolados (se envían al terminar)

private slots:
    void flushQueue();
};
