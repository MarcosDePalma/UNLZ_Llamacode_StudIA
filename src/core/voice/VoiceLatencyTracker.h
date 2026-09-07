#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QString>
#include <QVariantMap>

// Cronometra y conserva las etapas del hot path de voz. El reloj comienza al
// detectar fin de habla (antes del STT), de modo que endToFirstAudio sea una
// medicion end-to-end real y comparable entre motores/perfiles.
class VoiceLatencyTracker
{
public:
    void beginTurn(const QString &endpointReason = QString());
    void markTranscript();
    void markFirstLlmText();
    void markFirstUsefulLlmText();
    void markFirstTtsRequest();
    void markFirstAudioGenerated();
    QVariantMap markFirstAudioPlayed(const QString &sttEngine, const QString &ttsEngine);

    static QVariantMap summary();
    static QString storagePath();
    static int percentile(QList<int> values, double p);

private:
    static void append(const QVariantMap &sample);
    QElapsedTimer m_clock;
    int m_transcriptMs = -1;
    int m_firstLlmTextMs = -1;
    int m_firstUsefulLlmTextMs = -1;
    int m_firstTtsRequestMs = -1;
    int m_firstAudioGeneratedMs = -1;
    QString m_endpointReason;
    bool m_saved = false;
};
