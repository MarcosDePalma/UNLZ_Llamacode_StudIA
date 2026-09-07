#pragma once
#include <QString>
#include <QStringList>
#include <QMap>

struct DetectedCapabilities {
    QStringList flags;
    QMap<QString, QString> flagAliases;
    QString version;
    QStringList kvTypes;
    bool success = false;
    QString error;

    bool hasFlag(const QString &f) const { return flags.contains(f); }
};

class CapabilityDetector
{
public:
    static DetectedCapabilities detect(const QString &binaryPath, int timeoutMs = 15000);

    // Parsea el texto de `--help` y extrae flags + alias. Público para tests
    // unitarios (lógica pura, sin proceso). Lo usa detect() internamente.
    static DetectedCapabilities parse(const QString &helpOutput);
    static DetectedCapabilities parseProbeOutput(const QString &versionOutput,
                                                 const QString &helpOutput);
};
