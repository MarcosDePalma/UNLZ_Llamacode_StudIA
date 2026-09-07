#include <QtTest>
#include <QDateTime>
#include <QJsonArray>
#include <QTemporaryDir>

#include "core/agent/WorkRegistry.h"

class WorkRegistryTests : public QObject
{
    Q_OBJECT
private slots:
    void acquireAddPathsAndRelease();
    void conflictsMatchFilesAndDirectories();
    void claimPathsIsAtomicAgainstOtherSessions();
    void expiredClaimsDoNotBlockWork();
};

void WorkRegistryTests::acquireAddPathsAndRelease()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString id = WorkRegistry::acquire(
        dir.path(), QStringLiteral("session-a"), QStringLiteral("agent-a"),
        QStringLiteral("Implementar el módulo de contexto"),
        {QStringLiteral("src/context.cpp")});
    QVERIFY(!id.isEmpty());
    QCOMPARE(WorkRegistry::active(dir.path()).size(), 1);
    QVERIFY(WorkRegistry::formatActive(dir.path()).contains(QStringLiteral("contexto")));

    QVERIFY(WorkRegistry::addPaths(dir.path(), id, QStringLiteral("session-a"),
                                   {QStringLiteral("tests/test_context.cpp")}));
    const QJsonArray rows = WorkRegistry::active(dir.path());
    QCOMPARE(rows.size(), 1);
    const QJsonArray paths = rows.first().toObject().value(QStringLiteral("paths")).toArray();
    QCOMPARE(paths.size(), 2);
    QVERIFY(WorkRegistry::heartbeat(dir.path(), id, QStringLiteral("session-a")));

    QVERIFY(WorkRegistry::release(dir.path(), id, QStringLiteral("session-a")));
    QVERIFY(WorkRegistry::active(dir.path()).isEmpty());
}

void WorkRegistryTests::conflictsMatchFilesAndDirectories()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString id = WorkRegistry::acquire(
        dir.path(), QStringLiteral("session-a"), QStringLiteral("agent-a"),
        QStringLiteral("Trabajar en el módulo"), {QStringLiteral("src/agent")});
    QVERIFY(!id.isEmpty());

    QVERIFY(!WorkRegistry::conflicts(dir.path(), QStringLiteral("session-b"),
                                     {QStringLiteral("src/agent/Memory.cpp")}).isEmpty());
    QVERIFY(WorkRegistry::conflicts(dir.path(), QStringLiteral("session-b"),
                                    {QStringLiteral("docs/README.md")}).isEmpty());
    QVERIFY(WorkRegistry::conflicts(dir.path(), QStringLiteral("session-a"),
                                    {QStringLiteral("src/agent/Memory.cpp")}).isEmpty());
    QVERIFY(WorkRegistry::formatConflicts(
                dir.path(), QStringLiteral("session-b"), {QStringLiteral("src/agent/Memory.cpp")})
                .contains(QStringLiteral("work_conflict")));
}

void WorkRegistryTests::claimPathsIsAtomicAgainstOtherSessions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString first = WorkRegistry::acquire(
        dir.path(), QStringLiteral("session-a"), QStringLiteral("agent-a"),
        QStringLiteral("primer trabajo"));
    const QString second = WorkRegistry::acquire(
        dir.path(), QStringLiteral("session-b"), QStringLiteral("agent-b"),
        QStringLiteral("segundo trabajo"));
    QVERIFY(!first.isEmpty());
    QVERIFY(!second.isEmpty());

    QString conflict;
    QVERIFY(WorkRegistry::claimPaths(dir.path(), first, QStringLiteral("session-a"),
                                     {QStringLiteral("src/shared.cpp")}, &conflict));
    QVERIFY(!WorkRegistry::claimPaths(dir.path(), second, QStringLiteral("session-b"),
                                      {QStringLiteral("src/shared.cpp")}, &conflict));
    QVERIFY(conflict.contains(QStringLiteral("work_conflict")));
    QCOMPARE(WorkRegistry::active(dir.path()).at(1).toObject()
                 .value(QStringLiteral("paths")).toArray().size(), 0);
}

void WorkRegistryTests::expiredClaimsDoNotBlockWork()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(!WorkRegistry::acquire(
                 dir.path(), QStringLiteral("session-a"), QStringLiteral("agent-a"),
                 QStringLiteral("Trabajo con TTL"), {QStringLiteral("src/a.cpp")})
                 .isEmpty());

    const qint64 future = QDateTime::currentMSecsSinceEpoch() + 31 * 60 * 1000;
    QVERIFY(WorkRegistry::active(dir.path(), QString(), future).isEmpty());
    QVERIFY(WorkRegistry::conflicts(dir.path(), QStringLiteral("session-b"),
                                    {QStringLiteral("src/a.cpp")}, future).isEmpty());
}

QTEST_MAIN(WorkRegistryTests)
#include "test_work_registry.moc"
