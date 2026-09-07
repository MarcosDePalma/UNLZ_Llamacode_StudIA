#include "FuzzyMatch.h"

#include <QChar>
#include <QVector>

#include <algorithm>

namespace {

// Largo máximo comparado. Los nombres UIA ya vienen recortados a 120; el tope
// evita que un control con un nombre patológico dispare un DP cuadrático caro.
constexpr int kMaxLen = 160;

// Longitud de la subsecuencia común más larga. DP por filas (O(n*m) tiempo,
// O(min) memoria). Con kMaxLen el peor caso queda acotado.
int lcsLength(const QString &a, const QString &b)
{
    const int n = a.size(), m = b.size();
    if (n == 0 || m == 0) return 0;
    QVector<int> prev(m + 1, 0), cur(m + 1, 0);
    for (int i = 1; i <= n; ++i) {
        const QChar ca = a.at(i - 1);
        for (int j = 1; j <= m; ++j) {
            cur[j] = (ca == b.at(j - 1)) ? prev[j - 1] + 1
                                         : std::max(prev[j], cur[j - 1]);
        }
        prev = cur;
    }
    return prev[m];
}

int ratioNorm(const QString &a, const QString &b)
{
    if (a.isEmpty() && b.isEmpty()) return 100;
    if (a.isEmpty() || b.isEmpty()) return 0;
    if (a == b) return 100;
    const int total = a.size() + b.size();
    return static_cast<int>((200.0 * lcsLength(a, b)) / total + 0.5);
}

int partialRatioNorm(const QString &a, const QString &b)
{
    if (a.isEmpty() && b.isEmpty()) return 100;
    if (a.isEmpty() || b.isEmpty()) return 0;
    const QString &shortS = a.size() <= b.size() ? a : b;
    const QString &longS = a.size() <= b.size() ? b : a;
    if (shortS.size() == longS.size()) return ratioNorm(shortS, longS);
    int best = 0;
    const int win = shortS.size();
    for (int off = 0; off + win <= longS.size() && best < 100; ++off)
        best = std::max(best, ratioNorm(shortS, longS.mid(off, win)));
    return best;
}

QString tokenSorted(const QString &s)
{
    QStringList parts = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    std::sort(parts.begin(), parts.end());
    return parts.join(QLatin1Char(' '));
}

}   // namespace

namespace FuzzyMatch {

QString normalize(const QString &s)
{
    // NFKD separa la tilde en un diacrítico propio; descartar los diacríticos deja
    // "Añadir" == "Anadir" (el modelo suele perder tildes al repetir un nombre).
    const QString decomposed = s.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing) continue;
        if (c == QLatin1Char('&')) continue;   // acelerador: "&Guardar" == "Guardar"
        if (c.isLetterOrNumber()) out.append(c.toLower());
        else out.append(QLatin1Char(' '));     // puntuación/"..." → separador
    }
    return out.simplified().left(kMaxLen);
}

int ratio(const QString &a, const QString &b)
{
    return ratioNorm(normalize(a), normalize(b));
}

int partialRatio(const QString &a, const QString &b)
{
    return partialRatioNorm(normalize(a), normalize(b));
}

int tokenSortRatio(const QString &a, const QString &b)
{
    return ratioNorm(tokenSorted(normalize(a)), tokenSorted(normalize(b)));
}

int weightedRatio(const QString &a, const QString &b)
{
    const QString na = normalize(a), nb = normalize(b);
    if (na.isEmpty() && nb.isEmpty()) return 100;
    if (na.isEmpty() || nb.isEmpty()) return 0;

    int best = ratioNorm(na, nb);
    // token-sort casi no penaliza: reordenar palabras es una variación legítima.
    best = std::max(best, static_cast<int>(tokenSortRatio(a, b) * 0.95));

    const int lo = std::min(na.size(), nb.size());
    const int hi = std::max(na.size(), nb.size());
    const double lenRatio = static_cast<double>(hi) / static_cast<double>(lo);
    // Sólo vale mirar substrings cuando los largos ya difieren; si no, `ratio` es
    // mejor señal. Cuanto más desparejos, más penalizado: que "Guardar" matchee
    // adentro de un párrafo no puede puntuar como igualdad.
    if (lenRatio >= 1.5) {
        const double scale = lenRatio < 8.0 ? 0.9 : 0.6;
        best = std::max(best, static_cast<int>(partialRatioNorm(na, nb) * scale));
        best = std::max(best, static_cast<int>(
            ratioNorm(tokenSorted(na), tokenSorted(nb)) * scale * 0.95));
    }
    return std::min(best, 100);
}

Match extractOne(const QString &needle, const QStringList &candidates, int cutoff)
{
    Match best;
    if (normalize(needle).isEmpty()) return best;
    for (int i = 0; i < candidates.size(); ++i) {
        const int score = weightedRatio(needle, candidates.at(i));
        // `>` estricto: ante empate gana el primero → determinístico.
        if (score >= cutoff && score > best.score) {
            best.index = i;
            best.score = score;
            if (score == 100) break;
        }
    }
    return best;
}

}   // namespace FuzzyMatch
