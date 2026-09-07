#pragma once
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>

struct LlamaBinary {
    QString id;
    QString name;
    QString path;
    QString flavor;       // "official", "mtp-fork", "custom"
    QString backend;      // "cuda", "vulkan", "cpu", "metal"
    QString versionHint;
    QStringList supportedFlags;
    QStringList kvTypes;
    QMap<QString, QString> flagAliases;  // alias -> canonical
    QStringList conflictingFlags;
    QMap<QString, QString> envDefaults;
    QString workingDirectory;
    QString binaryHash;   // SHA256 hex of executable
    bool pathValid = false;

    bool supportsFlag(const QString &flag) const;
    QString resolveFlag(const QString &flag) const;  // alias resolution

    QJsonObject toJson() const;
    static LlamaBinary fromJson(const QJsonObject &obj);
    static QString generateId();
};
