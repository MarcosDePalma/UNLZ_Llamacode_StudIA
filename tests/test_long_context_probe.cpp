#include <QtTest>

#include <QSet>

#include "long_context_probe.h"
#include "core/tuner/TunerEngine.h"

using namespace long_context_probe;

class LongContextProbeTests : public QObject
{
    Q_OBJECT
private slots:
    void standardDepths_areSevenAndOrdered();
    void buildCases_isDeterministic();
    void buildCases_rejectsInvalidInputs();
    void buildCases_hasUniqueSingleOccurrencePasskeys();
    void buildCases_placesNeedleMonotonically();
    void buildCases_reachesRequestedApproximateContext();
    void buildCases_supportsOneMillionTokens();
    void retrievalAcceptance_isCaseInsensitiveButNotEmpty();
};

void LongContextProbeTests::standardDepths_areSevenAndOrdered()
{
    const QVector<double> depths = standardDepths();
    QCOMPARE(depths.size(), 7);
    QCOMPARE(depths.first(), 0.05);
    QCOMPARE(depths.last(), 0.95);
    for (int i = 1; i < depths.size(); ++i)
        QVERIFY(depths.at(i) > depths.at(i - 1));
}

void LongContextProbeTests::buildCases_isDeterministic()
{
    const QVector<double> depths{0.05, 0.50, 0.95};
    const auto first = buildCases(2048, depths, QStringLiteral("agent/0"));
    const auto second = buildCases(2048, depths, QStringLiteral("agent/0"));
    QCOMPARE(first.size(), 3);
    QCOMPARE(second.size(), first.size());
    for (int i = 0; i < first.size(); ++i) {
        QCOMPARE(first.at(i).id, second.at(i).id);
        QCOMPARE(first.at(i).passkey, second.at(i).passkey);
        QCOMPARE(first.at(i).prompt, second.at(i).prompt);
    }
}

void LongContextProbeTests::buildCases_rejectsInvalidInputs()
{
    QVERIFY(buildCases(0, standardDepths()).isEmpty());
    QVERIFY(buildCases(-1, standardDepths()).isEmpty());

    const QVector<double> depths{-0.1, 0.25, 1.01, qQNaN(), 0.75};
    const auto cases = buildCases(1024, depths);
    QCOMPARE(cases.size(), 2);
    QCOMPARE(cases.at(0).depth, 0.25);
    QCOMPARE(cases.at(1).depth, 0.75);
}

void LongContextProbeTests::buildCases_hasUniqueSingleOccurrencePasskeys()
{
    const auto cases = buildCases(4096, standardDepths(), QStringLiteral("user-7"));
    QCOMPARE(cases.size(), 7);
    QSet<QString> keys;
    for (const RetrievalCase &fixture : cases) {
        QVERIFY(!fixture.passkey.isEmpty());
        QVERIFY(!keys.contains(fixture.passkey));
        keys.insert(fixture.passkey);
        QCOMPARE(passkeyOccurrences(fixture), 1);
        QVERIFY(!fixture.prompt.endsWith(fixture.passkey));
        QVERIFY(fixture.prompt.contains(QStringLiteral("FINAL QUESTION:")));
    }
}

void LongContextProbeTests::buildCases_placesNeedleMonotonically()
{
    const auto cases = buildCases(4096, standardDepths());
    int previous = -1;
    for (const RetrievalCase &fixture : cases) {
        const int offset = fixture.prompt.indexOf(fixture.passkey);
        QVERIFY(offset >= 0);
        QVERIFY(offset > previous);
        previous = offset;
    }
}

void LongContextProbeTests::buildCases_reachesRequestedApproximateContext()
{
    const int target = 8192;
    const auto cases = buildCases(target, {0.05, 0.50, 0.95});
    QCOMPARE(cases.size(), 3);
    for (const RetrievalCase &fixture : cases) {
        const int approx = approximatePromptTokens(fixture.prompt);
        // Header/record/footer rounding is bounded; this must still be a real
        // long prompt rather than a short instruction repeated in a loop.
        QVERIFY2(approx >= target - 8, qPrintable(QString::number(approx)));
        QVERIFY(approx <= target + 32);
    }
}

void LongContextProbeTests::buildCases_supportsOneMillionTokens()
{
    const auto cases = buildCases(1048576, {0.50});
    QCOMPARE(cases.size(), 1);
    const int approx = approximatePromptTokens(cases.first().prompt);
    QVERIFY(approx >= 1048576 - 8);
    QVERIFY(approx <= 1048576 + 32);
}

void LongContextProbeTests::retrievalAcceptance_isCaseInsensitiveButNotEmpty()
{
    const auto fixture = buildCases(1024, {0.5}).first();
    QCOMPARE(TunerEngine::scoreQuality(fixture.passkey.toLower(), {fixture.passkey}), 1.0);
    QCOMPARE(TunerEngine::scoreQuality(QStringLiteral("not-found"), {fixture.passkey}), 0.0);
    QCOMPARE(TunerEngine::scoreQuality(QStringLiteral("anything"), {}), 1.0);
}

QTEST_GUILESS_MAIN(LongContextProbeTests)
#include "test_long_context_probe.moc"
