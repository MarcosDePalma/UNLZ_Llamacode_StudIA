#pragma once

#include <QString>
#include <QVector>

namespace long_context_probe {

// A deterministic long-context retrieval fixture. It deliberately lives in the
// test/QA layer: it must not become a model- or vendor-specific production
// prompt. The passkey is present exactly once in the prompt and never appears
// in the final question, so a response can be checked without fuzzy heuristics.
struct RetrievalCase {
    QString id;
    QString streamId;
    int contextTokens = 0;
    double depth = 0.0;
    QString passkey;
    QString prompt;
};

QVector<double> standardDepths();

// Invalid context sizes or depths are ignored. This keeps callers from
// accidentally creating a misleading "0% depth" or a negative-size fixture.
QVector<RetrievalCase> buildCases(int contextTokens,
                                   const QVector<double> &depths,
                                   const QString &streamId = QStringLiteral("default"));

int passkeyOccurrences(const RetrievalCase &fixture);
int approximatePromptTokens(const QString &prompt);

} // namespace long_context_probe
