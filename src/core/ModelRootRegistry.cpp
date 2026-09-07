#include "ModelRootRegistry.h"
#include "OllamaImporter.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>

ModelRootRegistry::ModelRootRegistry(ModelCatalog *catalog, QObject *parent)
    : QAbstractListModel(parent)
    , m_catalog(catalog)
    , m_scanner(new GGUFScanner(this))
{
    load();

}

int ModelRootRegistry::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant ModelRootRegistry::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size()) return {};

    const ModelRoot &r = m_items.at(index.row());
    switch (role) {
    case IdRole:       return r.id;
    case PathRole:     return r.path;
    case LabelRole:    return r.label;
    case ScanModeRole: return r.scanMode;
    case KindRole:     return r.kind;
    case EnabledRole:  return r.enabled;
    case PriorityRole: return r.priority;
    case TagsRole:     return r.tags;
    case IsOnlineRole: return r.isOnline;
    default:           return {};
    }
}

QHash<int, QByteArray> ModelRootRegistry::roleNames() const
{
    return {
        {IdRole,       "rootId"},
        {PathRole,     "path"},
        {LabelRole,    "label"},
        {ScanModeRole, "scanMode"},
        {KindRole,     "kind"},
        {EnabledRole,  "enabled"},
        {PriorityRole, "priority"},
        {TagsRole,     "tags"},
        {IsOnlineRole, "isOnline"},
    };
}

QString ModelRootRegistry::add(const QString &path, const QString &label,
                                const QString &scanMode, const QStringList &tags)
{
    ModelRoot r;
    r.id = ModelRoot::generateId();

    // Scheme "ollama://[dir]": ingesta de blobs ya descargados por Ollama. Se
    // resuelve a un directorio de store físico y se marca kind="ollama" para que
    // el scanner lea los blobs vía manifests en vez de globbear *.gguf.
    const QString ollamaDir = OllamaImporter::resolveStoreDir(path);
    if (!ollamaDir.isEmpty()) {
        r.kind = "ollama";
        r.path = ollamaDir;
        r.label = label.isEmpty() ? QStringLiteral("Ollama") : label;
    } else {
        r.path = path;
        r.label = label.isEmpty() ? QFileInfo(path).fileName() : label;
    }
    r.scanMode = scanMode.isEmpty() ? "manual" : scanMode;
    r.tags = tags;
    r.isOnline = QFileInfo::exists(r.path);
    r.enabled = true;
    r.priority = m_items.size();

    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.append(r);
    endInsertRows();
    emit countChanged();
    save();

    // Always scan a freshly added root once so its models show up immediately.
    // The scanMode only governs subsequent (startup/watch) rescans.
    if (r.enabled && r.isOnline)
        doScan(r);

    return r.id;
}

bool ModelRootRegistry::remove(const QString &id)
{
    const int idx = indexOfId(id);
    if (idx < 0) return false;

    m_catalog->removeByRootId(id);

    beginRemoveRows({}, idx, idx);
    m_items.removeAt(idx);
    endRemoveRows();
    emit countChanged();
    save();
    return true;
}

bool ModelRootRegistry::update(const QString &id, const QString &label,
                                const QString &scanMode, bool enabled,
                                int priority, const QStringList &tags)
{
    const int idx = indexOfId(id);
    if (idx < 0) return false;

    ModelRoot &r = m_items[idx];
    r.label = label;
    r.scanMode = scanMode;
    r.enabled = enabled;
    r.priority = priority;
    r.tags = tags;
    r.isOnline = QFileInfo::exists(r.path);

    if (!enabled)
        m_catalog->markRootUnavailable(id);

    const QModelIndex mi = index(idx);
    emit dataChanged(mi, mi);
    save();
    return true;
}

void ModelRootRegistry::scan(const QString &id)
{
    const int idx = indexOfId(id);
    if (idx < 0) return;
    doScan(m_items.at(idx));
}

void ModelRootRegistry::scanAll()
{
    for (const auto &r : m_items)
        if (r.enabled) doScan(r);
}

void ModelRootRegistry::scanStartupRoots()
{
    for (const auto &r : m_items)
        if (r.enabled && r.scanMode == QLatin1String("startup"))
            doScan(r);
}

void ModelRootRegistry::refresh()
{
    for (int i = 0; i < m_items.size(); ++i) {
        bool wasOnline = m_items[i].isOnline;
        m_items[i].isOnline = QFileInfo::exists(m_items[i].path);
        if (wasOnline != m_items[i].isOnline) {
            if (!m_items[i].isOnline)
                m_catalog->markRootUnavailable(m_items[i].id);
            const QModelIndex mi = index(i);
            emit dataChanged(mi, mi, {IsOnlineRole});
        }
    }
}

QVariantMap ModelRootRegistry::get(const QString &id) const
{
    const int idx = indexOfId(id);
    if (idx < 0) return {};
    const ModelRoot &r = m_items.at(idx);
    return {
        {"id", r.id}, {"path", r.path}, {"label", r.label},
        {"scanMode", r.scanMode}, {"kind", r.kind}, {"enabled", r.enabled},
        {"priority", r.priority}, {"tags", r.tags}, {"isOnline", r.isOnline}
    };
}

ModelRoot ModelRootRegistry::findById(const QString &id) const
{
    const int idx = indexOfId(id);
    if (idx < 0) return {};
    return m_items.at(idx);
}

void ModelRootRegistry::load()
{
    QFile f(storagePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const auto &v : arr)
        m_items.append(ModelRoot::fromJson(v.toObject()));
}

void ModelRootRegistry::save() const
{
    QJsonArray arr;
    for (const auto &r : m_items)
        arr.append(r.toJson());
    QDir().mkpath(QFileInfo(storagePath()).absolutePath());
    QFile f(storagePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson());
}

QString ModelRootRegistry::storagePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/model_roots.json";
}

int ModelRootRegistry::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].id == id) return i;
    return -1;
}

void ModelRootRegistry::doScan(const ModelRoot &root)
{
    if (!root.isOnline && !QFileInfo::exists(root.path)) {
        m_catalog->markRootUnavailable(root.id);
        return;
    }

    const QString rootId = root.id;
    const ModelRoot rootCopy = root;
    ModelCatalog *catalog = m_catalog;
    const QList<CatalogModel> cached = catalog->allForRoot(root.id);
    QPointer<ModelRootRegistry> self(this);

    m_scanning = true;
    emit scanningChanged();
    emit scanStarted(rootId);

    (void)QtConcurrent::run([self, rootCopy, rootId, catalog, cached]() {
        GGUFScanner scanner;
        QList<CatalogModel> found = scanner.scan(rootCopy, cached);

        if (!self)
            return;

        QMetaObject::invokeMethod(self.data(), [self, rootId, found, catalog]() {
            if (!self)
                return;
            catalog->addBatch(found);
            // Lo que el scan ya no vio en disco deja de estar disponible: si no,
            // un modelo borrado o movido sigue apareciendo en la UI y en los
            // perfiles que lo referencian.
            QSet<QString> present;
            present.reserve(found.size());
            for (const CatalogModel &m : found) present.insert(m.id);
            catalog->reconcileRoot(rootId, present);
            self->m_scanning = false;
            emit self->scanningChanged();
            emit self->scanFinished(rootId, found.size());
        }, Qt::QueuedConnection);
    });
}
