#pragma once

#include <QStringList>
#include <QVariantMap>

// Política pura de recuperación. El estado y los budgets los conserva
// WorkflowEngine; esta clase sólo decide qué evidencia pedir y qué grounding
// probar después de un fallo, sin crear una segunda máquina de estados.
class DesktopRecoveryPolicy final
{
public:
    static QStringList strategies(const QString &preferred = QString());
    static bool shouldReobserve(const QString &error);
    static bool isAmbiguous(const QVariantMap &result);
    static QVariantMap contractForStep(const QVariantMap &step);
};
