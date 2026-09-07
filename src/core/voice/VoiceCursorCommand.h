#pragma once

#include <QString>

// Comandos de cursor por voz (feature de accesibilidad, opt-in).
//
// Con `cursorOcr` activo, Charla intercepta frases como "clic en Guardar" ANTES
// de mandarlas al LLM: se OCRea la pantalla, se ubica el texto y se mueve/clickea
// el cursor. Todo lo demás sigue de largo al modelo como siempre.
//
// El parseo es puro → testeable sin micrófono, sin STT y sin pantalla.
namespace VoiceCursorCommand {

enum class Kind {
    None,        // no es un comando de cursor: va al LLM
    Move,        // "mover a X" / "cursor a X"
    Click,       // "clic en X"
    DoubleClick, // "doble clic en X"
    RightClick,  // "clic derecho en X"
};

struct Command
{
    Kind kind = Kind::None;
    QString target;   // el texto a buscar en pantalla
    bool ok() const { return kind != Kind::None && !target.isEmpty(); }
};

// Parsea un transcript. Devuelve Kind::None si no es un comando de cursor.
//
// Deliberadamente ESTRICTO: sólo matchea frases que arrancan con un verbo de
// cursor conocido. Charla es una conversación — si el parseo fuera laxo, decir
// "tendría que hacer clic en Guardar, ¿no?" secuestraría el turno en vez de
// contestarte. Ante duda, gana el LLM.
Command parse(const QString &transcript);

}   // namespace VoiceCursorCommand
