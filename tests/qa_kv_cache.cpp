#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
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

#include <cmath>

#include "core/tuner/TunerEngine.h"
#include "long_context_probe.h"

// Opt-in QA against a real llama-server. It intentionally is not registered
// with ctest: it needs a model, a running server and can allocate very large
// prompts. It measures retrieval and the two timing legs separately, while
// sending the requested users for each depth concurrently.

namespace {

using long_context_probe::RetrievalCase;

constexpr int kMaxContextTokens = 1048576;

struct ProbeResult {
    RetrievalCase fixture;
    bool finished = false;
    bool failed = false;
    bool containsPasskey = false;
    bool exactPasskey = false;
    int httpStatus = 0;
    qint64 latencyMs = -1;
    ThroughputSample timings;
    QString error;
};

bool parseIntList(const QString &raw, QVector<int> *out)
{
    out->clear();
    for (const QString &part : raw.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (!ok || value <= 0 || value > kMaxContextTokens) return false;
        out->append(value);
    }
    return !out->isEmpty();
}

bool parseDepthList(const QString &raw, QVector<double> *out)
{
    out->clear();
    for (const QString &part : raw.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const double value = part.trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(value) || value < 0.0 || value > 1.0)
            return false;
        out->append(value);
    }
    return !out->isEmpty();
}

QVector<ProbeResult> runBatch(QNetworkAccessManager &manager, const QString &baseUrl,
                              const QVector<RetrievalCase> &cases, int nPredict,
                              int timeoutMs)
{
    QVector<ProbeResult> results;
    results.resize(cases.size());
    if (cases.isEmpty()) return results;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QHash<QNetworkReply *, int> indexes;
    QHash<QNetworkReply *, qint64> startedAt;
    int remaining = cases.size();

    for (int i = 0; i < cases.size(); ++i) {
        results[i].fixture = cases.at(i);

        // Qwen3's raw /completion endpoint may echo the instruction before the
        // answer, which makes a valid retrieval look truncated or inexact.
        // Use the model's chat template and deterministic sampling so the A/B
        // receipt scores the answer rather than an endpoint formatting detail.
        const QJsonObject payload{
            {QStringLiteral("messages"), QJsonArray{
                QJsonObject{
                    {QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), cases.at(i).prompt},
                },
            }},
            {QStringLiteral("max_tokens"), nPredict},
            {QStringLiteral("temperature"), 0.0},
            {QStringLiteral("top_k"), 1},
            {QStringLiteral("top_p"), 1.0},
            {QStringLiteral("min_p"), 0.0},
            {QStringLiteral("repeat_penalty"), 1.0},
            {QStringLiteral("presence_penalty"), 0.0},
            {QStringLiteral("stream"), false},
            {QStringLiteral("cache_prompt"), false},
        };
        QNetworkRequest request(QUrl(baseUrl + QStringLiteral("/v1/chat/completions")));
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          QByteArrayLiteral("application/json"));
        QNetworkReply *reply = manager.post(
            request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        indexes.insert(reply, i);
        startedAt.insert(reply, QDateTime::currentMSecsSinceEpoch());

        QObject::connect(reply, &QNetworkReply::finished, &loop, [&, reply]() {
            const auto it = indexes.find(reply);
            if (it == indexes.end()) return; // timeout already classified it
            const int index = it.value();
            indexes.erase(it);
            const qint64 started = startedAt.take(reply);
            ProbeResult &result = results[index];
            result.finished = true;
            result.latencyMs = qMax<qint64>(0,
                QDateTime::currentMSecsSinceEpoch() - started);
            result.httpStatus = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();

            if (reply->error() != QNetworkReply::NoError) {
                result.failed = true;
                result.error = reply->errorString();
            } else {
                const QByteArray body = reply->readAll();
                const QString content = TunerEngine::extractContent(body).trimmed();
                result.timings = TunerEngine::parseThroughput(body);
                result.containsPasskey = TunerEngine::scoreQuality(
                    content, {result.fixture.passkey}) >= 1.0;
                result.exactPasskey = content == result.fixture.passkey;
                if (!result.timings.valid()) {
                    result.failed = true;
                    result.error = QStringLiteral("respuesta sin timings de llama-server");
                } else if (!result.containsPasskey) {
                    result.failed = true;
                    result.error = QStringLiteral("passkey no recuperada");
                } else if (!result.exactPasskey) {
                    result.failed = true;
                    result.error = QStringLiteral("passkey encontrada pero salida no exacta");
                }
            }
            reply->deleteLater();
            if (--remaining <= 0) loop.quit();
        });
    }

    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        const QList<QNetworkReply *> pending = indexes.keys();
        for (QNetworkReply *reply : pending) {
            const auto it = indexes.find(reply);
            if (it == indexes.end()) continue;
            ProbeResult &result = results[it.value()];
            result.finished = true;
            result.failed = true;
            result.error = QStringLiteral("timeout");
        }
        indexes.clear();
        startedAt.clear();
        for (QNetworkReply *reply : pending) {
            reply->abort();
            reply->deleteLater();
        }
        loop.quit();
    });

    timeout.start(timeoutMs);
    loop.exec();

    // If the event loop ended because the last reply finished, no pending
    // entries remain. This cleanup also covers an early loop termination.
    const QList<QNetworkReply *> pending = indexes.keys();
    for (QNetworkReply *reply : pending) {
        const auto it = indexes.find(reply);
        if (it != indexes.end()) {
            ProbeResult &result = results[it.value()];
            result.finished = true;
            result.failed = true;
            result.error = QStringLiteral("request abandoned");
            reply->abort();
            reply->deleteLater();
        }
    }
    return results;
}

QJsonObject resultToJson(const ProbeResult &result)
{
    const RetrievalCase &fixture = result.fixture;
    QJsonObject out{
        {QStringLiteral("id"), fixture.id},
        {QStringLiteral("stream"), fixture.streamId},
        {QStringLiteral("contextTokens"), fixture.contextTokens},
        {QStringLiteral("depth"), fixture.depth},
        {QStringLiteral("passkey"), fixture.passkey},
        {QStringLiteral("finished"), result.finished},
        {QStringLiteral("failed"), result.failed},
        {QStringLiteral("containsPasskey"), result.containsPasskey},
        {QStringLiteral("exactPasskey"), result.exactPasskey},
        {QStringLiteral("httpStatus"), result.httpStatus},
        {QStringLiteral("latencyMs"), result.latencyMs},
        {QStringLiteral("promptTps"), result.timings.promptTps},
        {QStringLiteral("genTps"), result.timings.genTps},
        {QStringLiteral("error"), result.error},
    };
    if (result.timings.draftTokens > 0) {
        out[QStringLiteral("draftTokens")] = result.timings.draftTokens;
        out[QStringLiteral("draftAcceptedTokens")] = result.timings.draftAcceptedTokens;
        out[QStringLiteral("draftAcceptancePct")] = result.timings.draftAcceptancePct();
    }
    return out;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qa_kv_cache"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Long-context retrieval and KV-cache timing probe for llama-server"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("base-url"),
                                 QStringLiteral("llama-server URL, e.g. http://127.0.0.1:8080"));
    parser.addOption(QCommandLineOption(
        QStringLiteral("contexts"), QStringLiteral("comma-separated context sizes"),
        QStringLiteral("tokens"), QStringLiteral("8192,32768,131072")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("depths"), QStringLiteral("comma-separated needle depths [0..1]"),
        QStringLiteral("fractions"), QStringLiteral("0.05,0.15,0.25,0.50,0.75,0.90,0.95")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("users"), QStringLiteral("concurrent users per depth"),
        QStringLiteral("count"), QStringLiteral("1")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("n-predict"), QStringLiteral("tokens generated per request"),
        QStringLiteral("tokens"), QStringLiteral("32")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("timeout-ms"), QStringLiteral("per-batch timeout"),
        QStringLiteral("milliseconds"), QStringLiteral("120000")));
    if (!parser.parse(QCoreApplication::arguments())) {
        QTextStream(stderr) << parser.errorText() << '\n';
        return 2;
    }

    const QStringList positional = parser.positionalArguments();
    if (positional.size() != 1) {
        QTextStream(stderr) << parser.helpText();
        return 2;
    }
    QString baseUrl = positional.first().trimmed();
    while (baseUrl.endsWith(QLatin1Char('/'))) baseUrl.chop(1);
    if (!QUrl(baseUrl).isValid() || !baseUrl.startsWith(QStringLiteral("http"))) {
        QTextStream(stderr) << "base-url invalida\n";
        return 2;
    }

    QVector<int> contexts;
    QVector<double> depths;
    if (!parseIntList(parser.value(QStringLiteral("contexts")), &contexts)
        || !parseDepthList(parser.value(QStringLiteral("depths")), &depths)) {
        QTextStream(stderr) << "contexts/depths invalidos\n";
        return 2;
    }
    bool usersOk = false;
    bool predictOk = false;
    bool timeoutOk = false;
    const int users = parser.value(QStringLiteral("users")).toInt(&usersOk);
    const int nPredict = parser.value(QStringLiteral("n-predict")).toInt(&predictOk);
    const int timeoutMs = parser.value(QStringLiteral("timeout-ms")).toInt(&timeoutOk);
    if (!usersOk || users < 1 || users > 256
        || !predictOk || nPredict < 1 || nPredict > 4096
        || !timeoutOk || timeoutMs < 1000 || timeoutMs > 3600000) {
        QTextStream(stderr) << "users/n-predict/timeout-ms invalidos\n";
        return 2;
    }

    QNetworkAccessManager manager;
    QJsonArray rows;
    int total = 0;
    int passed = 0;
    int failed = 0;
    double sumPromptTps = 0.0;
    double sumGenTps = 0.0;

    for (const int contextTokens : contexts) {
        for (int depthIndex = 0; depthIndex < depths.size(); ++depthIndex) {
            QVector<RetrievalCase> batch;
            for (int user = 0; user < users; ++user) {
                const QString stream = QStringLiteral("user-%1").arg(user, 3, 10,
                                                                       QLatin1Char('0'));
                const auto userCases = long_context_probe::buildCases(
                    contextTokens, {depths.at(depthIndex)}, stream);
                batch += userCases;
            }

            const QVector<ProbeResult> results = runBatch(
                manager, baseUrl, batch, nPredict, timeoutMs);
            for (const ProbeResult &result : results) {
                ++total;
                if (!result.failed && result.exactPasskey) ++passed;
                else ++failed;
                if (result.timings.promptTps > 0.0) sumPromptTps += result.timings.promptTps;
                if (result.timings.genTps > 0.0) sumGenTps += result.timings.genTps;
                rows.append(resultToJson(result));
            }
        }
    }

    const QJsonObject summary{
        {QStringLiteral("totalRequests"), total},
        {QStringLiteral("passedExactRetrieval"), passed},
        {QStringLiteral("failed"), failed},
        {QStringLiteral("allPassed"), total > 0 && failed == 0},
        {QStringLiteral("aggregatePromptTps"), sumPromptTps},
        {QStringLiteral("aggregateGenTps"), sumGenTps},
    };
    QJsonArray contextArray;
    for (const int context : contexts) contextArray.append(context);
    QJsonArray depthArray;
    for (const double depth : depths) depthArray.append(depth);
    const QJsonObject receipt{
        {QStringLiteral("schema"), QStringLiteral("llamacode-kv-cache-qa-v1")},
        {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("baseUrl"), baseUrl},
        {QStringLiteral("contexts"), contextArray},
        {QStringLiteral("depths"), depthArray},
        {QStringLiteral("users"), users},
        {QStringLiteral("nPredict"), nPredict},
        {QStringLiteral("timeoutMs"), timeoutMs},
        {QStringLiteral("results"), rows},
        {QStringLiteral("summary"), summary},
    };

    QTextStream(stdout) << QJsonDocument(receipt).toJson(QJsonDocument::Indented);
    return total > 0 && failed == 0 ? 0 : 1;
}
