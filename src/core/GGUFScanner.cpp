#include "GGUFScanner.h"
#include "OllamaImporter.h"
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUuid>
#include <QRegularExpression>
#include <QStringList>
#include <limits>

GGUFScanner::GGUFScanner(QObject *parent) : QObject(parent) {}

namespace {

// Construye un CatalogModel para un GGUF. `filePath` es la ruta física del blob/
// archivo; `displayName` es el nombre a mostrar/inferir (nombre de archivo para
// roots de disco, "model:tag" para blobs de Ollama, que no tienen extensión).
CatalogModel buildCatalogModel(const ModelRoot &root, const QString &filePath,
                               const QString &displayName)
{
    const QFileInfo info(filePath);

    CatalogModel m;
    // Id DETERMINISTA por ruta absoluta (UUIDv5). Antes era QUuid::createUuid()
    // (aleatorio por scan): cada rescan reasignaba ids y orfanaba los modelId
    // guardados en los perfiles (→ "No model selected" en benchmark). El mismo
    // namespace + ruta se replica en tools/relink_profiles.py para migrar perfiles.
    static const QUuid kCatalogNs(QStringLiteral("a1b2c3d4-e5f6-4a5b-8c7d-0e1f2a3b4c5d"));
    m.id = QUuid::createUuidV5(kCatalogNs, filePath.toUtf8()).toString(QUuid::WithoutBraces);
    m.rootId = root.id;
    m.absolutePath = filePath;
    m.fileName = displayName;
    m.sizeBytes = info.size();
    m.mtime = info.lastModified();
    m.familyHint = GGUFScanner::inferFamily(displayName);
    m.quantHint = GGUFScanner::inferQuant(displayName);

    // Composición real de tensores: el nombre de archivo miente (Google "Q4_0"
    // trae Q6_K/F16; unsloth "Q4_K_XL" es casi todo Q4_0). Clasificamos por el
    // contenido real y marcamos mismatch para avisar en UI.
    const GGUFScanner::Composition comp =
        GGUFScanner::readComposition(filePath, info.size());
    if (comp.valid) {
        m.quantReal = comp.dominantQuant;
        m.tensorBreakdown = comp.breakdown();
        m.bpw = comp.bpw;
        m.quantMismatch =
            !m.quantReal.isEmpty() && m.quantHint != "unknown"
                && m.quantHint.compare(m.quantReal, Qt::CaseInsensitive) != 0;
        m.architecture = comp.architecture;
        m.parameterCount = comp.parameterCount;
        m.trainedContext = comp.trainedContext;
    }

    m.isVisionCandidate = GGUFScanner::isVisionCandidate(displayName);
    m.isDraftCandidate = GGUFScanner::isDraftCandidate(displayName, info.size());
    m.isAvailable = true;
    return m;
}

} // namespace

QList<CatalogModel> GGUFScanner::scan(const ModelRoot &root)
{
    return scan(root, {});
}

QList<CatalogModel> GGUFScanner::scan(const ModelRoot &root,
                                      const QList<CatalogModel> &cached)
{
    QList<CatalogModel> results;

    QMap<QString, CatalogModel> cacheByPath;
    for (const CatalogModel &m : cached)
        cacheByPath.insert(m.absolutePath, m);

    auto appendCachedOrBuild = [&](const QString &path, const QString &name) {
        const QFileInfo info(path);
        const auto it = cacheByPath.constFind(path);
        if (it != cacheByPath.constEnd()
            && it->sizeBytes == info.size()
            && it->mtime == info.lastModified()) {
            CatalogModel cachedModel = it.value();
            cachedModel.rootId = root.id;
            cachedModel.isAvailable = true;
            results.append(cachedModel);
            return;
        }
        results.append(buildCatalogModel(root, path, name));
    };

    // Store de Ollama: los pesos son blobs sin extensión, resueltos vía manifests.
    if (root.kind == QLatin1String("ollama")) {
        const QList<OllamaImporter::Entry> entries = OllamaImporter::scan(root.path);
        for (const OllamaImporter::Entry &e : entries) {
            appendCachedOrBuild(e.blobPath, e.name);
            emit progress(root.id, results.size());
            // Modelo multimodal: el projector (mmproj) entra como entrada aparte,
            // marcada como candidata de visión para poder emparejar --mmproj.
            if (!e.mmprojPath.isEmpty()) {
                const QString mmName = e.name + QStringLiteral(" (mmproj)");
                const QFileInfo mmInfo(e.mmprojPath);
                const auto cachedIt = cacheByPath.constFind(e.mmprojPath);
                CatalogModel mm;
                if (cachedIt != cacheByPath.constEnd()
                    && cachedIt->sizeBytes == mmInfo.size()
                    && cachedIt->mtime == mmInfo.lastModified()) {
                    mm = cachedIt.value();
                    mm.rootId = root.id;
                    mm.isAvailable = true;
                } else {
                    mm = buildCatalogModel(root, e.mmprojPath, mmName);
                }
                mm.isVisionCandidate = true;
                results.append(mm);
                emit progress(root.id, results.size());
            }
        }
        return results;
    }

    QDirIterator it(root.path, {"*.gguf", "*.GGUF", "*.ninfer", "*.NINFER"},
                    QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const QString filePath = it.next();
        appendCachedOrBuild(filePath, QFileInfo(filePath).fileName());
        emit progress(root.id, results.size());
    }

    return results;
}

QString GGUFScanner::inferFamily(const QString &fileName)
{
    const QString lower = fileName.toLower();
    const QStringList families = {
        "deepseek", "codellama", "starcoder", "mixtral", "mistral",
        "vicuna", "wizard", "llama", "falcon", "gemma", "qwen",
        "solar", "orca", "yi", "phi"
    };
    for (const QString &f : families)
        if (lower.contains(f)) return f;
    return "unknown";
}

QString GGUFScanner::inferQuant(const QString &fileName)
{
    static const QRegularExpression re(
        R"((IQ[1-4]_[A-Z_]+|Q[1-8]_K_[SML]|Q[1-8]_[0-9]|BF16|F16|F32))",
        QRegularExpression::CaseInsensitiveOption
    );
    const auto match = re.match(fileName);
    return match.hasMatch() ? match.captured(1).toUpper() : "unknown";
}

bool GGUFScanner::isVisionCandidate(const QString &fileName)
{
    const QString lower = fileName.toLower();
    return lower.contains("mmproj") || lower.contains("vision")
           || lower.contains("clip") || lower.contains("llava");
}

bool GGUFScanner::isDraftCandidate(const QString &fileName, qint64 sizeBytes)
{
    const QString lower = fileName.toLower();
    if (lower.contains("draft") || lower.contains("small")) return true;
    // Heuristic: models under 2GB are draft candidates
    return sizeBytes > 0 && sizeBytes < 2LL * 1024 * 1024 * 1024;
}

bool GGUFScanner::isDegradedQatQuant(const QString &fileName, const QString &family,
                                     const QString &quantReal)
{
    const QString lower = fileName.toLower();
    if (!lower.contains("qat")) return false;
    if (!family.toLower().contains("gemma")) return false; // observado en Gemma4 QAT
    if (quantReal.compare("q4_0", Qt::CaseInsensitive) != 0) return false;
    // Dynamic quants de unsloth recuperan la calidad pese a ser q4_0 por dentro.
    if (lower.contains("ud-") || lower.contains("ud_")
        || lower.contains("unsloth") || lower.contains("k_xl"))
        return false;
    return true;
}

// ---- Parser de header GGUF (composición real de tensores) ------------------

QString GGUFScanner::ggmlTypeName(quint32 t)
{
    // Subconjunto estable de ggml_type (llama.cpp ggml.h).
    switch (t) {
    case 0:  return "f32";
    case 1:  return "f16";
    case 2:  return "q4_0";
    case 3:  return "q4_1";
    case 6:  return "q5_0";
    case 7:  return "q5_1";
    case 8:  return "q8_0";
    case 9:  return "q8_1";
    case 10: return "q2_k";
    case 11: return "q3_k";
    case 12: return "q4_k";
    case 13: return "q5_k";
    case 14: return "q6_k";
    case 15: return "q8_k";
    case 16: return "iq2_xxs";
    case 17: return "iq2_xs";
    case 18: return "iq3_xxs";
    case 19: return "iq1_s";
    case 20: return "iq4_nl";
    case 21: return "iq3_s";
    case 22: return "iq2_s";
    case 23: return "iq4_xs";
    case 24: return "i8";
    case 25: return "i16";
    case 26: return "i32";
    case 27: return "i64";
    case 28: return "f64";
    case 29: return "iq1_m";
    case 30: return "bf16";
    default: return QStringLiteral("t%1").arg(t);
    }
}

namespace {

// Lector secuencial little-endian con control de fin de buffer.
struct LeReader {
    const uchar *p;
    qint64 n;
    qint64 i = 0;
    bool ok = true;

    bool need(qint64 k) {
        if (!ok || i + k > n) { ok = false; return false; }
        return true;
    }
    quint32 u32() {
        if (!need(4)) return 0;
        quint32 v = quint32(p[i]) | (quint32(p[i+1])<<8)
                  | (quint32(p[i+2])<<16) | (quint32(p[i+3])<<24);
        i += 4; return v;
    }
    quint64 u64() {
        if (!need(8)) return 0;
        quint64 v = 0;
        for (int b = 0; b < 8; ++b) v |= quint64(p[i+b]) << (8*b);
        i += 8; return v;
    }
    void skip(qint64 k) { if (need(k)) i += k; }
    void skipStr() { quint64 len = u64(); skip(qint64(len)); }
    QString str() {
        const quint64 len = u64();
        if (len > quint64(std::numeric_limits<qint64>::max()) || !need(qint64(len)))
            return {};
        const QString value = QString::fromUtf8(reinterpret_cast<const char *>(p + i),
                                                qsizetype(len));
        i += qint64(len);
        return value;
    }
};

bool readUnsignedMeta(LeReader &r, quint32 type, quint64 &value) {
    switch (type) {
    case 0: value = r.need(1) ? r.p[r.i++] : 0; return r.ok;
    case 2: if (!r.need(2)) return false; value = quint64(r.p[r.i]) | (quint64(r.p[r.i+1]) << 8); r.i += 2; return true;
    case 4: value = r.u32(); return r.ok;
    case 10: value = r.u64(); return r.ok;
    default: return false;
    }
}

// Tamaño en bytes de un valor escalar de metadata GGUF por type-id.
qint64 ggufScalarSize(quint32 t) {
    switch (t) {
    case 0: case 1: case 7: return 1;   // uint8/int8/bool
    case 2: case 3: return 2;           // uint16/int16
    case 4: case 5: case 6: return 4;   // uint32/int32/float32
    case 10: case 11: case 12: return 8;// uint64/int64/float64
    default: return -1;                 // 8=string, 9=array -> manejo aparte
    }
}

// Salta un valor de metadata (escalar, string o array).
void skipMetaValue(LeReader &r, quint32 type) {
    if (type == 8) { r.skipStr(); return; }           // STRING
    if (type == 9) {                                   // ARRAY
        quint32 elemType = r.u32();
        quint64 count = r.u64();
        if (elemType == 8) {
            for (quint64 k = 0; k < count && r.ok; ++k) r.skipStr();
        } else {
            qint64 sz = ggufScalarSize(elemType);
            if (sz < 0) { r.ok = false; return; }      // array anidado: no soportado
            r.skip(qint64(count) * sz);
        }
        return;
    }
    qint64 sz = ggufScalarSize(type);
    if (sz < 0) { r.ok = false; return; }
    r.skip(sz);
}

} // namespace

QString GGUFScanner::Composition::breakdown() const
{
    QStringList parts;
    for (auto it = typeTensors.constBegin(); it != typeTensors.constEnd(); ++it)
        parts << QStringLiteral("%1:%2").arg(it.key()).arg(it.value());
    return parts.join(", ");
}

bool GGUFScanner::isNgramArchitectureName(const QString &fileName)
{
    if (fileName.isEmpty())
        return false;
    const QString base = QFileInfo(fileName).fileName().toLower();
    // Qwen3.8-Flash-Next y los Qwen4 que hereden la arquitectura qwen4exp.
    static const QRegularExpression re(
        QStringLiteral(R"((flash[-_.]?next)|(^|[-_.])qwen4)"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(base).hasMatch();
}

bool GGUFScanner::isNgramLookupTensor(const QString &tensorName)
{
    // Nombres tomados de llama-arch.cpp del soporte de qwen4exp (PR 27742):
    //   blk.%d.ple_key / blk.%d.ple_value / per_layer_token_embd
    // Los ple_norm_* y ple_conv1d comparten prefijo pero son compute, por eso el
    // match es exacto por sufijo y no un "contains(ple)".
    if (tensorName.isEmpty())
        return false;
    const QString base = tensorName.endsWith(QLatin1String(".weight"))
        ? tensorName.left(tensorName.size() - 7)
        : tensorName;
    if (base == QLatin1String("per_layer_token_embd"))
        return true;
    static const QRegularExpression blockRe(
        QStringLiteral(R"(^blk\.[0-9]+\.ple_(key|value)$)"));
    return blockRe.match(base).hasMatch();
}

QStringList GGUFScanner::shardPaths(const QString &anyShardPath)
{
    const QFileInfo info(anyShardPath);
    const QString name = info.fileName();
    // Convencion de gguf-split: "<base>-00001-of-00004.gguf".
    static const QRegularExpression re(
        QStringLiteral(R"(^(.*)-([0-9]{5})-of-([0-9]{5})\.gguf$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(name);
    if (!m.hasMatch())
        return {anyShardPath};

    const QString base = m.captured(1);
    const int total = m.captured(3).toInt();
    if (total <= 0 || total > 999)
        return {anyShardPath};

    const QDir dir = info.absoluteDir();
    QStringList out;
    for (int i = 1; i <= total; ++i) {
        const QString shard = QStringLiteral("%1-%2-of-%3.gguf")
            .arg(base,
                 QString::number(i).rightJustified(5, QLatin1Char('0')),
                 QString::number(total).rightJustified(5, QLatin1Char('0')));
        const QString full = dir.absoluteFilePath(shard);
        // Un shard faltante no invalida el resto: se suma lo que haya y el
        // caller ve un total menor, en vez de perder el dato entero.
        if (QFile::exists(full))
            out << full;
    }
    return out.isEmpty() ? QStringList{anyShardPath} : out;
}

GGUFScanner::Composition GGUFScanner::readCompositionAllShards(const QString &firstShardPath)
{
    const QStringList shards = shardPaths(firstShardPath);
    if (shards.size() == 1)
        return readComposition(firstShardPath, QFileInfo(firstShardPath).size());

    Composition agg;
    for (const QString &shard : shards) {
        const Composition c = readComposition(shard, QFileInfo(shard).size());
        agg.totalElements += c.totalElements;
        agg.ngramElements += c.ngramElements;
        for (auto it = c.typeTensors.constBegin(); it != c.typeTensors.constEnd(); ++it)
            agg.typeTensors[it.key()] += it.value();
        for (auto it = c.typeElements.constBegin(); it != c.typeElements.constEnd(); ++it)
            agg.typeElements[it.key()] += it.value();
        // Los metadatos generales viven en el primer shard que los traiga.
        if (agg.architecture.isEmpty())   agg.architecture = c.architecture;
        if (agg.parameterCount == 0)      agg.parameterCount = c.parameterCount;
        if (agg.trainedContext == 0)      agg.trainedContext = c.trainedContext;
    }

    // Recalcular el quant dominante sobre el agregado: hacerlo por shard daria
    // el dominante de UN shard, que puede no ser el del modelo.
    static const QStringList rawFloats = {"f32", "f16", "bf16", "f64"};
    qint64 best = -1;
    for (auto it = agg.typeElements.constBegin(); it != agg.typeElements.constEnd(); ++it) {
        if (rawFloats.contains(it.key())) continue;
        if (it.value() > best) { best = it.value(); agg.dominantQuant = it.key(); }
    }
    return agg;
}

GGUFScanner::Composition GGUFScanner::readComposition(const QString &filePath,
                                                      qint64 fileSizeBytes)
{
    Composition c;

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return c;

    // El header (metadata + tensor infos) está al inicio; con 16 MiB sobra para
    // cualquier GGUF razonable y evitamos mapear archivos de decenas de GB.
    const qint64 kCap = 16LL * 1024 * 1024;
    const qint64 toRead = qMin(kCap, f.size());
    const QByteArray buf = f.read(toRead);
    if (buf.size() < 24) return c;

    LeReader r{ reinterpret_cast<const uchar*>(buf.constData()), buf.size() };

    const quint32 magic = r.u32();
    if (magic != 0x46554747u) return c;  // "GGUF"
    const quint32 version = r.u32();
    if (version < 2 || version > 3) return c; // v1 usaba uint32 counts; no soportado

    const quint64 tensorCount = r.u64();
    const quint64 kvCount = r.u64();
    if (tensorCount == 0 || tensorCount > 100000) return c;

    // Leer los metadatos útiles y saltar el resto. Las claves de contexto son
    // específicas de arquitectura (llama.context_length, qwen*.context_length…).
    for (quint64 k = 0; k < kvCount && r.ok; ++k) {
        const QString key = r.str();
        quint32 vt = r.u32();        // value type
        if (key == QLatin1String("general.architecture") && vt == 8) {
            c.architecture = r.str();
        } else if (key == QLatin1String("general.parameter_count")) {
            quint64 value = 0;
            if (readUnsignedMeta(r, vt, value)) c.parameterCount = qint64(qMin(value, quint64(std::numeric_limits<qint64>::max())));
            else skipMetaValue(r, vt);
        } else if (key.endsWith(QLatin1String(".context_length"))) {
            quint64 value = 0;
            if (readUnsignedMeta(r, vt, value)) c.trainedContext = int(qMin(value, quint64(std::numeric_limits<int>::max())));
            else skipMetaValue(r, vt);
        } else {
            skipMetaValue(r, vt);
        }
    }
    if (!r.ok) return c;

    // Tensor infos: name(str), n_dims(u32), dims[n_dims](u64), type(u32), offset(u64).
    for (quint64 t = 0; t < tensorCount && r.ok; ++t) {
        const QString tensorName = r.str();
        quint32 nDims = r.u32();
        if (nDims > 8) { r.ok = false; break; }
        qint64 elems = 1;
        for (quint32 d = 0; d < nDims; ++d) elems *= qint64(r.u64());
        quint32 type = r.u32();
        r.u64(); // offset
        if (!r.ok) break;
        const QString name = ggmlTypeName(type);
        c.typeTensors[name] += 1;
        c.typeElements[name] += elems;
        c.totalElements += elems;
        if (isNgramLookupTensor(tensorName))
            c.ngramElements += elems;
    }
    if (!r.ok || c.totalElements <= 0) return c;

    // Quant dominante: dtype cuantizado (no float crudo) con más elementos.
    static const QStringList rawFloats = {"f32", "f16", "bf16", "f64"};
    qint64 best = -1;
    for (auto it = c.typeElements.constBegin(); it != c.typeElements.constEnd(); ++it) {
        if (rawFloats.contains(it.key())) continue;
        if (it.value() > best) { best = it.value(); c.dominantQuant = it.key(); }
    }
    // Modelo full-precision: sin tensores cuantizados.
    if (c.dominantQuant.isEmpty()) {
        qint64 b2 = -1;
        for (auto it = c.typeElements.constBegin(); it != c.typeElements.constEnd(); ++it)
            if (it.value() > b2) { b2 = it.value(); c.dominantQuant = it.key(); }
    }

    if (fileSizeBytes > 0)
        c.bpw = double(fileSizeBytes) * 8.0 / double(c.totalElements);

    c.valid = true;
    return c;
}
