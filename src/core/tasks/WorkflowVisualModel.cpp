#include "WorkflowVisualModel.h"

QVariantList WorkflowVisualModel::rows(const QVariantMap &definition)
{
    QVariantList out;
    const QVariantMap steps = definition.value(QStringLiteral("steps")).toMap();
    QStringList ids = steps.keys();
    const QString entry = definition.value(QStringLiteral("entry")).toString();
    if (ids.removeOne(entry)) ids.prepend(entry);
    for (const QString &id : ids) {
        const QVariantMap step = steps.value(id).toMap();
        QString routeKey;
        for (const QString &candidate : {QStringLiteral("next"), QStringLiteral("onSuccess"),
                                         QStringLiteral("onFailure"), QStringLiteral("onBlocked"),
                                         QStringLiteral("onTrue"), QStringLiteral("accept")}) {
            if (!step.value(candidate).toString().isEmpty()) { routeKey = candidate; break; }
        }
        out.append(QVariantMap{{QStringLiteral("stepId"), id},
            {QStringLiteral("stepType"), step.value(QStringLiteral("type"), QStringLiteral("agent"))},
            {QStringLiteral("stepNext"), step.value(routeKey).toString()},
            {QStringLiteral("stepPrompt"), step.value(QStringLiteral("prompt")).toString()},
            {QStringLiteral("routeKey"), routeKey}, {QStringLiteral("originalStep"), step}});
    }
    return out;
}

QVariantMap WorkflowVisualModel::merge(const QVariantMap &definition, const QVariantList &rows)
{
    QVariantMap out = definition;
    out[QStringLiteral("schemaVersion")] = 1;
    QVariantMap mergedSteps;
    QString firstId;
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        const QString id = row.value(QStringLiteral("stepId")).toString().trimmed();
        if (id.isEmpty() || mergedSteps.contains(id)) continue;
        if (firstId.isEmpty()) firstId = id;
        QVariantMap step = row.value(QStringLiteral("originalStep")).toMap();
        step[QStringLiteral("type")] = row.value(QStringLiteral("stepType"), QStringLiteral("agent"));
        const QString prompt = row.value(QStringLiteral("stepPrompt")).toString();
        if (prompt.isEmpty()) step.remove(QStringLiteral("prompt")); else step[QStringLiteral("prompt")] = prompt;
        QString routeKey = row.value(QStringLiteral("routeKey")).toString();
        if (routeKey.isEmpty()) routeKey = QStringLiteral("next");
        const QString next = row.value(QStringLiteral("stepNext")).toString().trimmed();
        if (next.isEmpty()) step.remove(routeKey); else step[routeKey] = next;
        mergedSteps[id] = step;
    }
    out[QStringLiteral("steps")] = mergedSteps;
    if (!mergedSteps.contains(out.value(QStringLiteral("entry")).toString()))
        out[QStringLiteral("entry")] = firstId;
    return out;
}
