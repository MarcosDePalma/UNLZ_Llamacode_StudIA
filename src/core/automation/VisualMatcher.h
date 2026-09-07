#pragma once

#include <QImage>
#include <QRect>
#include <QVariantMap>

// Matcher visual liviano y portable para superficies sin UI Automation/OCR.
// Trabaja en memoria, limita el costo mediante muestreo y nunca decide clicks:
// devuelve evidencia y deja la política de ambigüedad al backend de escritorio.
class VisualMatcher
{
public:
    struct Options {
        double threshold = 0.88;
        double minScale = 1.0;
        double maxScale = 1.0;
        bool requireUnique = true;
    };

    struct Result {
        bool found = false;
        bool ambiguous = false;
        QRect rect;
        double confidence = 0.0;
        double secondConfidence = 0.0;
        double scale = 1.0;
        QString backend = QStringLiteral("qt-sampled");
        QString error;

        QVariantMap toVariantMap(const QSize &haystackSize) const;
    };

    static Result find(const QImage &haystack, const QImage &needle,
                       const Options &options = {});
};
