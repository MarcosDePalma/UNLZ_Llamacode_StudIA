#pragma once
#include <QVariantMap>

namespace VoiceAgentPolicy {
// Clasifica la confiabilidad agentic desde el nombre/ruta del GGUF. Es una guarda
// conservadora; los benchmarks locales pueden elevar o bajar el resultado después.
QVariantMap assess(const QString &modelLabel, bool toolsEnabled, bool supervisorAvailable);
}
