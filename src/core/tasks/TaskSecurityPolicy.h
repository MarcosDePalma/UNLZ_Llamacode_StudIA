#pragma once

#include <QVariantList>
#include <QStringList>

// Decisiones de seguridad puras para Tasks. Mantenerlas fuera de AppController
// permite probarlas headless sin arrancar un agente ni un modelo.
namespace TaskSecurityPolicy {
bool isStrictProfile(const QString &profile);
bool autoApproveAllowed(const QVariantMap &task);
QStringList disabledTools(const QVariantMap &task, const QVariantList &catalog);
}
