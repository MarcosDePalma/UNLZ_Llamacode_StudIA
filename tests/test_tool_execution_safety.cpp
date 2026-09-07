#include <QtTest>

#include "core/agent/ToolExecutionSafety.h"

class ToolExecutionSafetyTests : public QObject
{
    Q_OBJECT
private slots:
    void unknownIsConservative();
    void standardAnnotationsAreHonored();
    void explicitContractOverridesHints();
    void canonicalHashIgnoresObjectKeyOrder();
    void idempotencyIsScopedByCorrelation();
};

void ToolExecutionSafetyTests::unknownIsConservative()
{
    const auto c = ToolExecutionSafety::fromMcpTool(
        QStringLiteral("do_thing"), QString(), {});
    QCOMPARE(c.effect, QStringLiteral("external_write"));
    QVERIFY(c.approvalRequired);
    QCOMPARE(c.source, QStringLiteral("conservative_default"));
}

void ToolExecutionSafetyTests::standardAnnotationsAreHonored()
{
    const auto c = ToolExecutionSafety::fromMcpTool(
        QStringLiteral("lookup"), QString(),
        {{QStringLiteral("readOnlyHint"), true},
         {QStringLiteral("destructiveHint"), false},
         {QStringLiteral("idempotentHint"), true},
         {QStringLiteral("openWorldHint"), false}});
    QCOMPARE(c.effect, QStringLiteral("read"));
    QVERIFY(!c.approvalRequired);
    QVERIFY(c.idempotent);
    QVERIFY(!c.openWorld);
}

void ToolExecutionSafetyTests::explicitContractOverridesHints()
{
    const QJsonObject annotations{
        {QStringLiteral("readOnlyHint"), true},
        {QStringLiteral("llamacode"), QJsonObject{
             {QStringLiteral("effect"), QStringLiteral("proposal")},
             {QStringLiteral("approvalRequired"), true},
             {QStringLiteral("receipt"), QStringLiteral("external_id")}}}};
    const auto c = ToolExecutionSafety::fromMcpTool(
        QStringLiteral("draft_update"), QString(), annotations);
    QCOMPARE(c.effect, QStringLiteral("proposal"));
    QVERIFY(c.approvalRequired);
    QCOMPARE(c.receipt, QStringLiteral("external_id"));
}

void ToolExecutionSafetyTests::canonicalHashIgnoresObjectKeyOrder()
{
    const QJsonObject a{{QStringLiteral("z"), 1},
                        {QStringLiteral("nested"), QJsonObject{
                             {QStringLiteral("b"), 2}, {QStringLiteral("a"), 1}}}};
    const QJsonObject b{{QStringLiteral("nested"), QJsonObject{
                             {QStringLiteral("a"), 1}, {QStringLiteral("b"), 2}}},
                        {QStringLiteral("z"), 1}};
    QCOMPARE(ToolExecutionSafety::payloadHash(QStringLiteral("s"), QStringLiteral("t"), a),
             ToolExecutionSafety::payloadHash(QStringLiteral("s"), QStringLiteral("t"), b));
}

void ToolExecutionSafetyTests::idempotencyIsScopedByCorrelation()
{
    const QString hash = ToolExecutionSafety::payloadHash(
        QStringLiteral("s"), QStringLiteral("t"), {{QStringLiteral("x"), 1}});
    const QString one = ToolExecutionSafety::idempotencyKey(QStringLiteral("run-1"), hash);
    QCOMPARE(one, ToolExecutionSafety::idempotencyKey(QStringLiteral("run-1"), hash));
    QVERIFY(one != ToolExecutionSafety::idempotencyKey(QStringLiteral("run-2"), hash));
}

QTEST_MAIN(ToolExecutionSafetyTests)
#include "test_tool_execution_safety.moc"
