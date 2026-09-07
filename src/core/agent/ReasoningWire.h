#pragma once

#include <QJsonObject>
#include <QString>

namespace ReasoningWire {

inline QString normalizeEffort(const QString &value)
{
    const QString effort = value.trimmed().toLower();
    if (effort == QLatin1String("low")
        || effort == QLatin1String("medium")
        || effort == QLatin1String("high")
        || effort == QLatin1String("xhigh")
        || effort == QLatin1String("max"))
        return effort;
    return {};
}

inline QJsonObject templateKwargs(bool thinkingEnabled,
                                  bool thinkingLeakGuard = false,
                                  const QString &reasoningEffort = {})
{
    QJsonObject kwargs{{QStringLiteral("enable_thinking"), thinkingEnabled}};
    const QString effort = normalizeEffort(reasoningEffort);
    if (thinkingEnabled && !effort.isEmpty())
        kwargs.insert(QStringLiteral("reasoning_effort"), effort);
    if (thinkingLeakGuard)
        kwargs.insert(QStringLiteral("preserve_thinking"), false);
    return kwargs;
}

} // namespace ReasoningWire
