#include "DownloadHistoryStore.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QUuid>

DownloadHistoryStore::DownloadHistoryStore(QObject *parent) : QObject(parent) {}

QJsonObject DownloadHistoryStore::toJson(const QVariantMap &r)
{
    QJsonObject o;
    o["id"]         = r.value("id").toString();
    o["kind"]       = r.value("kind").toString();
    o["name"]       = r.value("name").toString();
    o["repo"]       = r.value("repo").toString();
    o["path"]       = r.value("path").toString();
    o["state"]      = r.value("state").toString();
    o["detail"]     = r.value("detail").toString();
    o["sizeMb"]     = r.value("sizeMb").toDouble();
    o["finishedAt"] = r.value("finishedAt").toString();
    return o;
}

QVariantMap DownloadHistoryStore::fromJson(const QJsonObject &obj)
{
    QVariantMap r;
    r["id"]         = obj.value("id").toString();
    r["kind"]       = obj.value("kind").toString();
    r["name"]       = obj.value("name").toString();
    r["repo"]       = obj.value("repo").toString();
    r["path"]       = obj.value("path").toString();
    r["state"]      = obj.value("state").toString();
    r["detail"]     = obj.value("detail").toString();
    r["sizeMb"]     = obj.value("sizeMb").toDouble();
    r["finishedAt"] = obj.value("finishedAt").toString();
    return r;
}

QString DownloadHistoryStore::storagePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/download_history.json");
}

QVariantList DownloadHistoryStore::load() const
{
    QVariantList rows;
    QFile f(storagePath());
    if (!f.open(QIODevice::ReadOnly)) return rows;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    for (const QJsonValue &v : doc.array())
        rows.append(fromJson(v.toObject()));
    return rows;
}

void DownloadHistoryStore::save(const QVariantList &rows) const
{
    QJsonArray arr;
    for (const QVariant &v : rows)
        arr.append(toJson(v.toMap()));
    QFile f(storagePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
}

void DownloadHistoryStore::append(const QVariantMap &record)
{
    QVariantList rows = load();
    QVariantMap r = record;
    if (r.value("id").toString().isEmpty())
        r["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    if (r.value("finishedAt").toString().isEmpty())
        r["finishedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    rows.append(r);
    while (rows.size() > kMax)
        rows.removeFirst();
    save(rows);
    emit changed();
}

QVariantList DownloadHistoryStore::history() const
{
    QVariantList rows = load();
    QVariantList out;
    for (int i = rows.size() - 1; i >= 0; --i)   // más nuevo primero
        out.append(rows.at(i));
    return out;
}

void DownloadHistoryStore::clear()
{
    QFile::remove(storagePath());
    emit changed();
}
