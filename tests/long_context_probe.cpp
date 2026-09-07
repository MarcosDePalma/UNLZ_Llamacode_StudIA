#include "long_context_probe.h"

#include <QtGlobal>

#include <cmath>

namespace long_context_probe {
namespace {

QString safeStreamId(QString value)
{
    value = value.trimmed();
    if (value.isEmpty()) value = QStringLiteral("default");
    for (QChar &ch : value) {
        if (!(ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_')))
            ch = QLatin1Char('-');
    }
    return value;
}

QString filler(int characters, int seed)
{
    if (characters <= 0) return {};
    static const QStringList words{
        QStringLiteral("module"), QStringLiteral("request"),
        QStringLiteral("handler"), QStringLiteral("record"),
        QStringLiteral("context"), QStringLiteral("buffer"),
        QStringLiteral("session"), QStringLiteral("thread"),
        QStringLiteral("return"), QStringLiteral("value"),
        QStringLiteral("client"), QStringLiteral("index"),
    };

    QString out;
    out.reserve(characters);
    int word = qAbs(seed) % words.size();
    int line = 0;
    while (out.size() < characters) {
        out += words.at(word % words.size());
        out += (line % 10 == 9) ? QLatin1Char('\n') : QLatin1Char(' ');
        ++word;
        ++line;
    }
    return out.left(characters);
}

} // namespace

QVector<double> standardDepths()
{
    // Seven placements spanning the useful range without putting the needle
    // in the prompt header or directly on the final instruction.
    return {0.05, 0.15, 0.25, 0.50, 0.75, 0.90, 0.95};
}

QVector<RetrievalCase> buildCases(int contextTokens, const QVector<double> &depths,
                                   const QString &streamId)
{
    QVector<RetrievalCase> out;
    if (contextTokens <= 0) return out;

    const QString safeStream = safeStreamId(streamId);
    const QString header = QStringLiteral(
        "LONG CONTEXT RETRIEVAL EVALUATION.\n"
        "Read every record before answering. There is exactly one secret record.\n"
        "Do not explain your reasoning. At the end, return only its passkey.\n\n");
    const QString footer = QStringLiteral(
        "\n\nFINAL QUESTION: What is the passkey in the secret record?\n"
        "Reply with the passkey only, preserving every character.");

    int caseIndex = 0;
    for (const double depth : depths) {
        if (!std::isfinite(depth) || depth < 0.0 || depth > 1.0)
            continue;

        const QString passkey = QStringLiteral("LC-NIAH-%1-%2")
                                    .arg(safeStream)
                                    .arg(caseIndex, 3, 10, QLatin1Char('0'));
        const QString record = QStringLiteral(
            "\n[SECRET RECORD] passkey=%1; status=verified; checksum=amber-7429.\n")
                                   .arg(passkey);

        // Four chars/token is intentionally conservative and mirrors the
        // sizing heuristic used by TunerEngine. The fixture is for relative
        // comparisons and retrieval gates, not tokenizer-exact accounting.
        const int targetChars = qMax(512, contextTokens * 4);
        const int fillerChars = qMax(64, targetChars - header.size()
                                               - footer.size() - record.size());
        const int leftChars = qBound(0, qRound(fillerChars * depth), fillerChars);
        const int rightChars = fillerChars - leftChars;

        RetrievalCase fixture;
        fixture.id = QStringLiteral("%1-%2").arg(safeStream).arg(caseIndex, 3, 10,
                                                                  QLatin1Char('0'));
        fixture.streamId = safeStream;
        fixture.contextTokens = contextTokens;
        fixture.depth = depth;
        fixture.passkey = passkey;
        fixture.prompt = header
                       + filler(leftChars, caseIndex)
                       + record
                       + filler(rightChars, caseIndex + 17)
                       + footer;
        out.append(fixture);
        ++caseIndex;
    }
    return out;
}

int passkeyOccurrences(const RetrievalCase &fixture)
{
    if (fixture.passkey.isEmpty()) return 0;
    return fixture.prompt.count(fixture.passkey);
}

int approximatePromptTokens(const QString &prompt)
{
    return qMax(0, qRound(prompt.size() / 4.0));
}

} // namespace long_context_probe
