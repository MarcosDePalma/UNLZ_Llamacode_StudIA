#include "ModelRoot.h"
#include <QJsonArray>
#include <QUuid>
#include <QFileInfo>

QJsonObject ModelRoot::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["path"] = path;
    obj["label"] = label;
    obj["scanMode"] = scanMode;
    obj["kind"] = kind;
    obj["enabled"] = enabled;
    obj["priority"] = priority;
    obj["tags"] = QJsonArray::fromStringList(tags);
    return obj;
}

ModelRoot ModelRoot::fromJson(const QJsonObject &obj)
{
    ModelRoot r;
    r.id = obj["id"].toString();
    r.path = obj["path"].toString();
    r.label = obj["label"].toString();
    r.scanMode = obj["scanMode"].toString("manual");
    r.kind = obj["kind"].toString("dir");
    r.enabled = obj["enabled"].toBool(true);
    r.priority = obj["priority"].toInt(0);
    r.isOnline = QFileInfo::exists(r.path);
    for (const auto &v : obj["tags"].toArray())
        r.tags.append(v.toString());
    return r;
}

QString ModelRoot::generateId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
