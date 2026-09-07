// Unit tests de MasterCli (detección/metadata de CLIs maestro claude/codex).
// La ejecución real del CLI vive en AgentToolRunner; acá cubrimos la metadata
// determinista (supported/label/installCommand) y la forma del status + cache.

#include <QtTest>
#include "core/agent/MasterCli.h"

class MasterCliTests : public QObject
{
    Q_OBJECT
private slots:
    void supported_listsClaudeAndCodex();
    void label_known();
    void installCommand_known();
    void status_hasExpectedKeys();
    void status_cachedUntilInvalidate();
};

void MasterCliTests::supported_listsClaudeAndCodex()
{
    const QStringList s = MasterCli::supported();
    QVERIFY(s.contains("claude"));
    QVERIFY(s.contains("codex"));
}

void MasterCliTests::label_known()
{
    QCOMPARE(MasterCli::label("claude"), QStringLiteral("Claude Code"));
    QCOMPARE(MasterCli::label("codex"), QStringLiteral("Codex CLI"));
    QCOMPARE(MasterCli::label("otro"), QStringLiteral("otro"));  // fallback
}

void MasterCliTests::installCommand_known()
{
    QVERIFY(MasterCli::installCommand("claude").contains("claude-code"));
    QVERIFY(MasterCli::installCommand("codex").contains("codex"));
    QVERIFY(MasterCli::installCommand("xx").isEmpty());
}

void MasterCliTests::status_hasExpectedKeys()
{
    MasterCli cli;
    // Nombre inexistente → no resuelve en PATH, installed=false, pero forma estable.
    const QVariantMap st = cli.status("definitely_not_a_real_cli_xyz");
    QCOMPARE(st.value("installed").toBool(), false);
    QVERIFY(st.contains("name"));
    QVERIFY(st.contains("label"));
    QVERIFY(st.contains("installCommand"));
    QVERIFY(st.contains("path"));
    QVERIFY(st.contains("probeOk"));
    QVERIFY(st.contains("capabilities"));
}

void MasterCliTests::status_cachedUntilInvalidate()
{
    MasterCli cli;
    const QVariantMap a = cli.status("codex");
    const QVariantMap b = cli.status("codex");  // viene de cache
    QCOMPARE(a.value("name"), b.value("name"));
    cli.invalidate();  // no debe crashear; re-detecta
    const QVariantMap c = cli.status("codex", /*force=*/true);
    QCOMPARE(c.value("name").toString(), QStringLiteral("codex"));
}

QTEST_MAIN(MasterCliTests)
#include "test_master_cli.moc"
