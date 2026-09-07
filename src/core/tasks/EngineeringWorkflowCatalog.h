#pragma once

#include <QVariantList>
#include <QVariantMap>

// Presets de workflows de ingeniería. Son definiciones declarativas para el
// motor de Tasks: no contienen nombres de aplicaciones ni coordenadas y pueden
// ejecutarse sobre cualquier workspace autorizado.
class EngineeringWorkflowCatalog
{
public:
    static QVariantList workflows();
    static QVariantList safetyProfiles();
    static QVariantMap workflow(const QString &id);
    static QVariantMap installableTask(const QString &id);
    static bool isKnownWorkflow(const QString &id);
};
