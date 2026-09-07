#pragma once
#include "ProfileTypes.h"
#include "../LlamaBinary.h"
#include <QString>
#include <QList>
#include <QVariantList>

class ProfileManager;
class BinaryRegistry;
class ModelCatalog;

// Un problema detectado en un LaunchProfile (o en una entidad que referencia).
// severity: "error" (el perfil no puede lanzar) | "warning" (lanza, pero degradado).
// code: identificador estable máquina (ej "binary-missing") para UI/headless.
struct HealthIssue {
    QString severity;   // "error" | "warning"
    QString launchId;   // launch afectado
    QString entity;     // "backend" | "binary" | "model" | "mmproj" | "draft" | "runtime" | "agent" | "cloud"
    QString code;       // slug estable
    QString message;    // descripción legible
    QString fix;        // acción sugerida

    QVariantMap toMap() const;
};

// Validador de salud de perfiles (inspirado en el health-check de LazySkills:
// detecta instalaciones rotas y explica el porqué + el arreglo). Núcleo PURO y
// testeable: `checkLaunch` recibe las referencias ya resueltas (Refs) y no toca
// disco ni registries. `checkAll` es un wrapper delgado que resuelve cada launch
// contra ProfileManager + BinaryRegistry + ModelCatalog y acumula los issues.
class ProfileHealthChecker
{
public:
    // Referencias de un LaunchProfile ya resueltas. Los flags *Found indican si el
    // id referenciado existe en su registro; los *FileExists si el archivo del
    // modelo está presente en disco (catálogo IsAvailable).
    struct Refs {
        LaunchProfile launch;

        bool           backendFound = false;
        BackendProfile backend;

        bool        binaryFound = false;
        LlamaBinary binary;

        bool         modelRefFound = false;   // modelProfileId existe
        ModelProfile model;
        bool         modelFileExists = false; // el .gguf del modelo está en disco
        QString      modelFileName;           // para detectar MTP autocontenido

        bool mmprojFileExists = false;
        bool draftFileExists = false;

        bool         runtimeFound = false;
        bool         agentRefFound = false;   // agentProfileId existe (si no vacío)
    };

    // Núcleo puro: evalúa un launch resuelto y devuelve sus issues (vacío = sano).
    static QList<HealthIssue> checkLaunch(const Refs &r);

    // Resuelve TODOS los launches de usuario+sistema y acumula issues.
    static QList<HealthIssue> checkAll(ProfileManager *profiles,
                                       BinaryRegistry *binaries,
                                       ModelCatalog *catalog);

    // Conveniencia para QML/headless (ControlApi espeja Q_INVOKABLE).
    static QVariantList checkAllAsVariant(ProfileManager *profiles,
                                          BinaryRegistry *binaries,
                                          ModelCatalog *catalog);
};
