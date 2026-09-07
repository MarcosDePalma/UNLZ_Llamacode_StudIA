#include "RunHistoryStore.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QUuid>
#include <QRegularExpression>

RunHistoryStore::RunHistoryStore(QObject *parent) : QObject(parent) {}

QString RunHistoryStore::sanitize(const QString &id)
{
    QString s = id.trimmed().toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]+")), QStringLiteral("-"));
    s.replace(QRegularExpression(QStringLiteral("-+")), QStringLiteral("-"));
    while (s.startsWith('-')) s.remove(0, 1);
    while (s.endsWith('-')) s.chop(1);
    if (s.isEmpty()) s = QStringLiteral("unknown");
    return s;
}

QJsonObject RunHistoryStore::toJson(const QVariantMap &r)
{
    QJsonObject o;
    o["runId"]       = r.value("runId").toString();
    o["correlationId"] = r.value("correlationId").toString();
    o["ownerId"]     = r.value("ownerId").toString();
    o["startedAt"]   = r.value("startedAt").toString();
    o["finishedAt"]  = r.value("finishedAt").toString();
    o["status"]      = r.value("status").toString();
    o["summary"]     = r.value("summary").toString();
    o["source"]      = r.value("source").toString();
    o["automationId"]= r.value("automationId").toString();
    o["log"]         = r.value("log").toString();
    // Run-report por paso (auditoría): lista de {n,tool,ok,summary} de las tools.
    o["report"]      = QJsonArray::fromVariantList(r.value("report").toList());
    o["workflowState"] = QJsonObject::fromVariantMap(r.value("workflowState").toMap());
    o["metrics"]       = QJsonObject::fromVariantMap(r.value("metrics").toMap());
    o["receipts"]      = QJsonArray::fromVariantList(r.value("receipts").toList());
    // Metadata extensible: permite que nuevas superficies (por ejemplo una
    // corrida de un CLI externo) agreguen artefactos sin romper el formato
    // estable del historial existente.
    o["metadata"]      = QJsonObject::fromVariantMap(r.value("metadata").toMap());
    return o;
}

QVariantMap RunHistoryStore::fromJson(const QJsonObject &obj)
{
    QVariantMap r;
    r["runId"]       = obj.value("runId").toString();
    r["correlationId"] = obj.value("correlationId").toString();
    r["ownerId"]     = obj.value("ownerId").toString();
    r["startedAt"]   = obj.value("startedAt").toString();
    r["finishedAt"]  = obj.value("finishedAt").toString();
    r["status"]      = obj.value("status").toString();
    r["summary"]     = obj.value("summary").toString();
    r["source"]      = obj.value("source").toString();
    r["automationId"]= obj.value("automationId").toString();
    r["log"]         = obj.value("log").toString();
    r["report"]      = obj.value("report").toArray().toVariantList();
    r["workflowState"] = obj.value("workflowState").toObject().toVariantMap();
    r["metrics"]       = obj.value("metrics").toObject().toVariantMap();
    r["receipts"]      = obj.value("receipts").toArray().toVariantList();
    r["metadata"]      = obj.value("metadata").toObject().toVariantMap();
    return r;
}

QString RunHistoryStore::storagePath(const QString &ownerId) const
{
    const QByteArray overridePath = qgetenv("LLAMACODE_RUN_HISTORY_DIR");
    const QString dir = overridePath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
              + QStringLiteral("/run_history")
        : QString::fromLocal8Bit(overridePath);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/") + sanitize(ownerId) + QStringLiteral(".json");
}

QVariantList RunHistoryStore::load(const QString &ownerId) const
{
    QVariantList rows;
    QFile f(storagePath(ownerId));
    if (!f.open(QIODevice::ReadOnly)) return rows;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    for (const QJsonValue &v : doc.array())
        rows.append(fromJson(v.toObject()));
    return rows;
}

void RunHistoryStore::save(const QString &ownerId, const QVariantList &rows) const
{
    QJsonArray arr;
    for (const QVariant &v : rows)
        arr.append(toJson(v.toMap()));
    QFile f(storagePath(ownerId));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
}

void RunHistoryStore::append(const QString &ownerId, const QVariantMap &record)
{
    if (ownerId.isEmpty()) return;
    QVariantList rows = load(ownerId);
    QVariantMap r = record;
    r["ownerId"] = ownerId;
    if (r.value("runId").toString().isEmpty())
        r["runId"] = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    if (r.value("startedAt").toString().isEmpty())
        r["startedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    rows.append(r);
    while (rows.size() > kMaxPerOwner)
        rows.removeFirst();
    save(ownerId, rows);
    emit changed(ownerId);
}

QVariantList RunHistoryStore::history(const QString &ownerId) const
{
    QVariantList rows = load(ownerId);
    QVariantList out;
    for (int i = rows.size() - 1; i >= 0; --i)   // más nuevo primero
        out.append(rows.at(i));
    return out;
}

void RunHistoryStore::clear(const QString &ownerId)
{
    QFile::remove(storagePath(ownerId));
    emit changed(ownerId);
}
