#include <QtTest>

#include "core/tasks/TaskSecurityPolicy.h"

class TaskSecurityPolicyTests : public QObject
{
    Q_OBJECT
private slots:
    void strictProfilesRequireApproval();
    void autonomousOnlyWorksForNormalProfile();
    void productionDisablesExternalToolFamilies();
    void nonProductionDoesNotDisableTools();
};

void TaskSecurityPolicyTests::strictProfilesRequireApproval()
{
    QVERIFY(!TaskSecurityPolicy::isStrictProfile(QStringLiteral("normal")));
    for (const QString &profile : {QStringLiteral("investigation"),
                                   QStringLiteral("guarded"),
                                   QStringLiteral("production")})
        QVERIFY(TaskSecurityPolicy::isStrictProfile(profile));
}

void TaskSecurityPolicyTests::autonomousOnlyWorksForNormalProfile()
{
    QVariantMap task{{QStringLiteral("approvalPolicy"), QStringLiteral("autonomous")}};
    QVERIFY(TaskSecurityPolicy::autoApproveAllowed(task));
    for (const QString &profile : {QStringLiteral("investigation"),
                                   QStringLiteral("guarded"),
                                   QStringLiteral("production")}) {
        task[QStringLiteral("safetyProfile")] = profile;
        QVERIFY(!TaskSecurityPolicy::autoApproveAllowed(task));
    }
    task[QStringLiteral("approvalPolicy")] = QStringLiteral("sensitive");
    task[QStringLiteral("safetyProfile")] = QStringLiteral("normal");
    QVERIFY(!TaskSecurityPolicy::autoApproveAllowed(task));
}

void TaskSecurityPolicyTests::productionDisablesExternalToolFamilies()
{
    const QVariantList catalog{
        QVariantMap{{QStringLiteral("name"), QStringLiteral("browser_open")}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("web_search")}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("email_send")}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("run_shell")}},
        QVariantMap{{QStringLiteral("name"), QStringLiteral("desktop_click")}}
    };
    const QStringList disabled = TaskSecurityPolicy::disabledTools(
        {{QStringLiteral("safetyProfile"), QStringLiteral("production")}}, catalog);
    const QStringList expected{QStringLiteral("browser_open"),
                               QStringLiteral("web_search"),
                               QStringLiteral("email_send")};
    QCOMPARE(disabled, expected);
}

void TaskSecurityPolicyTests::nonProductionDoesNotDisableTools()
{
    const QVariantList catalog{
        QVariantMap{{QStringLiteral("name"), QStringLiteral("browser_open")}}
    };
    QVERIFY(TaskSecurityPolicy::disabledTools(
                 {{QStringLiteral("safetyProfile"), QStringLiteral("normal")}}, catalog)
                .isEmpty());
}

QTEST_MAIN(TaskSecurityPolicyTests)
#include "test_task_security_policy.moc"
