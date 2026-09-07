#include <QtTest>

#include "core/profiles/HarnessEngine.h"

class HarnessEngineTests final : public QObject {
    Q_OBJECT

private slots:
    void legacyDefaultsAreStable();
    void nextIsIsolatedAndDiscoverable();
    void fingerprintChangesWithRuntimeContract();
};

void HarnessEngineTests::legacyDefaultsAreStable()
{
    const HarnessSpec empty;
    QVERIFY(empty.isEmpty());
    QCOMPARE(HarnessEngine::effectiveId(empty.runtime), QStringLiteral("legacy"));
    QCOMPARE(HarnessEngine::effectiveVersion(empty.runtime), 1);
    QCOMPARE(HarnessEngine::storageNamespace(QStringLiteral("legacy")),
             QStringLiteral("agent_llamaagent"));
    QVERIFY(HarnessEngine::isKnown(QStringLiteral("legacy")));
}

void HarnessEngineTests::nextIsIsolatedAndDiscoverable()
{
    HarnessRuntimeModule runtime;
    runtime.set = true;
    runtime.engine = QStringLiteral("next");
    runtime.version = 2;
    QCOMPARE(HarnessEngine::effectiveId(runtime), QStringLiteral("next"));
    QCOMPARE(HarnessEngine::effectiveVersion(runtime), 2);
    QCOMPARE(HarnessEngine::storageNamespace(QStringLiteral("next")),
             QStringLiteral("agent_harness_next"));

    const QVariantList catalog = HarnessEngine::catalog();
    QCOMPARE(catalog.size(), 2);
    QCOMPARE(catalog.at(1).toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("next"));
    QVERIFY(catalog.at(1).toMap().value(QStringLiteral("experimental")).toBool());
}

void HarnessEngineTests::fingerprintChangesWithRuntimeContract()
{
    HarnessSpec legacy;
    HarnessSpec next = legacy;
    next.runtime.set = true;
    next.runtime.engine = QStringLiteral("next");
    next.runtime.version = 2;
    QVERIFY(HarnessEngine::fingerprint(legacy).startsWith(QStringLiteral("sha256:")));
    QVERIFY(HarnessEngine::fingerprint(legacy) != HarnessEngine::fingerprint(next));

    const HarnessSpec roundTrip = HarnessSpec::fromJson(next.toJson());
    QCOMPARE(roundTrip.runtime.engine, QStringLiteral("next"));
    QCOMPARE(roundTrip.runtime.version, 2);
}

QTEST_MAIN(HarnessEngineTests)
#include "test_harness_engine.moc"
