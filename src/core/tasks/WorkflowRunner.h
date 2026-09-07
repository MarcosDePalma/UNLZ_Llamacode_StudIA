#pragma once

#include "WorkflowEngine.h"
#include <QObject>

// Orquestador asíncrono y agnóstico del backend. Los consumidores ejecutan los
// pasos agent/tool/parallel y devuelven el resultado con completeCurrent().
// Condiciones, finish, aprobaciones, snapshots y presupuesto quedan centralizados.
class WorkflowRunner : public QObject
{
    Q_OBJECT
public:
    explicit WorkflowRunner(QObject *parent = nullptr);

    bool start(const QJsonObject &definition, const QString &workflowId,
               const QVariantMap &variables = {});
    bool restore(const QJsonObject &definition, const QJsonObject &snapshot);
    QJsonObject snapshot() const;
    WorkflowEngine::State state() const { return m_state; }
    bool active() const;
    bool paused() const { return m_paused; }
    void reset();

public slots:
    void setPaused(bool paused);
    void step();
    void completeCurrent(const QVariant &result, bool success = true,
                         const QString &route = QString());
    void approve(const QString &choice, const QString &userText = QString());
    void cancel(const QString &reason = QString());

signals:
    void stepRequested(const QString &stepId, const QString &type,
                       const QVariantMap &step, const QVariantMap &context);
    void approvalRequested(const QString &stepId, const QVariantMap &step);
    void stateChanged(const QJsonObject &snapshot);
    void finished(bool success, const QJsonObject &snapshot);

private:
    void dispatch();
    QVariantMap context() const;
    QString conditionRoute(const QJsonObject &step) const;

    QJsonObject m_definition;
    WorkflowEngine::State m_state;
    bool m_dispatched = false;
    bool m_paused = false;
    bool m_singleStep = false;
};
