#include <QtTest>
#include "core/agent/AgentProgressGovernor.h"

class AgentProgressTests : public QObject
{
    Q_OBJECT
private slots:
    void semanticVariantsCollapse();
    void distinctRequestedArtifactsRemainDistinct();
    void progressRenewsBudget();
    void stagnantVariantsReplanThenStop();
    void explicitMultiLanguageTaskAllowsVariants();
    void namedArtifactTriggersEarlyClosureHint();
    void repeatedEditsToSameFileCanMakeProgress();
    void repeatedIdenticalWriteToSameFileIsNotProgress();
    void repeatedIdenticalFailureCostsDouble();
    void sameContentUnderNewNameIsNotProgress();
    void renameLoopStopsAtDistinctWriteCeiling();
};

void AgentProgressTests::semanticVariantsCollapse()
{
    const QString py = AgentProgressGovernor::semanticKey(
        "write_file", R"({"path":"run_test.py"})");
    const QString cpp = AgentProgressGovernor::semanticKey(
        "write_file", R"({"path":"run_test.cpp"})");
    QCOMPARE(py, cpp);
}

void AgentProgressTests::distinctRequestedArtifactsRemainDistinct()
{
    QVERIFY(AgentProgressGovernor::semanticKey("write_file", R"({"path":"client.py"})")
            != AgentProgressGovernor::semanticKey("write_file", R"({"path":"server.cpp"})"));
}

void AgentProgressTests::progressRenewsBudget()
{
    AgentProgressGovernor g({4, 8, 3, 3});
    const auto first = g.record("write_file", R"({"path":"answer.py"})",
                                true, "[escrito 20 bytes]", true);
    QVERIFY(first.progress);
    QVERIFY(first.credits > 4);
    const auto read = g.record("read_file", R"({"path":"answer.py"})",
                               true, "def answer(): return 42", false);
    QVERIFY(read.progress);
    QCOMPARE(read.action, AgentProgressGovernor::Continue);
}

void AgentProgressTests::stagnantVariantsReplanThenStop()
{
    AgentProgressGovernor g({6, 10, 3, 3});
    QCOMPARE(g.record("write_file", R"({"path":"run_test.py"})",
                      true, "ok", true).action, AgentProgressGovernor::Continue);
    QCOMPARE(g.record("write_file", R"({"path":"run_test.cpp"})",
                      true, "ok2", true).action, AgentProgressGovernor::Continue);
    QCOMPARE(g.record("write_file", R"({"path":"run_test.go"})",
                      true, "ok3", true).action, AgentProgressGovernor::Continue);
    QCOMPARE(g.record("write_file", R"({"path":"run_test.java"})",
                      true, "ok4", true).action, AgentProgressGovernor::Replan);
    g.record("write_file", R"({"path":"run_test.js"})", true, "ok5", true);
    g.record("write_file", R"({"path":"run_test.rs"})", true, "ok6", true);
    QCOMPARE(g.record("write_file", R"({"path":"run_test.rb"})",
                      true, "ok7", true).action, AgentProgressGovernor::Stop);
}

void AgentProgressTests::explicitMultiLanguageTaskAllowsVariants()
{
    AgentProgressGovernor g({4, 8, 3, 3});
    g.reset(QStringLiteral("Create the example in multiple languages"));
    QVERIFY(g.record("write_file", R"({"path":"run_test.py"})", true, "py", true).progress);
    QVERIFY(g.record("write_file", R"({"path":"run_test.cpp"})", true, "cpp", true).progress);
}

void AgentProgressTests::namedArtifactTriggersEarlyClosureHint()
{
    AgentProgressGovernor g;
    g.reset(QStringLiteral("Create answer.py with the requested function"));
    const auto d = g.record("write_file", R"({"path":"answer.py"})",
                            true, "written", true);
    QVERIFY(d.objectiveSatisfied);
}

// Regresión medida en un benchmark real: una sola tarea generó 1.436 archivos .py
// (is_prime.py, prime_check.py, prime_checker.py, prime_checker_v10.py…) y otra 704
// con nombres ya degenerados (add.py, add_only.py, add_only_only.py). La clave
// semántica de una escritura incluye el path, así que alcanzaba con renombrar para
// que cada vuelta contara como evidencia nueva: reseteaba el estancamiento y encima
// sumaba créditos. El turno tardó 4.696 s en vez de ~400.
void AgentProgressTests::sameContentUnderNewNameIsNotProgress()
{
    AgentProgressGovernor g;
    auto writeArgs = [](const QString &path, const QString &content) {
        return QStringLiteral("{\"path\":\"%1\",\"content\":\"%2\"}").arg(path, content);
    };
    const QString body = QStringLiteral("def is_prime(n): return n > 1");

    QVERIFY(g.record("write_file", writeArgs("is_prime.py", body), true, "escrito", true).progress);

    // Mismo contenido, otro nombre: NO es progreso.
    const auto d = g.record("write_file", writeArgs("prime_check.py", body), true, "escrito", true);
    QVERIFY(!d.progress);
    QVERIFY(d.stagnant > 0);

    // Contenido realmente distinto en otro archivo sí sigue siendo progreso.
    QVERIFY(g.record("write_file",
                     writeArgs("merge_sort.py", QStringLiteral("def merge_sort(a): return sorted(a)")),
                     true, "escrito", true).progress);
}

// Red de seguridad para el caso que el chequeo de contenido no agarra: el modelo
// cambia una línea en cada vuelta, así que cada archivo es "nuevo" de verdad y el
// presupuesto elástico nunca cierra.
void AgentProgressTests::renameLoopStopsAtDistinctWriteCeiling()
{
    AgentProgressGovernor::Policy p;
    p.maxDistinctWrites = 5;
    AgentProgressGovernor g(p);
    auto renameArgs = [](int i) {
        return QStringLiteral("{\"path\":\"prime_v%1.py\",\"content\":\"def f(): return %1\"}").arg(i);
    };
    AgentProgressGovernor::Action last = AgentProgressGovernor::Continue;
    for (int i = 0; i < 40; ++i) {
        last = g.record("write_file", renameArgs(i), true,
                        QStringLiteral("escrito %1").arg(i), true).action;
        if (last == AgentProgressGovernor::Stop) break;
    }
    QCOMPARE(last, AgentProgressGovernor::Stop);

    // Con el techo por defecto, un puñado de archivos legítimos no se corta.
    AgentProgressGovernor ok;
    for (int i = 0; i < 6; ++i) {
        const QString args =
            QStringLiteral("{\"path\":\"mod%1.py\",\"content\":\"def f%1(): return %1\"}").arg(i);
        QVERIFY(ok.record("write_file", args, true, QStringLiteral("escrito %1").arg(i), true).action
                != AgentProgressGovernor::Stop);
    }
}

void AgentProgressTests::repeatedEditsToSameFileCanMakeProgress()
{
    AgentProgressGovernor g;
    QVERIFY(g.record("edit_file", R"({"path":"app.cpp"})", true, "diff one", true).progress);
    QVERIFY(g.record("edit_file", R"({"path":"app.cpp"})", true, "diff two", true).progress);
}

void AgentProgressTests::repeatedIdenticalWriteToSameFileIsNotProgress()
{
    AgentProgressGovernor g;
    const QString args = R"({"path":"solution.py","content":"def f(): return 1"})";
    QVERIFY(g.record("write_file", args, true, "escrito", true).progress);
    const auto repeated = g.record("write_file", args, true, "escrito", true);
    QVERIFY(!repeated.progress);
    QVERIFY(repeated.stagnant > 0);
}

// El bucle clasico: list_dir ".." rechazado una y otra vez. Cada repeticion
// identica cuesta doble para llegar al replanteo sin gastar diez turnos.
void AgentProgressTests::repeatedIdenticalFailureCostsDouble()
{
    AgentProgressGovernor g({8, 16, 99, 99});
    const char *args = R"({"path":".."})";
    const QString refusal = QStringLiteral("[ruta fuera del proyecto: ...]");
    const auto first = g.record("list_dir", args, false, refusal, false);
    QVERIFY(!first.repeatedFailure);
    QCOMPARE(first.credits, 7);
    const auto second = g.record("list_dir", args, false, refusal, false);
    QVERIFY(second.repeatedFailure);
    QCOMPARE(second.credits, 5);
    QCOMPARE(g.record("list_dir", args, false, refusal, false).credits, 3);
    QCOMPARE(g.record("list_dir", args, false, refusal, false).credits, 1);
    QCOMPARE(g.record("list_dir", args, false, refusal, false).action,
             AgentProgressGovernor::Replan);
}

QTEST_MAIN(AgentProgressTests)
#include "test_agent_progress.moc"
