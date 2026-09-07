#include "TurnDetector.h"
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace TurnDetector {
namespace {

// Palabras que NO pueden terminar una frase: si el parcial corta acá, la
// persona sigue hablando (conjunciones, preposiciones, artículos, relativos) o
// está pensando en voz alta (muletillas). es + en, que es lo que soporta STT
// hoy (VoiceConfig.sttLanguage).
const QSet<QString> &hangingWords()
{
    static const QSet<QString> s = {
        // conjunciones / nexos (es)
        QStringLiteral("y"), QStringLiteral("e"), QStringLiteral("o"),
        QStringLiteral("u"), QStringLiteral("ni"), QStringLiteral("pero"),
        QStringLiteral("porque"), QStringLiteral("pues"), QStringLiteral("aunque"),
        QStringLiteral("si"), QStringLiteral("como"), QStringLiteral("cuando"),
        QStringLiteral("mientras"), QStringLiteral("entonces"), QStringLiteral("que"),
        QStringLiteral("quien"), QStringLiteral("cual"), QStringLiteral("donde"),
        // preposiciones (es)
        QStringLiteral("a"), QStringLiteral("ante"), QStringLiteral("bajo"),
        QStringLiteral("con"), QStringLiteral("contra"), QStringLiteral("de"),
        QStringLiteral("desde"), QStringLiteral("en"), QStringLiteral("entre"),
        QStringLiteral("hacia"), QStringLiteral("hasta"), QStringLiteral("para"),
        QStringLiteral("por"), QStringLiteral("segun"), QStringLiteral("sin"),
        QStringLiteral("sobre"), QStringLiteral("tras"),
        // artículos / determinantes (es)
        QStringLiteral("el"), QStringLiteral("la"), QStringLiteral("los"),
        QStringLiteral("las"), QStringLiteral("un"), QStringLiteral("una"),
        QStringLiteral("unos"), QStringLiteral("unas"), QStringLiteral("mi"),
        QStringLiteral("tu"), QStringLiteral("su"), QStringLiteral("del"),
        QStringLiteral("al"), QStringLiteral("este"), QStringLiteral("esta"),
        QStringLiteral("esto"), QStringLiteral("ese"), QStringLiteral("esa"),
        // muletillas / hesitación (es)
        QStringLiteral("eh"), QStringLiteral("em"), QStringLiteral("este"),
        QStringLiteral("mmm"), QStringLiteral("osea"), QStringLiteral("sea"),
        QStringLiteral("digamos"), QStringLiteral("tipo"),
        // en
        QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("but"),
        QStringLiteral("because"), QStringLiteral("so"), QStringLiteral("if"),
        QStringLiteral("when"), QStringLiteral("while"), QStringLiteral("that"),
        QStringLiteral("which"), QStringLiteral("the"), QStringLiteral("my"),
        QStringLiteral("your"), QStringLiteral("to"), QStringLiteral("of"),
        QStringLiteral("for"), QStringLiteral("with"), QStringLiteral("from"),
        QStringLiteral("into"), QStringLiteral("uh"), QStringLiteral("um"),
        QStringLiteral("like")
    };
    return s;
}

// Última palabra en minúsculas y sin acentos/puntuación, para matchear la
// lista (el STT devuelve "según"/"segun" según el modelo; normalizamos).
QString lastWord(const QString &s)
{
    static const QString from = QStringLiteral("áéíóúüñàèìòù");
    static const QString to   = QStringLiteral("aeiouunaeiou");

    QString w;
    for (int i = s.size() - 1; i >= 0; --i) {
        const QChar c = s.at(i);
        if (c.isLetter() || c.isDigit()) w.prepend(c);
        else if (!w.isEmpty()) break;
    }
    w = w.toLower();
    QString out;
    out.reserve(w.size());
    for (const QChar c : w) {
        const int idx = from.indexOf(c);
        out.append(idx >= 0 ? to.at(idx) : c);
    }
    return out;
}

} // namespace

Ending classify(const QString &partial)
{
    const QString s = partial.trimmed();
    if (s.isEmpty()) return Unknown;

    const QChar last = s.at(s.size() - 1);

    // Terminador de oración → cerró.
    if (last == QLatin1Char('.') || last == QLatin1Char('!') || last == QLatin1Char('?')
        || last == QChar(0x2026) /* … */)
        return Complete;

    // Coma / dos puntos / guión: la cláusula sigue.
    if (last == QLatin1Char(',') || last == QLatin1Char(':') || last == QLatin1Char(';')
        || last == QLatin1Char('-') || last == QChar(0x2014) /* — */)
        return Incomplete;

    // Apertura española colgada ("¿cómo hago para" con el ¿ suelto).
    if (last == QChar(0x00BF) || last == QChar(0x00A1)) return Incomplete;

    const QString w = lastWord(s);
    if (w.isEmpty()) return Unknown;
    if (hangingWords().contains(w)) return Incomplete;

    // Sin terminador pero con palabra plena al final: los STT chicos (whisper
    // tiny/base) suelen no puntuar, así que una frase de varias palabras que
    // no queda colgada cuenta como cerrada. Una sola palabra es ambigua
    // ("dale", "sí", pero también el arranque de una frase) → base.
    return s.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() >= 3 ? Complete : Unknown;
}

int requiredSilenceMs(const QString &partial, int baseMs, const EndpointTuning &t)
{
    if (baseMs <= 0) return 0;
    double ms = baseMs;
    switch (classify(partial)) {
    case Complete:   ms = baseMs * t.completeFactor;   break;
    case Incomplete: ms = baseMs * t.incompleteFactor; break;
    case Unknown:    break;
    }
    const int lo = std::min(t.minMs, baseMs);   // base chico: no estirarlo por el piso
    const int hi = std::max(t.maxMs, lo);
    return std::clamp(int(ms + 0.5), lo, hi);
}

} // namespace TurnDetector
