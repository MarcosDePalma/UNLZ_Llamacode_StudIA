#pragma once
#include "ModelRoot.h"
#include "CatalogModel.h"
#include <QList>
#include <QMap>
#include <QObject>

class GGUFScanner : public QObject
{
    Q_OBJECT
public:
    explicit GGUFScanner(QObject *parent = nullptr);

    // Synchronous scan — call from thread pool. Rutea por root.kind:
    // "ollama" → blobs del store de Ollama; cualquier otro → *.gguf en disco.
    QList<CatalogModel> scan(const ModelRoot &root);
    // Igual que scan(), pero reutiliza la metadata ya catalogada cuando el
    // archivo no cambió. Esto conserva toda la información (incluida la
    // composición real) sin volver a leer headers de modelos enormes en cada
    // inicio.
    QList<CatalogModel> scan(const ModelRoot &root,
                             const QList<CatalogModel> &cached);

    // Inferencia pura sobre el nombre de archivo (públicas para tests).
    static QString inferFamily(const QString &fileName);
    static QString inferQuant(const QString &fileName);
    static bool isVisionCandidate(const QString &fileName);
    static bool isDraftCandidate(const QString &fileName, qint64 sizeBytes);

    // Lectura de composición real de tensores desde el header GGUF.
    struct Composition {
        bool valid = false;
        QMap<QString, int> typeTensors;     // nombre dtype -> nº de tensores
        QMap<QString, qint64> typeElements; // nombre dtype -> nº de elementos
        qint64 totalElements = 0;
        // Elementos que viven en tablas de lookup Ngram/PLE (arquitectura Qwen4).
        // No son compute: se pueden mandar al backend CPU y dejar que mmap los
        // pagine desde el SSD, asi que NO cuentan como peso residente.
        // OJO: es por archivo. En un GGUF en shards hay que sumar los shards.
        qint64 ngramElements = 0;
        QString dominantQuant;              // dtype cuantizado con más elementos
        double bpw = 0.0;                   // file_size*8 / totalElements
        QString architecture;               // general.architecture
        qint64 parameterCount = 0;           // general.parameter_count
        int trainedContext = 0;              // *.context_length
        QString breakdown() const;          // "q4_0:265, q6_k:1, f32:392"
    };
    static Composition readComposition(const QString &filePath, qint64 fileSizeBytes);
    // Un GGUF partido ("...-00001-of-00004.gguf") reparte los tensores entre los
    // shards, y el primero suele traer SOLO metadata (0 tensores). Leer nada mas
    // el shard registrado en el catalogo da totalElements=0 y ngramElements=0,
    // que es peor que no tener el dato. Esto suma los shards hermanos.
    // Con un archivo no particionado equivale a readComposition.
    static Composition readCompositionAllShards(const QString &firstShardPath);
    // Devuelve los paths de todos los shards de un GGUF particionado (incluido el
    // propio), en orden. Lista de un solo elemento si no esta particionado.
    static QStringList shardPaths(const QString &anyShardPath);
    static QString ggmlTypeName(quint32 t);
    // Tabla de lookup Ngram/PLE (blk.N.ple_key / ple_value, per_layer_token_embd).
    // Deliberadamente NO matchea ple_norm_*, ple_conv1d ni per_layer_proj_norm:
    // esos son compute, sacarlos de la GPU es perdida neta.
    static bool isNgramLookupTensor(const QString &tensorName);
    // Heuristica por NOMBRE de archivo para la familia Qwen4 con tablas Ngram/PLE.
    // La fuente de verdad es general.architecture == "qwen4exp" dentro del GGUF,
    // pero hay chequeos (health de perfiles) que solo tienen el nombre a mano.
    static bool isNgramArchitectureName(const QString &fileName);

    // True si el modelo es un Gemma QAT con quant real q4_0 "crudo" (Google-style):
    // llama.cpp aplica scales fp16 sobre un QAT entrenado con scales bf16 → clipping
    // de los pesos grandes → degradación (peor a partir de ~50-100k ctx). Los dynamic
    // quants de unsloth (UD-* / *K_XL) corrigen esto, así que se excluyen.
    static bool isDegradedQatQuant(const QString &fileName, const QString &family,
                                   const QString &quantReal);

signals:
    void progress(const QString &rootId, int found);
};
