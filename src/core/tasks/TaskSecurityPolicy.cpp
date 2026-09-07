#include "TaskSecurityPolicy.h"

#include <QVariantMap>

namespace TaskSecurityPolicy {

bool isStrictProfile(const QString &profile)
{
    return profile == QLatin1String("investigation")
        || profile == QLatin1String("guarded")
        || profile == QLatin1String("production");
}

bool autoApproveAllowed(const QVariantMap &task)
{
    return task.value(QStringLiteral("approvalPolicy"), QStringLiteral("sensitive")).toString()
               == QLatin1String("autonomous")
        && !isStrictProfile(task.value(QStringLiteral("safetyProfile"),
                                       QStringLiteral("normal")).toString());
}

QStringList disabledTools(const QVariantMap &task, const QVariantList &catalog)
{
    if (task.value(QStringLiteral("safetyProfile")).toString()
            != QLatin1String("production"))
        return {};

    QStringList result;
    for (const QVariant &value : catalog) {
        const QString name = value.toMap().value(QStringLiteral("name")).toString();
        if (name.startsWith(QStringLiteral("browser_"))
            || name.startsWith(QStringLiteral("web_"))
            || name.startsWith(QStringLiteral("email_")))
            result.append(name);
    }
    return result;
}

}
