#include "SttEngine.h"
#include "AudioCodec.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QDebug>
#include <QProcess>
#include <QTemporaryFile>
#include <QDir>
#include <QFile>

namespace {

void removeNativeWav(QString &path)
{
    if (!path.isEmpty()) QFile::remove(path);
    path.clear();
}

}

SttEngine::SttEngine(QObject *parent) : QObject(parent) {}

SttEngine::~SttEngine()
{
    cancel();
}

void SttEngine::setConfig(const VoiceConfig &cfg, const QString &resolvedKey)
{
    m_cfg = cfg;
    m_key = resolvedKey;
}

void SttEngine::setNativeStt(const QString &program, const QString &modelPath)
{
    m_nativeProgram = program.trimmed();
    m_nativeModelPath = modelPath.trimmed();
}

bool SttEngine::busy() const
{
    return m_reply != nullptr || m_streamingActive || !m_nativeProcess.isNull();
}

QByteArray SttEngine::buildMultipart(const QByteArray &boundary, const QByteArray &wav,
                                     const QString &model, const QString &language)
{
    const QByteArray dash = "--" + boundary + "\r\n";
    QByteArray body;
    body += dash;
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
    body += "Content-Type: audio/wav\r\n\r\n";
    body += wav;
    body += "\r\n";
    body += dash;
    body += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
    body += model.toUtf8();
    body += "\r\n";
    if (!language.isEmpty() && language != QLatin1String("auto")) {
        body += dash;
        body += "Content-Disposition: form-data; name=\"language\"\r\n\r\n";
        body += language.toUtf8();
        body += "\r\n";
    }
    body += "--" + boundary + "--\r\n";
    return body;
}

QString SttEngine::parseTranscript(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        if (o.contains("text")) return o.value("text").toString().trimmed();
        // Algunos servers devuelven {error:{message}}.
        if (o.contains("error"))
            return QString();
    }
    return QString();
}

QByteArray SttEngine::buildStreamingConfig(int sampleRate, const QString &language,
                                            const QString &model)
{
    const QJsonObject object{
        {QStringLiteral("type"), QStringLiteral("config")},
        {QStringLiteral("sample_rate"), sampleRate},
        {QStringLiteral("language"), language},
        {QStringLiteral("model"), model}};
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

QByteArray SttEngine::buildStreamingAudio(const QByteArray &pcm16, quint64 sequence)
{
    const QJsonObject object{
        {QStringLiteral("type"), QStringLiteral("audio")},
        {QStringLiteral("sequence"), static_cast<qint64>(sequence)},
        {QStringLiteral("pcm16_base64"), QString::fromLatin1(pcm16.toBase64())}};
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

QByteArray SttEngine::buildStreamingEnd()
{
    return QByteArray("{\"type\":\"end\"}\n");
}

QByteArray SttEngine::buildStreamingCancel()
{
    return QByteArray("{\"type\":\"cancel\"}\n");
}

QVariantMap SttEngine::parseStreamingMessage(const QByteArray &line)
{
    const QJsonDocument document = QJsonDocument::fromJson(line.trimmed());
    if (!document.isObject()) return {};
    const QJsonObject object = document.object();
    QVariantMap result = object.toVariantMap();
    const QString type = result.value(QStringLiteral("type")).toString().trimmed().toLower();
    if (type.isEmpty()) return {};
    result[QStringLiteral("type")] = type;
    return result;
}

QStringList SttEngine::buildNativeParakeetArgs(const QString &modelPath,
                                               const QString &wavPath,
                                               int threads)
{
    QStringList args{QStringLiteral("-m"), modelPath,
                     QStringLiteral("-f"), wavPath,
                     QStringLiteral("-ng")};
    if (threads > 0)
        args << QStringLiteral("-t") << QString::number(threads);
    return args;
}

QString SttEngine::parseNativeParakeetTranscript(const QByteArray &output)
{
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QStringLiteral("Text,"), Qt::CaseInsensitive))
            return line.mid(5).trimmed();
        if (line.startsWith(QStringLiteral("Text:"), Qt::CaseInsensitive))
            return line.mid(5).trimmed();
    }
    return {};
}

void SttEngine::attachStreamingProcess(QProcess *process)
{
    if (m_streamProcess)
        disconnect(m_streamProcess, nullptr, this, nullptr);
    m_streamProcess = process;
    m_streamOutput.clear();
    if (!m_streamProcess) return;

    connect(m_streamProcess, &QProcess::readyReadStandardOutput, this,
            &SttEngine::consumeStreamingOutput);
    connect(m_streamProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
        if (!m_streamingActive) return;
        m_streamingActive = false;
        emit failed(QStringLiteral("sidecar STT: ")
                    + (m_streamProcess ? m_streamProcess->errorString()
                                        : QStringLiteral("proceso no disponible")));
    });
    connect(m_streamProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus status) {
        if (!m_streamingActive) return;
        m_streamingActive = false;
        if (status != QProcess::NormalExit || !m_streamFinalSeen)
            emit failed(QStringLiteral("sidecar STT terminó antes de finalizar el turno"));
    });
}

bool SttEngine::startStreaming(int sampleRate)
{
    if (m_reply || m_streamingActive) return false;
    if (!m_streamProcess || m_streamProcess->state() != QProcess::Running) return false;

    m_streamOutput.clear();
    m_streamLatestText.clear();
    m_streamSequence = 0;
    m_streamEndRequested = false;
    m_streamFinalSeen = false;
    m_streamingActive = true;
    if (m_streamProcess->write(buildStreamingConfig(sampleRate, m_cfg.sttLanguage,
                                                     m_cfg.sttModel)) < 0) {
        m_streamingActive = false;
        return false;
    }
    return true;
}

void SttEngine::pushStreamingAudio(const QByteArray &pcm16)
{
    if (!m_streamingActive || pcm16.isEmpty()) return;
    if (!m_streamProcess || m_streamProcess->state() != QProcess::Running) {
        m_streamingActive = false;
        emit failed(QStringLiteral("sidecar STT no está ejecutándose"));
        return;
    }
    if (m_streamProcess->write(buildStreamingAudio(pcm16, ++m_streamSequence)) < 0) {
        m_streamingActive = false;
        emit failed(QStringLiteral("no se pudo enviar audio al sidecar STT"));
    }
}

void SttEngine::finishStreaming()
{
    if (!m_streamingActive || m_streamEndRequested) return;
    if (!m_streamProcess || m_streamProcess->state() != QProcess::Running) {
        m_streamingActive = false;
        emit failed(QStringLiteral("sidecar STT no está ejecutándose"));
        return;
    }
    m_streamEndRequested = true;
    if (m_streamProcess->write(buildStreamingEnd()) < 0) {
        m_streamingActive = false;
        emit failed(QStringLiteral("no se pudo cerrar la sesión STT"));
    }
}

void SttEngine::transcribeNative(const QByteArray &pcm16, int sampleRate)
{
    QTemporaryFile wav(QDir::tempPath() + QStringLiteral("/llamacode-parakeet-XXXXXX.wav"));
    const QByteArray wavData = AudioCodec::pcm16ToWav(pcm16, sampleRate);
    if (!wav.open() || wav.write(wavData) != wavData.size() || !wav.flush()) {
        emit failed(QStringLiteral("no se pudo preparar el WAV para Parakeet"));
        return;
    }
    m_nativeWavPath = wav.fileName();
    wav.setAutoRemove(false);
    wav.close();

    auto *process = new QProcess(this);
    m_nativeProcess = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    const QStringList args = buildNativeParakeetArgs(m_nativeModelPath,
                                                      m_nativeWavPath, 8);
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_nativeProcess != process) return;
        const QString detail = process->errorString();
        m_nativeProcess = nullptr;
        removeNativeWav(m_nativeWavPath);
        process->deleteLater();
        emit failed(QStringLiteral("Parakeet no pudo iniciarse: ") + detail);
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process](int exitCode, QProcess::ExitStatus status) {
        if (m_nativeProcess != process) return;
        const QByteArray output = process->readAll();
        m_nativeProcess = nullptr;
        removeNativeWav(m_nativeWavPath);
        process->deleteLater();
        if (status != QProcess::NormalExit || exitCode != 0) {
            QString detail = QString::fromUtf8(output).trimmed();
            if (detail.size() > 300) detail = detail.right(300);
            emit failed(QStringLiteral("Parakeet terminó con error")
                        + (detail.isEmpty() ? QString() : QStringLiteral(": ") + detail));
            return;
        }
        const QString text = parseNativeParakeetTranscript(output);
        if (text.isEmpty())
            emit failed(QStringLiteral("Parakeet no devolvió una transcripción"));
        else
            emit transcribed(text);
    });
    process->start(m_nativeProgram, args);
}

void SttEngine::consumeStreamingOutput()
{
    if (!m_streamProcess) return;
    m_streamOutput += m_streamProcess->readAllStandardOutput();
    int newline = -1;
    while ((newline = m_streamOutput.indexOf('\n')) >= 0) {
        const QByteArray line = m_streamOutput.left(newline).trimmed();
        m_streamOutput.remove(0, newline + 1);
        if (!line.isEmpty()) handleStreamingMessage(parseStreamingMessage(line));
    }
}

void SttEngine::handleStreamingMessage(const QVariantMap &message)
{
    if (message.isEmpty()) return;
    const QString type = message.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("partial")) {
        const QString text = message.value(QStringLiteral("text")).toString().trimmed();
        if (text.isEmpty() || !m_streamingActive) return;
        m_streamLatestText = text;
        emit partialTranscribed(text);
        return;
    }
    if (type == QLatin1String("final")) {
        if (!m_streamingActive) return;
        const QString text = message.value(QStringLiteral("text")).toString().trimmed();
        if (!text.isEmpty()) m_streamLatestText = text;
        m_streamFinalSeen = true;
        m_streamingActive = false;
        m_streamEndRequested = false;
        emit streamingFinished(m_streamLatestText);
        return;
    }
    if (type == QLatin1String("error")) {
        if (!m_streamingActive) return;
        m_streamingActive = false;
        const QString detail = message.value(QStringLiteral("error")).toString().trimmed();
        emit failed(detail.isEmpty() ? QStringLiteral("sidecar STT informó un error") : detail);
    }
}

void SttEngine::transcribe(const QByteArray &pcm16, int sampleRate)
{
    if (m_reply || m_streamingActive) { emit failed(QStringLiteral("STT ocupado")); return; }
    if (m_nativeProcess) { emit failed(QStringLiteral("STT ocupado")); return; }
    if (!m_nativeProgram.isEmpty()) {
        transcribeNative(pcm16, sampleRate);
        return;
    }
    const QByteArray wav = AudioCodec::pcm16ToWav(pcm16, sampleRate);
    const QByteArray boundary = "----LlamaCodeVoiceSTT";
    const QByteArray body = buildMultipart(boundary, wav, m_cfg.sttModel, m_cfg.sttLanguage);

    QString base = m_cfg.sttBaseUrl;
    while (base.endsWith('/')) base.chop(1);
    QString path = m_cfg.sttEndpointPath.isEmpty()
        ? QStringLiteral("/v1/audio/transcriptions") : m_cfg.sttEndpointPath;
    if (!path.startsWith('/')) path.prepend('/');
    QNetworkRequest req(QUrl(base + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "multipart/form-data; boundary=" + boundary);
    if (m_cfg.sttIsCloud() && !m_key.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_key.toUtf8());

    m_reply = m_nam.post(req, body);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->deleteLater();
        const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r->error() != QNetworkReply::NoError) {
            // Cuerpo del error (los servers suelen mandar detalle JSON útil).
            const QByteArray body = r->readAll().left(300);
            qWarning().noquote() << QStringLiteral("[charla] STT http %1 %2: %3 | %4")
                                        .arg(http).arg(r->url().toString(),
                                                       r->errorString(),
                                                       QString::fromUtf8(body));
            emit failed(r->errorString());
            return;
        }
        const QByteArray raw = r->readAll();
        const QString text = parseTranscript(raw);
        if (text.isEmpty()) {
            qWarning().noquote() << QStringLiteral("[charla] STT respuesta sin texto (http %1): %2")
                                        .arg(http).arg(QString::fromUtf8(raw.left(300)));
            emit failed(QStringLiteral("transcripción vacía"));
        } else emit transcribed(text);
    });
}

void SttEngine::cancel()
{
    if (m_reply) { m_reply->abort(); }
    if (m_nativeProcess) {
        QProcess *process = m_nativeProcess;
        m_nativeProcess = nullptr;
        process->disconnect(this);
        process->kill();
        process->deleteLater();
    }
    removeNativeWav(m_nativeWavPath);
    if (m_streamingActive && m_streamProcess
        && m_streamProcess->state() == QProcess::Running) {
        m_streamProcess->write(buildStreamingCancel());
    }
    m_streamingActive = false;
    m_streamEndRequested = false;
    m_streamFinalSeen = false;
    m_streamLatestText.clear();
    m_streamOutput.clear();
}
