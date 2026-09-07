#pragma once

#include <QJsonObject>
#include <QString>

// Bitacora append-only del agente nativo. Complementa MemoryStore/GraphStore:
// aca no guardamos "verdades" consolidadas, sino evidencia operacional tipada
// de lo que el agente intento, rechazo, ejecuto y observo.
namespace AgentEventLog {

QString jsonlPath(const QString &cwd);

// Agrega metadata comun (kind/id/ts/sessionId) y persiste un objeto JSONL.
// Devuelve true si pudo escribir. El log es mejor-esfuerzo: nunca debe romper
// el turno del agente.
bool append(const QString &cwd, const QString &sessionId, const QString &kind,
            QJsonObject data = {});

// Devuelve los ultimos `n` eventos del log como texto compacto legible (uno por
// linea: ts + kind + tool + ok + detalle). Si `sessionId` no esta vacio, filtra a
// esa sesion. Para que el agente relea su propio rastro reciente (que intento, que
// fallo) y se auto-corrija — el loop de "log + tail" del que hablan los harness.
// "[recent_actions: sin eventos]" si no hay log. Mejor-esfuerzo, nunca tira.
QString tail(const QString &cwd, const QString &sessionId, int n);

}  // namespace AgentEventLog
