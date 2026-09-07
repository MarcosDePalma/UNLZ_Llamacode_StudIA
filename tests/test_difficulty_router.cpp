#include <QObject>
#include <QTest>
#include "core/agent/DifficultyRouter.h"

class TestDifficultyRouter : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void defaultThresholds();
    void lowDifficulty();
    void mediumDifficulty_data();
    void mediumDifficulty();
    void highDifficulty_data();
    void highDifficulty();
    void customThresholds();
    void levelNames();
    void assessDetailedReasons();
    void multipleMediumSignalsEscalate();
    void emptyStateIsLow();
};

void TestDifficultyRouter::initTestCase()
{
}

void TestDifficultyRouter::defaultThresholds()
{
    DifficultyRouter router;
    // Defaults conservadores
    QCOMPARE(DifficultyRouter::levelName(DifficultyRouter::Low), QString("baja"));
    QCOMPARE(DifficultyRouter::levelName(DifficultyRouter::Medium), QString("media"));
    QCOMPARE(DifficultyRouter::levelName(DifficultyRouter::High), QString("alta"));
}

void TestDifficultyRouter::lowDifficulty()
{
    DifficultyRouter router;
    QVariantMap state;
    state["filesAffected"] = 2;
    state["contextTokens"] = 4096;
    state["repeatedFailures"] = 0;
    state["agentCycles"] = 1;
    state["confidence"] = 0.9;

    QCOMPARE(router.assess(state), DifficultyRouter::Low);
}

void TestDifficultyRouter::mediumDifficulty_data()
{
    QTest::addColumn<QString>("signal");
    QTest::addColumn<QVariantMap>("state");

    QVariantMap files;
    files["filesAffected"] = 4;  // mitad del umbral 8
    QTest::newRow("files_medium") << QString("files") << files;

    QVariantMap ctx;
    ctx["contextTokens"] = 12000;  // mitad del umbral 24000
    QTest::newRow("context_medium") << QString("context") << ctx;

    QVariantMap fails;
    fails["repeatedFailures"] = 1;
    QTest::newRow("failures_medium") << QString("failures") << fails;

    QVariantMap cycles;
    cycles["agentCycles"] = 3;  // mitad del umbral 5
    QTest::newRow("cycles_medium") << QString("cycles") << cycles;

    QVariantMap conf;
    conf["confidence"] = 0.45;  // entre floor y floor*2
    QTest::newRow("confidence_medium") << QString("confidence") << conf;
}

void TestDifficultyRouter::mediumDifficulty()
{
    QFETCH(QString, signal);
    QFETCH(QVariantMap, state);

    DifficultyRouter router;
    QCOMPARE(router.assess(state), DifficultyRouter::Medium);
    Q_UNUSED(signal)
}

void TestDifficultyRouter::highDifficulty_data()
{
    QTest::addColumn<QString>("signal");
    QTest::addColumn<QVariantMap>("state");

    QVariantMap files;
    files["filesAffected"] = 10;
    QTest::newRow("files_high") << QString("files") << files;

    QVariantMap ctx;
    ctx["contextTokens"] = 32000;
    QTest::newRow("context_high") << QString("context") << ctx;

    QVariantMap fails;
    fails["repeatedFailures"] = 5;
    QTest::newRow("failures_high") << QString("failures") << fails;

    QVariantMap cycles;
    cycles["agentCycles"] = 8;
    QTest::newRow("cycles_high") << QString("cycles") << cycles;

    QVariantMap conf;
    conf["confidence"] = 0.1;
    QTest::newRow("confidence_high") << QString("confidence") << conf;
}

void TestDifficultyRouter::highDifficulty()
{
    QFETCH(QString, signal);
    QFETCH(QVariantMap, state);

    DifficultyRouter router;
    QCOMPARE(router.assess(state), DifficultyRouter::High);
    Q_UNUSED(signal)
}

void TestDifficultyRouter::customThresholds()
{
    DifficultyRouter::Thresholds thresholds;
    thresholds.filesAffected = 2;
    thresholds.contextTokens = 1000;
    thresholds.repeatedFailures = 1;
    thresholds.agentCycles = 2;
    thresholds.confidenceFloor = 0.5;

    DifficultyRouter router(thresholds);

    QVariantMap state;
    state["filesAffected"] = 3;  // supera umbral custom de 2
    QCOMPARE(router.assess(state), DifficultyRouter::High);
}

void TestDifficultyRouter::levelNames()
{
    QCOMPARE(DifficultyRouter::levelName(DifficultyRouter::Low), QString("baja"));
    QCOMPARE(DifficultyRouter::levelName(DifficultyRouter::Medium), QString("media"));
    QCOMPARE(DifficultyRouter::levelName(DifficultyRouter::High), QString("alta"));
}

void TestDifficultyRouter::assessDetailedReasons()
{
    DifficultyRouter router;

    // Estado limpio → sin razones
    auto clean = router.assessDetailed({});
    QCOMPARE(clean.level, DifficultyRouter::Low);
    QVERIFY(clean.reasons.isEmpty());
    QCOMPARE(clean.shouldEscalate(), false);

    // Estado complejo → múltiples razones
    QVariantMap complex;
    complex["filesAffected"] = 15;
    complex["contextTokens"] = 30000;
    complex["repeatedFailures"] = 4;
    complex["confidence"] = 0.15;

    auto result = router.assessDetailed(complex);
    QCOMPARE(result.level, DifficultyRouter::High);
    QVERIFY(result.reasons.size() >= 3);
    QCOMPARE(result.shouldEscalate(), true);
}

void TestDifficultyRouter::multipleMediumSignalsEscalate()
{
    DifficultyRouter router;
    const auto result = router.assessDetailed({
        {QStringLiteral("filesAffected"), 4},
        {QStringLiteral("contextTokens"), 12000}
    });
    QCOMPARE(result.level, DifficultyRouter::High);
    QVERIFY(result.shouldEscalate());
}

void TestDifficultyRouter::emptyStateIsLow()
{
    DifficultyRouter router;
    QCOMPARE(router.assess({}), DifficultyRouter::Low);
}

QTEST_MAIN(TestDifficultyRouter)
#include "test_difficulty_router.moc"
