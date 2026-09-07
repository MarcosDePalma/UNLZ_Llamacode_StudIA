#include "ContextPreflight.h"
#include "ProjectBrain.h"
#include "CodeGraphIndexer.h"
#include "ContextIndex.h"
#include "KnowledgePacket.h"
#include "WorkRegistry.h"

#include <QJsonObject>

QString ContextPreflight::build(const QString &root, const QString &request, int maxFiles,
                                int tokenBudget, bool expandGraph,
                                bool includeKnowledge, int maxFacts, int maxEdges,
                                int knowledgeMaxChars, const QString &sessionId)
{
    QVariantMap brain = ProjectBrain::load(root);
    if (brain.isEmpty()) brain = ProjectBrain::refresh(root);
    if (brain.contains(QStringLiteral("error"))) return {};
    QString graphReport;
    CodeGraphIndexer::buildIncremental(root, {}, &graphReport);
    const QVariantMap scout = ContextIndex::scout(root, request,
                                                   qBound(64, tokenBudget, 16000),
                                                   qBound(1, maxFiles, 15), expandGraph);
    if (!scout.value(QStringLiteral("ok")).toBool()) return {};
    QString knowledge;
    if (includeKnowledge) {
        const QJsonObject packet = KnowledgePacket::build(root, request, maxFacts, maxEdges);
        knowledge = KnowledgePacket::format(packet, knowledgeMaxChars);
    }
    const QString activeWork = WorkRegistry::formatActive(root, sessionId, 8);
    return QStringLiteral("[preflight de contexto — índice local regenerable]\n"
                          "Archivos de ProjectBrain: %1\n%2\n"
                          "Leé los handles/rangos exactos antes de editar.\n"
                          "Graph: %3\n"
                          "Trabajo activo del proyecto:\n%4%5")
        .arg(brain.value(QStringLiteral("fileCount")).toInt())
        .arg(ContextIndex::formatScout(scout))
        .arg(graphReport)
        .arg(activeWork)
        .arg(knowledge.isEmpty() ? QString() : QStringLiteral("\n\n") + knowledge);
}
