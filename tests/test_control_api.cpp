// Integration tests del ControlApi (HTTP headless que espeja un QObject vía
// meta-object). Levantamos el server en un puerto libre contra un target de
// prueba y hacemos requests HTTP reales por QTcpSocket.
//   GET /health, GET /methods, GET /prop, POST /setprop, POST /invoke + errores.

#include <QtTest>
#include <QTcpSocket>
#include <QTcpServer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include "core/ControlApi.h"
#include "AppController.h"

// Sub-objeto hijo: emula un registry/profileManager expuesto como QObject* prop.
class FakeChild : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString label MEMBER m_label)
public:
    Q_INVOKABLE QString greet(const QString &who) { return QStringLiteral("hi ") + who; }
    QString m_label = QStringLiteral("child");
};

// Target de prueba: una propiedad escribible y un método invocable con retorno.
class FakeTarget : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int counter MEMBER m_counter)
    Q_PROPERTY(FakeChild* child READ child CONSTANT)
public:
    Q_INVOKABLE int addNums(int a, int b) { return a + b; }
    FakeChild *child() { return &m_child; }
    int m_counter = 7;
    FakeChild m_child;
};

class ControlApiTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void health();
    void getProperty();
    void setProperty();
    void invokeMethodWithReturn();
    void invokeUnknownMethodErrors();
    void methodsListsChildTargets();
    void getChildProperty();
    void setChildProperty();
    void invokeChildMethod();
    void unknownTargetErrors();
    void helpIndexDescribesEndpoints();
    void unknownPropertyListsAvailable();
    void unknownMethodListsAvailable();
    void wrongArityListsValidArgCounts();
    void unknownTargetListsAvailable();
    void requestIdFromQuery();
    void requestIdFromHeader();
    void requestIdFromBody();
    void requestIdGeneratedWhenMissing();
    void appControllerEngineeringCatalogIsHeadless();
    void harnessSpecIsHeadless();
    void auxiliarySchedulerIsHeadless();

private:
    QJsonObject request(const QByteArray &method, const QString &path,
                        const QByteArray &body = {});
    static quint16 freePort();

    FakeTarget m_target;
    ControlApi *m_api = nullptr;
    quint16 m_port = 0;
};

static QJsonObject requestJsonAt(quint16 port, const QByteArray &method,
                                 const QString &path, const QByteArray &body = {})
{
    QByteArray resp;
    QTcpSocket sock;
    QObject::connect(&sock, &QTcpSocket::readyRead, [&] { resp += sock.readAll(); });
    sock.connectToHost(QHostAddress::LocalHost, port);
    QByteArray req = method + " " + path.toUtf8() + " HTTP/1.1\r\nHost: localhost\r\n";
    if (!body.isEmpty()) {
        req += "Content-Type: application/json\r\nContent-Length: "
            + QByteArray::number(body.size()) + "\r\n";
    }
    req += "Connection: close\r\n\r\n" + body;
    QElapsedTimer timer;
    timer.start();
    bool sent = false;
    while (timer.elapsed() < 3000) {
        if (!sent && sock.state() == QAbstractSocket::ConnectedState) {
            sock.write(req);
            sock.flush();
            sent = true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const int hdr = resp.indexOf("\r\n\r\n");
        if (hdr >= 0) {
            int length = 0;
            for (const QByteArray &line : resp.left(hdr).split('\n'))
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
            if (resp.size() - hdr - 4 >= length)
                return QJsonDocument::fromJson(resp.mid(hdr + 4)).object();
        }
    }
    return {};
}

quint16 ControlApiTests::freePort()
{
    QTcpServer s;
    s.listen(QHostAddress::LocalHost, 0);
    const quint16 p = s.serverPort();
    s.close();
    return p;
}

void ControlApiTests::initTestCase()
{
    m_port = freePort();
    m_api = new ControlApi(&m_target, this);
    QVERIFY(m_api->start(m_port));
}

QJsonObject ControlApiTests::request(const QByteArray &method, const QString &path,
                                     const QByteArray &body)
{
    // Server y client viven en el MISMO hilo, así que NO podemos usar
    // waitForReadyRead (no atiende el socket de escucha del server). Bombeamos
    // el event loop global hasta tener la respuesta completa o timeout.
    QByteArray resp;
    QTcpSocket sock;
    QObject::connect(&sock, &QTcpSocket::readyRead, [&] { resp += sock.readAll(); });
    sock.connectToHost(QHostAddress::LocalHost, m_port);

    QByteArray req = method + " " + path.toUtf8() + " HTTP/1.1\r\n";
    req += "Host: localhost\r\n";
    if (!body.isEmpty()) {
        req += "Content-Type: application/json\r\n";
        req += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    }
    req += "Connection: close\r\n\r\n";
    req += body;

    auto responseComplete = [&]() -> bool {
        const int hdr = resp.indexOf("\r\n\r\n");
        if (hdr < 0) return false;
        int clen = 0;
        for (const QByteArray &l : resp.left(hdr).split('\n'))
            if (l.toLower().startsWith("content-length:"))
                clen = l.mid(l.indexOf(':') + 1).trimmed().toInt();
        return resp.size() - (hdr + 4) >= clen;
    };

    QElapsedTimer timer; timer.start();
    bool sent = false;
    while (timer.elapsed() < 3000 && !responseComplete()) {
        if (!sent && sock.state() == QAbstractSocket::ConnectedState) {
            sock.write(req); sock.flush(); sent = true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    const int hdr = resp.indexOf("\r\n\r\n");
    if (hdr < 0) return {};
    return QJsonDocument::fromJson(resp.mid(hdr + 4)).object();
}

static QJsonObject requestWithHeader(quint16 port, const QByteArray &method, const QString &path,
                                     const QByteArray &headerName, const QByteArray &headerValue)
{
    QByteArray resp;
    QTcpSocket sock;
    QObject::connect(&sock, &QTcpSocket::readyRead, [&] { resp += sock.readAll(); });
    sock.connectToHost(QHostAddress::LocalHost, port);

    QByteArray req = method + " " + path.toUtf8() + " HTTP/1.1\r\n";
    req += "Host: localhost\r\n";
    req += headerName + ": " + headerValue + "\r\n";
    req += "Connection: close\r\n\r\n";

    auto responseComplete = [&]() -> bool {
        const int hdr = resp.indexOf("\r\n\r\n");
        if (hdr < 0) return false;
        int clen = 0;
        for (const QByteArray &l : resp.left(hdr).split('\n'))
            if (l.toLower().startsWith("content-length:"))
                clen = l.mid(l.indexOf(':') + 1).trimmed().toInt();
        return resp.size() - (hdr + 4) >= clen;
    };

    QElapsedTimer timer; timer.start();
    bool sent = false;
    while (timer.elapsed() < 3000 && !responseComplete()) {
        if (!sent && sock.state() == QAbstractSocket::ConnectedState) {
            sock.write(req); sock.flush(); sent = true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    const int hdr = resp.indexOf("\r\n\r\n");
    if (hdr < 0) return {};
    return QJsonDocument::fromJson(resp.mid(hdr + 4)).object();
}

void ControlApiTests::health()
{
    const QJsonObject o = request("GET", "/health");
    QCOMPARE(o.value("ok").toBool(), true);
}

void ControlApiTests::getProperty()
{
    const QJsonObject o = request("GET", "/prop?name=counter");
    QCOMPARE(o.value("value").toInt(), 7);
}

void ControlApiTests::setProperty()
{
    const QJsonObject o = request("POST", "/setprop",
                                  R"({"name":"counter","value":42})");
    QCOMPARE(o.value("ok").toBool(), true);
    QCOMPARE(m_target.m_counter, 42);
}

void ControlApiTests::invokeMethodWithReturn()
{
    const QJsonObject o = request("POST", "/invoke",
                                  R"({"method":"addNums","args":[2,40]})");
    QCOMPARE(o.value("ok").toBool(), true);
    QCOMPARE(o.value("result").toInt(), 42);
}

void ControlApiTests::invokeUnknownMethodErrors()
{
    const QJsonObject o = request("POST", "/invoke",
                                  R"({"method":"nope","args":[]})");
    QVERIFY(o.contains("error"));
}

void ControlApiTests::methodsListsChildTargets()
{
    const QJsonObject o = request("GET", "/methods");
    const QJsonArray targets = o.value("targets").toArray();
    QVERIFY(targets.contains(QJsonValue(QStringLiteral("child"))));
}

void ControlApiTests::getChildProperty()
{
    const QJsonObject o = request("GET", "/prop?name=label&target=child");
    QCOMPARE(o.value("value").toString(), QStringLiteral("child"));
}

void ControlApiTests::setChildProperty()
{
    const QJsonObject o = request("POST", "/setprop",
                                  R"({"target":"child","name":"label","value":"x"})");
    QCOMPARE(o.value("ok").toBool(), true);
    QCOMPARE(m_target.m_child.m_label, QStringLiteral("x"));
}

void ControlApiTests::invokeChildMethod()
{
    const QJsonObject o = request("POST", "/invoke",
                                  R"({"target":"child","method":"greet","args":["bob"]})");
    QCOMPARE(o.value("ok").toBool(), true);
    QCOMPARE(o.value("result").toString(), QStringLiteral("hi bob"));
}

void ControlApiTests::unknownTargetErrors()
{
    const QJsonObject o = request("POST", "/invoke",
                                  R"({"target":"nope","method":"greet","args":["x"]})");
    QVERIFY(o.contains("error"));
}

// GET / y /help devuelven un índice autodescriptivo con los endpoints.
void ControlApiTests::helpIndexDescribesEndpoints()
{
    for (const QString &path : {QStringLiteral("/"), QStringLiteral("/help")}) {
        const QJsonObject o = request("GET", path);
        QVERIFY2(o.value("service").toString().contains("ControlApi"),
                 qPrintable("sin service en " + path));
        const QJsonObject ep = o.value("endpoints").toObject();
        QVERIFY(ep.contains("GET /health"));
        QVERIFY(ep.contains("POST /invoke"));
        QVERIFY(o.value("exampleFlow").toArray().size() > 0);
    }
}

// Propiedad desconocida → error con 'available' = nombres válidos (descubrimiento).
void ControlApiTests::unknownPropertyListsAvailable()
{
    const QJsonObject o = request("GET", "/prop?name=nope");
    QVERIFY(o.contains("error"));
    QVERIFY2(o.value("available").toArray().contains(QJsonValue(QStringLiteral("counter"))),
             "el error debería listar 'counter' como propiedad válida");
}

// Método desconocido → error con 'available' = "nombre/aridad".
void ControlApiTests::unknownMethodListsAvailable()
{
    const QJsonObject o = request("POST", "/invoke", R"({"method":"nope","args":[]})");
    QVERIFY(o.contains("error"));
    QVERIFY2(o.value("available").toArray().contains(QJsonValue(QStringLiteral("addNums/2"))),
             "el error debería listar 'addNums/2'");
}

// Método existente pero con aridad equivocada → 'validArgCounts' (no 'available').
void ControlApiTests::wrongArityListsValidArgCounts()
{
    const QJsonObject o = request("POST", "/invoke", R"({"method":"addNums","args":[1]})");
    QVERIFY(o.contains("error"));
    QVERIFY(o.value("error").toString().contains("aridad"));
    QVERIFY(o.value("validArgCounts").toArray().contains(QJsonValue(2)));
}

// Target desconocido → error con 'available' = sub-targets navegables.
void ControlApiTests::unknownTargetListsAvailable()
{
    const QJsonObject o = request("POST", "/invoke",
                                  R"({"target":"nope","method":"greet","args":["x"]})");
    QVERIFY(o.contains("error"));
    QVERIFY2(o.value("available").toArray().contains(QJsonValue(QStringLiteral("child"))),
             "el error debería listar 'child' como target válido");
}

void ControlApiTests::requestIdFromQuery()
{
    const QJsonObject o = request("GET", "/health?reqId=query-123");
    QCOMPARE(o.value("reqId").toString(), QStringLiteral("query-123"));
}

void ControlApiTests::requestIdFromHeader()
{
    const QJsonObject o = requestWithHeader(m_port, "GET", "/health", "x-req-id", "hdr-123");
    QCOMPARE(o.value("reqId").toString(), QStringLiteral("hdr-123"));
}

void ControlApiTests::requestIdFromBody()
{
    const QJsonObject o = request("POST", "/invoke",
                                  R"({"reqId":"body-123","method":"addNums","args":[1,2]})");
    QCOMPARE(o.value("reqId").toString(), QStringLiteral("body-123"));
    QCOMPARE(o.value("result").toInt(), 3);
}

void ControlApiTests::requestIdGeneratedWhenMissing()
{
    const QJsonObject o = request("GET", "/health");
    QVERIFY(!o.value("reqId").toString().isEmpty());
}

void ControlApiTests::appControllerEngineeringCatalogIsHeadless()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("LlamaCode"));
    QCoreApplication::setApplicationName(QStringLiteral("LlamaCode"));
    AppController app;
    ControlApi api(&app);
    const quint16 port = freePort();
    QVERIFY(api.start(port));

    const QJsonObject methods = requestJsonAt(port, "GET", "/methods");
    QVERIFY(methods.value(QStringLiteral("methods")).isArray());

    const QJsonObject catalog = requestJsonAt(
        port, "POST", "/invoke",
        R"({"method":"engineeringWorkflows","args":[]})");
    QVERIFY(catalog.value("ok").toBool());
    QCOMPARE(catalog.value("result").toArray().size(), 6);

    const QJsonObject safety = requestJsonAt(
        port, "POST", "/invoke",
        R"({"method":"engineeringSafetyProfiles","args":[]})");
    QVERIFY(safety.value("ok").toBool());
    QVERIFY(safety.value("result").toArray().size() >= 4);

    const QJsonObject installed = requestJsonAt(
        port, "POST", "/invoke",
        R"({"method":"installEngineeringWorkflow","args":["qa"]})");
    QVERIFY(installed.value("ok").toBool());
    QVERIFY(!installed.value("result").toString().isEmpty());
}

// Contrato headless del harness modular: catálogo de packs, spec resuelto de un
// perfil, diff contra el padre y resumen (tools + tokens + warnings) tienen que
// ser alcanzables por /invoke sobre el target profileManager. Sin esto la
// personalización sería sólo de UI (ver docs/HEADLESS.md).
void ControlApiTests::harnessSpecIsHeadless()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("LlamaCode"));
    QCoreApplication::setApplicationName(QStringLiteral("LlamaCode"));
    AppController app;
    ControlApi api(&app);
    const quint16 port = freePort();
    QVERIFY(api.start(port));

    const QJsonObject packs = requestJsonAt(
        port, "POST", "/invoke",
        R"({"target":"profileManager","method":"harnessPackCatalog","args":[]})");
    QVERIFY(packs.value("ok").toBool());
    QVERIFY(packs.value("result").toArray().size() >= 5);

    const QJsonObject spec = requestJsonAt(
        port, "POST", "/invoke",
        R"({"target":"profileManager","method":"agentProfileSpec","args":["agent-intermedio"]})");
    QVERIFY(spec.value("ok").toBool());
    QVERIFY(spec.value("result").isObject());

    const QJsonObject summary = requestJsonAt(
        port, "POST", "/invoke",
        R"({"target":"profileManager","method":"harnessSpecSummary","args":["agent-intermedio",""]})");
    QVERIFY(summary.value("ok").toBool());
    const QJsonObject sum = summary.value("result").toObject();
    QVERIFY(sum.value("toolCount").toInt() > 0);
    QVERIFY(sum.value("approxTokens").toInt() > 0);

    // Un preset de sistema no se puede pisar por headless: es inmutable.
    const QJsonObject denied = requestJsonAt(
        port, "POST", "/invoke",
        R"({"target":"profileManager","method":"setAgentProfileSpec","args":["agent-intermedio",{}]})");
    QVERIFY(!denied.value("result").toBool());
}

void ControlApiTests::auxiliarySchedulerIsHeadless()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("LlamaCode"));
    QCoreApplication::setApplicationName(QStringLiteral("LlamaCode"));
    AppController app;
    ControlApi api(&app);
    const quint16 port = freePort();
    QVERIFY(api.start(port));

    const QJsonObject methods = requestJsonAt(port, "GET", "/methods");
    QVERIFY(methods.value(QStringLiteral("targets")).toArray()
                .contains(QJsonValue(QStringLiteral("auxiliaryScheduler"))));

    const QJsonObject queued = requestJsonAt(
        port, "POST", "/invoke",
        R"({"target":"auxiliaryScheduler","method":"enqueue","args":["retrieval","cpu-embed",5,"headless test"]})");
    QVERIFY(queued.value(QStringLiteral("ok")).toBool());
    const QString id = queued.value(QStringLiteral("result")).toString();
    QVERIFY(!id.isEmpty());

    const QJsonObject started = requestJsonAt(
        port, "POST", "/invoke",
        R"({"target":"auxiliaryScheduler","method":"startNextJob","args":[]})");
    QCOMPARE(started.value(QStringLiteral("result")).toString(), id);

    const QJsonObject jobs = requestJsonAt(
        port, "GET", "/prop?target=auxiliaryScheduler&name=jobs");
    const QJsonArray rows = jobs.value(QStringLiteral("value")).toArray();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().toObject().value(QStringLiteral("state")).toString(),
             QStringLiteral("running"));

    const QByteArray completeBody = QStringLiteral(
        R"({"target":"auxiliaryScheduler","method":"complete","args":["%1",true,"done"]})")
        .arg(id).toUtf8();
    const QJsonObject finished = requestJsonAt(port, "POST", "/invoke", completeBody);
    QVERIFY(finished.value(QStringLiteral("ok")).toBool());

    const QJsonObject appJobs = requestJsonAt(port, "GET", "/prop?name=auxiliaryJobs");
    QCOMPARE(appJobs.value(QStringLiteral("value")).toArray().first().toObject()
                 .value(QStringLiteral("state")).toString(), QStringLiteral("completed"));
}

QTEST_MAIN(ControlApiTests)
#include "test_control_api.moc"
