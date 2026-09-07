#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// Catálogo pequeño y configurable de roles de modelo. No conoce aplicaciones
// concretas: sólo describe qué capacidad auxiliar se prefiere, su fallback y
// cómo debe reservarse el recurso en AuxiliaryJobScheduler.
class ModelRoleRegistry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList roles READ snapshot NOTIFY rolesChanged)
public:
    explicit ModelRoleRegistry(QObject *parent = nullptr);

    Q_INVOKABLE QVariantList snapshot() const;
    Q_INVOKABLE QVariantMap role(const QString &roleId) const;
    Q_INVOKABLE bool setRole(const QString &roleId, const QVariantMap &values);
    Q_INVOKABLE bool resetRole(const QString &roleId);

    QString resolveModel(const QString &roleId) const;
    QVariantMap schedulingHint(const QString &roleId) const;
    static QVariantList defaultRoles();
    static QString storagePath();

signals:
    void rolesChanged();

private:
    static QString normalizeRoleId(const QString &roleId);
    static QVariantMap normalizeRole(const QVariantMap &role);
    void load();
    void save() const;

    QVariantMap m_roles;
};
