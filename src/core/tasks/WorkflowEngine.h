#pragma once

#include <QJsonObject>
#include <QVariantMap>

// Motor determinista de estado. La ejecucion de agent/tool permanece en el
// controlador existente para conservar permisos, concurrencia y cancelacion.
class WorkflowEngine
{
public:
    enum Status { Ready, Running, WaitingApproval, Completed, Failed, Cancelled };
    struct State {
        int schemaVersion = 1;
        QString workflowId;
        QString currentStep;
        QStringList completedSteps;
        QVariantMap results;
        QVariantMap variables;
        int iterations = 0;
        int repairAttempts = 0;
        qint64 startedAtMs = 0;
        Status status = Ready;
        QString error;
        QString lastVerdict;
    };

    static QString validate(const QJsonObject &definition);
    static State start(const QJsonObject &definition, const QString &workflowId,
                       const QVariantMap &variables = {});
    static QJsonObject currentStep(const QJsonObject &definition, const State &state);
    static bool completeStep(const QJsonObject &definition, State *state,
                             const QVariant &result, bool success,
                             const QString &route = QString());
    // Extrae el contrato de salida de una fase. El formato canónico es
    // `LC_GATE: PASS|FAIL|BLOCKED`; también acepta VERDICT para respuestas
    // producidas por modelos que sigan el contrato equivalente.
    static QString resultVerdict(const QVariant &result);
    static bool approve(const QJsonObject &definition, State *state,
                        const QString &choice, const QString &userText = QString());
    static QJsonObject toJson(const State &state);
    static State fromJson(const QJsonObject &object, QString *error = nullptr);
    static QString statusName(Status status);

private:
    static QString nextStep(const QJsonObject &step, const QString &route);
    static bool budgetExceeded(const QJsonObject &definition, const State &state);
    static bool repairBudgetExceeded(const QJsonObject &definition, const State &state);
};
