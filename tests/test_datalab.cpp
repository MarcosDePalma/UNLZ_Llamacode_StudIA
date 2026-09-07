#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "core/data/DataLab.h"
#include "test_datalab.h"

class DataLabFakeLlm : public QTcpServer
{
public:
    enum class Mode { Valid, RetryThenValid, InvalidThenValid };

    explicit DataLabFakeLlm(Mode mode = Mode::Valid, QObject *parent = nullptr)
        : QTcpServer(parent), m_mode(mode)
    {
        connect(this, &QTcpServer::newConnection, this, [this]() {
            QTcpSocket *socket = nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
                socket->readAll();
                const int request = ++m_requests;
                QByteArray status = "HTTP/1.1 200 OK";
                QByteArray body = R"({"choices":[{"message":{"content":"{\"cliente\":\"ACME\"}"}}]})";
                if (m_mode == Mode::RetryThenValid && request == 1) {
                    status = "HTTP/1.1 500 Internal Server Error";
                    body = R"({"error":"temporary"})";
                } else if (m_mode == Mode::InvalidThenValid && request == 1) {
                    body = R"({"choices":[{"message":{"content":"no es json"}}]})";
                }
                const QByteArray response = status + "\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                socket->write(response);
                socket->disconnectFromHost();
            });
        });
    }

    int requests() const { return m_requests; }

private:
    Mode m_mode;
    int m_requests = 0;
};

void DataLabTests::initTestCase() { QStandardPaths::setTestModeEnabled(true); }

void DataLabTests::schemaValidation()
{
    QVERIFY(!DataLabStore::validateSchema({}).isEmpty());
    const QVariantMap schema{{"fields", QVariantMap{
        {"name", QVariantMap{{"type", "string"}, {"required", true}}},
        {"amount", QVariantMap{{"type", "number"}}}
    }}};
    QVERIFY(DataLabStore::validateSchema(schema).isEmpty());
}

void DataLabTests::recordNormalizationAndValidation()
{
    const QVariantMap schema{{"fields", QVariantMap{
        {"name", QVariantMap{{"type", "string"}, {"required", true}}},
        {"amount", QVariantMap{{"type", "number"}, {"required", true}}},
        {"date", QVariantMap{{"type", "date"}}}
    }}};
    const QVariantMap result = DataLabStore::validateRecordAgainstSchema(
        schema, {{"name", "  ACME  "}, {"amount", "$1.234,50"}, {"date", "2026-08-16"}});
    QCOMPARE(result.value("status").toString(), QStringLiteral("valid"));
    QCOMPARE(result.value("record").toMap().value("name").toString(), QStringLiteral("ACME"));
    QCOMPARE(result.value("record").toMap().value("amount").toDouble(), 1234.50);

    const QVariantMap invalid = DataLabStore::validateRecordAgainstSchema(schema, {{"amount", 2}});
    QCOMPARE(invalid.value("status").toString(), QStringLiteral("needs_review"));
    QVERIFY(!invalid.value("errors").toList().isEmpty());
}

void DataLabTests::jobExtractionAndPersistence()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile source(dir.filePath("invoice.txt"));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("cliente: ACME\nimporte: 10\n");
    source.close();

    const QVariantMap schema{{"fields", QVariantMap{
        {"cliente", QVariantMap{{"type", "string"}}}
    }}};
    const QString schemaJson = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(schema)).toJson());
    DataLabStore store;
    const QString id = store.createJob("test", schemaJson, {source.fileName()});
    QVERIFY(!id.isEmpty());
    const QVariantMap processed = store.processJob(id);
    QVERIFY(processed.value("ok").toBool());
    QCOMPARE(processed.value("extracted").toInt(), 1);
    const QVariantList documents = store.job(id).value("documents").toList();
    QCOMPARE(documents.size(), 1);
    QVERIFY(documents.first().toMap().value("text").toString().contains("ACME"));
    QVERIFY(!store.extractionPrompt(id, documents.first().toMap().value("id").toString()).isEmpty());
    const QString documentId = documents.first().toMap().value("id").toString();
    QCOMPARE(store.validateRecord(id, documentId, R"({"cliente":"ACME"})").value("status").toString(), QStringLiteral("valid"));
    const QVariantMap many = store.validateRecords(id, documentId,
        R"([{"cliente":"ACME"},{"cliente":"  BETA  "}])");
    QCOMPARE(many.value("status").toString(), QStringLiteral("valid"));
    QCOMPARE(many.value("records").toList().size(), 2);
    QCOMPARE(many.value("records").toList().at(1).toMap().value("cliente").toString(), QStringLiteral("BETA"));
    const QString dbPath = dir.filePath("out.sqlite");
    QCOMPARE(store.exportJob(id, dbPath, QStringLiteral("sqlite")), QFileInfo(dbPath).absoluteFilePath());
    const QString connection = QStringLiteral("verify_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    db.setDatabaseName(dbPath);
    QVERIFY(db.open());
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT cliente FROM records")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("ACME"));
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection);
    QVERIFY(store.deleteJob(id));
}

void DataLabTests::modelResponseParsing()
{
    const QVariantMap plain = DataLabStore::parseModelResponse(R"({"name":"ACME"})");
    QVERIFY(plain.value("ok").toBool());
    QCOMPARE(plain.value("record").toMap().value("name").toString(), QStringLiteral("ACME"));
    const QVariantMap fenced = DataLabStore::parseModelResponse("texto\n```json\n{\"name\":\"ACME\"}\n```\n");
    QVERIFY(fenced.value("ok").toBool());
    const QVariantMap invalid = DataLabStore::parseModelResponse("no hay json");
    QVERIFY(!invalid.value("ok").toBool());
    const QVariantMap array = DataLabStore::parseModelResponse("[{\"name\":\"A\"},{\"name\":\"B\"}]");
    QVERIFY(array.value("ok").toBool());
    QCOMPARE(array.value("records").toList().size(), 2);
}

void DataLabTests::routingAndArbitration()
{
    QCOMPARE(DataLabStore::routeStage({{"text", "breve"}, {"textChars", 5}}), QStringLiteral("DATA-FAST"));
    QCOMPARE(DataLabStore::routeStage({{"text", QString(130000, 'x')}, {"textChars", 130000}}), QStringLiteral("DATA-QUALITY"));
    const QVariantMap schema{{"fields", QVariantMap{{"name", QVariantMap{{"type", "string"}, {"required", true}}}}}};
    const QVariantMap same = DataLabStore::arbitrateCandidates(schema, {{"name", "ACME"}}, {{"name", "ACME"}});
    QCOMPARE(same.value("status").toString(), QStringLiteral("valid"));
    QVERIFY(same.value("agreement").toBool());
    const QVariantMap conflict = DataLabStore::arbitrateCandidates(schema, {{"name", "A"}}, {{"name", "B"}});
    QCOMPARE(conflict.value("status").toString(), QStringLiteral("needs_review"));
    const QVariantMap bounds{{"fields", QVariantMap{{"name", QVariantMap{{"type", "string"}, {"minLength", 3}}}}}};
    QCOMPARE(DataLabStore::validateRecordAgainstSchema(bounds, {{"name", "x"}}).value("status").toString(), QStringLiteral("needs_review"));
}

void DataLabTests::headlessFakeLlmExtraction()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile source(dir.filePath("doc.txt"));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("cliente: ACME");
    source.close();
    DataLabFakeLlm server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    const QVariantMap schema{{"fields", QVariantMap{
        {"cliente", QVariantMap{{"type", "string"}, {"required", true}}}
    }}};
    const QString schemaJson = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(schema)).toJson());
    DataLabStore store;
    const QString jobId = store.createJob("fake", schemaJson, {source.fileName()});
    QVERIFY(!jobId.isEmpty());
    QVERIFY(store.processJob(jobId).value("ok").toBool());
    const QString documentId = store.job(jobId).value("documents").toList().first().toMap().value("id").toString();
    QSignalSpy finished(&store, &DataLabStore::extractionFinished);
    store.runExtraction(jobId, documentId,
                        QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()),
                        QStringLiteral("fake"));
    QVERIFY(finished.wait(3000));
    QCOMPARE(finished.at(0).at(2).toBool(), true);
    QCOMPARE(store.job(jobId).value("documents").toList().first().toMap().value("status").toString(), QStringLiteral("valid"));
    const QVariantMap savedDoc = store.job(jobId).value("documents").toList().first().toMap();
    QVERIFY(savedDoc.value("validation").toMap().value("evidence").toMap()
                .value("cliente").toMap().value("matched").toBool());
    QVERIFY(store.deleteJob(jobId));
}

void DataLabTests::headlessRetryAndRepair()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile source(dir.filePath("doc.txt"));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("cliente: ACME");
    source.close();
    const QVariantMap schema{{"fields", QVariantMap{
        {"cliente", QVariantMap{{"type", "string"}, {"required", true}}}
    }}};
    const QString schemaJson = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(schema)).toJson());

    for (const DataLabFakeLlm::Mode mode : {DataLabFakeLlm::Mode::RetryThenValid,
                                            DataLabFakeLlm::Mode::InvalidThenValid}) {
        DataLabFakeLlm server(mode);
        QVERIFY(server.listen(QHostAddress::LocalHost));
        DataLabStore store;
        const QString jobId = store.createJob("sequence", schemaJson, {source.fileName()});
        QVERIFY(store.processJob(jobId).value("ok").toBool());
        const QString documentId = store.job(jobId).value("documents").toList().first().toMap().value("id").toString();
        QSignalSpy finished(&store, &DataLabStore::extractionFinished);
        store.runExtraction(jobId, documentId,
                            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()), "fake");
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.at(0).at(2).toBool(), true);
        QCOMPARE(server.requests(), 2);
        QCOMPARE(store.job(jobId).value("documents").toList().first().toMap().value("status").toString(), QStringLiteral("valid"));
        QVERIFY(store.deleteJob(jobId));
    }
}

void DataLabTests::benchmarkScoring()
{
    DataLabStore store;
    const QVariantMap score = store.scoreBenchmark(
        R"([{"name":"A","amount":10},{"name":"B","amount":20}])",
        R"([{"name":"A","amount":10},{"name":"B","amount":99}])");
    QVERIFY(score.value("ok").toBool());
    QCOMPARE(score.value("rowsExact").toInt(), 1);
    QCOMPARE(score.value("fieldsCorrect").toInt(), 3);
    QCOMPARE(score.value("fieldsTotal").toInt(), 4);
    QVERIFY(!store.scoreBenchmark("bad", "[]").value("ok").toBool());
}

QTEST_MAIN(DataLabTests)
