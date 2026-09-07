#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class QLockFile;

struct AgentRunRecord {
    QString runId;
    QString requestHash;
    QString sessionId;
    QString correlationId;
    QString workspace;
    QString objective;
    QString status;
    QString detail;
    QString ownerId;
    QString leaseToken;
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    qint64 finishedAt = 0;
    qint64 leaseExpiresAt = 0;
    qint64 eventSequence = 0;
    int attempt = 0;
    QJsonObject beforeSnapshot;
    QJsonObject metadata;

    QJsonObject toJson(bool includeLease = true) const;
    static AgentRunRecord fromJson(const QJsonObject &object);
    bool isTerminal() const;
};

// Estado durable de una corrida del agente. El lease evita que un worker
// antiguo publique el resultado después de un reinicio o de una recuperación.
// Los eventos son append-only y tienen secuencia por run para permitir replay.
class AgentRunStore final
{
public:
    static constexpr int FormatVersion = 1;

    bool open(const QString &root, QString *error = nullptr);
    QString root() const { return m_root; }

    // runId funciona también como identidad idempotente de la solicitud: si ya
    // existe, se recupera sólo cuando el payload lógico coincide.
    QString accept(const QString &runId, const QString &sessionId,
                   const QString &correlationId, const QString &workspace,
                   const QString &objective, const QJsonObject &beforeSnapshot,
                   QString *error = nullptr);

    bool claim(const QString &runId, const QString &ownerId, qint64 leaseMs,
               QString *leaseToken, QString *error = nullptr);
    bool heartbeat(const QString &runId, const QString &leaseToken, qint64 leaseMs,
                   QString *error = nullptr);
    bool transition(const QString &runId, const QString &leaseToken,
                    const QString &status, const QString &detail = QString(),
                    const QJsonObject &metadata = {}, QString *error = nullptr);

    // Marca como uncertain una corrida cuyo lease expiró. No la reejecuta:
    // cualquier efecto externo ambiguo debe quedar visible para decisión humana.
    int recoverStaleRuns(qint64 nowMs = 0, QString *error = nullptr);

    // Resolución humana explícita de una corrida uncertain. Nunca reejecuta el
    // objetivo: sólo permite cerrar la evidencia como cancelled o failed.
    bool resolveUncertain(const QString &runId, const QString &status,
                         const QString &detail, QString *error = nullptr);
    bool mergeTerminalMetadata(const QString &runId, const QJsonObject &metadata,
                               QString *error = nullptr);

    AgentRunRecord record(const QString &runId) const;
    QJsonArray events(const QString &runId) const;
    // Snapshot renderer-safe: nunca incluye leaseToken ni beforeSnapshot.
    QJsonArray pending(int limit = 100) const;
    // Snapshot renderer-safe de todas las corridas, incluidas las terminales.
    QJsonArray all(int limit = 100) const;

private:
    QString recordPath(const QString &runId) const;
    QString eventPath(const QString &runId) const;
    bool writeRecord(const AgentRunRecord &record, QString *error = nullptr) const;
    bool appendEvent(AgentRunRecord &record, const QString &kind,
                     const QJsonObject &payload, QString *error = nullptr) const;
    QJsonArray eventsUnlocked(const QString &runId) const;
    AgentRunRecord recordUnlocked(const QString &runId) const;
    bool acquireLock(QLockFile &lock, QString *error = nullptr) const;
    static QString safeId(const QString &value);
    static QString requestHash(const QString &sessionId, const QString &workspace,
                               const QString &objective);
    static bool validStatus(const QString &status);

    QString m_root;
};
