#include "ModelRoleRegistry.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>

namespace {
QString cleanText(const QVariantMap &values, const QString &key, int max = 512)
{
    return values.value(key).toString().trimmed().left(max);
}

QVariantMap makeRole(const QString &id, const QString &jobClass,
                     const QString &resourceKey, int priority,
                     int maxConcurrency)
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("model"), QString()},
        {QStringLiteral("fallbackModel"), QString()},
        {QStringLiteral("endpoint"), QString()},
        {QStringLiteral("jobClass"), jobClass},
        {QStringLiteral("resourceKey"), resourceKey},
        {QStringLiteral("priority"), priority},
        {QStringLiteral("maxConcurrency"), maxConcurrency},
        {QStringLiteral("enabled"), true},
    };
}
} // namespace

ModelRoleRegistry::ModelRoleRegistry(QObject *parent)
    : QObject(parent)
{
    for (const QVariant &value : defaultRoles()) {
        const QVariantMap item = value.toMap();
        m_roles.insert(item.value(QStringLiteral("id")).toString(), item);
    }
    load();
}

QVariantList ModelRoleRegistry::defaultRoles()
{
    return {
        makeRole(QStringLiteral("primary_agent"), QStringLiteral("interactive_text"),
                 QStringLiteral("primary"), 100, 1),
        makeRole(QStringLiteral("fast_agent"), QStringLiteral("interactive_text"),
                 QStringLiteral("fast"), 95, 1),
        makeRole(QStringLiteral("planner"), QStringLiteral("agent_tool"),
                 QStringLiteral("planner"), 45, 1),
        makeRole(QStringLiteral("verifier"), QStringLiteral("verification"),
                 QStringLiteral("verifier"), 60, 1),
        makeRole(QStringLiteral("embedding"), QStringLiteral("retrieval"),
                 QStringLiteral("embedding"), 35, 1),
        makeRole(QStringLiteral("reranker"), QStringLiteral("retrieval"),
                 QStringLiteral("reranker"), 40, 1),
        makeRole(QStringLiteral("stt"), QStringLiteral("voice"),
                 QStringLiteral("stt"), 80, 1),
        makeRole(QStringLiteral("tts"), QStringLiteral("voice"),
                 QStringLiteral("tts"), 70, 1),
        makeRole(QStringLiteral("vision"), QStringLiteral("document"),
                 QStringLiteral("vision"), 50, 1),
    };
}

QString ModelRoleRegistry::storagePath()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) root = QDir::homePath() + QStringLiteral("/.llamacode");
    QDir().mkpath(root);
    return QDir(root).filePath(QStringLiteral("model_roles.json"));
}

QString ModelRoleRegistry::normalizeRoleId(const QString &roleId)
{
    const QString value = roleId.trimmed().toLower();
    if (value.isEmpty() || value.size() > 64
        || !QRegularExpression(QStringLiteral("^[a-z0-9_-]+$")).match(value).hasMatch())
        return {};
    return value;
}

QVariantMap ModelRoleRegistry::normalizeRole(const QVariantMap &role)
{
    QVariantMap out = role;
    out[QStringLiteral("model")] = cleanText(role, QStringLiteral("model"));
    out[QStringLiteral("fallbackModel")] = cleanText(role, QStringLiteral("fallbackModel"));
    out[QStringLiteral("endpoint")] = cleanText(role, QStringLiteral("endpoint"), 2048);
    out[QStringLiteral("jobClass")] = cleanText(role, QStringLiteral("jobClass"), 64);
    out[QStringLiteral("resourceKey")] = cleanText(role, QStringLiteral("resourceKey"), 128);
    out[QStringLiteral("priority")] = qBound(-1000, role.value(QStringLiteral("priority"), 0).toInt(), 1000);
    out[QStringLiteral("maxConcurrency")] = qBound(0, role.value(QStringLiteral("maxConcurrency"), 1).toInt(), 128);
    out[QStringLiteral("enabled")] = role.value(QStringLiteral("enabled"), true).toBool();
    return out;
}

QVariantList ModelRoleRegistry::snapshot() const
{
    QVariantList result;
    for (auto it = m_roles.cbegin(); it != m_roles.cend(); ++it)
        result.append(it.value());
    return result;
}

QVariantMap ModelRoleRegistry::role(const QString &roleId) const
{
    return m_roles.value(normalizeRoleId(roleId)).toMap();
}

bool ModelRoleRegistry::setRole(const QString &roleId, const QVariantMap &values)
{
    const QString id = normalizeRoleId(roleId);
    if (id.isEmpty() || !m_roles.contains(id)) return false;
    QVariantMap next = m_roles.value(id).toMap();
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        if (it.key() != QStringLiteral("id")) next[it.key()] = it.value();
    next[QStringLiteral("id")] = id;
    next = normalizeRole(next);
    if (next == m_roles.value(id).toMap()) return true;
    m_roles[id] = next;
    save();
    emit rolesChanged();
    return true;
}

bool ModelRoleRegistry::resetRole(const QString &roleId)
{
    const QString id = normalizeRoleId(roleId);
    for (const QVariant &value : defaultRoles()) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("id")).toString() != id) continue;
        m_roles[id] = item;
        save();
        emit rolesChanged();
        return true;
    }
    return false;
}

QString ModelRoleRegistry::resolveModel(const QString &roleId) const
{
    const QVariantMap item = role(roleId);
    if (!item.value(QStringLiteral("enabled")).toBool()) return {};
    const QString model = item.value(QStringLiteral("model")).toString().trimmed();
    return model.isEmpty() ? item.value(QStringLiteral("fallbackModel")).toString().trimmed()
                           : model;
}

QVariantMap ModelRoleRegistry::schedulingHint(const QString &roleId) const
{
    const QVariantMap item = role(roleId);
    if (item.isEmpty() || !item.value(QStringLiteral("enabled"), true).toBool()) return {};
    return {
        {QStringLiteral("class"), item.value(QStringLiteral("jobClass"))},
        {QStringLiteral("resourceKey"), item.value(QStringLiteral("resourceKey"))},
        {QStringLiteral("priority"), item.value(QStringLiteral("priority"))},
        {QStringLiteral("maxConcurrency"), item.value(QStringLiteral("maxConcurrency"))},
        {QStringLiteral("model"), resolveModel(roleId)},
    };
}

void ModelRoleRegistry::load()
{
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return;
    const QJsonObject object = document.object();
    for (auto it = object.begin(); it != object.end(); ++it) {
        const QString id = normalizeRoleId(it.key());
        if (id.isEmpty() || !it.value().isObject() || !m_roles.contains(id)) continue;
        QVariantMap values = it.value().toObject().toVariantMap();
        values[QStringLiteral("id")] = id;
        m_roles[id] = normalizeRole(values);
    }
}

void ModelRoleRegistry::save() const
{
    QJsonObject object;
    for (auto it = m_roles.cbegin(); it != m_roles.cend(); ++it)
        object.insert(it.key(), QJsonObject::fromVariantMap(it.value().toMap()));
    QSaveFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.commit();
}
