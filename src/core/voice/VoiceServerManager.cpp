#include "VoiceServerManager.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QPair>
#include <QProcess>
#include <QDirIterator>

namespace {
// Catálogo declarativo. Las URLs de modelos ggml de whisper.cpp viven en HF
// (estables). Si una cambia, se ajusta acá.
struct Engine {
    const char *id; const char *name; const char *engine;
    const char *modelFile; const char *modelUrl; int sizeMb;
    const char *endpointPath; int defaultPort;
    const char *transport; bool installable; const char *docsUrl;
};
const Engine kEngines[] = {
    {"whisper-tiny",  "Whisper tiny (multilingüe, ~78 MB, rápido)",   "whisper-cpp",
     "ggml-tiny.bin",
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin",
     78,  "/inference", 8081, "http_batch", true, "https://github.com/ggerganov/whisper.cpp"},
    {"whisper-base",  "Whisper base (multilingüe, ~148 MB, balance)", "whisper-cpp",
     "ggml-base.bin",
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin",
     148, "/inference", 8081, "http_batch", true, "https://github.com/ggerganov/whisper.cpp"},
    {"whisper-small", "Whisper small (multilingüe, ~488 MB, preciso)","whisper-cpp",
     "ggml-small.bin",
     "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin",
     488, "/inference", 8081, "http_batch", true, "https://github.com/ggerganov/whisper.cpp"},
    {"parakeet-tdt-0.6b-v3", "Parakeet TDT v3 (local nativo, experimental, ~356 MB)", "parakeet-cli",
     "ggml-parakeet-tdt-0.6b-v3-q4_0.bin",
     "https://huggingface.co/ggml-org/parakeet-GGUF/resolve/main/ggml-parakeet-tdt-0.6b-v3-q4_0.bin?download=true",
     356, "", 0, "process_batch", true,
     "https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3"},
};

// Voces piper (TTS process-mode). Cada voz = .onnx + .onnx.json (rhasspy/piper-voices).
struct Voice {
    const char *id; const char *name; const char *lang;
    const char *modelFile; const char *modelUrl; const char *jsonUrl; int sizeMb;
};
const Voice kVoices[] = {
    {"es_ES-davefx-medium", "Español (davefx, medium)", "es",
     "es_ES-davefx-medium.onnx",
     "https://huggingface.co/rhasspy/piper-voices/resolve/main/es/es_ES/davefx/medium/es_ES-davefx-medium.onnx",
     "https://huggingface.co/rhasspy/piper-voices/resolve/main/es/es_ES/davefx/medium/es_ES-davefx-medium.onnx.json",
     63},
    {"es_MX-claude-high", "Español MX (claude, high)", "es",
     "es_MX-claude-high.onnx",
     "https://huggingface.co/rhasspy/piper-voices/resolve/main/es/es_MX/claude/high/es_MX-claude-high.onnx",
     "https://huggingface.co/rhasspy/piper-voices/resolve/main/es/es_MX/claude/high/es_MX-claude-high.onnx.json",
     114},
    {"en_US-amy-medium", "English US (amy, medium)", "en",
     "en_US-amy-medium.onnx",
     "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx",
     "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx.json",
     63},
};
}

VoiceServerManager::VoiceServerManager(QObject *parent) : QObject(parent) {}

QVariantList VoiceServerManager::sttCatalog()
{
    QVariantList out;
    for (const Engine &e : kEngines) {
        out.append(QVariantMap{
            {"id", QString::fromLatin1(e.id)},
            {"name", QString::fromUtf8(e.name)},
            {"engine", QString::fromLatin1(e.engine)},
            {"modelFile", QString::fromLatin1(e.modelFile)},
            {"modelUrl", QString::fromLatin1(e.modelUrl)},
            {"sizeMb", e.sizeMb},
            {"endpointPath", QString::fromLatin1(e.endpointPath)},
            {"defaultPort", e.defaultPort},
            {"transport", QString::fromLatin1(e.transport)},
            {"installable", e.installable},
            {"requiresCommand", !e.installable},
            {"docsUrl", QString::fromLatin1(e.docsUrl)}});
    }
    return out;
}

QVariantMap VoiceServerManager::sttEngine(const QString &id)
{
    for (const Engine &e : kEngines)
        if (id == QLatin1String(e.id))
            return sttCatalog().at(&e - kEngines).toMap();
    return {};
}

QVariantList VoiceServerManager::ttsCatalog()
{
    QVariantList out;
    for (const Voice &v : kVoices) {
        out.append(QVariantMap{
            {"id", QString::fromLatin1(v.id)},
            {"name", QString::fromUtf8(v.name)},
            {"lang", QString::fromLatin1(v.lang)},
            {"modelFile", QString::fromLatin1(v.modelFile)},
            {"modelUrl", QString::fromLatin1(v.modelUrl)},
            {"jsonUrl", QString::fromLatin1(v.jsonUrl)},
            {"sizeMb", v.sizeMb}});
    }
    return out;
}

QVariantMap VoiceServerManager::ttsVoice(const QString &id)
{
    for (const Voice &v : kVoices)
        if (id == QLatin1String(v.id))
            return ttsCatalog().at(&v - kVoices).toMap();
    return {};
}

QString VoiceServerManager::defaultTtsVoiceForLang(const QString &lang)
{
    const QString base = QStringLiteral("es_ES-davefx-medium");
    if (lang.isEmpty()) return base;
    for (const Voice &v : kVoices)
        if (lang == QLatin1String(v.lang))
            return QString::fromLatin1(v.id);
    return base;     // sin voz para ese idioma → español base
}

QString VoiceServerManager::ttsModelPath(const QString &voiceId)
{
    const QVariantMap v = ttsVoice(voiceId);
    if (v.isEmpty()) return {};
    return installRoot() + QStringLiteral("/tts/") + voiceId + QStringLiteral("/")
           + v.value("modelFile").toString();
}

bool VoiceServerManager::ttsVoiceInstalled(const QString &voiceId) const
{
    const QString p = ttsModelPath(voiceId);
    // piper requiere .onnx + .onnx.json
    return !p.isEmpty() && QFile::exists(p) && QFile::exists(p + QStringLiteral(".json"));
}

QStringList VoiceServerManager::buildPiperArgs(const QString &modelPath, const QString &outWav)
{
    return {QStringLiteral("-m"), modelPath, QStringLiteral("-f"), outWav};
}

QStringList VoiceServerManager::buildPiperResidentArgs(const QString &modelPath, const QString &outDir)
{
    return {QStringLiteral("-m"), modelPath,
            QStringLiteral("--json-input"),
            QStringLiteral("--output_dir"), outDir};
}

QStringList VoiceServerManager::buildPocketServerArgs(const QString &scriptPath,
                                                       const QString &language,
                                                       const QString &voice,
                                                       const QString &modelConfig,
                                                       int port, bool quantize)
{
    QStringList args{scriptPath,
                     QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                     QStringLiteral("--port"), QString::number(port),
                     QStringLiteral("--language"), language.isEmpty()
                         ? QStringLiteral("spanish") : language};
    if (!voice.trimmed().isEmpty())
        args << QStringLiteral("--voice") << voice.trimmed();
    if (!modelConfig.trimmed().isEmpty())
        args << QStringLiteral("--config") << modelConfig.trimmed();
    if (quantize) args << QStringLiteral("--quantize");
    return args;
}

QString VoiceServerManager::installRoot()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base + QStringLiteral("/voice");
}

QString VoiceServerManager::pocketRoot()
{
    return installRoot() + QStringLiteral("/pocket-tts");
}

QString VoiceServerManager::pocketVenvDir()
{
    return pocketRoot() + QStringLiteral("/venv");
}

QString VoiceServerManager::pocketManagedPythonPath()
{
#ifdef Q_OS_WIN
    return QDir(pocketVenvDir()).filePath(QStringLiteral("Scripts/python.exe"));
#else
    return QDir(pocketVenvDir()).filePath(QStringLiteral("bin/python"));
#endif
}

QString VoiceServerManager::pocketCacheDir()
{
    return pocketRoot() + QStringLiteral("/cache");
}

QString VoiceServerManager::pocketServerScriptPath()
{
    return pocketRoot() + QStringLiteral("/pocket_tts_server.py");
}

int VoiceServerManager::pocketDefaultPort()
{
    return 8200;
}

bool VoiceServerManager::pocketRuntimeInstalled()
{
    return QFileInfo::exists(pocketManagedPythonPath())
        && QFileInfo::exists(QDir(pocketRoot()).filePath(QStringLiteral("installed.ok")));
}

bool VoiceServerManager::pocketServerScriptAvailable()
{
    if (QFileInfo::exists(pocketServerScriptPath())
        || QFile(QStringLiteral(":/tools/pocket_tts_server.py")).exists())
        return true;
    const QString appDir = QCoreApplication::applicationDirPath();
    return QFileInfo::exists(QDir(appDir).filePath(QStringLiteral("../../tools/pocket_tts_server.py")))
        || QFileInfo::exists(QDir(appDir).filePath(QStringLiteral("../tools/pocket_tts_server.py")))
        || QFileInfo::exists(QDir::current().filePath(QStringLiteral("tools/pocket_tts_server.py")));
}

bool VoiceServerManager::ensurePocketServerScript(QString *error)
{
    const QString destination = pocketServerScriptPath();
    if (QFileInfo::exists(destination)) return true;
    if (!QDir().mkpath(pocketRoot())) {
        if (error) *error = QStringLiteral("no se pudo crear la carpeta de Pocket TTS");
        return false;
    }

    QFile resource(QStringLiteral(":/tools/pocket_tts_server.py"));
    if (resource.exists() && resource.open(QIODevice::ReadOnly)) {
        QFile output(destination);
        if (output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QByteArray data = resource.readAll();
            if (output.write(data) == data.size()) {
                output.close();
                return true;
            }
        }
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(QStringLiteral("../../tools/pocket_tts_server.py")),
        QDir(appDir).filePath(QStringLiteral("../tools/pocket_tts_server.py")),
        QDir::current().filePath(QStringLiteral("tools/pocket_tts_server.py"))};
    for (const QString &source : candidates) {
        if (!QFileInfo::exists(source)) continue;
        if (QFile::copy(source, destination)) return true;
    }
    if (error) *error = QStringLiteral("no se encontró el helper pocket_tts_server.py");
    return false;
}

QString VoiceServerManager::modelPath(const QString &engineId)
{
    const QVariantMap e = sttEngine(engineId);
    if (e.isEmpty() || e.value("modelFile").toString().isEmpty()) return {};
    return installRoot() + QStringLiteral("/") + engineId + QStringLiteral("/")
           + e.value("modelFile").toString();
}

bool VoiceServerManager::modelInstalled(const QString &engineId) const
{
    const QString p = modelPath(engineId);
    return !p.isEmpty() && QFile::exists(p);
}

QString VoiceServerManager::endpointPath(const QString &engineId)
{
    const QVariantMap e = sttEngine(engineId);
    return e.value("endpointPath", QStringLiteral("/inference")).toString();
}

QStringList VoiceServerManager::buildWhisperArgs(const QString &modelPath, const QString &host,
                                                 int port, const QString &language)
{
    QStringList a{QStringLiteral("-m"), modelPath,
                  QStringLiteral("--host"), host,
                  QStringLiteral("--port"), QString::number(port)};
    if (!language.isEmpty() && language != QLatin1String("auto"))
        a << QStringLiteral("-l") << language;
    return a;
}

void VoiceServerManager::installModel(const QString &engineId)
{
    if (m_reply || !m_dlQueue.isEmpty()) {
        emit installFinished(engineId, false, QStringLiteral("descarga en curso")); return;
    }
    const QVariantMap e = sttEngine(engineId);
    if (e.isEmpty()) { emit installFinished(engineId, false, QStringLiteral("motor desconocido")); return; }
    if (!e.value(QStringLiteral("installable"), true).toBool()
        || e.value(QStringLiteral("modelUrl")).toString().isEmpty()) {
        emit installFinished(engineId, false,
                             QStringLiteral("este motor requiere configurar un sidecar externo"));
        return;
    }
    startDownloadQueue(engineId, {{ e.value("modelUrl").toString(), modelPath(engineId) }});
}

void VoiceServerManager::installTtsVoice(const QString &voiceId)
{
    if (m_reply || !m_dlQueue.isEmpty()) {
        emit installFinished(voiceId, false, QStringLiteral("descarga en curso")); return;
    }
    const QVariantMap v = ttsVoice(voiceId);
    if (v.isEmpty()) { emit installFinished(voiceId, false, QStringLiteral("voz desconocida")); return; }
    const QString onnx = ttsModelPath(voiceId);
    startDownloadQueue(voiceId, {
        { v.value("modelUrl").toString(), onnx },
        { v.value("jsonUrl").toString(),  onnx + QStringLiteral(".json") }});
}

void VoiceServerManager::startDownloadQueue(const QString &id,
                                            const QList<QPair<QString, QString>> &files)
{
    m_engineId = id;
    m_dlQueue = files;
    m_dlTotal = files.size();
    emit installProgress(id, 0, QStringLiteral("Descargando…"));
    downloadNext();
}

void VoiceServerManager::downloadNext()
{
    if (m_dlQueue.isEmpty()) { emit installFinished(m_engineId, true, QString()); return; }
    const auto job = m_dlQueue.first();
    const QString url = job.first;
    const QString dst = job.second;
    QDir().mkpath(QFileInfo(dst).absolutePath());
    m_file = new QFile(dst + QStringLiteral(".part"));
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete m_file; m_file = nullptr;
        m_dlQueue.clear();
        emit installFinished(m_engineId, false, QStringLiteral("no se pudo escribir el archivo"));
        return;
    }
    QNetworkRequest req((QUrl(url)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam.get(req);
    const int doneFiles = m_dlTotal - m_dlQueue.size();
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_file) m_file->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this, doneFiles](qint64 r, qint64 t) {
        const int filePct = (t > 0) ? int(r * 100 / t) : 0;
        const int pct = m_dlTotal > 0 ? (doneFiles * 100 + filePct) / m_dlTotal : filePct;
        emit installProgress(m_engineId, pct, QStringLiteral("Descargando… %1%").arg(pct));
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, dst]() {
        QNetworkReply *r = m_reply; m_reply = nullptr;
        const bool ok = (r->error() == QNetworkReply::NoError);
        const QString err = r->errorString();
        if (m_file) { m_file->write(r->readAll()); m_file->close(); }
        const QString part = m_file ? m_file->fileName() : QString();
        delete m_file; m_file = nullptr;
        r->deleteLater();
        if (ok && !part.isEmpty()) {
            QFile::remove(dst);
            if (!QFile::rename(part, dst)) {
                m_dlQueue.clear();
                emit installFinished(m_engineId, false, QStringLiteral("no se pudo finalizar el archivo"));
                return;
            }
            m_dlQueue.removeFirst();
            downloadNext();
        } else {
            if (!part.isEmpty()) QFile::remove(part);
            m_dlQueue.clear();
            emit installFinished(m_engineId, false, ok ? QStringLiteral("descarga incompleta") : err);
        }
    });
}

void VoiceServerManager::cancelInstall()
{
    m_dlQueue.clear();
    if (m_reply) m_reply->abort();
}

// ── Binarios ─────────────────────────────────────────────────────────────────

QString VoiceServerManager::binDir()
{
    return installRoot() + QStringLiteral("/bin");
}

QString VoiceServerManager::defaultBinaryUrl(const QString &kind)
{
#if defined(Q_OS_WIN)
    if (kind == QLatin1String("piper"))
        return QStringLiteral("https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_windows_amd64.zip");
    if (kind == QLatin1String("whisper-server"))
        return QStringLiteral("https://github.com/ggml-org/whisper.cpp/releases/latest/download/whisper-bin-x64.zip");
#elif defined(Q_OS_LINUX)
    if (kind == QLatin1String("piper"))
        return QStringLiteral("https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz");
    // whisper.cpp no publica binario Linux prearmado → vacío (se compila).
#endif
    return {};
}

QString VoiceServerManager::installedBinaryPath(const QString &kind)
{
    // El paquete oficial de whisper.cpp trae parakeet-cli junto con
    // whisper-server. Aceptamos ambos lugares para que Parakeet quede
    // administrado por la misma descarga y no dupliquemos ~100 MB de binarios.
    const QStringList dirs = kind == QLatin1String("parakeet-cli")
        ? QStringList{binDir() + QStringLiteral("/whisper-server"),
                      binDir() + QStringLiteral("/parakeet-cli")}
        : QStringList{binDir() + QStringLiteral("/") + kind};
    QStringList names;
    if (kind == QLatin1String("piper")) names << "piper.exe" << "piper";
    else if (kind == QLatin1String("parakeet-cli")) names << "parakeet-cli.exe" << "parakeet-cli";
    else names << "whisper-server.exe" << "server.exe" << "whisper-server" << "server";
    for (const QString &dir : dirs) {
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            if (names.contains(it.fileName(), Qt::CaseInsensitive))
                return it.filePath();
        }
    }
    return {};
}

void VoiceServerManager::installBinary(const QString &kind, const QString &urlOverride)
{
    if (m_reply || m_extractProc) {
        emit binaryInstalled(kind, false, QString(), QStringLiteral("instalación en curso")); return;
    }
    const QString url = urlOverride.isEmpty() ? defaultBinaryUrl(kind) : urlOverride;
    if (url.isEmpty()) {
        emit binaryInstalled(kind, false, QString(),
            QStringLiteral("sin binario prearmado para esta plataforma; configurá la ruta a mano"));
        return;
    }
    const QString destDir = binDir() + QStringLiteral("/") + kind;
    QDir(destDir).removeRecursively();
    QDir().mkpath(destDir);
    const bool targz = url.endsWith(QLatin1String(".tar.gz")) || url.endsWith(QLatin1String(".tgz"));
    const QString archive = destDir + (targz ? QStringLiteral("/_dl.tar.gz") : QStringLiteral("/_dl.zip"));

    m_binKind = kind;
    m_file = new QFile(archive);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete m_file; m_file = nullptr;
        emit binaryInstalled(kind, false, QString(), QStringLiteral("no se pudo escribir el archivo")); return;
    }
    emit installProgress(kind, 0, QStringLiteral("Descargando binario…"));
    QNetworkRequest req((QUrl(url)));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam.get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_file) m_file->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this, kind](qint64 r, qint64 t) {
        const int pct = (t > 0) ? int(r * 90 / t) : 0;   // 0–90% descarga, 90–100 extracción
        emit installProgress(kind, pct, QStringLiteral("Descargando binario… %1%").arg(pct));
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, kind, archive, destDir]() {
        QNetworkReply *r = m_reply; m_reply = nullptr;
        const bool ok = (r->error() == QNetworkReply::NoError);
        const QString err = r->errorString();
        if (m_file) { m_file->write(r->readAll()); m_file->close(); delete m_file; m_file = nullptr; }
        r->deleteLater();
        if (!ok) { emit binaryInstalled(kind, false, QString(), err); return; }
        emit installProgress(kind, 92, QStringLiteral("Extrayendo…"));
        extractAndLocate(kind, archive, destDir);
    });
}

void VoiceServerManager::extractAndLocate(const QString &kind, const QString &archive,
                                          const QString &destDir)
{
    m_extractProc = new QProcess(this);
    QString prog; QStringList args;
#if defined(Q_OS_WIN)
    if (archive.endsWith(QLatin1String(".zip"))) {
        prog = QStringLiteral("powershell");
        args << QStringLiteral("-NoProfile") << QStringLiteral("-Command")
             << QStringLiteral("Expand-Archive -Force -LiteralPath '%1' -DestinationPath '%2'")
                .arg(archive, destDir);
    } else {
        prog = QStringLiteral("tar"); args << QStringLiteral("-xf") << archive
             << QStringLiteral("-C") << destDir;
    }
#else
    prog = QStringLiteral("tar"); args << QStringLiteral("-xf") << archive
         << QStringLiteral("-C") << destDir;
#endif
    connect(m_extractProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, kind, destDir, archive](int code, QProcess::ExitStatus) {
        QProcess *p = m_extractProc; m_extractProc = nullptr;
        if (p) p->deleteLater();
        QFile::remove(archive);
        if (code != 0) { emit binaryInstalled(kind, false, QString(), QStringLiteral("falló la extracción")); return; }
        // Localizar el ejecutable extraído.
        const QString found = installedBinaryPath(kind);
        if (found.isEmpty())
            emit binaryInstalled(kind, false, QString(),
                QStringLiteral("descargado pero no se encontró el ejecutable en el paquete"));
        else {
            emit installProgress(kind, 100, QStringLiteral("Listo"));
            emit binaryInstalled(kind, true, found, QString());
        }
    });
    connect(m_extractProc, &QProcess::errorOccurred, this, [this, kind](QProcess::ProcessError) {
        if (!m_extractProc) return;
        m_extractProc->deleteLater(); m_extractProc = nullptr;
        emit binaryInstalled(kind, false, QString(), QStringLiteral("no se pudo extraer (falta tar/powershell)"));
    });
    m_extractProc->start(prog, args);
}
