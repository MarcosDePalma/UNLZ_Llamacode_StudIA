// Unit tests de Perfiles de Agente:
//   - AgentProfile: serialización JSON ida/vuelta.
//   - systemPresets(): los 11 presets (incluye Browser Agent, Harness Next y los
//     perfiles de artefactos), sus
//     ids estables, capas acumulativas de tools y el sentinel "*" de Máximo.
//   - LlamaAgentBackend::directiveCatalog() + setDirectives() → buildSystemPrompt
//     incluye/excluye cada sección (gating por directiva); default = todas.
//   - ProfileManager: CRUD + duplicado + persistencia de AgentProfile, presets de
//     sistema inmutables (aislado vía LLAMACODE_PROFILES_DIR).
//
// NOTA: storagePath cachea la raíz en un 'static' en la primera construcción de
// ProfileManager del proceso → el env var se setea en initTestCase.

#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <algorithm>
#include "core/profiles/ProfileTypes.h"
#include "core/profiles/ProfileManager.h"
#include "core/agent/LlamaAgentBackend.h"

class AgentProfilesTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();

    void agentProfile_jsonRoundTrip();
    void systemPresets_shape();
    void presets_levelsRestrictCapabilities();
    void directiveCatalog_hasAllKeys();
    void setDirectives_gatesSystemPrompt();
    void setDirectives_defaultIncludesAll();
    void honey_isOptInOnly();
    void antiBias_isOptInOnly();
    void reviewTool_isCataloguedAndSchemaIsReadOnly();
    void mcpEnabled_roundTrips();
    void manager_crudAndPersistence();
    void manager_collapsesExactDuplicateAgentProfiles();
    void manager_systemPresetsImmutable();
    void manager_recommendsAndClonesTaskProfile();
    void personaStyle_jsonAndPromptBudget();
    void manager_personaStyleCrudAndImport();
    void manager_personaStyleDisabledAndPersonalityNoExamples();
    void manager_personaStyleListsAreTypedAndClearable();
    void manager_headlessPromptPreviewRanksRelevantExample();
    void manager_appliesModelAnalysisJsonSafely();
    void manager_rejectsInvalidImportAndClampsLimits();

private:
    QTemporaryDir m_dir;
};

void AgentProfilesTests::initTestCase()
{
    QVERIFY(m_dir.isValid());
    qputenv("LLAMACODE_PROFILES_DIR", m_dir.path().toLocal8Bit());
}

void AgentProfilesTests::cleanup()
{
    QDir dir(m_dir.path());
    dir.removeRecursively();
    QDir().mkpath(m_dir.path());
}

void AgentProfilesTests::agentProfile_jsonRoundTrip()
{
    AgentProfile p;
    p.id = "ap1"; p.name = "Custom";
    p.enabledTools = QStringList{"read_file", "grep", "run_shell"};
    p.directives = QStringList{"discipline", "style"};
    p.approvalMode = "manual";
    p.thinking = true;
    p.temperature = 0.4;
    p.systemExtra = "sé conciso";
    p.personalityProfileIds = {"persona-1"};
    p.styleProfileIds = {"style-1"};
    p.injectStyleExamples = false;
    p.styleExampleLimit = 1;
    p.styleContextLimit = 1200;
    const AgentProfile r = AgentProfile::fromJson(p.toJson());
    QCOMPARE(r.id, p.id);
    QCOMPARE(r.name, p.name);
    QCOMPARE(r.enabledTools, p.enabledTools);
    QCOMPARE(r.directives, p.directives);
    QCOMPARE(r.approvalMode, p.approvalMode);
    QCOMPARE(r.thinking, p.thinking);
    QCOMPARE(r.temperature, p.temperature);
    QCOMPARE(r.systemExtra, p.systemExtra);
    QCOMPARE(r.personalityProfileIds, p.personalityProfileIds);
    QCOMPARE(r.styleProfileIds, p.styleProfileIds);
    QCOMPARE(r.injectStyleExamples, p.injectStyleExamples);
    QCOMPARE(r.styleExampleLimit, p.styleExampleLimit);
    QCOMPARE(r.styleContextLimit, p.styleContextLimit);
    QCOMPARE(r.progressCredits, p.progressCredits);
    QCOMPARE(r.progressMaxCredits, p.progressMaxCredits);
    QCOMPARE(r.progressReplanAfter, p.progressReplanAfter);
    QCOMPARE(r.progressStopAfter, p.progressStopAfter);
    QCOMPARE(r.quickToolTimeoutSec, p.quickToolTimeoutSec);
}

void AgentProfilesTests::personaStyle_jsonAndPromptBudget()
{
    PersonaStyleProfile p;
    p.id = QStringLiteral("style-1"); p.name = QStringLiteral("Narrativo");
    p.kind = QStringLiteral("writing-style"); p.styleCard = QStringLiteral("Frases medias");
    p.examples = {QStringLiteral("Una muestra breve.")};
    const PersonaStyleProfile r = PersonaStyleProfile::fromJson(p.toJson());
    QCOMPARE(r.id, p.id); QCOMPARE(r.kind, p.kind); QCOMPARE(r.styleCard, p.styleCard);
    QCOMPARE(r.examples, p.examples);

    ProfileManager pm;
    const QString id = pm.addPersonaStyleProfile(QStringLiteral("Estilo"), QStringLiteral("writing-style"));
    QVERIFY(!id.isEmpty());
    QVERIFY(pm.updatePersonaStyleProfile({{"id", id}, {"styleCard", "voz cercana"},
                                          {"examples", QStringList{"Ejemplo uno."}}}));
    AgentProfile ap; ap.styleProfileIds = {id}; ap.styleContextLimit = 120;
    const QString rendered = pm.renderPersonaStyleContext(ap);
    QVERIFY(rendered.contains(QStringLiteral("voz cercana")));
    QVERIFY(rendered.size() <= 220); // margen por el cierre de instrucciones
}

void AgentProfilesTests::manager_personaStyleCrudAndImport()
{
    ProfileManager pm;
    const QString id = pm.addPersonaStyleProfile(QStringLiteral("Importable"), QStringLiteral("personality"));
    QVERIFY(!id.isEmpty());
    const QString json = pm.exportPersonaStyleProfile(id);
    QVERIFY(json.contains(QStringLiteral("Importable")));
    const QString imported = pm.importPersonaStyleProfile(json);
    QVERIFY(!imported.isEmpty()); QVERIFY(imported != id);
    QVERIFY(pm.removePersonaStyleProfile(imported));
    QVERIFY(pm.buildStyleAnalysisPrompt(QStringLiteral("Texto de prueba"), QStringLiteral("writing-style"))
                .contains(QStringLiteral("MUESTRA")));
    QVERIFY(pm.heuristicStyleCard(QStringLiteral("Una frase. Otra frase."))
                .contains(QStringLiteral("Promedio")));
}

void AgentProfilesTests::manager_personaStyleDisabledAndPersonalityNoExamples()
{
    ProfileManager pm;
    QSignalSpy stylesChanged(&pm, &ProfileManager::personaStylesChanged);
    const QString disabled = pm.addPersonaStyleProfile(QStringLiteral("Apagado"), QStringLiteral("writing-style"));
    const QString personality = pm.addPersonaStyleProfile(QStringLiteral("Conversacional"), QStringLiteral("personality"));
    QVERIFY(pm.updatePersonaStyleProfile({{"id", disabled}, {"enabled", false},
                                          {"styleCard", "NO DEBE APARECER"}}));
    QVERIFY(pm.updatePersonaStyleProfile({{"id", personality}, {"styleCard", "calmo"},
                                          {"examples", QStringList{"contenido privado que no debe entrar"}}}));
    AgentProfile ap;
    ap.personalityProfileIds = {disabled, personality};
    ap.styleContextLimit = 2000;
    const QString rendered = pm.renderPersonaStyleContext(ap);
    QVERIFY(!rendered.contains(QStringLiteral("NO DEBE APARECER")));
    QVERIFY(rendered.contains(QStringLiteral("calmo")));
    QVERIFY(!rendered.contains(QStringLiteral("contenido privado que no debe entrar")));
    QVERIFY(stylesChanged.count() >= 4); // altas + dos ediciones
}

void AgentProfilesTests::manager_personaStyleListsAreTypedAndClearable()
{
    ProfileManager pm;
    const QString persona = pm.addPersonaStyleProfile(QStringLiteral("P"), QStringLiteral("personality"));
    const QString style = pm.addPersonaStyleProfile(QStringLiteral("S"), QStringLiteral("writing-style"));
    const QVariantList personalities = pm.personalityProfiles();
    const QVariantList styles = pm.writingStyleProfiles();
    QVERIFY(!personalities.isEmpty()); QVERIFY(!styles.isEmpty());
    QCOMPARE(personalities.first().toMap().value("profileId").toString(), QString());
    QCOMPARE(styles.first().toMap().value("profileId").toString(), QString());
    QVERIFY(std::any_of(personalities.cbegin(), personalities.cend(), [&](const QVariant &v) {
        return v.toMap().value("profileId").toString() == persona;
    }));
    QVERIFY(std::any_of(styles.cbegin(), styles.cend(), [&](const QVariant &v) {
        return v.toMap().value("profileId").toString() == style;
    }));
    for (const QVariant &v : personalities)
        QVERIFY(v.toMap().value("profileId").toString() != style);
    for (const QVariant &v : styles)
        QVERIFY(v.toMap().value("profileId").toString() != persona);
}

void AgentProfilesTests::manager_headlessPromptPreviewRanksRelevantExample()
{
    ProfileManager pm;
    const QString style = pm.addPersonaStyleProfile(QStringLiteral("Ranked"), QStringLiteral("writing-style"));
    QVERIFY(pm.updatePersonaStyleProfile({{"id", style}, {"styleCard", "voz clara"},
        {"examples", QStringList{"bosque y río", "compilador y código"}}}));
    const QString agent = pm.addAgentProfile(QStringLiteral("Headless"));
    QVERIFY(pm.updateAgentProfile({{"id", agent}, {"styleProfileIds", QStringList{style}},
                                  {"styleExampleLimit", 1}, {"styleContextLimit", 2000}}));
    const QString preview = pm.previewPersonaStylePrompt(agent, QStringLiteral("necesito código del compilador"));
    QVERIFY(preview.contains(QStringLiteral("compilador y código")));
    QVERIFY(!preview.contains(QStringLiteral("bosque y río")));
}

void AgentProfilesTests::manager_appliesModelAnalysisJsonSafely()
{
    ProfileManager pm;
    const QString id = pm.addPersonaStyleProfile(QStringLiteral("Analizado"), QStringLiteral("writing-style"));
    QVERIFY(!pm.applyPersonaStyleAnalysis(id, QStringLiteral("respuesta sin json"), QStringLiteral("muestra")));
    const QString response = QStringLiteral("```json\n{\"schemaVersion\":1,\"description\":\"Cercano\","
        "\"styleCard\":\"Frases medias y sensoriales\",\"examples\":[\"Ejemplo validado\"]}\n```");
    QVERIFY(pm.applyPersonaStyleAnalysis(id, response, QStringLiteral("muestra original")));
    const QVariantMap out = pm.getPersonaStyleProfile(id);
    QCOMPARE(out.value("description").toString(), QStringLiteral("Cercano"));
    QCOMPARE(out.value("styleCard").toString(), QStringLiteral("Frases medias y sensoriales"));
    QCOMPARE(out.value("examples").toStringList(), QStringList{QStringLiteral("Ejemplo validado")});
}

void AgentProfilesTests::manager_rejectsInvalidImportAndClampsLimits()
{
    ProfileManager pm;
    QVERIFY(pm.importPersonaStyleProfile(QStringLiteral("not-json")).isEmpty());
    QVERIFY(pm.importPersonaStyleProfile(QStringLiteral("[]")).isEmpty());
    QVERIFY(pm.importPersonaStyleProfile(QStringLiteral("{} ")).isEmpty());

    const QString id = pm.addPersonaStyleProfile(QStringLiteral("Límites"),
                                                  QStringLiteral("writing-style"));
    QVERIFY(pm.updatePersonaStyleProfile({
        {"id", id}, {"maxExamples", 999}, {"maxChars", 1},
        {"examples", QStringList{QStringLiteral("uno"), QStringLiteral("dos")}}
    }));
    const QVariantMap profile = pm.getPersonaStyleProfile(id);
    QCOMPARE(profile.value("maxExamples").toInt(), 8);
    QCOMPARE(profile.value("maxChars").toInt(), 500);
    QCOMPARE(profile.value("examples").toStringList().size(), 2);

    const QString imported = pm.importPersonaStyleProfile(
        QStringLiteral("{\"name\":\"Importado\",\"kind\":\"unknown\","
                       "\"styleCard\":\"válido\",\"maxExamples\":-4,\"maxChars\":999999}"));
    QVERIFY(!imported.isEmpty());
    const QVariantMap importedProfile = pm.getPersonaStyleProfile(imported);
    QCOMPARE(importedProfile.value("kind").toString(), QStringLiteral("writing-style"));
    QCOMPARE(importedProfile.value("maxExamples").toInt(), 0);
    QCOMPARE(importedProfile.value("maxChars").toInt(), 20000);
}

void AgentProfilesTests::systemPresets_shape()
{
    const QList<AgentProfile> ps = AgentProfile::systemPresets();
    QCOMPARE(ps.size(), 11);

    // Orden: la escalera histórica primero, luego la variante comparable del
    // harness Next y finalmente los perfiles de propósito/artefactos.
    QCOMPARE(ps[0].id, QStringLiteral("agent-chat"));
    QCOMPARE(ps[1].id, QStringLiteral("agent-basico"));
    QCOMPARE(ps[2].id, QStringLiteral("agent-intermedio"));
    QCOMPARE(ps[3].id, QStringLiteral("agent-avanzado"));
    QCOMPARE(ps[4].id, QStringLiteral("agent-maximo"));
    QCOMPARE(ps[5].id, QStringLiteral("agent-intermedio-next"));
    QCOMPARE(ps[6].id, QStringLiteral("agent-minimal"));
    QCOMPARE(ps[7].id, QStringLiteral("agent-rpa"));
    QCOMPARE(ps[8].id, QStringLiteral("agent-browser"));
    QCOMPARE(ps[9].id, QStringLiteral("agent-artifact-local"));
    QCOMPARE(ps[10].id, QStringLiteral("agent-artifact-publisher"));
    QCOMPARE(AgentProfile::defaultPresetId(), QStringLiteral("agent-intermedio"));

    for (const AgentProfile &p : ps) QVERIFY(p.system);

    auto byId = [&](const QString &id) {
        for (const AgentProfile &p : ps) if (p.id == id) return p;
        return AgentProfile{};
    };
    const AgentProfile chat   = byId(QStringLiteral("agent-chat"));
    const AgentProfile basico = byId(QStringLiteral("agent-basico"));
    const AgentProfile inter  = byId(QStringLiteral("agent-intermedio"));
    const AgentProfile avanz  = byId(QStringLiteral("agent-avanzado"));
    const AgentProfile maximo = byId(QStringLiteral("agent-maximo"));
    const AgentProfile next   = byId(QStringLiteral("agent-intermedio-next"));
    const AgentProfile browser = byId(QStringLiteral("agent-browser"));
    const AgentProfile artifactLocal = byId(QStringLiteral("agent-artifact-local"));
    const AgentProfile artifactPublisher = byId(QStringLiteral("agent-artifact-publisher"));

    QVERIFY(next.hasSpec);
    QCOMPARE(next.spec.runtime.engine, QStringLiteral("next"));
    QCOMPARE(next.spec.runtime.version, 2);
    QCOMPARE(next.spec.runtime.fallbackEngine, QStringLiteral("legacy"));
    QVERIFY(next.spec.runtime.experimental);
    QCOMPARE(next.enabledTools, inter.enabledTools);

    // Chat liviano: set mínimo, SIN directivas y SIN MCP (presupuesto chico).
    // agent-minimal también apaga MCP por la misma razón; el resto lo trae.
    QVERIFY(chat.enabledTools.contains(QStringLiteral("read_file")));
    QVERIFY(chat.enabledTools.contains(QStringLiteral("edit_file")));
    QVERIFY(chat.directives.isEmpty());
    QVERIFY(!chat.mcpEnabled);
    QVERIFY(chat.enabledTools.size() < basico.enabledTools.size());
    for (const AgentProfile &p : {basico, inter, avanz, maximo})
        QVERIFY2(p.mcpEnabled, qPrintable(p.id + " debería traer MCP"));

    // Básico: core de archivos/código, sin directivas.
    QVERIFY(basico.enabledTools.contains(QStringLiteral("read_file")));
    QVERIFY(basico.enabledTools.contains(QStringLiteral("run_shell")));
    QVERIFY(basico.directives.isEmpty());

    // Capas acumulativas: Intermedio ⊇ Básico; Avanzado ⊇ Intermedio.
    for (const QString &t : basico.enabledTools) QVERIFY(inter.enabledTools.contains(t));
    for (const QString &t : inter.enabledTools) QVERIFY(avanz.enabledTools.contains(t));
    QVERIFY(inter.directives.contains(QStringLiteral("discipline")));
    QVERIFY(avanz.directives.contains(QStringLiteral("testNet")));
    QVERIFY(avanz.thinking);
    QVERIFY(avanz.hasSpec);
    QVERIFY(avanz.spec.context.preflight);
    QCOMPARE(avanz.spec.context.indexPolicy, QStringLiteral("lazy"));

    // Browser Agent nativo: tools web/browser acotadas, skills portables
    // apagadas por defecto y contrato modular explícito.
    QVERIFY(browser.hasSpec);
    QCOMPARE(browser.spec.tools.packs,
             QStringList({QStringLiteral("core"), QStringLiteral("web"),
                          QStringLiteral("browser")}));
    QVERIFY(browser.enabledTools.contains(QStringLiteral("web_search")));
    QVERIFY(browser.enabledTools.contains(QStringLiteral("browser_skill_replay")));
    QVERIFY(browser.spec.skills.set);
    QVERIFY(browser.spec.skills.include.isEmpty());
    QCOMPARE(browser.spec.loop.webToolTimeoutSec, 300);
    QVERIFY(!browser.enabledTools.contains(QStringLiteral("desktop_windows")));
    QVERIFY(!browser.enabledTools.contains(QStringLiteral("email_send")));

    // Artefactos: el perfil local conserva el flujo privado y reproducible;
    // publisher suma web/browser, pero mantiene approval ask y el contrato de
    // stash separado de publish.
    QVERIFY(artifactLocal.hasSpec);
    QCOMPARE(artifactLocal.spec.tools.packs, QStringList{QStringLiteral("core")});
    QVERIFY(!artifactLocal.spec.tools.mcpToolsEnabled);
    QVERIFY(!artifactLocal.enabledTools.contains(QStringLiteral("web_fetch")));
    QVERIFY(!artifactLocal.enabledTools.contains(QStringLiteral("browser_skill_replay")));
    QVERIFY(artifactLocal.spec.prompt.systemExtra.contains(QStringLiteral("privada/local")));
    QVERIFY(artifactLocal.spec.prompt.systemExtra.contains(QStringLiteral("aprobación humana")));

    QVERIFY(artifactPublisher.hasSpec);
    QCOMPARE(artifactPublisher.spec.tools.packs,
             QStringList({QStringLiteral("core"), QStringLiteral("web"),
                          QStringLiteral("browser")}));
    QVERIFY(artifactPublisher.spec.tools.mcpToolsEnabled);
    QVERIFY(artifactPublisher.enabledTools.contains(QStringLiteral("web_fetch")));
    QVERIFY(artifactPublisher.enabledTools.contains(QStringLiteral("browser_skill_replay")));
    QCOMPARE(artifactPublisher.spec.permissions.approvalMode, QStringLiteral("ask"));
    QVERIFY(artifactPublisher.spec.permissions.hitlDestructive);
    QVERIFY(artifactPublisher.spec.prompt.systemExtra.contains(QStringLiteral("publicar")));
    QVERIFY(artifactPublisher.spec.prompt.systemExtra.contains(QStringLiteral("confirmación explícita")));
    QCOMPARE(artifactPublisher.spec.loop.webToolTimeoutSec, 300);

    // Máximo: sentinel "*" = todo el catálogo, super + thinking.
    QCOMPARE(maximo.enabledTools, QStringList{QStringLiteral("*")});
    QCOMPARE(maximo.directives, QStringList{QStringLiteral("*")});
    QCOMPARE(maximo.approvalMode, QStringLiteral("super"));
    QVERIFY(maximo.thinking);
    QVERIFY(maximo.hasSpec);
    QVERIFY(maximo.spec.context.preflight);
    QVERIFY(chat.progressCredits < maximo.progressCredits);
    QVERIFY(chat.progressMaxCredits < maximo.progressMaxCredits);
    QVERIFY(chat.quickToolTimeoutSec > 0);
}

// Garantía clave para el benchmark por NIVEL: distintos niveles = distintas
// capacidades, así una comparación a "Básico" está realmente más restringida que
// a "Máximo". Si un preset deja de restringir, la comparación deja de ser justa.
void AgentProfilesTests::presets_levelsRestrictCapabilities()
{
    const QList<AgentProfile> ps = AgentProfile::systemPresets();
    auto byId = [&](const QString &id) {
        for (const AgentProfile &p : ps) if (p.id == id) return p;
        return AgentProfile{};
    };
    const AgentProfile basico = byId(QStringLiteral("agent-basico"));
    const AgentProfile avanzado = byId(QStringLiteral("agent-avanzado"));
    const AgentProfile maximo = byId(QStringLiteral("agent-maximo"));

    // Básico NO trae web ni búsqueda avanzada (debe estar acotado).
    QVERIFY(!basico.enabledTools.contains(QStringLiteral("web_search")));
    QVERIFY(!basico.enabledTools.contains(QStringLiteral("hybrid_search")));
    QVERIFY(!basico.enabledTools.contains(QStringLiteral("*")));

    // Avanzado SÍ las trae → estrictamente más capaz que Básico.
    QVERIFY(avanzado.enabledTools.contains(QStringLiteral("web_search")));
    QVERIFY(avanzado.enabledTools.size() > basico.enabledTools.size());

    // Máximo = "*" (todo el catálogo): cubre cualquier tool built-in.
    QVERIFY(maximo.enabledTools.contains(QStringLiteral("*")));
    QVERIFY(!LlamaAgentBackend::toolCatalog().isEmpty());
}

void AgentProfilesTests::directiveCatalog_hasAllKeys()
{
    const QVariantList cat = LlamaAgentBackend::directiveCatalog();
    QStringList keys;
    for (const QVariant &v : cat) keys << v.toMap().value("key").toString();
    for (const QString &k : {"discipline", "testNet", "projectContext",
                             "efficiency", "style", "honey", "antiBias"})
        QVERIFY2(keys.contains(k), qPrintable("falta directiva " + k));
    // Cada item tiene name + description no vacíos (para la UI de toggles).
    for (const QVariant &v : cat) {
        const QVariantMap m = v.toMap();
        QVERIFY(!m.value("name").toString().isEmpty());
        QVERIFY(!m.value("description").toString().isEmpty());
    }
}

void AgentProfilesTests::setDirectives_gatesSystemPrompt()
{
    LlamaAgentBackend be;
    // Solo "discipline": presente; las otras secciones ausentes.
    be.setDirectives(QStringList{"discipline"});
    const QString only = be.systemPromptForTest();
    QVERIFY(only.contains(QStringLiteral("Blast radius"), Qt::CaseInsensitive));
    QVERIFY(!only.contains(QStringLiteral("RED DE TESTS")));        // testNet off
    QVERIFY(!only.contains(QStringLiteral("CONTEXTO DEL PROYECTO"))); // projectContext off
    QVERIFY(!only.contains(QStringLiteral("ESTILO:")));            // style off

    // Solo "style": presente; discipline ausente.
    be.setDirectives(QStringList{"style"});
    const QString styled = be.systemPromptForTest();
    QVERIFY(styled.contains(QStringLiteral("ESTILO:")));
    QVERIFY(!styled.contains(QStringLiteral("Blast radius"), Qt::CaseInsensitive));

    // Vacío: ninguna sección de directiva.
    be.setDirectives(QStringList{});
    const QString none = be.systemPromptForTest();
    QVERIFY(!none.contains(QStringLiteral("Blast radius"), Qt::CaseInsensitive));
    QVERIFY(!none.contains(QStringLiteral("RED DE TESTS")));
    QVERIFY(!none.contains(QStringLiteral("ESTILO:")));
}

void AgentProfilesTests::setDirectives_defaultIncludesAll()
{
    // Sin setDirectives() = comportamiento histórico: todas las secciones ON
    // (no regresiona los perfiles/usuarios que no aplican un perfil de agente).
    LlamaAgentBackend be;
    const QString sp = be.systemPromptForTest();
    QVERIFY(sp.contains(QStringLiteral("Blast radius"), Qt::CaseInsensitive));
    QVERIFY(sp.contains(QStringLiteral("RED DE TESTS")));
    QVERIFY(sp.contains(QStringLiteral("CONTEXTO DEL PROYECTO")));
    QVERIFY(sp.contains(QStringLiteral("ESTILO:")));
    QVERIFY(sp.contains(QStringLiteral("EFICIENCIA")));
}

// Honey (frugalidad) es opt-in PURO: a diferencia del resto, NO se incluye en el
// default "todas las directivas on" (sin setDirectives). Es agresiva y en modelos
// chicos locales puede recortar el razonamiento, así que sólo aparece si el perfil
// la elige explícitamente.
void AgentProfilesTests::honey_isOptInOnly()
{
    LlamaAgentBackend be;
    // Default (sin setDirectives): NINGUNA sección Honey, aunque el resto esté on.
    QVERIFY(!be.systemPromptForTest().contains(QStringLiteral("FRUGALIDAD (Honey)")));

    // Catálogo completo explícito tampoco la trae si no se nombra honey.
    be.setDirectives(QStringList{"discipline", "testNet", "projectContext",
                                 "efficiency", "style"});
    QVERIFY(!be.systemPromptForTest().contains(QStringLiteral("FRUGALIDAD (Honey)")));

    // El sentinel "*" (preset Máximo = todo el catálogo) TAMPOCO la trae: honey
    // se nombra o no está. Garantiza que ningún preset de sistema la incluya.
    be.setDirectives(QStringList{"*"});
    QVERIFY(!be.systemPromptForTest().contains(QStringLiteral("FRUGALIDAD (Honey)")));

    // Elegida explícitamente: presente.
    be.setDirectives(QStringList{"honey"});
    const QString honey = be.systemPromptForTest();
    QVERIFY(honey.contains(QStringLiteral("FRUGALIDAD (Honey)")));
    QVERIFY(honey.contains(QStringLiteral("¿hace falta?")));
    QVERIFY(honey.contains(QStringLiteral("¿ya existe en este código?")));
    QVERIFY(honey.contains(QStringLiteral("validación, seguridad")));
    QVERIFY(honey.contains(QStringLiteral("accesibilidad")));
}

// Anti-sesgo es opt-in PURO como honey: endurece el razonamiento pero alarga el
// prompt y en modelos chicos puede inducir trap-paranoia, así que NO entra en el
// default "todas on" ni en el sentinel "*"; sólo si el perfil la nombra.
void AgentProfilesTests::antiBias_isOptInOnly()
{
    LlamaAgentBackend be;
    // Default (sin setDirectives): ausente, aunque el resto esté on.
    QVERIFY(!be.systemPromptForTest().contains(QStringLiteral("ANTI-SESGO")));

    // Catálogo completo explícito sin nombrarla: ausente.
    be.setDirectives(QStringList{"discipline", "testNet", "projectContext",
                                 "efficiency", "style"});
    QVERIFY(!be.systemPromptForTest().contains(QStringLiteral("ANTI-SESGO")));

    // Sentinel "*" (preset Máximo): tampoco la trae.
    be.setDirectives(QStringList{"*"});
    QVERIFY(!be.systemPromptForTest().contains(QStringLiteral("ANTI-SESGO")));

    // Elegida explícitamente: presente.
    be.setDirectives(QStringList{"antiBias"});
    QVERIFY(be.systemPromptForTest().contains(QStringLiteral("ANTI-SESGO")));
}

void AgentProfilesTests::reviewTool_isCataloguedAndSchemaIsReadOnly()
{
    bool inCatalog = false;
    for (const QVariant &value : LlamaAgentBackend::toolCatalog()) {
        if (value.toMap().value(QStringLiteral("name")).toString()
                == QLatin1String("review_overengineering")) {
            inCatalog = true;
            break;
        }
    }
    QVERIFY(inCatalog);

    bool found = false;
    for (const QJsonValue &value : LlamaAgentBackend::toolSchemas()) {
        const QJsonObject fn = value.toObject().value(QStringLiteral("function")).toObject();
        if (fn.value(QStringLiteral("name")).toString()
                != QLatin1String("review_overengineering")) continue;
        found = true;
        const QString description = fn.value(QStringLiteral("description")).toString();
        QVERIFY(description.contains(QStringLiteral("READ-ONLY")));
        QVERIFY(description.contains(QStringLiteral("No modifica archivos")));
        const QJsonObject props = fn.value(QStringLiteral("parameters"))
                                      .toObject().value(QStringLiteral("properties")).toObject();
        QVERIFY(props.contains(QStringLiteral("scope")));
        QVERIFY(props.contains(QStringLiteral("max_diff_chars")));
    }
    QVERIFY(found);
}

// mcpEnabled persiste por JSON y default es true para perfiles legacy (sin la
// clave) → no regresiona perfiles guardados antes de la feature.
void AgentProfilesTests::mcpEnabled_roundTrips()
{
    AgentProfile p;
    p.id = QStringLiteral("x"); p.name = QStringLiteral("X");
    p.mcpEnabled = false;
    const AgentProfile back = AgentProfile::fromJson(p.toJson());
    QVERIFY(!back.mcpEnabled);

    AgentProfile q;
    q.mcpEnabled = true;
    QVERIFY(AgentProfile::fromJson(q.toJson()).mcpEnabled);

    // Legacy: JSON sin la clave mcpEnabled → default true (MCP on, histórico).
    QJsonObject legacy{{"id", "y"}, {"name", "Y"}};
    QVERIFY(AgentProfile::fromJson(legacy).mcpEnabled);
}

void AgentProfilesTests::manager_crudAndPersistence()
{
    QString newId;
    {
        ProfileManager pm;
        newId = pm.addAgentProfile(QStringLiteral("Mi perfil"));
        QVERIFY(!newId.isEmpty());

        // Editar: tools/directivas/ajustes.
        QVariantMap upd{
            {"id", newId},
            {"name", "Mi perfil v2"},
            {"enabledTools", QStringList{"read_file", "grep"}},
            {"directives", QStringList{"discipline", "testNet"}},
            {"approvalMode", "manual"},
            {"thinking", true},
            {"temperature", 0.3},
            {"progressCredits", 11},
            {"progressMaxCredits", 22},
            {"progressReplanAfter", 4},
            {"progressStopAfter", 7},
            {"quickToolTimeoutSec", 20},
            {"systemExtra", "extra"}};
        QVERIFY(pm.updateAgentProfile(upd));

        const QVariantMap got = pm.getAgentProfile(newId);
        QCOMPARE(got.value("name").toString(), QStringLiteral("Mi perfil v2"));
        QCOMPARE(got.value("enabledTools").toStringList(), (QStringList{"read_file", "grep"}));
        QCOMPARE(got.value("directives").toStringList(), (QStringList{"discipline", "testNet"}));
        QCOMPARE(got.value("approvalMode").toString(), QStringLiteral("manual"));
        QVERIFY(got.value("thinking").toBool());
        QCOMPARE(got.value("progressCredits").toInt(), 11);
        QCOMPARE(got.value("progressMaxCredits").toInt(), 22);
        QCOMPARE(got.value("progressReplanAfter").toInt(), 4);
        QCOMPARE(got.value("progressStopAfter").toInt(), 7);
        QCOMPARE(got.value("quickToolTimeoutSec").toInt(), 20);

        // Duplicar.
        const QString dupId = pm.duplicateAgentProfile(newId);
        QVERIFY(!dupId.isEmpty() && dupId != newId);
        QVERIFY(pm.getAgentProfile(dupId).value("name").toString().contains("copia"));
    }
    // Persistencia entre instancias.
    {
        ProfileManager pm2;
        const QVariantMap got = pm2.getAgentProfile(newId);
        QCOMPARE(got.value("name").toString(), QStringLiteral("Mi perfil v2"));
        QVERIFY(pm2.removeAgentProfile(newId));
        QVERIFY(pm2.getAgentProfile(newId).isEmpty());
    }
}

void AgentProfilesTests::manager_collapsesExactDuplicateAgentProfiles()
{
    AgentProfile first;
    first.id = QStringLiteral("duplicate-a");
    first.name = QStringLiteral("Perfil repetido");
    first.enabledTools = QStringList{QStringLiteral("read_file")};
    first.directives = QStringList{QStringLiteral("discipline")};
    AgentProfile second = first;
    second.id = QStringLiteral("duplicate-b");

    QJsonArray items;
    items.append(first.toJson());
    items.append(second.toJson());
    QFile file(QDir(m_dir.path()).filePath(QStringLiteral("agent_profiles.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(items).toJson(QJsonDocument::Compact));
    file.close();

    {
        ProfileManager pm;
        QCOMPARE(pm.getAgentProfile(QStringLiteral("duplicate-a")).value("name").toString(),
                 QStringLiteral("Perfil repetido"));
        QVERIFY(pm.getAgentProfile(QStringLiteral("duplicate-b")).isEmpty());
    }

    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonArray persisted = QJsonDocument::fromJson(file.readAll()).array();
    file.close();
    QCOMPARE(persisted.size(), 1);
}

void AgentProfilesTests::manager_systemPresetsImmutable()
{
    ProfileManager pm;
    // Los 4 presets de sistema están presentes y son solo lectura.
    QVERIFY(pm.isSystemAgentProfile(QStringLiteral("agent-basico")));
    QVERIFY(pm.isSystemAgentProfile(QStringLiteral("agent-maximo")));
    QVERIFY(!pm.getAgentProfile(QStringLiteral("agent-intermedio")).isEmpty());

    // No se pueden editar ni borrar.
    QVERIFY(!pm.updateAgentProfile(QVariantMap{{"id", "agent-basico"}, {"name", "hack"}}));
    QVERIFY(!pm.removeAgentProfile(QStringLiteral("agent-basico")));
    QCOMPARE(pm.getAgentProfile(QStringLiteral("agent-basico")).value("name").toString(),
             QStringLiteral("Básico"));

    // Duplicar un preset de sistema produce una copia EDITABLE de usuario.
    const QString dupId = pm.duplicateAgentProfile(QStringLiteral("agent-avanzado"));
    QVERIFY(!dupId.isEmpty());
    QVERIFY(!pm.isSystemAgentProfile(dupId));
    QVERIFY(pm.updateAgentProfile(QVariantMap{{"id", dupId}, {"name", "editable"}}));
}

void AgentProfilesTests::manager_recommendsAndClonesTaskProfile()
{
    ProfileManager pm;
    const QVariantMap rec = pm.recommendAgentProfile(
        QStringLiteral("Investigá fuentes y verificá las afirmaciones del informe"),
        QStringLiteral("agent-basico"));
    QCOMPARE(rec.value("kind").toString(), QStringLiteral("research"));
    QVERIFY(rec.value("thinking").toBool());
    QVERIFY(rec.value("mcpEnabled").toBool());

    const QString id = pm.createRecommendedAgentProfile(
        QStringLiteral("agent-basico"), rec.value("kind").toString());
    QVERIFY(!id.isEmpty());
    QVERIFY(!pm.isSystemAgentProfile(id));
    const QVariantMap clone = pm.getAgentProfile(id);
    QVERIFY(clone.value("name").toString().contains(QStringLiteral("Investigación")));
    QCOMPARE(clone.value("temperature").toDouble(), 0.35);
    QVERIFY(clone.value("enabledTools").toStringList().contains(QStringLiteral("verify_claims")));

    // Una copia ya optimizada no vuelve a ofrecer exactamente lo mismo.
    QVERIFY(pm.recommendAgentProfile(
        QStringLiteral("Research con fuentes"), id).isEmpty());
    // Una charla sin señal suficiente no dispara sugerencias intrusivas.
    QVERIFY(pm.recommendAgentProfile(QStringLiteral("Hola"), id).isEmpty());
}

QTEST_MAIN(AgentProfilesTests)
#include "test_agent_profiles.moc"
