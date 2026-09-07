#pragma once

#include <QJsonObject>
#include <QString>

// Ensambla una vista acotada y con procedencia del conocimiento durable del
// proyecto. No agrega almacenamiento: combina MemoryStore + GraphStore y
// devuelve un paquete efímero para el contexto de un turno.
namespace KnowledgePacket {

QJsonObject build(const QString &root, const QString &query,
                  int maxFacts = 8, int maxEdges = 12);

QString format(const QJsonObject &packet, int maxChars = 12000);

} // namespace KnowledgePacket
