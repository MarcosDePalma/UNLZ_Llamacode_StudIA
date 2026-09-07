#pragma once

#include <QChar>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Acumula el tipeo del usuario durante una sesión Teach y lo convierte en pasos
// de timeline. Los caracteres imprimibles se juntan en un único paso
// `[type "..."]`; una tecla "límite" (ENTER, TAB, ESC, flechas, F1..F12) o un
// atajo (Ctrl/Alt/Win + algo) primero VACÍA el texto pendiente como un paso
// `[type]` y luego emite su propio paso `[key]`, preservando el orden real.
//
// Es PURA y testeable: no toca el SO ni captura pantalla. El recorder le pasa
// los eventos crudos (desde el hook de teclado) y appendea los pasos devueltos.
class TeachKeyBuffer
{
public:
    // Agrega un carácter imprimible al texto pendiente.
    void feedChar(QChar c);
    // Procesa una tecla nombrada (no imprimible) o un atajo. Devuelve los pasos
    // a emitir: primero el `[type]` pendiente (si hay), luego el `[key]`.
    QVariantList feedKey(const QString &keyName, const QStringList &modifiers = {});
    // Vacía el texto pendiente como un único paso `[type]` (lista vacía si no hay).
    QVariantList flush();

    bool hasPending() const { return !m_text.isEmpty(); }
    QString pendingText() const { return m_text; }
    void clear() { m_text.clear(); }

    // Constructores de paso (públicos para reuso/test).
    static QVariantMap typeStep(const QString &text);
    static QVariantMap keyStep(const QString &keyName, const QStringList &modifiers = {});

private:
    QString m_text;
};

// Distingue un tap "solo" de la tecla Windows (abre/cierra el menú Inicio) de su
// uso como modificador en un atajo (Win+R). La tecla Win es ambas cosas: en un
// hook de keydown no alcanza con verla, hay que esperar el release y saber si en
// el medio se usó en combo. Pura y testeable (sin SO).
class WinTapTracker
{
public:
    // Win keydown: empieza a observar un posible tap solo.
    void down() { m_down = true; m_combo = false; }
    // Otra tecla pulsada con Win sostenida → fue modificador, no tap solo.
    void markCombo() { if (m_down) m_combo = true; }
    // Win keyup: devuelve true si correspondió a un tap solo (emitir [key WIN]).
    bool up()
    {
        const bool lone = m_down && !m_combo;
        m_down = false;
        m_combo = false;
        return lone;
    }
    void reset() { m_down = false; m_combo = false; }

private:
    bool m_down = false;
    bool m_combo = false;
};
