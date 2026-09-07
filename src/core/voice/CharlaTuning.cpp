#include "CharlaTuning.h"
#include <QVariantMap>

namespace CharlaTuning {

QString argValue(const QStringList &args, const QString &flag)
{
    const int i = args.indexOf(flag);
    return (i >= 0 && i + 1 < args.size()) ? args.at(i + 1) : QString();
}

QList<Change> recommend(const QStringList &args, double vramGb)
{
    QList<Change> out;

    // ── Prefill: batches ── El default de llama.cpp ya es 2048/512; solo actuar
    // si el perfil los BAJÓ explícitamente. Subirlos cuesta VRAM de compute
    // buffers, así que se escala por VRAM disponible.
    int ubatchRec = 0, batchRec = 0;
    if (vramGb >= 8.0)      { ubatchRec = 512; batchRec = 2048; }
    else if (vramGb >= 4.0) { ubatchRec = 256; batchRec = 1024; }
    const QString ubatch = argValue(args, QStringLiteral("--ubatch-size"));
    if (ubatchRec > 0 && !ubatch.isEmpty() && ubatch.toInt() < ubatchRec)
        out.append({QStringLiteral("--ubatch-size"), ubatch, QString::number(ubatchRec),
                    QStringLiteral("prefill %1x más rápido (procesa el prompt en bloques más grandes)")
                        .arg(qMax(2, ubatchRec / qMax(1, ubatch.toInt())))});
    const QString batch = argValue(args, QStringLiteral("--batch-size"));
    if (batchRec > 0 && !batch.isEmpty() && batch.toInt() < batchRec)
        out.append({QStringLiteral("--batch-size"), batch, QString::number(batchRec),
                    QStringLiteral("acompaña al ubatch en prompts largos")});

    // ── Contexto ── Charla no necesita 262k: el KV gigante come VRAM y hace
    // checkpoints/gestión de cache pesados. 32k sobra para una conversación
    // hablada (el historial del agente además se poda de imágenes viejas).
    const QString ctx = argValue(args, QStringLiteral("--ctx-size"));
    if (!ctx.isEmpty() && ctx.toInt() > 65536)
        out.append({QStringLiteral("--ctx-size"), ctx, QStringLiteral("32768"),
                    QStringLiteral("KV cache liviano; la charla no usa contexto gigante")});

    // ── Reuso de prefijo ── Sin esto, un cambio a mitad del historial re-procesa
    // todo lo que sigue; con reuse el server salva los bloques comunes.
    const QString reuse = argValue(args, QStringLiteral("--cache-reuse"));
    if (reuse.isEmpty())
        out.append({QStringLiteral("--cache-reuse"), QString(), QStringLiteral("512"),
                    QStringLiteral("reusa bloques del prompt ya procesados entre turnos")});

    // ── Tope de generación ── Respuestas habladas son cortas; un --predict alto
    // (o infinito) deja al modelo divagar minutos si se va por las ramas.
    const QString predict = argValue(args, QStringLiteral("--predict"));
    if (predict.isEmpty() || predict.toInt() > 2048 || predict.toInt() < 0)
        out.append({QStringLiteral("--predict"), predict, QStringLiteral("1024"),
                    QStringLiteral("acota la respuesta (voz = respuestas cortas)")});

    return out;
}

QStringList apply(QStringList args, const QList<Change> &changes)
{
    for (const Change &c : changes) {
        const int i = args.indexOf(c.flag);
        if (i >= 0 && i + 1 < args.size()) args[i + 1] = c.recommended;
        else args << c.flag << c.recommended;
    }
    return args;
}

QVariantList toVariantList(const QList<Change> &changes)
{
    QVariantList out;
    for (const Change &c : changes)
        out.append(QVariantMap{{QStringLiteral("flag"), c.flag},
                               {QStringLiteral("current"), c.current},
                               {QStringLiteral("recommended"), c.recommended},
                               {QStringLiteral("reason"), c.reason}});
    return out;
}

} // namespace CharlaTuning
