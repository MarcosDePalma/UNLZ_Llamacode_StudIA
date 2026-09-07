// Contrato determinista de corridas administradas: transporte, artefactos y
// política de permisos. La ejecución real queda bajo QProcess y no se hace
// depender de que Claude/Codex estén instalados en la máquina del test.

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "core/agent/ManagedAgentRunStore.h"
#include "core/agent/WorkRegistry.h"
#include "core/tasks/RunHistoryStore.h"

class ManagedAgentRunTests : public QObject
{
    Q_OBJECT
private slots:
    void claude_defaults_toPlan();
    void codex_doesNotEnableFullAutoImplicitly();
    void codex_fullAutoRequiresExplicitDangerousApproval();
    void defaultTransportUsesStdin();
    void workspaceClaimRejectsOverlap();
    void invalidRunDoesNotStart();
    void startPersistsArtifactsAndHistory();
};

void ManagedAgentRunTests::claude_defaults_toPlan()
{
    const QVariantMap command = ManagedAgentRunStore::commandForRequest({
        {"runtime", "claude"}, {"cliPath", "claude"},
        {"prompt", "revisá el diff"}, {"applyEdits", false}
    });
    QCOMPARE(command.value("program").toString(), QStringLiteral("claude"));
    const QStringList args = command.value("args").toStringList();
    QVERIFY(args.contains("-p"));
    QVERIFY(!args.contains("revisá el diff"));
    QVERIFY(args.contains("--permission-mode"));
    QVERIFY(args.contains("plan"));
    QCOMPARE(command.value("permissionPosture").toString(), QStringLiteral("plan"));
    QVERIFY(!args.contains("--dangerously-skip-permissions"));
}

void ManagedAgentRunTests::codex_doesNotEnableFullAutoImplicitly()
{
    const QVariantMap command = ManagedAgentRunStore::commandForRequest({
        {"runtime", "codex"}, {"cliPath", "codex"}, {"prompt", "implementá tests"},
        {"applyEdits", true}, {"approvalMode", "super"}, {"allowDangerous", false}
    });
    const QStringList args = command.value("args").toStringList();
    QVERIFY(args.contains("exec"));
    QVERIFY(!args.contains("--full-auto"));
    QCOMPARE(command.value("permissionPosture").toString(),
             QStringLiteral("default-with-edits-requested"));
}

void ManagedAgentRunTests::codex_fullAutoRequiresExplicitDangerousApproval()
{
    const QVariantMap command = ManagedAgentRunStore::commandForRequest({
        {"runtime", "codex"}, {"cliPath", "codex"}, {"prompt", "revisá"},
        {"applyEdits", true}, {"approvalMode", "super"}, {"allowDangerous", true}
    });
    QVERIFY(command.value("args").toStringList().contains("--full-auto"));
    QCOMPARE(command.value("permissionPosture").toString(),
             QStringLiteral("full-auto-explicit"));
}

void ManagedAgentRunTests::defaultTransportUsesStdin()
{
    const QVariantMap command = ManagedAgentRunStore::commandForRequest({
        {"runtime", "claude"}, {"cliPath", "claude"},
        {"prompt", "secreto no debe ir en argv"}
    });
    QCOMPARE(command.value("promptTransport").toString(), QStringLiteral("stdin"));
    QVERIFY(!command.value("args").toStringList().contains(
        QStringLiteral("secreto no debe ir en argv")));
    QVERIFY(!command.value("display").toString().contains(QStringLiteral("secreto")));
}

void ManagedAgentRunTests::workspaceClaimRejectsOverlap()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString claim = WorkRegistry::acquire(root.path(), QStringLiteral("native-session"),
                                                QStringLiteral("native"), QStringLiteral("edición"));
    QVERIFY(!claim.isEmpty());
    QVERIFY(WorkRegistry::claimPaths(root.path(), claim, QStringLiteral("native-session"),
                                     {QStringLiteral("src/main.cpp")}));
    const QByteArray runs = (root.path() + QStringLiteral("/runs")).toUtf8();
    qputenv("LLAMACODE_MANAGED_RUNS_DIR", runs);
    ManagedAgentRunStore store(nullptr);
    const QString id = store.startRun({{"runtime", "claude"}, {"cliPath", "cmd.exe"},
                                       {"prompt", "editar"}, {"workspace", root.path()},
                                       {"applyEdits", true},
                                       {"prefixArgs", QStringList{"/c", "exit", "0"}}});
    QVERIFY(id.isEmpty());
    QVERIFY(store.lastError().contains(QStringLiteral("conflict"))
            || store.lastError().contains(QStringLiteral("reclama")));
    WorkRegistry::release(root.path(), claim, QStringLiteral("native-session"));
    qunsetenv("LLAMACODE_MANAGED_RUNS_DIR");
}

void ManagedAgentRunTests::invalidRunDoesNotStart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    qputenv("LLAMACODE_MANAGED_RUNS_DIR", dir.path().toUtf8());
    ManagedAgentRunStore store(nullptr);
    QVERIFY(store.startRun({{"runtime", "wat"}, {"prompt", "x"},
                             {"workspace", dir.path()}}).isEmpty());
    QVERIFY(store.lastError().contains("runtime"));
    QVERIFY(store.runs().isEmpty());
    qunsetenv("LLAMACODE_MANAGED_RUNS_DIR");
}

void ManagedAgentRunTests::startPersistsArtifactsAndHistory()
{
#ifndef Q_OS_WIN
    QSKIP("La prueba usa cmd.exe para no depender de Claude/Codex instalados.");
#else
    QTemporaryDir root;
    QTemporaryDir historyDir;
    QVERIFY(root.isValid() && historyDir.isValid());
    qputenv("LLAMACODE_MANAGED_RUNS_DIR", root.path().toUtf8());
    qputenv("LLAMACODE_RUN_HISTORY_DIR", historyDir.path().toUtf8());

    RunHistoryStore historyStore;
    ManagedAgentRunStore store(&historyStore);
    QSignalSpy finished(&store, &ManagedAgentRunStore::runFinished);
    const QString shell = qEnvironmentVariable("ComSpec", QStringLiteral("cmd.exe"));
    const QString id = store.startRun({
        {"runtime", "claude"}, {"cliPath", shell}, {"prompt", "revisá el diff"},
        {"workspace", root.path()}, {"ownerId", "managed-test"},
        {"prefixArgs", QStringList{"/c", "exit", "0"}}
    });
    QVERIFY(!id.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(finished.count() > 0, 5000);
    const QVariantMap run = store.run(id);
    QVERIFY(run.value("status").toString() == "failed"
            || run.value("status").toString() == "finished");
    QVERIFY(QFile::exists(run.value("promptPath").toString()));
    QVERIFY(QFile::exists(run.value("manifestPath").toString()));
    QVERIFY(QFile::exists(run.value("stdoutPath").toString()));
    QVERIFY(QFile::exists(run.value("stderrPath").toString()));
    QFile manifestFile(run.value("manifestPath").toString());
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    QVERIFY(!manifest.contains(QStringLiteral("prompt")));
    QCOMPARE(manifest.value(QStringLiteral("promptSha256")).toString(),
             run.value(QStringLiteral("promptSha256")).toString());
    RunHistoryStore history;
    const QVariantList rows = history.history(QStringLiteral("managed-test"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().toMap().value("source").toString(), QStringLiteral("managed_agent"));
    QCOMPARE(rows.first().toMap().value("metadata").toMap().value("runtime").toString(),
             QStringLiteral("claude"));
    qunsetenv("LLAMACODE_MANAGED_RUNS_DIR");
    qunsetenv("LLAMACODE_RUN_HISTORY_DIR");
#endif
}

QTEST_MAIN(ManagedAgentRunTests)
#include "test_managed_agent_runs.moc"
