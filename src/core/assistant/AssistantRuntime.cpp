#include "AssistantRuntime.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

namespace {
constexpr int kMaxBodyBytes = 256 * 1024;
constexpr int kMaxPending = 128;
constexpr int kMaxNotifications = 256;

QString clipped(const QString &value, int max)
{
    return value.trimmed().left(max);
}

bool constantTimeEqual(const QString &left, const QString &right)
{
    const QByteArray a = left.toUtf8();
    const QByteArray b = right.toUtf8();
    const int n = qMax(a.size(), b.size());
    unsigned char diff = static_cast<unsigned char>(a.size() ^ b.size());
    for (int i = 0; i < n; ++i) {
        const unsigned char av = i < a.size() ? static_cast<unsigned char>(a.at(i)) : 0;
        const unsigned char bv = i < b.size() ? static_cast<unsigned char>(b.at(i)) : 0;
        diff = static_cast<unsigned char>(diff | (av ^ bv));
    }
    return diff == 0;
}
} // namespace

AssistantRuntime::AssistantRuntime(QObject *parent) : QObject(parent)
{
    loadNotifications();
}

AssistantRuntime::~AssistantRuntime()
{
    stop();
}

bool AssistantRuntime::start(quint16 port, const QString &token,
                             const QHostAddress &address)
{
    stop();
    if (port == 0 || token.trimmed().isEmpty()) return false;
    m_token = token.trimmed();
    m_address = address;
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection,
            this, &AssistantRuntime::onNewConnection);
    if (!m_server->listen(address, port)) {
        m_server->deleteLater();
        m_server = nullptr;
        m_token.clear();
        return false;
    }
    m_port = m_server->serverPort();
    emit listeningChanged();
    return true;
}

void AssistantRuntime::stop()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_token.clear();
    m_port = 0;
    m_address = QHostAddress();
    emit listeningChanged();
}

bool AssistantRuntime::listening() const
{
    return m_server && m_server->isListening();
}

QHostAddress AssistantRuntime::address() const
{
    return m_server ? m_server->serverAddress() : m_address;
}

bool AssistantRuntime::authenticate(const QString &token) const
{
    return !m_token.isEmpty() && constantTimeEqual(m_token, token.trimmed());
}

QString AssistantRuntime::newMessageId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString AssistantRuntime::newEventId()
{
    return QStringLiteral("evt-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QVariantMap AssistantRuntime::receiveMessage(const QVariantMap &request,
                                              const QString &token)
{
    if (!authenticate(token))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("unauthorized")}};
    const QString text = clipped(request.value(QStringLiteral("text")).toString(), 65536);
    if (text.isEmpty())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("text_required")}};
    QString id = clipped(request.value(QStringLiteral("id")).toString(), 128);
    if (id.isEmpty()) id = newMessageId();

    if (m_messages.contains(id)) {
        QVariantMap duplicate = m_messages.value(id);
        duplicate[QStringLiteral("duplicate")] = true;
        duplicate[QStringLiteral("ok")] = true;
        return duplicate;
    }

    if (m_pending.size() >= kMaxPending)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("queue_full")}};

    QVariantMap message{
        {QStringLiteral("id"), id},
        {QStringLiteral("channel"), clipped(request.value(QStringLiteral("channel"))
                                               .toString(), 64)},
        {QStringLiteral("sender"), clipped(request.value(QStringLiteral("sender"))
                                              .toString(), 128)},
        {QStringLiteral("agentId"), clipped(request.value(QStringLiteral("agentId"))
                                              .toString(), 128)},
        {QStringLiteral("text"), text},
        {QStringLiteral("status"), QStringLiteral("queued")},
        {QStringLiteral("receivedAt"),
         static_cast<double>(QDateTime::currentMSecsSinceEpoch())}};
    if (message.value(QStringLiteral("channel")).toString().isEmpty())
        message[QStringLiteral("channel")] = QStringLiteral("local");
    if (message.value(QStringLiteral("sender")).toString().isEmpty())
        message[QStringLiteral("sender")] = QStringLiteral("unknown");

    m_messages.insert(id, message);
    m_pending.append(message);
    emit messagesChanged();
    emit messageReceived(message);

    QVariantMap result = message;
    result[QStringLiteral("ok")] = true;
    return result;
}

bool AssistantRuntime::completeMessage(const QString &id, const QString &response,
                                       bool ok, const QString &detail)
{
    if (!m_messages.contains(id)) return false;
    QVariantMap message = m_messages.value(id);
    message[QStringLiteral("status")] = ok ? QStringLiteral("completed")
                                            : QStringLiteral("failed");
    message[QStringLiteral("response")] = clipped(response, 256 * 1024);
    if (!detail.trimmed().isEmpty()) message[QStringLiteral("detail")] = clipped(detail, 4096);
    message[QStringLiteral("completedAt")] =
        static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    m_messages.insert(id, message);
    for (int i = m_pending.size() - 1; i >= 0; --i)
        if (m_pending.at(i).toMap().value(QStringLiteral("id")).toString() == id)
            m_pending.removeAt(i);
    emit messagesChanged();
    emit responseReady(message);
    addNotification({{QStringLiteral("type"), QStringLiteral("assistant.reply")},
                     {QStringLiteral("messageId"), id},
                     {QStringLiteral("severity"), ok ? QStringLiteral("info")
                                                       : QStringLiteral("error")},
                     {QStringLiteral("text"), clipped(response, 4096)}});
    return true;
}

bool AssistantRuntime::cancelMessage(const QString &id, const QString &detail)
{
    return completeMessage(id, QString(), false,
                           detail.isEmpty() ? QStringLiteral("cancelled") : detail);
}

void AssistantRuntime::addNotification(const QVariantMap &event)
{
    QVariantMap copy = event;
    if (copy.value(QStringLiteral("eventId")).toString().isEmpty())
        copy[QStringLiteral("eventId")] = newEventId();
    if (copy.value(QStringLiteral("createdAt")).isNull())
        copy[QStringLiteral("createdAt")] =
            static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    m_notifications.append(copy);
    while (m_notifications.size() > kMaxNotifications)
        m_notifications.removeFirst();
    persistNotifications();
    emit notificationsChanged();
    emit notificationAdded(copy);
}

void AssistantRuntime::clearNotifications()
{
    if (m_notifications.isEmpty()) return;
    m_notifications.clear();
    persistNotifications();
    emit notificationsChanged();
}

QString AssistantRuntime::notificationStorePath()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) root = QDir::homePath() + QStringLiteral("/.llamacode");
    QDir().mkpath(root);
    return QDir(root).filePath(QStringLiteral("assistant-events.json"));
}

void AssistantRuntime::loadNotifications()
{
    QFile file(notificationStorePath());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isArray()) return;
    for (const QJsonValue &value : document.array()) {
        if (value.isObject()) m_notifications.append(value.toObject().toVariantMap());
    }
    while (m_notifications.size() > kMaxNotifications)
        m_notifications.removeFirst();
}

void AssistantRuntime::persistNotifications() const
{
    QJsonArray array;
    for (const QVariant &value : m_notifications)
        array.append(QJsonObject::fromVariantMap(value.toMap()));
    QSaveFile file(notificationStorePath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
    file.commit();
}

QString AssistantRuntime::bearerToken(const QString &header)
{
    const QString value = header.trimmed();
    if (value.startsWith(QLatin1String("Bearer "), Qt::CaseInsensitive))
        return value.mid(7).trimmed();
    return value;
}

void AssistantRuntime::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            socket->setProperty("assistantBuffer",
                                socket->property("assistantBuffer").toByteArray()
                                    + socket->readAll());
            const QByteArray data = socket->property("assistantBuffer").toByteArray();
            const int headerEnd = data.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                if (data.size() > kMaxBodyBytes + 8192)
                    writeError(socket, 413, QStringLiteral("headers too large"));
                return;
            }
            const QList<QByteArray> lines = data.left(headerEnd).split('\n');
            int contentLength = 0;
            QString auth;
            for (const QByteArray &line : lines) {
                const QByteArray lower = line.toLower();
                if (lower.startsWith("content-length:"))
                    contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
                else if (lower.startsWith("authorization:") || lower.startsWith("x-api-key:"))
                    auth = QString::fromUtf8(line.mid(line.indexOf(':') + 1).trimmed());
            }
            if (contentLength < 0 || contentLength > kMaxBodyBytes) {
                writeError(socket, 413, QStringLiteral("body too large"));
                return;
            }
            const QByteArray body = data.mid(headerEnd + 4);
            if (body.size() < contentLength) return;
            const QByteArray requestLine = lines.isEmpty() ? QByteArray() : lines.first().trimmed();
            const QList<QByteArray> parts = requestLine.split(' ');
            if (parts.size() < 2) {
                writeError(socket, 400, QStringLiteral("invalid request"));
                return;
            }
            handle(socket, parts.at(0), QString::fromUtf8(parts.at(1)),
                   body.left(contentLength), bearerToken(auth));
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void AssistantRuntime::writeError(QTcpSocket *socket, int code, const QString &message)
{
    writeJson(socket, code, {{QStringLiteral("ok"), false},
                             {QStringLiteral("error"), message}});
}

void AssistantRuntime::writeJson(QTcpSocket *socket, int code, const QVariantMap &value)
{
    const QByteArray json = QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
    const QByteArray reason = code == 200 ? "OK" : code == 202 ? "Accepted"
        : code == 400 ? "Bad Request" : code == 401 ? "Unauthorized"
        : code == 404 ? "Not Found" : code == 413 ? "Payload Too Large" : "Error";
    QByteArray response = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Cache-Control: no-store\r\n";
    response += "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void AssistantRuntime::handle(QTcpSocket *socket, const QByteArray &method,
                              const QString &path, const QByteArray &body,
                              const QString &authHeader)
{
    if (!authenticate(authHeader)) {
        writeError(socket, 401, QStringLiteral("unauthorized"));
        return;
    }
    const QString p = path.section('?', 0, 0);
    if (method == "GET" && p == QLatin1String("/health")) {
        writeJson(socket, 200, {{QStringLiteral("ok"), true},
                                {QStringLiteral("assistant"), true},
                                {QStringLiteral("listening"), listening()}});
        return;
    }
    if (method == "GET" && p == QLatin1String("/v1/assistant/messages")) {
        writeJson(socket, 200, {{QStringLiteral("ok"), true},
                                {QStringLiteral("messages"), pendingMessages()}});
        return;
    }
    if (method == "GET" && p == QLatin1String("/v1/assistant/events")) {
        QVariantList events = notifications();
        const QString after = QUrlQuery(QUrl(path)).queryItemValue(QStringLiteral("after"));
        if (!after.isEmpty()) {
            int cursor = -1;
            for (int i = 0; i < events.size(); ++i) {
                if (events.at(i).toMap().value(QStringLiteral("eventId")).toString() == after) {
                    cursor = i;
                    break;
                }
            }
            if (cursor >= 0) events = events.mid(cursor + 1);
        }
        writeJson(socket, 200, {{QStringLiteral("ok"), true},
                                {QStringLiteral("events"), events},
                                {QStringLiteral("cursor"), notifications().isEmpty()
                                     ? QString() : notifications().last().toMap().value(
                                         QStringLiteral("eventId")).toString()}});
        return;
    }
    if (method == "POST" && p == QLatin1String("/v1/assistant/messages")) {
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            writeError(socket, 400, QStringLiteral("invalid json"));
            return;
        }
        const QVariantMap result = receiveMessage(document.object().toVariantMap(),
                                                   authHeader);
        if (!result.value(QStringLiteral("ok")).toBool()) {
            writeJson(socket, result.value(QStringLiteral("error")).toString()
                                  == QLatin1String("queue_full") ? 413 : 400, result);
            return;
        }
        writeJson(socket, result.value(QStringLiteral("duplicate")).toBool() ? 200 : 202, result);
        return;
    }
    writeError(socket, 404, QStringLiteral("not found"));
}
