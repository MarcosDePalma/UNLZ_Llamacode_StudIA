// Unit tests del AutoTuner (TPE-lite con gate de calidad). C++ puro: la
// evaluación de un candidato se inyecta como callback, sin servidor ni modelo.
// Foco: el gate de calidad evita el colapso al quant más bajo (bug clásico de
// tunear sólo por velocidad).

#include <QtTest>
#include "core/tuner/AutoTuner.h"
#include "core/tuner/TunerEngine.h"

using namespace tuner;

class TunerTests : public QObject
{
    Q_OBJECT
private slots:
    void paramSpec_intRange();
    void paramSpec_categorical();
    void computeLoss_penalizesSubGate();
    void run_qualityGateAvoidsLowestQuant();
    void tunedArgs_emitsSpecDraftNMax();
    void speculativeNMaxParam_usesAdaptiveBounds();
    void tunedArgs_emitsSpecConfMin();
    void tunedArgs_emitsCpuMoe();
    void tunedArgs_emitsSplitMode();
    void canTuneSplitMode_gatesUnsafeLayouts();
    void parsePerplexity_readsLastReportedValue();
    void parseThroughput_splitsPromptAndGen();
    void parseThroughput_derivesFromMsAndCount();
    void parseThroughput_invalidWhenNoTimings();
    void blended_respectsWeightExtremes();
    void blended_fallsBackToMeasuredLeg();
    void promotionGate_requiresMeasuredImprovementAndQuality();
    void padPrompt_reachesTargetAndKeepsInstruction();
};

void TunerTests::paramSpec_intRange()
{
    const ParamSpec p = ParamSpec::intRange("ngl", 0, 100, 25);
    QCOMPARE(p.optionCount(), 5);            // 0,25,50,75,100
    QCOMPARE(p.optionValue(0), std::string("0"));
    QCOMPARE(p.optionValue(4), std::string("100"));
}

void TunerTests::paramSpec_categorical()
{
    const ParamSpec p = ParamSpec::categorical("cache", {"q8_0", "q4_0"}, true);
    QCOMPARE(p.optionCount(), 2);
    QCOMPARE(p.optionValue(1), std::string("q4_0"));
    QVERIFY(p.qualityRisk);
}

void TunerTests::computeLoss_penalizesSubGate()
{
    TunerSettings s; s.qualityGate = 0.7;
    AutoTuner t({ParamSpec::intRange("ngl", 0, 1, 1)}, s);

    TrialResult good; good.throughput = 100; good.quality = 0.8;
    TrialResult fast; fast.throughput = 1000; fast.quality = 0.4;  // viola gate
    // Pese a 10x throughput, romper el gate debe dar PEOR (mayor) loss.
    QVERIFY(t.computeLoss(fast) > t.computeLoss(good));
}

void TunerTests::run_qualityGateAvoidsLowestQuant()
{
    TunerSettings s;
    s.maxTrials = 40; s.startupTrials = 10; s.qualityGate = 0.6; s.seed = 1234;
    std::vector<ParamSpec> space{
        ParamSpec::categorical("cache", {"q8_0", "q4_0"}, true),
    };
    AutoTuner t(space, s);

    // Modelo sintético: el quant más bajo (índice 2 = q4_0) es el más rápido
    // pero su calidad cae por debajo del gate. El óptimo real es q8_0.
    auto eval = [](const Config &c) {
        const int idx = c.at("cache");
        TrialResult r;
        r.throughput = 100.0 + idx * 100.0;          // q4_0 el más rápido
        r.quality = (idx == 1) ? 0.40 : 0.85;        // q4_0 rompe calidad
        return r;
    };

    const Trial best = t.run(eval);
    QVERIFY(best.config.at("cache") != 1);           // NO colapsó al peor quant
    QVERIFY(best.result.quality >= s.qualityGate);
}

// spec-draft-n-max (MTP) como param afinable: el índice elegido se mapea al flag
// de llama-server. intRange(1,5) idx 1 = "2".
void TunerTests::tunedArgs_emitsSpecDraftNMax()
{
    QVector<TunableParam> params{
        {ParamSpec::intRange("spec-draft-n-max", 1, 5, 1), "--spec-draft-n-max", false},
    };
    Config cfg; cfg["spec-draft-n-max"] = 1;  // -> "2"
    const QStringList args = TunerEngine::tunedArgs(params, cfg);
    const int i = args.indexOf("--spec-draft-n-max");
    QVERIFY(i >= 0 && args[i + 1] == "2");
}

void TunerTests::speculativeNMaxParam_usesAdaptiveBounds()
{
    const TunableParam adaptive = TunerEngine::speculativeNMaxParam(true, 3);
    QCOMPARE(adaptive.spec.name, std::string("spec-draft-n-max"));
    QCOMPARE(adaptive.spec.optionCount(), 7); // 3..9 inclusive
    QCOMPARE(adaptive.spec.optionValue(0), std::string("3"));
    QCOMPARE(adaptive.spec.optionValue(4), std::string("7"));
    QCOMPARE(adaptive.flag, QStringLiteral("--spec-draft-n-max"));

    const TunableParam fixed = TunerEngine::speculativeNMaxParam(false);
    QCOMPARE(fixed.spec.optionCount(), 5); // existing non-adaptive range 1..5
    QCOMPARE(fixed.spec.optionValue(0), std::string("1"));
}

void TunerTests::tunedArgs_emitsSpecConfMin()
{
    QVector<TunableParam> params{
        {ParamSpec::categorical("spec-draft-conf-min", {"0", "0.2", "0.4", "0.6"}),
         "--spec-draft-conf-min", false},
    };
    Config cfg; cfg["spec-draft-conf-min"] = 3;
    const QStringList args = TunerEngine::tunedArgs(params, cfg);
    const int i = args.indexOf("--spec-draft-conf-min");
    QVERIFY(i >= 0 && args.value(i + 1) == "0.6");
}

void TunerTests::tunedArgs_emitsCpuMoe()
{
    QVector<TunableParam> params{
        {ParamSpec::categorical("n-cpu-moe", {"31", "35", "39", "43"}),
         "--n-cpu-moe", false},
    };
    Config cfg; cfg["n-cpu-moe"] = 2;
    const QStringList args = TunerEngine::tunedArgs(params, cfg);
    const int i = args.indexOf("--n-cpu-moe");
    QVERIFY(i >= 0 && args.value(i + 1) == QLatin1String("39"));
}

void TunerTests::tunedArgs_emitsSplitMode()
{
    QVector<TunableParam> params{
        {ParamSpec::categorical("split-mode", {"layer", "tensor"}),
         "--split-mode", false},
    };
    Config cfg; cfg["split-mode"] = 1;
    const QStringList args = TunerEngine::tunedArgs(params, cfg);
    const int i = args.indexOf("--split-mode");
    QVERIFY(i >= 0);
    QCOMPARE(args.value(i + 1), QStringLiteral("tensor"));
}

void TunerTests::canTuneSplitMode_gatesUnsafeLayouts()
{
    const QStringList flags{QStringLiteral("--split-mode")};
    QVERIFY(TunerEngine::canTuneSplitMode(2, QStringLiteral("cuda"), flags, {}, false, false));
    QVERIFY(!TunerEngine::canTuneSplitMode(1, QStringLiteral("cuda"), flags, {}, false, false));
    QVERIFY(!TunerEngine::canTuneSplitMode(2, QStringLiteral("vulkan"), flags, {}, false, false));
    QVERIFY(!TunerEngine::canTuneSplitMode(2, QStringLiteral("cuda"), {}, {}, false, false));
    QVERIFY(!TunerEngine::canTuneSplitMode(2, QStringLiteral("cuda"), flags, {}, true, false));
    QVERIFY(!TunerEngine::canTuneSplitMode(2, QStringLiteral("cuda"), flags, {}, false, true));
    QVERIFY(!TunerEngine::canTuneSplitMode(
        2, QStringLiteral("cuda"), flags,
        {QStringLiteral("--override-tensor"), QStringLiteral("blk.*=CUDA0")}, false, false));
}

void TunerTests::parsePerplexity_readsLastReportedValue()
{
    const QByteArray out =
        "llama perplexity benchmark\n"
        "ppl = 12.50\n"
        "Final estimate: PPL = 10.25\n";
    QCOMPARE(TunerEngine::parsePerplexity(out), 10.25);
    QCOMPARE(TunerEngine::parsePerplexity("no metric here"), -1.0);
}

// llama.cpp reporta prefill y generación por separado. Tunear -b/-ub mirando
// sólo la generación mide el efecto secundario: su efecto principal es el
// prefill, así que las dos patas tienen que salir separadas del parseo.
void TunerTests::parseThroughput_splitsPromptAndGen()
{
    const QByteArray body =
        R"({"content":"hi","timings":{"prompt_per_second":812.5,"predicted_per_second":37.2,"draft_n":10,"draft_n_accepted":7}})";
    const ThroughputSample s = TunerEngine::parseThroughput(body);
    QVERIFY(s.valid());
    QCOMPARE(s.promptTps, 812.5);
    QCOMPARE(s.genTps, 37.2);
    QCOMPARE(s.draftTokens, 10);
    QCOMPARE(s.draftAcceptedTokens, 7);
    QCOMPARE(s.draftAcceptancePct(), 70.0);
}

void TunerTests::parseThroughput_derivesFromMsAndCount()
{
    // Sin los *_per_second: derivar de ms + cantidad de tokens.
    const QByteArray body =
        R"({"timings":{"prompt_ms":2000,"prompt_n":1000,"predicted_ms":2000,"predicted_n":100}})";
    const ThroughputSample s = TunerEngine::parseThroughput(body);
    QCOMPARE(s.promptTps, 500.0);
    QCOMPARE(s.genTps, 50.0);
}

void TunerTests::parseThroughput_invalidWhenNoTimings()
{
    QVERIFY(!TunerEngine::parseThroughput("{}").valid());
    QVERIFY(!TunerEngine::parseThroughput("no json").valid());
}

void TunerTests::blended_respectsWeightExtremes()
{
    ThroughputSample s;
    s.promptTps = 800.0;
    s.genTps = 40.0;
    // Peso 0 = comportamiento histórico (sólo TG): sin esto, activar la mezcla
    // cambiaría en silencio el resultado de todo tuning previo.
    QCOMPARE(s.blended(0.0), 40.0);
    QCOMPARE(s.blended(1.0), 800.0);
    QCOMPARE(s.blended(0.5), 420.0);
    // Fuera de rango se recorta en vez de extrapolar.
    QCOMPARE(s.blended(-1.0), 40.0);
    QCOMPARE(s.blended(2.0), 800.0);
}

void TunerTests::blended_fallsBackToMeasuredLeg()
{
    // Si una pata no se midió, el objetivo es la otra: castigar al candidato por
    // una métrica que el server no reportó lo sacaría de la búsqueda por un
    // motivo que no es suyo.
    ThroughputSample onlyGen;
    onlyGen.genTps = 40.0;
    QCOMPARE(onlyGen.blended(1.0), 40.0);

    ThroughputSample onlyPrompt;
    onlyPrompt.promptTps = 800.0;
    QCOMPARE(onlyPrompt.blended(0.0), 800.0);
}

void TunerTests::promotionGate_requiresMeasuredImprovementAndQuality()
{
    TrialResult base;
    base.throughput = 100.0;
    base.quality = 0.9;

    TrialResult equal = base;
    QVERIFY(!TunerEngine::passesPromotionGate(equal, base, 1.0));

    TrialResult faster = base;
    faster.throughput = 101.0;
    QVERIFY(TunerEngine::passesPromotionGate(faster, base, 1.0));

    TrialResult degraded = faster;
    degraded.quality = 0.899;
    QVERIFY(!TunerEngine::passesPromotionGate(degraded, base, 1.0));

    base.failed = true;
    QVERIFY(TunerEngine::passesPromotionGate(degraded, base, 1.0));
}

void TunerTests::padPrompt_reachesTargetAndKeepsInstruction()
{
    const QString instruction = QStringLiteral("Write is_prime. Return only code.");

    // 0 = no rellenar (comportamiento previo intacto).
    QCOMPARE(TunerEngine::padPromptToTokens(instruction, 0), instruction);

    const QString padded = TunerEngine::padPromptToTokens(instruction, 2048);
    // ~4 chars/token: el prefill tiene que quedar en el orden pedido, si no
    // -b/-ub siguen sin señal que optimizar.
    QVERIFY(padded.size() >= 2048 * 4 - 32);
    // La instrucción va al final: los criterios de aceptación se evalúan sobre
    // ella y el relleno no debe sepultarla.
    QVERIFY(padded.endsWith(instruction));
    // Un prompt ya más largo que el objetivo no se toca.
    QCOMPARE(TunerEngine::padPromptToTokens(padded, 8), padded);
}

QTEST_MAIN(TunerTests)
#include "test_tuner.moc"
