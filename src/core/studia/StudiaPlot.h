#pragma once
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class QProcess;

// Render de los bloques ```grafico de una respuesta a PNG, vía el sidecar
// tools/studia/graficar.py (matplotlib).
//
// Misma forma de uso que MermaidRenderer, para que la UI trate los dos igual:
// hash del source → PNG cacheado en AppLocalData/LlamaCode/studia/graficos.
// El render es async (QProcess) y se deduplica por hash: pedir dos veces el
// mismo gráfico no lanza dos procesos.
//
// El sidecar NO ejecuta código Python del modelo: interpreta un formato
// declarativo y evalúa la expresión en un espacio de nombres cerrado.
class StudiaPlot : public QObject
{
    Q_OBJECT
    // ¿Se puede graficar en esta máquina? (python + matplotlib + el script).
    Q_PROPERTY(bool disponible READ disponible CONSTANT)

public:
    explicit StudiaPlot(QObject *parent = nullptr);

    bool disponible() const;
    // Motivo por el que no está disponible ("" si lo está). Para poder decirle
    // al usuario qué le falta en vez de fallar en silencio.
    Q_INVOKABLE QString motivoNoDisponible() const;

    // md5 del source: identidad del gráfico y clave de cache.
    Q_INVOKABLE QString sourceHash(const QString &source) const;
    // Ruta del PNG ya generado, o "" si todavía no se generó.
    Q_INVOKABLE QString cachedPath(const QString &source) const;
    // Dispara el render. Emite renderReady o renderFailed. Idempotente.
    Q_INVOKABLE void requestRender(const QString &source);

signals:
    void renderReady(const QString &hash, const QString &pngPath);
    void renderFailed(const QString &hash, const QString &motivo);

private:
    static QString cacheDir();
    static QString rutaScript();

    QSet<QString> m_enVuelo;   // hashes que se están generando (dedupe)
};
