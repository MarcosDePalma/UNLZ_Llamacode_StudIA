#include "WorkflowRunner.h"

WorkflowRunner::WorkflowRunner(QObject *parent) : QObject(parent) {}

bool WorkflowRunner::start(const QJsonObject &definition, const QString &workflowId,
                           const QVariantMap &variables)
{
    if (active()) return false;
    m_definition = definition;
    m_state = WorkflowEngine::start(definition, workflowId, variables);
    m_dispatched = false;
    m_paused = false;
    m_singleStep = false;
    emit stateChanged(snapshot());
    dispatch();
    return m_state.status != WorkflowEngine::Failed;
}

bool WorkflowRunner::restore(const QJsonObject &definition, const QJsonObject &saved)
{
    if (active() || !WorkflowEngine::validate(definition).isEmpty()) return false;
    QString error;
    auto restored = WorkflowEngine::fromJson(saved, &error);
    if (!error.isEmpty() || restored.workflowId.isEmpty()) return false;
    m_definition = definition;
    m_state = restored;
    m_dispatched = false;
    m_paused = false;
    m_singleStep = false;
    emit stateChanged(snapshot());
    dispatch();
    return true;
}

QJsonObject WorkflowRunner::snapshot() const { return WorkflowEngine::toJson(m_state); }

bool WorkflowRunner::active() const
{
    return m_state.status == WorkflowEngine::Running
        || m_state.status == WorkflowEngine::WaitingApproval;
}

void WorkflowRunner::reset()
{
    m_definition = {};
    m_state = {};
    m_dispatched = false;
    m_paused = false;
    m_singleStep = false;
}

void WorkflowRunner::setPaused(bool paused)
{
    m_paused = paused;
    if (!m_paused) dispatch();
}

void WorkflowRunner::step()
{
    if (!active()) return;
    m_singleStep = true;
    m_paused = false;
    dispatch();
}

QVariantMap WorkflowRunner::context() const
{
    return {{QStringLiteral("variables"), m_state.variables},
            {QStringLiteral("results"), m_state.results},
            {QStringLiteral("iterations"), m_state.iterations}};
}

QString WorkflowRunner::conditionRoute(const QJsonObject &step) const
{
    const QString variable = step.value(QStringLiteral("variable")).toString();
    const QVariant actual = m_state.variables.value(variable);
    const QVariant expected = step.value(QStringLiteral("equals")).toVariant();
    const bool matches = step.contains(QStringLiteral("equals"))
        ? actual == expected : actual.toBool();
    return matches ? QStringLiteral("onTrue") : QStringLiteral("onFalse");
}

void WorkflowRunner::dispatch()
{
    if (m_paused || !active() || m_dispatched) return;
    if (m_singleStep) {
        // Pausar antes de emitir evita que un completeCurrent() síncrono
        // encadene más de un paso durante el modo paso-a-paso.
        m_singleStep = false;
        m_paused = true;
    }
    const QJsonObject step = WorkflowEngine::currentStep(m_definition, m_state);
    const QString type = step.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("approval")) {
        m_dispatched = true;
        emit approvalRequested(m_state.currentStep, step.toVariantMap());
        return;
    }
    if (type == QLatin1String("condition")) {
        const QString route = conditionRoute(step);
        WorkflowEngine::completeStep(m_definition, &m_state, route, true, route);
        emit stateChanged(snapshot());
        dispatch();
        return;
    }
    if (type == QLatin1String("finish")) {
        WorkflowEngine::completeStep(m_definition, &m_state, QVariant(), true);
        emit stateChanged(snapshot());
        emit finished(true, snapshot());
        return;
    }
    m_dispatched = true;
    emit stepRequested(m_state.currentStep, type, step.toVariantMap(), context());
}

void WorkflowRunner::completeCurrent(const QVariant &result, bool success,
                                     const QString &route)
{
    if (m_state.status != WorkflowEngine::Running || !m_dispatched) return;
    m_dispatched = false;
    const QJsonObject step = WorkflowEngine::currentStep(m_definition, m_state);
    QString effectiveRoute = route;
    if (step.value(QStringLiteral("verdictRequired")).toBool()) {
        const QString verdict = WorkflowEngine::resultVerdict(result);
        if (verdict == QLatin1String("pass")) {
            success = true;
            if (effectiveRoute.isEmpty()) effectiveRoute = QStringLiteral("onSuccess");
        } else if (verdict == QLatin1String("blocked")) {
            success = false;
            if (effectiveRoute.isEmpty()) effectiveRoute = QStringLiteral("onBlocked");
        } else {
            // La ausencia de un recibo verificable también falla el gate: una
            // respuesta elocuente no sustituye evidencia estructurada.
            success = false;
            if (effectiveRoute.isEmpty()) effectiveRoute = QStringLiteral("onFailure");
        }
    }
    WorkflowEngine::completeStep(m_definition, &m_state, result, success, effectiveRoute);
    emit stateChanged(snapshot());
    if (!active()) { emit finished(m_state.status == WorkflowEngine::Completed, snapshot()); return; }
    dispatch();
}

void WorkflowRunner::approve(const QString &choice, const QString &userText)
{
    if (m_state.status != WorkflowEngine::WaitingApproval || !m_dispatched) return;
    const QJsonObject step = WorkflowEngine::currentStep(m_definition, m_state);
    QString target = step.value(choice).toString();
    if (target.isEmpty())
        target = step.value(QStringLiteral("choices")).toObject().value(choice).toString();
    if (choice == QLatin1String("reject") && (target.isEmpty() || target == QLatin1String("stop"))) {
        m_state.status = WorkflowEngine::Cancelled;
        m_state.error = userText.isEmpty() ? QStringLiteral("aprobación rechazada") : userText;
        m_dispatched = false;
        emit stateChanged(snapshot());
        emit finished(false, snapshot());
        return;
    }
    m_dispatched = false;
    WorkflowEngine::approve(m_definition, &m_state, choice, userText);
    emit stateChanged(snapshot());
    if (!active()) { emit finished(m_state.status == WorkflowEngine::Completed, snapshot()); return; }
    dispatch();
}

void WorkflowRunner::cancel(const QString &reason)
{
    if (!active()) return;
    m_state.status = WorkflowEngine::Cancelled;
    m_state.error = reason;
    m_dispatched = false;
    emit stateChanged(snapshot());
    emit finished(false, snapshot());
}
