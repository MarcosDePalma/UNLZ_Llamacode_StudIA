// Tests de las variantes headless (sin diálogo) de AppController: export/import
// de datos de usuario y export de sesión de chat a ruta explícita. Garantizan
// que toda feature con diálogo tenga un camino api/headless equivalente.

#include <QtTest>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include "AppController.h"
#include "core/agent/BrowserTeach.h"
#include "core/CatalogModel.h"

class AppControllerTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void exportUserDataToWritesBackup();
    void importUserDataFromRoundTrips();
    void exportUserDataToEmptyPathErrors();
    void exportChatSessionToMissingSessionErrors();
    void parseGpuPowerCsvParses();
    void parseGpuPowerCsvTolerant();
    void ggufRecommendationCandidateFilter();
    void modelRecommendationsUseResolvableGgufNames();
    void createRecommendedLaunchProfileBuildsProfile();
    void browserMcpEffectiveResolves();
    void browserTeachSkillsLifecycle();

private:
    QTemporaryDir m_tmp;
};

void AppControllerTests::initTestCase()
{
    // Aísla AppData/AppLocalData a una ubicación de test.
    QStandardPaths::setTestModeEnabled(true);
    QVERIFY(m_tmp.isValid());
    // Y los perfiles, que viven en Documentos y NO los redirige el modo test.
    // Sin esto, createRecommendedLaunchProfileBuildsProfile() creaba un perfil
    // real por cada corrida de la suite en la máquina de quien la ejecuta.
    qputenv("LLAMACODE_PROFILES_DIR", m_tmp.filePath(QStringLiteral("profiles")).toLocal8Bit());
}

void AppControllerTests::exportUserDataToWritesBackup()
{
    AppController app;
    const QString path = m_tmp.filePath(QStringLiteral("backup.json"));
    const QString written = app.exportUserDataTo(path);
    QCOMPARE(written, path);

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("app")).toString(), QStringLiteral("LlamaCode"));
}

void AppControllerTests::importUserDataFromRoundTrips()
{
    AppController app;
    const QString path = m_tmp.filePath(QStringLiteral("backup_rt.json"));
    QVERIFY(!app.exportUserDataTo(path).isEmpty());
    // Re-importar el backup recién escrito devuelve la ruta (no vacío).
    QCOMPARE(app.importUserDataFrom(path), path);
}

void AppControllerTests::exportUserDataToEmptyPathErrors()
{
    AppController app;
    QVERIFY(app.exportUserDataTo(QString()).isEmpty());
}

void AppControllerTests::exportChatSessionToMissingSessionErrors()
{
    AppController app;
    // Sesión inexistente → "" (sin colgar en diálogo).
    const QString out = app.exportChatSessionTo(QStringLiteral("no-such-id"),
                                                QStringLiteral("md"),
                                                m_tmp.filePath(QStringLiteral("c.md")));
    QVERIFY(out.isEmpty());
}

void AppControllerTests::parseGpuPowerCsvParses()
{
    // index, name, limit, default, min, max, draw
    const QString csv =
        QStringLiteral("0, NVIDIA GeForce RTX 3090, 280.00, 350.00, 100.00, 350.00, 142.50\n");
    const QVariantList gpus = AppController::parseGpuPowerCsv(csv);
    QCOMPARE(gpus.size(), 1);
    const QVariantMap g = gpus.first().toMap();
    QCOMPARE(g.value("index").toInt(), 0);
    QCOMPARE(g.value("name").toString(), QStringLiteral("NVIDIA GeForce RTX 3090"));
    QCOMPARE(g.value("currentW").toDouble(), 280.0);
    QCOMPARE(g.value("defaultW").toDouble(), 350.0);
    QCOMPARE(g.value("minW").toDouble(), 100.0);
    QCOMPARE(g.value("maxW").toDouble(), 350.0);
    QCOMPARE(g.value("drawW").toDouble(), 142.5);
}

void AppControllerTests::parseGpuPowerCsvTolerant()
{
    // Línea basura (sin index numérico) y campo faltante → se ignoran sin romper.
    const QString csv = QStringLiteral("garbage line\n1, GPU B, 200, 250, 90, 250\n");
    const QVariantList gpus = AppController::parseGpuPowerCsv(csv);
    QCOMPARE(gpus.size(), 1);
    const QVariantMap g = gpus.first().toMap();
    QCOMPARE(g.value("index").toInt(), 1);
    QCOMPARE(g.value("drawW").toDouble(), 0.0);   // power.draw ausente
}

void AppControllerTests::ggufRecommendationCandidateFilter()
{
    QVERIFY(!AppController::isGgufRecommendationCandidate(
        QStringLiteral("lmstudio-community/Qwen3-14B-MLX-4bit"), false, false));
    QVERIFY(!AppController::isGgufRecommendationCandidate(
        QStringLiteral("cyankiwi/Qwen3.5-9B-AWQ-BF16-INT4"), false, false));
    QVERIFY(AppController::isGgufRecommendationCandidate(
        QStringLiteral("Qwen/Qwen3.5-9B-MTP"), false, true));
    QVERIFY(AppController::isGgufRecommendationCandidate(
        QStringLiteral("unsloth/Qwen3.5-9B-GGUF"), false, false));
}

void AppControllerTests::modelRecommendationsUseResolvableGgufNames()
{
    QCOMPARE(AppController::recommendedGgufFileName(
                 QStringLiteral("unsloth/Qwen3.6-35B-A3B-MTP-GGUF"),
                 QStringLiteral("Qwen/Qwen3.6-35B-A3B-MTP"),
                 QStringLiteral("Q4_K_M")),
             QStringLiteral("Qwen3.6-35B-A3B-UD-Q4_K_M.gguf"));
    QCOMPARE(AppController::recommendedGgufFileName(
                 QStringLiteral("unsloth/Qwen3.5-2B-MTP-GGUF"),
                 QStringLiteral("Qwen/Qwen3.5-2B-MTP"),
                 QStringLiteral("Q4_K_M")),
             QStringLiteral("Qwen3.5-2B-Q4_K_M.gguf"));
    QCOMPARE(AppController::recommendedGgufFileName(
                 QStringLiteral("bartowski/Qwen2.5-7B-Instruct-GGUF"),
                 QStringLiteral("Qwen/Qwen2.5-7B-Instruct"),
                 QStringLiteral("Q4_K_M")),
             QStringLiteral("Qwen2.5-7B-Instruct-Q4_K_M.gguf"));

    const QStringList siblings = {
        QStringLiteral("Qwen3.6-35B-A3B-UD-IQ3_S.gguf"),
        QStringLiteral("Qwen3.6-35B-A3B-UD-Q4_K_M.gguf"),
        QStringLiteral("Qwen3.6-35B-A3B-Q8_0.gguf")
    };
    QCOMPARE(AppController::resolveRecommendedGgufFileName(
                 siblings,
                 QStringLiteral("Qwen3.6-35B-A3B-MTP-Q4_K_M.gguf")),
             QStringLiteral("Qwen3.6-35B-A3B-UD-Q4_K_M.gguf"));
    const QStringList hermesSiblings = {
        QStringLiteral("Hermes-3-Llama-3.1-8B-Q3_K_M.gguf"),
        QStringLiteral("Hermes-3-Llama-3.1-8B-IQ4_XS.gguf"),
        QStringLiteral("Hermes-3-Llama-3.1-8B-Q8_0.gguf")
    };
    QCOMPARE(AppController::resolveRecommendedGgufFileName(
                 hermesSiblings,
                 QStringLiteral("Hermes-3-Llama-3.1-8B-IQ4_XS.gguf")),
             QStringLiteral("Hermes-3-Llama-3.1-8B-IQ4_XS.gguf"));

    const QStringList catalogCandidates = {
        QStringLiteral(":/assets/hwfit/hf_models.json"),
        QDir::current().absoluteFilePath(QStringLiteral("assets/hwfit/hf_models.json")),
        QDir::current().absoluteFilePath(QStringLiteral("../assets/hwfit/hf_models.json")),
        QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../assets/hwfit/hf_models.json"))
    };
    QFile catalog;
    for (const QString &path : catalogCandidates) {
        catalog.setFileName(path);
        if (catalog.exists())
            break;
    }
    QVERIFY(catalog.open(QIODevice::ReadOnly));
    const QJsonArray rows = QJsonDocument::fromJson(catalog.readAll()).array();
    bool sawTinyQwen = false;
    for (const QJsonValue &v : rows) {
        const QJsonObject row = v.toObject();
        if (row.value(QStringLiteral("name")).toString() == QLatin1String("Qwen/Qwen3.5-2B-MTP")) {
            const QJsonArray sources = row.value(QStringLiteral("gguf_sources")).toArray();
            QVERIFY(!sources.isEmpty());
            QCOMPARE(sources.first().toObject().value(QStringLiteral("repo")).toString(),
                     QStringLiteral("unsloth/Qwen3.5-2B-MTP-GGUF"));
            sawTinyQwen = true;
            break;
        }
    }
    QVERIFY(sawTinyQwen);
}

void AppControllerTests::createRecommendedLaunchProfileBuildsProfile()
{
    AppController app;

    const QString exePath = m_tmp.filePath(QStringLiteral("llama-server.exe"));
    QFile exe(exePath);
    QVERIFY(exe.open(QIODevice::WriteOnly));
    exe.write("fake-binary");
    exe.close();
    const QString binaryId = app.binaryRegistry()->add(
        exePath, QStringLiteral("fake llama-server"), QStringLiteral("custom"),
        QStringLiteral("cpu"), QString());
    QVERIFY(!binaryId.isEmpty());

    const QString modelPath = m_tmp.filePath(QStringLiteral("Qwen3.5-9B-Q4_K_M.gguf"));
    QFile modelFile(modelPath);
    QVERIFY(modelFile.open(QIODevice::WriteOnly));
    modelFile.write("GGUF");
    modelFile.close();

    CatalogModel model;
    model.id = QStringLiteral("setup-model");
    model.rootId = QStringLiteral("setup-root");
    model.absolutePath = modelPath;
    model.fileName = QFileInfo(modelPath).fileName();
    model.sizeBytes = QFileInfo(modelPath).size();
    model.mtime = QFileInfo(modelPath).lastModified();
    model.familyHint = QStringLiteral("qwen");
    model.quantHint = QStringLiteral("Q4_K_M");
    model.isAvailable = true;
    app.modelCatalog()->addOrUpdate(model);

    const QString launchId = app.createRecommendedLaunchProfile();
    QVERIFY(!launchId.isEmpty());
    QVERIFY(app.hasAnyLaunch());
    QVERIFY(!app.needsSetup());
    QCOMPARE(app.profileManager()->getLaunchProfile(launchId).value("id").toString(), launchId);
}

void AppControllerTests::browserMcpEffectiveResolves()
{
    // Override "on"/"off" pisa el toggle global; "inherit" (u otro) lo hereda.
    QVERIFY( AppController::browserMcpEffective(QStringLiteral("on"),  false));
    QVERIFY( AppController::browserMcpEffective(QStringLiteral("on"),  true));
    QVERIFY(!AppController::browserMcpEffective(QStringLiteral("off"), true));
    QVERIFY(!AppController::browserMcpEffective(QStringLiteral("off"), false));
    QVERIFY( AppController::browserMcpEffective(QStringLiteral("inherit"), true));
    QVERIFY(!AppController::browserMcpEffective(QStringLiteral("inherit"), false));
    QVERIFY( AppController::browserMcpEffective(QString(), true));   // vacío → hereda
}

void AppControllerTests::browserTeachSkillsLifecycle()
{
    // sanitize: slug seguro para filename.
    QCOMPARE(BrowserTeach::sanitize(QStringLiteral("Login   Banco!!")),
             QStringLiteral("login-banco"));
    QCOMPARE(BrowserTeach::sanitize(QStringLiteral("a.b/c")), QStringLiteral("a-b-c"));

    // skillPath usa el slug + .mjs dentro de skillsDir.
    const QString path = BrowserTeach::skillPath(QStringLiteral("My Skill"));
    QVERIFY(path.endsWith(QStringLiteral("/my-skill.mjs")));
    QVERIFY(path.startsWith(BrowserTeach::skillsDir()));

    // recordCommand: codegen con -o al path; agrega url http válida.
    const QString rc = BrowserTeach::recordCommand(QStringLiteral("My Skill"),
                                                   QStringLiteral("https://x.com"));
    QVERIFY(rc.contains(QStringLiteral("playwright codegen")));
    QVERIFY(rc.contains(QStringLiteral("my-skill.mjs")));
    QVERIFY(rc.endsWith(QStringLiteral("https://x.com")));
    // url no-http se ignora.
    QVERIFY(!BrowserTeach::recordCommand(QStringLiteral("s"), QStringLiteral("ftp://x"))
                 .contains(QStringLiteral("ftp://")));

    // replayProgramArgs: {node, path}.
    const QStringList pa = BrowserTeach::replayProgramArgs(QStringLiteral("My Skill"));
    QCOMPARE(pa.size(), 2);
    QCOMPARE(pa.first(), QStringLiteral("node"));
    QVERIFY(pa.at(1).endsWith(QStringLiteral("my-skill.mjs")));

    // list/has/remove sobre un .mjs real escrito a disco (skillsDir aislado por
    // setTestModeEnabled en initTestCase).
    QVERIFY(!BrowserTeach::hasSkill(QStringLiteral("My Skill")));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("// dummy\n");
    f.close();
    QVERIFY(BrowserTeach::hasSkill(QStringLiteral("My Skill")));
    QVERIFY(BrowserTeach::listSkills().contains(QStringLiteral("my-skill")));
    QVERIFY(BrowserTeach::removeSkill(QStringLiteral("My Skill")));
    QVERIFY(!BrowserTeach::hasSkill(QStringLiteral("My Skill")));
}

QTEST_MAIN(AppControllerTests)
#include "test_appcontroller.moc"
