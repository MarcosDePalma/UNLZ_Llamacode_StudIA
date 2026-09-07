#include <QtTest>
#include <QPainter>
#include <QElapsedTimer>

#include "core/automation/VisualMatcher.h"

class VisualMatcherTests : public QObject
{
    Q_OBJECT
private slots:
    void findsUniqueTemplateAndNormalizesRect();
    void rejectsMissingTemplate();
    void rejectsAmbiguousRepeatedTemplate();
    void findsScaledTemplate();
    void rejectsUniformRegionWhenTemplateHasBorder();
    void largeDesktopSearchStaysBounded();
};

static QImage patternedNeedle(int size = 12)
{
    QImage image(size, size, QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if ((x < size / 3) || (y > size / 2 && x > size / 2))
                image.setPixelColor(x, y, QColor(20 + x * 3, 30 + y * 4, 80));
    return image;
}

void VisualMatcherTests::findsUniqueTemplateAndNormalizesRect()
{
    QImage screen(80, 60, QImage::Format_RGB32);
    screen.fill(QColor(220, 220, 220));
    const QImage needle = patternedNeedle();
    QPainter(&screen).drawImage(31, 19, needle);

    const auto result = VisualMatcher::find(screen, needle);
    QVERIFY2(result.found, qPrintable(result.error));
    QVERIFY(!result.ambiguous);
    QCOMPARE(result.rect, QRect(31, 19, 12, 12));
    QVERIFY(result.confidence > 0.99);
    QVERIFY(result.backend == QLatin1String("qt-sampled")
            || result.backend == QLatin1String("opencv-matchTemplate"));
    const QVariantMap rect = result.toVariantMap(screen.size()).value("rect").toMap();
    QVERIFY(qAbs(rect.value("x").toDouble() - 31.0 / 80.0) < 0.001);
}

void VisualMatcherTests::largeDesktopSearchStaysBounded()
{
    QImage screen(2560, 1440, QImage::Format_RGB32);
    screen.fill(QColor(210, 210, 210));
    const QImage needle = patternedNeedle(48);
    QPainter(&screen).drawImage(2190, 1200, needle);
    QElapsedTimer timer;
    timer.start();
    const auto result = VisualMatcher::find(screen, needle);
    QVERIFY2(result.found, qPrintable(result.error));
    // Es una guarda amplia de regresión, no un benchmark: evita volver por error
    // a un barrido completo por píxel×template en escritorios grandes.
    QVERIFY2(timer.elapsed() < 8000, qPrintable(QString::number(timer.elapsed())));
}

void VisualMatcherTests::rejectsMissingTemplate()
{
    QImage screen(50, 50, QImage::Format_RGB32);
    screen.fill(Qt::black);
    const auto result = VisualMatcher::find(screen, patternedNeedle());
    QVERIFY(!result.found);
    QVERIFY(!result.error.isEmpty());
}

void VisualMatcherTests::rejectsUniformRegionWhenTemplateHasBorder()
{
    QImage screen(120, 90, QImage::Format_RGB32);
    screen.fill(QColor(235, 235, 235));
    QImage needle(48, 30, QImage::Format_RGB32);
    needle.fill(QColor(235, 235, 235));
    QPainter painter(&needle);
    painter.setPen(QPen(QColor(40, 80, 170), 4));
    painter.drawRect(1, 1, 45, 27);
    painter.end();
    const auto result = VisualMatcher::find(screen, needle);
    QVERIFY(!result.found);
}

void VisualMatcherTests::rejectsAmbiguousRepeatedTemplate()
{
    QImage screen(80, 50, QImage::Format_RGB32);
    screen.fill(Qt::white);
    const QImage needle = patternedNeedle();
    QPainter painter(&screen);
    painter.drawImage(8, 10, needle);
    painter.drawImage(52, 10, needle);
    painter.end();

    const auto result = VisualMatcher::find(screen, needle);
    QVERIFY(result.found);
    QVERIFY(result.ambiguous);
}

void VisualMatcherTests::findsScaledTemplate()
{
    QImage screen(100, 80, QImage::Format_RGB32);
    screen.fill(Qt::white);
    const QImage needle = patternedNeedle(20);
    const QImage scaled = needle.scaled(24, 24, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPainter(&screen).drawImage(40, 25, scaled);
    VisualMatcher::Options options;
    options.threshold = 0.96;
    options.minScale = 1.0;
    options.maxScale = 1.2;
    const auto result = VisualMatcher::find(screen, needle, options);
    QVERIFY2(result.found, qPrintable(result.error));
    QVERIFY(qAbs(result.scale - 1.2) < 0.01);
}

QTEST_MAIN(VisualMatcherTests)
#include "test_visual_matcher.moc"
