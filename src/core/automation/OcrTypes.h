#pragma once

#include <QList>
#include <QRect>
#include <QString>

// Resultado de OCR sobre una captura.
//
// El espacio de `rect` lo documenta QUIEN lo produce, y hay exactamente dos:
//   - OcrEngine::recognize() → píxeles de la IMAGEN (device px).
//   - DesktopAutomationBackend::readText() → píxeles LÓGICOS ABSOLUTOS de
//     pantalla, ya listos para apuntar.
// La conversión entre ambos (dividir por el devicePixelRatio y correr por el
// origen del alcance) vive en un solo lugar: readText(), que es el único que sabe
// con qué escala se capturó. Repartir esa cuenta entre los consumidores es cómo
// estos proyectos terminan clickeando 40 píxeles al costado en pantallas con
// escalado al 150%.
struct OcrWord
{
    QString text;
    QRect rect;
};

// Una línea reconocida, con sus palabras. Preservamos la línea (y no una sopa de
// palabras sueltas) porque un label suele ser multi-palabra ("Guardar como") y
// sólo tiene sentido buscarlo entre palabras contiguas de la MISMA línea.
struct OcrLine
{
    QString text;
    QList<OcrWord> words;
};
