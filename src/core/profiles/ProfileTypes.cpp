#include "ProfileTypes.h"
#include <QJsonArray>
#include <QUuid>

// ---- helpers ----
static QMap<QString, QString> mapFromJson(const QJsonObject &obj) {
    QMap<QString, QString> m;
    for (auto it = obj.begin(); it != obj.end(); ++it)
        m[it.key()] = it.value().toString();
    return m;
}
static QJsonObject mapToJson(const QMap<QString, QString> &m) {
    QJsonObject obj;
    for (auto it = m.begin(); it != m.end(); ++it)
        obj[it.key()] = it.value();
    return obj;
}
static QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

// ---- BackendProfile ----
QJsonObject BackendProfile::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name; o["binaryId"] = binaryId;
    o["host"] = host; o["port"] = port;
    o["baseArgs"] = QJsonArray::fromStringList(baseArgs);
    o["envOverrides"] = mapToJson(envOverrides);
    o["kind"] = kind;
    o["cloudBaseUrl"] = cloudBaseUrl;
    o["cloudKeyRef"] = cloudKeyRef;   // sólo el nombre de la ref, nunca el secreto
    o["cloudModel"] = cloudModel;
    o["cloudCtx"] = cloudCtx;
    return o;
}
BackendProfile BackendProfile::fromJson(const QJsonObject &o) {
    BackendProfile p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.binaryId = o["binaryId"].toString();
    p.host = o["host"].toString("127.0.0.1");
    p.port = o["port"].toInt(8080);
    for (const auto &v : o["baseArgs"].toArray()) p.baseArgs.append(v.toString());
    p.envOverrides = mapFromJson(o["envOverrides"].toObject());
    p.kind = o["kind"].toString("local");
    if (p.kind.isEmpty()) p.kind = "local";
    p.cloudBaseUrl = o["cloudBaseUrl"].toString();
    p.cloudKeyRef = o["cloudKeyRef"].toString();
    p.cloudModel = o["cloudModel"].toString();
    p.cloudCtx = o["cloudCtx"].toInt(0);
    return p;
}
QString BackendProfile::generateId() { return newId(); }

// ---- ModelProfile ----
QJsonObject ModelProfile::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name; o["modelId"] = modelId;
    o["mmprojId"] = mmprojId; o["draftModelId"] = draftModelId;
    o["modelStableId"] = static_cast<double>(modelStableId);
    o["mmprojStableId"] = static_cast<double>(mmprojStableId);
    o["draftStableId"] = static_cast<double>(draftStableId);
    o["specType"] = specType; o["specDraftNMax"] = specDraftNMax;
    o["specDraftNMin"] = specDraftNMin;
    o["specDraftAdaptive"] = specDraftAdaptive;
    o["specDraftConfMin"] = specDraftConfMin;
    o["specDraftNgl"] = specDraftNgl;
    o["specDraftTypeK"] = specDraftTypeK; o["specDraftTypeV"] = specDraftTypeV;
    return o;
}
ModelProfile ModelProfile::fromJson(const QJsonObject &o) {
    ModelProfile p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.modelId = o["modelId"].toString();
    p.mmprojId = o["mmprojId"].toString();
    p.draftModelId = o["draftModelId"].toString();
    p.modelStableId = static_cast<qint64>(o["modelStableId"].toDouble(0));
    p.mmprojStableId = static_cast<qint64>(o["mmprojStableId"].toDouble(0));
    p.draftStableId = static_cast<qint64>(o["draftStableId"].toDouble(0));
    p.specType = o["specType"].toString();
    p.specDraftNMax = o["specDraftNMax"].toInt(0);
    p.specDraftNMin = qMax(0, o["specDraftNMin"].toInt(0));
    p.specDraftAdaptive = o["specDraftAdaptive"].toBool(false);
    p.specDraftConfMin = qBound(0.0, o["specDraftConfMin"].toDouble(0.0), 1.0);
    p.specDraftNgl = o["specDraftNgl"].toString();
    p.specDraftTypeK = o["specDraftTypeK"].toString();
    p.specDraftTypeV = o["specDraftTypeV"].toString();
    return p;
}
QString ModelProfile::generateId() { return newId(); }

// ---- RuntimePreset ----
QJsonObject RuntimePreset::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name;
    o["ctx"] = ctx; o["batch"] = batch; o["ubatch"] = ubatch;
    o["threads"] = threads; o["gpuLayers"] = gpuLayers;
    o["flashAttention"] = flashAttention; o["mmap"] = mmap;
    o["mlock"] = mlock; o["contBatching"] = contBatching;
    o["cacheType"] = cacheType; o["parallelSlots"] = parallelSlots;
    if (!tensorOverrides.isEmpty())
        o["tensorOverrides"] = QJsonArray::fromStringList(tensorOverrides);
    return o;
}
RuntimePreset RuntimePreset::fromJson(const QJsonObject &o) {
    RuntimePreset p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.ctx = o["ctx"].toInt(4096); p.batch = o["batch"].toInt(512);
    p.ubatch = o["ubatch"].toInt(512); p.threads = o["threads"].toInt(-1);
    p.gpuLayers = o["gpuLayers"].toInt(-1);
    p.flashAttention = o["flashAttention"].toBool(false);
    p.mmap = o["mmap"].toBool(true); p.mlock = o["mlock"].toBool(false);
    p.contBatching = o["contBatching"].toBool(true);
    p.cacheType = o["cacheType"].toString("q8_0");
    p.parallelSlots = o["parallelSlots"].toInt(1);
    for (const auto &v : o["tensorOverrides"].toArray()) {
        const QString s = v.toString().trimmed();
        if (!s.isEmpty()) p.tensorOverrides.append(s);
    }
    return p;
}
QString RuntimePreset::generateId() { return newId(); }

// ---- HarnessProfile ----
QJsonObject HarnessProfile::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name; o["adapter"] = adapter;
    o["args"] = QJsonArray::fromStringList(args);
    o["env"] = mapToJson(env);
    return o;
}
HarnessProfile HarnessProfile::fromJson(const QJsonObject &o) {
    HarnessProfile p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.adapter = o["adapter"].toString("none");
    for (const auto &v : o["args"].toArray()) p.args.append(v.toString());
    p.env = mapFromJson(o["env"].toObject());
    return p;
}
QString HarnessProfile::generateId() { return newId(); }

// ---- WorkspaceProfile ----
QJsonObject WorkspaceProfile::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name; o["cwd"] = cwd;
    o["allowedPaths"] = QJsonArray::fromStringList(allowedPaths);
    o["blockedPaths"] = QJsonArray::fromStringList(blockedPaths);
    o["allowShellCommands"] = allowShellCommands;
    return o;
}
WorkspaceProfile WorkspaceProfile::fromJson(const QJsonObject &o) {
    WorkspaceProfile p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.cwd = o["cwd"].toString();
    for (const auto &v : o["allowedPaths"].toArray()) p.allowedPaths.append(v.toString());
    for (const auto &v : o["blockedPaths"].toArray()) p.blockedPaths.append(v.toString());
    p.allowShellCommands = o["allowShellCommands"].toBool(false);
    return p;
}
QString WorkspaceProfile::generateId() { return newId(); }

// ---- PersonaStyleProfile ----
QJsonObject PersonaStyleProfile::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name; o["kind"] = kind;
    o["description"] = description; o["styleCard"] = styleCard;
    o["examples"] = QJsonArray::fromStringList(examples);
    o["enabled"] = enabled;
    o["maxExamples"] = maxExamples; o["maxChars"] = maxChars;
    return o;
}
PersonaStyleProfile PersonaStyleProfile::fromJson(const QJsonObject &o) {
    PersonaStyleProfile p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.kind = o["kind"].toString() == QLatin1String("personality")
        ? QStringLiteral("personality") : QStringLiteral("writing-style");
    p.description = o["description"].toString();
    p.styleCard = o["styleCard"].toString();
    for (const auto &v : o["examples"].toArray()) p.examples.append(v.toString());
    p.enabled = o["enabled"].toBool(true);
    p.maxExamples = qBound(0, o["maxExamples"].toInt(2), 8);
    p.maxChars = qBound(500, o["maxChars"].toInt(6000), 20000);
    return p;
}
QString PersonaStyleProfile::generateId() { return newId(); }

// ---- AgentProfile ----
// Spec efectivo: el declarado (hasSpec) o el derivado de los campos legacy.
// La derivación es la MIGRACIÓN: cualquier perfil guardado antes del harness
// modular entra al pipeline nuevo sin cambiar de comportamiento.
HarnessSpec AgentProfile::toSpec() const {
    if (hasSpec) {
        HarnessSpec s = spec;
        if (s.extends.isEmpty()) s.extends = extendsId;
        return s;
    }
    HarnessSpec s;
    s.extends = extendsId;

    s.tools.set = true;
    s.tools.include = enabledTools;   // incluye el token "*" tal cual
    s.tools.mcpToolsEnabled = mcpEnabled;

    s.prompt.set = true;
    s.prompt.builtin = directives;    // idem con "*"
    s.prompt.systemExtra = systemExtra;

    s.loop.set = true;
    s.loop.credits = progressCredits;
    s.loop.maxCredits = progressMaxCredits;
    s.loop.replanAfter = progressReplanAfter;
    s.loop.stopAfter = progressStopAfter;
    s.loop.quickToolTimeoutSec = quickToolTimeoutSec;

    s.permissions.set = true;
    s.permissions.approvalMode = approvalMode;

    s.protocol.set = true;
    s.protocol.thinking = thinking;
    s.protocol.thinkingLeakGuard = thinkingLeakGuard;
    s.protocol.temperature = temperature;
    return s;
}

// Espeja un spec resuelto a los campos legacy. Sirve para dos cosas: que un
// binario viejo lea el perfil sin romperse, y que la UI/headless que todavía
// mira los campos planos vea lo mismo que el harness aplica.
void AgentProfile::applySpecToLegacyFields(const HarnessSpec &resolved) {
    if (resolved.tools.set) {
        enabledTools = resolved.tools.include.contains(QStringLiteral("*"))
                           ? QStringList{QStringLiteral("*")}
                           : HarnessTools::resolve(resolved.tools);
        mcpEnabled = resolved.tools.mcpToolsEnabled;
    }
    if (resolved.prompt.set) {
        directives = resolved.prompt.builtin;
        systemExtra = resolved.prompt.systemExtra;
    }
    if (resolved.loop.set) {
        progressCredits = resolved.loop.credits;
        progressMaxCredits = resolved.loop.maxCredits;
        progressReplanAfter = resolved.loop.replanAfter;
        progressStopAfter = resolved.loop.stopAfter;
        quickToolTimeoutSec = resolved.loop.quickToolTimeoutSec;
    }
    if (resolved.permissions.set)
        approvalMode = resolved.permissions.approvalMode;
    if (resolved.protocol.set) {
        thinking = resolved.protocol.thinking;
        thinkingLeakGuard = resolved.protocol.thinkingLeakGuard;
        temperature = resolved.protocol.temperature;
    }
}

QJsonObject AgentProfile::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name;
    o["enabledTools"] = QJsonArray::fromStringList(enabledTools);
    o["directives"] = QJsonArray::fromStringList(directives);
    o["approvalMode"] = approvalMode;
    o["thinking"] = thinking;
    o["temperature"] = temperature;
    o["systemExtra"] = systemExtra;
    o["personalityProfileIds"] = QJsonArray::fromStringList(personalityProfileIds);
    o["styleProfileIds"] = QJsonArray::fromStringList(styleProfileIds);
    o["injectStyleExamples"] = injectStyleExamples;
    o["styleExampleLimit"] = styleExampleLimit;
    o["styleContextLimit"] = styleContextLimit;
    o["mcpEnabled"] = mcpEnabled;
    o["thinkingLeakGuard"] = thinkingLeakGuard;
    o["progressCredits"] = progressCredits;
    o["progressMaxCredits"] = progressMaxCredits;
    o["progressReplanAfter"] = progressReplanAfter;
    o["progressStopAfter"] = progressStopAfter;
    o["quickToolTimeoutSec"] = quickToolTimeoutSec;
    if (!extendsId.isEmpty()) o["extends"] = extendsId;
    if (hasSpec) o["spec"] = spec.toJson();
    return o;
}
AgentProfile AgentProfile::fromJson(const QJsonObject &o) {
    AgentProfile p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    for (const auto &v : o["enabledTools"].toArray()) p.enabledTools.append(v.toString());
    for (const auto &v : o["directives"].toArray()) p.directives.append(v.toString());
    p.approvalMode = o["approvalMode"].toString("ask");
    if (p.approvalMode.isEmpty()) p.approvalMode = "ask";
    p.thinking = o["thinking"].toBool(false);
    p.temperature = o["temperature"].toDouble(-1.0);
    p.systemExtra = o["systemExtra"].toString();
    for (const auto &v : o["personalityProfileIds"].toArray()) p.personalityProfileIds.append(v.toString());
    for (const auto &v : o["styleProfileIds"].toArray()) p.styleProfileIds.append(v.toString());
    p.injectStyleExamples = o["injectStyleExamples"].toBool(true);
    p.styleExampleLimit = qBound(0, o["styleExampleLimit"].toInt(2), 8);
    p.styleContextLimit = qBound(0, o["styleContextLimit"].toInt(6000), 20000);
    p.mcpEnabled = o["mcpEnabled"].toBool(true);   // legacy sin la clave = MCP on
    p.thinkingLeakGuard = o["thinkingLeakGuard"].toBool(false);
    p.progressCredits = qMax(2, o["progressCredits"].toInt(8));
    p.progressMaxCredits = qMax(p.progressCredits, o["progressMaxCredits"].toInt(16));
    p.progressReplanAfter = qMax(2, o["progressReplanAfter"].toInt(3));
    p.progressStopAfter = qMax(2, o["progressStopAfter"].toInt(5));
    p.quickToolTimeoutSec = qBound(5, o["quickToolTimeoutSec"].toInt(15), 120);
    p.extendsId = o["extends"].toString();
    if (o["spec"].isObject()) {
        p.spec = HarnessSpec::fromJson(o["spec"].toObject());
        p.hasSpec = !p.spec.isEmpty();
        if (p.spec.extends.isEmpty()) p.spec.extends = p.extendsId;
        else if (p.extendsId.isEmpty()) p.extendsId = p.spec.extends;
    }
    return p;
}
QString AgentProfile::generateId() { return newId(); }
QString AgentProfile::defaultPresetId() { return QStringLiteral("agent-intermedio"); }

// Los 4 presets de sistema. enabledTools/directives con el token "*" = TODO el
// catálogo (se expande al aplicar; así "Máximo" sigue al catálogo si crece).
QList<AgentProfile> AgentProfile::systemPresets() {
    auto mk = [](const QString &id, const QString &name, const QStringList &tools,
                 const QStringList &dirs, const QString &approval, bool think,
                 bool mcp = true) {
        AgentProfile p;
        p.id = id; p.system = true; p.name = name;
        p.enabledTools = tools; p.directives = dirs;
        p.approvalMode = approval; p.thinking = think; p.temperature = -1.0;
        p.mcpEnabled = mcp;
        return p;
    };
    const QStringList coreTools{
        "read_file", "list_dir", "glob", "grep", "write_file", "edit_file", "run_shell"};
    // Chat liviano: el set MÍNIMO para chatear/codear. Sin MCP, sin directivas: el
    // contexto por request queda chico (clave en perfiles al límite de VRAM como los
    // de ctx enorme, donde cada KB de tools/prompt alarga la atención por token).
    const QStringList chatTools{"read_file", "list_dir", "grep", "write_file", "edit_file"};
    QStringList interTools = coreTools;
    interTools << "search_docs" << "memory" << "code_hotspots";
    QStringList advTools = interTools;
    advTools << "web_search" << "web_fetch" << "semantic_search"
             << "hybrid_search" << "repo_slice" << "context_status"
             << "context_scout" << "context_fetch" << "verify_claims" << "graph"
             << "browser_network_discover";
    QList<AgentProfile> presets{
        mk("agent-chat",       "Chat liviano", chatTools,  {},
           "ask", false, /*mcp=*/false),
        mk("agent-basico",     "Básico",     coreTools,  {},
           "ask", false),
        mk("agent-intermedio", "Intermedio", interTools, {"discipline"},
           "ask", false),
        mk("agent-avanzado",   "Avanzado",   advTools,
           {"discipline", "testNet", "projectContext", "efficiency", "style"},
           "ask", true),
        mk("agent-maximo",     "Máximo",     {"*"}, {"*"},
           "super", true),
    };
    presets[0].progressCredits = 6;
    presets[0].progressMaxCredits = 10;
    presets[0].progressStopAfter = 4;
    presets[3].progressCredits = 12;
    presets[3].progressMaxCredits = 24;
    presets[4].progressCredits = 16;
    presets[4].progressMaxCredits = 32;
    presets[4].progressReplanAfter = 5;
    presets[4].progressStopAfter = 8;

    // El contexto estructural se entrega al modelo al enviar cada objetivo,
    // sin esperar a que decida llamar context_scout. Queda limitado a los
    // perfiles de coding con presupuesto amplio; Chat/Minimal/RPA conservan
    // el flujo liviano y Next sigue siendo comparable con Intermedio.
    auto enableCodingPreflight = [](AgentProfile &profile) {
        profile.spec = profile.toSpec();
        profile.hasSpec = true;
        profile.spec.context.set = true;
        profile.spec.context.preflight = true;
        profile.spec.context.indexPolicy = QStringLiteral("lazy");
        profile.spec.context.graphExpansion = true;
    };
    enableCodingPreflight(presets[3]);
    enableCodingPreflight(presets[4]);
    presets[3].spec.tools.adaptiveRouting = true;
    presets[4].spec.tools.adaptiveRouting = true;

    // Browser Agent nativo: conserva el loop de LlamaCode, pero deja explícito
    // el perímetro de navegación para poder comparar el harness con el mismo
    // modelo/runtime. Las skills portables quedan apagadas a propósito; el
    // benchmark de browser debe medir web/browser/MCP, no instrucciones de
    // coding cargadas por accidente.
    AgentProfile browser = mk("agent-browser", "Browser Agent (nativo)", {},
                              {"discipline", "efficiency"}, "ask", true);
    browser.progressCredits = 12;
    browser.progressMaxCredits = 24;
    browser.progressReplanAfter = 4;
    browser.progressStopAfter = 8;
    browser.spec = browser.toSpec();
    browser.hasSpec = true;
    browser.spec.tools.set = true;
    browser.spec.tools.packs = QStringList{"core", "web", "browser"};
    browser.spec.skills.set = true;
    browser.spec.skills.include.clear();
    browser.spec.skills.exclude.clear();
    browser.spec.loop.set = true;
    browser.spec.loop.credits = browser.progressCredits;
    browser.spec.loop.maxCredits = browser.progressMaxCredits;
    browser.spec.loop.replanAfter = browser.progressReplanAfter;
    browser.spec.loop.stopAfter = browser.progressStopAfter;
    browser.spec.loop.webToolTimeoutSec = 300;
    browser.spec.context.set = true;
    browser.spec.context.compactionTrigger = 0.85;
    browser.spec.context.tailRatio = 0.55;
    browser.spec.context.keepLastImages = 2;
    browser.enabledTools = HarnessTools::resolve(browser.spec.tools);
    browser.spec.tools.adaptiveRouting = true;

    // --- Presets nuevos del harness modular -------------------------------
    // Minimal: el modo local-first duro (review de codehamr). Pocas tools, sin
    // MCP, prompt corto, contexto barato. Para 7B-30B con 32k reales.
    AgentProfile minimal = mk("agent-minimal", "Minimal (local-first)",
                              {"read_file", "list_dir", "grep", "write_file",
                               "edit_file", "run_shell"},
                              {"efficiency", "style"}, "ask", false, /*mcp=*/false);
    minimal.progressCredits = 6;
    minimal.progressMaxCredits = 12;
    minimal.progressStopAfter = 4;
    // Derivar el spec de los campos legacy ANTES de marcar hasSpec (toSpec()
    // devuelve el spec crudo si hasSpec ya está en true).
    minimal.spec = minimal.toSpec();
    minimal.hasSpec = true;
    minimal.spec.context.set = true;
    minimal.spec.context.keepLastImages = 0;   // sin visión: las capturas sólo inflan
    minimal.spec.context.preflight = false;
    minimal.spec.context.compactionTrigger = 0.85;
    minimal.spec.loop.set = true;
    minimal.spec.loop.credits = minimal.progressCredits;
    minimal.spec.loop.maxCredits = minimal.progressMaxCredits;
    minimal.spec.loop.stopAfter = minimal.progressStopAfter;
    minimal.spec.loop.sameCallLimit = 2;       // modelo chico: cortar antes el bucle
    minimal.spec.prompt.set = true;
    minimal.spec.prompt.builtin = minimal.directives;
    minimal.spec.prompt.maxChars = 8000;

    // RPA: automatización de escritorio con guardrails apretados.
    AgentProfile rpa = mk("agent-rpa", "RPA (escritorio)", {}, {"discipline", "efficiency"},
                          "ask", false);
    rpa.spec = rpa.toSpec();
    rpa.hasSpec = true;
    rpa.spec.tools.set = true;
    rpa.spec.tools.include.clear();
    rpa.spec.tools.packs = QStringList{"core", "rpa"};
    rpa.spec.permissions.set = true;
    rpa.spec.permissions.approvalMode = "ask";
    rpa.spec.permissions.hitlDestructive = true;
    rpa.spec.loop.set = true;
    rpa.spec.loop.quickToolTimeoutSec = 30;    // UIA/capturas son más lentas que un read
    rpa.spec.context.set = true;
    rpa.spec.context.keepLastImages = 2;       // ver el paso anterior ayuda a corregir
    rpa.enabledTools = HarnessTools::resolve(rpa.spec.tools);
    rpa.spec.tools.adaptiveRouting = true;

    // Artefactos: separa el trabajo local (crear, validar y dejar un entregable
    // privado) del trabajo con efectos externos (preparar y publicar mediante
    // browser/MCP sólo después de una aprobación explícita). El contrato es
    // deliberadamente general: sirve para HTML, Markdown, PDF, Office o apps
    // estáticas sin acoplarse a un proveedor de hosting concreto.
    auto configureArtifactContract = [](AgentProfile &profile, bool publisher) {
        profile.spec = profile.toSpec();
        profile.hasSpec = true;
        profile.spec.tools.set = true;
        profile.spec.tools.packs = publisher
            ? QStringList{QStringLiteral("core"), QStringLiteral("web"),
                          QStringLiteral("browser")}
            : QStringList{QStringLiteral("core")};
        profile.spec.tools.mcpToolsEnabled = publisher;
        profile.spec.prompt.set = true;
        profile.spec.prompt.builtin = {QStringLiteral("discipline"),
                                       QStringLiteral("testNet"),
                                       QStringLiteral("efficiency"),
                                       QStringLiteral("style")};
        profile.spec.prompt.maxChars = publisher ? 14000 : 10000;
        profile.spec.prompt.systemExtra = QStringLiteral(
            "PROTOCOLO DE ARTEFACTOS: tratá cada entregable como un contrato. "
            "Definí el formato, entrypoint, archivos esperados y criterios de "
            "validación antes de terminar. Después de escribir, releé o ejecutá "
            "una comprobación reproducible y dejá un manifiesto breve con estado, "
            "visibilidad y archivos. La salida es privada/local por defecto: "
            "no publiques, subas, envíes ni llames a un servicio externo sólo "
            "porque el artefacto existe. Separá preparar/stash de publicar y pedí "
            "aprobación humana justo antes de cualquier efecto externo.%1")
            .arg(publisher
                     ? QStringLiteral(
                           " Si el usuario pide publicar, inspeccioná primero el "
                           "destino y prepará el paquete; ejecutá la publicación sólo "
                           "después de la confirmación explícita y verificá la URL o "
                           "respuesta final sin exponer secretos.")
                     : QStringLiteral(
                           " En este perfil no uses red ni hosting: dejá el "
                           "entregable listo para una futura publicación."));
        profile.spec.permissions.set = true;
        profile.spec.permissions.approvalMode = QStringLiteral("ask");
        profile.spec.permissions.hitlDestructive = true;
        profile.spec.permissions.mailAutoSend = false;
        profile.spec.loop.set = true;
        profile.spec.loop.credits = publisher ? 12 : 8;
        profile.spec.loop.maxCredits = publisher ? 24 : 16;
        profile.spec.loop.replanAfter = 4;
        profile.spec.loop.stopAfter = publisher ? 8 : 5;
        profile.spec.loop.sameCallLimit = 2;
        profile.spec.loop.webToolTimeoutSec = 300;
        profile.spec.context.set = true;
        profile.spec.context.compactionTrigger = 0.85;
        profile.spec.context.tailRatio = 0.55;
        profile.spec.context.keepLastImages = publisher ? 2 : 0;
        profile.spec.context.preflight = false;
        profile.enabledTools = HarnessTools::resolve(profile.spec.tools);
    };

    AgentProfile artifactLocal = mk(
        "agent-artifact-local", "Artifacts · local/privado", {},
        {"discipline", "testNet", "efficiency", "style"}, "ask", false,
        /*mcp=*/false);
    artifactLocal.progressCredits = 8;
    artifactLocal.progressMaxCredits = 16;
    configureArtifactContract(artifactLocal, false);

    AgentProfile artifactPublisher = mk(
        "agent-artifact-publisher", "Artifacts · publicación aprobada", {},
        {"discipline", "testNet", "efficiency", "style"}, "ask", true,
        /*mcp=*/true);
    artifactPublisher.progressCredits = 12;
    artifactPublisher.progressMaxCredits = 24;
    artifactPublisher.progressReplanAfter = 4;
    artifactPublisher.progressStopAfter = 8;
    configureArtifactContract(artifactPublisher, true);

    // Variante comparable al Intermedio histórico. Sólo cambia el contrato
    // de ejecución; tools, prompt y límites parten de la misma configuración
    // para que una comparación A/B tenga una causa interpretable.
    AgentProfile next = presets[2];
    next.id = QStringLiteral("agent-intermedio-next");
    next.name = QStringLiteral("Intermedio · Harness Next (experimental)");
    next.spec = next.toSpec();
    next.hasSpec = true;
    next.spec.runtime.set = true;
    next.spec.runtime.engine = QStringLiteral("next");
    next.spec.runtime.version = 2;
    next.spec.runtime.fallbackEngine = QStringLiteral("legacy");
    next.spec.runtime.experimental = true;

    presets << next << minimal << rpa << browser << artifactLocal << artifactPublisher;
    return presets;
}

// ---- MasterFallback ----
QJsonObject MasterFallback::toJson() const {
    QJsonObject o;
    o["type"] = type; o["label"] = label;
    o["profileId"] = profileId;
    o["httpUrl"] = httpUrl; o["httpModel"] = httpModel; o["httpKeyRef"] = httpKeyRef;
    o["cliName"] = cliName;
    o["applyEdits"] = applyEdits; o["timeoutSec"] = timeoutSec;
    return o;
}
MasterFallback MasterFallback::fromJson(const QJsonObject &o) {
    MasterFallback f;
    f.type = o["type"].toString("http");
    f.label = o["label"].toString();
    f.profileId = o["profileId"].toString();
    f.httpUrl = o["httpUrl"].toString();
    f.httpModel = o["httpModel"].toString();
    f.httpKeyRef = o["httpKeyRef"].toString();
    f.cliName = o["cliName"].toString();
    f.applyEdits = o["applyEdits"].toBool(true);
    f.timeoutSec = o["timeoutSec"].toInt(300);
    return f;
}

// ---- MasterConfig ----
QJsonObject MasterConfig::toJson() const {
    QJsonObject o;
    QJsonArray arr;
    for (const MasterFallback &f : fallbacks) arr.append(f.toJson());
    o["fallbacks"] = arr;
    o["escalation"] = escalation; o["autoAfterFails"] = autoAfterFails;
    return o;
}
MasterConfig MasterConfig::fromJson(const QJsonObject &o) {
    MasterConfig m;
    m.escalation = o["escalation"].toString("manual");
    m.autoAfterFails = o["autoAfterFails"].toInt(3);
    if (o.contains("fallbacks")) {
        for (const QJsonValue &v : o["fallbacks"].toArray())
            m.fallbacks.append(MasterFallback::fromJson(v.toObject()));
        return m;
    }
    // --- Migración legacy: un solo maestro → cadena de un nivel. ---
    const QString kind = o["kind"].toString("none");
    if (kind == QLatin1String("none")) return m;
    MasterFallback f;
    if (kind == QLatin1String("cli")) {
        f.type = QStringLiteral("cli");
        f.cliName = o["cliName"].toString();
    } else {
        f.type = QStringLiteral("http");
        f.httpUrl = o["httpUrl"].toString();
        f.httpModel = o["httpModel"].toString();
        // Legacy guardaba la key en claro: la mantenemos como ref textual; la
        // UI puede re-guardarla en SecretStore al editar.
        f.httpKeyRef = o["httpKey"].toString();
    }
    f.applyEdits = o["applyEdits"].toBool(true);
    f.timeoutSec = o["timeoutSec"].toInt(300);
    m.fallbacks.append(f);
    return m;
}

// ---- LaunchProfile ----
QJsonObject LaunchProfile::toJson() const {
    QJsonObject o;
    o["id"] = id; o["name"] = name;
    o["alias"] = alias; o["best"] = best; o["favorite"] = favorite; o["benchmark"] = benchmark;
    o["tags"] = QJsonArray::fromStringList(tags);
    o["lastUsed"] = static_cast<double>(lastUsed);
    o["systemBadge"] = systemBadge;
    o["deprecated"] = deprecated;
    o["backendProfileId"] = backendProfileId;
    o["modelProfileId"] = modelProfileId;
    o["runtimePresetId"] = runtimePresetId;
    o["harnessProfileId"] = harnessProfileId;
    o["workspaceProfileId"] = workspaceProfileId;
    o["agentProfileId"] = agentProfileId;
    o["reasoningEffort"] = reasoningEffort;
    o["reasoningBudget"] = reasoningBudget;
    o["extraArgs"] = QJsonArray::fromStringList(extraArgs);
    o["envOverrides"] = mapToJson(envOverrides);
    o["master"] = master.toJson();
    o["plannerProfileId"] = plannerProfileId;
    o["hybridMode"] = hybridMode;
    o["powerLimitW"] = powerLimitW;
    o["browserAutomation"] = browserAutomation;
    o["voice"] = voice.toJson();
    return o;
}
LaunchProfile LaunchProfile::fromJson(const QJsonObject &o) {
    LaunchProfile p;
    p.id = o["id"].toString(); p.name = o["name"].toString();
    p.alias = o["alias"].toString();
    p.best = o["best"].toBool(false);
    p.favorite = o["favorite"].toBool(false);
    for (const QJsonValue &v : o["tags"].toArray())
        if (v.isString() && !v.toString().trimmed().isEmpty()) p.tags.append(v.toString().trimmed());
    p.lastUsed = static_cast<qint64>(o["lastUsed"].toDouble(0));
    p.benchmark = o["benchmark"].toBool(false);
    p.systemBadge = o["systemBadge"].toBool(false);
    p.deprecated = o["deprecated"].toBool(false);
    p.backendProfileId = o["backendProfileId"].toString();
    p.modelProfileId = o["modelProfileId"].toString();
    // Normally a string id. Tolerate an inline preset object (manual edits /
    // imports) by extracting its id so the launch still resolves its runtime.
    if (o["runtimePresetId"].isObject())
        p.runtimePresetId = o["runtimePresetId"].toObject()["id"].toString();
    else
        p.runtimePresetId = o["runtimePresetId"].toString();
    p.harnessProfileId = o["harnessProfileId"].toString();
    p.workspaceProfileId = o["workspaceProfileId"].toString();
    p.agentProfileId = o["agentProfileId"].toString();
    p.reasoningEffort = o["reasoningEffort"].toString().trimmed().toLower();
    if (p.reasoningEffort != QLatin1String("low")
        && p.reasoningEffort != QLatin1String("medium")
        && p.reasoningEffort != QLatin1String("high")
        && p.reasoningEffort != QLatin1String("xhigh")
        && p.reasoningEffort != QLatin1String("max"))
        p.reasoningEffort.clear();
    p.reasoningBudget = qMax(-1, o["reasoningBudget"].toInt(-1));
    for (const auto &v : o["extraArgs"].toArray()) p.extraArgs.append(v.toString());
    p.envOverrides = mapFromJson(o["envOverrides"].toObject());
    p.master = MasterConfig::fromJson(o["master"].toObject());
    p.plannerProfileId = o["plannerProfileId"].toString();
    p.hybridMode = o["hybridMode"].toString(QStringLiteral("off"));
    if (p.hybridMode.isEmpty()) p.hybridMode = QStringLiteral("off");
    p.powerLimitW = o["powerLimitW"].toInt(0);
    p.browserAutomation = o["browserAutomation"].toString(QStringLiteral("inherit"));
    if (p.browserAutomation.isEmpty()) p.browserAutomation = QStringLiteral("inherit");
    p.voice = VoiceConfig::fromJson(o["voice"].toObject());
    return p;
}
QString LaunchProfile::generateId() { return newId(); }
