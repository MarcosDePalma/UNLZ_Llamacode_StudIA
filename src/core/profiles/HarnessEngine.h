#pragma once

#include "HarnessSpec.h"

#include <QVariantList>

// Catálogo pequeño y estable de motores. El motor no es una segunda app: es
// una selección de contrato dentro del mismo perfil, con almacenamiento y
// evidencia aislados para poder comparar y volver atrás sin migrar datos.
namespace HarnessEngine {

struct Descriptor {
    QString id;
    QString name;
    QString description;
    int version = 1;
    bool experimental = false;
    QString storageNamespace;
    QString fallbackId;
};

QString effectiveId(const HarnessRuntimeModule &runtime);
int effectiveVersion(const HarnessRuntimeModule &runtime);
bool isKnown(const QString &id);
QString storageNamespace(const QString &id);
QString fingerprint(const HarnessSpec &spec);
QVariantList catalog();

}  // namespace HarnessEngine
