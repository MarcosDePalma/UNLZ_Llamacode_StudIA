#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

// Importa benchmarks PÚBLICOS estándar (GSM8K, HumanEval, MMLU, AIME…) para no
// depender de tareas caseras.
//
// El motivo es concreto: las suites propias se saturaron. En el barrido del
// 2026-08-07 un Qwen de 27B sacó 14/14 en todo lo que se pudo construir Y
// verificar con certeza, así que dejaron de separar modelos. Los sets públicos
// tienen dificultad calibrada y resultados comparables con los que publica cada
// modelo.
//
// Cada suite trae su formato, así que el import normaliza a un ítem único y la
// corrección se decide por `grade`, que es pura y testeable: el parseo de la
// respuesta de un LLM es donde se cometen los errores caros (medir el formato en
// vez del contenido, o dar por incorrecta una respuesta correcta escrita
// distinto).
struct BenchmarkItem {
    QString id;
    QString prompt;
    // multiple_choice: la respuesta es una letra (A-Z).
    // numeric:         un número; tolera separadores de miles y unidades.
    // code_tests:      el modelo devuelve código y `tests` lo verifica al correr.
    // contains:        alguna de las variantes de `expected` aparece en la respuesta.
    QString type;
    QString expected;
    QString tests;          // sólo code_tests: asserts que se ejecutan aparte
    // Preámbulo del prompt original (imports, firma, helpers). HumanEval lo da
    // por sentado y el modelo suele devolver sólo el cuerpo o sólo la función: si
    // no redefine `entryPoint`, hay que anteponerlo o falta contexto y explota.
    QString preamble;
    QString entryPoint;
    QStringList choices;    // sólo multiple_choice, para armar el prompt
};

struct BenchmarkPack {
    QString id;
    QString name;
    QString source;         // URL de origen, para poder rastrear qué se corrió
    QString license;
    QVector<BenchmarkItem> items;

    bool isEmpty() const { return items.isEmpty(); }

    // ── Import de los formatos públicos, tal cual se descargan ───────────────
    // GSM8K: JSONL con {question, answer} y la respuesta final tras "#### ".
    static BenchmarkPack fromGsm8kJsonl(const QByteArray &jsonl, QString *err = nullptr);
    // HumanEval: JSONL con {task_id, prompt, test, entry_point}.
    static BenchmarkPack fromHumanEvalJsonl(const QByteArray &jsonl, QString *err = nullptr);
    // MMLU: JSONL con {question, choices[], answer} (answer = índice o letra).
    static BenchmarkPack fromMmluJsonl(const QByteArray &jsonl, QString *err = nullptr);
    // Formato propio ya normalizado (el que guarda LlamaCode en disco).
    static BenchmarkPack fromPackJson(const QByteArray &json, QString *err = nullptr);
    QByteArray toPackJson() const;

    // Detecta el formato por las claves de la primera línea y delega.
    static BenchmarkPack autoImport(const QByteArray &data, const QString &hintName,
                                    QString *err = nullptr);

    // ¿La respuesta del modelo es correcta? Pura: sin red, sin disco, sin
    // ejecutar nada. Para code_tests devuelve false — esos necesitan correr el
    // código y eso lo hace el runner.
    static bool grade(const BenchmarkItem &item, const QString &response);

    // Helpers expuestos porque son donde se cometen los errores de medición.
    static QString extractChoice(const QString &response);   // "B" o vacío
    static QString extractNumber(const QString &response);   // normalizado, o vacío
    static QString extractCode(const QString &response);     // sin cercas markdown

    struct CodeRun {
        bool passed = false;
        bool timedOut = false;
        int exitCode = -1;
        QString error;      // stderr recortado: assert fallado y SyntaxError son
                            // diagnósticos distintos y hay que poder verlos
    };

    // Ejecuta el código que devolvió el modelo contra los tests del ítem. Es la
    // única forma de puntuar code_tests: no hay substring que sirva.
    //
    // El código viene de un LLM, así que corre en un directorio temporal propio
    // que se borra al terminar, y con timeout duro — tarde o temprano un modelo
    // devuelve `while True:` y sin timeout el benchmark se cuelga sin decir por
    // qué. NO es un sandbox de seguridad: no hay aislamiento de red ni de
    // filesystem más allá del cwd. No correr packs de origen desconocido.
    static CodeRun runCodeTests(const QString &code, const QString &tests,
                                int timeoutMs = 20000, const QString &pythonPath = QString(),
                                const QString &preamble = QString(),
                                const QString &entryPoint = QString());

    // grade() + ejecución cuando el ítem es code_tests. `python` vacío = buscar en PATH.
    static bool gradeWithExecution(const BenchmarkItem &item, const QString &response,
                                   int timeoutMs = 20000, QString *detail = nullptr);
};
