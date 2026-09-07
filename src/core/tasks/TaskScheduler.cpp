#include "TaskScheduler.h"
#include "AutomationStore.h"
#include "CronSchedule.h"
#include "TaskSchedule.h"
#include <QTimer>

TaskScheduler::TaskScheduler(AutomationStore *store, QObject *parent)
    : QObject(parent), m_store(store)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(20 * 1000);   // tick frecuente; el de-dup por minuto evita repetir
    connect(m_timer, &QTimer::timeout, this, &TaskScheduler::tick);
}

void TaskScheduler::setEnabled(bool on)
{
    if (m_enabled == on) return;
    m_enabled = on;
    if (on) {
        m_timer->start();
        tick();   // evalúa inmediato al activar
    } else {
        m_timer->stop();
    }
}

QVariantList TaskScheduler::snapshot() const
{
    return m_store ? m_store->all() : QVariantList{};
}

QStringList TaskScheduler::dueTaskIds(const QVariantList &tasks, const QDateTime &now)
{
    QStringList due;
    for (const QVariant &tv : tasks) {
        const QVariantMap t = tv.toMap();
        if (!t.value("scheduleEnabled", false).toBool()) continue;
        const QDateTime retryAt = QDateTime::fromString(
            t.value("nextAttemptAt").toString(), Qt::ISODate);
        if (retryAt.isValid()) {
            if (retryAt <= now) due << t.value("id").toString();
            continue;
        }
        // Modelo amigable (scheduleSpec) tiene prioridad; si no, cron crudo legacy.
        const QVariantMap spec = t.value("scheduleSpec").toMap();
        if (TaskSchedule::isValid(spec)) {
            if (TaskSchedule::matches(spec, now))
                due << t.value("id").toString();
            continue;
        }
        const QString cron = t.value("scheduleCron").toString().trimmed();
        if (cron.isEmpty()) continue;
        const CronSchedule cs = CronSchedule::parse(cron);
        if (cs.isValid() && cs.matches(now))
            due << t.value("id").toString();
    }
    return due;
}

void TaskScheduler::evaluate(const QDateTime &now)
{
    const QString minuteKey = now.toString(QStringLiteral("yyyyMMddHHmm"));
    for (const QString &id : dueTaskIds(snapshot(), now)) {
        const QString key = id + QLatin1Char('@') + minuteKey;
        if (m_fired.contains(key)) continue;
        m_fired.insert(key);
        emit automationDue(id);
    }
    // Poda: conserva solo el minuto actual (claves viejas ya no se repiten).
    if (m_fired.size() > 256) {
        QSet<QString> keep;
        for (const QString &k : m_fired)
            if (k.endsWith(minuteKey)) keep.insert(k);
        m_fired = keep;
    }
}

void TaskScheduler::tick()
{
    if (!m_enabled) return;
    evaluate(QDateTime::currentDateTime());
}
