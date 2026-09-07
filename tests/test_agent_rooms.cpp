#include <QtTest>

#include "core/agent/AgentRoomStore.h"

#include <QTemporaryDir>

class AgentRoomStoreTests : public QObject
{
    Q_OBJECT

private slots:
    void persistsRoomTimelineAndParticipants();
    void grantsCanOnlyNarrow();
    void presetsAreExecutableContracts();
    void compactContextHonorsAudienceAndBudget();
};

void AgentRoomStoreTests::persistsRoomTimelineAndParticipants()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString roomId;
    {
        AgentRoomStore store(dir.path());
        roomId = store.createRoom(QStringLiteral("Revisión"), QStringLiteral("C:/repo"));
        QVERIFY(!roomId.isEmpty());
        QCOMPARE(store.rooms().size(), 1);
        QCOMPARE(store.participants(roomId).size(), 2);
        QVERIFY(!store.postEvent(roomId, {{"type", "message"}, {"author", "human:owner"},
                                         {"content", "hola"}}).isEmpty());
    }
    AgentRoomStore restored(dir.path());
    QCOMPARE(restored.currentRoomId(), roomId);
    QCOMPARE(restored.room(roomId).value("title").toString(), QStringLiteral("Revisión"));
    const QVariantList rows = restored.events(roomId);
    QCOMPARE(rows.size(), 2); // room_created + message
    QCOMPARE(rows.last().toMap().value("content").toString(), QStringLiteral("hola"));
}

void AgentRoomStoreTests::grantsCanOnlyNarrow()
{
    QTemporaryDir dir;
    AgentRoomStore store(dir.path());
    const QString roomId = store.createRoom(QStringLiteral("Permisos"));
    QVariantMap p{{"id", "agent:reviewer"}, {"name", "Revisor"}, {"kind", "agent"},
                  {"grant", QVariantMap{{"read", true}, {"write", false}, {"shell", true}}}};
    QVERIFY(store.upsertParticipant(roomId, p));
    QVERIFY(store.updateGrant(roomId, "agent:reviewer",
                              {{"read", true}, {"write", false}, {"shell", false}}));
    QVERIFY(!store.updateGrant(roomId, "agent:reviewer",
                               {{"read", true}, {"write", true}, {"shell", false}}));
    const QVariantMap grant = store.participants(roomId).last().toMap().value("grant").toMap();
    QVERIFY(!grant.value("write").toBool());
    QVERIFY(!grant.value("shell").toBool());
}

void AgentRoomStoreTests::presetsAreExecutableContracts()
{
    QTemporaryDir dir;
    AgentRoomStore store(dir.path());
    for (const QString &name : {"review", "autoprompt", "council", "research"}) {
        const QVariantMap p = store.preset(name, QStringLiteral("objetivo"));
        QVERIFY2(!p.contains("error"), qPrintable(name));
        QCOMPARE(p.value("name").toString(), name);
        QVERIFY(p.value("participants").toList().size() >= 2);
        QVERIFY(!p.value("instructions").toString().isEmpty());
        if (name == QStringLiteral("autoprompt")) {
            const QVariantList participants = p.value("participants").toList();
            const QVariantMap reviewer = participants.at(1).toMap();
            const QVariantMap verifier = participants.at(2).toMap();
            QVERIFY(!reviewer.value("grant").toMap().value("write").toBool());
            QVERIFY(verifier.value("grant").toMap().value("shell").toBool());
        }
    }
    QVERIFY(store.preset("unknown", "x").contains("error"));
}

void AgentRoomStoreTests::compactContextHonorsAudienceAndBudget()
{
    QTemporaryDir dir;
    AgentRoomStore store(dir.path());
    const QString roomId = store.createRoom(QStringLiteral("Contexto"));
    store.postEvent(roomId, {{"author", "agent:a"}, {"content", "visible"},
                             {"audience", QStringList{"agent:coordinator"}}});
    store.postEvent(roomId, {{"author", "agent:b"}, {"content", "secreto"},
                             {"audience", QStringList{"agent:other"}}});
    const QString context = store.compactContext(roomId, "agent:coordinator", 1000);
    QVERIFY(context.contains("visible"));
    QVERIFY(!context.contains("secreto"));
    QVERIFY(context.size() <= 1000);
}

QTEST_MAIN(AgentRoomStoreTests)
#include "test_agent_rooms.moc"
