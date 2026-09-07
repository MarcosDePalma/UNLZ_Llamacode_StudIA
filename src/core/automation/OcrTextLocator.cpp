#include "OcrTextLocator.h"

#include "FuzzyMatch.h"

#include <algorithm>

namespace {

// Tope de palabras por frase candidata. El needle marca la escala: buscar
// "Guardar" (1 palabra) no requiere armar frases de 8. El +2 deja aire para que
// el OCR parta una palabra en dos ("Guar" "dar") sin perder el match.
int maxPhraseWords(const QString &needle)
{
    const int n = needle.split(QLatin1Char(' '), Qt::SkipEmptyParts).size();
    return std::clamp(n + 2, 1, 8);
}

}   // namespace

namespace OcrTextLocator {

QList<Hit> findAll(const QList<OcrLine> &lines, const QString &needle, int cutoff)
{
    QList<Hit> hits;
    if (FuzzyMatch::normalize(needle).isEmpty()) return hits;
    const int maxWords = maxPhraseWords(needle);

    for (int li = 0; li < lines.size(); ++li) {
        const QList<OcrWord> &words = lines.at(li).words;
        // Mejor frase POR LÍNEA: si no, "Guardar" y "Guardar como" de la misma
        // línea entrarían las dos y se leerían como una ambigüedad que no existe.
        Hit bestOfLine;
        for (int i = 0; i < words.size(); ++i) {
            QString phrase;
            QRect box;
            for (int len = 1; len <= maxWords && i + len <= words.size(); ++len) {
                const OcrWord &w = words.at(i + len - 1);
                if (!phrase.isEmpty()) phrase += QLatin1Char(' ');
                phrase += w.text;
                box = box.isNull() ? w.rect : box.united(w.rect);

                const int score = FuzzyMatch::weightedRatio(needle, phrase);
                // `>` estricto: ante empate gana la frase más corta (se evalúan de
                // menor a mayor) → "Guardar" antes que "Guardar como algo".
                if (score >= cutoff && score > bestOfLine.score) {
                    bestOfLine.score = score;
                    bestOfLine.rect = box;
                    bestOfLine.text = phrase;
                    bestOfLine.lineIndex = li;
                }
            }
        }
        if (bestOfLine.ok()) hits.append(bestOfLine);
    }

    // Mejor primero. Ante empate, el de más arriba-izquierda: da un orden estable
    // en vez de depender de cómo el OCR enumeró las líneas.
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.rect.y() != b.rect.y()) return a.rect.y() < b.rect.y();
        return a.rect.x() < b.rect.x();
    });
    return hits;
}

Hit find(const QList<OcrLine> &lines, const QString &needle, int cutoff)
{
    const QList<Hit> hits = findAll(lines, needle, cutoff);
    return hits.isEmpty() ? Hit{} : hits.first();
}

}   // namespace OcrTextLocator
