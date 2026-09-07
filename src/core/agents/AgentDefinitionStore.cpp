#include "AgentDefinitionStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace {
const QStringList kSemanticFields = {
    QStringLiteral("name"), QStringLiteral("description"),
    QStringLiteral("profileId"), QStringLiteral("launchProfileId"),
    QStringLiteral("workspaceId"), QStringLiteral("instructions"),
    QStringLiteral("skillIds"), QStringLiteral("mcpServers"),
    QStringLiteral("toolPermissions"), QStringLiteral("taskIds"),
    QStringLiteral("triggerIds")
};

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
}

AgentDefinitionStore::AgentDefinitionStore(QObject *parent)
    : QAbstractListModel(parent)
{
    load();
}

int AgentDefinitionStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant AgentDefinitionStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) return {};
    const QVariantMap &a = m_items.at(index.row());
    const auto names = roleNames();
    return names.contains(role) ? a.value(QString::fromUtf8(names.value(role))) : QVariant{};
}

QHash<int, QByteArray> AgentDefinitionStore::roleNames() const
{
    return {
        {IdRole, "id"}, {NameRole, "name"}, {DescriptionRole, "description"},
        {ProfileIdRole, "profileId"}, {LaunchProfileIdRole, "launchProfileId"},
        {WorkspaceIdRole, "workspaceId"}, {InstructionsRole, "instructions"},
        {SkillIdsRole, "skillIds"}, {McpServersRole, "mcpServers"},
        {ToolPermissionsRole, "toolPermissions"}, {TaskIdsRole, "taskIds"},
        {TriggerIdsRole, "triggerIds"}, {CurrentRevisionRole, "currentRevision"},
        {CreatedAtRole, "createdAt"}, {UpdatedAtRole, "updatedAt"}
    };
}

int AgentDefinitionStore::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).value(QStringLiteral("id")).toString() == id) return i;
    return -1;
}

QVariantMap AgentDefinitionStore::semanticSnapshot(const QVariantMap &definition)
{
    QVariantMap out;
    for (const QString &key : kSemanticFields) out.insert(key, definition.value(key));
    return out;
}

void AgentDefinitionStore::appendRevision(QVariantMap &definition, const QString &reason,
                                          const QVariantMap &snapshot)
{
    QVariantList revisions = definition.value(QStringLiteral("revisions")).toList();
    const int number = revisions.isEmpty()
        ? 1 : revisions.constLast().toMap().value(QStringLiteral("number")).toInt() + 1;
    revisions.append(QVariantMap{
        {QStringLiteral("number"), number},
        {QStringLiteral("createdAt"), nowIso()},
        {QStringLiteral("reason"), reason.trimmed().isEmpty()
            ? QStringLiteral("Actualización manual") : reason.trimmed()},
        {QStringLiteral("snapshot"), snapshot}
    });
    definition[QStringLiteral("revisions")] = revisions;
    definition[QStringLiteral("currentRevision")] = number;
}

QString AgentDefinitionStore::save(const QString &id, const QVariantMap &input,
                                   const QString &reason)
{
    const int row = indexOfId(id);
    QVariantMap a = row >= 0 ? m_items.at(row) : QVariantMap{};
    const QVariantMap before = semanticSnapshot(a);
    for (const QString &key : kSemanticFields)
        if (input.contains(key)) a[key] = input.value(key);
    if (a.value(QStringLiteral("name")).toString().trimmed().isEmpty()) return {};

    const bool created = row < 0;
    if (created) {
        a[QStringLiteral("id")] = id.trimmed().isEmpty() ? newId() : id.trimmed();
        a[QStringLiteral("createdAt")] = nowIso();
    }
    const QVariantMap after = semanticSnapshot(a);
    if (created || before != after) appendRevision(a, created ? QStringLiteral("Creación") : reason, after);
    a[QStringLiteral("updatedAt")] = nowIso();

    if (row >= 0) {
        m_items[row] = a;
        emit dataChanged(index(row), index(row));
    } else {
        beginInsertRows({}, m_items.size(), m_items.size());
        m_items.append(a);
        endInsertRows();
        emit countChanged();
    }
    persist();
    emit changed();
    return a.value(QStringLiteral("id")).toString();
}

bool AgentDefinitionStore::remove(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0) return false;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    for (int i = m_feedback.size() - 1; i >= 0; --i)
        if (m_feedback.at(i).value(QStringLiteral("agentId")).toString() == id)
            m_feedback.removeAt(i);
    persist();
    emit countChanged();
    emit changed();
    emit feedbackChanged();
    return true;
}

QVariantMap AgentDefinitionStore::get(const QString &id) const
{
    const int row = indexOfId(id);
    return row >= 0 ? m_items.at(row) : QVariantMap{};
}

QVariantList AgentDefinitionStore::all() const
{
    QVariantList out;
    for (const QVariantMap &a : m_items) out.append(a);
    return out;
}

QString AgentDefinitionStore::duplicate(const QString &id)
{
    QVariantMap copy = get(id);
    if (copy.isEmpty()) return {};
    copy.remove(QStringLiteral("id"));
    copy.remove(QStringLiteral("createdAt"));
    copy.remove(QStringLiteral("updatedAt"));
    copy.remove(QStringLiteral("revisions"));
    copy.remove(QStringLiteral("currentRevision"));
    copy[QStringLiteral("name")] = copy.value(QStringLiteral("name")).toString()
                                   + QStringLiteral(" (copia)");
    return save({}, copy, QStringLiteral("Duplicado"));
}

QVariantList AgentDefinitionStore::revisions(const QString &id) const
{
    return get(id).value(QStringLiteral("revisions")).toList();
}

QVariantMap AgentDefinitionStore::revisionDiff(const QString &id, int fromRevision,
                                               int toRevision) const
{
    QVariantMap from;
    QVariantMap to;
    for (const QVariant &value : revisions(id)) {
        const QVariantMap revision = value.toMap();
        if (revision.value(QStringLiteral("number")).toInt() == fromRevision)
            from = revision.value(QStringLiteral("snapshot")).toMap();
        if (revision.value(QStringLiteral("number")).toInt() == toRevision)
            to = revision.value(QStringLiteral("snapshot")).toMap();
    }
    if (from.isEmpty() || to.isEmpty()) return {};
    QVariantList changes;
    for (const QString &key : kSemanticFields) {
        if (from.value(key) == to.value(key)) continue;
        changes.append(QVariantMap{{QStringLiteral("field"), key},
                                   {QStringLiteral("before"), from.value(key)},
                                   {QStringLiteral("after"), to.value(key)}});
    }
    return {{QStringLiteral("fromRevision"), fromRevision},
            {QStringLiteral("toRevision"), toRevision},
            {QStringLiteral("changes"), changes}};
}

bool AgentDefinitionStore::restoreRevision(const QString &id, int revision)
{
    for (const QVariant &value : revisions(id)) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("number")).toInt() != revision) continue;
        return !save(id, item.value(QStringLiteral("snapshot")).toMap(),
                     QStringLiteral("Restauración de revisión %1").arg(revision)).isEmpty();
    }
    return false;
}

QString AgentDefinitionStore::proposeFeedback(const QString &agentId,
                                              const QString &feedback,
                                              const QString &scope)
{
    if (get(agentId).isEmpty() || feedback.trimmed().isEmpty()) return {};
    const QString id = newId();
    m_feedback.append({
        {QStringLiteral("id"), id}, {QStringLiteral("agentId"), agentId},
        {QStringLiteral("feedback"), feedback.trimmed()},
        {QStringLiteral("scope"), scope.trimmed().isEmpty() ? QStringLiteral("agent") : scope},
        {QStringLiteral("status"), QStringLiteral("pending")},
        {QStringLiteral("createdAt"), nowIso()}
    });
    persist();
    emit feedbackChanged();
    return id;
}

QVariantList AgentDefinitionStore::pendingFeedback(const QString &agentId) const
{
    QVariantList out;
    for (const QVariantMap &p : m_feedback)
        if (p.value(QStringLiteral("status")).toString() == QLatin1String("pending")
            && (agentId.isEmpty() || p.value(QStringLiteral("agentId")).toString() == agentId))
            out.append(p);
    return out;
}

int AgentDefinitionStore::proposalIndex(const QString &proposalId) const
{
    for (int i = 0; i < m_feedback.size(); ++i)
        if (m_feedback.at(i).value(QStringLiteral("id")).toString() == proposalId) return i;
    return -1;
}

bool AgentDefinitionStore::approveFeedback(const QString &proposalId)
{
    const int row = proposalIndex(proposalId);
    if (row < 0 || m_feedback.at(row).value(QStringLiteral("status")).toString()
                       != QLatin1String("pending")) return false;
    QVariantMap proposal = m_feedback.at(row);
    QVariantMap agent = get(proposal.value(QStringLiteral("agentId")).toString());
    if (agent.isEmpty()) return false;
    QString instructions = agent.value(QStringLiteral("instructions")).toString().trimmed();
    if (!instructions.isEmpty()) instructions += QStringLiteral("\n\n");
    instructions += QStringLiteral("Feedback aprobado: ")
                    + proposal.value(QStringLiteral("feedback")).toString();
    agent[QStringLiteral("instructions")] = instructions;
    if (save(agent.value(QStringLiteral("id")).toString(), agent,
             QStringLiteral("Feedback aprobado")).isEmpty()) return false;
    proposal[QStringLiteral("status")] = QStringLiteral("approved");
    proposal[QStringLiteral("resolvedAt")] = nowIso();
    m_feedback[row] = proposal;
    persist();
    emit feedbackChanged();
    return true;
}

bool AgentDefinitionStore::rejectFeedback(const QString &proposalId)
{
    const int row = proposalIndex(proposalId);
    if (row < 0 || m_feedback.at(row).value(QStringLiteral("status")).toString()
                       != QLatin1String("pending")) return false;
    m_feedback[row][QStringLiteral("status")] = QStringLiteral("rejected");
    m_feedback[row][QStringLiteral("resolvedAt")] = nowIso();
    persist();
    emit feedbackChanged();
    return true;
}

QVariantMap AgentDefinitionStore::aggregateMetrics(
    const QString &agentId,
    const std::function<QVariantList(const QString &)> &historyProvider) const
{
    const QVariantMap agent = get(agentId);
    if (agent.isEmpty()) return {};
    int runs = 0;
    int ok = 0;
    double promptTokens = 0;
    double generatedTokens = 0;
    double wallMs = 0;
    QSet<QString> seenRuns;
    for (const QString &taskId : agent.value(QStringLiteral("taskIds")).toStringList()) {
        for (const QVariant &value : historyProvider(taskId)) {
            const QVariantMap run = value.toMap();
            const QString runId = run.value(QStringLiteral("runId")).toString();
            if (!runId.isEmpty() && seenRuns.contains(runId)) continue;
            if (!runId.isEmpty()) seenRuns.insert(runId);
            ++runs;
            if (run.value(QStringLiteral("status")).toString() == QLatin1String("ok")) ++ok;
            const QVariantMap metrics = run.value(QStringLiteral("metrics")).toMap();
            promptTokens += metrics.value(QStringLiteral("promptTokens")).toDouble();
            generatedTokens += metrics.value(QStringLiteral("generatedTokens")).toDouble();
            wallMs += metrics.value(QStringLiteral("wallMs")).toDouble();
        }
    }
    return {
        {QStringLiteral("runs"), runs}, {QStringLiteral("successes"), ok},
        {QStringLiteral("successRate"), runs ? (100.0 * ok / runs) : 0.0},
        {QStringLiteral("promptTokens"), promptTokens},
        {QStringLiteral("generatedTokens"), generatedTokens},
        {QStringLiteral("wallMs"), wallMs}
    };
}

QJsonObject AgentDefinitionStore::toJson(const QVariantMap &definition)
{
    return QJsonObject::fromVariantMap(definition);
}

QVariantMap AgentDefinitionStore::fromJson(const QJsonObject &object)
{
    return object.toVariantMap();
}

QString AgentDefinitionStore::storagePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/agents");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/agents.json");
}

void AgentDefinitionStore::load()
{
    m_items.clear();
    m_feedback.clear();
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &value : root.value(QStringLiteral("agents")).toArray())
        m_items.append(fromJson(value.toObject()));
    for (const QJsonValue &value : root.value(QStringLiteral("feedback")).toArray())
        m_feedback.append(value.toObject().toVariantMap());
}

void AgentDefinitionStore::persist() const
{
    QJsonArray agents;
    for (const QVariantMap &a : m_items) agents.append(toJson(a));
    QJsonArray feedback;
    for (const QVariantMap &p : m_feedback) feedback.append(QJsonObject::fromVariantMap(p));
    QFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("agents"), agents},
                                         {QStringLiteral("feedback"), feedback}})
                   .toJson(QJsonDocument::Indented));
}
