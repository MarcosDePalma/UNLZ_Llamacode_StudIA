#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "core/profiles/HarnessSpec.h"

// Catálogo de habilidades portables estilo Agent Skills. Descubre SKILL.md en:
//   <AppLocalData>/skills/<slug>/SKILL.md
//   <workspace>/.llamacode/skills/<slug>/SKILL.md
// Sólo expone metadata hasta que skill_load solicita el cuerpo completo.
class PortableSkillStore
{
public:
    static QString globalRoot();
    static QString bundledRoot();
    static QString projectRoot(const QString &workspace);

    static QVariantList list(const QString &workspace = QString());
    static QVariantList list(const QString &workspace, const HarnessSkillsModule &policy);
    static QVariantMap load(const QString &name, const QString &workspace = QString());
    static QVariantMap load(const QString &name, const QString &workspace,
                            const HarnessSkillsModule &policy);

private:
    static QVariantMap parseFile(const QString &path, const QString &scope,
                                 const QString &expectedRoot, bool includeBody);
};
