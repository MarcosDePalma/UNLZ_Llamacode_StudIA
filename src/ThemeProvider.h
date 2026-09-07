#pragma once
#include <QObject>
#include <QString>
#include <QHash>
#include <QVariantList>
#include <QVariantMap>

// ThemeProvider — paleta de colores + fuentes para la UI QML.
//
// Temas built-in: "dark", "light", "oled" (literales hardcodeados, vía lit()).
// Temas custom: "custom:<id>". El usuario los crea/edita/borra desde Settings;
// se persisten como JSON en QSettings ("customThemes"). Un tema custom se define
// con 3 colores ancla (accent / background / foreground) + contraste + fuentes
// + sidebar translúcida; el resto de la paleta (~50 roles) se DERIVA de esos
// anclas en buildPalette() mezclando bg↔fg según el rol y el contraste. Los
// roles de estado (error/success/warn) y el overlay se heredan del tema base.
class ThemeProvider : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool themeIsCustom READ themeIsCustom NOTIFY themeChanged)
    Q_PROPERTY(QString currentCustomId READ currentCustomId NOTIFY themeChanged)
    Q_PROPERTY(QVariantList customThemes READ customThemes NOTIFY customThemesChanged)

    // Tipografía (parte del tema). uiFont se aplica global vía QGuiApplication;
    // codeFont lo consumen los bloques monospace del QML.
    Q_PROPERTY(QString uiFont   READ uiFont   NOTIFY themeChanged)
    Q_PROPERTY(QString codeFont READ codeFont NOTIFY themeChanged)
    Q_PROPERTY(bool sidebarTranslucent READ sidebarTranslucent NOTIFY themeChanged)

    // Backgrounds
    Q_PROPERTY(QString navBg          READ navBg          NOTIFY themeChanged)
    Q_PROPERTY(QString baseBg         READ baseBg         NOTIFY themeChanged)
    Q_PROPERTY(QString surfaceBg      READ surfaceBg      NOTIFY themeChanged)
    Q_PROPERTY(QString inputBg        READ inputBg        NOTIFY themeChanged)
    Q_PROPERTY(QString titleBg        READ titleBg        NOTIFY themeChanged)
    Q_PROPERTY(QString windowBg       READ windowBg       NOTIFY themeChanged)
    Q_PROPERTY(QString logBg          READ logBg          NOTIFY themeChanged)
    Q_PROPERTY(QString popupBg        READ popupBg        NOTIFY themeChanged)
    Q_PROPERTY(QString popupHeaderBg  READ popupHeaderBg  NOTIFY themeChanged)

    // Borders / dividers
    Q_PROPERTY(QString divider            READ divider            NOTIFY themeChanged)
    Q_PROPERTY(QString borderColor        READ borderColor        NOTIFY themeChanged)
    Q_PROPERTY(QString inputBorderColor   READ inputBorderColor   NOTIFY themeChanged)
    Q_PROPERTY(QString inputBorderFocus   READ inputBorderFocus   NOTIFY themeChanged)
    Q_PROPERTY(QString popupBorderColor   READ popupBorderColor   NOTIFY themeChanged)
    Q_PROPERTY(QString popupHeaderBorder  READ popupHeaderBorder  NOTIFY themeChanged)
    Q_PROPERTY(QString frameBorderActive  READ frameBorderActive  NOTIFY themeChanged)
    Q_PROPERTY(QString frameBorderInact   READ frameBorderInact   NOTIFY themeChanged)

    // Text
    Q_PROPERTY(QString textPrimary    READ textPrimary    NOTIFY themeChanged)
    Q_PROPERTY(QString textSecondary  READ textSecondary  NOTIFY themeChanged)
    Q_PROPERTY(QString textMuted      READ textMuted      NOTIFY themeChanged)
    Q_PROPERTY(QString textDim        READ textDim        NOTIFY themeChanged)
    Q_PROPERTY(QString dialogLabel    READ dialogLabel    NOTIFY themeChanged)

    // Status
    Q_PROPERTY(QString successText  READ successText  NOTIFY themeChanged)
    Q_PROPERTY(QString successBg    READ successBg    NOTIFY themeChanged)
    Q_PROPERTY(QString errorText    READ errorText    NOTIFY themeChanged)
    Q_PROPERTY(QString errorBg      READ errorBg      NOTIFY themeChanged)
    Q_PROPERTY(QString errorBorder  READ errorBorder  NOTIFY themeChanged)

    // Accent
    Q_PROPERTY(QString accent         READ accent         NOTIFY themeChanged)
    Q_PROPERTY(QString accentHover    READ accentHover    NOTIFY themeChanged)
    Q_PROPERTY(QString accentPressed  READ accentPressed  NOTIFY themeChanged)

    // Buttons
    Q_PROPERTY(QString btnPrimaryBg      READ btnPrimaryBg      NOTIFY themeChanged)
    Q_PROPERTY(QString btnPrimaryHover   READ btnPrimaryHover   NOTIFY themeChanged)
    Q_PROPERTY(QString btnPrimaryPrs     READ btnPrimaryPrs     NOTIFY themeChanged)
    Q_PROPERTY(QString btnPrimaryText    READ btnPrimaryText    NOTIFY themeChanged)
    Q_PROPERTY(QString btnSecondaryBg    READ btnSecondaryBg    NOTIFY themeChanged)
    Q_PROPERTY(QString btnSecondaryHov   READ btnSecondaryHov   NOTIFY themeChanged)
    Q_PROPERTY(QString btnSecondaryText  READ btnSecondaryText  NOTIFY themeChanged)
    Q_PROPERTY(QString btnDangerBg       READ btnDangerBg       NOTIFY themeChanged)
    Q_PROPERTY(QString btnDangerHov      READ btnDangerHov      NOTIFY themeChanged)
    Q_PROPERTY(QString btnDangerPrs      READ btnDangerPrs      NOTIFY themeChanged)
    Q_PROPERTY(QString btnDangerText     READ btnDangerText     NOTIFY themeChanged)

    // Item states
    Q_PROPERTY(QString highlight  READ highlight  NOTIFY themeChanged)
    Q_PROPERTY(QString hoverBg    READ hoverBg    NOTIFY themeChanged)

    // Chat
    Q_PROPERTY(QString chatUserBubble  READ chatUserBubble  NOTIFY themeChanged)
    Q_PROPERTY(QString chatUserText    READ chatUserText    NOTIFY themeChanged)
    Q_PROPERTY(QString chatAsstBubble  READ chatAsstBubble  NOTIFY themeChanged)
    Q_PROPERTY(QString chatAsstText    READ chatAsstText    NOTIFY themeChanged)

    // Misc
    Q_PROPERTY(QString splitterNormal  READ splitterNormal  NOTIFY themeChanged)
    Q_PROPERTY(QString splitterHover   READ splitterHover   NOTIFY themeChanged)
    Q_PROPERTY(QString closeHoverBg    READ closeHoverBg    NOTIFY themeChanged)
    Q_PROPERTY(QString overlayColor    READ overlayColor    NOTIFY themeChanged)
    Q_PROPERTY(QString warnText        READ warnText        NOTIFY themeChanged)

public:
    explicit ThemeProvider(QObject *parent = nullptr);

    QString theme() const { return m_theme; }
    void setTheme(const QString &t);

    bool themeIsCustom() const { return m_theme.startsWith(QLatin1String("custom:")); }
    QString currentCustomId() const;

    QString uiFont()   const { return m_uiFont; }
    QString codeFont() const { return m_codeFont.isEmpty() ? QStringLiteral("Consolas,monospace") : m_codeFont; }
    bool sidebarTranslucent() const { return m_sidebarTranslucent; }

    // CRUD de temas custom (llamados desde QML).
    QVariantList customThemes() const;                       // [{id,name,base,accent,...}]
    Q_INVOKABLE QVariantMap customTheme(const QString &id) const;
    Q_INVOKABLE QString saveCustomTheme(const QVariantMap &def); // crea/actualiza → id
    Q_INVOKABLE void deleteCustomTheme(const QString &id);
    Q_INVOKABLE void applyCustomTheme(const QString &id) { setTheme(QStringLiteral("custom:") + id); }
    // Plantilla de arranque (anclas del tema base) para el editor "Nuevo".
    Q_INVOKABLE QVariantMap defaultCustomDef(const QString &base) const;
    // Normaliza un hex escrito a mano ("f0a", "#F0A", "aarrggbb", con/sin '#')
    // → "#rrggbb" / "#aarrggbb" en minúsculas. "" si no es un color válido.
    // Lo usa el editor de tema (QML) para validar y saveCustomTheme() para
    // sanear las 3 anclas antes de persistirlas.
    Q_INVOKABLE QString normalizeHex(const QString &s) const;

    QString navBg()          const { return colorFor(QStringLiteral("navBg")); }
    QString baseBg()         const { return colorFor(QStringLiteral("baseBg")); }
    QString surfaceBg()      const { return colorFor(QStringLiteral("surfaceBg")); }
    QString inputBg()        const { return colorFor(QStringLiteral("inputBg")); }
    QString titleBg()        const { return colorFor(QStringLiteral("titleBg")); }
    QString windowBg()       const { return colorFor(QStringLiteral("windowBg")); }
    QString logBg()          const { return colorFor(QStringLiteral("logBg")); }
    QString popupBg()        const { return colorFor(QStringLiteral("popupBg")); }
    QString popupHeaderBg()  const { return colorFor(QStringLiteral("popupHeaderBg")); }

    QString divider()           const { return colorFor(QStringLiteral("divider")); }
    QString borderColor()       const { return colorFor(QStringLiteral("borderColor")); }
    QString inputBorderColor()  const { return colorFor(QStringLiteral("inputBorderColor")); }
    QString inputBorderFocus()  const { return colorFor(QStringLiteral("inputBorderFocus")); }
    QString popupBorderColor()  const { return colorFor(QStringLiteral("popupBorderColor")); }
    QString popupHeaderBorder() const { return colorFor(QStringLiteral("popupHeaderBorder")); }
    QString frameBorderActive() const { return colorFor(QStringLiteral("frameBorderActive")); }
    QString frameBorderInact()  const { return colorFor(QStringLiteral("frameBorderInact")); }

    QString textPrimary()   const { return colorFor(QStringLiteral("textPrimary")); }
    QString textSecondary() const { return colorFor(QStringLiteral("textSecondary")); }
    QString textMuted()     const { return colorFor(QStringLiteral("textMuted")); }
    QString textDim()       const { return colorFor(QStringLiteral("textDim")); }
    QString dialogLabel()   const { return colorFor(QStringLiteral("dialogLabel")); }

    QString successText() const { return colorFor(QStringLiteral("successText")); }
    QString successBg()   const { return colorFor(QStringLiteral("successBg")); }
    QString errorText()   const { return colorFor(QStringLiteral("errorText")); }
    QString errorBg()     const { return colorFor(QStringLiteral("errorBg")); }
    QString errorBorder() const { return colorFor(QStringLiteral("errorBorder")); }

    QString accent()        const { return colorFor(QStringLiteral("accent")); }
    QString accentHover()   const { return colorFor(QStringLiteral("accentHover")); }
    QString accentPressed() const { return colorFor(QStringLiteral("accentPressed")); }

    QString btnPrimaryBg()     const { return colorFor(QStringLiteral("btnPrimaryBg")); }
    QString btnPrimaryHover()  const { return colorFor(QStringLiteral("btnPrimaryHover")); }
    QString btnPrimaryPrs()    const { return colorFor(QStringLiteral("btnPrimaryPrs")); }
    QString btnPrimaryText()   const { return colorFor(QStringLiteral("btnPrimaryText")); }
    QString btnSecondaryBg()   const { return colorFor(QStringLiteral("btnSecondaryBg")); }
    QString btnSecondaryHov()  const { return colorFor(QStringLiteral("btnSecondaryHov")); }
    QString btnSecondaryText() const { return colorFor(QStringLiteral("btnSecondaryText")); }
    QString btnDangerBg()      const { return colorFor(QStringLiteral("btnDangerBg")); }
    QString btnDangerHov()     const { return colorFor(QStringLiteral("btnDangerHov")); }
    QString btnDangerPrs()     const { return colorFor(QStringLiteral("btnDangerPrs")); }
    QString btnDangerText()    const { return colorFor(QStringLiteral("btnDangerText")); }

    QString highlight() const { return colorFor(QStringLiteral("highlight")); }
    QString hoverBg()   const { return colorFor(QStringLiteral("hoverBg")); }

    QString chatUserBubble() const { return colorFor(QStringLiteral("chatUserBubble")); }
    QString chatUserText()   const { return colorFor(QStringLiteral("chatUserText")); }
    QString chatAsstBubble() const { return colorFor(QStringLiteral("chatAsstBubble")); }
    QString chatAsstText()   const { return colorFor(QStringLiteral("chatAsstText")); }

    QString splitterNormal() const { return colorFor(QStringLiteral("splitterNormal")); }
    QString splitterHover()  const { return colorFor(QStringLiteral("splitterHover")); }
    QString closeHoverBg()   const { return colorFor(QStringLiteral("closeHoverBg")); }
    QString overlayColor()   const { return colorFor(QStringLiteral("overlayColor")); }
    QString warnText()       const { return colorFor(QStringLiteral("warnText")); }

signals:
    void themeChanged();
    void customThemesChanged();

private:
    QString m_theme;

    // Estado del tema custom activo (vacío si built-in).
    QHash<QString, QString> m_pal;   // roles derivados → color (vacío si built-in)
    QString m_customBase;            // tema base para roles heredados (status/overlay)
    QString m_uiFont;
    QString m_codeFont;
    bool    m_sidebarTranslucent = false;

    // Devuelve el color de `role`: si hay tema custom activo y el rol fue derivado,
    // usa la paleta custom; si no, el literal del tema base correspondiente.
    QString colorFor(const QString &role) const;

    // Literal hardcodeado de `role` para un tema built-in ("dark"/"light"/"oled").
    static QString lit(const QString &role, const QString &theme);

    // Recompone m_pal/m_customBase/fuentes según m_theme. Aplica la uiFont global.
    void rebuild();
    void applyUiFontGlobally();
};
