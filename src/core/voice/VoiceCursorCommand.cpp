#include "VoiceCursorCommand.h"

#include <QRegularExpression>

namespace VoiceCursorCommand {

Command parse(const QString &transcript)
{
    Command cmd;
    const QString s = transcript.simplified();
    if (s.isEmpty()) return cmd;

    // Un patrón por variante. Anclados al inicio (^) a propósito: es lo que
    // distingue una ORDEN de una mención al pasar. "clic en Guardar" es un
    // comando; "no sé si hacer clic en Guardar" es charla y tiene que ir al LLM.
    //
    // El STT no pone tildes ni puntuación de forma confiable, así que se aceptan
    // "clic"/"click" y con/sin tilde donde aplica.
    struct Rule { Kind kind; const char *rx; };
    static const Rule rules[] = {
        // Los más específicos primero: "doble clic" y "clic derecho" contienen
        // "clic", así que si Click fuera antes se los comería.
        {Kind::DoubleClick,
         "^(?:hace?r?\\s+)?doble\\s+(?:cl[ií]c|click)\\s+(?:en|sobre)\\s+(.+)$"},
        {Kind::RightClick,
         "^(?:hace?r?\\s+)?(?:cl[ií]c|click)\\s+derecho\\s+(?:en|sobre)\\s+(.+)$"},
        {Kind::Click,
         "^(?:hace?r?\\s+)?(?:cl[ií]c|click|clickea?|presiona?r?|apreta?r?)\\s+"
         "(?:en|sobre)\\s+(.+)$"},
        // La tilde va DENTRO de la clase que sustituye a la vocal ("mov[ée]"), no
        // pegada después ("move[ée]" pediría m-o-v-e y ENCIMA una vocal: matchea
        // "movee", nunca "mové"). Vale para el voseo: mové / llevá / andá.
        {Kind::Move,
         "^(?:mov[ée]r?|llev[aá]r?|and[aá]|cursor)\\s+(?:el\\s+cursor\\s+)?"
         "(?:a|hasta|hacia)\\s+(.+)$"},
    };

    for (const Rule &r : rules) {
        // fromUtf8, NO fromLatin1: los patrones traen acentos ("cl[ií]c",
        // "move[ée]") y el archivo es UTF-8. fromLatin1 parte cada acento en dos
        // chars y rompe la clase de caracteres en silencio: "clíc" deja de matchear.
        const QRegularExpression rx(QString::fromUtf8(r.rx),
                                    QRegularExpression::CaseInsensitiveOption);
        const auto m = rx.match(s);
        if (!m.hasMatch()) continue;
        QString target = m.captured(1).trimmed();
        // Sacar puntuación de cierre que mete el STT ("clic en Guardar." → Guardar).
        while (!target.isEmpty() && (target.endsWith(QLatin1Char('.'))
                                     || target.endsWith(QLatin1Char(','))
                                     || target.endsWith(QLatin1Char('?'))
                                     || target.endsWith(QLatin1Char('!'))))
            target.chop(1);
        // Artículo inicial: se dice "clic en el botón Guardar", pero en pantalla
        // dice "Guardar". Quitarlo mejora el match sin cambiar la intención.
        // El `(?:\s+|$)` final es lo que hace que "clic en el botón" —sin nombrar
        // cuál— quede con destino vacío y caiga al LLM. Sin eso, iría a buscar el
        // texto literal "el botón" en pantalla, que no es lo que nadie pidió.
        static const QRegularExpression lead(
            QStringLiteral("^(?:el|la|los|las|un|una)\\s+(?:bot[oó]n|enlace|link|campo|"
                           "pesta[ñn]a|men[uú]|opci[oó]n)(?:\\s+|$)"),
            QRegularExpression::CaseInsensitiveOption);
        target.remove(lead);
        target = target.trimmed();
        // Sin destino no hay nada que ubicar en pantalla: que lo conteste el LLM.
        if (target.isEmpty()) return cmd;
        cmd.kind = r.kind;
        cmd.target = target;
        return cmd;
    }
    return cmd;
}

}   // namespace VoiceCursorCommand
