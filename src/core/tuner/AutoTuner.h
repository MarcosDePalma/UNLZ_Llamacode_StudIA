#pragma once
// AutoTuner: búsqueda automática de hiperparámetros de inferencia (estilo
// llama-launcher v1.3) PERO con gate de calidad. El optimizador (TPE-lite,
// Parzen discreto por parámetro) maximiza throughput sujeto a que la calidad
// medida por el harness (EvalSuite) no caiga por debajo de un umbral. Esto
// evita el fallo señalado en la comunidad: tunear el quant de KV cache solo por
// velocidad colapsa siempre al quant más bajo y degrada el modelo.
//
// Core en C++ puro (solo std) para ser testeable sin Qt ni servidor real: la
// evaluación de un candidato se inyecta como callback. La integración con
// AppController/llama-server y EvalSuite se hace en una capa aparte.

#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace tuner {

enum class ParamKind { IntRange, Categorical };

// Espacio de búsqueda de un parámetro de llama-server (p.ej. -ngl, -b, -ub,
// --threads, flash-attn, --cache-type-k/v...).
struct ParamSpec {
    std::string name;
    ParamKind kind = ParamKind::IntRange;

    // IntRange: rango inclusivo discretizado por step (lo, lo+step, ..., <=hi).
    long lo = 0;
    long hi = 0;
    long step = 1;

    // Categorical: opciones (p.ej. {"f16","q8_0","q4_0"} para cache-type).
    std::vector<std::string> choices;

    // Marca knobs que pueden degradar calidad (quant de KV cache, etc.).
    // No cambia la optimización (el gate ya protege), pero se reporta.
    bool qualityRisk = false;

    static ParamSpec intRange(std::string n, long lo, long hi, long step = 1,
                              bool risk = false);
    static ParamSpec categorical(std::string n, std::vector<std::string> opts,
                                 bool risk = false);

    // Número de opciones discretas.
    int optionCount() const;
    // Valor textual de la opción i (para construir args de llama-server).
    std::string optionValue(int i) const;
};

// Un candidato concreto: índice de opción elegido por cada parámetro.
using Config = std::map<std::string, int>;

// Resultado de evaluar un candidato.
struct TrialResult {
    // Objetivo que ve el optimizador (mayor = mejor). Es una mezcla ponderada de
    // prefill y generación; la capa de integración decide el peso. El core no
    // sabe de dónde sale: para el TPE sigue siendo un escalar.
    double throughput = 0.0;
    // Desglose informativo (tokens/seg). No participa de la optimización: sirve
    // para reportar en UI/logs, porque un score mezclado es ilegible sin ver las
    // dos patas. -1 = no medido.
    double promptTps = -1.0;   // prefill / prompt processing (PP)
    double genTps = -1.0;      // generación / token generation (TG)
    int draftTokens = 0;       // tokens propuestos por el drafter, si aplica
    int draftAcceptedTokens = 0; // tokens aceptados por el target, si aplica
    double draftAcceptancePct = -1.0; // -1 = backend no reportó speculative metrics
    double quality = 0.0;     // score EvalSuite normalizado [0,1] (mayor = mejor)
    bool failed = false;      // el servidor no arrancó / OOM / timeout
};

// Registro completo de un trial evaluado.
struct Trial {
    Config config;
    TrialResult result;
    double loss = 0.0;  // objetivo escalarizado (menor = mejor)
};

struct TunerSettings {
    int maxTrials = 30;
    int startupTrials = 8;   // muestreo aleatorio inicial antes de activar TPE
    double gamma = 0.25;     // fracción "buena" del historial para el modelo l(x)
    int eiCandidates = 24;   // candidatos muestreados de l(x) por iteración
    double qualityGate = 0.0;  // calidad mínima aceptable; <gate => penalizado
    double prior = 1.0;      // suavizado de Laplace para las distribuciones Parzen
    uint64_t seed = 0;       // 0 => semilla aleatoria
};

class AutoTuner {
public:
    using EvalFn = std::function<TrialResult(const Config &)>;

    AutoTuner(std::vector<ParamSpec> space, TunerSettings settings);

    // Corre la optimización. eval evalúa un candidato (lanzar servidor + medir).
    // shouldStop (opcional) se consulta antes de cada trial; si devuelve true,
    // corta y devuelve el mejor encontrado hasta el momento. Devuelve el mejor
    // trial (menor loss).
    Trial run(const EvalFn &eval, const std::function<bool()> &shouldStop = {});

    const std::vector<Trial> &history() const { return m_history; }
    const Trial &best() const { return m_best; }

    // Escalariza throughput+quality en una loss (menor = mejor). Si quality cae
    // bajo el gate, agrega una penalización grande proporcional al déficit, de
    // modo que ningún throughput compensa romper la calidad.
    double computeLoss(const TrialResult &r) const;

private:
    Config sampleRandom();
    Config sampleTPE();
    const ParamSpec &spec(const std::string &name) const;

    std::vector<ParamSpec> m_space;
    TunerSettings m_settings;
    std::mt19937_64 m_rng;
    std::vector<Trial> m_history;
    Trial m_best;
    bool m_haveBest = false;
};

}  // namespace tuner
