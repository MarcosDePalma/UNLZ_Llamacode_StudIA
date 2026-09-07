#pragma once
#include <QList>
#include <QString>

// Ingesta de modelos ya descargados por Ollama. Ollama guarda los pesos como
// blobs sin extensión (nombrados por su digest sha256) + manifests JSON que
// mapean nombre:tag → capas. Los blobs de la capa "model" SON GGUF, así que se
// pueden reusar con llama.cpp directamente sin volver a descargar.
//
// Layout en disco:
//   <store>/manifests/registry.ollama.ai/library/<model>/<tag>   (JSON)
//   <store>/blobs/sha256-<hex>                                    (pesos GGUF)
//
// Todo estático y puro (sin QObject) para testear sin tocar el disco real.
class OllamaImporter
{
public:
    struct Entry {
        QString name;         // "qwen2.5:3b"
        QString blobPath;     // ruta absoluta al blob GGUF de la capa "model"
        qint64  sizeBytes = 0;
        QString mmprojPath;   // capa "projector" (mmproj) si el modelo es multimodal; "" si no
    };

    // Directorio de store por defecto: $OLLAMA_MODELS, si no ~/.ollama/models.
    static QString defaultStoreDir();

    // ¿`path` parece un store de Ollama? (tiene subdirs manifests/ y blobs/).
    static bool looksLikeStore(const QString &path);

    // Resuelve un scheme "ollama://[dir]" a un directorio de store. Cadena vacía
    // tras el scheme = store por defecto. Devuelve "" si `uri` no es ollama://.
    static QString resolveStoreDir(const QString &uri);

    // Recorre los manifests y devuelve una Entry por modelo con capa "model"
    // presente como blob. No lee los GGUF (eso lo hace el scanner después).
    static QList<Entry> scan(const QString &storeDir);
};
