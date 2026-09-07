#include "TeachSessionRecorder.h"
#include "AutomationArtifactStore.h"
#include "DesktopAutomationBackend.h"
#include "core/agent/BrowserTeach.h"
#include <QCryptographicHash>
#include <QFile>

#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace {
QVariantMap pointerTrace(const QPoint &absolute, const QPointF &normalized,
                         const QString &button)
{
    return QVariantMap{
        {QStringLiteral("button"), button},
        {QStringLiteral("clickCount"), 1},
        {QStringLiteral("xAbs"), absolute.x()},
        {QStringLiteral("yAbs"), absolute.y()},
        {QStringLiteral("xNorm"), normalized.x()},
        {QStringLiteral("yNorm"), normalized.y()}};
}

QVariantMap targetTrace(const QString &surface, const QString &scopeKind,
                        const QString &scopeId, const QRect &bounds)
{
    return QVariantMap{
        {QStringLiteral("surface"), surface},
        {QStringLiteral("scopeKind"), scopeKind},
        {QStringLiteral("targetId"), scopeId},
        {QStringLiteral("x"), bounds.x()},
        {QStringLiteral("y"), bounds.y()},
        {QStringLiteral("width"), bounds.width()},
        {QStringLiteral("height"), bounds.height()}};
}
}  // namespace

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>

namespace {
// VK no imprimible → nombre canónico (mismo vocabulario que
// DesktopAutomationBackend::keyCode, así el replay vía desktop_key lo reconoce).
// Devuelve vacío si la tecla es imprimible (se captura como texto, no como [key]).
QString vkToName(quint32 vk)
{
    switch (vk) {
    case VK_RETURN:  return QStringLiteral("ENTER");
    case VK_TAB:     return QStringLiteral("TAB");
    case VK_ESCAPE:  return QStringLiteral("ESC");
    case VK_BACK:    return QStringLiteral("BACKSPACE");
    case VK_DELETE:  return QStringLiteral("DELETE");
    case VK_UP:      return QStringLiteral("UP");
    case VK_DOWN:    return QStringLiteral("DOWN");
    case VK_LEFT:    return QStringLiteral("LEFT");
    case VK_RIGHT:   return QStringLiteral("RIGHT");
    case VK_HOME:    return QStringLiteral("HOME");
    case VK_END:     return QStringLiteral("END");
    case VK_PRIOR:   return QStringLiteral("PAGEUP");
    case VK_NEXT:    return QStringLiteral("PAGEDOWN");
    default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F12)
        return QStringLiteral("F%1").arg(vk - VK_F1 + 1);
    return {};
}

// Nombre de la tecla base de un atajo (Ctrl/Alt/Win + X). Letras/dígitos → el
// carácter; el resto delega en vkToName (vacío = atajo no representable, se ignora).
QString shortcutKeyName(quint32 vk)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
        return QString(QChar(static_cast<ushort>(vk)));
    return vkToName(vk);
}

// Instancia que recibe el hook global (sólo hay una sesión Teach a la vez) y el
// callback C del hook de bajo nivel. File-static para mantener windows.h fuera
// del header (TeachSessionRecorder.h lo incluye AppController.h → todo el árbol).
TeachSessionRecorder *g_activeRecorder = nullptr;

LRESULT CALLBACK teachKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_activeRecorder) {
        const auto *kb = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
            g_activeRecorder->onKeyDown(kb->vkCode, kb->scanCode);
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
            g_activeRecorder->onKeyUp(kb->vkCode);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
}  // namespace
#endif

TeachSessionRecorder::TeachSessionRecorder(QObject *parent) : QObject(parent)
{
    // 80 ms perdía clicks rápidos (p.ej. elegir un color en Paint). 16 ms sigue
    // siendo barato y cubre prácticamente un frame de una pantalla a 60 Hz.
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &TeachSessionRecorder::sampleDesktop);
}

void TeachSessionRecorder::reset()
{
    m_timer.stop();
#ifdef Q_OS_WIN
    removeKeyHook();
#endif
    m_keys.clear();
    m_winTap.reset();
    m_strokeActive = false;
    m_strokePoints.clear();
    m_strokeMaxDist = 0;
    m_strokeControl.clear();
    m_ignoreGesture = false;
    m_leftDown = m_rightDown = false;
    m_error.clear();
    m_events.clear();
    m_evidence.clear();
    m_templateCount = 0;
    m_visualRegionArmed = false;
    m_artifactId.clear();
    m_task.clear();
    m_mode.clear();
    m_scopeKind.clear();
    m_scopeId.clear();
}

QString TeachSessionRecorder::startDesktop(const QVariantMap &task,
                                           const QString &scopeKind,
                                           const QString &scopeId)
{
    if (m_state == QLatin1String("recording") || m_state == QLatin1String("paused"))
        return QStringLiteral("Ya hay una sesión Teach activa.");
    reset();
    if (!DesktopAutomationBackend::interactiveSessionAvailable())
        return QStringLiteral("La sesión de Windows no está disponible para grabar.");
    if (DesktopAutomationBackend::targetInfo(scopeKind, scopeId).isEmpty())
        return QStringLiteral("Seleccioná una pantalla o ventana válida.");
    m_task = task;
    m_mode = QStringLiteral("desktop");
    m_scopeKind = scopeKind;
    m_scopeId = scopeId;
    m_clock.start();
    m_lastCursor = DesktopAutomationBackend::cursorPosPhysical();
    m_state = QStringLiteral("recording");
    appendEvent({{QStringLiteral("kind"), QStringLiteral("start")},
                 {QStringLiteral("intent"), QStringLiteral("Inicio de demostración de escritorio")}}, true);
    m_timer.start();
#ifdef Q_OS_WIN
    installKeyHook();   // capturar tipeo del usuario (texto + teclas/atajos)
#endif
    emit changed();
    return {};
}

QString TeachSessionRecorder::startBrowser(const QVariantMap &task, const QString &url)
{
    if (m_state == QLatin1String("recording") || m_state == QLatin1String("paused"))
        return QStringLiteral("Ya hay una sesión Teach activa.");
    reset();
    const QString skill = task.value(QStringLiteral("id")).toString();
    if (BrowserTeach::recordCommand(skill, url).isEmpty())
        return QStringLiteral("La Task necesita un nombre válido.");
    m_task = task;
    m_mode = QStringLiteral("browserBackground");
    m_scopeKind = QStringLiteral("screen");
    m_scopeId = QStringLiteral("0");
    m_clock.start();
    m_lastCursor = DesktopAutomationBackend::cursorPosPhysical();
    m_state = QStringLiteral("recording");
    appendEvent({{QStringLiteral("kind"), QStringLiteral("browser")},
                 {QStringLiteral("intent"), QStringLiteral("Inicio de demostración web en browser foreground con Playwright codegen y evidencia visual")},
                 {QStringLiteral("ref"), url},
                 {QStringLiteral("target"), QVariantMap{
                     {QStringLiteral("surface"), QStringLiteral("browser")},
                     {QStringLiteral("mode"), QStringLiteral("foregroundTeach")},
                     {QStringLiteral("driver"), QStringLiteral("playwright-codegen")},
                     {QStringLiteral("url"), url}}},
                 {QStringLiteral("surface"), QStringLiteral("browserForegroundTeach")}}, true);
    m_timer.start();
#ifdef Q_OS_WIN
    installKeyHook();   // capturar tipeo del usuario mientras usa el browser de codegen
#endif
    emit changed();
    return {};
}

void TeachSessionRecorder::setPaused(bool paused)
{
    if (paused && m_state == QLatin1String("recording")) {
        flushKeys();   // cerrar el texto pendiente antes de pausar
        m_state = QStringLiteral("paused");
        m_timer.stop();
    } else if (!paused && m_state == QLatin1String("paused")) {
        m_state = QStringLiteral("recording");
        if (m_mode == QLatin1String("desktop")
            || m_mode == QLatin1String("browserBackground"))
            m_timer.start();
    }
    emit changed();
}

void TeachSessionRecorder::addNote(const QString &note)
{
    if (m_state != QLatin1String("recording") && m_state != QLatin1String("paused")) return;
    const QString clean = AutomationArtifactStore::redact(note.trimmed());
    if (clean.isEmpty()) return;
    appendEvent({{QStringLiteral("kind"), QStringLiteral("note")},
                 {QStringLiteral("intent"), clean}}, true);
}

void TeachSessionRecorder::addAssertion(const QString &expectText)
{
    if (m_state != QLatin1String("recording") && m_state != QLatin1String("paused")) return;
    const QString clean = AutomationArtifactStore::redact(expectText.trimmed());
    if (clean.isEmpty()) return;
    QVariantMap ev{
        {QStringLiteral("kind"), QStringLiteral("assert")},
        {QStringLiteral("intent"), QStringLiteral("Verificar que aparezca: \"%1\"").arg(clean)},
        {QStringLiteral("expectText"), clean}};
#ifdef Q_OS_WIN
    const QVariantMap control = DesktopAutomationBackend::controlAtPoint(
        DesktopAutomationBackend::cursorPosPhysical());
    if (!control.value(QStringLiteral("windowId")).toString().isEmpty())
        ev[QStringLiteral("target")] = control;
#endif
    appendEvent(ev, true);
}

QString TeachSessionRecorder::captureEvidence()
{
    if (m_mode != QLatin1String("desktop")
        && m_mode != QLatin1String("browserBackground")) return {};
    const QString tempId = m_task.value(QStringLiteral("id")).toString();
    const QString dir = AutomationArtifactStore::artifactDir(tempId)
                        + QStringLiteral("/evidence");
    QDir().mkpath(dir);
    const QString name = QStringLiteral("%1.jpg").arg(m_evidence.size() + 1, 4, 10, QLatin1Char('0'));
    QString error;
    const QString path = DesktopAutomationBackend::saveCapture(
        m_scopeKind, m_scopeId, dir + QLatin1Char('/') + name, &error);
    if (path.isEmpty()) m_error = error;
    else m_evidence << name;
    return name;
}

QVariantMap TeachSessionRecorder::captureTemplateAt(const QPoint &physical, int requestedSize)
{
    const int size = qBound(24, requestedSize, 256);
    return captureTemplateRect(QRect(physical.x() - size / 2, physical.y() - size / 2,
                                     size, size));
}

QVariantMap TeachSessionRecorder::captureTemplateRect(const QRect &physicalRect)
{
    if (m_mode != QLatin1String("desktop")) return {};
    const QVariantMap info = DesktopAutomationBackend::targetInfo(m_scopeKind, m_scopeId);
    const QRect bounds(info.value(QStringLiteral("x")).toInt(), info.value(QStringLiteral("y")).toInt(),
                       info.value(QStringLiteral("width")).toInt(), info.value(QStringLiteral("height")).toInt());
    const QRect requested = physicalRect.normalized().intersected(bounds);
    if (!bounds.isValid() || requested.width() < 12 || requested.height() < 12) {
        m_error = QStringLiteral("La región visual debe quedar dentro del alcance y medir al menos 12×12.");
        return {};
    }
    QString error;
    const QImage capture = DesktopAutomationBackend::capture(m_scopeKind, m_scopeId, &error);
    if (capture.isNull()) { m_error = error; return {}; }
    const double sx = double(capture.width()) / bounds.width();
    const double sy = double(capture.height()) / bounds.height();
    QRect crop(qRound((requested.x() - bounds.x()) * sx),
               qRound((requested.y() - bounds.y()) * sy),
               qRound(requested.width() * sx), qRound(requested.height() * sy));
    crop = crop.intersected(capture.rect());
    if (crop.width() < 16 || crop.height() < 16) {
        m_error = QStringLiteral("La región visual quedó fuera del alcance.");
        return {};
    }
    const QString dir = AutomationArtifactStore::artifactDir(
                            m_task.value(QStringLiteral("id")).toString())
                        + QStringLiteral("/templates");
    QDir().mkpath(dir);
    const QString file = QStringLiteral("template-%1.png")
                             .arg(++m_templateCount, 4, 10, QLatin1Char('0'));
    const QImage needle = capture.copy(crop);
    const QString path = dir + QLatin1Char('/') + file;
    if (!needle.save(path, "PNG")) {
        m_error = QStringLiteral("No se pudo guardar la plantilla visual.");
        return {};
    }
    QFile hashFile(path);
    QByteArray hash;
    if (hashFile.open(QIODevice::ReadOnly))
        hash = QCryptographicHash::hash(hashFile.readAll(), QCryptographicHash::Sha256).toHex();
    return QVariantMap{
        {QStringLiteral("type"), QStringLiteral("image")},
        {QStringLiteral("file"), QStringLiteral("templates/") + file},
        {QStringLiteral("threshold"), 0.88},
        {QStringLiteral("minScale"), 0.8},
        {QStringLiteral("maxScale"), 1.25},
        {QStringLiteral("requireUnique"), true},
        {QStringLiteral("width"), needle.width()},
        {QStringLiteral("height"), needle.height()},
        {QStringLiteral("sha256"), QString::fromLatin1(hash)}};
}

QVariantMap TeachSessionRecorder::captureVisualReference(int size)
{
    if (m_state != QLatin1String("recording") && m_state != QLatin1String("paused")) return {};
    const QVariantMap locator = captureTemplateAt(
        DesktopAutomationBackend::cursorPosPhysical(), size);
    if (locator.isEmpty()) { emit changed(); return {}; }
    appendEvent({{QStringLiteral("kind"), QStringLiteral("visual_reference")},
                 {QStringLiteral("intent"), QStringLiteral("Referencia visual capturada")},
                 {QStringLiteral("locator"), locator}}, false);
    return locator;
}

bool TeachSessionRecorder::armVisualRegionSelection()
{
    if (m_state != QLatin1String("recording")) return false;
    m_visualRegionArmed = true;
    appendEvent({{QStringLiteral("kind"), QStringLiteral("visual_region_armed")},
                 {QStringLiteral("intent"), QStringLiteral("Arrastrá un rectángulo sobre el objetivo visual")}}, false);
    return true;
}

QVariantMap TeachSessionRecorder::captureVisualRegion(const QRect &physicalRect)
{
    if (!m_visualRegionArmed || m_state != QLatin1String("recording")) return {};
    m_visualRegionArmed = false;
    const QVariantMap locator = captureTemplateRect(physicalRect.normalized());
    if (!locator.isEmpty())
        appendEvent({{QStringLiteral("kind"), QStringLiteral("visual_reference")},
                     {QStringLiteral("intent"), QStringLiteral("Región visual seleccionada")},
                     {QStringLiteral("locator"), locator}}, false);
    else
        emit changed();
    return locator;
}

void TeachSessionRecorder::cancelVisualRegionSelection()
{
    if (!m_visualRegionArmed) return;
    m_visualRegionArmed = false;
    appendEvent({{QStringLiteral("kind"), QStringLiteral("visual_region_cancelled")},
                 {QStringLiteral("intent"), QStringLiteral("Selección visual cancelada")}}, false);
}

void TeachSessionRecorder::appendEvent(const QVariantMap &source, bool capture)
{
    QVariantMap event = source;
    event[QStringLiteral("atMs")] = m_clock.isValid() ? m_clock.elapsed() : 0;
    if (capture) {
        const QString evidence = captureEvidence();
        if (!evidence.isEmpty()) event[QStringLiteral("evidence")] = evidence;
    }
    m_events.append(event);
    emit changed();
}

void TeachSessionRecorder::sampleDesktop()
{
    if (m_state != QLatin1String("recording")) return;
    const QPoint cursor = DesktopAutomationBackend::cursorPosPhysical();
#ifdef Q_OS_WIN
    const bool left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const QVariantMap info = DesktopAutomationBackend::targetInfo(m_scopeKind, m_scopeId);
    const QRect bounds(info.value(QStringLiteral("x")).toInt(), info.value(QStringLiteral("y")).toInt(),
                       info.value(QStringLiteral("width")).toInt(), info.value(QStringLiteral("height")).toInt());
    const bool pressed = left || right;
    const bool wasPressed = m_leftDown || m_rightDown;

    // El botón flotante de stop (y cualquier ventana propia) es control del
    // recorder, no parte de la demostración. Ignorar el gesto completo evita que
    // aparezca como último click de la receta.
    if (pressed && !wasPressed) {
        HWND hit = GetAncestor(WindowFromPoint(POINT{cursor.x(), cursor.y()}), GA_ROOT);
        DWORD pid = 0;
        if (hit) GetWindowThreadProcessId(hit, &pid);
        m_ignoreGesture = pid == GetCurrentProcessId();
    }
    if (m_ignoreGesture) {
        if (!pressed) m_ignoreGesture = false;
        m_leftDown = left;
        m_rightDown = right;
        m_lastCursor = cursor;
        return;
    }

    // ── Presión inicial: arrancar el seguimiento de traza (aún no sabemos si es
    //    un click puntual o un arrastre; se decide al soltar por distancia). ──
    if (pressed && !wasPressed && bounds.contains(cursor)) {
        flushKeys();   // el texto tipeado va ANTES del próximo gesto
        m_strokeActive = true;
        m_strokeButton = left ? QStringLiteral("left") : QStringLiteral("right");
        m_strokeStartAbs = cursor;
        m_strokeMaxDist = 0;
        // Anclar el gesto al control UIA bajo el cursor (name/role/automationId +
        // ventana): en el replay se re-localiza el control aunque la UI se movió.
        m_strokeControl = DesktopAutomationBackend::controlAtPoint(cursor);
        const QPointF p = DesktopAutomationBackend::normalizePoint(cursor, bounds);
        m_strokePoints = QVariantList{QVariantMap{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}}};
    }
    // ── Botón sostenido moviéndose: muestrear la trayectoria. ──
    else if (pressed && wasPressed && m_strokeActive) {
        const QPoint clamped(qBound(bounds.left(), cursor.x(), bounds.right()),
                             qBound(bounds.top(), cursor.y(), bounds.bottom()));
        const int d = qMax(qAbs(clamped.x() - m_strokeStartAbs.x()),
                           qAbs(clamped.y() - m_strokeStartAbs.y()));
        m_strokeMaxDist = qMax(m_strokeMaxDist, d);
        const QPointF p = DesktopAutomationBackend::normalizePoint(clamped, bounds);
        m_strokePoints << QVariantMap{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}};
    }
    // ── Soltar: cerrar el gesto como [stroke] (si arrastró) o [click] (puntual). ──
    else if (!pressed && wasPressed && m_strokeActive) {
        m_strokeActive = false;
        if (m_visualRegionArmed) {
            m_visualRegionArmed = false;
            const QVariantMap locator = captureTemplateRect(
                QRect(m_strokeStartAbs, cursor).normalized());
            if (!locator.isEmpty())
                appendEvent({{QStringLiteral("kind"), QStringLiteral("visual_reference")},
                             {QStringLiteral("intent"), QStringLiteral("Región visual seleccionada")},
                             {QStringLiteral("locator"), locator}}, false);
            m_strokePoints.clear();
            m_leftDown = left;
            m_rightDown = right;
            m_lastCursor = cursor;
            return;
        }
        const QString btn = m_strokeButton;
        const QPointF start = m_strokePoints.isEmpty() ? QPointF()
            : QPointF(m_strokePoints.first().toMap().value(QStringLiteral("x")).toDouble(),
                      m_strokePoints.first().toMap().value(QStringLiteral("y")).toDouble());
        // target = geometría del alcance + ancla semántica del control (si UIA lo dio).
        QVariantMap target = targetTrace(m_mode, m_scopeKind, m_scopeId, bounds);
        const QString ctlName = m_strokeControl.value(QStringLiteral("name")).toString();
        if (!ctlName.isEmpty() || !m_strokeControl.value(QStringLiteral("controlId")).toString().isEmpty()) {
            target[QStringLiteral("name")] = ctlName;
            target[QStringLiteral("role")] = m_strokeControl.value(QStringLiteral("role"));
            target[QStringLiteral("controlId")] = m_strokeControl.value(QStringLiteral("controlId"));
            target[QStringLiteral("automationId")] = m_strokeControl.value(QStringLiteral("automationId"));
        }
        // La ventana existe aunque el punto no exponga un control UIA (canvas,
        // renderers y apps legacy). Guardar siempre su geometría y estado.
        if (!m_strokeControl.value(QStringLiteral("windowLabel")).toString().isEmpty()) {
            target[QStringLiteral("windowId")] = m_strokeControl.value(QStringLiteral("windowId"));
            target[QStringLiteral("windowLabel")] = m_strokeControl.value(QStringLiteral("windowLabel"));
            target[QStringLiteral("winX")] = m_strokeControl.value(QStringLiteral("winX"));
            target[QStringLiteral("winY")] = m_strokeControl.value(QStringLiteral("winY"));
            target[QStringLiteral("winWidth")] = m_strokeControl.value(QStringLiteral("winWidth"));
            target[QStringLiteral("winHeight")] = m_strokeControl.value(QStringLiteral("winHeight"));
            target[QStringLiteral("windowMaximized")] =
                m_strokeControl.value(QStringLiteral("windowMaximized"));
        }
        if (m_strokeMaxDist >= 8 && m_strokePoints.size() >= 2) {
            appendEvent({{QStringLiteral("kind"), QStringLiteral("stroke")},
                         {QStringLiteral("intent"), QStringLiteral("Arrastrar con botón %1 (traza de %2 puntos)")
                              .arg(btn).arg(m_strokePoints.size())},
                         {QStringLiteral("surface"), m_mode},
                         {QStringLiteral("button"), btn},
                         {QStringLiteral("x"), start.x()}, {QStringLiteral("y"), start.y()},
                         {QStringLiteral("points"), m_strokePoints},
                         {QStringLiteral("target"), target}}, true);
        } else {
            QVariantList locators;
            const QString controlId = target.value(QStringLiteral("controlId")).toString();
            const QString controlName = target.value(QStringLiteral("name")).toString();
            if (!controlId.isEmpty() || !controlName.isEmpty()) {
                locators << QVariantMap{{QStringLiteral("type"), QStringLiteral("uia")},
                                        {QStringLiteral("controlId"), controlId},
                                        {QStringLiteral("name"), controlName},
                                        {QStringLiteral("role"), target.value(QStringLiteral("role"))}};
                if (!controlName.isEmpty())
                    locators << QVariantMap{{QStringLiteral("type"), QStringLiteral("ocr")},
                                            {QStringLiteral("text"), controlName}};
            } else {
                const QVariantMap imageLocator = captureTemplateAt(m_strokeStartAbs, 72);
                if (!imageLocator.isEmpty()) locators << imageLocator;
            }
            locators << QVariantMap{{QStringLiteral("type"), QStringLiteral("normalizedPoint")},
                                    {QStringLiteral("x"), start.x()},
                                    {QStringLiteral("y"), start.y()}};
            appendEvent({{QStringLiteral("kind"), QStringLiteral("click")},
                         {QStringLiteral("intent"), btn == QLatin1String("right")
                              ? QStringLiteral("Click derecho") : QStringLiteral("Click izquierdo")},
                         {QStringLiteral("surface"), m_mode},
                         {QStringLiteral("button"), btn},
                         {QStringLiteral("x"), start.x()}, {QStringLiteral("y"), start.y()},
                         {QStringLiteral("pointer"), pointerTrace(m_strokeStartAbs, start, btn)},
                         {QStringLiteral("locators"), locators},
                         {QStringLiteral("target"), target}}, true);
        }
        m_strokePoints.clear();
    }
    m_leftDown = left;
    m_rightDown = right;
#endif
    m_lastCursor = cursor;
}

QString TeachSessionRecorder::finish()
{
    if (m_state != QLatin1String("recording") && m_state != QLatin1String("paused"))
        return {};
    m_state = QStringLiteral("compiling");
    m_timer.stop();
#ifdef Q_OS_WIN
    removeKeyHook();
#endif
    flushKeys();   // volcar el último texto tipeado antes del paso de verificación
    appendEvent({{QStringLiteral("kind"), QStringLiteral("verification")},
                 {QStringLiteral("intent"), QStringLiteral("Verificar que se cumplió el objetivo")}}, true);
    const QVariantMap scope{
        {QStringLiteral("kind"), m_scopeKind},
        {QStringLiteral("targetId"), m_scopeId},
        {QStringLiteral("target"), DesktopAutomationBackend::targetInfo(m_scopeKind, m_scopeId)},
        {QStringLiteral("logicalSurface"), m_mode}};
    QString browserScript;
    if (m_mode == QLatin1String("browserBackground")
        && BrowserTeach::hasSkill(m_task.value(QStringLiteral("id")).toString()))
        browserScript = BrowserTeach::skillPath(m_task.value(QStringLiteral("id")).toString());
    m_artifactId = AutomationArtifactStore::create(m_task, scope, m_events, m_evidence, browserScript);
    m_state = m_artifactId.isEmpty() ? QStringLiteral("failed") : QStringLiteral("ready");
    if (m_artifactId.isEmpty()) m_error = QStringLiteral("No se pudo guardar el artefacto Teach.");
    emit changed();
    if (!m_artifactId.isEmpty()) emit finished(m_artifactId);
    return m_artifactId;
}

void TeachSessionRecorder::cancel()
{
    if (m_state == QLatin1String("idle")) return;
    m_timer.stop();
#ifdef Q_OS_WIN
    removeKeyHook();
#endif
    m_keys.clear();
    m_winTap.reset();
    m_state = QStringLiteral("cancelled");
    emit changed();
}

void TeachSessionRecorder::flushKeys()
{
    emitKeySteps(m_keys.flush());
}

void TeachSessionRecorder::emitKeySteps(const QVariantList &steps)
{
    for (const QVariant &v : steps) {
        QVariantMap step = v.toMap();
        // Redactar el texto tipeado (puede contener credenciales) y reconstruir el
        // intent legible a partir del texto ya saneado.
        if (step.value(QStringLiteral("kind")).toString() == QLatin1String("type")) {
            const QString clean = AutomationArtifactStore::redact(
                step.value(QStringLiteral("text")).toString());
            step[QStringLiteral("text")] = clean;
            step[QStringLiteral("intent")] = QStringLiteral("Escribir: \"%1\"").arg(clean);
        }
        appendEvent(step, true);
    }
}

#ifdef Q_OS_WIN
void TeachSessionRecorder::installKeyHook()
{
    if (m_keyHook) return;
    g_activeRecorder = this;
    m_keyHook = SetWindowsHookExW(WH_KEYBOARD_LL, &teachKeyboardProc,
                                  GetModuleHandleW(nullptr), 0);
    if (!m_keyHook) g_activeRecorder = nullptr;   // sin hook: tipeo no capturado (best-effort)
}

void TeachSessionRecorder::removeKeyHook()
{
    if (m_keyHook) {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_keyHook));
        m_keyHook = nullptr;
    }
    if (g_activeRecorder == this) g_activeRecorder = nullptr;
}

void TeachSessionRecorder::onKeyDown(quint32 vk, quint32 scan)
{
    if (m_state != QLatin1String("recording")) return;

    // Atajo global de Teach: el cursor permanece sobre el objetivo, por lo que la
    // región capturada no termina accidentalmente sobre la ventana de LlamaCode.
    if (vk == VK_F8) {
        captureVisualReference(72);
        return;
    }
    if (vk == VK_F9) {
        emit visualRegionRequested();
        return;
    }

    // La tecla Win es modificador (Win+R) Y tecla independiente (tap → menú
    // Inicio). En keydown sólo arrancamos el seguimiento; el paso [key WIN] de un
    // tap solo se decide en el keyup (ver onKeyUp), para no duplicar con Win+X.
    if (vk == VK_LWIN || vk == VK_RWIN) {
        m_winTap.down();
        return;
    }

    // Modificadores y toggles solos: no generan un paso propio.
    switch (vk) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
    case VK_MENU: case VK_LMENU: case VK_RMENU:
    case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
        return;
    default: break;
    }

    // En un hook global LL, GetKeyState lee la cola del hilo instalador (no la del
    // app con foco) → poco fiable. GetAsyncKeyState lee el estado físico real.
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt  = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    const bool win  = (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
    const QString named = vkToName(vk);

    // Atajo (Ctrl/Alt/Win + algo): vaciar texto y emitir [key] con modificadores.
    if (ctrl || alt || win) {
        if (win) m_winTap.markCombo();   // Win se usó en combo → su keyup no es tap solo
        QStringList mods;
        if (ctrl) mods << QStringLiteral("CTRL");
        if (alt)  mods << QStringLiteral("ALT");
        if (win)  mods << QStringLiteral("WIN");
        const QString key = named.isEmpty() ? shortcutKeyName(vk) : named;
        if (!key.isEmpty()) emitKeySteps(m_keys.feedKey(key, mods));
        return;
    }

    // Tecla nombrada (ENTER, TAB, flechas…): vaciar texto y emitir [key].
    if (!named.isEmpty()) {
        emitKeySteps(m_keys.feedKey(named));
        return;
    }

    // Imprimible: traducir vk+scan a unicode. Llegamos acá sin ctrl/alt/win (los
    // atajos ya retornaron arriba), así que sólo shift+caps afectan el carácter.
    // Construimos el estado a mano con GetAsyncKeyState (el array del hilo del hook
    // no refleja el teclado del app con foco).
    BYTE state[256] = {};
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) state[VK_SHIFT] = 0x80;
    if (GetKeyState(VK_CAPITAL) & 0x0001)    state[VK_CAPITAL] = 0x01;   // toggle caps
    wchar_t buf[8] = {};
    const HKL layout = GetKeyboardLayout(0);
    const int n = ToUnicodeEx(vk, scan, state, buf, 7, 0, layout);
    for (int i = 0; i < n; ++i) {
        const QChar c(static_cast<ushort>(buf[i]));
        if (c.isPrint()) m_keys.feedChar(c);
    }
}

void TeachSessionRecorder::onKeyUp(quint32 vk)
{
    if (m_state != QLatin1String("recording")) return;
    // Sólo la tecla Win necesita el keyup: un tap solo (sin combo) → [key WIN].
    if ((vk == VK_LWIN || vk == VK_RWIN) && m_winTap.up())
        emitKeySteps(m_keys.feedKey(QStringLiteral("WIN")));
}
#endif
