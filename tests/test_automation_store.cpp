// Unit tests de AutomationStore: (de)serialización JSON roundtrip, CRUD
// persistente con aislamiento de disco (QStandardPaths test mode), poda de
// huérfanas, y que el TaskScheduler::dueTaskIds selecciona automatizaciones por
// su scheduleSpec/cron igual que lo hacía con procesos.

#include <QtTest>
#include "core/tasks/AutomationStore.h"
#include "core/tasks/TaskScheduler.h"

class AutomationStoreTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void jsonRoundTrip();
    void crud_persistsAcrossInstances();
    void markRun_updatesStatus();
    void pruneOrphans_dropsUnlinked();
    void dueTaskIds_matchesScheduleSpec();
    void failedRunSchedulesExponentialRetry();
};

static QVariantMap sampleAutomation()
{
    return QVariantMap{
        {"id", "a1"}, {"name", "Dólar diario"}, {"processId", "proc-dolar"},
        {"scheduleEnabled", true}, {"scheduleCron", "0 9 * * *"},
        {"scheduleSpec", QVariantMap{{"mode", "daily"}, {"hour", 9}, {"minute", 0}}},
        {"silentUnlessError", true}
    };
}

void AutomationStoreTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void AutomationStoreTests::jsonRoundTrip()
{
    const QVariantMap in = sampleAutomation();
    const QVariantMap out = AutomationStore::fromJson(AutomationStore::toJson(in));
    QCOMPARE(out.value("name").toString(), QStringLiteral("Dólar diario"));
    QCOMPARE(out.value("processId").toString(), QStringLiteral("proc-dolar"));
    QCOMPARE(out.value("scheduleEnabled").toBool(), true);
    QCOMPARE(out.value("scheduleCron").toString(), QStringLiteral("0 9 * * *"));
    QCOMPARE(out.value("silentUnlessError").toBool(), true);
    QCOMPARE(out.value("scheduleSpec").toMap().value("mode").toString(), QStringLiteral("daily"));
    QCOMPARE(out.value("scheduleSpec").toMap().value("hour").toInt(), 9);
}

void AutomationStoreTests::crud_persistsAcrossInstances()
{
    QString id;
    {
        AutomationStore s;
        // limpia estado previo de otras corridas
        for (const QVariant &a : s.all()) s.remove(a.toMap().value("id").toString());
        id = s.save({}, QVariantMap{
            {"name", "Reporte"}, {"processId", "proc-x"},
            {"scheduleSpec", QVariantMap{{"mode", "weekly"}, {"hour", 8}, {"minute", 30},
                                         {"weekdays", QVariantList{1, 3, 5}}}}
        });
        QVERIFY(!id.isEmpty());
        QCOMPARE(s.count(), 1);
    }
    {
        AutomationStore s2;
        const QVariantMap a = s2.get(id);
        QCOMPARE(a.value("processId").toString(), QStringLiteral("proc-x"));
        QCOMPARE(a.value("scheduleSpec").toMap().value("mode").toString(), QStringLiteral("weekly"));
        QCOMPARE(a.value("scheduleSpec").toMap().value("weekdays").toList().size(), 3);
        QVERIFY(s2.remove(id));
    }
}

void AutomationStoreTests::markRun_updatesStatus()
{
    AutomationStore s;
    for (const QVariant &a : s.all()) s.remove(a.toMap().value("id").toString());
    const QString id = s.save({}, QVariantMap{{"name", "X"}, {"processId", "p"}});
    s.markRun(id, QStringLiteral("ok"), QStringLiteral("listo"));
    const QVariantMap a = s.get(id);
    QCOMPARE(a.value("lastRunStatus").toString(), QStringLiteral("ok"));
    QCOMPARE(a.value("lastRunSummary").toString(), QStringLiteral("listo"));
    QVERIFY(!a.value("lastRunAt").toString().isEmpty());
    s.remove(id);
}

void AutomationStoreTests::pruneOrphans_dropsUnlinked()
{
    AutomationStore s;
    for (const QVariant &a : s.all()) s.remove(a.toMap().value("id").toString());
    const QString keep = s.save({}, QVariantMap{{"name", "Keep"}, {"processId", "p-keep"}});
    const QString drop = s.save({}, QVariantMap{{"name", "Drop"}, {"processId", "p-gone"}});
    const int removed = s.pruneOrphans(QStringList{QStringLiteral("p-keep")});
    QCOMPARE(removed, 1);
    QCOMPARE(s.count(), 1);
    QVERIFY(!s.get(keep).isEmpty());
    QVERIFY(s.get(drop).isEmpty());
    s.remove(keep);
}

void AutomationStoreTests::dueTaskIds_matchesScheduleSpec()
{
    // Lunes 2024-01-01 09:00 → daily@9 matchea; otro a 10:00 no.
    const QDateTime now(QDate(2024, 1, 1), QTime(9, 0));
    QVariantList rows;
    rows.append(QVariantMap{
        {"id", "due"}, {"scheduleEnabled", true},
        {"scheduleSpec", QVariantMap{{"mode", "daily"}, {"hour", 9}, {"minute", 0}}}});
    rows.append(QVariantMap{
        {"id", "later"}, {"scheduleEnabled", true},
        {"scheduleSpec", QVariantMap{{"mode", "daily"}, {"hour", 10}, {"minute", 0}}}});
    rows.append(QVariantMap{
        {"id", "off"}, {"scheduleEnabled", false},
        {"scheduleSpec", QVariantMap{{"mode", "daily"}, {"hour", 9}, {"minute", 0}}}});
    const QStringList due = TaskScheduler::dueTaskIds(rows, now);
    QCOMPARE(due.size(), 1);
    QCOMPARE(due.first(), QStringLiteral("due"));
}

void AutomationStoreTests::failedRunSchedulesExponentialRetry()
{
    AutomationStore s;
    for (const QVariant &a : s.all()) s.remove(a.toMap().value("id").toString());
    const QString id = s.save({}, {{"name", "Retry"}, {"processId", "p"},
                                   {"scheduleEnabled", true}, {"retryMax", 2},
                                   {"retryBackoffSec", 10}});
    s.markRun(id, QStringLiteral("error"), QStringLiteral("falló"));
    QVariantMap a = s.get(id);
    QCOMPARE(a.value("retryCount").toInt(), 1);
    const QDateTime first = QDateTime::fromString(a.value("nextAttemptAt").toString(), Qt::ISODate);
    QVERIFY(first.isValid());
    QVERIFY(TaskScheduler::dueTaskIds({a}, first.addSecs(-1)).isEmpty());
    QCOMPARE(TaskScheduler::dueTaskIds({a}, first), QStringList{id});

    s.markRun(id, QStringLiteral("running"));
    s.markRun(id, QStringLiteral("error"));
    a = s.get(id);
    QCOMPARE(a.value("retryCount").toInt(), 2);
    QVERIFY(QDateTime::fromString(a.value("nextAttemptAt").toString(), Qt::ISODate)
                .secsTo(QDateTime::currentDateTimeUtc()) <= 0);
    s.markRun(id, QStringLiteral("ok"));
    a = s.get(id);
    QCOMPARE(a.value("retryCount").toInt(), 0);
    QVERIFY(a.value("nextAttemptAt").toString().isEmpty());
    s.remove(id);
}

QTEST_MAIN(AutomationStoreTests)
#include "test_automation_store.moc"
