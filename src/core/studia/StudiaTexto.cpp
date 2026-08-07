#include "StudiaTexto.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QVariantMap>

#include <algorithm>
#include <functional>

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

bool esLineaEcuacion(const QString &linea)
{
    QString s = linea.trimmed();
    if (s.isEmpty() || s.size() > 220)
        return false;

    // Si todo el renglon es un span de codigo (`...`), se destapa: el modelo
    // suele envolver las formulas en backticks aunque se le pida que no.
    if (s.startsWith(QLatin1Char('`')) && s.endsWith(QLatin1Char('`')) && s.size() > 2) {
        s = s.mid(1, s.size() - 2).trimmed();
        s.remove(QLatin1Char('`'));
    }
    if (s.isEmpty())
        return false;

    // Estructura de Markdown: titulos, listas, citas, tablas. No son ecuaciones.
    static const QRegularExpression estructura(
        QStringLiteral("^(#{1,6}\\s|[-*+]\\s|>\\s|\\||\\d+[.)]\\s)"));
    if (estructura.match(s).hasMatch())
        return false;
    // Una oracion terminada en punto es prosa.
    if (s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char(':')))
        return false;

    // Tiene que haber algun signo matematico.
    static const QString signos = QStringLiteral("=≤≥≈≠∫∬∮Σ∏√∂∇→⇒·×÷±∞^_");
    bool hayMatematica = false;
    for (const QChar c : s) {
        if (signos.contains(c)) { hayMatematica = true; break; }
    }
    // Los superindices/subindices Unicode tambien cuentan (ya convertidos).
    if (!hayMatematica) {
        for (const QChar c : s) {
            const ushort u = c.unicode();
            if ((u >= 0x2070 && u <= 0x209F) || u == 0x00B2 || u == 0x00B3 || u == 0x00B9) {
                hayMatematica = true;
                break;
            }
        }
    }
    if (!hayMatematica)
        return false;

    // Y casi nada de prosa: una ecuacion tiene simbolos, no palabras largas.
    // "La velocidad v = d/t es constante" tiene 2 y NO es un bloque de ecuacion;
    // "∫∫S f(x,y,z) dS = ∫∫D f(r(u,v)) dA" no tiene ninguna.
    static const QRegularExpression palabraLarga(
        QStringLiteral("(?<![\\p{L}])[\\p{L}]{4,}(?![\\p{L}])"));
    int prosa = 0;
    QRegularExpressionMatchIterator it = palabraLarga.globalMatch(s);
    while (it.hasNext()) { it.next(); ++prosa; }
    return prosa <= 1;
}

// Parte el texto en tramos, separando los bloques cercados ```mermaid y
// ```grafico. Sale a la UI como bloques propios que se renderizan a imagen.
static void separarCercados(const QString &crudo, QVariantList *bloques,
                            const std::function<void(const QString &, const QString &)> &agregar,
                            const std::function<void(const QString &)> &procesarTexto)
{
    Q_UNUSED(bloques)
    static const QRegularExpression cercado(
        QStringLiteral("```[ \\t]*(mermaid|grafico|gráfico|plot)[ \\t]*\\n(.*?)```"),
        QRegularExpression::DotMatchesEverythingOption
        | QRegularExpression::CaseInsensitiveOption);

    int desde = 0;
    QRegularExpressionMatchIterator it = cercado.globalMatch(crudo);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        procesarTexto(crudo.mid(desde, m.capturedStart() - desde));
        const QString lenguaje = m.captured(1).toLower();
        const QString cuerpo = m.captured(2).trimmed();
        if (!cuerpo.isEmpty()) {
            agregar(lenguaje == QLatin1String("mermaid") ? QStringLiteral("mermaid")
                                                         : QStringLiteral("grafico"),
                    cuerpo);
        }
        desde = m.capturedEnd();
    }
    procesarTexto(crudo.mid(desde));
}

QString recortarTrasAbstencion(const QString &respuesta, const QString &frase)
{
    if (frase.isEmpty())
        return respuesta;
    const int i = respuesta.indexOf(frase);
    if (i < 0 || i > kMargenAbstencion)
        return respuesta;
    // Se conserva lo que haya antes (a veces abre con un "Lamentablemente…")
    // más la frase, y se descarta el resto.
    return respuesta.left(i + frase.size()).trimmed();
}

QString aHtmlConInterlineado(const QString &texto, bool markdown, int porcentaje,
                             int pixeles)
{
    QTextDocument doc;
    // El tamano tiene que ir DENTRO del HTML: al exportar, Qt escribe el
    // font-size del documento en el <body>, y ese le gana al font.pixelSize del
    // TextEdit. Sin esto el texto sale con el tamano por defecto de Qt (9pt,
    // ~12px) y se ve mas chico que antes de pasar a texto seleccionable.
    //
    // Los titulos no se tocan: Qt los exporta como `x-large`, que es relativo
    // al cuerpo, asi que escalan solos.
    QFont f = doc.defaultFont();
    f.setPointSizeF(pixeles * 0.75);   // 96 dpi logicos: 1 pt = 4/3 px
    doc.setDefaultFont(f);

    if (markdown)
        doc.setMarkdown(texto);
    else
        doc.setPlainText(texto);

    // El interlineado se pone bloque por bloque y no con un <div> envolvente:
    // asi lo heredan tambien los items de lista y los titulos, que Qt exporta
    // como bloques propios.
    QTextCursor cur(&doc);
    cur.beginEditBlock();
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        QTextBlockFormat f = b.blockFormat();
        f.setLineHeight(porcentaje, QTextBlockFormat::ProportionalHeight);
        cur.setPosition(b.position());
        cur.setBlockFormat(f);
    }
    cur.endEditBlock();
    return doc.toHtml();
}

QString tituloDeRespuesta(const QString &respuesta)
{
    // Titulo Markdown: "## Criterio de Routh". Es lo que se busca primero.
    static const QRegularExpression encabezado(
        QStringLiteral("^[ \\t]*#{1,6}[ \\t]+(.+)$"),
        QRegularExpression::MultilineOption);
    // Renglon entero en negrita: "**Criterio de Routh**", que el modelo usa
    // como titulo cuando no pone almohadillas.
    static const QRegularExpression negritaSola(
        QStringLiteral("^[ \\t]*\\*\\*(.+?)\\*\\*[ \\t]*:?[ \\t]*$"),
        QRegularExpression::MultilineOption);

    QString t;
    if (const auto m = encabezado.match(respuesta); m.hasMatch())
        t = m.captured(1);
    else if (const auto m2 = negritaSola.match(respuesta); m2.hasMatch())
        t = m2.captured(1);
    else {
        // Sin titulo: la primera linea con texto, que suele ser la frase de
        // apertura y describe el tema igual.
        for (const QString &linea : respuesta.split(QLatin1Char('\n'))) {
            const QString l = linea.trimmed();
            if (!l.isEmpty() && !l.startsWith(QStringLiteral("```"))) {
                t = l;
                break;
            }
        }
    }

    // Se limpia el marcado que haya quedado: en un titulo los asteriscos y los
    // backticks se leen como basura.
    t.remove(QLatin1Char('*')).remove(QLatin1Char('`')).remove(QLatin1Char('#'));
    t.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    t = t.trimmed();
    while (t.endsWith(QLatin1Char(':')) || t.endsWith(QLatin1Char('.')))
        t.chop(1);
    t = t.trimmed();
    if (t.isEmpty())
        return QString();
    if (t.size() <= kLargoTitulo)
        return t;
    // Se corta en el ultimo espacio para no partir una palabra al medio.
    QString corto = t.left(kLargoTitulo);
    const int esp = corto.lastIndexOf(QLatin1Char(' '));
    if (esp > kLargoTitulo / 2)
        corto = corto.left(esp);
    return corto.trimmed() + QStringLiteral("…");
}

QVariantList flashcards(const QString &respuesta)
{
    QVariantList tarjetas;
    // Reconoce el rotulo al principio del renglon y devuelve lo que viene
    // DESPUES en la misma linea. El modelo escribe indistintamente:
    //   **1. Frente**            (rotulo solo)
    //   **1. Frente** Pregunta   (rotulo y contenido en el mismo renglon)
    //   ### Frente:              / Dorso:
    // `encontrado` queda en false si el renglon no es ese rotulo.
    auto rotulo = [](const QString &linea, const QString &palabra, bool *encontrado) {
        *encontrado = false;
        QString s = linea.trimmed();
        s.remove(QLatin1Char('#'));
        s = s.trimmed();
        // Numeracion opcional, dentro o fuera de los asteriscos: "**1. Frente**".
        static const QRegularExpression num(QStringLiteral("^\\**\\s*\\d+[.)]\\s*"));
        s.remove(num);
        // El rotulo puede venir en negrita: se saca la marca de apertura.
        while (s.startsWith(QLatin1Char('*')))
            s.remove(0, 1);
        if (!s.startsWith(palabra, Qt::CaseInsensitive))
            return QString();
        QString resto = s.mid(palabra.size());
        // Lo que separa el rotulo del contenido: **, : o espacios.
        static const QRegularExpression sep(QStringLiteral("^[*:\\s.-]+"));
        const QString limpio = resto;
        resto.remove(sep);
        // Si no habia separador y sigue habiendo letras, era otra palabra que
        // empieza igual ("Frentes", "Dorsal"): no es el rotulo.
        if (resto == limpio && !resto.isEmpty())
            return QString();
        *encontrado = true;
        return resto.trimmed();
    };

    QString frente, dorso;
    int estado = 0;             // 0 = fuera, 1 = leyendo frente, 2 = leyendo dorso
    auto cerrar = [&]() {
        const QString f = frente.trimmed(), d = dorso.trimmed();
        if (!f.isEmpty() && !d.isEmpty())
            tarjetas.append(QVariantMap{{QStringLiteral("frente"), f},
                                        {QStringLiteral("dorso"), d}});
        frente.clear();
        dorso.clear();
    };

    for (const QString &linea : respuesta.split(QLatin1Char('\n'))) {
        bool hay = false;
        QString resto = rotulo(linea, QStringLiteral("Frente"), &hay);
        if (hay) {
            cerrar();
            estado = 1;
            if (!resto.isEmpty())
                frente += resto + QLatin1Char('\n');
            continue;
        }
        resto = rotulo(linea, QStringLiteral("Dorso"), &hay);
        if (hay) {
            estado = 2;
            if (!resto.isEmpty())
                dorso += resto + QLatin1Char('\n');
            continue;
        }
        if (linea.trimmed().startsWith(QStringLiteral("---"))) {
            cerrar();
            estado = 0;
            continue;
        }
        if (estado == 1)      frente += linea + QLatin1Char('\n');
        else if (estado == 2) dorso  += linea + QLatin1Char('\n');
    }
    cerrar();
    return tarjetas;
}

QString flashcardsATsv(const QVariantList &tarjetas)
{
    QStringList filas;
    for (const QVariant &v : tarjetas) {
        const QVariantMap t = v.toMap();
        // Anki separa campos por TAB y tarjetas por renglon: los saltos de
        // linea internos van como <br> (Anki interpreta HTML en los campos).
        auto plano = [](QString s) {
            s.replace(QLatin1Char('\t'), QLatin1Char(' '));
            s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            s.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
            return s.trimmed();
        };
        const QString f = plano(t.value(QStringLiteral("frente")).toString());
        const QString d = plano(t.value(QStringLiteral("dorso")).toString());
        if (!f.isEmpty() && !d.isEmpty())
            filas << f + QLatin1Char('\t') + d;
    }
    return filas.join(QLatin1Char('\n'));
}

QVariantList enBloques(const QString &textoCrudo)
{
    QVariantList bloques;
    auto agregar = [&bloques](const QString &tipo, const QString &contenido) {
        const QString c = contenido.trimmed();
        if (c.isEmpty())
            return;
        bloques.append(QVariantMap{{QStringLiteral("tipo"), tipo},
                                   {QStringLiteral("contenido"), c}});
    };

    // 0) Diagramas y gráficos: se sacan primero para que su contenido no pase
    //    por la conversión de LaTeX ni por el detector de ecuaciones.
    if (textoCrudo.contains(QStringLiteral("```"))) {
        separarCercados(textoCrudo, &bloques, agregar,
                        [&](const QString &tramo) {
                            for (const QVariant &b : enBloquesTexto(tramo))
                                bloques.append(b);
                        });
        return bloques;
    }
    return enBloquesTexto(textoCrudo);
}

// Igual que enBloques pero sin mirar bloques cercados: texto y ecuaciones.
QVariantList enBloquesTexto(const QString &textoCrudo)
{
    QVariantList bloques;
    auto agregar = [&bloques](const QString &tipo, const QString &contenido) {
        const QString c = contenido.trimmed();
        if (c.isEmpty())
            return;
        bloques.append(QVariantMap{{QStringLiteral("tipo"), tipo},
                                   {QStringLiteral("contenido"), c}});
    };

    // 1) Regiones explicitas de display math, que pueden ocupar varios renglones.
    static const QRegularExpression display(
        QStringLiteral("\\\\\\[(.+?)\\\\\\]"          // \[ ... \]
                       "|\\$\\$(.+?)\\$\\$"),         // $$ ... $$
        QRegularExpression::DotMatchesEverythingOption);

    QStringList tramos;          // texto suelto entre ecuaciones explicitas
    QVariantList explicitas;     // ecuaciones ya resueltas, intercaladas
    int desde = 0;
    QRegularExpressionMatchIterator it = display.globalMatch(textoCrudo);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString cuerpo = m.captured(1).isNull() ? m.captured(2) : m.captured(1);
        if (cuerpo.trimmed().isEmpty())
            continue;
        tramos << textoCrudo.mid(desde, m.capturedStart() - desde);
        explicitas.append(latexALegible(cuerpo));
        desde = m.capturedEnd();
    }
    tramos << textoCrudo.mid(desde);

    // 2) Dentro de cada tramo, los renglones que por si solos son una ecuacion
    //    (el modelo tambien las escribe sin delimitadores o entre backticks).
    for (int i = 0; i < tramos.size(); ++i) {
        QStringList acumulado;
        const QStringList lineas = tramos.at(i).split(QLatin1Char('\n'));
        for (const QString &cruda : lineas) {
            const QString convertida = latexALegible(cruda);
            if (esLineaEcuacion(convertida)) {
                agregar(QStringLiteral("texto"), acumulado.join(QLatin1Char('\n')));
                acumulado.clear();
                QString limpia = convertida.trimmed();
                if (limpia.startsWith(QLatin1Char('`')) && limpia.endsWith(QLatin1Char('`')))
                    limpia = limpia.mid(1, limpia.size() - 2).trimmed();
                // Los corchetes de \[ \] que el markdown dejo sueltos.
                if (limpia.startsWith(QLatin1Char('[')) && limpia.endsWith(QLatin1Char(']')))
                    limpia = limpia.mid(1, limpia.size() - 2).trimmed();
                agregar(QStringLiteral("ecuacion"), limpia);
            } else {
                acumulado << convertida;
            }
        }
        agregar(QStringLiteral("texto"), acumulado.join(QLatin1Char('\n')));
        if (i < explicitas.size())
            agregar(QStringLiteral("ecuacion"), explicitas.at(i).toString());
    }
    return bloques;
}

}  // namespace StudiaTexto
