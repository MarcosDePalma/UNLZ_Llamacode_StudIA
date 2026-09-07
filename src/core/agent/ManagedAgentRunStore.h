#pragma once

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

class QProcess;
class QTimer;
class RunHistoryStore;

// Orquesta corridas largas de Claude Code/Codex sin perder el contexto cuando
// el usuario vuelve a LlamaCode. Cada corrida tiene prompt, manifiesto, stdout
// y stderr propios, además de una fila resumida en RunHistoryStore.
//
// La superficie es deliberadamente agnóstica al canal de chat: AppController y
// ControlApi pueden iniciar la misma corrida, y la UI puede observar el mismo
// estado desde QML. El proceso se mantiene bajo QProcess para poder cancelarlo
// y para que el estado no dependa de una terminal visible.
class ManagedAgentRunStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList runs READ runs NOTIFY runsChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY runsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit ManagedAgentRunStore(RunHistoryStore *history,
                                  QObject *parent = nullptr);

    QVariantList runs() const;
    int activeCount() const { return m_processes.size(); }
    QString lastError() const { return m_lastError; }

    // request: runtime (claude|codex), prompt, workspace, ownerId/taskId,
    // cliPath opcional, applyEdits, approvalMode y presentation.
    Q_INVOKABLE QString startRun(const QVariantMap &request);
    Q_INVOKABLE bool stopRun(const QString &runId);
    Q_INVOKABLE QString retryRun(const QString &runId);
    Q_INVOKABLE QVariantMap run(const QString &runId) const;
    Q_INVOKABLE QString log(const QString &runId) const;
    Q_INVOKABLE bool removeRun(const QString &runId);

    // Función pura para validar el contrato y probar la política de permisos
    // sin arrancar un proceso real.
    static QVariantMap commandForRequest(const QVariantMap &request);

signals:
    void runsChanged();
    void runChanged(const QString &runId, const QVariantMap &run);
    void logAppended(const QString &runId, const QString &channel,
                     const QString &chunk);
    void runFinished(const QString &runId, const QVariantMap &run);
    void lastErrorChanged();

private:
    QString storageRoot() const;
    QString runDirectory(const QString &runId) const;
    static QString nowUtc();
    static QString newRunId();
    static QString tail(const QString &text, int maxChars = 65536);
    static QString runtimeName(const QVariantMap &request);
    static bool validRuntime(const QString &runtime);
    static QString resolveCliPath(const QVariantMap &request);

    void loadPersistedRuns();
    bool writeTextFile(const QString &path, const QByteArray &data,
                      bool append = false) const;
    bool persist(const QVariantMap &run) const;
    void setError(const QString &message);
    void updateRun(const QString &runId, const QVariantMap &run,
                   bool persistManifest = true);
    void appendOutput(const QString &runId, const QString &channel,
                      const QByteArray &data);
    void finishRun(const QString &runId, int exitCode, const QString &status,
                   const QString &error = QString());
    void completeRun(const QString &runId, const QString &status,
                     const QString &error = QString());
    void startVerification(const QString &runId);
    void finishVerification(const QString &runId, int exitCode,
                            const QString &error = QString());
    void requestStop(const QString &runId, const QString &reason);
    void housekeeping();
    void cleanupRetention();
    void recordHistory(const QVariantMap &run);

    RunHistoryStore *m_history = nullptr;
    QHash<QString, QVariantMap> m_runs;
    QHash<QString, QProcess *> m_processes;
    QHash<QString, QProcess *> m_verifiers;
    QHash<QString, bool> m_stopRequested;
    QHash<QString, QString> m_stopReasons;
    QHash<QString, QJsonObject> m_beforeSnapshots;
    QHash<QString, QString> m_verificationOutput;
    QTimer *m_housekeepingTimer = nullptr;
    QString m_lastError;
};
