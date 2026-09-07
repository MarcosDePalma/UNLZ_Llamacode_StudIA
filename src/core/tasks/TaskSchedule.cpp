#include "TaskSchedule.h"
#include "CronSchedule.h"
#include <QDate>
#include <QTime>
#include <QStringList>

namespace {

// Convención del spec: 0=Domingo .. 6=Sábado (igual que cron y JS getDay()).
// Qt: QDate::dayOfWeek() devuelve 1=Lunes .. 7=Domingo.
int dowSun0(const QDate &d)
{
    const int qt = d.dayOfWeek();   // 1..7 (Lun..Dom)
    return qt % 7;                  // Dom(7)→0, Lun(1)→1, ... Sáb(6)→6
}

bool hourMinuteMatches(const QVariantMap &spec, const QTime &t)
{
    const int h = spec.value(QStringLiteral("hour"), 0).toInt();
    const int m = spec.value(QStringLiteral("minute"), 0).toInt();
    return t.hour() == h && t.minute() == m;
}

QList<int> intList(const QVariant &v)
{
    QList<int> out;
    for (const QVariant &e : v.toList()) out.append(e.toInt());
    return out;
}

} // namespace

bool TaskSchedule::isValid(const QVariantMap &spec)
{
    const QString mode = spec.value(QStringLiteral("mode")).toString();
    if (mode == QLatin1String("cron"))
        return CronSchedule::parse(spec.value(QStringLiteral("cron")).toString()).isValid();
    if (mode == QLatin1String("daily"))
        return true;
    if (mode == QLatin1String("weekly"))
        return !intList(spec.value(QStringLiteral("weekdays"))).isEmpty();
    if (mode == QLatin1String("monthly")) {
        const QString k = spec.value(QStringLiteral("monthlyKind"),
                                     QStringLiteral("date")).toString();
        if (k == QLatin1String("nthWeekday")) {
            const int nth = spec.value(QStringLiteral("nth"), 0).toInt();
            return nth >= 1 && nth <= 5;
        }
        const int md = spec.value(QStringLiteral("monthDay"), 0).toInt();
        return md >= 1 && md <= 31;
    }
    if (mode == QLatin1String("everyNMonths"))
        return spec.value(QStringLiteral("everyN"), 0).toInt() >= 1;
    return false;
}

bool TaskSchedule::matches(const QVariantMap &spec, const QDateTime &dt)
{
    if (!isValid(spec)) return false;
    const QString mode = spec.value(QStringLiteral("mode")).toString();
    const QDate date = dt.date();
    const QTime time = dt.time();

    if (mode == QLatin1String("cron")) {
        const CronSchedule cs = CronSchedule::parse(spec.value(QStringLiteral("cron")).toString());
        return cs.isValid() && cs.matches(dt);
    }

    if (mode == QLatin1String("daily"))
        return hourMinuteMatches(spec, time);

    if (mode == QLatin1String("weekly")) {
        if (!hourMinuteMatches(spec, time)) return false;
        return intList(spec.value(QStringLiteral("weekdays"))).contains(dowSun0(date));
    }

    if (mode == QLatin1String("monthly")) {
        if (!hourMinuteMatches(spec, time)) return false;
        const QString k = spec.value(QStringLiteral("monthlyKind"),
                                     QStringLiteral("date")).toString();
        if (k == QLatin1String("nthWeekday")) {
            const int nth = spec.value(QStringLiteral("nth"), 1).toInt();      // 1..5 (5=último)
            const int wd  = spec.value(QStringLiteral("nthWeekday"), 1).toInt(); // 0=Dom..6=Sáb
            if (dowSun0(date) != wd) return false;
            if (nth == 5)   // último: no hay otra ocurrencia 7 días después en el mes
                return date.addDays(7).month() != date.month();
            const int occurrence = (date.day() - 1) / 7 + 1;  // 1..5
            return occurrence == nth;
        }
        int md = spec.value(QStringLiteral("monthDay"), 1).toInt();
        // Clamp al último día si el mes es más corto (ej. día 31 en febrero).
        md = qMin(md, date.daysInMonth());
        return date.day() == md;
    }

    if (mode == QLatin1String("everyNMonths")) {
        if (!hourMinuteMatches(spec, time)) return false;
        const int everyN = qMax(1, spec.value(QStringLiteral("everyN"), 1).toInt());
        const int start  = spec.value(QStringLiteral("startMonth"), 1).toInt(); // 1..12
        if (((date.month() - start) % everyN + everyN) % everyN != 0) return false;
        int md = spec.value(QStringLiteral("monthDay"), 1).toInt();
        md = qMin(md, date.daysInMonth());
        return date.day() == md;
    }

    return false;
}

QString TaskSchedule::describe(const QVariantMap &spec)
{
    if (!isValid(spec)) return {};
    const QString mode = spec.value(QStringLiteral("mode")).toString();

    auto hhmm = [&]() {
        const int h = spec.value(QStringLiteral("hour"), 0).toInt();
        const int m = spec.value(QStringLiteral("minute"), 0).toInt();
        return QStringLiteral("%1:%2").arg(h, 2, 10, QLatin1Char('0'))
                                      .arg(m, 2, 10, QLatin1Char('0'));
    };
    static const char *dn[] = {"Dom","Lun","Mar","Mié","Jue","Vie","Sáb"};

    if (mode == QLatin1String("cron"))
        return QStringLiteral("Cron · %1").arg(spec.value(QStringLiteral("cron")).toString());
    if (mode == QLatin1String("daily"))
        return QStringLiteral("Diario · %1").arg(hhmm());
    if (mode == QLatin1String("weekly")) {
        QStringList days;
        for (int d : intList(spec.value(QStringLiteral("weekdays"))))
            if (d >= 0 && d <= 6) days << QString::fromLatin1(dn[d]);
        return QStringLiteral("Semanal · %1 · %2").arg(days.join(QStringLiteral(", ")), hhmm());
    }
    if (mode == QLatin1String("monthly")) {
        const QString k = spec.value(QStringLiteral("monthlyKind"),
                                     QStringLiteral("date")).toString();
        if (k == QLatin1String("nthWeekday")) {
            static const char *ord[] = {"", "primer", "segundo", "tercer", "cuarto", "último"};
            const int nth = qBound(1, spec.value(QStringLiteral("nth"), 1).toInt(), 5);
            const int wd  = qBound(0, spec.value(QStringLiteral("nthWeekday"), 1).toInt(), 6);
            return QStringLiteral("Mensual · %1 %2 · %3")
                .arg(QString::fromLatin1(ord[nth]), QString::fromLatin1(dn[wd]), hhmm());
        }
        return QStringLiteral("Mensual · día %1 · %2")
            .arg(spec.value(QStringLiteral("monthDay"), 1).toInt()).arg(hhmm());
    }
    if (mode == QLatin1String("everyNMonths")) {
        const int n = qMax(1, spec.value(QStringLiteral("everyN"), 1).toInt());
        return QStringLiteral("Cada %1 mes(es) · día %2 · %3")
            .arg(n).arg(spec.value(QStringLiteral("monthDay"), 1).toInt()).arg(hhmm());
    }
    return {};
}
