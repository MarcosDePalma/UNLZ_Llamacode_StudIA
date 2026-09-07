#pragma once
#include <QString>
#include <QStringList>
#include <QList>

// LogTriage — agrupa líneas de error de un log ruidoso en firmas distintas con
// su conteo, para que un "barrido de errores" (Proceso en bucle) le pase al
// agente un resumen corto en vez de miles de líneas repetidas.
//
// Todo PURO y estático → unit-testeable sin disco ni red. Ver [[loops-feature]]:
// un Proceso con loopGoal "el log no tiene errores nuevos" usa este resumen.
class LogTriage
{
public:
    struct Group {
        QString signature;   // firma normalizada (sin timestamps/números/punteros)
        int     count = 0;   // cuántas líneas colapsaron en esta firma
        QString sample;      // primera línea original (con contexto legible)
    };

    // ¿La línea parece un error/crash accionable? (error|fatal|crash|panic|
    // exception|segfault|abort|assert|[stderr], etc.)
    static bool isErrorLine(const QString &line);

    // Normaliza una línea a una firma estable: saca timestamps, direcciones hex,
    // números, rutas y GUIDs para que repeticiones del mismo error colapsen.
    static QString normalizeSignature(const QString &line);

    // Agrupa las líneas de error de `log` por firma, ordenadas por count desc
    // (estable: a igual count, primera aparición primero).
    static QList<Group> group(const QString &log);

    // Resumen humano de las top-N firmas: "Nx  <sample>" por línea. Vacío si no
    // hay errores. `maxGroups` acota el ruido (default 10).
    static QString summarize(const QString &log, int maxGroups = 10);

    // Detecta degradaciones conocidas de llama.cpp que pueden dejar el prefill
    // en CPU sin que el proceso termine. Devuelve un diagnóstico accionable.
    static QString performanceWarnings(const QString &log);
};
