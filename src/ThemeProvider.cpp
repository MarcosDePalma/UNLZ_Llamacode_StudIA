#include "ThemeProvider.h"
#include <QSettings>
#include <QColor>
#include <QGuiApplication>
#include <QFont>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUuid>

// ── Tabla de literales built-in ──────────────────────────────────────────────
// role → {dark, light, oled}. Transcripción 1:1 de la paleta original.
namespace {
struct Lit { const char *role; const char *dark; const char *light; const char *oled; };
const Lit kLits[] = {
    {"navBg",          "#181825","#dce0e8","#000000"},
    {"baseBg",         "#1e1e2e","#eff1f5","#000000"},
    {"surfaceBg",      "#181825","#e6e9ef","#0a0a0a"},
    {"inputBg",        "#11111b","#ccd0da","#050505"},
    {"titleBg",        "#11111b","#dce0e8","#000000"},
    {"windowBg",       "#0b0b10","#dce0e8","#000000"},
    {"logBg",          "#0f1020","#d0d4de","#000000"},
    {"popupBg",        "#1b1d31","#e6e9ef","#0a0a0a"},
    {"popupHeaderBg",  "#14162a","#dce0e8","#050505"},

    {"divider",            "#313244","#bcc0cc","#1a1a1a"},
    {"borderColor",        "#313244","#bcc0cc","#1a1a1a"},
    {"inputBorderColor",   "#313244","#bcc0cc","#1a1a1a"},
    {"inputBorderFocus",   "#89b4fa","#1e66f5","#89b4fa"},
    {"popupBorderColor",   "#3a3f5c","#bcc0cc","#1a1a1a"},
    {"popupHeaderBorder",  "#333754","#bcc0cc","#1a1a1a"},
    {"frameBorderActive",  "#3f4360","#7c7f93","#2a2a2a"},
    {"frameBorderInact",   "#2a2d41","#9ca0b0","#1a1a1a"},

    {"textPrimary",   "#cdd6f4","#4c4f69","#ffffff"},
    {"textSecondary", "#a6adc8","#5c5f77","#aaaaaa"},
    {"textMuted",     "#585b70","#9ca0b0","#555555"},
    {"textDim",       "#45475a","#acb0be","#333333"},
    {"dialogLabel",   "#bac2de","#5c5f77","#cccccc"},

    {"successText", "#a6e3a1","#40a02b","#44ff88"},
    {"successBg",   "#1a3a1a","#d0f0d0","#001a00"},
    {"errorText",   "#f38ba8","#d20f39","#ff4444"},
    {"errorBg",     "#3a1a1a","#fce2da","#200000"},
    {"errorBorder", "#f38ba8","#d20f39","#ff4444"},

    {"accent",        "#89b4fa","#1e66f5","#89b4fa"},
    {"accentHover",   "#74c7ec","#4285f4","#74c7ec"},
    {"accentPressed", "#5e81ac","#1a56d5","#5e81ac"},

    {"btnPrimaryBg",     "#89b4fa","#1e66f5","#89b4fa"},
    {"btnPrimaryHover",  "#74c7ec","#4285f4","#74c7ec"},
    {"btnPrimaryPrs",    "#5e81ac","#1a56d5","#5e81ac"},
    {"btnPrimaryText",   "#1e1e2e","#ffffff","#000000"},
    {"btnSecondaryBg",   "#313244","#ccd0da","#151515"},
    {"btnSecondaryHov",  "#45475a","#bcc0cc","#252525"},
    {"btnSecondaryText", "#a6adc8","#4c4f69","#aaaaaa"},
    {"btnDangerBg",      "#f38ba8","#d20f39","#ff4444"},
    {"btnDangerHov",     "#e33054","#b90020","#ee3333"},
    {"btnDangerPrs",     "#a6192f","#8b0018","#cc2222"},
    {"btnDangerText",    "#1e1e2e","#ffffff","#000000"},

    {"highlight", "#313244","#ccd0da","#151515"},
    {"hoverBg",   "#1e1e2e","#e6e9ef","#0d0d0d"},

    {"chatUserBubble", "#2a2d5e","#c0cfff","#0a0a35"},
    {"chatUserText",   "#b4c2ff","#1a3080","#b4c2ff"},
    {"chatAsstBubble", "#181825","#e6e9ef","#0a0a0a"},
    {"chatAsstText",   "#cdd6f4","#4c4f69","#ffffff"},

    {"splitterNormal", "#2a2d41","#9ca0b0","#161616"},
    {"splitterHover",  "#3b3f63","#7c7f93","#252525"},
    {"closeHoverBg",   "#f38ba8","#d20f39","#ff4444"},
    {"overlayColor",   "#90090b14","#60000000","#90000000"},
    {"warnText",       "#f9e2af","#df8e1d","#ffcc44"},
};

QColor mix(const QColor &a, const QColor &b, double t) {
    t = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(a.redF()   * (1 - t) + b.redF()   * t,
                            a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF()  * (1 - t) + b.blueF()  * t);
}
double luminance(const QColor &c) {
    return 0.299 * c.redF() + 0.587 * c.greenF() + 0.114 * c.blueF();
}
QString hex(const QColor &c) { return c.name(QColor::HexRgb); }
} // namespace

ThemeProvider::ThemeProvider(QObject *parent) : QObject(parent) {
    QSettings s;
    m_theme = s.value(QStringLiteral("theme"), QStringLiteral("dark")).toString();
    rebuild();
}

QString ThemeProvider::lit(const QString &role, const QString &theme) {
    const int idx = (theme == QLatin1String("light")) ? 1
                  : (theme == QLatin1String("oled"))  ? 2 : 0;
    for (const Lit &l : kLits) {
        if (role == QLatin1String(l.role))
            return QLatin1String(idx == 1 ? l.light : idx == 2 ? l.oled : l.dark);
    }
    return QStringLiteral("#ff00ff"); // rol desconocido → magenta visible
}

QString ThemeProvider::colorFor(const QString &role) const {
    if (!m_pal.isEmpty()) {
        auto it = m_pal.constFind(role);
        if (it != m_pal.constEnd()) return it.value();
        return lit(role, m_customBase);   // rol no derivado → hereda del base
    }
    return lit(role, m_theme);
}

QString ThemeProvider::currentCustomId() const {
    return themeIsCustom() ? m_theme.mid(7) : QString();
}

void ThemeProvider::setTheme(const QString &t) {
    if (m_theme == t) return;
    m_theme = t;
    QSettings s;
    s.setValue(QStringLiteral("theme"), t);
    rebuild();
    emit themeChanged();
}

void ThemeProvider::applyUiFontGlobally() {
    if (!qGuiApp) return;
    QFont f = qGuiApp->font();
    if (!m_uiFont.trimmed().isEmpty()) {
        // "Geist, Inter" → tomar la primera familia.
        const QString fam = m_uiFont.section(QLatin1Char(','), 0, 0).trimmed();
        if (!fam.isEmpty()) f.setFamily(fam);
    }
    qGuiApp->setFont(f);
}

// Reconstruye el estado del tema activo. Para built-in deja m_pal vacío (colorFor
// usa los literales). Para custom deriva ~50 roles de 3 anclas + contraste.
void ThemeProvider::rebuild() {
    m_pal.clear();
    m_customBase.clear();
    m_uiFont.clear();
    m_codeFont.clear();
    m_sidebarTranslucent = false;

    if (themeIsCustom()) {
        const QVariantMap def = customTheme(currentCustomId());
        if (!def.isEmpty()) {
            m_customBase = def.value(QStringLiteral("base"), QStringLiteral("dark")).toString();
            m_uiFont     = def.value(QStringLiteral("uiFont")).toString();
            m_codeFont   = def.value(QStringLiteral("codeFont")).toString();
            m_sidebarTranslucent = def.value(QStringLiteral("translucent"), false).toBool();

            const QColor bg = QColor(def.value(QStringLiteral("background"),
                                     lit(QStringLiteral("baseBg"), m_customBase)).toString());
            const QColor fg = QColor(def.value(QStringLiteral("foreground"),
                                     lit(QStringLiteral("textPrimary"), m_customBase)).toString());
            const QColor ac = QColor(def.value(QStringLiteral("accent"),
                                     lit(QStringLiteral("accent"), m_customBase)).toString());
            // Contraste 0..100 → factor de separación bg↔fg (más alto = superficies
            // y bordes más marcados respecto del fondo).
            const double contrast = qBound(0, def.value(QStringLiteral("contrast"), 30).toInt(), 100) / 100.0;
            const double cf = 0.6 + contrast * 1.2;       // ~0.6 .. 1.8

            auto elev = [&](double t) { return mix(bg, fg, qBound(0.0, t * cf, 1.0)); };
            const QColor acHover = mix(ac, QColor(Qt::white), 0.15);
            const QColor acPress = mix(ac, QColor(Qt::black), 0.20);
            const QColor onAccent = luminance(ac) > 0.55 ? QColor(Qt::black) : QColor(Qt::white);

            auto put = [&](const char *role, const QColor &c) {
                m_pal.insert(QLatin1String(role), hex(c));
            };

            // Backgrounds (elevaciones crecientes desde el fondo)
            put("baseBg",        bg);
            put("windowBg",      mix(bg, QColor(Qt::black), 0.25));
            put("navBg",         elev(0.05));
            put("surfaceBg",     elev(0.05));
            put("titleBg",       elev(0.05));
            put("logBg",         mix(bg, QColor(Qt::black), 0.12));
            put("inputBg",       elev(0.09));
            put("popupBg",       elev(0.06));
            put("popupHeaderBg", elev(0.08));
            put("hoverBg",       elev(0.07));
            put("highlight",     elev(0.13));

            // Borders / dividers
            put("divider",           elev(0.18));
            put("borderColor",       elev(0.18));
            put("inputBorderColor",  elev(0.18));
            put("popupBorderColor",  elev(0.22));
            put("popupHeaderBorder", elev(0.20));
            put("frameBorderActive", elev(0.30));
            put("frameBorderInact",  elev(0.16));
            put("splitterNormal",    elev(0.16));
            put("splitterHover",     elev(0.28));
            put("inputBorderFocus",  ac);

            // Text (foreground → fondo)
            put("textPrimary",   fg);
            put("dialogLabel",   mix(fg, bg, 0.12));
            put("textSecondary", mix(fg, bg, 0.25));
            put("textMuted",     mix(fg, bg, 0.50));
            put("textDim",       mix(fg, bg, 0.65));
            put("chatAsstText",  fg);
            put("chatAsstBubble", elev(0.05));

            // Accent + botones
            put("accent",          ac);
            put("accentHover",     acHover);
            put("accentPressed",   acPress);
            put("btnPrimaryBg",    ac);
            put("btnPrimaryHover", acHover);
            put("btnPrimaryPrs",   acPress);
            put("btnPrimaryText",  onAccent);
            put("btnSecondaryBg",   elev(0.13));
            put("btnSecondaryHov",  elev(0.20));
            put("btnSecondaryText", mix(fg, bg, 0.22));

            // Chat usuario (tinte del accent)
            put("chatUserBubble", mix(ac, bg, 0.72));
            put("chatUserText",   mix(ac, fg, 0.25));

            // Estado (error/success/warn), danger y overlay se HEREDAN del base
            // (colorFor cae a lit(role, m_customBase)).
        }
    }

    applyUiFontGlobally();
}

// ── CRUD de temas custom (persistidos como JSON en QSettings) ────────────────
QVariantList ThemeProvider::customThemes() const {
    QSettings s;
    const QJsonArray arr = QJsonDocument::fromJson(
        s.value(QStringLiteral("customThemes")).toString().toUtf8()).array();
    QVariantList out;
    for (const QJsonValue &v : arr) out.append(v.toObject().toVariantMap());
    return out;
}

QVariantMap ThemeProvider::customTheme(const QString &id) const {
    for (const QVariant &v : customThemes()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == id) return m;
    }
    return {};
}

QString ThemeProvider::normalizeHex(const QString &s) const {
    QString t = s.trimmed();
    if (t.startsWith(QLatin1Char('#'))) t.remove(0, 1);
    for (const QChar &c : t) {
        const QChar l = c.toLower();
        const bool hexDigit = (l >= QLatin1Char('0') && l <= QLatin1Char('9'))
                           || (l >= QLatin1Char('a') && l <= QLatin1Char('f'));
        if (!hexDigit) return QString();
    }
    if (t.size() == 3) {   // shorthand rgb → rrggbb
        QString e;
        for (const QChar &c : t) { e.append(c); e.append(c); }
        t = e;
    }
    if (t.size() != 6 && t.size() != 8) return QString();
    return QStringLiteral("#") + t.toLower();
}

QString ThemeProvider::saveCustomTheme(const QVariantMap &def) {
    QVariantMap m = def;
    QString id = m.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        m.insert(QStringLiteral("id"), id);
    }
    if (m.value(QStringLiteral("name")).toString().trimmed().isEmpty())
        m.insert(QStringLiteral("name"), QStringLiteral("Tema custom"));

    // Anclas de color: un hex tipeado a mano puede llegar basura ("#1e1e2eaaaa").
    // Sanear acá y no en la UI: QColor("basura") es inválido y derivaría toda la
    // paleta en negro sin avisar.
    const QString base = m.value(QStringLiteral("base"), QStringLiteral("dark")).toString();
    const struct { const char *key; const char *role; } kAnchors[] = {
        {"accent",     "accent"},
        {"background", "baseBg"},
        {"foreground", "textPrimary"},
    };
    for (const auto &a : kAnchors) {
        const QString key = QLatin1String(a.key);
        const QString norm = normalizeHex(m.value(key).toString());
        m.insert(key, norm.isEmpty() ? lit(QLatin1String(a.role), base) : norm);
    }

    QJsonArray arr;
    bool replaced = false;
    for (const QVariant &v : customThemes()) {
        QVariantMap existing = v.toMap();
        if (existing.value(QStringLiteral("id")).toString() == id) {
            arr.append(QJsonObject::fromVariantMap(m));
            replaced = true;
        } else {
            arr.append(QJsonObject::fromVariantMap(existing));
        }
    }
    if (!replaced) arr.append(QJsonObject::fromVariantMap(m));

    QSettings s;
    s.setValue(QStringLiteral("customThemes"),
               QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    emit customThemesChanged();

    // Si el tema editado es el activo, refrescar la paleta en vivo.
    if (themeIsCustom() && currentCustomId() == id) {
        rebuild();
        emit themeChanged();
    }
    return id;
}

void ThemeProvider::deleteCustomTheme(const QString &id) {
    QJsonArray arr;
    for (const QVariant &v : customThemes()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() != id)
            arr.append(QJsonObject::fromVariantMap(m));
    }
    QSettings s;
    s.setValue(QStringLiteral("customThemes"),
               QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    emit customThemesChanged();

    if (themeIsCustom() && currentCustomId() == id)
        setTheme(QStringLiteral("dark"));   // fallback si borrás el activo
}

QVariantMap ThemeProvider::defaultCustomDef(const QString &base) const {
    const QString b = (base == QLatin1String("light") || base == QLatin1String("oled"))
                          ? base : QStringLiteral("dark");
    return QVariantMap{
        {QStringLiteral("name"), QStringLiteral("")},
        {QStringLiteral("base"), b},
        {QStringLiteral("accent"),     lit(QStringLiteral("accent"), b)},
        {QStringLiteral("background"), lit(QStringLiteral("baseBg"), b)},
        {QStringLiteral("foreground"), lit(QStringLiteral("textPrimary"), b)},
        {QStringLiteral("contrast"), 30},
        {QStringLiteral("translucent"), false},
        {QStringLiteral("uiFont"), QStringLiteral("")},
        {QStringLiteral("codeFont"), QStringLiteral("")},
    };
}
