#include "ProfileManager.h"
#include "SystemProfileVariants.h"
#include "core/agent/HarnessDirectiveStore.h"
#include "core/agent/PortableSkillStore.h"
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QTimer>
#include <QDebug>
#include <QRegularExpression>
#include <QUuid>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

// Launch profile display names carry a stable, non-repeating incremental id as a
// "<n>_" prefix. The number never changes for an existing profile and is never
// reused: a new profile always takes (max existing number + 1), even across
// deletions. These helpers parse/strip that prefix.
namespace {
const QRegularExpression kSeqPrefix(QStringLiteral("^(\\d+)_"));

int seqOf(const QString &name) {
    const auto m = kSeqPrefix.match(name);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}
QString stripSeq(const QString &name) {
    return QString(name).remove(kSeqPrefix);
}

bool isCuratedBestName(const LaunchProfile &profile)
{
    const QString key = (profile.name + QLatin1Char(' ') + profile.alias).toLower();
    return key.contains(QStringLiteral("kat-coder-7-8-26"))
        || key.contains(QStringLiteral("fast-kat"))
        || key.contains(QStringLiteral("bigbang-131k"))
        || key.contains(QStringLiteral("fast-bigbang"))
        || key.contains(QStringLiteral("thinkingcap-qwen3.6"))
        || key.contains(QStringLiteral("balance-thinkingcap"))
        || key.contains(QStringLiteral("laguna-s-2.1"))
        || key.contains(QStringLiteral("balance-laguna"))
        || profile.id == QStringLiteral("4f5cc556-333d-4310-955e-15042cd874d6")
        || (key.contains(QStringLiteral("141_"))
            && key.contains(QStringLiteral("fusion leloch")));
}
}

ProfileManager::ProfileManager(QObject *parent) : QObject(parent)
{
    load();

    // Seed default runtime preset if empty
    if (m_runtimes.m_items.isEmpty()) {
        RuntimePreset def;
        def.id = RuntimePreset::generateId();
        def.name = "Default";
        m_runtimes.add(def);
        save();
    }

    // Perfiles de sistema (bundled, inmutables): se anteponen en memoria y NO se
    // persisten. Cargar después del seed/save para no escribirlos al disco.
    loadSystemProfiles();

    setupWatcher();
}

void ProfileManager::reloadFromDisk()
{
    load();
    loadSystemProfiles();   // re-anteponer perfiles de sistema (load() los pisa)
    emit profilesReloaded();
}

void ProfileManager::setupWatcher()
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged,
            this, &ProfileManager::onProfileFileChanged);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &ProfileManager::onProfileDirectoryChanged);
    const QString profilesDir = QFileInfo(storagePath(QStringLiteral("backends"))).absolutePath();
    if (QDir(profilesDir).exists() && !m_watcher.directories().contains(profilesDir))
        m_watcher.addPath(profilesDir);
    for (const QString &ent : {QStringLiteral("backends"), QStringLiteral("models"),
                               QStringLiteral("runtimes"), QStringLiteral("harnesses"),
                               QStringLiteral("workspaces"), QStringLiteral("launches"),
                               QStringLiteral("agent_profiles"), QStringLiteral("persona_styles")}) {
        const QString p = storagePath(ent);
        if (QFile::exists(p)) m_watcher.addPath(p);
    }
}

void ProfileManager::onProfileFileChanged(const QString &path)
{
    // Re-armar el watch: una escritura atómica (rename) o algunos editores
    // reemplazan el archivo y el watcher pierde el path.
    QTimer::singleShot(0, this, [this, path]() {
        if (QFile::exists(path) && !m_watcher.files().contains(path))
            m_watcher.addPath(path);
    });

    if (m_saving) return;   // cambio provocado por nuestro propio save(): ignorar

    // Cambio externo (otra instancia / edición manual): recargar para no pisar.
    qInfo() << "[ProfileManager] cambio externo detectado, recargando:" << path;
    reloadFromDisk();
}

void ProfileManager::onProfileDirectoryChanged(const QString &path)
{
    // Atomic replacement removes a file watch briefly; the directory watch is
    // the reliable second signal for edits made by another process.
    if (m_saving) return;
    QTimer::singleShot(50, this, [this, path]() {
        if (m_saving) return;
        qInfo() << "[ProfileManager] cambio externo detectado en directorio, recargando:" << path;
        reloadFromDisk();
        for (const QString &ent : {QStringLiteral("backends"), QStringLiteral("models"),
                                   QStringLiteral("runtimes"), QStringLiteral("harnesses"),
                                   QStringLiteral("workspaces"), QStringLiteral("launches"),
                                   QStringLiteral("agent_profiles"), QStringLiteral("persona_styles")}) {
            const QString file = storagePath(ent);
            if (QFile::exists(file) && !m_watcher.files().contains(file))
                m_watcher.addPath(file);
        }
    });
}

// ---- BackendProfile ----

QString ProfileManager::addBackend(const QString &name, const QString &binaryId,
                                    const QString &host, int port)
{
    BackendProfile p;
    p.id = BackendProfile::generateId();
    p.name = name.isEmpty() ? "Backend" : name;
    p.binaryId = binaryId;
    p.host = host.isEmpty() ? "127.0.0.1" : host;
    p.port = port > 0 ? port : 8080;
    m_backends.add(p);
    save();
    return p.id;
}

bool ProfileManager::removeBackend(const QString &id)
{
    if (m_backends.findById(id).system) return false;
    bool ok = m_backends.remove(id);
    if (ok) save();
    return ok;
}

bool ProfileManager::updateBackend(const QString &id, const QString &name,
                                    const QString &binaryId, const QString &host,
                                    int port, const QStringList &baseArgs)
{
    BackendProfile p = m_backends.findById(id);
    if (p.id.isEmpty() || p.system) return false;
    p.name = name; p.binaryId = binaryId;
    p.host = host; p.port = port;
    p.baseArgs = baseArgs;
    bool ok = m_backends.update(p);
    if (ok) save();
    return ok;
}

bool ProfileManager::updateBackendPort(const QString &id, int port)
{
    if (port <= 0 || port > 65535) return false;
    BackendProfile p = m_backends.findById(id);
    if (p.id.isEmpty()) return false;
    if (p.port == port) return true;
    p.port = port;
    const bool ok = m_backends.update(p);
    if (ok) save();
    return ok;
}

QVariantMap ProfileManager::getBackend(const QString &id) const
{
    const auto p = m_backends.findById(id);
    if (p.id.isEmpty()) return {};
    return {{"id", p.id}, {"name", p.name}, {"binaryId", p.binaryId},
            {"host", p.host}, {"port", p.port}, {"baseArgs", p.baseArgs},
            {"kind", p.kind}, {"cloudBaseUrl", p.cloudBaseUrl},
            {"cloudKeyRef", p.cloudKeyRef}, {"cloudModel", p.cloudModel},
            {"cloudCtx", p.cloudCtx}};
}

bool ProfileManager::setBackendCloud(const QString &id, const QString &kind,
                                     const QString &baseUrl, const QString &keyRef,
                                     const QString &model, int ctx)
{
    BackendProfile p = m_backends.findById(id);
    if (p.id.isEmpty()) return false;
    p.kind = (kind == QLatin1String("cloud")) ? QStringLiteral("cloud")
                                              : QStringLiteral("local");
    p.cloudBaseUrl = baseUrl.trimmed();
    p.cloudKeyRef = keyRef.trimmed();
    p.cloudModel = model.trimmed();
    p.cloudCtx = ctx > 0 ? ctx : 0;
    bool ok = m_backends.update(p);
    if (ok) save();
    return ok;
}

// ---- ModelProfile ----

QString ProfileManager::addModelProfile(const QString &name, const QString &modelId,
                                         const QString &mmprojId, const QString &draftId)
{
    ModelProfile p;
    p.id = ModelProfile::generateId();
    p.name = name.isEmpty() ? "Model" : name;
    p.modelId = modelId;
    p.mmprojId = mmprojId;
    p.draftModelId = draftId;
    m_models.add(p);
    save();
    return p.id;
}

bool ProfileManager::removeModelProfile(const QString &id)
{
    if (m_models.findById(id).system) return false;
    bool ok = m_models.remove(id);
    if (ok) save();
    return ok;
}

bool ProfileManager::updateModelProfileFull(const ModelProfile &p)
{
    const ModelProfile cur = m_models.findById(p.id);
    if (cur.id.isEmpty() || cur.system) return false;
    ModelProfile next = p;
    next.system = false;
    const bool ok = m_models.update(next);
    if (ok) save();
    return ok;
}

bool ProfileManager::updateModelProfile(const QString &id, const QString &name,
                                         const QString &modelId, const QString &mmprojId,
                                         const QString &draftId)
{
    ModelProfile p = m_models.findById(id);
    if (p.id.isEmpty() || p.system) return false;
    // Elegir otro archivo invalida el ancla del anterior. La ponemos en 0 en vez de
    // arrastrar la vieja —que apuntaría al modelo equivocado— y buildContext la
    // vuelve a resolver contra el catálogo la próxima vez que se use el perfil.
    if (p.modelId != modelId)      p.modelStableId = 0;
    if (p.mmprojId != mmprojId)    p.mmprojStableId = 0;
    if (p.draftModelId != draftId) p.draftStableId = 0;
    p.name = name; p.modelId = modelId;
    p.mmprojId = mmprojId; p.draftModelId = draftId;
    bool ok = m_models.update(p);
    if (ok) save();
    return ok;
}

bool ProfileManager::setModelSpec(const QString &id, const QString &specType,
                                  int specDraftNMax, const QString &specDraftNgl,
                                  const QString &specDraftTypeK,
                                  const QString &specDraftTypeV,
                                  double specDraftConfMin,
                                  int specDraftNMin,
                                  bool specDraftAdaptive)
{
    ModelProfile p = m_models.findById(id);
    if (p.id.isEmpty() || p.system) return false;
    p.specType = specType;
    p.specDraftNMax = specDraftNMax > 0 ? specDraftNMax : 0;
    p.specDraftNMin = qMax(0, specDraftNMin);
    p.specDraftAdaptive = specDraftAdaptive;
    p.specDraftConfMin = qBound(0.0, specDraftConfMin, 1.0);
    p.specDraftNgl = specDraftNgl;
    p.specDraftTypeK = specDraftTypeK;
    p.specDraftTypeV = specDraftTypeV;
    bool ok = m_models.update(p);
    if (ok) save();
    return ok;
}

QVariantMap ProfileManager::getModelProfile(const QString &id) const
{
    const auto p = m_models.findById(id);
    if (p.id.isEmpty()) return {};
    return {{"id", p.id}, {"name", p.name}, {"modelId", p.modelId},
            {"mmprojId", p.mmprojId}, {"draftModelId", p.draftModelId},
            {"specType", p.specType}, {"specDraftNMax", p.specDraftNMax},
            {"specDraftNMin", p.specDraftNMin},
            {"specDraftAdaptive", p.specDraftAdaptive},
            {"specDraftConfMin", p.specDraftConfMin},
            {"specDraftNgl", p.specDraftNgl},
            {"specDraftTypeK", p.specDraftTypeK},
            {"specDraftTypeV", p.specDraftTypeV}};
}

// ---- RuntimePreset ----

QString ProfileManager::addRuntimePreset(const QString &name, int ctx, int batch,
                                          int gpuLayers, bool flashAttn, bool contBatch)
{
    RuntimePreset p;
    p.id = RuntimePreset::generateId();
    p.name = name.isEmpty() ? "Runtime" : name;
    p.ctx = ctx > 0 ? ctx : 4096;
    p.batch = batch > 0 ? batch : 512;
    p.gpuLayers = gpuLayers;
    p.flashAttention = flashAttn;
    p.contBatching = contBatch;
    m_runtimes.add(p);
    save();
    return p.id;
}

bool ProfileManager::removeRuntimePreset(const QString &id)
{
    if (m_runtimes.findById(id).system) return false;
    bool ok = m_runtimes.remove(id);
    if (ok) save();
    return ok;
}

bool ProfileManager::updateRuntimePreset(const QVariantMap &data)
{
    RuntimePreset p = m_runtimes.findById(data["id"].toString());
    if (p.id.isEmpty() || p.system) return false;
    p.name = data.value("name", p.name).toString();
    p.ctx = data.value("ctx", p.ctx).toInt();
    p.batch = data.value("batch", p.batch).toInt();
    p.ubatch = data.value("ubatch", p.ubatch).toInt();
    p.threads = data.value("threads", p.threads).toInt();
    p.gpuLayers = data.value("gpuLayers", p.gpuLayers).toInt();
    p.flashAttention = data.value("flashAttention", p.flashAttention).toBool();
    p.mmap = data.value("mmap", p.mmap).toBool();
    p.mlock = data.value("mlock", p.mlock).toBool();
    p.contBatching = data.value("contBatching", p.contBatching).toBool();
    p.cacheType = data.value("cacheType", p.cacheType).toString();
    p.parallelSlots = data.value("parallelSlots", p.parallelSlots).toInt();
    bool ok = m_runtimes.update(p);
    if (ok) save();
    return ok;
}

QVariantMap ProfileManager::getRuntimePreset(const QString &id) const
{
    const auto p = m_runtimes.findById(id);
    if (p.id.isEmpty()) return {};
    return {{"id", p.id}, {"name", p.name}, {"ctx", p.ctx}, {"batch", p.batch},
            {"ubatch", p.ubatch}, {"threads", p.threads}, {"gpuLayers", p.gpuLayers},
            {"flashAttention", p.flashAttention}, {"mmap", p.mmap}, {"mlock", p.mlock},
            {"contBatching", p.contBatching}, {"cacheType", p.cacheType},
            {"parallelSlots", p.parallelSlots}};
}

// ---- HarnessProfile ----

QString ProfileManager::addHarness(const QString &name, const QString &adapter)
{
    HarnessProfile p;
    p.id = HarnessProfile::generateId();
    p.name = name.isEmpty() ? adapter : name;
    p.adapter = adapter;
    m_harnesses.add(p);
    save();
    return p.id;
}

bool ProfileManager::removeHarness(const QString &id)
{
    bool ok = m_harnesses.remove(id);
    if (ok) save();
    return ok;
}

bool ProfileManager::updateHarness(const QVariantMap &data)
{
    HarnessProfile p = m_harnesses.findById(data["id"].toString());
    if (p.id.isEmpty()) return false;
    p.name    = data.value("name",    p.name).toString();
    p.adapter = data.value("adapter", p.adapter).toString();
    const QVariant argsV = data.value("args");
    if (argsV.isValid()) p.args = argsV.toStringList();
    if (data.contains("env")) {
        p.env.clear();
        const QVariantMap env = data.value("env").toMap();
        for (auto it = env.cbegin(); it != env.cend(); ++it) p.env.insert(it.key(), it.value().toString());
    }
    bool ok = m_harnesses.update(p);
    if (ok) save();
    return ok;
}

QVariantMap ProfileManager::getHarness(const QString &id) const
{
    const auto p = m_harnesses.findById(id);
    if (p.id.isEmpty()) return {};
    return {{"id", p.id}, {"name", p.name}, {"adapter", p.adapter}, {"args", p.args},
            {"env", QVariant::fromValue(p.env)}};
}

// ---- LaunchProfile ----

QString ProfileManager::addLaunchProfile(const QString &name,
                                          const QString &backendId,
                                          const QString &modelId,
                                          const QString &runtimeId)
{
    LaunchProfile p;
    p.id = LaunchProfile::generateId();
    // Assign a stable, non-repeating incremental id = max existing + 1.
    int maxSeq = 0;
    for (const auto &x : m_launches.m_items) maxSeq = std::max(maxSeq, seqOf(x.name));
    const QString base = stripSeq(name.isEmpty() ? QStringLiteral("Launch") : name);
    p.name = QStringLiteral("%1_%2").arg(maxSeq + 1).arg(base);
    p.backendProfileId = backendId;
    p.modelProfileId = modelId;
    p.runtimePresetId = runtimeId;
    m_launches.add(p);
    save();
    emit launchesChanged();
    return p.id;
}

bool ProfileManager::removeLaunchProfile(const QString &id)
{
    if (m_launches.findById(id).system) {
        emit errorOccurred(QStringLiteral("Perfil de sistema: solo lectura (duplicalo para editar)."));
        return false;
    }
    bool ok = m_launches.remove(id);
    if (ok) { save(); emit launchesChanged(); }
    return ok;
}

bool ProfileManager::updateLaunchProfile(const QVariantMap &data)
{
    LaunchProfile p = m_launches.findById(data["id"].toString());
    if (p.id.isEmpty()) return false;
    if (p.system) {
        emit errorOccurred(QStringLiteral("Perfil de sistema: solo lectura (duplicalo para editar)."));
        return false;
    }
    // Preserve this profile's stable incremental id even if the edited name
    // drops or changes the "<n>_" prefix.
    int seq = seqOf(p.name);
    if (seq == 0) {
        for (const auto &x : m_launches.m_items) seq = std::max(seq, seqOf(x.name));
        seq += 1;
    }
    const QString newName = data.value("name", p.name).toString();
    p.name = QStringLiteral("%1_%2").arg(seq).arg(stripSeq(newName));
    if (data.contains("alias"))    p.alias = data.value("alias").toString();
    if (data.contains("tags")) {
        p.tags.clear();
        for (const QString &tag : data.value("tags").toStringList()) {
            const QString clean = tag.trimmed();
            if (!clean.isEmpty() && !p.tags.contains(clean, Qt::CaseInsensitive))
                p.tags.append(clean);
        }
    }
    if (data.contains("best"))     p.best = data.value("best").toBool();
    if (data.contains("favorite")) p.favorite = data.value("favorite").toBool();
    if (data.contains("benchmark")) p.benchmark = data.value("benchmark").toBool();
    if (data.contains("systemBadge")) p.systemBadge = data.value("systemBadge").toBool();
    if (data.contains("deprecated")) p.deprecated = data.value("deprecated").toBool();
    p.backendProfileId = data.value("backendProfileId", p.backendProfileId).toString();
    p.modelProfileId = data.value("modelProfileId", p.modelProfileId).toString();
    p.runtimePresetId = data.value("runtimePresetId", p.runtimePresetId).toString();
    p.harnessProfileId = data.value("harnessProfileId", p.harnessProfileId).toString();
    p.workspaceProfileId = data.value("workspaceProfileId", p.workspaceProfileId).toString();
    p.agentProfileId = data.value("agentProfileId", p.agentProfileId).toString();
    p.plannerProfileId = data.value("plannerProfileId", p.plannerProfileId).toString();
    p.hybridMode = data.value("hybridMode", p.hybridMode).toString();
    if (p.plannerProfileId.isEmpty()) p.hybridMode = QStringLiteral("off");
    p.extraArgs = data.value("extraArgs", p.extraArgs).toStringList();
    if (data.contains("envOverrides")) {
        p.envOverrides.clear();
        const QVariantMap env = data.value("envOverrides").toMap();
        for (auto it = env.cbegin(); it != env.cend(); ++it)
            p.envOverrides.insert(it.key(), it.value().toString());
    }
    if (data.contains("master")) {
        const QVariantMap m = data.value("master").toMap();
        MasterConfig mc = p.master;
        mc.escalation = m.value("escalation", mc.escalation).toString();
        mc.autoAfterFails = m.value("autoAfterFails", mc.autoAfterFails).toInt();
        // Cadena de fallbacks (ordenada). Reemplaza la lista entera si viene.
        if (m.contains("fallbacks")) {
            mc.fallbacks.clear();
            const QVariantList arr = m.value("fallbacks").toList();
            for (const QVariant &fv : arr) {
                const QVariantMap fm = fv.toMap();
                MasterFallback f;
                f.type       = fm.value("type", "http").toString();
                f.label      = fm.value("label").toString();
                f.profileId  = fm.value("profileId").toString();
                f.httpUrl    = fm.value("httpUrl").toString();
                f.httpModel  = fm.value("httpModel").toString();
                f.httpKeyRef = fm.value("httpKeyRef").toString();
                f.cliName    = fm.value("cliName").toString();
                f.applyEdits = fm.value("applyEdits", true).toBool();
                f.timeoutSec = fm.value("timeoutSec", 300).toInt();
                mc.fallbacks.append(f);
            }
        }
        p.master = mc;
    }
    if (data.contains("browserAutomation"))
        p.browserAutomation = data.value("browserAutomation").toString();
    bool ok = m_launches.update(p);
    if (ok) { save(); emit launchesChanged(); }
    return ok;
}

// Serializa MasterConfig (cadena de fallbacks + política) a QVariantMap para QML.
static QVariantMap masterToVariant(const MasterConfig &mc)
{
    QVariantList arr;
    for (const MasterFallback &f : mc.fallbacks) {
        arr.append(QVariantMap{
            {"type", f.type}, {"label", f.label}, {"profileId", f.profileId},
            {"httpUrl", f.httpUrl}, {"httpModel", f.httpModel},
            {"httpKeyRef", f.httpKeyRef}, {"cliName", f.cliName},
            {"applyEdits", f.applyEdits}, {"timeoutSec", f.timeoutSec}});
    }
    return QVariantMap{
        {"fallbacks", arr},
        {"escalation", mc.escalation},
        {"autoAfterFails", mc.autoAfterFails}};
}

QVariantMap ProfileManager::getLaunchProfile(const QString &id) const
{
    const auto p = m_launches.findById(id);
    if (p.id.isEmpty()) return {};
    const QString displayName = p.alias.isEmpty()
        ? p.name
        : QStringLiteral("%1 - %2").arg(p.alias, p.name);
    return {{"id", p.id}, {"name", p.name},
            {"alias", p.alias}, {"best", p.best}, {"favorite", p.favorite}, {"benchmark", p.benchmark},
            {"tags", p.tags}, {"lastUsed", p.lastUsed},
            {"deprecated", p.deprecated}, {"systemBadge", p.systemBadge},
            {"system", p.system},
            {"displayName", displayName},
            {"backendProfileId", p.backendProfileId},
            {"modelProfileId", p.modelProfileId},
            {"runtimePresetId", p.runtimePresetId},
            {"harnessProfileId", p.harnessProfileId},
            {"workspaceProfileId", p.workspaceProfileId},
            {"agentProfileId", p.agentProfileId},
            {"reasoningEffort", p.reasoningEffort},
            {"reasoningBudget", p.reasoningBudget},
            {"plannerProfileId", p.plannerProfileId},
            {"hybridMode", p.hybridMode},
            {"extraArgs", p.extraArgs},
            // El env llega al server via EffectiveProfileBuilder, pero sin esto la UI
            // y el headless no pueden verlo: hay perfiles que dependen de una env var
            // para no crashear (GGML_CUDA_DISABLE_GRAPHS en el de 393k).
            {"envOverrides", QVariant::fromValue(p.envOverrides)},
            {"browserAutomation", p.browserAutomation},
            {"master", masterToVariant(p.master)}};
}

QVariantMap ProfileManager::getLaunchVoice(const QString &id) const
{
    const auto p = m_launches.findById(id);
    return p.voice.toJson().toVariantMap();   // defaults si el perfil no existe
}

bool ProfileManager::setLaunchVoice(const QString &id, const QVariantMap &voiceCfg)
{
    LaunchProfile p = m_launches.findById(id);
    if (p.id.isEmpty() || p.system) return false;
    p.voice = VoiceConfig::fromJson(QJsonObject::fromVariantMap(voiceCfg));
    const bool ok = m_launches.update(p);
    if (ok) { save(); emit launchesChanged(); }
    return ok;
}

void ProfileManager::setLaunchFavorite(const QString &id, bool favorite)
{
    LaunchProfile p = m_launches.findById(id);
    if (p.id.isEmpty() || p.system || p.favorite == favorite) return;
    p.favorite = favorite;
    if (m_launches.update(p)) { save(); emit launchesChanged(); }
}

void ProfileManager::setLaunchBenchmark(const QString &id, bool benchmark)
{
    LaunchProfile p = m_launches.findById(id);
    if (p.id.isEmpty() || p.system || p.benchmark == benchmark) return;
    p.benchmark = benchmark;
    if (m_launches.update(p)) { save(); emit launchesChanged(); }
}

void ProfileManager::setLaunchAlias(const QString &id, const QString &alias)
{
    LaunchProfile p = m_launches.findById(id);
    if (p.id.isEmpty() || p.system || p.alias == alias) return;
    p.alias = alias;
    if (m_launches.update(p)) { save(); emit launchesChanged(); }
}

void ProfileManager::setLaunchTags(const QString &id, const QStringList &tags)
{
    LaunchProfile p = m_launches.findById(id);
    if (p.id.isEmpty() || p.system) return;
    p.tags.clear();
    for (const QString &tag : tags) {
        const QString clean = tag.trimmed();
        if (!clean.isEmpty() && !p.tags.contains(clean, Qt::CaseInsensitive))
            p.tags.append(clean);
    }
    if (m_launches.update(p)) { save(); emit launchesChanged(); }
}

void ProfileManager::markLaunchUsed(const QString &id)
{
    LaunchProfile p = m_launches.findById(id);
    if (p.id.isEmpty() || p.system) return;
    p.lastUsed = QDateTime::currentMSecsSinceEpoch();
    if (m_launches.update(p)) { save(); emit launchesChanged(); }
}

// Lista de perfiles de lanzamiento para dropdowns: BEST primero (rayo), luego
// favoritos (estrella), y finalmente por id incremental.
// luego por id incremental; displayName = "alias - name" si hay alias.
QVariantList ProfileManager::launchProfilesForMenu() const
{
    QList<LaunchProfile> items = m_launches.m_items;
    std::stable_sort(items.begin(), items.end(),
        [](const LaunchProfile &a, const LaunchProfile &b) {
            if (a.best != b.best) return a.best;                 // BEST arriba
            if (a.favorite != b.favorite) return a.favorite;     // favoritos arriba
            if (a.lastUsed != b.lastUsed) return a.lastUsed > b.lastUsed;
            return seqOf(a.name) < seqOf(b.name);                // luego por nº incremental
        });
    QVariantList out;
    for (const auto &p : items) {
        if (p.deprecated) continue;
        const QString base = p.alias.isEmpty()
            ? p.name
            : QStringLiteral("%1 - %2").arg(p.alias, p.name);
        // Marcadores independientes: sistema, favorito y benchmark.
        QString mark;
        if (p.best) mark += QStringLiteral("⚡ ");
        if (p.systemBadge) mark += QStringLiteral("⚙ ");
        if (p.favorite) mark += QStringLiteral("★ ");
        if (p.benchmark) mark += QStringLiteral("🏆 ");
        out.append(QVariantMap{
            {"id", p.id}, {"name", p.name}, {"alias", p.alias},
            {"best", p.best}, {"favorite", p.favorite}, {"benchmark", p.benchmark},
            {"tags", p.tags}, {"lastUsed", p.lastUsed},
            {"deprecated", p.deprecated}, {"systemBadge", p.systemBadge},
            {"system", p.system},
            // displayName lleva el marcador (⚙ sistema / ★ favorito) para verlo en
            // el dropdown y el texto seleccionado; si hay alias va junto al nombre.
            {"displayName", mark + base}});
    }
    return out;
}

QVariantList ProfileManager::launchProfilesForProfilesPage(const QString &query) const
{
    QList<LaunchProfile> items = m_launches.m_items;
    const QString needle = query.trimmed().toCaseFolded();
    std::stable_sort(items.begin(), items.end(),
        [](const LaunchProfile &a, const LaunchProfile &b) {
            if (a.best != b.best) return a.best;
            if (a.favorite != b.favorite) return a.favorite;
            if (a.lastUsed != b.lastUsed) return a.lastUsed > b.lastUsed;
            return seqOf(a.name) < seqOf(b.name);
        });
    QVariantList out;
    for (const auto &p : items) {
        const QString base = p.alias.isEmpty()
            ? p.name : QStringLiteral("%1 - %2").arg(p.alias, p.name);
        if (!needle.isEmpty()
            && !p.name.toCaseFolded().contains(needle)
            && !p.alias.toCaseFolded().contains(needle)
            && !p.id.toCaseFolded().contains(needle)
            && !std::any_of(p.tags.cbegin(), p.tags.cend(), [&](const QString &tag) {
                return tag.toCaseFolded().contains(needle);
            }))
            continue;
        QString mark;
        if (p.best) mark += QStringLiteral("⚡ ");
        if (p.systemBadge) mark += QStringLiteral("⚙ ");
        if (p.favorite) mark += QStringLiteral("★ ");
        if (p.benchmark) mark += QStringLiteral("🏆 ");
        if (p.deprecated) mark += QStringLiteral("⚠ ");
        out.append(QVariantMap{
            {"id", p.id}, {"name", p.name}, {"alias", p.alias},
            {"best", p.best}, {"favorite", p.favorite}, {"benchmark", p.benchmark},
            {"tags", p.tags}, {"lastUsed", p.lastUsed},
            {"deprecated", p.deprecated}, {"systemBadge", p.systemBadge},
            {"system", p.system},
            {"displayName", mark + base}});
    }
    return out;
}

// ---- AgentProfile ----

static QVariantMap agentProfileToVariant(const AgentProfile &p)
{
    return {{"id", p.id}, {"name", p.name}, {"system", p.system},
            {"enabledTools", p.enabledTools}, {"directives", p.directives},
            {"approvalMode", p.approvalMode}, {"thinking", p.thinking},
            {"temperature", p.temperature}, {"systemExtra", p.systemExtra},
            {"personalityProfileIds", p.personalityProfileIds},
            {"styleProfileIds", p.styleProfileIds},
            {"injectStyleExamples", p.injectStyleExamples},
            {"styleExampleLimit", p.styleExampleLimit},
            {"styleContextLimit", p.styleContextLimit},
            {"mcpEnabled", p.mcpEnabled}, {"thinkingLeakGuard", p.thinkingLeakGuard},
            {"progressCredits", p.progressCredits},
            {"progressMaxCredits", p.progressMaxCredits},
            {"progressReplanAfter", p.progressReplanAfter},
            {"progressStopAfter", p.progressStopAfter},
            {"quickToolTimeoutSec", p.quickToolTimeoutSec}};
}

namespace {
struct AgentTaskPreset {
    QString kind;
    QString label;
    QString reason;
    QStringList tools;
    QStringList directives;
    double temperature;
    bool thinking;
    bool mcp;
};

AgentTaskPreset taskPreset(const QString &kind)
{
    const QStringList core{"read_file", "list_dir", "glob", "grep", "write_file",
                           "edit_file", "run_shell"};
    if (kind == QLatin1String("research"))
        return {kind, QStringLiteral("Investigación"),
                QStringLiteral("Conviene razonar, buscar fuentes y verificar afirmaciones."),
                core + QStringList{"search_docs", "memory", "web_search", "web_fetch",
                                   "semantic_search", "hybrid_search", "repo_slice",
                                   "context_status", "context_scout", "context_fetch",
                                   "verify_claims", "graph"},
                {"discipline", "testNet", "projectContext", "efficiency", "style"},
                0.35, true, true};
    if (kind == QLatin1String("planning"))
        return {kind, QStringLiteral("Planificación"),
                QStringLiteral("La tarea pide análisis y un plan cuidadoso con pocas herramientas."),
                {"read_file", "list_dir", "glob", "grep", "search_docs", "memory", "code_hotspots"},
                {"discipline", "projectContext", "efficiency", "style"}, 0.3, true, false};
    if (kind == QLatin1String("creative"))
        return {kind, QStringLiteral("Creatividad"),
                QStringLiteral("Una temperatura moderadamente mayor ayuda a explorar alternativas."),
                {"read_file", "list_dir", "grep", "write_file", "edit_file"},
                {"style", "efficiency"}, 0.75, false, false};
    if (kind == QLatin1String("quick"))
        return {kind, QStringLiteral("Tarea rápida"),
                QStringLiteral("Un perfil liviano reduce contexto y latencia innecesarios."),
                {"read_file", "list_dir", "grep", "write_file", "edit_file"}, {}, 0.4, false, false};
    return {QStringLiteral("coding"), QStringLiteral("Código preciso"),
            QStringLiteral("El cambio se beneficia de sampling conservador, herramientas de código y pruebas."),
            core + QStringList{"search_docs", "memory", "code_hotspots"},
            {"discipline", "testNet", "projectContext", "efficiency", "style"},
            0.6, true, false};
}

bool hasAny(const QString &text, const QStringList &needles)
{
    for (const QString &needle : needles)
        if (text.contains(needle)) return true;
    return false;
}

QString classifyAgentTask(const QString &prompt)
{
    const QString p = prompt.toLower();
    if (hasAny(p, {"investig", "research", "fuentes", "source", "verific", "compará",
                   "compara", "estado del arte", "deep dive"}))
        return QStringLiteral("research");
    if (hasAny(p, {"planific", "diseñá un plan", "diseña un plan", "arquitectura",
                   "estrategia", "roadmap", "analizá", "analiza", "review", "revisá"}))
        return QStringLiteral("planning");
    if (hasAny(p, {"creativ", "brainstorm", "ideas", "cuento", "poema", "eslogan",
                   "nombre para", "alternativas originales"}))
        return QStringLiteral("creative");
    if (p.size() < 90 && hasAny(p, {"explicá", "explica", "resumí", "resume", "qué es",
                                    "que es", "traducí", "traduce"}))
        return QStringLiteral("quick");
    if (hasAny(p, {"código", "codigo", "bug", "error", "test", "implement", "refactor",
                   "compil", "archivo", "función", "funcion", "clase", "repo"}))
        return QStringLiteral("coding");
    return {};
}
}

QVariantMap ProfileManager::recommendAgentProfile(const QString &prompt,
                                                   const QString &currentProfileId) const
{
    const QString kind = classifyAgentTask(prompt.trimmed());
    const AgentProfile current = m_agentProfiles.findById(currentProfileId);
    if (kind.isEmpty() || current.id.isEmpty()) return {};
    const AgentTaskPreset preset = taskPreset(kind);
    const bool same = current.enabledTools == preset.tools
        && current.directives == preset.directives
        && qAbs(current.temperature - preset.temperature) < 0.001
        && current.thinking == preset.thinking && current.mcpEnabled == preset.mcp;
    if (same) return {};
    return {{"kind", preset.kind}, {"label", preset.label}, {"reason", preset.reason},
            {"temperature", preset.temperature}, {"thinking", preset.thinking},
            {"toolCount", preset.tools.size()}, {"mcpEnabled", preset.mcp}};
}

QString ProfileManager::createRecommendedAgentProfile(const QString &sourceProfileId,
                                                        const QString &taskKind)
{
    const AgentProfile source = m_agentProfiles.findById(sourceProfileId);
    if (source.id.isEmpty()) return {};
    const AgentTaskPreset preset = taskPreset(taskKind);
    AgentProfile p = source;
    p.id = AgentProfile::generateId();
    p.system = false;
    p.name = source.name + QStringLiteral(" · ") + preset.label;
    p.enabledTools = preset.tools;
    p.directives = preset.directives;
    p.temperature = preset.temperature;
    p.thinking = preset.thinking;
    p.mcpEnabled = preset.mcp;
    m_agentProfiles.add(p);
    save();
    return p.id;
}

QString ProfileManager::addAgentProfile(const QString &name)
{
    AgentProfile p;
    p.id = AgentProfile::generateId();
    p.name = name.isEmpty() ? QStringLiteral("Perfil de agente") : name;
    // Arranca como copia del preset por defecto (Intermedio) para no quedar vacío.
    for (const AgentProfile &preset : AgentProfile::systemPresets())
        if (preset.id == AgentProfile::defaultPresetId()) {
            p.enabledTools = preset.enabledTools;
            p.directives = preset.directives;
            p.approvalMode = preset.approvalMode;
            p.thinking = preset.thinking;
            break;
        }
    m_agentProfiles.add(p);
    save();
    return p.id;
}

bool ProfileManager::removeAgentProfile(const QString &id)
{
    if (m_agentProfiles.findById(id).system) {
        emit errorOccurred(QStringLiteral("Perfil de agente de sistema: solo lectura (duplicalo para editar)."));
        return false;
    }
    bool ok = m_agentProfiles.remove(id);
    if (ok) save();
    return ok;
}

bool ProfileManager::updateAgentProfile(const QVariantMap &data)
{
    AgentProfile p = m_agentProfiles.findById(data["id"].toString());
    if (p.id.isEmpty()) return false;
    if (p.system) {
        emit errorOccurred(QStringLiteral("Perfil de agente de sistema: solo lectura (duplicalo para editar)."));
        return false;
    }
    if (data.contains("name"))         p.name = data.value("name").toString();
    if (data.contains("enabledTools")) p.enabledTools = data.value("enabledTools").toStringList();
    if (data.contains("directives"))   p.directives = data.value("directives").toStringList();
    if (data.contains("approvalMode")) p.approvalMode = data.value("approvalMode").toString();
    if (data.contains("thinking"))     p.thinking = data.value("thinking").toBool();
    if (data.contains("temperature"))  p.temperature = data.value("temperature").toDouble();
    if (data.contains("systemExtra"))  p.systemExtra = data.value("systemExtra").toString();
    if (data.contains("personalityProfileIds")) p.personalityProfileIds = data.value("personalityProfileIds").toStringList();
    if (data.contains("styleProfileIds")) p.styleProfileIds = data.value("styleProfileIds").toStringList();
    if (data.contains("injectStyleExamples")) p.injectStyleExamples = data.value("injectStyleExamples").toBool();
    if (data.contains("styleExampleLimit")) p.styleExampleLimit = qBound(0, data.value("styleExampleLimit").toInt(), 8);
    // El límite es un presupuesto explícito del perfil. No imponer un mínimo
    // de 500 caracteres: perfiles compactos (por ejemplo 120) son válidos y
    // evitan inflar el prompt cuando sólo se necesita una pista breve.
    if (data.contains("styleContextLimit"))
        p.styleContextLimit = qBound(0, data.value("styleContextLimit").toInt(), 20000);
    if (data.contains("mcpEnabled"))   p.mcpEnabled = data.value("mcpEnabled").toBool();
    if (data.contains("thinkingLeakGuard"))
        p.thinkingLeakGuard = data.value("thinkingLeakGuard").toBool();
    if (data.contains("progressCredits"))
        p.progressCredits = qMax(2, data.value("progressCredits").toInt());
    if (data.contains("progressMaxCredits"))
        p.progressMaxCredits = qMax(p.progressCredits, data.value("progressMaxCredits").toInt());
    if (data.contains("progressReplanAfter"))
        p.progressReplanAfter = qMax(2, data.value("progressReplanAfter").toInt());
    if (data.contains("progressStopAfter"))
        p.progressStopAfter = qMax(2, data.value("progressStopAfter").toInt());
    if (data.contains("quickToolTimeoutSec"))
        p.quickToolTimeoutSec = qBound(5, data.value("quickToolTimeoutSec").toInt(), 120);
    // Harness modular: el editor puede mandar el spec entero y/o el `extends`.
    if (data.contains("extends")) {
        p.extendsId = data.value("extends").toString();
        p.spec.extends = p.extendsId;
        if (!p.extendsId.isEmpty()) p.hasSpec = true;
    }
    if (data.contains("spec")) {
        const HarnessSpec spec =
            HarnessSpec::fromJson(QJsonObject::fromVariantMap(data.value("spec").toMap()));
        p.spec = spec;
        p.hasSpec = !spec.isEmpty();
        if (!spec.extends.isEmpty()) p.extendsId = spec.extends;
        else p.spec.extends = p.extendsId;
    }
    bool ok = m_agentProfiles.update(p);
    if (ok) save();
    return ok;
}

QString ProfileManager::duplicateAgentProfile(const QString &id)
{
    const AgentProfile src = m_agentProfiles.findById(id);
    if (src.id.isEmpty()) return {};
    AgentProfile p = src;
    p.id = AgentProfile::generateId();
    p.system = false;
    p.name = src.name + QStringLiteral(" (copia)");
    m_agentProfiles.add(p);
    save();
    return p.id;
}

bool ProfileManager::isSystemAgentProfile(const QString &id) const
{
    return m_agentProfiles.findById(id).system;
}

QVariantMap ProfileManager::getAgentProfile(const QString &id) const
{
    const auto p = m_agentProfiles.findById(id);
    if (p.id.isEmpty()) return {};
    return agentProfileToVariant(p);
}

AgentProfile ProfileManager::resolveAgentProfile(const QString &id) const
{
    return m_agentProfiles.findById(id);
}

// Resuelve la CADENA de herencia de un perfil: se sube por `extends` hasta la
// raíz y después se aplica de padre a hijo, así el hijo siempre pisa. Un ciclo
// (A extends B extends A) corta en el primer repetido: un perfil mal armado
// degrada, no cuelga el arranque.
HarnessSpec ProfileManager::resolveHarnessSpec(const AgentProfile &profile) const
{
    QList<AgentProfile> chain;
    QSet<QString> seen;
    AgentProfile cur = profile;
    while (true) {
        if (!cur.id.isEmpty()) {
            if (seen.contains(cur.id)) break;      // ciclo
            seen.insert(cur.id);
        }
        chain.prepend(cur);
        const QString parentId = cur.toSpec().extends;
        if (parentId.isEmpty() || chain.size() > 16) break;
        const AgentProfile parent = m_agentProfiles.findById(parentId);
        if (parent.id.isEmpty()) break;            // padre inexistente: degradar
        cur = parent;
    }

    HarnessSpec resolved;
    for (const AgentProfile &p : chain)
        resolved = HarnessSpec::resolve(resolved, p.toSpec());
    resolved.extends = profile.toSpec().extends;
    return resolved;
}

HarnessSpec ProfileManager::resolveHarnessSpecById(const QString &id) const
{
    return resolveHarnessSpec(m_agentProfiles.findById(id));
}

QVariantList ProfileManager::agentProfileDiff(const QString &id) const
{
    const AgentProfile p = m_agentProfiles.findById(id);
    if (p.id.isEmpty()) return {};
    const QString parentId = p.toSpec().extends;
    HarnessSpec base;                              // sin padre: contra defaults
    if (!parentId.isEmpty()) {
        const AgentProfile parent = m_agentProfiles.findById(parentId);
        if (!parent.id.isEmpty()) base = resolveHarnessSpec(parent);
    }
    return resolveHarnessSpec(p).diff(base);
}

QVariantMap ProfileManager::agentProfileSpec(const QString &id) const
{
    const AgentProfile p = m_agentProfiles.findById(id);
    if (p.id.isEmpty()) return {};
    return resolveHarnessSpec(p).toJson().toVariantMap();
}

// Guarda un spec declarado sobre un perfil de usuario. Espeja los campos legacy
// para que un binario anterior lea el perfil sin romperse.
bool ProfileManager::setAgentProfileSpec(const QString &id, const QVariantMap &specJson)
{
    AgentProfile p = m_agentProfiles.findById(id);
    if (p.id.isEmpty() || p.system) return false;
    const HarnessSpec spec = HarnessSpec::fromJson(QJsonObject::fromVariantMap(specJson));
    p.spec = spec;
    p.hasSpec = !spec.isEmpty();
    p.extendsId = spec.extends;
    p.applySpecToLegacyFields(resolveHarnessSpec(p));
    const bool ok = m_agentProfiles.update(p);
    if (ok) save();
    return ok;
}

QVariantList ProfileManager::harnessPackCatalog() const
{
    return HarnessTools::packCatalog();
}

QVariantList ProfileManager::harnessDirectiveCatalog(const QString &workspace) const
{
    return HarnessDirectiveStore::list(workspace);
}

QVariantList ProfileManager::eligibleParents(const QString &id) const
{
    // Descendientes de `id`: si A hereda de B, B no puede heredar de A. Se
    // recorre hacia arriba desde cada candidato en vez de construir el árbol:
    // son pocos perfiles y así no hay que mantener un índice de hijos.
    auto descendsFrom = [this](QString candidate, const QString &ancestor) {
        QSet<QString> seen;
        while (!candidate.isEmpty() && !seen.contains(candidate)) {
            if (candidate == ancestor) return true;
            seen.insert(candidate);
            candidate = m_agentProfiles.findById(candidate).toSpec().extends;
        }
        return false;
    };

    QVariantList out;
    out.append(QVariantMap{{QStringLiteral("profileId"), QString()},
                           {QStringLiteral("name"), QStringLiteral("(defaults del harness)")}});
    for (const AgentProfile &p : m_agentProfiles.items()) {
        if (p.id == id) continue;                       // no heredarse a sí mismo
        if (!id.isEmpty() && descendsFrom(p.id, id)) continue;   // ni de un hijo
        out.append(QVariantMap{{QStringLiteral("profileId"), p.id},
                               {QStringLiteral("name"), p.name}});
    }
    return out;
}

// Resumen para el editor: tools resueltas, costo aproximado en tokens y
// advertencias de dependencias. Es lo que convierte "personalizar" en una
// decisión informada en vez de una adivinanza.
QVariantMap ProfileManager::harnessSpecSummary(const QString &id, const QString &workspace,
                                               const QVariantMap &env) const
{
    const AgentProfile p = m_agentProfiles.findById(id);
    if (p.id.isEmpty()) return {};
    const HarnessSpec spec = resolveHarnessSpec(p);
    const QStringList tools = HarnessTools::resolve(spec.tools);
    HarnessTools::Environment environment;
    environment.hasGit = !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();
    auto envFlag = [&env](const char *key, bool def) {
        const QVariant v = env.value(QString::fromLatin1(key));
        return v.isValid() ? v.toBool() : def;
    };
    environment.hasEmbeddings = envFlag("hasEmbeddings", true);
    environment.hasDesktop = envFlag("hasDesktop", true);
    environment.hasMailAccount = envFlag("hasMailAccount", true);
    environment.hasMcpServers = envFlag("hasMcpServers", true);
    environment.hasBrowser = envFlag("hasBrowser", true);
    QStringList warnings = HarnessTools::dependencyWarnings(tools, environment,
                                                            spec.tools.mcpToolsEnabled);
    // Tamaño del prompt propio del perfil: instrucciones extra + cuerpos de las
    // directivas .md. Es la otra mitad del presupuesto de contexto (la primera
    // son los schemas de tools); mostrar sólo una era media foto.
    int promptChars = spec.prompt.systemExtra.size();
    for (const QString &slug : spec.prompt.custom) {
        const QVariantMap d = HarnessDirectiveStore::load(slug, workspace);
        if (!d.value(QStringLiteral("ok")).toBool()) {
            warnings << QStringLiteral("directiva '%1': %2")
                            .arg(slug, d.value(QStringLiteral("error")).toString());
            continue;
        }
        promptChars += d.value(QStringLiteral("body")).toString().size();
    }
    if (spec.prompt.maxChars > 0 && promptChars > spec.prompt.maxChars)
        warnings << QStringLiteral("prompt: %1 chars supera el tope del perfil (%2). Sacá alguna "
                                   "directiva o subí el tope.")
                        .arg(promptChars).arg(spec.prompt.maxChars);
    const QVariantList discoveredSkills = PortableSkillStore::list(workspace);
    QVariantList enabledSkills;
    for (const QVariant &value : discoveredSkills) {
        const QVariantMap skill = value.toMap();
        if (HarnessSkills::allows(spec.skills, skill.value(QStringLiteral("name")).toString()))
            enabledSkills.append(skill);
    }
    return QVariantMap{
        {QStringLiteral("tools"), tools},
        {QStringLiteral("toolCount"), tools.size()},
        {QStringLiteral("approxTokens"), HarnessTools::approxTokens(tools)},
        {QStringLiteral("skills"), enabledSkills},
        {QStringLiteral("skillCount"), enabledSkills.size()},
        {QStringLiteral("skillPolicyDeclared"), spec.skills.set},
        {QStringLiteral("promptChars"), promptChars},
        {QStringLiteral("warnings"), warnings},
        {QStringLiteral("extends"), spec.extends},
        {QStringLiteral("phases"), QStringList(spec.phases.keys())}};
}

QString ProfileManager::renderPersonaStyleContext(const AgentProfile &profile) const
{
    return renderPersonaStyleContext(profile, QString());
}

QString ProfileManager::renderPersonaStyleContext(const AgentProfile &profile,
                                                  const QString &query) const
{
    QString out;
    int remaining = qMax(0, profile.styleContextLimit);
    QStringList queryWords;
    const QRegularExpression tokenPattern(QStringLiteral("[\\p{L}\\p{N}]+"));
    auto tokenIt = tokenPattern.globalMatch(query.toLower());
    while (tokenIt.hasNext()) {
        const QString token = tokenIt.next().captured();
        if (!token.isEmpty()) queryWords.append(token);
    }
    auto append = [&](const PersonaStyleProfile &p, const QString &heading) {
        if (!p.enabled || p.id.isEmpty() || remaining <= 0) return;
        QString block = QStringLiteral("\n\n--- %1: %2 ---\n%3")
            .arg(heading, p.name, p.styleCard.trimmed());
        if (!p.description.trimmed().isEmpty())
            block += QStringLiteral("\nDescripción: ") + p.description.trimmed();
        if (profile.injectStyleExamples && p.kind == QLatin1String("writing-style")) {
            QList<QPair<int, QString>> ranked;
            for (const QString &example : p.examples) {
                int score = 0;
                const QString lower = example.toLower();
                for (const QString &word : queryWords)
                    if (word.size() >= 3) score += lower.count(word);
                ranked.append({score, example});
            }
            std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
                return a.first > b.first;
            });
            int count = 0;
            for (const auto &rankedExample : ranked) {
                if (count++ >= qMin(profile.styleExampleLimit, p.maxExamples)) break;
                const QString &example = rankedExample.second;
                if (example.trimmed().isEmpty()) continue;
                block += QStringLiteral("\nEjemplo de referencia (no copiar literalmente):\n")
                         + example.left(p.maxChars);
            }
        }
        block = block.left(remaining);
        out += block;
        remaining -= block.size();
    };
    for (const QString &id : profile.personalityProfileIds)
        append(m_personaStyles.findById(id), QStringLiteral("PERSONALIDAD DEL USUARIO"));
    for (const QString &id : profile.styleProfileIds)
        append(m_personaStyles.findById(id), QStringLiteral("ESTILO DEL USUARIO"));
    if (!out.isEmpty()) {
        const QString footer = QStringLiteral(
            "\nNo copies ejemplos ni inventes rasgos; preservá significado e intención.\n");
        out += footer;
        // El límite de contexto incluye el margen fijo del footer. Antes se
        // sumaba el footer completo, que podía superar el presupuesto y hacía
        // que perfiles pequeños rompieran el contrato de tamaño del prompt.
        if (profile.styleContextLimit > 0)
            out.truncate(qMin(out.size(), profile.styleContextLimit + 100));
    }
    return out;
}

static QVariantMap personaStyleToVariant(const PersonaStyleProfile &p)
{
    return {{"id", p.id}, {"profileId", p.id}, {"name", p.name}, {"system", p.system},
            {"kind", p.kind}, {"description", p.description},
            {"styleCard", p.styleCard}, {"examples", p.examples},
            {"enabled", p.enabled}, {"maxExamples", p.maxExamples},
            {"maxChars", p.maxChars}};
}

static QVariantList personaStyleList(const QList<PersonaStyleProfile> &items,
                                     const QString &kind)
{
    QVariantList out;
    out.append(QVariantMap{{"profileId", QString()}, {"name", QStringLiteral("Ninguno")},
                           {"system", false}, {"kind", kind}});
    for (const PersonaStyleProfile &p : items) {
        if (p.kind == kind) out.append(personaStyleToVariant(p));
    }
    return out;
}

QVariantList ProfileManager::personalityProfiles() const
{
    return personaStyleList(m_personaStyles.m_items, QStringLiteral("personality"));
}

QVariantList ProfileManager::writingStyleProfiles() const
{
    return personaStyleList(m_personaStyles.m_items, QStringLiteral("writing-style"));
}

QString ProfileManager::addPersonaStyleProfile(const QString &name, const QString &kind)
{
    PersonaStyleProfile p;
    p.id = PersonaStyleProfile::generateId();
    p.name = name.trimmed().isEmpty() ? QStringLiteral("Nuevo estilo") : name.trimmed();
    p.kind = kind == QLatin1String("personality") ? QStringLiteral("personality")
                                                    : QStringLiteral("writing-style");
    m_personaStyles.add(p);
    save();
    emit personaStylesChanged();
    return p.id;
}

bool ProfileManager::removePersonaStyleProfile(const QString &id)
{
    const PersonaStyleProfile p = m_personaStyles.findById(id);
    if (p.id.isEmpty() || p.system) return false;
    const bool ok = m_personaStyles.remove(id);
    if (ok) { save(); emit personaStylesChanged(); }
    return ok;
}

bool ProfileManager::updatePersonaStyleProfile(const QVariantMap &data)
{
    PersonaStyleProfile p = m_personaStyles.findById(data.value("id").toString());
    if (p.id.isEmpty() || p.system) return false;
    if (data.contains("name")) p.name = data.value("name").toString().trimmed();
    if (data.contains("kind")) p.kind = data.value("kind").toString() == QLatin1String("personality")
                                      ? QStringLiteral("personality") : QStringLiteral("writing-style");
    if (data.contains("description")) p.description = data.value("description").toString();
    if (data.contains("styleCard")) p.styleCard = data.value("styleCard").toString();
    if (data.contains("examples")) {
        p.examples.clear();
        for (const QString &e : data.value("examples").toStringList())
            if (!e.trimmed().isEmpty()) p.examples << e.left(20000);
    }
    if (data.contains("enabled")) p.enabled = data.value("enabled").toBool();
    if (data.contains("maxExamples")) p.maxExamples = qBound(0, data.value("maxExamples").toInt(), 8);
    if (data.contains("maxChars")) p.maxChars = qBound(500, data.value("maxChars").toInt(), 20000);
    const bool ok = m_personaStyles.update(p);
    if (ok) { save(); emit personaStylesChanged(); }
    return ok;
}

QVariantMap ProfileManager::getPersonaStyleProfile(const QString &id) const
{
    return personaStyleToVariant(m_personaStyles.findById(id));
}

QString ProfileManager::buildStyleAnalysisPrompt(const QString &sample, const QString &kind) const
{
    const QString mode = kind == QLatin1String("personality") ? QStringLiteral("personalidad conversacional")
                                                                 : QStringLiteral("estilo de escritura");
    return QStringLiteral("Analizá únicamente patrones observables de %1 en el texto siguiente. "
                          "No juzgues la calidad ni inventes rasgos. Devolvé una ficha breve "
                          "en español con tono, ritmo, longitud de frases, vocabulario, "
                          "preferencias y cosas a evitar. Preservá privacidad y no repitas el texto.\n\n"
                          "MUESTRA:\n%2").arg(mode, sample.left(20000));
}

QString ProfileManager::heuristicStyleCard(const QString &sample) const
{
    const QString s = sample.trimmed();
    if (s.isEmpty()) return {};
    const QStringList sentences = s.split(QRegularExpression(QStringLiteral("[.!?]+\\s*")), Qt::SkipEmptyParts);
    int words = s.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
    const double avg = sentences.isEmpty() ? 0.0 : double(words) / sentences.size();
    const QString length = avg < 12 ? QStringLiteral("frases cortas")
                          : avg < 24 ? QStringLiteral("frases medias, con variación")
                                     : QStringLiteral("frases largas y elaboradas");
    const bool questions = s.contains(QLatin1Char('?'));
    const bool exclamations = s.contains(QLatin1Char('!'));
    return QStringLiteral("Longitud: %1. Promedio aproximado: %2 palabras por oración. "
                          "Preguntas: %3. Exclamaciones: %4. Usá esta ficha como hipótesis "
                          "editable y preservá significado e intención.")
        .arg(length).arg(QString::number(avg, 'f', 1))
        .arg(questions ? QStringLiteral("presente") : QStringLiteral("escaso"))
        .arg(exclamations ? QStringLiteral("presente") : QStringLiteral("escaso"));
}

bool ProfileManager::applyPersonaStyleAnalysis(const QString &id, const QString &response,
                                                const QString &sample)
{
    PersonaStyleProfile p = m_personaStyles.findById(id);
    if (p.id.isEmpty() || p.system) return false;
    QString json = response.trimmed();
    const int begin = json.indexOf(QLatin1Char('{'));
    const int end = json.lastIndexOf(QLatin1Char('}'));
    if (begin < 0 || end <= begin) return false;
    json = json.mid(begin, end - begin + 1);
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject o = doc.object();
    if (o.contains(QStringLiteral("description"))) p.description = o.value("description").toString().left(2000);
    if (o.contains(QStringLiteral("styleCard"))) {
        const QJsonValue card = o.value("styleCard");
        p.styleCard = card.isObject()
            ? QString::fromUtf8(QJsonDocument(card.toObject()).toJson(QJsonDocument::Compact))
            : card.toString();
    }
    if (p.styleCard.trimmed().isEmpty()) {
        QStringList parts;
        for (const QString &key : {QStringLiteral("tone"), QStringLiteral("rhythm"),
                                   QStringLiteral("sentenceLength"), QStringLiteral("preferredPatterns"),
                                   QStringLiteral("avoid")}) {
            if (!o.contains(key)) continue;
            const QJsonValue value = o.value(key);
            QString text = value.toVariant().toString();
            if (value.isObject()) text = QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
            else if (value.isArray()) text = QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
            parts << key + QStringLiteral(": ") + text;
        }
        p.styleCard = parts.join(QStringLiteral("\n"));
    }
    p.examples.clear();
    for (const QJsonValue &v : o.value(QStringLiteral("examples")).toArray())
        if (v.isString() && !v.toString().trimmed().isEmpty()) p.examples << v.toString().left(20000);
    if (p.examples.isEmpty() && !sample.trimmed().isEmpty()) p.examples << sample.left(20000);
    if (p.styleCard.trimmed().isEmpty()) return false;
    const bool ok = m_personaStyles.update(p);
    if (ok) { save(); emit personaStylesChanged(); }
    return ok;
}

QString ProfileManager::exportPersonaStyleProfile(const QString &id) const
{
    const PersonaStyleProfile p = m_personaStyles.findById(id);
    return p.id.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(p.toJson()).toJson(QJsonDocument::Indented));
}

QString ProfileManager::importPersonaStyleProfile(const QString &json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return {};
    PersonaStyleProfile p = PersonaStyleProfile::fromJson(doc.object());
    if (p.name.trimmed().isEmpty()) return {};
    p.id = PersonaStyleProfile::generateId(); p.system = false;
    m_personaStyles.add(p); save(); emit personaStylesChanged(); return p.id;
}

QString ProfileManager::previewPersonaStylePrompt(const QString &agentProfileId,
                                                   const QString &query) const
{
    const AgentProfile p = m_agentProfiles.findById(agentProfileId);
    return p.id.isEmpty() ? QString() : renderPersonaStyleContext(p, query);
}

// ---- Resolvers ----

BackendProfile   ProfileManager::resolveBackend(const QString &id)    const { return m_backends.findById(id); }
ModelProfile     ProfileManager::resolveModelProfile(const QString &id) const { return m_models.findById(id); }
RuntimePreset    ProfileManager::resolveRuntime(const QString &id)     const { return m_runtimes.findById(id); }
HarnessProfile   ProfileManager::resolveHarness(const QString &id)     const { return m_harnesses.findById(id); }
WorkspaceProfile ProfileManager::resolveWorkspace(const QString &id)   const { return m_workspaces.findById(id); }
LaunchProfile    ProfileManager::resolveLaunch(const QString &id)      const { return m_launches.findById(id); }

// ---- Perfiles de sistema (bundled) ----

bool ProfileManager::isSystemLaunch(const QString &id) const
{
    return m_launches.findById(id).system;
}

void ProfileManager::loadSystemProfiles()
{
    auto stripSystem = [](auto &model) {
        std::decay_t<decltype(model.m_items)> userItems;
        for (const auto &item : model.m_items) {
            if (!item.system)
                userItems.append(item);
        }
        model.setItems(userItems);
    };
    stripSystem(m_backends);
    stripSystem(m_models);
    stripSystem(m_runtimes);
    stripSystem(m_launches);
    stripSystem(m_agentProfiles);
    stripSystem(m_personaStyles);

    // Perfiles de agente de sistema (Básico/Intermedio/Avanzado/Máximo): definidos
    // en código (AgentProfile::systemPresets), inmutables, no se persisten. Se
    // anteponen a los de usuario en cada arranque.
    {
        QList<AgentProfile> items = AgentProfile::systemPresets();
        items.append(m_agentProfiles.m_items);
        m_agentProfiles.setItems(items);
    }

    // Fuente: env override (tests) o el recurso qrc empaquetado.
    QByteArray raw;
    const QByteArray envPath = qgetenv("LLAMACODE_SYSTEM_PROFILES");
    QString src = !envPath.isEmpty() ? QString::fromLocal8Bit(envPath)
                                     : QStringLiteral(":/assets/system_profiles.json");
    QFile f(src);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[ProfileManager] no pude abrir system_profiles:" << src;
        return;
    }
    raw = f.readAll();
    const QJsonArray arr = expandSystemProfileVariants(QJsonDocument::fromJson(raw).array());
    if (arr.isEmpty()) return;

    // modelId DETERMINISTA por ruta (igual que GGUFScanner.cpp): liga el perfil al
    // gguf cuando exista en <AppLocalData>/models/<folder>/<file>.
    static const QUuid kNs(QStringLiteral("a1b2c3d4-e5f6-4a5b-8c7d-0e1f2a3b4c5d"));
    // Dir de modelos gestionado. Override por env LLAMACODE_MODELS_DIR (reusar una
    // librería existente sin re-descargar); DEBE coincidir con AppController::
    // modelDownloadDir() para que el id det ligue tras el scan.
    const QByteArray envModels = qgetenv("LLAMACODE_MODELS_DIR");
    const QString modelsDir = !envModels.isEmpty()
        ? QString::fromLocal8Bit(envModels)
        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
              + QStringLiteral("/models");
    auto detId = [](const QString &path) {
        return QUuid::createUuidV5(kNs, path.toUtf8()).toString(QUuid::WithoutBraces);
    };

    QList<BackendProfile> sysBe;
    QList<ModelProfile>   sysModel;
    QList<RuntimePreset>  sysRt;
    QList<LaunchProfile>  sysLaunch;

    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const QString id = o.value("id").toString();
        if (id.isEmpty()) continue;
        const QJsonObject backendConfig = o.value(QStringLiteral("backend")).toObject();
        const bool cloudBackend = backendConfig.value(QStringLiteral("kind")).toString()
                                      .compare(QStringLiteral("cloud"), Qt::CaseInsensitive) == 0;
        const QString folder = o.value("folder").toString();
        const QJsonObject mo = o.value("model").toObject();
        const QString file = mo.value("file").toString();
        // Descarga por subcarpeta (modelDownloadDir/<folder>/<file>): evita colisión
        // de nombres genéricos (p.ej. mmproj-F16.gguf de varios repos). El id det
        // se computa con esa misma ruta para ligar tras el scan recursivo.
        const QString subdir = folder.isEmpty() ? QString() : (folder + "/");
        const QString modelPath = modelsDir + "/" + subdir + file;

        const QJsonObject ro = o.value("runtime").toObject();
        RuntimePreset rt;
        rt.id = QStringLiteral("sysrt-") + id; rt.system = true;
        rt.name = o.value("displayName").toString() + QStringLiteral(" · rt");
        rt.ctx = ro.value("ctx").toInt(4096);
        rt.batch = ro.value("batch").toInt(512);
        rt.ubatch = ro.value("ubatch").toInt(512);
        rt.threads = ro.value("threads").toInt(-1);
        rt.gpuLayers = ro.value("gpuLayers").toInt(-1);
        rt.flashAttention = ro.value("flashAttn").toBool(true);
        rt.mmap = ro.value("mmap").toBool(true);
        rt.mlock = ro.value("mlock").toBool(false);
        rt.contBatching = true;
        rt.cacheType = ro.value("kv").toString(QStringLiteral("q8_0"));
        if (cloudBackend)
            rt.ctx = backendConfig.value(QStringLiteral("ctx")).toInt(rt.ctx);
        if (!cloudBackend)
            sysRt.append(rt);

        ModelProfile mp;
        mp.id = QStringLiteral("sysmodel-") + id; mp.system = true;
        mp.name = o.value("displayName").toString() + QStringLiteral(" · model");
        mp.modelId = detId(modelPath);
        const QString mmFile = mo.value("mmprojFile").toString();
        if (!mmFile.isEmpty())
            mp.mmprojId = detId(modelsDir + "/" + subdir + mmFile);
        // Draft model (speculative decoding, p.ej. DFlash de Gemma): subcarpeta
        // propia. Las opciones de speculative también pueden describir un cabezal
        // MTP autocontenido, por eso se leen aunque no haya draftModel externo.
        const QJsonObject spec = o.value("spec").toObject();
        mp.specType = spec.value("type").toString();
        mp.specDraftNgl = spec.value("draftNgl").toString();
        mp.specDraftNMax = spec.value("draftNMax").toInt(0);
        mp.specDraftNMin = qMax(0, spec.value("draftNMin").toInt(0));
        mp.specDraftAdaptive = spec.value("adaptive").toBool(false);
        mp.specDraftConfMin = qBound(0.0, spec.value("confMin").toDouble(0.0), 1.0);
        const QJsonObject draft = o.value("draftModel").toObject();
        if (!draft.isEmpty()) {
            const QString dFolder = draft.value("folder").toString();
            const QString dSub = dFolder.isEmpty() ? QString() : (dFolder + "/");
            mp.draftModelId = detId(modelsDir + "/" + dSub + draft.value("file").toString());
        }
        if (cloudBackend) {
            // External vLLM profiles carry model identity in BackendProfile. Do
            // not create a phantom local GGUF reference or ask the downloader to
            // resolve an empty path.
            mp.modelId.clear();
            mp.mmprojId.clear();
            mp.draftModelId.clear();
            mp.specType.clear();
            mp.specDraftNMax = 0;
            mp.specDraftNMin = 0;
            mp.specDraftAdaptive = false;
            mp.specDraftConfMin = 0.0;
            mp.specDraftNgl.clear();
        } else {
            sysModel.append(mp);
        }

        BackendProfile be;
        be.id = QStringLiteral("sysbe-") + id; be.system = true;
        be.name = o.value("displayName").toString() + QStringLiteral(" · backend");
        be.binaryId = QString();   // resuelto al construir el comando (AppController)
        be.host = cloudBackend
            ? backendConfig.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1"))
            : QStringLiteral("127.0.0.1");
        be.port = cloudBackend
            ? backendConfig.value(QStringLiteral("port")).toInt(0)
            : 8021;
        if (cloudBackend) {
            be.kind = QStringLiteral("cloud");
            be.cloudBaseUrl = backendConfig.value(QStringLiteral("baseUrl"))
                                  .toString(backendConfig.value(QStringLiteral("cloudBaseUrl"))
                                                .toString()).trimmed();
            be.cloudKeyRef = backendConfig.value(QStringLiteral("keyRef"))
                                  .toString(backendConfig.value(QStringLiteral("cloudKeyRef"))
                                                .toString()).trimmed();
            be.cloudModel = backendConfig.value(QStringLiteral("model"))
                                  .toString(backendConfig.value(QStringLiteral("cloudModel"))
                                                .toString()).trimmed();
            be.cloudCtx = backendConfig.value(QStringLiteral("ctx")).toInt(
                ro.value(QStringLiteral("ctx")).toInt(0));
        }
        sysBe.append(be);

        LaunchProfile lp;
        lp.id = id; lp.system = true;
        lp.name = o.value("displayName").toString();
        // Sólo los perfiles base para usuarios nuevos llevan el distintivo
        // visual de sistema. `system` sigue siendo la bandera interna de
        // inmutabilidad para todos los perfiles bundled.
        static const QSet<QString> baseSystemIds = {
            QStringLiteral("sys-maxq"), QStringLiteral("sys-maxctx"),
            QStringLiteral("sys-fastgemma"), QStringLiteral("sys-laguna-s-2-1-q2"),
            QStringLiteral("sys-vram-20"), QStringLiteral("sys-vram-16"),
            QStringLiteral("sys-vram-12-moe"), QStringLiteral("sys-vram-8-gemma"),
            QStringLiteral("sys-vram-8-qwen-agent"), QStringLiteral("sys-vram-4"),
            QStringLiteral("sys-vram-4-gemma"), QStringLiteral("sys-vram-2"),
            QStringLiteral("sys-vram-2-gemma"), QStringLiteral("sys-vram-0")
        };
        lp.systemBadge = baseSystemIds.contains(id);
        // Alias = solo la VRAM (ej "12GB"), sin sufijos MoE/Gemma/Qwen.
        lp.alias = QString::number(o.value("minVramGb").toInt()) + QStringLiteral("GB");
        // Las insignias pertenecen al catalogo: asi un perfil medido puede marcarse
        // sin recompilar esta clase ni propagar el estado a variantes hermanas.
        lp.favorite = o.value(QStringLiteral("favorite")).toBool(false);
        lp.best = o.value(QStringLiteral("best")).toBool(false);
        lp.benchmark = o.value(QStringLiteral("benchmark")).toBool(false);
        lp.backendProfileId = be.id;
        lp.modelProfileId = cloudBackend ? QString() : mp.id;
        lp.runtimePresetId = cloudBackend ? QString() : rt.id;
        lp.agentProfileId = o.value(QStringLiteral("agentProfileId")).toString();
        lp.reasoningEffort = o.value(QStringLiteral("reasoningEffort"))
                                 .toString().trimmed().toLower();
        if (lp.reasoningEffort != QLatin1String("low")
            && lp.reasoningEffort != QLatin1String("medium")
            && lp.reasoningEffort != QLatin1String("high")
            && lp.reasoningEffort != QLatin1String("xhigh")
            && lp.reasoningEffort != QLatin1String("max"))
            lp.reasoningEffort.clear();
        lp.reasoningBudget = qMax(-1, o.value(QStringLiteral("reasoningBudget"))
                                      .toInt(-1));
        lp.plannerProfileId = o.value(QStringLiteral("plannerProfileId")).toString();
        lp.hybridMode = o.value(QStringLiteral("hybridMode")).toString(QStringLiteral("off"));
        QStringList extra;
        for (const QJsonValue &a : o.value("extraArgs").toArray()) extra << a.toString();
        const QJsonObject mtp = o.value("mtp").toObject();
        if (mtp.value("enabled").toBool())
            for (const QJsonValue &a : mtp.value("args").toArray()) extra << a.toString();
        // Chat-template bundleado (ej fix de tool-calling de Gemma4): extraer del qrc
        // a una ruta estable y pasar --chat-template-file con esa ruta.
        const QString tpl = o.value("chatTemplate").toString();
        if (!tpl.isEmpty()) {
            const QString dstDir =
                QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                + QStringLiteral("/chat-templates");
            QDir().mkpath(dstDir);
            const QString dst = dstDir + "/" + tpl;
            QFile src(QStringLiteral(":/assets/chat-templates/") + tpl);
            if (src.open(QIODevice::ReadOnly)) {
                // La copia vive en AppData y sobrevive a las actualizaciones del
                // ejecutable. Compararla evita que un template corregido quede
                // oculto detrás de una versión vieja instalada previamente.
                const QByteArray bundled = src.readAll();
                QFile installed(dst);
                const bool stale = !installed.exists()
                                || !installed.open(QIODevice::ReadOnly)
                                || installed.readAll() != bundled;
                if (stale) {
                    QFile out(dst);
                    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
                        out.write(bundled);
                }
            }
            if (QFile::exists(dst)) extra << QStringLiteral("--chat-template-file") << dst;
        }
        lp.extraArgs = extra;
        const QJsonObject env = o.value("env").toObject();
        for (auto it = env.begin(); it != env.end(); ++it)
            lp.envOverrides.insert(it.key(), it.value().toString());
        sysLaunch.append(lp);
    }

    // Anteponer (sistema primero) sin pisar lo de usuario.
    auto prepend = [](auto &model, const auto &sys) {
        auto items = sys;            // copia (system primero)
        items.append(model.m_items); // luego usuario
        model.setItems(items);
    };
    prepend(m_backends, sysBe);
    prepend(m_models,   sysModel);
    prepend(m_runtimes, sysRt);
    prepend(m_launches, sysLaunch);
}

QString ProfileManager::duplicateLaunchProfile(const QString &id)
{
    const LaunchProfile src = m_launches.findById(id);
    if (src.id.isEmpty()) return {};

    // Clonar backing a entradas nuevas EDITABLES (system=false, ids frescos).
    BackendProfile be = m_backends.findById(src.backendProfileId);
    if (!src.backendProfileId.isEmpty() && be.id.isEmpty()) return {};
    be.id = BackendProfile::generateId(); be.system = false;
    if (!src.backendProfileId.isEmpty()) m_backends.add(be);

    ModelProfile mp = m_models.findById(src.modelProfileId);
    if (!src.modelProfileId.isEmpty() && mp.id.isEmpty()) return {};
    mp.id = ModelProfile::generateId(); mp.system = false;
    if (!src.modelProfileId.isEmpty()) m_models.add(mp);

    RuntimePreset rt = m_runtimes.findById(src.runtimePresetId);
    if (!src.runtimePresetId.isEmpty() && rt.id.isEmpty()) return {};
    rt.id = RuntimePreset::generateId(); rt.system = false;
    if (!src.runtimePresetId.isEmpty()) m_runtimes.add(rt);

    LaunchProfile lp = src;
    lp.id = LaunchProfile::generateId();
        lp.system = false;
        lp.systemBadge = false;
    lp.favorite = false;
    lp.best = false;
    lp.deprecated = false;
    if (!src.backendProfileId.isEmpty()) lp.backendProfileId = be.id;
    if (!src.modelProfileId.isEmpty()) lp.modelProfileId = mp.id;
    if (!src.runtimePresetId.isEmpty()) lp.runtimePresetId = rt.id;
    int maxSeq = 0;
    for (const auto &x : m_launches.m_items) maxSeq = std::max(maxSeq, seqOf(x.name));
    lp.name = QStringLiteral("%1_%2 (copia)").arg(maxSeq + 1).arg(stripSeq(src.name));
    lp.alias = src.alias.isEmpty() ? QString() : src.alias + QStringLiteral("-copia");
    m_launches.add(lp);

    save();
    emit launchesChanged();
    return lp.id;
}

// ---- Persistence ----

void ProfileManager::load()
{
    bool loadFailed = false;
    bool agentProfilesDeduplicated = false;
    auto loadList = [&loadFailed](const QString &path, auto &model, auto fromJsonFn) {
        QFile f(path);
        if (!f.exists()) return;                 // first run / never saved — fine
        if (!f.open(QIODevice::ReadOnly)) {
            // File exists but couldn't be read (e.g. locked by another instance).
            // Treat as a load failure so we never overwrite it with an empty list.
            qWarning() << "[ProfileManager::load] could not open existing file"
                       << path << f.errorString();
            loadFailed = true;
            return;
        }
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        QList<std::decay_t<decltype(model.m_items[0])>> items;
        for (const auto &v : arr)
            items.append(fromJsonFn(v.toObject()));
        model.setItems(items);
    };

    loadList(storagePath("backends"),   m_backends,   &BackendProfile::fromJson);
    loadList(storagePath("models"),     m_models,     &ModelProfile::fromJson);
    loadList(storagePath("runtimes"),   m_runtimes,   &RuntimePreset::fromJson);
    loadList(storagePath("harnesses"),  m_harnesses,  &HarnessProfile::fromJson);
    loadList(storagePath("workspaces"), m_workspaces, &WorkspaceProfile::fromJson);
    loadList(storagePath("launches"),   m_launches,   &LaunchProfile::fromJson);
    loadList(storagePath("agent_profiles"), m_agentProfiles, &AgentProfile::fromJson);
    loadList(storagePath("persona_styles"), m_personaStyles, &PersonaStyleProfile::fromJson);

    // Una ejecución de pruebas o una importación repetida puede dejar varias
    // copias exactas del mismo perfil con IDs nuevos. Conservamos la primera:
    // perfiles con el mismo nombre pero distinta configuración siguen siendo
    // válidos y no deben ocultarse entre sí.
    {
        QList<AgentProfile> unique;
        QSet<QByteArray> fingerprints;
        for (const AgentProfile &profile : m_agentProfiles.m_items) {
            QJsonObject comparable = profile.toJson();
            comparable.remove(QStringLiteral("id"));
            const QByteArray fingerprint = QJsonDocument(comparable).toJson(QJsonDocument::Compact);
            if (fingerprints.contains(fingerprint)) continue;
            fingerprints.insert(fingerprint);
            unique.append(profile);
        }
        if (unique.size() != m_agentProfiles.m_items.size()) {
            m_agentProfiles.setItems(unique);
            agentProfilesDeduplicated = true;
        }
    }

    // Importa la insignia BEST para perfiles curados que ya existían antes de
    // introducir el campo. No modifica nombres ni perfiles que no coincidan.
    for (LaunchProfile &profile : m_launches.m_items)
        if (!profile.best && isCuratedBestName(profile))
            profile.best = true;

    // Only allow persistence once we've loaded cleanly. If any existing file
    // failed to load, block all saves so a partial/empty in-memory state can
    // never wipe the user's profiles on disk.
    m_persistAllowed = !loadFailed;
    if (loadFailed)
        qWarning() << "[ProfileManager] load incomplete — saving DISABLED to protect existing data";
    else if (agentProfilesDeduplicated)
        save();
}

void ProfileManager::save() const
{
    if (!m_persistAllowed) {
        qWarning() << "[ProfileManager::save] skipped — persistence disabled after a failed load";
        return;
    }

    // Marca que los próximos cambios de archivo son nuestros, para no auto-recargar.
    m_saving = true;

    auto saveList = [](const QString &path, const auto &items) {
        // Anti-wipe guard: never overwrite a non-empty file with an empty list.
        if (items.isEmpty()) {
            QFile existing(path);
            if (existing.exists() && existing.open(QIODevice::ReadOnly)) {
                const QJsonArray cur = QJsonDocument::fromJson(existing.readAll()).array();
                existing.close();
                if (!cur.isEmpty()) {
                    qWarning() << "[ProfileManager::save] refusing to wipe non-empty file with empty list:"
                               << path;
                    return;
                }
            }
        }

        QJsonArray arr;
        for (const auto &item : items) arr.append(item.toJson());
        const QByteArray data = QJsonDocument(arr).toJson();
        QDir().mkpath(QFileInfo(path).absolutePath());

        // Rolling backup of the current on-disk file BEFORE overwriting, so any
        // bad/lossy write is always recoverable. Keep the last N per entity.
        QFileInfo fi(path);
        if (fi.exists()) {
            const QString base = fi.completeBaseName();               // e.g. "launches"
            const QString bdir = fi.absolutePath() + "/.backups";
            QDir().mkpath(bdir);
            const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
            QFile::copy(path, bdir + "/" + base + "." + stamp + ".json");
            // Prune to the most recent 15 backups per entity (sorted by name == time).
            QDir d(bdir);
            QStringList olds = d.entryList(QStringList{base + ".*.json"}, QDir::Files, QDir::Name);
            while (olds.size() > 15)
                QFile::remove(bdir + "/" + olds.takeFirst());
            // Warn loudly on a large count drop (possible accidental loss).
            QFile prev(path);
            if (prev.open(QIODevice::ReadOnly)) {
                const int curCount = QJsonDocument::fromJson(prev.readAll()).array().size();
                prev.close();
                if (curCount > 5 && arr.size() < curCount / 2)
                    qWarning() << "[ProfileManager::save] LARGE COUNT DROP" << path
                               << curCount << "->" << arr.size()
                               << "(backup kept in" << bdir << ")";
            }
        }

        // Atomic write: write to a temp file, then rename over the target so a
        // crash mid-write can never leave a truncated/corrupt profiles file.
        const QString tmp = path + ".tmp";
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "[ProfileManager::save] FAILED to open temp" << tmp << f.errorString();
            return;
        }
        const qint64 written = f.write(data);
        f.flush();
        f.close();
        if (written != data.size()) {
            qWarning() << "[ProfileManager::save] short write, aborting rename" << tmp;
            QFile::remove(tmp);
            return;
        }
        QFile::remove(path);                 // Windows: rename requires absent target
        if (!QFile::rename(tmp, path))
            qWarning() << "[ProfileManager::save] FAILED to rename" << tmp << "->" << path;
    };

    // Filtrar perfiles de SISTEMA: no se persisten (se reconstruyen del bundle).
    auto userOnly = [](const auto &items) {
        std::decay_t<decltype(items)> out;
        for (const auto &x : items) if (!x.system) out.append(x);
        return out;
    };
    saveList(storagePath("backends"),   userOnly(m_backends.m_items));
    saveList(storagePath("models"),     userOnly(m_models.m_items));
    saveList(storagePath("runtimes"),   userOnly(m_runtimes.m_items));
    saveList(storagePath("harnesses"),  userOnly(m_harnesses.m_items));
    saveList(storagePath("workspaces"), userOnly(m_workspaces.m_items));
    saveList(storagePath("launches"),   userOnly(m_launches.m_items));
    saveList(storagePath("agent_profiles"), userOnly(m_agentProfiles.m_items));
    saveList(storagePath("persona_styles"), userOnly(m_personaStyles.m_items));

    // Historial append-only: un snapshot por perfil presente en cada save.
    // Es deliberadamente JSONL para poder inspeccionarlo y recuperarlo aunque
    // una escritura posterior quede incompleta.
    const QString historyPath = QFileInfo(storagePath(QStringLiteral("backends"))).absolutePath()
                                + QStringLiteral("/profile_history.jsonl");
    QFile history(historyPath);
    if (history.open(QIODevice::WriteOnly | QIODevice::Append)) {
        const double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
        auto append = [&](const QString &entity, const auto &items) {
            for (const auto &item : items) {
                if (item.system) continue;
                QJsonObject row{{QStringLiteral("timestamp"), now},
                                {QStringLiteral("entity"), entity},
                                {QStringLiteral("id"), item.id},
                                {QStringLiteral("snapshot"), item.toJson()}};
                history.write(QJsonDocument(row).toJson(QJsonDocument::Compact));
                history.write("\n");
            }
        };
        append(QStringLiteral("backend"), m_backends.m_items);
        append(QStringLiteral("model"), m_models.m_items);
        append(QStringLiteral("runtime"), m_runtimes.m_items);
        append(QStringLiteral("harness"), m_harnesses.m_items);
        append(QStringLiteral("workspace"), m_workspaces.m_items);
        append(QStringLiteral("launch"), m_launches.m_items);
        append(QStringLiteral("agent"), m_agentProfiles.m_items);
        append(QStringLiteral("personaStyle"), m_personaStyles.m_items);
        history.close();
    }

    // La escritura atómica (rename) hace que el watcher pierda los paths: re-armarlos.
    for (const QString &ent : {QStringLiteral("backends"), QStringLiteral("models"),
                               QStringLiteral("runtimes"), QStringLiteral("harnesses"),
                               QStringLiteral("workspaces"), QStringLiteral("launches"),
                               QStringLiteral("agent_profiles"), QStringLiteral("persona_styles")}) {
        const QString p = storagePath(ent);
        if (QFile::exists(p) && !m_watcher.files().contains(p)) m_watcher.addPath(p);
    }
    // Limpiar el flag tras drenar los eventos fileChanged de nuestro propio save().
    QTimer::singleShot(400, const_cast<ProfileManager*>(this), [this]() { m_saving = false; });
}

QString ProfileManager::exportProfilesBundle() const
{
    QJsonObject root;
    root[QStringLiteral("schemaVersion")] = 1;
    auto addUser = [&root](const QString &key, const auto &items) {
        QJsonArray out;
        for (const auto &item : items)
            if (!item.system) out.append(item.toJson());
        root[key] = out;
    };
    addUser(QStringLiteral("backends"), m_backends.m_items);
    addUser(QStringLiteral("models"), m_models.m_items);
    addUser(QStringLiteral("runtimes"), m_runtimes.m_items);
    addUser(QStringLiteral("harnesses"), m_harnesses.m_items);
    addUser(QStringLiteral("workspaces"), m_workspaces.m_items);
    addUser(QStringLiteral("launches"), m_launches.m_items);
    addUser(QStringLiteral("agentProfiles"), m_agentProfiles.m_items);
    addUser(QStringLiteral("personaStyles"), m_personaStyles.m_items);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QVariantList ProfileManager::profileChangeHistory(const QString &entity,
                                                    const QString &id,
                                                    int limit) const
{
    QVariantList out;
    if (entity.trimmed().isEmpty() || id.trimmed().isEmpty() || limit <= 0) return out;
    const QString path = QFileInfo(storagePath(QStringLiteral("backends"))).absolutePath()
                         + QStringLiteral("/profile_history.jsonl");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return out;
    QList<QVariantMap> matches;
    while (!f.atEnd()) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readLine(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) continue;
        const QJsonObject row = doc.object();
        if (row.value(QStringLiteral("entity")).toString() != entity
            || row.value(QStringLiteral("id")).toString() != id) continue;
        matches.append(row.toVariantMap());
    }
    const int begin = qMax(0, matches.size() - limit);
    for (int i = matches.size() - 1; i >= begin; --i) out.append(matches.at(i));
    return out;
}

int ProfileManager::importProfilesBundle(const QString &json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return -1;
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != 1) return -1;

    int imported = 0;
    auto hasId = [](const auto &items, const QString &id) {
        for (const auto &item : items) if (item.id == id) return true;
        return false;
    };
    auto importList = [&](const QString &key, auto &model, auto fromJson) {
        const QJsonValue value = root.value(key);
        if (!value.isArray()) return;
        for (const QJsonValue &entry : value.toArray()) {
            if (!entry.isObject()) continue;
            const auto item = fromJson(entry.toObject());
            if (item.id.isEmpty() || item.system || hasId(model.m_items, item.id)) continue;
            model.add(item);
            ++imported;
        }
    };
    importList(QStringLiteral("backends"), m_backends, &BackendProfile::fromJson);
    importList(QStringLiteral("models"), m_models, &ModelProfile::fromJson);
    importList(QStringLiteral("runtimes"), m_runtimes, &RuntimePreset::fromJson);
    importList(QStringLiteral("harnesses"), m_harnesses, &HarnessProfile::fromJson);
    importList(QStringLiteral("workspaces"), m_workspaces, &WorkspaceProfile::fromJson);
    importList(QStringLiteral("launches"), m_launches, &LaunchProfile::fromJson);
    importList(QStringLiteral("agentProfiles"), m_agentProfiles, &AgentProfile::fromJson);
    importList(QStringLiteral("personaStyles"), m_personaStyles, &PersonaStyleProfile::fromJson);
    if (imported > 0) {
        save();
        emit launchesChanged();
        emit personaStylesChanged();
    }
    return imported;
}

QVariantList ProfileManager::profileTemplates() const
{
    QFile f(storagePath(QStringLiteral("profile_templates")));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QVariantList out;
    for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).array())
        if (v.isObject()) out.append(v.toObject().toVariantMap());
    return out;
}

QString ProfileManager::saveLaunchAsTemplate(const QString &launchId,
                                             const QString &templateName)
{
    const LaunchProfile p = m_launches.findById(launchId);
    const QString name = templateName.trimmed();
    if (p.id.isEmpty() || p.system || name.isEmpty()) return {};

    QJsonArray arr;
    QFile f(storagePath(QStringLiteral("profile_templates")));
    if (f.open(QIODevice::ReadOnly)) arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject env;
    for (auto it = p.envOverrides.cbegin(); it != p.envOverrides.cend(); ++it) env[it.key()] = it.value();
    QJsonObject row{{"id", id}, {"name", name}, {"createdAt", QDateTime::currentMSecsSinceEpoch()},
                    {"sourceLaunchId", p.id}, {"alias", p.alias}, {"tags", QJsonArray::fromStringList(p.tags)},
                    {"backendProfileId", p.backendProfileId}, {"modelProfileId", p.modelProfileId},
                    {"runtimePresetId", p.runtimePresetId}, {"harnessProfileId", p.harnessProfileId},
                    {"workspaceProfileId", p.workspaceProfileId}, {"agentProfileId", p.agentProfileId},
                    {"plannerProfileId", p.plannerProfileId}, {"hybridMode", p.hybridMode},
                    {"extraArgs", QJsonArray::fromStringList(p.extraArgs)},
                    {"envOverrides", env}, {"browserAutomation", p.browserAutomation},
                    {"master", p.master.toJson()}};
    arr.append(row);
    QDir().mkpath(QFileInfo(f.fileName()).absolutePath());
    QFile out(f.fileName() + QStringLiteral(".tmp"));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    const QByteArray bytes = QJsonDocument(arr).toJson(QJsonDocument::Indented);
    if (out.write(bytes) != bytes.size()) { out.close(); QFile::remove(out.fileName()); return {}; }
    out.close();
    QFile::remove(f.fileName());
    if (!QFile::rename(out.fileName(), f.fileName())) return {};
    return id;
}

QString ProfileManager::createLaunchFromTemplate(const QString &templateId,
                                                 const QString &name)
{
    QVariantMap selected;
    for (const QVariant &v : profileTemplates())
        if (v.toMap().value(QStringLiteral("id")).toString() == templateId) { selected = v.toMap(); break; }
    if (selected.isEmpty()) return {};
    const QString id = addLaunchProfile(name.trimmed().isEmpty()
                                        ? selected.value(QStringLiteral("name")).toString()
                                        : name,
                                        selected.value(QStringLiteral("backendProfileId")).toString(),
                                        selected.value(QStringLiteral("modelProfileId")).toString(),
                                        selected.value(QStringLiteral("runtimePresetId")).toString());
    if (id.isEmpty()) return {};
    QVariantMap data = selected;
    data[QStringLiteral("id")] = id;
    data[QStringLiteral("name")] = name.trimmed().isEmpty()
        ? selected.value(QStringLiteral("name")).toString() : name;
    updateLaunchProfile(data);
    return id;
}

bool ProfileManager::removeProfileTemplate(const QString &templateId)
{
    QFile f(storagePath(QStringLiteral("profile_templates")));
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();
    const int before = arr.size();
    QJsonArray kept;
    for (const QJsonValue &v : arr)
        if (v.toObject().value(QStringLiteral("id")).toString() != templateId) kept.append(v);
    if (kept.size() == before) return false;
    QFile out(f.fileName() + QStringLiteral(".tmp"));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray bytes = QJsonDocument(kept).toJson(QJsonDocument::Indented);
    if (out.write(bytes) != bytes.size()) { out.close(); QFile::remove(out.fileName()); return false; }
    out.close(); QFile::remove(f.fileName());
    return QFile::rename(out.fileName(), f.fileName());
}

QString ProfileManager::rutaPerfilesLegacy()
{
    // Hasta esta versión la ruta estaba escrita a mano y apuntaba a la carpeta
    // del usuario del desarrollador original. En otras máquinas la app creaba
    // igual ese árbol —C:\Users\cristian\…— y ahí terminaban los perfiles del
    // usuario real. Se conserva sólo para migrar de ahí una vez.
    return QStringLiteral("C:/Users/cristian/Documents/LlamaCode/profiles");
}

int ProfileManager::migrarPerfiles(const QString &destino, const QString &origen)
{
    // Misma lista que load()/save(): si se agrega una entidad alla, va aca.
    // profile_templates no es una lista de perfiles pero vive en la misma
    // carpeta y lo escribe el usuario, asi que tambien se migra.
    static const QStringList entidades{
        QStringLiteral("backends"), QStringLiteral("models"),
        QStringLiteral("runtimes"), QStringLiteral("harnesses"),
        QStringLiteral("workspaces"), QStringLiteral("launches"),
        QStringLiteral("agent_profiles"), QStringLiteral("persona_styles"),
        QStringLiteral("profile_templates")};

    const QDir dirOrigen(origen);
    if (!dirOrigen.exists() || QDir::cleanPath(origen) == QDir::cleanPath(destino))
        return 0;

    // Si el destino ya tiene perfiles, no se toca nada: lo del usuario manda.
    const QDir dirDestino(destino);
    for (const QString &e : entidades) {
        if (dirDestino.exists(e + QStringLiteral(".json")))
            return 0;
    }

    int copiados = 0;
    for (const QString &e : entidades) {
        const QString archivo = e + QStringLiteral(".json");
        if (!dirOrigen.exists(archivo))
            continue;
        if (!QDir().mkpath(destino))
            return copiados;
        if (QFile::copy(dirOrigen.filePath(archivo), dirDestino.filePath(archivo)))
            ++copiados;
    }
    return copiados;
}

QString ProfileManager::profilesRoot()
{
    // Se resuelve una sola vez por proceso (los tests setean
    // LLAMACODE_PROFILES_DIR en initTestCase, antes del primer ProfileManager).
    static const QString root = []() {
        const QByteArray env = qgetenv("LLAMACODE_PROFILES_DIR");
        if (!env.isEmpty())
            return QString::fromLocal8Bit(env);

        // Bajo test, NUNCA los Documentos del usuario.
        //
        // setTestModeEnabled() redirige AppData y AppLocalData, pero NO
        // DocumentsLocation, que es donde viven los perfiles. Un test que se
        // olvide de setear LLAMACODE_PROFILES_DIR escribe perfiles de verdad en
        // la máquina de quien corre la suite. Ya pasó: un caso de
        // test_appcontroller creaba un perfil de lanzamiento por corrida y
        // llegaron a acumularse 29 en el equipo de desarrollo.
        //
        // Que cada test se acuerde no alcanza como garantía; esto lo hace
        // imposible.
        if (QStandardPaths::isTestModeEnabled()) {
            return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                   + QStringLiteral("/profiles");
        }

        // Documentos del usuario ACTUAL. Antes esto estaba fijo a la carpeta de
        // otro usuario, lo que obligaba a permisos de administrador para editar
        // los perfiles propios.
        const QString actual =
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/LlamaCode/profiles");
        const int migrados = migrarPerfiles(actual, rutaPerfilesLegacy());
        if (migrados > 0) {
            qInfo() << "[ProfileManager] perfiles migrados desde"
                    << rutaPerfilesLegacy() << "->" << actual << "(" << migrados << "archivos)";
        }
        return actual;
    }();
    return root;
}

QString ProfileManager::storagePath(const QString &entity) const
{
    return profilesRoot() + "/" + entity + ".json";
}
