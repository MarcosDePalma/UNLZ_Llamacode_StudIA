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
// Devuelve una lista de mapas {tipo, contenido} con tipo = "texto" | "ecuacion",
// en el orden original. El contenido ya viene pasado por latexALegible().
// Si no hay ecuaciones de display devuelve un unico bloque de texto.
QVariantList enBloques(const QString &textoCrudo);

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
