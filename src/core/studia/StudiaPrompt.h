#pragma once
#include "StudiaIndex.h"

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

// Construccion de prompts de StudIA. Todo estatico y sin estado: son funciones
// puras sobre texto, faciles de testear sin base de datos ni red.
//
// Se separa de StudiaController porque son dos responsabilidades distintas:
// aca vive QUE se le dice al modelo; alla, COMO se orquesta la conversacion.
namespace StudiaPrompt {

// ── Modos de tutor ───────────────────────────────────────────────────────────
// Cada modo cambia la CONSIGNA, no la documentacion: se recupera igual y sobre
// los mismos fragmentos se pide resumir, explicar, tomar examen, etc.
struct Modo {
    QString id;            // "flashcards"  → el usuario escribe /flashcards/ ...
    QString etiqueta;      // "Flashcards"  → lo que ve en el menu
    QString descripcion;   // ayuda corta para la UI
    QString instruccion;   // lo que se agrega al prompt de sistema
};

// Catalogo completo, en el orden en que se muestra.
QVector<Modo> modos();
// Para exponerlo a QML como lista de mapas.
QVariantList modosParaQml();
// Busca un modo por id. Devuelve un Modo con id vacio si no existe.
Modo modoPorId(const QString &id);
// El modo "conversacion normal" (sin consigna especial).
QString idModoLibre();

// Separa "/flashcards/ texto de la pregunta" en (id de modo, texto). Si no hay
// prefijo valido devuelve (idModoLibre(), texto original). Tolera espacios y
// mayusculas: "/Flashcards/  algo" y "/flashcards/algo" funcionan igual.
void separarModo(const QString &entrada, QString *idModo, QString *texto);

// ── Prompts ──────────────────────────────────────────────────────────────────

// Frase exacta con la que el sistema se abstiene. Constante del modulo: la UI
// la muestra y las pruebas de evaluacion la buscan tal cual.
QString fraseAbstencion();

// Prompt de sistema: reglas del asistente + materia + consigna del modo.
QString sistema(const QString &materia, const QString &idModo);

// Bloque de usuario: fragmentos numerados + historial breve + la pregunta.
// `historial` son los ultimos turnos (rol, contenido) ya recortados por el
// llamador; se etiquetan como contexto, NO como documentacion.
struct Turno { QString rol; QString contenido; };
QString usuario(const QString &pregunta,
                const QVector<StudiaFragmento> &frags,
                const QVector<Turno> &historial = {});

// ── Consulta de recuperacion ─────────────────────────────────────────────────

// Arma la consulta con la que se busca en el indice. Una repregunta corta
// ("¿y como funciona?") no tiene terminos propios suficientes: se completa con
// los de las preguntas anteriores para no perder el tema.
//   `pregunta`   texto actual del usuario
//   `anteriores` preguntas previas del usuario, de la mas reciente a la mas vieja
// Devuelve el texto a pasarle a StudiaIndex::buscar.
QString consultaConContexto(const QString &pregunta, const QStringList &anteriores);
// A partir de cuantos terminos propios se considera que la pregunta se sostiene
// sola y no hace falta arrastrar el tema anterior.
constexpr int kMinTerminosAutonomos = 3;
// Tope de terminos de la consulta expandida.
constexpr int kMaxTerminosExpandida = 8;

}  // namespace StudiaPrompt
