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
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, DataRole };

    explicit ProfileListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &p = {}) const override {
        return p.isValid() ? 0 : m_items.size();
    }
    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override {
        if (!idx.isValid() || idx.row() >= m_items.size()) return {};
        switch (role) {
        case IdRole:   return m_items[idx.row()].id;
        case NameRole: return m_items[idx.row()].name;
        default:       return {};
        }
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{IdRole, "profileId"}, {NameRole, "name"}};
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

public:
    explicit ProfileManager(QObject *parent = nullptr);

    QAbstractListModel *backendProfiles()   { return &m_backends; }
    QAbstractListModel *modelProfiles()     { return &m_models; }
    QAbstractListModel *runtimePresets()    { return &m_runtimes; }
    QAbstractListModel *harnessProfiles()   { return &m_harnesses; }
    QAbstractListModel *workspaceProfiles() { return &m_workspaces; }
    QAbstractListModel *launchProfiles()    { return &m_launches; }

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
    Q_INVOKABLE QVariantMap getModelProfile(const QString &id) const;
    // Config de speculative decoding / MTP del ModelProfile (separado para no
    // romper las firmas de add/update). Vacío/0 = no emitir.
    Q_INVOKABLE bool setModelSpec(const QString &id, const QString &specType,
                                  int specDraftNMax, const QString &specDraftNgl,
                                  const QString &specDraftTypeK,
                                  const QString &specDraftTypeV);

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
    Q_INVOKABLE QVariantMap getLaunchProfile(const QString &id) const;
    // Config de Charla (voz) por LaunchProfile. get devuelve defaults si no hay.
    Q_INVOKABLE QVariantMap getLaunchVoice(const QString &id) const;
    Q_INVOKABLE bool setLaunchVoice(const QString &id, const QVariantMap &voiceCfg);
    // Alias opcional (prioridad sobre name en la UI) y favorito (estrella, arriba).
    Q_INVOKABLE void setLaunchFavorite(const QString &id, bool favorite);
    Q_INVOKABLE void setLaunchAlias(const QString &id, const QString &alias);
    // Perfiles ordenados para dropdowns: favoritos primero, displayName=alias - name.
    Q_INVOKABLE QVariantList launchProfilesForMenu() const;
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

private:
    void load();
    void save() const;
    QString storagePath(const QString &entity) const;
    void setupWatcher();
    void onProfileFileChanged(const QString &path);

    ProfileListModel<BackendProfile>   m_backends;
    ProfileListModel<ModelProfile>     m_models;
    ProfileListModel<RuntimePreset>    m_runtimes;
    ProfileListModel<HarnessProfile>   m_harnesses;
    ProfileListModel<WorkspaceProfile> m_workspaces;
    ProfileListModel<LaunchProfile>    m_launches;

    // Set false when load() can't read an existing file (e.g. locked by another
    // instance); blocks save() so a partial/empty state can't wipe stored data.
    mutable bool m_persistAllowed = true;

    // Detecta ediciones externas del archivo de perfiles para recargar y evitar
    // que una instancia con estado viejo pise cambios hechos por fuera/otra instancia.
    mutable QFileSystemWatcher m_watcher;
    mutable bool m_saving = false;   // ignora los cambios provocados por nuestro propio save()
};
