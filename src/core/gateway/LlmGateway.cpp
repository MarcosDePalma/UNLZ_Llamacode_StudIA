#include "LlmGateway.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonValue>
#include <QTimer>
#include <QUuid>
#include <QElapsedTimer>
#include <QPointer>
#include <QUdpSocket>
#include <QNetworkDatagram>
#include <QHostInfo>
#include <QHash>
#include "core/integrations/ClaudeDesktopIntegration.h"

// ── Funciones puras ──────────────────────────────────────────────────────────

// Aplana el `content` de Anthropic (string o array de blocks) a texto OpenAI.
static QString flattenAnthropicContent(const QJsonValue &content)
{
    if (content.isString()) return content.toString();
    if (!content.isArray()) return {};
    QString out;
    for (const QJsonValue &v : content.toArray()) {
        const QJsonObject b = v.toObject();
        const QString type = b.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text"))
            out += b.value(QStringLiteral("text")).toString();
        else if (type == QLatin1String("tool_result"))
            out += flattenAnthropicContent(b.value(QStringLiteral("content")));
    }
    return out;
}

static QByteArray compactJsonValue(const QJsonValue &value)
{
    if (value.isObject()) return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    if (value.isArray()) return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    if (value.isString()) return value.toString().toUtf8();
    return QByteArrayLiteral("{}");
}

QJsonObject LlmGateway::anthropicToOpenAI(const QJsonObject &a)
{
    QJsonObject o;
    o.insert(QStringLiteral("model"), a.value(QStringLiteral("model")));
    if (a.contains(QStringLiteral("max_tokens")))
        o.insert(QStringLiteral("max_tokens"), a.value(QStringLiteral("max_tokens")));
    if (a.contains(QStringLiteral("temperature")))
        o.insert(QStringLiteral("temperature"), a.value(QStringLiteral("temperature")));
    if (a.contains(QStringLiteral("top_p")))
        o.insert(QStringLiteral("top_p"), a.value(QStringLiteral("top_p")));
    o.insert(QStringLiteral("stream"), a.value(QStringLiteral("stream")).toBool(false));

    // stop_sequences → stop
    if (a.contains(QStringLiteral("stop_sequences")))
        o.insert(QStringLiteral("stop"), a.value(QStringLiteral("stop_sequences")));

    QJsonArray msgs;
    // system (string o array de blocks) → primer mensaje system
    const QJsonValue sys = a.value(QStringLiteral("system"));
    const QString sysText = flattenAnthropicContent(sys);
    if (!sysText.isEmpty())
        msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                {QStringLiteral("content"), sysText}});

    for (const QJsonValue &mv : a.value(QStringLiteral("messages")).toArray()) {
        const QJsonObject m = mv.toObject();
        const QString role = m.value(QStringLiteral("role")).toString();
        const QJsonValue content = m.value(QStringLiteral("content"));
        if (!content.isArray()) {
            msgs.append(QJsonObject{{QStringLiteral("role"), role},
                                    {QStringLiteral("content"), flattenAnthropicContent(content)}});
            continue;
        }

        QString text;
        QJsonArray toolCalls;
        bool emittedToolResult = false;
        for (const QJsonValue &blockValue : content.toArray()) {
            const QJsonObject block = blockValue.toObject();
            const QString type = block.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("text")) {
                text += block.value(QStringLiteral("text")).toString();
            } else if (type == QLatin1String("tool_use")) {
                toolCalls.append(QJsonObject{
                    {QStringLiteral("id"), block.value(QStringLiteral("id"))},
                    {QStringLiteral("type"), QStringLiteral("function")},
                    {QStringLiteral("function"), QJsonObject{
                        {QStringLiteral("name"), block.value(QStringLiteral("name"))},
                        {QStringLiteral("arguments"),
                         QString::fromUtf8(compactJsonValue(block.value(QStringLiteral("input"))))}
                    }}
                });
            } else if (type == QLatin1String("tool_result")) {
                msgs.append(QJsonObject{
                    {QStringLiteral("role"), QStringLiteral("tool")},
                    {QStringLiteral("tool_call_id"), block.value(QStringLiteral("tool_use_id"))},
                    {QStringLiteral("content"), flattenAnthropicContent(
                        block.value(QStringLiteral("content")))}
                });
                emittedToolResult = true;
            }
        }

        if (role == QLatin1String("assistant") && !toolCalls.isEmpty()) {
            QJsonObject assistant{
                {QStringLiteral("role"), role},
                {QStringLiteral("content"), text.isEmpty() ? QJsonValue::Null
                                                             : QJsonValue(text)},
                {QStringLiteral("tool_calls"), toolCalls}
            };
            msgs.append(assistant);
        } else if (!text.isEmpty() || !emittedToolResult) {
            msgs.append(QJsonObject{{QStringLiteral("role"), role},
                                    {QStringLiteral("content"), text}});
        }
    }
    o.insert(QStringLiteral("messages"), msgs);

    // tools: anthropic {name,description,input_schema} → openai {type:function,function:{...}}
    if (a.contains(QStringLiteral("tools"))) {
        QJsonArray tools;
        for (const QJsonValue &tv : a.value(QStringLiteral("tools")).toArray()) {
            const QJsonObject t = tv.toObject();
            tools.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"), QJsonObject{
                    {QStringLiteral("name"), t.value(QStringLiteral("name"))},
                    {QStringLiteral("description"), t.value(QStringLiteral("description"))},
                    {QStringLiteral("parameters"), t.value(QStringLiteral("input_schema"))}
                }}
            });
        }
        if (!tools.isEmpty()) o.insert(QStringLiteral("tools"), tools);
    }
    return o;
}

QJsonObject LlmGateway::openAIToAnthropic(const QJsonObject &o)
{
    const QJsonObject choice = o.value(QStringLiteral("choices")).toArray().isEmpty()
        ? QJsonObject() : o.value(QStringLiteral("choices")).toArray().first().toObject();
    const QJsonObject message = choice.value(QStringLiteral("message")).toObject();

    QJsonArray content;
    const QJsonValue messageContent = message.value(QStringLiteral("content"));
    if (messageContent.isString()) {
        const QString text = messageContent.toString();
        if (!text.isEmpty())
            content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                       {QStringLiteral("text"), text}});
    } else if (messageContent.isArray()) {
        for (const QJsonValue &partValue : messageContent.toArray()) {
            const QJsonObject part = partValue.toObject();
            if (part.value(QStringLiteral("type")).toString() == QLatin1String("text"))
                content.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                           {QStringLiteral("text"), part.value(QStringLiteral("text"))}});
        }
    }
    // tool_calls → tool_use blocks
    for (const QJsonValue &tcv : message.value(QStringLiteral("tool_calls")).toArray()) {
        const QJsonObject tc = tcv.toObject();
        const QJsonObject fn = tc.value(QStringLiteral("function")).toObject();
        QJsonValue input = QJsonValue::Object;
        const QJsonValue arguments = fn.value(QStringLiteral("arguments"));
        if (arguments.isObject()) input = arguments;
        else if (arguments.isString()) {
            QJsonParseError parseError;
            const QJsonDocument parsed = QJsonDocument::fromJson(
                arguments.toString().toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && parsed.isObject())
                input = parsed.object();
        }
        content.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("tool_use")},
            {QStringLiteral("id"), tc.value(QStringLiteral("id"))},
            {QStringLiteral("name"), fn.value(QStringLiteral("name"))},
            {QStringLiteral("input"), input}
        });
    }

    const QString finish = choice.value(QStringLiteral("finish_reason")).toString();
    const QString stopReason = finish == QLatin1String("length") ? QStringLiteral("max_tokens")
                             : finish == QLatin1String("tool_calls") ? QStringLiteral("tool_use")
                             : QStringLiteral("end_turn");

    const QJsonObject usage = o.value(QStringLiteral("usage")).toObject();
    return QJsonObject{
        {QStringLiteral("id"), o.value(QStringLiteral("id")).toString(
            QStringLiteral("msg_") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(24))},
        {QStringLiteral("type"), QStringLiteral("message")},
        {QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("model"), o.value(QStringLiteral("model"))},
        {QStringLiteral("content"), content},
        {QStringLiteral("stop_reason"), stopReason},
        {QStringLiteral("stop_sequence"), QJsonValue::Null},
        {QStringLiteral("usage"), QJsonObject{
            {QStringLiteral("input_tokens"), usage.value(QStringLiteral("prompt_tokens")).toInt()},
            {QStringLiteral("output_tokens"), usage.value(QStringLiteral("completion_tokens")).toInt()}
        }}
    };
}

QString LlmGateway::resolveModel(const QString &requested, const QStringList &available)
{
    const QString r = requested.trimmed();
    if (r.isEmpty() || available.isEmpty()) return {};
    // Exacto (case-insensitive)
    for (const QString &m : available)
        if (m.compare(r, Qt::CaseInsensitive) == 0) return m;
    // El pedido contiene al candidato, o viceversa (substring, el más largo gana).
    QString best; int bestLen = -1;
    for (const QString &m : available) {
        const bool hit = m.contains(r, Qt::CaseInsensitive) || r.contains(m, Qt::CaseInsensitive);
        if (hit && m.size() > bestLen) { best = m; bestLen = m.size(); }
    }
    return best;
}

QString LlmGateway::resolveModelId(const QString &requested, const QJsonArray &models)
{
    const QString r = requested.trimmed();
    if (r.isEmpty()) return {};
    for (const QJsonValue &value : models) {
        const QJsonObject model = value.toObject();
        if (model.value(QStringLiteral("id")).toString().compare(r, Qt::CaseInsensitive) == 0)
            return model.value(QStringLiteral("id")).toString();
    }
    for (const QJsonValue &value : models) {
        const QJsonObject model = value.toObject();
        const QString id = model.value(QStringLiteral("id")).toString();
        if (model.value(QStringLiteral("name")).toString().compare(r, Qt::CaseInsensitive) == 0
            || ClaudeDesktopIntegration::modelAlias(id).compare(r, Qt::CaseInsensitive) == 0)
            return id;
    }
    return {};
}

QJsonObject LlmGateway::modelsResponse(const QJsonArray &models)
{
    QJsonArray data;
    for (const QJsonValue &value : models) {
        const QJsonObject model = value.toObject();
        const QString id = model.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;
        const QJsonObject base{
            {QStringLiteral("id"), id},
            {QStringLiteral("object"), QStringLiteral("model")},
            {QStringLiteral("owned_by"), QStringLiteral("llamacode")}
        };
        data.append(base);
        const QString alias = ClaudeDesktopIntegration::modelAlias(id);
        if (!alias.isEmpty() && alias != id) {
            QJsonObject desktop = base;
            desktop.insert(QStringLiteral("id"), alias);
            desktop.insert(QStringLiteral("display_name"),
                           model.value(QStringLiteral("name")).toString(id));
            data.append(desktop);
        }
    }
    return QJsonObject{
        {QStringLiteral("object"), QStringLiteral("list")},
        {QStringLiteral("data"), data}
    };
}

QJsonObject LlmGateway::discoveryResponse(const QString &name, quint16 port,
                                          bool ready, const QString &currentModel,
                                          const QJsonArray &models)
{
    return QJsonObject{
        {QStringLiteral("protocol"), QStringLiteral("llamacode-lan-v1")},
        {QStringLiteral("name"), name},
        {QStringLiteral("port"), static_cast<int>(port)},
        {QStringLiteral("ready"), ready},
        {QStringLiteral("currentModel"), currentModel},
        {QStringLiteral("profiles"), models}
    };
}

QStringList LlmGateway::lruTouch(QStringList &order, const QString &name, int keepN)
{
    if (name.isEmpty()) return {};
    order.removeAll(name);
    order.prepend(name);
    QStringList evicted;
    const int keep = qMax(1, keepN);
    while (order.size() > keep) evicted.append(order.takeLast());
    return evicted;
}

QJsonObject LlmGateway::applyStructuredOutput(QJsonObject payload,
                                              const QString &grammar,
                                              const QJsonObject &jsonSchema)
{
    if (!grammar.trimmed().isEmpty()) {
        payload.insert(QStringLiteral("grammar"), grammar);
    } else if (!jsonSchema.isEmpty()) {
        // llama-server: response_format json_schema o json_schema directo.
        payload.insert(QStringLiteral("response_format"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("json_schema")},
            {QStringLiteral("json_schema"), QJsonObject{{QStringLiteral("schema"), jsonSchema}}}
        });
    }
    return payload;
}

// ── Runtime ──────────────────────────────────────────────────────────────────

LlmGateway::LlmGateway(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

LlmGateway::~LlmGateway() { stop(); }

bool LlmGateway::start(quint16 port, const QHostAddress &addr)
{
    stop();
    if (port == 0) return false;
    m_lanMode = addr != QHostAddress::LocalHost && addr != QHostAddress::LocalHostIPv6;
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &LlmGateway::onNewConnection);
    if (!m_server->listen(addr, port)) {
        qWarning("LlmGateway: no pude escuchar en %s:%u", qPrintable(addr.toString()), port);
        m_server->deleteLater(); m_server = nullptr;
        return false;
    }
    m_port = port;
    if (addr != QHostAddress::LocalHost && addr != QHostAddress::LocalHostIPv6) {
        m_discovery = new QUdpSocket(this);
        if (m_discovery->bind(QHostAddress::AnyIPv4, DiscoveryPort,
                              QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            connect(m_discovery, &QUdpSocket::readyRead, this, [this]() {
                while (m_discovery && m_discovery->hasPendingDatagrams()) {
                    const QNetworkDatagram request = m_discovery->receiveDatagram();
                    if (request.data().trimmed() != QByteArrayLiteral("LLAMACODE_DISCOVER_V1"))
                        continue;
                    const QJsonObject response = discoveryResponse(
                        QHostInfo::localHostName(), m_port,
                        m_hooks.ready ? m_hooks.ready() : false,
                        m_hooks.currentModel ? m_hooks.currentModel() : QString(),
                        m_hooks.models ? m_hooks.models() : QJsonArray{});
                    m_discovery->writeDatagram(
                        QJsonDocument(response).toJson(QJsonDocument::Compact),
                        request.senderAddress(), request.senderPort());
                }
            });
        } else {
            m_discovery->deleteLater();
            m_discovery = nullptr;
        }
    }
    qInfo("LlmGateway: escuchando en http://%s:%u", qPrintable(addr.toString()), port);
    return true;
}

void LlmGateway::stop()
{
    if (m_discovery) {
        m_discovery->close();
        m_discovery->deleteLater();
        m_discovery = nullptr;
    }
    if (m_server) { m_server->close(); m_server->deleteLater(); m_server = nullptr; }
    m_lanMode = false;
    m_port = 0;
}

bool LlmGateway::listening() const { return m_server && m_server->isListening(); }

QHostAddress LlmGateway::serverAddress() const
{
    return m_server ? m_server->serverAddress() : QHostAddress();
}

QHostAddress LlmGateway::preferredLanAddress(const QList<QHostAddress> &addresses)
{
    QHostAddress fallback;
    for (const QHostAddress &address : addresses) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol
            || address.isLoopback() || address.isNull()) {
            continue;
        }
        const quint32 ip = address.toIPv4Address();
        // 169.254/16 es link-local y no sirve como dirección anunciable normal.
        if ((ip & 0xffff0000U) == 0xa9fe0000U)
            continue;
        const bool privateUse = (ip & 0xff000000U) == 0x0a000000U
            || (ip & 0xfff00000U) == 0xac100000U
            || (ip & 0xffff0000U) == 0xc0a80000U;
        if (privateUse)
            return address;
        if (fallback.isNull())
            fallback = address;
    }
    return fallback;
}

void LlmGateway::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            sock->setProperty("buf", sock->property("buf").toByteArray() + sock->readAll());
            QByteArray data = sock->property("buf").toByteArray();
            const int hdrEnd = data.indexOf("\r\n\r\n");
            if (hdrEnd < 0) return;
            const QByteArray headers = data.left(hdrEnd);
            int contentLen = 0;
            QString auth;
            const QList<QByteArray> lines = headers.split('\n');
            for (const QByteArray &l : lines) {
                const QByteArray ll = l.toLower();
                if (ll.startsWith("content-length:"))
                    contentLen = l.mid(l.indexOf(':') + 1).trimmed().toInt();
                else if (ll.startsWith("authorization:") || ll.startsWith("x-api-key:"))
                    auth = QString::fromUtf8(l.mid(l.indexOf(':') + 1).trimmed());
            }
            const QByteArray body = data.mid(hdrEnd + 4);
            if (body.size() < contentLen) return;   // esperar resto del body
            const QByteArray reqLine = lines.isEmpty() ? QByteArray() : lines.first().trimmed();
            const QList<QByteArray> parts = reqLine.split(' ');
            if (parts.size() < 2) { sock->disconnectFromHost(); return; }
            handle(sock, parts.at(0), QString::fromUtf8(parts.at(1)),
                   body.left(contentLen), auth);
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void LlmGateway::writeError(QTcpSocket *sock, int code, const QString &msg)
{
    writeJson(sock, code, QJsonObject{
        {QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), msg}}}
    });
}

void LlmGateway::writeJson(QTcpSocket *sock, int code, const QJsonObject &value)
{
    const QByteArray json = QJsonDocument(value).toJson(QJsonDocument::Compact);
    const QByteArray reason = code == 200 ? "OK"
        : code == 400 ? "Bad Request"
        : code == 401 ? "Unauthorized"
        : code == 404 ? "Not Found"
        : code == 405 ? "Method Not Allowed"
        : code == 502 ? "Bad Gateway"
        : code == 503 ? "Service Unavailable" : "Error";
    QByteArray r = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    r += "Content-Type: application/json\r\n";
    r += "Access-Control-Allow-Origin: *\r\n";
    r += "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    sock->write(r); sock->flush(); sock->disconnectFromHost();
}

void LlmGateway::handle(QTcpSocket *sock, const QByteArray &method, const QString &path,
                        const QByteArray &body, const QString &authHeader)
{
    const QString p = path.section('?', 0, 0);

    if (method == "OPTIONS") {   // CORS preflight
        QByteArray r = "HTTP/1.1 204 No Content\r\n";
        r += "Access-Control-Allow-Origin: *\r\n";
        r += "Access-Control-Allow-Headers: *\r\n";
        r += "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n\r\n";
        sock->write(r); sock->flush(); sock->disconnectFromHost();
        return;
    }
    // En loopback la health pública es útil para la UI y watchdog. En LAN toda
    // respuesta, incluida health, exige la clave; nunca se publica estado del
    // proceso a cualquiera que pueda alcanzar el puerto.
    if (!m_lanMode && (p == QLatin1String("/health") || p == QLatin1String("/"))) {
        const QByteArray j = "{\"ok\":true,\"gateway\":true}";
        QByteArray r = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
        r += "Content-Length: " + QByteArray::number(j.size()) + "\r\n\r\n" + j;
        sock->write(r); sock->flush(); sock->disconnectFromHost();
        return;
    }

    // Auth obligatoria para LAN; local sigue permitiendo gateway sin clave para
    // conservar el flujo histórico de OpenCode/Claude Desktop.
    if (m_lanMode || !m_apiKey.isEmpty()) {
        const QString tok = authHeader.startsWith(QLatin1String("Bearer "))
            ? authHeader.mid(7).trimmed() : authHeader.trimmed();
        if (m_apiKey.isEmpty() || tok != m_apiKey) {
            writeError(sock, 401, QStringLiteral("API key inválida")); return;
        }
    }

    if (p == QLatin1String("/health") || p == QLatin1String("/")) {
        const QByteArray j = "{\"ok\":true,\"gateway\":true}";
        QByteArray r = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
        r += "Content-Length: " + QByteArray::number(j.size()) + "\r\n\r\n" + j;
        sock->write(r); sock->flush(); sock->disconnectFromHost();
        return;
    }

    if (p == QLatin1String("/v1/models")) {
        if (method != "GET") {
            writeError(sock, 405, QStringLiteral("método no permitido"));
            return;
        }
        writeJson(sock, 200, modelsResponse(m_hooks.models ? m_hooks.models() : QJsonArray{}));
        return;
    }

    if (p == QLatin1String("/llamacode/v1/activate")) {
        if (method != "POST") {
            writeError(sock, 405, QStringLiteral("método no permitido"));
            return;
        }
        const QString requested = QJsonDocument::fromJson(body).object()
                                      .value(QStringLiteral("model")).toString();
        const QString resolved = resolveModelId(
            requested, m_hooks.models ? m_hooks.models() : QJsonArray{});
        if (resolved.isEmpty()) {
            writeError(sock, 404, QStringLiteral("perfil remoto desconocido"));
            return;
        }
        if (m_hooks.activity) m_hooks.activity();
        if (m_hooks.ensureModel) m_hooks.ensureModel(resolved);
        writeJson(sock, 200, QJsonObject{{"ok", true}, {"model", resolved},
                                         {"status", "starting"}});
        return;
    }

    const bool anthropic = p == QLatin1String("/v1/messages");
    const bool openai = p == QLatin1String("/v1/chat/completions");
    if (!anthropic && !openai) {
        writeError(sock, 404, QStringLiteral("endpoint desconocido"));
        return;
    }
    if (method != "POST") {
        writeError(sock, 405, QStringLiteral("método no permitido"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument requestDocument = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !requestDocument.isObject()) {
        writeError(sock, 400, QStringLiteral("JSON inválido"));
        return;
    }
    const QJsonObject reqObj = requestDocument.object();
    const bool stream = reqObj.value(QStringLiteral("stream")).toBool(false);
    const QString requested = reqObj.value(QStringLiteral("model")).toString();
    const QJsonArray models = m_hooks.models ? m_hooks.models() : QJsonArray{};
    const QString resolved = resolveModelId(requested, models);
    if (!requested.isEmpty() && resolved.isEmpty()) {
        writeError(sock, 404, QStringLiteral("modelo desconocido: %1").arg(requested));
        return;
    }

    if (m_hooks.activity) m_hooks.activity();

    // Auto-load del modelo pedido (si difiere del activo y autoSwap está on).
    if (m_autoSwap && m_hooks.ensureModel) {
        const QString current = m_hooks.currentModel ? m_hooks.currentModel() : QString();
        if (!resolved.isEmpty() && resolved.compare(current, Qt::CaseInsensitive) != 0) {
            lruTouch(m_lru, resolved, m_keepN);
            m_hooks.ensureModel(resolved);
        } else if (!resolved.isEmpty()) {
            lruTouch(m_lru, resolved, m_keepN);
        }
    }

    // Esperar a que el server esté listo (carga puede tardar), con timeout.
    auto *waitTimer = new QTimer(this);
    auto *clock = new QElapsedTimer; clock->start();
    QPointer<QTcpSocket> psock(sock);
    waitTimer->setInterval(150);
    connect(waitTimer, &QTimer::timeout, this,
            [this, waitTimer, clock, psock, p, body, anthropic, stream, resolved]() {
        const bool serverReady = m_hooks.ready ? m_hooks.ready() : true;
        const QString current = m_hooks.currentModel ? m_hooks.currentModel() : QString();
        const bool rightModel = resolved.isEmpty()
            || current.compare(resolved, Qt::CaseInsensitive) == 0;
        const bool ready = serverReady && rightModel;
        if (!psock) { waitTimer->stop(); waitTimer->deleteLater(); delete clock; return; }
        if (ready) {
            waitTimer->stop(); waitTimer->deleteLater();
            delete clock;
            forward(psock, p, body, anthropic, stream, resolved);
            return;
        }
        if (clock->elapsed() > 90000) {   // 90s
            waitTimer->stop(); waitTimer->deleteLater(); delete clock;
            writeError(psock, 503, QStringLiteral("modelo no quedó listo a tiempo"));
        }
    });
    waitTimer->start();
}

void LlmGateway::forward(QTcpSocket *sock, const QString &path, const QByteArray &body,
                         bool anthropic, bool stream, const QString &resolvedModel)
{
    const QString base = m_hooks.baseUrl ? m_hooks.baseUrl() : QString();
    if (base.isEmpty()) { writeError(sock, 502, QStringLiteral("no hay server activo")); return; }

    // Cuerpo a reenviar: Anthropic se traduce a OpenAI; OpenAI pasa tal cual.
    QByteArray upstreamBody = body;
    QString upstreamPath = path;
    QJsonObject reqObj = QJsonDocument::fromJson(body).object();
    if (!resolvedModel.isEmpty()) {
        reqObj.insert(QStringLiteral("model"), resolvedModel);
        if (!anthropic)
            upstreamBody = QJsonDocument(reqObj).toJson(QJsonDocument::Compact);
    }
    if (anthropic) {
        QJsonObject oai = anthropicToOpenAI(reqObj);
        oai.insert(QStringLiteral("stream"), stream);
        upstreamBody = QJsonDocument(oai).toJson(QJsonDocument::Compact);
        upstreamPath = QStringLiteral("/v1/chat/completions");
    }

    QNetworkRequest req(QUrl(base + upstreamPath));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    QNetworkReply *reply = m_nam->post(req, upstreamBody);
    QPointer<QTcpSocket> psock(sock);
    emit requestServed(path, reqObj.value(QStringLiteral("model")).toString());

    if (!stream) {
        connect(reply, &QNetworkReply::finished, this, [this, reply, psock, anthropic]() {
            if (!psock) { reply->deleteLater(); return; }
            const QByteArray raw = reply->readAll();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError || status >= 400) {
                QJsonObject error = QJsonDocument::fromJson(raw).object();
                const QString message = error.value(QStringLiteral("error")).toObject()
                    .value(QStringLiteral("message")).toString(reply->errorString());
                writeError(psock, status >= 400 ? status : 502, message);
                reply->deleteLater();
                return;
            }
            QByteArray out = raw;
            if (anthropic) {
                const QJsonObject oai = QJsonDocument::fromJson(raw).object();
                out = QJsonDocument(openAIToAnthropic(oai)).toJson(QJsonDocument::Compact);
            }
            QByteArray r = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
            r += "Access-Control-Allow-Origin: *\r\n";
            r += "Content-Length: " + QByteArray::number(out.size()) + "\r\n\r\n" + out;
            psock->write(r); psock->flush(); psock->disconnectFromHost();
            reply->deleteLater();
        });
        return;
    }

    // ── Streaming ────────────────────────────────────────────────────────────
    // Headers SSE una sola vez.
    QByteArray hdr = "HTTP/1.1 200 OK\r\n";
    hdr += "Content-Type: text/event-stream\r\n";
    hdr += "Cache-Control: no-cache\r\n";
    hdr += "Access-Control-Allow-Origin: *\r\n";
    hdr += "Connection: close\r\n\r\n";
    sock->write(hdr); sock->flush();

    if (anthropic) {
        // Estado de traducción OpenAI-SSE → Anthropic-SSE. Además de texto,
        // conserva tool_use/input_json_delta para que Desktop pueda ejecutar
        // herramientas en Cowork/Code.
        auto *buf = new QByteArray;
        auto *started = new bool(false);
        auto *nextBlockIndex = new int(0);
        auto *textBlockIndex = new int(-1);
        auto *toolBlocks = new QHash<int, int>;
        auto *blockIndexes = new QList<int>;
        auto *finishReason = new QString;
        const QString msgId = QStringLiteral("msg_") +
            QUuid::createUuid().toString(QUuid::WithoutBraces).left(24);
        const QString responseModel = reqObj.value(QStringLiteral("model")).toString();

        auto sendEvent = [psock](const char *ev, const QJsonObject &data) {
            if (!psock) return;
            QByteArray e = "event: "; e += ev; e += "\r\n";
            e += "data: " + QJsonDocument(data).toJson(QJsonDocument::Compact) + "\r\n\r\n";
            psock->write(e); psock->flush();
        };
        auto startMessage = [started, sendEvent, msgId, responseModel]() {
            if (*started) return;
            *started = true;
            sendEvent("message_start", QJsonObject{
                {QStringLiteral("type"), QStringLiteral("message_start")},
                {QStringLiteral("message"), QJsonObject{
                    {QStringLiteral("id"), msgId},
                    {QStringLiteral("type"), QStringLiteral("message")},
                    {QStringLiteral("role"), QStringLiteral("assistant")},
                    {QStringLiteral("model"), responseModel},
                    {QStringLiteral("content"), QJsonArray()},
                    {QStringLiteral("stop_reason"), QJsonValue::Null}
                }}});
        };

        connect(reply, &QNetworkReply::readyRead, this,
                [reply, psock, buf, started, nextBlockIndex, textBlockIndex,
                 toolBlocks, blockIndexes, finishReason, startMessage, sendEvent]() {
            if (!psock) return;
            buf->append(reply->readAll());
            while (true) {
                const int nl = buf->indexOf('\n');
                if (nl < 0) break;
                QByteArray line = buf->left(nl).trimmed();
                buf->remove(0, nl + 1);
                if (!line.startsWith("data:")) continue;
                const QByteArray d = line.mid(5).trimmed();
                if (d == "[DONE]") continue;
                const QJsonObject obj = QJsonDocument::fromJson(d).object();
                const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
                if (choices.isEmpty()) continue;
                const QJsonObject choice = choices.first().toObject();
                const QString reason = choice.value(QStringLiteral("finish_reason")).toString();
                if (!reason.isEmpty()) *finishReason = reason;
                const QJsonObject delta = choice.value(QStringLiteral("delta")).toObject();
                const QString chunk = delta.value(QStringLiteral("content")).toString();
                if (!chunk.isEmpty()) {
                    startMessage();
                    if (*textBlockIndex < 0) {
                        *textBlockIndex = (*nextBlockIndex)++;
                        blockIndexes->append(*textBlockIndex);
                        sendEvent("content_block_start", QJsonObject{
                            {QStringLiteral("type"), QStringLiteral("content_block_start")},
                            {QStringLiteral("index"), *textBlockIndex},
                            {QStringLiteral("content_block"), QJsonObject{
                                {QStringLiteral("type"), QStringLiteral("text")},
                                {QStringLiteral("text"), QStringLiteral("")}}}});
                    }
                    sendEvent("content_block_delta", QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("content_block_delta")},
                        {QStringLiteral("index"), *textBlockIndex},
                        {QStringLiteral("delta"), QJsonObject{
                            {QStringLiteral("type"), QStringLiteral("text_delta")},
                            {QStringLiteral("text"), chunk}}}});
                }

                int fallbackToolIndex = toolBlocks->size();
                for (const QJsonValue &toolValue :
                     delta.value(QStringLiteral("tool_calls")).toArray()) {
                    const QJsonObject toolCall = toolValue.toObject();
                    const QJsonObject fn = toolCall.value(QStringLiteral("function")).toObject();
                    const int toolIndex = toolCall.value(QStringLiteral("index"))
                                              .toInt(fallbackToolIndex++);
                    int blockIndex = toolBlocks->value(toolIndex, -1);
                    if (blockIndex < 0) {
                        blockIndex = (*nextBlockIndex)++;
                        toolBlocks->insert(toolIndex, blockIndex);
                        blockIndexes->append(blockIndex);
                        startMessage();
                        const QString id = toolCall.value(QStringLiteral("id"))
                                               .toString(QStringLiteral("call_%1").arg(toolIndex));
                        const QString name = fn.value(QStringLiteral("name"))
                                                .toString(QStringLiteral("tool"));
                        sendEvent("content_block_start", QJsonObject{
                            {QStringLiteral("type"), QStringLiteral("content_block_start")},
                            {QStringLiteral("index"), blockIndex},
                            {QStringLiteral("content_block"), QJsonObject{
                                {QStringLiteral("type"), QStringLiteral("tool_use")},
                                {QStringLiteral("id"), id},
                                {QStringLiteral("name"), name},
                                {QStringLiteral("input"), QJsonObject()}}}});
                    }
                    const QString arguments = fn.value(QStringLiteral("arguments")).toString();
                    if (!arguments.isEmpty()) {
                        sendEvent("content_block_delta", QJsonObject{
                            {QStringLiteral("type"), QStringLiteral("content_block_delta")},
                            {QStringLiteral("index"), blockIndex},
                            {QStringLiteral("delta"), QJsonObject{
                                {QStringLiteral("type"), QStringLiteral("input_json_delta")},
                                {QStringLiteral("partial_json"), arguments}}}});
                    }
                }
            }
        });
        connect(reply, &QNetworkReply::finished, this,
                [reply, psock, buf, started, nextBlockIndex, textBlockIndex,
                 toolBlocks, blockIndexes, finishReason, startMessage, sendEvent]() {
            if (psock) {
                startMessage();
                for (const int blockIndex : *blockIndexes) {
                    sendEvent("content_block_stop", QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("content_block_stop")},
                        {QStringLiteral("index"), blockIndex}});
                }
                const QString stopReason = *finishReason == QLatin1String("length")
                    ? QStringLiteral("max_tokens")
                    : *finishReason == QLatin1String("tool_calls")
                        ? QStringLiteral("tool_use") : QStringLiteral("end_turn");
                sendEvent("message_delta", QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("message_delta")},
                    {QStringLiteral("delta"), QJsonObject{
                        {QStringLiteral("stop_reason"), stopReason}}}});
                sendEvent("message_stop", QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("message_stop")}});
                psock->disconnectFromHost();
            }
            delete buf;
            delete started;
            delete nextBlockIndex;
            delete textBlockIndex;
            delete toolBlocks;
            delete blockIndexes;
            delete finishReason;
            reply->deleteLater();
        });
        return;
    }

    // OpenAI passthrough stream: relay crudo de bytes.
    connect(reply, &QNetworkReply::readyRead, this, [reply, psock]() {
        if (!psock) return;
        psock->write(reply->readAll()); psock->flush();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, psock]() {
        if (psock) { psock->write(reply->readAll()); psock->flush(); psock->disconnectFromHost(); }
        reply->deleteLater();
    });
}
