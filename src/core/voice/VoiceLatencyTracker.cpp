#include "VoiceLatencyTracker.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>
#include <utility>

void VoiceLatencyTracker::beginTurn(const QString &endpointReason)
{
    m_clock.restart();
    m_transcriptMs = m_firstLlmTextMs = m_firstUsefulLlmTextMs =
        m_firstTtsRequestMs = m_firstAudioGeneratedMs = -1;
    m_endpointReason = endpointReason.trimmed();
    m_saved = false;
}

void VoiceLatencyTracker::markTranscript()
{
    if (m_clock.isValid() && m_transcriptMs < 0) m_transcriptMs = int(m_clock.elapsed());
}

void VoiceLatencyTracker::markFirstLlmText()
{
    if (m_clock.isValid() && m_firstLlmTextMs < 0) m_firstLlmTextMs = int(m_clock.elapsed());
}

void VoiceLatencyTracker::markFirstUsefulLlmText()
{
    if (m_clock.isValid() && m_firstUsefulLlmTextMs < 0)
        m_firstUsefulLlmTextMs = int(m_clock.elapsed());
}

void VoiceLatencyTracker::markFirstTtsRequest()
{
    if (m_clock.isValid() && m_firstTtsRequestMs < 0) m_firstTtsRequestMs = int(m_clock.elapsed());
}

void VoiceLatencyTracker::markFirstAudioGenerated()
{
    if (m_clock.isValid() && m_firstAudioGeneratedMs < 0)
        m_firstAudioGeneratedMs = int(m_clock.elapsed());
}

QVariantMap VoiceLatencyTracker::markFirstAudioPlayed(const QString &sttEngine,
                                                       const QString &ttsEngine)
{
    if (!m_clock.isValid() || m_saved) return {};
    m_saved = true;
    const int played = int(m_clock.elapsed());
    QVariantMap s{
        {QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("sttMs"), m_transcriptMs},
        {QStringLiteral("llmFirstTextMs"), m_firstLlmTextMs},
        {QStringLiteral("llmFirstUsefulTextMs"), m_firstUsefulLlmTextMs},
        {QStringLiteral("ttsRequestMs"), m_firstTtsRequestMs},
        {QStringLiteral("audioGeneratedMs"), m_firstAudioGeneratedMs},
        {QStringLiteral("endToFirstAudioMs"), played},
        {QStringLiteral("llmMs"), m_firstLlmTextMs >= 0 && m_transcriptMs >= 0
             ? m_firstLlmTextMs - m_transcriptMs : -1},
        {QStringLiteral("ttsMs"), m_firstAudioGeneratedMs >= 0 && m_firstTtsRequestMs >= 0
             ? m_firstAudioGeneratedMs - m_firstTtsRequestMs : -1},
        {QStringLiteral("playbackStartMs"), m_firstAudioGeneratedMs >= 0
             ? played - m_firstAudioGeneratedMs : -1},
        {QStringLiteral("sttEngine"), sttEngine},
        {QStringLiteral("ttsEngine"), ttsEngine},
        {QStringLiteral("endpointReason"), m_endpointReason},
    };
    append(s);
    return s;
}

QString VoiceLatencyTracker::storagePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/voice");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/latency.jsonl");
}

void VoiceLatencyTracker::append(const QVariantMap &sample)
{
    const QString path = storagePath();
    QList<QByteArray> lines;
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!existing.atEnd()) {
            const QByteArray line = existing.readLine().trimmed();
            if (!line.isEmpty()) lines << line;
        }
    }
    lines << QJsonDocument(QJsonObject::fromVariantMap(sample)).toJson(QJsonDocument::Compact);
    while (lines.size() > 500) lines.removeFirst();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return;
    for (const QByteArray &line : std::as_const(lines)) { f.write(line); f.write("\n"); }
}

int VoiceLatencyTracker::percentile(QList<int> values, double p)
{
    if (values.isEmpty()) return -1;
    std::sort(values.begin(), values.end());
    const int i = qBound(0, int((values.size() - 1) * p + 0.5), values.size() - 1);
    return values.at(i);
}

QVariantMap VoiceLatencyTracker::summary()
{
    QFile f(storagePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {{QStringLiteral("count"), 0}};
    QList<int> total, stt, llm, useful, tts;
    QVariantMap last;
    while (!f.atEnd()) {
        const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
        if (o.isEmpty()) continue;
        last = o.toVariantMap();
        auto add = [&o](const char *key, QList<int> &out) {
            const int v = o.value(QLatin1String(key)).toInt(-1);
            if (v >= 0) out << v;
        };
        add("endToFirstAudioMs", total); add("sttMs", stt); add("llmMs", llm);
        add("llmFirstUsefulTextMs", useful); add("ttsMs", tts);
    }
    return {{QStringLiteral("count"), total.size()}, {QStringLiteral("last"), last},
            {QStringLiteral("p50Ms"), percentile(total, .50)},
            {QStringLiteral("p90Ms"), percentile(total, .90)},
            {QStringLiteral("p95Ms"), percentile(total, .95)},
            {QStringLiteral("sttP50Ms"), percentile(stt, .50)},
            {QStringLiteral("llmP50Ms"), percentile(llm, .50)},
            {QStringLiteral("llmUsefulP50Ms"), percentile(useful, .50)},
            {QStringLiteral("ttsP50Ms"), percentile(tts, .50)}};
}
