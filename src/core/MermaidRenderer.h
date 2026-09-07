#pragma once
#include <QObject>
#include <QString>
#include <QSet>
#include <QVariantList>

// Renderiza bloques ```mermaid (sidecar mmdc, async) y ```svg (QSvgRenderer,
// sincrónico) del chat a PNG.
//  - Detección/split de bloques en C++ (testeable) → segments(): type
//    "text" | "mermaid" | "svg".
//  - mermaid: render async con QProcess; cache por md5 en AppLocalData/mermaid.
//  - svg: rasterizado a PNG con QSvgRenderer → no se depende del plugin SVG de
//    QtQuick en runtime; el pipeline PNG ya está desplegado.
// Bloques sin cerrar (streaming) quedan como texto: no se rinde a medias.
// Sidecar: env LLAMACODE_MMDC pisa; si no, se busca "mmdc" en PATH.
class MermaidRenderer : public QObject
{
    Q_OBJECT
    // ¿Está mmdc disponible? La UI lo usa para decidir si parsear mermaid.
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit MermaidRenderer(QObject *parent = nullptr);

    bool available() const;

    // Parte el contenido en segmentos {type:"text"|"mermaid", text:...}.
    // Sólo cierra bloques mermaid con fence de apertura Y cierre.
    Q_INVOKABLE QVariantList segments(const QString &content) const;

    // md5 hex del source (clave de cache/identidad de diagrama).
    Q_INVOKABLE QString sourceHash(const QString &source) const;

    // Ruta del PNG cacheado, o "" si todavía no se rindió.
    Q_INVOKABLE QString cachedPath(const QString &source) const;

    // Dispara el render async. Emite renderReady o renderFailed. Idempotente:
    // si ya hay cache emite ready directo; si está en vuelo, no relanza.
    Q_INVOKABLE void requestRender(const QString &source);

    // Rasteriza un bloque ```svg a PNG (sincrónico, sin sidecar) vía QSvgRenderer.
    // Cachea por md5 en AppLocalData/mermaid (sufijo .svg.png). Devuelve la ruta
    // del PNG o "" si el SVG es inválido / no se pudo escribir.
    Q_INVOKABLE QString renderSvg(const QString &source);

    // Exporta el artifact renderizado a PNG en destPath. mermaid: usa el PNG
    // cacheado (false si todavía no se rindió). svg: rasteriza si hace falta.
    // Devuelve true si copió el archivo.
    Q_INVOKABLE bool exportPng(const QString &source, const QString &destPath);

    // Escribe el source crudo (svg/mermaid) tal cual en destPath. Devuelve true
    // si escribió todos los bytes.
    Q_INVOKABLE bool exportSource(const QString &source, const QString &destPath);

    // Helper estático para tests: split puro sin instanciar QML.
    static QVariantList splitSegments(const QString &content);

signals:
    void renderReady(const QString &hash, const QString &pngPath);
    void renderFailed(const QString &hash, const QString &reason);

private:
    static QString cacheDir();
    static QString mmdcExe();

    QSet<QString> m_pending;  // hashes en vuelo (dedupe)
};
