// Selftest del AutoTuner sin Qt ni servidor real. Usa un objetivo sintético que
// reproduce el fallo de llama-launcher v1.3: el quant de KV cache más bajo es el
// más rápido pero degrada la calidad. Verifica que:
//   (1) el tuner CON gate de calidad NO colapsa al quant KV más bajo, y
//   (2) el tuner SIN gate (= solo velocidad, como llama-launcher) SÍ lo hace.
//
// Compilar (Developer PowerShell / con cl en PATH):
//   cl /std:c++17 /EHsc /I src tools\tuner_selftest.cpp src\core\tuner\AutoTuner.cpp
// Ejecutar: tuner_selftest.exe   (exit 0 = OK)

#include "core/tuner/AutoTuner.h"

#include <cstdio>
#include <random>

using namespace tuner;

int main()
{
    // Espacio de búsqueda representativo de llama-server.
    std::vector<ParamSpec> space = {
        ParamSpec::intRange("ngl", 0, 40, 8),                       // capas en GPU
        ParamSpec::intRange("batch", 256, 1024, 256),               // -b
        ParamSpec::categorical("cache-type-k", {"q8_0", "q4_0"},
                               /*qualityRisk=*/true),               // quant KV
    };

    // Mapas de efecto del quant KV: índice 0=q8_0, 1=q4_0.
    const double quantSpeed[2]   = {10.0, 25.0};  // q más bajo = más rápido
    const double quantQuality[2] = {0.97, 0.80};  // q4 degrada fuerte

    std::mt19937_64 noiseRng(1234);
    std::normal_distribution<double> noise(0.0, 0.8);

    auto findSpec = [&](const std::string &n) -> const ParamSpec & {
        for (const auto &s : space) if (s.name == n) return s;
        throw std::runtime_error("spec?");
    };

    // Objetivo sintético: throughput crece con ngl/batch y con quant más bajo;
    // calidad cae con quant bajo. (No depende de servidor real.)
    auto eval = [&](const Config &c) -> TrialResult {
        const int ng1Idx = c.at("ngl");
        const long ngl = std::stol(findSpec("ngl").optionValue(ng1Idx));
        const int batchIdx = c.at("batch");
        const long batch = std::stol(findSpec("batch").optionValue(batchIdx));
        const int kIdx = c.at("cache-type-k");

        TrialResult r;
        r.throughput = 20.0 + ngl * 0.6 + (batch / 256.0) * 1.5
                       + quantSpeed[kIdx] + noise(noiseRng);
        r.quality = quantQuality[kIdx];
        r.failed = false;
        return r;
    };

    auto kName = [&](const Trial &t) {
        return findSpec("cache-type-k").optionValue(t.config.at("cache-type-k"));
    };
    auto report = [&](const char *label, const Trial &t) {
        std::printf("%-22s -> ngl=%s batch=%s cache-type-k=%-4s | tps=%.1f q=%.2f\n",
                    label,
                    findSpec("ngl").optionValue(t.config.at("ngl")).c_str(),
                    findSpec("batch").optionValue(t.config.at("batch")).c_str(),
                    kName(t).c_str(), t.result.throughput, t.result.quality);
    };

    // (1) CON gate: calidad mínima 0.95 -> q4_0 (0.80) prohibido, q8_0 (0.97) OK.
    TunerSettings gated;
    gated.maxTrials = 60;
    gated.startupTrials = 12;
    gated.qualityGate = 0.95;
    gated.seed = 42;
    AutoTuner tunerGated(space, gated);
    Trial bestGated = tunerGated.run(eval);

    // (2) SIN gate: solo velocidad (comportamiento llama-launcher).
    TunerSettings speedOnly = gated;
    speedOnly.qualityGate = 0.0;
    AutoTuner tunerSpeed(space, speedOnly);
    Trial bestSpeed = tunerSpeed.run(eval);

    report("CON gate (LlamaCode)", bestGated);
    report("SIN gate (llama-launcher)", bestSpeed);

    int rc = 0;
    if (kName(bestGated) == "q4_0") {
        std::printf("FAIL: tuner con gate colapso al quant KV mas bajo\n");
        rc = 1;
    }
    if (bestGated.result.quality < gated.qualityGate) {
        std::printf("FAIL: mejor config con gate viola el umbral de calidad\n");
        rc = 1;
    }
    if (kName(bestSpeed) != "q4_0") {
        std::printf("WARN: tuner sin gate no eligio q4_0 (esperado por velocidad)\n");
        // No es fallo duro: el contraste es ilustrativo.
    }
    if (rc == 0)
        std::printf("OK: gate de calidad respetado; sin gate degrada como se esperaba\n");
    return rc;
}
