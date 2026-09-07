#pragma once

#include "OcrTypes.h"

#include <QList>
#include <QRect>
#include <QString>

// Ubica un texto pedido ("Guardar como") dentro de un resultado de OCR y devuelve
// DÓNDE está en pantalla. Es la lógica que convierte "lo que se ve" en "a dónde
// apuntar", y es pura → se testea con líneas fabricadas, sin OCR ni pantalla.
//
// Es el ÚLTIMO recurso: UIA (desktop_controls/clickElement) da el árbol semántico
// real y siempre es preferible. Esto sólo sirve donde UIA está ciego (Canvas,
// juegos, apps que no exponen nada).
namespace OcrTextLocator {

struct Hit
{
    QRect rect;     // caja del texto encontrado, coords absolutas de pantalla
    QString text;   // el texto tal como lo leyó el OCR (para el trace/confirmación)
    int score = 0;  // 0..100 (FuzzyMatch::weightedRatio)
    int lineIndex = -1;
    bool ok() const { return score > 0 && !rect.isEmpty(); }
    QPoint center() const { return rect.center(); }
};

// Mejor coincidencia de `needle` entre las frases de palabras contiguas de cada
// línea. Devuelve un Hit vacío si nada supera `cutoff`.
//
// `cutoff` alto a propósito (85, más que el 75 de FuzzyMatch): un click a ciegas
// guiado por OCR es irreversible y el OCR además mete su propio ruido de lectura.
// Ante duda conviene no hacer nada y que el usuario repita.
Hit find(const QList<OcrLine> &lines, const QString &needle, int cutoff = 85);

// Todas las coincidencias sobre el cutoff, mejor primero. Sirve para detectar
// AMBIGÜEDAD: si hay dos "Aceptar" en pantalla, clickear "el primero" es una
// moneda al aire — el llamador debe preguntar en vez de adivinar.
QList<Hit> findAll(const QList<OcrLine> &lines, const QString &needle, int cutoff = 85);

}   // namespace OcrTextLocator
