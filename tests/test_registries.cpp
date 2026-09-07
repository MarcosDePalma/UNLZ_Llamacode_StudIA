// Unit + integration tests de los registries persistentes:
//   - LlamaBinary / ModelRoot: serialización + resolución de flags/alias.
//   - BinaryRegistry: add/get/update/remove, supportedFlags, computeHash,
//     detectBackend, persistencia (aislada vía QStandardPaths test mode).
//   - ModelRootRegistry: add/get/remove + scan de un root real con .gguf falsos.
//
// El almacenamiento usa AppDataLocation; QStandardPaths::setTestModeEnabled(true)
// lo redirige a una ubicación de test efímera, así no tocamos datos reales.

#include <QtTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSignalSpy>
#include "core/LlamaBinary.h"
#include "core/ModelRoot.h"
#include "core/BinaryRegistry.h"
#include "core/ModelRootRegistry.h"
#include "core/ModelCatalog.h"
#include "core/GGUFScanner.h"
#include "core/OllamaImporter.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

static QString writeFakeFile(const QString &path, const QByteArray &data = "x")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) { f.write(data); f.close(); }
    return path;
}

class RegistriesTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void llamaBinary_flagsAndAlias();
    void llamaBinary_jsonRoundTrip();
    void modelRoot_jsonRoundTrip();

    void binaryRegistry_addGetRemove();
    void binaryRegistry_addIsAppendOnly();
    void binaryRegistry_supportedFlagsAndHash();
    void binaryRegistry_persists();

    void modelRootRegistry_addRemove();
    void modelRootRegistry_scanFindsGguf();
    void ggufScanner_reusesUnchangedCatalogMetadata();

    void ollamaImporter_resolveScheme();
    void ollamaImporter_scanFindsBlobs();
    void ollamaImporter_extractsProjector();
    void modelRootRegistry_ollamaSchemeIngests();
};

// Arma un store de Ollama falso: un manifest JSON con una capa "model" que
// apunta a un blob presente en blobs/. Devuelve el directorio del store.
static QString makeFakeOllamaStore(const QString &root, const QString &model,
                                   const QString &tag)
{
    const QString digestHex = "1111111111111111111111111111111111111111111111111111111111111111";
    const QString blobPath = root + "/blobs/sha256-" + digestHex;
    writeFakeFile(blobPath, "GGUF-fake-weights");

    QJsonObject modelLayer{
        {"mediaType", "application/vnd.ollama.image.model"},
        {"digest", "sha256:" + digestHex},
        {"size", 17}
    };
    QJsonObject manifest{{"layers", QJsonArray{modelLayer}}};
    const QString manifestPath =
        root + "/manifests/registry.ollama.ai/library/" + model + "/" + tag;
    writeFakeFile(manifestPath, QJsonDocument(manifest).toJson());
    return root;
}

void RegistriesTests::llamaBinary_flagsAndAlias()
{
    LlamaBinary b;
    b.supportedFlags = QStringList{"--flash-attn", "--ctx-size"};
    b.flagAliases.insert("-c", "--ctx-size");
    QVERIFY(b.supportsFlag("--flash-attn"));
    QVERIFY(!b.supportsFlag("--nope"));
    QCOMPARE(b.resolveFlag("-c"), QStringLiteral("--ctx-size"));
}

void RegistriesTests::llamaBinary_jsonRoundTrip()
{
    LlamaBinary b;
    b.id = "bin1"; b.name = "server"; b.path = "C:/x/llama-server.exe";
    b.flavor = "official"; b.backend = "cuda"; b.versionHint = "b1234";
    b.supportedFlags = QStringList{"--host", "--port"};
    b.binaryHash = "abcd";
    const LlamaBinary r = LlamaBinary::fromJson(b.toJson());
    QCOMPARE(r.name, b.name);
    QCOMPARE(r.backend, b.backend);
    QCOMPARE(r.supportedFlags, b.supportedFlags);
    QCOMPARE(r.binaryHash, b.binaryHash);
}

void RegistriesTests::modelRoot_jsonRoundTrip()
{
    ModelRoot m;
    m.id = "root1"; m.path = "C:/models"; m.label = "main";
    m.scanMode = "startup"; m.enabled = false; m.priority = 3;
    m.tags = QStringList{"gpu", "fast"};
    const ModelRoot r = ModelRoot::fromJson(m.toJson());
    QCOMPARE(r.path, m.path);
    QCOMPARE(r.label, m.label);
    QCOMPARE(r.scanMode, m.scanMode);
    QCOMPARE(r.enabled, m.enabled);
    QCOMPARE(r.priority, m.priority);
    QCOMPARE(r.tags, m.tags);
}

void RegistriesTests::binaryRegistry_addGetRemove()
{
    QTemporaryDir dir;
    const QString exe = writeFakeFile(dir.filePath("llama-server.exe"));
    BinaryRegistry reg;
    const QString id = reg.add(exe, "srv", "official", "cpu", "b1");
    QVERIFY(!id.isEmpty());
    QCOMPARE(reg.get(id).value("name").toString(), QStringLiteral("srv"));
    QVERIFY(reg.update(id, "srv2", "custom", "cuda", "b2", QString()));
    QCOMPARE(reg.get(id).value("name").toString(), QStringLiteral("srv2"));
    QVERIFY(reg.remove(id));
    QVERIFY(reg.get(id).isEmpty());
}

// Bump de binario = append-only: instalar un build nuevo NUNCA reemplaza ni borra
// uno viejo (un perfil puede rendir mejor en un binario previo). Cada entrada debe
// quedar identificable por su versionHint/tag.
void RegistriesTests::binaryRegistry_addIsAppendOnly()
{
    QTemporaryDir dir;
    const QString exeOld = writeFakeFile(dir.filePath("old/llama-server.exe"), "old");
    const QString exeNew = writeFakeFile(dir.filePath("new/llama-server.exe"), "new");
    BinaryRegistry reg;
    // El storage (AppData test mode) persiste entre corridas: contar el delta, no
    // el total absoluto.
    const int before = reg.rowCount(QModelIndex());
    const QString idOld = reg.add(exeOld, "llama-server (official b9000)", "official", "cuda", "b9000");
    const QString idNew = reg.add(exeNew, "llama-server (official b9274)", "official", "cuda", "b9274");
    // Dos entradas distintas; el viejo sigue presente tras agregar el nuevo.
    QCOMPARE(reg.rowCount(QModelIndex()), before + 2);
    QVERIFY(idOld != idNew);
    QCOMPARE(reg.get(idOld).value("versionHint").toString(), QStringLiteral("b9000"));
    QCOMPARE(reg.get(idNew).value("versionHint").toString(), QStringLiteral("b9274"));
    QCOMPARE(reg.get(idOld).value("path").toString(), exeOld);
}

void RegistriesTests::binaryRegistry_supportedFlagsAndHash()
{
    QTemporaryDir dir;
    const QString exe = writeFakeFile(dir.filePath("a.exe"), "hello-bytes");
    BinaryRegistry reg;
    const QString id = reg.add(exe, "a", "official", "cpu", "");
    // Sin detección de caps los flags pueden venir vacíos; el hash sí debe existir.
    QCOMPARE(reg.get(id).value("binaryHash").toString().isEmpty(), false);
    // computeHash determinista para el mismo contenido.
    const QString h1 = reg.get(id).value("binaryHash").toString();
    const QString exe2 = writeFakeFile(dir.filePath("b.exe"), "hello-bytes");
    const QString id2 = reg.add(exe2, "b", "official", "cpu", "");
    QCOMPARE(reg.get(id2).value("binaryHash").toString(), h1);
}

void RegistriesTests::binaryRegistry_persists()
{
    QTemporaryDir dir;
    const QString exe = writeFakeFile(dir.filePath("p.exe"));
    QString id;
    { BinaryRegistry reg; id = reg.add(exe, "persisted", "official", "cpu", ""); }
    { BinaryRegistry reg2; QCOMPARE(reg2.get(id).value("name").toString(),
                                    QStringLiteral("persisted")); }
}

void RegistriesTests::modelRootRegistry_addRemove()
{
    QTemporaryDir dir;
    ModelCatalog cat;
    ModelRootRegistry reg(&cat);
    const QString id = reg.add(dir.path(), "main", "manual", QStringList{"t"});
    QVERIFY(!id.isEmpty());
    QCOMPARE(reg.get(id).value("label").toString(), QStringLiteral("main"));
    QVERIFY(reg.remove(id));
}

void RegistriesTests::modelRootRegistry_scanFindsGguf()
{
    QTemporaryDir dir;
    writeFakeFile(dir.filePath("Qwen2.5-7B-Q4_K_M.gguf"));
    writeFakeFile(dir.filePath("readme.txt"));  // no .gguf → ignorado
    ModelCatalog cat;
    ModelRootRegistry reg(&cat);
    const QString id = reg.add(dir.path(), "main", "manual", QStringList{});
    QSignalSpy spy(&reg, &ModelRootRegistry::scanFinished);
    reg.scan(id);
    QVERIFY(spy.wait(5000) || spy.count() > 0);
    QVERIFY(cat.count() >= 1);  // el .gguf entró al catálogo
}

void RegistriesTests::ggufScanner_reusesUnchangedCatalogMetadata()
{
    QTemporaryDir dir;
    const QString path = writeFakeFile(dir.filePath("cached-Q4_K_M.gguf"), "x");
    const QFileInfo info(path);

    ModelRoot root;
    root.id = QStringLiteral("root-cache");
    root.path = dir.path();
    root.enabled = true;
    root.isOnline = true;

    CatalogModel cached;
    cached.id = QStringLiteral("stable-cached-id");
    cached.rootId = root.id;
    cached.absolutePath = path;
    cached.fileName = info.fileName();
    cached.sizeBytes = info.size();
    cached.mtime = info.lastModified();
    cached.quantReal = QStringLiteral("cached-quant");
    cached.tensorBreakdown = QStringLiteral("cached-metadata");
    cached.isAvailable = true;

    GGUFScanner scanner;
    const QList<CatalogModel> found = scanner.scan(root, {cached});
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().id, cached.id);
    QCOMPARE(found.first().quantReal, cached.quantReal);
    QCOMPARE(found.first().tensorBreakdown, cached.tensorBreakdown);
}

void RegistriesTests::ollamaImporter_resolveScheme()
{
    // "ollama://" (sin dir) → store por defecto (no vacío).
    QVERIFY(!OllamaImporter::resolveStoreDir("ollama://").isEmpty());
    // "ollama://<dir>" → ese dir.
    QCOMPARE(OllamaImporter::resolveStoreDir("ollama://C:/x/models"),
             QStringLiteral("C:/x/models"));
    // No-scheme → vacío (no es un root de Ollama).
    QVERIFY(OllamaImporter::resolveStoreDir("C:/models").isEmpty());
}

void RegistriesTests::ollamaImporter_scanFindsBlobs()
{
    QTemporaryDir dir;
    const QString store = makeFakeOllamaStore(dir.path(), "tinyllama", "latest");
    QVERIFY(OllamaImporter::looksLikeStore(store));

    const QList<OllamaImporter::Entry> entries = OllamaImporter::scan(store);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().name, QStringLiteral("tinyllama:latest"));
    QVERIFY(entries.first().blobPath.contains("sha256-"));
    QCOMPARE(entries.first().sizeBytes, qint64(17));
}

void RegistriesTests::ollamaImporter_extractsProjector()
{
    QTemporaryDir dir;
    const QString modelHex(64, QLatin1Char('b'));
    const QString projHex(64, QLatin1Char('c'));
    writeFakeFile(dir.path() + "/blobs/sha256-" + modelHex, "GGUF-model");
    writeFakeFile(dir.path() + "/blobs/sha256-" + projHex, "GGUF-mmproj");

    QJsonObject modelLayer{{"mediaType", "application/vnd.ollama.image.model"},
                           {"digest", "sha256:" + modelHex}, {"size", 10}};
    QJsonObject projLayer{{"mediaType", "application/vnd.ollama.image.projector"},
                          {"digest", "sha256:" + projHex}, {"size", 11}};
    QJsonObject manifest{{"layers", QJsonArray{modelLayer, projLayer}}};
    writeFakeFile(dir.path() + "/manifests/registry.ollama.ai/library/llava/7b",
                  QJsonDocument(manifest).toJson());

    const QList<OllamaImporter::Entry> entries = OllamaImporter::scan(dir.path());
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().name, QStringLiteral("llava:7b"));
    QVERIFY(entries.first().mmprojPath.contains("sha256-" + projHex));
}

void RegistriesTests::modelRootRegistry_ollamaSchemeIngests()
{
    QTemporaryDir dir;
    const QString store = makeFakeOllamaStore(dir.path(), "qwen2.5", "3b");
    ModelCatalog cat;
    ModelRootRegistry reg(&cat);
    // El scheme ollama:// resuelve al dir del store y marca kind="ollama".
    const QString id = reg.add("ollama://" + store, "", "manual", QStringList{});
    QVERIFY(!id.isEmpty());
    QCOMPARE(reg.get(id).value("kind").toString(), QStringLiteral("ollama"));

    QSignalSpy spy(&reg, &ModelRootRegistry::scanFinished);
    reg.scan(id);
    QVERIFY(spy.wait(5000) || spy.count() > 0);
    QVERIFY(cat.count() >= 1);  // el blob de Ollama entró al catálogo
}

QTEST_MAIN(RegistriesTests)
#include "test_registries.moc"
