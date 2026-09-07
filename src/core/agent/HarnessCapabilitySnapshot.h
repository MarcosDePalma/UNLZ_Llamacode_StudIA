#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

struct HarnessCapabilityGrant {
    QString name;
    QString handle;
    QString reason;
    int generation = 0;
    bool granted = false;

    QJsonObject toJson() const;
    static HarnessCapabilityGrant fromJson(const QJsonObject &object);
};

// Snapshot inmutable por activación. Un pedido no concede nada: sólo los
// nombres presentes en grants con granted=true pueden convertirse en handles.
class HarnessCapabilitySnapshot final {
public:
    QString activationId;
    QString engineId = QStringLiteral("legacy");
    QString profileId;
    int generation = 1;
    QMap<QString, HarnessCapabilityGrant> grants;

    static HarnessCapabilitySnapshot admit(const QString &activationId,
                                           const QString &engineId,
                                           const QString &profileId,
                                           int generation,
                                           const QStringList &requested,
                                           const QStringList &allowed);
    bool canUse(const QString &name) const;
    QString handleFor(const QString &name) const;
    QStringList grantedNames() const;
    QJsonObject toJson() const;
    static HarnessCapabilitySnapshot fromJson(const QJsonObject &object);
};
