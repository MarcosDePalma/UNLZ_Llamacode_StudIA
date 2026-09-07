#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "core/tasks/EvidenceBundle.h"

class EvidenceBundleTests : public QObject
{
    Q_OBJECT
private slots:
    void build_isVersionedAndHashesEachRun();
    void write_createsReadableJson();
};

void EvidenceBundleTests::build_isVersionedAndHashesEachRun()
{
    QVariantMap runInput;
    runInput.insert(QStringLiteral("runId"), QStringLiteral("abc"));
    runInput.insert(QStringLiteral("status"), QStringLiteral("ok"));
    const QVariantList runs{runInput};
    const QJsonObject bundle = EvidenceBundle::build(QStringLiteral("task-1"), runs,
                                                      QStringLiteral("1.0"));
    QCOMPARE(bundle.value(QStringLiteral("schema")).toString(),
             QStringLiteral("llamacode.evidence.v1"));
    QCOMPARE(bundle.value(QStringLiteral("ownerId")).toString(), QStringLiteral("task-1"));
    const QJsonObject run = bundle.value(QStringLiteral("runs")).toArray().first().toObject();
    QCOMPARE(run.value(QStringLiteral("runId")).toString(), QStringLiteral("abc"));
    QCOMPARE(run.value(QStringLiteral("evidenceSha256")).toString().size(), 64);
}

void EvidenceBundleTests::write_createsReadableJson()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("evidence.json"));
    QString error;
    QVERIFY(EvidenceBundle::write(path, EvidenceBundle::build(QStringLiteral("x"), {}, "1"), &error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!QJsonDocument::fromJson(file.readAll()).isNull());
    QVERIFY(error.isEmpty());
}

QTEST_MAIN(EvidenceBundleTests)
#include "test_evidence_bundle.moc"
