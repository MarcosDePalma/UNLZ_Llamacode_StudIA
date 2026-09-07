#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

class HybridPlanning
{
public:
    static QJsonObject parsePlan(const QString &text, QString *error = nullptr);
    // Tolera las formas que los modelos devuelven de más: un string donde se
    // esperaba un array, listas ausentes, o items no-string.
    static QJsonObject normalizePlan(const QJsonObject &plan);
    static bool validatePlan(const QJsonObject &plan, QString *error = nullptr);
    static QString executorPrompt(const QString &request, const QJsonObject &plan);
    static QByteArray cacheKey(const QString &request, const QString &context,
                               const QString &plannerId, int promptVersion = 1);
};
