#pragma once
#include <QString>
#include <QStringList>
#include <QJsonObject>

struct ModelRoot {
    QString id;
    QString path;
    QString label;
    QString scanMode;  // "manual", "startup", "watch"
    QString kind = "dir";  // "dir" (filesystem GGUF) | "ollama" (Ollama blob store)
    bool enabled = true;
    int priority = 0;
    QStringList tags;
    bool isOnline = false;

    QJsonObject toJson() const;
    static ModelRoot fromJson(const QJsonObject &obj);
    static QString generateId();
};
