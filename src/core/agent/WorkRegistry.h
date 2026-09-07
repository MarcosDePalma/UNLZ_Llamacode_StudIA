#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>

// Estado mutable y efímero del trabajo que está ocurriendo sobre un proyecto.
// No reemplaza AgentEventLog: este registro responde "quién está trabajando
// ahora" y el event log conserva el historial de lo que ocurrió.
//
// El store vive en <root>/.llamacode/active_work.json, usa un lock de proceso y
// elimina claims vencidas antes de cada lectura/escritura. Las rutas se guardan
// relativas al proyecto para que el mismo contrato funcione en Windows y Unix.
namespace WorkRegistry {

QString storePath(const QString &root);

// Crea una claim de trabajo. Devuelve un id vacío si no pudo persistirla.
QString acquire(const QString &root, const QString &sessionId,
                const QString &agentId, const QString &goal,
                const QStringList &paths = {},
                const QString &branch = QString(),
                const QString &worktree = QString(), int ttlSec = 900);

bool heartbeat(const QString &root, const QString &claimId,
               const QString &sessionId, int ttlSec = 900);

// Agrega rutas que el agente está por modificar a su claim existente.
bool addPaths(const QString &root, const QString &claimId,
              const QString &sessionId, const QStringList &paths,
              int ttlSec = 900);

// Reserva rutas de forma atómica. Si otra claim activa se solapa, no modifica
// la claim propia y devuelve el motivo formateado en conflictMessage.
bool claimPaths(const QString &root, const QString &claimId,
                const QString &sessionId, const QStringList &paths,
                QString *conflictMessage = nullptr, int ttlSec = 900);

bool release(const QString &root, const QString &claimId,
             const QString &sessionId, const QString &status = QStringLiteral("completed"));

// Devuelve claims activas, excluyendo opcionalmente la sesión indicada.
QJsonArray active(const QString &root, const QString &excludeSessionId = QString(),
                  qint64 nowMs = 0);

// Devuelve sólo claims de otras sesiones que comparten una ruta con `paths`.
QJsonArray conflicts(const QString &root, const QString &sessionId,
                     const QStringList &paths, qint64 nowMs = 0);

QString formatActive(const QString &root, const QString &excludeSessionId = QString(),
                     int maxClaims = 8);
QString formatConflicts(const QString &root, const QString &sessionId,
                        const QStringList &paths, int maxClaims = 5);

} // namespace WorkRegistry
