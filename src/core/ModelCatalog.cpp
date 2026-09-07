#include "ModelCatalog.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QUuid>
#include <QDebug>
#include <QThread>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

static const char *kSchema = R"(
CREATE TABLE IF NOT EXISTS catalog_models (
    id TEXT PRIMARY KEY,
    root_id TEXT NOT NULL,
    absolute_path TEXT NOT NULL,
    file_name TEXT NOT NULL,
    size_bytes INTEGER NOT NULL DEFAULT 0,
    mtime TEXT NOT NULL,
    family_hint TEXT,
    quant_hint TEXT,
    quant_real TEXT,
    tensor_breakdown TEXT,
    bpw REAL NOT NULL DEFAULT 0,
    quant_mismatch INTEGER NOT NULL DEFAULT 0,
    architecture TEXT,
    parameter_count INTEGER NOT NULL DEFAULT 0,
    trained_context INTEGER NOT NULL DEFAULT 0,
    is_vision_candidate INTEGER NOT NULL DEFAULT 0,
    is_draft_candidate INTEGER NOT NULL DEFAULT 0,
    sha256 TEXT,
    is_available INTEGER NOT NULL DEFAULT 1,
    stable_id INTEGER NOT NULL DEFAULT 0
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_path ON catalog_models(absolute_path);
CREATE INDEX IF NOT EXISTS idx_stable ON catalog_models(stable_id);
CREATE INDEX IF NOT EXISTS idx_identity ON catalog_models(file_name, size_bytes);
)";

ModelCatalog::ModelCatalog(QObject *parent)
    : QAbstractListModel(parent)
    , m_connName(QUuid::createUuid().toString())
{
    // Load with retry: if a previous instance still holds the DB at startup,
    // db.open()/the query can briefly fail and leave us with an empty catalog
    // for the whole session (→ every profile becomes "No model selected").
    const bool dbHasData = QFileInfo(dbPath()).size() > 4096;
    // No bloquear varios segundos antes de que exista una ventana. SQLite ya
    // tiene busy_timeout; si otra instancia mantiene el lock, el rescan de
    // arranque puede reintentarse después desde el flujo de startup.
    for (int attempt = 0; attempt < 3; ++attempt) {
        openDb();
        loadManualCompatibility();
        m_all.clear();
        loadFromDb();
        if (!m_all.isEmpty() || !dbHasData) break;
        QThread::msleep(50);
    }
    rebuildVisible();
}

int ModelCatalog::reload()
{
    beginResetModel();
    const bool dbHasData = QFileInfo(dbPath()).size() > 4096;
    for (int attempt = 0; attempt < 3; ++attempt) {
        openDb();
        loadManualCompatibility();
        m_all.clear();
        loadFromDb();
        if (!m_all.isEmpty() || !dbHasData) break;
        QThread::msleep(50);
    }
    endResetModel();
    rebuildVisible();
    return m_all.size();
}

int ModelCatalog::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_visible.size();
}

QVariant ModelCatalog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_visible.size()) return {};

    const CatalogModel *m = m_visible.at(index.row());
    switch (role) {
    case IdRole:               return m->id;
    case RootIdRole:           return m->rootId;
    case AbsolutePathRole:     return m->absolutePath;
    case FileNameRole:         return m->fileName;
    case SizeBytesRole:        return m->sizeBytes;
    case SizeLabelRole:        return m->sizeLabel();
    case MtimeRole:            return m->mtime.toString(Qt::ISODate);
    case FamilyHintRole:       return m->familyHint;
    case QuantHintRole:        return m->quantHint;
    case QuantRealRole:        return m->quantReal;
    case TensorBreakdownRole:  return m->tensorBreakdown;
    case BpwRole:              return m->bpw;
    case QuantMismatchRole:    return m->quantMismatch;
    case ArchitectureRole:     return m->architecture;
    case ParameterCountRole:   return m->parameterCount;
    case TrainedContextRole:   return m->trainedContext;
    case IsVisionCandidateRole: return m->isVisionCandidate;
    case IsDraftCandidateRole:  return m->isDraftCandidate;
    case IsAvailableRole:      return m->isAvailable;
    default:                   return {};
    }
}

QHash<int, QByteArray> ModelCatalog::roleNames() const
{
    return {
        {IdRole,                "modelId"},
        {RootIdRole,            "rootId"},
        {AbsolutePathRole,      "absolutePath"},
        {FileNameRole,          "fileName"},
        {SizeBytesRole,         "sizeBytes"},
        {SizeLabelRole,         "sizeLabel"},
        {MtimeRole,             "mtime"},
        {FamilyHintRole,        "family"},
        {QuantHintRole,         "quant"},
        {QuantRealRole,         "quantReal"},
        {TensorBreakdownRole,   "tensorBreakdown"},
        {BpwRole,               "bpw"},
        {QuantMismatchRole,     "quantMismatch"},
        {ArchitectureRole,      "architecture"},
        {ParameterCountRole,    "parameterCount"},
        {TrainedContextRole,    "trainedContext"},
        {IsVisionCandidateRole, "isVision"},
        {IsDraftCandidateRole,  "isDraft"},
        {IsAvailableRole,       "isAvailable"},
    };
}

int ModelCatalog::count() const
{
    return m_visible.size();
}

void ModelCatalog::setFilterRootId(const QString &id)
{
    if (m_filterRootId == id) return;
    m_filterRootId = id;
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

void ModelCatalog::setFilterFamily(const QString &f)
{
    if (m_filterFamily == f) return;
    m_filterFamily = f;
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

void ModelCatalog::setFilterVisionOnly(bool v)
{
    if (m_filterVisionOnly == v) return;
    m_filterVisionOnly = v;
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit filterChanged();
    emit countChanged();
}

void ModelCatalog::addOrUpdate(const CatalogModel &model)
{
    // El scanner ahora genera ids DETERMINISTAS por ruta (UUIDv5), así que el id
    // entrante para un mismo archivo es siempre el mismo. Adoptarlo en match por
    // ruta hace converger filas viejas (ids aleatorios legacy) al id estable.
    CatalogModel incoming = model;
    if (m_manualCompatibility.contains(incoming.id)) {
        const auto flags = m_manualCompatibility.value(incoming.id);
        incoming.isVisionCandidate = flags.first;
        incoming.isDraftCandidate = flags.second;
    }
    const int idx = indexOfId(incoming.id);
    if (idx >= 0) {
        // Conservar el ancla de la fila que reemplazamos: el scanner no la conoce.
        if (incoming.stableId <= 0) incoming.stableId = m_all[idx].stableId;
        if (incoming.stableId <= 0) incoming.stableId = assignStableId(incoming);
        m_all[idx] = incoming;
    } else {
        // Check by path
        int pathIdx = -1;
        for (int i = 0; i < m_all.size(); ++i) {
            if (m_all[i].absolutePath == incoming.absolutePath) { pathIdx = i; break; }
        }
        if (incoming.stableId <= 0 && pathIdx >= 0) incoming.stableId = m_all[pathIdx].stableId;
        if (incoming.stableId <= 0) incoming.stableId = assignStableId(incoming);
        if (pathIdx >= 0) m_all[pathIdx] = incoming;
        else m_all.append(incoming);
    }
    saveToDb(incoming);
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit countChanged();
}

void ModelCatalog::addBatch(const QList<CatalogModel> &models)
{
    auto db = QSqlDatabase::database(m_connName);
    db.transaction();
    for (const auto &incomingRef : models) {
        CatalogModel m = incomingRef;
        if (m_manualCompatibility.contains(m.id)) {
            const auto flags = m_manualCompatibility.value(m.id);
            m.isVisionCandidate = flags.first;
            m.isDraftCandidate = flags.second;
        }
        const int idx = indexOfId(m.id);
        if (idx >= 0) {
            if (m.stableId <= 0) m.stableId = m_all[idx].stableId;
            if (m.stableId <= 0) m.stableId = assignStableId(m);
            m_all[idx] = m;
        } else {
            int pathIdx = -1;
            for (int i = 0; i < m_all.size(); ++i)
                if (m_all[i].absolutePath == m.absolutePath) { pathIdx = i; break; }
            // El ancla sobrevive al cambio de id textual: si el archivo ya se vio
            // antes (misma ruta, o mismo nombre+tamaño en otra carpeta), se reusa.
            if (m.stableId <= 0 && pathIdx >= 0) m.stableId = m_all[pathIdx].stableId;
            if (m.stableId <= 0) m.stableId = assignStableId(m);
            if (pathIdx >= 0) m_all[pathIdx] = m;   // converge a id determinista por ruta
            else m_all.append(m);
        }
        saveToDb(m);
    }
    db.commit();
    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit countChanged();
}

void ModelCatalog::markRootUnavailable(const QString &rootId)
{
    bool changed = false;
    for (auto &m : m_all) {
        if (m.rootId == rootId && m.isAvailable) {
            m.isAvailable = false;
            saveToDb(m);
            changed = true;
        }
    }
    if (changed) {
        beginResetModel();
        rebuildVisible();
        endResetModel();
    }
}

void ModelCatalog::reconcileRoot(const QString &rootId, const QSet<QString> &presentIds)
{
    // Scan vacío = root offline o fallado. No invalidamos nada: markRootUnavailable
    // es el camino explícito para eso.
    if (presentIds.isEmpty()) return;

    auto db = QSqlDatabase::database(m_connName);
    db.transaction();
    bool changed = false;
    for (auto &m : m_all) {
        if (m.rootId != rootId) continue;
        const bool present = presentIds.contains(m.id);
        if (m.isAvailable == present) continue;
        m.isAvailable = present;
        saveToDb(m);
        changed = true;
    }
    db.commit();

    if (changed) {
        beginResetModel();
        rebuildVisible();
        endResetModel();
        emit countChanged();
    }
}

void ModelCatalog::removeByRootId(const QString &rootId)
{
    auto db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare("DELETE FROM catalog_models WHERE root_id = ?");
    q.addBindValue(rootId);
    q.exec();

    m_all.erase(std::remove_if(m_all.begin(), m_all.end(),
        [&](const CatalogModel &m) { return m.rootId == rootId; }), m_all.end());

    beginResetModel();
    rebuildVisible();
    endResetModel();
    emit countChanged();
}

QVariantMap ModelCatalog::get(const QString &id) const
{
    const int idx = indexOfId(id);
    if (idx < 0) return {};
    const CatalogModel &m = m_all.at(idx);
    return {
        {"id", m.id}, {"stableId", m.stableId},
        {"rootId", m.rootId}, {"absolutePath", m.absolutePath},
        {"fileName", m.fileName}, {"sizeBytes", m.sizeBytes},
        {"sizeLabel", m.sizeLabel()}, {"family", m.familyHint},
        {"quant", m.quantHint}, {"quantReal", m.quantReal},
        {"tensorBreakdown", m.tensorBreakdown}, {"bpw", m.bpw},
        {"quantMismatch", m.quantMismatch}, {"isVision", m.isVisionCandidate},
        {"visionManual", m_manualCompatibility.contains(m.id)},
        {"architecture", m.architecture}, {"parameterCount", m.parameterCount},
        {"trainedContext", m.trainedContext},
        {"isDraft", m.isDraftCandidate}, {"draftManual", m_manualCompatibility.contains(m.id)}, {"isAvailable", m.isAvailable}
    };
}

bool ModelCatalog::setManualCompatibility(const QString &id, bool vision, bool draft)
{
    const int idx = indexOfId(id);
    if (idx < 0) return false;
    m_manualCompatibility.insert(id, qMakePair(vision, draft));
    m_all[idx].isVisionCandidate = vision;
    m_all[idx].isDraftCandidate = draft;
    saveManualCompatibility();
    saveToDb(m_all[idx]);
    beginResetModel(); rebuildVisible(); endResetModel();
    emit countChanged();
    return true;
}

QVariantMap ModelCatalog::getAt(int row) const
{
    if (row < 0 || row >= m_visible.size()) return {};
    return get(m_visible.at(row)->id);
}

CatalogModel ModelCatalog::findById(const QString &id) const
{
    const int idx = indexOfId(id);
    if (idx < 0) return {};
    return m_all.at(idx);
}

CatalogModel ModelCatalog::findByStableId(qint64 stableId) const
{
    if (stableId <= 0) return {};
    // Puede haber más de una fila con el mismo ancla: el archivo se movió y la fila
    // de la ruta vieja sigue en la DB, o hay dos copias en carpetas distintas.
    // Preferimos las disponibles y, entre ellas, la de mtime más reciente — la que
    // el último scan vio. Determinista, sin depender del orden de inserción.
    const CatalogModel *best = nullptr;
    for (const auto &m : m_all) {
        if (m.stableId != stableId) continue;
        if (!best) { best = &m; continue; }
        if (m.isAvailable != best->isAvailable) {
            if (m.isAvailable) best = &m;
            continue;
        }
        if (m.mtime > best->mtime) best = &m;
    }
    return best ? *best : CatalogModel{};
}

QList<CatalogModel> ModelCatalog::allForRoot(const QString &rootId) const
{
    QList<CatalogModel> res;
    for (const auto &m : m_all)
        if (m.rootId == rootId) res.append(m);
    return res;
}

bool ModelCatalog::openDb()
{
    QDir().mkpath(QFileInfo(dbPath()).absolutePath());
    auto db = QSqlDatabase::contains(m_connName)
                  ? QSqlDatabase::database(m_connName, /*open=*/false)
                  : QSqlDatabase::addDatabase("QSQLITE", m_connName);
    db.setDatabaseName(dbPath());
    if (!db.isOpen() && !db.open()) {
        qWarning() << "ModelCatalog: cannot open DB:" << db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA busy_timeout=3000"));
    const QStringList stmts = QString::fromUtf8(kSchema).split(';', Qt::SkipEmptyParts);
    for (const auto &s : stmts) {
        if (!s.trimmed().isEmpty())
            q.exec(s.trimmed());
    }
    // Migración: DBs viejas no tienen las columnas de composición real. ALTER
    // falla silencioso si la columna ya existe (lo ignoramos).
    for (const char *alter : {
             "ALTER TABLE catalog_models ADD COLUMN quant_real TEXT",
             "ALTER TABLE catalog_models ADD COLUMN tensor_breakdown TEXT",
             "ALTER TABLE catalog_models ADD COLUMN bpw REAL NOT NULL DEFAULT 0",
             "ALTER TABLE catalog_models ADD COLUMN quant_mismatch INTEGER NOT NULL DEFAULT 0",
             "ALTER TABLE catalog_models ADD COLUMN architecture TEXT",
             "ALTER TABLE catalog_models ADD COLUMN parameter_count INTEGER NOT NULL DEFAULT 0",
             "ALTER TABLE catalog_models ADD COLUMN trained_context INTEGER NOT NULL DEFAULT 0",
             "ALTER TABLE catalog_models ADD COLUMN stable_id INTEGER NOT NULL DEFAULT 0",
             "CREATE INDEX IF NOT EXISTS idx_stable ON catalog_models(stable_id)",
             "CREATE INDEX IF NOT EXISTS idx_identity ON catalog_models(file_name, size_bytes)" }) {
        q.exec(QString::fromUtf8(alter));
    }
    return true;
}

qint64 ModelCatalog::assignStableId(const CatalogModel &m)
{
    // El id textual del catálogo se deriva de la RUTA, así que mover un gguf de
    // carpeta le cambia el id y orfana todo perfil que lo referenciara. El stable
    // id es un entero incremental que se asigna UNA vez por archivo y no vuelve a
    // cambiar: es el ancla que los perfiles pueden guardar sin miedo.
    //
    // Identidad del archivo = (nombre, tamaño en bytes). Sobrevive a mover o
    // renombrar carpetas, que es el caso real que rompía los perfiles. No sobrevive
    // a renombrar el archivo en sí; ahí se trata como uno nuevo, que es lo prudente.
    auto db = QSqlDatabase::database(m_connName);

    QSqlQuery find(db);
    find.prepare("SELECT stable_id FROM catalog_models "
                 "WHERE file_name = ? AND size_bytes = ? AND stable_id > 0 LIMIT 1");
    find.addBindValue(m.fileName);
    find.addBindValue(m.sizeBytes);
    if (find.exec() && find.next()) {
        const qint64 existing = find.value(0).toLongLong();
        if (existing > 0) return existing;
    }

    QSqlQuery next(db);
    if (next.exec("SELECT COALESCE(MAX(stable_id), 0) + 1 FROM catalog_models") && next.next())
        return next.value(0).toLongLong();
    return 1;
}

void ModelCatalog::loadFromDb()
{
    auto db = QSqlDatabase::database(m_connName);
    QSqlQuery q("SELECT id, root_id, absolute_path, file_name, size_bytes, mtime, "
                "family_hint, quant_hint, is_vision_candidate, is_draft_candidate, "
                "sha256, is_available, quant_real, tensor_breakdown, bpw, "
                "quant_mismatch, architecture, parameter_count, trained_context, "
                "stable_id FROM catalog_models", db);
    while (q.next()) {
        CatalogModel m;
        m.id = q.value(0).toString();
        m.rootId = q.value(1).toString();
        m.absolutePath = q.value(2).toString();
        m.fileName = q.value(3).toString();
        m.sizeBytes = q.value(4).toLongLong();
        m.mtime = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
        m.familyHint = q.value(6).toString();
        m.quantHint = q.value(7).toString();
        m.isVisionCandidate = q.value(8).toBool();
        m.isDraftCandidate = q.value(9).toBool();
        m.sha256 = q.value(10).toString();
        m.isAvailable = q.value(11).toBool();
        m.quantReal = q.value(12).toString();
        m.tensorBreakdown = q.value(13).toString();
        m.bpw = q.value(14).toDouble();
        m.quantMismatch = q.value(15).toBool();
        m.architecture = q.value(16).toString();
        m.parameterCount = q.value(17).toLongLong();
        m.trainedContext = q.value(18).toInt();
        m.stableId = q.value(19).toLongLong();
        m_all.append(m);
    }
    backfillStableIds();
}

// DBs anteriores al stable id (y filas escritas por versiones viejas) llegan con 0.
// Se lo asignamos una vez, al cargar, para que toda fila tenga ancla desde el arranque.
void ModelCatalog::backfillStableIds()
{
    auto db = QSqlDatabase::database(m_connName);
    bool any = false;
    for (auto &m : m_all) {
        if (m.stableId > 0) continue;
        if (!any) { db.transaction(); any = true; }
        m.stableId = assignStableId(m);
        saveToDb(m);
    }
    if (any) db.commit();
}

void ModelCatalog::saveToDb(const CatalogModel &m)
{
    auto db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.prepare(R"(
        INSERT OR REPLACE INTO catalog_models
        (id, root_id, absolute_path, file_name, size_bytes, mtime,
         family_hint, quant_hint, is_vision_candidate, is_draft_candidate,
         sha256, is_available, quant_real, tensor_breakdown, bpw, quant_mismatch,
         architecture, parameter_count, trained_context, stable_id)
        VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)
    )");
    q.addBindValue(m.id);
    q.addBindValue(m.rootId);
    q.addBindValue(m.absolutePath);
    q.addBindValue(m.fileName);
    q.addBindValue(m.sizeBytes);
    q.addBindValue(m.mtime.toString(Qt::ISODate));
    q.addBindValue(m.familyHint);
    q.addBindValue(m.quantHint);
    q.addBindValue(m.isVisionCandidate ? 1 : 0);
    q.addBindValue(m.isDraftCandidate ? 1 : 0);
    q.addBindValue(m.sha256);
    q.addBindValue(m.isAvailable ? 1 : 0);
    q.addBindValue(m.quantReal);
    q.addBindValue(m.tensorBreakdown);
    q.addBindValue(m.bpw);
    q.addBindValue(m.quantMismatch ? 1 : 0);
    q.addBindValue(m.architecture);
    q.addBindValue(m.parameterCount);
    q.addBindValue(m.trainedContext);
    q.addBindValue(m.stableId);
    if (!q.exec())
        qWarning() << "saveToDb failed:" << q.lastError().text();
}

void ModelCatalog::rebuildVisible()
{
    m_visible.clear();
    for (const auto &m : m_all) {
        if (matchesFilter(m))
            m_visible.append(&m);
    }
}

void ModelCatalog::loadManualCompatibility()
{
    m_manualCompatibility.clear();
    QFile f(dbPath() + QStringLiteral(".compat.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject flags = it.value().toObject();
        m_manualCompatibility.insert(it.key(), qMakePair(flags.value(QStringLiteral("vision")).toBool(),
                                                          flags.value(QStringLiteral("draft")).toBool()));
    }
}

void ModelCatalog::saveManualCompatibility() const
{
    QJsonObject root;
    for (auto it = m_manualCompatibility.cbegin(); it != m_manualCompatibility.cend(); ++it)
        root[it.key()] = QJsonObject{{QStringLiteral("vision"), it.value().first},
                                     {QStringLiteral("draft"), it.value().second}};
    QDir().mkpath(QFileInfo(dbPath()).absolutePath());
    QFile f(dbPath() + QStringLiteral(".compat.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool ModelCatalog::matchesFilter(const CatalogModel &m) const
{
    if (!m_filterRootId.isEmpty() && m.rootId != m_filterRootId) return false;
    if (!m_filterFamily.isEmpty() && m.familyHint != m_filterFamily) return false;
    if (m_filterVisionOnly && !m.isVisionCandidate) return false;
    return true;
}

QString ModelCatalog::dbPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + "/model_catalog.db";
}

int ModelCatalog::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_all.size(); ++i)
        if (m_all[i].id == id) return i;
    return -1;
}
