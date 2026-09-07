#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <QVariantList>

#include "TeachKeyBuffer.h"

class TeachSessionRecorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantList timeline READ timeline NOTIFY changed)
public:
    explicit TeachSessionRecorder(QObject *parent = nullptr);

    QString state() const { return m_state; }
    QString error() const { return m_error; }
    QVariantList timeline() const { return m_events; }
    QString artifactId() const { return m_artifactId; }

    QString startDesktop(const QVariantMap &task, const QString &scopeKind,
                         const QString &scopeId);
    QString startBrowser(const QVariantMap &task, const QString &url);
    void setPaused(bool paused);
    void addNote(const QString &note);
    // Marca una aserción verificable en la timeline: "acá debe aparecer <expectText>".
    // Se graba como paso [assert] anclado al control bajo el cursor → el replay lo
    // reproduce con desktop_assert (objetivo comprobable, no opinable).
    void addAssertion(const QString &expectText);
    // Captura una plantilla cuadrada alrededor del cursor y la agrega a la receta.
    // Útil para controles puramente visuales; devuelve metadata o {} si falla.
    QVariantMap captureVisualReference(int size = 72);
    bool armVisualRegionSelection();
    QVariantMap captureVisualRegion(const QRect &physicalRect);
    void cancelVisualRegionSelection();
    QString finish();
    void cancel();

signals:
    void changed();
    void finished(const QString &artifactId);
    void visualRegionRequested();

private slots:
    void sampleDesktop();

private:
    void appendEvent(const QVariantMap &event, bool captureEvidence);
    QString captureEvidence();
    QVariantMap captureTemplateAt(const QPoint &physical, int size);
    QVariantMap captureTemplateRect(const QRect &physicalRect);
    void reset();
    // Vacía el texto pendiente del buffer de teclado como un paso [type].
    void flushKeys();
    // Redacta los pasos [type] y los appendea a la timeline (con evidencia).
    void emitKeySteps(const QVariantList &steps);
#ifdef Q_OS_WIN
public:
    // Invocadas por el hook global de teclado (callback file-static en el .cpp).
    // Públicas para no exponer tipos de windows.h en este header.
    void onKeyDown(quint32 vkCode, quint32 scanCode);
    void onKeyUp(quint32 vkCode);
private:
    void installKeyHook();
    void removeKeyHook();
    void *m_keyHook = nullptr;   // HHOOK opaco (sin incluir windows.h en el header)
#endif

    QString m_state = QStringLiteral("idle");
    QString m_error;
    QString m_mode;
    QString m_scopeKind;
    QString m_scopeId;
    QString m_artifactId;
    QVariantMap m_task;
    QVariantList m_events;
    QStringList m_evidence;
    int m_templateCount = 0;
    bool m_visualRegionArmed = false;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QPoint m_lastCursor;
    bool m_leftDown = false;
    bool m_rightDown = false;
    bool m_ignoreGesture = false;  // clicks sobre controles flotantes de LlamaCode
    // Traza en curso (botón apretado moviéndose): puntos NORMALIZADOS muestreados.
    // Si al soltar recorrió más que un umbral → paso [stroke]; si no → [click].
    bool m_strokeActive = false;
    QString m_strokeButton;
    QVariantList m_strokePoints;   // {x,y} normalizados
    QPoint m_strokeStartAbs;
    int m_strokeMaxDist = 0;       // desplazamiento máximo en px desde el inicio
    QVariantMap m_strokeControl;   // control UIA bajo el cursor al apretar (ancla semántica)
    TeachKeyBuffer m_keys;
    WinTapTracker m_winTap;
};
