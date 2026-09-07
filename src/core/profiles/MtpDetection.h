#pragma once

#include <QFileInfo>
#include <QRegularExpression>
#include <QString>

namespace MtpDetection {

// Heurística conservadora para GGUFs que transportan un speculator (MTP/DSpark)
// junto al modelo principal. La mayoría de publishers marca MTP en el filename.
// BottleCapAI publica ThinkingCap con el cabezal integrado pero conserva nombres
// de quant estándar (p. ej. ThinkingCap-Qwen3.6-27B-Q4_K_M.gguf).
// DeepSeek-V4-Flash-0731 es el checkpoint oficial con cabezal DSpark integrado.
inline bool isSelfContained(const QString &fileName)
{
    const QString base = QFileInfo(fileName).completeBaseName();
    static const QRegularExpression marker(
        QStringLiteral(R"((^|[-_.])mtp($|[-_.]))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression thinkingCapQwen36(
        QStringLiteral(R"((^|[-_.])thinkingcap[-_.]qwen3[._-]?6($|[-_.]))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression deepSeekV4Flash0731(
        QStringLiteral(R"((^|[-_.])deepseek[-_.]?v4[-_.]flash[-_.]0731($|[-_.]))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bigBangV1(
        QStringLiteral("(^|[-_.])bigbang[-_.]?v1($|[-_.])"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression qwen38(
        QStringLiteral(R"((^|[-_.])qwen3[._-]?8[-_.]27b($|[-_.]))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression katCoderApexMtp(
        QStringLiteral(R"((^|[-_.])kat[-_.]?coder[-_.]?v2[._-]?5[-_.]?dev[-_.]?mtp[-_.]?apex($|[-_.]))"),
        QRegularExpression::CaseInsensitiveOption);
    return marker.match(base).hasMatch()
        || thinkingCapQwen36.match(base).hasMatch()
        || deepSeekV4Flash0731.match(base).hasMatch()
        || bigBangV1.match(base).hasMatch()
        || qwen38.match(base).hasMatch()
        || katCoderApexMtp.match(base).hasMatch();
}

} // namespace MtpDetection
