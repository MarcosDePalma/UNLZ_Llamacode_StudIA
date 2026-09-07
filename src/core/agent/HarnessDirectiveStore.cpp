#include "HarnessDirectiveStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

namespace {

bool validSlug(const QString &value)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9][a-z0-9-]{0,63}$"));
    return re.match(value).hasMatch();
}

QString unquote(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2
        && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
            || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\'')))) {
        value = value.mid(1, value.size() - 2);
    }
    return value.trimmed();
}

// Parseo de un .md de directiva. Devuelve {ok,...} siempre: una directiva rota
// NO puede tirar el arranque del agente, sólo quedar fuera con su error visible.
QVariantMap parseFile(const QString &path, const QString &scope, bool includeBody)
{
    QVariantMap out{{QStringLiteral("ok"), false}};
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0
        || info.size() > HarnessDirectiveStore::kMaxDirectiveBytes) {
        out[QStringLiteral("error")] = QStringLiteral("Directiva inexistente o demasiado grande");
        return out;
    }
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        out[QStringLiteral("error")] = QStringLiteral("No se pudo leer la directiva");
        return out;
    }
    QString text = QString::fromUtf8(file.readAll());
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (!text.startsWith(QStringLiteral("---\n"))) {
        out[QStringLiteral("error")] = QStringLiteral("Falta frontmatter YAML");
        return out;
    }
    const int end = text.indexOf(QStringLiteral("\n---\n"), 4);
    if (end < 0) {
        out[QStringLiteral("error")] = QStringLiteral("Frontmatter YAML incompleto");
        return out;
    }

    QVariantMap meta;
    const QStringList lines = text.mid(4, end - 4).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty() || line.trimmed().startsWith(QLatin1Char('#'))) continue;
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0) continue;
        const QString key = line.left(colon).trimmed();
        const QString value = unquote(line.mid(colon + 1));
        if (key == QLatin1String("name") || key == QLatin1String("description")
            || key == QLatin1String("when") || key == QLatin1String("author")) {
            meta[key] = value;
        }
    }

    const QString fileSlug = info.completeBaseName().toLower();
    QString name = meta.value(QStringLiteral("name")).toString().toLower();
    if (name.isEmpty()) name = fileSlug;
    if (!validSlug(name) || name != fileSlug) {
        out[QStringLiteral("error")] =
            QStringLiteral("El name debe coincidir con el archivo y usar kebab-case");
        return out;
    }
    const QString description = meta.value(QStringLiteral("description")).toString().trimmed();
    if (description.isEmpty() || description.size() > 1024) {
        out[QStringLiteral("error")] = QStringLiteral("description es obligatoria (máx. 1024)");
        return out;
    }

    out = meta;
    out[QStringLiteral("ok")] = true;
    out[QStringLiteral("name")] = name;
    out[QStringLiteral("description")] = description;
    out[QStringLiteral("when")] = meta.value(QStringLiteral("when")).toString();
    out[QStringLiteral("scope")] = scope;
    out[QStringLiteral("path")] = info.absoluteFilePath();
    out[QStringLiteral("bytes")] = static_cast<int>(info.size());
    if (includeBody) {
        const QString body = text.mid(end + 5).trimmed();
        if (body.isEmpty()) {
            out[QStringLiteral("ok")] = false;
            out[QStringLiteral("error")] = QStringLiteral("La directiva no tiene contenido");
        } else {
            out[QStringLiteral("body")] = body;
        }
    }
    return out;
}

QList<QPair<QString, QString>> rootsFor(const QString &workspace)
{
    return {{QStringLiteral("global"), HarnessDirectiveStore::globalRoot()},
            {QStringLiteral("project"), HarnessDirectiveStore::projectRoot(workspace)}};
}

}  // namespace

QString HarnessDirectiveStore::globalRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("harness/directives"));
}

QString HarnessDirectiveStore::projectRoot(const QString &workspace)
{
    if (workspace.trimmed().isEmpty()) return {};
    return QDir(workspace).filePath(QStringLiteral(".llamacode/directives"));
}

QVariantList HarnessDirectiveStore::list(const QString &workspace)
{
    seedBundledExamples();
    // Global primero; la de proyecto pisa por nombre (override local).
    QMap<QString, QVariantMap> byName;
    for (const auto &[scope, root] : rootsFor(workspace)) {
        if (root.isEmpty()) continue;
        const QFileInfoList entries = QDir(root).entryInfoList(
            QStringList{QStringLiteral("*.md")}, QDir::Files, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &entry : entries) {
            const QVariantMap parsed = parseFile(entry.absoluteFilePath(), scope, false);
            if (!parsed.value(QStringLiteral("ok")).toBool()) continue;
            byName.insert(parsed.value(QStringLiteral("name")).toString(), parsed);
        }
    }
    QVariantList out;
    for (const QVariantMap &m : byName) out.append(m);
    return out;
}

QVariantMap HarnessDirectiveStore::load(const QString &name, const QString &workspace)
{
    const QString slug = name.trimmed().toLower();
    if (!validSlug(slug))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Nombre de directiva inválido")}};
    // Proyecto primero: un override local gana sobre la global.
    const auto roots = rootsFor(workspace);
    for (auto it = roots.crbegin(); it != roots.crend(); ++it) {
        if (it->second.isEmpty()) continue;
        const QString path = QDir(it->second).filePath(slug + QStringLiteral(".md"));
        if (!QFileInfo::exists(path)) continue;
        return parseFile(path, it->first, true);
    }
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Directiva no encontrada: %1").arg(slug)}};
}

namespace {
// Raiz de un scope, o vacio si el scope pide workspace y no hay.
QString rootForScope(const QString &scope, const QString &workspace)
{
    if (scope == QLatin1String("project"))
        return HarnessDirectiveStore::projectRoot(workspace);
    return HarnessDirectiveStore::globalRoot();
}
}  // namespace

QVariantMap HarnessDirectiveStore::save(const QString &name, const QString &description,
                                        const QString &when, const QString &body,
                                        const QString &scope, const QString &workspace)
{
    const QString slug = name.trimmed().toLower();
    if (!validSlug(slug))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"),
                 QStringLiteral("El nombre debe ser kebab-case (a-z, 0-9, guiones)")}};
    const QString desc = description.trimmed();
    if (desc.isEmpty() || desc.size() > 1024)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("description es obligatoria (máx. 1024)")}};
    if (body.trimmed().isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("La directiva no tiene contenido")}};
    // El frontmatter es delimitado por líneas "---": un valor multilínea rompería
    // el archivo. Se rechaza acá en vez de escribir algo que después no carga.
    if (desc.contains(QLatin1Char('\n')) || when.contains(QLatin1Char('\n')))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("description y when son de una sola línea")}};

    const QString root = rootForScope(scope, workspace);
    if (root.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Scope 'project' sin workspace abierto")}};

    QString text = QStringLiteral("---\nname: %1\ndescription: %2\n").arg(slug, desc);
    if (!when.trimmed().isEmpty())
        text += QStringLiteral("when: %1\n").arg(when.trimmed());
    text += QStringLiteral("---\n") + body.trimmed() + QLatin1Char('\n');

    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() > kMaxDirectiveBytes)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"),
                 QStringLiteral("La directiva supera %1 KB").arg(kMaxDirectiveBytes / 1024)}};

    QDir().mkpath(root);
    const QString path = QDir(root).filePath(slug + QStringLiteral(".md"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("No se pudo escribir %1").arg(path)}};
    f.write(utf8);
    f.close();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path},
            {QStringLiteral("name"), slug}, {QStringLiteral("scope"), scope}};
}

QVariantMap HarnessDirectiveStore::remove(const QString &name, const QString &scope,
                                          const QString &workspace)
{
    const QString slug = name.trimmed().toLower();
    if (!validSlug(slug))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Nombre de directiva inválido")}};
    const QString root = rootForScope(scope, workspace);
    if (root.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Scope 'project' sin workspace abierto")}};
    const QString path = QDir(root).filePath(slug + QStringLiteral(".md"));
    if (!QFileInfo::exists(path))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("No existe: %1").arg(slug)}};
    if (!QFile::remove(path))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("No se pudo borrar %1").arg(path)}};
    return {{QStringLiteral("ok"), true}};
}

// Copia las directivas bundleadas a la raíz global la PRIMERA vez (si el usuario
// no tiene ninguna). Sirven de plantilla y de smoke del descubrimiento: si la
// sección aparece vacía, es que el catálogo no está leyendo la carpeta.
void HarnessDirectiveStore::seedBundledExamples()
{
    const QString root = globalRoot();
    QDir().mkpath(root);
    // Marcador de sembrado: si el criterio fuera "la carpeta está vacía", borrar
    // la única directiva propia haría reaparecer el ejemplo. Se siembra una vez
    // y listo; borrarlo es una decisión del usuario que hay que respetar.
    const QString stamp = QDir(root).filePath(QStringLiteral(".seeded"));
    if (QFileInfo::exists(stamp)) return;

    // Copiar PRIMERO y marcar después: al revés, una copia fallida (permisos,
    // disco lleno) dejaba el marcador puesto y el usuario se quedaba sin el
    // ejemplo para siempre, sin explicación y sin reintento.
    const QDir bundled(QStringLiteral(":/assets/harness/directives"));
    if (!bundled.exists()) return;
    bool allCopied = true;
    for (const QFileInfo &entry : bundled.entryInfoList(QStringList{QStringLiteral("*.md")},
                                                        QDir::Files)) {
        const QString dest = QDir(root).filePath(entry.fileName());
        if (QFileInfo::exists(dest)) continue;
        if (!QFile::copy(entry.absoluteFilePath(), dest)) {
            allCopied = false;
            continue;
        }
        // El qrc viene read-only: sin esto el usuario no puede editar la copia.
        QFile(dest).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    if (!allCopied) return;   // sin marcador: se reintenta en el próximo list()

    QFile stampFile(stamp);
    if (stampFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        stampFile.write("bundled directives seeded\n");
        stampFile.close();
    }
}

QVariantList HarnessDirectiveStore::loadMany(const QStringList &names, const QString &workspace)
{
    QVariantList out;
    for (const QString &name : names) {
        const QVariantMap d = load(name, workspace);
        if (!d.value(QStringLiteral("ok")).toBool()) continue;
        out.append(QVariantMap{{QStringLiteral("slug"), d.value(QStringLiteral("name"))},
                               {QStringLiteral("body"), d.value(QStringLiteral("body"))},
                               {QStringLiteral("when"), d.value(QStringLiteral("when"))}});
    }
    return out;
}
