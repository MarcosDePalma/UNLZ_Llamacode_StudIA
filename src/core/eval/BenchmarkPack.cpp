#include "BenchmarkPack.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {

QVector<QJsonObject> parseJsonl(const QByteArray &data)
{
    QVector<QJsonObject> out;
    for (const QByteArray &raw : data.split('\n')) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty()) continue;
        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(line, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            out.append(doc.object());
    }
    return out;
}

// Saca lo que no cambia la respuesta pero rompe el matcheo: el bloque de
// razonamiento y las cercas de markdown.
QString clean(const QString &raw)
{
    QString s = raw;
    static const QRegularExpression think(
        QStringLiteral("<think>.*?</think>|<thinking>.*?</thinking>"),
        QRegularExpression::DotMatchesEverythingOption
            | QRegularExpression::CaseInsensitiveOption);
    s.remove(think);
    return s;
}

}  // namespace

// ── Extractores ──────────────────────────────────────────────────────────────

QString BenchmarkPack::extractChoice(const QString &response)
{
    const QString s = clean(response);
    // Preferir una marca explícita: "Answer: B", "Respuesta: B", "(B)".
    static const QRegularExpression tagged(
        // "Answer: B", "Respuesta B", "La respuesta es (B)", "final answer is B".
        QStringLiteral("(?:answer|respuesta|final)\\s*(?:\\w+\\s+){0,2}[:\\-]?\\s*\\(?([A-Z])\\)?\\b"),
        QRegularExpression::CaseInsensitiveOption);
    QString last;
    auto it = tagged.globalMatch(s);
    while (it.hasNext()) last = it.next().captured(1).toUpper();
    if (!last.isEmpty()) return last;

    // Si no, una letra sola en su propia línea (típico de "solo la letra").
    static const QRegularExpression alone(QStringLiteral("(?m)^\\s*\\(?([A-Z])\\)?\\s*[.)]?\\s*$"));
    it = alone.globalMatch(s);
    while (it.hasNext()) last = it.next().captured(1).toUpper();
    return last;
}

QString BenchmarkPack::extractNumber(const QString &response)
{
    QString s = clean(response);
    // GSM8K marca la respuesta final con "#### 42".
    static const QRegularExpression hashed(QStringLiteral("####\\s*(-?[\\d.,]+)"));
    const auto h = hashed.match(s);
    QString raw;
    if (h.hasMatch()) {
        raw = h.captured(1);
    } else {
        // El último número del texto: los modelos razonan y concluyen al final.
        static const QRegularExpression num(QStringLiteral("-?\\d[\\d.,]*"));
        auto it = num.globalMatch(s);
        while (it.hasNext()) raw = it.next().captured();
    }
    if (raw.isEmpty()) return {};

    // Normalizar separadores: "1,234" y "1.234" son mil doscientos treinta y
    // cuatro según el locale, pero "3.5" es un decimal. Se resuelve mirando el
    // tamaño del último grupo.
    raw.remove(QLatin1Char(' '));
    const int lastDot = raw.lastIndexOf(QLatin1Char('.'));
    const int lastComma = raw.lastIndexOf(QLatin1Char(','));
    const int sep = qMax(lastDot, lastComma);
    QString intPart = raw, fracPart;
    if (sep >= 0 && raw.size() - sep - 1 != 3) {       // no es separador de miles
        intPart = raw.left(sep);
        fracPart = raw.mid(sep + 1);
    }
    intPart.remove(QLatin1Char('.')).remove(QLatin1Char(','));
    fracPart.remove(QLatin1Char('.')).remove(QLatin1Char(','));
    while (fracPart.endsWith(QLatin1Char('0'))) fracPart.chop(1);   // 3.50 == 3.5
    QString out = intPart;
    if (!fracPart.isEmpty()) out += QLatin1Char('.') + fracPart;
    if (out == QLatin1String("-") || out.isEmpty()) return {};
    // "-0" y "007" normalizan a "0" y "7".
    bool neg = out.startsWith(QLatin1Char('-'));
    if (neg) out.remove(0, 1);
    while (out.size() > 1 && out.startsWith(QLatin1Char('0')) && !out.startsWith(QLatin1String("0.")))
        out.remove(0, 1);
    if (neg && out != QLatin1String("0")) out.prepend(QLatin1Char('-'));
    return out;
}

QString BenchmarkPack::extractCode(const QString &response)
{
    const QString s = clean(response);
    static const QRegularExpression fence(QStringLiteral("```(?:python|py)?\\s*(.*?)```"),
                                          QRegularExpression::DotMatchesEverythingOption);
    QString last;
    auto it = fence.globalMatch(s);
    while (it.hasNext()) last = it.next().captured(1);
    return last.isEmpty() ? s : last;
}

// ── Corrección ───────────────────────────────────────────────────────────────

bool BenchmarkPack::grade(const BenchmarkItem &item, const QString &response)
{
    if (item.type == QLatin1String("multiple_choice"))
        return !item.expected.isEmpty()
            && extractChoice(response).compare(item.expected, Qt::CaseInsensitive) == 0;

    if (item.type == QLatin1String("numeric")) {
        const QString got = extractNumber(response);
        if (got.isEmpty()) return false;
        BenchmarkItem tmp;
        const QString want = extractNumber(item.expected);
        return !want.isEmpty() && got == want;
    }

    if (item.type == QLatin1String("contains")) {
        const QString s = clean(response);
        for (const QString &variant : item.expected.split(QLatin1Char('|'), Qt::SkipEmptyParts))
            if (s.contains(variant.trimmed(), Qt::CaseInsensitive)) return true;
        return false;
    }

    // code_tests necesita ejecutar el código: no se decide acá.
    return false;
}

// ── Ejecución de code_tests ──────────────────────────────────────────────────

BenchmarkPack::CodeRun BenchmarkPack::runCodeTests(const QString &code, const QString &tests,
                                                   int timeoutMs, const QString &pythonPath,
                                                   const QString &preamble,
                                                   const QString &entryPoint)
{
    CodeRun r;
    if (code.trimmed().isEmpty()) {
        r.error = QStringLiteral("la respuesta no traía código");
        return r;
    }

    QString python = pythonPath;
    if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty()) {
        r.error = QStringLiteral("no se encontró python en el PATH");
        return r;
    }

    QTemporaryDir dir;
    if (!dir.isValid()) {
        r.error = QStringLiteral("no se pudo crear el directorio temporal");
        return r;
    }
    const QString file = dir.filePath(QStringLiteral("candidate.py"));
    {
        QFile f(file);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            r.error = QStringLiteral("no se pudo escribir el archivo temporal");
            return r;
        }
        // Los prompts de HumanEval traen los imports arriba de la firma, y el
        // modelo suele devolver SOLO la función. Ejecutar la función sola falla
        // con "NameError: name 'List' is not defined", que no es un error del
        // modelo sino del harness: el oficial concatena prompt + completion.
        // Anteponer los imports habituales cubre el caso sin tener que arrastrar
        // el prompt entero (que termina en una docstring sin cuerpo y no compila
        // si el modelo ya redefinió la función).
        if (!code.contains(QStringLiteral("import ")))
            f.write("from typing import List, Dict, Tuple, Optional, Any, Set, Union\n"
                    "import math, re, collections, itertools, functools, heapq, string\n\n");
        // Si el modelo NO redefinió la función objetivo, devolvió sólo el cuerpo o
        // se apoyó en un helper del enunciado. En los dos casos hay que anteponer
        // el prompt, que es lo que hace el harness oficial. Sin esto se ve como
        // "SyntaxError: 'return' outside function" o "NameError: name
        // 'is_palindrome' is not defined": parecen errores del modelo y no lo son.
        // Si redefine, igual puede necesitar los imports y los helpers del
        // enunciado (DeepSeek fallaba con "NameError: name 'is_palindrome' is not
        // defined" por esto). Se antepone el preámbulo RECORTADO hasta antes de la
        // firma: pegarlo entero dejaría un `def` sin cuerpo y no compilaría.
        const bool redefines = !entryPoint.isEmpty()
            && code.contains(QStringLiteral("def ") + entryPoint);
        if (!preamble.isEmpty()) {
            QString head = preamble;
            if (redefines) {
                const int sig = preamble.indexOf(QStringLiteral("def ") + entryPoint);
                head = sig > 0 ? preamble.left(sig) : QString();
            }
            if (!head.trimmed().isEmpty()) {
                f.write(head.toUtf8());
                f.write("\n");
            }
        }
        f.write(code.toUtf8());
        f.write("\n\n");
        f.write(tests.toUtf8());
        f.write("\n");
    }

    QProcess p;
    p.setWorkingDirectory(dir.path());   // que no escriba sobre el repo
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    p.setProcessEnvironment(env);
    p.start(python, {QStringLiteral("-I"), file});   // -I: sin site-packages del usuario
    if (!p.waitForStarted(5000)) {
        r.error = QStringLiteral("no se pudo lanzar python");
        return r;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        r.timedOut = true;
        r.error = QStringLiteral("timeout de %1 ms (¿bucle infinito?)").arg(timeoutMs);
        return r;
    }

    r.exitCode = p.exitCode();
    r.passed = (p.exitStatus() == QProcess::NormalExit && r.exitCode == 0);
    if (!r.passed) {
        const QString stderrText = QString::fromUtf8(p.readAllStandardError()).trimmed();
        const QString stdoutText = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        // La última línea sola ocultaba la causa útil para reparar BCB (por
        // ejemplo, el traceback que explica qué contrato esperaba el mock).
        // Conservamos una cola acotada del diagnóstico completo para que el
        // agente pueda corregir con evidencia y no adivinar.
        QString diagnostic = stderrText;
        if (diagnostic.isEmpty()) diagnostic = stdoutText;
        if (diagnostic.isEmpty()) diagnostic = QStringLiteral("exit %1").arg(r.exitCode);
        r.error = diagnostic.right(6000);
    }
    return r;
}

bool BenchmarkPack::gradeWithExecution(const BenchmarkItem &item, const QString &response,
                                       int timeoutMs, QString *detail)
{
    if (item.type != QLatin1String("code_tests"))
        return grade(item, response);

    const CodeRun r = runCodeTests(extractCode(response), item.tests, timeoutMs,
                                   QString(), item.preamble, item.entryPoint);
    if (detail) *detail = r.error;
    return r.passed;
}

// ── Importadores ─────────────────────────────────────────────────────────────

BenchmarkPack BenchmarkPack::fromGsm8kJsonl(const QByteArray &jsonl, QString *err)
{
    BenchmarkPack p;
    p.id = QStringLiteral("gsm8k");
    p.name = QStringLiteral("GSM8K");
    p.source = QStringLiteral("https://huggingface.co/datasets/openai/gsm8k");
    p.license = QStringLiteral("MIT");
    int n = 0;
    for (const QJsonObject &o : parseJsonl(jsonl)) {
        const QString q = o.value(QStringLiteral("question")).toString();
        const QString a = o.value(QStringLiteral("answer")).toString();
        if (q.isEmpty() || a.isEmpty()) continue;
        BenchmarkItem it;
        it.id = QStringLiteral("gsm8k-%1").arg(++n, 4, 10, QLatin1Char('0'));
        it.type = QStringLiteral("numeric");
        it.prompt = q + QStringLiteral("\n\nRespondé únicamente con el número final.");
        it.expected = extractNumber(a);   // el "#### N" del final
        if (it.expected.isEmpty()) continue;
        p.items.append(it);
    }
    if (p.items.isEmpty() && err) *err = QStringLiteral("GSM8K: ninguna línea válida");
    return p;
}

BenchmarkPack BenchmarkPack::fromHumanEvalJsonl(const QByteArray &jsonl, QString *err)
{
    BenchmarkPack p;
    p.id = QStringLiteral("humaneval");
    p.name = QStringLiteral("HumanEval");
    p.source = QStringLiteral("https://huggingface.co/datasets/openai/openai_humaneval");
    p.license = QStringLiteral("MIT");
    for (const QJsonObject &o : parseJsonl(jsonl)) {
        const QString prompt = o.value(QStringLiteral("prompt")).toString();
        const QString test = o.value(QStringLiteral("test")).toString();
        const QString entry = o.value(QStringLiteral("entry_point")).toString();
        if (prompt.isEmpty() || test.isEmpty()) continue;
        BenchmarkItem it;
        it.id = o.value(QStringLiteral("task_id")).toString(
            QStringLiteral("humaneval-%1").arg(p.items.size()));
        it.type = QStringLiteral("code_tests");
        it.prompt = QStringLiteral("Completá esta función Python. Devolvé SOLO el código "
                                   "completo de la función, sin explicación.\n\n") + prompt;
        // El harness de HumanEval llama check(entry_point) al final.
        it.tests = test + QStringLiteral("\n\ncheck(%1)\n").arg(entry);
        // El enunciado trae imports, la firma y a veces un helper que el modelo da
        // por sentado: se guarda para poder anteponerlo si su respuesta no
        // redefine la función.
        it.preamble = prompt;
        it.entryPoint = entry;
        p.items.append(it);
    }
    if (p.items.isEmpty() && err) *err = QStringLiteral("HumanEval: ninguna línea válida");
    return p;
}

BenchmarkPack BenchmarkPack::fromMmluJsonl(const QByteArray &jsonl, QString *err)
{
    BenchmarkPack p;
    p.id = QStringLiteral("mmlu");
    p.name = QStringLiteral("MMLU");
    p.source = QStringLiteral("https://huggingface.co/datasets/cais/mmlu");
    p.license = QStringLiteral("MIT");
    int n = 0;
    for (const QJsonObject &o : parseJsonl(jsonl)) {
        const QString q = o.value(QStringLiteral("question")).toString();
        const QJsonArray ch = o.value(QStringLiteral("choices")).toArray();
        if (q.isEmpty() || ch.size() < 2) continue;
        BenchmarkItem it;
        it.id = QStringLiteral("mmlu-%1").arg(++n, 4, 10, QLatin1Char('0'));
        it.type = QStringLiteral("multiple_choice");
        QString body = q + QLatin1Char('\n');
        for (int i = 0; i < ch.size(); ++i) {
            it.choices << ch.at(i).toString();
            body += QStringLiteral("\n%1) %2").arg(QChar('A' + i), ch.at(i).toString());
        }
        it.prompt = body + QStringLiteral("\n\nRespondé únicamente con la letra.");
        // answer viene como índice (0-3) o como letra según el dump.
        const QJsonValue ans = o.value(QStringLiteral("answer"));
        it.expected = ans.isDouble() ? QString(QChar('A' + ans.toInt()))
                                     : ans.toString().trimmed().toUpper();
        if (it.expected.isEmpty()) continue;
        p.items.append(it);
    }
    if (p.items.isEmpty() && err) *err = QStringLiteral("MMLU: ninguna línea válida");
    return p;
}

BenchmarkPack BenchmarkPack::fromPackJson(const QByteArray &json, QString *err)
{
    BenchmarkPack p;
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        if (err) *err = QStringLiteral("pack: no es un objeto JSON");
        return p;
    }
    const QJsonObject o = doc.object();
    p.id = o.value(QStringLiteral("id")).toString();
    p.name = o.value(QStringLiteral("name")).toString();
    p.source = o.value(QStringLiteral("source")).toString();
    p.license = o.value(QStringLiteral("license")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("items")).toArray()) {
        const QJsonObject io = v.toObject();
        BenchmarkItem it;
        it.id = io.value(QStringLiteral("id")).toString();
        it.prompt = io.value(QStringLiteral("prompt")).toString();
        it.type = io.value(QStringLiteral("type")).toString();
        it.expected = io.value(QStringLiteral("expected")).toString();
        it.tests = io.value(QStringLiteral("tests")).toString();
        it.preamble = io.value(QStringLiteral("preamble")).toString();
        it.entryPoint = io.value(QStringLiteral("entryPoint")).toString();
        for (const QJsonValue &c : io.value(QStringLiteral("choices")).toArray())
            it.choices << c.toString();
        if (!it.prompt.isEmpty() && !it.type.isEmpty()) p.items.append(it);
    }
    if (p.items.isEmpty() && err) *err = QStringLiteral("pack: sin ítems válidos");
    return p;
}

QByteArray BenchmarkPack::toPackJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("source")] = source;
    o[QStringLiteral("license")] = license;
    QJsonArray arr;
    for (const BenchmarkItem &it : items) {
        QJsonObject io;
        io[QStringLiteral("id")] = it.id;
        io[QStringLiteral("prompt")] = it.prompt;
        io[QStringLiteral("type")] = it.type;
        if (!it.expected.isEmpty()) io[QStringLiteral("expected")] = it.expected;
        if (!it.tests.isEmpty()) io[QStringLiteral("tests")] = it.tests;
        if (!it.preamble.isEmpty()) io[QStringLiteral("preamble")] = it.preamble;
        if (!it.entryPoint.isEmpty()) io[QStringLiteral("entryPoint")] = it.entryPoint;
        if (!it.choices.isEmpty())
            io[QStringLiteral("choices")] = QJsonArray::fromStringList(it.choices);
        arr.append(io);
    }
    o[QStringLiteral("items")] = arr;
    return QJsonDocument(o).toJson();
}

BenchmarkPack BenchmarkPack::autoImport(const QByteArray &data, const QString &hintName,
                                        QString *err)
{
    // Formato propio: es un objeto con "items".
    const auto doc = QJsonDocument::fromJson(data);
    if (doc.isObject() && doc.object().contains(QStringLiteral("items")))
        return fromPackJson(data, err);

    const QVector<QJsonObject> lines = parseJsonl(data);
    if (lines.isEmpty()) {
        if (err) *err = QStringLiteral("no es JSONL ni un pack JSON");
        return {};
    }
    const QJsonObject &first = lines.first();
    if (first.contains(QStringLiteral("entry_point")) || first.contains(QStringLiteral("test")))
        return fromHumanEvalJsonl(data, err);
    if (first.contains(QStringLiteral("choices")))
        return fromMmluJsonl(data, err);
    if (first.contains(QStringLiteral("question")) && first.contains(QStringLiteral("answer")))
        return fromGsm8kJsonl(data, err);

    if (err)
        *err = QStringLiteral("formato no reconocido en %1: claves %2")
                   .arg(hintName, QStringList(first.keys()).join(QLatin1Char(',')));
    return {};
}
