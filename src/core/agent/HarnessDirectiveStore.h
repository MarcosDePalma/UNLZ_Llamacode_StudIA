#pragma once
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Directivas de usuario para el system prompt (packs del harness modular).
// Misma convención que PortableSkillStore, pero el cuerpo se inyecta en el
// prompt en vez de cargarse bajo demanda:
//   <AppLocalData>/harness/directives/<slug>.md
//   <workspace>/.llamacode/directives/<slug>.md      (pisa a la global)
//
// Frontmatter YAML mínimo:
//   ---
//   name: mi-directiva          (debe coincidir con el archivo, kebab-case)
//   description: una línea      (obligatoria; la ve el editor)
//   when: tools.desktop         (opcional; gate declarativo, ver
//                                LlamaAgentBackend::directiveConditionMet)
//   ---
//   <cuerpo que se inyecta>
//
// El cuerpo tiene tope de tamaño: una directiva de 8 KB es una regresión de
// contexto silenciosa, y el perfil ya paga tokens por cada tool que habilita.
namespace HarnessDirectiveStore {

QString globalRoot();
QString projectRoot(const QString &workspace);

// Metadata de todas las directivas visibles (sin cuerpo): {name, description,
// when, scope, path, bytes}. Ordenadas por nombre.
QVariantList list(const QString &workspace = QString());

// Una directiva con cuerpo: agrega {body}. `ok=false` + `error` si no existe o
// es inválida.
QVariantMap load(const QString &name, const QString &workspace = QString());

// Carga varias por slug, en el orden pedido, salteando las inválidas. Devuelve
// {slug, body, when} — el formato que consume LlamaAgentBackend::setCustomDirectives.
QVariantList loadMany(const QStringList &names, const QString &workspace = QString());

// Alta/edición de una directiva. scope: "global" | "project" (necesita
// `workspace`). Valida lo mismo que el parseo (slug kebab-case, description
// obligatoria, tope de bytes) ANTES de escribir: un archivo que después no se
// puede cargar sería peor que un error temprano. Devuelve {ok, path} o
// {ok:false, error}.
QVariantMap save(const QString &name, const QString &description, const QString &when,
                 const QString &body, const QString &scope = QStringLiteral("global"),
                 const QString &workspace = QString());

// Baja. Devuelve {ok} o {ok:false, error}. No toca el spec de los perfiles que
// la referencian: una directiva ausente ya degrada con warning (no rompe).
QVariantMap remove(const QString &name, const QString &scope = QStringLiteral("global"),
                   const QString &workspace = QString());

// Directiva de ejemplo bundleada (assets/harness/directives). Se copia a la raíz
// global la primera vez que se lista, si el usuario no tiene ninguna: sin esto
// la sección arranca vacía y nadie sabe qué se espera ahí.
void seedBundledExamples();

constexpr qint64 kMaxDirectiveBytes = 32 * 1024;

}  // namespace HarnessDirectiveStore
