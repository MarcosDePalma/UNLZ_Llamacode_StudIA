#include "OllamaImporter.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

QString OllamaImporter::defaultStoreDir()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString override = env.value(QStringLiteral("OLLAMA_MODELS"));
    if (!override.isEmpty())
        return QDir::cleanPath(override);

    // ~/.ollama/models (mismo layout en Windows/macOS/Linux).
    const QString home = QDir::homePath();
    return QDir::cleanPath(home + QStringLiteral("/.ollama/models"));
}

bool OllamaImporter::looksLikeStore(const QString &path)
{
    if (path.isEmpty()) return false;
    const QDir d(path);
    return d.exists(QStringLiteral("manifests")) && d.exists(QStringLiteral("blobs"));
}

QString OllamaImporter::resolveStoreDir(const QString &uri)
{
    const QString scheme = QStringLiteral("ollama://");
    if (!uri.startsWith(scheme))
        return QString();
    const QString rest = uri.mid(scheme.size()).trimmed();
    if (rest.isEmpty())
        return defaultStoreDir();
    return QDir::cleanPath(rest);
}

QList<OllamaImporter::Entry> OllamaImporter::scan(const QString &storeDir)
{
    QList<Entry> out;
    const QString manifestsRoot = QDir::cleanPath(storeDir + QStringLiteral("/manifests"));
    const QString blobsRoot = QDir::cleanPath(storeDir + QStringLiteral("/blobs"));
    if (!QFileInfo::exists(manifestsRoot) || !QFileInfo::exists(blobsRoot))
        return out;

    // Cada archivo bajo manifests/ es un manifest de un tag. El nombre del modelo
    // se deriva de la ruta: .../<model>/<tag> → "<model>:<tag>". Si vive bajo el
    // registry estándar (registry.ollama.ai/library) se recorta ese prefijo para
    // dejar nombres cortos ("qwen2.5:3b"); si es un namespace custom se conserva.
    QDirIterator it(manifestsRoot, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString manifestPath = it.next();

        QFile f(manifestPath);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!doc.isObject()) continue;
        const QJsonArray layers = doc.object().value(QStringLiteral("layers")).toArray();
        if (layers.isEmpty()) continue;

        // "sha256:<hex>" → ruta del blob "sha256-<hex>" (o "" si no existe).
        auto blobFor = [&blobsRoot](const QString &digest) -> QString {
            if (digest.isEmpty()) return QString();
            QString file = digest;
            file.replace(QLatin1Char(':'), QLatin1Char('-'));
            const QString path = QDir::cleanPath(blobsRoot + QStringLiteral("/") + file);
            return QFileInfo::exists(path) ? path : QString();
        };

        // Capa de pesos (mediaType …image.model) y, si es multimodal, la capa
        // projector (mmproj, …image.projector) que hay que emparejar con --mmproj.
        QString modelDigest, projDigest;
        qint64 size = 0;
        for (const QJsonValue &lv : layers) {
            const QJsonObject l = lv.toObject();
            const QString mt = l.value(QStringLiteral("mediaType")).toString();
            if (mt.endsWith(QStringLiteral("model"))) {
                modelDigest = l.value(QStringLiteral("digest")).toString();
                size = l.value(QStringLiteral("size")).toVariant().toLongLong();
            } else if (mt.endsWith(QStringLiteral("projector"))) {
                projDigest = l.value(QStringLiteral("digest")).toString();
            }
        }
        const QString blobPath = blobFor(modelDigest);
        if (blobPath.isEmpty()) continue;

        // Nombre model:tag desde la ruta relativa.
        QString rel = QDir(manifestsRoot).relativeFilePath(manifestPath);
        rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QString stdPrefix = QStringLiteral("registry.ollama.ai/library/");
        if (rel.startsWith(stdPrefix))
            rel = rel.mid(stdPrefix.size());
        const int lastSlash = rel.lastIndexOf(QLatin1Char('/'));
        QString name = rel;
        if (lastSlash > 0)
            name = rel.left(lastSlash) + QLatin1Char(':') + rel.mid(lastSlash + 1);

        Entry e;
        e.name = name;
        e.blobPath = blobPath;
        e.sizeBytes = size > 0 ? size : QFileInfo(blobPath).size();
        e.mmprojPath = blobFor(projDigest);
        out.append(e);
    }

    return out;
}
