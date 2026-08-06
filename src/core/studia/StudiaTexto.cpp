#include "StudiaTexto.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>

namespace {

// Tope de pasadas al resolver fracciones anidadas. Cada pasada elimina al menos
// un \frac, asi que en la practica nunca se llega; es una red de seguridad.
constexpr int kMaxPasadas = 40;

// Si s[abre] es '{', devuelve el indice del '}' que lo cierra (contando
// anidados). -1 si no cierra.
int cierreDeLlave(const QString &s, int abre)
{
    if (abre < 0 || abre >= s.size() || s.at(abre) != QLatin1Char('{'))
        return -1;
    int nivel = 0;
    for (int i = abre; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('{')) ++nivel;
        else if (c == QLatin1Char('}')) {
            if (--nivel == 0) return i;
        }
    }
    return -1;
}

// ¿Necesita parentesis al ponerlo en un cociente? Un solo simbolo no; una
// expresion con operadores o espacios, si.
bool necesitaParentesis(const QString &s)
{
    if (s.size() <= 1) return false;
    static const QRegularExpression simple(QStringLiteral("^[\\p{L}\\p{N}.,]+$"));
    return !simple.match(s).hasMatch();
}

QString envolver(const QString &s)
{
    return necesitaParentesis(s) ? QStringLiteral("(%1)").arg(s) : s;
}

const QHash<QChar, QChar> &mapaSuper()
{
    static const QHash<QChar, QChar> m = {
        {u'0', u'⁰'}, {u'1', u'¹'}, {u'2', u'²'}, {u'3', u'³'}, {u'4', u'⁴'},
        {u'5', u'⁵'}, {u'6', u'⁶'}, {u'7', u'⁷'}, {u'8', u'⁸'}, {u'9', u'⁹'},
        {u'+', u'⁺'}, {u'-', u'⁻'}, {u'=', u'⁼'}, {u'(', u'⁽'}, {u')', u'⁾'},
        // Letras: en apuntes de ingeniería abundan d^m, e^t, x^k, r^p…
        // (la 'q' no tiene superíndice Unicode; cae al formato ^(q)).
        {u'a', u'ᵃ'}, {u'b', u'ᵇ'}, {u'c', u'ᶜ'}, {u'd', u'ᵈ'}, {u'e', u'ᵉ'},
        {u'f', u'ᶠ'}, {u'g', u'ᵍ'}, {u'h', u'ʰ'}, {u'i', u'ⁱ'}, {u'j', u'ʲ'},
        {u'k', u'ᵏ'}, {u'l', u'ˡ'}, {u'm', u'ᵐ'}, {u'n', u'ⁿ'}, {u'o', u'ᵒ'},
        {u'p', u'ᵖ'}, {u'r', u'ʳ'}, {u's', u'ˢ'}, {u't', u'ᵗ'}, {u'u', u'ᵘ'},
        {u'v', u'ᵛ'}, {u'w', u'ʷ'}, {u'x', u'ˣ'}, {u'y', u'ʸ'}, {u'z', u'ᶻ'},
    };
    return m;
}

const QHash<QChar, QChar> &mapaSub()
{
    static const QHash<QChar, QChar> m = {
        {u'0', u'₀'}, {u'1', u'₁'}, {u'2', u'₂'}, {u'3', u'₃'}, {u'4', u'₄'},
        {u'5', u'₅'}, {u'6', u'₆'}, {u'7', u'₇'}, {u'8', u'₈'}, {u'9', u'₉'},
        {u'+', u'₊'}, {u'-', u'₋'}, {u'=', u'₌'}, {u'(', u'₍'}, {u')', u'₎'},
        {u'a', u'ₐ'}, {u'e', u'ₑ'}, {u'o', u'ₒ'}, {u'x', u'ₓ'}, {u'h', u'ₕ'},
        {u'k', u'ₖ'}, {u'l', u'ₗ'}, {u'm', u'ₘ'}, {u'n', u'ₙ'}, {u'p', u'ₚ'},
        {u's', u'ₛ'}, {u't', u'ₜ'}, {u'i', u'ᵢ'}, {u'j', u'ⱼ'}, {u'r', u'ᵣ'},
        {u'u', u'ᵤ'}, {u'v', u'ᵥ'},
    };
    return m;
}

// Comandos LaTeX de un simbolo → su equivalente Unicode.
const QList<QPair<QString, QString>> &tablaSimbolos()
{
    static const QList<QPair<QString, QString>> t = {
        // Griegas minusculas
        {QStringLiteral("\\alpha"), QStringLiteral("α")},
        {QStringLiteral("\\beta"), QStringLiteral("β")},
        {QStringLiteral("\\gamma"), QStringLiteral("γ")},
        {QStringLiteral("\\delta"), QStringLiteral("δ")},
        {QStringLiteral("\\epsilon"), QStringLiteral("ε")},
        {QStringLiteral("\\varepsilon"), QStringLiteral("ε")},
        {QStringLiteral("\\zeta"), QStringLiteral("ζ")},
        {QStringLiteral("\\eta"), QStringLiteral("η")},
        {QStringLiteral("\\theta"), QStringLiteral("θ")},
        {QStringLiteral("\\vartheta"), QStringLiteral("θ")},
        {QStringLiteral("\\iota"), QStringLiteral("ι")},
        {QStringLiteral("\\kappa"), QStringLiteral("κ")},
        {QStringLiteral("\\lambda"), QStringLiteral("λ")},
        {QStringLiteral("\\mu"), QStringLiteral("μ")},
        {QStringLiteral("\\nu"), QStringLiteral("ν")},
        {QStringLiteral("\\xi"), QStringLiteral("ξ")},
        {QStringLiteral("\\pi"), QStringLiteral("π")},
        {QStringLiteral("\\rho"), QStringLiteral("ρ")},
        {QStringLiteral("\\sigma"), QStringLiteral("σ")},
        {QStringLiteral("\\tau"), QStringLiteral("τ")},
        {QStringLiteral("\\upsilon"), QStringLiteral("υ")},
        {QStringLiteral("\\phi"), QStringLiteral("φ")},
        {QStringLiteral("\\varphi"), QStringLiteral("φ")},
        {QStringLiteral("\\chi"), QStringLiteral("χ")},
        {QStringLiteral("\\psi"), QStringLiteral("ψ")},
        {QStringLiteral("\\omega"), QStringLiteral("ω")},
        // Griegas mayusculas
        {QStringLiteral("\\Gamma"), QStringLiteral("Γ")},
        {QStringLiteral("\\Delta"), QStringLiteral("Δ")},
        {QStringLiteral("\\Theta"), QStringLiteral("Θ")},
        {QStringLiteral("\\Lambda"), QStringLiteral("Λ")},
        {QStringLiteral("\\Xi"), QStringLiteral("Ξ")},
        {QStringLiteral("\\Pi"), QStringLiteral("Π")},
        {QStringLiteral("\\Sigma"), QStringLiteral("Σ")},
        {QStringLiteral("\\Phi"), QStringLiteral("Φ")},
        {QStringLiteral("\\Psi"), QStringLiteral("Ψ")},
        {QStringLiteral("\\Omega"), QStringLiteral("Ω")},
        // Operadores y relaciones
        {QStringLiteral("\\cdot"), QStringLiteral("·")},
        {QStringLiteral("\\times"), QStringLiteral("×")},
        {QStringLiteral("\\div"), QStringLiteral("÷")},
        {QStringLiteral("\\pm"), QStringLiteral("±")},
        {QStringLiteral("\\mp"), QStringLiteral("∓")},
        {QStringLiteral("\\leq"), QStringLiteral("≤")},
        {QStringLiteral("\\le"), QStringLiteral("≤")},
        {QStringLiteral("\\geq"), QStringLiteral("≥")},
        {QStringLiteral("\\ge"), QStringLiteral("≥")},
        {QStringLiteral("\\neq"), QStringLiteral("≠")},
        {QStringLiteral("\\ne"), QStringLiteral("≠")},
        {QStringLiteral("\\approx"), QStringLiteral("≈")},
        {QStringLiteral("\\equiv"), QStringLiteral("≡")},
        {QStringLiteral("\\propto"), QStringLiteral("∝")},
        {QStringLiteral("\\infty"), QStringLiteral("∞")},
        {QStringLiteral("\\partial"), QStringLiteral("∂")},
        {QStringLiteral("\\nabla"), QStringLiteral("∇")},
        {QStringLiteral("\\int"), QStringLiteral("∫")},
        {QStringLiteral("\\iint"), QStringLiteral("∬")},
        {QStringLiteral("\\oint"), QStringLiteral("∮")},
        {QStringLiteral("\\sum"), QStringLiteral("Σ")},
        {QStringLiteral("\\prod"), QStringLiteral("Π")},
        {QStringLiteral("\\in"), QStringLiteral("∈")},
        {QStringLiteral("\\notin"), QStringLiteral("∉")},
        {QStringLiteral("\\subset"), QStringLiteral("⊂")},
        {QStringLiteral("\\cup"), QStringLiteral("∪")},
        {QStringLiteral("\\cap"), QStringLiteral("∩")},
        {QStringLiteral("\\forall"), QStringLiteral("∀")},
        {QStringLiteral("\\exists"), QStringLiteral("∃")},
        {QStringLiteral("\\angle"), QStringLiteral("∠")},
        {QStringLiteral("\\degree"), QStringLiteral("°")},
        {QStringLiteral("\\circ"), QStringLiteral("°")},
        {QStringLiteral("\\rightarrow"), QStringLiteral("→")},
        {QStringLiteral("\\to"), QStringLiteral("→")},
        {QStringLiteral("\\leftarrow"), QStringLiteral("←")},
        {QStringLiteral("\\Rightarrow"), QStringLiteral("⇒")},
        {QStringLiteral("\\leftrightarrow"), QStringLiteral("↔")},
        // Puntos suspensivos
        {QStringLiteral("\\ldots"), QStringLiteral("…")},
        {QStringLiteral("\\cdots"), QStringLiteral("…")},
        {QStringLiteral("\\dots"), QStringLiteral("…")},
        {QStringLiteral("\\vdots"), QStringLiteral("⋮")},
    };
    return t;
}

}  // namespace


namespace StudiaTexto {

QString aSuperindice(const QString &s)
{
    QString out;
    for (const QChar c : s) {
        const auto it = mapaSuper().constFind(c);
        if (it == mapaSuper().constEnd())
            return {};          // sin equivalente: el llamador usa ^
        out.append(it.value());
    }
    return out;
}

QString aSubindice(const QString &s)
{
    QString out;
    for (const QChar c : s) {
        const auto it = mapaSub().constFind(c);
        if (it == mapaSub().constEnd())
            return {};
        out.append(it.value());
    }
    return out;
}

QString quitarDelimitadores(const QString &entrada)
{
    QString s = entrada;
    // Delimitadores de math en LaTeX. Se sacan; el contenido queda inline.
    s.replace(QStringLiteral("\\[") , QStringLiteral(" "));
    s.replace(QStringLiteral("\\]") , QStringLiteral(" "));
    s.replace(QStringLiteral("\\(") , QStringLiteral(" "));
    s.replace(QStringLiteral("\\)") , QStringLiteral(" "));
    s.replace(QStringLiteral("$$")  , QStringLiteral(" "));
    // $ suelto: sólo si el texto lo usa como delimitador (número par).
    if (s.count(QLatin1Char('$')) % 2 == 0)
        s.remove(QLatin1Char('$'));
    // \left( \right] … no aportan nada al texto plano.
    s.remove(QRegularExpression(QStringLiteral("\\\\(left|right|big|Big|bigg|Bigg)\\b")));
    // Espaciados: \, \; \: \! \quad \qquad
    s.replace(QRegularExpression(QStringLiteral("\\\\(quad|qquad)\\b")), QStringLiteral(" "));
    s.replace(QRegularExpression(QStringLiteral("\\\\[,;:!]")), QStringLiteral(" "));
    return s;
}

QString convertirFracciones(const QString &entrada)
{
    QString s = entrada;
    for (int pasada = 0; pasada < kMaxPasadas; ++pasada) {
        // Se resuelve de adentro hacia afuera buscando el ULTIMO \frac: sus
        // argumentos ya no contienen otro \frac sin resolver.
        const int i = s.lastIndexOf(QStringLiteral("\\frac"));
        if (i < 0)
            break;
        int p = i + 5;
        while (p < s.size() && s.at(p).isSpace()) ++p;
        const int c1 = cierreDeLlave(s, p);
        if (c1 < 0) { s.replace(i, 5, QStringLiteral("frac")); continue; }  // malformado
        const QString num = s.mid(p + 1, c1 - p - 1);
        int q = c1 + 1;
        while (q < s.size() && s.at(q).isSpace()) ++q;
        const int c2 = cierreDeLlave(s, q);
        if (c2 < 0) { s.replace(i, 5, QStringLiteral("frac")); continue; }
        const QString den = s.mid(q + 1, c2 - q - 1);
        s.replace(i, c2 - i + 1,
                  QStringLiteral("%1/%2").arg(envolver(num.trimmed()),
                                              envolver(den.trimmed())));
    }
    // \sqrt{x} → √(x) ; \sqrt[3]{x} → ∛(x) no se cubre, se deja legible.
    for (int pasada = 0; pasada < kMaxPasadas; ++pasada) {
        const int i = s.lastIndexOf(QStringLiteral("\\sqrt"));
        if (i < 0) break;
        int p = i + 5;
        while (p < s.size() && s.at(p).isSpace()) ++p;
        const int c = cierreDeLlave(s, p);
        if (c < 0) { s.replace(i, 5, QStringLiteral("√")); continue; }
        s.replace(i, c - i + 1,
                  QStringLiteral("√(%1)").arg(s.mid(p + 1, c - p - 1).trimmed()));
    }
    return s;
}

QString convertirIndices(const QString &entrada)
{
    QString s = entrada;
    // Primero las formas con llaves: x^{n-1}, a_{max}
    static const QRegularExpression conLlaves(QStringLiteral("([\\^_])\\{([^{}]*)\\}"));
    for (int pasada = 0; pasada < kMaxPasadas; ++pasada) {
        const QRegularExpressionMatch m = conLlaves.match(s);
        if (!m.hasMatch())
            break;
        const bool arriba = (m.captured(1) == QLatin1String("^"));
        const QString cuerpo = m.captured(2);
        const QString uni = arriba ? aSuperindice(cuerpo) : aSubindice(cuerpo);
        // Si no hay equivalente Unicode se conserva la notación con ^/_ pero
        // sin llaves, que es como se escribe a mano.
        const QString reemplazo = uni.isEmpty()
            ? QStringLiteral("%1(%2)").arg(m.captured(1), cuerpo) : uni;
        s.replace(m.capturedStart(), m.capturedLength(), reemplazo);
    }
    // Luego las de un solo caracter: x^2, a_n
    static const QRegularExpression simple(QStringLiteral("([\\^_])([\\p{L}\\p{N}])"));
    for (int pasada = 0; pasada < kMaxPasadas * 10; ++pasada) {
        const QRegularExpressionMatch m = simple.match(s);
        if (!m.hasMatch())
            break;
        const bool arriba = (m.captured(1) == QLatin1String("^"));
        const QString uni = arriba ? aSuperindice(m.captured(2)) : aSubindice(m.captured(2));
        if (uni.isEmpty()) {
            // Sin equivalente: se deja tal cual, pero hay que avanzar para no
            // volver a encontrarlo (si no, el bucle no termina).
            static const QString marca = QStringLiteral("\x01");
            s.replace(m.capturedStart(), 1, marca);
            continue;
        }
        s.replace(m.capturedStart(), m.capturedLength(), uni);
    }
    s.replace(QStringLiteral("\x01"), QStringLiteral("^"));
    return s;
}

QString convertirSimbolos(const QString &entrada)
{
    QString s = entrada;
    // \text{...} y variantes tipográficas: se queda el contenido.
    static const QRegularExpression texto(
        QStringLiteral("\\\\(text|mathrm|mathbf|mathit|operatorname)\\s*\\{([^{}]*)\\}"));
    for (int pasada = 0; pasada < kMaxPasadas; ++pasada) {
        const QRegularExpressionMatch m = texto.match(s);
        if (!m.hasMatch()) break;
        s.replace(m.capturedStart(), m.capturedLength(), m.captured(2));
    }
    // Los comandos largos primero, para que \leq no se coma \le.
    QList<QPair<QString, QString>> tabla = tablaSimbolos();
    std::sort(tabla.begin(), tabla.end(),
              [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
    for (const auto &par : tabla)
        s.replace(par.first, par.second);
    return s;
}

QString latexALegible(const QString &texto)
{
    if (!texto.contains(QLatin1Char('\\')) && !texto.contains(QLatin1Char('$'))
        && !texto.contains(QLatin1Char('^')) && !texto.contains(QLatin1Char('_')))
        return texto;   // camino rápido: no hay nada que convertir

    QString s = quitarDelimitadores(texto);
    s = convertirSimbolos(s);
    s = convertirFracciones(s);
    s = convertirIndices(s);

    // Restos de sintaxis: \\ de salto de fila y & de alineación en matrices.
    s.replace(QStringLiteral("\\\\"), QStringLiteral("\n"));
    // Llaves sueltas que quedaron de agrupaciones ya resueltas.
    s.remove(QRegularExpression(QStringLiteral("(?<!\\\\)[{}]")));
    // Espacios duplicados que dejaron los delimitadores.
    s.replace(QRegularExpression(QStringLiteral("[ \\t]{2,}")), QStringLiteral(" "));
    return s;
}

QVariantList enBloques(const QString &textoCrudo)
{
    // Patrones de ecuacion "de display": la que el modelo pone en su propio
    // renglon. Se reconocen las tres formas que emite en la practica.
    static const QRegularExpression display(
        QStringLiteral(
            "\\\\\\[(.+?)\\\\\\]"          // \[ ... \]
            "|\\$\\$(.+?)\\$\\$"           // $$ ... $$
            // Renglon entero entre corchetes con al menos un comando LaTeX:
            // es como llega cuando el markdown se comio las barras.
            "|(?:^|\\n)[ \\t]*\\[[ \\t]*([^\\]\\n]*\\\\[^\\]\\n]*)\\][ \\t]*(?=\\n|$)"),
        QRegularExpression::DotMatchesEverythingOption);

    QVariantList bloques;
    auto agregar = [&bloques](const QString &tipo, const QString &contenido) {
        const QString c = contenido.trimmed();
        if (c.isEmpty())
            return;
        bloques.append(QVariantMap{{QStringLiteral("tipo"), tipo},
                                   {QStringLiteral("contenido"), c}});
    };

    int desde = 0;
    QRegularExpressionMatchIterator it = display.globalMatch(textoCrudo);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        // Cuerpo de la ecuacion: el grupo que haya capturado.
        QString cuerpo;
        for (int g = 1; g <= 3; ++g) {
            if (!m.captured(g).isNull()) { cuerpo = m.captured(g); break; }
        }
        if (cuerpo.trimmed().isEmpty())
            continue;
        agregar(QStringLiteral("texto"),
                latexALegible(textoCrudo.mid(desde, m.capturedStart() - desde)));
        agregar(QStringLiteral("ecuacion"), latexALegible(cuerpo));
        desde = m.capturedEnd();
    }
    agregar(QStringLiteral("texto"), latexALegible(textoCrudo.mid(desde)));

    if (bloques.isEmpty() && !textoCrudo.trimmed().isEmpty())
        agregar(QStringLiteral("texto"), latexALegible(textoCrudo));
    return bloques;
}

}  // namespace StudiaTexto
