#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <algorithm>

#include "core/ServerBenchmarkMetrics.h"
#include "core/tuner/TunerEngine.h"

// Opt-in QA against a real llama-server. It compares the same workload at
// different levels of simultaneous requests, which is the useful measurement
// for deciding whether an auxiliary CPU model should share a server or have a
// separate process. It intentionally does not enter ctest: it needs a live
// model and the operator chooses the prompt/context size.

namespace {

struct Sample {
    bool failed = false;
    qint64 elapsedMs = -1;
    ThroughputSample timings;
    QString error;
};

struct Batch {
    int concurrency = 0;
    int requested = 0;
    int completed = 0;
    int failed = 0;
    qint64 wallMs = -1;
    QVector<Sample> samples;
};

bool parsePositiveList(const QString &raw, QVector<int> *out)
{
    if (!out) return false;
    out->clear();
    for (const QString &part : raw.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (!ok || value < 1 || value > 256) return false;
        out->append(value);
    }
    return !out->isEmpty();
}

Batch runBatch(QNetworkAccessManager &manager, const QString &baseUrl,
               int concurrency, int nPredict, const QString &prompt, int timeoutMs)
{
    Batch batch;
    batch.concurrency = concurrency;
    batch.requested = concurrency;
    batch.samples.resize(concurrency);
    if (concurrency <= 0) return batch;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QHash<QNetworkReply *, int> pending;
    QVector<qint64> startedAt(concurrency, -1);
    int remaining = concurrency;
    const qint64 batchStartedAt = QDateTime::currentMSecsSinceEpoch();

    for (int i = 0; i < concurrency; ++i) {
        const QJsonObject payload{
            {QStringLiteral("prompt"), prompt},
            {QStringLiteral("n_predict"), nPredict},
            {QStringLiteral("stream"), false},
            {QStringLiteral("cache_prompt"), false},
        };
        QNetworkRequest request(QUrl(baseUrl + QStringLiteral("/completion")));
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QByteArrayLiteral("application/json"));
        QNetworkReply *reply = manager.post(
            request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        pending.insert(reply, i);
        startedAt[i] = QDateTime::currentMSecsSinceEpoch();

        QObject::connect(reply, &QNetworkReply::finished, &loop, [&, reply]() {
            const auto it = pending.find(reply);
            if (it == pending.end()) return; // timeout already classified it
            const int index = it.value();
            pending.erase(it);
            Sample &sample = batch.samples[index];
            sample.elapsedMs = qMax<qint64>(0,
                QDateTime::currentMSecsSinceEpoch() - startedAt.at(index));
            ++batch.completed;

            if (reply->error() != QNetworkReply::NoError) {
                sample.failed = true;
                sample.error = reply->errorString();
            } else {
                const QByteArray body = reply->readAll();
                sample.timings = TunerEngine::parseThroughput(body);
                if (!sample.timings.valid()) {
                    sample.failed = true;
                    sample.error = QStringLiteral(
                        "respuesta sin timings; iniciá llama-server con --metrics");
                }
            }
            if (sample.failed) ++batch.failed;
            reply->deleteLater();
            if (--remaining == 0) loop.quit();
        });
    }

    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        const auto timedOut = pending;
        pending.clear();
        for (auto it = timedOut.cbegin(); it != timedOut.cend(); ++it) {
            QNetworkReply *reply = it.key();
            const int index = it.value();
            Sample &sample = batch.samples[index];
            sample.failed = true;
            sample.error = QStringLiteral("timeout");
            sample.elapsedMs = qMax<qint64>(0,
                QDateTime::currentMSecsSinceEpoch() - startedAt.at(index));
            ++batch.completed;
            ++batch.failed;
            reply->abort();
            reply->deleteLater();
        }
        remaining = 0;
        loop.quit();
    });

    timeout.start(timeoutMs);
    loop.exec();
    batch.wallMs = qMax<qint64>(0,
        QDateTime::currentMSecsSinceEpoch() - batchStartedAt);
    return batch;
}

QJsonObject sampleToJson(const Sample &sample)
{
    return {
        {QStringLiteral("failed"), sample.failed},
        {QStringLiteral("elapsedMs"), sample.elapsedMs},
        {QStringLiteral("promptTps"), sample.timings.promptTps},
        {QStringLiteral("decodeTps"), sample.timings.genTps},
        {QStringLiteral("error"), sample.error},
    };
}

QJsonObject batchToJson(const Batch &batch)
{
    QVariantList metricsRows;
    QJsonArray samples;
    double aggregatePromptTps = 0.0;
    double aggregateDecodeTps = 0.0;
    int validTimings = 0;
    for (const Sample &sample : batch.samples) {
        samples.append(sampleToJson(sample));
        metricsRows.append(QVariantMap{
            {QStringLiteral("failed"), sample.failed},
            {QStringLiteral("elapsedMs"), sample.elapsedMs},
            {QStringLiteral("promptTps"), sample.timings.promptTps},
            {QStringLiteral("decodeTps"), sample.timings.genTps},
        });
        if (!sample.failed) {
            if (sample.timings.promptTps > 0.0) aggregatePromptTps += sample.timings.promptTps;
            if (sample.timings.genTps > 0.0) aggregateDecodeTps += sample.timings.genTps;
            if (sample.timings.valid()) ++validTimings;
        }
    }

    const QVariantMap summary = ServerBenchmarkMetrics::summarizeSamples(metricsRows);
    return {
        {QStringLiteral("concurrency"), batch.concurrency},
        {QStringLiteral("requested"), batch.requested},
        {QStringLiteral("completed"), batch.completed},
        {QStringLiteral("failed"), batch.failed},
        {QStringLiteral("validTimings"), validTimings},
        {QStringLiteral("wallMs"), batch.wallMs},
        {QStringLiteral("requestsPerSecond"), batch.wallMs > 0
            ? 1000.0 * batch.completed / batch.wallMs : 0.0},
        {QStringLiteral("aggregatePromptTps"), aggregatePromptTps},
        {QStringLiteral("aggregateDecodeTps"), aggregateDecodeTps},
        {QStringLiteral("metrics"), QJsonObject::fromVariantMap(summary)},
        {QStringLiteral("samples"), samples},
    };
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qa_auxiliary_concurrency"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Compara concurrencia y throughput de un llama-server para workloads auxiliares"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("base-url"),
                                  QStringLiteral("URL del llama-server, por ejemplo http://127.0.0.1:8080"));
    parser.addOption(QCommandLineOption(
        QStringLiteral("concurrency"), QStringLiteral("niveles de concurrencia separados por coma"),
        QStringLiteral("levels"), QStringLiteral("1,2,4")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("requests"), QStringLiteral("requests totales por nivel"),
        QStringLiteral("count"), QStringLiteral("8")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("n-predict"), QStringLiteral("tokens a generar por request"),
        QStringLiteral("tokens"), QStringLiteral("32")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("prompt-tokens"), QStringLiteral("largo aproximado del prompt"),
        QStringLiteral("tokens"), QStringLiteral("256")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("timeout-ms"), QStringLiteral("timeout de cada batch"),
        QStringLiteral("milliseconds"), QStringLiteral("120000")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("output"), QStringLiteral("archivo JSON de salida opcional"),
        QStringLiteral("path")));
    if (!parser.parse(QCoreApplication::arguments())) {
        QTextStream(stderr) << parser.errorText() << '\n';
        return 2;
    }
    if (parser.isSet(QStringLiteral("help"))) {
        QTextStream(stdout) << parser.helpText();
        return 0;
    }

    const QStringList positional = parser.positionalArguments();
    if (positional.size() != 1) {
        QTextStream(stderr) << parser.helpText();
        return 2;
    }
    QString baseUrl = positional.first().trimmed();
    while (baseUrl.endsWith(QLatin1Char('/'))) baseUrl.chop(1);
    const QUrl parsedUrl(baseUrl);
    if (!parsedUrl.isValid() || (parsedUrl.scheme() != QLatin1String("http")
                                 && parsedUrl.scheme() != QLatin1String("https"))) {
        QTextStream(stderr) << "base-url invalida\n";
        return 2;
    }

    QVector<int> levels;
    if (!parsePositiveList(parser.value(QStringLiteral("concurrency")), &levels)) {
        QTextStream(stderr) << "concurrency invalida\n";
        return 2;
    }
    bool requestsOk = false;
    bool predictOk = false;
    bool promptOk = false;
    bool timeoutOk = false;
    const int requests = parser.value(QStringLiteral("requests")).toInt(&requestsOk);
    const int nPredict = parser.value(QStringLiteral("n-predict")).toInt(&predictOk);
    const int promptTokens = parser.value(QStringLiteral("prompt-tokens")).toInt(&promptOk);
    const int timeoutMs = parser.value(QStringLiteral("timeout-ms")).toInt(&timeoutOk);
    if (!requestsOk || requests < 1 || requests > 256
        || !predictOk || nPredict < 1 || nPredict > 4096
        || !promptOk || promptTokens < 0 || promptTokens > 1000000
        || !timeoutOk || timeoutMs < 1000 || timeoutMs > 3600000) {
        QTextStream(stderr) << "requests/n-predict/prompt-tokens/timeout-ms invalidos\n";
        return 2;
    }

    const QString prompt = TunerEngine::padPromptToTokens(
        QStringLiteral("Return exactly the word READY."), promptTokens);
    QNetworkAccessManager manager;
    QJsonArray levelsJson;
    bool allPassed = true;

    for (const int requestedConcurrency : levels) {
        QJsonArray batches;
        int remaining = requests;
        while (remaining > 0) {
            const int batchConcurrency = qMin(requestedConcurrency, remaining);
            const Batch batch = runBatch(manager, baseUrl, batchConcurrency,
                                          nPredict, prompt, timeoutMs);
            batches.append(batchToJson(batch));
            if (batch.failed > 0 || batch.completed != batch.requested)
                allPassed = false;
            remaining -= batchConcurrency;
        }
        levelsJson.append(QJsonObject{
            {QStringLiteral("requestedConcurrency"), requestedConcurrency},
            {QStringLiteral("requests"), requests},
            {QStringLiteral("batches"), batches},
        });
    }

    const QJsonObject receipt{
        {QStringLiteral("schema"), QStringLiteral("llamacode-auxiliary-concurrency-v1")},
        {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("baseUrl"), baseUrl},
        {QStringLiteral("promptTokensApprox"), promptTokens},
        {QStringLiteral("nPredict"), nPredict},
        {QStringLiteral("timeoutMs"), timeoutMs},
        {QStringLiteral("levels"), levelsJson},
        {QStringLiteral("allPassed"), allPassed},
    };
    const QByteArray output = QJsonDocument(receipt).toJson(QJsonDocument::Indented);
    const QString outputPath = parser.value(QStringLiteral("output")).trimmed();
    if (!outputPath.isEmpty()) {
        QFile file(outputPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream(stderr) << "no se pudo escribir output: " << file.errorString() << '\n';
            return 2;
        }
        file.write(output);
        file.close();
    }
    QTextStream(stdout) << output;
    return allPassed ? 0 : 1;
}
