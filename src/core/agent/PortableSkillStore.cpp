#include "PortableSkillStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
constexpr qint64 kMaxSkillBytes = 256 * 1024;

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

bool isInside(const QString &path, const QString &root)
{
    const QFileInfo pathInfo(path);
    const QFileInfo rootInfo(root);
    const QString cleanPath = QDir::cleanPath(
        pathInfo.canonicalFilePath().isEmpty() ? pathInfo.absoluteFilePath()
                                               : pathInfo.canonicalFilePath());
    const QString cleanRoot = QDir::cleanPath(
        rootInfo.canonicalFilePath().isEmpty() ? rootInfo.absoluteFilePath()
                                               : rootInfo.canonicalFilePath());
    const QString relative = QDir(cleanRoot).relativeFilePath(cleanPath);
    return relative != QLatin1String("..")
        && !relative.startsWith(QStringLiteral("../"))
        && !QDir::isAbsolutePath(relative);
}
}

QString PortableSkillStore::globalRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("skills"));
}

QString PortableSkillStore::bundledRoot()
{
    const QString resource = QStringLiteral(":/assets/skills");
    if (QDir(resource).exists()) return resource;
#ifdef LC_BUNDLED_SKILLS_DIR
    return QDir::fromNativeSeparators(QStringLiteral(LC_BUNDLED_SKILLS_DIR));
#else
    return resource;
#endif
}

QString PortableSkillStore::projectRoot(const QString &workspace)
{
    if (workspace.trimmed().isEmpty()) return {};
    return QDir(workspace).filePath(QStringLiteral(".llamacode/skills"));
}

QVariantMap PortableSkillStore::parseFile(const QString &path, const QString &scope,
                                          const QString &expectedRoot, bool includeBody)
{
    QVariantMap out{{QStringLiteral("ok"), false}};
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0 || info.size() > kMaxSkillBytes
        || !isInside(info.absoluteFilePath(), expectedRoot)) {
        out[QStringLiteral("error")] = QStringLiteral("SKILL.md inválido o demasiado grande");
        return out;
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        out[QStringLiteral("error")] = QStringLiteral("No se pudo leer SKILL.md");
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
            || key == QLatin1String("version") || key == QLatin1String("author")) {
            meta[key] = value;
        }
    }

    const QString folderSlug = info.dir().dirName().toLower();
    QString name = meta.value(QStringLiteral("name")).toString().toLower();
    if (name.isEmpty()) name = folderSlug;
    if (!validSlug(name) || name != folderSlug) {
        out[QStringLiteral("error")] =
            QStringLiteral("El name debe coincidir con la carpeta y usar kebab-case");
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
    out[QStringLiteral("scope")] = scope;
    out[QStringLiteral("path")] = info.absoluteFilePath();
    if (includeBody) {
        const QString body = text.mid(end + 5).trimmed();
        if (body.isEmpty()) {
            out[QStringLiteral("ok")] = false;
            out[QStringLiteral("error")] = QStringLiteral("La habilidad no tiene instrucciones");
        } else {
            out[QStringLiteral("instructions")] = body;
        }
    }
    return out;
}

QVariantList PortableSkillStore::list(const QString &workspace)
{
    return list(workspace, HarnessSkillsModule{});
}

QVariantList PortableSkillStore::list(const QString &workspace,
                                      const HarnessSkillsModule &policy)
{
    // Global primero; proyecto reemplaza por nombre para permitir overrides locales.
    QMap<QString, QVariantMap> byName;
    const QList<QPair<QString, QString>> roots{
        {QStringLiteral("bundled"), bundledRoot()},
        {QStringLiteral("global"), globalRoot()},
        {QStringLiteral("project"), projectRoot(workspace)}
    };
    for (const auto &[scope, root] : roots) {
        if (root.isEmpty()) continue;
        const QDir dir(root);
        const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                        QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &entry : entries) {
            const QString file = QDir(entry.absoluteFilePath()).filePath(QStringLiteral("SKILL.md"));
            const QVariantMap skill = parseFile(file, scope, root, false);
            if (skill.value(QStringLiteral("ok")).toBool()
                && HarnessSkills::allows(policy, skill.value(QStringLiteral("name")).toString()))
                byName[skill.value(QStringLiteral("name")).toString()] = skill;
        }
    }
    QVariantList out;
    for (const QVariantMap &skill : byName) out << skill;
    return out;
}

QVariantMap PortableSkillStore::load(const QString &name, const QString &workspace)
{
    return load(name, workspace, HarnessSkillsModule{});
}

QVariantMap PortableSkillStore::load(const QString &name, const QString &workspace,
                                     const HarnessSkillsModule &policy)
{
    const QString wanted = name.trimmed().toLower();
    if (!validSlug(wanted))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Nombre inválido")}};
    if (!HarnessSkills::allows(policy, wanted))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Habilidad desactivada por el harness: %1")
                                                     .arg(wanted)}};

    // Proyecto tiene precedencia.
    const QList<QPair<QString, QString>> roots{
        {QStringLiteral("project"), projectRoot(workspace)},
        {QStringLiteral("global"), globalRoot()},
        {QStringLiteral("bundled"), bundledRoot()}
    };
    for (const auto &[scope, root] : roots) {
        if (root.isEmpty()) continue;
        const QString file = QDir(root).filePath(wanted + QStringLiteral("/SKILL.md"));
        const QVariantMap skill = parseFile(file, scope, root, true);
        if (skill.value(QStringLiteral("ok")).toBool()) return skill;
    }
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Habilidad no encontrada o inválida: %1").arg(wanted)}};
}
