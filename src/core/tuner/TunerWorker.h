#pragma once
// TunerWorker: corre TunerEngine::run() en un hilo aparte para no congelar la UI
// (cada trial lanza llama-server y genera tokens, segundos a minutos). Se mueve a
// un QThread; al arrancar ejecuta el job y emite progreso/fin por señales queued.

#include "TunerEngine.h"

#include <QObject>
#include <QString>

class TunerWorker : public QObject {
    Q_OBJECT
public:
    explicit TunerWorker(TunerJob job, QObject *parent = nullptr);

public slots:
    void run();
    void cancel();  // thread-safe; pide cortar el tuning en curso

signals:
    void trial(int index, int total, double throughput, double quality,
               const QString &summary, double promptTps, double genTps,
               double draftAcceptancePct);
    // bestArgs: flags afinados (sin host/port) para fusionar en extraArgs.
    // promptTps/genTps: desglose de la mejor config (-1 = no medido).
    // basePromptTps/baseGenTps: lo mismo para el perfil sin tunear, para poder
    // reportar la mejora real (-1 = no se midió baseline).
    void finished(bool ok, const QStringList &bestArgs, double throughput,
                  double quality, double promptTps, double genTps,
                  double basePromptTps, double baseGenTps,
                  double draftAcceptancePct, double baseDraftAcceptancePct);

private:
    TunerJob m_job;
    TunerEngine *m_engine = nullptr;  // válido sólo durante run()
};
