#include "VisualMatcher.h"

#include <QtMath>
#include <algorithm>
#include <limits>

#ifdef LC_HAVE_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace {
struct Candidate { QRect rect; double score = 0.0; double scale = 1.0; };

double sampledSimilarity(const QImage &haystack, const QImage &needle, int left, int top)
{
    // Hasta 20x20 muestras uniformes. El barrido adapta su stride al costo total,
    // por lo que ganamos estructura sin perder el límite de tiempo en 4K.
    const int sx = qMax(1, needle.width() / 20);
    const int sy = qMax(1, needle.height() / 20);
    qint64 difference = 0;
    int count = 0;
    for (int y = sy / 2; y < needle.height(); y += sy) {
        const uchar *n = needle.constScanLine(y);
        const uchar *h = haystack.constScanLine(top + y) + left;
        for (int x = sx / 2; x < needle.width(); x += sx) {
            difference += qAbs(int(h[x]) - int(n[x]));
            ++count;
        }
    }
    // El muestreo centrado puede caer íntegramente dentro de un botón de color
    // plano y omitir su borde. En ese caso una zona uniforme del escritorio daba
    // confianza 1.0 aunque no tuviera la estructura de la plantilla. Muestrear el
    // perímetro conserva el costo acotado y vuelve observable esa estructura.
    const auto add = [&](int x, int y) {
        difference += qAbs(int(haystack.constScanLine(top + y)[left + x])
                           - int(needle.constScanLine(y)[x]));
        ++count;
    };
    const int edgeStepX = qMax(1, needle.width() / 20);
    const int edgeStepY = qMax(1, needle.height() / 20);
    for (int x = 0; x < needle.width(); x += edgeStepX) {
        add(x, 0);
        if (needle.height() > 1) add(x, needle.height() - 1);
    }
    for (int y = edgeStepY; y + 1 < needle.height(); y += edgeStepY) {
        add(0, y);
        if (needle.width() > 1) add(needle.width() - 1, y);
    }
    return count > 0 ? 1.0 - double(difference) / (255.0 * count) : 0.0;
}

bool overlapsSameObject(const QRect &a, const QRect &b)
{
    const QRect overlap = a.intersected(b);
    // Cualquier solapamiento entre ventanas del mismo tamaño es el mismo objeto
    // visto con un offset vecino del barrido, no una alternativa accionable. La
    // política anterior (sólo >1/3 del área) marcaba como ambiguo un único botón.
    return !overlap.isEmpty() && overlap.width() > 0 && overlap.height() > 0;
}
}

QVariantMap VisualMatcher::Result::toVariantMap(const QSize &size) const
{
    QVariantMap out{{QStringLiteral("found"), found},
                    {QStringLiteral("ambiguous"), ambiguous},
                    {QStringLiteral("confidence"), confidence},
                    {QStringLiteral("secondConfidence"), secondConfidence},
                    {QStringLiteral("scale"), scale}};
    out[QStringLiteral("backend")] = backend;
    if (!error.isEmpty()) out[QStringLiteral("error")] = error;
    if (found && size.width() > 0 && size.height() > 0) {
        out[QStringLiteral("rect")] = QVariantMap{
            {QStringLiteral("x"), double(rect.x()) / size.width()},
            {QStringLiteral("y"), double(rect.y()) / size.height()},
            {QStringLiteral("width"), double(rect.width()) / size.width()},
            {QStringLiteral("height"), double(rect.height()) / size.height()}};
    }
    return out;
}

VisualMatcher::Result VisualMatcher::find(const QImage &haystackSource,
                                          const QImage &needleSource,
                                          const Options &rawOptions)
{
    Result result;
    if (haystackSource.isNull() || needleSource.isNull()) {
        result.error = QStringLiteral("La captura o la plantilla visual es inválida.");
        return result;
    }
    if (needleSource.width() < 3 || needleSource.height() < 3) {
        result.error = QStringLiteral("La plantilla visual debe medir al menos 3x3 píxeles.");
        return result;
    }

    const double threshold = qBound(0.5, rawOptions.threshold, 1.0);
    const double minScale = qBound(0.5, qMin(rawOptions.minScale, rawOptions.maxScale), 2.0);
    const double maxScale = qBound(minScale, qMax(rawOptions.minScale, rawOptions.maxScale), 2.0);
    const QImage haystack = haystackSource.convertToFormat(QImage::Format_Grayscale8);
    const QImage original = needleSource.convertToFormat(QImage::Format_Grayscale8);

    QList<double> scales{minScale};
    if (maxScale - minScale > 0.01) {
        for (double s = minScale + 0.10; s < maxScale - 0.01; s += 0.10) scales << s;
        scales << maxScale;
    }
    if (minScale < 1.0 && maxScale > 1.0) scales << 1.0;
    std::sort(scales.begin(), scales.end());
    scales.erase(std::unique(scales.begin(), scales.end(),
                             [](double a, double b) { return qAbs(a - b) < 0.01; }), scales.end());

#ifdef LC_HAVE_OPENCV
    Candidate cvBest;
    Candidate cvSecond;
    bool cvFits = false;
    const cv::Mat hay(haystack.height(), haystack.width(), CV_8UC1,
                      const_cast<uchar *>(haystack.constBits()), haystack.bytesPerLine());
    const cv::Mat base(original.height(), original.width(), CV_8UC1,
                       const_cast<uchar *>(original.constBits()), original.bytesPerLine());
    for (double scale : scales) {
        const QSize size(qMax(3, qRound(original.width() * scale)),
                         qMax(3, qRound(original.height() * scale)));
        if (size.width() > haystack.width() || size.height() > haystack.height()) continue;
        cvFits = true;
        cv::Mat needle;
        if (qAbs(scale - 1.0) < 0.001) needle = base;
        else cv::resize(base, needle, cv::Size(size.width(), size.height()), 0, 0, cv::INTER_AREA);
        cv::Mat scores;
        cv::matchTemplate(hay, needle, scores, cv::TM_CCOEFF_NORMED);
        double maxScore = 0.0;
        cv::Point maxPoint;
        cv::minMaxLoc(scores, nullptr, &maxScore, nullptr, &maxPoint);
        Candidate first{QRect(maxPoint.x, maxPoint.y, needle.cols, needle.rows), maxScore, scale};
        cv::Mat suppressed = scores.clone();
        const int left = qMax(0, maxPoint.x - needle.cols / 2);
        const int top = qMax(0, maxPoint.y - needle.rows / 2);
        const int right = qMin(suppressed.cols, maxPoint.x + needle.cols / 2 + 1);
        const int bottom = qMin(suppressed.rows, maxPoint.y + needle.rows / 2 + 1);
        suppressed(cv::Rect(left, top, right - left, bottom - top)).setTo(-1.0f);
        double secondScore = 0.0;
        cv::Point secondPoint;
        cv::minMaxLoc(suppressed, nullptr, &secondScore, nullptr, &secondPoint);
        Candidate alt{QRect(secondPoint.x, secondPoint.y, needle.cols, needle.rows), secondScore, scale};
        for (const Candidate &candidate : {first, alt}) {
            if (candidate.score > cvBest.score) { cvSecond = cvBest; cvBest = candidate; }
            else if (!overlapsSameObject(candidate.rect, cvBest.rect)
                     && candidate.score > cvSecond.score) cvSecond = candidate;
        }
    }
    if (!cvFits) {
        result.error = QStringLiteral("La plantilla es más grande que el alcance capturado.");
        return result;
    }
    result.backend = QStringLiteral("opencv-matchTemplate");
    result.confidence = cvBest.score;
    result.secondConfidence = cvSecond.score;
    result.scale = cvBest.scale;
    result.rect = cvBest.rect;
    result.found = cvBest.score >= threshold;
    result.ambiguous = result.found && rawOptions.requireUnique
        && cvSecond.score >= threshold && cvBest.score - cvSecond.score < 0.025;
    if (result.ambiguous)
        result.error = QStringLiteral("Hay más de una coincidencia visual igualmente probable.");
    else if (!result.found)
        result.error = QStringLiteral("No se encontró la plantilla con la confianza requerida.");
    return result;
#endif

    Candidate best;
    Candidate second;
    bool anyFits = false;
    for (double scale : scales) {
        const QSize scaledSize(qMax(3, qRound(original.width() * scale)),
                               qMax(3, qRound(original.height() * scale)));
        if (scaledSize.width() > haystack.width() || scaledSize.height() > haystack.height())
            continue;
        anyFits = true;
        const QImage needle = scale == 1.0
            ? original
            : original.scaled(scaledSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        const qint64 positions = qint64(haystack.width() - needle.width() + 1)
                               * qint64(haystack.height() - needle.height() + 1);
        const int stride = qMax(1, int(qCeil(qSqrt(double(positions) / 250000.0))));
        for (int y = 0; y <= haystack.height() - needle.height(); y += stride) {
            for (int x = 0; x <= haystack.width() - needle.width(); x += stride) {
                const Candidate candidate{QRect(x, y, needle.width(), needle.height()),
                                          sampledSimilarity(haystack, needle, x, y), scale};
                if (candidate.score > best.score) {
                    if (!overlapsSameObject(candidate.rect, best.rect)) second = best;
                    best = candidate;
                } else if (!overlapsSameObject(candidate.rect, best.rect)
                           && candidate.score > second.score) {
                    second = candidate;
                }
            }
        }
    }
    if (!anyFits) {
        result.error = QStringLiteral("La plantilla es más grande que el alcance capturado.");
        return result;
    }

    result.confidence = best.score;
    result.secondConfidence = second.score;
    result.scale = best.scale;
    result.rect = best.rect;
    result.found = best.score >= threshold;
    // Una alternativa casi igual vuelve inseguro el objetivo. El margen absoluto
    // evita que dos íconos repetidos terminen en un click elegido al azar.
    result.ambiguous = result.found && rawOptions.requireUnique
        && second.score >= threshold && best.score - second.score < 0.025;
    if (result.ambiguous)
        result.error = QStringLiteral("Hay más de una coincidencia visual igualmente probable.");
    else if (!result.found)
        result.error = QStringLiteral("No se encontró la plantilla con la confianza requerida.");
    return result;
}
