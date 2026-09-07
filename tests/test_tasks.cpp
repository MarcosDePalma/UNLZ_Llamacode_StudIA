// Unit tests de TaskStore: (de)serialización JSON roundtrip, composición pura del
// prompt-objetivo (incluye la consigna de adaptación), y CRUD persistente con
// aislamiento de disco (QStandardPaths test mode).

#include <QtTest>
#include <functional>
#include "core/tasks/TaskStore.h"
#include "core/tasks/SchedulerDaemonRegistration.h"

class TasksTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void jsonRoundTrip();
    void jsonRoundTrip_permsAndScheduleSpec();
    void composePrompt_mentionsAllowedFolder();
    void composePrompt_includesGoalStepsAndAdaptation();
    void composePrompt_includesPreAndPostPrompts();
    void sanitize_slug();
    void crud_persistsAcrossInstances();
    void duplicate_clonesWithNewId();
    void markRun_updatesStatus();
    void reload_recoversOrphanRunningStatus();
    void reload_preservesResumableWorkflow();
    void loop_jsonRoundTrip();
    void loop_decideStopsAndRepeats();
    void loop_stopsAtOperationalTimeBudget();
    void loop_composeGoalPrompt();
    void loop_runsExactlyMaxIterationsWhenGoalNeverMet();
    void loop_stopsEarlyWhenGoalMet();
    void loop_composeProgressCarriesPriorVerdict();
    void verifyProfile_routesOnlyWhenSetAndDifferent();
    void schedulerDaemon_quotesExecutablePath();
};

static QVariantMap sampleTask()
{
    QVariantList steps;
    steps.append(QVariantMap{{"kind", "browser"}, {"intent", "abrir banco"}, {"ref", "https://x"}});
    steps.append(QVariantMap{{"kind", "instruction"}, {"intent", "copiar el valor"}, {"ref", ""}});
    return QVariantMap{
        {"id", "t1"}, {"name", "Cotización dólar"}, {"description", "extraer la cotización"},
        {"profileId", "p1"}, {"scheduleEnabled", true}, {"scheduleCron", "0 9 * * *"},
        {"prePrompt", "Usá browser si hace falta."}, {"postPrompt", "Verificá que el valor tenga compra y venta."},
        {"silentUnlessError", true},
        {"executionMode", "browserBackground"}, {"approvalPolicy", "sensitive"},
        {"safetyProfile", "guarded"},
        {"teachArtifactId", "artifact-1"}, {"teachFormatVersion", 1},
        {"scopeKind", "screen"}, {"scopeTargetId", "0"}, {"scopeLabel", "Pantalla 1"},
        {"scopeWidth", 1920}, {"scopeHeight", 1080}, {"scopeDpi", 96.0},
        {"timeoutSec", 300}, {"maxActions", 50}, {"maxRetries", 2},
        {"automationStatus", "ready"},
        {"steps", steps}
    };
}

void TasksTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void TasksTests::schedulerDaemon_quotesExecutablePath()
{
    const QString command = SchedulerDaemonRegistration::startupCommand(
        QStringLiteral("C:/Program Files/LlamaCode/LlamaCode.exe"));
    QVERIFY(command.startsWith(QLatin1Char('"')));
    QVERIFY(command.contains(QStringLiteral("--scheduler-daemon")));
    QVERIFY(command.contains(QStringLiteral("Program Files")));
}

void TasksTests::jsonRoundTrip()
{
    QVariantMap in = sampleTask();
    in["workflow"] = QVariantMap{{"schemaVersion", 1}, {"entry", "run"},
                                  {"steps", QVariantMap{{"run", QVariantMap{{"type", "agent"}}}}}};
    const QVariantMap out = TaskStore::fromJson(TaskStore::toJson(in));
    QCOMPARE(out.value("name").toString(), in.value("name").toString());
    QCOMPARE(out.value("profileId").toString(), QStringLiteral("p1"));
    QCOMPARE(out.value("scheduleEnabled").toBool(), true);
    QCOMPARE(out.value("scheduleCron").toString(), QStringLiteral("0 9 * * *"));
    QCOMPARE(out.value("prePrompt").toString(), QStringLiteral("Usá browser si hace falta."));
    QCOMPARE(out.value("postPrompt").toString(), QStringLiteral("Verificá que el valor tenga compra y venta."));
    QCOMPARE(out.value("silentUnlessError").toBool(), true);
    QCOMPARE(out.value("executionMode").toString(), QStringLiteral("browserBackground"));
    QCOMPARE(out.value("approvalPolicy").toString(), QStringLiteral("sensitive"));
    QCOMPARE(out.value("safetyProfile").toString(), QStringLiteral("guarded"));
    QCOMPARE(out.value("teachArtifactId").toString(), QStringLiteral("artifact-1"));
    QCOMPARE(out.value("maxActions").toInt(), 50);
    // Tipo de entrenamiento: default "literal"; y round-trip de "adaptive".
    QCOMPARE(out.value("trainingType").toString(), QStringLiteral("literal"));
    QVariantMap adaptive = in;
    adaptive["trainingType"] = QStringLiteral("adaptive");
    QCOMPARE(TaskStore::fromJson(TaskStore::toJson(adaptive)).value("trainingType").toString(),
             QStringLiteral("adaptive"));
    const QVariantList steps = out.value("steps").toList();
    QCOMPARE(steps.size(), 2);
    QCOMPARE(steps.at(0).toMap().value("kind").toString(), QStringLiteral("browser"));
    QCOMPARE(steps.at(0).toMap().value("ref").toString(), QStringLiteral("https://x"));
    QCOMPARE(out.value("workflow").toMap().value("entry").toString(), QStringLiteral("run"));
}

void TasksTests::jsonRoundTrip_permsAndScheduleSpec()
{
    QVariantMap in = sampleTask();
    in["permScope"] = QStringLiteral("folder");
    in["permFolders"] = QVariantList{QStringLiteral("C:/Users/X/PruebasIA"),
                                     QStringLiteral("D:/Datos")};
    in["scheduleSpec"] = QVariantMap{{"mode", "weekly"}, {"hour", 9}, {"minute", 30},
                                     {"weekdays", QVariantList{1, 3}}};
    const QVariantMap out = TaskStore::fromJson(TaskStore::toJson(in));
    QCOMPARE(out.value("permScope").toString(), QStringLiteral("folder"));
    QCOMPARE(out.value("permFolders").toStringList().size(), 2);
    QCOMPARE(out.value("permFolders").toStringList().at(0), QStringLiteral("C:/Users/X/PruebasIA"));
    const QVariantMap spec = out.value("scheduleSpec").toMap();
    QCOMPARE(spec.value("mode").toString(), QStringLiteral("weekly"));
    QCOMPARE(spec.value("hour").toInt(), 9);
    QCOMPARE(spec.value("weekdays").toList().size(), 2);

    // Default seguro cuando el JSON no trae permScope.
    const QVariantMap legacy = TaskStore::fromJson(TaskStore::toJson(sampleTask()));
    QCOMPARE(legacy.value("permScope").toString(), QStringLiteral("project"));

    QVariantMap noSafety = sampleTask();
    noSafety.remove(QStringLiteral("safetyProfile"));
    QCOMPARE(TaskStore::fromJson(TaskStore::toJson(noSafety))
                 .value(QStringLiteral("safetyProfile")).toString(), QStringLiteral("normal"));
}

void TasksTests::composePrompt_mentionsAllowedFolder()
{
    QVariantMap task = sampleTask();
    task["permScope"] = QStringLiteral("folder");
    task["permFolders"] = QVariantList{QStringLiteral("C:/Users/X/PruebasIA")};
    const QString p = TaskStore::composePrompt(task);
    QVERIFY(p.contains(QStringLiteral("C:/Users/X/PruebasIA")));
    QVERIFY(p.contains(QStringLiteral("permitida")));
}

void TasksTests::composePrompt_includesPreAndPostPrompts()
{
    const QVariantMap task = sampleTask();
    const QString p = TaskStore::composePrompt(task);
    QVERIFY(p.contains(QStringLiteral("Preprompt operativo")));
    QVERIFY(p.contains(QStringLiteral("Usá browser si hace falta.")));

    const QString post = TaskStore::composePostPrompt(task);
    QVERIFY(post.contains(QStringLiteral("Postprompt de verificación")));
    QVERIFY(post.contains(QStringLiteral("Verificá que el valor tenga compra y venta.")));
}

void TasksTests::composePrompt_includesGoalStepsAndAdaptation()
{
    const QString p = TaskStore::composePrompt(sampleTask());
    QVERIFY(p.contains(QStringLiteral("Cotización dólar")));
    QVERIFY(p.contains(QStringLiteral("extraer la cotización")));
    QVERIFY(p.contains(QStringLiteral("[browser] abrir banco")));
    QVERIFY(p.contains(QStringLiteral("ref: https://x")));
    // La consigna clave: pasos son guía, no guion literal (replay adaptativo).
    QVERIFY(p.contains(QStringLiteral("no un guion literal")));
    QVERIFY(p.contains(QStringLiteral("adaptate")));
}

void TasksTests::sanitize_slug()
{
    QCOMPARE(TaskStore::sanitize(QStringLiteral("  Hola Mundo!! ")), QStringLiteral("hola-mundo"));
    QCOMPARE(TaskStore::sanitize(QStringLiteral("A/B__C")), QStringLiteral("a-b__c"));
    QVERIFY(TaskStore::sanitize(QStringLiteral("!!!")).isEmpty());
}

void TasksTests::crud_persistsAcrossInstances()
{
    QString id;
    {
        TaskStore s;
        const int before = s.count();
        id = s.save({}, sampleTask());
        QVERIFY(!id.isEmpty());
        QCOMPARE(s.count(), before + 1);
        QVERIFY(!s.get(id).isEmpty());
    }
    {
        TaskStore s2;   // recarga desde disco
        const QVariantMap t = s2.get(id);
        QCOMPARE(t.value("name").toString(), QStringLiteral("Cotización dólar"));
        QCOMPARE(t.value("steps").toList().size(), 2);
        QVERIFY(s2.remove(id));
        QVERIFY(s2.get(id).isEmpty());
    }
    { TaskStore s3; QVERIFY(s3.get(id).isEmpty()); }   // borrado persistió
}

void TasksTests::duplicate_clonesWithNewId()
{
    TaskStore s;
    const QString id = s.save({}, sampleTask());
    const QString dupId = s.duplicate(id);
    QVERIFY(!dupId.isEmpty());
    QVERIFY(dupId != id);
    QVERIFY(s.get(dupId).value("name").toString().contains(QStringLiteral("copia")));
    s.remove(id);
    s.remove(dupId);
}

void TasksTests::markRun_updatesStatus()
{
    TaskStore s;
    const QString id = s.save({}, sampleTask());
    s.markRun(id, QStringLiteral("ok"), QStringLiteral("terminó bien"));
    QCOMPARE(s.get(id).value("lastRunStatus").toString(), QStringLiteral("ok"));
    QCOMPARE(s.get(id).value("lastRunSummary").toString(), QStringLiteral("terminó bien"));
    QVERIFY(!s.get(id).value("lastRunAt").toString().isEmpty());
    s.remove(id);
}

void TasksTests::reload_recoversOrphanRunningStatus()
{
    QString id;
    {
        TaskStore s;
        id = s.save({}, sampleTask());
        s.markRun(id, QStringLiteral("running"), QStringLiteral("Ejecutando Task..."));
        QCOMPARE(s.get(id).value("lastRunStatus").toString(), QStringLiteral("running"));
    }
    {
        TaskStore reloaded;
        const QVariantMap task = reloaded.get(id);
        QCOMPARE(task.value("lastRunStatus").toString(), QStringLiteral("error"));
        QVERIFY(task.value("lastRunSummary").toString().contains(QStringLiteral("interrumpida")));
        reloaded.remove(id);
    }
}

void TasksTests::reload_preservesResumableWorkflow()
{
    QString id;
    {
        TaskStore s;
        QVariantMap task = sampleTask();
        task["workflow"] = QVariantMap{{"schemaVersion", 1}, {"entry", "work"},
            {"steps", QVariantMap{{"work", QVariantMap{{"type", "agent"}}}}}};
        id = s.save({}, task);
        s.markRun(id, QStringLiteral("running"));
        s.markWorkflowState(id, {{"schemaVersion", 1}, {"workflowId", id},
                                 {"currentStep", "work"}, {"status", "running"}});
    }
    {
        TaskStore reloaded;
        QCOMPARE(reloaded.get(id).value("lastRunStatus").toString(), QStringLiteral("resumable"));
        QVERIFY(reloaded.get(id).value("lastRunSummary").toString().contains(QStringLiteral("reanudará")));
        reloaded.remove(id);
    }
}

void TasksTests::loop_jsonRoundTrip()
{
    QVariantMap in = sampleTask();
    in["loopEnabled"] = true;
    in["loopGoal"] = QStringLiteral("todos los tests en verde");
    in["loopMaxIterations"] = 7;
    in["loopMaxSeconds"] = 7200;
    const QVariantMap out = TaskStore::fromJson(TaskStore::toJson(in));
    QCOMPARE(out.value("loopEnabled").toBool(), true);
    QCOMPARE(out.value("loopGoal").toString(), QStringLiteral("todos los tests en verde"));
    QCOMPARE(out.value("loopMaxIterations").toInt(), 7);
    QCOMPARE(out.value("loopMaxSeconds").toInt(), 7200);

    // Default seguro para JSON legacy sin campos de loop.
    const QVariantMap legacy = TaskStore::fromJson(TaskStore::toJson(sampleTask()));
    QCOMPARE(legacy.value("loopEnabled").toBool(), false);
    QCOMPARE(legacy.value("loopMaxIterations").toInt(), 5);
    QCOMPARE(legacy.value("loopMaxSeconds").toInt(), 0);
}

void TasksTests::loop_decideStopsAndRepeats()
{
    QVariantMap task = sampleTask();

    // Loop apagado → nunca repite.
    QVERIFY(!TaskStore::decideLoop(task, 1, QStringLiteral("ok"), QString()).repeat);

    task["loopEnabled"] = true;
    task["loopMaxIterations"] = 3;

    // Objetivo no cumplido y bajo el techo → repite.
    auto d = TaskStore::decideLoop(task, 1, QStringLiteral("ok"), QStringLiteral("GOAL_NOT_MET faltan 2"));
    QVERIFY(d.repeat);

    // Objetivo cumplido → corta.
    QVERIFY(!TaskStore::decideLoop(task, 1, QStringLiteral("ok"),
                                   QStringLiteral("GOAL_MET todo verde")).repeat);

    // GOAL_NOT_MET contiene GOAL_MET como subcadena: NO debe contar como cumplido.
    QVERIFY(TaskStore::decideLoop(task, 1, QStringLiteral("ok"),
                                  QStringLiteral("GOAL_NOT_MET")).repeat);

    // Techo alcanzado → corta aunque no esté cumplido.
    QVERIFY(!TaskStore::decideLoop(task, 3, QStringLiteral("ok"),
                                   QStringLiteral("GOAL_NOT_MET")).repeat);

    // Error en la corrida → corta, no insiste.
    QVERIFY(!TaskStore::decideLoop(task, 1, QStringLiteral("error"),
                                   QStringLiteral("GOAL_NOT_MET")).repeat);
}

void TasksTests::loop_composeGoalPrompt()
{
    QVariantMap task = sampleTask();
    QVERIFY(TaskStore::composeLoopGoalPrompt(task).isEmpty());   // loop apagado

    task["loopEnabled"] = true;
    task["loopGoal"] = QStringLiteral("la pizza está horneada");
    const QString p = TaskStore::composeLoopGoalPrompt(task);
    QVERIFY(p.contains(QStringLiteral("la pizza está horneada")));
    QVERIFY(p.contains(TaskStore::kGoalMetMarker));
    QVERIFY(p.contains(TaskStore::kGoalNotMetMarker));

    // Sin goal → sin prompt aunque esté habilitado.
    task["loopGoal"] = QString();
    QVERIFY(TaskStore::composeLoopGoalPrompt(task).isEmpty());
}

// Simula el driver del bucle de AppController::onAgentTurnFinished usando la
// decisión pura: cuenta cuántas veces correría el cuerpo. `verdictAt(iter)`
// devuelve el veredicto del goal-check tras la iteración `iter` (1-based).
static int simulateLoopBodyRuns(const QVariantMap &task,
                                std::function<QString(int)> verdictAt)
{
    int iteration = 1;               // la 1ª corrida del cuerpo cuenta como iter 1
    int bodyRuns = 1;
    for (;;) {
        const QString verdict = verdictAt(iteration);
        const auto d = TaskStore::decideLoop(task, iteration, QStringLiteral("ok"), verdict);
        if (!d.repeat) break;
        iteration++;
        bodyRuns++;
    }
    return bodyRuns;
}

void TasksTests::loop_runsExactlyMaxIterationsWhenGoalNeverMet()
{
    QVariantMap task = sampleTask();
    task["loopEnabled"] = true;
    task["loopMaxIterations"] = 4;
    const int runs = simulateLoopBodyRuns(task,
        [](int) { return QStringLiteral("GOAL_NOT_MET sigue faltando"); });
    QCOMPARE(runs, 4);   // ni 3 (corte temprano) ni 5 (off-by-one)
}

void TasksTests::loop_stopsEarlyWhenGoalMet()
{
    QVariantMap task = sampleTask();
    task["loopEnabled"] = true;
    task["loopMaxIterations"] = 10;
    // Objetivo recién cumplido tras la 3ª corrida.
    const int runs = simulateLoopBodyRuns(task, [](int iter) {
        return iter >= 3 ? QStringLiteral("GOAL_MET listo")
                         : QStringLiteral("GOAL_NOT_MET");
    });
    QCOMPARE(runs, 3);
}

void TasksTests::loop_composeProgressCarriesPriorVerdict()
{
    // Sin veredicto → sin preámbulo (1ª corrida no arrastra nada).
    QVERIFY(TaskStore::composeLoopProgress(QString(), 0).isEmpty());
    QVERIFY(TaskStore::composeLoopProgress(QStringLiteral("   "), 1).isEmpty());

    // Veredicto típico: marcador en la 1ª línea + evidencia/qué ajustar debajo.
    const QString verdict = QStringLiteral(
        "GOAL_NOT_MET faltó adjuntar el PDF\n"
        "Ya completé el login y llegué al formulario; falta subir el archivo.");
    const QString p = TaskStore::composeLoopProgress(verdict, 2);
    QVERIFY(!p.isEmpty());
    // El marcador NO se arrastra (es ruido de control), la evidencia SÍ.
    QVERIFY(!p.contains(TaskStore::kGoalNotMetMarker));
    QVERIFY(p.contains(QStringLiteral("faltó adjuntar el PDF")));
    QVERIFY(p.contains(QStringLiteral("falta subir el archivo")));
    QVERIFY(p.contains(QStringLiteral("2")));                 // nº de iteraciones hechas
    QVERIFY(p.contains(QStringLiteral("DESDE este estado")));  // instrucción de resume

    // Veredicto que es SÓLO el marcador → no aporta nota → vacío.
    QVERIFY(TaskStore::composeLoopProgress(TaskStore::kGoalNotMetMarker, 1).isEmpty());
}

void TasksTests::verifyProfile_routesOnlyWhenSetAndDifferent()
{
    QVariantMap task = sampleTask();
    // Sin verifyProfileId → no rutea.
    QVERIFY(TaskStore::verifyProfileFor(task, QStringLiteral("exec")).isEmpty());

    task["verifyProfileId"] = QStringLiteral("strong");
    QCOMPARE(TaskStore::verifyProfileFor(task, QStringLiteral("exec")),
             QStringLiteral("strong"));
    // Igual al de ejecución → no hay nada que cambiar.
    QVERIFY(TaskStore::verifyProfileFor(task, QStringLiteral("strong")).isEmpty());

    // Roundtrip persiste el campo.
    task["verifyProfileId"] = QStringLiteral("p-verify");
    task["autoDifficultyRouting"] = true;
    const QVariantMap out = TaskStore::fromJson(TaskStore::toJson(task));
    QCOMPARE(out.value("verifyProfileId").toString(), QStringLiteral("p-verify"));
    QCOMPARE(out.value("autoDifficultyRouting").toBool(), true);
}

void TasksTests::loop_stopsAtOperationalTimeBudget()
{
    QVariantMap task = sampleTask();
    task["loopEnabled"] = true;
    task["loopMaxIterations"] = 100;
    task["loopMaxSeconds"] = 3600;

    QVERIFY(TaskStore::decideLoop(task, 1, QStringLiteral("ok"),
                                  QStringLiteral("GOAL_NOT_MET"), 3599).repeat);
    const auto d = TaskStore::decideLoop(task, 1, QStringLiteral("ok"),
                                         QStringLiteral("GOAL_NOT_MET"), 3600);
    QVERIFY(!d.repeat);
    QVERIFY(d.reason.contains(QStringLiteral("tiempo")));
}

QTEST_MAIN(TasksTests)
#include "test_tasks.moc"
