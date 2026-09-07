#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QString>
#include <QStringList>

#include "HarnessCapabilitySnapshot.h"
#include "HarnessSandbox.h"

class QTimer;
struct HarnessWorkerModule;
class HarnessWorkerDriver;

struct HarnessWorkerFrame {
    QString type;
    QJsonObject body;
};

class HarnessWorkerProtocol final {
public:
    static constexpr quint32 kHeaderBytes = 4;
    static constexpr quint32 kDefaultMaxFrameBytes = 1024 * 1024;

    static QByteArray encode(const QJsonObject &body, quint32 maxFrameBytes,
                             QString *error = nullptr);
    static bool take(QByteArray &buffer, quint32 maxFrameBytes, QJsonObject *body,
                     QString *error = nullptr);
    static bool validate(const QJsonObject &body, QString *error = nullptr);
};

class HarnessWorkerSession final {
public:
    bool acceptHelloAck(const QJsonObject &body, const QString &nonce, QString *error = nullptr);
    bool beginCall(const QString &callId, QString *error = nullptr);
    bool finishCall(const QString &callId, QString *error = nullptr);
    bool cancelCall(const QString &callId, QString *error = nullptr);
    bool authenticated() const { return m_authenticated; }
    bool hasCall(const QString &callId) const { return m_calls.contains(callId); }
    QSet<QString> calls() const { return m_calls; }

private:
    bool m_authenticated = false;
    QSet<QString> m_calls;
};

struct HarnessWorkerPolicy {
    quint32 maxFrameBytes = HarnessWorkerProtocol::kDefaultMaxFrameBytes;
    int startupTimeoutMs = 10000;
    int callTimeoutMs = 120000;
    bool allowNetwork = false;
    HarnessSandboxPolicy sandbox;
    HarnessCapabilitySnapshot capabilities;
    bool hasCapabilitySnapshot = false;
};

struct HarnessWorkerLaunchSpec {
    QString program;
    QStringList arguments;
    QString workingDirectory;
    HarnessWorkerPolicy policy;
};

// Converts the declarative profile module into an executable launch. It keeps
// runtime selection out of the SDKs: Node/Python only speak the wire contract,
// while the host chooses the interpreter, working directory and admission.
class HarnessWorkerFactory final {
public:
    static HarnessWorkerLaunchSpec build(const HarnessWorkerModule &module,
                                         const QString &projectDirectory,
                                         const QString &activationId,
                                         const QString &engineId,
                                         const QString &profileId,
                                         int generation,
                                         const QStringList &allowedCapabilities,
                                         QString *error = nullptr);
    static bool start(HarnessWorkerDriver &driver, const HarnessWorkerModule &module,
                      const QString &projectDirectory, const QString &activationId,
                      const QString &engineId, const QString &profileId, int generation,
                      const QStringList &allowedCapabilities, QString *error = nullptr);
};

// Driver host-side para Node/Python u otros workers. El worker nunca hereda la
// autoridad de la app: primero debe responder al nonce y luego sólo recibe
// llamadas explícitas con IDs no reutilizables.
class HarnessWorkerDriver final : public QObject {
    Q_OBJECT

public:
    explicit HarnessWorkerDriver(QObject *parent = nullptr);
    ~HarnessWorkerDriver() override;

    bool start(const QString &program, const QStringList &arguments,
               const QString &workingDirectory, const HarnessWorkerPolicy &policy = {});
    bool start(const HarnessWorkerLaunchSpec &spec)
    {
        return start(spec.program, spec.arguments, spec.workingDirectory, spec.policy);
    }
    bool call(const QString &callId, const QJsonObject &payload);
    bool cancel(const QString &callId);
    bool respondCapabilityCall(const QString &requestId, const QJsonObject &payload,
                               const QString &errorCode = QString(),
                               const QString &errorMessage = QString());
    void stop();
    bool running() const;
    bool authenticated() const { return m_session.authenticated(); }
    QString lastError() const { return m_lastError; }

signals:
    void authenticatedChanged(bool authenticated);
    void callResult(const QString &callId, const QJsonObject &payload);
    void capabilityCallRequested(const QString &requestId, const QString &capability,
                                 const QString &operation, const QJsonObject &payload);
    void workerError(const QString &message);
    void workerExited(int exitCode);

private:
    void fail(const QString &message);
    void readStdout();
    bool send(const QJsonObject &body);

    QProcess m_process;
    HarnessWorkerPolicy m_policy;
    HarnessWorkerSession m_session;
    QByteArray m_buffer;
    QString m_nonce;
    QString m_lastError;
    QTimer *m_handshakeTimer = nullptr;
    QTimer *m_callTimer = nullptr;
    QSet<QString> m_capabilityCalls;
    HarnessSandbox m_sandbox;
};
