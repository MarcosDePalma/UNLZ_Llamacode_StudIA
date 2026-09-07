#include "LogTriage.h"
#include <QRegularExpression>
#include <QHash>

bool LogTriage::isErrorLine(const QString &line)
{
    static const QRegularExpression rx(
        QStringLiteral("error|fatal|crash|panic|exception|segfault|sigsegv|"
                       "abort|assert|traceback|\\[stderr\\]|/stderr\\]|"
                       "failed|cudamalloc|out of memory|oom|access violation"),
        QRegularExpression::CaseInsensitiveOption);
    return rx.match(line).hasMatch();
}

QString LogTriage::normalizeSignature(const QString &line)
{
    QString s = line;
    // Timestamps tipo [2026-06-19 10:11:12.345] o 2026/06/19, horas HH:MM:SS.
    s.remove(QRegularExpression(QStringLiteral(
        "\\d{4}[-/]\\d{2}[-/]\\d{2}[ T]?\\d{2}:\\d{2}:\\d{2}(\\.\\d+)?")));
    s.remove(QRegularExpression(QStringLiteral("\\d{2}:\\d{2}:\\d{2}(\\.\\d+)?")));
    // Direcciones hex / punteros.
    s.replace(QRegularExpression(QStringLiteral("0x[0-9a-fA-F]+")), QStringLiteral("0xADDR"));
    // GUIDs.
    s.replace(QRegularExpression(QStringLiteral(
        "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}")),
        QStringLiteral("GUID"));
    // Rutas Windows/Unix → placeholder (quedan firmas estables entre máquinas).
    s.replace(QRegularExpression(QStringLiteral("[A-Za-z]:\\\\[^\\s\"']+")), QStringLiteral("PATH"));
    s.replace(QRegularExpression(QStringLiteral("/[^\\s\"']+/[^\\s\"']+")), QStringLiteral("PATH"));
    // Números sueltos → N (líneas/offsets/tamaños variables).
    s.replace(QRegularExpression(QStringLiteral("\\b\\d+\\b")), QStringLiteral("N"));
    // Espacios colapsados.
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return s.trimmed();
}

QList<LogTriage::Group> LogTriage::group(const QString &log)
{
    QList<Group> groups;
    QHash<QString, int> indexBySig;   // firma → posición en `groups`
    const QStringList lines = log.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || !isErrorLine(line)) continue;
        const QString sig = normalizeSignature(line);
        if (sig.isEmpty()) continue;
        auto it = indexBySig.find(sig);
        if (it == indexBySig.end()) {
            indexBySig.insert(sig, groups.size());
            groups.append(Group{ sig, 1, line });
        } else {
            groups[it.value()].count++;
        }
    }
    // Orden estable por count desc; std::stable_sort preserva primera aparición.
    std::stable_sort(groups.begin(), groups.end(),
                     [](const Group &a, const Group &b) { return a.count > b.count; });
    return groups;
}

QString LogTriage::summarize(const QString &log, int maxGroups)
{
    const QList<Group> groups = group(log);
    if (groups.isEmpty()) return {};
    const int n = qMax(1, maxGroups);
    QStringList out;
    for (int i = 0; i < groups.size() && i < n; ++i)
        out << QStringLiteral("%1x  %2").arg(groups.at(i).count).arg(groups.at(i).sample);
    if (groups.size() > n)
        out << QStringLiteral("… y %1 firma(s) de error más.").arg(groups.size() - n);
    return out.join(QLatin1Char('\n'));
}

QString LogTriage::performanceWarnings(const QString &log)
{
    QStringList warnings;
    const auto addOnce = [&warnings](const QString &warning) {
        if (!warnings.contains(warning)) warnings << warning;
    };

    if (log.contains(QStringLiteral("SPLIT_MODE_TENSOR"), Qt::CaseInsensitive) &&
        (log.contains(QStringLiteral("using CPU sampler"), Qt::CaseInsensitive) ||
         log.contains(QStringLiteral("sampling not supported"), Qt::CaseInsensitive))) {
        addOnce(QStringLiteral(
            "Rendimiento: split-mode tensor está usando sampler CPU; compará --split-mode layer para recuperar prefill GPU."));
    }
    if (QRegularExpression(QStringLiteral(
            "layer\\s+[^\\n]*assigned to device CPU|assigned to device CPU[^\\n]*layer"),
            QRegularExpression::CaseInsensitiveOption).match(log).hasMatch()) {
        addOnce(QStringLiteral(
            "Rendimiento: al menos una capa cayó en CPU; bajá ctx/batch/ubatch o revisá la distribución GPU antes de medir tok/s."));
    }
    if (log.contains(QStringLiteral("usually due to missing support"), Qt::CaseInsensitive)) {
        addOnce(QStringLiteral(
            "Rendimiento: hay un operador sin soporte en el backend GPU; verificá el log de asignación porque puede degradar todo el prefill."));
    }
    return warnings.join(QLatin1Char('\n'));
}
