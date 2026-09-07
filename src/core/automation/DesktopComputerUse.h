#pragma once

#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Contratos compartidos por la percepción, la ejecución y la auditoría de
// Computer Use. Se mantienen en un módulo pequeño para que AgentToolRunner,
// Teach y Tasks no tengan que inventar formatos levemente distintos.
namespace DesktopComputerUse {

struct TargetRef {
    QString snapshotId;
    QString windowId;
    QString controlId;
    QString automationId;
    QString name;
    QString role;
    QString windowTitle;
    qint64 pid = 0;
    QRect bounds;
    bool enabled = true;
    bool password = false;

    QVariantMap toVariantMap() const;
    static TargetRef fromVariantMap(const QVariantMap &map);
};

struct Snapshot {
    QString snapshotId;
    QString scopeKind;
    QString targetId;
    QString provider;
    QString fingerprint;
    qint64 createdAt = 0;
    QVariantMap target;
    QVariantList windows;
    QVariantList controls;
    QVariantMap focus;
    QVariantMap capture;

    QVariantMap toVariantMap() const;
    QJsonObject toJson() const;
};

struct ActionReceipt {
    QString receiptId;
    QString tool;
    QString status;
    QString strategy;
    QString snapshotId;
    QString sessionId;
    QString correlationId;
    QString payloadHash;
    QString resultHash;
    QString detail;
    qint64 createdAt = 0;
    QVariantMap target;

    QVariantMap toVariantMap() const;
};

struct SessionLease {
    QString leaseId;
    QString sessionId;
    QString scopeKind;
    QString targetId;
    qint64 expiresAt = 0;
    int maxActions = 200;
    int actions = 0;
    bool active = true;

    bool consume();
    QVariantMap toVariantMap() const;
};

QString stableHash(const QVariantMap &value);
QString snapshotId(const QString &scopeKind, const QString &targetId,
                   const QVariantMap &target, const QVariantList &windows,
                   const QVariantList &controls);
bool snapshotMatches(const QString &expected, const QString &actual, QString *error = nullptr);

ActionReceipt makeReceipt(const QString &tool, const QJsonObject &args, bool ok,
                          const QString &result, const QString &sessionId,
                          const QString &correlationId, const QString &strategy = QString(),
                          const QString &snapshot = QString(),
                          const QVariantMap &target = {});

bool isDesktopTool(const QString &name);
bool isDesktopReadTool(const QString &name);
bool isDesktopActionTool(const QString &name);
bool isSensitiveTarget(const QVariantMap &target);
bool isDestructiveLabel(const QString &text);
QString redactForEvidence(const QString &text);

} // namespace DesktopComputerUse
