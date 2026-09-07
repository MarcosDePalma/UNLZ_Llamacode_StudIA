// Unit tests de EvalSuite: loader desde JSON (string y archivo), categorías
// únicas en orden, manejo de JSON inválido. Reusa el sample real del repo.

#include <QtTest>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSet>
#include "AppController.h"
#include "core/eval/EvalSuite.h"
#include "core/eval/BenchmarkPack.h"

class EvalTests : public QObject
{
    Q_OBJECT
private slots:
    void loadFromJson_parsesTasks();
    void categories_uniqueInOrder();
    void invalidJson_returnsEmptyWithError();
    void loadFromFile_roundTrip();
    void reasoningBiasSuite_isValid();
    void snakeSuite_isValid();
    void agentAcceptance_scoresGeneratedFiles();
    void benchPack_extractsAnswersFromRealModelOutput();
    void benchPack_importsPublicFormats();
    void benchPack_runsCodeTestsWithTimeout();
};

static QByteArray sampleJson()
{
    return R"({
        "name": "demo",
        "description": "suite de prueba",
        "tasks": [
            {"id":"t1","category":"coding","prompt":"escribe fizzbuzz",
             "acceptance":["FizzBuzz"],"weight":2},
            {"id":"t2","category":"docs","prompt":"resume el doc",
             "acceptance":["resumen"],"attachments":["a.pdf"]},
            {"id":"t3","category":"coding","prompt":"otra de coding",
             "acceptance":[]}
        ]
    })";
}

void EvalTests::loadFromJson_parsesTasks()
{
    QString err;
    const EvalSuite s = EvalSuite::loadFromJson(sampleJson(), &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QCOMPARE(s.name, QStringLiteral("demo"));
    QCOMPARE(s.tasks.size(), 3);
    QCOMPARE(s.tasks.first().category, QStringLiteral("coding"));
    QCOMPARE(s.tasks.first().weight, 2);
    QVERIFY(!s.isEmpty());
}

void EvalTests::categories_uniqueInOrder()
{
    const EvalSuite s = EvalSuite::loadFromJson(sampleJson());
    const QStringList cats = s.categories();
    QCOMPARE(cats, (QStringList{"coding", "docs"}));  // únicas, en orden de aparición
}

void EvalTests::invalidJson_returnsEmptyWithError()
{
    QString err;
    const EvalSuite s = EvalSuite::loadFromJson("{ not json", &err);
    QVERIFY(s.isEmpty());
    QVERIFY(!err.isEmpty());
}

void EvalTests::loadFromFile_roundTrip()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("suite.json");
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(sampleJson());
    f.close();
    QString err;
    const EvalSuite s = EvalSuite::loadFromFile(path, &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QCOMPARE(s.tasks.size(), 3);
}

// La suite anti-sesgo bundleada (assets/eval/reasoning_bias.json) debe parsear
// limpia y tener forma sana: trampas con respuesta esperada, controles que son el
// par mínimo de las trampas, y categorías reasoning/normal/chat. Es la red de
// regresión de la directiva antiBias.
void EvalTests::reasoningBiasSuite_isValid()
{
#ifdef LC_REASONING_BIAS_JSON
    QString err;
    const EvalSuite s = EvalSuite::loadFromFile(QStringLiteral(LC_REASONING_BIAS_JSON), &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QVERIFY(!s.isEmpty());
    QVERIFY(s.tasks.size() >= 20);
    // Categorías esperadas presentes.
    const QStringList cats = s.categories();
    for (const QString &c : {"reasoning", "normal", "chat"})
        QVERIFY2(cats.contains(c), qPrintable("falta categoría " + c));
    // Ids únicos; toda tarea reasoning/normal trae al menos un substring esperado
    // (las chat son de eval manual y pueden ir sin acceptance).
    QSet<QString> ids;
    for (const EvalTask &t : s.tasks) {
        QVERIFY2(!ids.contains(t.id), qPrintable("id duplicado " + t.id));
        ids.insert(t.id);
        QVERIFY(!t.prompt.isEmpty());
        if (t.category == QLatin1String("reasoning") || t.category == QLatin1String("normal"))
            QVERIFY2(!t.acceptance.isEmpty(), qPrintable("sin acceptance: " + t.id));
    }
    // Par mínimo trampa↔control: la trampa de la rueda pinchada y su gemelo.
    QVERIFY(ids.contains(QStringLiteral("trap-flat-tyre")));
    QVERIFY(ids.contains(QStringLiteral("ctrl-two-shops")));
#else
    QSKIP("LC_REASONING_BIAS_JSON no definido");
#endif
}

// Suite Snake retro single-file (assets/eval/snake_retro_singlefile.json): la
// tarea autocontenida para comparar NIVELES de agente. Debe parsear y traer la
// tarea con su acceptance (substrings que prueban un Snake jugable en un HTML).
void EvalTests::snakeSuite_isValid()
{
#ifdef LC_SNAKE_SUITE_JSON
    QString err;
    const EvalSuite s = EvalSuite::loadFromFile(QStringLiteral(LC_SNAKE_SUITE_JSON), &err);
    QVERIFY2(err.isEmpty(), qPrintable(err));
    QVERIFY(!s.isEmpty());
    QCOMPARE(s.tasks.size(), 1);
    const EvalTask &t = s.tasks.first();
    QCOMPARE(t.category, QStringLiteral("coding"));
    QVERIFY(t.prompt.contains(QStringLiteral("SNAKE")));
    // Acceptance cubre los marcadores de un Snake jugable en un único HTML.
    for (const QString &need : {"<canvas", "getContext", "keydown", "score"})
        QVERIFY2(t.acceptance.contains(need), qPrintable("falta acceptance " + need));
#else
    QSKIP("LC_SNAKE_SUITE_JSON no definido");
#endif
}

void EvalTests::agentAcceptance_scoresGeneratedFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile html(dir.filePath("snake_retro.html"));
    QVERIFY(html.open(QIODevice::WriteOnly | QIODevice::Text));
    html.write(R"(<html><head><style></style></head><body>
<canvas id="game"></canvas>
<script>
const ctx = document.getElementById('game').getContext('2d');
document.addEventListener('keydown', () => {});
let score = 0;
</script>
</body></html>)");
    html.close();

    QVariantMap acceptance;
    acceptance["expectSubstrings"] = QVariantList{
        QStringLiteral("<canvas"),
        QStringLiteral("<style"),
        QStringLiteral("<script"),
        QStringLiteral("getContext"),
        QStringLiteral("addEventListener"),
        QStringLiteral("keydown"),
        QStringLiteral("score"),
    };
    QVariantMap task;
    task["id"] = QStringLiteral("snake-singlefile");
    task["acceptance"] = acceptance;

    const QVariantMap scored = AppController::scoreAgentBenchmarkAcceptanceForTest(
        dir.path(),
        QStringLiteral("Creé snake_retro.html y lo verifiqué."),
        QVariantList{task},
        QStringList{QStringLiteral("snake_retro.html")});

    QCOMPARE(scored.value(QStringLiteral("score")).toInt(), 7);
    QCOMPARE(scored.value(QStringLiteral("total")).toInt(), 7);
    const QVariantList rows = scored.value(QStringLiteral("rows")).toList();
    QCOMPARE(rows.size(), 7);
    for (const QVariant &row : rows)
        QVERIFY2(row.toMap().value(QStringLiteral("passed")).toBool(),
                 qPrintable(row.toMap().value(QStringLiteral("name")).toString()));

    // HumanEval regression: a valid source file must not be concatenated with
    // the agent's natural-language completion summary before execution.
    QFile py(dir.filePath("solution.py"));
    QVERIFY(py.open(QIODevice::WriteOnly | QIODevice::Text));
    py.write("def has_close_elements(numbers, threshold):\n"
             "    return any(abs(a - b) < threshold\n"
             "               for i, a in enumerate(numbers)\n"
             "               for b in numbers[i + 1:])\n");
    py.close();
    QVariantMap heAcceptance;
    heAcceptance["graderType"] = QStringLiteral("code_tests");
    heAcceptance["entryPoint"] = QStringLiteral("has_close_elements");
    heAcceptance["preamble"] = QStringLiteral("def has_close_elements(numbers, threshold):\n");
    heAcceptance["tests"] = QStringLiteral(
        "def check(f):\n"
        "    assert f([1.0, 2.0, 3.9, 4.0], 0.3)\n"
        "check(has_close_elements)\n");
    QVariantMap heTask;
    heTask["id"] = QStringLiteral("HumanEval/0");
    heTask["acceptance"] = heAcceptance;
    const QVariantMap heScore = AppController::scoreAgentBenchmarkAcceptanceForTest(
        dir.path(), QStringLiteral("Archivo solution.py creado y verificado."),
        QVariantList{heTask}, QStringList{QStringLiteral("solution.py")});
    QCOMPARE(heScore.value(QStringLiteral("score")).toInt(), 1);
    QCOMPARE(heScore.value(QStringLiteral("total")).toInt(), 1);
}

// Parsear la respuesta de un LLM es donde se cometen los errores caros: en este
// mismo proyecto hubo evaluadores que comparaban literales y daban por incorrecto
// codigo perfecto escrito con otro espaciado. Estos casos son respuestas REALES
// que devolvieron los modelos durante el barrido del 2026-08-07.
void EvalTests::benchPack_extractsAnswersFromRealModelOutput()
{
    // ── opción múltiple ──
    QCOMPARE(BenchmarkPack::extractChoice("B"), QStringLiteral("B"));
    QCOMPARE(BenchmarkPack::extractChoice("Answer: C"), QStringLiteral("C"));
    QCOMPARE(BenchmarkPack::extractChoice("Respuesta: (D)"), QStringLiteral("D"));
    QCOMPARE(BenchmarkPack::extractChoice("<think>A parece, pero no</think>\nAnswer: B"),
             QStringLiteral("B"));
    // Si razona y se corrige, vale la ULTIMA marca explicita.
    QCOMPARE(BenchmarkPack::extractChoice("Answer: A\nMe equivoque. Answer: C"),
             QStringLiteral("C"));
    QVERIFY(BenchmarkPack::extractChoice("No estoy seguro").isEmpty());

    // ── numérico ──
    QCOMPARE(BenchmarkPack::extractNumber("El resultado es 436."), QStringLiteral("436"));
    QCOMPARE(BenchmarkPack::extractNumber("...\n#### 18"), QStringLiteral("18"));
    // Separador de miles: NO es un decimal.
    QCOMPARE(BenchmarkPack::extractNumber("Son 34,650 formas"), QStringLiteral("34650"));
    QCOMPARE(BenchmarkPack::extractNumber("Son 34.650 formas"), QStringLiteral("34650"));
    // Decimal de verdad, y los ceros a la derecha no cambian el valor.
    QCOMPARE(BenchmarkPack::extractNumber("cuesta 0.05"), QStringLiteral("0.05"));
    QCOMPARE(BenchmarkPack::extractNumber("cuesta 3.50"), QStringLiteral("3.5"));
    QCOMPARE(BenchmarkPack::extractNumber("da -0"), QStringLiteral("0"));
    QCOMPARE(BenchmarkPack::extractNumber("total 007"), QStringLiteral("7"));
    QVERIFY(BenchmarkPack::extractNumber("no se puede calcular").isEmpty());

    // ── código ──
    QCOMPARE(BenchmarkPack::extractCode("```python\ndef f(): pass\n```").trimmed(),
             QStringLiteral("def f(): pass"));
    // Con varios bloques vale el ultimo: el modelo suele mostrar el mal ejemplo
    // primero y la version corregida despues.
    QCOMPARE(BenchmarkPack::extractCode("```\nmalo\n```\ntexto\n```\nbueno\n```").trimmed(),
             QStringLiteral("bueno"));
    QCOMPARE(BenchmarkPack::extractCode("def g(): pass").trimmed(),
             QStringLiteral("def g(): pass"));

    // ── grade ──
    BenchmarkItem mc;
    mc.type = QStringLiteral("multiple_choice");
    mc.expected = QStringLiteral("B");
    QVERIFY(BenchmarkPack::grade(mc, QStringLiteral("La respuesta es B porque...")));
    QVERIFY(!BenchmarkPack::grade(mc, QStringLiteral("Answer: A")));

    BenchmarkItem num;
    num.type = QStringLiteral("numeric");
    num.expected = QStringLiteral("#### 72");
    QVERIFY(BenchmarkPack::grade(num, QStringLiteral("Entonces son 72 manzanas.")));
    QVERIFY(BenchmarkPack::grade(num, QStringLiteral("son 72.00")));
    QVERIFY(!BenchmarkPack::grade(num, QStringLiteral("son 71")));

    BenchmarkItem con;
    con.type = QStringLiteral("contains");
    con.expected = QStringLiteral("Raft|raft");
    QVERIFY(BenchmarkPack::grade(con, QStringLiteral("etcd usa RAFT")));
    QVERIFY(!BenchmarkPack::grade(con, QStringLiteral("usa Paxos")));

    // code_tests NO se decide sin ejecutar: el runner tiene que correr los tests.
    BenchmarkItem code;
    code.type = QStringLiteral("code_tests");
    QVERIFY(!BenchmarkPack::grade(code, QStringLiteral("def f(): return 1")));
}

// Cada suite publica su propio formato: el import los normaliza a un item unico.
void EvalTests::benchPack_importsPublicFormats()
{
    const QByteArray gsm =
        "{\"question\":\"Juan tiene 3 cajas de 6 huevos. Cuantos huevos hay?\","
        "\"answer\":\"3*6 = 18\\n#### 18\"}\n"
        "{\"question\":\"2+2?\",\"answer\":\"#### 4\"}\n";
    QString err;
    BenchmarkPack g = BenchmarkPack::fromGsm8kJsonl(gsm, &err);
    QCOMPARE(g.items.size(), 2);
    QCOMPARE(g.items.at(0).type, QStringLiteral("numeric"));
    QCOMPARE(g.items.at(0).expected, QStringLiteral("18"));   // del "#### 18"
    QVERIFY(g.items.at(0).prompt.contains(QStringLiteral("huevos")));

    const QByteArray he =
        "{\"task_id\":\"HumanEval/0\",\"prompt\":\"def add(a,b):\\n\","
        "\"test\":\"def check(f):\\n    assert f(1,2)==3\\n\",\"entry_point\":\"add\"}\n";
    BenchmarkPack h = BenchmarkPack::fromHumanEvalJsonl(he, &err);
    QCOMPARE(h.items.size(), 1);
    QCOMPARE(h.items.at(0).type, QStringLiteral("code_tests"));
    QCOMPARE(h.items.at(0).id, QStringLiteral("HumanEval/0"));
    QVERIFY(h.items.at(0).tests.contains(QStringLiteral("check(add)")));

    const QByteArray mmlu =
        "{\"question\":\"Capital de Francia\",\"choices\":[\"Roma\",\"Paris\",\"Lima\"],"
        "\"answer\":1}\n";
    BenchmarkPack m = BenchmarkPack::fromMmluJsonl(mmlu, &err);
    QCOMPARE(m.items.size(), 1);
    QCOMPARE(m.items.at(0).expected, QStringLiteral("B"));    // indice 1 -> B
    QVERIFY(m.items.at(0).prompt.contains(QStringLiteral("B) Paris")));

    // El auto-import distingue los tres por sus claves, sin que el usuario elija.
    QCOMPARE(BenchmarkPack::autoImport(gsm, QStringLiteral("x"), &err).id,
             QStringLiteral("gsm8k"));
    QCOMPARE(BenchmarkPack::autoImport(he, QStringLiteral("x"), &err).id,
             QStringLiteral("humaneval"));
    QCOMPARE(BenchmarkPack::autoImport(mmlu, QStringLiteral("x"), &err).id,
             QStringLiteral("mmlu"));

    // Round-trip por el formato propio: lo que se guarda se vuelve a leer igual.
    const BenchmarkPack back = BenchmarkPack::fromPackJson(g.toPackJson(), &err);
    QCOMPARE(back.items.size(), g.items.size());
    QCOMPARE(back.items.at(0).expected, g.items.at(0).expected);
    QCOMPARE(back.id, QStringLiteral("gsm8k"));

    // Basura: falla con motivo, no con un pack vacio silencioso.
    err.clear();
    QVERIFY(BenchmarkPack::autoImport("no soy json", QStringLiteral("x"), &err).isEmpty());
    QVERIFY(!err.isEmpty());
}

// code_tests es el unico tipo que NO se puede puntuar sin ejecutar. El codigo
// viene de un LLM, asi que el runner necesita timeout duro y cwd propio: tarde o
// temprano un modelo devuelve `while True:`.
void EvalTests::benchPack_runsCodeTestsWithTimeout()
{
    if (QStandardPaths::findExecutable(QStringLiteral("python")).isEmpty()
        && QStandardPaths::findExecutable(QStringLiteral("python3")).isEmpty())
        QSKIP("python no esta en el PATH");

    const QString tests = QStringLiteral("def check(f):\n    assert f(1,2)==3\n    "
                                         "assert f(0,0)==0\n\ncheck(add)\n");

    // Correcto -> pasa.
    auto ok = BenchmarkPack::runCodeTests(QStringLiteral("def add(a,b):\n    return a+b\n"), tests);
    QVERIFY2(ok.passed, qPrintable(ok.error));
    QVERIFY(!ok.timedOut);

    // Compila pero falla el assert -> no pasa, y el motivo queda visible.
    auto bad = BenchmarkPack::runCodeTests(QStringLiteral("def add(a,b):\n    return a-b\n"), tests);
    QVERIFY(!bad.passed);
    QVERIFY(!bad.timedOut);
    QVERIFY2(bad.error.contains(QStringLiteral("AssertionError")), qPrintable(bad.error));
    // El diagnóstico de reparación conserva el traceback, no sólo la última
    // línea: el agente necesita saber qué archivo/check originó el contrato.
    QVERIFY2(bad.error.contains(QStringLiteral("candidate.py")), qPrintable(bad.error));

    // No parsea -> tampoco pasa, y se distingue del assert fallado.
    auto broken = BenchmarkPack::runCodeTests(QStringLiteral("def add(a,b)\n    return a+b\n"), tests);
    QVERIFY(!broken.passed);
    QVERIFY2(broken.error.contains(QStringLiteral("SyntaxError")), qPrintable(broken.error));

    // Sin codigo -> falla sin crashear ni lanzar python.
    auto empty = BenchmarkPack::runCodeTests(QString(), tests);
    QVERIFY(!empty.passed);
    QVERIFY(!empty.error.isEmpty());

    // Bucle infinito -> corta por timeout Y lo respeta. Sin esto el benchmark se
    // cuelga sin decir por que.
    QElapsedTimer t;
    t.start();
    auto loop = BenchmarkPack::runCodeTests(
        QStringLiteral("def add(a,b):\n    while True:\n        pass\n"), tests, 3000);
    const qint64 elapsed = t.elapsed();
    QVERIFY(!loop.passed);
    QVERIFY(loop.timedOut);
    QVERIFY2(elapsed < 15000, qPrintable(QStringLiteral("tardo %1 ms").arg(elapsed)));

    // gradeWithExecution: extrae el codigo de las cercas y ejecuta.
    BenchmarkItem item;
    item.type = QStringLiteral("code_tests");
    item.tests = tests;
    QString detail;
    QVERIFY(BenchmarkPack::gradeWithExecution(
        item, QStringLiteral("Aca va:\n```python\ndef add(a,b):\n    return a+b\n```"),
        20000, &detail));
    QVERIFY(!BenchmarkPack::gradeWithExecution(
        item, QStringLiteral("```python\ndef add(a,b):\n    return 99\n```"), 20000, &detail));
    QVERIFY(!detail.isEmpty());

    // HumanEval: el modelo devuelve la funcion SIN los imports que estaban en el
    // prompt. Ejecutarla sola daba "NameError: name 'List' is not defined", que es
    // un error del harness y no del modelo — paso de verdad con KAT en HumanEval/0.
    const QString typedTests = QStringLiteral(
        "def check(f):\n    assert f([1.0, 2.0], 0.5) == False\n\ncheck(has_close)\n");
    auto typed = BenchmarkPack::runCodeTests(
        QStringLiteral("def has_close(numbers: List[float], threshold: float) -> bool:\n"
                       "    return False\n"),
        typedTests);
    QVERIFY2(typed.passed, qPrintable(typed.error));

    // Si el modelo YA trae su import, no se le pisa nada.
    auto selfImport = BenchmarkPack::runCodeTests(
        QStringLiteral("from typing import List\n"
                       "def has_close(numbers: List[float], threshold: float) -> bool:\n"
                       "    return False\n"),
        typedTests);
    QVERIFY2(selfImport.passed, qPrintable(selfImport.error));

    // El caso que quedaba: el modelo devuelve SOLO el cuerpo, o usa un helper que
    // estaba en el enunciado. Sin anteponer el prompt se ve como "SyntaxError:
    // 'return' outside function" y "NameError: name 'is_palindrome' is not
    // defined" — paso de verdad con KAT y DeepSeek, y parecian errores de ellos.
    BenchmarkItem he;
    he.type = QStringLiteral("code_tests");
    he.entryPoint = QStringLiteral("suma_pares");
    he.preamble = QStringLiteral(
        "def es_par(n):\n    return n % 2 == 0\n\ndef suma_pares(xs):\n");
    he.tests = QStringLiteral("def check(f):\n    assert f([1,2,3,4]) == 6\n\ncheck(suma_pares)\n");

    // Solo el cuerpo, indentado: se antepone el prompt y compila.
    QString d1;
    QVERIFY2(BenchmarkPack::gradeWithExecution(
                 he, QStringLiteral("    return sum(x for x in xs if es_par(x))\n"), 20000, &d1),
             qPrintable(d1));

    // Redefine la funcion pero usa el helper del enunciado: tambien necesita el
    // preambulo, y no se rompe por definirla dos veces.
    QString d2;
    QVERIFY2(BenchmarkPack::gradeWithExecution(
                 he,
                 QStringLiteral("```python\ndef suma_pares(xs):\n"
                                "    return sum(x for x in xs if es_par(x))\n```"),
                 20000, &d2),
             qPrintable(d2));

    // Y una solucion mal sigue estando mal: el preambulo no regala puntos.
    QString d3;
    QVERIFY(!BenchmarkPack::gradeWithExecution(
        he, QStringLiteral("def suma_pares(xs):\n    return 999\n"), 20000, &d3));

    // Para los otros tipos delega en grade(): no lanza python al pedo.
    BenchmarkItem num;
    num.type = QStringLiteral("numeric");
    num.expected = QStringLiteral("42");
    QVERIFY(BenchmarkPack::gradeWithExecution(num, QStringLiteral("son 42")));
}

QTEST_MAIN(EvalTests)
#include "test_eval.moc"
