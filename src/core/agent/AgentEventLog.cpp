#include "AgentEventLog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStringList>
#include <QUuid>

namespace {

QString baseDir(const QString &cwd)
{
    const QString root = cwd.trimmed().isEmpty() ? QDir::homePath() : cwd;
    const QString dir = QDir(root).filePath(QStringLiteral(".llamacode"));
    QDir().mkpath(dir);
    return dir;
}

}  // namespace

namespace AgentEventLog {

QString jsonlPath(const QString &cwd)
{
    return QDir(baseDir(cwd)).filePath(QStringLiteral("agent_events.jsonl"));
}

bool append(const QString &cwd, const QString &sessionId, const QString &kind,
            QJsonObject data)
{
    if (kind.trimmed().isEmpty()) return false;

    data[QStringLiteral("kind")] = kind;
    data[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    data[QStringLiteral("ts")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (!sessionId.isEmpty())
        data[QStringLiteral("sessionId")] = sessionId;

    QFile f(jsonlPath(cwd));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;
    f.write(QJsonDocument(data).toJson(QJsonDocument::Compact));
    f.write("\n");
    return true;
}

QString tail(const QString &cwd, const QString &sessionId, int n)
{
    if (n <= 0) n = 20;
    if (n > 200) n = 200;
    QFile f(jsonlPath(cwd));
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("[recent_actions: sin eventos registrados todavía]");

    // Parsear linea a linea; quedarnos con las ultimas n que matcheen la sesion.
    QStringList matched;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        if (o.isEmpty()) continue;
        if (!sessionId.isEmpty()
            && o.value(QStringLiteral("sessionId")).toString() != sessionId)
            continue;

        const QString ts   = o.value(QStringLiteral("ts")).toString();
        const QString kind = o.value(QStringLiteral("kind")).toString();
        QString row = QStringLiteral("%1 [%2]")
                          .arg(ts.section(QLatin1Char('T'), 1).left(12), kind);
        const QString tool = o.value(QStringLiteral("tool")).toString();
        if (!tool.isEmpty()) row += QStringLiteral(" tool=%1").arg(tool);
        if (o.contains(QStringLiteral("ok")))
            row += o.value(QStringLiteral("ok")).toBool() ? QStringLiteral(" ok")
                                                          : QStringLiteral(" FALLO");
        const int rep = o.value(QStringLiteral("repeatCount")).toInt();
        if (rep > 1) row += QStringLiteral(" x%1").arg(rep);
        const QString reason = o.value(QStringLiteral("reason")).toString();
        if (!reason.isEmpty()) row += QStringLiteral(" (%1)").arg(reason);
        // Recorte chico de args/resultado/detalle para dar contexto sin inflar.
        const QString detail = o.value(QStringLiteral("detail")).toString();
        const QString args   = o.value(QStringLiteral("args")).toString();
        const QString result = o.value(QStringLiteral("result")).toString();
        const QString extra  = !detail.isEmpty() ? detail
                               : !args.isEmpty() ? args : result;
        if (!extra.isEmpty())
            row += QStringLiteral(" — %1").arg(extra.simplified().left(160));
        matched << row;
    }
    if (matched.isEmpty())
        return QStringLiteral("[recent_actions: sin eventos para esta sesión]");
    if (matched.size() > n) matched = matched.mid(matched.size() - n);
    return QStringLiteral("[recent_actions: últimas %1 de tu rastro]\n%2")
        .arg(matched.size()).arg(matched.join(QLatin1Char('\n')));
}

}  // namespace AgentEventLog
