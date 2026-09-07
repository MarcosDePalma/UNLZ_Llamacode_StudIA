// Tests unitarios de la lógica pura del núcleo:
//   - GGUFScanner: inferencia familia/quant/vision/draft por nombre de archivo.
//   - EffectiveProfileBuilder: composición de args + degradación/bloqueo por flags.
//
// Build: cmake -DBUILD_TESTS=ON ...  → target LlamaCodeTests.
// Run:   ctest --test-dir build  (o ejecutar LlamaCodeTests directo).

#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <tuple>
#include <QTemporaryDir>
#include "core/GGUFScanner.h"
#include "core/profiles/EffectiveProfileBuilder.h"
#include "core/profiles/ProfileTypes.h"
#include "core/profiles/MtpDetection.h"
#include "core/LlamaBinary.h"
#include "core/CatalogModel.h"

class CoreTests : public QObject
{
    Q_OBJECT

private slots:
    // ── GGUFScanner::inferFamily ──
    void inferFamily_data();
    void inferFamily();

    // ── GGUFScanner::inferQuant ──
    void inferQuant_data();
    void inferQuant();

    // ── GGUFScanner candidatos vision/draft ──
    void visionCandidate();
    void draftCandidate();
    void katTemplateSupportsVisionContent();

    // ── GGUFScanner::readComposition (parser binario) ──
    void ngramLookupTensorClassification();
    void readComposition_countsNgramElements();
    void ngramArchitectureNameDetection();
    void shardPaths_enumeratesSiblings();
    void readCompositionAllShards_aggregates();
    void readComposition_realTensors();
    void readComposition_metadata();
    void readComposition_rejectsGarbage();

    // ── GGUFScanner::isDegradedQatQuant ──
    void degradedQat_data();
    void degradedQat();

    // ── EffectiveProfileBuilder ──
    void builder_emitsHostPort();
    void builder_dropsUnsupportedFlag();
    void builder_missingModelIsBlocking();
    void builder_cloudProfileIsValidWithoutLocalModel();
    void builder_cpuAuxiliaryRuntimeEmitsConservativeFlags();
    void builder_parallelRuntimeEmitsConfiguredSlots();
    void builder_emitsSpecFlags();
    void builder_missingDraftIsBlocking();
    void builder_rawDraftMtpRequiresDraftModel();
    void builder_emitsKatApexMtpWithMmproj();
    void builder_rawDsparkDoesNotRequireExternalDraftModel();
    void builder_externalDsparkEmitsDraftAndType();
    void builder_emitsSelfContainedMtpFlags();
    void builder_emitsAdaptiveSelfContainedMtpFlags();
    void builder_emitsAdaptiveExternalDraftFlags();
    void builder_rejectsAdaptiveWithoutCeiling();
    void builder_rejectsAdaptiveMinAboveMax();
    void builder_rejectsAdaptiveWithoutBinaryCapability();
    void builder_dropsGemmaDraftOnOldBinary();
    void builder_respectsKvCapWithDraft();
    void builder_appliesQwenCodingSamplingPreset();
    void builder_warnsOnManualQwenSampling();
    void builder_emitsTensorOverrides();
    void builder_warnsOnMalformedTensorOverride();
    void builder_ninfer3090UsesNativeArtifactCli();
    void builder_preservesSpecializedKvRuntimeArgsAndEnv();
    void runtimePreset_roundtripsTensorOverrides();
};

void CoreTests::inferFamily_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("family");
    QTest::newRow("qwen")    << "Qwen2.5-7B-Instruct-Q4_K_M.gguf" << "qwen";
    QTest::newRow("llama")   << "Meta-Llama-3.1-8B.Q5_K_M.gguf"   << "llama";
    QTest::newRow("mistral") << "Mistral-7B-v0.3-Q6_K.gguf"       << "mistral";
    QTest::newRow("gemma")   << "gemma-2-9b-it-Q4_0.gguf"         << "gemma";
    QTest::newRow("phi")     << "Phi-3.5-mini-instruct-Q8_0.gguf" << "phi";
    QTest::newRow("deepseek")<< "DeepSeek-Coder-V2-Q4_K_M.gguf"   << "deepseek";
}

void CoreTests::inferFamily()
{
    QFETCH(QString, file);
    QFETCH(QString, family);
    QCOMPARE(GGUFScanner::inferFamily(file).toLower(), family);
}

void CoreTests::inferQuant_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("quant");
    QTest::newRow("q4km") << "model-Q4_K_M.gguf" << "Q4_K_M";
    QTest::newRow("q8")   << "model-Q8_0.gguf"   << "Q8_0";
    QTest::newRow("iq3")  << "model-IQ3_XS.gguf" << "IQ3_XS";
    QTest::newRow("bf16") << "model-BF16.gguf"   << "BF16";
}

void CoreTests::inferQuant()
{
    QFETCH(QString, file);
    QFETCH(QString, quant);
    QCOMPARE(GGUFScanner::inferQuant(file).toUpper(), quant);
}

void CoreTests::visionCandidate()
{
    QVERIFY(GGUFScanner::isVisionCandidate("llava-v1.6-mmproj-f16.gguf"));
    QVERIFY(!GGUFScanner::isVisionCandidate("Qwen2.5-7B-Q4_K_M.gguf"));
}

void CoreTests::draftCandidate()
{
    // "draft" en el nombre, o tamaño chico (<2GB).
    QVERIFY(GGUFScanner::isDraftCandidate("qwen-0.5b-draft-Q4.gguf", 400LL * 1024 * 1024));
    QVERIFY(GGUFScanner::isDraftCandidate("tiny.gguf", 1LL * 1024 * 1024 * 1024));
    QVERIFY(!GGUFScanner::isDraftCandidate("Qwen2.5-32B-Q5_K_M.gguf", 20LL * 1024 * 1024 * 1024));
    QVERIFY(MtpDetection::isSelfContained("ThinkingCap-Qwen3.6-27B-Q3_K_M-MTP.gguf"));
    QVERIFY(MtpDetection::isSelfContained("ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf"));
    QVERIFY(MtpDetection::isSelfContained("bottlecapai_ThinkingCap-Qwen3_6-27B-Q8_0.gguf"));
    QVERIFY(MtpDetection::isSelfContained(
        "DeepSeek-V4-Flash-0731-UD-IQ3_S-00001-of-00004.gguf"));
    QVERIFY(MtpDetection::isSelfContained(
        "KAT-Coder-V2.5-Dev-MTP-APEX-i-quality-v2.gguf"));
    QVERIFY(MtpDetection::isSelfContained(
        "KAT_Coder_V2_5_Dev_MTP_APEX_I_Quality_v2.gguf"));
    QVERIFY(MtpDetection::isSelfContained("Qwen3.8-27B-UD-Q4_K_XL.gguf"));
    QVERIFY(MtpDetection::isSelfContained("Qwen3_8-27B-Q5_K_M.gguf"));
    QVERIFY(!MtpDetection::isSelfContained("DeepSeek-V4-Flash-Preview-UD-IQ3_S.gguf"));
    QVERIFY(!MtpDetection::isSelfContained("ThinkingCap-Qwen3.5-27B-Q4_K_M.gguf"));
    QVERIFY(!MtpDetection::isSelfContained("Qwen3.6-27B-Q3_K_M.gguf"));
}

void CoreTests::katTemplateSupportsVisionContent()
{
    const QStringList candidates = {
        QDir::current().absoluteFilePath(
            QStringLiteral("assets/chat-templates/kat-coder-tools.jinja")),
        QDir::current().absoluteFilePath(
            QStringLiteral("../assets/chat-templates/kat-coder-tools.jinja")),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
            QStringLiteral("../../assets/chat-templates/kat-coder-tools.jinja")),
    };
    QFile templateFile;
    QString resolvedPath;
    for (const QString &candidate : candidates) {
        if (!QFile::exists(candidate))
            continue;
        templateFile.setFileName(candidate);
        resolvedPath = candidate;
        break;
    }
    QVERIFY2(!resolvedPath.isEmpty(),
             "no se encontró el template KAT bundleado para probar visión");
    QVERIFY2(templateFile.open(QIODevice::ReadOnly), qPrintable(resolvedPath));
    const QByteArray source = templateFile.readAll();
    QVERIFY(source.contains("render_content"));
    QVERIFY(source.contains("'image_url' in item"));
    QVERIFY(source.contains("<|vision_start|><|image_pad|><|vision_end|>"));
    QVERIFY(source.contains("render_content(message.content)"));
}

// ── Helpers para construir un GGUF sintético en disco ──────────────────────
namespace {
void putU32(QByteArray &b, quint32 v) {
    for (int i = 0; i < 4; ++i) b.append(char((v >> (8*i)) & 0xFF));
}
void putU64(QByteArray &b, quint64 v) {
    for (int i = 0; i < 8; ++i) b.append(char((v >> (8*i)) & 0xFF));
}
void putStr(QByteArray &b, const QByteArray &s) {
    putU64(b, quint64(s.size()));
    b.append(s);
}
void putMetaString(QByteArray &b, const QByteArray &key, const QByteArray &value) {
    putStr(b, key); putU32(b, 8); putStr(b, value);
}
void putMetaU32(QByteArray &b, const QByteArray &key, quint32 value) {
    putStr(b, key); putU32(b, 4); putU32(b, value);
}
void putMetaU64(QByteArray &b, const QByteArray &key, quint64 value) {
    putStr(b, key); putU32(b, 10); putU64(b, value);
}
// Escribe un tensor info: name, n_dims, dims[], type, offset.
void putTensor(QByteArray &b, const QByteArray &name,
               const QList<quint64> &dims, quint32 type) {
    putStr(b, name);
    putU32(b, quint32(dims.size()));
    for (quint64 d : dims) putU64(b, d);
    putU32(b, type);
    putU64(b, 0); // offset
}
// GGUF v3 mínimo: magic, version, tensorCount, kvCount=0, luego tensor infos.
// Igual que writeGgufFixture pero con nombres de tensor explicitos: hace falta
// para ejercitar la clasificacion Ngram/PLE, que depende del NOMBRE.
QString writeGgufFixtureNamedAt(const QString &path,
                                const QList<std::tuple<QByteArray, quint32, quint64>> &tensors)
{
    QByteArray b;
    putU32(b, 0x46554747u);
    putU32(b, 3);
    putU64(b, quint64(tensors.size()));
    putU64(b, 0);
    for (const auto &t : tensors)
        putTensor(b, std::get<0>(t), {std::get<2>(t)}, std::get<1>(t));
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) { f.write(b); f.close(); }
    return path;
}

QString writeGgufFixtureNamed(const QString &name,
                              const QList<std::tuple<QByteArray, quint32, quint64>> &tensors)
{
    return writeGgufFixtureNamedAt(QDir(QDir::tempPath()).filePath(name), tensors);
}

QString writeGgufFixture(const QString &name, const QList<QPair<quint32, quint64>> &tensors)
{
    QByteArray b;
    putU32(b, 0x46554747u); // "GGUF"
    putU32(b, 3);           // version
    putU64(b, quint64(tensors.size()));
    putU64(b, 0);           // kv count
    int idx = 0;
    for (const auto &t : tensors)
        putTensor(b, QByteArray("t") + QByteArray::number(idx++),
                  {t.second}, t.first);
    const QString path = QDir(QDir::tempPath()).filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) { f.write(b); f.close(); }
    return path;
}
} // namespace

void CoreTests::ngramLookupTensorClassification()
{
    // Tablas de lookup Ngram/PLE: offloadeables a CPU/SSD sin costo de compute.
    QVERIFY(GGUFScanner::isNgramLookupTensor(QStringLiteral("per_layer_token_embd")));
    QVERIFY(GGUFScanner::isNgramLookupTensor(QStringLiteral("per_layer_token_embd.weight")));
    QVERIFY(GGUFScanner::isNgramLookupTensor(QStringLiteral("blk.0.ple_key")));
    QVERIFY(GGUFScanner::isNgramLookupTensor(QStringLiteral("blk.47.ple_value.weight")));

    // Comparten prefijo pero son COMPUTE: mandarlos fuera de la GPU es perdida
    // neta. Un "contains(ple)" los agarraria; por eso el match es exacto.
    QVERIFY(!GGUFScanner::isNgramLookupTensor(QStringLiteral("blk.0.ple_norm_key")));
    QVERIFY(!GGUFScanner::isNgramLookupTensor(QStringLiteral("blk.0.ple_norm_query")));
    QVERIFY(!GGUFScanner::isNgramLookupTensor(QStringLiteral("blk.0.ple_conv1d")));
    QVERIFY(!GGUFScanner::isNgramLookupTensor(QStringLiteral("per_layer_proj_norm")));
    QVERIFY(!GGUFScanner::isNgramLookupTensor(QStringLiteral("per_layer_model_proj")));

    // Arquitecturas sin Ngram no deben marcar nada.
    QVERIFY(!GGUFScanner::isNgramLookupTensor(QStringLiteral("token_embd.weight")));
    QVERIFY(!GGUFScanner::isNgramLookupTensor(QStringLiteral("blk.0.attn_q.weight")));
    QVERIFY(!GGUFScanner::isNgramLookupTensor(QString()));
}

void CoreTests::readComposition_countsNgramElements()
{
    // type 12 = q4_K en la tabla de ggml; el valor exacto no importa aca, lo que
    // se mide es que los elementos se atribuyan al balde correcto.
    const QString path = writeGgufFixtureNamed(QStringLiteral("lc_ngram_fixture.gguf"), {
        {QByteArray("token_embd.weight"),        12u, 1000ull},
        {QByteArray("blk.0.attn_q.weight"),      12u, 2000ull},
        {QByteArray("blk.0.ple_norm_key"),       12u,   50ull},  // compute, NO cuenta
        {QByteArray("blk.0.ple_key"),            12u, 3000ull},  // lookup
        {QByteArray("blk.0.ple_value"),          12u, 4000ull},  // lookup
        {QByteArray("per_layer_token_embd.weight"), 12u, 5000ull} // lookup
    });
    QVERIFY(!path.isEmpty());

    const GGUFScanner::Composition c =
        GGUFScanner::readComposition(path, QFileInfo(path).size());
    QCOMPARE(c.totalElements, 15050ll);
    QCOMPARE(c.ngramElements, 12000ll);
    // El backbone es lo que realmente tiene que entrar en memoria residente.
    QCOMPARE(c.totalElements - c.ngramElements, 3050ll);

    QFile::remove(path);
}

void CoreTests::ngramArchitectureNameDetection()
{
    QVERIFY(GGUFScanner::isNgramArchitectureName(
        QStringLiteral("Qwen3.8-Flash-Next-UD-Q4_K_XL-00001-of-00004.gguf")));
    QVERIFY(GGUFScanner::isNgramArchitectureName(QStringLiteral("qwen4-something.gguf")));
    QVERIFY(GGUFScanner::isNgramArchitectureName(
        QStringLiteral("D:/Models/Qwen3.8-Flash-Next/flash_next.gguf")));

    // "Flash" solo NO alcanza: DeepSeek V4 Flash es otra arquitectura sin Ngram.
    QVERIFY(!GGUFScanner::isNgramArchitectureName(
        QStringLiteral("DeepSeek-V4-Flash-0731-UD-IQ3_S.gguf")));
    QVERIFY(!GGUFScanner::isNgramArchitectureName(QStringLiteral("Qwen3.8-27B-Q4_K_M.gguf")));
    QVERIFY(!GGUFScanner::isNgramArchitectureName(QString()));
}

void CoreTests::shardPaths_enumeratesSiblings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString base = QStringLiteral("Model-UD-Q4_K_XL");
    QStringList created;
    for (int i = 1; i <= 3; ++i) {
        const QString p = QDir(dir.path()).filePath(
            QStringLiteral("%1-%2-of-00003.gguf").arg(base,
                QString::number(i).rightJustified(5, QLatin1Char('0'))));
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        created << p;
    }

    // Desde cualquier shard se llega a todos, en orden.
    const QStringList fromFirst = GGUFScanner::shardPaths(created.first());
    QCOMPARE(fromFirst.size(), 3);
    QVERIFY(fromFirst.first().endsWith(QStringLiteral("00001-of-00003.gguf")));
    QVERIFY(fromFirst.last().endsWith(QStringLiteral("00003-of-00003.gguf")));
    QCOMPARE(GGUFScanner::shardPaths(created.at(1)).size(), 3);

    // Un archivo sin particionar se devuelve solo, no vacio.
    const QString single = QDir(dir.path()).filePath(QStringLiteral("plain.gguf"));
    QFile pf(single);
    QVERIFY(pf.open(QIODevice::WriteOnly));
    pf.close();
    QCOMPARE(GGUFScanner::shardPaths(single), QStringList{single});
}

void CoreTests::readCompositionAllShards_aggregates()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Reproduce el layout real de Qwen3.8-Flash-Next: el shard 1 es SOLO
    // metadata (0 tensores) y los Ngram viven todos en el shard 2. Leer nada
    // mas el shard 1 -- que es el que queda registrado en el catalogo -- daria
    // total=0 y ngram=0, que es justo el bug que esto arregla.
    auto shardPath = [&](int i) {
        return QDir(dir.path()).filePath(
            QStringLiteral("Split-%1-of-00002.gguf")
                .arg(QString::number(i).rightJustified(5, QLatin1Char('0'))));
    };
    writeGgufFixtureNamedAt(shardPath(1), {});
    writeGgufFixtureNamedAt(shardPath(2), {
        {QByteArray("blk.0.attn_q.weight"),        12u, 1000ull},
        {QByteArray("blk.0.ple_key"),              12u, 2000ull},
        {QByteArray("per_layer_token_embd.weight"),12u, 3000ull}
    });

    const GGUFScanner::Composition solo =
        GGUFScanner::readComposition(shardPath(1), QFileInfo(shardPath(1)).size());
    QCOMPARE(solo.totalElements, 0ll);

    const GGUFScanner::Composition agg =
        GGUFScanner::readCompositionAllShards(shardPath(1));
    QCOMPARE(agg.totalElements, 6000ll);
    QCOMPARE(agg.ngramElements, 5000ll);
    QCOMPARE(agg.totalElements - agg.ngramElements, 1000ll);
}

void CoreTests::readComposition_realTensors()
{
    // Archivo llamado "Q4_K_XL" pero contenido = mayoría q4_0 (caso unsloth).
    // type ids: 2=q4_0, 14=q6_k, 0=f32.
    const QString path = writeGgufFixture(
        "gemma-fake-Q4_K_XL.gguf",
        { {2, 1000000}, {2, 2000000}, {14, 50000}, {0, 1000} });

    const auto c = GGUFScanner::readComposition(path, QFileInfo(path).size());
    QVERIFY(c.valid);
    QCOMPARE(c.dominantQuant, QStringLiteral("q4_0")); // por elementos, no por nombre
    QCOMPARE(c.typeTensors.value("q4_0"), 2);
    QCOMPARE(c.typeTensors.value("q6_k"), 1);
    QVERIFY(c.totalElements == 3051000);
    QVERIFY(c.bpw > 0.0);
    QVERIFY(c.breakdown().contains("q4_0:2"));
}

void CoreTests::readComposition_metadata()
{
    QByteArray b;
    putU32(b, 0x46554747u); putU32(b, 3);
    putU64(b, 1); putU64(b, 3);
    putMetaString(b, "general.architecture", "qwen2");
    putMetaU64(b, "general.parameter_count", 7000000000ULL);
    putMetaU32(b, "qwen2.context_length", 131072);
    putTensor(b, "token_embd.weight", {1024, 1024}, 2);
    const QString path = QDir(QDir::tempPath()).filePath("metadata-fixture.gguf");
    QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(b); f.close();

    const auto c = GGUFScanner::readComposition(path, QFileInfo(path).size());
    QVERIFY(c.valid);
    QCOMPARE(c.architecture, QStringLiteral("qwen2"));
    QCOMPARE(c.parameterCount, 7000000000LL);
    QCOMPARE(c.trainedContext, 131072);
}

void CoreTests::readComposition_rejectsGarbage()
{
    const QString path = QDir(QDir::tempPath()).filePath("not_a_gguf.bin");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("this is definitely not a gguf header at all");
    f.close();
    const auto c = GGUFScanner::readComposition(path, QFileInfo(path).size());
    QVERIFY(!c.valid);
}

void CoreTests::degradedQat_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("family");
    QTest::addColumn<QString>("quantReal");
    QTest::addColumn<bool>("degraded");

    QTest::newRow("google-raw")  << "gemma-4-E4B_q4_0-it-qat.gguf" << "gemma" << "q4_0" << true;
    QTest::newRow("unsloth-ud")  << "gemma-4-E4B-it-qat-UD-Q4_K_XL.gguf" << "gemma" << "q4_0" << false;
    QTest::newRow("unsloth-name")<< "gemma-4-qat-unsloth.gguf" << "gemma" << "q4_0" << false;
    QTest::newRow("not-qat")     << "gemma-4-E4B-it-Q4_0.gguf" << "gemma" << "q4_0" << false;
    QTest::newRow("not-gemma")   << "qwen-qat-q4_0.gguf" << "qwen" << "q4_0" << false;
    QTest::newRow("not-q4_0")    << "gemma-qat-Q6_K.gguf" << "gemma" << "q6_k" << false;
}

void CoreTests::degradedQat()
{
    QFETCH(QString, file);
    QFETCH(QString, family);
    QFETCH(QString, quantReal);
    QFETCH(bool, degraded);
    QCOMPARE(GGUFScanner::isDegradedQatQuant(file, family, quantReal), degraded);
}

// build() valida que el binario exista en disco → necesitamos un archivo real.
static QString existingBinaryPath()
{
    static QString p;
    if (p.isEmpty()) {
        p = QDir(QDir::tempPath()).filePath("llamacode_test_bin.exe");
        QFile f(p);
        if (f.open(QIODevice::WriteOnly)) { f.write("x"); f.close(); }
    }
    return p;
}

// Construye un Context mínimo válido (binario + modelo presentes).
static EffectiveProfileBuilder::Context makeCtx()
{
    EffectiveProfileBuilder::Context ctx;
    ctx.binary.id = "bin1";
    ctx.binary.path = existingBinaryPath();
    ctx.binary.name = "test";
    ctx.backend.host = "127.0.0.1";
    ctx.backend.port = 9099;
    ctx.backend.binaryId = "bin1";
    ctx.catalogModel.id = "m1";
    ctx.catalogModel.absolutePath = "C:/models/test.gguf";
    ctx.model.modelId = "m1";
    return ctx;
}

void CoreTests::builder_emitsHostPort()
{
    auto ctx = makeCtx();
    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    const int hi = ep.effectiveArgs.indexOf("--host");
    const int pi = ep.effectiveArgs.indexOf("--port");
    QVERIFY(hi >= 0 && hi + 1 < ep.effectiveArgs.size());
    QCOMPARE(ep.effectiveArgs[hi + 1], QStringLiteral("127.0.0.1"));
    QVERIFY(pi >= 0 && pi + 1 < ep.effectiveArgs.size());
    QCOMPARE(ep.effectiveArgs[pi + 1], QStringLiteral("9099"));
}

void CoreTests::builder_dropsUnsupportedFlag()
{
    auto ctx = makeCtx();
    // Binario que sólo soporta --host/--port/--model → flash-attn debe dropearse.
    ctx.binary.supportedFlags = QStringList{"--host", "--port", "--model"};
    ctx.runtime.flashAttention = true;
    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY(!ep.effectiveArgs.contains("--flash-attn"));
    QVERIFY(!ep.warnings.isEmpty());  // degradación reportada
}

void CoreTests::builder_missingModelIsBlocking()
{
    auto ctx = makeCtx();
    ctx.model.modelId.clear();
    ctx.catalogModel = CatalogModel{};  // sin modelo resuelto
    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY(!ep.blockingErrors.isEmpty());
}

void CoreTests::builder_cloudProfileIsValidWithoutLocalModel()
{
    EffectiveProfileBuilder::Context ctx;
    ctx.backend.kind = QStringLiteral("cloud");
    ctx.backend.cloudBaseUrl = QStringLiteral("http://127.0.0.1:8000");
    ctx.backend.cloudModel = QStringLiteral("lued/Qwen3.8-27B-INT8-W8A16-DFlash2");
    ctx.backend.cloudKeyRef = QStringLiteral("VLLM_KEY");
    ctx.backend.cloudCtx = 262144;
    ctx.launch.extraArgs = {QStringLiteral("--external-profile")};

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY2(ep.isValid(), qPrintable(ep.blockingErrors.join(QStringLiteral("\n"))));
    QVERIFY(ep.binaryPath.isEmpty());
    QCOMPARE(ep.effectiveArgs, ctx.launch.extraArgs);
    QVERIFY(ep.commandLine.contains(QStringLiteral("127.0.0.1:8000")));
    QVERIFY(ep.commandLine.contains(QStringLiteral("lued/Qwen3.8-27B-INT8-W8A16-DFlash2")));
    QVERIFY(!ep.commandLine.contains(QStringLiteral("VLLM_KEY")));
}

void CoreTests::builder_cpuAuxiliaryRuntimeEmitsConservativeFlags()
{
    auto ctx = makeCtx();
    ctx.binary.backend = QStringLiteral("cpu");
    ctx.runtime.ctx = 32768;
    ctx.runtime.threads = 20;
    ctx.runtime.gpuLayers = 0;
    ctx.runtime.batch = 256;
    ctx.runtime.ubatch = 128;
    ctx.runtime.flashAttention = false;

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY2(ep.blockingErrors.isEmpty(),
             qPrintable(ep.blockingErrors.join(QStringLiteral("\n"))));
    const QStringList &args = ep.effectiveArgs;
    const auto valueAfter = [&args](const QString &flag) {
        const int index = args.indexOf(flag);
        return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : QString();
    };
    QCOMPARE(valueAfter(QStringLiteral("--ctx-size")), QStringLiteral("32768"));
    QCOMPARE(valueAfter(QStringLiteral("--threads")), QStringLiteral("20"));
    QCOMPARE(valueAfter(QStringLiteral("--n-gpu-layers")), QStringLiteral("0"));
    QCOMPARE(valueAfter(QStringLiteral("--batch-size")), QStringLiteral("256"));
    QCOMPARE(valueAfter(QStringLiteral("--ubatch-size")), QStringLiteral("128"));
    QVERIFY(!args.contains(QStringLiteral("--flash-attn")));
}

void CoreTests::builder_parallelRuntimeEmitsConfiguredSlots()
{
    auto ctx = makeCtx();
    ctx.runtime.parallelSlots = 4;
    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY2(ep.blockingErrors.isEmpty(),
             qPrintable(ep.blockingErrors.join(QStringLiteral("\n"))));
    const int index = ep.effectiveArgs.indexOf(QStringLiteral("--parallel"));
    QVERIFY(index >= 0 && index + 1 < ep.effectiveArgs.size());
    QCOMPARE(ep.effectiveArgs.at(index + 1), QStringLiteral("4"));

    ctx.runtime.parallelSlots = 1;
    const EffectiveProfile single = EffectiveProfileBuilder::build(ctx);
    QVERIFY(!single.effectiveArgs.contains(QStringLiteral("--parallel")));
}

// Con draft model resuelto, los flags spec-draft seteados deben emitirse.
void CoreTests::builder_emitsSpecFlags()
{
    auto ctx = makeCtx();
    ctx.model.draftModelId = "d1";
    ctx.model.specType = "draft-mtp";
    ctx.model.specDraftNMax = 3;
    ctx.model.specDraftNgl = "all";
    ctx.model.specDraftTypeK = "q8_0";
    ctx.model.specDraftTypeV = "q8_0";
    ctx.draftModel.id = "d1";
    ctx.draftModel.isAvailable = true;
    ctx.draftModel.absolutePath = "C:/models/draft.gguf";

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    const QStringList &a = ep.effectiveArgs;
    // Con specType seteado (MTP/DFlash) el draft va por --spec-draft-model.
    // --spec-type es sólo para modos sin draft model (ngram-*); emitir
    // "draft-mtp" rompe llama-server actual.
    QVERIFY(a.contains("--spec-draft-model"));
    QVERIFY(!a.contains("--spec-type"));
    int i = a.indexOf("--spec-draft-n-max");
    QVERIFY(i >= 0 && a[i + 1] == "3");
    i = a.indexOf("--spec-draft-ngl");
    QVERIFY(i >= 0 && a[i + 1] == "all");
    QVERIFY(a.contains("--spec-draft-type-k"));
    QVERIFY(a.contains("--spec-draft-type-v"));
}

void CoreTests::builder_missingDraftIsBlocking()
{
    auto ctx = makeCtx();
    ctx.model.draftModelId = "d1";
    ctx.model.specType = "draft-mtp";
    ctx.model.specDraftNMax = 3;
    ctx.model.specDraftNgl = "all";
    ctx.draftModel = CatalogModel{};

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY(!ep.blockingErrors.isEmpty());
    bool mentionsDraft = false;
    for (const QString &e : ep.blockingErrors)
        if (e.contains(QStringLiteral("Draft model"), Qt::CaseInsensitive))
            mentionsDraft = true;
    QVERIFY2(mentionsDraft, qPrintable(ep.blockingErrors.join(QStringLiteral("\n"))));
    QVERIFY(!ep.effectiveArgs.contains(QStringLiteral("--spec-draft-model")));
}

void CoreTests::builder_rawDraftMtpRequiresDraftModel()
{
    auto ctx = makeCtx();
    ctx.launch.extraArgs = {
        QStringLiteral("--spec-type"),
        QStringLiteral("draft-mtp"),
        QStringLiteral("--spec-draft-n-max"),
        QStringLiteral("3")
    };

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY(!ep.blockingErrors.isEmpty());
    bool mentionsProfileDraft = false;
    for (const QString &e : ep.blockingErrors)
        if (e.contains(QStringLiteral("draftModel"), Qt::CaseInsensitive))
            mentionsProfileDraft = true;
    QVERIFY2(mentionsProfileDraft, qPrintable(ep.blockingErrors.join(QStringLiteral("\n"))));
}

void CoreTests::builder_emitsKatApexMtpWithMmproj()
{
    auto ctx = makeCtx();
    ctx.catalogModel.fileName =
        "KAT-Coder-V2.5-Dev-MTP-APEX-i-quality-v2.gguf";
    ctx.model.mmprojId = "mmproj";
    ctx.mmprojModel.id = "mmproj";
    ctx.mmprojModel.isAvailable = true;
    ctx.mmprojModel.absolutePath = "C:/models/mmproj-F16.gguf";
    ctx.model.specType = "draft-mtp";
    ctx.model.specDraftNMax = 2;

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY2(ep.blockingErrors.isEmpty(),
             qPrintable(ep.blockingErrors.join(QStringLiteral("\n"))));
    const QStringList &args = ep.effectiveArgs;
    const int mmproj = args.indexOf(QStringLiteral("--mmproj"));
    QVERIFY(mmproj >= 0 && mmproj + 1 < args.size());
    QCOMPARE(args.at(mmproj + 1), ctx.mmprojModel.absolutePath);
    const int type = args.indexOf(QStringLiteral("--spec-type"));
    QVERIFY(type >= 0 && type + 1 < args.size());
    QCOMPARE(args.at(type + 1), QStringLiteral("draft-mtp"));
    const int nmax = args.indexOf(QStringLiteral("--spec-draft-n-max"));
    QVERIFY(nmax >= 0 && nmax + 1 < args.size());
    QCOMPARE(args.at(nmax + 1), QStringLiteral("2"));
    QVERIFY(!args.contains(QStringLiteral("--spec-draft-model")));
}

void CoreTests::builder_rawDsparkDoesNotRequireExternalDraftModel()
{
    auto ctx = makeCtx();
    ctx.catalogModel.fileName = "DeepSeek-V4-Flash-0731-UD-IQ3_S-00001-of-00004.gguf";
    ctx.launch.extraArgs = {
        QStringLiteral("--spec-type"),
        QStringLiteral("draft-dspark"),
        QStringLiteral("--spec-draft-n-max"),
        QStringLiteral("5")
    };

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY2(ep.blockingErrors.isEmpty(),
             qPrintable(ep.blockingErrors.join(QStringLiteral("\n"))));
    const int type = ep.effectiveArgs.indexOf(QStringLiteral("--spec-type"));
    QVERIFY(type >= 0);
    QCOMPARE(ep.effectiveArgs.value(type + 1), QStringLiteral("draft-dspark"));
    QVERIFY(!ep.effectiveArgs.contains(QStringLiteral("--spec-draft-model")));
}

void CoreTests::builder_externalDsparkEmitsDraftAndType()
{
    auto ctx = makeCtx();
    ctx.catalogModel.fileName = "DeepSeek-V4-Flash-0731-UD-IQ3_S-00001-of-00004.gguf";
    ctx.model.draftModelId = "dspark";
    ctx.model.specType = "draft-dspark";
    ctx.model.specDraftNMax = 5;
    ctx.model.specDraftConfMin = 0.6;
    ctx.model.specDraftNgl = "auto";
    ctx.draftModel.id = "dspark";
    ctx.draftModel.isAvailable = true;
    ctx.draftModel.absolutePath = "C:/models/DeepseekV4-Flash-20260731-DSpark.gguf";
    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY2(ep.blockingErrors.isEmpty(),
             qPrintable(ep.blockingErrors.join(QStringLiteral("\n"))));
    const QStringList &args = ep.effectiveArgs;
    int index = args.indexOf(QStringLiteral("--spec-draft-model"));
    QVERIFY(index >= 0);
    QCOMPARE(args.value(index + 1), ctx.draftModel.absolutePath);
    index = args.indexOf(QStringLiteral("--spec-type"));
    QVERIFY(index >= 0);
    QCOMPARE(args.value(index + 1), QStringLiteral("draft-dspark"));
    index = args.indexOf(QStringLiteral("--spec-draft-n-max"));
    QVERIFY(index >= 0);
    QCOMPARE(args.value(index + 1), QStringLiteral("5"));
    index = args.indexOf(QStringLiteral("--spec-draft-ngl"));
    QVERIFY(index >= 0);
    QCOMPARE(args.value(index + 1), QStringLiteral("auto"));
    index = args.indexOf(QStringLiteral("--spec-draft-conf-min"));
    QVERIFY(index >= 0);
    QCOMPARE(args.value(index + 1), QStringLiteral("0.600"));
}

void CoreTests::builder_emitsSelfContainedMtpFlags()
{
    auto ctx = makeCtx();
    // El GGUF oficial de BottleCapAI tiene el cabezal MTP integrado aunque el
    // filename no incluya el token "-MTP".
    ctx.catalogModel.fileName = "ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf";
    ctx.model.specType = "draft-mtp";
    ctx.model.specDraftNMax = 4;

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY(ep.blockingErrors.isEmpty());
    const int type = ep.effectiveArgs.indexOf("--spec-type");
    QVERIFY(type >= 0 && ep.effectiveArgs.value(type + 1) == "draft-mtp");
    const int nmax = ep.effectiveArgs.indexOf("--spec-draft-n-max");
    QVERIFY(nmax >= 0 && ep.effectiveArgs.value(nmax + 1) == "4");
    QVERIFY(!ep.effectiveArgs.contains("--spec-draft-model"));
}

void CoreTests::builder_emitsAdaptiveSelfContainedMtpFlags()
{
    auto ctx = makeCtx();
    ctx.catalogModel.fileName = "Qwen3.8-27B-UD-Q4_K_XL.gguf";
    ctx.model.specType = "draft-mtp";
    ctx.model.specDraftNMin = 3;
    ctx.model.specDraftNMax = 7;
    ctx.model.specDraftAdaptive = true;

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY2(ep.blockingErrors.isEmpty(),
             qPrintable(ep.blockingErrors.join(QStringLiteral("\n"))));
    const QStringList &args = ep.effectiveArgs;
    QVERIFY(args.contains(QStringLiteral("--spec-draft-adaptive")));
    int index = args.indexOf(QStringLiteral("--spec-draft-n-min"));
    QVERIFY(index >= 0);
    QCOMPARE(args.value(index + 1), QStringLiteral("3"));
    index = args.indexOf(QStringLiteral("--spec-draft-n-max"));
    QVERIFY(index >= 0);
    QCOMPARE(args.value(index + 1), QStringLiteral("7"));
}

void CoreTests::builder_emitsAdaptiveExternalDraftFlags()
{
    auto ctx = makeCtx();
    ctx.model.draftModelId = "d1";
    ctx.model.specType = "draft-mtp";
    ctx.model.specDraftNMin = 3;
    ctx.model.specDraftNMax = 8;
    ctx.model.specDraftAdaptive = true;
    ctx.draftModel.id = "d1";
    ctx.draftModel.isAvailable = true;
    ctx.draftModel.absolutePath = "C:/models/draft.gguf";

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY2(ep.blockingErrors.isEmpty(),
             qPrintable(ep.blockingErrors.join(QStringLiteral("\n"))));
    QVERIFY(ep.effectiveArgs.contains(QStringLiteral("--spec-draft-model")));
    QVERIFY(ep.effectiveArgs.contains(QStringLiteral("--spec-draft-adaptive")));
    const int index = ep.effectiveArgs.indexOf(QStringLiteral("--spec-draft-n-min"));
    QVERIFY(index >= 0);
    QCOMPARE(ep.effectiveArgs.value(index + 1), QStringLiteral("3"));
}

void CoreTests::builder_rejectsAdaptiveWithoutCeiling()
{
    auto ctx = makeCtx();
    ctx.catalogModel.fileName = "Qwen3.8-27B-UD-Q4_K_XL.gguf";
    ctx.model.specType = "draft-mtp";
    ctx.model.specDraftNMin = 3;
    ctx.model.specDraftAdaptive = true;

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY(!ep.blockingErrors.isEmpty());
    QVERIFY(ep.blockingErrors.join(QStringLiteral("\n")).contains(
        QStringLiteral("n-max"), Qt::CaseInsensitive));
    QVERIFY(!ep.effectiveArgs.contains(QStringLiteral("--spec-draft-adaptive")));
}

void CoreTests::builder_rejectsAdaptiveMinAboveMax()
{
    auto ctx = makeCtx();
    ctx.catalogModel.fileName = "Qwen3.8-27B-UD-Q4_K_XL.gguf";
    ctx.model.specType = "draft-mtp";
    ctx.model.specDraftNMin = 8;
    ctx.model.specDraftNMax = 7;
    ctx.model.specDraftAdaptive = true;

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY(!ep.blockingErrors.isEmpty());
    QVERIFY(ep.blockingErrors.join(QStringLiteral("\n")).contains(
        QStringLiteral("n-min"), Qt::CaseInsensitive));
    QVERIFY(!ep.effectiveArgs.contains(QStringLiteral("--spec-draft-adaptive")));
}

void CoreTests::builder_rejectsAdaptiveWithoutBinaryCapability()
{
    auto ctx = makeCtx();
    ctx.catalogModel.fileName = "Qwen3.8-27B-UD-Q4_K_XL.gguf";
    ctx.model.specType = "draft-mtp";
    ctx.model.specDraftNMin = 3;
    ctx.model.specDraftNMax = 7;
    ctx.model.specDraftAdaptive = true;
    ctx.binary.supportedFlags = {
        QStringLiteral("--host"), QStringLiteral("--port"),
        QStringLiteral("--model"), QStringLiteral("--spec-type"),
        QStringLiteral("--spec-draft-n-max"), QStringLiteral("--spec-draft-n-min")
    };

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY(!ep.blockingErrors.isEmpty());
    QVERIFY(ep.blockingErrors.join(QStringLiteral("\n")).contains(
        QStringLiteral("--spec-draft-adaptive"), Qt::CaseInsensitive));
    QVERIFY(!ep.effectiveArgs.contains(QStringLiteral("--spec-draft-adaptive")));
}

void CoreTests::builder_dropsGemmaDraftOnOldBinary()
{
    auto ctx = makeCtx();
    ctx.binary.versionHint = "b9045";
    ctx.model.draftModelId = "d1";
    ctx.model.specType = "draft-mtp";
    ctx.model.specDraftNMax = 3;
    ctx.model.specDraftNgl = "all";
    ctx.draftModel.id = "d1";
    ctx.draftModel.isAvailable = true;
    ctx.draftModel.absolutePath = "C:/models/mtp-gemma-4-12b-it.gguf";

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    const QStringList &a = ep.effectiveArgs;
    QVERIFY(!ep.blockingErrors.isEmpty());
    QVERIFY(!a.contains("--spec-draft-model"));
    QVERIFY(!a.contains("--spec-draft-n-max"));
    QVERIFY(!a.contains("--spec-draft-ngl"));
}

// Spec decoding activo + KV cache cuantizado → respetar q8/q4, avisar y no
// elevar silenciosamente a f16. Sin draft, el quant pasa normal.
void CoreTests::builder_respectsKvCapWithDraft()
{
    // Caso sin draft: el quant se emite.
    {
        auto ctx = makeCtx();
        ctx.runtime.cacheType = "q4_0";
        const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
        QVERIFY(ep.effectiveArgs.contains("--cache-type-k"));
    }
    // Caso con draft disponible: se conserva el quant solicitado y se avisa.
    {
        auto ctx = makeCtx();
        ctx.runtime.cacheType = "q4_0";
        ctx.model.draftModelId = "d1";
        ctx.draftModel.id = "d1";
        ctx.draftModel.isAvailable = true;
        ctx.draftModel.absolutePath = "C:/models/draft.gguf";
        const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
        const int cacheIndex = ep.effectiveArgs.indexOf("--cache-type-k");
        QVERIFY(cacheIndex >= 0);
        QCOMPARE(ep.effectiveArgs.value(cacheIndex + 1), QStringLiteral("q4_0"));
        bool warned = false;
        for (const QString &w : ep.warnings)
            if (w.contains("aceptación del draft")) warned = true;
        QVERIFY(warned);
    }
}

void CoreTests::builder_appliesQwenCodingSamplingPreset()
{
    auto ctx = makeCtx();
    ctx.catalogModel.fileName = "Qwen3.6-27B-Coder-Q5_K_M.gguf";
    ctx.catalogModel.familyHint = "qwen";

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    const QStringList &a = ep.effectiveArgs;

    int i = a.indexOf("--temp");
    QVERIFY(i >= 0 && i + 1 < a.size());
    QCOMPARE(a[i + 1], QStringLiteral("0.6"));
    i = a.indexOf("--top-k");
    QVERIFY(i >= 0 && i + 1 < a.size());
    QCOMPARE(a[i + 1], QStringLiteral("20"));
    i = a.indexOf("--min-p");
    QVERIFY(i >= 0 && i + 1 < a.size());
    QCOMPARE(a[i + 1], QStringLiteral("0.0"));
}

void CoreTests::builder_warnsOnManualQwenSampling()
{
    auto ctx = makeCtx();
    ctx.catalogModel.fileName = "Qwen3.6-27B-Coder-Q5_K_M.gguf";
    ctx.catalogModel.familyHint = "qwen";
    ctx.launch.extraArgs = {"--temp 1.0 --top-k 64"};

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QCOMPARE(ep.effectiveArgs.count("--temp"), 1);
    QCOMPARE(ep.effectiveArgs.count("--top-k"), 1);

    bool tempWarn = false;
    bool topKWarn = false;
    for (const QString &w : ep.warnings) {
        if (w.contains("--temp=1.0"))
            tempWarn = true;
        if (w.contains("--top-k=64"))
            topKWarn = true;
    }
    QVERIFY(tempWarn);
    QVERIFY(topKWarn);
}

// Role-aware per-tensor quant: llama.cpp exige una unica ocurrencia comma-separated;
// si se repite el flag, b10228 conserva silenciosamente solo la ultima regla.
void CoreTests::builder_emitsTensorOverrides()
{
    auto ctx = makeCtx();
    ctx.runtime.tensorOverrides = {"ffn_.*=Q4_K", "attn_.*=Q8_0"};
    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    const QStringList &a = ep.effectiveArgs;
    QCOMPARE(a.count("--override-tensor"), 1);
    int i = a.indexOf("--override-tensor");
    QVERIFY(i >= 0 && i + 1 < a.size());
    QCOMPARE(a[i + 1], QStringLiteral("ffn_.*=Q4_K,attn_.*=Q8_0"));
}

// Spec sin '=' se descarta con warning, sin emitir el flag.
void CoreTests::builder_warnsOnMalformedTensorOverride()
{
    auto ctx = makeCtx();
    ctx.runtime.tensorOverrides = {"ffn_only_no_type", "  ", "ffn_.*=Q4_K"};
    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QCOMPARE(ep.effectiveArgs.count("--override-tensor"), 1);
    bool warned = false;
    for (const QString &w : ep.warnings)
        if (w.contains("ffn_only_no_type")) warned = true;
    QVERIFY(warned);
}

void CoreTests::builder_ninfer3090UsesNativeArtifactCli()
{
    auto ctx = makeCtx();
    ctx.binary.flavor = QStringLiteral("ninfer-3090");
    ctx.catalogModel.fileName = QStringLiteral("qwen3_6_35b_a3b.ninfer");
    ctx.catalogModel.absolutePath = QStringLiteral("C:/models/qwen3_6_35b_a3b.ninfer");
    ctx.runtime.ctx = 4096;
    ctx.runtime.ubatch = 128;
    ctx.runtime.cacheType = QStringLiteral("q8_0");

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    const QStringList &a = ep.effectiveArgs;
    QVERIFY2(ep.blockingErrors.isEmpty(), qPrintable(ep.blockingErrors.join("\n")));
    QVERIFY(a.contains(QStringLiteral("C:/models/qwen3_6_35b_a3b.ninfer")));
    QVERIFY(!a.contains(QStringLiteral("--model")));
    QVERIFY(!a.contains(QStringLiteral("--ctx-size")));
    QVERIFY(a.contains(QStringLiteral("--max-context")));
    QVERIFY(a.contains(QStringLiteral("--kv-dtype")));
    QVERIFY(a.contains(QStringLiteral("int8")));
    QVERIFY(a.contains(QStringLiteral("--text-only")));
    QVERIFY(!a.contains(QStringLiteral("--jinja")));
}

void CoreTests::builder_preservesSpecializedKvRuntimeArgsAndEnv()
{
    auto ctx = makeCtx();
    ctx.launch.extraArgs = {
        QStringLiteral("--fraqtl-kv"),
        QStringLiteral("--fraqtl-eigenbasis"),
        QStringLiteral("C:/sidecars/v-eigenbasis.bin"),
        QStringLiteral("--fraqtl-k-protect"),
        QStringLiteral("32"),
        QStringLiteral("--fraqtl-k-eigenbasis"),
        QStringLiteral("C:/sidecars/k-eigenbasis.bin"),
        QStringLiteral("--fraqtl-sink-tokens"),
        QStringLiteral("0"),
        QStringLiteral("--fraqtl-residual-window"),
        QStringLiteral("0"),
    };
    ctx.launch.envOverrides = {
        {QStringLiteral("FRAQTL_MEMBRANE"), QStringLiteral("1")},
        {QStringLiteral("FRAQTL_MEMBRANE_EXCLUSIVE"), QStringLiteral("1")},
    };

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
    QVERIFY2(ep.blockingErrors.isEmpty(),
             qPrintable(ep.blockingErrors.join(QStringLiteral("\n"))));
    for (const QString &arg : ctx.launch.extraArgs)
        QVERIFY2(ep.effectiveArgs.contains(arg), qPrintable(arg));
    QCOMPARE(ep.effectiveEnv.value(QStringLiteral("FRAQTL_MEMBRANE")),
             QStringLiteral("1"));
    QCOMPARE(ep.effectiveEnv.value(QStringLiteral("FRAQTL_MEMBRANE_EXCLUSIVE")),
             QStringLiteral("1"));
}

// Persistencia: tensorOverrides sobrevive toJson→fromJson; entries vacías se filtran.
void CoreTests::runtimePreset_roundtripsTensorOverrides()
{
    RuntimePreset p;
    p.tensorOverrides = {"ffn_.*=Q4_K", "  ", "attn_.*=Q8_0"};
    const RuntimePreset r = RuntimePreset::fromJson(p.toJson());
    QCOMPARE(r.tensorOverrides.size(), 2);
    QCOMPARE(r.tensorOverrides[0], QStringLiteral("ffn_.*=Q4_K"));
    QCOMPARE(r.tensorOverrides[1], QStringLiteral("attn_.*=Q8_0"));

    // Vacío no debe escribir la key.
    RuntimePreset empty;
    QVERIFY(!empty.toJson().contains("tensorOverrides"));
}

QTEST_MAIN(CoreTests)
#include "test_gguf_profiles.moc"
