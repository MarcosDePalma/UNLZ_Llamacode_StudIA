// Selftest de TunerEngine sin modelo real. Levanta un mock HTTP (QTcpServer) que
// imita las respuestas de llama-server (/health 200, /completion con timings +
// content) y verifica las primitivas puras + la medición HTTP real.
//
// Build: ver tools\build_tuner_tests.ps1   (exit 0 = OK)

#include "core/tuner/TunerEngine.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <cstdio>

using namespace tuner;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ std::printf("FAIL: %s\n", msg); ++g_fail; } } while(0)

// Mock que responde como llama-server. predicted_per_second fijo; content con
// "is_prime" para que el scoring de aceptación dé 1.0.
class MockServer {
public:
    explicit MockServer(QTcpServer *srv) {
        QObject::connect(srv, &QTcpServer::newConnection, [srv]() {
            while (QTcpSocket *s = srv->nextPendingConnection()) {
                QObject::connect(s, &QTcpSocket::readyRead, [s]() {
                    const QByteArray req = s->readAll();
                    QByteArray bodyJson;
                    if (req.contains("/completion")) {
                        bodyJson = R"({"content":"def is_prime(n): return n>1",)"
                                   R"("tokens_predicted":120,)"
                                   R"("timings":{"predicted_per_second":42.5,)"
                                   R"("predicted_ms":2823.5,"predicted_n":120}})";
                    } else {
                        bodyJson = R"({"status":"ok"})";  // /health
                    }
                    QByteArray resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
                    resp += "Content-Length: " + QByteArray::number(bodyJson.size()) + "\r\n";
                    resp += "Connection: close\r\n\r\n";
                    resp += bodyJson;
                    s->write(resp);
                    s->flush();
                    s->disconnectFromHost();
                });
            }
        });
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ── Primitivas puras ──────────────────────────────────────────────────────
    QVector<TunableParam> params = {
        {ParamSpec::intRange("ngl", 0, 40, 8), "-ngl", false},
        {ParamSpec::categorical("cache-type-k", {"q8_0", "q4_0"}, true),
         "--cache-type-k", false},
        {ParamSpec::categorical("flash-attn", {"off", "on"}), "--flash-attn", true},
    };
    Config cfg;
    cfg["ngl"] = 5;            // 0 + 5*8 = 40
    cfg["cache-type-k"] = 1;   // q8_0
    cfg["flash-attn"] = 1;     // on -> flag presente

    QStringList args = TunerEngine::composeArgs({"-m", "model.gguf"}, params, cfg,
                                                "127.0.0.1", 18080);
    CHECK(args.contains("-ngl") && args.at(args.indexOf("-ngl") + 1) == "40",
          "composeArgs ngl=40");
    CHECK(args.contains("--cache-type-k") &&
          args.at(args.indexOf("--cache-type-k") + 1) == "q8_0",
          "composeArgs cache-type-k=q8_0");
    CHECK(args.contains("--flash-attn"), "composeArgs flash-attn switch presente");
    {
        const int fi = args.indexOf("--flash-attn");
        // switch no inserta valor: lo que sigue es otro flag (empieza con '-')
        CHECK(fi + 1 < args.size() && args.at(fi + 1).startsWith('-'),
              "composeArgs flash-attn sin valor");
    }
    CHECK(args.contains("--port") && args.at(args.indexOf("--port") + 1) == "18080",
          "composeArgs port");

    // flash-attn off -> flag ausente
    Config cfg2 = cfg; cfg2["flash-attn"] = 0;
    QStringList args2 = TunerEngine::composeArgs({}, params, cfg2, "127.0.0.1", 1);
    CHECK(!args2.contains("--flash-attn"), "composeArgs flash-attn off ausente");

    const QByteArray sample =
        R"({"content":"hi","timings":{"predicted_per_second":37.2}})";
    CHECK(qAbs(TunerEngine::parseThroughput(sample).genTps - 37.2) < 0.01,
          "parseThroughput predicted_per_second");
    const QByteArray sampleFallback =
        R"({"timings":{"predicted_ms":2000,"predicted_n":100}})";
    CHECK(qAbs(TunerEngine::parseThroughput(sampleFallback).genTps - 50.0) < 0.01,
          "parseThroughput fallback ms/n");
    CHECK(!TunerEngine::parseThroughput("{}").valid(), "parseThroughput sin datos = invalido");

    CHECK(qAbs(TunerEngine::scoreQuality("hay def is_prime aca", {"is_prime", "def"}) - 1.0) < 0.01,
          "scoreQuality 2/2");
    CHECK(qAbs(TunerEngine::scoreQuality("solo def aca", {"is_prime", "def"}) - 0.5) < 0.01,
          "scoreQuality 1/2");
    CHECK(qAbs(TunerEngine::scoreQuality("loquesea", {}) - 1.0) < 0.01,
          "scoreQuality sin criterios = 1.0");

    // tunedArgs: solo flags afinados, sin host/port (para extraArgs del perfil)
    QStringList tuned = TunerEngine::tunedArgs(params, cfg);
    CHECK(!tuned.contains("--host") && !tuned.contains("--port"),
          "tunedArgs sin host/port");
    CHECK(tuned.contains("-ngl") && tuned.contains("--flash-attn"),
          "tunedArgs incluye flags afinados");

    // ── Medición HTTP real contra mock ─────────────────────────────────────────
    QTcpServer srv;
    bool listening = srv.listen(QHostAddress::LocalHost, 0);
    CHECK(listening, "mock server listen");
    MockServer mock(&srv);
    const QString baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(srv.serverPort());

    TunerEngine engine;
    TrialResult r = engine.evaluateAgainstUrl(baseUrl,
        "Write is_prime", 128, {"is_prime"}, 10000);
    CHECK(!r.failed, "evaluateAgainstUrl no falla");
    CHECK(qAbs(r.throughput - 42.5) < 0.01, "evaluateAgainstUrl throughput=42.5");
    CHECK(qAbs(r.quality - 1.0) < 0.01, "evaluateAgainstUrl quality=1.0 (is_prime)");

    if (g_fail == 0)
        std::printf("OK: TunerEngine primitivas + medicion HTTP correctas\n");
    return g_fail == 0 ? 0 : 1;
}
