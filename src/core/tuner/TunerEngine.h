#pragma once
// TunerEngine: capa de integración entre el core AutoTuner (TPE-lite + gate de
// calidad) y el mundo real de LlamaCode: lanza llama-server por candidato, mide
// throughput (tok/s) vía /completion y califica la salida con criterios de
// aceptación (substrings, estilo EvalSuite) y, cuando está disponible,
// llama-perplexity contra un corpus baseline. El resultado alimenta al optimizador
// y la mejor config se exporta como RuntimePreset/extraArgs.
//
// Las primitivas de medición (composeArgs / parseThroughput / scoreQuality) son
// estáticas y puras para poder testearse sin modelo real. evaluateAgainstUrl()
// hace la medición HTTP contra un server ya levantado (testeable con mock).
// evaluate() añade el ciclo lanzar/esperar/medir/matar con QProcess.

#include "AutoTuner.h"

#include <QObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>

class QNetworkAccessManager;

// Mapea un ParamSpec del optimizador a un flag de llama-server.
struct TunableParam {
    tuner::ParamSpec spec;
    QString flag;        // p.ej. "-ngl", "-b", "--cache-type-k", "--flash-attn"
    bool isSwitch = false;  // flag booleano sin valor; categórico on/off
};

// Medición cruda de un trial: llama.cpp reporta prefill y generación por
// separado en "timings". Tunear -b/-ub mirando sólo la generación es medir el
// efecto secundario: su efecto principal es el prefill.
struct ThroughputSample {
    double promptTps = -1.0;   // timings.prompt_per_second
    double genTps = -1.0;      // timings.predicted_per_second
    int draftTokens = 0;       // timings.draft_n, cuando hay speculative decoding
    int draftAcceptedTokens = 0; // timings.draft_n_accepted
    bool valid() const { return promptTps > 0.0 || genTps > 0.0; }
    double draftAcceptancePct() const {
        return draftTokens > 0 ? 100.0 * draftAcceptedTokens / draftTokens : -1.0;
    }
    // Mezcla ponderada: ppWeight 0 = sólo generación (comportamiento histórico),
    // 1 = sólo prefill. Si falta una pata, devuelve la otra en vez de castigar
    // al candidato por algo que no se pudo medir.
    double blended(double ppWeight) const;
};

struct TunerJob {
    QString binaryPath;
    QMap<QString, QString> env;  // overrides de entorno (CUDA/PATH para DLLs, etc.)
    QStringList baseArgs;        // args fijos (modelo, ctx, etc.) sin host/port/tuned
    // Args del perfil SIN tocar (con sus flags tuneables originales): se miden
    // como "antes" para poder comparar contra la mejor config encontrada.
    QStringList baselineArgs;
    bool measureBaseline = false;
    double minBaselineGainPct = 1.0; // no promocionar resultados dentro del ruido
    QString host = QStringLiteral("127.0.0.1");
    int port = 18080;            // puerto scratch, distinto del server principal
    QString evalPrompt;          // prompt de medición
    int nPredict = 256;          // tokens a generar por trial
    // Largo objetivo del prefill (tokens aprox.). El prompt de eval se rellena
    // con relleno sintético hasta llegar acá: con un prompt de ~30 tokens el PP
    // es ruido y -b/-ub no tienen señal que optimizar. 0 = no rellenar.
    int prefillTokens = 0;
    // Peso del PP en el objetivo [0,1]. 0 = sólo TG (histórico). Un agente con
    // system prompt largo quiere PP; un chat corto quiere TG.
    double ppWeight = 0.0;
    QStringList acceptance;      // substrings esperados -> calidad [0,1]
    QString perplexityBinaryPath; // llama-perplexity(.exe), opcional
    QString perplexityCorpusPath; // corpus para gate PPL, opcional
    double perplexityThresholdPct = 3.0; // tolerancia vs baseline
    bool usePerplexityGate = false;
    bool cpuOnly = false;        // fuerza -ngl 0 y espacio CPU-friendly
    int readyTimeoutMs = 120000; // espera de arranque (carga de modelo)
    int evalTimeoutMs = 120000;  // espera de la respuesta /completion
    tuner::TunerSettings settings;
    QVector<TunableParam> params;
};

class TunerEngine : public QObject {
    Q_OBJECT
public:
    explicit TunerEngine(QObject *parent = nullptr);

    // Corre la optimización completa (bloqueante; usa QEventLoop por trial).
    // Devuelve el mejor trial. Emite trialDone() en cada iteración.
    tuner::Trial run(const TunerJob &job);

    // Medición del perfil sin tunear (válida sólo tras run() con
    // measureBaseline). failed=true si no se midió.
    tuner::TrialResult baseline() const { return m_baseline; }

    // Pide cortar tras el trial en curso (thread-safe). El server del trial
    // activo se mata igual al terminar la medición.
    void cancel() { m_cancel.store(true); }

    // Mide un candidato contra un server YA levantado en baseUrl (sin gestionar
    // proceso). Testeable con un mock HTTP.
    tuner::TrialResult evaluateAgainstUrl(const QString &baseUrl,
                                          const QString &prompt, int nPredict,
                                          const QStringList &acceptance,
                                          int timeoutMs, double ppWeight = 0.0);

    // ── Primitivas puras (testeables sin red ni proceso) ──────────────────────

    // Compone el argv final: baseArgs + flags de los params según config + host/port.
    static QStringList composeArgs(const QStringList &baseArgs,
                                   const QVector<TunableParam> &params,
                                   const tuner::Config &config,
                                   const QString &host, int port);

    // Extrae prefill y generación (tok/s) de una respuesta /completion de
    // llama.cpp. Usa timings.prompt_per_second / predicted_per_second; si
    // faltan, los deriva de prompt_n|prompt_ms y tokens_predicted|predicted_ms.
    // Los campos no medibles quedan en -1.
    static ThroughputSample parseThroughput(const QByteArray &json);

    // Rellena el prompt hasta ~targetTokens con texto sintético (heurística
    // ~4 chars/token) para que el prefill sea medible. El prompt original queda
    // AL FINAL: así los criterios de aceptación siguen aplicando sobre una
    // instrucción que el modelo tiene fresca, y el relleno no la sepulta.
    static QString padPromptToTokens(const QString &prompt, int targetTokens);

    // Fracción de substrings de aceptación presentes en la respuesta [0,1].
    // Sin criterios -> 1.0 (calidad no penaliza).
    static double scoreQuality(const QString &content, const QStringList &acceptance);

    // Extrae el texto generado de una respuesta /completion (campo "content").
    static QString extractContent(const QByteArray &json);

    // Extrae un valor de perplexity desde stdout/stderr de llama-perplexity.
    // Devuelve -1 si no encuentra un número reconocible.
    static double parsePerplexity(const QByteArray &text);

    // Solo los flags afinados (sin baseArgs ni host/port) para fusionar en
    // LaunchProfile.extraArgs y persistir la mejor config como perfil.
    static QStringList tunedArgs(const QVector<TunableParam> &params,
                                 const tuner::Config &config);

    // Rango seguro para el techo de speculative decoding. En modo adaptativo
    // se conserva el n-min del perfil y se explora hasta 9: cubre el sweet spot
    // observado sin empujar la búsqueda a drafts excesivamente largos.
    static TunableParam speculativeNMaxParam(bool adaptive, int minDraftTokens = 0);

    // Gate conservador para explorar --split-mode: sólo CUDA multi-GPU, flag
    // confirmado y sin layouts MoE/per-tensor cuyo reparto no es intercambiable.
    static bool canTuneSplitMode(int gpuCount, const QString &backend,
                                 const QStringList &supportedFlags,
                                 const QStringList &effectiveArgs,
                                 bool cpuOnly, bool cpuMoe);

    // Un candidato sólo se promociona si supera al perfil medido por el margen
    // pedido y no pierde calidad. Si el baseline no pudo medirse, no se inventa
    // un bloqueo: queda a cargo del quality gate normal.
    static bool passesPromotionGate(const tuner::TrialResult &candidate,
                                    const tuner::TrialResult &baseline,
                                    double minGainPct = 1.0);

signals:
    void trialDone(int index, int total, double throughput, double quality,
                   const QString &summary, double promptTps, double genTps,
                   double draftAcceptancePct);

private:
    // Ciclo lanzar/esperar-ready/medir/matar para un argv dado. Compartido por
    // el baseline y por cada trial: si midieran distinto, la comparación
    // "antes vs después" no valdría nada.
    tuner::TrialResult launchAndMeasure(const TunerJob &job, const QStringList &args,
                                        const QString &evalPrompt, QString *diag);

    // Espera /health 200. Aborta apenas el proceso muere (server crasheó).
    bool waitForReady(const QString &baseUrl, int timeoutMs, class QProcess *proc);
    double runPerplexity(const TunerJob &job, const tuner::Config *config = nullptr,
                         QString *diag = nullptr);
    QStringList perplexityArgs(const TunerJob &job, const tuner::Config *config) const;
    bool configChangesQualityRisk(const TunerJob &job, const tuner::Config &config) const;

    QNetworkAccessManager *m_nam = nullptr;
    std::atomic<bool> m_cancel{false};
    tuner::TrialResult m_baseline;
};
