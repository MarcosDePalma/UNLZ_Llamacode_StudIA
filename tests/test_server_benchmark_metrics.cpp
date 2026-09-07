#include <QtTest>

#include "core/ServerBenchmarkMetrics.h"

class ServerBenchmarkMetricsTests : public QObject
{
    Q_OBJECT
private slots:
    void percentile_interpolates();
    void meanAndDeviation_handleEmptyAndSampledData();
    void summarizeSamples_keepsFailuresAndHonestTails();
    void summarizeSamples_acceptsAliasesAndRejectsInvalidValues();
    void summarizeSamples_exposesTailOnlyWithEnoughObservations();
    void summarizeSamples_convertsItlToRate();
    void summarizePaired_rejectsInsufficientData();
    void summarizePaired_ignoresInvalidPairsAndSupportsCustomKeys();
    void summarizePaired_detectsStableWinner();
    void summarizePaired_acceptsNullControl();
};

void ServerBenchmarkMetricsTests::percentile_interpolates()
{
    QCOMPARE(ServerBenchmarkMetrics::percentile({1.0, 2.0, 3.0, 4.0}, 0.5), 2.5);
    QCOMPARE(ServerBenchmarkMetrics::percentile({1.0, 2.0, 3.0, 4.0}, 0.0), 1.0);
    QCOMPARE(ServerBenchmarkMetrics::percentile({1.0, 2.0, 3.0, 4.0}, 1.0), 4.0);
}

void ServerBenchmarkMetricsTests::meanAndDeviation_handleEmptyAndSampledData()
{
    QCOMPARE(ServerBenchmarkMetrics::mean({}), -1.0);
    QCOMPARE(ServerBenchmarkMetrics::standardDeviation({}), 0.0);
    QCOMPARE(ServerBenchmarkMetrics::mean({2.0, 4.0, 6.0}), 4.0);
    QCOMPARE(ServerBenchmarkMetrics::standardDeviation({2.0, 4.0, 6.0}), 2.0);
}

void ServerBenchmarkMetricsTests::summarizeSamples_keepsFailuresAndHonestTails()
{
    QVariantList rows;
    for (int i = 0; i < 4; ++i) {
        rows.append(QVariantMap{{QStringLiteral("decodeTps"), 10.0 + i},
                                {QStringLiteral("ttftMs"), 100.0 + i},
                                {QStringLiteral("elapsedMs"), 500.0 + i}});
    }
    rows.append(QVariantMap{{QStringLiteral("failed"), true},
                            {QStringLiteral("decodeTps"), 999.0}});

    const QVariantMap summary = ServerBenchmarkMetrics::summarizeSamples(rows);
    QCOMPARE(summary.value(QStringLiteral("sampleCount")).toInt(), 5);
    QCOMPARE(summary.value(QStringLiteral("validSamples")).toInt(), 4);
    QCOMPARE(summary.value(QStringLiteral("failedSamples")).toInt(), 1);
    QCOMPARE(summary.value(QStringLiteral("decodeTpsP50")).toDouble(), 11.5);
    QCOMPARE(summary.value(QStringLiteral("decodeTpsP95")).toDouble(), -1.0);
    QVERIFY(!summary.value(QStringLiteral("tailPercentilesSupported")).toBool());
}

void ServerBenchmarkMetricsTests::summarizeSamples_acceptsAliasesAndRejectsInvalidValues()
{
    const QVariantList rows{
        QVariantMap{{QStringLiteral("tps"), 20.0},
                    {QStringLiteral("ttft_ms"), 100.0},
                    {QStringLiteral("prompt_tps"), 500.0}},
        QVariantMap{{QStringLiteral("decodeTps"), -1.0},
                    {QStringLiteral("ttftMs"), 0.0}},
        QVariantMap{{QStringLiteral("decodeTps"), 999.0},
                    {QStringLiteral("failed"), true}},
        QVariantMap{{QStringLiteral("decodeTps"), 30.0},
                    {QStringLiteral("ttftMs"), 200.0}}
    };
    const QVariantMap summary = ServerBenchmarkMetrics::summarizeSamples(rows);
    QCOMPARE(summary.value(QStringLiteral("sampleCount")).toInt(), 4);
    QCOMPARE(summary.value(QStringLiteral("validSamples")).toInt(), 2);
    QCOMPARE(summary.value(QStringLiteral("failedSamples")).toInt(), 1);
    QCOMPARE(summary.value(QStringLiteral("decodeTpsP50")).toDouble(), 25.0);
    QCOMPARE(summary.value(QStringLiteral("ttftMsP50")).toDouble(), 150.0);
    QCOMPARE(summary.value(QStringLiteral("promptTpsP50")).toDouble(), 500.0);
}

void ServerBenchmarkMetricsTests::summarizeSamples_exposesTailOnlyWithEnoughObservations()
{
    QVariantList rows;
    for (int i = 0; i < 100; ++i) {
        rows.append(QVariantMap{{QStringLiteral("decodeTps"), 100.0 + i},
                                {QStringLiteral("ttftMs"), 10.0 + i}});
    }
    const QVariantMap summary = ServerBenchmarkMetrics::summarizeSamples(rows);
    QVERIFY(!summary.value(QStringLiteral("tailPercentilesSupported")).toBool());
    QVERIFY(summary.value(QStringLiteral("decodeTpsP95")).toDouble() > 0.0);
    QCOMPARE(summary.value(QStringLiteral("decodeTpsP99")).toDouble(), -1.0);
}

void ServerBenchmarkMetricsTests::summarizeSamples_convertsItlToRate()
{
    const QVariantList rows = {
        QVariantMap{{QStringLiteral("decodeTps"), 20.0},
                    {QStringLiteral("itlMs"), QVariantList{50.0, 100.0}}},
        QVariantMap{{QStringLiteral("decodeTps"), 20.0},
                    {QStringLiteral("itlMs"), QVariantList{25.0}}}
    };
    const QVariantMap summary = ServerBenchmarkMetrics::summarizeSamples(rows);
    QCOMPARE(summary.value(QStringLiteral("itlMsCount")).toInt(), 3);
    QCOMPARE(summary.value(QStringLiteral("itlMsRateP50")).toDouble(), 20.0);
    QCOMPARE(summary.value(QStringLiteral("itlMsRateP01")).toDouble(), -1.0);
}

void ServerBenchmarkMetricsTests::summarizePaired_rejectsInsufficientData()
{
    const QVariantList pairs = {QVariantMap{{QStringLiteral("aTps"), 100.0},
                                             {QStringLiteral("bTps"), 110.0}}};
    const QVariantMap summary = ServerBenchmarkMetrics::summarizePaired(pairs);
    QCOMPARE(summary.value(QStringLiteral("pairCount")).toInt(), 1);
    QVERIFY(!summary.value(QStringLiteral("significant")).toBool());
    QCOMPARE(summary.value(QStringLiteral("winner")).toString(), QStringLiteral("insufficient-data"));
}

void ServerBenchmarkMetricsTests::summarizePaired_ignoresInvalidPairsAndSupportsCustomKeys()
{
    const QVariantList pairs{
        QVariantMap{{QStringLiteral("left"), 100.0}, {QStringLiteral("right"), 110.0}},
        QVariantMap{{QStringLiteral("left"), 0.0}, {QStringLiteral("right"), 200.0}},
        QVariantMap{{QStringLiteral("left"), -1.0}, {QStringLiteral("right"), 200.0}},
        QVariantMap{{QStringLiteral("left"), 100.0}, {QStringLiteral("right"), 90.0}}
    };
    const QVariantMap summary = ServerBenchmarkMetrics::summarizePaired(
        pairs, QStringLiteral("left"), QStringLiteral("right"));
    QCOMPARE(summary.value(QStringLiteral("pairCount")).toInt(), 2);
    QCOMPARE(summary.value(QStringLiteral("deltaPctMedian")).toDouble(), 0.0);
    QCOMPARE(summary.value(QStringLiteral("winner")).toString(), QStringLiteral("within-noise"));
}

void ServerBenchmarkMetricsTests::summarizePaired_detectsStableWinner()
{
    QVariantList pairs;
    for (int i = 0; i < 10; ++i)
        pairs.append(QVariantMap{{QStringLiteral("aTps"), 100.0},
                                 {QStringLiteral("bTps"), 110.0}});
    const QVariantMap summary = ServerBenchmarkMetrics::summarizePaired(pairs);
    QVERIFY(summary.value(QStringLiteral("significant")).toBool());
    QCOMPARE(summary.value(QStringLiteral("winner")).toString(), QStringLiteral("B"));
    QCOMPARE(summary.value(QStringLiteral("deltaPctMedian")).toDouble(), 10.0);
}

void ServerBenchmarkMetricsTests::summarizePaired_acceptsNullControl()
{
    QVariantList pairs;
    for (int i = 0; i < 10; ++i)
        pairs.append(QVariantMap{{QStringLiteral("aTps"), 100.0 + i},
                                 {QStringLiteral("bTps"), 100.0 + i}});
    const QVariantMap summary = ServerBenchmarkMetrics::summarizePaired(pairs);
    QVERIFY(!summary.value(QStringLiteral("significant")).toBool());
    QCOMPARE(summary.value(QStringLiteral("winner")).toString(), QStringLiteral("within-noise"));
}

QTEST_MAIN(ServerBenchmarkMetricsTests)
#include "test_server_benchmark_metrics.moc"
