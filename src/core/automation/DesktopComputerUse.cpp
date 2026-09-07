#include "DesktopComputerUse.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <QUuid>

namespace DesktopComputerUse {

QVariantMap TargetRef::toVariantMap() const
{
    return {{QStringLiteral("snapshotId"), snapshotId},
            {QStringLiteral("windowId"), windowId},
            {QStringLiteral("controlId"), controlId},
            {QStringLiteral("automationId"), automationId},
            {QStringLiteral("name"), name},
            {QStringLiteral("role"), role},
            {QStringLiteral("windowTitle"), windowTitle},
            {QStringLiteral("pid"), pid},
            {QStringLiteral("x"), bounds.x()},
            {QStringLiteral("y"), bounds.y()},
            {QStringLiteral("width"), bounds.width()},
            {QStringLiteral("height"), bounds.height()},
            {QStringLiteral("enabled"), enabled},
            {QStringLiteral("password"), password}};
}

TargetRef TargetRef::fromVariantMap(const QVariantMap &map)
{
    TargetRef ref;
    ref.snapshotId = map.value(QStringLiteral("snapshotId")).toString();
    ref.windowId = map.value(QStringLiteral("windowId")).toString();
    ref.controlId = map.value(QStringLiteral("controlId")).toString();
    ref.automationId = map.value(QStringLiteral("automationId")).toString();
    ref.name = map.value(QStringLiteral("name")).toString();
    ref.role = map.value(QStringLiteral("role")).toString();
    ref.windowTitle = map.value(QStringLiteral("windowTitle")).toString();
    ref.pid = map.value(QStringLiteral("pid")).toLongLong();
    ref.bounds = QRect(map.value(QStringLiteral("x")).toInt(),
                       map.value(QStringLiteral("y")).toInt(),
                       map.value(QStringLiteral("width")).toInt(),
                       map.value(QStringLiteral("height")).toInt());
    ref.enabled = map.value(QStringLiteral("enabled"), true).toBool();
    ref.password = map.value(QStringLiteral("password")).toBool();
    return ref;
}

QVariantMap Snapshot::toVariantMap() const
{
    return {{QStringLiteral("snapshotId"), snapshotId},
            {QStringLiteral("scopeKind"), scopeKind},
            {QStringLiteral("targetId"), targetId},
            {QStringLiteral("provider"), provider},
            {QStringLiteral("fingerprint"), fingerprint},
            {QStringLiteral("createdAt"), createdAt},
            {QStringLiteral("target"), target},
            {QStringLiteral("windows"), windows},
            {QStringLiteral("controls"), controls},
            {QStringLiteral("focus"), focus},
            {QStringLiteral("capture"), capture}};
}

QJsonObject Snapshot::toJson() const
{
    return QJsonObject::fromVariantMap(toVariantMap());
}

QVariantMap ActionReceipt::toVariantMap() const
{
    return {{QStringLiteral("receiptId"), receiptId},
            {QStringLiteral("tool"), tool},
            {QStringLiteral("status"), status},
            {QStringLiteral("strategy"), strategy},
            {QStringLiteral("snapshotId"), snapshotId},
            {QStringLiteral("sessionId"), sessionId},
            {QStringLiteral("correlationId"), correlationId},
            {QStringLiteral("payloadHash"), payloadHash},
            {QStringLiteral("resultHash"), resultHash},
            {QStringLiteral("detail"), redactForEvidence(detail)},
            {QStringLiteral("createdAt"), createdAt},
            {QStringLiteral("target"), target}};
}

bool SessionLease::consume()
{
    if (!active || actions >= maxActions) {
        active = false;
        return false;
    }
    ++actions;
    return true;
}

QVariantMap SessionLease::toVariantMap() const
{
    return {{QStringLiteral("leaseId"), leaseId},
            {QStringLiteral("sessionId"), sessionId},
            {QStringLiteral("scopeKind"), scopeKind},
            {QStringLiteral("targetId"), targetId},
            {QStringLiteral("expiresAt"), expiresAt},
            {QStringLiteral("maxActions"), maxActions},
            {QStringLiteral("actions"), actions},
            {QStringLiteral("active"), active}};
}

QString stableHash(const QVariantMap &value)
{
    const QByteArray bytes = QJsonDocument(QJsonObject::fromVariantMap(value))
                                  .toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)
                                   .toHex().left(24));
}

QString snapshotId(const QString &scopeKind, const QString &targetId,
                   const QVariantMap &target, const QVariantList &windows,
                   const QVariantList &controls)
{
    return stableHash({{QStringLiteral("scopeKind"), scopeKind},
                       {QStringLiteral("targetId"), targetId},
                       {QStringLiteral("target"), target},
                       {QStringLiteral("windows"), windows},
                       {QStringLiteral("controls"), controls}});
}

bool snapshotMatches(const QString &expected, const QString &actual, QString *error)
{
    if (expected.trimmed().isEmpty()) return true;
    if (expected == actual) return true;
    if (error) *error = QStringLiteral(
        "El snapshot quedó obsoleto; la ventana o sus controles cambiaron. Observá de nuevo antes de actuar.");
    return false;
}

ActionReceipt makeReceipt(const QString &tool, const QJsonObject &args, bool ok,
                          const QString &result, const QString &sessionId,
                          const QString &correlationId, const QString &strategy,
                          const QString &snapshot, const QVariantMap &target)
{
    ActionReceipt receipt;
    receipt.receiptId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    receipt.tool = tool;
    receipt.status = ok ? QStringLiteral("settled") : QStringLiteral("failed");
    receipt.strategy = strategy.isEmpty() ? QStringLiteral("native") : strategy;
    receipt.snapshotId = snapshot;
    receipt.sessionId = sessionId;
    receipt.correlationId = correlationId;
    receipt.payloadHash = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(args).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    receipt.resultHash = QString::fromLatin1(QCryptographicHash::hash(
        result.toUtf8(), QCryptographicHash::Sha256).toHex());
    receipt.detail = redactForEvidence(result.left(600));
    receipt.createdAt = QDateTime::currentMSecsSinceEpoch();
    receipt.target = target;
    return receipt;
}

bool isDesktopTool(const QString &name)
{
    return name.startsWith(QStringLiteral("desktop_"));
}

bool isDesktopReadTool(const QString &name)
{
    return name == QLatin1String("desktop_windows")
        || name == QLatin1String("desktop_controls")
        || name == QLatin1String("desktop_find_image")
        || name == QLatin1String("desktop_wait_image")
        || name == QLatin1String("desktop_assert_image")
        || name == QLatin1String("desktop_observe")
        || name == QLatin1String("desktop_wait")
        || name == QLatin1String("desktop_wait_for")
        || name == QLatin1String("desktop_assert");
}

bool isDesktopActionTool(const QString &name)
{
    return isDesktopTool(name) && !isDesktopReadTool(name);
}

bool isSensitiveTarget(const QVariantMap &target)
{
    if (target.value(QStringLiteral("password")).toBool()) return true;
    const QString role = target.value(QStringLiteral("role")).toString().toLower();
    const QString name = target.value(QStringLiteral("name")).toString().toLower();
    return role == QLatin1String("password")
        || name.contains(QStringLiteral("password"))
        || name.contains(QStringLiteral("contraseña"))
        || name.contains(QStringLiteral("token"))
        || name.contains(QStringLiteral("secret"));
}

bool isDestructiveLabel(const QString &text)
{
    const QString value = text.toLower();
    static const QStringList markers{
        QStringLiteral("delete"), QStringLiteral("eliminar"), QStringLiteral("borrar"),
        QStringLiteral("remove"), QStringLiteral("quitar"), QStringLiteral("format"),
        QStringLiteral("formatear"), QStringLiteral("wipe"), QStringLiteral("uninstall"),
        QStringLiteral("desinstalar"), QStringLiteral("factory reset"),
        QStringLiteral("restablecer"), QStringLiteral("vaciar"),
        QStringLiteral("empty trash"), QStringLiteral("shred")};
    for (const QString &marker : markers)
        if (value.contains(marker)) return true;
    return false;
}

QString redactForEvidence(const QString &text)
{
    QString result = text;
    static const QRegularExpression secret(
        QStringLiteral("(?i)(password|contraseña|token|secret|api[_ -]?key)\\s*[:=]\\s*[^\\s,;]+"));
    result.replace(secret, QStringLiteral("\\1=[REDACTED]"));
    return result;
}

} // namespace DesktopComputerUse
