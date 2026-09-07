#pragma once

#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Pure aggregation helpers for the native server-speed benchmark.  Keeping
// these calculations outside AppController makes the measurement contract
// testable without a running llama-server and prevents the UI from becoming a
// second source of statistical rules.
class ServerBenchmarkMetrics
{
public:
    static double percentile(QVector<double> values, double p);
    static double mean(const QVector<double> &values);
    static double standardDeviation(const QVector<double> &values);

    // Summarizes rows produced by AppController::benchmarkRequest(). Failed
    // rows remain visible in the counts but never enter speed statistics.
    // Percentiles whose tails do not have at least five observations are -1;
    // the median and IQR are always available when there are valid samples.
    static QVariantMap summarizeSamples(const QVariantList &samples);

    // A pair contains aKey/bKey values measured for the same prompt.  Delta is
    // expressed as (B-A)/A * 100.  The interval is a conservative normal
    // approximation over paired deltas; it is intentionally refused for a
    // single pair so the UI cannot call one measurement significant.
    static QVariantMap summarizePaired(const QVariantList &pairs,
                                       const QString &aKey = QStringLiteral("aTps"),
                                       const QString &bKey = QStringLiteral("bTps"));

private:
    static double supportedPercentile(QVector<double> values, double p);
    static void addDistribution(QVariantMap *out, const QString &prefix,
                                const QVector<double> &values,
                                bool invertForRate = false);
};
