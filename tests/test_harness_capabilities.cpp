#include <QtTest>

#include "core/agent/HarnessCapabilitySnapshot.h"

class HarnessCapabilityTests final : public QObject {
    Q_OBJECT

private slots:
    void admissionIsFailClosed();
    void snapshotRoundTripPreservesGeneration();
};

void HarnessCapabilityTests::admissionIsFailClosed()
{
    const HarnessCapabilitySnapshot snapshot = HarnessCapabilitySnapshot::admit(
        QStringLiteral("activation-1"), QStringLiteral("next"), QStringLiteral("profile"),
        7, {QStringLiteral("fs.read"), QStringLiteral("mail.send")},
        {QStringLiteral("fs.read")});
    QVERIFY(snapshot.canUse(QStringLiteral("fs.read")));
    QVERIFY(snapshot.handleFor(QStringLiteral("mail.send")).isEmpty());
    QVERIFY(!snapshot.canUse(QStringLiteral("mail.send")));
    QCOMPARE(snapshot.grantedNames(), QStringList{QStringLiteral("fs.read")});
    QCOMPARE(snapshot.grants.value(QStringLiteral("mail.send")).reason,
             QStringLiteral("denied_by_policy"));
}

void HarnessCapabilityTests::snapshotRoundTripPreservesGeneration()
{
    const HarnessCapabilitySnapshot original = HarnessCapabilitySnapshot::admit(
        QStringLiteral("a"), QStringLiteral("next"), QStringLiteral("p"), 3,
        {QStringLiteral("one")}, {QStringLiteral("one")});
    const HarnessCapabilitySnapshot restored =
        HarnessCapabilitySnapshot::fromJson(original.toJson());
    QCOMPARE(restored.activationId, QStringLiteral("a"));
    QCOMPARE(restored.engineId, QStringLiteral("next"));
    QCOMPARE(restored.generation, 3);
    QVERIFY(restored.canUse(QStringLiteral("one")));
}

QTEST_MAIN(HarnessCapabilityTests)
#include "test_harness_capabilities.moc"
