#include "VoiceAgentPolicy.h"
#include <QRegularExpression>

QVariantMap VoiceAgentPolicy::assess(const QString &label, bool tools, bool supervisor)
{
    const QString s = label.toLower();
    double denseB = 0.0;
    double totalB = 0.0;
    QRegularExpression re(QStringLiteral("(?:^|[^0-9.])(\\d+(?:\\.\\d+)?)\\s*b(?:[^a-z]|$)"));
    auto it = re.globalMatch(s);
    while (it.hasNext()) totalB = qMax(totalB, it.next().captured(1).toDouble());
    denseB = totalB;
    const QRegularExpression moeRe(QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*b[-_ ]*a(\\d+(?:\\.\\d+)?)\\s*b"));
    const auto mm = moeRe.match(s);
    const bool moe = mm.hasMatch();
    if (moe) { totalB = mm.captured(1).toDouble(); denseB = mm.captured(2).toDouble(); }

    QString level, reason;
    bool trusted = true;
    bool requireSupervisor = false;
    if (!tools) {
        level = QStringLiteral("chat");
        reason = QStringLiteral("Conversación sin herramientas: se admite un modelo compacto");
    } else if (totalB <= 0.0) {
        level = QStringLiteral("desconocido"); trusted = false; requireSupervisor = supervisor;
        reason = QStringLiteral("No se pudo inferir el tamaño; validar con benchmark de herramientas");
    } else if (!moe && totalB < 4.0) {
        level = QStringLiteral("comandos_acotados"); trusted = false; requireSupervisor = supervisor;
        reason = QStringLiteral("Menos de 4B: alto riesgo de tool incorrecta, deriva semántica y loops");
    } else if (!moe && totalB < 7.0) {
        level = QStringLiteral("agente_basico"); trusted = false; requireSupervisor = supervisor;
        reason = QStringLiteral("4B–7B: útil para tareas acotadas, pero no confiable para autonomía compleja");
    } else {
        level = totalB >= 12.0 || moe ? QStringLiteral("agente_avanzado") : QStringLiteral("agente");
        reason = moe
            ? QStringLiteral("MoE con %1B totales/%2B activos: candidato fuerte; medir costo de expertos en RAM").arg(totalB).arg(denseB)
            : QStringLiteral("Modelo de %1B: piso conservador para orquestación de herramientas").arg(totalB);
    }
    return {{QStringLiteral("level"), level}, {QStringLiteral("trustedForTools"), trusted},
            {QStringLiteral("requireSupervisor"), requireSupervisor}, {QStringLiteral("reason"), reason},
            {QStringLiteral("paramsB"), totalB}, {QStringLiteral("activeParamsB"), denseB},
            {QStringLiteral("moe"), moe}};
}
