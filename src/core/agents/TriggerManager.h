#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QVariantMap>

// TriggerManager normaliza fuentes externas a un evento común y las enruta a una
// Task. Filesystem funciona de forma nativa; webhook/appEvent se inyectan mediante
// dispatchEvent (ControlApi, conectores o UI pueden usar el mismo contrato).
class TriggerManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1, NameRole, AgentIdRole, TaskIdRole,
        TypeRole, EnabledRole, ConfigRole, DebounceMsRole, LastFiredAtRole
    };

    explicit TriggerManager(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_items.size(); }

    Q_INVOKABLE QString save(const QString &id, const QVariantMap &definition);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE QVariantMap get(const QString &id) const;
    Q_INVOKABLE QVariantList all() const;
    Q_INVOKABLE QVariantList dispatchEvent(const QString &type,
                                           const QVariantMap &event);
    Q_INVOKABLE QStringList watchedPaths() const;

    static bool matches(const QVariantMap &trigger, const QString &type,
                        const QVariantMap &event);

signals:
    void countChanged();
    void changed();
    void taskRequested(const QString &taskId, const QString &triggerId,
                       const QVariantMap &event);

private:
    int indexOfId(const QString &id) const;
    QString storagePath() const;
    void load();
    void persist() const;
    void rebuildWatchers();
    void onPathChanged(const QString &path);

    QList<QVariantMap> m_items;
    QFileSystemWatcher m_watcher;
    QHash<QString, qint64> m_lastFireMs;
};
