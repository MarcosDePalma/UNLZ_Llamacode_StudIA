// Unit tests del scheduler de Tasks: parser cron PURO (campos, listas, rangos,
// pasos, día de semana 0/7, OR dom/dow) y selección de Tasks vencidas
// (TaskScheduler::dueTaskIds, también pura).

#include <QtTest>
#include "core/tasks/CronSchedule.h"
#include "core/tasks/TaskSchedule.h"
#include "core/tasks/TaskScheduler.h"

class CronTests : public QObject
{
    Q_OBJECT
private slots:
    void parse_rejectsBadFieldCount();
    void parse_rejectsOutOfRange();
    void matches_exactMinuteHour();
    void matches_wildcards();
    void matches_stepAndRange();
    void matches_list();
    void matches_sundayZeroOrSeven();
    void matches_domDowOrSemantics();
    void dueTaskIds_selectsEnabledMatching();
    // TaskSchedule (modelo amigable)
    void sched_daily();
    void sched_weekly();
    void sched_monthlyByDate();
    void sched_monthlyNthWeekday();
    void sched_everyNMonths();
    void sched_cronDelegates();
    void dueTaskIds_prefersSpecOverCron();
};

static QDateTime dt(int y, int mo, int d, int h, int mi)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi));
}

void CronTests::parse_rejectsBadFieldCount()
{
    QVERIFY(!CronSchedule::parse(QStringLiteral("* * * *")).isValid());
    QVERIFY(!CronSchedule::parse(QStringLiteral("")).isValid());
    QVERIFY(CronSchedule::parse(QStringLiteral("* * * * *")).isValid());
}

void CronTests::parse_rejectsOutOfRange()
{
    QVERIFY(!CronSchedule::parse(QStringLiteral("60 * * * *")).isValid());
    QVERIFY(!CronSchedule::parse(QStringLiteral("* 24 * * *")).isValid());
    QVERIFY(!CronSchedule::parse(QStringLiteral("* * 0 * *")).isValid());   // dom min 1
    QVERIFY(!CronSchedule::parse(QStringLiteral("* * * 13 *")).isValid());
    QVERIFY(!CronSchedule::parse(QStringLiteral("* * * * 8")).isValid());
}

void CronTests::matches_exactMinuteHour()
{
    const CronSchedule c = CronSchedule::parse(QStringLiteral("30 9 * * *"));
    QVERIFY(c.isValid());
    QVERIFY(c.matches(dt(2026, 6, 15, 9, 30)));
    QVERIFY(!c.matches(dt(2026, 6, 15, 9, 31)));
    QVERIFY(!c.matches(dt(2026, 6, 15, 10, 30)));
}

void CronTests::matches_wildcards()
{
    const CronSchedule c = CronSchedule::parse(QStringLiteral("* * * * *"));
    QVERIFY(c.matches(dt(2026, 1, 1, 0, 0)));
    QVERIFY(c.matches(dt(2026, 12, 31, 23, 59)));
}

void CronTests::matches_stepAndRange()
{
    const CronSchedule c = CronSchedule::parse(QStringLiteral("*/15 9-17 * * *"));
    QVERIFY(c.matches(dt(2026, 6, 15, 9, 0)));
    QVERIFY(c.matches(dt(2026, 6, 15, 17, 45)));
    QVERIFY(!c.matches(dt(2026, 6, 15, 8, 0)));    // hora fuera de rango
    QVERIFY(!c.matches(dt(2026, 6, 15, 9, 10)));   // minuto no múltiplo de 15
}

void CronTests::matches_list()
{
    const CronSchedule c = CronSchedule::parse(QStringLiteral("0 8,12,18 * * *"));
    QVERIFY(c.matches(dt(2026, 6, 15, 8, 0)));
    QVERIFY(c.matches(dt(2026, 6, 15, 12, 0)));
    QVERIFY(c.matches(dt(2026, 6, 15, 18, 0)));
    QVERIFY(!c.matches(dt(2026, 6, 15, 9, 0)));
}

void CronTests::matches_sundayZeroOrSeven()
{
    // 2026-06-14 es domingo.
    const CronSchedule c0 = CronSchedule::parse(QStringLiteral("0 10 * * 0"));
    const CronSchedule c7 = CronSchedule::parse(QStringLiteral("0 10 * * 7"));
    QVERIFY(c0.matches(dt(2026, 6, 14, 10, 0)));
    QVERIFY(c7.matches(dt(2026, 6, 14, 10, 0)));
    QVERIFY(!c0.matches(dt(2026, 6, 15, 10, 0)));  // lunes
}

void CronTests::matches_domDowOrSemantics()
{
    // dom=1 y dow=lunes(1) ambos restringidos → OR. 2026-06-15 es lunes (no día 1).
    const CronSchedule c = CronSchedule::parse(QStringLiteral("0 0 1 * 1"));
    QVERIFY(c.matches(dt(2026, 6, 15, 0, 0)));   // lunes
    QVERIFY(c.matches(dt(2026, 7, 1, 0, 0)));    // día 1 (miércoles)
    QVERIFY(!c.matches(dt(2026, 6, 16, 0, 0)));  // martes, no día 1
}

void CronTests::dueTaskIds_selectsEnabledMatching()
{
    QVariantList tasks;
    tasks.append(QVariantMap{{"id", "a"}, {"scheduleEnabled", true},  {"scheduleCron", "30 9 * * *"}});
    tasks.append(QVariantMap{{"id", "b"}, {"scheduleEnabled", false}, {"scheduleCron", "30 9 * * *"}}); // off
    tasks.append(QVariantMap{{"id", "c"}, {"scheduleEnabled", true},  {"scheduleCron", "0 0 * * *"}});  // no match
    tasks.append(QVariantMap{{"id", "d"}, {"scheduleEnabled", true},  {"scheduleCron", "garbage"}});    // inválido

    const QStringList due = TaskScheduler::dueTaskIds(tasks, dt(2026, 6, 15, 9, 30));
    QCOMPARE(due, QStringList{QStringLiteral("a")});
}

// ───────────────────────── TaskSchedule (modelo amigable) ─────────────────────────
// Referencia de calendario: 2026-06-15 es lunes; 2026-06-14 domingo; los lunes de
// junio 2026 son 1, 8, 15, 22, 29 (primer lunes = 1, último = 29).

void CronTests::sched_daily()
{
    const QVariantMap s{{"mode", "daily"}, {"hour", 9}, {"minute", 30}};
    QVERIFY(TaskSchedule::matches(s, dt(2026, 6, 15, 9, 30)));
    QVERIFY(TaskSchedule::matches(s, dt(2026, 1, 1, 9, 30)));
    QVERIFY(!TaskSchedule::matches(s, dt(2026, 6, 15, 9, 31)));
}

void CronTests::sched_weekly()
{
    // Lunes (1) y miércoles (3) a las 9:30.
    const QVariantMap s{{"mode", "weekly"}, {"hour", 9}, {"minute", 30},
                        {"weekdays", QVariantList{1, 3}}};
    QVERIFY(TaskSchedule::matches(s, dt(2026, 6, 15, 9, 30)));   // lunes
    QVERIFY(TaskSchedule::matches(s, dt(2026, 6, 17, 9, 30)));   // miércoles
    QVERIFY(!TaskSchedule::matches(s, dt(2026, 6, 16, 9, 30)));  // martes
    QVERIFY(!TaskSchedule::matches(s, dt(2026, 6, 15, 10, 30))); // hora distinta
}

void CronTests::sched_monthlyByDate()
{
    const QVariantMap s{{"mode", "monthly"}, {"monthlyKind", "date"},
                        {"monthDay", 15}, {"hour", 9}, {"minute", 0}};
    QVERIFY(TaskSchedule::matches(s, dt(2026, 6, 15, 9, 0)));
    QVERIFY(TaskSchedule::matches(s, dt(2026, 7, 15, 9, 0)));
    QVERIFY(!TaskSchedule::matches(s, dt(2026, 6, 16, 9, 0)));
    // Día 31 en febrero (28 días) → clamp al último día.
    const QVariantMap s31{{"mode", "monthly"}, {"monthlyKind", "date"},
                          {"monthDay", 31}, {"hour", 0}, {"minute", 0}};
    QVERIFY(TaskSchedule::matches(s31, dt(2026, 2, 28, 0, 0)));
}

void CronTests::sched_monthlyNthWeekday()
{
    // Primer lunes (nth=1, wd=1).
    const QVariantMap first{{"mode", "monthly"}, {"monthlyKind", "nthWeekday"},
                            {"nth", 1}, {"nthWeekday", 1}, {"hour", 8}, {"minute", 0}};
    QVERIFY(TaskSchedule::matches(first, dt(2026, 6, 1, 8, 0)));
    QVERIFY(!TaskSchedule::matches(first, dt(2026, 6, 8, 8, 0)));   // segundo lunes
    // Último lunes (nth=5).
    const QVariantMap last{{"mode", "monthly"}, {"monthlyKind", "nthWeekday"},
                           {"nth", 5}, {"nthWeekday", 1}, {"hour", 8}, {"minute", 0}};
    QVERIFY(TaskSchedule::matches(last, dt(2026, 6, 29, 8, 0)));
    QVERIFY(!TaskSchedule::matches(last, dt(2026, 6, 22, 8, 0)));
}

void CronTests::sched_everyNMonths()
{
    // Cada 2 meses desde enero (1): ene, mar, may, jul... → mayo sí, junio no.
    const QVariantMap s{{"mode", "everyNMonths"}, {"everyN", 2}, {"startMonth", 1},
                        {"monthDay", 15}, {"hour", 9}, {"minute", 0}};
    QVERIFY(TaskSchedule::matches(s, dt(2026, 5, 15, 9, 0)));
    QVERIFY(!TaskSchedule::matches(s, dt(2026, 6, 15, 9, 0)));
    QVERIFY(TaskSchedule::matches(s, dt(2026, 7, 15, 9, 0)));
}

void CronTests::sched_cronDelegates()
{
    const QVariantMap s{{"mode", "cron"}, {"cron", "30 9 * * *"}};
    QVERIFY(TaskSchedule::isValid(s));
    QVERIFY(TaskSchedule::matches(s, dt(2026, 6, 15, 9, 30)));
    QVERIFY(!TaskSchedule::matches(s, dt(2026, 6, 15, 9, 31)));
    QVERIFY(!TaskSchedule::isValid(QVariantMap{{"mode", "weekly"}})); // sin weekdays
}

void CronTests::dueTaskIds_prefersSpecOverCron()
{
    // scheduleSpec válido tiene prioridad sobre scheduleCron.
    QVariantList tasks;
    tasks.append(QVariantMap{
        {"id", "a"}, {"scheduleEnabled", true},
        {"scheduleCron", "0 0 * * *"},   // no matchea 9:30
        {"scheduleSpec", QVariantMap{{"mode", "daily"}, {"hour", 9}, {"minute", 30}}}});
    const QStringList due = TaskScheduler::dueTaskIds(tasks, dt(2026, 6, 15, 9, 30));
    QCOMPARE(due, QStringList{QStringLiteral("a")});
}

QTEST_MAIN(CronTests)
#include "test_cron.moc"
