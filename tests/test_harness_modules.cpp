// Unit tests del CABLEADO de los módulos del HarnessSpec al backend:
//   - loop: sameCallLimit/failureSpiral/transportRetries/watchdogs configurables,
//     y el idle timeout con precedencia spec > env > default.
//   - context: compactación on/off + umbral, poda on/off, imágenes, dedup.
//   - prompt: directivas de usuario (.md) con gate `when` y tope de tamaño.
//   - protocol: auto | native | text.
//   - HarnessDirectiveStore: descubrimiento, override de proyecto, inválidas.
//
// Sin servidor: se usan los hooks *ForTest del backend (los mismos que ya usa
// test_agent_wire para compactación).

#include <QtTest>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include "core/agent/HarnessDirectiveStore.h"
#include "core/agent/AgentLifecycle.h"
#include "core/agent/LlamaAgentBackend.h"
#include "core/agent/RawChatBackend.h"
#include "core/profiles/HarnessSpec.h"

class HarnessModulesTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void loop_policyReachesBackend();
    void loop_watchdogUsesConfiguredTimeouts();
    void lifecycle_normalizesToolInputs();
    void context_compactionCanBeDisabled();
    void context_triggerChangesWhenCompactionKicksIn();
    void context_pruneCanBeDisabled();
    void context_keepLastImagesDropsOlderCaptures();
    void skills_policyReachesBackend();
    void protocol_modeIsHonoured();
    void directiveCondition_evaluatesFacts();
    void customDirectives_injectedAndGated();
    void directiveStore_listsLoadsAndRejectsInvalid();
    void directiveStore_projectOverridesGlobal();
    void directiveStore_savesEditsAndRemoves();
    void directiveStore_rejectsInvalidInput();
    void directiveFactKeys_matchTheFactsActuallyUsed();
    void memory_policyGovernsWhatGetsInjected();
    void knowledge_policyReachesBackend();
    void chat_moduleReachesThePreamble();

private:
    QTemporaryDir m_ws;
    QString writeDirective(const QString &root, const QString &slug, const QString &body,
                           const QString &when = QString(), bool validFrontmatter = true);
};

void HarnessModulesTests::initTestCase()
{
    QVERIFY(m_ws.isValid());
    QStandardPaths::setTestModeEnabled(true);
}

QString HarnessModulesTests::writeDirective(const QString &root, const QString &slug,
                                            const QString &body, const QString &when,
                                            bool validFrontmatter)
{
    QDir().mkpath(root);
    const QString path = QDir(root).filePath(slug + QStringLiteral(".md"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    QString text;
    if (validFrontmatter) {
        text = QStringLiteral("---\nname: %1\ndescription: test\n").arg(slug);
        if (!when.isEmpty()) text += QStringLiteral("when: %1\n").arg(when);
        text += QStringLiteral("---\n") + body + QLatin1Char('\n');
    } else {
        text = QStringLiteral("sin frontmatter\n") + body;
    }
    f.write(text.toUtf8());
    f.close();
    return path;
}

void HarnessModulesTests::loop_policyReachesBackend()
{
    LlamaAgentBackend be;
    QCOMPARE(be.loopPolicyForTest().sameCallLimit, 3);   // default histórico

    HarnessLoopModule loop;
    loop.set = true;
    loop.sameCallLimit = 1;
    loop.failureSpiral = 2;
    loop.transportRetries = 4;
    loop.quickToolTimeoutSec = 40;
    loop.credits = 3;
    loop.maxCredits = 5;
    be.setLoopPolicy(loop);

    QCOMPARE(be.loopPolicyForTest().sameCallLimit, 1);
    QCOMPARE(be.loopPolicyForTest().failureSpiral, 2);
    QCOMPARE(be.loopPolicyForTest().transportRetries, 4);
    QCOMPARE(be.loopPolicyForTest().quickToolTimeoutSec, 40);
}

void HarnessModulesTests::loop_watchdogUsesConfiguredTimeouts()
{
    // Tool rápida: manda quickTimeout. Tool web/MCP: manda webTimeout.
    QCOMPARE(LlamaAgentBackend::toolWatchdogSeconds(QStringLiteral("write_file"), {}, 15, 180), 15);
    QCOMPARE(LlamaAgentBackend::toolWatchdogSeconds(QStringLiteral("write_file"), {}, 45, 180), 45);
    QCOMPARE(LlamaAgentBackend::toolWatchdogSeconds(QStringLiteral("web_search"), {}, 15, 180), 180);
    QCOMPARE(LlamaAgentBackend::toolWatchdogSeconds(QStringLiteral("web_search"), {}, 15, 600), 600);
    QCOMPARE(LlamaAgentBackend::toolWatchdogSeconds(QStringLiteral("mcp__x__y"), {}, 15, 30), 30);
    // run_shell sigue mandando por su propio timeout_s (no lo pisa el perfil).
    QCOMPARE(LlamaAgentBackend::toolWatchdogSeconds(
                 QStringLiteral("run_shell"),
                 QJsonObject{{QStringLiteral("timeout_s"), 60}}, 15, 180), 75);
}

void HarnessModulesTests::lifecycle_normalizesToolInputs()
{
    const QStringList editPaths = AgentLifecycle::changedPathsFromToolInput(
        QStringLiteral("Edit"), QJsonObject{{QStringLiteral("file_path"),
                                             QStringLiteral("src\\main.cpp")}});
    QCOMPARE(editPaths, QStringList{QStringLiteral("src/main.cpp")});

    const QString patch = QStringLiteral(
        "*** Begin Patch\n*** Update File: src/a.cpp\n"
        "*** Add File: src/b.cpp\n*** End Patch");
    const QStringList patchPaths = AgentLifecycle::changedPathsFromToolInput(
        QStringLiteral("apply_patch"),
        QJsonObject{{QStringLiteral("command"), patch}});
    QCOMPARE(patchPaths, QStringList({QStringLiteral("src/a.cpp"),
                                      QStringLiteral("src/b.cpp")}));

    const QVariantMap event = AgentLifecycle::promptSubmit(
        QStringLiteral("s1"), QStringLiteral("C:/repo"), QStringLiteral("c1"),
        QStringLiteral("arreglar el parser"), 0);
    QCOMPARE(event.value(QStringLiteral("event")).toString(),
             QStringLiteral("prompt.submit"));
    QCOMPARE(event.value(QStringLiteral("sessionId")).toString(), QStringLiteral("s1"));
    QCOMPARE(event.value(QStringLiteral("prompt")).toString(),
             QStringLiteral("arreglar el parser"));
}

// Historial que, con ctx 8192, sí dispara compactación en modo nativo (mismo
// tamaño que usa test_agent_wire).
static QJsonArray bigHistory()
{
    QJsonArray msgs;
    msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                            {QStringLiteral("content"), QString(400, QLatin1Char('x'))}});
    msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                            {QStringLiteral("content"), QString(400, QLatin1Char('y'))}});
    for (int i = 0; i < 6; ++i) {
        msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                {QStringLiteral("content"), QString(1000, QLatin1Char('a'))}});
        msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), QString(1000, QLatin1Char('b'))}});
    }
    return msgs;
}

void HarnessModulesTests::context_compactionCanBeDisabled()
{
    int head = 0, keepFrom = 0;
    LlamaAgentBackend on;
    on.setCtxLimitForTest(8192);
    on.setApiMessagesForTest(bigHistory());
    QVERIFY2(on.planCompactionForTest(head, keepFrom),
             "con la política por default este historial debe compactar");

    LlamaAgentBackend off;
    HarnessContextModule ctx;
    ctx.set = true;
    ctx.compaction = false;
    off.setContextPolicy(ctx);
    off.setCtxLimitForTest(8192);
    off.setApiMessagesForTest(bigHistory());
    QVERIFY2(!off.planCompactionForTest(head, keepFrom),
             "compaction=false debe desactivar la compactación por completo");
}

void HarnessModulesTests::context_triggerChangesWhenCompactionKicksIn()
{
    // Historial mediano: con umbral alto entra en presupuesto; con umbral bajo no.
    auto history = []() {
        QJsonArray msgs;
        msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                {QStringLiteral("content"), QString(200, QLatin1Char('x'))}});
        msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), QString(200, QLatin1Char('y'))}});
        for (int i = 0; i < 8; ++i)
            msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                    {QStringLiteral("content"), QString(900, QLatin1Char('a'))}});
        return msgs;
    };
    int head = 0, keepFrom = 0;

    LlamaAgentBackend high;
    HarnessContextModule ctxHigh;
    ctxHigh.set = true;
    ctxHigh.compactionTrigger = 0.99;
    high.setContextPolicy(ctxHigh);
    high.setCtxLimitForTest(32768);
    high.setApiMessagesForTest(history());
    const bool compactsHigh = high.planCompactionForTest(head, keepFrom);

    LlamaAgentBackend low;
    HarnessContextModule ctxLow;
    ctxLow.set = true;
    ctxLow.compactionTrigger = 0.35;
    low.setContextPolicy(ctxLow);
    low.setCtxLimitForTest(32768);
    low.setApiMessagesForTest(history());
    const bool compactsLow = low.planCompactionForTest(head, keepFrom);

    // Un umbral más bajo nunca puede compactar MENOS que uno alto.
    QVERIFY2(compactsLow || !compactsHigh,
             "bajar compactionTrigger debe compactar al menos tanto como subirlo");
}

void HarnessModulesTests::context_pruneCanBeDisabled()
{
    // Dos llamadas idénticas con resultado repetido: la poda determinista
    // reemplaza el resultado viejo por un recibo.
    auto history = []() {
        QJsonArray m;
        m.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                             {QStringLiteral("content"), QStringLiteral("sys")}});
        m.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                             {QStringLiteral("content"), QStringLiteral("objetivo")}});
        for (int i = 0; i < 3; ++i) {
            QJsonObject call{
                {QStringLiteral("id"), QStringLiteral("c%1").arg(i)},
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"),
                 QJsonObject{{QStringLiteral("name"), QStringLiteral("grep")},
                             {QStringLiteral("arguments"), QStringLiteral("{\"pattern\":\"x\"}")}}}};
            m.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                                 {QStringLiteral("tool_calls"), QJsonArray{call}}});
            m.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("tool")},
                                 {QStringLiteral("tool_call_id"), QStringLiteral("c%1").arg(i)},
                                 {QStringLiteral("content"), QString(500, QLatin1Char('r'))}});
        }
        return m;
    };

    LlamaAgentBackend on;
    on.setCtxLimitForTest(8192);
    on.setApiMessagesForTest(history());
    QVERIFY2(on.pruneWorkingContextForTest() > 0,
             "la poda determinista debe reemplazar resultados repetidos");

    LlamaAgentBackend off;
    HarnessContextModule ctx;
    ctx.set = true;
    ctx.prune = false;
    off.setContextPolicy(ctx);
    off.setCtxLimitForTest(8192);
    off.setApiMessagesForTest(history());
    QCOMPARE(off.pruneWorkingContextForTest(), 0);
}

void HarnessModulesTests::context_keepLastImagesDropsOlderCaptures()
{
    auto withImages = []() {
        QJsonArray m;
        for (int i = 0; i < 3; ++i) {
            QJsonArray content{
                QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                            {QStringLiteral("text"), QStringLiteral("captura %1").arg(i)}},
                QJsonObject{{QStringLiteral("type"), QStringLiteral("image_url")},
                            {QStringLiteral("image_url"),
                             QJsonObject{{QStringLiteral("url"),
                                          QStringLiteral("data:image/png;base64,AAAA")}}}}};
            m.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                 {QStringLiteral("content"), content}});
        }
        return m;
    };
    auto imageCount = [](const QJsonArray &msgs) {
        int n = 0;
        for (const QJsonValue &mv : msgs)
            for (const QJsonValue &cv : mv.toObject().value(QStringLiteral("content")).toArray())
                if (cv.toObject().value(QStringLiteral("type")).toString()
                    == QLatin1String("image_url"))
                    ++n;
        return n;
    };

    QCOMPARE(imageCount(LlamaAgentBackend::trimStaleImages(withImages(), 1)), 1);
    QCOMPARE(imageCount(LlamaAgentBackend::trimStaleImages(withImages(), 2)), 2);
    QCOMPARE(imageCount(LlamaAgentBackend::trimStaleImages(withImages(), 0)), 0);
}

void HarnessModulesTests::skills_policyReachesBackend()
{
    LlamaAgentBackend be;
    HarnessSkillsModule policy;
    policy.set = true;
    policy.include = {QStringLiteral("autoprompt-coding"), QStringLiteral("peer-review")};
    policy.exclude = {QStringLiteral("peer-review")};
    be.setPortableSkillPolicy(policy);
    const HarnessSkillsModule effective = be.portableSkillPolicyForTest();
    const QStringList expectedInclude = {QStringLiteral("autoprompt-coding"),
                                         QStringLiteral("peer-review")};
    const QStringList expectedExclude = {QStringLiteral("peer-review")};
    QVERIFY(effective.set);
    QCOMPARE(effective.include, expectedInclude);
    QCOMPARE(effective.exclude, expectedExclude);
    QVERIFY(HarnessSkills::allows(effective, QStringLiteral("autoprompt-coding")));
    QVERIFY(!HarnessSkills::allows(effective, QStringLiteral("peer-review")));
}

void HarnessModulesTests::protocol_modeIsHonoured()
{
    LlamaAgentBackend be;
    QCOMPARE(be.toolProtocolForTest(), QStringLiteral("auto"));
    be.setToolProtocol(QStringLiteral("text"));
    QCOMPARE(be.toolProtocolForTest(), QStringLiteral("text"));
    be.setToolProtocol(QStringLiteral("native"));
    QCOMPARE(be.toolProtocolForTest(), QStringLiteral("native"));
    be.setToolProtocol(QStringLiteral("cualquier-cosa"));
    QCOMPARE(be.toolProtocolForTest(), QStringLiteral("auto"));   // valor inválido → auto
}

void HarnessModulesTests::directiveCondition_evaluatesFacts()
{
    const QVariantMap facts{{QStringLiteral("tools.desktop"), true},
                            {QStringLiteral("vision"), false}};
    QVERIFY(LlamaAgentBackend::directiveConditionMet(QString(), facts));       // sin gate
    QVERIFY(LlamaAgentBackend::directiveConditionMet(QStringLiteral("tools.desktop"), facts));
    QVERIFY(!LlamaAgentBackend::directiveConditionMet(QStringLiteral("vision"), facts));
    QVERIFY(LlamaAgentBackend::directiveConditionMet(QStringLiteral("!vision"), facts));
    QVERIFY(LlamaAgentBackend::directiveConditionMet(
        QStringLiteral("tools.desktop, !vision"), facts));
    QVERIFY(!LlamaAgentBackend::directiveConditionMet(
        QStringLiteral("tools.desktop, vision"), facts));
    // Hecho desconocido = no cumple: una directiva mal escrita no se cuela.
    QVERIFY(!LlamaAgentBackend::directiveConditionMet(QStringLiteral("no.existe"), facts));
}

void HarnessModulesTests::customDirectives_injectedAndGated()
{
    LlamaAgentBackend be;
    be.setCwdForTest(m_ws.path());
    const QString before = be.systemPromptForTest();
    QVERIFY(!before.contains(QStringLiteral("CUERPO-DIRECTIVA")));

    be.setCustomDirectives(QVariantList{
        QVariantMap{{QStringLiteral("slug"), QStringLiteral("mia")},
                    {QStringLiteral("body"), QStringLiteral("CUERPO-DIRECTIVA")},
                    {QStringLiteral("when"), QString()}},
        QVariantMap{{QStringLiteral("slug"), QStringLiteral("solo-vision")},
                    {QStringLiteral("body"), QStringLiteral("CUERPO-VISION")},
                    {QStringLiteral("when"), QStringLiteral("vision")}}});

    const QString prompt = be.systemPromptForTest();
    QVERIFY2(prompt.contains(QStringLiteral("CUERPO-DIRECTIVA")),
             "la directiva sin gate debe inyectarse");
    QVERIFY2(!prompt.contains(QStringLiteral("CUERPO-VISION")),
             "la directiva con when=vision no debe inyectarse sin mmproj");

    be.setVisionAvailable(true);
    QVERIFY(be.systemPromptForTest().contains(QStringLiteral("CUERPO-VISION")));

    // El tope de tamaño avisa pero NO trunca: cortar una instrucción al medio es
    // peor que un prompt largo.
    be.setPromptMaxChars(10);
    QVERIFY(be.systemPromptForTest().contains(QStringLiteral("CUERPO-DIRECTIVA")));
}

void HarnessModulesTests::directiveStore_listsLoadsAndRejectsInvalid()
{
    const QString root = HarnessDirectiveStore::projectRoot(m_ws.path());
    writeDirective(root, QStringLiteral("buena"), QStringLiteral("texto bueno"));
    writeDirective(root, QStringLiteral("rota"), QStringLiteral("texto"), QString(),
                   /*validFrontmatter=*/false);

    const QVariantList listed = HarnessDirectiveStore::list(m_ws.path());
    QStringList names;
    for (const QVariant &v : listed) names << v.toMap().value(QStringLiteral("name")).toString();
    QVERIFY(names.contains(QStringLiteral("buena")));
    QVERIFY2(!names.contains(QStringLiteral("rota")),
             "una directiva sin frontmatter queda fuera, no rompe el listado");

    const QVariantMap good = HarnessDirectiveStore::load(QStringLiteral("buena"), m_ws.path());
    QVERIFY(good.value(QStringLiteral("ok")).toBool());
    QCOMPARE(good.value(QStringLiteral("body")).toString(), QStringLiteral("texto bueno"));

    const QVariantMap missing = HarnessDirectiveStore::load(QStringLiteral("no-existe"),
                                                            m_ws.path());
    QVERIFY(!missing.value(QStringLiteral("ok")).toBool());
    QVERIFY(!missing.value(QStringLiteral("error")).toString().isEmpty());

    // loadMany saltea las inválidas y respeta el orden pedido.
    const QVariantList many = HarnessDirectiveStore::loadMany(
        {QStringLiteral("no-existe"), QStringLiteral("buena")}, m_ws.path());
    QCOMPARE(many.size(), 1);
    QCOMPARE(many.first().toMap().value(QStringLiteral("slug")).toString(),
             QStringLiteral("buena"));
}

void HarnessModulesTests::directiveStore_projectOverridesGlobal()
{
    const QString slug = QStringLiteral("compartida");
    writeDirective(HarnessDirectiveStore::globalRoot(), slug, QStringLiteral("GLOBAL"));
    const QVariantMap onlyGlobal = HarnessDirectiveStore::load(slug, m_ws.path());
    QVERIFY(onlyGlobal.value(QStringLiteral("ok")).toBool());
    QCOMPARE(onlyGlobal.value(QStringLiteral("body")).toString(), QStringLiteral("GLOBAL"));

    writeDirective(HarnessDirectiveStore::projectRoot(m_ws.path()), slug,
                   QStringLiteral("PROYECTO"));
    const QVariantMap overridden = HarnessDirectiveStore::load(slug, m_ws.path());
    QCOMPARE(overridden.value(QStringLiteral("body")).toString(), QStringLiteral("PROYECTO"));
    QCOMPARE(overridden.value(QStringLiteral("scope")).toString(), QStringLiteral("project"));
}

// Alta/edicion/baja desde la app: sin esto el usuario podia elegir directivas
// pero no crearlas, y la seccion se veia vacia sin explicacion.
void HarnessModulesTests::directiveStore_savesEditsAndRemoves()
{
    const QString slug = QStringLiteral("ciclo-completo");
    const QVariantMap saved = HarnessDirectiveStore::save(
        slug, QStringLiteral("una descripcion"), QStringLiteral("project.hasGit"),
        QStringLiteral("CUERPO ORIGINAL"), QStringLiteral("project"), m_ws.path());
    QVERIFY2(saved.value(QStringLiteral("ok")).toBool(),
             qPrintable(saved.value(QStringLiteral("error")).toString()));

    // Aparece en el catalogo y se carga con el frontmatter bien formado: lo que
    // se escribe tiene que poder releerse con el MISMO parser.
    QStringList names;
    for (const QVariant &v : HarnessDirectiveStore::list(m_ws.path()))
        names << v.toMap().value(QStringLiteral("name")).toString();
    QVERIFY(names.contains(slug));

    QVariantMap loaded = HarnessDirectiveStore::load(slug, m_ws.path());
    QVERIFY(loaded.value(QStringLiteral("ok")).toBool());
    QCOMPARE(loaded.value(QStringLiteral("body")).toString(), QStringLiteral("CUERPO ORIGINAL"));
    QCOMPARE(loaded.value(QStringLiteral("when")).toString(), QStringLiteral("project.hasGit"));
    QCOMPARE(loaded.value(QStringLiteral("description")).toString(),
             QStringLiteral("una descripcion"));

    // Edicion = guardar de nuevo con el mismo slug.
    QVERIFY(HarnessDirectiveStore::save(slug, QStringLiteral("otra descripcion"), QString(),
                                        QStringLiteral("CUERPO NUEVO"),
                                        QStringLiteral("project"), m_ws.path())
                .value(QStringLiteral("ok")).toBool());
    loaded = HarnessDirectiveStore::load(slug, m_ws.path());
    QCOMPARE(loaded.value(QStringLiteral("body")).toString(), QStringLiteral("CUERPO NUEVO"));
    QVERIFY(loaded.value(QStringLiteral("when")).toString().isEmpty());

    // Una global con el mismo nombre sigue perdiendo contra la de proyecto.
    QVERIFY(HarnessDirectiveStore::save(slug, QStringLiteral("global"), QString(),
                                        QStringLiteral("CUERPO GLOBAL"),
                                        QStringLiteral("global"))
                .value(QStringLiteral("ok")).toBool());
    QCOMPARE(HarnessDirectiveStore::load(slug, m_ws.path())
                 .value(QStringLiteral("body")).toString(),
             QStringLiteral("CUERPO NUEVO"));

    // Baja: se va la de proyecto y queda visible la global.
    QVERIFY(HarnessDirectiveStore::remove(slug, QStringLiteral("project"), m_ws.path())
                .value(QStringLiteral("ok")).toBool());
    QCOMPARE(HarnessDirectiveStore::load(slug, m_ws.path())
                 .value(QStringLiteral("body")).toString(),
             QStringLiteral("CUERPO GLOBAL"));
    QVERIFY(HarnessDirectiveStore::remove(slug, QStringLiteral("global"))
                .value(QStringLiteral("ok")).toBool());
    QVERIFY(!HarnessDirectiveStore::load(slug, m_ws.path())
                 .value(QStringLiteral("ok")).toBool());
    // Borrar algo que no existe es un error explicito, no un silencio.
    QVERIFY(!HarnessDirectiveStore::remove(slug, QStringLiteral("global"))
                 .value(QStringLiteral("ok")).toBool());
}

void HarnessModulesTests::directiveStore_rejectsInvalidInput()
{
    // Slug invalido: se rechaza ANTES de escribir (un archivo que despues no
    // carga es peor que un error temprano).
    QVERIFY(!HarnessDirectiveStore::save(QStringLiteral("Con Mayusculas Y Espacios"),
                                         QStringLiteral("d"), QString(), QStringLiteral("b"))
                 .value(QStringLiteral("ok")).toBool());
    // description obligatoria y cuerpo no vacio.
    QVERIFY(!HarnessDirectiveStore::save(QStringLiteral("sin-desc"), QString(), QString(),
                                         QStringLiteral("b"))
                 .value(QStringLiteral("ok")).toBool());
    QVERIFY(!HarnessDirectiveStore::save(QStringLiteral("sin-cuerpo"), QStringLiteral("d"),
                                         QString(), QStringLiteral("   "))
                 .value(QStringLiteral("ok")).toBool());
    // description/when multilinea romperian el frontmatter delimitado por ---.
    QVERIFY(!HarnessDirectiveStore::save(QStringLiteral("multilinea"),
                                         QStringLiteral("una\ndos"), QString(),
                                         QStringLiteral("b"))
                 .value(QStringLiteral("ok")).toBool());
    // Cuerpo sobredimensionado: el tope existe porque cada KB es contexto.
    const QString huge(HarnessDirectiveStore::kMaxDirectiveBytes + 1024, QLatin1Char('x'));
    QVERIFY(!HarnessDirectiveStore::save(QStringLiteral("gigante"), QStringLiteral("d"),
                                         QString(), huge)
                 .value(QStringLiteral("ok")).toBool());
    // Scope project sin workspace: error claro, no escribir en cualquier lado.
    QVERIFY(!HarnessDirectiveStore::save(QStringLiteral("sin-ws"), QStringLiteral("d"), QString(),
                                         QStringLiteral("b"), QStringLiteral("project"),
                                         QString())
                 .value(QStringLiteral("ok")).toBool());
}

// El catalogo de hechos que enumera la UI y los hechos que realmente evalua el
// gate `when` tienen que ser EL MISMO conjunto. Si divergen, el usuario escribe
// una condicion que nunca se cumple y la directiva queda fuera sin decir nada:
// justo el tipo de falla muda que no se nota hasta que el prompt esta mal.
void HarnessModulesTests::directiveFactKeys_matchTheFactsActuallyUsed()
{
    LlamaAgentBackend be;
    const QStringList declared = LlamaAgentBackend::directiveFactKeys();
    QStringList real = be.directiveFactsForTest().keys();
    QStringList sortedDeclared = declared;
    sortedDeclared.sort();
    real.sort();
    QCOMPARE(real, sortedDeclared);
    QVERIFY(!declared.isEmpty());
    // Y cada clave declarada es evaluable: `when` con esa clave no rebota por
    // desconocida (rebotaria si el nombre tuviera un typo en el catalogo).
    const QVariantMap facts = be.directiveFactsForTest();
    for (const QString &key : declared)
        QVERIFY2(facts.contains(key), qPrintable(key + " declarada pero no evaluada"));
}

// Memoria: cuantos hechos y si va la memoria de proyecto lo decide el modulo
// `memory`. Antes eran 12 hechos y 64 KB fijos, sin forma de apagarlo — lo
// primero que uno querria recortar en un perfil al limite de contexto.
void HarnessModulesTests::memory_policyGovernsWhatGetsInjected()
{
    // Workspace con memoria de proyecto en disco.
    const QString cwd = m_ws.path();
    QFile mem(QDir(cwd).filePath(QStringLiteral(".llamacode/memory.md")));
    QDir().mkpath(QFileInfo(mem).absolutePath());
    QVERIFY(mem.open(QIODevice::WriteOnly | QIODevice::Truncate));
    mem.write("MEMORIA-DEL-PROYECTO-MARCADOR\n");
    mem.close();

    LlamaAgentBackend be;
    be.setCwdForTest(cwd);
    QVERIFY2(be.systemPromptForTest().contains(QStringLiteral("MEMORIA-DEL-PROYECTO-MARCADOR")),
             "por default la memoria de proyecto se inyecta (comportamiento historico)");

    HarnessMemoryModule off;
    off.set = true;
    off.projectMemory = false;
    be.setMemoryPolicy(off);
    QVERIFY2(!be.systemPromptForTest().contains(QStringLiteral("MEMORIA-DEL-PROYECTO-MARCADOR")),
             "projectMemory=false la saca del prompt");
    QCOMPARE(be.memoryPolicyForTest().projectMemory, false);

    // El tope recorta el archivo en vez de leerlo entero.
    HarnessMemoryModule tiny;
    tiny.set = true;
    tiny.projectMemoryMaxChars = 8;
    be.setMemoryPolicy(tiny);
    const QString prompt = be.systemPromptForTest();
    QVERIFY(prompt.contains(QStringLiteral("MEMORIA-")));
    QVERIFY2(!prompt.contains(QStringLiteral("MEMORIA-DEL-PROYECTO-MARCADOR")),
             "con el tope chico el archivo entra recortado");

    // structuredFacts=0 apaga el recall aunque structuredEnabled siga en true:
    // pedir "cero hechos" y que igual inyecte seria una perilla que miente.
    HarnessMemoryModule zero;
    zero.set = true;
    zero.structuredFacts = 0;
    be.setMemoryPolicy(zero);
    QVERIFY(!be.systemPromptForTest().contains(QStringLiteral("Memoria estructurada relevante")));
}

void HarnessModulesTests::knowledge_policyReachesBackend()
{
    LlamaAgentBackend be;
    be.setCwdForTest(m_ws.path());
    HarnessKnowledgeModule policy;
    policy.set = true;
    policy.enabled = true;
    policy.preflight = true;
    policy.citeSources = true;
    policy.maxFacts = 4;
    policy.maxEdges = 6;
    policy.maxChars = 4096;

    be.setKnowledgePolicy(policy);
    const HarnessKnowledgeModule actual = be.knowledgePolicyForTest();
    QVERIFY(actual.enabled);
    QVERIFY(actual.preflight);
    QVERIFY(actual.citeSources);
    QCOMPARE(actual.maxFacts, 4);
    QCOMPARE(actual.maxEdges, 6);
    QCOMPARE(actual.maxChars, 4096);

    const QString prompt = be.systemPromptForTest();
    QVERIFY(prompt.contains(QStringLiteral("Protocolo de conocimiento")));
    QVERIFY(prompt.contains(QStringLiteral("Citá las fuentes")));
}

// Modo Chat: el modulo llega al preamble real que se manda al server.
void HarnessModulesTests::chat_moduleReachesThePreamble()
{
    // Sin systemExtra: el preamble es el historico (nota de no-thinking).
    QJsonArray base = RawChatBackend::buildSystemPreamble(false, false, QString());
    QCOMPARE(base.size(), 1);

    // Con instrucciones del perfil: van PRIMERAS (son del usuario, no formato).
    const QString extra = QStringLiteral("Respondé siempre en una línea.");
    QJsonArray withExtra = RawChatBackend::buildSystemPreamble(false, false, extra);
    QCOMPARE(withExtra.size(), 2);
    QCOMPARE(withExtra.first().toObject().value(QStringLiteral("content")).toString(), extra);
    QCOMPARE(withExtra.first().toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("system"));

    // Con thinking ON no se agrega la nota de "sin razonamiento", pero el extra
    // del perfil sigue estando: son cosas independientes.
    QJsonArray thinking = RawChatBackend::buildSystemPreamble(true, false, extra);
    QCOMPARE(thinking.size(), 1);
    QCOMPARE(thinking.first().toObject().value(QStringLiteral("content")).toString(), extra);

    // Espacios en blanco no cuentan como instruccion.
    QCOMPARE(RawChatBackend::buildSystemPreamble(false, false, QStringLiteral("   ")).size(), 1);
}

QTEST_MAIN(HarnessModulesTests)
#include "test_harness_modules.moc"
