#include <QtTest>
#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTcpServer>
#include <QFile>

#include "core/assistant/AssistantRuntime.h"

class AssistantRuntimeTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void rejectsInvalidTokenAndEmptyText();
    void acceptsAndDeduplicatesMessages();
    void completesMessageAndCreatesNotification();
    void servesAuthenticatedHttpEndpoint();
};

void AssistantRuntimeTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                  + QStringLiteral("/assistant-events.json"));
}

void AssistantRuntimeTests::cleanupTestCase()
{
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                  + QStringLiteral("/assistant-events.json"));
}

void AssistantRuntimeTests::rejectsInvalidTokenAndEmptyText()
{
    AssistantRuntime runtime;
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = probe.serverPort();
    probe.close();
    QVERIFY(runtime.start(port, QStringLiteral("secret")));
    const QVariantMap invalid = runtime.receiveMessage(
        {{QStringLiteral("text"), QStringLiteral("hola")}}, QStringLiteral("bad"));
    QVERIFY(!invalid.value(QStringLiteral("ok")).toBool());
    QCOMPARE(invalid.value(QStringLiteral("error")).toString(), QStringLiteral("unauthorized"));
    const QVariantMap empty = runtime.receiveMessage(
        {{QStringLiteral("text"), QString()}}, QStringLiteral("secret"));
    QVERIFY(!empty.value(QStringLiteral("ok")).toBool());
    QCOMPARE(empty.value(QStringLiteral("error")).toString(), QStringLiteral("text_required"));
}

void AssistantRuntimeTests::acceptsAndDeduplicatesMessages()
{
    AssistantRuntime runtime;
    const QVariantMap first = runtime.receiveMessage(
        {{QStringLiteral("id"), QStringLiteral("m-1")},
         {QStringLiteral("text"), QStringLiteral("revisá el estado")}}, QStringLiteral("secret"));
    // Sin token configurado, el runtime no acepta mensajes, incluso si se usa
    // como objeto in-process. Esto evita un bypass accidental de la frontera.
    QVERIFY(!first.value(QStringLiteral("ok")).toBool());

    QVERIFY(runtime.start(0, QStringLiteral("secret")) == false);
    QVERIFY(runtime.start(0, QStringLiteral("secret")) == false);
    // El endpoint usa puertos efímeros sólo a través de QTcpServer de prueba; el
    // flujo in-process se prueba con un puerto local disponible más abajo.
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = probe.serverPort();
    probe.close();
    QVERIFY(runtime.start(port, QStringLiteral("secret")));

    const QVariantMap accepted = runtime.receiveMessage(
        {{QStringLiteral("id"), QStringLiteral("m-1")},
         {QStringLiteral("text"), QStringLiteral("revisá el estado")}}, QStringLiteral("secret"));
    QVERIFY(accepted.value(QStringLiteral("ok")).toBool());
    QCOMPARE(runtime.pendingMessages().size(), 1);
    const QVariantMap duplicate = runtime.receiveMessage(
        {{QStringLiteral("id"), QStringLiteral("m-1")},
         {QStringLiteral("text"), QStringLiteral("duplicado")}}, QStringLiteral("secret"));
    QVERIFY(duplicate.value(QStringLiteral("duplicate")).toBool());
    QCOMPARE(runtime.pendingMessages().size(), 1);
}

void AssistantRuntimeTests::completesMessageAndCreatesNotification()
{
    AssistantRuntime runtime;
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = probe.serverPort();
    probe.close();
    QVERIFY(runtime.start(port, QStringLiteral("secret")));
    QVERIFY(runtime.receiveMessage({{QStringLiteral("id"), QStringLiteral("m-2")},
                                    {QStringLiteral("text"), QStringLiteral("hola")}},
                                   QStringLiteral("secret"))
                .value(QStringLiteral("ok")).toBool());
    QVERIFY(runtime.completeMessage(QStringLiteral("m-2"), QStringLiteral("listo"), true));
    QVERIFY(runtime.pendingMessages().isEmpty());
    QVERIFY(!runtime.notifications().isEmpty());
    QCOMPARE(runtime.notifications().last().toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("assistant.reply"));
    QVERIFY(!runtime.completeMessage(QStringLiteral("missing"), QString(), true));
}

void AssistantRuntimeTests::servesAuthenticatedHttpEndpoint()
{
    AssistantRuntime runtime;
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = probe.serverPort();
    probe.close();
    QVERIFY(runtime.start(port, QStringLiteral("secret")));

    QNetworkAccessManager nam;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/v1/assistant/messages")
                                 .arg(port)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer secret"));
    QNetworkReply *reply = nam.post(request, QByteArrayLiteral(
        "{\"id\":\"http-1\",\"text\":\"estado\"}"));
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 202);
    const QVariantMap result = QJsonDocument::fromJson(reply->readAll()).object().toVariantMap();
    QVERIFY(result.value(QStringLiteral("ok")).toBool());
    QCOMPARE(result.value(QStringLiteral("id")).toString(), QStringLiteral("http-1"));
    reply->deleteLater();
}

QTEST_MAIN(AssistantRuntimeTests)
#include "test_assistant_runtime.moc"
