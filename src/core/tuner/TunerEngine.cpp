#include "TunerEngine.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <algorithm>

namespace {

bool truthy(const std::string &v)
{
    return v == "on" || v == "1" || v == "true" || v == "yes";
}

}  // namespace

TunerEngine::TunerEngine(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
}

QStringList TunerEngine::composeArgs(const QStringList &baseArgs,
                                     const QVector<TunableParam> &params,
                                     const tuner::Config &config,
                                     const QString &host, int port)
{
    QStringList args = baseArgs;
    args << tunedArgs(params, config);
    args << QStringLiteral("--host") << host
         << QStringLiteral("--port") << QString::number(port);
    return args;
}

QStringList TunerEngine::tunedArgs(const QVector<TunableParam> &params,
                                   const tuner::Config &config)
{
    QStringList args;
    for (const TunableParam &p : params) {
        const auto it = config.find(p.spec.name);
        if (it == config.end()) continue;
        const std::string val = p.spec.optionValue(it->second);
        if (val.empty()) continue;
        if (p.isSwitch) {
            if (truthy(val)) args << p.flag;
        } else {
            args << p.flag << QString::fromStdString(val);
        }
    }
    return args;
}

TunableParam TunerEngine::speculativeNMaxParam(bool adaptive, int minDraftTokens)
{
    const long lower = adaptive ? std::max(1, minDraftTokens) : 1;
    const long upper = adaptive ? std::max(lower, 9L) : 5L;
    return {tuner::ParamSpec::intRange("spec-draft-n-max", lower, upper, 1),
            QStringLiteral("--spec-draft-n-max"), false};
}

bool TunerEngine::canTuneSplitMode(int gpuCount, const QString &backend,
                                   const QStringList &supportedFlags,
                                   const QStringList &effectiveArgs,
                                   bool cpuOnly, bool cpuMoe)
{
    if (cpuOnly || cpuMoe || gpuCount < 2
        || backend.compare(QStringLiteral("cuda"), Qt::CaseInsensitive) != 0)
        return false;
    if (!supportedFlags.contains(QStringLiteral("--split-mode"))
        && !supportedFlags.contains(QStringLiteral("-sm")))
        return false;
    return !effectiveArgs.contains(QStringLiteral("--override-tensor"))
           && !effectiveArgs.contains(QStringLiteral("-ot"));
}

bool TunerEngine::passesPromotionGate(const tuner::TrialResult &candidate,
                                      const tuner::TrialResult &baseline,
                                      double minGainPct)
{
    if (candidate.failed || candidate.throughput <= 0.0)
        return false;
    // Sin baseline válido no se puede comparar, pero el quality gate normal
    // del AutoTuner sigue protegiendo la búsqueda.
    if (baseline.failed || baseline.throughput <= 0.0)
        return true;
    const double required = baseline.throughput
        * (1.0 + qMax(0.0, minGainPct) / 100.0);
    return candidate.throughput >= required
           && candidate.quality + 1e-9 >= baseline.quality;
}

double ThroughputSample::blended(double ppWeight) const
{
    const double w = qBound(0.0, ppWeight, 1.0);
    // Si falta una pata, el objetivo es la otra: castigar un candidato por una
    // métrica que el server no reportó lo sacaría de la búsqueda por un motivo
    // que no es suyo.
    if (promptTps <= 0.0) return genTps;
    if (genTps <= 0.0) return promptTps;
    return w * promptTps + (1.0 - w) * genTps;
}

ThroughputSample TunerEngine::parseThroughput(const QByteArray &json)
{
    ThroughputSample s;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return s;
    const QJsonObject root = doc.object();
    const QJsonObject timings = root.value(QStringLiteral("timings")).toObject();
    auto metricInt = [&timings, &root](const QString &key) {
        const QJsonValue value = timings.contains(key) ? timings.value(key) : root.value(key);
        return value.isDouble() ? value.toInt(-1) : -1;
    };

    // Generación: predicted_per_second, con fallback derivado.
    if (timings.contains(QStringLiteral("predicted_per_second"))) {
        const double v = timings.value(QStringLiteral("predicted_per_second")).toDouble(-1.0);
        if (v > 0.0) s.genTps = v;
    }
    if (s.genTps <= 0.0) {
        const double predMs = timings.value(QStringLiteral("predicted_ms")).toDouble(-1.0);
        const double predTok = timings.value(QStringLiteral("predicted_n"))
                                   .toDouble(root.value(QStringLiteral("tokens_predicted")).toDouble(-1.0));
        if (predMs > 0.0 && predTok > 0.0) s.genTps = predTok / (predMs / 1000.0);
    }

    // Prefill: prompt_per_second, con fallback derivado.
    if (timings.contains(QStringLiteral("prompt_per_second"))) {
        const double v = timings.value(QStringLiteral("prompt_per_second")).toDouble(-1.0);
        if (v > 0.0) s.promptTps = v;
    }
    if (s.promptTps <= 0.0) {
        const double promptMs = timings.value(QStringLiteral("prompt_ms")).toDouble(-1.0);
        const double promptTok = timings.value(QStringLiteral("prompt_n"))
                                     .toDouble(root.value(QStringLiteral("tokens_evaluated")).toDouble(-1.0));
        if (promptMs > 0.0 && promptTok > 0.0) s.promptTps = promptTok / (promptMs / 1000.0);
    }
    s.draftTokens = qMax(0, metricInt(QStringLiteral("draft_n")));
    s.draftAcceptedTokens = qMax(0, metricInt(QStringLiteral("draft_n_accepted")));
    if (s.draftTokens > 0)
        s.draftAcceptedTokens = qMin(s.draftAcceptedTokens, s.draftTokens);
    return s;
}

QString TunerEngine::padPromptToTokens(const QString &prompt, int targetTokens)
{
    if (targetTokens <= 0) return prompt;
    // ~4 chars por token: alcanza para dimensionar el prefill. No hace falta
    // exactitud, sí que el prefill domine sobre la generación.
    const int targetChars = targetTokens * 4;
    if (prompt.size() >= targetChars) return prompt;

    // Relleno con variedad léxica: un mismo token repetido puede activar caminos
    // degenerados de caché/atención y no representa un prefill real.
    static const QStringList kFiller = {
        QStringLiteral("system"), QStringLiteral("module"), QStringLiteral("returns"),
        QStringLiteral("value"), QStringLiteral("context"), QStringLiteral("buffer"),
        QStringLiteral("request"), QStringLiteral("handler"), QStringLiteral("record"),
        QStringLiteral("session"), QStringLiteral("client"), QStringLiteral("thread"),
    };
    QString filler;
    filler.reserve(targetChars);
    int i = 0;
    while (filler.size() < targetChars - prompt.size()) {
        filler += kFiller.at(i % kFiller.size());
        filler += (i % 12 == 11) ? QLatin1Char('\n') : QLatin1Char(' ');
        ++i;
    }
    return filler + QLatin1Char('\n') + prompt;
}

QString TunerEngine::extractContent(const QByteArray &json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    const QJsonObject root = doc.object();
    // /completion -> "content"; /v1/chat/completions -> choices[0].message.content
    if (root.contains(QStringLiteral("content")))
        return root.value(QStringLiteral("content")).toString();
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (!choices.isEmpty()) {
        const QJsonObject msg = choices.first().toObject()
                                    .value(QStringLiteral("message")).toObject();
        return msg.value(QStringLiteral("content")).toString();
    }
    return {};
}

double TunerEngine::parsePerplexity(const QByteArray &text)
{
    const QString s = QString::fromUtf8(text);
    static const QRegularExpression re(
        QStringLiteral(R"((?:final\s+)?(?:perplexity|ppl)\s*[:=]\s*([0-9]+(?:\.[0-9]+)?))"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(s);
    double last = -1.0;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const double v = m.captured(1).toDouble();
        if (v > 0.0) last = v;
    }
    return last;
}

double TunerEngine::scoreQuality(const QString &content, const QStringList &acceptance)
{
    if (acceptance.isEmpty()) return 1.0;
    int hits = 0;
    for (const QString &needle : acceptance)
        if (!needle.isEmpty() && content.contains(needle, Qt::CaseInsensitive)) ++hits;
    return static_cast<double>(hits) / acceptance.size();
}

QStringList TunerEngine::perplexityArgs(const TunerJob &job, const tuner::Config *config) const
{
    auto valueAfter = [](const QStringList &args, std::initializer_list<QString> flags) {
        for (int i = 0; i + 1 < args.size(); ++i)
            for (const QString &f : flags)
                if (args[i] == f) return args[i + 1];
        return QString();
    };
    auto appendIfPresent = [](QStringList &out, const QStringList &args,
                              const QString &dstFlag, std::initializer_list<QString> srcFlags) {
        for (int i = 0; i + 1 < args.size(); ++i) {
            for (const QString &f : srcFlags) {
                if (args[i] == f) {
                    out << dstFlag << args[i + 1];
                    return;
                }
            }
        }
    };

    QStringList args;
    const QString model = valueAfter(job.baseArgs, {QStringLiteral("-m"), QStringLiteral("--model")});
    if (!model.isEmpty()) args << QStringLiteral("-m") << model;
    args << QStringLiteral("-f") << job.perplexityCorpusPath;
    appendIfPresent(args, job.baseArgs, QStringLiteral("-c"),
                    {QStringLiteral("-c"), QStringLiteral("--ctx-size")});
    if (job.cpuOnly) {
        args << QStringLiteral("-ngl") << QStringLiteral("0");
    } else {
        appendIfPresent(args, job.baseArgs, QStringLiteral("-ngl"),
                        {QStringLiteral("-ngl"), QStringLiteral("--n-gpu-layers"),
                         QStringLiteral("--gpu-layers")});
    }

    if (config) {
        const QStringList tuned = tunedArgs(job.params, *config);
        appendIfPresent(args, tuned, QStringLiteral("-ngl"),
                        {QStringLiteral("-ngl"), QStringLiteral("--n-gpu-layers"),
                         QStringLiteral("--gpu-layers")});
        appendIfPresent(args, tuned, QStringLiteral("-b"),
                        {QStringLiteral("-b"), QStringLiteral("--batch-size")});
        appendIfPresent(args, tuned, QStringLiteral("-ub"),
                        {QStringLiteral("-ub"), QStringLiteral("--ubatch-size")});
        appendIfPresent(args, tuned, QStringLiteral("-ctk"),
                        {QStringLiteral("--cache-type-k"), QStringLiteral("-ctk")});
        appendIfPresent(args, tuned, QStringLiteral("-ctv"),
                        {QStringLiteral("--cache-type-v"), QStringLiteral("-ctv")});
        if (tuned.contains(QStringLiteral("--flash-attn")) || tuned.contains(QStringLiteral("-fa")))
            args << QStringLiteral("-fa");
    }
    return args;
}

double TunerEngine::runPerplexity(const TunerJob &job, const tuner::Config *config, QString *diag)
{
    if (!job.usePerplexityGate || job.perplexityBinaryPath.isEmpty()
        || job.perplexityCorpusPath.isEmpty())
        return -1.0;
    QProcess proc;
    QProcessEnvironment penv = QProcessEnvironment::systemEnvironment();
    for (auto it = job.env.constBegin(); it != job.env.constEnd(); ++it)
        penv.insert(it.key(), it.value());
    proc.setProcessEnvironment(penv);
    proc.setWorkingDirectory(QFileInfo(job.perplexityBinaryPath).absolutePath());
    proc.start(job.perplexityBinaryPath, perplexityArgs(job, config));
    if (!proc.waitForStarted(15000)) {
        if (diag) *diag = QStringLiteral("llama-perplexity no arrancó");
        return -1.0;
    }
    if (!proc.waitForFinished(job.evalTimeoutMs)) {
        proc.kill();
        proc.waitForFinished(3000);
        if (diag) *diag = QStringLiteral("llama-perplexity timeout");
        return -1.0;
    }
    const QByteArray all = proc.readAllStandardOutput() + '\n' + proc.readAllStandardError();
    const double ppl = parsePerplexity(all);
    if (ppl <= 0.0 && diag) *diag = QString::fromUtf8(all).trimmed().right(200);
    return ppl;
}

bool TunerEngine::configChangesQualityRisk(const TunerJob &job, const tuner::Config &config) const
{
    for (const TunableParam &p : job.params) {
        if (!p.spec.qualityRisk) continue;
        const auto it = config.find(p.spec.name);
        if (it == config.end()) continue;
        const std::string val = p.spec.optionValue(it->second);
        if (!val.empty()) return true;
    }
    return false;
}

bool TunerEngine::waitForReady(const QString &baseUrl, int timeoutMs, QProcess *proc)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        if (m_cancel.load()) return false;
        // Si el server ya murió, no tiene sentido seguir esperando.
        if (proc && proc->state() == QProcess::NotRunning) return false;
        QNetworkRequest req(QUrl(baseUrl + QStringLiteral("/health")));
        QNetworkReply *reply = m_nam->get(req);
        QEventLoop loop;
        QTimer poll;
        poll.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&poll, &QTimer::timeout, &loop, &QEventLoop::quit);
        poll.start(2000);
        loop.exec();
        const bool ok = reply->isFinished() &&
                        reply->error() == QNetworkReply::NoError &&
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        reply->deleteLater();
        if (ok) return true;
        // backoff corto antes de reintentar
        QTimer pause;
        pause.setSingleShot(true);
        QEventLoop wloop;
        QObject::connect(&pause, &QTimer::timeout, &wloop, &QEventLoop::quit);
        pause.start(250);
        wloop.exec();
    }
    return false;
}

tuner::TrialResult TunerEngine::evaluateAgainstUrl(const QString &baseUrl,
                                                   const QString &prompt, int nPredict,
                                                   const QStringList &acceptance,
                                                   int timeoutMs, double ppWeight)
{
    tuner::TrialResult r;

    QJsonObject payload;
    payload[QStringLiteral("prompt")] = prompt;
    payload[QStringLiteral("n_predict")] = nPredict;
    payload[QStringLiteral("stream")] = false;
    payload[QStringLiteral("cache_prompt")] = false;

    QNetworkRequest req(QUrl(baseUrl + QStringLiteral("/completion")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    QEventLoop loop;
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(timeoutMs);
    loop.exec();

    if (!reply->isFinished() || reply->error() != QNetworkReply::NoError) {
        r.failed = true;
        reply->deleteLater();
        return r;
    }
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const ThroughputSample sample = parseThroughput(body);
    if (!sample.valid()) { r.failed = true; return r; }
    r.promptTps = sample.promptTps;
    r.genTps = sample.genTps;
    r.draftTokens = sample.draftTokens;
    r.draftAcceptedTokens = sample.draftAcceptedTokens;
    r.draftAcceptancePct = sample.draftAcceptancePct();
    r.throughput = sample.blended(ppWeight);
    if (r.throughput <= 0.0) { r.failed = true; return r; }
    r.quality = scoreQuality(extractContent(body), acceptance);
    return r;
}

tuner::TrialResult TunerEngine::launchAndMeasure(const TunerJob &job,
                                                 const QStringList &args,
                                                 const QString &evalPrompt,
                                                 QString *diag)
{
    const QString baseUrl =
        QStringLiteral("http://%1:%2").arg(job.host).arg(job.port);

    QProcess proc;
    // Entorno: sistema + overrides (PATH/CUDA para que cargue las DLLs del
    // backend GPU). Sin esto el server puede no usar GPU o crashear.
    QProcessEnvironment penv = QProcessEnvironment::systemEnvironment();
    for (auto it = job.env.constBegin(); it != job.env.constEnd(); ++it)
        penv.insert(it.key(), it.value());
    proc.setProcessEnvironment(penv);
    // Working dir = carpeta del binario (resuelve DLLs adyacentes en Windows).
    proc.setWorkingDirectory(QFileInfo(job.binaryPath).absolutePath());

    proc.start(job.binaryPath, args);
    if (!proc.waitForStarted(15000)) {
        tuner::TrialResult bad;
        bad.failed = true;
        return bad;
    }

    tuner::TrialResult r;
    if (!waitForReady(baseUrl, job.readyTimeoutMs, &proc)) {
        r.failed = true;
    } else {
        r = evaluateAgainstUrl(baseUrl, evalPrompt, job.nPredict,
                               job.acceptance, job.evalTimeoutMs, job.ppWeight);
    }

    // Capturar diagnóstico si falló (server murió o midió 0).
    if (r.failed && diag) {
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        const QString tail = (err + QLatin1Char('\n') + out).trimmed();
        *diag = tail.right(200);
        if (proc.state() == QProcess::NotRunning)
            *diag = QStringLiteral("[exit %1] ").arg(proc.exitCode()) + *diag;
    }

    proc.terminate();
    if (!proc.waitForFinished(8000)) {
        proc.kill();
        proc.waitForFinished(3000);
    }
    return r;
}

tuner::Trial TunerEngine::run(const TunerJob &job)
{
    m_baseline = tuner::TrialResult{};
    m_baseline.failed = true;   // sin medir hasta que se mida

    std::vector<tuner::ParamSpec> space;
    space.reserve(job.params.size());
    for (const TunableParam &p : job.params) space.push_back(p.spec);

    const QString baseUrl =
        QStringLiteral("http://%1:%2").arg(job.host).arg(job.port);

    // El relleno se calcula UNA vez: todos los trials deben ver exactamente el
    // mismo prompt, si no las mediciones no son comparables entre sí.
    const QString evalPrompt = padPromptToTokens(job.evalPrompt, job.prefillTokens);

    tuner::AutoTuner tuner(space, job.settings);
    int index = 0;
    const int total = job.settings.maxTrials;
    QString baselinePplDiag;
    const double baselinePpl = runPerplexity(job, nullptr, &baselinePplDiag);

    // Baseline: mide el perfil TAL CUAL lo tiene el usuario (args sin tocar),
    // antes de optimizar. Es la pata de benchmarking: sin un "antes" medido en
    // la misma máquina y con el mismo prompt, la mejora del tuner no es
    // comparable contra nada.
    if (job.measureBaseline && !job.baselineArgs.isEmpty() && !m_cancel.load()) {
        QString diag;
        QStringList args = job.baselineArgs;
        args << QStringLiteral("--host") << job.host
             << QStringLiteral("--port") << QString::number(job.port);
        const tuner::TrialResult b =
            launchAndMeasure(job, args, evalPrompt, &diag);
        m_baseline = b;
        QString sum = QStringLiteral("baseline (perfil actual)");
        if (b.promptTps > 0.0 || b.genTps > 0.0) {
            sum += QStringLiteral(" pp=%1 tg=%2")
                       .arg(b.promptTps > 0.0 ? QString::number(b.promptTps, 'f', 1)
                                              : QStringLiteral("n/d"),
                            b.genTps > 0.0 ? QString::number(b.genTps, 'f', 1)
                                           : QStringLiteral("n/d"));
        }
        if (b.draftAcceptancePct >= 0.0)
            sum += QStringLiteral(" spec=%1%% (%2/%3)")
                       .arg(b.draftAcceptancePct, 0, 'f', 1)
                       .arg(b.draftAcceptedTokens).arg(b.draftTokens);
        if (!diag.isEmpty()) sum += QStringLiteral(" ! %1").arg(diag);
        // index 0 = baseline: no cuenta como trial del presupuesto.
        emit trialDone(0, total, b.throughput, b.quality, sum, b.promptTps,
                       b.genTps, b.draftAcceptancePct);
    }

    auto eval = [&](const tuner::Config &cfg) -> tuner::TrialResult {
        ++index;
        const QStringList args =
            composeArgs(job.baseArgs, job.params, cfg, job.host, job.port);

        QString diag;
        tuner::TrialResult r = launchAndMeasure(job, args, evalPrompt, &diag);
        if (r.failed && diag.isEmpty())
            diag = QStringLiteral("server no arrancó");

        double ppl = -1.0;
        if (!r.failed && baselinePpl > 0.0 && configChangesQualityRisk(job, cfg)) {
            QString pplDiag;
            ppl = runPerplexity(job, &cfg, &pplDiag);
            if (ppl <= 0.0) {
                r.failed = true;
                diag = pplDiag.isEmpty() ? QStringLiteral("PPL falló") : pplDiag;
            } else {
                const double maxPpl = baselinePpl * (1.0 + job.perplexityThresholdPct / 100.0);
                const double pplQuality = ppl <= maxPpl ? 1.0 : qMax(0.0, maxPpl / ppl);
                r.quality = qMin(r.quality, pplQuality);
            }
        }

        QString summary;
        for (const TunableParam &p : job.params) {
            const auto it = cfg.find(p.spec.name);
            if (it != cfg.end())
                summary += QStringLiteral("%1=%2 ")
                               .arg(QString::fromStdString(p.spec.name),
                                    QString::fromStdString(p.spec.optionValue(it->second)));
        }
        QString sum = summary.trimmed();
        // Desglose pp/tg: el score es una mezcla y sin las dos patas a la vista
        // no hay forma de entender por qué ganó un candidato.
        if (r.promptTps > 0.0 || r.genTps > 0.0) {
            sum += QStringLiteral(" pp=%1 tg=%2")
                       .arg(r.promptTps > 0.0 ? QString::number(r.promptTps, 'f', 1)
                                              : QStringLiteral("n/d"),
                            r.genTps > 0.0 ? QString::number(r.genTps, 'f', 1)
                                           : QStringLiteral("n/d"));
        }
        if (r.draftAcceptancePct >= 0.0)
            sum += QStringLiteral(" spec=%1%% (%2/%3)")
                       .arg(r.draftAcceptancePct, 0, 'f', 1)
                       .arg(r.draftAcceptedTokens).arg(r.draftTokens);
        if (baselinePpl > 0.0) {
            if (ppl > 0.0)
                sum += QStringLiteral(" ppl=%1 base=%2")
                           .arg(ppl, 0, 'f', 2).arg(baselinePpl, 0, 'f', 2);
            else
                sum += QStringLiteral(" ppl-base=%1").arg(baselinePpl, 0, 'f', 2);
        } else if (job.usePerplexityGate && !baselinePplDiag.isEmpty()) {
            sum += QStringLiteral(" ppl-off=%1").arg(baselinePplDiag.left(80));
        }
        if (!diag.isEmpty()) sum += QStringLiteral(" ! %1").arg(diag);
        emit trialDone(index, total, r.throughput, r.quality, sum, r.promptTps,
                       r.genTps, r.draftAcceptancePct);
        return r;
    };

    return tuner.run(eval, [this]() { return m_cancel.load(); });
}
