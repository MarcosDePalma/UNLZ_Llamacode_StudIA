#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

// Registro append-only del contrato Next. Se mantiene separado de
// AgentEventLog, que es la bitácora histórica por proyecto, para que una
// comparación A/B no contamine la evidencia del perfil legacy.
class HarnessEventLog final {
public:
    bool open(const QString &path, QString *error = nullptr);
    bool append(const QString &kind, QJsonObject payload = {});
    bool load(QString *error = nullptr);
    QJsonArray events() const { return m_events; }
    QString path() const { return m_path; }
    qint64 nextSequence() const { return m_nextSequence; }

private:
    QString m_path;
    QJsonArray m_events;
    qint64 m_nextSequence = 1;
};
