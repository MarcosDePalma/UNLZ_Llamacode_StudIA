#pragma once
#include "ProfileTypes.h"
#include <QObject>
#include <QAbstractListModel>
#include <QList>
#include <QFileSystemWatcher>

// Generic list model for a profile type
template <typename T>
class ProfileListModel : public QAbstractListModel
{
public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, DataRole, SystemRole };

    explicit ProfileListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : m_items.size();
    }
    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override {
        if (!idx.isValid() || idx.row() >= m_items.size()) return {};
        switch (role) {
        case IdRole:     return m_items[idx.row()].id;
        case NameRole:   return m_items[idx.row()].name;
        case SystemRole: return m_items[idx.row()].system;
        default:         return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{IdRole, "profileId"}, {NameRole, "name"}, {SystemRole, "system"}};
    }

    void setItems(const QList<T> &items) {
        beginResetModel(); m_items = items; endResetModel();
    }
    const QList<T> &items() const { return m_items; }
    T findById(const QString &id) const {
        for (const auto &x : m_items) if (x.id == id) return x;
        return T{};
    }
    int indexOfId(const QString &id) const {
        for (int i = 0; i < m_items.size(); ++i) if (m_items[i].id == id) return i;
        return -1;
    }

    void add(const T &item) {
        beginInsertRows({}, m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
    }
    bool remove(const QString &id) {
        int idx = indexOfId(id);
        if (idx < 0) return false;
        beginRemoveRows({}, idx, idx);
        m_items.removeAt(idx);
        endRemoveRows();
        return true;
    }
    bool update(const T &item) {
        int idx = indexOfId(item.id);
        if (idx < 0) return false;
        m_items[idx] = item;
        const auto mi = index(idx);
        emit dataChanged(mi, mi);
        return true;
    }

    QList<T> m_items;
};

class ProfileManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel* backendProfiles READ backendProfiles CONSTANT)
    Q_PROPERTY(QAbstractListModel* modelProfiles   READ modelProfiles   CONSTANT)
    Q_PROPERTY(QAbstractListModel* runtimePresets  READ runtimePresets  CONSTANT)
    Q_PROPERTY(QAbstractListModel* harnessProfiles READ harnessProfiles CONSTANT)
    Q_PROPERTY(QAbstractListModel* workspaceProfiles READ workspaceProfiles CONSTANT)
    Q_PROPERTY(QAbstractListModel* launchProfiles  READ launchProfiles  CONSTANT)
    Q_PROPERTY(QAbstractListModel* agentProfiles   READ agentProfiles   CONSTANT)
    Q_PROPERTY(QAbstractListModel* personaStyleProfiles READ personaStyleProfiles CONSTANT)
    Q_PROPERTY(QVariantList personalityProfiles READ personalityProfiles NOTIFY personaStylesChanged)
    Q_PROPERTY(QVariantList writingStyleProfiles READ writingStyleProfiles NOTIFY personaStylesChanged)

public:
    explicit ProfileManager(QObject *parent = nullptr);

    QAbstractListModel *backendProfiles()   { return &m_backends; }
    QAbstractListModel *modelProfiles()     { return &m_models; }
    QAbstractListModel *runtimePresets()    { return &m_runtimes; }
    QAbstractListModel *harnessProfiles()   { return &m_harnesses; }
    QAbstractListModel *workspaceProfiles() { return &m_workspaces; }
    QAbstractListModel *launchProfiles()    { return &m_launches; }
    QAbstractListModel *agentProfiles()     { return &m_agentProfiles; }
    QAbstractListModel *personaStyleProfiles() { return &m_personaStyles; }
    QVariantList personalityProfiles() const;
    QVariantList writingStyleProfiles() const;

    // BackendProfile
    Q_INVOKABLE QString addBackend(const QString &name, const QString &binaryId,
                                   const QString &host, int port);
    Q_INVOKABLE bool removeBackend(const QString &id);
    Q_INVOKABLE bool updateBackend(const QString &id, const QString &name,
                                   const QString &binaryId, const QString &host,
                                   int port, const QStringList &baseArgs);
    Q_INVOKABLE bool updateBackendPort(const QString &id, int port);
    Q_INVOKABLE QVariantMap getBackend(const QString &id) const;
    // Config cloud (provider OpenAI-compat externo) de un backend existente.
    // kind: "local" (limpia cloud) | "cloud". keyRef = NOMBRE de la referencia al
    // secreto (nunca la key). Devuelve false si no existe el id.
    Q_INVOKABLE bool setBackendCloud(const QString &id, const QString &kind,
                                     const QString &baseUrl, const QString &keyRef,
                                     const QString &model, int ctx);

    // ModelProfile
    Q_INVOKABLE QString addModelProfile(const QString &name, const QString &modelId,
                                        const QString &mmprojId, const QString &draftId);
    Q_INVOKABLE bool removeModelProfile(const QString &id);
    Q_INVOKABLE bool updateModelProfile(const QString &id, const QString &name,
                                        const QString &modelId, const QString &mmprojId,
                                        const QString &draftId);
    // Reemplaza el ModelProfile entero, anclas incluidas. Lo usa el auto-sanado de
    // buildContext, que necesita persistir stable ids y no sólo los ids textuales.
    bool updateModelProfileFull(const ModelProfile &p);
    Q_INVOKABLE QVariantMap getModelProfile(const QString &id) const;
    // Config de speculative decoding / MTP del ModelProfile (separado para no
    // romper las firmas de add/update). Vacío/0 = no emitir.
    Q_INVOKABLE bool setModelSpec(const QString &id, const QString &specType,
                                  int specDraftNMax, const QString &specDraftNgl,
                                  const QString &specDraftTypeK,
                                  const QString &specDraftTypeV,
                                  double specDraftConfMin = 0.0,
                                  int specDraftNMin = 0,
                                  bool specDraftAdaptive = false);

    // RuntimePreset
    Q_INVOKABLE QString addRuntimePreset(const QString &name, int ctx, int batch,
                                         int gpuLayers, bool flashAttn, bool contBatch);
    Q_INVOKABLE bool removeRuntimePreset(const QString &id);
    Q_INVOKABLE bool updateRuntimePreset(const QVariantMap &data);
    Q_INVOKABLE QVariantMap getRuntimePreset(const QString &id) const;

    // HarnessProfile
    Q_INVOKABLE QString addHarness(const QString &name, const QString &adapter);
    Q_INVOKABLE bool removeHarness(const QString &id);
    Q_INVOKABLE bool updateHarness(const QVariantMap &data);
    Q_INVOKABLE QVariantMap getHarness(const QString &id) const;

    // LaunchProfile
    Q_INVOKABLE QString addLaunchProfile(const QString &name,
                                         const QString &backendId,
                                         const QString &modelId,
                                         const QString &runtimeId);
    Q_INVOKABLE bool removeLaunchProfile(const QString &id);
    Q_INVOKABLE bool updateLaunchProfile(const QVariantMap &data);
    // Duplica un launch (incl. de sistema) a una copia EDITABLE de usuario:
    // clona backend/model/runtime/workspace a entradas nuevas (system=false) e ids
    // frescos. Devuelve el id del nuevo launch, o "" si no existe el origen.
    Q_INVOKABLE QString duplicateLaunchProfile(const QString &id);
    // True si el launch es de sistema (solo lectura).
    Q_INVOKABLE bool isSystemLaunch(const QString &id) const;
    Q_INVOKABLE QVariantMap getLaunchProfile(const QString &id) const;
    // Config de Charla (voz) por LaunchProfile. get devuelve defaults si no hay.
    Q_INVOKABLE QVariantMap getLaunchVoice(const QString &id) const;
    Q_INVOKABLE bool setLaunchVoice(const QString &id, const QVariantMap &voiceCfg);
    // Alias opcional (prioridad sobre name en la UI), favorito (estrella, arriba)
    // y marca persistente de candidato pendiente para benchmark.
    Q_INVOKABLE void setLaunchFavorite(const QString &id, bool favorite);
    Q_INVOKABLE void setLaunchBenchmark(const QString &id, bool benchmark);
    Q_INVOKABLE void setLaunchAlias(const QString &id, const QString &alias);
    Q_INVOKABLE void setLaunchTags(const QString &id, const QStringList &tags);
    Q_INVOKABLE void markLaunchUsed(const QString &id);
    // Perfiles ordenados para dropdowns: favoritos primero, displayName=alias - name.
    Q_INVOKABLE QVariantList launchProfilesForMenu() const;
    // Lista ordenada para Perfiles. Si query no está vacío, filtra por nombre,
    // alias o id (case-insensitive), manteniendo el orden de favoritos/BEST.
    Q_INVOKABLE QVariantList launchProfilesForProfilesPage(const QString &query = QString()) const;
    // AgentProfile (perfiles de agente: capacidades + directivas + ajustes)
    Q_INVOKABLE QString addAgentProfile(const QString &name);
    Q_INVOKABLE bool removeAgentProfile(const QString &id);
    Q_INVOKABLE bool updateAgentProfile(const QVariantMap &data);
    // Duplica un perfil de agente (incl. de sistema) a una copia EDITABLE de usuario.
    Q_INVOKABLE QString duplicateAgentProfile(const QString &id);
    // Analiza una consigna sin llamar a otro modelo y propone un perfil especializado
    // sólo cuando difiere materialmente del activo. La aplicación crea una copia;
    // nunca modifica el perfil de origen.
    Q_INVOKABLE QVariantMap recommendAgentProfile(const QString &prompt,
                                                  const QString &currentProfileId) const;
    Q_INVOKABLE QString createRecommendedAgentProfile(const QString &sourceProfileId,
                                                       const QString &taskKind);
    Q_INVOKABLE bool isSystemAgentProfile(const QString &id) const;
    Q_INVOKABLE QVariantMap getAgentProfile(const QString &id) const;
    AgentProfile resolveAgentProfile(const QString &id) const;
    // Spec del harness con la cadena de `extends` ya aplicada (padre → hijo).
    HarnessSpec resolveHarnessSpec(const AgentProfile &profile) const;
    HarnessSpec resolveHarnessSpecById(const QString &id) const;
    // Para la UI/headless: {module, field, base, value} de lo que cambia este
    // perfil respecto de su `extends` (o de los defaults si no hereda).
    Q_INVOKABLE QVariantList agentProfileDiff(const QString &id) const;
    Q_INVOKABLE QVariantMap agentProfileSpec(const QString &id) const;
    Q_INVOKABLE bool setAgentProfileSpec(const QString &id, const QVariantMap &specJson);
    Q_INVOKABLE QVariantList harnessPackCatalog() const;
    Q_INVOKABLE QVariantList harnessDirectiveCatalog(const QString &workspace = QString()) const;
    // Candidatos válidos para `extends` de un perfil: excluye el propio id y su
    // subárbol (ofrecer un ciclo es ofrecer un error). Devuelve
    // [{profileId, name}], con una entrada vacía al tope = "sin herencia".
    Q_INVOKABLE QVariantList eligibleParents(const QString &id) const;
    // `env` permite al caller (AppController) informar lo que ProfileManager no
    // sabe: si hay embeddings, escritorio, cuentas de correo o browser. Vacío =
    // se asume disponible (sólo git se detecta acá).
    Q_INVOKABLE QVariantMap harnessSpecSummary(const QString &id,
                                               const QString &workspace = QString(),
                                               const QVariantMap &env = {}) const;
    QString renderPersonaStyleContext(const AgentProfile &profile) const;
    QString renderPersonaStyleContext(const AgentProfile &profile, const QString &query) const;

    // Perfiles reutilizables de personalidad/estilo. Se guardan separados de los
    // perfiles de agente para poder compartirlos entre modelos y backends.
    Q_INVOKABLE QString addPersonaStyleProfile(const QString &name, const QString &kind);
    Q_INVOKABLE bool removePersonaStyleProfile(const QString &id);
    Q_INVOKABLE bool updatePersonaStyleProfile(const QVariantMap &data);
    Q_INVOKABLE QVariantMap getPersonaStyleProfile(const QString &id) const;
    // Exporta/importa todos los perfiles de usuario como un bundle JSON. Los
    // ids se conservan para mantener referencias entre backend/model/runtime/
    // launch; los perfiles de sistema nunca salen ni entran.
    Q_INVOKABLE QString exportProfilesBundle() const;
    Q_INVOKABLE int importProfilesBundle(const QString &json);
    // Plantillas reutilizables de launch: guardan referencias y overrides, pero
    // no crean ni modifican perfiles hasta que se aplican explícitamente.
    Q_INVOKABLE QVariantList profileTemplates() const;
    Q_INVOKABLE QString saveLaunchAsTemplate(const QString &launchId,
                                             const QString &templateName);
    Q_INVOKABLE QString createLaunchFromTemplate(const QString &templateId,
                                                 const QString &name = QString());
    Q_INVOKABLE bool removeProfileTemplate(const QString &templateId);
    // Historial append-only de snapshots de perfiles de usuario. Es headless y
    // permite auditar/restaurar manualmente cambios sin exponer secretos.
    Q_INVOKABLE QVariantList profileChangeHistory(const QString &entity,
                                                   const QString &id,
                                                   int limit = 50) const;
    Q_INVOKABLE QString buildStyleAnalysisPrompt(const QString &sample,
                                                  const QString &kind) const;
    Q_INVOKABLE QString heuristicStyleCard(const QString &sample) const;
    Q_INVOKABLE bool applyPersonaStyleAnalysis(const QString &id, const QString &response,
                                               const QString &sample = QString());
    Q_INVOKABLE QString exportPersonaStyleProfile(const QString &id) const;
    Q_INVOKABLE QString importPersonaStyleProfile(const QString &json);
    Q_INVOKABLE QString previewPersonaStylePrompt(const QString &agentProfileId,
                                                   const QString &query = QString()) const;

    Q_INVOKABLE void saveProfiles() { save(); }
    void reloadFromDisk();

    // Resolve for builder
    BackendProfile resolveBackend(const QString &id) const;
    ModelProfile resolveModelProfile(const QString &id) const;
    RuntimePreset resolveRuntime(const QString &id) const;
    HarnessProfile resolveHarness(const QString &id) const;
    WorkspaceProfile resolveWorkspace(const QString &id) const;
    LaunchProfile resolveLaunch(const QString &id) const;

    // Carpeta donde viven los JSON de perfiles. Se resuelve una sola vez:
    //   1. LLAMACODE_PROFILES_DIR, si está seteada.
    //   2. Documentos del usuario ACTUAL (…/Documents/LlamaCode/profiles).
    // La primera vez migra los perfiles desde la ruta vieja, que estaba fija a
    // la carpeta del usuario del desarrollador original.
    static QString profilesRoot();

    // Copia los .json de perfiles de `origen` a `destino` si destino todavía no
    // tiene ninguno y origen sí. Devuelve cuántos archivos copió. Pública y
    // estática para poder testear la migración sin tocar el disco del usuario.
    static int migrarPerfiles(const QString &destino, const QString &origen);

    // Ruta que usaban las versiones anteriores. Sólo se lee, para migrar.
    static QString rutaPerfilesLegacy();

signals:
    void errorOccurred(const QString &message);
    // Emitida cuando los perfiles se recargaron por un cambio externo del archivo.
    void profilesReloaded();
    // Emitida cuando cambia la lista de launches (alta/baja/edición/alias/favorito)
    // para que los dropdowns reconstruyan launchProfilesForMenu().
    void launchesChanged();
    void personaStylesChanged();

private:
    void load();
    // Carga assets/system_profiles.json (qrc o env LLAMACODE_SYSTEM_PROFILES) y
    // antepone perfiles de sistema (system=true) a las listas en memoria. No se
    // persisten: se reconstruyen en cada arranque.
    void loadSystemProfiles();
    void save() const;
    QString storagePath(const QString &entity) const;
    void setupWatcher();
    void onProfileFileChanged(const QString &path);
    void onProfileDirectoryChanged(const QString &path);

    ProfileListModel<BackendProfile>   m_backends;
    ProfileListModel<ModelProfile>     m_models;
    ProfileListModel<RuntimePreset>    m_runtimes;
    ProfileListModel<HarnessProfile>   m_harnesses;
    ProfileListModel<WorkspaceProfile> m_workspaces;
    ProfileListModel<LaunchProfile>    m_launches;
    ProfileListModel<AgentProfile>     m_agentProfiles;
    ProfileListModel<PersonaStyleProfile> m_personaStyles;

    // Set false when load() can't read an existing file (e.g. locked by another
    // instance); blocks save() so a partial/empty state can't wipe stored data.
    mutable bool m_persistAllowed = true;

    // Detecta ediciones externas del archivo de perfiles para recargar y evitar
    // que una instancia con estado viejo pise cambios hechos por fuera/otra instancia.
    mutable QFileSystemWatcher m_watcher;
    mutable bool m_saving = false;   // ignora los cambios provocados por nuestro propio save()
};
