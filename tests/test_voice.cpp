#include <QtTest>
#include <QDir>
#include <QTemporaryFile>
#include <QJsonObject>
#include <QJsonDocument>
#include "core/voice/VoiceTypes.h"
#include "core/voice/AudioCodec.h"
#include "core/voice/SttEngine.h"
#include "core/voice/TtsEngine.h"
#include "core/voice/TtsPolicy.h"
#include "core/voice/VoiceAgentPolicy.h"
#include "core/voice/VoiceController.h"
#include "core/voice/VadEngine.h"
#include "core/voice/TurnDetector.h"
#include "core/voice/CharlaTuning.h"
#include "core/voice/VoiceCursorCommand.h"
#include "core/profiles/ProfileTypes.h"
#include "core/voice/VoiceServerManager.h"
#include "core/voice/VoiceLatencyTracker.h"

// Tests del modo Charla. Solo funciones puras (sin micrófono/red/playback):
// round-trip de config, codec WAV/RMS, builders de request STT/TTS, lógica VAD.
class TestVoice : public QObject
{
    Q_OBJECT
private slots:
    void configRoundTrip();
    void turnModeDefaultsAndAliases();
    void cursorCommandParsesOrders();
    void cursorCommandIgnoresConversation();
    void wavHeaderAndExtract();
    void wavPcm16FormatParse();
    void wavPcm16DataRangeParse();
    void rmsLevels();
    void sttMultipart();
    void sttParseTranscript();
    void sttStreamingProtocol();
    void sttStreamingParser();
    void sttNativeParakeet();
    void ttsSpeechBody();
    void ttsPiperJsonLine();
    void ttsPiperResidentArgs();
    void ttsPiperAvailability();
    void ttsQwenArgsAndPolicy();
    void ttsPocketArgsAndPolicy();
    void voiceAgentCapabilityPolicy();
    void vadTurnEnded();
    void vadAdaptiveNoiseFloor();
    void vadHysteresisHangover();
    void vadIgnoresIsolatedClick();
    void vadTuningFromConfig();
    void turnDetectorClassify();
    void turnDetectorSilence();
    void turnFailedIdleIsNoop();
    void charlaTuningRecommendations();
    void ttsSentenceSplit();
    void ttsStreamingSentences();
    void ttsSanitizeForSpeech();
    void latencyPercentiles();
    void voiceInLaunchProfile();
    void sttServerCatalog();
    void voiceBinaryUrls();
    void managedBinaryLookup();
    void ttsVoiceCatalog();
    void ttsVoiceForLang();
};

void TestVoice::ttsQwenArgsAndPolicy()
{
    VoiceConfig c;
    c.qwenModelDir = "C:/models/qwen";
    c.qwenModelName = "qwen-talker-0.6b-base-Q8_0.gguf";
    c.qwenSpeakerEmbedding = "C:/voices/profe.json";
    c.qwenInstruction = "tono docente";
    const QStringList a = TtsEngine::buildQwenArgs(c, "Hola", "out.wav");
    QVERIFY(a.contains("--model-name"));
    QVERIFY(a.contains("--speaker-embedding"));
    QVERIFY(a.contains("--instruct"));

    QCOMPARE(TtsPolicy::recommend(c, 8.0, 32.0, 7.5, true, true)
                 .value("mode").toString(), QString("qwen3"));
    QCOMPARE(TtsPolicy::recommend(c, 8.0, 16.0, 1.0, true, true)
                 .value("mode").toString(), QString("piper"));
    QCOMPARE(TtsPolicy::recommend(c, 0.0, 8.0, 0.0, false, false)
                 .value("mode").toString(), QString("http"));

    c.inflectModelDir = "C:/models/Inflect-Nano-v2-ONNX";
    c.inflectProvider = "directml";
    const QStringList inflect =
        TtsEngine::buildInflectArgs(c, "Hello from Ingi", "voice.wav");
    QVERIFY(inflect.first().endsWith("onnx/inference_onnx.py"));
    QVERIFY(inflect.contains("Hello from Ingi"));
    QVERIFY(inflect.contains("voice.wav"));
    QVERIFY(inflect.contains("directml"));
}

void TestVoice::ttsPocketArgsAndPolicy()
{
    const QStringList args = VoiceServerManager::buildPocketServerArgs(
        "pocket_tts_server.py", "spanish", "C:/voices/ref.wav",
        "C:/models/pocket.toml", 8320, true);
    QVERIFY(args.contains("--language"));
    QVERIFY(args.contains("spanish"));
    QVERIFY(args.contains("--voice"));
    QVERIFY(args.contains("C:/voices/ref.wav"));
    QVERIFY(args.contains("--config"));
    QVERIFY(args.contains("--quantize"));
    QCOMPARE(args.at(0), QStringLiteral("pocket_tts_server.py"));

    VoiceConfig cfg;
    cfg.pocketAutoEnable = true;
    QCOMPARE(TtsPolicy::recommend(cfg, 0.0, 8.0, 0.0, false, false, true)
                 .value("mode").toString(), QStringLiteral("pocket"));
    cfg.pocketAutoEnable = false;
    QCOMPARE(TtsPolicy::recommend(cfg, 0.0, 8.0, 0.0, false, false, true)
                 .value("mode").toString(), QStringLiteral("http"));
}

void TestVoice::voiceAgentCapabilityPolicy()
{
    QVERIFY(!VoiceAgentPolicy::assess("Qwen3.5-2B-Q8.gguf", true, true)
                 .value("trustedForTools").toBool());
    QVERIFY(VoiceAgentPolicy::assess("Qwen3.5-9B-Q5_K_M.gguf", true, false)
                .value("trustedForTools").toBool());
    const QVariantMap moe = VoiceAgentPolicy::assess("Qwen3.6-35B-A3B-Q4.gguf", true, false);
    QVERIFY(moe.value("moe").toBool());
    QCOMPARE(moe.value("activeParamsB").toDouble(), 3.0);
}

void TestVoice::cursorCommandParsesOrders()
{
    using namespace VoiceCursorCommand;
    auto p = [](const char *s) { return parse(QString::fromUtf8(s)); };

    QCOMPARE(p("clic en Guardar").kind, Kind::Click);
    QCOMPARE(p("clic en Guardar").target, QStringLiteral("Guardar"));
    // El STT no pone tildes de forma confiable y dice "click" tanto como "clic".
    QCOMPARE(p("click en Guardar").kind, Kind::Click);
    QCOMPARE(p("clíc en Guardar").kind, Kind::Click);
    QCOMPARE(p("hacer clic en Guardar").kind, Kind::Click);
    QCOMPARE(p("apretar en Aceptar").kind, Kind::Click);

    // Los específicos ganan sobre el genérico "clic".
    QCOMPARE(p("doble clic en Documentos").kind, Kind::DoubleClick);
    QCOMPARE(p("doble clic en Documentos").target, QStringLiteral("Documentos"));
    QCOMPARE(p("clic derecho en Escritorio").kind, Kind::RightClick);
    QCOMPARE(p("clic derecho en Escritorio").target, QStringLiteral("Escritorio"));

    QCOMPARE(p("mover a Guardar").kind, Kind::Move);
    QCOMPARE(p("mové el cursor a Guardar").kind, Kind::Move);
    QCOMPARE(p("mové el cursor a Guardar").target, QStringLiteral("Guardar"));
    // Voseo con tilde: la clase tiene que sustituir la vocal ("mov[ée]"), no
    // sumarse a ella. Es un error de dedo fácil que deja la orden muda.
    QCOMPARE(p("move a Guardar").kind, Kind::Move);
    QCOMPARE(p("llevá el cursor a Aceptar").kind, Kind::Move);
    QCOMPARE(p("llevá el cursor a Aceptar").target, QStringLiteral("Aceptar"));
    QCOMPARE(p("andá a Cancelar").kind, Kind::Move);
    QCOMPARE(p("cursor a Guardar").kind, Kind::Move);

    // El artículo + sustantivo de UI se descartan: en pantalla dice "Guardar".
    QCOMPARE(p("clic en el botón Guardar").target, QStringLiteral("Guardar"));
    QCOMPARE(p("clic en la pestaña Inicio").target, QStringLiteral("Inicio"));
    // Puntuación de cierre del STT.
    QCOMPARE(p("clic en Guardar.").target, QStringLiteral("Guardar"));
    // Destino multi-palabra se preserva entero.
    QCOMPARE(p("clic en Guardar como").target, QStringLiteral("Guardar como"));
}

void TestVoice::cursorCommandIgnoresConversation()
{
    using namespace VoiceCursorCommand;
    auto p = [](const char *s) { return parse(QString::fromUtf8(s)); };

    // LO MÁS IMPORTANTE de este parser: Charla es una CONVERSACIÓN. Mencionar un
    // clic al pasar no puede secuestrar el turno y mover el mouse — tiene que ir
    // al LLM como cualquier otra frase.
    QCOMPARE(p("no sé si hacer clic en Guardar, ¿vos qué opinás?").kind, Kind::None);
    QCOMPARE(p("¿tendría que hacer clic en Guardar?").kind, Kind::None);
    QCOMPARE(p("me explicás qué pasa si hago clic en Guardar").kind, Kind::None);
    QCOMPARE(p("ayer hice clic en Guardar y se colgó").kind, Kind::None);

    // Charla normal, sin verbo de cursor.
    QCOMPARE(p("hola, ¿cómo andás?").kind, Kind::None);
    QCOMPARE(p("guardá el archivo").kind, Kind::None);
    QCOMPARE(p("").kind, Kind::None);

    // Verbo de cursor sin destino: no alcanza para actuar.
    QCOMPARE(p("clic en").kind, Kind::None);
    QCOMPARE(p("clic en el botón").kind, Kind::None);
    QVERIFY(!p("mover a").ok());
}

void TestVoice::configRoundTrip()
{
    VoiceConfig c;
    c.enabled = true;
    c.sttProvider = "cloud";
    c.sttBaseUrl = "https://api.openai.com";
    c.sttModel = "whisper-1";
    c.sttKeyRef = "voice/openai";
    c.sttLanguage = "es";
    c.sttManagedCommand = "python";
    c.sttManagedArgs = {"stt_server.py", "--port", "8081"};
    c.ttsProvider = "local";
    c.ttsVoice = "nova";
    c.ttsManagedCommand = "python";
    c.ttsManagedArgs = {"tts_server.py", "--port", "8082"};
    c.ttsFormat = "mp3";
    c.ttsStreamAudio = true;
    c.ttsPcmSampleRate = 22050;
    c.ttsPcmChannels = 2;
    c.pocketPythonPath = "C:/Python/python.exe";
    c.pocketLanguage = "spanish";
    c.pocketVoice = "lola";
    c.pocketVoicePath = "C:/voices/profe.wav";
    c.pocketModelConfig = "C:/models/pocket-24layer.toml";
    c.pocketPort = 8320;
    c.pocketQuantize = true;
    c.pocketAutoEnable = true;
    c.inflectPythonPath = "C:/Python/python.exe";
    c.inflectModelDir = "C:/models/Inflect-Nano-v2-ONNX";
    c.inflectProvider = "directml";
    c.vadSilenceMs = 1200;
    c.bargeIn = false;
    c.cursorOcr = true;

    const VoiceConfig r = VoiceConfig::fromJson(c.toJson());
    QCOMPARE(r.cursorOcr, true);
    // Default OFF, y un JSON sin la clave NO lo enciende: actualizar la app no
    // puede estrenar la captura de pantalla sin que el usuario la pida.
    QCOMPARE(VoiceConfig().cursorOcr, false);
    QCOMPARE(VoiceConfig::fromJson(QJsonObject{{"enabled", true}}).cursorOcr, false);
    QCOMPARE(r.enabled, true);
    QCOMPARE(r.sttProvider, QString("cloud"));
    QVERIFY(r.sttIsCloud());
    QCOMPARE(r.sttKeyRef, QString("voice/openai"));
    QCOMPARE(r.sttLanguage, QString("es"));
    QCOMPARE(r.sttMode, QString("http_batch"));
    QCOMPARE(r.sttManagedCommand, QString("python"));
    QCOMPARE(r.sttManagedArgs, QStringList({"stt_server.py", "--port", "8081"}));
    QCOMPARE(r.ttsVoice, QString("nova"));
    QCOMPARE(r.ttsManagedCommand, QString("python"));
    QCOMPARE(r.ttsManagedArgs, QStringList({"tts_server.py", "--port", "8082"}));
    QCOMPARE(r.ttsMode, QString("auto"));
    QCOMPARE(r.ttsFormat, QString("mp3"));
    QCOMPARE(r.ttsStreamAudio, true);
    QCOMPARE(r.ttsPcmSampleRate, 22050);
    QCOMPARE(r.ttsPcmChannels, 2);
    QCOMPARE(r.pocketPythonPath, QString("C:/Python/python.exe"));
    QCOMPARE(r.pocketLanguage, QString("spanish"));
    QCOMPARE(r.pocketVoice, QString("lola"));
    QCOMPARE(r.pocketVoicePath, QString("C:/voices/profe.wav"));
    QCOMPARE(r.pocketModelConfig, QString("C:/models/pocket-24layer.toml"));
    QCOMPARE(r.pocketPort, 8320);
    QCOMPARE(r.pocketQuantize, true);
    QCOMPARE(r.pocketAutoEnable, true);
    QCOMPARE(r.inflectPythonPath, QString("C:/Python/python.exe"));
    QCOMPARE(r.inflectModelDir, QString("C:/models/Inflect-Nano-v2-ONNX"));
    QCOMPARE(r.inflectProvider, QString("directml"));
    QCOMPARE(r.vadSilenceMs, 1200);
    QCOMPARE(r.bargeIn, false);
    QVERIFY(!r.ttsIsCloud());
}

void TestVoice::turnModeDefaultsAndAliases()
{
    QCOMPARE(VoiceConfig().turnMode, QStringLiteral("vad"));

    VoiceConfig c;
    c.turnMode = QStringLiteral("push_to_talk");
    const VoiceConfig roundTrip = VoiceConfig::fromJson(c.toJson());
    QCOMPARE(roundTrip.turnMode, QStringLiteral("push_to_talk"));

    // Configs editados a mano pueden usar el alias corto, pero no se propaga
    // internamente una variante no canónica.
    QCOMPARE(VoiceConfig::fromJson(QJsonObject{{"turnMode", "ptt"}}).turnMode,
             QStringLiteral("push_to_talk"));
    QCOMPARE(VoiceConfig::fromJson(QJsonObject{{"turnMode", "otro"}}).turnMode,
             QStringLiteral("vad"));
    QCOMPARE(VoiceController::stateName(VoiceController::Ready),
             QStringLiteral("ready"));
}

void TestVoice::latencyPercentiles()
{
    QCOMPARE(VoiceLatencyTracker::percentile({}, .50), -1);
    const QList<int> values{100, 200, 300, 400, 500};
    QCOMPARE(VoiceLatencyTracker::percentile(values, .50), 300);
    QCOMPARE(VoiceLatencyTracker::percentile(values, .90), 500);
    QCOMPARE(VoiceLatencyTracker::percentile(values, .95), 500);
}

void TestVoice::wavHeaderAndExtract()
{
    QByteArray pcm;
    for (int i = 0; i < 100; ++i) {
        qint16 s = qint16(i * 100);
        pcm.append(char(s & 0xFF));
        pcm.append(char((s >> 8) & 0xFF));
    }
    const QByteArray wav = AudioCodec::pcm16ToWav(pcm, 16000);
    QVERIFY(wav.startsWith("RIFF"));
    QCOMPARE(wav.mid(8, 4), QByteArray("WAVE"));
    QCOMPARE(wav.size(), 44 + pcm.size());      // header de 44 bytes + datos
    // Round-trip: extraer el PCM debe devolver lo original.
    QCOMPARE(AudioCodec::wavExtractPcm(wav), pcm);
    // Entrada no-WAV: se devuelve tal cual.
    QCOMPARE(AudioCodec::wavExtractPcm(pcm), pcm);
}

void TestVoice::wavPcm16FormatParse()
{
    // WAV PCM16 propio: se parsea rate/canales (habilita el path QAudioSink).
    const QByteArray wav = AudioCodec::pcm16ToWav(QByteArray(200, '\1'), 22050, 1);
    int rate = 0, ch = 0;
    QVERIFY(AudioCodec::wavPcm16Format(wav, &rate, &ch));
    QCOMPARE(rate, 22050);
    QCOMPARE(ch, 1);

    // Estéreo 48k.
    const QByteArray wav2 = AudioCodec::pcm16ToWav(QByteArray(400, '\1'), 48000, 2);
    QVERIFY(AudioCodec::wavPcm16Format(wav2, &rate, &ch));
    QCOMPARE(rate, 48000);
    QCOMPARE(ch, 2);

    // No-WAV → false (cae al path QMediaPlayer).
    QVERIFY(!AudioCodec::wavPcm16Format(QByteArray("no soy wav"), &rate, &ch));
    // WAV con formato no-PCM (float=3) → false.
    QByteArray f32 = wav;
    f32[20] = 3; f32[21] = 0;
    QVERIFY(!AudioCodec::wavPcm16Format(f32, &rate, &ch));
    // Header truncado → false, sin crash.
    QVERIFY(!AudioCodec::wavPcm16Format(wav.left(16), &rate, &ch));
}

void TestVoice::wavPcm16DataRangeParse()
{
    QByteArray streamWav = AudioCodec::pcm16ToWav(QByteArray(100, '\1'), 24000, 1);
    // Pocket's streaming header leaves both RIFF and data sizes unknown.
    for (int i = 4; i < 8; ++i) streamWav[i] = char(0xff);
    for (int i = 40; i < 44; ++i) streamWav[i] = char(0xff);
    int rate = 0, channels = 0, offset = -1;
    quint32 size = 0;
    QVERIFY(AudioCodec::wavPcm16DataRange(streamWav, &rate, &channels,
                                          &offset, &size));
    QCOMPARE(rate, 24000);
    QCOMPARE(channels, 1);
    QCOMPARE(offset, 44);
    QCOMPARE(size, quint32(0xffffffffu));
    QVERIFY(!AudioCodec::wavPcm16DataRange(streamWav.left(20), &rate, &channels,
                                           &offset, &size));
}

void TestVoice::rmsLevels()
{
    QByteArray silence(800, '\0');
    QCOMPARE(AudioCodec::rmsBytes(silence), 0.0);

    QByteArray loud;
    for (int i = 0; i < 400; ++i) {
        qint16 s = (i % 2) ? 30000 : -30000;
        loud.append(char(s & 0xFF));
        loud.append(char((s >> 8) & 0xFF));
    }
    QVERIFY(AudioCodec::rmsBytes(loud) > 0.85);
}

void TestVoice::sttMultipart()
{
    const QByteArray wav = AudioCodec::pcm16ToWav(QByteArray(20, '\1'), 16000);
    QByteArray body = SttEngine::buildMultipart("BND", wav, "whisper-1", "es");
    QVERIFY(body.contains("name=\"file\""));
    QVERIFY(body.contains("name=\"model\""));
    QVERIFY(body.contains("whisper-1"));
    QVERIFY(body.contains("name=\"language\""));
    QVERIFY(body.contains("--BND--"));

    // language="auto" no debe emitir el campo language.
    QByteArray body2 = SttEngine::buildMultipart("BND", wav, "whisper-1", "auto");
    QVERIFY(!body2.contains("name=\"language\""));
}

void TestVoice::sttParseTranscript()
{
    QCOMPARE(SttEngine::parseTranscript("{\"text\":\"  hola mundo \"}"), QString("hola mundo"));
    QCOMPARE(SttEngine::parseTranscript("{\"error\":{\"message\":\"x\"}}"), QString());
    QCOMPARE(SttEngine::parseTranscript("garbage"), QString());
}

void TestVoice::sttStreamingProtocol()
{
    const QByteArray config = SttEngine::buildStreamingConfig(16000, "es", "parakeet");
    QVERIFY(config.endsWith('\n'));
    const QJsonObject cfg = QJsonDocument::fromJson(config.trimmed()).object();
    QCOMPARE(cfg.value("type").toString(), QString("config"));
    QCOMPARE(cfg.value("sample_rate").toInt(), 16000);
    QCOMPARE(cfg.value("language").toString(), QString("es"));
    QCOMPARE(cfg.value("model").toString(), QString("parakeet"));

    const QByteArray audio = SttEngine::buildStreamingAudio(QByteArray("\1\2\3\4"), 7);
    const QJsonObject packet = QJsonDocument::fromJson(audio.trimmed()).object();
    QCOMPARE(packet.value("type").toString(), QString("audio"));
    QCOMPARE(packet.value("sequence").toInteger(), qint64(7));
    QCOMPARE(QByteArray::fromBase64(packet.value("pcm16_base64").toString().toLatin1()),
             QByteArray("\1\2\3\4"));
    QCOMPARE(SttEngine::buildStreamingEnd(), QByteArray("{\"type\":\"end\"}\n"));
    QCOMPARE(SttEngine::buildStreamingCancel(), QByteArray("{\"type\":\"cancel\"}\n"));
}

void TestVoice::sttNativeParakeet()
{
    const QStringList args = SttEngine::buildNativeParakeetArgs(
        "parakeet.bin", "audio.wav", 8);
    QCOMPARE(args.at(0), QStringLiteral("-m"));
    QCOMPARE(args.at(1), QStringLiteral("parakeet.bin"));
    QCOMPARE(args.at(2), QStringLiteral("-f"));
    QCOMPARE(args.at(3), QStringLiteral("audio.wav"));
    QVERIFY(args.contains(QStringLiteral("-ng")));
    QVERIFY(args.contains(QStringLiteral("-t")));
    QVERIFY(args.contains(QStringLiteral("8")));

    const QByteArray output =
        "Loading Parakeet model\nText, abrir el navegador.\nparakeet_print_timings: total";
    QCOMPARE(SttEngine::parseNativeParakeetTranscript(output),
             QStringLiteral("abrir el navegador."));
    QCOMPARE(SttEngine::parseNativeParakeetTranscript("sin texto"), QString());
}

void TestVoice::sttStreamingParser()
{
    const QVariantMap partial = SttEngine::parseStreamingMessage(
        "{\"type\":\"partial\",\"text\":\"hola\"}");
    QCOMPARE(partial.value("type").toString(), QString("partial"));
    QCOMPARE(partial.value("text").toString(), QString("hola"));
    QCOMPARE(SttEngine::parseStreamingMessage("garbage"), QVariantMap());
    QCOMPARE(SttEngine::parseStreamingMessage("{\"text\":\"sin tipo\"}"), QVariantMap());

    const QVariantMap upper = SttEngine::parseStreamingMessage(
        "{\"type\":\"FINAL\",\"text\":\"listo\"}");
    QCOMPARE(upper.value("type").toString(), QString("final"));
}

void TestVoice::ttsSpeechBody()
{
    const QByteArray body = TtsEngine::buildSpeechBody("tts-1", "alloy", "hola", "wav");
    const QJsonObject o = QJsonDocument::fromJson(body).object();
    QCOMPARE(o.value("model").toString(), QString("tts-1"));
    QCOMPARE(o.value("voice").toString(), QString("alloy"));
    QCOMPARE(o.value("input").toString(), QString("hola"));
    QCOMPARE(o.value("response_format").toString(), QString("wav"));
}

void TestVoice::ttsPiperJsonLine()
{
    // Línea JSON para piper residente (--json-input): un objeto por línea, con
    // text + output_file, terminado en '\n'.
    const QByteArray line = TtsEngine::buildPiperJsonLine("hola mundo", "C:/tmp/a.wav");
    QVERIFY(line.endsWith('\n'));
    const QJsonObject o = QJsonDocument::fromJson(line.trimmed()).object();
    QCOMPARE(o.value("text").toString(), QString("hola mundo"));
    QCOMPARE(o.value("output_file").toString(), QString("C:/tmp/a.wav"));

    // Caracteres especiales se escapan (sigue siendo una sola línea JSON válida).
    const QByteArray line2 = TtsEngine::buildPiperJsonLine("a\nb \"x\"", "o.wav");
    QCOMPARE(line2.count('\n'), 1);   // solo el terminador
    const QJsonObject o2 = QJsonDocument::fromJson(line2.trimmed()).object();
    QCOMPARE(o2.value("text").toString(), QString("a\nb \"x\""));
}

void TestVoice::ttsPiperResidentArgs()
{
    // Args del piper residente: -m <model> --json-input --output_dir <dir>.
    const QStringList a = VoiceServerManager::buildPiperResidentArgs("v.onnx", "C:/tmp");
    QVERIFY(a.contains("-m")); QVERIFY(a.contains("v.onnx"));
    QVERIFY(a.contains("--json-input"));
    const int idx = a.indexOf("--output_dir");
    QVERIFY(idx >= 0);
    QCOMPARE(a.value(idx + 1), QString("C:/tmp"));
    // No usa -f (eso es el modo per-call/fallback).
    QVERIFY(!a.contains("-f"));
}

void TestVoice::ttsPiperAvailability()
{
    // Fallback HTTP→piper: piperAvailable() refleja si el modelo .onnx existe.
    TtsEngine eng;
    const QString missing = QDir::temp().filePath("lc-no-such-voice.onnx");
    QFile::remove(missing);
    eng.setPiper(QString(), missing);
    QVERIFY(!eng.piperAvailable());

    QTemporaryFile voice(QDir::temp().filePath("lc-voice-XXXXXX.onnx"));
    QVERIFY(voice.open());
    eng.setPiper(QString(), voice.fileName());
    QVERIFY(eng.piperAvailable());
}

void TestVoice::turnFailedIdleIsNoop()
{
    // Con la charla inactiva (Idle), un error de backend no debe activar nada
    // ni tocar lastError (evita "escuchando" fantasma fuera de la charla).
    VoiceController vc;
    QCOMPARE(vc.state(), VoiceController::Idle);
    vc.notifyTurnFailed(QStringLiteral("server caído"));
    QCOMPARE(vc.state(), VoiceController::Idle);
    QVERIFY(vc.lastError().isEmpty());
}

void TestVoice::charlaTuningRecommendations()
{
    // Perfil estilo MAX-Q: ubatch 64, ctx 262k, predict 4096, con cache-reuse.
    const QStringList maxq{
        "--ctx-size", "262000", "--batch-size", "512", "--ubatch-size", "64",
        "--cache-reuse", "512", "--predict", "4096"};
    const auto recs = CharlaTuning::recommend(maxq, 24.0);
    QStringList flags;
    for (const auto &c : recs) flags << c.flag;
    QVERIFY(flags.contains("--ubatch-size"));   // 64 → 512
    QVERIFY(flags.contains("--batch-size"));    // 512 → 2048
    QVERIFY(flags.contains("--ctx-size"));      // 262k → 32768
    QVERIFY(flags.contains("--predict"));       // 4096 → 1024
    QVERIFY(!flags.contains("--cache-reuse"));  // ya lo tiene

    // apply: reemplaza valores existentes sin duplicar flags.
    const QStringList tuned = CharlaTuning::apply(maxq, recs);
    QCOMPARE(CharlaTuning::argValue(tuned, "--ubatch-size"), QString("512"));
    QCOMPARE(CharlaTuning::argValue(tuned, "--ctx-size"), QString("32768"));
    QCOMPARE(tuned.count("--ubatch-size"), 1);
    // Args tuneados → sin nuevas recomendaciones (no re-pregunta en loop).
    QVERIFY(CharlaTuning::recommend(tuned, 24.0).isEmpty());

    // VRAM chica (2GB): no subir batches (compute buffers no entran); pero
    // cache-reuse ausente y predict sin tope sí se recomiendan.
    const QStringList tiny{"--ctx-size", "3072", "--batch-size", "64", "--ubatch-size", "64"};
    const auto recsTiny = CharlaTuning::recommend(tiny, 2.0);
    QStringList tinyFlags;
    for (const auto &c : recsTiny) tinyFlags << c.flag;
    QVERIFY(!tinyFlags.contains("--ubatch-size"));
    QVERIFY(!tinyFlags.contains("--batch-size"));
    QVERIFY(tinyFlags.contains("--cache-reuse"));
    QVERIFY(tinyFlags.contains("--predict"));

    // Perfil ya óptimo para voz: nada que recomendar (no molesta con popup).
    const QStringList good{
        "--ctx-size", "32768", "--batch-size", "2048", "--ubatch-size", "512",
        "--cache-reuse", "512", "--predict", "1024"};
    QVERIFY(CharlaTuning::recommend(good, 24.0).isEmpty());
}

void TestVoice::vadTurnEnded()
{
    // Sin voz (peak bajo): nunca termina aunque haya silencio.
    QVERIFY(!VoiceController::turnEnded(0.01, 0.03, 2000, 800));
    // Hubo voz pero el silencio aún no llega al umbral.
    QVERIFY(!VoiceController::turnEnded(0.10, 0.03, 500, 800));
    // Hubo voz y silencio suficiente → fin de turno.
    QVERIFY(VoiceController::turnEnded(0.10, 0.03, 900, 800));
}

// El piso de ruido se calibra solo: un mic ruidoso (RMS de fondo 0.02, por
// encima del vadThreshold fijo de 0.012) NO es voz para el VAD adaptativo, pero
// sí lo era para el umbral fijo — ahí el turno no cerraba nunca.
void TestVoice::vadAdaptiveNoiseFloor()
{
    VadEngine vad;
    for (int i = 0; i < 100; ++i) vad.push(0.02, 20);
    QVERIFY(!vad.speaking());
    QVERIFY(!vad.sawSpeech());
    QVERIFY(qAbs(vad.noiseFloor() - 0.02) < 0.005);

    // El umbral fijo viejo (0.012) habría dado voz con este mismo ruido.
    QVERIFY(0.02 >= VoiceConfig().vadThreshold);

    // Voz real (3× el piso): abre tras onsetFrames.
    vad.push(0.10, 20);
    vad.push(0.10, 20);
    QVERIFY(vad.speaking());
    QVERIFY(vad.sawSpeech());

    // Con mic bajo el piso también se adapta: 0.006 de fondo y voz a 0.025
    // (por debajo del vadActivationLevel fijo de 0.03) igual se detecta.
    VadEngine quiet;
    for (int i = 0; i < 100; ++i) quiet.push(0.002, 20);
    quiet.push(0.025, 20);
    quiet.push(0.025, 20);
    QVERIFY(quiet.speaking());
    QVERIFY(0.025 < VoiceConfig().vadActivationLevel);
}

// Histéresis: se sale de voz por debajo de offFactor×piso, no de onFactor×piso.
// Hangover: la voz se sostiene hangoverMs tras el último frame fuerte.
void TestVoice::vadHysteresisHangover()
{
    VadTuning t;
    t.hangoverMs = 200;
    VadEngine vad(t);
    for (int i = 0; i < 50; ++i) vad.push(0.01, 20);  // piso ≈ 0.01
    vad.push(0.10, 20); vad.push(0.10, 20);
    QVERIFY(vad.speaking());

    // Nivel entre offTh (0.018) y onTh (0.03): sigue siendo voz (histéresis),
    // el hangover se recarga y no cae por más frames que pasen.
    for (int i = 0; i < 50; ++i) vad.push(0.025, 20);
    QVERIFY(vad.speaking());

    // Silencio real: aguanta el hangover (200 ms = 10 frames) y recién ahí corta.
    for (int i = 0; i < 5; ++i) vad.push(0.005, 20);
    QVERIFY(vad.speaking());
    for (int i = 0; i < 8; ++i) vad.push(0.005, 20);
    QVERIFY(!vad.speaking());
    QVERIFY(vad.sawSpeech());   // el turno igual recuerda que hubo voz
}

// Un frame fuerte aislado (golpe de teclado) no abre turno ni ensucia el piso.
void TestVoice::vadIgnoresIsolatedClick()
{
    VadEngine vad;
    for (int i = 0; i < 50; ++i) vad.push(0.01, 20);
    const double floorBefore = vad.noiseFloor();

    vad.push(0.6, 20);            // click: 1 frame, onsetFrames exige 2
    QVERIFY(!vad.speaking());
    QVERIFY(!vad.sawSpeech());
    QCOMPARE(vad.noiseFloor(), floorBefore);   // el pico no entra al piso

    vad.push(0.01, 20);
    vad.push(0.6, 20);            // otro click aislado: la racha se reinició
    QVERIFY(!vad.speaking());

    // reset() borra el piso y el "hubo voz".
    vad.push(0.6, 20);
    QVERIFY(vad.speaking());
    vad.reset();
    QVERIFY(!vad.speaking());
    QVERIFY(!vad.sawSpeech());
}

void TestVoice::vadTuningFromConfig()
{
    VoiceConfig c;
    c.vadThreshold = 0.012;
    c.vadSegmentMs = 350;
    const VadTuning t = VoiceController::vadTuningFor(c);
    // vadThreshold pasa a piso absoluto (mitad: es suelo, no umbral).
    QCOMPARE(t.absFloor, 0.006);
    QVERIFY(t.offFactor < t.onFactor);          // histéresis
    QVERIFY(t.hangoverMs <= c.vadSegmentMs / 2); // no se traga el corte de segmento

    // Segmentos cortos recortan el hangover.
    c.vadSegmentMs = 100;
    QVERIFY(VoiceController::vadTuningFor(c).hangoverMs <= 50);
}

void TestVoice::turnDetectorClassify()
{
    using namespace TurnDetector;
    // Terminadores → cerrado.
    QCOMPARE(classify(QStringLiteral("dale, hacelo.")), Complete);
    QCOMPARE(classify(QStringLiteral("¿cuánto falta?")), Complete);
    // Sin puntuación (whisper tiny no puntúa) pero frase plena → cerrado.
    QCOMPARE(classify(QStringLiteral("abrime el log de ayer")), Complete);

    // Colgado: conjunción, preposición, artículo, muletilla, coma.
    QCOMPARE(classify(QStringLiteral("quiero que busques el log y")), Incomplete);
    QCOMPARE(classify(QStringLiteral("compilá esto para")), Incomplete);
    QCOMPARE(classify(QStringLiteral("mirá el")), Incomplete);
    QCOMPARE(classify(QStringLiteral("necesito que revises eh")), Incomplete);
    QCOMPARE(classify(QStringLiteral("primero el build,")), Incomplete);
    QCOMPARE(classify(QStringLiteral("open the file and")), Incomplete);
    // Acentos normalizados contra la lista.
    QCOMPARE(classify(QStringLiteral("ordená la lista según")), Incomplete);

    // Ambiguo: vacío o una sola palabra plena.
    QCOMPARE(classify(QString()), Unknown);
    QCOMPARE(classify(QStringLiteral("   ")), Unknown);
    QCOMPARE(classify(QStringLiteral("compilá")), Unknown);
}

void TestVoice::turnDetectorSilence()
{
    using namespace TurnDetector;
    const int base = 800;
    // Sin parcial todavía → el base sin tocar.
    QCOMPARE(requiredSilenceMs(QString(), base), base);
    // Una palabra suelta: ambiguo → base.
    QCOMPARE(requiredSilenceMs(QStringLiteral("compilá"), base), base);

    // Frase cerrada → corta antes (menos latencia).
    const int done = requiredSilenceMs(QStringLiteral("dale, hacelo."), base);
    QVERIFY(done < base);
    QVERIFY(done >= EndpointTuning().minMs);

    // Frase colgada → espera más (no cortar a la persona pensando).
    const int hang = requiredSilenceMs(QStringLiteral("quiero que busques el log y"), base);
    QVERIFY(hang > base);
    QVERIFY(hang <= EndpointTuning().maxMs);

    // Clamps: base absurdo no se va de rango.
    QVERIFY(requiredSilenceMs(QStringLiteral("listo."), 100) <= 100);
    QVERIFY(requiredSilenceMs(QStringLiteral("y"), 9000) <= EndpointTuning().maxMs);
    QCOMPARE(requiredSilenceMs(QStringLiteral("listo."), 0), 0);
}

void TestVoice::ttsSentenceSplit()
{
    // Texto corto (< minLen): un solo chunk.
    QCOMPARE(VoiceController::splitSentences("Hola.", 40),
             QStringList{QStringLiteral("Hola.")});

    // Varias oraciones largas: se separan por cierre (.!?) superando minLen.
    const QString t = QStringLiteral(
        "Voy a abrir el navegador ahora mismo para vos. "
        "Después busco la página que pediste. ¿Te parece bien así?");
    const QStringList parts = VoiceController::splitSentences(t, 20);
    QCOMPARE(parts.size(), 3);
    QVERIFY(parts.first().startsWith("Voy a abrir"));
    QVERIFY(parts.last().endsWith("?"));

    // Fragmentos cortos se acumulan hasta superar minLen (no spawnea por "Sí.").
    // Ningún cierre intermedio alcanza minLen → todo queda en un solo chunk.
    const QStringList merged = VoiceController::splitSentences(
        QStringLiteral("Sí. No. Tal vez. Lo voy a pensar con calma un rato."), 30);
    QCOMPARE(merged.size(), 1);

    // Solo espacios → sin chunks. Texto sin puntuación final → un chunk.
    QVERIFY(VoiceController::splitSentences("   ", 40).isEmpty());
    QCOMPARE(VoiceController::splitSentences("texto sin puntuacion final largo", 40).size(), 1);
}

void TestVoice::ttsStreamingSentences()
{
    // Solo emite oraciones YA cerradas; el fragmento incompleto queda sin consumir.
    int consumed = -1;
    QStringList s = VoiceController::splitCompleteSentences(
        QStringLiteral("Voy a abrir el navegador ahora. Después busc"), 20, &consumed);
    QCOMPARE(s, QStringList{QStringLiteral("Voy a abrir el navegador ahora.")});
    // consumed apunta justo después de la oración cerrada; el resto se reacumula.
    QCOMPARE(QStringLiteral("Voy a abrir el navegador ahora. Después busc").mid(consumed),
             QStringLiteral(" Después busc"));

    // Sin ningún cierre que supere minLen → nada emitido, consumed 0.
    consumed = -1;
    QVERIFY(VoiceController::splitCompleteSentences(QStringLiteral("hola"), 20, &consumed).isEmpty());
    QCOMPARE(consumed, 0);

    // Simulación de streaming incremental: alimentar prefijos crecientes y juntar
    // lo emitido debe reconstruir el texto sin duplicar ni perder oraciones.
    const QString finalText = QStringLiteral(
        "Abro el navegador para vos. Busco la página pedida. ¿Algo más?");
    const QList<int> cuts = {10, 28, 45, 55, int(finalText.size())};
    int off = 0;                      // chars ya consumidos (como m_streamConsumed)
    QStringList spoken;
    for (int cut : cuts) {
        const QString accumulated = finalText.left(cut);   // texto recibido hasta ahora
        int c = 0;
        spoken += VoiceController::splitCompleteSentences(accumulated.mid(off), 20, &c);
        off += c;
    }
    // Cola final (sin terminador "?" cerrado por minLen quizá) se encola al flush.
    const QString tail = finalText.mid(off).trimmed();
    if (!tail.isEmpty()) spoken += VoiceController::splitSentences(tail);
    // Reconstrucción: las dos primeras oraciones y la pregunta final, en orden.
    QCOMPARE(spoken.size(), 3);
    QVERIFY(spoken.at(0).startsWith("Abro el navegador"));
    QVERIFY(spoken.at(1).startsWith("Busco la página"));
    QVERIFY(spoken.last().endsWith("?"));

    // Primera oración del turno: umbral corto (12) → arranca a hablar con una
    // confirmación breve sin esperar 40 chars.
    consumed = 0;
    s = VoiceController::splitCompleteSentences(
        QStringLiteral("Ok, lo hago. Ahora abro el nav"), 12, &consumed);
    QCOMPARE(s, QStringList{QStringLiteral("Ok, lo hago.")});
}

void TestVoice::ttsSanitizeForSpeech()
{
    // Bloque <think> cerrado: se quita, queda solo la respuesta.
    QCOMPARE(VoiceController::sanitizeForSpeech(
                 QStringLiteral("<think>razono esto</think>\nHola, abro el navegador.")),
             QStringLiteral("Hola, abro el navegador."));
    // <think> abierto sin cerrar (razonamiento en vuelo): cortar desde ahí.
    QCOMPARE(VoiceController::sanitizeForSpeech(
                 QStringLiteral("Dale. <think>todavía pensando")),
             QStringLiteral("Dale."));
    // Indicador transitorio de tool ⏳: se descarta esa línea, queda el texto.
    QCOMPARE(VoiceController::sanitizeForSpeech(
                 QStringLiteral("Abro el navegador.\n⏳ preparando `open_url`… (~12 tokens generados)")),
             QStringLiteral("Abro el navegador."));
    // Texto limpio pasa intacto (modo chat sin thinking).
    QCOMPARE(VoiceController::sanitizeForSpeech(QStringLiteral("Listo, ya está.")),
             QStringLiteral("Listo, ya está."));
}

void TestVoice::voiceInLaunchProfile()
{
    // La config de voz viaja dentro del LaunchProfile (round-trip JSON).
    LaunchProfile p;
    p.id = "lp1"; p.name = "1_test";
    p.voice.sttProvider = "cloud";
    p.voice.sttBaseUrl = "https://stt.example";
    p.voice.ttsVoice = "nova";
    p.voice.vadSegmentMs = 420;
    const LaunchProfile r = LaunchProfile::fromJson(p.toJson());
    QCOMPARE(r.voice.sttProvider, QString("cloud"));
    QCOMPARE(r.voice.sttBaseUrl, QString("https://stt.example"));
    QCOMPARE(r.voice.ttsVoice, QString("nova"));
    QCOMPARE(r.voice.vadSegmentMs, 420);
}

void TestVoice::sttServerCatalog()
{
    const QVariantList cat = VoiceServerManager::sttCatalog();
    QVERIFY(cat.size() >= 1);
    // El motor base existe y es whisper.cpp (endpoint /inference).
    const QVariantMap base = VoiceServerManager::sttEngine("whisper-base");
    QVERIFY(!base.isEmpty());
    QCOMPARE(VoiceServerManager::endpointPath("whisper-base"), QString("/inference"));
    QVERIFY(VoiceServerManager::modelPath("whisper-base").endsWith("ggml-base.bin"));
    QVERIFY(VoiceServerManager::sttEngine("inexistente").isEmpty());

    const QVariantMap parakeet = VoiceServerManager::sttEngine("parakeet-tdt-0.6b-v3");
    QVERIFY(!parakeet.isEmpty());
    QCOMPARE(parakeet.value("transport").toString(), QString("process_batch"));
    QVERIFY(!parakeet.value("requiresCommand").toBool());
    QVERIFY(parakeet.value("installable").toBool());
    QVERIFY(!parakeet.value("docsUrl").toString().isEmpty());
    QVERIFY(VoiceServerManager::modelPath("parakeet-tdt-0.6b-v3")
                .endsWith("ggml-parakeet-tdt-0.6b-v3-q4_0.bin"));

    // Args de whisper-server: modelo + host + port, y -l solo si lang != auto.
    QStringList a = VoiceServerManager::buildWhisperArgs("m.bin", "127.0.0.1", 8081, "auto");
    QVERIFY(a.contains("-m")); QVERIFY(a.contains("m.bin"));
    QVERIFY(a.contains("--port")); QVERIFY(a.contains("8081"));
    QVERIFY(!a.contains("-l"));
    QStringList b = VoiceServerManager::buildWhisperArgs("m.bin", "127.0.0.1", 8081, "es");
    QVERIFY(b.contains("-l")); QVERIFY(b.contains("es"));
}

void TestVoice::voiceBinaryUrls()
{
#ifdef Q_OS_WIN
    const QString whisperUrl = VoiceServerManager::defaultBinaryUrl("whisper-server");
    QVERIFY(whisperUrl.startsWith("https://github.com/ggml-org/whisper.cpp/"));
    QVERIFY(whisperUrl.contains("/releases/latest/download/"));
    QVERIFY(whisperUrl.endsWith("/whisper-bin-x64.zip"));
    const QString piperUrl = VoiceServerManager::defaultBinaryUrl("piper");
    QVERIFY(piperUrl.startsWith("https://github.com/rhasspy/piper/releases/download/"));
    QVERIFY(piperUrl.endsWith("/piper_windows_amd64.zip"));
#endif
}

void TestVoice::managedBinaryLookup()
{
    const QString path = VoiceServerManager::installedBinaryPath("unknown-kind");
    QVERIFY(path.isEmpty());
}

void TestVoice::ttsVoiceCatalog()
{
    const QVariantList cat = VoiceServerManager::ttsCatalog();
    QVERIFY(cat.size() >= 1);
    const QVariantMap v = VoiceServerManager::ttsVoice("es_ES-davefx-medium");
    QVERIFY(!v.isEmpty());
    QCOMPARE(v.value("lang").toString(), QString("es"));
    QVERIFY(VoiceServerManager::ttsModelPath("es_ES-davefx-medium").endsWith(".onnx"));
    QVERIFY(VoiceServerManager::ttsVoice("nope").isEmpty());

    // piper args: -m model -f out.
    const QStringList a = VoiceServerManager::buildPiperArgs("v.onnx", "out.wav");
    QVERIFY(a.contains("-m")); QVERIFY(a.contains("v.onnx"));
    QVERIFY(a.contains("-f")); QVERIFY(a.contains("out.wav"));
}

void TestVoice::ttsVoiceForLang()
{
    // Voz piper por idioma de la app: es→española, en→inglesa, idioma sin voz o
    // vacío → cae a la española base.
    QCOMPARE(VoiceServerManager::defaultTtsVoiceForLang("es"), QString("es_ES-davefx-medium"));
    QCOMPARE(VoiceServerManager::defaultTtsVoiceForLang("en"), QString("en_US-amy-medium"));
    QCOMPARE(VoiceServerManager::defaultTtsVoiceForLang("zh"), QString("es_ES-davefx-medium"));
    QCOMPARE(VoiceServerManager::defaultTtsVoiceForLang(QString()), QString("es_ES-davefx-medium"));
}

QTEST_MAIN(TestVoice)
#include "test_voice.moc"
