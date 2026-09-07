#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QVariantList>

// Recomendador de ajustes de llama-server para Ingi Charla (voz real-time).
// Analiza los args EFECTIVOS del perfil activo y propone solo lo que mejora la
// latencia de voz en ESE perfil (dinámico, no una lista fija):
//   - ubatch/batch chicos → prefill lento (el asesino: 53k tokens a 230 tok/s).
//   - ctx gigante → KV/checkpoints pesados y VRAM desperdiciada para charla.
//   - sin --cache-reuse → cada turno re-procesa el prefijo entero.
//   - --predict alto/infinito → respuestas de voz no necesitan 4k tokens.
// VRAM-aware: en equipos chicos no sube batches (costo de compute buffers).
// Todo puro y testeable; AppController lo aplica al relanzar el server.
namespace CharlaTuning {

struct Change {
    QString flag;         // ej "--ubatch-size"
    QString current;      // valor actual ("" = ausente)
    QString recommended;  // valor propuesto
    QString reason;       // explicación corta para el popup
};

// Valor de un flag en los args ("" si no está).
QString argValue(const QStringList &args, const QString &flag);

// Cambios recomendados para voz. vramGb <= 0 = desconocida (conservador:
// no sube batches, solo ctx/predict/cache-reuse).
QList<Change> recommend(const QStringList &args, double vramGb);

// Aplica los cambios sobre args: reemplaza el valor si el flag existe, o
// agrega el par al final si falta. No toca nada más.
QStringList apply(QStringList args, const QList<Change> &changes);

// Para QML: [{flag, current, recommended, reason}].
QVariantList toVariantList(const QList<Change> &changes);

} // namespace CharlaTuning
