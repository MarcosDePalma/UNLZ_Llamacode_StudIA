// Tests de ThemeProvider: temas built-in + CRUD/derivación de temas custom.
// QGuiApplication (no QCoreApplication): ThemeProvider toca fuentes globales.

#include <QtTest>
#include <QGuiApplication>
#include <QColor>
#include <QStandardPaths>
#include <QSettings>
#include "ThemeProvider.h"

class ThemeTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void builtinThemesSwitch();
    void customThemeRoundTrips();
    void applyCustomDerivesPalette();
    void deleteActiveFallsBack();
    void contrastChangesSurfaces();
    void normalizeHexAcceptsAndRejects();
    void saveSanitizesColorAnchors();
};

void ThemeTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("LlamaCode"));
    QCoreApplication::setApplicationName(QStringLiteral("LlamaCodeThemeTest"));
    QSettings().clear();   // estado limpio (customThemes / theme)
}

void ThemeTests::builtinThemesSwitch()
{
    ThemeProvider t;
    t.setTheme(QStringLiteral("dark"));
    QVERIFY(!t.themeIsCustom());
    QCOMPARE(t.baseBg(), QStringLiteral("#1e1e2e"));
    t.setTheme(QStringLiteral("light"));
    QCOMPARE(t.baseBg(), QStringLiteral("#eff1f5"));
    t.setTheme(QStringLiteral("oled"));
    QCOMPARE(t.baseBg(), QStringLiteral("#000000"));
}

void ThemeTests::customThemeRoundTrips()
{
    ThemeProvider t;
    const QVariantMap def{
        {QStringLiteral("name"), QStringLiteral("Vercel")},
        {QStringLiteral("base"), QStringLiteral("oled")},
        {QStringLiteral("accent"), QStringLiteral("#006efe")},
        {QStringLiteral("background"), QStringLiteral("#000000")},
        {QStringLiteral("foreground"), QStringLiteral("#ededed")},
        {QStringLiteral("contrast"), 29},
        {QStringLiteral("translucent"), true},
        {QStringLiteral("codeFont"), QStringLiteral("Geist Mono")},
    };
    const QString id = t.saveCustomTheme(def);
    QVERIFY(!id.isEmpty());

    // Persiste y se lee de vuelta por id.
    const QVariantMap got = t.customTheme(id);
    QCOMPARE(got.value(QStringLiteral("name")).toString(), QStringLiteral("Vercel"));
    QCOMPARE(got.value(QStringLiteral("accent")).toString(), QStringLiteral("#006efe"));
    QCOMPARE(t.customThemes().size(), 1);

    // Update (mismo id) no duplica.
    QVariantMap upd = got;
    upd.insert(QStringLiteral("name"), QStringLiteral("Vercel 2"));
    QCOMPARE(t.saveCustomTheme(upd), id);
    QCOMPARE(t.customThemes().size(), 1);
    QCOMPARE(t.customTheme(id).value(QStringLiteral("name")).toString(),
             QStringLiteral("Vercel 2"));

    // Persiste entre instancias.
    ThemeProvider t2;
    QCOMPARE(t2.customThemes().size(), 1);
}

void ThemeTests::applyCustomDerivesPalette()
{
    ThemeProvider t;
    QSettings().setValue(QStringLiteral("customThemes"), QString());  // reset
    const QString id = t.saveCustomTheme(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("X")},
        {QStringLiteral("base"), QStringLiteral("dark")},
        {QStringLiteral("accent"), QStringLiteral("#006efe")},
        {QStringLiteral("background"), QStringLiteral("#000000")},
        {QStringLiteral("foreground"), QStringLiteral("#ededed")},
        {QStringLiteral("contrast"), 30},
        {QStringLiteral("codeFont"), QStringLiteral("Geist Mono")},
    });
    t.applyCustomTheme(id);

    QVERIFY(t.themeIsCustom());
    QCOMPARE(t.currentCustomId(), id);
    // Anclas se respetan exactamente.
    QCOMPARE(t.accent().toLower(), QStringLiteral("#006efe"));
    QCOMPARE(t.baseBg().toLower(), QStringLiteral("#000000"));
    QCOMPARE(t.textPrimary().toLower(), QStringLiteral("#ededed"));
    // Texto sobre acento azul → blanco (luminancia baja).
    QCOMPARE(t.btnPrimaryText().toLower(), QStringLiteral("#ffffff"));
    // codeFont expuesta; estado (errorText) heredado del base dark.
    QCOMPARE(t.codeFont(), QStringLiteral("Geist Mono"));
    QCOMPARE(t.errorText(), QStringLiteral("#f38ba8"));
}

void ThemeTests::contrastChangesSurfaces()
{
    ThemeProvider t;
    QSettings().setValue(QStringLiteral("customThemes"), QString());
    auto surfaceAt = [&](int contrast) {
        const QString id = t.saveCustomTheme(QVariantMap{
            {QStringLiteral("name"), QStringLiteral("c")},
            {QStringLiteral("base"), QStringLiteral("dark")},
            {QStringLiteral("accent"), QStringLiteral("#006efe")},
            {QStringLiteral("background"), QStringLiteral("#000000")},
            {QStringLiteral("foreground"), QStringLiteral("#ffffff")},
            {QStringLiteral("contrast"), contrast},
        });
        t.applyCustomTheme(id);
        const QString s = t.surfaceBg();
        t.deleteCustomTheme(id);
        return QColor(s).lightness();
    };
    // Más contraste → superficie más separada del fondo negro (más clara).
    QVERIFY(surfaceAt(80) > surfaceAt(10));
}

void ThemeTests::deleteActiveFallsBack()
{
    ThemeProvider t;
    QSettings().setValue(QStringLiteral("customThemes"), QString());
    const QString id = t.saveCustomTheme(t.defaultCustomDef(QStringLiteral("dark")));
    t.applyCustomTheme(id);
    QVERIFY(t.themeIsCustom());
    t.deleteCustomTheme(id);
    // Borrar el activo → vuelve a un built-in y la lista queda vacía.
    QVERIFY(!t.themeIsCustom());
    QCOMPARE(t.customThemes().size(), 0);
}

// El editor de tema valida el hex con esto (largo fijo + shorthand + '#' opcional).
void ThemeTests::normalizeHexAcceptsAndRejects()
{
    ThemeProvider t;
    QCOMPARE(t.normalizeHex(QStringLiteral("#89B4FA")), QStringLiteral("#89b4fa"));
    QCOMPARE(t.normalizeHex(QStringLiteral("89b4fa")),  QStringLiteral("#89b4fa"));
    QCOMPARE(t.normalizeHex(QStringLiteral("  #f0a ")), QStringLiteral("#ff00aa"));  // shorthand
    QCOMPARE(t.normalizeHex(QStringLiteral("#8089b4fa")), QStringLiteral("#8089b4fa")); // AARRGGBB

    // Basura: el caso real era tipear de más en el campo hex sin límite.
    QCOMPARE(t.normalizeHex(QStringLiteral("#1e1e2eaaaaaaaaaaaaaaaaaa")), QString());
    QCOMPARE(t.normalizeHex(QStringLiteral("#1e1e2")), QString());   // 5 dígitos
    QCOMPARE(t.normalizeHex(QStringLiteral("#zzzzzz")), QString());  // no-hex
    QCOMPARE(t.normalizeHex(QString()), QString());
}

void ThemeTests::saveSanitizesColorAnchors()
{
    ThemeProvider t;
    QSettings().setValue(QStringLiteral("customThemes"), QString());
    const QString id = t.saveCustomTheme(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("Sucio")},
        {QStringLiteral("base"), QStringLiteral("dark")},
        {QStringLiteral("accent"), QStringLiteral("f0a")},                    // shorthand
        {QStringLiteral("background"), QStringLiteral("#1e1e2eaaaaaaaaaa")},  // basura
        {QStringLiteral("foreground"), QStringLiteral("#CDD6F4")},
    });
    const QVariantMap got = t.customTheme(id);
    QCOMPARE(got.value(QStringLiteral("accent")).toString(), QStringLiteral("#ff00aa"));
    // Ancla inválida → literal del tema base, nunca se persiste el texto roto.
    QCOMPARE(got.value(QStringLiteral("background")).toString(), QStringLiteral("#1e1e2e"));
    QCOMPARE(got.value(QStringLiteral("foreground")).toString(), QStringLiteral("#cdd6f4"));
    t.deleteCustomTheme(id);
}

QTEST_MAIN(ThemeTests)
#include "test_theme.moc"
