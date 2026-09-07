#include "HarnessSandbox.h"

#include <QDir>
#include <QFileInfo>
#include <QOperatingSystemVersion>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <csignal>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace {

QString normalizedMode(const QString &raw)
{
    const QString mode = raw.trimmed().toLower();
    if (mode == QLatin1String("process") || mode == QLatin1String("strong")) return mode;
    return QStringLiteral("none");
}

bool validWorkerDirectory(const QString &path)
{
    return path.trimmed().isEmpty() || QFileInfo(path).isDir();
}

}  // namespace

HarnessSandboxPlan HarnessSandbox::plan(const QString &program, const QStringList &arguments,
                                        const QString &workingDirectory,
                                        const HarnessSandboxPolicy &policy)
{
    HarnessSandboxPlan out;
    out.program = program;
    out.arguments = arguments;
    const QString mode = normalizedMode(policy.mode);
    if (program.trimmed().isEmpty()) {
        out.error = QStringLiteral("worker program is empty");
        return out;
    }
    if (!validWorkerDirectory(workingDirectory)) {
        out.error = QStringLiteral("worker working directory does not exist");
        return out;
    }
    if (policy.memoryLimitMb < 0 || policy.processLimit < 1 || policy.cpuTimeLimitSec < 0) {
        out.error = QStringLiteral("invalid worker sandbox limits");
        return out;
    }
    if (mode == QLatin1String("none")) {
        out.supported = true;
        out.backend = QStringLiteral("none");
        return out;
    }

#ifdef Q_OS_WIN
    if (mode == QLatin1String("strong")) {
        out.error = QStringLiteral(
            "strong worker sandbox is unavailable on Windows; use process or install an "
            "external AppContainer policy");
        return out;
    }
    // The child is assigned to a Job Object after CreateProcess. This keeps
    // QProcess' normal stdio/quoting semantics and makes process mode usable
    // with both Node and CPython without a shell wrapper.
    out.supported = true;
    out.backend = QStringLiteral("windows-job-object");
    return out;
#else
    if (mode == QLatin1String("strong")) {
        QString bwrap = QStandardPaths::findExecutable(QStringLiteral("bwrap"));
        if (bwrap.isEmpty()) bwrap = QStandardPaths::findExecutable(QStringLiteral("bubblewrap"));
        if (bwrap.isEmpty()) {
            out.error = QStringLiteral(
                "strong worker sandbox requires bubblewrap (bwrap) on PATH");
            return out;
        }
        if (workingDirectory.trimmed().isEmpty()) {
            out.error = QStringLiteral("strong worker sandbox requires a working directory");
            return out;
        }
        // Read-only root + writable project and /tmp. Network is unshared by
        // default. The worker remains supervised by the parent through
        // --die-with-parent and --new-session.
        out.program = bwrap;
        out.arguments = {QStringLiteral("--die-with-parent"), QStringLiteral("--new-session"),
                         QStringLiteral("--unshare-pid"), QStringLiteral("--unshare-ipc"),
                         QStringLiteral("--unshare-uts"), QStringLiteral("--unshare-cgroup-try"),
                         QStringLiteral("--ro-bind"), QStringLiteral("/"), QStringLiteral("/"),
                         QStringLiteral("--proc"), QStringLiteral("/proc"),
                         QStringLiteral("--dev"), QStringLiteral("/dev"),
                         QStringLiteral("--tmpfs"), QStringLiteral("/tmp"),
                         QStringLiteral("--bind"), workingDirectory, QStringLiteral("/workspace"),
                         QStringLiteral("--chdir"), QStringLiteral("/workspace")};
        if (!policy.allowNetwork) out << QStringLiteral("--unshare-net");
        out << program << arguments;
        out.supported = true;
        out.backend = QStringLiteral("bubblewrap");
        return out;
    }
    const QString setsid = QStandardPaths::findExecutable(QStringLiteral("setsid"));
    if (setsid.isEmpty()) {
        out.error = QStringLiteral("process worker sandbox requires setsid on PATH");
        return out;
    }
    out.program = setsid;
    out.arguments = {program};
    out.arguments += arguments;
    out.supported = true;
    out.backend = QStringLiteral("unix-process-group");
    return out;
#endif
}

QStringList HarnessSandbox::availableModes()
{
    QStringList modes{QStringLiteral("none"), QStringLiteral("process")};
#ifndef Q_OS_WIN
    const QString bwrap = QStandardPaths::findExecutable(QStringLiteral("bwrap")).isEmpty()
        ? QStandardPaths::findExecutable(QStringLiteral("bubblewrap"))
        : QStandardPaths::findExecutable(QStringLiteral("bwrap"));
    if (!bwrap.isEmpty()) modes << QStringLiteral("strong");
#endif
    return modes;
}

bool HarnessSandbox::attach(QProcess &process, const HarnessSandboxPolicy &policy, QString *error)
{
#ifdef Q_OS_WIN
    if (m_job) {
        CloseHandle(static_cast<HANDLE>(m_job));
        m_job = nullptr;
    }
#endif
    m_backend.clear();
    m_processGroupId = 0;
    const QString mode = normalizedMode(policy.mode);
    if (mode == QLatin1String("none")) return true;

#ifdef Q_OS_WIN
    if (mode != QLatin1String("process")) {
        if (error) *error = QStringLiteral("unsupported Windows sandbox mode");
        return false;
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        if (error) *error = QStringLiteral("CreateJobObject failed (%1)").arg(GetLastError());
        return false;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (policy.processLimit > 0) {
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit =
            static_cast<DWORD>(qMax(1, policy.processLimit));
    }
    if (policy.memoryLimitMb > 0) {
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit = static_cast<SIZE_T>(policy.memoryLimitMb) * 1024u * 1024u;
    }
    if (policy.cpuTimeLimitSec > 0) {
        limits.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_TIME;
        limits.BasicLimitInformation.PerJobUserTimeLimit.QuadPart =
            static_cast<LONGLONG>(policy.cpuTimeLimitSec) * 10000000LL;
    }
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits))) {
        if (error) *error = QStringLiteral("SetInformationJobObject failed (%1)").arg(GetLastError());
        CloseHandle(job);
        return false;
    }
    HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                               FALSE, static_cast<DWORD>(process.processId()));
    if (!child || !AssignProcessToJobObject(job, child)) {
        if (error) *error = QStringLiteral("AssignProcessToJobObject failed (%1)").arg(GetLastError());
        if (child) CloseHandle(child);
        CloseHandle(job);
        return false;
    }
    CloseHandle(child);
    m_job = job;
    m_backend = QStringLiteral("windows-job-object");
    return true;
#else
    Q_UNUSED(policy);
    m_processGroupId = process.processId();
    m_backend = mode == QLatin1String("strong") ? QStringLiteral("bubblewrap")
                                                  : QStringLiteral("unix-process-group");
    return true;
#endif
}

void HarnessSandbox::terminate(QProcess &process)
{
#ifdef Q_OS_WIN
    if (m_job) {
        CloseHandle(static_cast<HANDLE>(m_job));
        m_job = nullptr;
    }
#else
    if (m_processGroupId > 0) {
        ::kill(static_cast<pid_t>(-m_processGroupId), SIGTERM);
        m_processGroupId = 0;
    }
#endif
    if (process.state() != QProcess::NotRunning) process.terminate();
    m_backend.clear();
}

bool HarnessSandbox::active() const
{
#ifdef Q_OS_WIN
    return m_job != nullptr;
#else
    return m_processGroupId > 0;
#endif
}
