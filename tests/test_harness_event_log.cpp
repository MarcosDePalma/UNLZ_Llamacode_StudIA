#include <QtTest>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>

#include "core/agent/HarnessEventLog.h"

class HarnessEventLogTests final : public QObject {
    Q_OBJECT

private slots:
    void appendsAndReloadsInOrder();
    void rejectsBrokenSequence();
};

void HarnessEventLogTests::appendsAndReloadsInOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("events/session.jsonl"));

    HarnessEventLog log;
    QVERIFY(log.open(path));
    QVERIFY(log.append(QStringLiteral("turn/start"), QJsonObject{{"text", "hola"}}));
    QVERIFY(log.append(QStringLiteral("tool/result"), QJsonObject{{"ok", true}}));
    QCOMPARE(log.events().size(), 2);
    QCOMPARE(log.events().at(0).toObject().value(QStringLiteral("seq")).toInt(), 1);
    QCOMPARE(log.events().at(1).toObject().value(QStringLiteral("seq")).toInt(), 2);

    HarnessEventLog reloaded;
    QVERIFY(reloaded.open(path));
    QCOMPARE(reloaded.events().size(), 2);
    QCOMPARE(reloaded.nextSequence(), qint64(3));
}

void HarnessEventLogTests::rejectsBrokenSequence()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("events/broken.jsonl"));
    QFile file(path);
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("{\"seq\":2,\"kind\":\"turn/start\"}\n");
    file.close();

    HarnessEventLog log;
    QString error;
    QVERIFY(!log.open(path, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(HarnessEventLogTests)
#include "test_harness_event_log.moc"
