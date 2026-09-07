#include "TunerWorker.h"

TunerWorker::TunerWorker(TunerJob job, QObject *parent)
    : QObject(parent), m_job(std::move(job))
{
}

void TunerWorker::run()
{
    // TunerEngine se crea en este hilo: su QNetworkAccessManager y los QProcess
    // de cada trial quedan con la afinidad correcta.
    TunerEngine engine;
    m_engine = &engine;
    QObject::connect(&engine, &TunerEngine::trialDone, this,
                     [this](int i, int n, double tps, double q, const QString &s,
                            double pp, double tg, double specAcceptance) {
                         emit trial(i, n, tps, q, s, pp, tg, specAcceptance);
                     });

    const tuner::Trial best = engine.run(m_job);
    const tuner::TrialResult base = engine.baseline();
    m_engine = nullptr;
    const bool baselineMeasured = !base.failed && base.throughput > 0.0;
    const bool promotionOk = !m_job.measureBaseline || !baselineMeasured
        || TunerEngine::passesPromotionGate(best.result, base,
                                             m_job.minBaselineGainPct);
    const bool ok = !best.result.failed && best.result.throughput > 0.0 && promotionOk;
    const QStringList bestArgs =
        ok ? TunerEngine::tunedArgs(m_job.params, best.config) : QStringList{};
    emit finished(ok, bestArgs, best.result.throughput, best.result.quality,
                  best.result.promptTps, best.result.genTps,
                  base.failed ? -1.0 : base.promptTps,
                  base.failed ? -1.0 : base.genTps,
                  best.result.draftAcceptancePct,
                  base.failed ? -1.0 : base.draftAcceptancePct);
}

void TunerWorker::cancel()
{
    if (m_engine) m_engine->cancel();  // sólo setea un atomic; thread-safe
}
