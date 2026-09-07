// Raíz de trabajo del agente: de dónde sale el cwd con el que corren las tools.
// El bug que originó estos tests: sin proyecto válido el backend caía al home
// del usuario, el índice de sesiones perdía el projectDir y el system prompt
// seguía anunciando otro proyecto; el modelo pedía ".." y el runner lo denegaba
// en bucle hasta cortar el turno.
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "core/agent/LlamaAgentBackend.h"

class AgentProjectDirTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void safeProjectDir_rejectsHomeAndRoot();
    void startNeverFallsBackToHome();
    void snapshotKeepsProjectDirWhenIndexLostIt();
    void loadFromDiskRecoversSessionsMissingFromIndex();

private:
    QString storeDir() const;
    void clearStore() const;
};

QString AgentProjectDirTests::storeDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/agent_llamaagent");
}

void AgentProjectDirTests::clearStore() const
{
    QDir(storeDir()).removeRecursively();
    QVERIFY(QDir().mkpath(storeDir()));
}

void AgentProjectDirTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void AgentProjectDirTests::safeProjectDir_rejectsHomeAndRoot()
{
    QVERIFY(LlamaAgentBackend::safeProjectDir(QString()).isEmpty());
    QVERIFY(LlamaAgentBackend::safeProjectDir(QStringLiteral("   ")).isEmpty());
    QVERIFY(LlamaAgentBackend::safeProjectDir(QDir::homePath()).isEmpty());
    QVERIFY(LlamaAgentBackend::safeProjectDir(QDir::homePath() + QStringLiteral("/")).isEmpty());
    QDir homeParent(QDir::homePath());
    QVERIFY(homeParent.cdUp());
    QVERIFY(LlamaAgentBackend::safeProjectDir(homeParent.absolutePath()).isEmpty());
    QVERIFY(LlamaAgentBackend::safeProjectDir(QDir::rootPath()).isEmpty());
    QVERIFY(LlamaAgentBackend::safeProjectDir(
                QDir::homePath() + QStringLiteral("/__no_existe_llamacode__")).isEmpty());

    QTemporaryDir project;
    QVERIFY(project.isValid());
    QCOMPARE(LlamaAgentBackend::safeProjectDir(project.path()),
             QDir::cleanPath(QFileInfo(project.path()).absoluteFilePath()));
}

void AgentProjectDirTests::startNeverFallsBackToHome()
{
    clearStore();
    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.cwd = QDir::homePath();          // lo que llegaba cuando no había proyecto
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:1");
    ctx.modelId = QStringLiteral("test-model");

    LlamaAgentBackend backend;
    backend.start(ctx);
    const QString prompt = backend.systemPromptForTest();
    backend.stop();

    QVERIFY(!prompt.contains(QDir::toNativeSeparators(QDir::homePath())
                             + QStringLiteral("\n")));
    QCOMPARE(backend.currentProjectDir(), LlamaAgentBackend::fallbackWorkspaceDir());
    QVERIFY(prompt.contains(QDir::toNativeSeparators(
        LlamaAgentBackend::fallbackWorkspaceDir())));
    clearStore();
}

void AgentProjectDirTests::snapshotKeepsProjectDirWhenIndexLostIt()
{
    clearStore();
    QTemporaryDir project;
    QVERIFY(project.isValid());
    const QString projectPath = QDir::cleanPath(project.path());

    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.cwd = projectPath;
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:1");
    ctx.modelId = QStringLiteral("test-model");

    QString sessionId;
    {
        LlamaAgentBackend backend;
        backend.start(ctx);
        sessionId = backend.currentSessionId();
        QVERIFY(!sessionId.isEmpty());
        backend.stop();
    }
    // El snapshot guarda el proyecto, no sólo el índice.
    QFile snapshot(storeDir() + QStringLiteral("/") + sessionId + QStringLiteral(".json"));
    QVERIFY(snapshot.open(QIODevice::ReadOnly));
    const QJsonObject obj = QJsonDocument::fromJson(snapshot.readAll()).object();
    snapshot.close();
    QCOMPARE(obj.value(QStringLiteral("projectDir")).toString(), projectPath);

    // Darle un mensaje: una sesión vacía se descarta al arrancar y no llega a
    // restaurar nada. Acá lo que se prueba es el cwd de una sesión con historia.
    QJsonObject used = obj;
    used[QStringLiteral("messages")] = QJsonArray{QJsonObject{
        {QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("content"), QStringLiteral("mensaje")}}};
    QVERIFY(snapshot.open(QIODevice::WriteOnly | QIODevice::Truncate));
    snapshot.write(QJsonDocument(used).toJson());
    snapshot.close();

    // Índice sin projectDir (el caso real: entrada vieja o incompleta).
    QFile index(storeDir() + QStringLiteral("/index.json"));
    QVERIFY(index.open(QIODevice::WriteOnly | QIODevice::Truncate));
    index.write(QJsonDocument(QJsonArray{QJsonObject{
        {QStringLiteral("id"), sessionId},
        {QStringLiteral("title"), QStringLiteral("Sesión")},
        {QStringLiteral("created"), 1.0}}}).toJson());
    index.close();

    AgentContext blank = ctx;
    blank.cwd = QString();               // arranque sin proyecto: antes → home
    LlamaAgentBackend restored;
    restored.start(blank);
    QCOMPARE(restored.currentProjectDir(), projectPath);
    // Y el system prompt deja de anunciar el cwd viejo.
    QVERIFY(restored.systemPromptForTest().contains(
        QDir::toNativeSeparators(projectPath)));
    restored.stop();
    clearStore();
}

void AgentProjectDirTests::loadFromDiskRecoversSessionsMissingFromIndex()
{
    clearStore();
    QTemporaryDir project;
    QVERIFY(project.isValid());
    const QString projectPath = QDir::cleanPath(project.path());
    const QString sessionId = QStringLiteral("huerfana-1");

    QFile snapshot(storeDir() + QStringLiteral("/") + sessionId + QStringLiteral(".json"));
    QVERIFY(snapshot.open(QIODevice::WriteOnly | QIODevice::Truncate));
    snapshot.write(QJsonDocument(QJsonObject{
        {QStringLiteral("id"), sessionId},
        {QStringLiteral("title"), QStringLiteral("Rescatada")},
        {QStringLiteral("projectDir"), projectPath},
        {QStringLiteral("projectName"), QFileInfo(projectPath).fileName()},
        {QStringLiteral("messages"), QJsonArray{QJsonObject{
            {QStringLiteral("role"), QStringLiteral("assistant")},
            {QStringLiteral("content"), QStringLiteral("mensaje")}}}},
        {QStringLiteral("api"), QJsonArray{}},
        {QStringLiteral("snapshotVersion"), 2}}).toJson());
    snapshot.close();
    // Índice vacío: la sesión existe en disco pero nadie la lista.
    QFile index(storeDir() + QStringLiteral("/index.json"));
    QVERIFY(index.open(QIODevice::WriteOnly | QIODevice::Truncate));
    index.write(QJsonDocument(QJsonArray{}).toJson());
    index.close();

    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:1");
    ctx.modelId = QStringLiteral("test-model");

    LlamaAgentBackend backend;
    backend.start(ctx);
    bool found = false;
    for (const QVariant &v : backend.sessions()) {
        if (v.toMap().value(QStringLiteral("id")).toString() != sessionId) continue;
        found = true;
        QCOMPARE(v.toMap().value(QStringLiteral("projectDir")).toString(), projectPath);
    }
    QVERIFY(found);
    QCOMPARE(backend.currentProjectDir(), projectPath);
    backend.stop();

    // El índice quedó reparado en disco.
    QVERIFY(index.open(QIODevice::ReadOnly));
    const QJsonArray repaired = QJsonDocument::fromJson(index.readAll()).array();
    index.close();
    QCOMPARE(repaired.size(), 1);
    QCOMPARE(repaired.first().toObject().value(QStringLiteral("id")).toString(), sessionId);
    clearStore();
}

QTEST_MAIN(AgentProjectDirTests)
#include "test_agent_project_dir.moc"
