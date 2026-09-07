#pragma once

#include <QString>
#include <QStringList>

// Matching difuso de texto corto (nombres de controles UIA, títulos de ventana).
//
// Por qué: el modelo dice "Guardar como" y el control se llama "&Guardar como...".
// Un compare exacto falla por un ampersand de acelerador y tres puntos. Esto da
// tolerancia acotada SIN OCR ni heurística de píxeles: sigue operando sobre el
// árbol semántico de UIA, sólo afloja la comparación del nombre.
//
// Todas las funciones son puras y no dependen de Windows → testeables en cualquier
// plataforma. Los puntajes van 0..100 (100 = idéntico tras normalizar).
namespace FuzzyMatch {

// Normaliza para comparar: minúsculas, sin tildes, sin '&' de acelerador, sin
// puntos suspensivos ni puntuación de borde, espacios colapsados.
QString normalize(const QString &s);

// Similitud por indel (LCS): 100 * 2*LCS / (len(a)+len(b)). Simétrica.
int ratio(const QString &a, const QString &b);

// Mejor `ratio` entre el string corto y cada ventana del largo de su mismo tamaño.
// Sirve para "Guardar" dentro de "Guardar como...".
int partialRatio(const QString &a, const QString &b);

// `ratio` tras partir en tokens, ordenarlos alfabéticamente y re-unirlos.
// Sirve para "como guardar" vs "guardar como".
int tokenSortRatio(const QString &a, const QString &b);

// Combinación ponderada al estilo del WRatio de rapidfuzz (no es una port exacta:
// mismo espíritu, reglas propias). Toma el mejor entre ratio directo, token-sort y
// —sólo cuando los largos difieren bastante— partial, penalizado por la diferencia
// de largo para que un substring corto no puntúe como match perfecto.
int weightedRatio(const QString &a, const QString &b);

struct Match
{
    int index = -1;   // índice en `candidates`, -1 = sin match sobre el cutoff
    int score = 0;    // 0..100
    bool ok() const { return index >= 0; }
};

// Mejor candidato por `weightedRatio` con puntaje >= cutoff. Ante empate gana el
// de menor índice (orden de llegada), así el resultado es determinístico.
Match extractOne(const QString &needle, const QStringList &candidates, int cutoff = 75);

}   // namespace FuzzyMatch
