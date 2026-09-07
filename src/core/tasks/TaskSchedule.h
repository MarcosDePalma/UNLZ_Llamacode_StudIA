#pragma once
#include <QString>
#include <QVariantMap>
#include <QDateTime>

// TaskSchedule — evaluador PURO del "schedule amigable" de una Task. Modela las
// frecuencias que la UI ofrece sin obligar al usuario a escribir cron:
//
//   mode = "daily"        → todos los días a hour:minute
//          "weekly"       → en los weekdays elegidos (0=Dom..6=Sáb) a hour:minute
//          "monthly"      → monthlyKind = "date": el día `monthDay` del mes
//                           monthlyKind = "nthWeekday": el `nth` (1..5; 5=último)
//                           `nthWeekday` del mes (ej. primer lunes)
//          "everyNMonths" → cada `everyN` meses (a partir de `startMonth`, 1..12),
//                           el día `monthDay`, a hour:minute
//          "cron"         → delega en CronSchedule(`cron`) — modo avanzado
//
// Resolución de minuto, igual que CronSchedule. matches(spec, dt) decide contra un
// QDateTime dado → unit-testeable sin reloj real. El spec se persiste como
// QVariantMap (scheduleSpec) en el TaskStore.
class TaskSchedule
{
public:
    // ¿El spec dispara en ese instante? Spec inválido/vacío → false.
    static bool matches(const QVariantMap &spec, const QDateTime &dt);

    // ¿El spec tiene un `mode` reconocido y los campos mínimos? (no toca disco/reloj)
    static bool isValid(const QVariantMap &spec);

    // Resumen legible para la UI (ej. "Semanal · Lun, Mié · 09:00"). Vacío si inválido.
    static QString describe(const QVariantMap &spec);
};
