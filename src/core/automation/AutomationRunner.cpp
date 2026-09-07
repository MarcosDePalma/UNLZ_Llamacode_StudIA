#include "AutomationRunner.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace {

bool parseArithmetic(const QString &text, double *expected)
{
    static const QRegularExpression exprRe(
        QStringLiteral("(-?\\d+(?:[\\.,]\\d+)?)\\s*([+\\-*/x×÷])\\s*(-?\\d+(?:[\\.,]\\d+)?)"));
    const QRegularExpressionMatch match = exprRe.match(text.toLower());
    if (!match.hasMatch()) return false;
    auto number = [](QString value, bool *ok) {
        return value.replace(QLatin1Char(','), QLatin1Char('.')).toDouble(ok);
    };
    bool aOk = false, bOk = false;
    const double a = number(match.captured(1), &aOk);
    const double b = number(match.captured(3), &bOk);
    if (!aOk || !bOk) return false;
    const QString op = match.captured(2);
    if (op == QLatin1String("+")) *expected = a + b;
    else if (op == QLatin1String("-")) *expected = a - b;
    else if (op == QLatin1String("*") || op == QLatin1String("x") || op == QLatin1String("×"))
        *expected = a * b;
    else if (op == QLatin1String("/") || op == QLatin1String("÷")) {
        if (qFuzzyIsNull(b)) return false;
        *expected = a / b;
    } else return false;
    return true;
}

QString lastCalculatorDisplay(const QString &text)
{
    static const QRegularExpression displayRe(
        QStringLiteral("Se muestra\\s+(-?\\d+(?:[\\.,]\\d+)?)"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = displayRe.globalMatch(text);
    QString value;
    while (it.hasNext()) value = it.next().captured(1);
    return value;
}

} // namespace

bool AutomationRunner::isSensitiveAction(const QString &intent)
{
    const QString s = intent.toLower();
    static const QStringList markers{
        QStringLiteral("comprar"), QStringLiteral("pagar"), QStringLiteral("transfer"),
        QStringLiteral("enviar"), QStringLiteral("publicar"), QStringLiteral("borrar"),
        QStringLiteral("eliminar"), QStringLiteral("confirmar"), QStringLiteral("delete"),
        QStringLiteral("purchase"), QStringLiteral("payment"), QStringLiteral("send")};
    for (const QString &marker : markers)
        if (s.contains(marker)) return true;
    return false;
}

bool AutomationRunner::recipeHasWebStep(const QVariantList &steps)
{
    for (const QVariant &s : steps) {
        const QString k = s.toMap().value(QStringLiteral("kind")).toString();
        if (k == QLatin1String("browser") || k == QLatin1String("web"))
            return true;
    }
    return false;
}

QString AutomationRunner::validateTask(const QVariantMap &task, bool hasVision)
{
    Q_UNUSED(hasVision)
    const QString mode = task.value(QStringLiteral("executionMode"), QStringLiteral("auto")).toString();
    if (mode == QLatin1String("desktop")
        && task.value(QStringLiteral("teachArtifactId")).toString().isEmpty())
        return QStringLiteral("La automatización de escritorio todavía no fue enseñada.");
    return {};
}

bool AutomationRunner::arithmeticResultMismatch(const QVariantMap &task, const QString &workLog,
                                                QString *message)
{
    if (message)
        message->clear();

    const QString taskText = (task.value(QStringLiteral("name")).toString()
                              + QLatin1Char('\n')
                              + task.value(QStringLiteral("description")).toString()).toLower();
    if (!taskText.contains(QStringLiteral("calculadora")))
        return false;

    double expected = 0.0;
    if (!parseArithmetic(taskText, &expected))
        return false;
    const QString lastDisplay = lastCalculatorDisplay(workLog);
    if (lastDisplay.isEmpty()) {
        if (message) {
            *message = QStringLiteral("No se pudo verificar la Calculadora: falta el visor "
                                      "actual ('Se muestra X') en desktop_controls. "
                                      "No se acepta Historial/Memoria como resultado final.");
        }
        return true;
    }

    const QString normalized = QString(lastDisplay).replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double actual = normalized.toDouble(&ok);
    if (!ok || qAbs(actual - expected) < 0.001)
        return false;

    if (message) {
        *message = QStringLiteral("La verificación de Calculadora contradice el objetivo: "
                                  "el visor actual dice %1, pero se esperaba %2. "
                                  "No se acepta historial viejo como éxito.")
                       .arg(lastDisplay, QString::number(expected, 'g', 12));
    }
    return true;
}

QVariantList AutomationRunner::safeDesktopWarmStart(const QVariantMap &task,
                                                    const QVariantMap &recipe)
{
    if (task.value(QStringLiteral("executionMode")).toString() != QLatin1String("desktop"))
        return {};
    const QVariantList taught = recipe.value(QStringLiteral("steps")).toList();
    QVariantList out;
    bool sawWin = false, sawTypeAfterWin = false, launched = false;
    static const QSet<QString> safeKeys{
        QStringLiteral("WIN"), QStringLiteral("ENTER"), QStringLiteral("ESC"),
        QStringLiteral("TAB"), QStringLiteral("UP"), QStringLiteral("DOWN"),
        QStringLiteral("LEFT"), QStringLiteral("RIGHT")};

    for (const QVariant &value : taught) {
        const QVariantMap step = value.toMap();
        const QString kind = step.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("start")) continue;
        if (kind != QLatin1String("key") && kind != QLatin1String("type")) break;
        if (kind == QLatin1String("key")) {
            const QString key = step.value(QStringLiteral("key")).toString().toUpper();
            if (!safeKeys.contains(key)) break;
            if (out.isEmpty() && key != QLatin1String("WIN")) break;
            sawWin = sawWin || key == QLatin1String("WIN");
            if (sawWin && sawTypeAfterWin && key == QLatin1String("ENTER")) launched = true;
        } else {
            const QString text = step.value(QStringLiteral("text")).toString();
            if (text.isEmpty() || !sawWin) break;
            sawTypeAfterWin = true;
        }
        out.append(step);
    }
    // Sin una secuencia completa de lanzamiento no hacemos replay parcial: evita
    // dejar abierto Inicio o texto escrito en la ventana equivocada.
    return launched ? out : QVariantList{};
}

QVariantMap AutomationRunner::limits(const QVariantMap &task)
{
    return {
        {QStringLiteral("timeoutSec"), qBound(30, task.value(QStringLiteral("timeoutSec"), 300).toInt(), 3600)},
        {QStringLiteral("maxActions"), qBound(1, task.value(QStringLiteral("maxActions"), 50).toInt(), 500)},
        {QStringLiteral("maxRetries"), qBound(0, task.value(QStringLiteral("maxRetries"), 2).toInt(), 10)}};
}

QString AutomationRunner::resolveExecutionMode(const QVariantMap &task)
{
    const QString mode = task.value(QStringLiteral("executionMode"), QStringLiteral("auto")).toString();
    if (mode == QLatin1String("desktop") || mode == QLatin1String("browserBackground"))
        return mode;
    // "auto" (o legacy "agent"/"prompt"/vacío): el sistema elige la superficie.
    // Señal fuerte y determinista: el tipo de los pasos. Cualquier paso de
    // escritorio implica controlar la pantalla real → "desktop". Si no, la tarea
    // es web → "browserBackground".
    const QVariantList steps = task.value(QStringLiteral("steps")).toList();
    for (const QVariant &value : steps)
        if (value.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("desktop"))
            return QStringLiteral("desktop");
    return QStringLiteral("browserBackground");
}

QString AutomationRunner::headlessBrowserCommand(const QString &command)
{
    const QString c = command.trimmed();
    if (c.isEmpty()) return c;
    // Respeta una elección explícita del usuario.
    if (c.contains(QLatin1String("--headless")) || c.contains(QLatin1String("--headed")))
        return c;
    // Sólo el MCP de Playwright: no tocamos comandos MCP de terceros.
    if (!c.contains(QLatin1String("@playwright/mcp")) && !c.contains(QLatin1String("playwright-mcp")))
        return c;
    return c + QStringLiteral(" --headless");
}

QString AutomationRunner::foregroundBrowserCommand(const QString &command)
{
    QString c = command.trimmed();
    if (c.isEmpty()) return c;
    if (!c.contains(QLatin1String("@playwright/mcp")) && !c.contains(QLatin1String("playwright-mcp")))
        return c;
    c.replace(QStringLiteral(" --headless"), QString());
    c.replace(QStringLiteral("--headless "), QString());
    c.replace(QStringLiteral("--headless"), QString());
    while (c.contains(QStringLiteral("  "))) c.replace(QStringLiteral("  "), QStringLiteral(" "));
    c = c.trimmed();
    if (!c.contains(QLatin1String("--headed")))
        c += QStringLiteral(" --headed");
    return c;
}

QStringList AutomationRunner::desktopToolNames()
{
    return {
        QStringLiteral("desktop_snapshot"),
        QStringLiteral("desktop_windows"),
        QStringLiteral("desktop_controls"),
        QStringLiteral("desktop_click_element"),
        QStringLiteral("desktop_control_action"),
        QStringLiteral("desktop_find_image"),
        QStringLiteral("desktop_click_image"),
        QStringLiteral("desktop_wait_image"),
        QStringLiteral("desktop_assert_image"),
        QStringLiteral("desktop_observe"),
        QStringLiteral("desktop_click"),
        QStringLiteral("desktop_stroke"),
        QStringLiteral("desktop_type"),
        QStringLiteral("desktop_key"),
        QStringLiteral("desktop_scroll"),
        QStringLiteral("desktop_focus"),
        QStringLiteral("desktop_resize"),
        QStringLiteral("desktop_wait"),
        QStringLiteral("desktop_wait_for"),
        QStringLiteral("desktop_assert"),
        QStringLiteral("desktop_launch")};
}

QString AutomationRunner::augmentPrompt(const QVariantMap &task, const QVariantMap &manifest,
                                        const QVariantMap &recipe)
{
    // El bloque de receta sólo tiene sentido con una superficie concreta; los
    // callers ya gatean por teachArtifactId, así que el modo siempre resuelve a
    // "desktop"/"browserBackground".
    const QString mode = resolveExecutionMode(task);
    QString out = QStringLiteral(
        "\n\nMODO DE AUTOMATIZACIÓN: %1.\n"
        "Usá la receta enseñada como evidencia semántica, no como replay rígido. "
        "Ejecutá una acción por vez y verificá el progreso por el medio más barato "
        "disponible (texto/estructura antes que captura). Detenete si no podés "
        "verificar el objetivo.\n")
        .arg(mode);
    if (mode == QLatin1String("desktop")) {
        out += QStringLiteral(
            "Superficie: escritorio foreground nativo. Usá las tools desktop_* para operar y "
            "verificar la pantalla real.\n"
            "CAMINO RÁPIDO (seguilo, evita el loop de observar):\n"
            "1) desktop_launch <app>; no uses run_shell para apps GUI.\n"
            "2) desktop_wait ~800 ms y UNA sola desktop_windows. NO repitas desktop_windows; "
            "si aparece la ventana, enfocá y actuá.\n"
            "3) TECLADO primero: desktop_focus <id>, desktop_type texto, desktop_key ENTER/=.\n"
            "Para cálculos cortos: primero limpiá con desktop_key ESC, después detectá la "
            "expresión del objetivo y escribila completa con '=' en una sola llamada "
            "(desktop_type \"<expresión>=\"); no la partas ni presiones ENTER después.\n"
            "4) Si no hay teclado: desktop_controls <id> y desktop_click_element por nombre/controlId; "
            "si la receta trae locator image y UIA/OCR fallan, usá desktop_find_image/click_image.\n"
            "Antes de una acción semántica preferí desktop_snapshot o el snapshot_id de "
            "desktop_controls/desktop_observe; pasalo a la acción. Si el stale guard falla, "
            "no insistas: observá de nuevo y re-resolvé el target. Para editar, alternar, "
            "seleccionar, expandir o desplazar un control preferí desktop_control_action.\n"
            "5) Verificá con desktop_controls usando el visor ACTUAL ('Se muestra X'); "
            "no aceptes Historial/Memoria como resultado final.\n"
            "Cada tool de click devuelve trace con pointer/target; usalo para validar "
            "qué se accionó. Preferí target semántico (controlId/selector) y dejá "
            "coordenadas sólo como respaldo o diagnóstico.\n"
            "Cada acción desktop produce un receipt estructurado; si queda uncertain o "
            "failed, no declares éxito y entrá por wait/assert/repair.\n"
            "ANTI-LOOP: una acción -> una verificación por texto -> terminá. Los pasos "
            "[type]/[key]/[click] son intención; traducilos a desktop_* sobre la app. "
            "Un paso [stroke] es un ARRASTRE continuo (dibujo/pintura/swipe): reproducilo "
            "con desktop_stroke pasando sus 'points' (no lo partas en clicks). "
            "SINCRONIZACIÓN: en vez de desktop_wait con ms fijos, usá desktop_wait_for para "
            "esperar la ventana/control antes de actuar (más robusto ante latencia). "
            "Cada paso trae un 'target' con name/role/controlId del control real bajo el "
            "cursor: preferí re-localizarlo (desktop_controls + desktop_click_element por "
            "ese name/role) y dejá las coordenadas sólo como respaldo. "
            "VENTANA ENSEÑADA: si el target informa windowLabel, winWidth y winHeight, "
            "buscá esa ventana por título y restaurá ese tamaño con desktop_resize antes "
            "del click/trazo cuando no estaba maximizada. No arrastres sus bordes a mano. "
            "VERIFICACIÓN: un paso [assert] es una condición que DEBE cumplirse; reproducilo "
            "con desktop_assert o desktop_assert_image (PASS/FAIL). Cerrá la Task con una aserción del objetivo "
            "en vez de declarar éxito de memoria. "
            "No leas evidence/*.jpg con read_file.\n");
    } else if (mode == QLatin1String("browserBackground")) {
        out += QStringLiteral(
            "Superficie: navegador background al ejecutar. El Teach se grabó con un "
            "browser foreground de Playwright y evidencia visual por acción para entender "
            "la intención del usuario. Usá las tools de browser/Playwright para navegar y "
            "verificar páginas web; adaptá selectores, textos y posiciones si la interfaz "
            "cambió. Tratá selector/control como target primario; coordenadas de mouse "
            "son respaldo cuando no haya selector confiable. Registrá y validá cada click "
            "contra la salida/trace de la tool o el snapshot posterior.\n");
        if (recipe.value(QStringLiteral("networkDiscoveryEnabled")).toBool()
            || task.value(QStringLiteral("discoverNetwork")).toBool()) {
            out += QStringLiteral(
                "DESCUBRIMIENTO DE RED AUTORIZADO: después de la acción principal y su "
                "verificación visible, llamá una vez browser_network_discover con "
                "artifact_id=\"%1\" y action describiendo brevemente esa acción. La tool "
                "sólo observa requests ya vistos, redacta secretos y guarda el contrato "
                "para revisión; no reproduzcas endpoints ni eludas permisos.\n")
                       .arg(manifest.value(QStringLiteral("id")).toString());
        }
    }
    out += QStringLiteral("Artefacto: %1\n").arg(manifest.value(QStringLiteral("id")).toString());
    const QVariantList steps = recipe.value(QStringLiteral("steps")).toList();
    // Mantener la receta legible sin dejar que una secuencia larga de intenciones
    // repetitivas expulse del contexto las instrucciones operativas y los targets.
    // La información accionable adicional (target, ventana, puntos y aserciones)
    // se agrega por separado debajo de este resumen corto.
    constexpr int kIntentPromptLimit = 120;
    for (const QVariant &value : steps) {
        const QVariantMap step = value.toMap();
        out += QStringLiteral("- [%1] %2")
                   .arg(step.value(QStringLiteral("kind")).toString(),
                        step.value(QStringLiteral("intent")).toString().left(kIntentPromptLimit));
        if (step.contains(QStringLiteral("x")))
            out += QStringLiteral(" (referencia normalizada %1,%2)")
                       .arg(step.value(QStringLiteral("x")).toDouble(), 0, 'f', 3)
                       .arg(step.value(QStringLiteral("y")).toDouble(), 0, 'f', 3);
        const QString button = step.value(QStringLiteral("button")).toString();
        if (!button.isEmpty())
            out += QStringLiteral(" (botón %1)").arg(button);
        // Traza: emitir los puntos normalizados para que el modelo los pase tal cual
        // a desktop_stroke (submuestreados si son muchos, para no inflar el prompt).
        if (step.value(QStringLiteral("kind")).toString() == QLatin1String("stroke")) {
            const QVariantList pts = step.value(QStringLiteral("points")).toList();
            const int stride = pts.size() > 40 ? (pts.size() + 39) / 40 : 1;
            QStringList coords;
            for (int i = 0; i < pts.size(); i += stride) {
                const QVariantMap p = pts.at(i).toMap();
                coords << QStringLiteral("[%1,%2]")
                              .arg(p.value(QStringLiteral("x")).toDouble(), 0, 'f', 3)
                              .arg(p.value(QStringLiteral("y")).toDouble(), 0, 'f', 3);
            }
            if (stride > 1 && !pts.isEmpty()) {   // asegurar el último punto
                const QVariantMap last = pts.last().toMap();
                coords << QStringLiteral("[%1,%2]")
                              .arg(last.value(QStringLiteral("x")).toDouble(), 0, 'f', 3)
                              .arg(last.value(QStringLiteral("y")).toDouble(), 0, 'f', 3);
            }
            out += QStringLiteral(" (points=%1)").arg(coords.join(QLatin1Char(' ')));
        }
        // Aserción: emitir el texto esperado para reproducirla con desktop_assert.
        if (step.value(QStringLiteral("kind")).toString() == QLatin1String("assert")) {
            const QString expect = step.value(QStringLiteral("expectText")).toString();
            if (!expect.isEmpty())
                out += QStringLiteral(" (verificá con desktop_assert expect_text=\"%1\")")
                           .arg(expect.left(120));
        }
        const QVariantMap target = step.value(QStringLiteral("target")).toMap();
        const QString role = target.value(QStringLiteral("role")).toString();
        const QString name = target.value(QStringLiteral("name")).toString();
        if (!role.isEmpty() || !name.isEmpty())
            out += QStringLiteral(" (target %1 \"%2\")").arg(role, name.left(80));
        const QString windowLabel = target.value(QStringLiteral("windowLabel")).toString();
        const int winWidth = target.value(QStringLiteral("winWidth")).toInt();
        const int winHeight = target.value(QStringLiteral("winHeight")).toInt();
        if (!windowLabel.isEmpty() && winWidth > 0 && winHeight > 0) {
            out += QStringLiteral(" (ventana \"%1\", tamaño Teach %2x%3, maximizada=%4)")
                       .arg(windowLabel.left(80)).arg(winWidth).arg(winHeight)
                       .arg(target.value(QStringLiteral("windowMaximized")).toBool()
                                ? QStringLiteral("sí") : QStringLiteral("no"));
        }
        out += QLatin1Char('\n');
    }
    const QVariantList learnings = recipe.value(QStringLiteral("learnings")).toList();
    const QVariantList discoveries =
        recipe.value(QStringLiteral("networkDiscoveries")).toList();
    if (!discoveries.isEmpty()) {
        out += QStringLiteral("Contratos de red observados:\n");
        int shown = 0;
        for (int i = discoveries.size() - 1; i >= 0 && shown < 2; --i) {
            const QVariantMap discovery = discoveries.at(i).toMap();
            if (discovery.value(QStringLiteral("reviewStatus")).toString()
                == QLatin1String("rejected"))
                continue;
            ++shown;
            const QString action = discovery.value(QStringLiteral("action")).toString();
            const QString review = discovery.value(QStringLiteral("reviewStatus"),
                                                    QStringLiteral("pending")).toString();
            if (!action.isEmpty())
                out += QStringLiteral("- acción: %1 · revisión=%2\n")
                           .arg(action.left(160), review);
            for (const QVariant &ev : discovery.value(QStringLiteral("endpoints")).toList()) {
                const QVariantMap endpoint = ev.toMap();
                const QString statuses = QString::fromUtf8(QJsonDocument(
                    QJsonArray::fromVariantList(endpoint.value(QStringLiteral("statuses")).toList()))
                                                               .toJson(QJsonDocument::Compact));
                out += QStringLiteral("  %1 %2%3 · estados=%4 · observaciones=%5\n")
                           .arg(endpoint.value(QStringLiteral("method")).toString(),
                                endpoint.value(QStringLiteral("origin")).toString(),
                                endpoint.value(QStringLiteral("pathTemplate")).toString(),
                                statuses,
                                endpoint.value(QStringLiteral("count")).toString());
            }
        }
        out += QStringLiteral("Son evidencia de observación, no autorización para invocarlos directamente.\n");
    }
    // Dedup en el prompt: artefactos viejos ya acumularon varios learnings de
    // éxito casi idénticos (antes del dedup en appendLearning). Colapsarlos acá
    // por firma normalizada evita reinyectar el mismo texto N veces. Recorremos
    // de lo más RECIENTE a lo más viejo (adaptaciones nuevas primero) y cortamos
    // en 4 distintos: prompt más chico → primer turno más rápido.
    auto signature = [](const QString &s) {
        QString t;
        for (const QChar &c : s) {
            if (c.isLetter()) t += c.toLower();
            else if (c.isSpace() && !t.endsWith(QLatin1Char(' '))) t += QLatin1Char(' ');
        }
        return t.trimmed().left(160);
    };
    QStringList seenSig, picked;
    for (int i = learnings.size() - 1; i >= 0 && picked.size() < 2; --i) {
        const QString summary = learnings.at(i).toMap()
                                    .value(QStringLiteral("summary")).toString().trimmed();
        if (summary.isEmpty()) continue;
        const QString sig = signature(summary);
        if (seenSig.contains(sig)) continue;
        seenSig << sig;
        picked << summary.left(260);
    }
    if (!picked.isEmpty()) {
        out += QStringLiteral("Aprendizajes auto-actualizados de corridas previas:\n");
        for (const QString &p : picked)
            out += QStringLiteral("- %1\n").arg(p);
        out += QStringLiteral("Si durante esta corrida la interfaz cambió y lográs completar el objetivo, "
                              "explicá en la respuesta final qué adaptación funcionó para que el Teach "
                              "pueda actualizarse.\n");
    } else {
        out += QStringLiteral("Si la interfaz cambió y aun así lográs completar el objetivo, "
                              "documentá en la respuesta final qué adaptación funcionó para auto-actualizar el Teach.\n");
    }
    const QVariantMap l = limits(task);
    out += QStringLiteral("Límites: %1 acciones, %2 segundos, %3 reintentos.\n")
               .arg(l.value(QStringLiteral("maxActions")).toInt())
               .arg(l.value(QStringLiteral("timeoutSec")).toInt())
               .arg(l.value(QStringLiteral("maxRetries")).toInt());
    return out;
}

QString AutomationRunner::expandVariables(const QString &text, const QVariantMap &row)
{
    if (row.isEmpty() || !text.contains(QLatin1String("{{"))) return text;
    // Índice case-insensitive de la fila (los encabezados del usuario pueden
    // diferir en mayúsculas del {{placeholder}}).
    QHash<QString, QString> lut;
    for (auto it = row.constBegin(); it != row.constEnd(); ++it)
        lut.insert(it.key().trimmed().toLower(), it.value().toString());
    static const QRegularExpression ph(QStringLiteral("\\{\\{\\s*([^{}]+?)\\s*\\}\\}"));
    QString out;
    out.reserve(text.size());
    int last = 0;
    auto matches = ph.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch m = matches.next();
        out += text.mid(last, m.capturedStart() - last);
        const QString key = m.captured(1).trimmed().toLower();
        out += lut.contains(key) ? lut.value(key) : m.captured(0);  // clave ausente: intacto
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

namespace {
// Parser CSV mínimo con comillas dobles (RFC-4180-lite): campos entre "", ""→"
// literal, comas y saltos dentro de comillas respetados.
QStringList parseCsvLine(const QString &line)
{
    QStringList fields;
    QString cur;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) { cur += c; ++i; }
                else inQuotes = false;
            } else cur += c;
        } else if (c == QLatin1Char('"')) {
            inQuotes = true;
        } else if (c == QLatin1Char(',')) {
            fields << cur; cur.clear();
        } else {
            cur += c;
        }
    }
    fields << cur;
    return fields;
}
}  // namespace

QVariantList AutomationRunner::parseDataset(const QString &raw, const QString &format)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return {};
    QString fmt = format.trimmed().toLower();
    if (fmt.isEmpty())
        fmt = (trimmed.startsWith(QLatin1Char('[')) || trimmed.startsWith(QLatin1Char('{')))
                  ? QStringLiteral("json") : QStringLiteral("csv");

    QVariantList rows;
    if (fmt == QLatin1String("json")) {
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8());
        const QJsonArray arr = doc.isArray() ? doc.array()
                             : (doc.isObject() ? QJsonArray{doc.object()} : QJsonArray{});
        for (const QJsonValue &v : arr) {
            if (!v.isObject()) continue;
            QVariantMap row;
            const QJsonObject o = v.toObject();
            for (auto it = o.constBegin(); it != o.constEnd(); ++it)
                row.insert(it.key(), it.value().toVariant());
            if (!row.isEmpty()) rows << row;
        }
        return rows;
    }
    // CSV: primera línea no vacía = encabezados.
    const QStringList lines = trimmed.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    QStringList headers;
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) continue;
        const QStringList cells = parseCsvLine(line);
        if (headers.isEmpty()) {
            for (const QString &h : cells) headers << h.trimmed();
            continue;
        }
        QVariantMap row;
        for (int i = 0; i < headers.size() && i < cells.size(); ++i)
            row.insert(headers.at(i), cells.at(i));
        if (!row.isEmpty()) rows << row;
    }
    return rows;
}

QVariantList AutomationRunner::datasetRows(const QVariantMap &task)
{
    QVariantList rows;
    const QVariantList inlineRows = task.value(QStringLiteral("datasetRows")).toList();
    if (!inlineRows.isEmpty()) {
        for (const QVariant &v : inlineRows)
            if (v.canConvert<QVariantMap>()) rows << v.toMap();
    } else {
        const QString inlineText = task.value(QStringLiteral("datasetInline")).toString();
        const QString fmt = task.value(QStringLiteral("datasetFormat")).toString();
        if (!inlineText.trimmed().isEmpty()) {
            rows = parseDataset(inlineText, fmt);
        } else {
            const QString path = task.value(QStringLiteral("datasetPath")).toString().trimmed();
            if (!path.isEmpty()) {
                QFile f(path);
                if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    const QString detected = fmt.isEmpty() && path.toLower().endsWith(QLatin1String(".json"))
                                                 ? QStringLiteral("json") : fmt;
                    rows = parseDataset(QString::fromUtf8(f.readAll()), detected);
                }
            }
        }
    }
    if (rows.size() > 1000) rows = rows.mid(0, 1000);   // cota de seguridad
    return rows;
}

QVariantList AutomationRunner::fileWatchTriggers(const QVariantList &tasks)
{
    QVariantList out;
    for (const QVariant &tv : tasks) {
        const QVariantMap t = tv.toMap();
        if (t.value(QStringLiteral("triggerType")).toString() != QLatin1String("fileWatch"))
            continue;
        const QString path = t.value(QStringLiteral("triggerPath")).toString().trimmed();
        const QString id = t.value(QStringLiteral("id")).toString();
        if (path.isEmpty() || id.isEmpty()) continue;
        out << QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("path"), path},
            {QStringLiteral("debounceMs"),
             qBound(0, t.value(QStringLiteral("triggerDebounceMs"), 1500).toInt(), 60000)}};
    }
    return out;
}

QVariantList AutomationRunner::hotkeyTriggers(const QVariantList &tasks)
{
    QVariantList out;
    for (const QVariant &tv : tasks) {
        const QVariantMap t = tv.toMap();
        if (t.value(QStringLiteral("triggerType")).toString() != QLatin1String("hotkey"))
            continue;
        const QString hk = t.value(QStringLiteral("triggerHotkey")).toString().trimmed();
        const QString id = t.value(QStringLiteral("id")).toString();
        if (hk.isEmpty() || id.isEmpty()) continue;
        if (!parseHotkey(hk).value(QStringLiteral("valid")).toBool()) continue;
        out << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("hotkey"), hk}};
    }
    return out;
}

QVariantMap AutomationRunner::parseHotkey(const QString &spec)
{
    QStringList mods;
    QString key;
    const QStringList parts = spec.toUpper().split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString &raw : parts) {
        const QString p = raw.trimmed();
        if (p == QLatin1String("CTRL") || p == QLatin1String("CONTROL")) mods << QStringLiteral("CTRL");
        else if (p == QLatin1String("ALT")) mods << QStringLiteral("ALT");
        else if (p == QLatin1String("SHIFT")) mods << QStringLiteral("SHIFT");
        else if (p == QLatin1String("WIN") || p == QLatin1String("META")) mods << QStringLiteral("WIN");
        else if (!p.isEmpty()) key = p;   // la última no-modificadora es la tecla
    }
    mods.removeDuplicates();
    bool validKey = false;
    if (key.size() == 1 && (key.at(0).isLetterOrNumber())) validKey = true;
    else if (key.size() >= 2 && key.at(0) == QLatin1Char('F')) {
        bool ok = false;
        const int n = key.mid(1).toInt(&ok);
        validKey = ok && n >= 1 && n <= 24;
    }
    // Un atajo global necesita al menos un modificador + una tecla válida.
    const bool valid = validKey && !mods.isEmpty();
    return QVariantMap{{QStringLiteral("valid"), valid},
                       {QStringLiteral("mods"), mods},
                       {QStringLiteral("key"), key}};
}

QVariantList AutomationRunner::desktopReplaySteps(const QVariantMap &recipe)
{
    QVariantList out;
    for (const QVariant &sv : recipe.value(QStringLiteral("steps")).toList()) {
        const QVariantMap s = sv.toMap();
        const QString kind = s.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("key")) {
            out << QVariantMap{{QStringLiteral("kind"), kind},
                               {QStringLiteral("atMs"), s.value(QStringLiteral("atMs"))},
                               {QStringLiteral("key"), s.value(QStringLiteral("key"))},
                               {QStringLiteral("modifiers"), s.value(QStringLiteral("modifiers"))}};
        } else if (kind == QLatin1String("type")) {
            out << QVariantMap{{QStringLiteral("kind"), kind},
                               {QStringLiteral("atMs"), s.value(QStringLiteral("atMs"))},
                               {QStringLiteral("text"), s.value(QStringLiteral("text"))}};
        } else if (kind == QLatin1String("click")) {
            out << QVariantMap{{QStringLiteral("kind"), kind},
                               {QStringLiteral("atMs"), s.value(QStringLiteral("atMs"))},
                               {QStringLiteral("x"), s.value(QStringLiteral("x"))},
                               {QStringLiteral("y"), s.value(QStringLiteral("y"))},
                               {QStringLiteral("button"), s.value(QStringLiteral("button"), QStringLiteral("left"))},
                               {QStringLiteral("target"), s.value(QStringLiteral("target"))}};
        } else if (kind == QLatin1String("stroke")) {
            out << QVariantMap{{QStringLiteral("kind"), kind},
                               {QStringLiteral("atMs"), s.value(QStringLiteral("atMs"))},
                               {QStringLiteral("button"), s.value(QStringLiteral("button"), QStringLiteral("left"))},
                               {QStringLiteral("points"), s.value(QStringLiteral("points"))},
                               {QStringLiteral("target"), s.value(QStringLiteral("target"))}};
        }
    }
    return out;
}

bool AutomationRunner::windowTitleMatches(const QString &recorded, const QString &current)
{
    const QString r = recorded.trimmed();
    const QString c = current.trimmed();
    if (r.isEmpty() || c.isEmpty()) return false;
    if (r.compare(c, Qt::CaseInsensitive) == 0) return true;
    auto appSuffix = [](const QString &s) {
        int best = -1;
        for (const QString &sep : {QStringLiteral(" - "), QStringLiteral(" — "), QStringLiteral(": ")}) {
            const int i = s.lastIndexOf(sep);
            if (i >= 0) best = qMax(best, i + sep.size());
        }
        return (best >= 0 ? s.mid(best) : s).trimmed();
    };
    const QString rs = appSuffix(r), cs = appSuffix(c);
    return !rs.isEmpty() && rs.compare(cs, Qt::CaseInsensitive) == 0;
}

QVariantMap AutomationRunner::recordedWindowState(const QVariantMap &target,
                                                   const QVariantMap &scope)
{
    if (target.contains(QStringLiteral("windowMaximized")))
        return {{QStringLiteral("known"), true},
                {QStringLiteral("maximized"), target.value(QStringLiteral("windowMaximized")).toBool()},
                {QStringLiteral("width"), target.value(QStringLiteral("winWidth"))},
                {QStringLiteral("height"), target.value(QStringLiteral("winHeight"))}};

    const double sw = scope.value(QStringLiteral("width")).toDouble();
    const double sh = scope.value(QStringLiteral("height")).toDouble();
    const double ww = target.value(QStringLiteral("winWidth")).toDouble();
    const double wh = target.value(QStringLiteral("winHeight")).toDouble();
    if (sw > 0 && sh > 0 && ww > 0 && wh > 0 && ww / sw >= 0.94 && wh / sh >= 0.90)
        return {{QStringLiteral("known"), true}, {QStringLiteral("maximized"), true},
                {QStringLiteral("width"), ww}, {QStringLiteral("height"), wh}};
    if (ww > 0 && wh > 0)
        return {{QStringLiteral("known"), true}, {QStringLiteral("maximized"), false},
                {QStringLiteral("width"), ww}, {QStringLiteral("height"), wh}};
    return {{QStringLiteral("known"), false}};
}

QVariantList AutomationRunner::reanchorPointsToWindow(const QVariantList &points,
                                                      const QVariantMap &scope,
                                                      const QVariantMap &win)
{
    const double sx = scope.value(QStringLiteral("x")).toDouble();
    const double sy = scope.value(QStringLiteral("y")).toDouble();
    const double sw = scope.value(QStringLiteral("width")).toDouble();
    const double sh = scope.value(QStringLiteral("height")).toDouble();
    const double wx = win.value(QStringLiteral("x")).toDouble();
    const double wy = win.value(QStringLiteral("y")).toDouble();
    const double ww = win.value(QStringLiteral("width")).toDouble();
    const double wh = win.value(QStringLiteral("height")).toDouble();
    if (sw <= 0 || sh <= 0 || ww <= 0 || wh <= 0) return points;   // datos incompletos: sin cambio
    QVariantList out;
    out.reserve(points.size());
    for (const QVariant &pv : points) {
        const QVariantMap p = pv.toMap();
        const double absX = sx + p.value(QStringLiteral("x")).toDouble() * sw;
        const double absY = sy + p.value(QStringLiteral("y")).toDouble() * sh;
        out << QVariantMap{
            {QStringLiteral("x"), qBound(0.0, (absX - wx) / ww, 1.0)},
            {QStringLiteral("y"), qBound(0.0, (absY - wy) / wh, 1.0)}};
    }
    return out;
}

QVariantList AutomationRunner::buildRunReport(const QVariantList &agentMessages)
{
    QVariantList steps;
    int n = 0;
    for (const QVariant &mv : agentMessages) {
        const QVariantMap m = mv.toMap();
        if (m.value(QStringLiteral("role")).toString() != QLatin1String("toolcall")) continue;
        const QString tool = m.value(QStringLiteral("name")).toString();
        const QString output = m.value(QStringLiteral("output")).toString();
        const QString low = output.toLower();
        // Heurística de fallo: marcadores típicos en la salida de las tools nativas.
        const bool bad = low.contains(QStringLiteral(": fail"))
                         || low.contains(QStringLiteral(": error"))
                         || low.contains(QStringLiteral("[error"))
                         || low.contains(QStringLiteral("no encontrad"))
                         || low.contains(QStringLiteral("timeout"));
        steps << QVariantMap{
            {QStringLiteral("n"), ++n},
            {QStringLiteral("tool"), tool},
            {QStringLiteral("ok"), !bad},
            {QStringLiteral("summary"), output.simplified().left(300)}};
    }
    return steps;
}
