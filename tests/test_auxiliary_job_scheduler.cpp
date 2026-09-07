#include <QtTest>
#include <QSignalSpy>

#include "core/AuxiliaryJobScheduler.h"

class AuxiliaryJobSchedulerTests : public QObject
{
    Q_OBJECT
private slots:
    void startsHighestPriorityFirst();
    void equalPriorityIsFifo();
    void respectsClassLimit();
    void nonPositiveClassLimitBlocksOnlyThatClass();
    void blocksBusyResource();
    void cancellingRunningJobReleasesResource();
    void failedJobIsTerminal();
    void cancelQueuedJob();
};

static QString stateFor(const QVariantList &rows, const QString &id)
{
    for (const QVariant &row : rows) {
        const QVariantMap m = row.toMap();
        if (m.value(QStringLiteral("id")).toString() == id)
            return m.value(QStringLiteral("state")).toString();
    }
    return {};
}

void AuxiliaryJobSchedulerTests::startsHighestPriorityFirst()
{
    AuxiliaryJobScheduler s;
    const QString low = s.enqueue(QStringLiteral("document"), {}, 1);
    const QString high = s.enqueue(QStringLiteral("voice"), {}, 10);

    QString started;
    QVERIFY(s.startNext(&started));
    QCOMPARE(started, high);
    QCOMPARE(stateFor(s.snapshot(), high), QStringLiteral("running"));
    QCOMPARE(stateFor(s.snapshot(), low), QStringLiteral("queued"));
}

void AuxiliaryJobSchedulerTests::equalPriorityIsFifo()
{
    AuxiliaryJobScheduler s;
    const QString first = s.enqueue(QStringLiteral("document"), {}, 3);
    const QString second = s.enqueue(QStringLiteral("document"), {}, 3);

    QString started;
    QVERIFY(s.startNext(&started));
    QCOMPARE(started, first);
    QVERIFY(s.complete(first));
    QVERIFY(s.startNext(&started));
    QCOMPARE(started, second);
}

void AuxiliaryJobSchedulerTests::respectsClassLimit()
{
    AuxiliaryJobScheduler s;
    s.setClassLimit(QStringLiteral("document"), 1);
    const QString first = s.enqueue(QStringLiteral("document"), {}, 5);
    const QString second = s.enqueue(QStringLiteral("document"), {}, 4);

    QString started;
    QVERIFY(s.startNext(&started));
    QCOMPARE(started, first);
    QVERIFY(!s.startNext(&started));

    QVERIFY(s.complete(first));
    QVERIFY(s.startNext(&started));
    QCOMPARE(started, second);
}

void AuxiliaryJobSchedulerTests::nonPositiveClassLimitBlocksOnlyThatClass()
{
    AuxiliaryJobScheduler s;
    s.setClassLimit(QStringLiteral("retrieval"), 0);
    s.setClassLimit(QStringLiteral("verification"), -4);
    const QString blockedRetrieval = s.enqueue(QStringLiteral("retrieval"), {}, 100);
    const QString blockedVerification = s.enqueue(QStringLiteral("verification"), {}, 99);
    const QString allowed = s.enqueue(QStringLiteral("document"), {}, 1);

    QString started;
    QVERIFY(s.startNext(&started));
    QCOMPARE(started, allowed);
    QVERIFY(!s.startNext(&started));
    QCOMPARE(stateFor(s.snapshot(), blockedRetrieval), QStringLiteral("queued"));
    QCOMPARE(stateFor(s.snapshot(), blockedVerification), QStringLiteral("queued"));
}

void AuxiliaryJobSchedulerTests::blocksBusyResource()
{
    AuxiliaryJobScheduler s;
    s.setClassLimit(QStringLiteral("voice"), 2);
    const QString gpuDoc = s.enqueue(QStringLiteral("document"), QStringLiteral("gpu"), 1);
    const QString gpuVoice = s.enqueue(QStringLiteral("voice"), QStringLiteral("gpu"), 10);
    const QString diskVoice = s.enqueue(QStringLiteral("voice"), QStringLiteral("disk"), 5);

    QString started;
    QVERIFY(s.startNext(&started));
    QCOMPARE(started, gpuVoice);
    QVERIFY(s.startNext(&started));
    QCOMPARE(started, diskVoice);
    QCOMPARE(stateFor(s.snapshot(), gpuDoc), QStringLiteral("queued"));
}

void AuxiliaryJobSchedulerTests::cancellingRunningJobReleasesResource()
{
    AuxiliaryJobScheduler s;
    s.setClassLimit(QStringLiteral("retrieval"), 2);
    const QString first = s.enqueue(QStringLiteral("retrieval"), QStringLiteral("cpu"), 2);
    const QString second = s.enqueue(QStringLiteral("verification"), QStringLiteral("cpu"), 1);

    QSignalSpy startedSpy(&s, &AuxiliaryJobScheduler::jobStarted);
    QSignalSpy cancelledSpy(&s, &AuxiliaryJobScheduler::jobCancelled);
    QString started;
    QVERIFY(s.startNext(&started));
    QCOMPARE(started, first);
    QVERIFY(!s.startNext(&started));

    QVERIFY(s.cancel(first, QStringLiteral("preempted")));
    QCOMPARE(cancelledSpy.count(), 1);
    QVERIFY(s.startNext(&started));
    QCOMPARE(started, second);
    QCOMPARE(startedSpy.count(), 2);
    QCOMPARE(stateFor(s.snapshot(), first), QStringLiteral("cancelled"));
    QCOMPARE(stateFor(s.snapshot(), second), QStringLiteral("running"));
}

void AuxiliaryJobSchedulerTests::failedJobIsTerminal()
{
    AuxiliaryJobScheduler s;
    const QString id = s.enqueue(QStringLiteral("verification"));
    QString started;
    QVERIFY(s.startNext(&started));
    QCOMPARE(started, id);

    QSignalSpy finishedSpy(&s, &AuxiliaryJobScheduler::jobFinished);
    QVERIFY(s.complete(id, false, QStringLiteral("model unavailable")));
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(stateFor(s.snapshot(), id), QStringLiteral("failed"));
    QVERIFY(!s.complete(id));
    QVERIFY(!s.cancel(id));
    QVERIFY(!s.startNext());
}

void AuxiliaryJobSchedulerTests::cancelQueuedJob()
{
    AuxiliaryJobScheduler s;
    const QString id = s.enqueue(QStringLiteral("verification"));
    QVERIFY(s.cancel(id, QStringLiteral("user")));
    QCOMPARE(stateFor(s.snapshot(), id), QStringLiteral("cancelled"));
    QVERIFY(!s.startNext());
}

QTEST_MAIN(AuxiliaryJobSchedulerTests)
#include "test_auxiliary_job_scheduler.moc"
