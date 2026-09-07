#include "DifficultyRouter.h"

DifficultyRouter::DifficultyRouter(const Thresholds &thresholds)
    : m_thresholds(thresholds)
{
}

DifficultyRouter::Level DifficultyRouter::assess(const QVariantMap &state) const
{
    return assessDetailed(state).level;
}

DifficultyRouter::Assessment DifficultyRouter::assessDetailed(const QVariantMap &state) const
{
    Assessment result;
    int mediumSignals = 0;

    int filesAffected = state.value(QStringLiteral("filesAffected"), 0).toInt();
    int contextTokens = state.value(QStringLiteral("contextTokens"), 0).toInt();
    int repeatedFailures = state.value(QStringLiteral("repeatedFailures"), 0).toInt();
    int agentCycles = state.value(QStringLiteral("agentCycles"), 0).toInt();
    double confidence = state.value(QStringLiteral("confidence"), 1.0).toDouble();

    // Archivos afectados
    if (filesAffected >= m_thresholds.filesAffected) {
        result.level = High;
        result.reasons.append(QStringLiteral("Muchos archivos afectados (%1)").arg(filesAffected));
    } else if (filesAffected >= m_thresholds.filesAffected / 2) {
        if (result.level != High) result.level = Medium;
        ++mediumSignals;
        result.reasons.append(QStringLiteral("Varios archivos afectados (%1)").arg(filesAffected));
    }

    // Contexto grande
    if (contextTokens >= m_thresholds.contextTokens) {
        result.level = High;
        result.reasons.append(QStringLiteral("Contexto muy grande (%1 tokens)").arg(contextTokens));
    } else if (contextTokens >= m_thresholds.contextTokens / 2) {
        if (result.level != High) result.level = Medium;
        ++mediumSignals;
        result.reasons.append(QStringLiteral("Contexto grande (%1 tokens)").arg(contextTokens));
    }

    // Fallos repetidos
    if (repeatedFailures >= m_thresholds.repeatedFailures) {
        result.level = High;
        result.reasons.append(QStringLiteral("Fallos repetidos (%1)").arg(repeatedFailures));
    } else if (repeatedFailures > 0) {
        if (result.level != High) result.level = Medium;
        ++mediumSignals;
        result.reasons.append(QStringLiteral("Algunos fallos (%1)").arg(repeatedFailures));
    }

    // Ciclos del agente
    if (agentCycles >= m_thresholds.agentCycles) {
        result.level = High;
        result.reasons.append(QStringLiteral("Ciclos sin progreso (%1)").arg(agentCycles));
    } else if (agentCycles >= m_thresholds.agentCycles / 2) {
        if (result.level != High) result.level = Medium;
        ++mediumSignals;
        result.reasons.append(QStringLiteral("Posible estancamiento (%1 ciclos)").arg(agentCycles));
    }

    // Confianza baja
    if (confidence < m_thresholds.confidenceFloor) {
        result.level = High;
        result.reasons.append(QStringLiteral("Confianza muy baja (%1)").arg(confidence));
    } else if (confidence < m_thresholds.confidenceFloor * 2) {
        if (result.level != High) result.level = Medium;
        ++mediumSignals;
        result.reasons.append(QStringLiteral("Confianza baja (%1)").arg(confidence));
    }

    if (result.level != High && mediumSignals >= 2) {
        result.level = High;
        result.reasons.append(QStringLiteral("Múltiples señales de dificultad (%1)")
                                  .arg(mediumSignals));
    }

    return result;
}

QString DifficultyRouter::levelName(Level level)
{
    switch (level) {
    case Low:    return QStringLiteral("baja");
    case Medium: return QStringLiteral("media");
    case High:   return QStringLiteral("alta");
    }
    return QStringLiteral("desconocida");
}
