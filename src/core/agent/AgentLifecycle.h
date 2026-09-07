#pragma once

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

// Contrato interno de eventos del agente. Los backends externos pueden usar
// payloads distintos (por ejemplo file_path vs command para una edicion), pero
// la app expone una forma estable para observabilidad, sincronizacion de indices
// y futuras extensiones del harness.
namespace AgentLifecycle {

inline QVariantMap base(const QString &event, const QString &sessionId,
                        const QString &cwd, const QString &correlationId)
{
    return {{QStringLiteral("event"), event},
            {QStringLiteral("sessionId"), sessionId},
            {QStringLiteral("cwd"), cwd},
            {QStringLiteral("correlationId"), correlationId},
            {QStringLiteral("timestampMs"), QDateTime::currentMSecsSinceEpoch()}};
}

inline QVariantMap sessionStart(const QString &sessionId, const QString &cwd,
                                const QString &correlationId, const QString &profileId,
                                const QString &engine, int version)
{
    QVariantMap out = base(QStringLiteral("session.start"), sessionId, cwd, correlationId);
    out.insert(QStringLiteral("profileId"), profileId);
    out.insert(QStringLiteral("engine"), engine);
    out.insert(QStringLiteral("engineVersion"), version);
    return out;
}

inline QVariantMap promptSubmit(const QString &sessionId, const QString &cwd,
                                const QString &correlationId, const QString &prompt,
                                int attachmentCount)
{
    QVariantMap out = base(QStringLiteral("prompt.submit"), sessionId, cwd, correlationId);
    out.insert(QStringLiteral("prompt"), prompt.left(4096));
    out.insert(QStringLiteral("attachmentCount"), attachmentCount);
    return out;
}

inline QVariantMap contextPreflight(const QString &sessionId, const QString &cwd,
                                    const QString &correlationId, int chars,
                                    const QString &mode)
{
    QVariantMap out = base(QStringLiteral("context.preflight"), sessionId, cwd, correlationId);
    out.insert(QStringLiteral("mode"), mode);
    out.insert(QStringLiteral("chars"), chars);
    return out;
}

inline QVariantMap toolEvent(const QString &event, const QString &sessionId,
                             const QString &cwd, const QString &correlationId,
                             const QString &callId, const QString &tool,
                             const QString &arguments, const QStringList &paths = {})
{
    QVariantMap out = base(event, sessionId, cwd, correlationId);
    out.insert(QStringLiteral("callId"), callId);
    out.insert(QStringLiteral("tool"), tool);
    out.insert(QStringLiteral("arguments"), arguments.left(8192));
    if (!paths.isEmpty()) out.insert(QStringLiteral("paths"), paths);
    return out;
}

inline QVariantMap toolResult(const QString &sessionId, const QString &cwd,
                              const QString &correlationId, const QString &callId,
                              const QString &tool, bool ok, bool isWrite,
                              bool externalWrite, const QString &result,
                              const QStringList &paths = {})
{
    QVariantMap out = toolEvent(QStringLiteral("tool.finish"), sessionId, cwd,
                                 correlationId, callId, tool, QString(), paths);
    out.insert(QStringLiteral("ok"), ok);
    out.insert(QStringLiteral("isWrite"), isWrite);
    out.insert(QStringLiteral("externalWrite"), externalWrite);
    out.insert(QStringLiteral("result"), result.left(8192));
    return out;
}

inline QVariantMap contextResync(const QString &sessionId, const QString &cwd,
                                 const QString &correlationId, const QString &reason,
                                 const QStringList &paths, const QString &graphReport)
{
    QVariantMap out = base(QStringLiteral("context.resync"), sessionId, cwd, correlationId);
    out.insert(QStringLiteral("reason"), reason);
    out.insert(QStringLiteral("paths"), paths);
    out.insert(QStringLiteral("graphReport"), graphReport.left(4096));
    return out;
}

inline void collectPathValue(const QJsonValue &value, QStringList &out)
{
    if (value.isString()) {
        QString path = value.toString().trimmed();
        path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        path = QDir::cleanPath(path);
        if (!path.isEmpty() && path != QLatin1String(".")) out.append(path);
    } else if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) collectPathValue(item, out);
    }
}

// Detecta comandos shell con una mutación de filesystem aunque el proveedor no
// haya marcado la tool como write. No intenta interpretar un shell completo:
// sólo reconoce verbos/redirecciones inequívocos y deja pasar builds, tests y
// comandos de consulta. La política de coordinación usa esta señal para exigir
// que el agente declare `changed_paths` antes de ejecutar una mutación opaca.
inline bool shellCommandMayMutate(const QString &command)
{
    const QString value = command.trimmed();
    if (value.isEmpty()) return false;

    static const QRegularExpression verb(
        QStringLiteral(
            "(^|[;&|]\\s*)"
            "(?:tee|touch|mkdir|md|cp|mv|rm|del|erase|rmdir|rd|"
            "copy-item|move-item|remove-item|rename-item|new-item|"
            "set-content|add-content|out-file|"
            "git\\s+(?:apply|checkout|restore|reset|clean|mv|rm)|"
            "sed\\s+[^\\r\\n]*\\s-i(?:\\s|$)|"
            "perl\\s+[^\\r\\n]*\\s-i(?:\\s|$))"),
        QRegularExpression::CaseInsensitiveOption);
    if (verb.match(value).hasMatch()) return true;

    // Redirecciones y pipe a tee mutan aunque el verbo anterior sea una
    // consulta. Se exige un destino no vacío; `2>&1` solo no es una mutación.
    static const QRegularExpression redirect(
        QStringLiteral("(?:^|\\s)(?:>>?|\\|\\s*tee)\\s*[^\\s&|]+"),
        QRegularExpression::CaseInsensitiveOption);
    if (redirect.match(value).hasMatch()) return true;

    // Python/Node y similares pueden escribir mediante APIs embebidas. Sólo
    // marcamos la forma evidente; no bloqueamos comandos arbitrarios de build.
    static const QRegularExpression embeddedWriter(
        QStringLiteral("(?:python(?:3)?|python\\.exe|node(?:\\.exe)?)"
                       "[^\\r\\n]*(?:open\\s*\\([^)]*(?:['\\\"](?:w|a|x|wb|ab)"
                       "['\\\"])[^)]*\\)|write_text\\s*\\(|writeFile(?:Sync)?\\s*\\()"),
        QRegularExpression::CaseInsensitiveOption);
    return embeddedWriter.match(value).hasMatch();
}

// Extrae rutas de inputs de tools conocidas sin asumir un proveedor concreto.
// Para apply_patch soporta tanto el campo file_path como los encabezados del
// patch que suelen viajar dentro de tool_input.command.
inline QStringList changedPathsFromToolInput(const QString &toolName,
                                             const QJsonObject &input)
{
    QStringList paths;
    const QStringList keys{QStringLiteral("path"), QStringLiteral("file_path"),
                           QStringLiteral("filePath"), QStringLiteral("filename"),
                           QStringLiteral("target"), QStringLiteral("destination"),
                           QStringLiteral("paths"), QStringLiteral("files"),
                           QStringLiteral("changed_paths"), QStringLiteral("changedPaths")};
    for (const QString &key : keys)
        if (input.contains(key)) collectPathValue(input.value(key), paths);

    const QStringList nestedKeys{QStringLiteral("arguments"), QStringLiteral("payload")};
    for (const QString &key : nestedKeys) {
        const QJsonObject nested = input.value(key).toObject();
        if (!nested.isEmpty()) paths << changedPathsFromToolInput(toolName, nested);
    }

    const QString command = input.value(QStringLiteral("command")).toString();
    const QString lowerTool = toolName.trimmed().toLower();
    if (lowerTool == QLatin1String("apply_patch")
        || command.contains(QStringLiteral("*** Begin Patch"))) {
        static const QRegularExpression patchHeader(
            QStringLiteral("^\\s*\\*\\*\\*\\s+(?:Update|Add|Delete) File:\\s*(.+?)\\s*$"),
            QRegularExpression::MultilineOption);
        for (auto it = patchHeader.globalMatch(command); it.hasNext();)
            paths.append(it.next().captured(1).trimmed());
    }

    QSet<QString> unique;
    QStringList normalized;
    for (const QString &raw : paths) {
        QString path = raw.trimmed();
        path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        path = QDir::cleanPath(path);
        if (path.isEmpty() || path == QLatin1String(".") || unique.contains(path)) continue;
        unique.insert(path);
        normalized.append(path);
    }
    normalized.sort(Qt::CaseInsensitive);
    return normalized;
}

} // namespace AgentLifecycle
