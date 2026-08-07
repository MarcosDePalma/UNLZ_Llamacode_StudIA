#pragma once
#include <QString>
#include <QVariantList>

// Limpieza del texto que devuelve el modelo, antes de mostrarlo.
//
// Aunque el prompt le pide explicitamente que NO use LaTeX, Qwen igual emite
// cosas como `\frac{d^n y(t)}{dt^n}` o `a_{n-1}`. Qt no renderiza LaTeX, asi
// que al estudiante le llegaba la formula cruda, ilegible. Estas funciones la
// convierten a notacion de texto plano con simbolos Unicode.
//
// No pretende ser un motor de LaTeX: cubre lo que aparece en apuntes de
// ingenieria (fracciones, indices, griegas, operadores, sumatorias). Lo que no
// reconoce lo deja como estaba, nunca lo rompe.
namespace StudiaTexto {

// Convierte las expresiones LaTeX del texto a algo legible. Idempotente en la
// practica: aplicar dos veces no empeora el resultado.
QString latexALegible(const QString &texto);

// Parte la respuesta en bloques para que la UI pueda mostrar las ecuaciones
// "de display" (las que el modelo escribe en su propio renglon) centradas y en
// cuerpo mas grande, como una ecuacion insertada en Word. El resto sigue siendo
// texto Markdown normal.
//
// Devuelve una lista de mapas {tipo, contenido} en el orden original, con
// tipo = "texto" | "ecuacion" | "mermaid" | "grafico". El contenido de texto y
// ecuacion ya viene pasado por latexALegible(); el de mermaid/grafico se
// entrega crudo, porque lo consume un renderizador y no el lector.
QVariantList enBloques(const QString &textoCrudo);
// Sólo texto y ecuaciones, sin mirar bloques cercados. Uso interno y de tests.
QVariantList enBloquesTexto(const QString &textoCrudo);

// ¿Este renglón, solo, es una ecuación? Se usa para las que el modelo escribe
// sin delimitadores (o entre backticks, como si fueran código). La heurística:
// tiene algún signo matemático y casi nada de prosa. Expuesta para testearla.
bool esLineaEcuacion(const QString &linea);

// Si la respuesta ARRANCA declarando que no hay información suficiente, corta
// todo lo que venga después y devuelve sólo esa frase.
//
// El modelo a veces se contradice: dice "no encontré información" y a
// continuación completa igual el formato del modo con secciones vacías o temas
// que no vienen al caso. Abstenerse es una decisión del sistema, no una
// sugerencia: si el modelo la declara, se cumple.
//
// Sólo actúa cuando la frase aparece al PRINCIPIO. Una respuesta que primero
// explica y después aclara "esto otro no está en el material" es legítima y no
// se toca.
QString recortarTrasAbstencion(const QString &respuesta, const QString &frase);
// Hasta qué posición se considera que la frase está "al principio".
constexpr int kMargenAbstencion = 220;

// ── Texto seleccionable ──────────────────────────────────────────────────────

// Convierte el contenido a HTML con interlineado, para mostrarlo en un TextEdit.
//
// El motivo es una limitacion de Qt: `Text` tiene `lineHeight` pero NO deja
// seleccionar con el mouse; `TextEdit` deja seleccionar pero no tiene
// `lineHeight`. Como se quieren las dos cosas, el interlineado se aplica en el
// HTML —donde TextEdit si lo respeta— en vez de en la propiedad del item.
//
// `markdown` distingue la respuesta de StudIA (que trae **negritas**, titulos y
// listas) del mensaje del estudiante, que es texto tal cual: interpretarlo como
// Markdown le comeria los asteriscos y guiones que haya escrito.
constexpr int kInterlineado = 150;   // %, equivale al lineHeight 1.5 anterior
constexpr int kCuerpoPx    = 16;     // el mismo tamano que tenia el Text
QString aHtmlConInterlineado(const QString &texto, bool markdown,
                             int porcentaje = kInterlineado,
                             int pixeles = kCuerpoPx);

// Titulo de un tema a partir del PRIMER TITULO de una respuesta.
//
// Se usa la respuesta y no la pregunta porque el modelo encabeza con el
// concepto ("## Criterio de Routh-Hurwitz") mientras que la pregunta suele ser
// coloquial y larga ("che, me explicas eso de la estabilidad?"). Si la
// respuesta no trae ningun titulo, cae a su primera linea con texto.
//
// Devuelve "" si no hay de donde sacarlo; el llamador decide que hacer (para
// una abstencion, dejar el tema sin nombre hasta que haya una respuesta real).
QString tituloDeRespuesta(const QString &respuesta);
constexpr int kLargoTitulo = 48;

// ── Consignas con las respuestas plegadas ────────────────────────────────────

// Un par de una autoevaluación o una tanda de flashcards.
struct ParQR { QString pregunta; QString respuesta; };

// Lee los pares "1. P: … / R: …" de una respuesta.
//
// El formato se eligió midiendo contra el modelo real. Pedirle que escriba
// PRIMERO las diez preguntas y DESPUÉS las diez respuestas, separadas por una
// línea, le sale mal: es una estructura global, y un 7B pierde el hilo a mitad
// de camino —intercalaba las respuestas, o cortaba después de las preguntas—.
// Escribir cada pregunta con su respuesta al lado es un patrón LOCAL que se
// repite diez veces, y eso sí lo sostiene.
//
// Separar las dos mitades es trabajo del sistema, que puede hacerlo siempre
// bien, no del modelo. Devuelve vacío si no reconoce ningún par.
QVector<ParQR> paresQR(const QString &respuesta);

// Parte una respuesta en (consigna, respuestas) para poder mostrar primero la
// consigna y desplegar el resto con un botón.
//
// Arma las dos mitades a partir de los pares. Si no los encuentra, prueba con
// una línea separadora explícita, por si el modelo la escribió igual. Si no
// hay ninguna de las dos, `respuestas` queda vacío y todo es consigna: la UI
// no ofrece desplegar nada, que es lo correcto.
struct ConsignaPartida { QString consigna; QString respuestas; };
ConsignaPartida partirConsigna(const QString &respuesta);

// ── Flashcards ───────────────────────────────────────────────────────────────

// Extrae las tarjetas de una respuesta del modo Flashcards. Tolerante al
// formato: busca los rotulos "Frente"/"Dorso" y, si no estan, cae a separar por
// la linea "---". Devuelve mapas {frente, dorso}; lista vacia si no reconoce
// ninguna tarjeta.
QVariantList flashcards(const QString &respuesta);

// Serializa las tarjetas al formato que importa Anki: un renglon por tarjeta,
// frente y dorso separados por TAB. Es el camino sin dependencias — Anki lo
// importa de fabrica (Archivo → Importar, separado por tabulaciones).
QString flashcardsATsv(const QVariantList &tarjetas);

// Piezas expuestas para poder testearlas por separado.
QString convertirFracciones(const QString &s);
QString convertirIndices(const QString &s);     // ^{} y _{}
QString convertirSimbolos(const QString &s);    // griegas y operadores
QString quitarDelimitadores(const QString &s);  // $, \[ \], \( \)

// Traduce a superindice/subindice Unicode. Devuelve "" si algun caracter no
// tiene equivalente (el llamador cae a la notacion con ^ o _).
QString aSuperindice(const QString &s);
QString aSubindice(const QString &s);

}  // namespace StudiaTexto
