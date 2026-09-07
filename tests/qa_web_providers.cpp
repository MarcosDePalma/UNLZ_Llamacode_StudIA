#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include "core/agent/AgentToolRunner.h"

// Probe opt-in contra servicios reales; no forma parte de ctest.
// Ejemplos:
//   qa_web_providers camofox https://example.com
//   set LLAMACODE_QA_PLAYWRIGHT_CMD=npx @playwright/mcp@latest --headless
//   qa_web_providers playwright https://example.com
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    if (argc < 3) {
        err << "uso: qa_web_providers <camofox|playwright> <url>\n";
        return 2;
    }
    const QString provider = QString::fromLocal8Bit(argv[1]).toLower();
    const QString url = QString::fromLocal8Bit(argv[2]);
    AgentToolRunner runner;

    if (provider == QLatin1String("camofox")) {
        const QString base = qEnvironmentVariable(
            "LLAMACODE_QA_CAMOFOX_URL", QStringLiteral("http://127.0.0.1:9377"));
        runner.setWebProviders({QVariantMap{
            {QStringLiteral("provider"), QStringLiteral("camofox")},
            {QStringLiteral("baseUrl"), base},
            {QStringLiteral("apiKey"), qEnvironmentVariable("CAMOFOX_API_KEY")},
            {QStringLiteral("enabled"), true}}});
    } else if (provider == QLatin1String("playwright")) {
        const QString command = qEnvironmentVariable("LLAMACODE_QA_PLAYWRIGHT_CMD");
        if (command.isEmpty()) {
            err << "falta LLAMACODE_QA_PLAYWRIGHT_CMD\n";
            return 2;
        }
        runner.initServers({QVariantMap{
            {QStringLiteral("name"), QStringLiteral("playwright")},
            {QStringLiteral("type"), QStringLiteral("local")},
            {QStringLiteral("command"), command},
            {QStringLiteral("enabled"), true}}}, QCoreApplication::applicationDirPath());
    } else {
        err << "provider invalido\n";
        return 2;
    }

    int exitCode = 1;
    QObject::connect(&runner, &AgentToolRunner::toolExecuted, &app,
                     [&](const QVariantMap &result) {
        QTextStream(stdout) << result.value(QStringLiteral("result")).toString() << '\n';
        exitCode = result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
        app.quit();
    });
    QTimer::singleShot(60000, &app, [&]() {
        err << "timeout\n";
        app.quit();
    });
    runner.executeTool(
        QStringLiteral("qa"), QStringLiteral("web_fetch"),
        QString::fromUtf8(QJsonDocument(QJsonObject{
            {QStringLiteral("url"), url},
            {QStringLiteral("provider"), provider}}).toJson(QJsonDocument::Compact)),
        QCoreApplication::applicationDirPath());
    app.exec();
    runner.shutdown();
    return exitCode;
}
