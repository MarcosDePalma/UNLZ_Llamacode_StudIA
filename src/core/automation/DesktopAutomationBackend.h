#pragma once

#include "OcrTypes.h"

#include <QImage>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class DesktopAutomationBackend
{
public:
    static QVariantList screens();
    static QVariantList windows();
    static QVariantMap targetInfo(const QString &kind, const QString &targetId);
    // Snapshot compacto y determinista de un alcance. El snapshotId cambia si
    // cambia la ventana, su geometría o el árbol UIA; las acciones pueden usarlo
    // como stale guard antes de tocar el escritorio.
    static QVariantMap snapshot(const QString &kind, const QString &targetId,
                                const QString &query = QString(), int max = 120,
                                bool includeCapture = false, QString *error = nullptr);
    static bool validateSnapshot(const QString &kind, const QString &targetId,
                                 const QString &snapshotId, QString *error = nullptr);
    static QImage capture(const QString &kind, const QString &targetId, QString *error = nullptr);
    static QString saveCapture(const QString &kind, const QString &targetId,
                               const QString &path, QString *error = nullptr);

    static QPointF normalizePoint(const QPoint &absolute, const QRect &bounds);
    static QPoint denormalizePoint(const QPointF &normalized, const QRect &bounds);
    // Las tools públicas usan exclusivamente coordenadas normalizadas 0..1.
    // Validar evita clamp silencioso de una salida visual en grilla 0..1000.
    static bool isNormalizedPoint(double x, double y);

    static bool interactiveSessionAvailable();
    // Lanza una app DESPRENDIDA (no bloquea, no hereda pipes): a diferencia de
    // run_shell, lanzar una app GUI (calc, notepad, ms-settings:, una ruta .exe)
    // acá vuelve al instante y no cuelga el turno esperando que el proceso termine.
    // `app` = programa/comando/verbo del shell; `args` = argumentos extra (opcional).
    static bool launchApp(const QString &app, const QString &args, QString *error = nullptr);
    static bool focusWindow(const QString &targetId, QString *error = nullptr);
    // Restaura el estado visual enseñado antes de reproducir gestos anclados.
    // Es genérico para cualquier ventana Win32; no depende de la aplicación.
    static bool setWindowMaximized(const QString &targetId, bool maximized,
                                   QString *error = nullptr);
    // Ajusta sólo el tamaño exterior de una ventana restaurada, preservando su
    // posición. Evita repetir SetWindowPos cuando ya coincide (tolerancia DPI).
    static bool setWindowSize(const QString &targetId, int width, int height,
                              QString *error = nullptr);
    static bool click(const QString &kind, const QString &targetId, double x, double y,
                      const QString &button = QStringLiteral("left"),
                      QString *error = nullptr, QVariantMap *trace = nullptr);
    // Traza continua (Paint, sliders, gestos): apretar el botón en el primer punto,
    // arrastrar por la secuencia y soltar en el último. `points` = lista de {x,y}
    // NORMALIZADOS 0..1 dentro del alcance (mínimo 2). Interpola segmentos para que
    // la línea salga continua aunque los puntos vengan espaciados. `holdMs` = pausa
    // por segmento (default ~8ms). Sólo Windows.
    static bool stroke(const QString &kind, const QString &targetId,
                       const QVariantList &points, const QString &button = QStringLiteral("left"),
                       int holdMs = 8, QString *error = nullptr, QVariantMap *trace = nullptr);
    static bool typeText(const QString &text, QString *error = nullptr);
    static bool pressKey(const QString &key, const QStringList &modifiers = {},
                         QString *error = nullptr);
    static bool scroll(int delta, QString *error = nullptr);
    static QVariantMap cursorState();

    // Posición del cursor en píxeles FISICOS — el espacio en el que hablan TODAS
    // las coordenadas de este backend (bounds de targets, controlAtPoint, readText,
    // click). QCursor::pos() devuelve lógicos de Qt y NO sirve acá: en un monitor
    // escalado los dos espacios difieren y el punto termina en otro lado.
    static QPoint cursorPosPhysical();

    // Mueve el cursor a un punto FISICO (par de cursorPosPhysical). QCursor::setPos
    // no sirve: interpreta el punto como lógico.
    static bool moveCursor(const QPoint &physical);

    // ── UI Automation: el escritorio como árbol de controles (DOM-aware) ──
    // Enumera los controles (control-view) descendientes de una ventana: nombre,
    // rol, geometría, habilitado e invocable. `windowTargetId` = id hex de la
    // ventana (ver windows()). `query` filtra por substring del nombre (vacío =
    // todos). `max` acota la cantidad. Devuelve filas {controlId,name,role,x,y,
    // width,height,enabled,invokable}. controlId = RuntimeId serializado, estable
    // dentro de la vida de la ventana → usalo con clickElement. Sólo Windows.
    static QVariantList controls(const QString &windowTargetId, const QString &query,
                                 int max, QString *error = nullptr);
    // Click sobre un control por su controlId (de controls()): si expone el patrón
    // Invoke lo invoca (más robusto que pixel); si no, clickea el centro de su
    // bounding rect. Sólo Windows.
    //
    // Si el controlId no existe (el modelo pasó un NOMBRE en vez del RuntimeId, o
    // la ventana hizo reflow y el id quedó viejo), reintenta resolviéndolo como
    // nombre por matching difuso sobre los controles de la ventana. Sigue siendo
    // UIA: el fallback afloja la comparación del nombre, no adivina píxeles. El
    // trace anota matchedBy=fuzzy-name + matchScore cuando pasa por ahí.
    static bool clickElement(const QString &windowTargetId, const QString &controlId,
                             QString *error = nullptr, QVariantMap *trace = nullptr);
    static bool clickElement(const QString &windowTargetId, const QString &controlId,
                             const QString &snapshotId, QString *error,
                             QVariantMap *trace = nullptr);
    // Acciones UIA semánticas. No roban foco cuando el patrón correspondiente
    // existe; sólo el fallback Invoke puede requerir una acción foreground.
    // action: invoke, toggle, set_value, select, expand, collapse,
    // range_set, scroll_into_view, read.
    static bool controlAction(const QString &windowTargetId, const QString &controlId,
                              const QString &action, const QString &value = QString(),
                              QString *error = nullptr, QVariantMap *trace = nullptr);

    // ── OCR: leer el texto que se VE (último recurso, apps que UIA no expone) ──
    // Captura el alcance, lo pasa por OcrEngine y devuelve las líneas con sus rects
    // ya traducidos a coords LÓGICAS ABSOLUTAS de pantalla. Acá vive —una sola vez—
    // la corrección de escalado: el factor sale de comparar el tamaño real de la
    // captura (device px) contra el del alcance (lógicos), así sale bien con
    // cualquier DPI y en multi-monitor sin hardcodear nada.
    // La captura se OCRea en RAM y se descarta: nunca toca el disco.
    static QList<OcrLine> readText(const QString &kind, const QString &targetId,
                                   QString *error = nullptr);

    // Clickea el texto visible que mejor matchee `text`. Preferí SIEMPRE
    // clickElement (UIA): esto es para cuando el árbol no expone el control.
    // Si hay dos coincidencias igual de buenas NO adivina: falla pidiendo precisión
    // (clickear "el primero" de dos botones iguales es una moneda al aire).
    // `clickCount` 2 = doble clic (dos clics seguidos dentro del tiempo del sistema).
    static bool clickText(const QString &kind, const QString &targetId, const QString &text,
                          const QString &button = QStringLiteral("left"), int clickCount = 1,
                          QString *error = nullptr, QVariantMap *trace = nullptr);

    // Fallback visual para canvas, iconos y escritorios remotos que no exponen
    // UIA ni texto. Devuelve rect normalizado + confianza; clickImage se abstiene
    // si hay dos matches equivalentes o cambia la geometría del target.
    static QVariantMap findImage(const QString &kind, const QString &targetId,
                                 const QString &templatePath, double threshold = 0.88,
                                 double minScale = 1.0, double maxScale = 1.0,
                                 bool requireUnique = true, QString *error = nullptr);
    static bool clickImage(const QString &kind, const QString &targetId,
                           const QString &templatePath, double threshold = 0.88,
                           double minScale = 1.0, double maxScale = 1.0,
                           const QString &button = QStringLiteral("left"),
                           QString *error = nullptr, QVariantMap *trace = nullptr);
    static QVariantMap waitImage(const QString &kind, const QString &targetId,
                                 const QString &templatePath, bool appear = true,
                                 int timeoutMs = 4000, double threshold = 0.88,
                                 double minScale = 1.0, double maxScale = 1.0,
                                 QString *error = nullptr);
    static QVariantMap assertImage(const QString &kind, const QString &targetId,
                                   const QString &templatePath, bool shouldExist = true,
                                   int timeoutMs = 1500, double threshold = 0.88,
                                   double minScale = 1.0, double maxScale = 1.0,
                                   QString *error = nullptr);

    // Control UIA bajo un punto absoluto de pantalla. Lo usa el grabador Teach para
    // anclar cada click/stroke a un control semántico (name/role/controlId + la
    // ventana dueña) en vez de sólo coordenadas → replay robusto ante reflow de UI.
    // Devuelve {} si no hay elemento o UIA no está. Sólo Windows.
    static QVariantMap controlAtPoint(const QPoint &absolute);

    // Espera (poll) hasta que exista una condición o venza el timeout. Sincroniza
    // el replay sin sleeps fijos. Un caso por parámetros:
    //  - windowTitle no vacío → espera una ventana cuyo título contenga el texto.
    //  - windowTargetId + (query|role) → espera un control (name contiene query,
    //    role coincide) dentro de esa ventana.
    // Devuelve {found:bool, elapsedMs, ...datos del match}. Sólo Windows.
    static QVariantMap waitFor(const QString &windowTargetId, const QString &windowTitle,
                               const QString &query, const QString &role, int timeoutMs,
                               QString *error = nullptr);

    // Aserción verificable para el replay/RPA: comprueba una condición y devuelve
    // {pass:bool, detail}. Casos (poll hasta timeout):
    //  - expectText no vacío → pasa si algún control (de la ventana target, o de
    //    cualquier ventana si no se da target) tiene ese texto en su nombre.
    //  - si no, delega en waitFor (existencia de ventana por título / control por
    //    query/role). Es el primitivo que hace el objetivo comprobable, no opinable.
    static QVariantMap assertCondition(const QString &windowTargetId, const QString &windowTitle,
                                       const QString &query, const QString &role,
                                       const QString &expectText, int timeoutMs,
                                       QString *error = nullptr);

private:
    // `allowFuzzy` corta la recursión: el reintento por nombre resuelve un
    // controlId exacto y re-entra con false, así nunca hay más de un rebote.
    static bool clickElementInternal(const QString &windowTargetId, const QString &controlId,
                                     bool allowFuzzy, QString *error, QVariantMap *trace);
};
