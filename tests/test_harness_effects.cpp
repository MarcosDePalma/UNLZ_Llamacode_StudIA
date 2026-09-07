#include <QtTest>

#include <QTemporaryDir>

#include "core/agent/HarnessEffectLedger.h"

class HarnessEffectTests final : public QObject {
    Q_OBJECT

private slots:
    void effectLifecycleIsAppendOnly();
    void duplicatePreparationIsRejected();
};

void HarnessEffectTests::effectLifecycleIsAppendOnly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HarnessEffectLedger ledger;
    QVERIFY(ledger.open(dir.filePath(QStringLiteral("effects.jsonl"))));
    HarnessEffectRecord effect;
    effect.effectId = QStringLiteral("e-1");
    effect.kind = QStringLiteral("mail.send");
    effect.payloadHash = QStringLiteral("sha256:p");
    QVERIFY(ledger.prepare(effect));
    QVERIFY(ledger.transition(QStringLiteral("e-1"), QStringLiteral("dispatching")));
    QVERIFY(ledger.transition(QStringLiteral("e-1"), QStringLiteral("uncertain"),
                             QStringLiteral("worker disconnected")));
    QCOMPARE(ledger.record(QStringLiteral("e-1")).state, QStringLiteral("uncertain"));
    QCOMPARE(ledger.records().size(), 3);

    HarnessEffectLedger reloaded;
    QVERIFY(reloaded.open(dir.filePath(QStringLiteral("effects.jsonl"))));
    QCOMPARE(reloaded.record(QStringLiteral("e-1")).detail,
             QStringLiteral("worker disconnected"));
}

void HarnessEffectTests::duplicatePreparationIsRejected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    HarnessEffectLedger ledger;
    QVERIFY(ledger.open(dir.filePath(QStringLiteral("effects.jsonl"))));
    HarnessEffectRecord effect;
    effect.effectId = QStringLiteral("same");
    QVERIFY(ledger.prepare(effect));
    QVERIFY(!ledger.prepare(effect));
}

QTEST_MAIN(HarnessEffectTests)
#include "test_harness_effects.moc"
