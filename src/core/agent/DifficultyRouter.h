#pragma once

#include <QString>
#include <QVariantMap>

// Router automático por dificultad: evalúa señales de riesgo de una tarea
// en ejecución y decide si el modelo actual es suficiente o hay que escalar
// a uno más capaz (ej. MoE → dense).
//
// Señales evaluadas:
//   - archivos afectados (muchos archivos = complejidad alta)
//   - tamaño de contexto (contexto grande = necesita más capacidad)
//   - fallos repetidos de tools (tests fallando, mismas herramientas fallando)
//   - ciclos del agente (mismas tool calls repetidas)
//   - baja confianza en la salida
//
// Clase pura, testeable sin servidor ni agente.
class DifficultyRouter
{
public:
    // Nivel de dificultad detectado.
    enum Level {
        Low,       // El modelo actual basta
        Medium,    // Atención, pero sin escalar aún
        High       // Escalar a modelo más capaz
    };

    // Umbral configurable. Defaults conservadores.
    struct Thresholds {
        int filesAffected = 8;          // archivos afectados para considerar High
        int contextTokens = 24000;      // tokens de contexto para considerar High
        int repeatedFailures = 3;       // fallos consecutivos iguales para High
        int agentCycles = 5;           // iteraciones sin progreso para High
        double confidenceFloor = 0.3;  // confianza mínima antes de escalar
    };

    explicit DifficultyRouter(const Thresholds &thresholds = Thresholds());

    // Evalúa el estado actual del agente y devuelve el nivel de dificultad.
    // El QVariantMap debe contener:
    //   filesAffected: int (opcional, default 0)
    //   contextTokens: int (opcional, default 0)
    //   repeatedFailures: int (opcional, default 0)
    //   agentCycles: int (opcional, default 0)
    //   confidence: double (opcional, default 1.0)
    Level assess(const QVariantMap &state) const;

    // Versión detallada: devuelve nivel + razones.
    struct Assessment {
        Level level = Low;
        QStringList reasons;
        bool shouldEscalate() const { return level == High; }
    };
    Assessment assessDetailed(const QVariantMap &state) const;

    // Nombre legible del nivel.
    static QString levelName(Level level);

private:
    Thresholds m_thresholds;
};
