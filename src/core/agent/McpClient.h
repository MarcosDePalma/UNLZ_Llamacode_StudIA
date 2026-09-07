#pragma once
#include <QObject>
#include <QJsonObject>
#include <QList>
#include <QString>

class QProcess;

// Cliente MCP (Model Context Protocol) mínimo sobre stdio.
// Lanza un server local por QProcess y habla JSON-RPC 2.0 (newline-delimited):
//   initialize → notifications/initialized → tools/list → tools/call
// I/O bloqueante (el loop del agente ejecuta tools de forma síncrona).
class McpClient : public QObject
{
    Q_OBJECT
public:
    struct ToolDef {
        QString name;            // nombre crudo del server
        QString description;
        QJsonObject inputSchema; // JSON Schema de parámetros
        QJsonObject annotations; // hints MCP + extensión opcional "llamacode"
    };

    explicit McpClient(const QString &serverName, QObject *parent = nullptr);
    ~McpClient() override;

    // Lanza el server y hace el handshake + tools/list. true si quedó listo.
    bool start(const QString &command, const QString &cwd);
    void shutdown();

    QString serverName() const { return m_serverName; }
    bool ready() const { return m_ready; }
    const QList<ToolDef> &tools() const { return m_tools; }

    // Ejecuta una tool del server. Devuelve el texto del resultado; *ok = !isError.
    QString callTool(const QString &toolName, const QJsonObject &args, bool *ok,
                     QJsonObject *rawResult = nullptr,
                     const QString &idempotencyKey = {},
                     const QString &correlationId = {});

signals:
    void logAppended(const QString &chunk);

private:
    QJsonObject request(const QString &method, const QJsonObject &params, int timeoutMs);
    void notify(const QString &method, const QJsonObject &params);

    QString m_serverName;
    QProcess *m_proc = nullptr;
    bool m_ready = false;
    int m_id = 0;
    QList<ToolDef> m_tools;
};
