#pragma once
#include <QString>

// Extrae texto/markdown de documentos para inlinear al contexto del LLM.
//  - Texto plano / código  → lectura directa.
//  - PDF / Office / HTML / EPUB → sidecar Python (markitdown).
//  - Imágenes → "" (las maneja la visión/mmproj, no se extrae texto).
// Cache por md5 del CONTENIDO (ediciones invalidan). Cap de tamaño.
namespace DocumentExtractor {

// Devuelve markdown/texto, o "" si no aplica (imagen) o falla.
// Si err != nullptr se setea con el motivo del fallo (vacío si ok o n/a).
QString extract(const QString &path, QString *err = nullptr);

// ¿La extensión es de imagen? (se maneja por visión, no se extrae)
bool isImage(const QString &path);

// ¿La extensión es de un formato "rico" (pdf/office/epub/html) que NO se puede
// leer como texto plano y necesita el sidecar? Lo usa el indexador del RAG
// híbrido para decidir a qué archivos vale la pena pagarles la extracción.
bool isRich(const QString &path);

}  // namespace DocumentExtractor
