#include <QtTest>

#include "core/tasks/EngineeringWorkflowCatalog.h"
#include "core/tasks/WorkflowEngine.h"

class EngineeringWorkflowTests : public QObject
{
    Q_OBJECT
private slots:
    void catalogHasCoreWorkflows();
    void definitionsValidate();
    void installableTaskIsRunnable();
    void safetyProfilesAreExplicit();
};

void EngineeringWorkflowTests::catalogHasCoreWorkflows()
{
    const QVariantList all = EngineeringWorkflowCatalog::workflows();
    QCOMPARE(all.size(), 6);
    for (const QString &id : {QStringLiteral("investigate"), QStringLiteral("qa"),
                              QStringLiteral("document-audit"), QStringLiteral("review"),
                              QStringLiteral("autoprompt"),
                              QStringLiteral("release-check")}) {
        QVERIFY(EngineeringWorkflowCatalog::isKnownWorkflow(id));
        QVERIFY(!EngineeringWorkflowCatalog::workflow(id).value(QStringLiteral("steps"))
                     .toMap().isEmpty());
    }
}

void EngineeringWorkflowTests::definitionsValidate()
{
    for (const QVariant &value : EngineeringWorkflowCatalog::workflows()) {
        const QVariantMap wf = value.toMap();
        const QString error = WorkflowEngine::validate(
            QJsonObject::fromVariantMap(wf));
        QVERIFY2(error.isEmpty(), qPrintable(wf.value(QStringLiteral("id")).toString()
                                               + QStringLiteral(": ") + error));
    }
}

void EngineeringWorkflowTests::installableTaskIsRunnable()
{
    const QVariantMap task = EngineeringWorkflowCatalog::installableTask(
        QStringLiteral("qa"));
    QVERIFY(!task.isEmpty());
    QCOMPARE(task.value(QStringLiteral("approvalPolicy")).toString(),
             QStringLiteral("sensitive"));
    QCOMPARE(task.value(QStringLiteral("safetyProfile")).toString(),
             QStringLiteral("normal"));
    QCOMPARE(task.value(QStringLiteral("workflow")).toMap().value(QStringLiteral("id"))
                 .toString(), QStringLiteral("qa"));
    QVERIFY(EngineeringWorkflowCatalog::installableTask(QStringLiteral("missing")).isEmpty());

    const QVariantMap autoprompt = EngineeringWorkflowCatalog::installableTask(
        QStringLiteral("autoprompt"));
    QVERIFY(!autoprompt.isEmpty());
    const QVariantMap budget = autoprompt.value(QStringLiteral("workflow")).toMap()
                                   .value(QStringLiteral("budget")).toMap();
    QCOMPARE(budget.value(QStringLiteral("maxRepairs")).toInt(), 3);
    const QVariantMap review = autoprompt.value(QStringLiteral("workflow")).toMap()
                                   .value(QStringLiteral("steps")).toMap()
                                   .value(QStringLiteral("review_verify")).toMap();
    QCOMPARE(review.value(QStringLiteral("type")).toString(), QStringLiteral("parallel"));
    const QVariantList branches = review.value(QStringLiteral("branches")).toList();
    QCOMPARE(branches.size(), 2);
    QVERIFY(branches.at(0).toMap().value(QStringLiteral("readOnly")).toBool());
}

void EngineeringWorkflowTests::safetyProfilesAreExplicit()
{
    const QVariantList profiles = EngineeringWorkflowCatalog::safetyProfiles();
    QCOMPARE(profiles.size(), 4);
    for (const QVariant &value : profiles) {
        const QVariantMap profile = value.toMap();
        QVERIFY(!profile.value(QStringLiteral("id")).toString().isEmpty());
        QVERIFY(!profile.value(QStringLiteral("approvalPolicy")).toString().isEmpty());
        QVERIFY(!profile.value(QStringLiteral("permScope")).toString().isEmpty());
    }
}

QTEST_MAIN(EngineeringWorkflowTests)
#include "test_engineering_workflows.moc"
