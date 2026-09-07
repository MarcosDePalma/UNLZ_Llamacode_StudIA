#pragma once
#include <QObject>
#include <QDateTime>
#include <QSet>
#include <QString>
#include <QVariantList>

class AutomationStore;
class QTimer;

// TaskScheduler — disparador in-app de Automatizaciones programadas (cron). Tick
// por minuto; en cada tick consulta el AutomationStore por filas con
// scheduleEnabled + cron/spec válido que matcheen el minuto actual y emite
// automationDue(id). AppController conecta automationDue→ejecuta el proceso
// enlazado. Solo corre mientras la app vive (decisión: timer in-app).
//
// La selección es PURA y estática (dueTaskIds) → unit-testeable sin reloj real.
// La instancia evita doble-disparo en el mismo minuto con una clave id@minuto.
class TaskScheduler : public QObject
{
    Q_OBJECT
public:
    explicit TaskScheduler(AutomationStore *store, QObject *parent = nullptr);

    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }

    // Ids de filas que deben dispararse en `now` (scheduleEnabled + cron/spec
    // matchea). Pura: no toca estado de instancia ni de-duplica. tasks = filas
    // del store como QVariantList de QVariantMap.
    static QStringList dueTaskIds(const QVariantList &tasks, const QDateTime &now);

    // Evalúa `now` y emite automationDue por cada fila vencida no disparada aún
    // ese minuto. Expuesta para test (inyecta el instante); el tick interno la
    // llama con now().
    void evaluate(const QDateTime &now);

signals:
    void automationDue(const QString &id);

private:
    void tick();
    QVariantList snapshot() const;

    AutomationStore *m_store = nullptr;
    QTimer    *m_timer = nullptr;
    bool       m_enabled = false;
    QSet<QString> m_fired;   // claves "id@yyyyMMddHHmm" ya disparadas
};
