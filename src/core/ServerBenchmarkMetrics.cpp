#include "ServerBenchmarkMetrics.h"

#include <QStringList>
#include <QtMath>

#include <algorithm>
#include <utility>

namespace {

bool usable(double value)
{
    return qIsFinite(value) && value >= 0.0;
}

QVector<double> valuesFor(const QVariantList &samples, const QStringList &keys)
{
    QVector<double> out;
    out.reserve(samples.size());
    for (const QVariant &value : samples) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("failed")).toBool()) continue;
        double found = -1.0;
        for (const QString &key : keys) {
            const double candidate = row.value(key).toDouble();
            if (usable(candidate) && candidate > 0.0) {
                found = candidate;
                break;
            }
        }
        if (found > 0.0) out.append(found);
    }
    return out;
}

QVector<double> nestedValuesFor(const QVariantList &samples, const QString &key)
{
    QVector<double> out;
    for (const QVariant &value : samples) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("failed")).toBool()) continue;
        for (const QVariant &nested : row.value(key).toList()) {
            const double number = nested.toDouble();
            if (usable(number) && number > 0.0) out.append(number);
        }
    }
    return out;
}

} // namespace

double ServerBenchmarkMetrics::percentile(QVector<double> values, double p)
{
    if (values.isEmpty()) return -1.0;
    std::sort(values.begin(), values.end());
    const double bounded = qBound(0.0, p, 1.0);
    if (values.size() == 1) return values.first();
    const double position = bounded * (values.size() - 1);
    const qsizetype lower = static_cast<qsizetype>(qFloor(position));
    const qsizetype upper = static_cast<qsizetype>(qCeil(position));
    if (lower == upper) return values.at(lower);
    const double fraction = position - static_cast<double>(lower);
    return values.at(lower) + fraction * (values.at(upper) - values.at(lower));
}

double ServerBenchmarkMetrics::mean(const QVector<double> &values)
{
    if (values.isEmpty()) return -1.0;
    double total = 0.0;
    for (const double value : values) total += value;
    return total / values.size();
}

double ServerBenchmarkMetrics::standardDeviation(const QVector<double> &values)
{
    if (values.size() < 2) return 0.0;
    const double average = mean(values);
    double sum = 0.0;
    for (const double value : values) {
        const double delta = value - average;
        sum += delta * delta;
    }
    return qSqrt(sum / (values.size() - 1));
}

double ServerBenchmarkMetrics::supportedPercentile(QVector<double> values, double p)
{
    if (values.isEmpty()) return -1.0;
    // BetterBench's honesty rule: a tail is useful only when it contains at
    // least five observations.  p50 is always supported; p95 needs 100
    // samples and p99 needs 500.  Token-level ITL usually reaches this while
    // per-request tails intentionally remain hidden for short runs.
    const double tail = qMin(p, 1.0 - p);
    if (tail > 0.0 && values.size() * tail < 5.0) return -1.0;
    return percentile(std::move(values), p);
}

void ServerBenchmarkMetrics::addDistribution(QVariantMap *out, const QString &prefix,
                                              const QVector<double> &values,
                                              bool invertForRate)
{
    if (!out) return;
    (*out)[prefix + QStringLiteral("Count")] = values.size();
    (*out)[prefix + QStringLiteral("Mean")] = mean(values);
    (*out)[prefix + QStringLiteral("P50")] = percentile(values, 0.50);
    (*out)[prefix + QStringLiteral("Iqr")] =
        values.isEmpty() ? -1.0 : percentile(values, 0.75) - percentile(values, 0.25);
    (*out)[prefix + QStringLiteral("P05")] = supportedPercentile(values, 0.05);
    (*out)[prefix + QStringLiteral("P95")] = supportedPercentile(values, 0.95);
    (*out)[prefix + QStringLiteral("P01")] = supportedPercentile(values, 0.01);
    (*out)[prefix + QStringLiteral("P99")] = supportedPercentile(values, 0.99);

    if (!invertForRate || values.isEmpty()) return;
    QVector<double> rates;
    rates.reserve(values.size());
    for (const double value : values)
        if (value > 0.0) rates.append(1000.0 / value);
    (*out)[prefix + QStringLiteral("RateP01")] = supportedPercentile(rates, 0.01);
    (*out)[prefix + QStringLiteral("RateP50")] = percentile(rates, 0.50);
    (*out)[prefix + QStringLiteral("RateP99")] = supportedPercentile(rates, 0.99);
}

QVariantMap ServerBenchmarkMetrics::summarizeSamples(const QVariantList &samples)
{
    QVariantMap out;
    int failed = 0;
    for (const QVariant &value : samples)
        failed += value.toMap().value(QStringLiteral("failed")).toBool() ? 1 : 0;

    const QVector<double> ttft = valuesFor(samples, {QStringLiteral("ttftMs"),
                                                      QStringLiteral("ttft_ms")});
    const QVector<double> decode = valuesFor(samples, {QStringLiteral("decodeTps"),
                                                        QStringLiteral("tps")});
    const QVector<double> prefill = valuesFor(samples, {QStringLiteral("promptTps"),
                                                         QStringLiteral("prompt_tps")});
    const QVector<double> elapsed = valuesFor(samples, {QStringLiteral("elapsedMs"),
                                                         QStringLiteral("elapsed_ms")});
    const QVector<double> itl = nestedValuesFor(samples, QStringLiteral("itlMs"));

    out[QStringLiteral("sampleCount")] = samples.size();
    out[QStringLiteral("validSamples")] = decode.size();
    out[QStringLiteral("failedSamples")] = failed;
    out[QStringLiteral("tailPercentilesSupported")] = decode.size() >= 500;
    addDistribution(&out, QStringLiteral("ttftMs"), ttft);
    addDistribution(&out, QStringLiteral("decodeTps"), decode);
    addDistribution(&out, QStringLiteral("promptTps"), prefill);
    addDistribution(&out, QStringLiteral("elapsedMs"), elapsed);
    addDistribution(&out, QStringLiteral("itlMs"), itl, true);
    return out;
}

QVariantMap ServerBenchmarkMetrics::summarizePaired(const QVariantList &pairs,
                                                     const QString &aKey,
                                                     const QString &bKey)
{
    QVector<double> deltas;
    deltas.reserve(pairs.size());
    for (const QVariant &value : pairs) {
        const QVariantMap row = value.toMap();
        const double a = row.value(aKey).toDouble();
        const double b = row.value(bKey).toDouble();
        if (!usable(a) || !usable(b) || a <= 0.0 || b <= 0.0) continue;
        deltas.append((b - a) * 100.0 / a);
    }

    QVariantMap out;
    out[QStringLiteral("pairCount")] = deltas.size();
    out[QStringLiteral("deltaPctMean")] = mean(deltas);
    out[QStringLiteral("deltaPctMedian")] = percentile(deltas, 0.50);
    out[QStringLiteral("deltaPctIqr")] = deltas.isEmpty()
        ? -1.0 : percentile(deltas, 0.75) - percentile(deltas, 0.25);
    out[QStringLiteral("confidenceMethod")] =
        QStringLiteral("paired-normal-approximation-95");

    if (deltas.size() < 2) {
        out[QStringLiteral("ci95LowPct")] = -1.0;
        out[QStringLiteral("ci95HighPct")] = -1.0;
        out[QStringLiteral("significant")] = false;
        out[QStringLiteral("winner")] = QStringLiteral("insufficient-data");
        return out;
    }

    const double average = mean(deltas);
    const double margin = 1.96 * standardDeviation(deltas) / qSqrt(deltas.size());
    const double low = average - margin;
    const double high = average + margin;
    out[QStringLiteral("ci95LowPct")] = low;
    out[QStringLiteral("ci95HighPct")] = high;
    const bool significant = low > 0.0 || high < 0.0;
    out[QStringLiteral("significant")] = significant;
    out[QStringLiteral("winner")] = !significant ? QStringLiteral("within-noise")
        : average > 0.0 ? QStringLiteral("B") : QStringLiteral("A");
    return out;
}
