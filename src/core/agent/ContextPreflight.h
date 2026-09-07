#pragma once
#include <QString>

namespace ContextPreflight {
QString build(const QString &root, const QString &request, int maxFiles = 12,
              int tokenBudget = 720, bool expandGraph = true,
              bool includeKnowledge = false, int maxFacts = 8,
              int maxEdges = 12, int knowledgeMaxChars = 12000,
              const QString &sessionId = QString());
}
