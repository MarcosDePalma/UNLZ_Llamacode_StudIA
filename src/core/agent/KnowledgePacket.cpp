#include "KnowledgePacket.h"

#include "GraphStore.h"
#include "MemoryStore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace {

QStringList terms(const QString &text)
{
    QStringList out;
    for (const QString &term : text.toLower().split(
             QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")), Qt::SkipEmptyParts)) {
        if (term.size() >= 3 && !out.contains(term)) out << term;
    }
    return out;
}

QString edgeKey(const QJsonObject &edge)
{
    return edge.value(QStringLiteral("id")).toString()
        + edge.value(QStringLiteral("subj")).toString()
        + edge.value(QStringLiteral("obj")).toString();
}

} // namespace

namespace KnowledgePacket {

QJsonObject build(const QString &root, const QString &query, int maxFacts, int maxEdges)
{
    maxFacts = qBound(0, maxFacts, 50);
    maxEdges = qBound(0, maxEdges, 100);
    const QJsonArray memories = maxFacts > 0
        ? MemoryStore::recallFacts(root, query, QStringLiteral("project"), maxFacts)
        : QJsonArray();
    QJsonArray decisions;
    QJsonArray supportingFacts;
    for (const QJsonValue &value : memories) {
        const QJsonObject memory = value.toObject();
        if (memory.value(QStringLiteral("type")).toString() == QLatin1String("decision"))
            decisions.append(memory);
        else
            supportingFacts.append(memory);
    }
    const QString facts = maxFacts > 0
        ? MemoryStore::recall(root, query, QStringLiteral("project"), maxFacts)
        : QString();

    QJsonArray edges;
    QJsonArray nodes;
    QJsonArray sources;
    QSet<QString> seenEdges, seenNodes, seenSources;
    const QStringList qterms = terms(query);
    const QStringList names = GraphStore::entityNames(root, QString());
    for (const QString &name : names) {
        const QString low = name.toLower();
        bool match = qterms.isEmpty();
        for (const QString &term : qterms) {
            if (low.contains(term)) { match = true; break; }
        }
        if (!match || edges.size() >= maxEdges) continue;
        const QJsonObject packet = GraphStore::queryPacket(root, name, 2);
        if (!packet.value(QStringLiteral("ok")).toBool()) continue;
        for (const QJsonValue &v : packet.value(QStringLiteral("nodes")).toArray()) {
            const QJsonObject node = v.toObject();
            const QString key = node.value(QStringLiteral("id")).toString();
            if (!key.isEmpty() && !seenNodes.contains(key)) {
                seenNodes.insert(key);
                nodes.append(node);
            }
        }
        for (const QJsonValue &v : packet.value(QStringLiteral("edges")).toArray()) {
            const QJsonObject edge = v.toObject();
            const QString key = edgeKey(edge);
            if (key.isEmpty() || seenEdges.contains(key)) continue;
            seenEdges.insert(key);
            edges.append(edge);
            for (const QJsonValue &sv : edge.value(QStringLiteral("sources")).toArray()) {
                const QJsonObject source = sv.toObject();
                const QString skey = QString::fromUtf8(
                    QJsonDocument(source).toJson(QJsonDocument::Compact));
                if (!seenSources.contains(skey)) {
                    seenSources.insert(skey);
                    sources.append(source);
                }
            }
            if (edges.size() >= maxEdges) break;
        }
    }

    const bool budgetCut = edges.size() >= maxEdges;
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("query"), query},
            {QStringLiteral("factsText"), facts},
            {QStringLiteral("decisions"), decisions},
            {QStringLiteral("supportingFacts"), supportingFacts},
            {QStringLiteral("nodes"), nodes},
            {QStringLiteral("edges"), edges},
            {QStringLiteral("sources"), sources},
            {QStringLiteral("authority"), QJsonObject{
                {QStringLiteral("precedence"), QJsonArray{
                    QStringLiteral("source_and_tests"),
                    QStringLiteral("accepted_decision"),
                    QStringLiteral("verified_memory"),
                    QStringLiteral("unreviewed_inference"),
                    QStringLiteral("historical_session")}},
                {QStringLiteral("rule"), QStringLiteral(
                    "El código y los tests actuales son la fuente de verdad; una decisión "
                    "sólo guía al agente mientras siga vigente y respaldada.")},
                {QStringLiteral("decisionCount"), decisions.size()},
                {QStringLiteral("supportingFactCount"), supportingFacts.size()}}},
            {QStringLiteral("receipt"), QJsonObject{
                {QStringLiteral("maxFacts"), maxFacts},
                {QStringLiteral("maxEdges"), maxEdges},
                {QStringLiteral("factChars"), facts.size()},
                {QStringLiteral("decisionCount"), decisions.size()},
                {QStringLiteral("edgeCount"), edges.size()},
                {QStringLiteral("sourceCount"), sources.size()},
             {QStringLiteral("budgetCut"), budgetCut}}}};
}

QString format(const QJsonObject &packet, int maxChars)
{
    if (maxChars <= 0) maxChars = 12000;
    QStringList lines{QStringLiteral("[knowledge-packet · evidencia durable]")};
    const QJsonObject authority = packet.value(QStringLiteral("authority")).toObject();
    if (!authority.isEmpty()) {
        lines << QStringLiteral("Autoridad: %1")
                     .arg(authority.value(QStringLiteral("rule")).toString());
    }

    const QJsonArray decisions = packet.value(QStringLiteral("decisions")).toArray();
    if (!decisions.isEmpty()) {
        lines << QStringLiteral("Decisiones vigentes (no reemplazan código/tests actuales):");
        for (const QJsonValue &value : decisions) {
            const QJsonObject decision = value.toObject();
            QString line = QStringLiteral("- %1")
                .arg(decision.value(QStringLiteral("content")).toString());
            const QString verification = decision.value(QStringLiteral("verification"))
                                              .toString(QStringLiteral("inferred"));
            line += QStringLiteral(" [verification=%1 conf=%2]")
                .arg(verification)
                .arg(decision.value(QStringLiteral("confidence")).toDouble(0.0), 0, 'g', 2);
            const QString source = decision.value(QStringLiteral("source")).toString();
            if (!source.isEmpty()) line += QStringLiteral(" · src=%1").arg(source);
            const QString supersedes = decision.value(QStringLiteral("supersedes")).toString();
            if (!supersedes.isEmpty()) line += QStringLiteral(" · supersedes=%1").arg(supersedes);
            lines << line;
        }
    }

    const QJsonArray supporting = packet.value(QStringLiteral("supportingFacts")).toArray();
    if (!supporting.isEmpty()) {
        lines << QStringLiteral("Hechos de apoyo:");
        for (const QJsonValue &value : supporting) {
            const QJsonObject fact = value.toObject();
            QString line = QStringLiteral("- %1")
                .arg(fact.value(QStringLiteral("content")).toString());
            const QString verification = fact.value(QStringLiteral("verification"))
                                             .toString(QStringLiteral("inferred"));
            line += QStringLiteral(" [verification=%1]").arg(verification);
            const QString source = fact.value(QStringLiteral("source")).toString();
            if (!source.isEmpty()) line += QStringLiteral(" · src=%1").arg(source);
            lines << line;
        }
    }

    // Compatibilidad con paquetes construidos por versiones anteriores o con
    // maxFacts=0: si no hay arrays estructurados, conserva la salida histórica.
    if (decisions.isEmpty() && supporting.isEmpty()) {
        const QString facts = packet.value(QStringLiteral("factsText")).toString().trimmed();
        if (!facts.isEmpty() && !facts.startsWith(QLatin1Char('[')))
            lines << QStringLiteral("Hechos:") << facts;
    }
    const QJsonArray edges = packet.value(QStringLiteral("edges")).toArray();
    if (!edges.isEmpty()) {
        lines << QStringLiteral("Relaciones:");
        for (const QJsonValue &v : edges) {
            const QJsonObject e = v.toObject();
            QString line = QStringLiteral("- %1 -[%2]-> %3")
                .arg(e.value(QStringLiteral("subj")).toString(),
                     e.value(QStringLiteral("etype")).toString(),
                     e.value(QStringLiteral("obj")).toString());
            if (e.value(QStringLiteral("status")).toString() == QLatin1String("unreviewed"))
                line += QStringLiteral(" [unreviewed]");
            else
                line += QStringLiteral(" [verified conf=%1]")
                    .arg(e.value(QStringLiteral("conf")).toDouble(), 0, 'g', 2);
            QStringList citations;
            for (const QJsonValue &c : e.value(QStringLiteral("citations")).toArray())
                citations << c.toString();
            if (!citations.isEmpty()) line += QStringLiteral(" · ") + citations.join(", ");
            lines << line;
        }
    }
    const QJsonObject receipt = packet.value(QStringLiteral("receipt")).toObject();
    lines << QStringLiteral("\n── knowledge-receipt ──")
          << QString::fromUtf8(QJsonDocument(receipt).toJson(QJsonDocument::Compact));
    QString out = lines.join(QLatin1Char('\n'));
    if (out.size() > maxChars) {
        out.truncate(qMax(0, maxChars - 32));
        out += QStringLiteral("\n[knowledge-packet truncado]");
    }
    return out;
}

} // namespace KnowledgePacket
