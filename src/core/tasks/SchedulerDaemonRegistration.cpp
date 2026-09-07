#include "SchedulerDaemonRegistration.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

QString SchedulerDaemonRegistration::startupCommand(const QString &executable)
{
    return QStringLiteral("\"%1\" --scheduler-daemon").arg(QDir::toNativeSeparators(executable));
}

QString SchedulerDaemonRegistration::heartbeatPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("scheduler-daemon.heartbeat"));
}

bool SchedulerDaemonRegistration::setEnabled(bool enabled, const QString &executable, QString *error)
{
    if (error) error->clear();
#ifdef Q_OS_WIN
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    if (enabled) run.setValue(QStringLiteral("LlamaCodeScheduler"), startupCommand(executable));
    else run.remove(QStringLiteral("LlamaCodeScheduler"));
    run.sync();
    if (run.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("No se pudo actualizar el inicio automático de Windows.");
        return false;
    }
#elif defined(Q_OS_MACOS)
    const QString path = QDir(QDir::homePath()).filePath(QStringLiteral("Library/LaunchAgents/dev.llamacode.scheduler.plist"));
    if (!enabled) return !QFile::exists(path) || QFile::remove(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    const QByteArray xml = QStringLiteral("<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
        "<key>Label</key><string>dev.llamacode.scheduler</string><key>ProgramArguments</key><array>"
        "<string>%1</string><string>--scheduler-daemon</string></array><key>RunAtLoad</key><true/>"
        "</dict></plist>").arg(executable.toHtmlEscaped()).toUtf8();
    file.write(xml); return file.commit();
#else
    const QString path = QDir(QDir::homePath()).filePath(QStringLiteral(".config/autostart/llamacode-scheduler.desktop"));
    if (!enabled) return !QFile::exists(path) || QFile::remove(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    file.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=LlamaCode Scheduler\nExec=%1\n"
                              "X-GNOME-Autostart-enabled=true\n")
                   .arg(startupCommand(executable)).toUtf8());
    return file.commit();
#endif
    return true;
}

bool SchedulerDaemonRegistration::isRegistered()
{
#ifdef Q_OS_WIN
    QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                  QSettings::NativeFormat);
    return !run.value(QStringLiteral("LlamaCodeScheduler")).toString().isEmpty();
#elif defined(Q_OS_MACOS)
    return QFile::exists(QDir(QDir::homePath()).filePath(QStringLiteral("Library/LaunchAgents/dev.llamacode.scheduler.plist")));
#else
    return QFile::exists(QDir(QDir::homePath()).filePath(QStringLiteral(".config/autostart/llamacode-scheduler.desktop")));
#endif
}

QVariantMap SchedulerDaemonRegistration::status()
{
    const QFileInfo heartbeat(heartbeatPath());
    const qint64 age = heartbeat.exists()
        ? heartbeat.lastModified().msecsTo(QDateTime::currentDateTime()) : -1;
    return {{QStringLiteral("registered"), isRegistered()},
            {QStringLiteral("running"), age >= 0 && age < 45000},
            {QStringLiteral("heartbeatAgeMs"), age}};
}
