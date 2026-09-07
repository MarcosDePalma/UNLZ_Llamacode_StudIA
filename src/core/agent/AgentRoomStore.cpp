#include "AgentRoomStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace {

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString cleanText(const QVariant &value, int max = 200000)
{
    QString out = value.toString().trimmed();
    if (out.size() > max) out.truncate(max);
    return out;
}

QVariantMap defaultHuman()
{
    return {
        {"id", "human:owner"}, {"name", "Usuario"}, {"kind", "human"},
        {"role", "owner"}, {"status", "available"},
        {"capabilities", QStringList{"approve", "steer", "assign"}},
        {"grant", QVariantMap{{"read", true}, {"write", true}, {"shell", true},
                              {"network", true}, {"externalWrite", true},
                              {"destructive", true}}}
    };
}

QVariantMap defaultCoordinator()
{
    return {
        {"id", "agent:coordinator"}, {"name", "Coordinador"}, {"kind", "agent"},
        {"role", "coordinator"}, {"status", "available"},
        {"capabilities", QStringList{"delegate", "synthesize", "use_tools"}},
        {"grant", QVariantMap{{"read", true}, {"write", true}, {"shell", true},
                              {"network", true}, {"externalWrite", false},
                              {"destructive", false}}}
    };
}

}

AgentRoomStore::AgentRoomStore(QObject *parent)
    : AgentRoomStore(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                     + QStringLiteral("/agent-rooms"), parent)
{
}

AgentRoomStore::AgentRoomStore(const QString &storageDir, QObject *parent)
    : QObject(parent), m_storageDir(storageDir)
{
    QDir().mkpath(m_storageDir);
    load();
}

void AgentRoomStore::load()
{
    QFile f(QDir(m_storageDir).filePath(QStringLiteral("rooms.json")));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        m_rooms = root.value(QStringLiteral("rooms")).toArray().toVariantList();
        m_currentRoomId = root.value(QStringLiteral("currentRoomId")).toString();
    }
    if (roomIndex(m_currentRoomId) < 0)
        m_currentRoomId = m_rooms.isEmpty() ? QString() : m_rooms.first().toMap().value("id").toString();
}

bool AgentRoomStore::saveRooms() const
{
    QSaveFile f(QDir(m_storageDir).filePath(QStringLiteral("rooms.json")));
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QJsonObject root{
        {"version", 1},
        {"currentRoomId", m_currentRoomId},
        {"rooms", QJsonArray::fromVariantList(m_rooms)}
    };
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

int AgentRoomStore::roomIndex(const QString &roomId) const
{
    for (int i = 0; i < m_rooms.size(); ++i)
        if (m_rooms.at(i).toMap().value("id").toString() == roomId) return i;
    return -1;
}

QVariantList AgentRoomStore::rooms() const
{
    QVariantList out = m_rooms;
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("updatedAt").toString() > b.toMap().value("updatedAt").toString();
    });
    return out;
}

QVariantMap AgentRoomStore::room(const QString &roomId) const
{
    const int i = roomIndex(roomId);
    return i < 0 ? QVariantMap{} : m_rooms.at(i).toMap();
}

QVariantMap AgentRoomStore::currentRoom() const
{
    return room(m_currentRoomId);
}

QVariantList AgentRoomStore::currentEvents() const
{
    return events(m_currentRoomId);
}

QVariantList AgentRoomStore::currentParticipants() const
{
    return participants(m_currentRoomId);
}

QString AgentRoomStore::eventsPath(const QString &roomId) const
{
    return QDir(m_storageDir).filePath(QStringLiteral("events/%1.jsonl").arg(roomId));
}

QString AgentRoomStore::createRoom(const QString &title, const QString &projectDir)
{
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = nowIso();
    const QVariantMap r{
        {"id", id},
        {"title", cleanText(title, 160).isEmpty() ? QStringLiteral("Nueva sala") : cleanText(title, 160)},
        {"projectDir", QDir::cleanPath(projectDir.trimmed())},
        {"createdAt", now}, {"updatedAt", now},
        {"participants", QVariantList{defaultHuman(), defaultCoordinator()}}
    };
    m_rooms.append(r);
    m_currentRoomId = id;
    saveRooms();
    postEvent(id, {{"type", "room_created"}, {"author", "system"},
                   {"content", QStringLiteral("Sala creada")}});
    emit roomsChanged();
    emit currentRoomChanged();
    emit participantsChanged();
    return id;
}

bool AgentRoomStore::removeRoom(const QString &roomId)
{
    const int i = roomIndex(roomId);
    if (i < 0) return false;
    m_rooms.removeAt(i);
    QFile::remove(eventsPath(roomId));
    if (m_currentRoomId == roomId)
        m_currentRoomId = m_rooms.isEmpty() ? QString() : m_rooms.first().toMap().value("id").toString();
    saveRooms();
    emit roomsChanged();
    emit currentRoomChanged();
    emit eventsChanged();
    emit participantsChanged();
    return true;
}

bool AgentRoomStore::setCurrentRoomId(const QString &roomId)
{
    if (roomIndex(roomId) < 0 || roomId == m_currentRoomId) return roomId == m_currentRoomId;
    m_currentRoomId = roomId;
    saveRooms();
    emit currentRoomChanged();
    emit eventsChanged();
    emit participantsChanged();
    return true;
}

QVariantList AgentRoomStore::participants(const QString &roomId) const
{
    return room(roomId).value("participants").toList();
}

bool AgentRoomStore::upsertParticipant(const QString &roomId, const QVariantMap &participant)
{
    const int ri = roomIndex(roomId);
    QString id = cleanText(participant.value("id"), 120);
    if (ri < 0 || id.isEmpty()) return false;
    QVariantMap safe = participant;
    safe["id"] = id;
    safe["name"] = cleanText(safe.value("name"), 160);
    safe["kind"] = safe.value("kind").toString() == QLatin1String("human") ? "human" : "agent";
    safe["status"] = cleanText(safe.value("status"), 40);
    safe["grant"] = normalizedGrant(safe.value("grant").toMap());
    QVariantMap r = m_rooms.at(ri).toMap();
    QVariantList ps = r.value("participants").toList();
    int pi = -1;
    for (int i = 0; i < ps.size(); ++i)
        if (ps.at(i).toMap().value("id").toString() == id) { pi = i; break; }
    if (pi >= 0) {
        const QVariantMap previous = ps.at(pi).toMap();
        if (!grantDoesNotEscalate(previous.value("grant").toMap(), safe.value("grant").toMap()))
            return false;
        ps[pi] = safe;
    } else {
        ps.append(safe);
    }
    r["participants"] = ps;
    r["updatedAt"] = nowIso();
    m_rooms[ri] = r;
    saveRooms();
    emit roomsChanged();
    emit participantsChanged();
    return true;
}

QVariantMap AgentRoomStore::normalizedGrant(const QVariantMap &grant)
{
    return {
        {"read", grant.value("read", true).toBool()},
        {"write", grant.value("write", false).toBool()},
        {"shell", grant.value("shell", false).toBool()},
        {"network", grant.value("network", false).toBool()},
        {"externalWrite", grant.value("externalWrite", false).toBool()},
        {"destructive", grant.value("destructive", false).toBool()}
    };
}

bool AgentRoomStore::grantDoesNotEscalate(const QVariantMap &oldGrant, const QVariantMap &nextGrant)
{
    const QVariantMap oldNorm = normalizedGrant(oldGrant);
    const QVariantMap nextNorm = normalizedGrant(nextGrant);
    for (auto it = nextNorm.cbegin(); it != nextNorm.cend(); ++it)
        if (it.value().toBool() && !oldNorm.value(it.key()).toBool()) return false;
    return true;
}

bool AgentRoomStore::updateGrant(const QString &roomId, const QString &participantId,
                                 const QVariantMap &requestedGrant)
{
    const QVariantList ps = participants(roomId);
    for (const QVariant &v : ps) {
        QVariantMap p = v.toMap();
        if (p.value("id").toString() != participantId) continue;
        p["grant"] = normalizedGrant(requestedGrant);
        return upsertParticipant(roomId, p);
    }
    return false;
}

QString AgentRoomStore::postEvent(const QString &roomId, const QVariantMap &event)
{
    const int ri = roomIndex(roomId);
    if (ri < 0) return {};
    QVariantMap safe = event;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    safe["id"] = id;
    safe["roomId"] = roomId;
    safe["createdAt"] = nowIso();
    safe["type"] = cleanText(safe.value("type"), 60);
    if (safe.value("type").toString().isEmpty()) safe["type"] = "message";
    safe["author"] = cleanText(safe.value("author"), 120);
    if (safe.value("author").toString().isEmpty()) safe["author"] = "system";
    safe["content"] = cleanText(safe.value("content"));
    safe["replyTo"] = cleanText(safe.value("replyTo"), 120);
    safe["correlationId"] = cleanText(safe.value("correlationId"), 120);

    const QString path = eventsPath(roomId);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return {};
    f.write(QJsonDocument(QJsonObject::fromVariantMap(safe)).toJson(QJsonDocument::Compact));
    f.write("\n");
    f.close();

    QVariantMap r = m_rooms.at(ri).toMap();
    r["updatedAt"] = safe.value("createdAt");
    r["lastEvent"] = safe.value("content").toString().left(180);
    m_rooms[ri] = r;
    saveRooms();
    emit roomsChanged();
    if (roomId == m_currentRoomId) emit eventsChanged();
    return id;
}

QVariantList AgentRoomStore::events(const QString &roomId, int limit) const
{
    QVariantList out;
    if (roomIndex(roomId) < 0) return out;
    limit = qBound(1, limit, 5000);
    QFile f(eventsPath(roomId));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    while (!f.atEnd()) {
        const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
        if (!o.isEmpty()) out.append(o.toVariantMap());
        if (out.size() > limit) out.removeFirst();
    }
    return out;
}

QVariantMap AgentRoomStore::preset(const QString &name, const QString &goal) const
{
    const QString n = name.trimmed().toLower();
    QVariantList members;
    QString instructions;
    if (n == QLatin1String("review")) {
        members = {
            QVariantMap{{"id", "agent:implementer"}, {"name", "Implementador"}, {"role", "implementer"},
                        {"grant", QVariantMap{{"read", true}, {"write", true}, {"shell", true}}}},
            QVariantMap{{"id", "agent:reviewer"}, {"name", "Revisor"}, {"role", "reviewer"},
                        {"grant", QVariantMap{{"read", true}, {"write", false}, {"shell", true}}}}
        };
        instructions = QStringLiteral("Delegá implementación y revisión a subagentes aislados. "
                                      "El revisor no puede editar. Repará hallazgos, ejecutá las pruebas y sintetizá evidencia.");
    } else if (n == QLatin1String("autoprompt")) {
        members = {
            QVariantMap{{"id", "agent:implementer"}, {"name", "Implementador"}, {"role", "implementer"},
                        {"grant", QVariantMap{{"read", true}, {"write", true}, {"shell", true}}}},
            QVariantMap{{"id", "agent:reviewer"}, {"name", "Revisor independiente"}, {"role", "reviewer"},
                        {"grant", QVariantMap{{"read", true}, {"write", false}, {"shell", false}}}},
            QVariantMap{{"id", "agent:verifier"}, {"name", "Verificador"}, {"role", "verifier"},
                        {"grant", QVariantMap{{"read", true}, {"write", false}, {"shell", true}}}}
        };
        instructions = QStringLiteral(
            "Seguí el ciclo alcance → plan → implementación → revisión independiente → "
            "verificación → reparación. Cada fase debe dejar evidencia y un veredicto "
            "PASS/FAIL/BLOCKED; no cierres con una respuesta sin pruebas y acotá las "
            "reparaciones para evitar bucles.");
    } else if (n == QLatin1String("council")) {
        members = {
            QVariantMap{{"id", "agent:perspective-a"}, {"name", "Perspectiva A"}, {"role", "analyst"}},
            QVariantMap{{"id", "agent:perspective-b"}, {"name", "Perspectiva B"}, {"role", "critic"}},
            QVariantMap{{"id", "agent:verifier"}, {"name", "Verificador"}, {"role", "verifier"}}
        };
        instructions = QStringLiteral("Pedí perspectivas independientes en paralelo, hacé que el verificador contraste "
                                      "supuestos y luego emití una síntesis con acuerdos, desacuerdos y confianza.");
    } else if (n == QLatin1String("research")) {
        members = {
            QVariantMap{{"id", "agent:researcher-a"}, {"name", "Investigador A"}, {"role", "researcher"}},
            QVariantMap{{"id", "agent:researcher-b"}, {"name", "Investigador B"}, {"role", "researcher"}},
            QVariantMap{{"id", "agent:citation-checker"}, {"name", "Verificador de fuentes"}, {"role", "verifier"}}
        };
        instructions = QStringLiteral("Dividí la investigación en fuentes independientes, verificá cada afirmación "
                                      "importante y entregá un informe con enlaces y límites explícitos.");
    } else {
        return {{"error", QStringLiteral("preset desconocido: %1").arg(name)}};
    }
    return {{"name", n}, {"goal", cleanText(goal)}, {"participants", members},
            {"instructions", instructions}};
}

QString AgentRoomStore::compactContext(const QString &roomId, const QString &participantId,
                                       int maxChars) const
{
    maxChars = qBound(1000, maxChars, 50000);
    const QVariantList rows = events(roomId, 1000);
    QStringList lines;
    for (const QVariant &v : rows) {
        const QVariantMap e = v.toMap();
        const QStringList audience = e.value("audience").toStringList();
        if (!audience.isEmpty() && !audience.contains(participantId)
            && !audience.contains(QStringLiteral("*")))
            continue;
        lines << QStringLiteral("%1 [%2] %3: %4")
                     .arg(e.value("createdAt").toString(),
                          e.value("type").toString(),
                          e.value("author").toString(),
                          e.value("content").toString());
    }
    QString out;
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QString candidate = lines.at(i) + QLatin1Char('\n') + out;
        if (candidate.size() > maxChars) break;
        out = candidate;
    }
    return out.trimmed();
}
