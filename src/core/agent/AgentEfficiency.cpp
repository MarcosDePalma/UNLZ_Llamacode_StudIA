#include "AgentEfficiency.h"

#include <algorithm>
#include <functional>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaType>
#include <QSet>
#include <QVector>
#include <QtMath>

static double firstNumber(const QJsonObject &a, const QJsonObject &b,
                          const QStringList &keys)
{
    for (const QString &key : keys) {
        if (a.value(key).isDouble()) return a.value(key).toDouble();
        if (b.value(key).isDouble()) return b.value(key).toDouble();
    }
    return 0.0;
}

QVariantMap AgentEfficiency::Request::toVariant() const
{
    return {{QStringLiteral("phase"), phase},
            {QStringLiteral("promptTokens"), promptTokens},
            {QStringLiteral("generatedTokens"), generatedTokens},
            {QStringLiteral("promptMs"), promptMs},
            {QStringLiteral("generatedMs"), generatedMs},
            {QStringLiteral("wallMs"), wallMs},
            {QStringLiteral("toolCalls"), toolCalls},
            {QStringLiteral("toolBytes"), toolBytes},
            {QStringLiteral("draftTokens"), draftTokens},
            {QStringLiteral("draftAcceptedTokens"), draftAcceptedTokens},
            {QStringLiteral("draftAcceptancePct"),
             draftTokens > 0 ? 100.0 * draftAcceptedTokens / draftTokens : -1.0}};
}

AgentEfficiency::Request AgentEfficiency::Request::fromResponse(
    const QJsonObject &root, const QString &phaseName, double elapsed)
{
    const QJsonObject timings = root.value(QStringLiteral("timings")).toObject();
    const QJsonObject usage = root.value(QStringLiteral("usage")).toObject();
    Request r;
    r.phase = AgentEfficiency::normalizedPhase(phaseName);
    r.promptTokens = qRound(firstNumber(timings, usage,
        {QStringLiteral("prompt_n"), QStringLiteral("prompt_tokens")}));
    r.generatedTokens = qRound(firstNumber(timings, usage,
        {QStringLiteral("predicted_n"), QStringLiteral("completion_tokens")}));
    r.promptMs = firstNumber(timings, root, {QStringLiteral("prompt_ms")});
    r.generatedMs = firstNumber(timings, root, {QStringLiteral("predicted_ms")});
    r.draftTokens = qMax(0, qRound(firstNumber(
        timings, root, {QStringLiteral("draft_n")})));
    r.draftAcceptedTokens = qMax(0, qRound(firstNumber(
        timings, root, {QStringLiteral("draft_n_accepted")})));
    if (r.draftTokens > 0)
        r.draftAcceptedTokens = qMin(r.draftAcceptedTokens, r.draftTokens);
    r.wallMs = qMax(0.0, elapsed);
    return r;
}

QVariantMap AgentEfficiency::summarize(const QVariantList &requests)
{
    QVariantMap out{{QStringLiteral("requests"), requests.size()}};
    qint64 prompt = 0, generated = 0, toolBytes = 0;
    qint64 draft = 0, draftAccepted = 0;
    int tools = 0;
    double promptMs = 0.0, generatedMs = 0.0, wallMs = 0.0;
    QVariantMap phases;
    for (const QVariant &v : requests) {
        const QVariantMap r = v.toMap();
        prompt += r.value(QStringLiteral("promptTokens")).toLongLong();
        generated += r.value(QStringLiteral("generatedTokens")).toLongLong();
        promptMs += r.value(QStringLiteral("promptMs")).toDouble();
        generatedMs += r.value(QStringLiteral("generatedMs")).toDouble();
        wallMs += r.value(QStringLiteral("wallMs")).toDouble();
        tools += r.value(QStringLiteral("toolCalls")).toInt();
        toolBytes += r.value(QStringLiteral("toolBytes")).toLongLong();
        draft += r.value(QStringLiteral("draftTokens")).toLongLong();
        draftAccepted += r.value(QStringLiteral("draftAcceptedTokens")).toLongLong();
        const QString phase = normalizedPhase(r.value(QStringLiteral("phase")).toString());
        QVariantMap p = phases.value(phase).toMap();
        p[QStringLiteral("requests")] = p.value(QStringLiteral("requests")).toInt() + 1;
        p[QStringLiteral("promptTokens")] = p.value(QStringLiteral("promptTokens")).toLongLong()
                                               + r.value(QStringLiteral("promptTokens")).toLongLong();
        p[QStringLiteral("wallMs")] = p.value(QStringLiteral("wallMs")).toDouble()
                                      + r.value(QStringLiteral("wallMs")).toDouble();
        phases[phase] = p;
    }
    out[QStringLiteral("promptTokens")] = prompt;
    out[QStringLiteral("generatedTokens")] = generated;
    out[QStringLiteral("promptMs")] = promptMs;
    out[QStringLiteral("generatedMs")] = generatedMs;
    out[QStringLiteral("wallMs")] = wallMs;
    out[QStringLiteral("toolCalls")] = tools;
    out[QStringLiteral("toolBytes")] = toolBytes;
    out[QStringLiteral("draftTokens")] = draft;
    out[QStringLiteral("draftAcceptedTokens")] = draftAccepted;
    out[QStringLiteral("draftAcceptancePct")] =
        draft > 0 ? 100.0 * draftAccepted / draft : -1.0;
    out[QStringLiteral("phases")] = phases;
    return out;
}

QVariantMap AgentEfficiency::compare(const QVariantMap &base, const QVariantMap &candidate)
{
    QVariantMap out;
    for (const QString &key : {QStringLiteral("promptTokens"), QStringLiteral("generatedTokens"),
                               QStringLiteral("promptMs"), QStringLiteral("wallMs"),
                               QStringLiteral("toolCalls"), QStringLiteral("toolBytes"),
                               QStringLiteral("draftTokens"),
                               QStringLiteral("draftAcceptedTokens")}) {
        const double a = base.value(key).toDouble();
        const double b = candidate.value(key).toDouble();
        out[key + QStringLiteral("Delta")] = b - a;
        out[key + QStringLiteral("ChangePct")] = a > 0.0 ? ((b - a) * 100.0 / a) : 0.0;
    }
    return out;
}

static double median(QList<double> values)
{
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    const qsizetype middle = values.size() / 2;
    return values.size() % 2
        ? values.at(middle)
        : (values.at(middle - 1) + values.at(middle)) / 2.0;
}

static QString canonicalToolArguments(const QVariant &raw, bool *valid = nullptr)
{
    if (valid) *valid = true;
    if (!raw.isValid() || raw.isNull()) return QStringLiteral("{}");

    QJsonValue value;
    if (raw.typeId() == QMetaType::QString) {
        const QByteArray bytes = raw.toString().trimmed().toUtf8();
        if (bytes.isEmpty()) return QStringLiteral("{}");
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            if (valid) *valid = false;
            return QStringLiteral("<invalid-json>");
        }
        value = doc.object();
    } else {
        value = QJsonValue::fromVariant(raw);
        if (!value.isObject()) {
            if (valid) *valid = false;
            return QStringLiteral("<invalid-object>");
        }
    }

    // QJsonObject preserves insertion order. Re-encode through sorted keys so
    // two providers with a different serialization order compare equally.
    std::function<QJsonValue(const QJsonValue &)> canonical =
        [&](const QJsonValue &item) -> QJsonValue {
            if (item.isArray()) {
                QJsonArray array;
                for (const QJsonValue &entry : item.toArray()) array.append(canonical(entry));
                return array;
            }
            if (!item.isObject()) return item;
            QStringList keys = item.toObject().keys();
            std::sort(keys.begin(), keys.end());
            QJsonObject object;
            for (const QString &key : keys)
                object.insert(key, canonical(item.toObject().value(key)));
            return object;
        };
    return QString::fromUtf8(QJsonDocument(canonical(value).toObject())
                                 .toJson(QJsonDocument::Compact));
}

QVariantList AgentEfficiency::toolCallsFromLifecycle(const QVariantList &events)
{
    QVariantList calls;
    QHash<QString, int> byCallId;
    for (const QVariant &value : events) {
        const QVariantMap event = value.toMap();
        const QString kind = event.value(QStringLiteral("event")).toString();
        if (kind == QLatin1String("tool.request")) {
            QVariantMap call{
                {QStringLiteral("callId"), event.value(QStringLiteral("callId"))},
                {QStringLiteral("tool"), event.value(QStringLiteral("tool"))},
                {QStringLiteral("arguments"), event.value(QStringLiteral("arguments"))},
                {QStringLiteral("completed"), false}};
            const int index = calls.size();
            calls.append(call);
            const QString id = event.value(QStringLiteral("callId")).toString();
            if (!id.isEmpty()) byCallId.insert(id, index);
            continue;
        }
        if (kind != QLatin1String("tool.finish")) continue;

        const QString id = event.value(QStringLiteral("callId")).toString();
        const auto it = byCallId.constFind(id);
        if (it == byCallId.cend()) continue;
        QVariantMap call = calls.at(it.value()).toMap();
        call[QStringLiteral("ok")] = event.value(QStringLiteral("ok")).toBool();
        call[QStringLiteral("completed")] = true;
        call[QStringLiteral("result")] = event.value(QStringLiteral("result"));
        calls[it.value()] = call;
    }
    return calls;
}

QVariantMap AgentEfficiency::evaluateToolCalls(const QVariantList &calls,
                                               const QVariantList &expected)
{
    int successful = 0;
    int failed = 0;
    int incomplete = 0;
    int invalid = 0;
    int redundant = 0;
    QSet<QString> seen;
    QVariantList normalized;

    for (const QVariant &value : calls) {
        const QVariantMap call = value.toMap();
        const QString tool = call.value(QStringLiteral("tool")).toString().trimmed();
        bool argsValid = false;
        const QVariant args = call.contains(QStringLiteral("arguments"))
            ? call.value(QStringLiteral("arguments")) : call.value(QStringLiteral("args"));
        const QString argsJson = canonicalToolArguments(args, &argsValid);
        const bool valid = !tool.isEmpty() && argsValid;
        if (!valid) ++invalid;
        const QString signature = tool + QLatin1Char('\n') + argsJson;
        if (seen.contains(signature)) ++redundant;
        seen.insert(signature);

        const bool completed = call.contains(QStringLiteral("completed"))
            ? call.value(QStringLiteral("completed")).toBool()
            : call.contains(QStringLiteral("ok"));
        if (!completed) {
            ++incomplete;
        } else if (call.value(QStringLiteral("ok")).toBool()) {
            ++successful;
        } else {
            ++failed;
        }
        normalized.append(QVariantMap{{QStringLiteral("tool"), tool},
                                      {QStringLiteral("arguments"), argsJson},
                                      {QStringLiteral("valid"), valid},
                                      {QStringLiteral("completed"), completed},
                                      {QStringLiteral("ok"), completed
                                          && call.value(QStringLiteral("ok")).toBool()}});
    }

    const auto matchesExpected = [](const QVariantMap &actual, const QVariantMap &wanted) {
        if (!actual.value(QStringLiteral("valid")).toBool()) return false;
        if (actual.value(QStringLiteral("tool")).toString().trimmed()
                != wanted.value(QStringLiteral("tool")).toString().trimmed())
            return false;
        if (!wanted.contains(QStringLiteral("arguments"))
            && !wanted.contains(QStringLiteral("args")))
            return true;
        bool wantedValid = false;
        const QVariant wantedArgs = wanted.contains(QStringLiteral("arguments"))
            ? wanted.value(QStringLiteral("arguments")) : wanted.value(QStringLiteral("args"));
        const QString wantedJson = canonicalToolArguments(wantedArgs, &wantedValid);
        return wantedValid
            && actual.value(QStringLiteral("arguments")).toString() == wantedJson;
    };

    int matched = 0;
    int unexpected = 0;
    QVector<bool> expectedMatched(expected.size(), false);
    for (const QVariant &value : normalized) {
        const QVariantMap actual = value.toMap();
        int found = -1;
        for (int i = 0; i < expected.size(); ++i) {
            if (expectedMatched.at(i)) continue;
            const QVariantMap wanted = expected.at(i).toMap();
            if (matchesExpected(actual, wanted)) {
                found = i;
                break;
            }
        }
        if (found >= 0) {
            expectedMatched[found] = true;
            ++matched;
        } else {
            ++unexpected;
        }
    }

    const int missing = expected.size() - matched;
    const int total = calls.size();
    const auto pct = [](int numerator, int denominator) {
        return denominator > 0 ? 100.0 * numerator / denominator : -1.0;
    };
    const double precision = pct(matched, total);
    const double recall = pct(matched, expected.size());
    const double f1 = precision >= 0.0 && recall >= 0.0 && precision + recall > 0.0
        ? 2.0 * precision * recall / (precision + recall) : -1.0;
    const bool hasExpectations = !expected.isEmpty();
    bool sequenceExact = hasExpectations && total == expected.size();
    if (sequenceExact) {
        for (int i = 0; i < total; ++i) {
            const QVariantMap actual = normalized.at(i).toMap();
            const QVariantMap wanted = expected.at(i).toMap();
            if (!actual.value(QStringLiteral("completed")).toBool()
                || !actual.value(QStringLiteral("ok")).toBool()
                || !matchesExpected(actual, wanted)) {
                sequenceExact = false;
                break;
            }
        }
    }

    return {{QStringLiteral("totalCalls"), total},
            {QStringLiteral("successfulCalls"), successful},
            {QStringLiteral("failedCalls"), failed},
            {QStringLiteral("incompleteCalls"), incomplete},
            {QStringLiteral("invalidCalls"), invalid},
            {QStringLiteral("redundantCalls"), redundant},
            {QStringLiteral("successRatePct"), pct(successful, total)},
            {QStringLiteral("expectedCalls"), expected.size()},
            {QStringLiteral("matchedExpectedCalls"), matched},
            {QStringLiteral("missingExpectedCalls"), hasExpectations ? missing : 0},
            {QStringLiteral("unexpectedCalls"), hasExpectations ? unexpected : 0},
            {QStringLiteral("precisionPct"), hasExpectations ? precision : -1.0},
            {QStringLiteral("recallPct"), hasExpectations ? recall : -1.0},
            {QStringLiteral("f1Pct"), hasExpectations ? f1 : -1.0},
            {QStringLiteral("sequenceExact"), sequenceExact}};
}

QVariantMap AgentEfficiency::benchmarkComparison(const QVariantList &runs, const QString &groupBy)
{
    // groupBy: "profileId" (default, compara MODELOS) o "agentProfileId" (compara
    // HARNESS: mismo modelo, distinto spec). Es la misma matemática; lo único que
    // cambia es qué eje se mantiene fijo.
    const QString key = groupBy.trimmed().isEmpty() ? QStringLiteral("profileId")
                                                    : groupBy.trimmed();
    const bool byAgent = key == QLatin1String("agentProfileId");
    QMap<QString, QVariantList> grouped;
    for (const QVariant &value : runs) {
        const QVariantMap run = value.toMap();
        const QString groupId = run.value(key).toString().trimmed();
        if (!groupId.isEmpty())
            grouped[groupId].append(run);
    }

    QVariantList profiles;
    QMap<QString, QVariantMap> aggregateById;
    for (auto it = grouped.cbegin(); it != grouped.cend(); ++it) {
        const QVariantList profileRuns = it.value();
        QList<double> qualityPct;
        QList<double> elapsedSec;
        QList<double> firstAttemptSec;
        QList<double> coldFirstAttemptSec;
        QList<double> warmFirstAttemptSec;
        QList<double> repairAttempts;
        QList<double> filesChanged;
        QList<double> addedLines;
        QList<double> removedLines;
        QList<double> toolCalls;
        QList<double> toolSuccessRatePct;
        QList<double> toolRedundantCalls;
        QList<double> toolF1Pct;
        int successful = 0;
        int fullyAccepted = 0;
        int failed = 0;
        QString profileName;
        QString agentVariant;
        bool honeyEnabled = false;

        for (const QVariant &value : profileRuns) {
            const QVariantMap run = value.toMap();
            if (profileName.isEmpty())
                profileName = run.value(byAgent ? QStringLiteral("agentProfileName")
                                                : QStringLiteral("profileName")).toString()
                                      .section(QStringLiteral(" · pasada "), 0, 0);
            if (agentVariant.isEmpty())
                agentVariant = run.value(QStringLiteral("agentVariant")).toString();
            honeyEnabled = honeyEnabled || run.value(QStringLiteral("honeyEnabled")).toBool();
            const bool runFailed = run.value(QStringLiteral("failed")).toBool();
            if (runFailed) {
                failed++;
                continue;
            }
            const QVariantMap complexity = run.value(QStringLiteral("complexityMetrics")).toMap();
            filesChanged.append(complexity.value(QStringLiteral("filesChanged")).toDouble());
            addedLines.append(complexity.value(QStringLiteral("addedLines")).toDouble());
            removedLines.append(complexity.value(QStringLiteral("removedLines")).toDouble());
            successful++;
            const int score = run.value(QStringLiteral("qualityScore")).toInt();
            const int total = run.value(QStringLiteral("qualityTotal")).toInt();
            if (total > 0) {
                qualityPct.append(100.0 * score / total);
                if (score >= total) fullyAccepted++;
            }
            const double elapsed = run.value(QStringLiteral("elapsedSec")).toDouble();
            if (elapsed > 0.0) elapsedSec.append(elapsed);
            const double first = run.value(QStringLiteral("timeToFirstAttempt")).toDouble();
            if (first > 0.0) {
                firstAttemptSec.append(first);
                const bool warm = run.value(QStringLiteral("measurementPhase")).toString()
                                      == QLatin1String("warm")
                               || run.value(QStringLiteral("pass")).toInt() > 1;
                (warm ? warmFirstAttemptSec : coldFirstAttemptSec).append(first);
            }
            repairAttempts.append(run.value(QStringLiteral("repairAttempts")).toDouble());
            const QVariantMap toolQuality = run.value(QStringLiteral("toolCallQuality")).toMap();
            const auto appendKnown = [](QList<double> &target, const QVariant &value) {
                const double number = value.toDouble();
                if (number >= 0.0) target.append(number);
            };
            appendKnown(toolCalls, toolQuality.value(QStringLiteral("totalCalls")));
            appendKnown(toolSuccessRatePct, toolQuality.value(QStringLiteral("successRatePct")));
            appendKnown(toolRedundantCalls, toolQuality.value(QStringLiteral("redundantCalls")));
            appendKnown(toolF1Pct, toolQuality.value(QStringLiteral("f1Pct")));
        }

        const int totalRuns = profileRuns.size();
        const int majority = qMax(successful, failed);
        const auto minmaxQuality = std::minmax_element(qualityPct.cbegin(), qualityPct.cend());
        QVariantMap aggregate{
            {QStringLiteral("profileId"), it.key()},
            {QStringLiteral("profileName"), profileName},
            {QStringLiteral("agentVariant"), agentVariant.isEmpty()
                ? (honeyEnabled ? QStringLiteral("honey") : QStringLiteral("baseline"))
                : agentVariant},
            {QStringLiteral("honeyEnabled"), honeyEnabled},
            {QStringLiteral("runs"), totalRuns},
            {QStringLiteral("successfulRuns"), successful},
            {QStringLiteral("failedRuns"), failed},
            {QStringLiteral("fullyAcceptedRuns"), fullyAccepted},
            {QStringLiteral("successRatePct"), totalRuns > 0 ? 100.0 * successful / totalRuns : 0.0},
            {QStringLiteral("stabilityRatePct"), totalRuns > 0 ? 100.0 * majority / totalRuns : 0.0},
            {QStringLiteral("outcomeSpread"), successful > 0 && failed > 0},
            {QStringLiteral("medianQualityPct"), median(qualityPct)},
            {QStringLiteral("medianElapsedSec"), median(elapsedSec)},
            {QStringLiteral("medianFirstAttemptSec"), median(firstAttemptSec)},
            {QStringLiteral("medianColdFirstAttemptSec"), median(coldFirstAttemptSec)},
            {QStringLiteral("medianWarmFirstAttemptSec"), median(warmFirstAttemptSec)},
            {QStringLiteral("comparisonTimeMetric"),
             warmFirstAttemptSec.isEmpty() ? QStringLiteral("timeToFirstAttempt")
                                           : QStringLiteral("warmTimeToFirstAttempt")},
            {QStringLiteral("medianRepairAttempts"), median(repairAttempts)},
            {QStringLiteral("medianFilesChanged"), median(filesChanged)},
            {QStringLiteral("medianAddedLines"), median(addedLines)},
            {QStringLiteral("medianRemovedLines"), median(removedLines)}
        };
        aggregate[QStringLiteral("medianToolCalls")] = median(toolCalls);
        aggregate[QStringLiteral("medianToolSuccessRatePct")] = median(toolSuccessRatePct);
        aggregate[QStringLiteral("medianToolRedundantCalls")] = median(toolRedundantCalls);
        aggregate[QStringLiteral("medianToolF1Pct")] = median(toolF1Pct);
        aggregate[QStringLiteral("qualityRangePctPoints")] = qualityPct.isEmpty()
            ? 0.0 : *minmaxQuality.second - *minmaxQuality.first;
        profiles.append(aggregate);
        aggregateById.insert(it.key(), aggregate);
    }

    QVariantList comparisons;
    const QStringList ids = aggregateById.keys();
    for (qsizetype i = 0; i < ids.size(); ++i) {
        for (qsizetype j = i + 1; j < ids.size(); ++j) {
            const QVariantMap baseline = aggregateById.value(ids.at(i));
            const QVariantMap candidate = aggregateById.value(ids.at(j));
            const QString timeKey = baseline.value(QStringLiteral("comparisonTimeMetric")).toString()
                                    == QLatin1String("warmTimeToFirstAttempt")
                && candidate.value(QStringLiteral("comparisonTimeMetric")).toString()
                       == QLatin1String("warmTimeToFirstAttempt")
                ? QStringLiteral("medianWarmFirstAttemptSec")
                : QStringLiteral("medianFirstAttemptSec");
            const double baseTime = baseline.value(timeKey).toDouble();
            const double candidateTime = candidate.value(timeKey).toDouble();
            QVariantMap comparison{
                {QStringLiteral("baselineProfileId"), ids.at(i)},
                {QStringLiteral("candidateProfileId"), ids.at(j)},
                {QStringLiteral("qualityDeltaPctPoints"),
                 candidate.value(QStringLiteral("medianQualityPct")).toDouble()
                     - baseline.value(QStringLiteral("medianQualityPct")).toDouble()},
                {QStringLiteral("successRateDeltaPctPoints"),
                 candidate.value(QStringLiteral("successRatePct")).toDouble()
                     - baseline.value(QStringLiteral("successRatePct")).toDouble()},
                {QStringLiteral("comparisonTimeChangePct"),
                 baseTime > 0.0 ? (candidateTime / baseTime - 1.0) * 100.0 : 0.0},
                {QStringLiteral("filesChangedDelta"),
                 candidate.value(QStringLiteral("medianFilesChanged")).toDouble()
                     - baseline.value(QStringLiteral("medianFilesChanged")).toDouble()},
                {QStringLiteral("addedLinesDelta"),
                 candidate.value(QStringLiteral("medianAddedLines")).toDouble()
                     - baseline.value(QStringLiteral("medianAddedLines")).toDouble()},
                {QStringLiteral("removedLinesDelta"),
                 candidate.value(QStringLiteral("medianRemovedLines")).toDouble()
                     - baseline.value(QStringLiteral("medianRemovedLines")).toDouble()},
                // Backward-compatible alias for consumers of schemaVersion 1.
                {QStringLiteral("elapsedChangePct"),
                  baseTime > 0.0 ? (candidateTime / baseTime - 1.0) * 100.0 : 0.0}
            };
            const auto knownDelta = [](const QVariantMap &base,
                                       const QVariantMap &candidate,
                                       const QString &key) {
                if (!base.contains(key) || !candidate.contains(key)) return -1.0;
                const double a = base.value(key).toDouble();
                const double b = candidate.value(key).toDouble();
                return a >= 0.0 && b >= 0.0 ? b - a : -1.0;
            };
            comparison[QStringLiteral("toolF1DeltaPctPoints")] =
                knownDelta(baseline, candidate, QStringLiteral("medianToolF1Pct"));
            comparison[QStringLiteral("toolSuccessRateDeltaPctPoints")] =
                knownDelta(baseline, candidate, QStringLiteral("medianToolSuccessRatePct"));
            comparison[QStringLiteral("toolRedundantCallsDelta")] =
                knownDelta(baseline, candidate, QStringLiteral("medianToolRedundantCalls"));
            comparison[QStringLiteral("toolCallsDelta")] =
                knownDelta(baseline, candidate, QStringLiteral("medianToolCalls"));
            comparisons.append(comparison);
        }
    }

    return {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("runCount"), runs.size()},
        {QStringLiteral("profileCount"), profiles.size()},
        {QStringLiteral("profiles"), profiles},
        {QStringLiteral("comparisons"), comparisons}
    };
}

QString AgentEfficiency::normalizedPhase(const QString &phase)
{
    const QString p = phase.trimmed().toLower();
    if (p == QLatin1String("explore") || p == QLatin1String("explorar")) return QStringLiteral("explore");
    if (p == QLatin1String("plan") || p == QLatin1String("planificar")) return QStringLiteral("plan");
    if (p == QLatin1String("execute") || p == QLatin1String("ejecutar")) return QStringLiteral("execute");
    if (p == QLatin1String("verify") || p == QLatin1String("verificar")) return QStringLiteral("verify");
    return p.isEmpty() ? QStringLiteral("general") : p;
}
