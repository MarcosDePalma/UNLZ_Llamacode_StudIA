#pragma once
#include "CatalogModel.h"
#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QHash>
#include <QPair>

class ModelCatalog : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString filterRootId READ filterRootId WRITE setFilterRootId NOTIFY filterChanged)
    Q_PROPERTY(QString filterFamily READ filterFamily WRITE setFilterFamily NOTIFY filterChanged)
    Q_PROPERTY(bool filterVisionOnly READ filterVisionOnly WRITE setFilterVisionOnly NOTIFY filterChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        RootIdRole,
        AbsolutePathRole,
        FileNameRole,
        SizeBytesRole,
        SizeLabelRole,
        MtimeRole,
        FamilyHintRole,
        QuantHintRole,
        QuantRealRole,
        TensorBreakdownRole,
        BpwRole,
        QuantMismatchRole,
        ArchitectureRole,
        ParameterCountRole,
        TrainedContextRole,
        IsVisionCandidateRole,
        IsDraftCandidateRole,
        IsAvailableRole
    };

    explicit ModelCatalog(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    // Re-open the DB and reload all rows into memory. Returns the loaded count.
    // Used to self-heal a transient empty load (e.g. DB was briefly locked by a
    // previous instance at startup).
    int reload();

    QString filterRootId() const { return m_filterRootId; }
    void setFilterRootId(const QString &id);

    QString filterFamily() const { return m_filterFamily; }
    void setFilterFamily(const QString &f);

    bool filterVisionOnly() const { return m_filterVisionOnly; }
    void setFilterVisionOnly(bool v);

    Q_INVOKABLE void addOrUpdate(const CatalogModel &model);
    Q_INVOKABLE void addBatch(const QList<CatalogModel> &models);
    Q_INVOKABLE void markRootUnavailable(const QString &rootId);
    // Tras un scan COMPLETO de un root: marca no disponibles las filas de ese root
    // que el scan ya no encontró en disco. Sin esto, un modelo borrado o movido
    // queda disponible para siempre y la UI lo sigue ofreciendo (y un perfil que
    // lo referencia falla recién al arrancar el server). `presentIds` son los ids
    // que devolvió el scan; si viene vacío no se toca nada, para que un scan
    // fallido no invalide el catálogo entero.
    void reconcileRoot(const QString &rootId, const QSet<QString> &presentIds);

private:
    // Ancla estable por archivo (identidad = nombre + tamaño). Reusa la de una fila
    // previa del mismo archivo; si es la primera vez, devuelve MAX+1.
    qint64 assignStableId(const CatalogModel &m);
    void backfillStableIds();
public:
    Q_INVOKABLE void removeByRootId(const QString &rootId);
    Q_INVOKABLE QVariantMap get(const QString &id) const;
    Q_INVOKABLE QVariantMap getAt(int row) const;
    // Override manual de compatibilidad, persistido aparte del scanner para que
    // un rescan no lo borre.
    Q_INVOKABLE bool setManualCompatibility(const QString &id, bool vision, bool draft);

    CatalogModel findById(const QString &id) const;
    // Resolución por ancla estable: sobrevive a que el gguf se mueva de carpeta,
    // cosa que el id textual (derivado de la ruta) no hace. Prefiere una fila
    // disponible si hay varias con el mismo ancla.
    CatalogModel findByStableId(qint64 stableId) const;
    QList<CatalogModel> allForRoot(const QString &rootId) const;

signals:
    void countChanged();
    void filterChanged();

private:
    bool openDb();
    void loadFromDb();
    void saveToDb(const CatalogModel &m);
    void rebuildVisible();

    QString dbPath() const;
    int indexOfId(const QString &id) const;
    bool matchesFilter(const CatalogModel &m) const;
    void loadManualCompatibility();
    void saveManualCompatibility() const;

    QList<CatalogModel> m_all;
    QList<const CatalogModel *> m_visible;

    QString m_filterRootId;
    QString m_filterFamily;
    bool m_filterVisionOnly = false;

    // SQLite connection name
    QString m_connName;
    QHash<QString, QPair<bool, bool>> m_manualCompatibility;
};
