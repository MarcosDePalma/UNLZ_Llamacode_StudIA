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
    // Rigor del modo. Los modos EXIGENTES producen material de estudio que se
    // va a usar como si fuera fiel al apunte (flashcards, autoevaluacion,
    // resumen, plan): ahi conviene abstenerse antes que arriesgar. Los modos
    // FLEXIBLES son conversacion: el estudiante quiere entender, y contestar
    // "no tengo informacion" ante una repregunta es inutil.
    bool exigente = true;
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

// Variante SIN fragmentos: la recuperacion no encontro nada nuevo pero la
// pregunta se puede contestar con lo que ya se hablo ("repetí la ecuación
// anterior", "explicalo más simple", "no entendí el paso 2"). Se usa sólo en
// modos flexibles y deja claro que no hay documentacion nueva de respaldo.
QString usuarioSoloConversacion(const QString &pregunta,
                                const QVector<Turno> &historial);
// ¿Vale la pena intentar responder desde la conversacion? Necesita historial
// util (al menos una respuesta previa con contenido).
bool puedeResponderDesdeConversacion(const QVector<Turno> &historial);

// ── Consulta de recuperacion ─────────────────────────────────────────────────

// ── Encuadre de la pregunta ──────────────────────────────────────────────────
//
// Antes de buscar hay que decidir si la pregunta se sostiene sola o si continúa
// la anterior. Las tres situaciones se ven parecidas si uno sólo cuenta
// palabras, pero pedirle al índice cuáles de esos términos EXISTEN las separa:
//
//   Autonoma    "¿qué es el criterio de Routh?"      → tiene términos propios
//               que ubican el tema. Se busca tal cual.
//   Dependiente "¿y cómo funciona?"                  → no aporta ningún término
//               con contenido. Continúa lo anterior: se le suma ese tema.
//   Ajena       "¿cómo hago milanesas?"              → aporta términos, pero
//               NINGUNO existe en el corpus. Es un cambio de tema hacia algo
//               que el material no cubre: NO se le suma el tema anterior.
//
// Distinguir Ajena de Dependiente es lo que evita que una pregunta fuera de
// tema herede el tema previo y termine respondiéndose con material que no le
// corresponde.
enum class Encuadre { Autonoma, Dependiente, Ajena };

// `propios`        términos útiles de la pregunta (sin palabras vacías)
// `existentes`     cuántos de ellos aparecen en el corpus (frecuencia > 0)
// `discriminantes` cuántos, además, son específicos (ver StudiaIndex)
Encuadre encuadrar(int propios, int existentes, int discriminantes);

// Arma la consulta con la que se busca en el indice, según el encuadre.
//   `anteriores` preguntas previas del usuario, de la mas reciente a la mas vieja
QString consultaConContexto(const QString &pregunta, const QStringList &anteriores,
                            Encuadre encuadre);
// Con al menos esta cantidad de terminos especificos, la pregunta se sostiene sola.
constexpr int kMinDiscriminantesAutonomos = 2;
// Tope de terminos de la consulta expandida.
constexpr int kMaxTerminosExpandida = 8;

}  // namespace StudiaPrompt
