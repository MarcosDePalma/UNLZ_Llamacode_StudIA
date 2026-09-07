#pragma once
#include "VoiceTypes.h"
#include "SttEngine.h"
#include "TtsEngine.h"
#include "VadEngine.h"
#include "TurnDetector.h"
#include "VoiceLatencyTracker.h"
#include <QObject>
#include <QByteArray>
#include <QPointer>
#include <QVariantList>
#include <QList>
#include <QPair>
#include <QStringList>
#include <QString>
#include <QElapsedTimer>

class QAudioSource;
class QIODevice;
class QMediaPlayer;
class QAudioOutput;
class QAudioSink;
class QBuffer;
class QProcess;

// Orquestador del modo "Charla" (voz-a-voz):
//   Listening → (VAD detecta fin de habla) → Transcribing (STT) →
//   emite transcriptReady(text) [AppController lo manda al backend de chat] →
//   Thinking (mientras el LLM genera) → speak(reply) → Speaking (TTS+playback) →
//   (autoListen) vuelve a Listening.
// Captura en PCM16 mono 16 kHz. VAD por energía RMS (sin libs externas).
class VoiceController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ stateStr NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(double level READ level NOTIFY levelChanged)        // RMS live [0..1] para meter
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QVariantMap latencyStats READ latencyStats NOTIFY latencyUpdated)
public:
    enum State { Idle, Ready, Listening, Transcribing, Thinking, Speaking, Error };
    Q_ENUM(State)

    explicit VoiceController(QObject *parent = nullptr);
    ~VoiceController() override;

    void setConfig(const VoiceConfig &cfg, const QString &sttKey, const QString &ttsKey);
    // Rutas para TTS modo piper (process-mode).
    void setTtsPiper(const QString &binPath, const QString &modelPath);
    // Máscara CUDA para TTS local; la voz puede quedar aislada en la GPU
    // reservada por el plan de Charla.
    void setTtsGpuDeviceMask(const QString &mask);
    // Conecta un sidecar STT persistente administrado por AppController. El
    // proceso mantiene su lifecycle existente; acá sólo se usa stdin/stdout.
    void setStreamingSttProcess(QProcess *process);
    // Configura un motor STT nativo batch administrado por AppController
    // (actualmente parakeet-cli). Se ejecuta por turno y no requiere Python.
    void setNativeStt(const QString &program, const QString &modelPath);
    // Dispositivo de entrada por id (QAudioDevice::id() como string utf8). "" = default.
    void setInputDevice(const QString &id);
    // Lista de micrófonos: [{id,name,isDefault}].
    static QVariantList inputDevices();

    QString stateStr() const;
    static QString stateName(State s);   // nombre legible (logs/UI)
    bool active() const { return m_state != Idle && m_state != Error; }
    double level() const { return m_level; }
    QString lastError() const { return m_lastError; }
    QVariantMap latencyStats() const { return VoiceLatencyTracker::summary(); }
    bool pushToTalkMode() const
    {
        return m_cfg.turnMode == QLatin1String("push_to_talk") && !m_forceVad;
    }

    State state() const { return m_state; }

    // ── Función pura de VAD (testeable) ──
    // Decide si el turno terminó: hubo voz (peak>=activation) y luego silencio
    // continuo >= silenceMs. silenceAccumMs es el silencio acumulado hasta ahora.
    // Path legacy (VoiceConfig.vadAdaptive=false); con VAD adaptativo la decisión
    // la toma VadEngine + TurnDetector.
    static bool turnEnded(double peakLevel, double activationLevel,
                          int silenceAccumMs, int silenceMs);

    // ── Función pura (testeable) ──
    // Traduce la config de voz al tuning del VAD adaptativo. vadThreshold pasa a
    // ser el piso absoluto (suelo del umbral relativo), no el umbral en sí.
    static VadTuning vadTuningFor(const VoiceConfig &cfg);

    // ── Función pura (testeable) ──
    // Trocea la respuesta en oraciones para TTS por chunks: se sintetiza la
    // primera oración apenas se cierra (audio rápido) mientras se generan las
    // siguientes. Acumula hasta un cierre de oración (.!?…\n) que supere minLen
    // para no spawnear piper por fragmentos minúsculos. Devuelve >=1 chunk.
    static QStringList splitSentences(const QString &text, int minLen = 40);

    // ── Función pura (testeable) — variante para streaming incremental ──
    // Devuelve solo las oraciones YA cerradas (terminador .!?…\n que superó
    // minLen); deja el fragmento final incompleto sin emitir. *consumed = cantidad
    // de chars consumidos (prefijo que formó oraciones completas); el resto
    // (text.mid(consumed)) se sigue acumulando hasta cerrarse. Permite hablar la
    // respuesta del agente mientras se genera, sin esperar el fin del turno.
    static QStringList splitCompleteSentences(const QString &text, int minLen, int *consumed);

    // Limpia el texto en vivo del agente para TTS: quita bloques <think>…</think>
    // (cerrados y el abierto-sin-cerrar, razonamiento en vuelo) y el indicador
    // transitorio "⏳ preparando tool…". Devuelve solo lo hablable. Pura.
    static QString sanitizeForSpeech(const QString &s);

public slots:
    void start();          // arranca Charla (Listening o Ready si usa PTT)
    void startDictation(); // arranca dictado con VAD aunque Charla use PTT
    void finishTurn();     // cierra captura y transcribe lo acumulado
    void stop();           // corta todo y vuelve a Idle
    void startListening(); // fuerza escucha (corta TTS si suena)
    // En modo push_to_talk, abre/cierra el micrófono para un turno explícito.
    // En modo VAD son equivalentes a iniciar/finalizar la escucha normal.
    void pushToTalkStart();
    void pushToTalkStop();
    void micTest();        // captura solo para ver nivel (sin STT/chat); probar micrófono
    void stopMicTest();
    void speak(const QString &text);   // habla un texto (lo invoca AppController al cerrar turno)
    // Streaming incremental: habla las oraciones ya cerradas del texto acumulado
    // del agente (bubbleId identifica la burbuja; al cambiar, resetea el puntero).
    // Arranca a hablar apenas hay una oración, en paralelo a la generación/tools.
    void speakStreaming(int bubbleId, const QString &fullText);
    // Cierre del turno: encola el fragmento final que quedó sin terminador y
    // resetea el estado de streaming. Si no quedaba nada, retoma escucha.
    void speakFlush(int bubbleId, const QString &fullText);
    void notifyThinking();             // el LLM empezó a generar
    // El turno del backend falló (server LLM caído, error de red...). Sin esto la
    // charla quedaba clavada en "pensando" para siempre. Muestra el error y, si
    // autoListen, retoma la escucha para poder reintentar hablando.
    void notifyTurnFailed(const QString &err);

signals:
    void stateChanged();
    void levelChanged();
    void errorChanged();
    // Texto reconocido del usuario; AppController lo envía al backend de chat.
    void transcriptReady(const QString &text);
    // Transcripción parcial en vivo (segmentos ya reconocidos del turno en curso).
    void partialTranscript(const QString &text);
    void latencyUpdated(const QVariantMap &sample);
    // El usuario pidió hablar mientras el asistente estaba reproduciendo. El
    // dueño del backend debe abortar también la generación, no sólo el audio.
    void interruptRequested();

private slots:
    void onAudioReady();
    void onSttDone(const QString &text);
    void onSttFailed(const QString &err);
    void onSttPartial(const QString &text);
    void onSttStreamFinished(const QString &text);
    void onTtsAudio(const QByteArray &audio, const QString &format);
    void onTtsAudioChunk(const QByteArray &pcm, int sampleRate, int channels);
    void onTtsStreamFinished();
    void onTtsFailed(const QString &err);

private:
    void setState(State s);
    void fail(const QString &err);
    bool segmentHadVoice() const;   // ¿el segmento en curso tuvo voz? (adaptativo o legacy)
    void beginCapture();
    void endCapture();              // tear-down de captura + reset del estado de stream
    void stopSource();             // detiene el QAudioSource sin resetear el transcript
    void flushSegment(bool finalSeg); // encola el segmento actual (si tuvo voz) y pumpea
    void pumpSegments();           // transcribe el próximo segmento si STT está libre
    void finishStreamingTurn();    // cierra una sesión STT NDJSON y espera el final
    void finalizeTurn();           // arma el texto final del turno y lo emite
    void playAudio(const QByteArray &audio, const QString &format);
    // Path rápido: WAV PCM16 (piper/whisper-speech) directo a QAudioSink, sin el
    // pipeline de QMediaPlayer (que en Windows arma MediaFoundation por clip).
    // El sink se reusa entre oraciones del mismo turno → sin gap entre clips.
    void playPcm(const QByteArray &pcm, int sampleRate, int channels);
    void onClipFinished();   // continuación común: próximo clip / fin de turno
    void teardownPlayback();
    void teardownSink();
    void pumpTts();        // sintetiza la próxima oración pendiente si TTS está libre
    void playNextClip();   // reproduce el próximo clip de audio ya sintetizado

    VoiceConfig m_cfg;
    State m_state = Idle;
    double m_level = 0.0;
    QString m_lastError;

    SttEngine m_stt;
    TtsEngine m_tts;

    QAudioSource *m_source = nullptr;
    QPointer<QIODevice> m_input;     // device de QAudioSource (no es dueño)
    int   m_sampleRate = 16000;
    int   m_silenceMs = 0;           // silencio continuo acumulado (ms)
    double m_peak = 0.0;             // máximo RMS visto en el turno (¿hubo voz?)
    VadEngine m_vad;                 // VAD adaptativo (activo si cfg.vadAdaptive)

    // Transcripción incremental (chunked): el turno se trocea en segmentos por
    // micro-silencios; cada segmento se transcribe en orden y se acumula en
    // m_partial (mostrado en vivo). Al silencio largo se finaliza el turno.
    QByteArray m_segment;            // PCM16 del segmento en curso
    double m_segPeak = 0.0;          // ¿el segmento tuvo voz? (path legacy)
    bool   m_segVoice = false;       // ídem, según el VAD adaptativo
    int    m_segSilenceMs = 0;       // micro-silencio del segmento
    QString m_partial;               // transcripción acumulada del turno
    QList<QByteArray> m_segQueue;    // segmentos esperando transcripción (en orden)
    bool   m_turnEnding = false;     // silencio largo detectado: finalizar al drenar la cola
    bool   m_streamingTurn = false;  // sesión STT sidecar activa para este turno
    QString m_endpointReason;        // métrica: vad/ptt/manual
    bool  m_monitorOnly = false;     // true durante Speaking (barge-in): no acumula
    bool  m_testMode = false;        // micTest: captura para nivel, no VAD/STT
    bool  m_pttHeld = false;         // botón PTT actualmente presionado
    bool  m_forceVad = false;        // override transitorio usado por dictado
    QString m_deviceId;              // micrófono elegido ("" = default del sistema)

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOut = nullptr;
    QBuffer *m_playBuf = nullptr;
    QAudioSink *m_sink = nullptr;    // path PCM16 directo (reusado entre clips)
    QPointer<QIODevice> m_sinkIo;    // push-mode para HTTP PCM incremental
    int m_sinkRate = 0;
    int m_sinkChannels = 0;

    // TTS por chunks (streaming de oraciones): cola de oraciones pendientes de
    // sintetizar + cola de clips ya sintetizados esperando reproducción. Permite
    // empezar a hablar tras la primera oración mientras se generan las demás.
    QStringList m_ttsQueue;                          // oraciones pendientes
    QList<QPair<QByteArray, QString>> m_audioQueue;  // {audio, formato} a reproducir
    bool m_playing = false;                          // hay un clip sonando
    bool m_streamingPcm = false;
    bool m_streamingPcmFinished = false;

    // Estado del streaming incremental (modo agente): burbuja en curso y cuántos
    // chars de su texto ya se encolaron como oraciones completas.
    int m_streamBubble = -1;
    int m_streamConsumed = 0;

    // Timers de diagnóstico (logs [charla]): duración de cada estado, del request
    // STT en vuelo, de la síntesis TTS en vuelo, y del gap fin-de-habla→primer audio.
    QElapsedTimer m_tState;
    QElapsedTimer m_tStt;
    QElapsedTimer m_tTts;
    QElapsedTimer m_tTurn;         // arranca al finalizar el habla del usuario
    bool m_turnFirstAudio = false; // ya se logueó el primer audio del turno
    VoiceLatencyTracker m_latency;
    bool m_firstPlaybackPending = false;
};
