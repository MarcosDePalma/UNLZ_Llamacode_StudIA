#pragma once
#include "IAgentBackend.h"
#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <functional>

// Backend opencode: lanza `opencode serve` (headless) y habla por HTTP + SSE.
class OpencodeBackend : public IAgentBackend
{
    Q_OBJECT
public:
    explicit OpencodeBackend(QObject *parent = nullptr);
    ~OpencodeBackend() override;

    QString adapter() const override { return QStringLiteral("opencode"); }
    bool running() const override;

    void start(const AgentContext &ctx) override;
    void stop() override;
    void sendMessage(const QString &text) override;

    void newSession() override;
    void newSessionInProject(const QString &projectDir) override;
    void switchSession(const QString &sessionId) override;
    void renameSession(const QString &sessionId, const QString &title) override;
    void deleteSession(const QString &sessionId) override;
    void forkSession(const QString &sessionId) override;
    void refreshSessions() override;

    void approveTool(const QString &id, bool always = false) override;
    void rejectTool(const QString &id) override;
    void setApprovalPolicy(const QString &mode) override { m_approvalMode = mode; }

    QString currentSessionId() const override { return m_sessionId; }
    QString currentSessionTitle() const override { return m_sessionTitle; }
    QString currentProjectDir() const override;
    QVariantList messages() const override { return m_messages; }
    QVariantList sessions() const override { return m_sessions; }

private:
    void emitSessionLifecycle();
    QString lifecycleCorrelation(const QString &sessionId) const;
    static QString compactJson(const QJsonObject &object);
    void emitToolRequest(const QString &sessionId, const QString &callId,
                         const QString &tool, const QString &arguments,
                         const QStringList &paths);
    void emitToolStart(const QString &sessionId, const QString &callId,
                       const QString &tool, const QString &arguments,
                       const QStringList &paths);
    void emitToolFinish(const QString &sessionId, const QString &callId,
                        const QString &tool, bool ok, const QString &result,
                        const QStringList &paths);
    void finishOpenLifecycleTools(const QString &sessionId, const QString &reason);
    void launchProcess();
    void initSession();
    void loadSessionList(std::function<void()> then);
    void resumeOrCreateSession();
    void doCreateSession();
    void loadSessionMessages(const QString &sessionId);
    void subscribeEvents();
    QVariantList &messagesForSession(const QString &sessionId);
    int &assistantIndexForSession(const QString &sessionId);
    // Publica sólo la sesión que el usuario está mirando. Las demás pueden
    // seguir generando en segundo plano sin contaminar el panel visible.
    void publishSessionMessages(const QString &sessionId);
    // response: "once" | "always" | "reject"
    void respondPermission(const QString &sessionId, const QString &permissionId,
                           const QString &response);
    // Clasifica una tool por su tipo: "read" | "write" | "shell".
    static QString toolKind(const QString &type);
    static QString projectNameFromDir(const QString &dir);

    QNetworkAccessManager *m_nam = nullptr;
    QProcess  *m_proc = nullptr;
    QNetworkReply *m_eventReply = nullptr;
    QString    m_attachUrl = QStringLiteral("http://127.0.0.1:4096");
    AgentContext m_ctx;
    QString    m_sessionId;
    QString    m_sessionTitle;
    QVariantList m_messages;
    QVariantList m_sessions;
    int        m_curAsstIdx = -1;
    QHash<QString, QVariantList> m_sessionMessages;
    QHash<QString, int> m_sessionAssistantIndices;
    bool       m_stopping = false;
    bool       m_forceNew = false;
    QString    m_approvalMode = QStringLiteral("ask");
    QHash<QString, QString> m_pendingPerm;  // permissionId -> sessionId
    QString    m_correlationId;
    QString    m_lifecycleSessionId;
    QHash<QString, QString> m_sessionCorrelations;
    QHash<QString, QVariantMap> m_lifecycleTools;
    QSet<QString> m_lifecycleRequested;
    QSet<QString> m_lifecycleStarted;
    QSet<QString> m_lifecycleFinished;
};
