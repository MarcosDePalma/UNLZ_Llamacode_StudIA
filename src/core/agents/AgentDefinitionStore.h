#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

// AgentDefinitionStore — entidad de producto que agrupa la configuración técnica
// ya existente (AgentProfile/LaunchProfile/Workspace) con instrucciones, skills,
// Tasks y triggers. Cada cambio semántico crea una revisión inmutable y restaurar
// una revisión crea una revisión nueva (no reescribe la historia).
class AgentDefinitionStore : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        ProfileIdRole,
        LaunchProfileIdRole,
        WorkspaceIdRole,
        InstructionsRole,
        SkillIdsRole,
        McpServersRole,
        ToolPermissionsRole,
        TaskIdsRole,
        TriggerIdsRole,
        CurrentRevisionRole,
        CreatedAtRole,
        UpdatedAtRole
    };

    explicit AgentDefinitionStore(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_items.size(); }

    Q_INVOKABLE QString save(const QString &id, const QVariantMap &definition,
                             const QString &reason = QString());
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE QVariantMap get(const QString &id) const;
    Q_INVOKABLE QVariantList all() const;
    Q_INVOKABLE QString duplicate(const QString &id);

    Q_INVOKABLE QVariantList revisions(const QString &id) const;
    Q_INVOKABLE QVariantMap revisionDiff(const QString &id, int fromRevision,
                                         int toRevision) const;
    Q_INVOKABLE bool restoreRevision(const QString &id, int revision);

    // Feedback supervisado: primero persiste una propuesta revisable. Sólo
    // approveFeedback modifica la definición y crea una revisión.
    Q_INVOKABLE QString proposeFeedback(const QString &agentId, const QString &feedback,
                                        const QString &scope = QStringLiteral("agent"));
    Q_INVOKABLE QVariantList pendingFeedback(const QString &agentId = QString()) const;
    Q_INVOKABLE bool approveFeedback(const QString &proposalId);
    Q_INVOKABLE bool rejectFeedback(const QString &proposalId);

    // Agrega métricas de RunHistoryStore para las Tasks vinculadas. El formato
    // del callback permite mantener este store desacoplado del store de historial.
    QVariantMap aggregateMetrics(
        const QString &agentId,
        const std::function<QVariantList(const QString &)> &historyProvider) const;

    static QJsonObject toJson(const QVariantMap &definition);
    static QVariantMap fromJson(const QJsonObject &object);
    static QVariantMap semanticSnapshot(const QVariantMap &definition);

signals:
    void countChanged();
    void changed();
    void feedbackChanged();

private:
    int indexOfId(const QString &id) const;
    int proposalIndex(const QString &proposalId) const;
    QString storagePath() const;
    void load();
    void persist() const;
    void appendRevision(QVariantMap &definition, const QString &reason,
                        const QVariantMap &snapshot);

    QList<QVariantMap> m_items;
    QList<QVariantMap> m_feedback;
};
