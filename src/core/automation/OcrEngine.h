#pragma once

#include "OcrTypes.h"

#include <QImage>
#include <QList>
#include <QString>

// OCR local vía Windows.Media.Ocr (WinRT), el motor que ya trae Windows 10/11.
//
// Por qué éste y no easyocr/tesseract: no agrega NINGUNA dependencia (ni torch,
// ni modelos que descargar, ni un runtime Python), corre en CPU, y los paquetes
// de idioma son los que el usuario ya tiene instalados en el sistema. Para leer
// labels de UI —texto corto, nítido, renderizado— alcanza y sobra; no estamos
// haciendo OCR de fotos.
//
// Es un ÚLTIMO RECURSO dentro de la automatización: UIA (desktop_controls) ve el
// árbol real de controles y siempre es preferible. Esto es para donde UIA es
// ciego.
namespace OcrEngine {

// ¿Hay un motor OCR utilizable? False si Windows no tiene ningún paquete de
// idioma OCR instalado (o no es Windows). Barato: cachea el resultado.
bool available();

// Idioma que se va a usar (BCP-47, ej "es-MX"), o "" si no hay motor. Para
// diagnosticar "¿por qué no lee mis botones en español?".
QString languageTag();

// Nombre legible del idioma del motor (ej "Español (México)"), o "" si no hay.
// Es lo que se muestra en la UI: "es-MX" no le dice nada a nadie.
QString languageName();

// Reconoce el texto de `image`. Los rects vuelven en PÍXELES DE LA IMAGEN — el
// llamador los traduce a pantalla (ver DesktopAutomationBackend::readText).
// Devuelve {} y setea `error` si no hay motor o falla el reconocimiento.
//
// Bloquea hasta terminar (decenas de ms para una pantalla): la API de WinRT es
// async y se la espera en un hilo MTA propio, porque bloquear la espera en el
// hilo STA de la GUI deadlockea.
QList<OcrLine> recognize(const QImage &image, QString *error = nullptr);

}   // namespace OcrEngine
