#include "BrowserTeach.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

QString BrowserTeach::skillsDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/browser_skills");
    QDir().mkpath(dir);
    return dir;
}

QString BrowserTeach::sanitize(const QString &name)
{
    QString s;
    s.reserve(name.size());
    for (const QChar c : name.toLower()) {
        if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
            (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
            c == QLatin1Char('_') || c == QLatin1Char('-'))
            s.append(c);
        else if (c == QLatin1Char(' ') || c == QLatin1Char('.') || c == QLatin1Char('/'))
            s.append(QLatin1Char('-'));
        // resto: se descarta
    }
    while (s.contains(QStringLiteral("--"))) s.replace(QStringLiteral("--"), QStringLiteral("-"));
    if (s.startsWith(QLatin1Char('-'))) s.remove(0, 1);
    if (s.endsWith(QLatin1Char('-')))   s.chop(1);
    return s;
}

QString BrowserTeach::profileDir(const QString &name)
{
    const QString slug = sanitize(name);
    if (slug.isEmpty()) return QString();
    const QString dir = skillsDir() + QStringLiteral("/profiles/") + slug;
    QDir().mkpath(dir);
    return dir;
}

QString BrowserTeach::skillPath(const QString &name)
{
    const QString slug = sanitize(name);
    if (slug.isEmpty()) return QString();
    return skillsDir() + QLatin1Char('/') + slug + QStringLiteral(".mjs");
}

QStringList BrowserTeach::listSkills()
{
    QDir d(skillsDir());
    QStringList out;
    for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("*.mjs")}, QDir::Files, QDir::Name))
        out << fi.completeBaseName();
    return out;
}

bool BrowserTeach::hasSkill(const QString &name)
{
    const QString p = skillPath(name);
    return !p.isEmpty() && QFileInfo::exists(p);
}

bool BrowserTeach::removeSkill(const QString &name)
{
    const QString p = skillPath(name);
    if (p.isEmpty() || !QFileInfo::exists(p)) return false;
    return QFile::remove(p);
}

QString BrowserTeach::recordCommand(const QString &name, const QString &url)
{
    const QString p = skillPath(name);
    if (p.isEmpty()) return QString();
    // Playwright codegen: abre browser + inspector, graba las acciones y al cerrar
    // escribe el script en -o. --target=javascript = script plano con require('playwright').
    // --user-data-dir: perfil persistente → el .mjs generado usa launchPersistentContext
    // y el replay reusa la sesión logueada (no re-loguear en cada corrida).
    QString cmd = QStringLiteral("npx playwright codegen --target=javascript "
                                 "--user-data-dir=\"%1\" -o \"%2\"")
                      .arg(profileDir(name), p);
    const QString u = url.trimmed();
    if (!u.isEmpty() && u.startsWith(QLatin1String("http")))
        cmd += QLatin1Char(' ') + u;
    return cmd;
}

QStringList BrowserTeach::replayProgramArgs(const QString &name)
{
    const QString p = skillPath(name);
    if (p.isEmpty()) return {};
    return {QStringLiteral("node"), p};
}

bool BrowserTeach::runtimeReady()
{
    return QFileInfo::exists(skillsDir() + QStringLiteral("/node_modules/playwright"));
}
