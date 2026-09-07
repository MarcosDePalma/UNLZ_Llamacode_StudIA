#include "LlamaBinary.h"
#include <QJsonArray>
#include <QUuid>
#include <QFileInfo>

bool LlamaBinary::supportsFlag(const QString &flag) const
{
    return supportedFlags.contains(flag);
}

QString LlamaBinary::resolveFlag(const QString &flag) const
{
    // For long flags, prefer the caller-provided spelling unless a safe alias is needed.
    // This prevents accidental inversions like --no-context-shift -> --context-shift.
    if (flag.startsWith("--")) {
        if (supportedFlags.contains(flag))
            return flag;
        const QString resolved = flagAliases.value(flag, flag);
        if (flag.startsWith("--no-") && !resolved.startsWith("--no-"))
            return flag;
        return resolved;
    }
    return flagAliases.value(flag, flag);
}

QJsonObject LlamaBinary::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["path"] = path;
    obj["flavor"] = flavor;
    obj["backend"] = backend;
    obj["versionHint"] = versionHint;
    obj["supportedFlags"] = QJsonArray::fromStringList(supportedFlags);
    obj["kvTypes"] = QJsonArray::fromStringList(kvTypes);
    obj["conflictingFlags"] = QJsonArray::fromStringList(conflictingFlags);
    obj["workingDirectory"] = workingDirectory;
    obj["binaryHash"] = binaryHash;

    QJsonObject aliases;
    for (auto it = flagAliases.begin(); it != flagAliases.end(); ++it)
        aliases[it.key()] = it.value();
    obj["flagAliases"] = aliases;

    QJsonObject envDef;
    for (auto it = envDefaults.begin(); it != envDefaults.end(); ++it)
        envDef[it.key()] = it.value();
    obj["envDefaults"] = envDef;

    return obj;
}

LlamaBinary LlamaBinary::fromJson(const QJsonObject &obj)
{
    LlamaBinary b;
    b.id = obj["id"].toString();
    b.name = obj["name"].toString();
    b.path = obj["path"].toString();
    b.flavor = obj["flavor"].toString("official");
    b.backend = obj["backend"].toString("cpu");
    b.versionHint = obj["versionHint"].toString();
    b.workingDirectory = obj["workingDirectory"].toString();
    b.binaryHash = obj["binaryHash"].toString();
    b.pathValid = QFileInfo::exists(b.path);

    for (const auto &v : obj["supportedFlags"].toArray())
        b.supportedFlags.append(v.toString());
    for (const auto &v : obj["kvTypes"].toArray())
        b.kvTypes.append(v.toString());
    for (const auto &v : obj["conflictingFlags"].toArray())
        b.conflictingFlags.append(v.toString());

    const QJsonObject aliases = obj["flagAliases"].toObject();
    for (auto it = aliases.begin(); it != aliases.end(); ++it)
        b.flagAliases[it.key()] = it.value().toString();

    const QJsonObject envDef = obj["envDefaults"].toObject();
    for (auto it = envDef.begin(); it != envDef.end(); ++it)
        b.envDefaults[it.key()] = it.value().toString();

    return b;
}

QString LlamaBinary::generateId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
