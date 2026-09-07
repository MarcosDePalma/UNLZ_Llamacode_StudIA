#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>

// RunHistoryStore — historial de corridas por "owner" (un Proceso del TaskStore o
// una Programación del AutomationStore). Cada corrida queda registrada con su
// inicio, fin, estado, resumen y el log/reporte completo del trabajo del agente.
//
// Persistencia: <AppLocalData>/run_history/<ownerId>.json (un archivo por owner,
// array de registros, más nuevo al final). Respeta
// QStandardPaths::setTestModeEnabled para aislamiento en tests. Se capa a los
// últimos kMaxPerOwner registros por owner.
//
// Una misma corrida lanzada vía Programación se registra dos veces: bajo el id
// del Proceso y bajo el id de la Programación, así ambas pestañas muestran su
// historial.
class RunHistoryStore : public QObject
{
    Q_OBJECT
public:
    static constexpr int kMaxPerOwner = 50;

    explicit RunHistoryStore(QObject *parent = nullptr);

    // Agrega un registro al historial del owner. `record` usa las claves de
    // toJson (startedAt/finishedAt/status/summary/source/log/metadata...).
    // `metadata` es extensible para artefactos y runtimes sin cambiar el
    // contrato de los campos históricos. Capa a los últimos kMaxPerOwner.
    Q_INVOKABLE void append(const QString &ownerId, const QVariantMap &record);
    // Historial del owner, más nuevo primero.
    Q_INVOKABLE QVariantList history(const QString &ownerId) const;
    // Borra el historial del owner.
    Q_INVOKABLE void clear(const QString &ownerId);

    static QString sanitize(const QString &id);
    static QJsonObject toJson(const QVariantMap &r);
    static QVariantMap fromJson(const QJsonObject &obj);

signals:
    void changed(const QString &ownerId);

private:
    QString storagePath(const QString &ownerId) const;
    QVariantList load(const QString &ownerId) const;   // orden de disco (más nuevo al final)
    void save(const QString &ownerId, const QVariantList &rows) const;
};
