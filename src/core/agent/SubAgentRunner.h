#pragma once
#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class QThread;
class AgentToolRunner;

// Sub-agente headless: corre un loop ReAct propio (sin UI/sesiones/MCP) contra
// el mismo llama-server, con su propio worker de tools confinado a `cwd` (una
// git worktree aislada). Lo usa la tool `task` del agente principal para delegar
// subtareas en paralelo. Emite finished(result) al terminar.
class SubAgentRunner : public QObject
{
    Q_OBJECT
public:
    SubAgentRunner(const QString &id, const QString &serverBaseUrl, const QString &modelId,
                   const QString &cwd, const QString &taskPrompt,
                   double temperature, bool honey = false, QObject *parent = nullptr);
    ~SubAgentRunner() override;

    QString id() const { return m_id; }
    QString cwd() const { return m_cwd; }
    void start();
    void cancel();

    // Guardrail Zero-Autonomy en el sub-agente: como corre headless (sin HITL), no
    // puede pedir aprobación. Con el guardrail ON, una tool destructiva/irreversible
    // se RECHAZA de plano (no se ejecuta) y se le devuelve al sub-agente un mensaje
    // para que difiera la acción al agente principal. ON por defecto; lo propaga el
    // agente principal desde su propio m_hitlDestructive. Sin efecto en modo super.
    void setHitlDestructive(bool on) { m_hitlDestructive = on; }
    // Política de ramas de revisión/verificación. Una rama read-only sólo
    // recibe tools de lectura; allowShell agrega la excepción necesaria para
    // ejecutar tests, siempre dentro del cwd confinado.
    void setReadOnly(bool on) { m_readOnly = on; }
    void setReadOnlyShell(bool allow) { m_readOnlyShell = allow; }

    // System prompt del sub-agente. Pura y estática → unit-testeable. honey=true
    // suma la directiva de frugalidad (código YAGNI, respuesta-primero, salida
    // mínima) para que el sub-árbol entero emita menos. Se propaga desde la
    // directiva 'honey' del perfil del agente principal.
    static QString systemPrompt(const QString &cwd, bool honey);

signals:
    void progressed(const QString &id, const QString &note);   // tool/avance (para tarjeta en vivo)
    void finished(const QString &id, const QString &result, bool ok);

private slots:
    void onToolExecuted(const QVariantMap &result);

private:
    void runCompletion();
    // Despacha el tool_call `call` al worker, salvo que el guardrail lo clasifique
    // destructivo: en ese caso NO ejecuta, inyecta un tool result de rechazo y sigue
    // con el resto del turno. Devuelve true si despachó (hay que esperar el worker),
    // false si lo bloqueó (ya avanzó al siguiente).
    bool dispatchCall(const QJsonObject &call);
    void handleStreamData();
    void handleStreamFinished(bool ok, const QString &err);
    void finishUp(const QString &result, bool ok);

    QString m_id;
    QString m_serverBaseUrl;
    QString m_modelId;
    QString m_cwd;
    QString m_taskPrompt;
    double  m_temperature = -1.0;
    bool    m_honey = false;
    bool    m_hitlDestructive = true;   // guardrail: rechazar destructivas (headless)
    bool    m_readOnly = false;
    bool    m_readOnlyShell = false;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QThread *m_workerThread = nullptr;
    AgentToolRunner *m_worker = nullptr;

    QJsonArray m_messages;        // conversación API del sub-agente
    QJsonArray m_pendingCalls;    // tool_calls del turno actual
    QString    m_execCallId;
    QString    m_lastAssistantText;

    QByteArray m_sseBuf;
    QString    m_streamContent;
    QHash<int, QJsonObject> m_streamToolCalls;

    int  m_iters = 0;
    bool m_done = false;
    static constexpr int kMaxIters = 40;
};
