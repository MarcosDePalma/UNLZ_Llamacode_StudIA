#include "TriggerManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

TriggerManager::TriggerManager(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged,
            this, &TriggerManager::onPathChanged);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &TriggerManager::onPathChanged);
    load();
    rebuildWatchers();
}

int TriggerManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant TriggerManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) return {};
    const auto names = roleNames();
    return names.contains(role)
        ? m_items.at(index.row()).value(QString::fromUtf8(names.value(role))) : QVariant{};
}

QHash<int, QByteArray> TriggerManager::roleNames() const
{
    return {{IdRole, "id"}, {NameRole, "name"}, {AgentIdRole, "agentId"},
            {TaskIdRole, "taskId"}, {TypeRole, "type"}, {EnabledRole, "enabled"},
            {ConfigRole, "config"}, {DebounceMsRole, "debounceMs"},
            {LastFiredAtRole, "lastFiredAt"}};
}

int TriggerManager::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).value(QStringLiteral("id")).toString() == id) return i;
    return -1;
}

QString TriggerManager::save(const QString &id, const QVariantMap &definition)
{
    const int row = indexOfId(id);
    QVariantMap item = row >= 0 ? m_items.at(row) : QVariantMap{};
    for (const QString &key : {QStringLiteral("name"), QStringLiteral("agentId"),
                               QStringLiteral("taskId"), QStringLiteral("type"),
                               QStringLiteral("enabled"), QStringLiteral("config"),
                               QStringLiteral("debounceMs")})
        if (definition.contains(key)) item[key] = definition.value(key);
    const QString type = item.value(QStringLiteral("type")).toString();
    static const QSet<QString> allowed = {QStringLiteral("filesystem"),
        QStringLiteral("webhook"), QStringLiteral("appEvent"), QStringLiteral("manual")};
    if (!allowed.contains(type) || item.value(QStringLiteral("taskId")).toString().isEmpty())
        return {};
    item[QStringLiteral("enabled")] = item.value(QStringLiteral("enabled"), true).toBool();
    item[QStringLiteral("debounceMs")] =
        qBound(0, item.value(QStringLiteral("debounceMs"), 1500).toInt(), 600000);
    item[QStringLiteral("updatedAt")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QString outId = id;
    if (row >= 0) {
        m_items[row] = item;
        emit dataChanged(index(row), index(row));
    } else {
        outId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        item[QStringLiteral("id")] = outId;
        item[QStringLiteral("createdAt")] = item.value(QStringLiteral("updatedAt"));
        beginInsertRows({}, m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
        emit countChanged();
    }
    persist();
    rebuildWatchers();
    emit changed();
    return outId;
}

bool TriggerManager::remove(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0) return false;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    persist();
    rebuildWatchers();
    emit countChanged();
    emit changed();
    return true;
}

QVariantMap TriggerManager::get(const QString &id) const
{
    const int row = indexOfId(id);
    return row >= 0 ? m_items.at(row) : QVariantMap{};
}

QVariantList TriggerManager::all() const
{
    QVariantList out;
    for (const QVariantMap &item : m_items) out.append(item);
    return out;
}

bool TriggerManager::matches(const QVariantMap &trigger, const QString &type,
                             const QVariantMap &event)
{
    if (!trigger.value(QStringLiteral("enabled"), true).toBool()
        || trigger.value(QStringLiteral("type")).toString() != type) return false;
    const QVariantMap config = trigger.value(QStringLiteral("config")).toMap();
    if (type == QLatin1String("filesystem"))
        return QFileInfo(config.value(QStringLiteral("path")).toString()).absoluteFilePath()
               == QFileInfo(event.value(QStringLiteral("path")).toString()).absoluteFilePath();
    if (type == QLatin1String("webhook"))
        return config.value(QStringLiteral("key")).toString()
               == event.value(QStringLiteral("key")).toString();
    if (type == QLatin1String("appEvent")) {
        if (config.value(QStringLiteral("name")).toString()
            != event.value(QStringLiteral("name")).toString()) return false;
        const QVariantMap filters = config.value(QStringLiteral("filters")).toMap();
        const QVariantMap payload = event.value(QStringLiteral("payload")).toMap();
        for (auto it = filters.constBegin(); it != filters.constEnd(); ++it)
            if (payload.value(it.key()) != it.value()) return false;
    }
    return true;
}

QVariantList TriggerManager::dispatchEvent(const QString &type, const QVariantMap &event)
{
    QVariantList fired;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int row = 0; row < m_items.size(); ++row) {
        QVariantMap &trigger = m_items[row];
        if (!matches(trigger, type, event)) continue;
        const QString id = trigger.value(QStringLiteral("id")).toString();
        const int debounce = trigger.value(QStringLiteral("debounceMs"), 1500).toInt();
        if (now - m_lastFireMs.value(id, 0) < debounce) continue;
        m_lastFireMs[id] = now;
        trigger[QStringLiteral("lastFiredAt")] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        fired.append(id);
        emit dataChanged(index(row), index(row), {LastFiredAtRole});
        emit taskRequested(trigger.value(QStringLiteral("taskId")).toString(), id, event);
    }
    if (!fired.isEmpty()) persist();
    return fired;
}

void TriggerManager::onPathChanged(const QString &path)
{
    dispatchEvent(QStringLiteral("filesystem"), {{QStringLiteral("path"), path}});
    if (QFileInfo::exists(path) && !m_watcher.files().contains(path)
        && !m_watcher.directories().contains(path)) m_watcher.addPath(path);
}

QStringList TriggerManager::watchedPaths() const
{
    return m_watcher.files() + m_watcher.directories();
}

void TriggerManager::rebuildWatchers()
{
    const QStringList previous = watchedPaths();
    if (!previous.isEmpty()) m_watcher.removePaths(previous);
    QStringList paths;
    for (const QVariantMap &trigger : m_items) {
        if (!trigger.value(QStringLiteral("enabled"), true).toBool()
            || trigger.value(QStringLiteral("type")).toString() != QLatin1String("filesystem"))
            continue;
        const QString path = trigger.value(QStringLiteral("config")).toMap()
                                 .value(QStringLiteral("path")).toString();
        if (QFileInfo::exists(path) && !paths.contains(path)) paths.append(path);
    }
    if (!paths.isEmpty()) m_watcher.addPaths(paths);
}

QString TriggerManager::storagePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/agents");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/triggers.json");
}

void TriggerManager::load()
{
    m_items.clear();
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly)) return;
    for (const QJsonValue &value : QJsonDocument::fromJson(file.readAll()).array())
        m_items.append(value.toObject().toVariantMap());
}

void TriggerManager::persist() const
{
    QJsonArray array;
    for (const QVariantMap &item : m_items)
        array.append(QJsonObject::fromVariantMap(item));
    QFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
}
