pragma Singleton
import QtQuick

// Stub del ThemeProvider (que en el app es una context property de C++ y no
// existe bajo el runtime `qml`). normalizeHex replica ThemeProvider::normalizeHex
// — si divergen, el que manda es el C++ (tests/test_theme.cpp lo fija).
QtObject {
    property string accent: "#89b4fa"
    property string baseBg: "#1e1e2e"
    property color surfaceBg: "#181825"
    property color highlight: "#313244"
    property string divider: "#313244"
    property string errorBorder: "#f38ba8"
    property string inputBg: "#11111b"
    property string inputBorderColor: "#313244"
    property string inputBorderFocus: "#89b4fa"
    property string popupBg: "#1b1d31"
    property string popupBorderColor: "#3a3f5c"
    property string popupHeaderBg: "#14162a"
    property string popupHeaderBorder: "#333754"
    property string textPrimary: "#cdd6f4"
    property string textSecondary: "#a6adc8"
    property string textMuted: "#585b70"
    property string overlayColor: "#90090b14"
    // Las usa LcHarnessEditor. En el app salen de ThemeProvider (borderColor /
    // warnText); si faltan aca, QML asigna undefined a un QColor y el test rojea
    // aunque todos los asserts pasen.
    property string borderColor: "#313244"
    property string warnText: "#f9e2af"

    function normalizeHex(s) {
        var t = String(s).trim()
        if (t.charAt(0) === "#") t = t.substring(1)
        if (!/^[0-9a-fA-F]*$/.test(t)) return ""
        if (t.length === 3) t = t[0] + t[0] + t[1] + t[1] + t[2] + t[2]
        if (t.length !== 6 && t.length !== 8) return ""
        return "#" + t.toLowerCase()
    }
}
