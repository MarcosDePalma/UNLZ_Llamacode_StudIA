// Unit tests de CapabilityDetector::parse (PURO: parsea el texto de --help y
// extrae flags soportados + alias corto→largo). Sin proceso ni binario real.

#include <QtTest>
#include "core/CapabilityDetector.h"

class CapabilityTests : public QObject
{
    Q_OBJECT
private slots:
    void parse_extractsLongFlags();
    void parse_shortAliasesToLong();
    void parse_stripsAnsi();
    void parse_legacyDraftAlias();
    void parse_ignoresDescriptionContinuation();
    void parse_probeCapturesVersionKvAndSpecTypes();
    void parse_probeCapturesSpecializedKvFlags();
    void parse_extractsAdaptiveSpecFlags();
};

void CapabilityTests::parse_extractsLongFlags()
{
    const QString help =
        "  --host HOST          listen address\n"
        "  --port PORT          listen port\n"
        "  --flash-attn         enable flash attention\n";
    const auto cap = CapabilityDetector::parse(help);
    QVERIFY(cap.success);
    QVERIFY(cap.hasFlag("--host"));
    QVERIFY(cap.hasFlag("--port"));
    QVERIFY(cap.hasFlag("--flash-attn"));
}

void CapabilityTests::parse_shortAliasesToLong()
{
    const QString help = "  -c, --ctx-size N     context size\n";
    const auto cap = CapabilityDetector::parse(help);
    QVERIFY(cap.hasFlag("--ctx-size"));
    QCOMPARE(cap.flagAliases.value("-c"), QStringLiteral("--ctx-size"));
}

void CapabilityTests::parse_stripsAnsi()
{
    const QString help = "  \x1B[1m--mmap\x1B[0m   use mmap\n";
    const auto cap = CapabilityDetector::parse(help);
    QVERIFY(cap.hasFlag("--mmap"));
}

void CapabilityTests::parse_legacyDraftAlias()
{
    const QString help = "  --spec-draft-model PATH   speculative draft model\n";
    const auto cap = CapabilityDetector::parse(help);
    QVERIFY(cap.hasFlag("--spec-draft-model"));
    QCOMPARE(cap.flagAliases.value("--draft-model"), QStringLiteral("--spec-draft-model"));
}

void CapabilityTests::parse_ignoresDescriptionContinuation()
{
    // Líneas con 17+ espacios de indent son continuación de descripción, no flags.
    const QString help =
        "  --host HOST\n"
        "                     this --not-a-flag is part of the description\n";
    const auto cap = CapabilityDetector::parse(help);
    QVERIFY(cap.hasFlag("--host"));
    QVERIFY(!cap.hasFlag("--not-a-flag"));
}

void CapabilityTests::parse_probeCapturesVersionKvAndSpecTypes()
{
    const QString version = "version: b9761 commit abcdef0\n";
    const QString help =
        "  --cache-type-k TYPE       KV cache type\n"
        "  --spec-type [none|draft|nextn] speculative mode\n";
    const auto cap = CapabilityDetector::parseProbeOutput(version, help);
    QVERIFY(cap.success);
    QCOMPARE(cap.version, QStringLiteral("b9761 commit abcdef0"));
    QVERIFY(cap.kvTypes.contains(QStringLiteral("q8_0")));
    QVERIFY(cap.hasFlag(QStringLiteral("spec-type:none")));
    QVERIFY(cap.hasFlag(QStringLiteral("spec-type:nextn")));
}

void CapabilityTests::parse_probeCapturesSpecializedKvFlags()
{
    // A future compressed-KV runtime must be discoverable as a capability, not
    // silently treated as an ordinary q4/q8 cache profile.
    const QString help =
        "  --fraqtl-kv PATH                 enable compressed KV pages\n"
        "  --fraqtl-eigenbasis PATH         V eigenbasis sidecar\n"
        "  --fraqtl-k-protect N             protected K dimensions\n"
        "  --fraqtl-k-eigenbasis PATH       K eigenbasis sidecar\n"
        "  --fraqtl-sink-tokens N           sink tokens\n"
        "  --fraqtl-residual-window N       residual window\n";
    const auto cap = CapabilityDetector::parse(help);
    QVERIFY(cap.success);
    for (const QString &flag : {
             QStringLiteral("--fraqtl-kv"),
             QStringLiteral("--fraqtl-eigenbasis"),
             QStringLiteral("--fraqtl-k-protect"),
             QStringLiteral("--fraqtl-k-eigenbasis"),
             QStringLiteral("--fraqtl-sink-tokens"),
             QStringLiteral("--fraqtl-residual-window")})
        QVERIFY2(cap.hasFlag(flag), qPrintable(flag));
    // Specialized flags alone must not silently expand the ordinary K/V
    // quantization menu; the runtime adapter owns its sidecar format.
    QCOMPARE(cap.kvTypes, QStringList{QStringLiteral("f16")});
}

void CapabilityTests::parse_extractsAdaptiveSpecFlags()
{
    const QString help =
        "  --spec-draft-adaptive       adjust draft length automatically\n"
        "  --spec-draft-n-min N        minimum draft tokens\n"
        "  --spec-draft-n-max N        maximum draft tokens\n";
    const auto cap = CapabilityDetector::parse(help);
    QVERIFY(cap.success);
    QVERIFY(cap.hasFlag(QStringLiteral("--spec-draft-adaptive")));
    QVERIFY(cap.hasFlag(QStringLiteral("--spec-draft-n-min")));
    QVERIFY(cap.hasFlag(QStringLiteral("--spec-draft-n-max")));
}

QTEST_MAIN(CapabilityTests)
#include "test_capability.moc"
