#pragma once

#include <QString>
#include <QVariantMap>

class SchedulerDaemonRegistration
{
public:
    static QString startupCommand(const QString &executable);
    static bool setEnabled(bool enabled, const QString &executable, QString *error = nullptr);
    static bool isRegistered();
    static QString heartbeatPath();
    static QVariantMap status();
};
