#pragma once

#include <QProcess>
#include <QString>
#include <QStringList>

// OS containment for external harness workers. `none` is the compatibility
// mode. `process` contains the worker lifecycle (Windows Job Object or a Unix
// process group). `strong` additionally needs bubblewrap on Unix; Windows
// deliberately reports it unavailable because a Job Object is not a network
// security boundary.
struct HarnessSandboxPolicy {
    QString mode = QStringLiteral("none"); // none|process|strong
    bool allowNetwork = false;
    int memoryLimitMb = 512;
    int processLimit = 32;
    int cpuTimeLimitSec = 0; // 0 = host default
};

struct HarnessSandboxPlan {
    bool supported = false;
    QString backend;
    QString program;
    QStringList arguments;
    QString error;
};

class HarnessSandbox final {
public:
    static HarnessSandboxPlan plan(const QString &program, const QStringList &arguments,
                                   const QString &workingDirectory,
                                   const HarnessSandboxPolicy &policy);
    static QStringList availableModes();

    bool attach(QProcess &process, const HarnessSandboxPolicy &policy,
                QString *error = nullptr);
    void terminate(QProcess &process);
    bool active() const;
    QString backend() const { return m_backend; }

private:
    QString m_backend;
    qint64 m_processGroupId = 0;
#ifdef Q_OS_WIN
    void *m_job = nullptr;
#endif
};
