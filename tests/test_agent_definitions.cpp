#include <QtTest>

#include "core/agents/AgentDefinitionStore.h"
#include "core/agents/TriggerManager.h"

class AgentDefinitionTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void revisionsDiffAndRestore();
    void feedbackRequiresApproval();
    void persistsAndDuplicates();
    void metricsAggregateLinkedTasks();
    void triggersMatchDispatchAndDebounce();
};

void AgentDefinitionTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void AgentDefinitionTests::init()
{
    AgentDefinitionStore agents;
    for (const QVariant &value : agents.all())
        agents.remove(value.toMap().value(QStringLiteral("id")).toString());
    TriggerManager triggers;
    for (const QVariant &value : triggers.all())
        triggers.remove(value.toMap().value(QStringLiteral("id")).toString());
}

void AgentDefinitionTests::revisionsDiffAndRestore()
{
    AgentDefinitionStore store;
    const QString id = store.save({}, {{"name", "Investigador"},
                                       {"instructions", "Citar fuentes"},
                                       {"taskIds", QStringList{"research"}}});
    QVERIFY(!id.isEmpty());
    QCOMPARE(store.get(id).value("currentRevision").toInt(), 1);
    QVERIFY(!store.save(id, {{"name", "Investigador"},
                              {"instructions", "Citar fuentes primarias"},
                              {"taskIds", QStringList{"research"}}}, "Más precisión").isEmpty());
    QCOMPARE(store.revisions(id).size(), 2);
    const QVariantList changes = store.revisionDiff(id, 1, 2).value("changes").toList();
    QCOMPARE(changes.size(), 1);
    QCOMPARE(changes.first().toMap().value("field").toString(), QStringLiteral("instructions"));
    QVERIFY(store.restoreRevision(id, 1));
    QCOMPARE(store.get(id).value("instructions").toString(), QStringLiteral("Citar fuentes"));
    QCOMPARE(store.get(id).value("currentRevision").toInt(), 3);
}

void AgentDefinitionTests::feedbackRequiresApproval()
{
    AgentDefinitionStore store;
    const QString id = store.save({}, {{"name", "Coder"}, {"instructions", "Validar"}});
    const QString proposal = store.proposeFeedback(id, QStringLiteral("Agregar prueba de regresión"));
    QVERIFY(!proposal.isEmpty());
    QCOMPARE(store.get(id).value("currentRevision").toInt(), 1);
    QCOMPARE(store.pendingFeedback(id).size(), 1);
    QVERIFY(store.approveFeedback(proposal));
    QCOMPARE(store.get(id).value("currentRevision").toInt(), 2);
    QVERIFY(store.get(id).value("instructions").toString().contains("Agregar prueba"));
    QVERIFY(store.pendingFeedback(id).isEmpty());

    const QString rejected = store.proposeFeedback(id, QStringLiteral("Desactivar permisos"));
    QVERIFY(store.rejectFeedback(rejected));
    QCOMPARE(store.get(id).value("currentRevision").toInt(), 2);
}

void AgentDefinitionTests::persistsAndDuplicates()
{
    QString id;
    {
        AgentDefinitionStore store;
        id = store.save({}, {{"name", "Persistente"}, {"profileId", "agent-intermedio"}});
    }
    AgentDefinitionStore restored;
    QCOMPARE(restored.get(id).value("profileId").toString(), QStringLiteral("agent-intermedio"));
    const QString copy = restored.duplicate(id);
    QVERIFY(!copy.isEmpty());
    QVERIFY(copy != id);
    QCOMPARE(restored.get(copy).value("currentRevision").toInt(), 1);
}

void AgentDefinitionTests::metricsAggregateLinkedTasks()
{
    AgentDefinitionStore store;
    const QString id = store.save({}, {{"name", "Medible"},
                                       {"taskIds", QStringList{"a", "b"}}});
    const auto provider = [](const QString &owner) {
        if (owner == QLatin1String("a"))
            return QVariantList{
                QVariantMap{{"runId", "r1"}, {"status", "ok"},
                            {"metrics", QVariantMap{{"promptTokens", 10},
                                                    {"generatedTokens", 5},
                                                    {"wallMs", 100}}}},
                QVariantMap{{"runId", "r2"}, {"status", "error"},
                            {"metrics", QVariantMap{{"promptTokens", 7}}}}};
        return QVariantList{
            QVariantMap{{"runId", "r3"}, {"status", "ok"},
                        {"metrics", QVariantMap{{"promptTokens", 3},
                                                {"generatedTokens", 2},
                                                {"wallMs", 50}}}}};
    };
    const QVariantMap metrics = store.aggregateMetrics(id, provider);
    QCOMPARE(metrics.value("runs").toInt(), 3);
    QCOMPARE(metrics.value("successes").toInt(), 2);
    QCOMPARE(metrics.value("promptTokens").toDouble(), 20.0);
    QCOMPARE(metrics.value("generatedTokens").toDouble(), 7.0);
}

void AgentDefinitionTests::triggersMatchDispatchAndDebounce()
{
    TriggerManager triggers;
    QSignalSpy spy(&triggers, &TriggerManager::taskRequested);
    const QString id = triggers.save({}, {
        {"name", "Issue nuevo"}, {"agentId", "agent-1"}, {"taskId", "triage"},
        {"type", "appEvent"}, {"debounceMs", 10000},
        {"config", QVariantMap{{"name", "github.issue.created"},
                               {"filters", QVariantMap{{"repo", "llamacode"}}}}}
    });
    QVERIFY(!id.isEmpty());
    QVERIFY(triggers.dispatchEvent("appEvent",
        {{"name", "github.issue.created"},
         {"payload", QVariantMap{{"repo", "otro"}}}}).isEmpty());
    QCOMPARE(triggers.dispatchEvent("appEvent",
        {{"name", "github.issue.created"},
         {"payload", QVariantMap{{"repo", "llamacode"}}}}).size(), 1);
    QCOMPARE(spy.size(), 1);
    QVERIFY(triggers.dispatchEvent("appEvent",
        {{"name", "github.issue.created"},
         {"payload", QVariantMap{{"repo", "llamacode"}}}}).isEmpty());
}

QTEST_MAIN(AgentDefinitionTests)
#include "test_agent_definitions.moc"
