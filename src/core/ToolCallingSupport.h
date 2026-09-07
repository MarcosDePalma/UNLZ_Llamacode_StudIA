#pragma once
#include <QString>
#include <QJsonArray>

// ToolCallingSupport — inferencia PURA de si el modelo de un perfil soporta
// tool-calling (function calling). Dos señales, ambas derivadas de datos, no de un
// whitelist manual de modelos:
//   1. Cookbook (assets/hwfit/hf_models.json): capabilities[] con "tool_use" /
//      "function_calling".
//   2. chat-template del GGUF (vía /props de llama-server): jinja que referencia
//      `tools` / `tool_calls` → la plantilla realmente sabe emitir tool calls.
//
// Sin disco ni red: recibe el cookbook ya parseado y/o el string del template →
// unit-testeable de forma determinista.
class ToolCallingSupport
{
public:
    enum class Support { Supported, Unsupported, Unknown };

    // Normaliza un nombre/filename de modelo a una clave estable para matchear
    // (minúsculas, sin quant/formato/fecha/tags de rol, separadores colapsados).
    // Es el mismo normalizador que usa el matching de benchmarks.
    static QString normalizeKey(const QString &rawName);

    // Busca la mejor coincidencia de `modelName` en el cookbook (array de objetos
    // estilo hf_models.json con {name, capabilities[]}). Supported si la enticia
    // matcheada declara tool_use/function_calling; Unsupported si matchea pero no lo
    // declara; Unknown si no hay match.
    static Support fromCookbook(const QString &modelName, const QJsonArray &cookbook);

    // ¿El chat-template jinja referencia tools? (tool_calls / tool_call / "tools").
    static bool templateMentionsTools(const QString &chatTemplateJinja);

    // Combina cookbook + señal de template. El template positivo confirma soporte; un
    // cookbook positivo también basta. Solo se concluye Unsupported cuando el cookbook
    // dice Unsupported y el template (si lo hay) tampoco menciona tools.
    static Support combine(Support cookbook, bool haveTemplate, bool templateTools);

    static QString toString(Support s);   // "supported" | "unsupported" | "unknown"

    // ¿Arrancar directo en el protocolo TEXTUAL de tools? Sólo cuando el soporte
    // nativo está descartado. Con "unknown" se intenta nativo: si el server lo
    // rechaza (400) el backend cae a texto solo. Al revés no hay vuelta atrás —
    // el turno entero se gasta en el protocolo lento aunque el modelo sirviera
    // tool-calling nativo (pasaba en cada swap de modelo: el reset de la señal
    // de template dejaba "unknown" hasta que respondía el /props del server nuevo).
    static bool shouldForceTextTools(const QString &support)
    { return support == QLatin1String("unsupported"); }
};
