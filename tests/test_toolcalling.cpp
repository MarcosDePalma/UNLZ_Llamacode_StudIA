// Unit tests de ToolCallingSupport: inferencia PURA del soporte de tool-calling a
// partir del cookbook (capabilities) y del chat-template del GGUF.

#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include "core/ToolCallingSupport.h"

using TS = ToolCallingSupport;

class ToolCallingTests : public QObject
{
    Q_OBJECT
private slots:
    void normalizeKey_equivalentVariants();
    void fromCookbook_supported();
    void fromCookbook_unsupported();
    void fromCookbook_unknownNoMatch();
    void templateMentionsTools_detects();
    void combine_matrix();
    void forceTextTools_onlyWhenUnsupported();
};

static QJsonArray sampleCookbook()
{
    QJsonArray a;
    a.append(QJsonObject{{"name", "Qwen/Qwen2.5-7B-Instruct"},
                         {"capabilities", QJsonArray{"chat", "tool_use"}}});
    a.append(QJsonObject{{"name", "deepseek-ai/DeepSeek-R1-Distill-Qwen-7B"},
                         {"capabilities", QJsonArray{"reasoning"}}});  // sin tool_use
    return a;
}

void ToolCallingTests::normalizeKey_equivalentVariants()
{
    QCOMPARE(TS::normalizeKey(QStringLiteral("Qwen2.5-7B-Instruct-Q4_K_M.gguf")),
             TS::normalizeKey(QStringLiteral("Qwen/Qwen2.5-7B-Instruct")));
}

void ToolCallingTests::fromCookbook_supported()
{
    QCOMPARE(TS::fromCookbook(QStringLiteral("Qwen2.5-7B-Instruct-Q4_K_M.gguf"), sampleCookbook()),
             TS::Support::Supported);
}

void ToolCallingTests::fromCookbook_unsupported()
{
    // Matchea el R1-Distill (sin tool_use) → Unsupported.
    QCOMPARE(TS::fromCookbook(QStringLiteral("DeepSeek-R1-Distill-Qwen-7B-Q4_K_M.gguf"), sampleCookbook()),
             TS::Support::Unsupported);
}

void ToolCallingTests::fromCookbook_unknownNoMatch()
{
    QCOMPARE(TS::fromCookbook(QStringLiteral("SomeRandom-Model-13B.gguf"), sampleCookbook()),
             TS::Support::Unknown);
    QCOMPARE(TS::fromCookbook(QString(), sampleCookbook()), TS::Support::Unknown);
}

void ToolCallingTests::templateMentionsTools_detects()
{
    QVERIFY(TS::templateMentionsTools(QStringLiteral(
        "{% for message in messages %}{% if message.tool_calls %}...{% endif %}{% endfor %}")));
    QVERIFY(TS::templateMentionsTools(QStringLiteral(
        "{% if tools %}{{ tools | tojson }}{% endif %}")));
    QVERIFY(!TS::templateMentionsTools(QStringLiteral(
        "{% for m in messages %}{{ m.role }}: {{ m.content }}{% endfor %}")));
    QVERIFY(!TS::templateMentionsTools(QString()));
}

void ToolCallingTests::combine_matrix()
{
    // Template real con tools manda, aun si el cookbook lo desconoce.
    QCOMPARE(TS::combine(TS::Support::Unknown, true, true), TS::Support::Supported);
    // Cookbook positivo basta aunque el template no haya respondido.
    QCOMPARE(TS::combine(TS::Support::Supported, false, false), TS::Support::Supported);
    // Cookbook negativo → Unsupported.
    QCOMPARE(TS::combine(TS::Support::Unsupported, false, false), TS::Support::Unsupported);
    // Desconocido y template respondió sin tools → Unsupported.
    QCOMPARE(TS::combine(TS::Support::Unknown, true, false), TS::Support::Unsupported);
    // Desconocido sin señal de template → Unknown.
    QCOMPARE(TS::combine(TS::Support::Unknown, false, false), TS::Support::Unknown);
}

// "unknown" es transitorio: en cada swap de modelo la señal de template se
// resetea hasta que responde el /props del server nuevo. Forzar texto ahí
// gastaba el turno entero en el protocolo lento con modelos que sí traen
// tool-calling nativo (KAT-Coder respondía 200 al payload con tools).
void ToolCallingTests::forceTextTools_onlyWhenUnsupported()
{
    QVERIFY(TS::shouldForceTextTools(QStringLiteral("unsupported")));
    QVERIFY(!TS::shouldForceTextTools(QStringLiteral("unknown")));
    QVERIFY(!TS::shouldForceTextTools(QStringLiteral("supported")));
    QVERIFY(!TS::shouldForceTextTools(QString()));
}

QTEST_MAIN(ToolCallingTests)
#include "test_toolcalling.moc"
