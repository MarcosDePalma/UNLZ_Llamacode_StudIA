#include "HybridPlanning.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>

QJsonObject HybridPlanning::parsePlan(const QString &text, QString *error)
{
    QString clean = text.trimmed();
    if (clean.startsWith(QStringLiteral("```"))) {
        const int firstNl = clean.indexOf(QLatin1Char('\n'));
        const int lastFence = clean.lastIndexOf(QStringLiteral("```"));
        if (firstNl >= 0 && lastFence > firstNl)
            clean = clean.mid(firstNl + 1, lastFence - firstNl - 1).trimmed();
    }
    const int begin = clean.indexOf(QLatin1Char('{'));
    const int end = clean.lastIndexOf(QLatin1Char('}'));
    if (begin < 0 || end < begin) {
        if (error) *error = QStringLiteral("el plan no contiene un objeto JSON");
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(
        clean.mid(begin, end - begin + 1).toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("JSON inválido: %1").arg(parseError.errorString());
        return {};
    }
    const QJsonObject plan = normalizePlan(doc.object());
    if (!validatePlan(plan, error)) return {};
    return plan;
}

QJsonObject HybridPlanning::normalizePlan(const QJsonObject &plan)
{
    QJsonObject out = plan;
    // schemaVersion como string ("1") es la otra desviación habitual.
    if (out.value(QStringLiteral("schemaVersion")).isString())
        out.insert(QStringLiteral("schemaVersion"),
                   out.value(QStringLiteral("schemaVersion")).toString().trimmed().toInt());

    const auto toLines = [](const QString &raw) {
        QJsonArray items;
        const QStringList lines = raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            QString item = line.trimmed();
            // Viñetas y numeración quedan fuera: el valor es el paso, no su marca.
            static const QRegularExpression bullet(
                QStringLiteral("^(?:[-*\\x{2022}]|\\d+[.)])\\s+"));
            item.remove(bullet);
            item = item.trimmed();
            if (!item.isEmpty()) items.append(item);
        }
        return items;
    };

    for (const QString &key : {QStringLiteral("assumptions"), QStringLiteral("files"),
                               QStringLiteral("steps"), QStringLiteral("tests"),
                               QStringLiteral("risks"), QStringLiteral("doneWhen")}) {
        const QJsonValue value = out.value(key);
        if (value.isArray()) {
            QJsonArray items;
            for (const QJsonValue &item : value.toArray()) {
                if (item.isString()) {
                    const QString text = item.toString().trimmed();
                    if (!text.isEmpty()) items.append(text);
                } else if (item.isObject()) {
                    items.append(QString::fromUtf8(
                        QJsonDocument(item.toObject()).toJson(QJsonDocument::Compact)));
                } else if (item.isArray()) {
                    items.append(QString::fromUtf8(
                        QJsonDocument(item.toArray()).toJson(QJsonDocument::Compact)));
                } else if (!item.isNull() && !item.isUndefined()) {
                    items.append(item.toVariant().toString());
                }
            }
            out.insert(key, items);
        } else if (value.isString()) {
            out.insert(key, toLines(value.toString()));
        } else {
            out.insert(key, QJsonArray{});
        }
    }
    return out;
}

bool HybridPlanning::validatePlan(const QJsonObject &plan, QString *error)
{
    if (plan.value(QStringLiteral("schemaVersion")).toInt() != 1) {
        if (error) *error = QStringLiteral("schemaVersion debe ser 1");
        return false;
    }
    if (plan.value(QStringLiteral("goal")).toString().trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("falta goal");
        return false;
    }
    for (const QString &key : {QStringLiteral("steps"), QStringLiteral("tests"),
                               QStringLiteral("risks"), QStringLiteral("doneWhen")}) {
        if (!plan.value(key).isArray()) {
            if (error) *error = QStringLiteral("%1 debe ser un array").arg(key);
            return false;
        }
    }
    if (plan.value(QStringLiteral("steps")).toArray().isEmpty()
        || plan.value(QStringLiteral("doneWhen")).toArray().isEmpty()) {
        if (error) *error = QStringLiteral("steps y doneWhen no pueden estar vacíos");
        return false;
    }
    if (QJsonDocument(plan).toJson(QJsonDocument::Compact).size() > 256 * 1024) {
        if (error) *error = QStringLiteral("plan demasiado grande");
        return false;
    }
    return true;
}

QString HybridPlanning::executorPrompt(const QString &request, const QJsonObject &plan)
{
    return QStringLiteral(
        "REQUEST ORIGINAL:\n%1\n\nPLAN ESTRUCTURADO DEL PLANIFICADOR (validalo contra la evidencia; "
        "si debés apartarte, registrá el motivo):\n%2\n\nEjecutá el trabajo completo, corré las "
        "verificaciones y respondé con el resultado final.")
        .arg(request.trimmed(), QString::fromUtf8(QJsonDocument(plan).toJson(QJsonDocument::Indented)));
}

QByteArray HybridPlanning::cacheKey(const QString &request, const QString &context,
                                    const QString &plannerId, int promptVersion)
{
    const QByteArray payload = request.toUtf8() + '\0' + context.toUtf8() + '\0'
        + plannerId.toUtf8() + '\0' + QByteArray::number(promptVersion);
    return QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex();
}
