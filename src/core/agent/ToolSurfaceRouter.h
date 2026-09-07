#pragma once

#include <QVariantList>
#include <QVariantMap>
#include <QStringList>

// Decide la superficie inicial de tools por intención, sin conocer apps,
// controles ni layouts concretos. Ante ambigüedad falla abierto.
class ToolSurfaceRouter
{
public:
    struct Decision {
        QStringList groups;
        double confidence = 0.0;
        bool fullSurface = true;
        QString reason;
        QVariantMap toVariantMap() const;
    };

    static Decision decide(const QString &prompt, const QVariantList &catalog);
    static QString canonicalGroup(const QString &label);
    static bool groupAllowed(const QString &group, const Decision &decision);
};
