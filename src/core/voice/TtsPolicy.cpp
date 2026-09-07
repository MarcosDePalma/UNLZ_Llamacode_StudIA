#include "TtsPolicy.h"

QVariantMap TtsPolicy::recommend(const VoiceConfig &cfg, double vramGb, double ramGb,
                                 double vramFreeGb, bool qwenReady, bool piperReady,
                                 bool pocketReady)
{
    QString mode = cfg.ttsMode;
    QString reason;
    QString model = cfg.qwenModelName;
    if (mode != QLatin1String("auto") || !cfg.ttsAutoConfigure) {
        reason = QStringLiteral("Selección manual del usuario");
    } else {
        const double usableVram = vramFreeGb > 0.0 ? vramFreeGb : vramGb;
        if (pocketReady && cfg.pocketAutoEnable
            && (!qwenReady || usableVram < 3.5 || ramGb < 12.0)) {
            mode = QStringLiteral("pocket");
            reason = QStringLiteral(
                "Pocket TTS local está instalado y fue habilitado para equipos con poca VRAM");
        } else if (qwenReady && usableVram >= 3.5 && ramGb >= 12.0) {
            mode = QStringLiteral("qwen3");
            if (usableVram >= 7.0 && ramGb >= 20.0) {
                model = QStringLiteral("qwen-talker-1.7b-base-Q8_0.gguf");
                reason = QStringLiteral("Qwen3 1.7B: memoria suficiente para priorizar calidad");
            } else {
                model = QStringLiteral("qwen-talker-0.6b-base-Q8_0.gguf");
                reason = QStringLiteral("Qwen3 0.6B: GPU disponible con margen moderado");
            }
        } else if (piperReady) {
            mode = QStringLiteral("piper");
            reason = qwenReady ? QStringLiteral("Piper preserva VRAM para el LLM")
                               : QStringLiteral("Piper local está disponible y Qwen3 no está configurado");
        } else {
            mode = QStringLiteral("http");
            reason = QStringLiteral("No hay un motor local listo; se conserva el endpoint HTTP");
        }
    }
    return {{QStringLiteral("mode"), mode}, {QStringLiteral("qwenModelName"), model},
            {QStringLiteral("reason"), reason}};
}
