#include "BinaryRegistry.h"
#include "CapabilityDetector.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QtConcurrent/QtConcurrentRun>

BinaryRegistry::BinaryRegistry(QObject *parent)
    : QAbstractListModel(parent)
{
    load();
}

int BinaryRegistry::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant BinaryRegistry::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};

    const LlamaBinary &b = m_items.at(index.row());
    switch (role) {
    case IdRole:           return b.id;
    case NameRole:         return b.name;
    case PathRole:         return b.path;
    case FlavorRole:       return b.flavor;
    case BackendRole:      return b.backend;
    case VersionHintRole:  return b.versionHint;
    case PathValidRole:    return b.pathValid;
    case HasCapabilitiesRole: return !b.supportedFlags.isEmpty();
    case BinaryHashRole:   return b.binaryHash;
    case KvTypesRole:      return b.kvTypes;
    case DisplayLabelRole: {
        // Si hay otro binario con el mismo nombre, agregar la carpeta padre del
        // exe para distinguirlos (ej. "bin (cpu) · llama-mtp-b9274").
        bool dup = false;
        for (int i = 0; i < m_items.size(); ++i) {
            if (i != index.row() && m_items.at(i).name == b.name) { dup = true; break; }
        }
        if (!dup) return b.name;
        const QString parent = QFileInfo(b.path).absoluteDir().dirName();
        // Subir un nivel si la carpeta inmediata es genérica ("bin").
        QString tag = parent;
        if (tag.compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0) {
            const QString up = QFileInfo(QFileInfo(b.path).absolutePath()).dir().dirName();
            if (!up.isEmpty()) tag = up + QStringLiteral("/") + parent;
        }
        return tag.isEmpty() ? b.name : (b.name + QStringLiteral(" · ") + tag);
    }
    default:               return {};
    }
}

QHash<int, QByteArray> BinaryRegistry::roleNames() const
{
    return {
        {IdRole,              "binId"},
        {NameRole,            "name"},
        {PathRole,            "path"},
        {FlavorRole,          "flavor"},
        {BackendRole,         "backend"},
        {VersionHintRole,     "versionHint"},
        {PathValidRole,       "pathValid"},
        {HasCapabilitiesRole, "hasCapabilities"},
        {BinaryHashRole,      "binaryHash"},
        {KvTypesRole,         "kvTypes"},
        {DisplayLabelRole,    "displayLabel"},
    };
}

QString BinaryRegistry::add(const QString &path, const QString &name,
                             const QString &flavor, const QString &backend,
                             const QString &versionHint)
{
    LlamaBinary b;
    b.id = LlamaBinary::generateId();
    b.path = path;
    b.name = name.isEmpty() ? QFileInfo(path).fileName() : name;
    b.flavor = flavor.isEmpty() ? "official" : flavor;
    b.backend = backend.isEmpty() ? "cpu" : backend;
    b.versionHint = versionHint;
    b.pathValid = QFileInfo::exists(path);
    b.binaryHash = computeHash(path);

    beginInsertRows({}, m_items.size(), m_items.size());
    m_items.append(b);
    endInsertRows();
    emit countChanged();
    save();
    return b.id;
}

bool BinaryRegistry::remove(const QString &id)
{
    const int idx = indexOfId(id);
    if (idx < 0) return false;

    beginRemoveRows({}, idx, idx);
    m_items.removeAt(idx);
    endRemoveRows();
    emit countChanged();
    save();
    return true;
}

bool BinaryRegistry::update(const QString &id, const QString &name,
                             const QString &flavor, const QString &backend,
                             const QString &versionHint, const QString &workingDir)
{
    const int idx = indexOfId(id);
    if (idx < 0) return false;

    LlamaBinary &b = m_items[idx];
    b.name = name;
    b.flavor = flavor;
    b.backend = backend;
    b.versionHint = versionHint;
    b.workingDirectory = workingDir;
    b.pathValid = QFileInfo::exists(b.path);

    const QModelIndex mi = index(idx);
    emit dataChanged(mi, mi);
    save();
    return true;
}

QString BinaryRegistry::detectBackend(const QString &exePath)
{
    const QFileInfo fi(exePath);
    if (!fi.exists()) return QString();
    const QDir dir = fi.absoluteDir();
    // Lista de archivos en la carpeta del exe (DLLs en Windows, .so en Linux).
    const QStringList libs = dir.entryList(
        QStringList{ QStringLiteral("*.dll"), QStringLiteral("*.so*") },
        QDir::Files);
    const QString joined = libs.join(QLatin1Char('|')).toLower();
    // CUDA: ggml-cuda, cudart, cublas. Vulkan: ggml-vulkan, vulkan-1.
    if (joined.contains(QStringLiteral("cuda")) || joined.contains(QStringLiteral("cublas")))
        return QStringLiteral("cuda");
    if (joined.contains(QStringLiteral("vulkan")))
        return QStringLiteral("vulkan");
    // Pista por ruta como respaldo (ej. ...\cuda12.4\bin\...).
    const QString p = exePath.toLower();
    if (p.contains(QStringLiteral("cuda"))) return QStringLiteral("cuda");
    if (p.contains(QStringLiteral("vulkan"))) return QStringLiteral("vulkan");
    return QString();   // desconocido: no pisar lo que haya
}

void BinaryRegistry::detectCapabilities(const QString &id)
{
    const int idx = indexOfId(id);
    if (idx < 0) return;

    const QString path = m_items.at(idx).path;
    const QString binId = id;

    (void)QtConcurrent::run([this, path, binId, idx]() {
        DetectedCapabilities caps = CapabilityDetector::detect(path);

        QMetaObject::invokeMethod(this, [this, binId, idx, caps]() {
            const int i = indexOfId(binId);
            if (i < 0) return;

            LlamaBinary &b = m_items[i];
            b.supportedFlags = caps.flags;
            b.kvTypes = caps.kvTypes;
            b.flagAliases = caps.flagAliases;
            if (!caps.version.isEmpty())
                b.versionHint = caps.version;
            b.pathValid = QFileInfo::exists(b.path);
            // Auto-corregir backend mal etiquetado: si está en el default "cpu"
            // pero hay DLLs cuda/vulkan al lado, ascender. Nunca degradar.
            if (b.backend.isEmpty() || b.backend == QLatin1String("cpu")) {
                const QString det = detectBackend(b.path);
                if (!det.isEmpty() && det != QLatin1String("cpu")) {
                    b.backend = det;
                    if (b.name.endsWith(QStringLiteral("(cpu)")))
                        b.name = b.name.left(b.name.size() - 5).trimmed()
                                 + QStringLiteral(" (") + det + QStringLiteral(")");
                }
            }

            const QModelIndex mi = index(i);
            emit dataChanged(mi, mi);
            save();
            emit capabilitiesDetected(binId, caps.success, caps.error);
        }, Qt::QueuedConnection);
    });
}

bool BinaryRegistry::detectCapabilitiesSync(const QString &id)
{
    int idx = indexOfId(id);
    if (idx < 0) return false;

    const QString path = m_items.at(idx).path;
    DetectedCapabilities caps = CapabilityDetector::detect(path);
    if (!caps.success)
        return false;

    idx = indexOfId(id);   // re-resolve in case the model changed
    if (idx < 0) return false;

    LlamaBinary &b = m_items[idx];
    b.supportedFlags = caps.flags;
    b.kvTypes = caps.kvTypes;
    b.flagAliases = caps.flagAliases;
    if (!caps.version.isEmpty())
        b.versionHint = caps.version;
    b.pathValid = QFileInfo::exists(b.path);
    if (b.backend.isEmpty() || b.backend == QLatin1String("cpu")) {
        const QString det = detectBackend(b.path);
        if (!det.isEmpty() && det != QLatin1String("cpu")) {
            b.backend = det;
            if (b.name.endsWith(QStringLiteral("(cpu)")))
                b.name = b.name.left(b.name.size() - 5).trimmed()
                         + QStringLiteral(" (") + det + QStringLiteral(")");
        }
    }

    const QModelIndex mi = index(idx);
    emit dataChanged(mi, mi);
    save();
    emit capabilitiesDetected(id, caps.success, caps.error);
    return true;
}

QVariantMap BinaryRegistry::get(const QString &id) const
{
    const int idx = indexOfId(id);
    if (idx < 0) return {};

    const LlamaBinary &b = m_items.at(idx);
    QVariantMap m;
    m["id"] = b.id;
    m["name"] = b.name;
    m["path"] = b.path;
    m["flavor"] = b.flavor;
    m["backend"] = b.backend;
    m["versionHint"] = b.versionHint;
    m["workingDirectory"] = b.workingDirectory;
    m["pathValid"] = b.pathValid;
    m["binaryHash"] = b.binaryHash;
    m["supportedFlags"] = b.supportedFlags;
    m["kvTypes"] = b.kvTypes;
    m["hasCapabilities"] = !b.supportedFlags.isEmpty();
    return m;
}

void BinaryRegistry::refresh()
{
    for (int i = 0; i < m_items.size(); ++i) {
        bool wasValid = m_items[i].pathValid;
        m_items[i].pathValid = QFileInfo::exists(m_items[i].path);
        if (wasValid != m_items[i].pathValid) {
            const QModelIndex mi = index(i);
            emit dataChanged(mi, mi, {PathValidRole});
        }
    }
}

QStringList BinaryRegistry::supportedFlags(const QString &id) const
{
    const int idx = indexOfId(id);
    if (idx < 0) return {};
    return m_items.at(idx).supportedFlags;
}

QStringList BinaryRegistry::kvTypes(const QString &id) const
{
    const int idx = indexOfId(id);
    if (idx < 0) return {};
    return m_items.at(idx).kvTypes;
}

LlamaBinary BinaryRegistry::findById(const QString &id) const
{
    const int idx = indexOfId(id);
    if (idx < 0) return {};
    return m_items.at(idx);
}

void BinaryRegistry::load()
{
    QFile f(storagePath());
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.array();
    for (const auto &v : arr) {
        LlamaBinary b = LlamaBinary::fromJson(v.toObject());
        m_items.append(b);
    }
}

void BinaryRegistry::save() const
{
    QJsonArray arr;
    for (const auto &b : m_items)
        arr.append(b.toJson());

    QFile f(storagePath());
    QDir().mkpath(QFileInfo(storagePath()).absolutePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson());
}

QString BinaryRegistry::storagePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/binary_registry.json";
}

QString BinaryRegistry::computeHash(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    // Hash first 1MB only for speed on large binaries
    hash.addData(f.read(1024 * 1024));
    return hash.result().toHex();
}

int BinaryRegistry::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].id == id) return i;
    return -1;
}
