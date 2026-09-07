#include "McpClient.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>

McpClient::McpClient(const QString &serverName, QObject *parent)
    : QObject(parent), m_serverName(serverName) {}

McpClient::~McpClient() { shutdown(); }

bool McpClient::start(const QString &command, const QString &cwd)
{
    if (command.trimmed().isEmpty()) return false;
    m_proc = new QProcess(this);
    if (!cwd.isEmpty() && QFileInfo(cwd).isDir())
        m_proc->setWorkingDirectory(cwd);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels); // stderr no contamina stdout

#ifdef Q_OS_WIN
    // cmd /c resuelve PATH y .cmd/.bat (npx, uvx, etc.).
    m_proc->start(QStringLiteral("cmd"), {QStringLiteral("/c"), command});
#else
    m_proc->start(QStringLiteral("sh"), {QStringLiteral("-c"), command});
#endif
    if (!m_proc->waitForStarted(8000)) {
        emit logAppended(QStringLiteral("[mcp:%1] no se pudo iniciar: %2\n")
                             .arg(m_serverName, command));
        shutdown();
        return false;
    }

    // Handshake.
    const QJsonObject init = request(QStringLiteral("initialize"), QJsonObject{
        {QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05")},
        {QStringLiteral("capabilities"), QJsonObject{}},
        {QStringLiteral("clientInfo"), QJsonObject{
            {QStringLiteral("name"), QStringLiteral("LlamaCode")},
            {QStringLiteral("version"), QStringLiteral("1.0")}}}
    }, 60000);   // primer arranque: npx/uvx puede descargar el paquete
    if (init.isEmpty()) {
        emit logAppended(QStringLiteral("[mcp:%1] initialize sin respuesta\n").arg(m_serverName));
        shutdown();
        return false;
    }
    notify(QStringLiteral("notifications/initialized"), {});

    // Descubrir tools.
    const QJsonObject tl = request(QStringLiteral("tools/list"), {}, 30000);
    const QJsonArray arr = tl.value(QStringLiteral("result")).toObject()
                              .value(QStringLiteral("tools")).toArray();
    for (const QJsonValue &tv : arr) {
        const QJsonObject t = tv.toObject();
        ToolDef d;
        d.name        = t.value(QStringLiteral("name")).toString();
        d.description = t.value(QStringLiteral("description")).toString();
        d.inputSchema = t.value(QStringLiteral("inputSchema")).toObject();
        d.annotations = t.value(QStringLiteral("annotations")).toObject();
        if (!d.name.isEmpty()) m_tools.append(d);
    }
    m_ready = true;
    emit logAppended(QStringLiteral("[mcp:%1] listo · %2 tool(s)\n")
                         .arg(m_serverName).arg(m_tools.size()));
    return true;
}

void McpClient::shutdown()
{
    m_ready = false;
    if (m_proc) {
        m_proc->closeWriteChannel();
        m_proc->terminate();
        if (!m_proc->waitForFinished(2000)) m_proc->kill();
        m_proc->deleteLater();
        m_proc = nullptr;
    }
}

void McpClient::notify(const QString &method, const QJsonObject &params)
{
    if (!m_proc) return;
    QJsonObject msg{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                    {QStringLiteral("method"), method}};
    if (!params.isEmpty()) msg.insert(QStringLiteral("params"), params);
    m_proc->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
}

// Envía una request y bloquea hasta la respuesta con el id correspondiente.
QJsonObject McpClient::request(const QString &method, const QJsonObject &params, int timeoutMs)
{
    if (!m_proc) return {};
    const int id = ++m_id;
    QJsonObject msg{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                    {QStringLiteral("id"), id},
                    {QStringLiteral("method"), method}};
    if (!params.isEmpty()) msg.insert(QStringLiteral("params"), params);
    m_proc->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');

    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        if (!m_proc->canReadLine()) {
            const int remaining = static_cast<int>(timeoutMs - t.elapsed());
            if (remaining <= 0) break;
            if (!m_proc->waitForReadyRead(remaining)) {
                if (m_proc->state() != QProcess::Running) break;
                continue;
            }
        }
        while (m_proc->canReadLine()) {
            const QByteArray line = m_proc->readLine().trimmed();
            if (line.isEmpty()) continue;
            const QJsonObject o = QJsonDocument::fromJson(line).object();
            // Ignorar notificaciones/otros ids (logs, progress, etc.).
            if (o.contains(QStringLiteral("id")) && o.value(QStringLiteral("id")).toInt(-1) == id)
                return o;
        }
    }
    return {};
}

QString McpClient::callTool(const QString &toolName, const QJsonObject &args, bool *ok,
                            QJsonObject *rawResult, const QString &idempotencyKey,
                            const QString &correlationId)
{
    if (ok) *ok = false;
    if (!m_ready) return QStringLiteral("[mcp:%1] server no disponible]").arg(m_serverName);

    QJsonObject params{
        {QStringLiteral("name"), toolName},
        {QStringLiteral("arguments"), args}
    };
    QJsonObject meta;
    if (!idempotencyKey.isEmpty())
        meta[QStringLiteral("io.llamacode/idempotencyKey")] = idempotencyKey;
    if (!correlationId.isEmpty())
        meta[QStringLiteral("io.llamacode/correlationId")] = correlationId;
    if (!meta.isEmpty()) params[QStringLiteral("_meta")] = meta;
    const QJsonObject resp = request(QStringLiteral("tools/call"), params, 120000);

    if (resp.isEmpty())
        return QStringLiteral("[mcp:%1] sin respuesta de %2]").arg(m_serverName, toolName);
    if (resp.contains(QStringLiteral("error"))) {
        const QJsonObject e = resp.value(QStringLiteral("error")).toObject();
        return QStringLiteral("[mcp error: %1]").arg(e.value(QStringLiteral("message")).toString());
    }

    const QJsonObject result = resp.value(QStringLiteral("result")).toObject();
    if (rawResult) *rawResult = result;
    const bool isError = result.value(QStringLiteral("isError")).toBool(false);
    QString text;
    for (const QJsonValue &cv : result.value(QStringLiteral("content")).toArray()) {
        const QJsonObject c = cv.toObject();
        if (c.value(QStringLiteral("type")).toString() == QLatin1String("text"))
            text += c.value(QStringLiteral("text")).toString() + QLatin1Char('\n');
    }
    if (text.isEmpty()) text = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    if (ok) *ok = !isError;
    return text.trimmed();
}
