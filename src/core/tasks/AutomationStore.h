#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>

// AutomationStore — catálogo de "Automatizaciones": cada fila enlaza un Proceso
// (definido en TaskStore) con una programación (scheduleSpec/cron). Un mismo
// proceso puede tener varias automatizaciones (one-to-many): diaria + mensual,
// etc. La definición operativa (prompt, perfil, modo, permisos) vive en el
// Proceso; acá solo se guarda el enlace + el horario + el estado de la última
// corrida.
//
// Persistencia: <AppLocalData>/automations/automations.json (respeta
// QStandardPaths::setTestModeEnabled para aislamiento en tests).
//
// (de)serialización y CRUD puros como en TaskStore; el TaskScheduler consume
// all() (mismas claves scheduleEnabled/scheduleSpec/scheduleCron/id que ya usa
// dueTaskIds, por lo que la selección es genérica).
class AutomationStore : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ProcessIdRole,
        ScheduleEnabledRole,
        ScheduleCronRole,
        ScheduleSpecRole,
        SilentUnlessErrorRole,
        CreatedAtRole,
        UpdatedAtRole,
        LastRunAtRole,
        LastRunStatusRole,
        LastRunSummaryRole
    };

    explicit AutomationStore(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }

    // CRUD. `def` usa las claves de roleNames. Devuelve el id (nuevo o existente).
    Q_INVOKABLE QString save(const QString &id, const QVariantMap &def);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE QVariantMap get(const QString &id) const;
    // Todas las automatizaciones como filas (para el scheduler).
    QVariantList all() const;
    Q_INVOKABLE void refresh();
    // Marca el resultado de una corrida (actualiza lastRunAt/lastRunStatus).
    Q_INVOKABLE void markRun(const QString &id, const QString &status, const QString &summary = QString());
    // Borra toda automatización que enlace a un proceso inexistente. Devuelve cuántas.
    int pruneOrphans(const QStringList &validProcessIds);

    static QString sanitize(const QString &name);
    static QJsonObject toJson(const QVariantMap &a);
    static QVariantMap fromJson(const QJsonObject &obj);

signals:
    void countChanged();
    void changed();

private:
    void load();
    void save() const;
    QString storagePath() const;
    int indexOfId(const QString &id) const;

    QList<QVariantMap> m_items;
};
