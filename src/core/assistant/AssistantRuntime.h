#pragma once

#include <QHostAddress>
#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QTcpServer;
class QTcpSocket;

// Runtime de asistente personal independiente del canal. Expone un HTTP local
// estrecho para clientes externos, con token obligatorio, deduplicación por id
// de mensaje y un outbox pequeño de eventos. No expone ControlApi ni métodos
// reflectivos de AppController.
class AssistantRuntime : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(quint16 port READ port NOTIFY listeningChanged)
    Q_PROPERTY(QVariantList pendingMessages READ pendingMessages NOTIFY messagesChanged)
    Q_PROPERTY(QVariantList notifications READ notifications NOTIFY notificationsChanged)
public:
    explicit AssistantRuntime(QObject *parent = nullptr);
    ~AssistantRuntime() override;

    bool start(quint16 port, const QString &token,
               const QHostAddress &address = QHostAddress::LocalHost);
    void stop();
    bool listening() const;
    quint16 port() const { return m_port; }
    QHostAddress address() const;

    bool authenticate(const QString &token) const;
    QVariantMap receiveMessage(const QVariantMap &request, const QString &token);
    bool completeMessage(const QString &id, const QString &response, bool ok,
                         const QString &detail = QString());
    bool cancelMessage(const QString &id, const QString &detail = QString());
    void addNotification(const QVariantMap &event);
    Q_INVOKABLE void clearNotifications();
    QVariantList pendingMessages() const { return m_pending; }
    QVariantList notifications() const { return m_notifications; }

signals:
    void listeningChanged();
    void messagesChanged();
    void notificationsChanged();
    void messageReceived(const QVariantMap &message);
    void responseReady(const QVariantMap &message);
    void notificationAdded(const QVariantMap &event);

private:
    void onNewConnection();
    void handle(QTcpSocket *socket, const QByteArray &method, const QString &path,
                const QByteArray &body, const QString &authHeader);
    void writeJson(QTcpSocket *socket, int code, const QVariantMap &value);
    void writeError(QTcpSocket *socket, int code, const QString &message);
    static QString bearerToken(const QString &header);
    static QString newMessageId();
    static QString newEventId();
    static QString notificationStorePath();
    void loadNotifications();
    void persistNotifications() const;

    QTcpServer *m_server = nullptr;
    QString m_token;
    quint16 m_port = 0;
    QHostAddress m_address;
    QVariantList m_pending;
    QVariantList m_notifications;
    QHash<QString, QVariantMap> m_messages;
};
