#pragma once

#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

// Telemetria estable del harness. Acepta respuestas llama.cpp y OpenAI-compatible
// incompletas: los campos desconocidos permanecen en cero en vez de inventarse.
class AgentEfficiency
{
public:
    struct Request {
        QString phase;
        int promptTokens = 0;
        int generatedTokens = 0;
        double promptMs = 0.0;
        double generatedMs = 0.0;
        double wallMs = 0.0;
        int toolCalls = 0;
        qint64 toolBytes = 0;
        int draftTokens = 0;
        int draftAcceptedTokens = 0;

        QVariantMap toVariant() const;
        static Request fromResponse(const QJsonObject &root, const QString &phase,
                                    double wallMs = 0.0);
    };

    static QVariantMap summarize(const QVariantList &requests);
    static QVariantMap compare(const QVariantMap &baseline, const QVariantMap &candidate);
    // Convierte eventos normalizados tool.request/tool.finish en llamadas
    // comparables. Los backends externos pueden entregar el mismo contrato sin
    // depender de su formato wire particular.
    static QVariantList toolCallsFromLifecycle(const QVariantList &events);
    // Evalua eficiencia/correccion de tools. `calls` acepta mapas con tool,
    // arguments, ok y completed; `expected` es opcional y puede declarar tool
    // y arguments exactos por llamada. Sin expectativas, la correccion queda
    // como desconocida (-1), pero se siguen midiendo fallos y redundancias.
    static QVariantMap evaluateToolCalls(const QVariantList &calls,
                                         const QVariantList &expected = {});
    // Agrupa pasadas de benchmark por perfil y calcula estadísticos robustos.
    // Las filas fallidas cuentan para estabilidad/éxito, pero no contaminan
    // medianas de tiempo o calidad con ceros sintéticos.
    // groupBy = "profileId" (default: compara modelos) | "agentProfileId"
    // (compara harness: mismo modelo, distinto HarnessSpec).
    static QVariantMap benchmarkComparison(const QVariantList &runs,
                                           const QString &groupBy = QStringLiteral("profileId"));
    static QString normalizedPhase(const QString &phase);
};
