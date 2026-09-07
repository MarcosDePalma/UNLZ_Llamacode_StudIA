#pragma once
#include <QString>
#include <QStringList>

// Modo "teach" de browser: el usuario GRABA acciones (Playwright codegen) que se
// guardan como un "skill" reproducible (.mjs). El agente luego lo REPRODUCE.
// Complementa al MCP Playwright (tools en vivo): acá la novedad es grabar/reusar.
//
// Almacenamiento: <AppLocalData>/browser_skills/ (respeta
// QStandardPaths::setTestModeEnabled para aislamiento en tests). El directorio se
// gestiona como un paquete npm con 'playwright' como dependencia local, así tanto
// codegen como el replay (node skill.mjs con cwd=dir) resuelven el módulo.
//
// Las funciones de path/list/sanitize/argv son PURAS (sin red ni proceso) → unit
// test. La grabación/replay reales (QProcess + browser) viven en AppController /
// AgentToolRunner y no se cubren en unit (dependen de browser instalado).
class BrowserTeach
{
public:
    // Directorio de skills (lo crea si no existe).
    static QString skillsDir();
    // Ruta absoluta del .mjs de un skill (nombre saneado).
    static QString skillPath(const QString &name);
    // Skills disponibles (basenames sin extensión, ordenados).
    static QStringList listSkills();
    static bool hasSkill(const QString &name);
    static bool removeSkill(const QString &name);

    // Nombre → slug seguro para filename (minúsculas, [a-z0-9_-]).
    static QString sanitize(const QString &name);

    // Directorio de perfil de browser persistente del skill (cookies/localStorage/
    // sesión). Codegen graba con --user-data-dir acá → Playwright genera el .mjs con
    // launchPersistentContext(<dir>), así el replay reusa el login (no re-loguear en
    // cada corrida). Lo crea si no existe. "" si el nombre no da slug válido.
    static QString profileDir(const QString &name);

    // Comando de shell (para `cmd /c` en Win / `sh -c`) que graba el skill con
    // Playwright codegen. Al cerrar el inspector se escribe el .mjs.
    static QString recordCommand(const QString &name, const QString &url);
    // Programa + args para reproducir el skill: {"node", "<skillPath>"}.
    // Ejecutar con cwd = skillsDir() para que resuelva 'playwright' local.
    static QStringList replayProgramArgs(const QString &name);
    // true si el runtime npm (node_modules/playwright) ya está instalado en el dir.
    static bool runtimeReady();
};
