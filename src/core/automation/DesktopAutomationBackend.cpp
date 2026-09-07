#include "DesktopAutomationBackend.h"
#include "DesktopComputerUse.h"
#include "VisualMatcher.h"

#include "FuzzyMatch.h"
#include "OcrEngine.h"
#include "OcrTextLocator.h"

#include <QCursor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QScreen>
#include <QSet>
#include <QThread>
#include <QVector>
#include <QWindow>

#include <cmath>

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#  include <uiautomation.h>   // árbol de controles del escritorio (UI Automation)
#endif

namespace {

#ifdef Q_OS_WIN
// Rect FISICO del monitor que arranca en `origin`, o {} si no hay.
//
// Por qué existe: TODA la automatización de escritorio habla en píxeles FISICOS
// —SetCursorPos, SendInput y UIAutomation::ElementFromPoint lo son—, y
// GetWindowRect (que usa el scope "window") también. Pero QScreen::geometry()
// devuelve píxeles LOGICOS de Qt (físicos ÷ devicePixelRatio). En un monitor al
// 150% los dos espacios NO coinciden, así que mezclarlos manda el clic a otro
// lado. Medido con tests/qa_ocr_probe sobre 3 monitores (100/125/150%): con
// coords lógicas el acuerdo OCR↔UIA caía a 10% en el de 150%; con físicas, 75%.
//
// Se empareja por ORIGEN: el orden de EnumDisplayMonitors no coincide con el de
// QGuiApplication::screens(), y QScreen::name() da el nombre comercial ("27Q17"),
// no el device ("\\.\DISPLAY6"). El origen sí coincide entre ambos mundos; lo que
// difiere es el tamaño.
QRect physicalRectForOrigin(const QPoint &origin)
{
    struct Ctx { QPoint want; QRect found; } ctx{origin, QRect()};
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto *c = reinterpret_cast<Ctx *>(lp);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(hm, &mi) && mi.rcMonitor.left == c->want.x()
                && mi.rcMonitor.top == c->want.y()) {
                c->found = QRect(mi.rcMonitor.left, mi.rcMonitor.top,
                                 mi.rcMonitor.right - mi.rcMonitor.left,
                                 mi.rcMonitor.bottom - mi.rcMonitor.top);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

#endif

// Geometría de un QScreen en píxeles FISICOS. Fallback a la lógica si Windows no
// reporta el monitor (o fuera de Windows): con dpr=1 son idénticas.
QRect screenPhysicalGeometryOf(QScreen *screen)
{
    if (!screen) return {};
    const QRect logical = screen->geometry();
#ifdef Q_OS_WIN
    const QRect phys = physicalRectForOrigin(logical.topLeft());
    return phys.isValid() ? phys : logical;
#else
    return logical;
#endif
}

// La pantalla que contiene un punto FISICO. No sirve QGuiApplication::screenAt():
// ese espera un punto lógico y en monitores escalados devolvería la equivocada.
QScreen *screenForPhysicalPoint(const QPoint &p)
{
    for (QScreen *s : QGuiApplication::screens())
        if (screenPhysicalGeometryOf(s).contains(p)) return s;
    return QGuiApplication::primaryScreen();
}

// Alcance de un target en píxeles FISICOS, el único espacio que entienden las
// APIs de input de Windows. El scope "window" ya lo estaba (GetWindowRect); el
// scope "screen" devolvía lógicos y por eso clickeaba corrido en monitores
// escalados.
QRect targetBounds(const QString &kind, const QString &targetId)
{
    if (kind == QLatin1String("screen")) {
        const auto list = QGuiApplication::screens();
        bool ok = false;
        const int index = targetId.toInt(&ok);
        QScreen *found = nullptr;
        if (ok && index >= 0 && index < list.size()) found = list.at(index);
        if (!found)
            for (QScreen *screen : list)
                if (screen->name() == targetId) { found = screen; break; }
        if (!found) found = QGuiApplication::primaryScreen();
        return screenPhysicalGeometryOf(found);
    }
#ifdef Q_OS_WIN
    bool ok = false;
    const quintptr raw = targetId.toULongLong(&ok, 16);
    HWND hwnd = ok ? reinterpret_cast<HWND>(raw) : nullptr;
    RECT r{};
    if (hwnd && IsWindow(hwnd) && GetWindowRect(hwnd, &r))
        return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
#else
    Q_UNUSED(targetId)
#endif
    return {};
}

#ifdef Q_OS_WIN
struct WindowCollector { QVariantList rows; };
BOOL CALLBACK collectWindow(HWND hwnd, LPARAM param)
{
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    wchar_t title[512]{};
    GetWindowTextW(hwnd, title, 511);
    const QString text = QString::fromWCharArray(title).trimmed();
    wchar_t className[256]{};
    GetClassNameW(hwnd, className, 255);
    const QString classText = QString::fromWCharArray(className).trimmed();
    RECT r{};
    if (text.isEmpty() || !GetWindowRect(hwnd, &r) || r.right <= r.left || r.bottom <= r.top)
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    auto *collector = reinterpret_cast<WindowCollector *>(param);
    collector->rows.append(QVariantMap{
        {QStringLiteral("id"), QString::number(reinterpret_cast<quintptr>(hwnd), 16)},
        {QStringLiteral("kind"), QStringLiteral("window")},
        {QStringLiteral("label"), text},
        {QStringLiteral("className"), classText},
        {QStringLiteral("pid"), static_cast<qulonglong>(pid)},
        {QStringLiteral("x"), static_cast<int>(r.left)},
        {QStringLiteral("y"), static_cast<int>(r.top)},
        {QStringLiteral("width"), static_cast<int>(r.right - r.left)},
        {QStringLiteral("height"), static_cast<int>(r.bottom - r.top)},
        {QStringLiteral("maximized"), IsZoomed(hwnd) != FALSE},
        {QStringLiteral("minimized"), IsIconic(hwnd) != FALSE}});
    return TRUE;
}

WORD keyCode(const QString &key)
{
    const QString k = key.trimmed().toUpper();
    if (k.size() == 1) return static_cast<WORD>(VkKeyScanW(k.at(0).unicode()) & 0xff);
    static const QHash<QString, WORD> keys{
        {"ENTER", VK_RETURN}, {"TAB", VK_TAB}, {"ESC", VK_ESCAPE},
        {"ESCAPE", VK_ESCAPE}, {"BACKSPACE", VK_BACK}, {"DELETE", VK_DELETE},
        {"SPACE", VK_SPACE}, {"UP", VK_UP}, {"DOWN", VK_DOWN},
        {"LEFT", VK_LEFT}, {"RIGHT", VK_RIGHT}, {"HOME", VK_HOME},
        {"END", VK_END}, {"PAGEDOWN", VK_NEXT}, {"PAGEUP", VK_PRIOR},
        {"WIN", VK_LWIN}, {"LWIN", VK_LWIN}, {"RWIN", VK_RWIN}, {"META", VK_LWIN},
        {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4},
        {"F5", VK_F5}, {"F6", VK_F6}, {"F7", VK_F7}, {"F8", VK_F8},
        {"F9", VK_F9}, {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12}};
    return keys.value(k, 0);
}

void sendKey(WORD vk, bool down)
{
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

// ── UI Automation helpers ────────────────────────────────────────────────────

// CoInitialize per-hilo (mejor-esfuerzo). UIA corre en el worker thread; si la
// sesión ya estaba en STA, CoInitializeEx devuelve RPC_E_CHANGED_MODE y NO
// uninicializamos (COM sigue usable). Sólo balanceamos lo que abrimos.
struct ComGuard
{
    bool owned = false;
    ComGuard() { owned = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
    ~ComGuard() { if (owned) CoUninitialize(); }
};

QString uiaControlTypeName(CONTROLTYPEID t)
{
    switch (t) {
    case UIA_ButtonControlTypeId:      return QStringLiteral("button");
    case UIA_EditControlTypeId:        return QStringLiteral("edit");
    case UIA_TextControlTypeId:        return QStringLiteral("text");
    case UIA_CheckBoxControlTypeId:    return QStringLiteral("checkbox");
    case UIA_RadioButtonControlTypeId: return QStringLiteral("radio");
    case UIA_ComboBoxControlTypeId:    return QStringLiteral("combobox");
    case UIA_ListControlTypeId:        return QStringLiteral("list");
    case UIA_ListItemControlTypeId:    return QStringLiteral("listitem");
    case UIA_MenuItemControlTypeId:    return QStringLiteral("menuitem");
    case UIA_TabItemControlTypeId:     return QStringLiteral("tabitem");
    case UIA_HyperlinkControlTypeId:   return QStringLiteral("link");
    case UIA_ImageControlTypeId:       return QStringLiteral("image");
    case UIA_TreeItemControlTypeId:    return QStringLiteral("treeitem");
    case UIA_TabControlTypeId:         return QStringLiteral("tabs");
    case UIA_ToolBarControlTypeId:     return QStringLiteral("toolbar");
    case UIA_GroupControlTypeId:       return QStringLiteral("group");
    case UIA_PaneControlTypeId:        return QStringLiteral("pane");
    case UIA_WindowControlTypeId:      return QStringLiteral("window");
    default: return QStringLiteral("control(%1)").arg(static_cast<int>(t));
    }
}

// RuntimeId (SAFEARRAY de LONG) → "a.b.c". Estable mientras viva el elemento.
QString uiaRuntimeId(IUIAutomationElement *el)
{
    SAFEARRAY *psa = nullptr;
    if (FAILED(el->GetRuntimeId(&psa)) || !psa) return QString();
    LONG lb = 0, ub = -1;
    SafeArrayGetLBound(psa, 1, &lb);
    SafeArrayGetUBound(psa, 1, &ub);
    QStringList parts;
    for (LONG i = lb; i <= ub; ++i) {
        LONG v = 0;
        if (SUCCEEDED(SafeArrayGetElement(psa, &i, &v)))
            parts << QString::number(static_cast<int>(v));
    }
    SafeArrayDestroy(psa);
    return parts.join(QLatin1Char('.'));
}

QString uiaName(IUIAutomationElement *el)
{
    BSTR b = nullptr;
    if (FAILED(el->get_CurrentName(&b)) || !b) return QString();
    const QString s = QString::fromWCharArray(b, static_cast<int>(SysStringLen(b)));
    SysFreeString(b);
    return s;
}

QStringList uiaPatternNames(IUIAutomationElement *el)
{
    QStringList names;
    const QList<QPair<PATTERNID, QString>> patterns{
        {UIA_InvokePatternId, QStringLiteral("invoke")},
        {UIA_ValuePatternId, QStringLiteral("value")},
        {UIA_TogglePatternId, QStringLiteral("toggle")},
        {UIA_SelectionItemPatternId, QStringLiteral("select")},
        {UIA_ExpandCollapsePatternId, QStringLiteral("expandCollapse")},
        {UIA_RangeValuePatternId, QStringLiteral("range")},
        {UIA_ScrollItemPatternId, QStringLiteral("scrollIntoView")},
        {UIA_TextPatternId, QStringLiteral("text")}};
    for (const auto &entry : patterns) {
        IUnknown *pattern = nullptr;
        if (SUCCEEDED(el->GetCurrentPattern(entry.first, &pattern)) && pattern) {
            names << entry.second;
            pattern->Release();
        }
    }
    return names;
}

// Resumen semántico de un elemento UIA: name/role/controlId/geometría + la HWND
// de la ventana de nivel superior que lo contiene (para re-anclarlo al reproducir).
QVariantMap uiaElementInfo(IUIAutomation *uia, IUIAutomationElement *el)
{
    const QString name = uiaName(el);
    CONTROLTYPEID ct = 0;
    el->get_CurrentControlType(&ct);
    RECT rc{};
    el->get_CurrentBoundingRectangle(&rc);
    BOOL enabled = FALSE;
    el->get_CurrentIsEnabled(&enabled);
    BOOL invokable = FALSE;
    IUnknown *pat = nullptr;
    if (SUCCEEDED(el->GetCurrentPattern(UIA_InvokePatternId, &pat)) && pat) {
        invokable = TRUE;
        pat->Release();
    }
    BOOL password = FALSE;
    el->get_CurrentIsPassword(&password);
    const QStringList patterns = uiaPatternNames(el);
    QString value;
    if (!password) {
        IUIAutomationValuePattern *valuePattern = nullptr;
        if (SUCCEEDED(el->GetCurrentPatternAs(UIA_ValuePatternId,
                          IID_IUIAutomationValuePattern,
                          reinterpret_cast<void **>(&valuePattern))) && valuePattern) {
            BSTR current = nullptr;
            if (SUCCEEDED(valuePattern->get_CurrentValue(&current)) && current) {
                value = QString::fromWCharArray(current, static_cast<int>(SysStringLen(current)))
                            .simplified().left(160);
                SysFreeString(current);
            }
            valuePattern->Release();
        }
    }
    // AutomationId: id estable definido por el dev de la app (mejor ancla que name).
    QString automationId;
    BSTR aid = nullptr;
    if (SUCCEEDED(el->get_CurrentAutomationId(&aid)) && aid) {
        automationId = QString::fromWCharArray(aid, static_cast<int>(SysStringLen(aid)));
        SysFreeString(aid);
    }
    // HWND de la ventana top-level dueña del control + su título y rectángulo:
    // permiten re-anclar en el replay contra la ventana ACTUAL (robusto a que la
    // ventana esté en otra posición/tamaño que al grabar).
    QString windowId, windowLabel;
    int winX = 0, winY = 0, winW = 0, winH = 0;
    UIA_HWND h = nullptr;
    if (uia && SUCCEEDED(el->get_CurrentNativeWindowHandle(&h)) && h) {
        HWND top = GetAncestor(reinterpret_cast<HWND>(h), GA_ROOT);
        if (top) {
            windowId = QString::number(reinterpret_cast<quintptr>(top), 16);
            wchar_t title[512]{};
            GetWindowTextW(top, title, 511);
            windowLabel = QString::fromWCharArray(title).trimmed();
            RECT wr{};
            if (GetWindowRect(top, &wr)) {
                winX = wr.left; winY = wr.top;
                winW = wr.right - wr.left; winH = wr.bottom - wr.top;
            }
        }
    }
    return QVariantMap{
        {QStringLiteral("controlId"), uiaRuntimeId(el)},
        {QStringLiteral("automationId"), automationId},
        {QStringLiteral("name"), name.simplified().left(120)},
        {QStringLiteral("role"), uiaControlTypeName(ct)},
        {QStringLiteral("windowId"), windowId},
        {QStringLiteral("windowLabel"), windowLabel},
        {QStringLiteral("winX"), winX}, {QStringLiteral("winY"), winY},
        {QStringLiteral("winWidth"), winW}, {QStringLiteral("winHeight"), winH},
        {QStringLiteral("x"), static_cast<int>(rc.left)},
        {QStringLiteral("y"), static_cast<int>(rc.top)},
        {QStringLiteral("width"), static_cast<int>(rc.right - rc.left)},
        {QStringLiteral("height"), static_cast<int>(rc.bottom - rc.top)},
        {QStringLiteral("enabled"), static_cast<bool>(enabled)},
        {QStringLiteral("invokable"), static_cast<bool>(invokable)},
        {QStringLiteral("password"), static_cast<bool>(password)},
        {QStringLiteral("patterns"), patterns},
        {QStringLiteral("value"), value}};
}

QImage captureWindowOffscreen(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return {};
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return {};
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return {};
    HDC dc = GetDC(nullptr);
    if (!dc) return {};
    HDC memory = CreateCompatibleDC(dc);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memory || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, dc);
        return {};
    }
    HGDIOBJ old = SelectObject(memory, bitmap);
    const BOOL printed = PrintWindow(hwnd, memory, PW_RENDERFULLCONTENT);
    QImage result;
    if (printed && bits)
        result = QImage(static_cast<uchar *>(bits), width, height, width * 4,
                        QImage::Format_ARGB32).copy();
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, dc);
    return result;
}
#endif
}

QVariantList DesktopAutomationBackend::screens()
{
    QVariantList out;
    const auto list = QGuiApplication::screens();
    for (int i = 0; i < list.size(); ++i) {
        QScreen *s = list.at(i);
        // FISICO, igual que windows() (GetWindowRect): las dos listas alimentan los
        // mismos clics, así que tienen que hablar el mismo idioma. Con lógicos, un
        // monitor al 150% reportaba 1280x720 y los clics caían corridos.
        const QRect r = screenPhysicalGeometryOf(s);
        out.append(QVariantMap{
            {QStringLiteral("id"), QString::number(i)},
            {QStringLiteral("kind"), QStringLiteral("screen")},
            {QStringLiteral("label"), s->name().isEmpty()
                 ? QStringLiteral("Pantalla %1").arg(i + 1) : s->name()},
            {QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()},
            {QStringLiteral("width"), r.width()}, {QStringLiteral("height"), r.height()},
            {QStringLiteral("dpi"), s->logicalDotsPerInch()},
            {QStringLiteral("primary"), s == QGuiApplication::primaryScreen()}});
    }
    return out;
}

QVariantList DesktopAutomationBackend::windows()
{
#ifdef Q_OS_WIN
    WindowCollector collector;
    EnumWindows(collectWindow, reinterpret_cast<LPARAM>(&collector));
    return collector.rows;
#else
    return {};
#endif
}

QVariantMap DesktopAutomationBackend::targetInfo(const QString &kind, const QString &targetId)
{
    const QVariantList list = kind == QLatin1String("window") ? windows() : screens();
    for (const QVariant &item : list) {
        const QVariantMap row = item.toMap();
        if (row.value(QStringLiteral("id")).toString() == targetId) return row;
    }
    return {};
}

QVariantMap DesktopAutomationBackend::snapshot(const QString &kind, const QString &targetId,
                                                const QString &query, int max,
                                                bool includeCapture, QString *error)
{
    if (error) error->clear();
    const QVariantMap target = targetInfo(kind, targetId);
    if (target.isEmpty()) {
        if (error) *error = QStringLiteral("El alcance visual ya no está disponible.");
        return {};
    }
    const QVariantList currentWindows = windows();
    QVariantList currentControls;
    if (kind == QLatin1String("window"))
        currentControls = controls(targetId, query, max, error);
    if (error && !error->isEmpty() && kind == QLatin1String("window")) return {};

    QVariantMap result{
        {QStringLiteral("scopeKind"), kind},
        {QStringLiteral("targetId"), targetId},
        {QStringLiteral("target"), target},
        {QStringLiteral("windows"), currentWindows.mid(0, 32)},
        {QStringLiteral("controls"), currentControls},
        {QStringLiteral("provider"), kind == QLatin1String("window")
            ? QStringLiteral("windows-offscreen-or-screen-fallback")
            : QStringLiteral("screen")}};
#ifdef Q_OS_WIN
    const HWND foreground = GetForegroundWindow();
    const QString foregroundId = foreground
        ? QString::number(reinterpret_cast<quintptr>(foreground), 16) : QString();
    result[QStringLiteral("focus")] = QVariantMap{
        {QStringLiteral("windowId"), foregroundId},
        {QStringLiteral("matchesTarget"), kind == QLatin1String("window")
            && foregroundId == targetId}};
#else
    result[QStringLiteral("focus")] = QVariantMap{};
#endif
    const QString id = DesktopComputerUse::snapshotId(
        kind, targetId, target, result.value(QStringLiteral("windows")).toList(), currentControls);
    result[QStringLiteral("snapshotId")] = id;
    result[QStringLiteral("fingerprint")] = id;
    result[QStringLiteral("createdAt")] = QDateTime::currentMSecsSinceEpoch();
    result[QStringLiteral("captureRequested")] = includeCapture;
    if (includeCapture) {
        QString captureError;
        const QImage image = capture(kind, targetId, &captureError);
        if (image.isNull()) {
            if (error) *error = captureError;
            return {};
        }
        result[QStringLiteral("capture")] = QVariantMap{
            {QStringLiteral("width"), image.width()},
            {QStringLiteral("height"), image.height()},
            {QStringLiteral("provider"), kind == QLatin1String("window")
                ? QStringLiteral("windows-offscreen-or-screen-fallback")
                : QStringLiteral("screen")}};
    }
    return result;
}

bool DesktopAutomationBackend::validateSnapshot(const QString &kind, const QString &targetId,
                                                 const QString &snapshotId, QString *error)
{
    if (snapshotId.trimmed().isEmpty()) return true;
    const QVariantMap current = snapshot(kind, targetId, QString(), 120, false, error);
    if (current.isEmpty()) return false;
    return DesktopComputerUse::snapshotMatches(
        snapshotId, current.value(QStringLiteral("snapshotId")).toString(), error);
}

QImage DesktopAutomationBackend::capture(const QString &kind, const QString &targetId, QString *error)
{
    if (error) error->clear();
    const QRect bounds = targetBounds(kind, targetId);   // FISICO
    if (!bounds.isValid()) {
        if (error) *error = QStringLiteral("El alcance visual ya no está disponible.");
        return {};
    }
#ifdef Q_OS_WIN
    if (kind == QLatin1String("window")) {
        bool parsed = false;
        const quintptr raw = targetId.toULongLong(&parsed, 16);
        const QImage offscreen = parsed
            ? captureWindowOffscreen(reinterpret_cast<HWND>(raw)) : QImage();
        if (!offscreen.isNull()) return offscreen;
    }
#endif
    QScreen *screen = screenForPhysicalPoint(bounds.center());
    if (!screen) {
        if (error) *error = QStringLiteral("No hay una pantalla disponible.");
        return {};
    }
    // grabWindow() recibe coords LOGICAS relativas a la pantalla, pero `bounds` es
    // físico: hay que convertir. (Antes se le pasaba el rect físico de
    // GetWindowRect tal cual, así que capturar una ventana en un monitor escalado
    // agarraba la región equivocada.) La captura vuelve en píxeles de dispositivo,
    // o sea del tamaño físico de `bounds` → readText() la mapea 1:1.
    const QRect sp = screenPhysicalGeometryOf(screen);
    const double dpr = screen->devicePixelRatio() > 0 ? screen->devicePixelRatio() : 1.0;
    const QPixmap pix = screen->grabWindow(0,
        qRound((bounds.x() - sp.x()) / dpr), qRound((bounds.y() - sp.y()) / dpr),
        qRound(bounds.width() / dpr), qRound(bounds.height() / dpr));
    if (pix.isNull() && error) *error = QStringLiteral("No se pudo capturar el escritorio.");
    return pix.toImage();
}

QString DesktopAutomationBackend::saveCapture(const QString &kind, const QString &targetId,
                                              const QString &path, QString *error)
{
    const QImage image = capture(kind, targetId, error);
    if (image.isNull()) return {};
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!image.save(path, "JPG", 82)) {
        if (error) *error = QStringLiteral("No se pudo guardar la captura.");
        return {};
    }
    return path;
}

QPointF DesktopAutomationBackend::normalizePoint(const QPoint &absolute, const QRect &bounds)
{
    if (bounds.width() <= 0 || bounds.height() <= 0) return {};
    return QPointF(qBound(0.0, double(absolute.x() - bounds.x()) / bounds.width(), 1.0),
                   qBound(0.0, double(absolute.y() - bounds.y()) / bounds.height(), 1.0));
}

QPoint DesktopAutomationBackend::denormalizePoint(const QPointF &normalized, const QRect &bounds)
{
    return QPoint(bounds.x() + qRound(qBound(0.0, normalized.x(), 1.0) * bounds.width()),
                  bounds.y() + qRound(qBound(0.0, normalized.y(), 1.0) * bounds.height()));
}

bool DesktopAutomationBackend::interactiveSessionAvailable()
{
#ifdef Q_OS_WIN
    HDESK desk = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
    if (!desk) return false;
    const BOOL available = SwitchDesktop(desk);
    CloseDesktop(desk);
    return available != FALSE;
#else
    return true;
#endif
}

bool DesktopAutomationBackend::launchApp(const QString &app, const QString &args, QString *error)
{
    const QString program = app.trimmed();
    if (program.isEmpty()) {
        if (error) *error = QStringLiteral("Falta el nombre de la app a lanzar.");
        return false;
    }
#ifdef Q_OS_WIN
    // `start "" <app> <args>` vía cmd: lanza DESPRENDIDO (cmd sale al instante) y
    // resuelve apps del PATH, rutas .exe y verbos del shell (calc, notepad,
    // ms-settings:, etc.). startDetached no bloquea ni hereda los pipes → el turno
    // no queda colgado como con run_shell sobre una app GUI.
    QStringList argv{QStringLiteral("/c"), QStringLiteral("start"), QString(), program};
    const QString extra = args.trimmed();
    if (!extra.isEmpty()) argv << extra;
    const bool ok = QProcess::startDetached(QStringLiteral("cmd"), argv);
    if (!ok && error) *error = QStringLiteral("No se pudo lanzar: %1").arg(program);
    return ok;
#else
    QStringList argv;
    const QString extra = args.trimmed();
    if (!extra.isEmpty()) argv = extra.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const bool ok = QProcess::startDetached(program, argv);
    if (!ok && error) *error = QStringLiteral("No se pudo lanzar: %1").arg(program);
    return ok;
#endif
}

bool DesktopAutomationBackend::focusWindow(const QString &targetId, QString *error)
{
#ifdef Q_OS_WIN
    bool ok = false;
    HWND hwnd = reinterpret_cast<HWND>(targetId.toULongLong(&ok, 16));
    if (!ok || !hwnd || !IsWindow(hwnd)) {
        if (error) *error = QStringLiteral("Ventana no encontrada.");
        return false;
    }
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    ShowWindow(hwnd, SW_SHOW);

    // SetForegroundWindow puede rechazar una llamada válida por las reglas de
    // foreground-lock de Windows (muy común cuando la ventana fue abierta por
    // `cmd /c start`, como Calculadora). Primero probamos el camino barato. Si
    // falla, enlazamos temporalmente las colas de input del proceso actual, la
    // ventana foreground y el target; así BringWindowToTop/SetFocus operan en el
    // mismo grupo de input sin dejar hilos adjuntos después de esta función.
    if (GetForegroundWindow() == hwnd || SetForegroundWindow(hwnd)) return true;

    const DWORD currentThread = GetCurrentThreadId();
    const HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = foreground
        ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD targetThread = GetWindowThreadProcessId(hwnd, nullptr);
    const bool attachedForeground = foregroundThread != 0
        && foregroundThread != currentThread
        && AttachThreadInput(currentThread, foregroundThread, TRUE);
    const bool attachedTarget = targetThread != 0
        && targetThread != currentThread && targetThread != foregroundThread
        && AttachThreadInput(currentThread, targetThread, TRUE);

    BringWindowToTop(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    const bool requested = SetForegroundWindow(hwnd) != FALSE;
    const bool focused = requested || GetForegroundWindow() == hwnd;

    if (attachedTarget)
        AttachThreadInput(currentThread, targetThread, FALSE);
    if (attachedForeground)
        AttachThreadInput(currentThread, foregroundThread, FALSE);

    if (!focused) {
        if (error) *error = QStringLiteral("Windows no permitió enfocar la ventana.");
        return false;
    }
    return true;
#else
    Q_UNUSED(targetId)
    if (error) *error = QStringLiteral("Control de ventanas disponible sólo en Windows.");
    return false;
#endif
}

bool DesktopAutomationBackend::click(const QString &kind, const QString &targetId,
                                     double x, double y, const QString &button,
                                     QString *error, QVariantMap *trace)
{
    if (!isNormalizedPoint(x, y)) {
        if (error) *error = QStringLiteral(
            "Coordenadas inválidas: x/y deben estar normalizadas entre 0 y 1; "
            "no se ejecutó ningún clic.");
        return false;
    }
    if (!interactiveSessionAvailable()) {
        if (error) *error = QStringLiteral("La sesión de escritorio está bloqueada.");
        return false;
    }
    const QRect bounds = targetBounds(kind, targetId);
    if (!bounds.isValid()) {
        if (error) *error = QStringLiteral("Alcance visual no disponible.");
        return false;
    }
#ifdef Q_OS_WIN
    if (kind == QLatin1String("window") && !focusWindow(targetId, error)) return false;
    const QPoint p = denormalizePoint(QPointF(x, y), bounds);
    SetCursorPos(p.x(), p.y());
    const QString b = button.trimmed().toLower();
    const bool right = b == QLatin1String("right");
    const bool middle = b == QLatin1String("middle");
    INPUT inputs[2]{};
    inputs[0].type = inputs[1].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN
                         : middle ? MOUSEEVENTF_MIDDLEDOWN
                                  : MOUSEEVENTF_LEFTDOWN;
    inputs[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP
                         : middle ? MOUSEEVENTF_MIDDLEUP
                                  : MOUSEEVENTF_LEFTUP;
    if (trace) {
        *trace = QVariantMap{
            {QStringLiteral("surface"), QStringLiteral("desktop")},
            {QStringLiteral("action"), QStringLiteral("click")},
            {QStringLiteral("pointer"), QVariantMap{
                {QStringLiteral("button"), right ? QStringLiteral("right")
                                      : middle ? QStringLiteral("middle")
                                               : QStringLiteral("left")},
                {QStringLiteral("clickCount"), 1},
                {QStringLiteral("xAbs"), p.x()},
                {QStringLiteral("yAbs"), p.y()},
                {QStringLiteral("xNorm"), x},
                {QStringLiteral("yNorm"), y}}},
            {QStringLiteral("target"), QVariantMap{
                {QStringLiteral("scopeKind"), kind},
                {QStringLiteral("targetId"), targetId},
                {QStringLiteral("x"), bounds.x()},
                {QStringLiteral("y"), bounds.y()},
                {QStringLiteral("width"), bounds.width()},
                {QStringLiteral("height"), bounds.height()}}}};
    }
    return SendInput(2, inputs, sizeof(INPUT)) == 2;
#else
    Q_UNUSED(x) Q_UNUSED(y) Q_UNUSED(button) Q_UNUSED(trace)
    if (error) *error = QStringLiteral("Control de escritorio disponible sólo en Windows.");
    return false;
#endif
}

bool DesktopAutomationBackend::stroke(const QString &kind, const QString &targetId,
                                      const QVariantList &points, const QString &button,
                                      int holdMs, QString *error, QVariantMap *trace)
{
    if (points.size() < 2) {
        if (error) *error = QStringLiteral("Una traza necesita al menos 2 puntos.");
        return false;
    }
    for (const QVariant &v : points) {
        const QVariantMap point = v.toMap();
        if (!isNormalizedPoint(point.value(QStringLiteral("x")).toDouble(),
                               point.value(QStringLiteral("y")).toDouble())) {
            if (error) *error = QStringLiteral(
                "Traza inválida: todos los puntos x/y deben estar normalizados "
                "entre 0 y 1; no se movió el mouse.");
            return false;
        }
    }
    const QRect bounds = targetBounds(kind, targetId);
    if (!bounds.isValid()) {
        if (error) *error = QStringLiteral("Alcance visual no disponible.");
        return false;
    }
#ifdef Q_OS_WIN
    if (kind == QLatin1String("window") && !focusWindow(targetId, error)) return false;
    const QString b = button.trimmed().toLower();
    const bool right = b == QLatin1String("right");
    const bool middle = b == QLatin1String("middle");
    const DWORD downFlag = right ? MOUSEEVENTF_RIGHTDOWN
                         : middle ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_LEFTDOWN;
    const DWORD upFlag   = right ? MOUSEEVENTF_RIGHTUP
                         : middle ? MOUSEEVENTF_MIDDLEUP : MOUSEEVENTF_LEFTUP;

    // Puntos normalizados → absolutos.
    QVector<QPoint> abs;
    abs.reserve(points.size());
    for (const QVariant &v : points) {
        const QVariantMap m = v.toMap();
        abs << denormalizePoint(QPointF(m.value(QStringLiteral("x")).toDouble(),
                                        m.value(QStringLiteral("y")).toDouble()), bounds);
    }

    // Movimiento por SendInput con MOUSEEVENTF_MOVE|ABSOLUTE (no SetCursorPos): las
    // apps de dibujo (Paint) SÓLO registran el arrastre si el movimiento llega como
    // eventos de mouse reales entre el down y el up. SetCursorPos no genera ese
    // stream y el trazo no se dibuja. Coordenadas absolutas 0..65535 sobre el
    // escritorio virtual (multi-monitor).
    const int vsX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vsY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vsW = qMax(1, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1);
    const int vsH = qMax(1, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
    auto sendMove = [&](const QPoint &pt, DWORD extra) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dx = static_cast<LONG>((pt.x() - vsX) * 65535.0 / vsW);
        in.mi.dy = static_cast<LONG>((pt.y() - vsY) * 65535.0 / vsH);
        in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | extra;
        SendInput(1, &in, sizeof(INPUT));
    };
    const int hold = qBound(1, holdMs, 200);

    sendMove(abs.first(), 0);            // posicionar
    Sleep(15);
    sendMove(abs.first(), downFlag);     // apretar en el primer punto
    Sleep(hold);
    // Interpolar cada segmento: pasos de ~4px para que la línea salga continua
    // aunque los puntos grabados vengan cada 80ms (muy espaciados en un swipe).
    for (int i = 1; i < abs.size(); ++i) {
        const QPoint a = abs.at(i - 1), c = abs.at(i);
        const int dist = qMax(qAbs(c.x() - a.x()), qAbs(c.y() - a.y()));
        const int steps = qBound(1, dist / 4, 400);
        for (int s = 1; s <= steps; ++s) {
            const int x = a.x() + (c.x() - a.x()) * s / steps;
            const int y = a.y() + (c.y() - a.y()) * s / steps;
            sendMove(QPoint(x, y), 0);
            Sleep(hold);
        }
    }
    sendMove(abs.last(), upFlag);        // soltar

    if (trace) {
        *trace = QVariantMap{
            {QStringLiteral("surface"), QStringLiteral("desktop")},
            {QStringLiteral("action"), QStringLiteral("stroke")},
            {QStringLiteral("pointer"), QVariantMap{
                {QStringLiteral("button"), right ? QStringLiteral("right")
                                      : middle ? QStringLiteral("middle") : QStringLiteral("left")},
                {QStringLiteral("points"), static_cast<int>(points.size())},
                {QStringLiteral("xAbsStart"), abs.first().x()},
                {QStringLiteral("yAbsStart"), abs.first().y()},
                {QStringLiteral("xAbsEnd"), abs.last().x()},
                {QStringLiteral("yAbsEnd"), abs.last().y()}}},
            {QStringLiteral("target"), QVariantMap{
                {QStringLiteral("scopeKind"), kind},
                {QStringLiteral("targetId"), targetId},
                {QStringLiteral("x"), bounds.x()},
                {QStringLiteral("y"), bounds.y()},
                {QStringLiteral("width"), bounds.width()},
                {QStringLiteral("height"), bounds.height()}}}};
    }
    return true;
#else
    Q_UNUSED(button) Q_UNUSED(holdMs) Q_UNUSED(trace)
    if (error) *error = QStringLiteral("Control de escritorio disponible sólo en Windows.");
    return false;
#endif
}

bool DesktopAutomationBackend::typeText(const QString &text, QString *error)
{
#ifdef Q_OS_WIN
    if (!interactiveSessionAvailable()) {
        if (error) *error = QStringLiteral("La sesión está bloqueada.");
        return false;
    }
    for (QChar c : text) {
        INPUT inputs[2]{};
        inputs[0].type = inputs[1].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = inputs[1].ki.wScan = c.unicode();
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        if (SendInput(2, inputs, sizeof(INPUT)) != 2) {
            if (error) *error = QStringLiteral("Falló la inyección de texto.");
            return false;
        }
    }
    return true;
#else
    Q_UNUSED(text)
    if (error) *error = QStringLiteral("Control de escritorio disponible sólo en Windows.");
    return false;
#endif
}

bool DesktopAutomationBackend::pressKey(const QString &key, const QStringList &modifiers,
                                        QString *error)
{
#ifdef Q_OS_WIN
    const WORD vk = keyCode(key);
    if (!vk) {
        if (error) *error = QStringLiteral("Tecla no reconocida: %1").arg(key);
        return false;
    }
    QList<WORD> mods;
    for (const QString &m : modifiers) {
        const QString upper = m.toUpper();
        if (upper == QLatin1String("CTRL")) mods << VK_CONTROL;
        else if (upper == QLatin1String("ALT")) mods << VK_MENU;
        else if (upper == QLatin1String("SHIFT")) mods << VK_SHIFT;
        else if (upper == QLatin1String("WIN")) mods << VK_LWIN;
    }
    for (WORD m : mods) sendKey(m, true);
    sendKey(vk, true); sendKey(vk, false);
    for (auto it = mods.crbegin(); it != mods.crend(); ++it) sendKey(*it, false);
    return true;
#else
    Q_UNUSED(key) Q_UNUSED(modifiers)
    if (error) *error = QStringLiteral("Control de escritorio disponible sólo en Windows.");
    return false;
#endif
}

bool DesktopAutomationBackend::scroll(int delta, QString *error)
{
#ifdef Q_OS_WIN
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(delta);
    const bool ok = SendInput(1, &input, sizeof(INPUT)) == 1;
    if (!ok && error) *error = QStringLiteral("No se pudo desplazar.");
    return ok;
#else
    Q_UNUSED(delta)
    if (error) *error = QStringLiteral("Control de escritorio disponible sólo en Windows.");
    return false;
#endif
}

QVariantMap DesktopAutomationBackend::cursorState()
{
    const QPoint p = cursorPosPhysical();
    return {{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}};
}

bool DesktopAutomationBackend::moveCursor(const QPoint &physical)
{
#ifdef Q_OS_WIN
    return SetCursorPos(physical.x(), physical.y());
#else
    QCursor::setPos(physical);
    return true;
#endif
}

QPoint DesktopAutomationBackend::cursorPosPhysical()
{
#ifdef Q_OS_WIN
    // GetCursorPos, no QCursor::pos(): éste devuelve lógicos de Qt y todo el resto
    // del stack (SetCursorPos, UIA, los bounds de los targets) habla en físicos.
    // Mezclarlos hacía que Teach anclara el control equivocado en un monitor
    // escalado, y que el replay clickeara corrido.
    POINT p{};
    if (GetCursorPos(&p)) return QPoint(p.x, p.y);
#endif
    return QCursor::pos();
}

QVariantList DesktopAutomationBackend::controls(const QString &windowTargetId,
                                                const QString &query, int max, QString *error)
{
#ifdef Q_OS_WIN
    bool ok = false;
    HWND hwnd = reinterpret_cast<HWND>(windowTargetId.toULongLong(&ok, 16));
    if (!ok || !hwnd || !IsWindow(hwnd)) {
        if (error) *error = QStringLiteral("Ventana no encontrada.");
        return {};
    }
    if (max <= 0) max = 120;
    if (max > 400) max = 400;

    ComGuard com;
    IUIAutomation *uia = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IUIAutomation, reinterpret_cast<void **>(&uia))) || !uia) {
        if (error) *error = QStringLiteral("UI Automation no disponible.");
        return {};
    }
    IUIAutomationElement *root = nullptr;
    IUIAutomationTreeWalker *walker = nullptr;
    if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root
        || FAILED(uia->get_ControlViewWalker(&walker)) || !walker) {
        if (root) root->Release();
        uia->Release();
        if (error) *error = QStringLiteral("No se pudo abrir la ventana en UIA.");
        return {};
    }

    const QString needle = query.trimmed().toLower();
    struct Node { IUIAutomationElement *el; int depth; };
    QList<Node> queue;
    auto enqueueChildren = [&](IUIAutomationElement *parent, int depth) {
        if (depth > 14) return;
        IUIAutomationElement *child = nullptr;
        if (FAILED(walker->GetFirstChildElement(parent, &child))) return;
        while (child) {
            queue.append({child, depth});
            IUIAutomationElement *next = nullptr;
            walker->GetNextSiblingElement(child, &next);
            child = next;
        }
    };

    QVariantList out;
    enqueueChildren(root, 1);
    int visited = 0;
    while (!queue.isEmpty() && out.size() < max && visited < 3000) {
        const Node n = queue.takeFirst();
        IUIAutomationElement *el = n.el;
        ++visited;

        const QVariantMap info = uiaElementInfo(uia, el);
        const QString name = info.value(QStringLiteral("name")).toString();
        const bool enabled = info.value(QStringLiteral("enabled")).toBool();
        const bool invokable = info.value(QStringLiteral("invokable")).toBool();

        // Incluir sólo controles útiles: con nombre o invocables. Filtrar por query.
        const bool interesting = !name.trimmed().isEmpty() || invokable;
        if (interesting && (needle.isEmpty() || name.toLower().contains(needle))) {
            out.append(info);
        }
        enqueueChildren(el, n.depth + 1);
        el->Release();
    }
    for (const Node &n : queue) n.el->Release();
    walker->Release();
    root->Release();
    uia->Release();
    return out;
#else
    Q_UNUSED(windowTargetId) Q_UNUSED(query) Q_UNUSED(max)
    if (error) *error = QStringLiteral("UI Automation disponible sólo en Windows.");
    return {};
#endif
}

bool DesktopAutomationBackend::clickElement(const QString &windowTargetId,
                                            const QString &controlId, QString *error,
                                            QVariantMap *trace)
{
    return clickElement(windowTargetId, controlId, QString(), error, trace);
}

bool DesktopAutomationBackend::clickElement(const QString &windowTargetId,
                                            const QString &controlId,
                                            const QString &snapshotId,
                                            QString *error, QVariantMap *trace)
{
    if (!validateSnapshot(QStringLiteral("window"), windowTargetId, snapshotId, error))
        return false;
    return clickElementInternal(windowTargetId, controlId, /*allowFuzzy=*/true, error, trace);
}

bool DesktopAutomationBackend::clickElementInternal(const QString &windowTargetId,
                                                    const QString &controlId, bool allowFuzzy,
                                                    QString *error, QVariantMap *trace)
{
#ifdef Q_OS_WIN
    bool ok = false;
    HWND hwnd = reinterpret_cast<HWND>(windowTargetId.toULongLong(&ok, 16));
    if (!ok || !hwnd || !IsWindow(hwnd)) {
        if (error) *error = QStringLiteral("Ventana no encontrada.");
        return false;
    }
    if (controlId.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("controlId vacío.");
        return false;
    }
    ComGuard com;
    IUIAutomation *uia = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IUIAutomation, reinterpret_cast<void **>(&uia))) || !uia) {
        if (error) *error = QStringLiteral("UI Automation no disponible.");
        return false;
    }
    IUIAutomationElement *root = nullptr;
    IUIAutomationTreeWalker *walker = nullptr;
    if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root
        || FAILED(uia->get_ControlViewWalker(&walker)) || !walker) {
        if (root) root->Release();
        uia->Release();
        if (error) *error = QStringLiteral("No se pudo abrir la ventana en UIA.");
        return false;
    }

    struct Node { IUIAutomationElement *el; int depth; };
    QList<Node> queue;
    auto enqueueChildren = [&](IUIAutomationElement *parent, int depth) {
        if (depth > 14) return;
        IUIAutomationElement *child = nullptr;
        if (FAILED(walker->GetFirstChildElement(parent, &child))) return;
        while (child) {
            queue.append({child, depth});
            IUIAutomationElement *next = nullptr;
            walker->GetNextSiblingElement(child, &next);
            child = next;
        }
    };

    bool done = false, success = false;
    enqueueChildren(root, 1);
    int visited = 0;
    while (!queue.isEmpty() && !done && visited < 3000) {
        const Node n = queue.takeFirst();
        IUIAutomationElement *el = n.el;
        ++visited;
        if (uiaRuntimeId(el) == controlId) {
            done = true;
            const QString name = uiaName(el).simplified().left(120);
            CONTROLTYPEID ct = 0;
            el->get_CurrentControlType(&ct);
            RECT rc{};
            el->get_CurrentBoundingRectangle(&rc);
            const QVariantMap targetTrace{
                {QStringLiteral("windowTargetId"), windowTargetId},
                {QStringLiteral("controlId"), controlId},
                {QStringLiteral("matchedBy"), QStringLiteral("controlId")},
                {QStringLiteral("name"), name},
                {QStringLiteral("role"), uiaControlTypeName(ct)},
                {QStringLiteral("x"), static_cast<int>(rc.left)},
                {QStringLiteral("y"), static_cast<int>(rc.top)},
                {QStringLiteral("width"), static_cast<int>(rc.right - rc.left)},
                {QStringLiteral("height"), static_cast<int>(rc.bottom - rc.top)}};
            // Preferir el patrón Invoke (clic semántico, robusto); si no, clic al
            // centro del bounding rect.
            IUIAutomationInvokePattern *inv = nullptr;
            if (SUCCEEDED(el->GetCurrentPatternAs(UIA_InvokePatternId,
                              IID_IUIAutomationInvokePattern, reinterpret_cast<void **>(&inv)))
                && inv) {
                success = SUCCEEDED(inv->Invoke());
                inv->Release();
                if (trace) {
                    *trace = QVariantMap{
                        {QStringLiteral("surface"), QStringLiteral("desktop")},
                        {QStringLiteral("action"), QStringLiteral("click_element")},
                        {QStringLiteral("target"), targetTrace},
                        {QStringLiteral("pointer"), QVariantMap{
                            {QStringLiteral("button"), QStringLiteral("semantic-invoke")},
                            {QStringLiteral("clickCount"), 1}}}};
                }
            } else {
                if (rc.right > rc.left && rc.bottom > rc.top) {
                    // Sólo el fallback físico necesita foco; Invoke es
                    // semántico y debe poder operar sin robarlo.
                    if (!interactiveSessionAvailable()) {
                        if (error) *error = QStringLiteral(
                            "El control no expone un patrón semántico y la sesión interactiva está bloqueada.");
                        break;
                    }
                    focusWindow(windowTargetId, nullptr);
                    const int cx = (rc.left + rc.right) / 2;
                    const int cy = (rc.top + rc.bottom) / 2;
                    SetCursorPos(cx, cy);
                    INPUT inputs[2]{};
                    inputs[0].type = inputs[1].type = INPUT_MOUSE;
                    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                    success = SendInput(2, inputs, sizeof(INPUT)) == 2;
                    if (trace) {
                        *trace = QVariantMap{
                            {QStringLiteral("surface"), QStringLiteral("desktop")},
                            {QStringLiteral("action"), QStringLiteral("click_element")},
                            {QStringLiteral("target"), targetTrace},
                            {QStringLiteral("pointer"), QVariantMap{
                                {QStringLiteral("button"), QStringLiteral("left")},
                                {QStringLiteral("clickCount"), 1},
                                {QStringLiteral("xAbs"), cx},
                                {QStringLiteral("yAbs"), cy}}}};
                    }
                } else if (error) {
                    *error = QStringLiteral("El control no es invocable ni tiene área clickeable.");
                }
            }
        } else {
            enqueueChildren(el, n.depth + 1);
        }
        el->Release();
    }
    for (const Node &n : queue) n.el->Release();
    walker->Release();
    root->Release();
    uia->Release();

    // Fallback: el id no existe. Tratarlo como NOMBRE y resolverlo por matching
    // difuso contra los controles vivos de la ventana. Cubre el caso común de que
    // el modelo mande "Guardar como" en vez del RuntimeId, y el de un id viejo tras
    // un reflow. Una sola re-entrada (allowFuzzy=false) y el id ya es exacto.
    if (!done && allowFuzzy) {
        const QVariantList all = controls(windowTargetId, QString(), 3000, nullptr);
        QStringList names;
        names.reserve(all.size());
        for (const QVariant &v : all)
            names << v.toMap().value(QStringLiteral("name")).toString();
        const FuzzyMatch::Match m = FuzzyMatch::extractOne(controlId, names);
        const QString resolved = m.ok()
            ? all.at(m.index).toMap().value(QStringLiteral("controlId")).toString()
            : QString();
        if (!resolved.isEmpty() && resolved != controlId) {
            if (!clickElementInternal(windowTargetId, resolved, /*allowFuzzy=*/false,
                                      error, trace))
                return false;
            if (trace) {
                QVariantMap t = trace->value(QStringLiteral("target")).toMap();
                t[QStringLiteral("matchedBy")] = QStringLiteral("fuzzy-name");
                t[QStringLiteral("matchScore")] = m.score;
                t[QStringLiteral("requested")] = controlId;
                (*trace)[QStringLiteral("target")] = t;
            }
            return true;
        }
    }

    if (!done && error) *error = QStringLiteral("No se encontró el control (¿cambió la ventana? "
                                                "re-listá con desktop_controls).");
    return success;
#else
    Q_UNUSED(windowTargetId) Q_UNUSED(controlId) Q_UNUSED(allowFuzzy) Q_UNUSED(trace)
    if (error) *error = QStringLiteral("UI Automation disponible sólo en Windows.");
    return false;
#endif
}

bool DesktopAutomationBackend::controlAction(const QString &windowTargetId,
                                             const QString &controlId,
                                             const QString &action,
                                             const QString &value,
                                             QString *error,
                                             QVariantMap *trace)
{
#ifdef Q_OS_WIN
    bool ok = false;
    HWND hwnd = reinterpret_cast<HWND>(windowTargetId.toULongLong(&ok, 16));
    if (!ok || !hwnd || !IsWindow(hwnd)) {
        if (error) *error = QStringLiteral("Ventana no encontrada.");
        return false;
    }
    if (controlId.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("controlId vacío.");
        return false;
    }
    const QString requested = action.trimmed().toLower();
    const QString op = requested.isEmpty() ? QStringLiteral("invoke") : requested;
    const QSet<QString> allowed{
        QStringLiteral("invoke"), QStringLiteral("toggle"), QStringLiteral("set_value"),
        QStringLiteral("select"), QStringLiteral("expand"), QStringLiteral("collapse"),
        QStringLiteral("range_set"), QStringLiteral("scroll_into_view"), QStringLiteral("read")};
    if (!allowed.contains(op)) {
        if (error) *error = QStringLiteral("Acción UIA no soportada: %1.").arg(op);
        return false;
    }

    ComGuard com;
    IUIAutomation *uia = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IUIAutomation, reinterpret_cast<void **>(&uia))) || !uia) {
        if (error) *error = QStringLiteral("UI Automation no disponible.");
        return false;
    }
    IUIAutomationElement *root = nullptr;
    IUIAutomationTreeWalker *walker = nullptr;
    if (FAILED(uia->ElementFromHandle(hwnd, &root)) || !root
        || FAILED(uia->get_ControlViewWalker(&walker)) || !walker) {
        if (root) root->Release();
        uia->Release();
        if (error) *error = QStringLiteral("No se pudo abrir la ventana en UIA.");
        return false;
    }

    struct Node { IUIAutomationElement *el; int depth; };
    QList<Node> queue;
    auto enqueueChildren = [&](IUIAutomationElement *parent, int depth) {
        if (depth > 14) return;
        IUIAutomationElement *child = nullptr;
        if (FAILED(walker->GetFirstChildElement(parent, &child))) return;
        while (child) {
            queue.append({child, depth});
            IUIAutomationElement *next = nullptr;
            walker->GetNextSiblingElement(child, &next);
            child = next;
        }
    };

    bool found = false;
    bool success = false;
    enqueueChildren(root, 1);
    int visited = 0;
    while (!queue.isEmpty() && !found && visited < 3000) {
        const Node node = queue.takeFirst();
        IUIAutomationElement *el = node.el;
        ++visited;
        if (uiaRuntimeId(el) == controlId) {
            found = true;
            const QVariantMap info = uiaElementInfo(uia, el);
            if (trace) {
                *trace = QVariantMap{{QStringLiteral("surface"), QStringLiteral("desktop")},
                                     {QStringLiteral("action"), op},
                                     {QStringLiteral("strategy"), QStringLiteral("uia-pattern")},
                                     {QStringLiteral("target"), info}};
            }
            if (op == QLatin1String("read")) {
                QVariantMap read = info;
                IUIAutomationTogglePattern *toggle = nullptr;
                if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TogglePatternId,
                                  IID_IUIAutomationTogglePattern,
                                  reinterpret_cast<void **>(&toggle))) && toggle) {
                    ToggleState state = ToggleState_Indeterminate;
                    if (SUCCEEDED(toggle->get_CurrentToggleState(&state)))
                        read[QStringLiteral("toggleState")] = static_cast<int>(state);
                    toggle->Release();
                }
                IUIAutomationExpandCollapsePattern *expand = nullptr;
                if (SUCCEEDED(el->GetCurrentPatternAs(UIA_ExpandCollapsePatternId,
                                  IID_IUIAutomationExpandCollapsePattern,
                                  reinterpret_cast<void **>(&expand))) && expand) {
                    ExpandCollapseState state = ExpandCollapseState_LeafNode;
                    if (SUCCEEDED(expand->get_CurrentExpandCollapseState(&state)))
                        read[QStringLiteral("expandState")] = static_cast<int>(state);
                    expand->Release();
                }
                if (trace) (*trace)[QStringLiteral("value")] = read;
                success = true;
            } else if (op == QLatin1String("invoke")) {
                IUIAutomationInvokePattern *pattern = nullptr;
                if (SUCCEEDED(el->GetCurrentPatternAs(UIA_InvokePatternId,
                                  IID_IUIAutomationInvokePattern,
                                  reinterpret_cast<void **>(&pattern))) && pattern) {
                    success = SUCCEEDED(pattern->Invoke());
                    pattern->Release();
                } else {
                    // Sólo este fallback es foreground: no existe operación
                    // semántica disponible para el control.
                    RECT rc{};
                    el->get_CurrentBoundingRectangle(&rc);
                    if (rc.right > rc.left && rc.bottom > rc.top
                        && interactiveSessionAvailable()) {
                        focusWindow(windowTargetId, nullptr);
                        SetCursorPos((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2);
                        INPUT inputs[2]{};
                        inputs[0].type = inputs[1].type = INPUT_MOUSE;
                        inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                        success = SendInput(2, inputs, sizeof(INPUT)) == 2;
                        if (trace) (*trace)[QStringLiteral("strategy")] = QStringLiteral("foreground-pointer");
                    }
                }
            } else if (op == QLatin1String("toggle")) {
                IUIAutomationTogglePattern *pattern = nullptr;
                if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TogglePatternId,
                                  IID_IUIAutomationTogglePattern,
                                  reinterpret_cast<void **>(&pattern))) && pattern) {
                    success = SUCCEEDED(pattern->Toggle());
                    pattern->Release();
                }
            } else if (op == QLatin1String("set_value")) {
                IUIAutomationValuePattern *pattern = nullptr;
                if (SUCCEEDED(el->GetCurrentPatternAs(UIA_ValuePatternId,
                                  IID_IUIAutomationValuePattern,
                                  reinterpret_cast<void **>(&pattern))) && pattern) {
                    BSTR desired = SysAllocString(
                        reinterpret_cast<LPCWSTR>(value.utf16()));
                    success = desired && SUCCEEDED(pattern->SetValue(desired));
                    if (desired) SysFreeString(desired);
                    pattern->Release();
                }
            } else if (op == QLatin1String("select")) {
                IUIAutomationSelectionItemPattern *pattern = nullptr;
                if (SUCCEEDED(el->GetCurrentPatternAs(UIA_SelectionItemPatternId,
                                  IID_IUIAutomationSelectionItemPattern,
                                  reinterpret_cast<void **>(&pattern))) && pattern) {
                    success = SUCCEEDED(pattern->Select());
                    pattern->Release();
                }
            } else if (op == QLatin1String("expand") || op == QLatin1String("collapse")) {
                IUIAutomationExpandCollapsePattern *pattern = nullptr;
                if (SUCCEEDED(el->GetCurrentPatternAs(UIA_ExpandCollapsePatternId,
                                  IID_IUIAutomationExpandCollapsePattern,
                                  reinterpret_cast<void **>(&pattern))) && pattern) {
                    success = SUCCEEDED(op == QLatin1String("expand")
                        ? pattern->Expand() : pattern->Collapse());
                    pattern->Release();
                }
            } else if (op == QLatin1String("range_set")) {
                bool numeric = false;
                const double number = value.toDouble(&numeric);
                IUIAutomationRangeValuePattern *pattern = nullptr;
                if (numeric && SUCCEEDED(el->GetCurrentPatternAs(UIA_RangeValuePatternId,
                                  IID_IUIAutomationRangeValuePattern,
                                  reinterpret_cast<void **>(&pattern))) && pattern) {
                    success = SUCCEEDED(pattern->SetValue(number));
                    pattern->Release();
                }
            } else if (op == QLatin1String("scroll_into_view")) {
                IUIAutomationScrollItemPattern *pattern = nullptr;
                if (SUCCEEDED(el->GetCurrentPatternAs(UIA_ScrollItemPatternId,
                                  IID_IUIAutomationScrollItemPattern,
                                  reinterpret_cast<void **>(&pattern))) && pattern) {
                    success = SUCCEEDED(pattern->ScrollIntoView());
                    pattern->Release();
                }
            }
            if (!success && error)
                *error = QStringLiteral("El control no expone el patrón requerido para '%1'.").arg(op);
        } else {
            enqueueChildren(el, node.depth + 1);
        }
        el->Release();
    }
    for (const Node &node : queue) node.el->Release();
    walker->Release();
    root->Release();
    uia->Release();
    if (!found && error)
        *error = QStringLiteral("No se encontró el control; generá un snapshot nuevo.");
    return found && success;
#else
    Q_UNUSED(windowTargetId) Q_UNUSED(controlId) Q_UNUSED(action) Q_UNUSED(value)
    Q_UNUSED(trace)
    if (error) *error = QStringLiteral("UI Automation disponible sólo en Windows.");
    return false;
#endif
}

QList<OcrLine> DesktopAutomationBackend::readText(const QString &kind, const QString &targetId,
                                                  QString *error)
{
    if (error) error->clear();
    const QRect bounds = targetBounds(kind, targetId);
    if (!bounds.isValid()) {
        if (error) *error = QStringLiteral("El alcance visual ya no está disponible.");
        return {};
    }
    const QImage image = capture(kind, targetId, error);
    if (image.isNull()) return {};
    QList<OcrLine> lines = OcrEngine::recognize(image, error);
    if (lines.isEmpty()) return lines;

    // Escala device→lógico deducida de la captura real, no de un devicePixelRatio
    // consultado aparte: si algo cambia (monitor distinto, escalado raro), el
    // tamaño del bitmap ya lo refleja y la cuenta se corrige sola.
    const double sx = image.width()  > 0 ? double(bounds.width())  / image.width()  : 1.0;
    const double sy = image.height() > 0 ? double(bounds.height()) / image.height() : 1.0;
    for (OcrLine &line : lines) {
        for (OcrWord &w : line.words) {
            w.rect = QRect(bounds.x() + qRound(w.rect.x() * sx),
                           bounds.y() + qRound(w.rect.y() * sy),
                           qRound(w.rect.width()  * sx),
                           qRound(w.rect.height() * sy));
        }
    }
    return lines;
}

bool DesktopAutomationBackend::clickText(const QString &kind, const QString &targetId,
                                         const QString &text, const QString &button,
                                         int clickCount, QString *error, QVariantMap *trace)
{
    if (error) error->clear();
    if (text.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Texto vacío.");
        return false;
    }
    const QRect bounds = targetBounds(kind, targetId);
    const QList<OcrLine> lines = readText(kind, targetId, error);
    if (lines.isEmpty()) {
        if (error && error->isEmpty())
            *error = QStringLiteral("No se leyó ningún texto en pantalla.");
        return false;
    }
    const QList<OcrTextLocator::Hit> hits = OcrTextLocator::findAll(lines, text);
    if (hits.isEmpty()) {
        if (error) *error = QStringLiteral("No se encontró \"%1\" en pantalla.").arg(text);
        return false;
    }
    // Ambigüedad → NO adivinar. Dos coincidencias casi igual de buenas significa
    // que el pedido no identifica un único destino; clickear una al azar puede
    // apretar cualquier cosa. Mejor devolver las opciones y que se precise.
    if (hits.size() >= 2 && hits.at(0).score - hits.at(1).score < 5) {
        if (error) {
            QStringList opts;
            for (const auto &h : hits) {
                if (opts.size() >= 4) break;
                opts << QStringLiteral("\"%1\" en (%2,%3)")
                            .arg(h.text).arg(h.rect.center().x()).arg(h.rect.center().y());
            }
            *error = QStringLiteral("\"%1\" es ambiguo: %2 coincidencias parecidas (%3). "
                                    "Precisá el texto o usá desktop_click con coordenadas.")
                         .arg(text).arg(hits.size()).arg(opts.join(QStringLiteral(", ")));
        }
        return false;
    }

    const OcrTextLocator::Hit &hit = hits.first();
    // Reusar click() con coords normalizadas: la conversión a píxeles reales y el
    // envío del input ya están resueltos y probados ahí.
    const QPointF norm = normalizePoint(hit.center(), bounds);
    // Doble clic = dos clics seguidos: Windows los une si caen dentro de su
    // tiempo de doble clic, y SendInput consecutivo lo cumple de sobra.
    const int times = qBound(1, clickCount, 2);
    for (int i = 0; i < times; ++i)
        if (!click(kind, targetId, norm.x(), norm.y(), button, error, trace))
            return false;
    if (trace) {
        QVariantMap t = trace->value(QStringLiteral("target")).toMap();
        t[QStringLiteral("matchedBy")] = QStringLiteral("ocr-text");
        t[QStringLiteral("matchScore")] = hit.score;
        t[QStringLiteral("requested")] = text;
        t[QStringLiteral("readText")] = hit.text;
        (*trace)[QStringLiteral("target")] = t;
        QVariantMap p = trace->value(QStringLiteral("pointer")).toMap();
        p[QStringLiteral("clickCount")] = times;
        (*trace)[QStringLiteral("pointer")] = p;
    }
    return true;
}

QVariantMap DesktopAutomationBackend::findImage(const QString &kind, const QString &targetId,
                                                 const QString &templatePath, double threshold,
                                                 double minScale, double maxScale,
                                                 bool requireUnique, QString *error)
{
    if (error) error->clear();
    const QImage needle(templatePath);
    if (needle.isNull()) {
        if (error) *error = QStringLiteral("No se pudo leer la plantilla visual: %1")
                                .arg(templatePath);
        return {};
    }
    QString captureError;
    QImage haystack;
#ifdef Q_OS_WIN
    if (kind == QLatin1String("window")) {
        bool parsed = false;
        const quintptr raw = targetId.toULongLong(&parsed, 16);
        if (parsed)
            haystack = captureWindowOffscreen(reinterpret_cast<HWND>(raw));
    }
#endif
    if (haystack.isNull())
        haystack = capture(kind, targetId, &captureError);
    if (haystack.isNull()) {
        if (error) *error = captureError;
        return {};
    }
    VisualMatcher::Options options;
    options.threshold = threshold;
    options.minScale = minScale;
    options.maxScale = maxScale;
    options.requireUnique = requireUnique;
    const VisualMatcher::Result result = VisualMatcher::find(haystack, needle, options);
    if (error && !result.error.isEmpty()) *error = result.error;
    QVariantMap out = result.toVariantMap(haystack.size());
    out[QStringLiteral("templatePath")] = QFileInfo(templatePath).absoluteFilePath();
    out[QStringLiteral("scopeKind")] = kind;
    out[QStringLiteral("targetId")] = targetId;
    return out;
}

bool DesktopAutomationBackend::clickImage(const QString &kind, const QString &targetId,
                                           const QString &templatePath, double threshold,
                                           double minScale, double maxScale,
                                           const QString &button, QString *error,
                                           QVariantMap *trace)
{
    if (error) error->clear();
    const QRect before = targetBounds(kind, targetId);
    const QVariantMap match = findImage(kind, targetId, templatePath, threshold,
                                        minScale, maxScale, true, error);
    if (!match.value(QStringLiteral("found")).toBool()
        || match.value(QStringLiteral("ambiguous")).toBool())
        return false;
    // Evita clickear otra posición si la ventana se movió o redimensionó entre la
    // captura y la acción. El llamador puede observar de nuevo y reintentar.
    const QRect after = targetBounds(kind, targetId);
    if (!before.isValid() || before != after) {
        if (error) *error = QStringLiteral("El alcance cambió después de localizar la imagen; "
                                          "no se ejecutó ningún clic.");
        return false;
    }
    const QVariantMap rect = match.value(QStringLiteral("rect")).toMap();
    const double x = rect.value(QStringLiteral("x")).toDouble()
                   + rect.value(QStringLiteral("width")).toDouble() / 2.0;
    const double y = rect.value(QStringLiteral("y")).toDouble()
                   + rect.value(QStringLiteral("height")).toDouble() / 2.0;
    QVariantMap clickTrace;
    if (!click(kind, targetId, x, y, button, error, &clickTrace)) return false;
    QString postError;
    const QVariantMap post = findImage(kind, targetId, templatePath, threshold,
                                       minScale, maxScale, false, &postError);
    clickTrace[QStringLiteral("visualMatch")] = match;
    clickTrace[QStringLiteral("postMatch")] = post;
    if (trace) *trace = clickTrace;
    return true;
}

QVariantMap DesktopAutomationBackend::waitImage(const QString &kind, const QString &targetId,
                                                 const QString &templatePath, bool appear,
                                                 int timeoutMs, double threshold,
                                                 double minScale, double maxScale,
                                                 QString *error)
{
    if (error) error->clear();
    QElapsedTimer timer;
    timer.start();
    QVariantMap last;
    const int timeout = qBound(0, timeoutMs, 60000);
    do {
        QString matchError;
        last = findImage(kind, targetId, templatePath, threshold, minScale, maxScale,
                         true, &matchError);
        const bool present = last.value(QStringLiteral("found")).toBool()
                          && !last.value(QStringLiteral("ambiguous")).toBool();
        if (present == appear) {
            last[QStringLiteral("conditionMet")] = true;
            last[QStringLiteral("elapsedMs")] = timer.elapsed();
            last[QStringLiteral("expected")] = appear ? QStringLiteral("appear")
                                                        : QStringLiteral("disappear");
            return last;
        }
        if (timer.elapsed() >= timeout) break;
        QThread::msleep(100);
    } while (true);
    last[QStringLiteral("conditionMet")] = false;
    last[QStringLiteral("elapsedMs")] = timer.elapsed();
    if (error) *error = QStringLiteral("La condición visual no se cumplió antes del timeout.");
    return last;
}

QVariantMap DesktopAutomationBackend::assertImage(const QString &kind, const QString &targetId,
                                                   const QString &templatePath, bool shouldExist,
                                                   int timeoutMs, double threshold,
                                                   double minScale, double maxScale,
                                                   QString *error)
{
    QVariantMap result = waitImage(kind, targetId, templatePath, shouldExist, timeoutMs,
                                   threshold, minScale, maxScale, error);
    result[QStringLiteral("pass")] = result.value(QStringLiteral("conditionMet"));
    result[QStringLiteral("assertion")] = shouldExist
        ? QStringLiteral("image exists") : QStringLiteral("image absent");
    return result;
}

QVariantMap DesktopAutomationBackend::controlAtPoint(const QPoint &absolute)
{
#ifdef Q_OS_WIN
    ComGuard com;
    IUIAutomation *uia = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IUIAutomation, reinterpret_cast<void **>(&uia))) || !uia)
        return {};
    POINT pt{absolute.x(), absolute.y()};
    IUIAutomationElement *el = nullptr;
    QVariantMap info;
    if (SUCCEEDED(uia->ElementFromPoint(pt, &el)) && el) {
        info = uiaElementInfo(uia, el);
        el->Release();
    }
    uia->Release();

    // Ancla de ventana por WindowFromPoint (independiente de UIA): la mayoría de los
    // controles (lienzo de Paint, etc.) NO tienen HWND propio, así que el campo de
    // ventana de uiaElementInfo queda vacío. WindowFromPoint + GA_ROOT siempre da la
    // ventana top-level bajo el cursor → título + rect para re-anclar en el replay.
    HWND top = WindowFromPoint(pt);
    if (top) top = GetAncestor(top, GA_ROOT);
    if (top && IsWindow(top)) {
        wchar_t title[512]{};
        GetWindowTextW(top, title, 511);
        const QString label = QString::fromWCharArray(title).trimmed();
        RECT wr{};
        if (!label.isEmpty() && GetWindowRect(top, &wr)) {
            info[QStringLiteral("windowId")] = QString::number(reinterpret_cast<quintptr>(top), 16);
            info[QStringLiteral("windowLabel")] = label;
            info[QStringLiteral("winX")] = static_cast<int>(wr.left);
            info[QStringLiteral("winY")] = static_cast<int>(wr.top);
            info[QStringLiteral("winWidth")] = static_cast<int>(wr.right - wr.left);
            info[QStringLiteral("winHeight")] = static_cast<int>(wr.bottom - wr.top);
            info[QStringLiteral("windowMaximized")] = IsZoomed(top) != FALSE;
        }
    }
    return info;
#else
    Q_UNUSED(absolute)
    return {};
#endif
}

bool DesktopAutomationBackend::setWindowMaximized(const QString &targetId, bool maximized,
                                                   QString *error)
{
#ifdef Q_OS_WIN
    bool parsed = false;
    const quintptr raw = targetId.toULongLong(&parsed, 16);
    HWND hwnd = parsed ? reinterpret_cast<HWND>(raw) : nullptr;
    if (!hwnd || !IsWindow(hwnd)) {
        if (error) *error = QStringLiteral("La ventana ya no está disponible.");
        return false;
    }
    if ((IsZoomed(hwnd) != FALSE) != maximized)
        ShowWindow(hwnd, maximized ? SW_MAXIMIZE : SW_RESTORE);
    if (IsIconic(hwnd)) ShowWindow(hwnd, maximized ? SW_MAXIMIZE : SW_RESTORE);
    return focusWindow(targetId, error);
#else
    Q_UNUSED(targetId) Q_UNUSED(maximized)
    if (error) *error = QStringLiteral("Control de ventanas disponible sólo en Windows.");
    return false;
#endif
}

bool DesktopAutomationBackend::isNormalizedPoint(double x, double y)
{
    return std::isfinite(x) && std::isfinite(y)
        && x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0;
}

bool DesktopAutomationBackend::setWindowSize(const QString &targetId, int width, int height,
                                              QString *error)
{
#ifdef Q_OS_WIN
    bool parsed = false;
    const quintptr raw = targetId.toULongLong(&parsed, 16);
    HWND hwnd = parsed ? reinterpret_cast<HWND>(raw) : nullptr;
    if (!hwnd || !IsWindow(hwnd)) {
        if (error) *error = QStringLiteral("La ventana ya no está disponible.");
        return false;
    }
    if (width <= 0 || height <= 0) {
        if (error) *error = QStringLiteral("El tamaño de ventana enseñado no es válido.");
        return false;
    }
    if (IsZoomed(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        if (error) *error = QStringLiteral("No se pudo leer el tamaño actual de la ventana.");
        return false;
    }
    const int currentWidth = static_cast<int>(rect.right - rect.left);
    const int currentHeight = static_cast<int>(rect.bottom - rect.top);
    if (qAbs(currentWidth - width) > 2 || qAbs(currentHeight - height) > 2) {
        if (!SetWindowPos(hwnd, nullptr, 0, 0, width, height,
                          SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
            if (error) *error = QStringLiteral("Windows rechazó el tamaño de ventana solicitado.");
            return false;
        }
    }
    return focusWindow(targetId, error);
#else
    Q_UNUSED(targetId) Q_UNUSED(width) Q_UNUSED(height)
    if (error) *error = QStringLiteral("Control de ventanas disponible sólo en Windows.");
    return false;
#endif
}

QVariantMap DesktopAutomationBackend::waitFor(const QString &windowTargetId,
                                              const QString &windowTitle, const QString &query,
                                              const QString &role, int timeoutMs, QString *error)
{
#ifdef Q_OS_WIN
    const int budget = qBound(0, timeoutMs, 60000);
    const QString title = windowTitle.trimmed().toLower();
    const QString needle = query.trimmed().toLower();
    const QString wantRole = role.trimmed().toLower();
    QElapsedTimer clock;
    clock.start();
    do {
        // Resolver ventana: por id explícito o buscando por título.
        QString windowId = windowTargetId.trimmed();
        QString foundTitle;
        if (windowId.isEmpty() && !title.isEmpty()) {
            for (const QVariant &w : windows()) {
                const QVariantMap row = w.toMap();
                if (row.value(QStringLiteral("label")).toString().toLower().contains(title)) {
                    windowId = row.value(QStringLiteral("id")).toString();
                    foundTitle = row.value(QStringLiteral("label")).toString();
                    break;
                }
            }
        }
        if (!windowId.isEmpty()) {
            // Sólo esperábamos la ventana (sin query/role) → listo.
            if (needle.isEmpty() && wantRole.isEmpty()) {
                return QVariantMap{{QStringLiteral("found"), true},
                                   {QStringLiteral("elapsedMs"), static_cast<int>(clock.elapsed())},
                                   {QStringLiteral("windowId"), windowId},
                                   {QStringLiteral("title"), foundTitle}};
            }
            // Esperar un control dentro de la ventana.
            for (const QVariant &c : controls(windowId, query, 400, nullptr)) {
                const QVariantMap row = c.toMap();
                if (!wantRole.isEmpty()
                    && row.value(QStringLiteral("role")).toString().toLower() != wantRole)
                    continue;
                QVariantMap hit = row;
                hit[QStringLiteral("found")] = true;
                hit[QStringLiteral("elapsedMs")] = static_cast<int>(clock.elapsed());
                hit[QStringLiteral("windowId")] = windowId;
                return hit;
            }
        }
        if (clock.elapsed() >= budget) break;
        Sleep(200);
    } while (clock.elapsed() < budget);

    if (error) *error = QStringLiteral("Timeout: la condición no se cumplió en %1 ms.").arg(budget);
    return QVariantMap{{QStringLiteral("found"), false},
                       {QStringLiteral("elapsedMs"), static_cast<int>(clock.elapsed())}};
#else
    Q_UNUSED(windowTargetId) Q_UNUSED(windowTitle) Q_UNUSED(query)
    Q_UNUSED(role) Q_UNUSED(timeoutMs)
    if (error) *error = QStringLiteral("Disponible sólo en Windows.");
    return QVariantMap{{QStringLiteral("found"), false}};
#endif
}

QVariantMap DesktopAutomationBackend::assertCondition(const QString &windowTargetId,
                                                      const QString &windowTitle,
                                                      const QString &query, const QString &role,
                                                      const QString &expectText, int timeoutMs,
                                                      QString *error)
{
#ifdef Q_OS_WIN
    const QString want = expectText.trimmed();
    // Sin texto esperado → aserción de existencia: reusa waitFor.
    if (want.isEmpty()) {
        QString err;
        const QVariantMap r = waitFor(windowTargetId, windowTitle, query, role, timeoutMs, &err);
        const bool pass = r.value(QStringLiteral("found")).toBool();
        if (!pass && error) *error = err;
        return QVariantMap{{QStringLiteral("pass"), pass},
                           {QStringLiteral("elapsedMs"), r.value(QStringLiteral("elapsedMs"))},
                           {QStringLiteral("detail"), pass
                                ? QStringLiteral("Condición de existencia cumplida.")
                                : QStringLiteral("No apareció la ventana/control esperado.")}};
    }
    // Con texto esperado → escanear controles buscando el texto en el nombre.
    const int budget = qBound(0, timeoutMs, 60000);
    const QString needle = want.toLower();
    QElapsedTimer clock;
    clock.start();
    do {
        QStringList winIds;
        const QString explicitId = windowTargetId.trimmed();
        if (!explicitId.isEmpty()) {
            winIds << explicitId;
        } else if (!windowTitle.trimmed().isEmpty()) {
            const QString t = windowTitle.trimmed().toLower();
            for (const QVariant &w : windows())
                if (w.toMap().value(QStringLiteral("label")).toString().toLower().contains(t))
                    winIds << w.toMap().value(QStringLiteral("id")).toString();
        } else {
            for (const QVariant &w : windows())
                winIds << w.toMap().value(QStringLiteral("id")).toString();
        }
        for (const QString &wid : winIds) {
            for (const QVariant &c : controls(wid, want, 400, nullptr)) {
                if (c.toMap().value(QStringLiteral("name")).toString().toLower().contains(needle)) {
                    return QVariantMap{{QStringLiteral("pass"), true},
                                       {QStringLiteral("elapsedMs"), static_cast<int>(clock.elapsed())},
                                       {QStringLiteral("windowId"), wid},
                                       {QStringLiteral("detail"),
                                        QStringLiteral("Texto \"%1\" presente.").arg(want.left(80))}};
                }
            }
        }
        if (clock.elapsed() >= budget) break;
        Sleep(200);
    } while (clock.elapsed() < budget);

    if (error) *error = QStringLiteral("El texto \"%1\" no apareció en %2 ms.")
                            .arg(want.left(80)).arg(budget);
    return QVariantMap{{QStringLiteral("pass"), false},
                       {QStringLiteral("elapsedMs"), static_cast<int>(clock.elapsed())},
                       {QStringLiteral("detail"),
                        QStringLiteral("Texto \"%1\" NO encontrado.").arg(want.left(80))}};
#else
    Q_UNUSED(windowTargetId) Q_UNUSED(windowTitle) Q_UNUSED(query)
    Q_UNUSED(role) Q_UNUSED(expectText) Q_UNUSED(timeoutMs)
    if (error) *error = QStringLiteral("Disponible sólo en Windows.");
    return QVariantMap{{QStringLiteral("pass"), false}};
#endif
}
