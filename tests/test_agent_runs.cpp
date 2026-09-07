#include <QtTest>
#include <QSaveFile>

#include "core/agent/AgentDeliverableStore.h"
#include "core/agent/AgentRunStore.h"

class AgentRunTests : public QObject
{
    Q_OBJECT
private slots:
    void durableClaimAndTerminalTransition();
    void expiredLeaseBecomesUncertainWithoutReplay();
    void uncertainResolutionIsExplicitAndRendererSafe();
    void deliverablesCaptureAndSaveAsRequireOverwrite();
};

void AgentRunTests::durableClaimAndTerminalTransition()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AgentRunStore store;
    QVERIFY(store.open(dir.path()));

    const QString runId = store.accept(
        QStringLiteral("request-1"), QStringLiteral("session-1"), QStringLiteral("corr-1"),
        dir.path(), QStringLiteral("crear un archivo"), QJsonObject{{"files", QJsonObject{}}});
    QCOMPARE(runId, QStringLiteral("request-1"));
    QCOMPARE(store.accept(QStringLiteral("request-1"), QStringLiteral("session-1"),
                          QStringLiteral("corr-1"), dir.path(), QStringLiteral("crear un archivo"),
                          QJsonObject{{"files", QJsonObject{}}}), runId);

    QString token;
    QVERIFY(store.claim(runId, QStringLiteral("owner-a"), 30000, &token));
    QVERIFY(!token.isEmpty());
    QString error;
    QVERIFY(!store.heartbeat(runId, QStringLiteral("wrong-token"), 30000, &error));
    QVERIFY(store.heartbeat(runId, token, 30000));
    const QJsonObject pending = store.pending().at(0).toObject();
    QVERIFY(!pending.contains(QStringLiteral("leaseToken")));
    QVERIFY(!pending.contains(QStringLiteral("beforeSnapshot")));
    QVERIFY(store.transition(runId, token, QStringLiteral("completed"),
                             QStringLiteral("ok"), QJsonObject{{"quality", "verified"}}));

    const AgentRunRecord record = store.record(runId);
    QCOMPARE(record.status, QStringLiteral("completed"));
    QVERIFY(record.leaseToken.isEmpty());
    QCOMPARE(record.metadata.value(QStringLiteral("quality")).toString(), QStringLiteral("verified"));
    const QJsonArray events = store.events(runId);
    QCOMPARE(events.size(), 3); // accepted, claimed, completed
    QCOMPARE(events.at(0).toObject().value(QStringLiteral("seq")).toInt(), 1);
    QCOMPARE(events.at(2).toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("run.completed"));
    QVERIFY(!store.transition(runId, token, QStringLiteral("failed"), QStringLiteral("late")));
}

void AgentRunTests::expiredLeaseBecomesUncertainWithoutReplay()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AgentRunStore store;
    QVERIFY(store.open(dir.path()));
    const QString runId = store.accept(QStringLiteral("request-2"), QStringLiteral("session"),
                                       QStringLiteral("corr"), dir.path(), QStringLiteral("efecto"), {});
    QString token;
    QVERIFY(store.claim(runId, QStringLiteral("owner"), 1000, &token));
    const AgentRunRecord before = store.record(runId);
    QVERIFY(store.recoverStaleRuns(before.leaseExpiresAt + 1) == 1);
    const AgentRunRecord recovered = store.record(runId);
    QCOMPARE(recovered.status, QStringLiteral("uncertain"));
    QVERIFY(recovered.leaseToken.isEmpty());
    QString retryToken;
    QVERIFY(!store.claim(runId, QStringLiteral("new-owner"), 30000, &retryToken));
    QVERIFY(!store.transition(runId, token, QStringLiteral("completed")));
    QVERIFY(store.pending().size() == 1);
}

void AgentRunTests::uncertainResolutionIsExplicitAndRendererSafe()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AgentRunStore store;
    QVERIFY(store.open(dir.path()));
    const QString runId = store.accept(
        QStringLiteral("request-uncertain"), QStringLiteral("session"), QStringLiteral("corr"),
        dir.path(), QStringLiteral("efecto ambiguo"),
        QJsonObject{{QStringLiteral("files"), QJsonObject{{QStringLiteral("secret.txt"),
                                                            QJsonObject{{QStringLiteral("hash"),
                                                                         QStringLiteral("private")}}}}}});
    QString token;
    QVERIFY(store.claim(runId, QStringLiteral("owner"), 1000, &token));
    const AgentRunRecord before = store.record(runId);
    QCOMPARE(store.recoverStaleRuns(before.leaseExpiresAt + 1), 1);

    QString error;
    QVERIFY(!store.resolveUncertain(runId, QStringLiteral("completed"), QStringLiteral("no"), &error));
    QVERIFY(store.resolveUncertain(runId, QStringLiteral("cancelled"),
                                   QStringLiteral("decisión humana"), &error));
    const AgentRunRecord resolved = store.record(runId);
    QCOMPARE(resolved.status, QStringLiteral("cancelled"));
    QCOMPARE(resolved.detail, QStringLiteral("decisión humana"));
    QVERIFY(resolved.leaseToken.isEmpty());
    QVERIFY(store.pending().isEmpty());
    QVERIFY(store.mergeTerminalMetadata(runId,
                                        QJsonObject{{QStringLiteral("deliverables"),
                                                     QJsonObject{{QStringLiteral("changedCount"), 2}}}},
                                        &error));
    QCOMPARE(store.record(runId).metadata.value(QStringLiteral("deliverables"))
                 .toObject().value(QStringLiteral("changedCount")).toInt(), 2);

    const QJsonArray publicRows = store.all();
    QCOMPARE(publicRows.size(), 1);
    QVERIFY(!publicRows.first().toObject().contains(QStringLiteral("beforeSnapshot")));
    QVERIFY(!publicRows.first().toObject().contains(QStringLiteral("leaseToken")));
    const QJsonArray events = store.events(runId);
    QCOMPARE(events.last().toObject().value(QStringLiteral("kind")).toString(),
             QStringLiteral("run.metadata_merged"));
}

void AgentRunTests::deliverablesCaptureAndSaveAsRequireOverwrite()
{
    QTemporaryDir root;
    QTemporaryDir storage;
    QTemporaryDir destination;
    QVERIFY(root.isValid() && storage.isValid() && destination.isValid());
    qputenv("LLAMACODE_DELIVERABLES_DIR", storage.path().toUtf8());

    const QString source = QDir(root.path()).filePath(QStringLiteral("result.txt"));
    QFile initial(source);
    QVERIFY(initial.open(QIODevice::WriteOnly | QIODevice::Text));
    initial.write("antes");
    initial.close();
    const QJsonObject before = AgentDeliverableStore::snapshot(root.path());
    QVERIFY(!before.isEmpty());

    QVERIFY(initial.open(QIODevice::WriteOnly | QIODevice::Text));
    initial.write("despues");
    initial.close();
    QFile created(QDir(root.path()).filePath(QStringLiteral("nuevo.txt")));
    QVERIFY(created.open(QIODevice::WriteOnly | QIODevice::Text));
    created.write("nuevo");
    created.close();

    QString error;
    const QJsonObject manifest = AgentDeliverableStore::capture(
        QStringLiteral("run-outputs"), root.path(), before, &error);
    QVERIFY2(!manifest.isEmpty(), qPrintable(error));
    QCOMPARE(manifest.value(QStringLiteral("changedCount")).toInt(), 2);
    const QString target = QDir(destination.path()).filePath(QStringLiteral("result.txt"));
    QVERIFY(AgentDeliverableStore::saveAs(QStringLiteral("run-outputs"),
                                          QStringLiteral("result.txt"), target, false, &error));
    QVERIFY(!AgentDeliverableStore::saveAs(QStringLiteral("run-outputs"),
                                           QStringLiteral("result.txt"), target, false, &error));
    QVERIFY(AgentDeliverableStore::saveAs(QStringLiteral("run-outputs"),
                                          QStringLiteral("result.txt"), target, true, &error));
    QFile restored(target);
    QVERIFY(restored.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(restored.readAll(), QByteArray("despues"));

    // Un manifiesto local también se valida antes de leerlo: una ruta alterada
    // no puede hacer que Save As copie fuera del directorio de la corrida.
    QJsonObject tampered = AgentDeliverableStore::manifest(QStringLiteral("run-outputs"));
    QJsonArray entries = tampered.value(QStringLiteral("entries")).toArray();
    for (int i = 0; i < entries.size(); ++i) {
        QJsonObject entry = entries.at(i).toObject();
        if (entry.value(QStringLiteral("path")).toString() != QStringLiteral("result.txt")) continue;
        entry[QStringLiteral("storedPath")] = QStringLiteral("../outside.txt");
        entries[i] = entry;
    }
    tampered[QStringLiteral("entries")] = entries;
    QSaveFile manifestFile(QDir(storage.path()).filePath(QStringLiteral("run-outputs/manifest.json")));
    QVERIFY(manifestFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(manifestFile.write(QJsonDocument(tampered).toJson()) > 0);
    QVERIFY(manifestFile.commit());
    QVERIFY(!AgentDeliverableStore::saveAs(QStringLiteral("run-outputs"),
                                           QStringLiteral("result.txt"),
                                           QDir(destination.path()).filePath(QStringLiteral("escape.txt")),
                                           true, &error));
    qunsetenv("LLAMACODE_DELIVERABLES_DIR");
}

QTEST_MAIN(AgentRunTests)
#include "test_agent_runs.moc"
