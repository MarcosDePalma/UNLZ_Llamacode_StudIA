#pragma once

#include <QVariantList>
#include <QVariantMap>

// Matriz declarativa de configuraciones que pueden medirse con llama-server.
// No lanza procesos: sirve igual para QML, ControlApi y tests headless.
class PerformanceMatrix
{
public:
    static QVariantList candidates(const QVariantMap &hardware,
                                   const QString &target = {},
                                   bool withVision = false);
    static QVariantMap annotate(const QVariantMap &sample,
                                const QVariantMap &hardware,
                                const QVariantMap &candidate);
    static QVariantList rank(const QVariantList &samples,
                             const QString &target = {});
};
