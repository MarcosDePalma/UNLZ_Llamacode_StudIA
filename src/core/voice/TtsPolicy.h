#pragma once
#include "VoiceTypes.h"
#include <QVariantMap>

namespace TtsPolicy {
// Selección pura y explicable. qwenReady implica binario + directorio/modelos.
QVariantMap recommend(const VoiceConfig &cfg, double vramGb, double ramGb,
                      double vramFreeGb, bool qwenReady, bool piperReady,
                      bool pocketReady = false);
}
