#include "HarnessSpec.h"
#include "core/agent/LlamaAgentBackend.h"

#include <QJsonArray>
#include <QSet>

namespace {

QStringList toStringList(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &item : v.toArray()) {
        const QString s = item.toString().trimmed();
        if (!s.isEmpty()) out << s;
    }
    return out;
}

QJsonArray fromStringList(const QStringList &list)
{
    QJsonArray arr;
    for (const QString &s : list) arr.append(s);
    return arr;
}

int boundedInt(const QJsonObject &o, const char *key, int def, int lo, int hi)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (!v.isDouble()) return def;
    return qBound(lo, v.toInt(def), hi);
}

double boundedDouble(const QJsonObject &o, const char *key, double def, double lo, double hi)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (!v.isDouble()) return def;
    return qBound(lo, v.toDouble(def), hi);
}

void addDiff(QVariantList &out, const char *module, const char *field,
             const QVariant &baseValue, const QVariant &value)
{
    if (baseValue == value) return;
    out.append(QVariantMap{{QStringLiteral("module"), QString::fromLatin1(module)},
                           {QStringLiteral("field"), QString::fromLatin1(field)},
                           {QStringLiteral("base"), baseValue},
                           {QStringLiteral("value"), value}});
}

}  // namespace

// ---------------------------------------------------------------------------
// Modulos
// ---------------------------------------------------------------------------

QJsonObject HarnessRuntimeModule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("engine")] = engine;
    o[QStringLiteral("version")] = version;
    o[QStringLiteral("fallbackEngine")] = fallbackEngine;
    o[QStringLiteral("experimental")] = experimental;
    return o;
}

HarnessRuntimeModule HarnessRuntimeModule::fromJson(const QJsonObject &o)
{
    HarnessRuntimeModule m;
    m.set = true;
    m.engine = o.value(QStringLiteral("engine")).toString(QStringLiteral("legacy")).trimmed().toLower();
    if (m.engine.isEmpty()) m.engine = QStringLiteral("legacy");
    m.version = qBound(1, o.value(QStringLiteral("version")).toInt(1), 100);
    m.fallbackEngine = o.value(QStringLiteral("fallbackEngine"))
                           .toString(QStringLiteral("legacy"))
                           .trimmed()
                           .toLower();
    if (m.fallbackEngine.isEmpty()) m.fallbackEngine = QStringLiteral("legacy");
    m.experimental = o.value(QStringLiteral("experimental")).toBool(false);
    return m;
}

QJsonObject HarnessWorkerModule::toJson() const
{
    QJsonObject o{{QStringLiteral("lane"), lane},
                  {QStringLiteral("entrypoint"), entrypoint},
                  {QStringLiteral("arguments"), fromStringList(arguments)},
                  {QStringLiteral("sandbox"), sandbox},
                  {QStringLiteral("workingDirectory"), workingDirectory},
                  {QStringLiteral("allowNetwork"), allowNetwork},
                  {QStringLiteral("maxFrameBytes"), maxFrameBytes},
                  {QStringLiteral("startupTimeoutMs"), startupTimeoutMs},
                  {QStringLiteral("callTimeoutMs"), callTimeoutMs},
                  {QStringLiteral("memoryLimitMb"), memoryLimitMb},
                  {QStringLiteral("processLimit"), processLimit},
                  {QStringLiteral("cpuTimeLimitSec"), cpuTimeLimitSec},
                  {QStringLiteral("requestedCapabilities"), fromStringList(requestedCapabilities)}};
    return o;
}

HarnessWorkerModule HarnessWorkerModule::fromJson(const QJsonObject &o)
{
    HarnessWorkerModule m;
    m.set = true;
    const QString rawLane = o.value(QStringLiteral("lane")).toString().trimmed().toLower();
    m.lane = (rawLane == QLatin1String("node") || rawLane == QLatin1String("python"))
        ? rawLane : QStringLiteral("builtin");
    m.entrypoint = o.value(QStringLiteral("entrypoint")).toString().trimmed();
    m.arguments = toStringList(o.value(QStringLiteral("arguments")));
    const QString rawSandbox = o.value(QStringLiteral("sandbox")).toString().trimmed().toLower();
    m.sandbox = (rawSandbox == QLatin1String("process") || rawSandbox == QLatin1String("strong"))
        ? rawSandbox : QStringLiteral("none");
    m.workingDirectory = o.value(QStringLiteral("workingDirectory")).toString().trimmed();
    m.allowNetwork = o.value(QStringLiteral("allowNetwork")).toBool(false);
    m.maxFrameBytes = boundedInt(o, "maxFrameBytes", 1024 * 1024, 1024, 64 * 1024 * 1024);
    m.startupTimeoutMs = boundedInt(o, "startupTimeoutMs", 10000, 100, 120000);
    m.callTimeoutMs = boundedInt(o, "callTimeoutMs", 120000, 100, 3600000);
    m.memoryLimitMb = boundedInt(o, "memoryLimitMb", 512, 0, 1024 * 1024);
    m.processLimit = boundedInt(o, "processLimit", 32, 1, 4096);
    m.cpuTimeLimitSec = boundedInt(o, "cpuTimeLimitSec", 0, 0, 7 * 24 * 60 * 60);
    m.requestedCapabilities = toStringList(o.value(QStringLiteral("requestedCapabilities")));
    return m;
}

QJsonObject HarnessToolsModule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("packs")] = fromStringList(packs);
    o[QStringLiteral("include")] = fromStringList(include);
    o[QStringLiteral("exclude")] = fromStringList(exclude);
    o[QStringLiteral("mcpTools")] = mcpToolsEnabled;
    o[QStringLiteral("adaptiveRouting")] = adaptiveRouting;
    return o;
}

HarnessToolsModule HarnessToolsModule::fromJson(const QJsonObject &o)
{
    HarnessToolsModule m;
    m.set = true;
    m.packs = toStringList(o.value(QStringLiteral("packs")));
    m.include = toStringList(o.value(QStringLiteral("include")));
    m.exclude = toStringList(o.value(QStringLiteral("exclude")));
    m.mcpToolsEnabled = o.value(QStringLiteral("mcpTools")).toBool(true);
    m.adaptiveRouting = o.value(QStringLiteral("adaptiveRouting")).toBool(false);
    return m;
}

QJsonObject HarnessSkillsModule::toJson() const
{
    return QJsonObject{{QStringLiteral("include"), fromStringList(include)},
                       {QStringLiteral("exclude"), fromStringList(exclude)}};
}

HarnessSkillsModule HarnessSkillsModule::fromJson(const QJsonObject &o)
{
    HarnessSkillsModule m;
    m.set = true;
    m.include = toStringList(o.value(QStringLiteral("include")));
    m.exclude = toStringList(o.value(QStringLiteral("exclude")));
    for (QStringList *list : {&m.include, &m.exclude}) {
        for (QString &slug : *list) slug = slug.trimmed().toLower();
        list->removeDuplicates();
    }
    return m;
}

QJsonObject HarnessPromptModule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("builtin")] = fromStringList(builtin);
    o[QStringLiteral("custom")] = fromStringList(custom);
    o[QStringLiteral("systemExtra")] = systemExtra;
    o[QStringLiteral("maxChars")] = maxChars;
    return o;
}

HarnessPromptModule HarnessPromptModule::fromJson(const QJsonObject &o)
{
    HarnessPromptModule m;
    m.set = true;
    m.builtin = toStringList(o.value(QStringLiteral("builtin")));
    m.custom = toStringList(o.value(QStringLiteral("custom")));
    m.systemExtra = o.value(QStringLiteral("systemExtra")).toString();
    m.maxChars = boundedInt(o, "maxChars", 24000, 1000, 400000);
    return m;
}

QJsonObject HarnessLoopModule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("credits")] = credits;
    o[QStringLiteral("maxCredits")] = maxCredits;
    o[QStringLiteral("replanAfter")] = replanAfter;
    o[QStringLiteral("stopAfter")] = stopAfter;
    o[QStringLiteral("maxDistinctWrites")] = maxDistinctWrites;
    o[QStringLiteral("sameCallLimit")] = sameCallLimit;
    o[QStringLiteral("failureSpiral")] = failureSpiral;
    o[QStringLiteral("transportRetries")] = transportRetries;
    o[QStringLiteral("quickToolTimeoutSec")] = quickToolTimeoutSec;
    o[QStringLiteral("webToolTimeoutSec")] = webToolTimeoutSec;
    o[QStringLiteral("streamIdleTimeoutSec")] = streamIdleTimeoutSec;
    return o;
}

HarnessLoopModule HarnessLoopModule::fromJson(const QJsonObject &o)
{
    HarnessLoopModule m;
    m.set = true;
    m.credits = boundedInt(o, "credits", 8, 1, 200);
    m.maxCredits = boundedInt(o, "maxCredits", 16, 1, 400);
    m.replanAfter = boundedInt(o, "replanAfter", 3, 1, 100);
    m.stopAfter = boundedInt(o, "stopAfter", 5, 1, 200);
    m.maxDistinctWrites = boundedInt(o, "maxDistinctWrites", 24, 1, 500);
    m.sameCallLimit = boundedInt(o, "sameCallLimit", 3, 1, 50);
    m.failureSpiral = boundedInt(o, "failureSpiral", 3, 1, 50);
    m.transportRetries = boundedInt(o, "transportRetries", 60, 0, 600);
    m.quickToolTimeoutSec = boundedInt(o, "quickToolTimeoutSec", 15, 5, 120);
    m.webToolTimeoutSec = boundedInt(o, "webToolTimeoutSec", 180, 10, 1800);
    m.streamIdleTimeoutSec = boundedInt(o, "streamIdleTimeoutSec", 0, 0, 24 * 60 * 60);
    if (m.maxCredits < m.credits) m.maxCredits = m.credits;
    return m;
}

QJsonObject HarnessContextModule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("compaction")] = compaction;
    o[QStringLiteral("compactionTrigger")] = compactionTrigger;
    o[QStringLiteral("tailRatio")] = tailRatio;
    o[QStringLiteral("prune")] = prune;
    o[QStringLiteral("keepLastImages")] = keepLastImages;
    o[QStringLiteral("readDedup")] = readDedup;
    o[QStringLiteral("preflight")] = preflight;
    o[QStringLiteral("indexPolicy")] = indexPolicy;
    o[QStringLiteral("scoutBudget")] = scoutBudget;
    o[QStringLiteral("scoutK")] = scoutK;
    o[QStringLiteral("graphExpansion")] = graphExpansion;
    o[QStringLiteral("warmup")] = warmup;
    return o;
}

HarnessContextModule HarnessContextModule::fromJson(const QJsonObject &o)
{
    HarnessContextModule m;
    m.set = true;
    m.compaction = o.value(QStringLiteral("compaction")).toBool(true);
    m.compactionTrigger = boundedDouble(o, "compactionTrigger", 0.90, 0.30, 0.99);
    m.tailRatio = boundedDouble(o, "tailRatio", 0.60, 0.10, 0.95);
    m.prune = o.value(QStringLiteral("prune")).toBool(true);
    m.keepLastImages = boundedInt(o, "keepLastImages", 1, 0, 20);
    m.readDedup = o.value(QStringLiteral("readDedup")).toBool(true);
    m.preflight = o.value(QStringLiteral("preflight")).toBool(false);
    const QString policy = o.value(QStringLiteral("indexPolicy")).toString().trimmed().toLower();
    m.indexPolicy = (policy == QLatin1String("off") || policy == QLatin1String("eager"))
        ? policy : QStringLiteral("lazy");
    m.scoutBudget = boundedInt(o, "scoutBudget", 700, 64, 16000);
    m.scoutK = boundedInt(o, "scoutK", 8, 1, 15);
    m.graphExpansion = o.value(QStringLiteral("graphExpansion")).toBool(true);
    m.warmup = o.value(QStringLiteral("warmup")).toBool(true);
    return m;
}

QJsonObject HarnessPermissionsModule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("approvalMode")] = approvalMode;
    o[QStringLiteral("rules")] = fromStringList(rules);
    // Sólo serializamos fsScope si fue declarado: escribirlo siempre convertiría
    // un perfil legacy en uno que estrecha el alcance de las Tasks.
    if (fsScopeDeclared) o[QStringLiteral("fsScope")] = fsScope;
    o[QStringLiteral("folders")] = fromStringList(folders);
    o[QStringLiteral("hitlDestructive")] = hitlDestructive;
    o[QStringLiteral("mailAutoSend")] = mailAutoSend;
    return o;
}

HarnessPermissionsModule HarnessPermissionsModule::fromJson(const QJsonObject &o)
{
    HarnessPermissionsModule m;
    m.set = true;
    m.approvalMode = o.value(QStringLiteral("approvalMode")).toString(QStringLiteral("ask"));
    m.rules = toStringList(o.value(QStringLiteral("rules")));
    m.fsScope = o.value(QStringLiteral("fsScope")).toString(QStringLiteral("project"));
    m.fsScopeDeclared = o.contains(QStringLiteral("fsScope"));
    m.folders = toStringList(o.value(QStringLiteral("folders")));
    m.hitlDestructive = o.value(QStringLiteral("hitlDestructive")).toBool(true);
    m.mailAutoSend = o.value(QStringLiteral("mailAutoSend")).toBool(false);
    // El guardrail Zero-Autonomy solo se puede bajar en modo super declarado. Un
    // perfil no puede desactivarlo "de costado" (invariante testeada).
    if (!m.hitlDestructive && m.approvalMode != QLatin1String("super"))
        m.hitlDestructive = true;
    return m;
}

QJsonObject HarnessEscalationModule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("maxParallelSubagents")] = maxParallelSubagents;
    o[QStringLiteral("isolateSubagents")] = isolateSubagents;
    o[QStringLiteral("masterEscalation")] = masterEscalation;
    o[QStringLiteral("masterAutoAfterFails")] = masterAutoAfterFails;
    if (!masterFallbacks.isEmpty()) o[QStringLiteral("masterFallbacks")] = masterFallbacks;
    o[QStringLiteral("routerFilesAffected")] = routerFilesAffected;
    o[QStringLiteral("routerContextTokens")] = routerContextTokens;
    o[QStringLiteral("routerRepeatedFailures")] = routerRepeatedFailures;
    o[QStringLiteral("routerAgentCycles")] = routerAgentCycles;
    o[QStringLiteral("routerConfidenceFloor")] = routerConfidenceFloor;
    return o;
}

HarnessEscalationModule HarnessEscalationModule::fromJson(const QJsonObject &o)
{
    HarnessEscalationModule m;
    m.set = true;
    m.maxParallelSubagents = boundedInt(o, "maxParallelSubagents", 5, 1, 5);
    m.isolateSubagents = o.value(QStringLiteral("isolateSubagents")).toBool(true);
    m.masterEscalation = o.value(QStringLiteral("masterEscalation")).toString();
    m.masterAutoAfterFails = boundedInt(o, "masterAutoAfterFails", 0, 0, 100);
    m.masterFallbacks = o.value(QStringLiteral("masterFallbacks")).toArray();
    m.routerFilesAffected = boundedInt(o, "routerFilesAffected", 8, 1, 500);
    m.routerContextTokens = boundedInt(o, "routerContextTokens", 24000, 1000, 1000000);
    m.routerRepeatedFailures = boundedInt(o, "routerRepeatedFailures", 3, 1, 50);
    m.routerAgentCycles = boundedInt(o, "routerAgentCycles", 5, 1, 100);
    m.routerConfidenceFloor = boundedDouble(o, "routerConfidenceFloor", 0.3, 0.0, 1.0);
    return m;
}

QJsonObject HarnessMemoryModule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("structuredEnabled")] = structuredEnabled;
    o[QStringLiteral("structuredFacts")] = structuredFacts;
    o[QStringLiteral("projectMemory")] = projectMemory;
    o[QStringLiteral("projectMemoryMaxChars")] = projectMemoryMaxChars;
    o[QStringLiteral("consolidateOnLeave")] = consolidateOnLeave;
    return o;
}

HarnessMemoryModule HarnessMemoryModule::fromJson(const QJsonObject &o)
{
    HarnessMemoryModule m;
    m.set = true;
    m.structuredEnabled = o.value(QStringLiteral("structuredEnabled")).toBool(true);
    m.structuredFacts = boundedInt(o, "structuredFacts", 12, 0, 200);
    m.projectMemory = o.value(QStringLiteral("projectMemory")).toBool(true);
    m.projectMemoryMaxChars = boundedInt(o, "projectMemoryMaxChars", 65536, 0, 1048576);
    m.consolidateOnLeave = o.value(QStringLiteral("consolidateOnLeave")).toBool(true);
    return m;
}

QJsonObject HarnessKnowledgeModule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("enabled")] = enabled;
    o[QStringLiteral("preflight")] = preflight;
    o[QStringLiteral("citeSources")] = citeSources;
    o[QStringLiteral("maxFacts")] = maxFacts;
    o[QStringLiteral("maxEdges")] = maxEdges;
    o[QStringLiteral("maxChars")] = maxChars;
    return o;
}

HarnessKnowledgeModule HarnessKnowledgeModule::fromJson(const QJsonObject &o)
{
    HarnessKnowledgeModule m;
    m.set = true;
    m.enabled = o.value(QStringLiteral("enabled")).toBool(false);
    m.preflight = o.value(QStringLiteral("preflight")).toBool(false);
    m.citeSources = o.value(QStringLiteral("citeSources")).toBool(true);
    m.maxFacts = boundedInt(o, "maxFacts", 8, 0, 50);
    m.maxEdges = boundedInt(o, "maxEdges", 12, 0, 100);
    m.maxChars = boundedInt(o, "maxChars", 12000, 512, 64000);
    return m;
}

QJsonObject HarnessChatModule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("thinking")] = thinking;
    o[QStringLiteral("reasoningEffort")] = reasoningEffort;
    o[QStringLiteral("temperature")] = temperature;
    o[QStringLiteral("topP")] = topP;
    o[QStringLiteral("topK")] = topK;
    o[QStringLiteral("designerPersona")] = designerPersona;
    o[QStringLiteral("systemExtra")] = systemExtra;
    return o;
}

HarnessChatModule HarnessChatModule::fromJson(const QJsonObject &o)
{
    HarnessChatModule m;
    m.set = true;
    m.thinking = o.value(QStringLiteral("thinking")).toBool(false);
    m.reasoningEffort = o.value(QStringLiteral("reasoningEffort")).toString();
    m.temperature = o.value(QStringLiteral("temperature")).toDouble(-1.0);
    m.topP = o.value(QStringLiteral("topP")).toDouble(-1.0);
    m.topK = o.value(QStringLiteral("topK")).toInt(-1);
    m.designerPersona = o.value(QStringLiteral("designerPersona")).toBool(false);
    m.systemExtra = o.value(QStringLiteral("systemExtra")).toString();
    return m;
}

QJsonObject HarnessProtocolModule::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("toolProtocol")] = toolProtocol;
    o[QStringLiteral("thinking")] = thinking;
    o[QStringLiteral("thinkingLeakGuard")] = thinkingLeakGuard;
    o[QStringLiteral("temperature")] = temperature;
    o[QStringLiteral("reasoningEffort")] = reasoningEffort;
    o[QStringLiteral("reasoningBudget")] = reasoningBudget;
    return o;
}

HarnessProtocolModule HarnessProtocolModule::fromJson(const QJsonObject &o)
{
    HarnessProtocolModule m;
    m.set = true;
    m.toolProtocol = o.value(QStringLiteral("toolProtocol")).toString(QStringLiteral("auto"));
    if (m.toolProtocol != QLatin1String("native") && m.toolProtocol != QLatin1String("text"))
        m.toolProtocol = QStringLiteral("auto");
    m.thinking = o.value(QStringLiteral("thinking")).toBool(false);
    m.thinkingLeakGuard = o.value(QStringLiteral("thinkingLeakGuard")).toBool(false);
    m.temperature = o.value(QStringLiteral("temperature")).toDouble(-1.0);
    m.reasoningEffort = o.value(QStringLiteral("reasoningEffort")).toString();
    m.reasoningBudget = o.value(QStringLiteral("reasoningBudget")).toInt(-1);
    return m;
}

// ---------------------------------------------------------------------------
// HarnessSpec
// ---------------------------------------------------------------------------

bool HarnessSpec::isEmpty() const
{
    return extends.isEmpty() && !runtime.set && !worker.set && !tools.set && !prompt.set && !loop.set && !context.set
           && !skills.set && !permissions.set && !escalation.set && !protocol.set && !memory.set
           && !knowledge.set
           && !chat.set && phases.isEmpty();
}

QJsonObject HarnessSpec::toJson() const
{
    QJsonObject o;
    if (!extends.isEmpty()) o[QStringLiteral("extends")] = extends;
    if (runtime.set) o[QStringLiteral("runtime")] = runtime.toJson();
    if (worker.set) o[QStringLiteral("worker")] = worker.toJson();
    if (tools.set) o[QStringLiteral("tools")] = tools.toJson();
    if (skills.set) o[QStringLiteral("skills")] = skills.toJson();
    if (prompt.set) o[QStringLiteral("prompt")] = prompt.toJson();
    if (loop.set) o[QStringLiteral("loop")] = loop.toJson();
    if (context.set) o[QStringLiteral("context")] = context.toJson();
    if (permissions.set) o[QStringLiteral("permissions")] = permissions.toJson();
    if (escalation.set) o[QStringLiteral("escalation")] = escalation.toJson();
    if (protocol.set) o[QStringLiteral("protocol")] = protocol.toJson();
    if (memory.set) o[QStringLiteral("memory")] = memory.toJson();
    if (knowledge.set) o[QStringLiteral("knowledge")] = knowledge.toJson();
    if (chat.set) o[QStringLiteral("chat")] = chat.toJson();
    if (!phases.isEmpty()) {
        QJsonObject ph;
        for (auto it = phases.cbegin(); it != phases.cend(); ++it)
            ph[it.key()] = it.value();
        o[QStringLiteral("phases")] = ph;
    }
    return o;
}

HarnessSpec HarnessSpec::fromJson(const QJsonObject &o)
{
    HarnessSpec s;
    s.extends = o.value(QStringLiteral("extends")).toString();
    if (o.value(QStringLiteral("runtime")).isObject())
        s.runtime = HarnessRuntimeModule::fromJson(o.value(QStringLiteral("runtime")).toObject());
    if (o.value(QStringLiteral("worker")).isObject())
        s.worker = HarnessWorkerModule::fromJson(o.value(QStringLiteral("worker")).toObject());
    if (o.value(QStringLiteral("tools")).isObject())
        s.tools = HarnessToolsModule::fromJson(o.value(QStringLiteral("tools")).toObject());
    if (o.value(QStringLiteral("skills")).isObject())
        s.skills = HarnessSkillsModule::fromJson(o.value(QStringLiteral("skills")).toObject());
    if (o.value(QStringLiteral("prompt")).isObject())
        s.prompt = HarnessPromptModule::fromJson(o.value(QStringLiteral("prompt")).toObject());
    if (o.value(QStringLiteral("loop")).isObject())
        s.loop = HarnessLoopModule::fromJson(o.value(QStringLiteral("loop")).toObject());
    if (o.value(QStringLiteral("context")).isObject())
        s.context = HarnessContextModule::fromJson(o.value(QStringLiteral("context")).toObject());
    if (o.value(QStringLiteral("permissions")).isObject())
        s.permissions = HarnessPermissionsModule::fromJson(
            o.value(QStringLiteral("permissions")).toObject());
    if (o.value(QStringLiteral("escalation")).isObject())
        s.escalation = HarnessEscalationModule::fromJson(
            o.value(QStringLiteral("escalation")).toObject());
    if (o.value(QStringLiteral("protocol")).isObject())
        s.protocol = HarnessProtocolModule::fromJson(
            o.value(QStringLiteral("protocol")).toObject());
    if (o.value(QStringLiteral("memory")).isObject())
        s.memory = HarnessMemoryModule::fromJson(o.value(QStringLiteral("memory")).toObject());
    if (o.value(QStringLiteral("knowledge")).isObject())
        s.knowledge = HarnessKnowledgeModule::fromJson(
            o.value(QStringLiteral("knowledge")).toObject());
    if (o.value(QStringLiteral("chat")).isObject())
        s.chat = HarnessChatModule::fromJson(o.value(QStringLiteral("chat")).toObject());
    const QJsonObject ph = o.value(QStringLiteral("phases")).toObject();
    for (auto it = ph.constBegin(); it != ph.constEnd(); ++it)
        if (it.value().isObject()) s.phases.insert(it.key(), it.value().toObject());
    return s;
}

HarnessSpec HarnessSpec::resolve(const HarnessSpec &base, const HarnessSpec &override)
{
    HarnessSpec out = base;
    // extends del hijo se conserva como metadato de linaje (quien resuelve la
    // cadena es ProfileManager); el resuelto ya no hereda de nadie mas.
    out.extends = base.extends;
    if (override.runtime.set) out.runtime = override.runtime;
    if (override.worker.set) out.worker = override.worker;
    if (override.tools.set) out.tools = override.tools;
    if (override.skills.set) out.skills = override.skills;
    if (override.prompt.set) out.prompt = override.prompt;
    if (override.loop.set) out.loop = override.loop;
    if (override.context.set) out.context = override.context;
    if (override.permissions.set) out.permissions = override.permissions;
    if (override.escalation.set) out.escalation = override.escalation;
    if (override.protocol.set) out.protocol = override.protocol;
    if (override.memory.set) out.memory = override.memory;
    if (override.knowledge.set) out.knowledge = override.knowledge;
    if (override.chat.set) out.chat = override.chat;
    for (auto it = override.phases.cbegin(); it != override.phases.cend(); ++it)
        out.phases.insert(it.key(), it.value());
    return out;
}

HarnessSpec HarnessSpec::forPhase(const HarnessSpec &spec, const QString &phase)
{
    const auto it = spec.phases.constFind(phase);
    if (it == spec.phases.constEnd()) return spec;
    HarnessSpec patch = HarnessSpec::fromJson(it.value());
    patch.phases.clear();                 // una fase no define fases
    HarnessSpec out = resolve(spec, patch);
    out.phases = spec.phases;             // conservar el mapa original
    return out;
}

QVariantList HarnessSpec::diff(const HarnessSpec &base) const
{
    QVariantList out;
    addDiff(out, "runtime", "engine", base.runtime.engine, runtime.engine);
    addDiff(out, "runtime", "version", base.runtime.version, runtime.version);
    addDiff(out, "runtime", "fallbackEngine", base.runtime.fallbackEngine,
            runtime.fallbackEngine);
    addDiff(out, "runtime", "experimental", base.runtime.experimental, runtime.experimental);
    addDiff(out, "worker", "lane", base.worker.lane, worker.lane);
    addDiff(out, "worker", "entrypoint", base.worker.entrypoint, worker.entrypoint);
    addDiff(out, "worker", "arguments", base.worker.arguments, worker.arguments);
    addDiff(out, "worker", "sandbox", base.worker.sandbox, worker.sandbox);
    addDiff(out, "worker", "workingDirectory", base.worker.workingDirectory,
            worker.workingDirectory);
    addDiff(out, "worker", "allowNetwork", base.worker.allowNetwork, worker.allowNetwork);
    addDiff(out, "worker", "maxFrameBytes", base.worker.maxFrameBytes, worker.maxFrameBytes);
    addDiff(out, "worker", "startupTimeoutMs", base.worker.startupTimeoutMs,
            worker.startupTimeoutMs);
    addDiff(out, "worker", "callTimeoutMs", base.worker.callTimeoutMs, worker.callTimeoutMs);
    addDiff(out, "worker", "memoryLimitMb", base.worker.memoryLimitMb, worker.memoryLimitMb);
    addDiff(out, "worker", "processLimit", base.worker.processLimit, worker.processLimit);
    addDiff(out, "worker", "cpuTimeLimitSec", base.worker.cpuTimeLimitSec,
            worker.cpuTimeLimitSec);
    addDiff(out, "worker", "requestedCapabilities", base.worker.requestedCapabilities,
            worker.requestedCapabilities);
    addDiff(out, "tools", "packs", base.tools.packs, tools.packs);
    addDiff(out, "tools", "include", base.tools.include, tools.include);
    addDiff(out, "tools", "exclude", base.tools.exclude, tools.exclude);
    addDiff(out, "tools", "mcpTools", base.tools.mcpToolsEnabled, tools.mcpToolsEnabled);
    addDiff(out, "tools", "adaptiveRouting", base.tools.adaptiveRouting,
            tools.adaptiveRouting);
    addDiff(out, "skills", "include", base.skills.include, skills.include);
    addDiff(out, "skills", "exclude", base.skills.exclude, skills.exclude);

    addDiff(out, "prompt", "builtin", base.prompt.builtin, prompt.builtin);
    addDiff(out, "prompt", "custom", base.prompt.custom, prompt.custom);
    addDiff(out, "prompt", "systemExtra", base.prompt.systemExtra, prompt.systemExtra);
    addDiff(out, "prompt", "maxChars", base.prompt.maxChars, prompt.maxChars);

    addDiff(out, "loop", "credits", base.loop.credits, loop.credits);
    addDiff(out, "loop", "maxCredits", base.loop.maxCredits, loop.maxCredits);
    addDiff(out, "loop", "replanAfter", base.loop.replanAfter, loop.replanAfter);
    addDiff(out, "loop", "stopAfter", base.loop.stopAfter, loop.stopAfter);
    addDiff(out, "loop", "maxDistinctWrites", base.loop.maxDistinctWrites, loop.maxDistinctWrites);
    addDiff(out, "loop", "sameCallLimit", base.loop.sameCallLimit, loop.sameCallLimit);
    addDiff(out, "loop", "failureSpiral", base.loop.failureSpiral, loop.failureSpiral);
    addDiff(out, "loop", "transportRetries", base.loop.transportRetries, loop.transportRetries);
    addDiff(out, "loop", "quickToolTimeoutSec", base.loop.quickToolTimeoutSec,
            loop.quickToolTimeoutSec);
    addDiff(out, "loop", "webToolTimeoutSec", base.loop.webToolTimeoutSec, loop.webToolTimeoutSec);
    addDiff(out, "loop", "streamIdleTimeoutSec", base.loop.streamIdleTimeoutSec,
            loop.streamIdleTimeoutSec);

    addDiff(out, "context", "compaction", base.context.compaction, context.compaction);
    addDiff(out, "context", "compactionTrigger", base.context.compactionTrigger,
            context.compactionTrigger);
    addDiff(out, "context", "tailRatio", base.context.tailRatio, context.tailRatio);
    addDiff(out, "context", "prune", base.context.prune, context.prune);
    addDiff(out, "context", "keepLastImages", base.context.keepLastImages, context.keepLastImages);
    addDiff(out, "context", "readDedup", base.context.readDedup, context.readDedup);
    addDiff(out, "context", "preflight", base.context.preflight, context.preflight);
    addDiff(out, "context", "indexPolicy", base.context.indexPolicy, context.indexPolicy);
    addDiff(out, "context", "scoutBudget", base.context.scoutBudget, context.scoutBudget);
    addDiff(out, "context", "scoutK", base.context.scoutK, context.scoutK);
    addDiff(out, "context", "graphExpansion", base.context.graphExpansion, context.graphExpansion);
    addDiff(out, "context", "warmup", base.context.warmup, context.warmup);

    addDiff(out, "permissions", "approvalMode", base.permissions.approvalMode,
            permissions.approvalMode);
    addDiff(out, "permissions", "rules", base.permissions.rules, permissions.rules);
    addDiff(out, "permissions", "fsScope", base.permissions.fsScope, permissions.fsScope);
    addDiff(out, "permissions", "folders", base.permissions.folders, permissions.folders);
    addDiff(out, "permissions", "hitlDestructive", base.permissions.hitlDestructive,
            permissions.hitlDestructive);
    addDiff(out, "permissions", "mailAutoSend", base.permissions.mailAutoSend,
            permissions.mailAutoSend);

    addDiff(out, "escalation", "maxParallelSubagents", base.escalation.maxParallelSubagents,
            escalation.maxParallelSubagents);
    addDiff(out, "escalation", "isolateSubagents", base.escalation.isolateSubagents,
            escalation.isolateSubagents);
    addDiff(out, "escalation", "masterEscalation", base.escalation.masterEscalation,
            escalation.masterEscalation);
    addDiff(out, "escalation", "masterAutoAfterFails", base.escalation.masterAutoAfterFails,
            escalation.masterAutoAfterFails);
    addDiff(out, "escalation", "masterFallbacks", base.escalation.masterFallbacks.size(),
            escalation.masterFallbacks.size());
    addDiff(out, "escalation", "routerFilesAffected", base.escalation.routerFilesAffected,
            escalation.routerFilesAffected);
    addDiff(out, "escalation", "routerContextTokens", base.escalation.routerContextTokens,
            escalation.routerContextTokens);
    addDiff(out, "escalation", "routerRepeatedFailures", base.escalation.routerRepeatedFailures,
            escalation.routerRepeatedFailures);
    addDiff(out, "escalation", "routerAgentCycles", base.escalation.routerAgentCycles,
            escalation.routerAgentCycles);
    addDiff(out, "escalation", "routerConfidenceFloor", base.escalation.routerConfidenceFloor,
            escalation.routerConfidenceFloor);

    addDiff(out, "protocol", "toolProtocol", base.protocol.toolProtocol, protocol.toolProtocol);
    addDiff(out, "protocol", "thinking", base.protocol.thinking, protocol.thinking);
    addDiff(out, "protocol", "thinkingLeakGuard", base.protocol.thinkingLeakGuard,
            protocol.thinkingLeakGuard);
    addDiff(out, "protocol", "temperature", base.protocol.temperature, protocol.temperature);
    addDiff(out, "protocol", "reasoningEffort", base.protocol.reasoningEffort,
            protocol.reasoningEffort);
    addDiff(out, "protocol", "reasoningBudget", base.protocol.reasoningBudget,
            protocol.reasoningBudget);

    addDiff(out, "memory", "structuredEnabled", base.memory.structuredEnabled,
            memory.structuredEnabled);
    addDiff(out, "memory", "structuredFacts", base.memory.structuredFacts,
            memory.structuredFacts);
    addDiff(out, "memory", "projectMemory", base.memory.projectMemory, memory.projectMemory);
    addDiff(out, "memory", "projectMemoryMaxChars", base.memory.projectMemoryMaxChars,
            memory.projectMemoryMaxChars);
    addDiff(out, "memory", "consolidateOnLeave", base.memory.consolidateOnLeave,
            memory.consolidateOnLeave);

    addDiff(out, "knowledge", "enabled", base.knowledge.enabled, knowledge.enabled);
    addDiff(out, "knowledge", "preflight", base.knowledge.preflight, knowledge.preflight);
    addDiff(out, "knowledge", "citeSources", base.knowledge.citeSources,
            knowledge.citeSources);
    addDiff(out, "knowledge", "maxFacts", base.knowledge.maxFacts, knowledge.maxFacts);
    addDiff(out, "knowledge", "maxEdges", base.knowledge.maxEdges, knowledge.maxEdges);
    addDiff(out, "knowledge", "maxChars", base.knowledge.maxChars, knowledge.maxChars);

    addDiff(out, "chat", "thinking", base.chat.thinking, chat.thinking);
    addDiff(out, "chat", "temperature", base.chat.temperature, chat.temperature);
    addDiff(out, "chat", "topP", base.chat.topP, chat.topP);
    addDiff(out, "chat", "topK", base.chat.topK, chat.topK);
    addDiff(out, "chat", "designerPersona", base.chat.designerPersona, chat.designerPersona);
    addDiff(out, "chat", "systemExtra", base.chat.systemExtra, chat.systemExtra);
    return out;
}

// ---------------------------------------------------------------------------
// Packs
// ---------------------------------------------------------------------------

namespace {

// Slug ASCII de un grupo del toolCatalog ("Multi-Agente" -> "multi-agente").
QString groupKey(const QString &group)
{
    // Normalizacion NFD + descarte de marcas de acento: evita literales acentuados
    // en el fuente (los .cpp del repo se compilan con /utf-8, pero un literal
    // acentuado dentro de una comparacion es justo el tipo de cosa que falla mudo
    // si alguien recompila sin la flag). "Busqueda" -> "busqueda".
    const QString decomposed = group.normalized(QString::NormalizationForm_D);
    QString out;
    for (const QChar &c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing) continue;
        if (c.isLetterOrNumber()) out += c.toLower();
        else if (c == QLatin1Char('-') || c.isSpace()) out += QLatin1Char('-');
    }
    return out;
}

QStringList toolsOfGroup(const QString &group)
{
    QStringList out;
    for (const QVariant &v : LlamaAgentBackend::toolCatalog()) {
        const QVariantMap m = v.toMap();
        if (groupKey(m.value(QStringLiteral("group")).toString()) == group)
            out << m.value(QStringLiteral("name")).toString();
    }
    return out;
}

QStringList allToolNames()
{
    QStringList out;
    for (const QVariant &v : LlamaAgentBackend::toolCatalog())
        out << v.toMap().value(QStringLiteral("name")).toString();
    return out;
}

QVariantMap mkPack(const QString &key, const QString &name, const QString &desc,
                   const QStringList &tools)
{
    return QVariantMap{{QStringLiteral("key"), key},
                       {QStringLiteral("name"), name},
                       {QStringLiteral("description"), desc},
                       {QStringLiteral("tools"), tools}};
}

}  // namespace

QVariantList HarnessTools::packCatalog()
{
    QVariantList out;
    // Packs compuestos: la escalera historica de niveles, ahora nombrable.
    out << mkPack(QStringLiteral("chat"), QStringLiteral("Chat"),
                  QStringLiteral("Set minimo para chatear y editar codigo."),
                  {QStringLiteral("read_file"), QStringLiteral("list_dir"),
                   QStringLiteral("grep"), QStringLiteral("write_file"),
                   QStringLiteral("edit_file")});
    out << mkPack(QStringLiteral("core"), QStringLiteral("Core"),
                  QStringLiteral("Archivos + busqueda + shell: el piso de un agente de codigo."),
                  {QStringLiteral("read_file"), QStringLiteral("list_dir"),
                   QStringLiteral("glob"), QStringLiteral("grep"),
                   QStringLiteral("write_file"), QStringLiteral("edit_file"),
                   QStringLiteral("run_shell")});
    out << mkPack(QStringLiteral("rag"), QStringLiteral("RAG / conocimiento"),
                  QStringLiteral("Busqueda semantica, hibrida, memoria y grafo."),
                  {QStringLiteral("search_docs"), QStringLiteral("semantic_search"),
                   QStringLiteral("hybrid_search"), QStringLiteral("repo_slice"),
                   QStringLiteral("context_status"), QStringLiteral("work_status"),
                   QStringLiteral("context_scout"),
                   QStringLiteral("context_fetch"),
                   QStringLiteral("verify_claims"), QStringLiteral("memory"),
                   QStringLiteral("graph"), QStringLiteral("project_brain")});
    out << mkPack(QStringLiteral("web"), QStringLiteral("Web"),
                  QStringLiteral("Busqueda y descarga de paginas."),
                  toolsOfGroup(QStringLiteral("web")));
    out << mkPack(QStringLiteral("rpa"), QStringLiteral("Escritorio (RPA)"),
                  QStringLiteral("Automatizacion de apps nativas por UIA y vision."),
                  toolsOfGroup(QStringLiteral("escritorio")));
    out << mkPack(QStringLiteral("multiagente"), QStringLiteral("Multi-agente"),
                  QStringLiteral("Sub-agentes en worktree y escalado al maestro."),
                  toolsOfGroup(QStringLiteral("multi-agente")));
    out << mkPack(QStringLiteral("browser"), QStringLiteral("Browser"),
                  QStringLiteral("Skills grabados de browser y evidencia de red."),
                  toolsOfGroup(QStringLiteral("browser")));
    out << mkPack(QStringLiteral("mail"), QStringLiteral("Correo"),
                  QStringLiteral("Leer y enviar correo (SMTP/IMAP)."),
                  toolsOfGroup(QStringLiteral("correo")));
    out << mkPack(QStringLiteral("skills"), QStringLiteral("Habilidades"),
                  QStringLiteral("Catalogo de skills portables bajo demanda."),
                  toolsOfGroup(QStringLiteral("habilidades")));
    out << mkPack(QStringLiteral("all"), QStringLiteral("Todo"),
                  QStringLiteral("Todas las tools del catalogo."), allToolNames());

    // Un pack por grupo del catalogo (los que no quedaron ya cubiertos arriba).
    QStringList seen{QStringLiteral("web"), QStringLiteral("escritorio"),
                     QStringLiteral("multi-agente"), QStringLiteral("browser"),
                     QStringLiteral("correo"), QStringLiteral("habilidades")};
    QStringList groupsInOrder;
    QMap<QString, QString> labelOf;
    for (const QVariant &v : LlamaAgentBackend::toolCatalog()) {
        const QString label = v.toMap().value(QStringLiteral("group")).toString();
        const QString key = groupKey(label);
        if (seen.contains(key) || groupsInOrder.contains(key)) continue;
        groupsInOrder << key;
        labelOf.insert(key, label);
    }
    for (const QString &key : groupsInOrder)
        out << mkPack(key, labelOf.value(key),
                      QStringLiteral("Grupo del catalogo: %1.").arg(labelOf.value(key)),
                      toolsOfGroup(key));
    return out;
}

QStringList HarnessTools::resolve(const HarnessToolsModule &module)
{
    QSet<QString> on;
    const bool includeAll = module.include.contains(QStringLiteral("*"))
                            || module.packs.contains(QStringLiteral("all"));
    if (includeAll) {
        const QStringList all = allToolNames();
        on = QSet<QString>(all.cbegin(), all.cend());
    } else {
        QMap<QString, QStringList> packs;
        for (const QVariant &v : packCatalog()) {
            const QVariantMap m = v.toMap();
            packs.insert(m.value(QStringLiteral("key")).toString(),
                         m.value(QStringLiteral("tools")).toStringList());
        }
        for (const QString &p : module.packs)
            for (const QString &t : packs.value(p.trimmed().toLower()))
                on.insert(t);
        for (const QString &t : module.include) on.insert(t);
    }
    for (const QString &t : module.exclude) on.remove(t);

    // Orden estable = orden del catalogo (la UI y los tests dependen de esto).
    QStringList out;
    for (const QString &name : allToolNames())
        if (on.contains(name)) out << name;
    return out;
}

QStringList HarnessTools::disabledFrom(const QStringList &enabled)
{
    const QSet<QString> on(enabled.cbegin(), enabled.cend());
    QStringList out;
    for (const QString &name : allToolNames())
        if (!on.contains(name)) out << name;
    return out;
}

bool HarnessSkills::allows(const HarnessSkillsModule &module, const QString &slug)
{
    const QString wanted = slug.trimmed().toLower();
    if (wanted.isEmpty()) return false;
    if (!module.set) return true;
    const bool includeAll = module.include.contains(QStringLiteral("*"));
    if (!includeAll && !module.include.contains(wanted)) return false;
    return !module.exclude.contains(wanted);
}

int HarnessTools::approxTokens(const QStringList &enabled)
{
    const QSet<QString> on(enabled.cbegin(), enabled.cend());
    int total = 0;
    for (const QVariant &v : LlamaAgentBackend::toolCatalog()) {
        const QVariantMap m = v.toMap();
        if (on.contains(m.value(QStringLiteral("name")).toString()))
            total += m.value(QStringLiteral("approxTokens")).toInt();
    }
    return total;
}

QStringList HarnessTools::dependencyWarnings(const QStringList &enabled,
                                             const Environment &env,
                                             bool mcpToolsEnabled)
{
    const QSet<QString> on(enabled.cbegin(), enabled.cend());
    QStringList out;
    auto anyOn = [&on](const QStringList &names) {
        for (const QString &n : names)
            if (on.contains(n)) return true;
        return false;
    };
    // Cada aviso dice QUE falta y QUE HACER: un warning sin acción obliga al
    // usuario a adivinar dónde se arregla.
    if (!env.hasGit && on.contains(QStringLiteral("task")))
        out << QStringLiteral("task: sin git no se pueden crear worktrees para sub-agentes. "
                              "Instalá git y reabrí el proyecto, o apagá el aislamiento en "
                              "el módulo escalation.");
    if (!env.hasGit && on.contains(QStringLiteral("code_hotspots")))
        out << QStringLiteral("code_hotspots: necesita historial git para calcular churn. "
                              "Instalá git o sacá la tool del perfil.");
    if (!env.hasEmbeddings
        && anyOn({QStringLiteral("semantic_search"), QStringLiteral("hybrid_search")}))
        out << QStringLiteral("semantic_search/hybrid_search: no hay server activo que exponga "
                              "/v1/embeddings. Arrancá el server del perfil, o usá grep/"
                              "search_docs en su lugar.");
    if (!env.hasDesktop && anyOn(toolsOfGroup(QStringLiteral("escritorio"))))
        out << QStringLiteral("desktop_*: no hay sesion de escritorio interactiva (headless). "
                              "Corré la app con UI o sacá el pack rpa del perfil.");
    if (!env.hasMailAccount && anyOn(toolsOfGroup(QStringLiteral("correo"))))
        out << QStringLiteral("email_*: no hay cuentas de correo configuradas. Agregá una en "
                              "Ajustes → Integraciones, o sacá el pack mail del perfil.");
    if (!env.hasBrowser && anyOn(toolsOfGroup(QStringLiteral("browser"))))
        out << QStringLiteral("browser_*: la automatizacion de browser esta apagada. Activala "
                              "en Ajustes (o por perfil, browserAutomation), o sacá el pack.");
    if (!env.hasMcpServers && mcpToolsEnabled)
        out << QStringLiteral("MCP: no hay servers configurados/habilitados, así que no se "
                              "inyecta ninguna tool MCP. Configurá uno en Ajustes → MCP, o "
                              "apagá mcpTools para ahorrar el chequeo.");
    return out;
}

// ---------------------------------------------------------------------------
// Politica
// ---------------------------------------------------------------------------

namespace {
int scopeRank(const QString &scope)
{
    if (scope == QLatin1String("full")) return 2;
    if (scope == QLatin1String("folder")) return 1;
    return 0;   // project (el mas restrictivo)
}
}  // namespace

QString HarnessPolicy::narrowerScope(const QString &a, const QString &b)
{
    return scopeRank(a) <= scopeRank(b) ? a : b;
}

QStringList HarnessPolicy::intersectFolders(const QString &scopeA, const QStringList &a,
                                            const QString &scopeB, const QStringList &b)
{
    // "full" no aporta restriccion: la lista del otro manda.
    if (scopeA == QLatin1String("full")) return b;
    if (scopeB == QLatin1String("full")) return a;
    if (scopeA != QLatin1String("folder")) return {};
    if (scopeB != QLatin1String("folder")) return {};
    const QSet<QString> setB(b.cbegin(), b.cend());
    QStringList out;
    for (const QString &f : a)
        if (setB.contains(f)) out << f;
    return out;
}
