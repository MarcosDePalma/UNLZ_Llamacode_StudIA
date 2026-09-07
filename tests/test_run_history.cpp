// Unit tests de RunHistoryStore: (de)serialización JSON roundtrip, append +
// persistencia con aislamiento de disco (QStandardPaths test mode), orden
// más-nuevo-primero, cap por owner, y aislamiento entre owners distintos.

#include <QtTest>
#include "core/tasks/RunHistoryStore.h"

class RunHistoryTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void jsonRoundTrip();
    void append_persistsAndOrdersNewestFirst();
    void append_capsPerOwner();
    void owners_areIsolated();
    void clear_removesHistory();
};

void RunHistoryTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void RunHistoryTests::init()
{
    RunHistoryStore s;
    s.clear(QStringLiteral("proc-a"));
    s.clear(QStringLiteral("proc-b"));
}

void RunHistoryTests::jsonRoundTrip()
{
    const QVariantMap in{
        {"runId", "r1"}, {"ownerId", "proc-a"}, {"startedAt", "2024-01-01T09:00:00"},
        {"finishedAt", "2024-01-01T09:05:00"}, {"status", "ok"}, {"summary", "listo"},
        {"source", "manual"}, {"automationId", ""}, {"correlationId", "corr-1"},
        {"log", "traza completa\nlinea 2"},
        {"metadata", QVariantMap{{"runtime", "codex"}, {"manifestPath", "run/manifest.json"}}},
        {"receipts", QVariantList{QVariantMap{{"status", "executed"}, {"tool", "create_issue"}}}},
        {"report", QVariantList{
            QVariantMap{{"n", 1}, {"tool", "desktop_launch"}, {"ok", true}, {"summary", "ok"}},
            QVariantMap{{"n", 2}, {"tool", "desktop_assert"}, {"ok", false}, {"summary", "FAIL"}}}}
    };
    const QVariantMap out = RunHistoryStore::fromJson(RunHistoryStore::toJson(in));
    QCOMPARE(out.value("runId").toString(), QStringLiteral("r1"));
    QCOMPARE(out.value("status").toString(), QStringLiteral("ok"));
    QCOMPARE(out.value("log").toString(), QStringLiteral("traza completa\nlinea 2"));
    QCOMPARE(out.value("correlationId").toString(), QStringLiteral("corr-1"));
    QCOMPARE(out.value("metadata").toMap().value("runtime").toString(), QStringLiteral("codex"));
    QCOMPARE(out.value("receipts").toList().first().toMap().value("tool").toString(),
             QStringLiteral("create_issue"));
    // El run-report por paso sobrevive el round-trip.
    const QVariantList rep = out.value("report").toList();
    QCOMPARE(rep.size(), 2);
    QCOMPARE(rep.at(1).toMap().value("tool").toString(), QStringLiteral("desktop_assert"));
    QCOMPARE(rep.at(1).toMap().value("ok").toBool(), false);
}

void RunHistoryTests::append_persistsAndOrdersNewestFirst()
{
    {
        RunHistoryStore s;
        s.append(QStringLiteral("proc-a"), {{"runId", "old"}, {"status", "ok"}, {"log", "1"}});
        s.append(QStringLiteral("proc-a"), {{"runId", "new"}, {"status", "error"}, {"log", "2"}});
    }
    RunHistoryStore s2;   // instancia nueva → lee de disco
    const QVariantList h = s2.history(QStringLiteral("proc-a"));
    QCOMPARE(h.size(), 2);
    QCOMPARE(h.first().toMap().value("runId").toString(), QStringLiteral("new"));
    QCOMPARE(h.last().toMap().value("runId").toString(), QStringLiteral("old"));
    // startedAt se autocompleta si falta.
    QVERIFY(!h.first().toMap().value("startedAt").toString().isEmpty());
}

void RunHistoryTests::append_capsPerOwner()
{
    RunHistoryStore s;
    for (int i = 0; i < RunHistoryStore::kMaxPerOwner + 10; ++i)
        s.append(QStringLiteral("proc-a"), {{"runId", QString::number(i)}, {"status", "ok"}});
    const QVariantList h = s.history(QStringLiteral("proc-a"));
    QCOMPARE(h.size(), RunHistoryStore::kMaxPerOwner);
    // El más nuevo es el último agregado; los más viejos se descartaron.
    const int newest = RunHistoryStore::kMaxPerOwner + 10 - 1;
    QCOMPARE(h.first().toMap().value("runId").toString(), QString::number(newest));
}

void RunHistoryTests::owners_areIsolated()
{
    RunHistoryStore s;
    s.append(QStringLiteral("proc-a"), {{"runId", "a"}, {"status", "ok"}});
    s.append(QStringLiteral("proc-b"), {{"runId", "b"}, {"status", "ok"}});
    QCOMPARE(s.history(QStringLiteral("proc-a")).size(), 1);
    QCOMPARE(s.history(QStringLiteral("proc-b")).size(), 1);
    QCOMPARE(s.history(QStringLiteral("proc-a")).first().toMap().value("runId").toString(),
             QStringLiteral("a"));
}

void RunHistoryTests::clear_removesHistory()
{
    RunHistoryStore s;
    s.append(QStringLiteral("proc-a"), {{"runId", "a"}, {"status", "ok"}});
    QCOMPARE(s.history(QStringLiteral("proc-a")).size(), 1);
    s.clear(QStringLiteral("proc-a"));
    QCOMPARE(s.history(QStringLiteral("proc-a")).size(), 0);
}

QTEST_MAIN(RunHistoryTests)
#include "test_run_history.moc"
