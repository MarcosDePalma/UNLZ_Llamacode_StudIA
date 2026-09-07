#include "HarnessCapabilitySnapshot.h"

#include <QSet>
#include <QUuid>

QJsonObject HarnessCapabilityGrant::toJson() const
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("handle"), handle},
            {QStringLiteral("reason"), reason},
            {QStringLiteral("generation"), generation},
            {QStringLiteral("granted"), granted}};
}

HarnessCapabilityGrant HarnessCapabilityGrant::fromJson(const QJsonObject &object)
{
    HarnessCapabilityGrant grant;
    grant.name = object.value(QStringLiteral("name")).toString();
    grant.handle = object.value(QStringLiteral("handle")).toString();
    grant.reason = object.value(QStringLiteral("reason")).toString();
    grant.generation = object.value(QStringLiteral("generation")).toInt();
    grant.granted = object.value(QStringLiteral("granted")).toBool(false);
    return grant;
}

HarnessCapabilitySnapshot HarnessCapabilitySnapshot::admit(
    const QString &activationId, const QString &engineId, const QString &profileId,
    int generation, const QStringList &requested, const QStringList &allowed)
{
    HarnessCapabilitySnapshot snapshot;
    snapshot.activationId = activationId;
    snapshot.engineId = engineId.trimmed().isEmpty() ? QStringLiteral("legacy") : engineId;
    snapshot.profileId = profileId;
    snapshot.generation = qMax(1, generation);
    const QSet<QString> admitted(allowed.cbegin(), allowed.cend());
    for (const QString &raw : requested) {
        const QString name = raw.trimmed();
        if (name.isEmpty() || snapshot.grants.contains(name)) continue;
        HarnessCapabilityGrant grant;
        grant.name = name;
        grant.generation = snapshot.generation;
        grant.granted = admitted.contains(name);
        grant.reason = grant.granted ? QStringLiteral("admitted")
                                      : QStringLiteral("denied_by_policy");
        if (grant.granted)
            grant.handle = QStringLiteral("cap:%1:%2")
                               .arg(snapshot.activationId, QUuid::createUuid().toString(
                                                                   QUuid::WithoutBraces));
        snapshot.grants.insert(name, grant);
    }
    return snapshot;
}

bool HarnessCapabilitySnapshot::canUse(const QString &name) const
{
    const auto it = grants.constFind(name.trimmed());
    return it != grants.cend() && it->granted && it->generation == generation
           && !it->handle.isEmpty();
}

QString HarnessCapabilitySnapshot::handleFor(const QString &name) const
{
    return canUse(name) ? grants.value(name.trimmed()).handle : QString();
}

QStringList HarnessCapabilitySnapshot::grantedNames() const
{
    QStringList names;
    for (auto it = grants.cbegin(); it != grants.cend(); ++it)
        if (it->granted && it->generation == generation) names << it.key();
    return names;
}

QJsonObject HarnessCapabilitySnapshot::toJson() const
{
    QJsonObject serialized{{QStringLiteral("activationId"), activationId},
                           {QStringLiteral("engineId"), engineId},
                           {QStringLiteral("profileId"), profileId},
                           {QStringLiteral("generation"), generation}};
    QJsonObject grantObject;
    for (auto it = grants.cbegin(); it != grants.cend(); ++it)
        grantObject[it.key()] = it.value().toJson();
    serialized[QStringLiteral("grants")] = grantObject;
    return serialized;
}

HarnessCapabilitySnapshot HarnessCapabilitySnapshot::fromJson(const QJsonObject &object)
{
    HarnessCapabilitySnapshot snapshot;
    snapshot.activationId = object.value(QStringLiteral("activationId")).toString();
    snapshot.engineId = object.value(QStringLiteral("engineId"))
                            .toString(QStringLiteral("legacy"));
    snapshot.profileId = object.value(QStringLiteral("profileId")).toString();
    snapshot.generation = qMax(1, object.value(QStringLiteral("generation")).toInt(1));
    const QJsonObject grantObject = object.value(QStringLiteral("grants")).toObject();
    for (auto it = grantObject.constBegin(); it != grantObject.constEnd(); ++it)
        if (it.value().isObject())
            snapshot.grants.insert(it.key(), HarnessCapabilityGrant::fromJson(it.value().toObject()));
    return snapshot;
}
