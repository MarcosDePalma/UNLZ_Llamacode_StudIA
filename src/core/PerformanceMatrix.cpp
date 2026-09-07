#include "PerformanceMatrix.h"

#include "HardwareDiagnostics.h"

#include <algorithm>

QVariantList PerformanceMatrix::candidates(const QVariantMap &hardware,
                                           const QString &target,
                                           bool withVision)
{
    bool gpuCountOk = false;
    const int parsedGpuCount = hardware.value(QStringLiteral("gpuCount")).toInt(&gpuCountOk);
    const int gpuCount = gpuCountOk ? parsedGpuCount
                                    : hardware.value(QStringLiteral("gpus")).toList().size();
    QStringList modes{QStringLiteral("layer")};
    // Topología ausente = capacidad desconocida: conservar layer como fallback.
    if (gpuCount > 1 && hardware.value(QStringLiteral("p2pAvailable"), false).toBool())
        modes << QStringLiteral("tensor");

    const QStringList kvs = target.compare(QStringLiteral("quality"), Qt::CaseInsensitive) == 0
        ? QStringList{QStringLiteral("f16"), QStringLiteral("q8_0")}
        : QStringList{QStringLiteral("q8_0"), QStringLiteral("f16")};
    const QList<int> contexts = {32768, 65536, 131072};
    QVariantList out;
    for (const QString &mode : modes) {
        for (const QString &kv : kvs) {
            for (const int ctx : contexts) {
                const QString id = QStringLiteral("%1-%2-%3%4")
                    .arg(mode, kv).arg(ctx)
                    .arg(withVision ? QStringLiteral("-mmproj") : QString());
                out.append(QVariantMap{
                    {QStringLiteral("id"), id},
                    {QStringLiteral("splitMode"), mode},
                    {QStringLiteral("kvCache"), kv},
                    {QStringLiteral("ctxSize"), ctx},
                    {QStringLiteral("mmproj"), withVision},
                    {QStringLiteral("target"), target.isEmpty() ? QStringLiteral("balanced") : target},
                    {QStringLiteral("status"), QStringLiteral("pending")},
                });
            }
        }
    }
    return out;
}

QVariantMap PerformanceMatrix::annotate(const QVariantMap &sample,
                                         const QVariantMap &hardware,
                                         const QVariantMap &candidate)
{
    QVariantMap result = sample;
    result[QStringLiteral("performanceCandidate")] = candidate;
    result[QStringLiteral("performanceMatrixId")] = candidate.value(QStringLiteral("id"));
    result[QStringLiteral("hardwareFingerprint")] =
        hardware.value(QStringLiteral("hardwareFingerprint"));
    result[QStringLiteral("performanceScore")] =
        HardwareDiagnostics::performanceScore(sample,
            candidate.value(QStringLiteral("target")).toString());
    const bool stable = sample.contains(QStringLiteral("stable"))
        ? sample.value(QStringLiteral("stable")).toBool()
        : !sample.value(QStringLiteral("failed")).toBool()
          && !sample.value(QStringLiteral("timedOut")).toBool();
    result[QStringLiteral("measurementStatus")] = stable
        ? QStringLiteral("measured") : QStringLiteral("failed");
    return result;
}

QVariantList PerformanceMatrix::rank(const QVariantList &samples, const QString &target)
{
    QVariantList out;
    for (const QVariant &value : samples) {
        QVariantMap row = value.toMap();
        const QVariantMap candidate = row.value(QStringLiteral("performanceCandidate")).toMap();
        const QString objective = target.isEmpty()
            ? candidate.value(QStringLiteral("target")).toString() : target;
        row[QStringLiteral("performanceScore")] = HardwareDiagnostics::performanceScore(row, objective);
        out.append(row);
    }
    std::sort(out.begin(), out.end(), [target](const QVariant &a, const QVariant &b) {
        const QVariantMap am = a.toMap();
        const QVariantMap bm = b.toMap();
        const double as = am.value(QStringLiteral("performanceScore")).toDouble();
        const double bs = bm.value(QStringLiteral("performanceScore")).toDouble();
        return as > bs;
    });
    for (int i = 0; i < out.size(); ++i) {
        QVariantMap row = out.at(i).toMap();
        row[QStringLiteral("rank")] = i + 1;
        out[i] = row;
    }
    return out;
}
