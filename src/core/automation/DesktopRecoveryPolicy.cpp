#include "DesktopRecoveryPolicy.h"

QStringList DesktopRecoveryPolicy::strategies(const QString &preferred)
{
    QStringList result;
    const QStringList all{QStringLiteral("uia"), QStringLiteral("ocr"),
                          QStringLiteral("template"), QStringLiteral("normalized")};
    const QString first = preferred.trimmed().toLower();
    if (!first.isEmpty() && all.contains(first)) result << first;
    for (const QString &item : all)
        if (!result.contains(item)) result << item;
    return result;
}

bool DesktopRecoveryPolicy::shouldReobserve(const QString &error)
{
    const QString text = error.toLower();
    return text.contains(QStringLiteral("stale"))
        || text.contains(QStringLiteral("obsoleto"))
        || text.contains(QStringLiteral("cambió"))
        || text.contains(QStringLiteral("cambio"))
        || text.contains(QStringLiteral("ambigu"))
        || text.contains(QStringLiteral("no se encontró"))
        || text.contains(QStringLiteral("no se encontro"));
}

bool DesktopRecoveryPolicy::isAmbiguous(const QVariantMap &result)
{
    return result.value(QStringLiteral("ambiguous")).toBool()
        || result.value(QStringLiteral("candidateCount")).toInt() > 1
        || result.value(QStringLiteral("matchCount")).toInt() > 1;
}

QVariantMap DesktopRecoveryPolicy::contractForStep(const QVariantMap &step)
{
    QVariantMap result = step;
    if (!result.contains(QStringLiteral("precondition")))
        result[QStringLiteral("precondition")] = QVariantMap{
            {QStringLiteral("targetPresent"), true},
            {QStringLiteral("snapshotRequired"), true}};
    if (!result.contains(QStringLiteral("postcondition"))) {
        QVariantMap post;
        const QString intent = result.value(QStringLiteral("intent")).toString().trimmed();
        if (!intent.isEmpty()) post[QStringLiteral("intent")] = intent.left(240);
        if (result.contains(QStringLiteral("assert")))
            post[QStringLiteral("assert")] = result.value(QStringLiteral("assert"));
        result[QStringLiteral("postcondition")] = post;
    }
    if (!result.contains(QStringLiteral("repair")))
        result[QStringLiteral("repair")] = QVariantMap{
            {QStringLiteral("maxAttempts"), 2},
            {QStringLiteral("reobserveOn"), QVariantList{
                QStringLiteral("stale"), QStringLiteral("ambiguous"),
                QStringLiteral("target_changed")}},
            {QStringLiteral("strategies"), strategies(
                result.value(QStringLiteral("strategy")).toString())}};
    return result;
}
