#include <QtTest>
#include <QImage>
#include <QTemporaryDir>
#include <limits>
#include "core/automation/AutomationArtifactStore.h"
#include "core/automation/AutomationRunner.h"
#include "core/automation/DesktopAutomationBackend.h"
#include "core/automation/DesktopComputerUse.h"
#include "core/automation/DesktopRecoveryPolicy.h"
#include "core/automation/TeachKeyBuffer.h"

class AutomationTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void normalizedCoordinatesScaleAcrossResolutions();
    void normalizedCoordinatesRejectVisualGridAndNonFiniteValues();
    void sensitiveActionClassification();
    void desktopRequiresTraining();
    void limitsAreClamped();
    void autoModeRoutesBySurface();
    void recipeWebStepDetection();
    void desktopPromptPrefersNativeTools();
    void desktopToolPolicyKeepsGuiToolsAvailable();
    void calculatorMismatchRejectsHistoryFalsePositive();
    void desktopWarmStartUsesTeachWithoutHardcodedApps();
    void browserPromptUsesForegroundTeachEvidence();
    void actionTraceSurvivesRecipeAndPrompt();
    void strokePointsSurviveRecipeAndPrompt();
    void datasetParseAndVariableExpansion();
    void assertStepRendersInPrompt();
    void runReportFromToolMessages();
    void hotkeyParseAndTriggers();
    void desktopReplayStepsFiltersMechanical();
    void reanchorPointsToWindowMapsCoords();
    void windowTitleMatchesByAppSuffix();
    void recordedWindowStateSupportsExplicitAndLegacyRecipes();
    void headlessBrowserCommandForcesHeadless();
    void artifactsRoundTripAndRedactSecrets();
    void artifactLearningsAppendAndPrompt();
    void networkDiscoveriesPersistDedupAndReachPrompt();
    void promptDedupsRepeatedLearnings();
    void desktopPromptKeepsVerboseRecipesCompact();
    void keyBufferAccumulatesTextIntoTypeStep();
    void keyBufferFlushesTextBeforeNamedKey();
    void keyBufferEmitsShortcutWithModifiers();
    void winTapDistinguishesLoneTapFromShortcut();
    void desktopComputerUseContractsAndReceipts();
    void desktopActionClassificationIsGeneric();
    void desktopRecoveryPolicyBuildsContracts();
};

void AutomationTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void AutomationTests::normalizedCoordinatesScaleAcrossResolutions()
{
    const QPointF normalized = DesktopAutomationBackend::normalizePoint(
        QPoint(960, 540), QRect(0, 0, 1920, 1080));
    QVERIFY(qAbs(normalized.x() - 0.5) < 0.001);
    QVERIFY(qAbs(normalized.y() - 0.5) < 0.001);
    QCOMPARE(DesktopAutomationBackend::denormalizePoint(
                 normalized, QRect(100, 50, 3840, 2160)),
             QPoint(2020, 1130));
}

void AutomationTests::normalizedCoordinatesRejectVisualGridAndNonFiniteValues()
{
    QVERIFY(DesktopAutomationBackend::isNormalizedPoint(0.0, 0.0));
    QVERIFY(DesktopAutomationBackend::isNormalizedPoint(0.578, 0.674));
    QVERIFY(DesktopAutomationBackend::isNormalizedPoint(1.0, 1.0));
    QVERIFY(!DesktopAutomationBackend::isNormalizedPoint(578.0, 674.0));
    QVERIFY(!DesktopAutomationBackend::isNormalizedPoint(-0.01, 0.5));
    QVERIFY(!DesktopAutomationBackend::isNormalizedPoint(0.5, 1.01));
    QVERIFY(!DesktopAutomationBackend::isNormalizedPoint(
        std::numeric_limits<double>::infinity(), 0.5));
    QVERIFY(!DesktopAutomationBackend::isNormalizedPoint(
        std::numeric_limits<double>::quiet_NaN(), 0.5));

    // Debe fallar antes de consultar la sesión interactiva o mover el cursor.
    QString error;
    QVERIFY(!DesktopAutomationBackend::click(QStringLiteral("screen"), QStringLiteral("0"),
                                              578.0, 674.0, QStringLiteral("left"), &error));
    QVERIFY(error.contains(QStringLiteral("no se ejecutó ningún clic")));
}

void AutomationTests::sensitiveActionClassification()
{
    QVERIFY(AutomationRunner::isSensitiveAction(QStringLiteral("Confirmar pago")));
    QVERIFY(AutomationRunner::isSensitiveAction(QStringLiteral("Enviar formulario")));
    QVERIFY(!AutomationRunner::isSensitiveAction(QStringLiteral("Abrir la calculadora")));
}

void AutomationTests::desktopRequiresTraining()
{
    QVariantMap task{{"executionMode", "desktop"}};
    QVERIFY(AutomationRunner::validateTask(task, false).contains(QStringLiteral("enseñada")));
    task["teachArtifactId"] = QStringLiteral("demo");
    QVERIFY(AutomationRunner::validateTask(task, false).isEmpty());
}

void AutomationTests::limitsAreClamped()
{
    const QVariantMap limits = AutomationRunner::limits(
        QVariantMap{{"timeoutSec", 1}, {"maxActions", 9999}, {"maxRetries", -4}});
    QCOMPARE(limits.value("timeoutSec").toInt(), 30);
    QCOMPARE(limits.value("maxActions").toInt(), 500);
    QCOMPARE(limits.value("maxRetries").toInt(), 0);
}

void AutomationTests::autoModeRoutesBySurface()
{
    using R = AutomationRunner;
    // "auto" sin pasos de escritorio → web (browserBackground).
    QCOMPARE(R::resolveExecutionMode(QVariantMap{{"executionMode", "auto"}}),
             QStringLiteral("browserBackground"));
    QCOMPARE(R::resolveExecutionMode(QVariantMap{
                 {"executionMode", "auto"},
                 {"steps", QVariantList{QVariantMap{{"kind", "browser"}}}}}),
             QStringLiteral("browserBackground"));
    // "auto" con algún paso de escritorio → escritorio.
    QCOMPARE(R::resolveExecutionMode(QVariantMap{
                 {"executionMode", "auto"},
                 {"steps", QVariantList{QVariantMap{{"kind", "browser"}},
                                        QVariantMap{{"kind", "desktop"}}}}}),
             QStringLiteral("desktop"));
    // Modos concretos pasan tal cual.
    QCOMPARE(R::resolveExecutionMode(QVariantMap{{"executionMode", "desktop"}}),
             QStringLiteral("desktop"));
    QCOMPARE(R::resolveExecutionMode(QVariantMap{{"executionMode", "browserBackground"}}),
             QStringLiteral("browserBackground"));
    // Legacy/vacío caen en "auto" (→ web por defecto, sin pasos de escritorio).
    QCOMPARE(R::resolveExecutionMode(QVariantMap{{"executionMode", "agent"}}),
             QStringLiteral("browserBackground"));
    QCOMPARE(R::resolveExecutionMode(QVariantMap{}),
             QStringLiteral("browserBackground"));
}

void AutomationTests::recipeWebStepDetection()
{
    using R = AutomationRunner;
    // Puro escritorio (teclado/UIA) → sin pasos web → no necesita browser/MCP.
    QVERIFY(!R::recipeHasWebStep(QVariantList{
        QVariantMap{{"kind", "key"}}, QVariantMap{{"kind", "type"}},
        QVariantMap{{"kind", "desktop"}}, QVariantMap{{"kind", "click"}}}));
    QVERIFY(!R::recipeHasWebStep(QVariantList{}));
    // Cualquier paso browser/web → sí necesita el MCP de navegador.
    QVERIFY(R::recipeHasWebStep(QVariantList{
        QVariantMap{{"kind", "type"}}, QVariantMap{{"kind", "browser"}}}));
    QVERIFY(R::recipeHasWebStep(QVariantList{QVariantMap{{"kind", "web"}}}));
}

void AutomationTests::desktopPromptPrefersNativeTools()
{
    const QString prompt = AutomationRunner::augmentPrompt(
        QVariantMap{{"executionMode", "desktop"}},
        QVariantMap{{"id", "calc-demo"}},
        QVariantMap{{"steps", QVariantList{
            QVariantMap{{"kind", "key"}, {"intent", "Tecla WIN"}},
    QVariantMap{{"kind", "type"}, {"intent", "Escribir: \"2+2\""}}}}});
    QVERIFY(prompt.contains(QStringLiteral("Superficie: escritorio foreground nativo")));
    QVERIFY(prompt.contains(QStringLiteral("No leas")));
    // Regresión del bug "sumar 2+2": el modelo se colgaba repitiendo
    // desktop_windows/desktop_observe sin nunca escribir. El prompt debe empujar
    // el camino rápido por teclado, verificación por texto y una regla anti-loop.
    QVERIFY(prompt.contains(QStringLiteral("CAMINO RÁPIDO")));
    QVERIFY(prompt.contains(QStringLiteral("TECLADO primero")));
    QVERIFY(prompt.contains(QStringLiteral("desktop_type")));
    QVERIFY(prompt.contains(QStringLiteral("desktop_type \"<expresión>=\"")));
    QVERIFY(prompt.contains(QStringLiteral("desktop_key ESC")));
    QVERIFY(prompt.contains(QStringLiteral("visor ACTUAL")));
    QVERIFY(prompt.contains(QStringLiteral("Historial")));
    QVERIFY(prompt.contains(QStringLiteral("desktop_controls")));
    QVERIFY(prompt.contains(QStringLiteral("ANTI-LOOP")));
    QVERIFY(prompt.contains(QStringLiteral("NO repitas desktop_windows")));
}

void AutomationTests::desktopToolPolicyKeepsGuiToolsAvailable()
{
    const QStringList tools = AutomationRunner::desktopToolNames();
    QVERIFY(tools.contains(QStringLiteral("desktop_observe")));
    QVERIFY(tools.contains(QStringLiteral("desktop_launch")));
    QVERIFY(tools.contains(QStringLiteral("desktop_key")));
    QVERIFY(tools.contains(QStringLiteral("desktop_type")));
    QVERIFY(!tools.contains(QStringLiteral("run_shell")));
}

void AutomationTests::calculatorMismatchRejectsHistoryFalsePositive()
{
    const QVariantMap task{
        {QStringLiteral("name"), QStringLiteral("sumar 2 + 2")},
        {QStringLiteral("description"), QStringLiteral("sumar 2+2 en la calculadora de windows")}};
    const QString badLog = QStringLiteral(
        "[desktop_controls: 3 control(es)]\n"
        "controlId=1 [text] \"Se muestra 6\"\n"
        "controlId=2 [text] \"2 + 2= 4\"\n");
    QString message;
    QVERIFY(AutomationRunner::arithmeticResultMismatch(task, badLog, &message));
    QVERIFY(message.contains(QStringLiteral("visor actual dice 6")));
    QVERIFY(message.contains(QStringLiteral("esperaba 4")));

    const QString goodLog = QStringLiteral("[desktop_controls]\ncontrolId=1 [text] \"Se muestra 4\"\n");
    QVERIFY(!AutomationRunner::arithmeticResultMismatch(task, goodLog));

    // Regresión: el resultado puede venir de la salida de la tarjeta de tool y
    // no del log técnico del backend.
    const QString toolCardEvidence = QStringLiteral(
        "[turn] model requested 1 tool call(s)\n"
        "desktop_controls output: controlId=1 [text] \"Se muestra 4\"");
    QVERIFY(!AutomationRunner::arithmeticResultMismatch(task, toolCardEvidence));

    const QVariantMap otherSum{
        {QStringLiteral("name"), QStringLiteral("sumar 3 + 3")},
        {QStringLiteral("description"), QStringLiteral("sumar 3+3 en la calculadora de windows")}};
    QVERIFY(!AutomationRunner::arithmeticResultMismatch(
        otherSum, QStringLiteral("[desktop_controls]\ncontrolId=1 [text] \"Se muestra 6\"\n")));
    QVERIFY(AutomationRunner::arithmeticResultMismatch(
        otherSum, QStringLiteral("[desktop_controls]\ncontrolId=1 [text] \"Se muestra 4\"\n")));

    QString missingDisplayMessage;
    QVERIFY(AutomationRunner::arithmeticResultMismatch(
        otherSum,
        QStringLiteral("[desktop_controls]\ncontrolId=1 [text] \"3 + 3= 6\"\n"),
        &missingDisplayMessage));
    QVERIFY(missingDisplayMessage.contains(QStringLiteral("falta el visor actual")));

    const QVariantMap product{
        {QStringLiteral("name"), QStringLiteral("multiplicar 3 x 4")},
        {QStringLiteral("description"), QStringLiteral("hacer 3 x 4 en la calculadora")}};
    QVERIFY(!AutomationRunner::arithmeticResultMismatch(
        product, QStringLiteral("[desktop_controls]\ncontrolId=1 [text] \"Se muestra 12\"\n")));
}

void AutomationTests::desktopWarmStartUsesTeachWithoutHardcodedApps()
{
    const QVariantMap recipe{{"steps", QVariantList{
        QVariantMap{{"kind", "start"}, {"atMs", 0}},
        QVariantMap{{"kind", "key"}, {"key", "WIN"}, {"atMs", 100}},
        QVariantMap{{"kind", "type"}, {"text", "cualquier app"}, {"atMs", 300}},
        QVariantMap{{"kind", "key"}, {"key", "ENTER"}, {"atMs", 350}},
        QVariantMap{{"kind", "type"}, {"text", "dato"}, {"atMs", 1200}},
        QVariantMap{{"kind", "key"}, {"key", "ENTER"}, {"atMs", 1250}},
        QVariantMap{{"kind", "click"}, {"atMs", 1400}}}}};
    const QVariantMap normal{{"executionMode", "desktop"}, {"name", "probar app"}};
    const QVariantList allKeyboard = AutomationRunner::safeDesktopWarmStart(normal, recipe);
    QCOMPARE(allKeyboard.size(), 5);
    QCOMPARE(allKeyboard.at(1).toMap().value("text").toString(), QStringLiteral("cualquier app"));

    // El contenido no se reinterpreta: si el usuario enseñó la secuencia, se
    // reproduce igual aunque el texto mencione WhatsApp o enviar un mensaje.
    const QVariantMap sensitive{{"executionMode", "desktop"},
                                {"name", "enviar mensaje por WhatsApp"}};
    QCOMPARE(AutomationRunner::safeDesktopWarmStart(sensitive, recipe).size(), 5);

    QVERIFY(AutomationRunner::safeDesktopWarmStart(
        QVariantMap{{"executionMode", "browserBackground"}}, recipe).isEmpty());
    QVERIFY(AutomationRunner::safeDesktopWarmStart(
        normal, QVariantMap{{"steps", QVariantList{
            QVariantMap{{"kind", "key"}, {"key", "ENTER"}}}}}).isEmpty());
}

void AutomationTests::browserPromptUsesForegroundTeachEvidence()
{
    const QString prompt = AutomationRunner::augmentPrompt(
        QVariantMap{{"executionMode", "browserBackground"}},
        QVariantMap{{"id", "web-demo"}},
        QVariantMap{{"steps", QVariantList{
            QVariantMap{{"kind", "click"}, {"intent", "Click login"},
                        {"x", 0.4}, {"y", 0.2}, {"evidence", "0002.jpg"}}}}});
    QVERIFY(prompt.contains(QStringLiteral("browser foreground de Playwright")));
    QVERIFY(!prompt.contains(QStringLiteral("captura: evidence/0002.jpg")));
    QVERIFY(prompt.contains(QStringLiteral("adaptá selectores")));
}

void AutomationTests::actionTraceSurvivesRecipeAndPrompt()
{
    QVariantMap task{{"id", "trace-demo"}, {"name", "Trace"},
                     {"description", "Clickear Guardar"},
                     {"executionMode", "desktop"}};
    const QVariantMap click{
        {QStringLiteral("kind"), QStringLiteral("click")},
        {QStringLiteral("intent"), QStringLiteral("Click en Guardar")},
        {QStringLiteral("button"), QStringLiteral("left")},
        {QStringLiteral("x"), 0.25},
        {QStringLiteral("y"), 0.75},
        {QStringLiteral("pointer"), QVariantMap{
            {QStringLiteral("button"), QStringLiteral("left")},
            {QStringLiteral("xAbs"), 100},
            {QStringLiteral("yAbs"), 200},
            {QStringLiteral("xNorm"), 0.25},
            {QStringLiteral("yNorm"), 0.75}}},
        {QStringLiteral("target"), QVariantMap{
            {QStringLiteral("surface"), QStringLiteral("desktop")},
            {QStringLiteral("role"), QStringLiteral("button")},
            {QStringLiteral("name"), QStringLiteral("Guardar")}}}};
    const QString id = AutomationArtifactStore::create(
        task, QVariantMap{{"kind", "window"}, {"targetId", "abc"}}, QVariantList{click}, {});
    const QVariantMap stored = AutomationArtifactStore::recipe(id)
                                  .value(QStringLiteral("steps")).toList().first().toMap();
    QCOMPARE(stored.value(QStringLiteral("pointer")).toMap().value(QStringLiteral("xAbs")).toInt(), 100);
    QCOMPARE(stored.value(QStringLiteral("target")).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Guardar"));

    const QString prompt = AutomationRunner::augmentPrompt(
        task, AutomationArtifactStore::manifest(id), AutomationArtifactStore::recipe(id));
    QVERIFY(prompt.contains(QStringLiteral("botón left")));
    QVERIFY(prompt.contains(QStringLiteral("target button \"Guardar\"")));
    QVERIFY(prompt.contains(QStringLiteral("trace con pointer/target")));
    QDir(AutomationArtifactStore::artifactDir(id)).removeRecursively();
}

void AutomationTests::strokePointsSurviveRecipeAndPrompt()
{
    // Un trazo (dibujar en Paint) se graba como paso [stroke] con su lista de
    // puntos normalizados; deben sobrevivir el round-trip del artefacto y salir
    // en el prompt como points= para que el modelo los pase a desktop_stroke.
    QVariantMap task{{"id", "stroke-demo"}, {"name", "Dibujar"},
                     {"description", "Pintar una línea"},
                     {"executionMode", "desktop"}};
    QVariantList points;
    for (int i = 0; i <= 4; ++i)
        points << QVariantMap{{"x", 0.1 * i}, {"y", 0.2 + 0.1 * i}};
    const QVariantMap stroke{
        {QStringLiteral("kind"), QStringLiteral("stroke")},
        {QStringLiteral("intent"), QStringLiteral("Arrastrar con botón left (traza de 5 puntos)")},
        {QStringLiteral("button"), QStringLiteral("left")},
        {QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.2},
        {QStringLiteral("points"), points},
        {QStringLiteral("target"), QVariantMap{
             {QStringLiteral("windowLabel"), QStringLiteral("Sin título - Paint")},
             {QStringLiteral("winWidth"), 1200}, {QStringLiteral("winHeight"), 800},
             {QStringLiteral("windowMaximized"), false}}}};
    const QString id = AutomationArtifactStore::create(
        task, QVariantMap{{"kind", "window"}, {"targetId", "abc"}}, QVariantList{stroke}, {});
    const QVariantMap stored = AutomationArtifactStore::recipe(id)
                                  .value(QStringLiteral("steps")).toList().first().toMap();
    QCOMPARE(stored.value(QStringLiteral("points")).toList().size(), 5);

    const QString prompt = AutomationRunner::augmentPrompt(
        task, AutomationArtifactStore::manifest(id), AutomationArtifactStore::recipe(id));
    QVERIFY(prompt.contains(QStringLiteral("[stroke]")));
    QVERIFY(prompt.contains(QStringLiteral("points=")));
    QVERIFY(prompt.contains(QStringLiteral("desktop_stroke")));
    QVERIFY(prompt.contains(QStringLiteral("desktop_resize")));
    QVERIFY(prompt.contains(QStringLiteral("tamaño Teach 1200x800")));
    QVERIFY(prompt.contains(QStringLiteral("maximizada=no")));
    QVERIFY(prompt.contains(QStringLiteral("[0.400,0.600]")));   // último punto
    // El prompt de escritorio ofrece la sincronización por condición.
    QVERIFY(prompt.contains(QStringLiteral("desktop_wait_for")));
    QDir(AutomationArtifactStore::artifactDir(id)).removeRecursively();
}

void AutomationTests::assertStepRendersInPrompt()
{
    // Un paso [assert] grabado en Teach debe salir en el prompt con su texto
    // esperado y la instrucción de reproducirlo con desktop_assert.
    QVariantMap task{{"id", "assert-demo"}, {"name", "Verif"},
                     {"description", "Confirmar total"},
                     {"executionMode", "desktop"}};
    const QVariantMap assertStep{
        {QStringLiteral("kind"), QStringLiteral("assert")},
        {QStringLiteral("intent"), QStringLiteral("Verificar que aparezca: \"TOTAL 42\"")},
        {QStringLiteral("expectText"), QStringLiteral("TOTAL 42")}};
    const QString id = AutomationArtifactStore::create(
        task, QVariantMap{{"kind", "window"}, {"targetId", "abc"}}, QVariantList{assertStep}, {});

    const QString prompt = AutomationRunner::augmentPrompt(
        task, AutomationArtifactStore::manifest(id), AutomationArtifactStore::recipe(id));
    QVERIFY(prompt.contains(QStringLiteral("[assert]")));
    QVERIFY(prompt.contains(QStringLiteral("desktop_assert")));
    QVERIFY(prompt.contains(QStringLiteral("TOTAL 42")));
    QDir(AutomationArtifactStore::artifactDir(id)).removeRecursively();
}

void AutomationTests::windowTitleMatchesByAppSuffix()
{
    using R = AutomationRunner;
    QVERIFY(R::windowTitleMatches("Sin título - Paint", "Sin título - Paint"));   // exacto
    QVERIFY(R::windowTitleMatches("Sin título - Paint", "Sin guardar - Paint"));  // mismo app
    QVERIFY(R::windowTitleMatches("doc1.txt: Bloc de notas", "doc2.txt: Bloc de notas"));
    QVERIFY(!R::windowTitleMatches("Sin título - Paint", "Documento - Word"));    // otra app
    QVERIFY(!R::windowTitleMatches("", "Paint"));                                 // vacío
}

void AutomationTests::recordedWindowStateSupportsExplicitAndLegacyRecipes()
{
    using R = AutomationRunner;
    const QVariantMap scope{{"x", 0}, {"y", 0}, {"width", 2560}, {"height", 1440}};
    QVariantMap explicitRestored{{"windowMaximized", false}, {"winWidth", 2500}, {"winHeight", 1400}};
    QVariantMap state = R::recordedWindowState(explicitRestored, scope);
    QVERIFY(state.value("known").toBool());
    QVERIFY(!state.value("maximized").toBool());
    QCOMPARE(state.value("width").toInt(), 2500);
    QCOMPARE(state.value("height").toInt(), 1400);

    state = R::recordedWindowState(
        QVariantMap{{"winWidth", 2576}, {"winHeight", 1408}}, scope);
    QVERIFY(state.value("known").toBool());
    QVERIFY(state.value("maximized").toBool());

    state = R::recordedWindowState(
        QVariantMap{{"winWidth", 900}, {"winHeight", 700}}, scope);
    QVERIFY(state.value("known").toBool());
    QVERIFY(!state.value("maximized").toBool());
    QCOMPARE(state.value("width").toInt(), 900);
    QCOMPARE(state.value("height").toInt(), 700);

    state = R::recordedWindowState(QVariantMap{}, scope);
    QVERIFY(!state.value("known").toBool());
}

void AutomationTests::reanchorPointsToWindowMapsCoords()
{
    // Alcance = pantalla 1000x1000. Ventana grabada en (200,200) de 400x400.
    // Un punto al centro de pantalla (0.5,0.5 → abs 500,500) cae en la fracción
    // (500-200)/400 = 0.75 dentro de la ventana → así el replay lo denormaliza
    // contra la ventana ACTUAL (esté donde esté).
    const QVariantMap scope{{"x", 0}, {"y", 0}, {"width", 1000}, {"height", 1000}};
    const QVariantMap win{{"x", 200}, {"y", 200}, {"width", 400}, {"height", 400}};
    const QVariantList pts{QVariantMap{{"x", 0.5}, {"y", 0.5}}};
    const QVariantList out = AutomationRunner::reanchorPointsToWindow(pts, scope, win);
    QCOMPARE(out.size(), 1);
    QCOMPARE(out.first().toMap().value("x").toDouble(), 0.75);
    QCOMPARE(out.first().toMap().value("y").toDouble(), 0.75);
    // Datos incompletos (win sin tamaño) → devuelve los puntos sin cambio.
    const QVariantList same = AutomationRunner::reanchorPointsToWindow(
        pts, scope, QVariantMap{{"x", 0}, {"y", 0}});
    QCOMPARE(same.first().toMap().value("x").toDouble(), 0.5);
}

void AutomationTests::desktopReplayStepsFiltersMechanical()
{
    // desktopReplaySteps deja sólo key/type/click/stroke en orden (descarta start/
    // verification/note) para la reproducción fiel determinista.
    const QVariantMap recipe{{"steps", QVariantList{
        QVariantMap{{"kind", "start"}},
        QVariantMap{{"kind", "key"}, {"key", "WIN"}, {"atMs", 10}},
        QVariantMap{{"kind", "type"}, {"text", "paint"}, {"atMs", 200}},
        QVariantMap{{"kind", "stroke"}, {"atMs", 900}, {"button", "left"},
                    {"points", QVariantList{QVariantMap{{"x", 0.1}, {"y", 0.1}},
                                            QVariantMap{{"x", 0.5}, {"y", 0.5}}}}},
        QVariantMap{{"kind", "verification"}}}}};
    const QVariantList out = AutomationRunner::desktopReplaySteps(recipe);
    QCOMPARE(out.size(), 3);   // key, type, stroke (sin start/verification)
    QCOMPARE(out.at(0).toMap().value("kind").toString(), QStringLiteral("key"));
    QCOMPARE(out.at(2).toMap().value("kind").toString(), QStringLiteral("stroke"));
    QCOMPARE(out.at(2).toMap().value("points").toList().size(), 2);
    // Sin pasos mecánicos → vacío (el caller cae al replay adaptativo).
    QVERIFY(AutomationRunner::desktopReplaySteps(
        QVariantMap{{"steps", QVariantList{QVariantMap{{"kind", "verification"}}}}}).isEmpty());
}

void AutomationTests::hotkeyParseAndTriggers()
{
    using R = AutomationRunner;
    // Parse válido: modificador(es) + tecla.
    const QVariantMap ok = R::parseHotkey(QStringLiteral("ctrl+alt+R"));
    QVERIFY(ok.value("valid").toBool());
    QCOMPARE(ok.value("key").toString(), QStringLiteral("R"));
    QCOMPARE(ok.value("mods").toStringList(), (QStringList{"CTRL", "ALT"}));
    // F-key con Win.
    QVERIFY(R::parseHotkey(QStringLiteral("WIN+F5")).value("valid").toBool());
    // Inválidos: sin modificador, sin tecla, tecla basura.
    QVERIFY(!R::parseHotkey(QStringLiteral("R")).value("valid").toBool());
    QVERIFY(!R::parseHotkey(QStringLiteral("CTRL")).value("valid").toBool());
    QVERIFY(!R::parseHotkey(QStringLiteral("CTRL+F99")).value("valid").toBool());

    // hotkeyTriggers: filtra por triggerType + hotkey válido.
    const QVariantList tasks{
        QVariantMap{{"id", "a"}, {"triggerType", "hotkey"}, {"triggerHotkey", "CTRL+ALT+R"}},
        QVariantMap{{"id", "b"}, {"triggerType", "hotkey"}, {"triggerHotkey", "sinmods"}},  // inválido
        QVariantMap{{"id", "c"}, {"triggerType", "manual"}}};
    const QVariantList trg = R::hotkeyTriggers(tasks);
    QCOMPARE(trg.size(), 1);
    QCOMPARE(trg.first().toMap().value("id").toString(), QStringLiteral("a"));
}

void AutomationTests::runReportFromToolMessages()
{
    // Del hilo de mensajes del agente, buildRunReport queda sólo con los toolcall
    // y marca ok=false cuando el output trae un marcador de error.
    const QVariantList msgs{
        QVariantMap{{"role", "assistant"}, {"content", "voy a abrir"}},
        QVariantMap{{"role", "toolcall"}, {"name", "desktop_launch"}, {"output", "[desktop_launch: ok]"}},
        QVariantMap{{"role", "toolcall"}, {"name", "desktop_assert"},
                    {"output", "[desktop_assert: FAIL] el texto no apareció"}},
    };
    const QVariantList rep = AutomationRunner::buildRunReport(msgs);
    QCOMPARE(rep.size(), 2);   // sólo los 2 toolcall
    QCOMPARE(rep.at(0).toMap().value("tool").toString(), QStringLiteral("desktop_launch"));
    QCOMPARE(rep.at(0).toMap().value("ok").toBool(), true);
    QCOMPARE(rep.at(1).toMap().value("ok").toBool(), false);   // FAIL → ok=false
    QCOMPARE(rep.at(1).toMap().value("n").toInt(), 2);
}

void AutomationTests::datasetParseAndVariableExpansion()
{
    using R = AutomationRunner;
    // CSV con encabezados + comillas dobles con coma interna.
    const QVariantList csv = R::parseDataset(
        QStringLiteral("nombre,ciudad\nAna,\"La Plata, BA\"\nBeto,Rosario"));
    QCOMPARE(csv.size(), 2);
    QCOMPARE(csv.at(0).toMap().value("nombre").toString(), QStringLiteral("Ana"));
    QCOMPARE(csv.at(0).toMap().value("ciudad").toString(), QStringLiteral("La Plata, BA"));

    // JSON autodetectado (empieza con '[').
    const QVariantList json = R::parseDataset(
        QStringLiteral("[{\"a\":\"1\",\"b\":\"x\"},{\"a\":\"2\",\"b\":\"y\"}]"));
    QCOMPARE(json.size(), 2);
    QCOMPARE(json.at(1).toMap().value("a").toString(), QStringLiteral("2"));

    // Sustitución {{var}}: case-insensitive, con espacios, y clave ausente intacta.
    const QVariantMap row{{"nombre", "Ana"}, {"Edad", "30"}};
    QCOMPARE(R::expandVariables(QStringLiteral("Hola {{nombre}}, {{ EDAD }} años {{falta}}"), row),
             QStringLiteral("Hola Ana, 30 años {{falta}}"));

    // datasetRows resuelve desde datasetInline + format.
    const QVariantMap task{{"datasetInline", "k\nv1\nv2\nv3"}, {"datasetFormat", "csv"}};
    QCOMPARE(R::datasetRows(task).size(), 3);
    // Sin dataset → vacío.
    QVERIFY(R::datasetRows(QVariantMap{{"name", "x"}}).isEmpty());
}

void AutomationTests::headlessBrowserCommandForcesHeadless()
{
    using R = AutomationRunner;
    // El MCP de Playwright por defecto se fuerza headless.
    QCOMPARE(R::headlessBrowserCommand(QStringLiteral("npx @playwright/mcp@latest")),
             QStringLiteral("npx @playwright/mcp@latest --headless"));
    // Si ya hay un flag explícito, se respeta (no se duplica).
    QCOMPARE(R::headlessBrowserCommand(QStringLiteral("npx @playwright/mcp@latest --headless")),
             QStringLiteral("npx @playwright/mcp@latest --headless"));
    QCOMPARE(R::headlessBrowserCommand(QStringLiteral("npx @playwright/mcp@latest --headed")),
             QStringLiteral("npx @playwright/mcp@latest --headed"));
    // MCP de terceros no se toca.
    QCOMPARE(R::headlessBrowserCommand(QStringLiteral("node mi-mcp.js")),
             QStringLiteral("node mi-mcp.js"));
    QCOMPARE(R::foregroundBrowserCommand(QStringLiteral("npx @playwright/mcp@latest")),
             QStringLiteral("npx @playwright/mcp@latest --headed"));
    QCOMPARE(R::foregroundBrowserCommand(QStringLiteral("npx @playwright/mcp@latest --headless")),
             QStringLiteral("npx @playwright/mcp@latest --headed"));
    QCOMPARE(R::foregroundBrowserCommand(QStringLiteral("node mi-mcp.js")),
             QStringLiteral("node mi-mcp.js"));
}

void AutomationTests::artifactsRoundTripAndRedactSecrets()
{
    QVariantMap task{{"id", "demo-automation"}, {"name", "Demo"},
                     {"description", "Completar formulario"},
                     {"executionMode", "desktop"}, {"approvalPolicy", "sensitive"}};
    const QString templateDir = AutomationArtifactStore::artifactDir(QStringLiteral("demo-automation"))
                                + QStringLiteral("/templates");
    QDir().mkpath(templateDir);
    QImage needle(24, 24, QImage::Format_RGB32);
    needle.fill(Qt::red);
    QVERIFY(needle.save(templateDir + QStringLiteral("/template-0001.png")));
    const QVariantList events{
        QVariantMap{{"kind", "note"}, {"text", "password=secreto"},
                    {"intent", "Ingresar datos"}},
        QVariantMap{{"kind", "click"}, {"locators", QVariantList{QVariantMap{
                        {"type", "image"}, {"file", "templates/template-0001.png"},
                        {"width", 24}, {"height", 24}, {"threshold", 0.88}}}}},
        QVariantMap{{"kind", "verification"}, {"intent", "Verificar resultado"},
                    {"evidence", "final.jpg"}}};
    const QString id = AutomationArtifactStore::create(
        task, QVariantMap{{"kind", "screen"}, {"targetId", "0"}}, events, {});
    QCOMPARE(id, QStringLiteral("demo-automation"));
    QCOMPARE(AutomationArtifactStore::manifest(id).value("formatVersion").toInt(),
             AutomationArtifactStore::FormatVersion);
    const QVariantList timeline = AutomationArtifactStore::timeline(id);
    QCOMPARE(timeline.size(), 3);
    QVERIFY(timeline.first().toMap().value("text").toString().contains(QStringLiteral("[REDACTED]")));
    const QString redactedJson = AutomationArtifactStore::redact(
        QStringLiteral("{\"password\":\"secreto\",\"value\":1}"));
    QVERIFY(!redactedJson.contains(QStringLiteral("secreto")));
    QVERIFY(redactedJson.contains(QStringLiteral("[REDACTED]")));
    QCOMPARE(AutomationArtifactStore::recipe(id).value("finalReference").toString(),
             QStringLiteral("final.jpg"));
    QCOMPARE(AutomationArtifactStore::templates(id).size(), 1);
    QTemporaryDir replacements;
    QImage replacement(30, 18, QImage::Format_RGB32);
    replacement.fill(Qt::blue);
    const QString replacementPath = replacements.filePath(QStringLiteral("replacement.png"));
    QVERIFY(replacement.save(replacementPath));
    QVERIFY(AutomationArtifactStore::replaceTemplate(
        id, QStringLiteral("template-0001.png"), replacementPath));
    QVariantMap updated = AutomationArtifactStore::templates(id).first().toMap();
    QCOMPARE(updated.value("width").toInt(), 30);
    QCOMPARE(updated.value("height").toInt(), 18);
    QVERIFY(!updated.value("sha256").toString().isEmpty());
    const QVariantList updatedLocators = AutomationArtifactStore::timeline(id).at(1).toMap()
                                              .value("locators").toList();
    QCOMPARE(updatedLocators.first().toMap().value("width").toInt(), 30);
    QVERIFY(AutomationArtifactStore::addTemplateVariant(
        id, QStringLiteral("template-0001.png"), replacementPath));
    QCOMPARE(AutomationArtifactStore::templates(id).size(), 2);
    QVERIFY(AutomationArtifactStore::removeTemplate(id, QStringLiteral("template-0001.png")));
    QVERIFY(AutomationArtifactStore::templates(id).isEmpty());
    // Eliminar el primario limpia variantes y localizadores para no dejar rutas huérfanas.
    QVERIFY(AutomationArtifactStore::timeline(id).at(1).toMap()
                .value("locators").toList().isEmpty());
    QDir(AutomationArtifactStore::artifactDir(id)).removeRecursively();
}

void AutomationTests::artifactLearningsAppendAndPrompt()
{
    QVariantMap task{{"id", "learn-demo"}, {"name", "Demo"},
                     {"description", "Completar login"},
                     {"executionMode", "browserBackground"}};
    const QString id = AutomationArtifactStore::create(
        task, QVariantMap{{"kind", "screen"}, {"targetId", "0"}},
        QVariantList{QVariantMap{{"kind", "browser"}, {"intent", "Abrir login"}}}, {});
    QCOMPARE(id, QStringLiteral("learn-demo"));
    QVERIFY(AutomationArtifactStore::appendLearning(
        id,
        QStringLiteral("La interfaz movió Login a Acceder y se completó con el selector nuevo."),
        QStringLiteral("[tool] mcp__playwright__browser_click ok")));
    const QVariantList learnings = AutomationArtifactStore::recipe(id)
                                       .value(QStringLiteral("learnings")).toList();
    QCOMPARE(learnings.size(), 1);

    // Dedup: partiendo del learning previo (login), un resumen de éxito nuevo
    // distinto se guarda (→2); su repetición casi idéntica (sólo cambian números/
    // emoji/puntuación) NO apila otro (→sigue 2); una adaptación con texto
    // distinto SÍ (→3).
    QVERIFY(AutomationArtifactStore::appendLearning(
        id, QStringLiteral("¡Tarea completada! La calculadora muestra 2+2 = 4."),
        QStringLiteral("[tool] desktop_type ok")));
    QCOMPARE(AutomationArtifactStore::recipe(id)
                 .value(QStringLiteral("learnings")).toList().size(), 2);   // distinto del login → guarda
    QVERIFY(AutomationArtifactStore::appendLearning(
        id, QStringLiteral("Tarea completada :) la calculadora muestra 5+5 = 10"),
        QStringLiteral("[tool] desktop_type ok")));
    QCOMPARE(AutomationArtifactStore::recipe(id)
                 .value(QStringLiteral("learnings")).toList().size(), 2);   // firma igual → sin duplicar
    QVERIFY(AutomationArtifactStore::appendLearning(
        id, QStringLiteral("La UI movió el visor a otro control; leí el resultado por desktop_controls."),
        QStringLiteral("[tool] desktop_controls ok")));
    QCOMPARE(AutomationArtifactStore::recipe(id)
                 .value(QStringLiteral("learnings")).toList().size(), 3);   // texto distinto → sí guarda
    const QString prompt = AutomationRunner::augmentPrompt(
        QVariantMap{{"executionMode", "browserBackground"}},
        AutomationArtifactStore::manifest(id),
        AutomationArtifactStore::recipe(id));
    QVERIFY(prompt.contains(QStringLiteral("Aprendizajes auto-actualizados")));
    QVERIFY(prompt.contains(QStringLiteral("desktop_controls")));
    QDir(AutomationArtifactStore::artifactDir(id)).removeRecursively();
}

void AutomationTests::desktopComputerUseContractsAndReceipts()
{
    using namespace DesktopComputerUse;
    const QVariantMap target{{QStringLiteral("id"), QStringLiteral("w1")},
                             {QStringLiteral("label"), QStringLiteral("Editor")},
                             {QStringLiteral("pid"), 42},
                             {QStringLiteral("width"), 800},
                             {QStringLiteral("height"), 600}};
    const QVariantList controls{QVariantMap{{QStringLiteral("controlId"), QStringLiteral("c1")},
                                             {QStringLiteral("name"), QStringLiteral("Guardar")},
                                             {QStringLiteral("role"), QStringLiteral("button")}}};
    const QString first = snapshotId(QStringLiteral("window"), QStringLiteral("w1"),
                                     target, {}, controls);
    const QString second = snapshotId(QStringLiteral("window"), QStringLiteral("w1"),
                                      target, {}, controls);
    QCOMPARE(first, second);
    QVERIFY(!first.isEmpty());
    QString error;
    QVERIFY(snapshotMatches(first, second, &error));
    QVERIFY(!snapshotMatches(first, QStringLiteral("stale"), &error));
    QVERIFY(error.contains(QStringLiteral("obsoleto")));

    const ActionReceipt receipt = makeReceipt(
        QStringLiteral("desktop_control_action"),
        QJsonObject{{QStringLiteral("action"), QStringLiteral("set_value")},
                    {QStringLiteral("value"), QStringLiteral("password=secret")}},
        true, QStringLiteral("password=secret"), QStringLiteral("session"),
        QStringLiteral("corr"), QStringLiteral("uia-pattern"), first, target);
    const QVariantMap serialized = receipt.toVariantMap();
    QCOMPARE(serialized.value(QStringLiteral("status")).toString(), QStringLiteral("settled"));
    QVERIFY(serialized.value(QStringLiteral("receiptId")).toString().size() > 10);
    QVERIFY(!serialized.value(QStringLiteral("payloadHash")).toString().isEmpty());
    QVERIFY(serialized.value(QStringLiteral("detail")).toString().contains(QStringLiteral("[REDACTED]")));
}

void AutomationTests::desktopActionClassificationIsGeneric()
{
    using namespace DesktopComputerUse;
    QVERIFY(isDesktopTool(QStringLiteral("desktop_snapshot")));
    QVERIFY(isDesktopReadTool(QStringLiteral("desktop_observe")));
    QVERIFY(!isDesktopActionTool(QStringLiteral("desktop_observe")));
    QVERIFY(isDesktopActionTool(QStringLiteral("desktop_control_action")));
    QVERIFY(isSensitiveTarget(QVariantMap{{QStringLiteral("password"), true}}));
    QVERIFY(isSensitiveTarget(QVariantMap{{QStringLiteral("role"), QStringLiteral("edit")},
                                         {QStringLiteral("name"), QStringLiteral("Token")}}));
    QVERIFY(isDestructiveLabel(QStringLiteral("Eliminar elemento")));
    QVERIFY(!isDestructiveLabel(QStringLiteral("Guardar cambios")));
}

void AutomationTests::desktopRecoveryPolicyBuildsContracts()
{
    const QStringList order = DesktopRecoveryPolicy::strategies(QStringLiteral("template"));
    QCOMPARE(order.first(), QStringLiteral("template"));
    QCOMPARE(order.size(), 4);
    QVERIFY(DesktopRecoveryPolicy::shouldReobserve(QStringLiteral("snapshot obsoleto")));
    QVERIFY(DesktopRecoveryPolicy::shouldReobserve(QStringLiteral("resultado ambiguo")));
    QVERIFY(!DesktopRecoveryPolicy::shouldReobserve(QStringLiteral("timeout de espera")));
    const QVariantMap contract = DesktopRecoveryPolicy::contractForStep(
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("click")},
                    {QStringLiteral("intent"), QStringLiteral("Activar opción")}});
    QVERIFY(contract.contains(QStringLiteral("precondition")));
    QVERIFY(contract.contains(QStringLiteral("postcondition")));
    const QVariantMap repair = contract.value(QStringLiteral("repair")).toMap();
    QCOMPARE(repair.value(QStringLiteral("maxAttempts")).toInt(), 2);
    QCOMPARE(repair.value(QStringLiteral("strategies")).toList().size(), 4);
}

void AutomationTests::networkDiscoveriesPersistDedupAndReachPrompt()
{
    const QVariantMap task{{"id", "network-discovery-test"},
                           {"name", "Descubrir contrato"},
                           {"description", "Crear un elemento"},
                           {"executionMode", "browserBackground"},
                           {"discoverNetwork", true}};
    const QString id = AutomationArtifactStore::create(
        task, QVariantMap{{"kind", "browser"}}, QVariantList{}, QStringList{});
    QVERIFY(!id.isEmpty());
    const QVariantMap evidence{
        {"kind", "browser_network_evidence"},
        {"endpoints", QVariantList{
             QVariantMap{{"method", "POST"},
                         {"origin", "https://api.example.com"},
                         {"pathTemplate", "/v1/items/{id}"},
                         {"count", 2},
                         {"statuses", QVariantList{201, 202}}}}},
        {"privacy", QVariantMap{{"queryValuesRetained", false},
                                {"headersRetained", false},
                                {"bodiesRetained", false}}}};
    QVERIFY(AutomationArtifactStore::appendNetworkDiscovery(
        id, evidence, QStringLiteral("Click en Crear token=SECRETO")));
    QVERIFY(AutomationArtifactStore::appendNetworkDiscovery(
        id, evidence, QStringLiteral("Click repetido")));
    const QVariantList discoveries = AutomationArtifactStore::networkDiscoveries(id);
    QCOMPARE(discoveries.size(), 1);
    QVERIFY(!discoveries.first().toMap().value("action").toString().contains("SECRETO"));
    QCOMPARE(discoveries.first().toMap().value("reviewStatus").toString(),
             QStringLiteral("pending"));
    const QString signature = discoveries.first().toMap().value("signature").toString();
    QVERIFY(AutomationArtifactStore::setNetworkDiscoveryReview(
        id, signature, QStringLiteral("approved")));
    QCOMPARE(AutomationArtifactStore::networkDiscoveries(id).first().toMap()
                 .value("reviewStatus").toString(), QStringLiteral("approved"));
    QVERIFY(!AutomationArtifactStore::setNetworkDiscoveryReview(
        id, signature, QStringLiteral("unsafe-status")));

    const QString prompt = AutomationRunner::augmentPrompt(
        task, AutomationArtifactStore::manifest(id), AutomationArtifactStore::recipe(id));
    QVERIFY(prompt.contains(QStringLiteral("browser_network_discover")));
    QVERIFY(prompt.contains(QStringLiteral("POST https://api.example.com/v1/items/{id}")));
    QVERIFY(prompt.contains(QStringLiteral("no autorización")));
    QVERIFY(AutomationArtifactStore::clearNetworkDiscoveries(id));
    QVERIFY(AutomationArtifactStore::networkDiscoveries(id).isEmpty());
    QDir(AutomationArtifactStore::artifactDir(id)).removeRecursively();
}

void AutomationTests::promptDedupsRepeatedLearnings()
{
    // Artefacto viejo con 3 learnings de éxito casi idénticos (misma firma
    // normalizada, sólo cambian números/emoji). augmentPrompt debe colapsarlos a
    // UN bullet (el más reciente), no reinyectar el mismo texto 3 veces.
    QVariantMap recipe;
    recipe[QStringLiteral("steps")] = QVariantList{};
    recipe[QStringLiteral("learnings")] = QVariantList{
        QVariantMap{{QStringLiteral("summary"), QStringLiteral("Tarea completada: 2+2 = 4.")}},
        QVariantMap{{QStringLiteral("summary"), QStringLiteral("Tarea completada :) 3+3 = 6")}},
        QVariantMap{{QStringLiteral("summary"), QStringLiteral("Tarea completada! 9+9 = 18")}}};
    const QString prompt = AutomationRunner::augmentPrompt(
        QVariantMap{{"executionMode", "desktop"}}, QVariantMap{{"id", "x"}}, recipe);
    QCOMPARE(prompt.count(QStringLiteral("Aprendizajes auto-actualizados")), 1);
    QCOMPARE(prompt.count(QStringLiteral("- Tarea completada")), 1);   // un solo bullet
    QVERIFY(prompt.contains(QStringLiteral("9+9 = 18")));              // el más reciente
}

void AutomationTests::desktopPromptKeepsVerboseRecipesCompact()
{
    QVariantList steps;
    for (int i = 0; i < 24; ++i) {
        steps << QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("type")},
            {QStringLiteral("intent"), QStringLiteral("Paso verbose %1: escribir texto largo para simular una receta enseñada con mucha evidencia y notas repetidas que no deben inflar el prompt.").arg(i)},
            {QStringLiteral("evidence"), QStringLiteral("%1.jpg").arg(i, 4, 10, QLatin1Char('0'))}};
    }
    const QString longSummary = QStringLiteral(
        "Task ejecutada correctamente. Resultado: la calculadora muestra Se muestra 4. "
        "Resumen largo con pasos, tool calls, ids de ventana y explicación repetida para simular "
        "aprendizajes acumulados en recipe.json que antes empujaban perfiles 8k fuera de contexto. ");
    QVariantList learnings;
    for (int i = 0; i < 8; ++i)
        learnings << QVariantMap{{QStringLiteral("summary"), longSummary.repeated(8)}};

    const QString prompt = AutomationRunner::augmentPrompt(
        QVariantMap{{"executionMode", "desktop"}},
        QVariantMap{{"id", "sumar-2-2"}},
        QVariantMap{{"steps", steps}, {"learnings", learnings}});

    QVERIFY(prompt.contains(QStringLiteral("CAMINO RÁPIDO")));
    QVERIFY(prompt.contains(QStringLiteral("Aprendizajes auto-actualizados")));
    QVERIFY(!prompt.contains(QStringLiteral("captura: evidence/")));
    QVERIFY2(prompt.size() < 7000, qPrintable(QStringLiteral("prompt chars=%1").arg(prompt.size())));
}

void AutomationTests::keyBufferAccumulatesTextIntoTypeStep()
{
    TeachKeyBuffer kb;
    for (QChar c : QStringLiteral("2 + 2")) kb.feedChar(c);
    QVERIFY(kb.hasPending());
    const QVariantList steps = kb.flush();
    QCOMPARE(steps.size(), 1);
    const QVariantMap step = steps.first().toMap();
    QCOMPARE(step.value("kind").toString(), QStringLiteral("type"));
    QCOMPARE(step.value("text").toString(), QStringLiteral("2 + 2"));
    QVERIFY(step.value("intent").toString().contains(QStringLiteral("2 + 2")));
    // El buffer queda vacío tras el flush → no re-emite.
    QVERIFY(!kb.hasPending());
    QVERIFY(kb.flush().isEmpty());
}

void AutomationTests::keyBufferFlushesTextBeforeNamedKey()
{
    TeachKeyBuffer kb;
    for (QChar c : QStringLiteral("hola")) kb.feedChar(c);
    const QVariantList steps = kb.feedKey(QStringLiteral("ENTER"));
    // Orden real: primero el texto tipeado, después la tecla.
    QCOMPARE(steps.size(), 2);
    QCOMPARE(steps.at(0).toMap().value("kind").toString(), QStringLiteral("type"));
    QCOMPARE(steps.at(0).toMap().value("text").toString(), QStringLiteral("hola"));
    QCOMPARE(steps.at(1).toMap().value("kind").toString(), QStringLiteral("key"));
    QCOMPARE(steps.at(1).toMap().value("key").toString(), QStringLiteral("ENTER"));
    // Sin texto pendiente, feedKey emite sólo el paso de tecla.
    const QVariantList only = kb.feedKey(QStringLiteral("TAB"));
    QCOMPARE(only.size(), 1);
    QCOMPARE(only.first().toMap().value("key").toString(), QStringLiteral("TAB"));
}

void AutomationTests::keyBufferEmitsShortcutWithModifiers()
{
    TeachKeyBuffer kb;
    const QVariantList steps = kb.feedKey(QStringLiteral("R"),
                                          QStringList{QStringLiteral("WIN")});
    QCOMPARE(steps.size(), 1);
    const QVariantMap step = steps.first().toMap();
    QCOMPARE(step.value("kind").toString(), QStringLiteral("key"));
    QCOMPARE(step.value("key").toString(), QStringLiteral("R"));
    QCOMPARE(step.value("modifiers").toStringList(), QStringList{QStringLiteral("WIN")});
    QCOMPARE(step.value("intent").toString(), QStringLiteral("Tecla WIN+R"));
}

void AutomationTests::winTapDistinguishesLoneTapFromShortcut()
{
    WinTapTracker t;
    // Tap solo: down → up sin combo → es tap (abre menú Inicio).
    t.down();
    QVERIFY(t.up());
    // up sin down previo → no es tap.
    QVERIFY(!t.up());
    // Win+R: down → markCombo (al pulsar R) → up NO es tap solo.
    t.down();
    t.markCombo();
    QVERIFY(!t.up());
    // Tras un combo, el estado se reinicia: un nuevo tap solo vuelve a contar.
    t.down();
    QVERIFY(t.up());
    // markCombo sin down no arma nada → el próximo down/up es tap limpio.
    t.markCombo();
    t.down();
    QVERIFY(t.up());
}

QTEST_MAIN(AutomationTests)
#include "test_automation.moc"
