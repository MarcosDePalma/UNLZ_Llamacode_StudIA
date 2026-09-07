#include "ToolCallingSupport.h"
#include <QJsonObject>
#include <QRegularExpression>

QString ToolCallingSupport::normalizeKey(const QString &rawName)
{
    QString s = rawName.section(QLatin1Char('/'), -1).toLower();
    // Sacar extensión .gguf si vino un filename.
    s.remove(QRegularExpression(QStringLiteral("\\.gguf$"), QRegularExpression::CaseInsensitiveOption));
    const auto strip = [&s](const QString &pat) {
        s.remove(QRegularExpression(pat, QRegularExpression::CaseInsensitiveOption));
    };
    // GGUF k-quant tiers (q4_k_m, q5_k, q8_0, iq4_xs…)
    strip(QStringLiteral("[-_.]?(q[0-9](_k(_[a-z])?|_[0-9])?|iq[0-9][a-z0-9]*)\\b"));
    // Prequantized / float formats, with optional bit-width
    strip(QStringLiteral("[-_.]?(awq|gptq|gguf|mlx|exl2|bnb|nvfp4|mxfp4|fp4|fp8|fp16|bf16|f16|f32|int4|int8|w4a16|w8a8|w8a16|nf4)([-_]?[0-9]{1,2}(bit)?)?\\b"));
    // Bare bit-width tags (4bit, 8bit)
    strip(QStringLiteral("[-_.]?[0-9]{1,2}bit\\b"));
    // Date-stamp tokens (2507, 2501, 2512…)
    strip(QStringLiteral("[-_.]?2[0-9]{3}\\b"));
    // Role / variant tags
    strip(QStringLiteral("[-_](instruct|it|base|chat|thinking|reasoning|captioner|preview|distill|hf|mtp)\\b"));
    // Collapse separators
    s.replace(QRegularExpression(QStringLiteral("[-_/]")), QStringLiteral(" "));
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return s.trimmed();
}

ToolCallingSupport::Support
ToolCallingSupport::fromCookbook(const QString &modelName, const QJsonArray &cookbook)
{
    const QString key = normalizeKey(modelName);
    if (key.isEmpty()) return Support::Unknown;

    auto hasToolUse = [](const QJsonArray &caps) {
        for (const QJsonValue &c : caps) {
            const QString s = c.toString().toLower();
            if (s == QLatin1String("tool_use") || s == QLatin1String("function_calling"))
                return true;
        }
        return false;
    };

    bool matchedAny = false;
    bool matchedHasTools = false;
    for (const QJsonValue &v : cookbook) {
        const QJsonObject m = v.toObject();
        const QString cand = normalizeKey(m.value(QStringLiteral("name")).toString());
        if (cand.isEmpty()) continue;
        // Match: clave igual, o una contiene a la otra (cubre variantes de tamaño/repo).
        const bool exact = (cand == key);
        const bool contains = exact || cand.contains(key) || key.contains(cand);
        if (!contains) continue;
        matchedAny = true;
        if (hasToolUse(m.value(QStringLiteral("capabilities")).toArray())) {
            matchedHasTools = true;
            if (exact) return Support::Supported;   // match exacto con tools: definitivo
        }
    }
    if (!matchedAny) return Support::Unknown;
    return matchedHasTools ? Support::Supported : Support::Unsupported;
}

bool ToolCallingSupport::templateMentionsTools(const QString &chatTemplateJinja)
{
    if (chatTemplateJinja.isEmpty()) return false;
    const QString t = chatTemplateJinja.toLower();
    if (t.contains(QStringLiteral("tool_calls")) || t.contains(QStringLiteral("tool_call")))
        return true;
    if (t.contains(QStringLiteral("\"tools\"")) || t.contains(QStringLiteral("'tools'")))
        return true;
    // Bloque de tools jinja típico: itera `tools` y serializa con tojson.
    return t.contains(QStringLiteral("tools")) && t.contains(QStringLiteral("tojson"));
}

ToolCallingSupport::Support
ToolCallingSupport::combine(Support cookbook, bool haveTemplate, bool templateTools)
{
    // El template realmente cargado en el GGUF es la señal más directa.
    if (haveTemplate && templateTools) return Support::Supported;
    if (cookbook == Support::Supported) return Support::Supported;
    if (cookbook == Support::Unsupported) return Support::Unsupported;
    // Cookbook desconocido: si el template respondió y NO menciona tools → Unsupported.
    if (haveTemplate && !templateTools) return Support::Unsupported;
    return Support::Unknown;
}

QString ToolCallingSupport::toString(Support s)
{
    switch (s) {
    case Support::Supported:   return QStringLiteral("supported");
    case Support::Unsupported: return QStringLiteral("unsupported");
    default:                   return QStringLiteral("unknown");
    }
}
