#pragma once
#include "ModelRoot.h"
#include "ModelCatalog.h"
#include "GGUFScanner.h"
#include <QAbstractListModel>
#include <QList>

class ModelRootRegistry : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        PathRole,
        LabelRole,
        ScanModeRole,
        KindRole,
        EnabledRole,
        PriorityRole,
        TagsRole,
        IsOnlineRole
    };

    explicit ModelRootRegistry(ModelCatalog *catalog, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    bool scanning() const { return m_scanning; }

    Q_INVOKABLE QString add(const QString &path, const QString &label,
                            const QString &scanMode, const QStringList &tags);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool update(const QString &id, const QString &label,
                            const QString &scanMode, bool enabled,
                            int priority, const QStringList &tags);
    Q_INVOKABLE void scan(const QString &id);
    Q_INVOKABLE void scanAll();
    // Escanea sólo roots configurados para iniciar automáticamente. Se llama
    // después del primer pintado, no durante la construcción del controlador.
    void scanStartupRoots();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap get(const QString &id) const;

    ModelRoot findById(const QString &id) const;

signals:
    void countChanged();
    void scanningChanged();
    void scanStarted(const QString &rootId);
    void scanFinished(const QString &rootId, int modelsFound);
    void errorOccurred(const QString &message);

private:
    void load();
    void save() const;
    QString storagePath() const;
    int indexOfId(const QString &id) const;
    void doScan(const ModelRoot &root);

    QList<ModelRoot> m_items;
    ModelCatalog *m_catalog;
    GGUFScanner *m_scanner;
    bool m_scanning = false;
};
