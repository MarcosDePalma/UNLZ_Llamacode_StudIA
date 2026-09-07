// Bisección del PRESUPUESTO DE CONTEXTO del harness para el perfil MAX-Q.
//
// Contexto del bug: el perfil MAX-Q (Qwen3.6-27B IQ4_XS, ctx 262k, KV q4, full
// offload en una 3090 de 24 GB) "andaba bárbaro con el contexto completo". Después
// de varios commits del harness empezó a ir a ~4 t/s y a devolver respuestas
// vacías. El server (llama-server) arranca con la MISMA línea de comando que antes
// (verificado en los logs), así que la regresión no está en el server: está en
// CUÁNTO contexto le manda el harness en cada request. En un perfil al borde de la
// VRAM, cada KB extra de system prompt / schemas de tools agranda el KV vivo y tira
// el decode a swap (PCIe), degradando los t/s.
//
// Este test NO corre el modelo (no se puede en CI). Mide, byte a byte, qué inyecta
// cada toggle del harness en el contexto, y BISECA hasta aislar la regresión:
//   - Las secciones de system prompt por DIRECTIVA, que por defecto (perfil sin
//     agent-profile aplicado, m_directivesSet=false) están TODAS ON. Esas secciones
//     (discipline/testNet/projectContext/efficiency/style) NO existían cuando se
//     creó el perfil MAX-Q → bloat silencioso = la regresión principal.
//   - La inyección de memoria de proyecto (.llamacode/memory.md, cap 64 KB).
//   - Los schemas de tools MCP (filesystem + playwright = 37 tools).
//
// La red de regresión: fija que el camino "lean" (directivas vacías) recupera el
// presupuesto chico, y que el default "todas on" es medible y mucho mayor.

#include <QtTest>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <algorithm>
#include "core/agent/LlamaAgentBackend.h"
#include "core/profiles/ProfileTypes.h"

class AgentContextBudgetTests : public QObject
{
    Q_OBJECT
private slots:
    void defaultDirectivesAreAllOn_silentBloat();
    void perDirectiveContribution_bisect();
    void projectMemoryInjection_cost();
    void perBuiltinTool_byteRanking();
    void mcpToolSchemas_cost();
    void leanChatPreset_cutsToolBudget();
    void maxQ_beforeVsNow_regressionIsRecoverable();

private:
    // Bytes UTF-8 del system prompt para un set de directivas dado.
    static int sysBytes(LlamaAgentBackend &be, const QStringList &directives)
    {
        be.setDirectives(directives);
        return be.systemPromptForTest().toUtf8().size();
    }
    // 37 tools MCP sintéticas (14 filesystem + 23 playwright) con descripción y
    // schema realistas, para aproximar el payload real de buildToolSchemas().
    static QVariantList syntheticMcpTools()
    {
        auto mk = [](const QString &server, const QString &name) {
            QJsonObject schema{
                {"type", "object"},
                {"properties", QJsonObject{
                    {"path", QJsonObject{{"type","string"},
                        {"description","Ruta absoluta o relativa al recurso."}}},
                    {"content", QJsonObject{{"type","string"},
                        {"description","Contenido o selector a aplicar."}}},
                    {"options", QJsonObject{{"type","object"},
                        {"description","Opciones adicionales de la operación."}}}}},
                {"required", QJsonArray{"path"}}};
            return QVariant(QVariantMap{
                {"server", server},
                {"name", name},
                {"description", QStringLiteral("Tool %1 del servidor MCP %2. "
                    "Realiza la operación correspondiente sobre el recurso indicado.")
                    .arg(name, server)},
                {"schema", QString::fromUtf8(QJsonDocument(schema).toJson(QJsonDocument::Compact))}});
        };
        QVariantList out;
        for (int i = 0; i < 14; ++i) out << mk("filesystem", QStringLiteral("fs_op_%1").arg(i));
        for (int i = 0; i < 23; ++i) out << mk("playwright", QStringLiteral("pw_op_%1").arg(i));
        return out;
    }
};

// PASO 1 — La regresión raíz: un perfil sin directivas aplicadas (m_directivesSet
// false = histórico) ahora arrastra TODAS las secciones nuevas. Medimos lean vs
// default-todas-on: el default debe ser MUCHO más grande. Eso es el bloat que el
// perfil MAX-Q nunca tuvo cuando "andaba bárbaro".
void AgentContextBudgetTests::defaultDirectivesAreAllOn_silentBloat()
{
    LlamaAgentBackend lean;       // explícitamente sin secciones
    const int leanBytes = sysBytes(lean, QStringList{});

    LlamaAgentBackend def;        // SIN setDirectives() = comportamiento default
    const int defBytes = def.systemPromptForTest().toUtf8().size();

    qInfo() << "system prompt lean (dirs=[]):" << leanBytes << "bytes";
    qInfo() << "system prompt default (sin aplicar perfil):" << defBytes << "bytes";
    qInfo() << "bloat por default-todas-on:" << (defBytes - leanBytes) << "bytes";

    // El default mete discipline+testNet+projectContext+efficiency+style. Eso son
    // varios miles de bytes que el perfil viejo no enviaba. Si esto se rompe (el
    // default volvió chico) es porque alguien cambió la política — bien, pero hay
    // que actualizar el test a conciencia.
    QVERIFY2(defBytes > leanBytes + 1500,
             qPrintable(QStringLiteral("default sólo %1 bytes mayor que lean")
                            .arg(defBytes - leanBytes)));
}

// PASO 2 — Bisección por directiva: cuánto pesa cada sección por separado. Aísla
// las prosa-pesadas (discipline/testNet/projectContext) como las que más inflan.
void AgentContextBudgetTests::perDirectiveContribution_bisect()
{
    LlamaAgentBackend be;
    const int base = sysBytes(be, QStringList{});   // sin secciones

    struct Row { const char *key; int bytes; };
    QVector<Row> rows;
    for (const char *k : {"discipline", "testNet", "projectContext",
                          "efficiency", "style", "honey", "antiBias"}) {
        const int b = sysBytes(be, QStringList{QString::fromLatin1(k)}) - base;
        rows.push_back({k, b});
        qInfo() << "directiva" << k << "→ +" << b << "bytes";
    }

    // Cada directiva agrega contexto real (>0). Si alguna quedó en 0, la sección
    // no se está inyectando (gating roto).
    for (const Row &r : rows)
        QVERIFY2(r.bytes > 0, qPrintable(QStringLiteral("directiva %1 no agrega contexto")
                                             .arg(r.key)));

    // Las tres secciones de disciplina/tests/contexto son las que más pesan: son el
    // grueso del bloat que cayó sobre MAX-Q. Verificamos que cada una supere a la
    // más liviana (style) — documenta el ranking sin fijar números frágiles.
    auto bytesOf = [&](const char *k) {
        for (const Row &r : rows) if (qstrcmp(r.key, k) == 0) return r.bytes;
        return -1;
    };
    QVERIFY(bytesOf("projectContext") > bytesOf("style"));
    QVERIFY(bytesOf("discipline")     > bytesOf("style"));
    QVERIFY(bytesOf("testNet")        > bytesOf("style"));
}

// PASO 3 — Inyección de memoria de proyecto: si el cwd tiene .llamacode/memory.md,
// el harness lo mete ENTERO (cap 64 KB) en cada request. El cwd de MAX-Q es un
// proyecto real → costo fijo por turno, independiente de las directivas.
void AgentContextBudgetTests::projectMemoryInjection_cost()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    LlamaAgentBackend be;
    be.setDirectives(QStringList{});            // aislar: sólo el efecto de memoria
    be.setCwdForTest(dir.path());
    const int noMem = be.systemPromptForTest().toUtf8().size();

    // Memoria de ~8 KB, como la de un proyecto vivo.
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral(".llamacode")));
    const QString memPath = LlamaAgentBackend::memoryFilePath(dir.path());
    QFile f(memPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    const QByteArray chunk = "- nota durable del proyecto: convención importante.\n";
    QByteArray mem;
    while (mem.size() < 8 * 1024) mem += chunk;
    f.write(mem);
    f.close();

    const int withMem = be.systemPromptForTest().toUtf8().size();
    qInfo() << "memoria de proyecto inyectada:" << (withMem - noMem) << "bytes";

    // Debe sumar aproximadamente el tamaño del archivo (al menos 7 KB de los 8).
    QVERIFY2(withMem - noMem >= 7 * 1024,
             qPrintable(QStringLiteral("memoria sólo sumó %1 bytes").arg(withMem - noMem)));
}

// PASO 3b — Ranking por tool built-in: cuántos bytes pesa el schema de CADA tool.
// El payload built-in (22 KB) creció commit a commit (code_graph, hybrid_search,
// desktop, browser, research…). Esto muestra cuáles son las más gordas para saber
// qué recortar primero en un perfil al límite.
void AgentContextBudgetTests::perBuiltinTool_byteRanking()
{
    const QJsonArray schemas = LlamaAgentBackend::toolSchemas();
    QVERIFY(!schemas.isEmpty());

    struct Row { QString name; int bytes; };
    QVector<Row> rows;
    int total = 0;
    for (const QJsonValue &v : schemas) {
        const QJsonObject fn = v.toObject().value("function").toObject();
        const int b = QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact).size();
        rows.push_back({fn.value("name").toString(), b});
        total += b;
    }
    std::sort(rows.begin(), rows.end(),
              [](const Row &a, const Row &b) { return a.bytes > b.bytes; });

    qInfo() << "tools built-in:" << rows.size() << "· total" << total << "bytes";
    const int top = qMin(10, rows.size());
    for (int i = 0; i < top; ++i)
        qInfo().noquote() << QStringLiteral("  %1. %2 — %3 bytes")
            .arg(i + 1, 2).arg(rows[i].name, -22).arg(rows[i].bytes);

    // El ranking debe estar bien formado: cada tool aporta bytes, el total cuadra
    // y la más gorda pesa bastante más que la mediana (señala buenos candidatos a
    // recortar). Sin números frágiles: sólo invariantes.
    for (const Row &r : rows) QVERIFY(r.bytes > 0);
    const int median = rows[rows.size() / 2].bytes;
    QVERIFY2(rows.first().bytes > median * 2,
             "la tool más gorda debería superar holgadamente a la mediana");
}

// PASO 4 — Schemas de tools MCP: 37 tools (filesystem+playwright) se serializan en
// el request. Medimos su costo vs sólo las built-in. Es config del usuario, no una
// regresión del harness, pero es el otro gran consumidor de contexto en MAX-Q.
void AgentContextBudgetTests::mcpToolSchemas_cost()
{
    LlamaAgentBackend be;
    be.setDirectives(QStringList{});

    const int builtinBytes =
        QJsonDocument(be.toolSchemasForTest()).toJson(QJsonDocument::Compact).size();

    be.setMcpToolsForTest(syntheticMcpTools());
    const QJsonArray withMcp = be.toolSchemasForTest();
    const int withMcpBytes = QJsonDocument(withMcp).toJson(QJsonDocument::Compact).size();

    qInfo() << "tool schemas built-in:" << builtinBytes << "bytes";
    qInfo() << "tool schemas + 37 MCP:" << withMcpBytes << "bytes";
    qInfo() << "costo de 37 tools MCP:" << (withMcpBytes - builtinBytes) << "bytes";

    // Lazy discovery mantiene el catálogo completo fuera del request y expone
    // sólo dos meta-tools (mcp_search_tools / mcp_call_tool). Contamos por prefijo
    // "mcp_": si el descubrimiento lazy fallara y se filtraran las tools crudas
    // (mcp__<server>__<tool>, que también empiezan con "mcp_"), el conteo se
    // dispararía muy por encima de 2 → la aserción lo detecta.
    int mcpCount = 0;
    for (const QJsonValue &v : withMcp) {
        const QString n = v.toObject().value("function").toObject().value("name").toString();
        if (n.startsWith(QStringLiteral("mcp_"))) ++mcpCount;
    }
    QCOMPARE(mcpCount, 2);
    QVERIFY(withMcpBytes - builtinBytes < 3000);
    QStringList names;
    for (const QJsonValue &v : withMcp)
        names << v.toObject().value("function").toObject().value("name").toString();
    QVERIFY(names.contains(QStringLiteral("mcp_search_tools")));
    QVERIFY(names.contains(QStringLiteral("mcp_call_tool")));
}

// PASO 4b — El fix de un toque: el preset "Chat liviano" (agent-chat) recorta el
// payload de tools de forma masiva. Aplica el preset igual que applyAgentProfileCaps
// (disabled = catálogo − enabledTools, mcpEnabled=false) y compara contra "Máximo"
// (todo el catálogo + 37 MCP). Esto prueba que el preset es la palanca real.
void AgentContextBudgetTests::leanChatPreset_cutsToolBudget()
{
    const QVariantList mcp = syntheticMcpTools();

    // Traduce un AgentProfile a los setters del backend (espejo de
    // AppController::applyAgentProfileCaps) y devuelve los bytes de tool schemas.
    auto toolBytesFor = [&](const AgentProfile &ap) {
        LlamaAgentBackend be;
        be.setMcpToolsForTest(mcp);
        QStringList disabled;
        if (!ap.enabledTools.contains(QStringLiteral("*"))) {
            const QSet<QString> on(ap.enabledTools.cbegin(), ap.enabledTools.cend());
            for (const QVariant &v : LlamaAgentBackend::toolCatalog()) {
                const QString n = v.toMap().value(QStringLiteral("name")).toString();
                if (!on.contains(n)) disabled << n;
            }
        }
        be.setDisabledTools(disabled);
        be.setMcpToolsEnabled(ap.mcpEnabled);
        return QJsonDocument(be.toolSchemasForTest()).toJson(QJsonDocument::Compact).size();
    };

    const QList<AgentProfile> ps = AgentProfile::systemPresets();
    auto byId = [&](const QString &id) {
        for (const AgentProfile &p : ps) if (p.id == id) return p;
        return AgentProfile{};
    };
    const int chatBytes   = toolBytesFor(byId(QStringLiteral("agent-chat")));
    const int maximoBytes = toolBytesFor(byId(QStringLiteral("agent-maximo")));

    qInfo() << "tool schemas · Chat liviano:" << chatBytes << "bytes";
    qInfo() << "tool schemas · Máximo (+37 MCP):" << maximoBytes << "bytes";
    qInfo() << "recorte del preset Chat:" << (maximoBytes - chatBytes) << "bytes";

    // El preset Chat debe quedar MUY por debajo de Máximo: apaga la mayoría de las
    // built-in y todo MCP. Es el fix de un toque para MAX-Q.
    QVERIFY2(chatBytes * 3 < maximoBytes,
             qPrintable(QStringLiteral("Chat=%1 no es <<< Máximo=%2")
                            .arg(chatBytes).arg(maximoBytes)));
}

// PASO 5 — El veredicto: reconstruye el contexto de MAX-Q "antes" (secciones no
// existían → lean) vs "ahora" (default todas on + memoria + 37 MCP), y prueba que
// el FIX (volver a lean por directivas vacías) recupera el presupuesto. Esto es lo
// que hay que cambiar en el perfil para que MAX-Q vuelva a volar.
void AgentContextBudgetTests::maxQ_beforeVsNow_regressionIsRecoverable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral(".llamacode")));
    QFile f(LlamaAgentBackend::memoryFilePath(dir.path()));
    QVERIFY(f.open(QIODevice::WriteOnly));
    QByteArray mem;
    while (mem.size() < 8 * 1024) mem += "- memoria del proyecto.\n";
    f.write(mem);
    f.close();

    const QVariantList mcp = syntheticMcpTools();
    auto totalCtx = [&](bool defaultDirs) {
        LlamaAgentBackend be;
        be.setCwdForTest(dir.path());
        if (!defaultDirs) be.setDirectives(QStringList{});  // lean (fix)
        // si defaultDirs: NO setDirectives → todas on (estado actual de MAX-Q)
        be.setMcpToolsForTest(mcp);
        const int sys = be.systemPromptForTest().toUtf8().size();
        const int tools =
            QJsonDocument(be.toolSchemasForTest()).toJson(QJsonDocument::Compact).size();
        return sys + tools;
    };

    const int nowCtx  = totalCtx(/*defaultDirs=*/true);    // MAX-Q como va hoy
    const int fixCtx  = totalCtx(/*defaultDirs=*/false);   // MAX-Q con fix lean

    qInfo() << "contexto MAX-Q hoy (default todas las directivas):" << nowCtx << "bytes";
    qInfo() << "contexto MAX-Q con fix (directivas lean):" << fixCtx << "bytes";
    qInfo() << "recuperado por el fix:" << (nowCtx - fixCtx) << "bytes";

    // El fix (directivas vacías) tiene que recortar contexto de forma material:
    // ese recorte es exactamente el bloat que se le metió al perfil. Memoria y MCP
    // quedan iguales en ambos → la diferencia es 100% directivas del harness.
    QVERIFY2(nowCtx - fixCtx > 1500,
             qPrintable(QStringLiteral("el fix sólo recortó %1 bytes")
                            .arg(nowCtx - fixCtx)));
    QVERIFY(fixCtx < nowCtx);
}

QTEST_MAIN(AgentContextBudgetTests)
#include "test_agent_context_budget.moc"
