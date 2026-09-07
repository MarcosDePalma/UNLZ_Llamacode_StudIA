#pragma once
#include <QString>
#include <QStringList>
#include <QJsonObject>

// Config del modo "Charla" (voz-a-voz). STT batch y TTS van por endpoints
// OpenAI-compatibles (/v1/audio/transcriptions y /v1/audio/speech); STT streaming
// o nativo batch puede usar un proceso administrado según sttMode. La única
// diferencia entre 100% local y cloud es la baseUrl + si requiere API key:
//   - local: baseUrl http://127.0.0.1:<puerto> de un server local
//            (whisper.cpp server, openedai-speech, piper-http...). Sin key.
//   - cloud: baseUrl remota (OpenAI/Groq/...) + keyRef resuelto vía SecretStore.
// keyRef es una *referencia* al secreto (nunca la key en claro en el JSON).
struct VoiceConfig {
    bool enabled = false;

    // ── STT (speech-to-text) ──
    QString sttProvider = QStringLiteral("local");  // local | cloud
    QString sttBaseUrl  = QStringLiteral("http://127.0.0.1:8081");
    QString sttModel    = QStringLiteral("whisper-1");
    QString sttKeyRef;                               // "" salvo cloud
    QString sttLanguage = QStringLiteral("es");      // "auto" = no enviar param
    // Transporte STT: `http_batch` conserva el contrato OpenAI-compatible;
    // `stream_process` usa un sidecar persistente con protocolo NDJSON v1;
    // `process_batch` usa un CLI nativo administrado por la app (Parakeet).
    QString sttMode = QStringLiteral("http_batch");
    // Path del endpoint de transcripción. OpenAI/openedai-speech usan
    // "/v1/audio/transcriptions"; whisper.cpp server usa "/inference".
    QString sttEndpointPath = QStringLiteral("/v1/audio/transcriptions");
    // Servidor gestionado por la app: si != "", al iniciar Charla se descarga
    // (si falta) y se lanza este motor STT, fijando baseUrl/endpointPath. Id del
    // catálogo de VoiceServerManager (ej "whisper-base").
    QString sttManagedEngine;
    // Comando opcional para un endpoint STT externo local. Si se configura,
    // LlamaCode lo lanza y lo mata con la sesión de Charla, aislándolo con la
    // misma CUDA_VISIBLE_DEVICES de la GPU de voz. No controla procesos que ya
    // estaban corriendo fuera de la app.
    QString sttManagedCommand;
    QStringList sttManagedArgs;

    // ── TTS (text-to-speech) ──
    QString ttsProvider = QStringLiteral("local");  // local | cloud
    QString ttsBaseUrl  = QStringLiteral("http://127.0.0.1:8082");
    QString ttsModel    = QStringLiteral("tts-1");
    QString ttsVoice    = QStringLiteral("alloy");
    QString ttsKeyRef;
    QString ttsFormat   = QStringLiteral("wav");     // wav | mp3 | pcm
    // Modo TTS: auto elige según hardware/disponibilidad; http usa un endpoint
    // OpenAI-compatible; pocket, piper, qwen3 e inflect son procesos locales.
    QString ttsMode = QStringLiteral("auto");        // auto | http | kokoro | pocket | piper | qwen3 | inflect
    // HTTP PCM incremental: reproduce cada bloque al llegar, sin esperar un WAV
    // completo. Kokoro y cualquier endpoint compatible pueden usar esta ruta.
    bool ttsStreamAudio = false;
    int ttsPcmSampleRate = 24000;
    int ttsPcmChannels = 1;
    QString ttsManagedVoice = QStringLiteral("es_ES-davefx-medium");
    QString ttsFallbackMode = QStringLiteral("piper"); // none | http | piper
    // Comando opcional para un endpoint TTS externo local administrado por la
    // sesión. Los argumentos se pasan sin shell, como una lista separada.
    QString ttsManagedCommand;
    QStringList ttsManagedArgs;

    // Pocket TTS (Kyutai): runtime Python administrado por INGI-CHARLA y
    // servidor local residente. `pocketVoice` puede ser una voz incorporada
    // (por ejemplo "lola") o `pocketVoicePath` puede apuntar a WAV/embedding.
    QString pocketPythonPath = QStringLiteral("python");
    QString pocketLanguage = QStringLiteral("spanish");
    QString pocketVoice = QStringLiteral("lola");
    QString pocketVoicePath;
    QString pocketModelConfig;
    int pocketPort = 8200;
    bool pocketQuantize = false;
    // Se mantiene false hasta que el benchmark local de voz confirme que el
    // equipo puede sostener la latencia deseada; la selección manual siempre
    // está disponible aunque esto permanezca apagado.
    bool pocketAutoEnable = false;
    QString qwenBinaryPath;                          // qwen3-tts-cli[.exe]
    QString qwenModelDir;                            // carpeta con GGUFs del runtime
    QString qwenModelName = QStringLiteral("qwen-talker-0.6b-base-Q8_0.gguf");
    QString qwenSpeakerEmbedding;                    // JSON/bin extraído previamente
    QString qwenReferenceWav;                        // alternativa: clonación desde WAV
    QString qwenReferenceText;                       // mejora el modo ICL
    QString qwenSpeaker;                             // CustomVoice speaker
    QString qwenInstruction;                         // estilo/tono
    QString qwenLanguage = QStringLiteral("es");
    int qwenThreads = 0;                             // 0 = cores físicos/default motor
    // Inflect v2 ONNX experimental: runner oficial Python, sólo inglés y voz fija.
    // modelDir debe contener onnx/inference_onnx.py y los artefactos descargados.
    QString inflectPythonPath = QStringLiteral("python");
    QString inflectModelDir;
    QString inflectProvider = QStringLiteral("cpu"); // cpu | directml | cuda
    bool ttsAutoConfigure = true;

    // ── Captura / VAD (detección de fin de habla) ──
    // Cómo se inicia un turno: "vad" = manos libres (detecta voz y silencio),
    // "push_to_talk" = la captura sólo está abierta mientras el usuario
    // mantiene pulsado el botón. El VAD sigue delimitando segmentos parciales,
    // pero nunca cierra por sí solo un turno PTT.
    QString turnMode = QStringLiteral("vad"); // vad | push_to_talk
    // Umbral de energía RMS [0..1] por debajo del cual el frame es "silencio".
    double  vadThreshold = 0.012;
    // Silencio continuo (ms) tras voz para dar el turno por terminado.
    int     vadSilenceMs = 800;
    // Micro-silencio (ms) que corta un segmento para transcripción incremental
    // (texto parcial en vivo mientras hablás). Debe ser < vadSilenceMs.
    int     vadSegmentMs = 350;
    // Energía mínima que tuvo que superarse para considerar que hubo voz.
    double  vadActivationLevel = 0.03;
    // VAD adaptativo (VadEngine): umbral relativo al ruido de fondo medido en
    // vivo, con histéresis y hangover. false = umbral fijo (vadThreshold /
    // vadActivationLevel), el comportamiento viejo.
    bool    vadAdaptive = true;
    // Endpointing semántico (TurnDetector): el silencio exigido para cerrar el
    // turno se ajusta según cómo quedó el transcript parcial (cerrado → cortar
    // antes; colgado en "y…"/"porque…" → esperar más). false = vadSilenceMs fijo.
    bool    smartTurn = true;
    // Reanudar escucha automáticamente tras hablar la respuesta.
    bool    autoListen = true;
    // Cortar el TTS si el usuario empieza a hablar (barge-in).
    bool    bargeIn = true;

    // ── Cursor por voz vía OCR (accesibilidad) ──
    // OFF por defecto, y a propósito: cuando está activo, cada frase que empieza
    // con un verbo de cursor ("clic en Guardar") dispara una captura de TODA la
    // pantalla para OCRearla. Eso no es lo que alguien espera de un modo de charla
    // sin haberlo pedido. Se OCRea en RAM y se descarta (nunca va a disco), pero
    // la decisión de mirar la pantalla es del usuario, no nuestra.
    bool    cursorOcr = false;

    bool sttIsCloud() const { return sttProvider == QLatin1String("cloud"); }
    bool ttsIsCloud() const { return ttsProvider == QLatin1String("cloud"); }

    QJsonObject toJson() const;
    static VoiceConfig fromJson(const QJsonObject &obj);
};
