#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// Persistencia local de salas donde humanos y agentes comparten un timeline.
// El store no ejecuta modelos: conserva identidad, grants y evidencia operacional;
// AppController decide cómo despachar un preset al backend activo.
class AgentRoomStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList rooms READ rooms NOTIFY roomsChanged)
    Q_PROPERTY(QString currentRoomId READ currentRoomId WRITE setCurrentRoomId NOTIFY currentRoomChanged)
    Q_PROPERTY(QVariantMap currentRoom READ currentRoom NOTIFY currentRoomChanged)
    Q_PROPERTY(QVariantList currentEvents READ currentEvents NOTIFY eventsChanged)
    Q_PROPERTY(QVariantList currentParticipants READ currentParticipants NOTIFY participantsChanged)

public:
    explicit AgentRoomStore(QObject *parent = nullptr);
    explicit AgentRoomStore(const QString &storageDir, QObject *parent = nullptr);

    QVariantList rooms() const;
    QString currentRoomId() const { return m_currentRoomId; }
    QVariantMap currentRoom() const;
    QVariantList currentEvents() const;
    QVariantList currentParticipants() const;

    Q_INVOKABLE QString createRoom(const QString &title, const QString &projectDir = QString());
    Q_INVOKABLE bool removeRoom(const QString &roomId);
    Q_INVOKABLE bool setCurrentRoomId(const QString &roomId);
    Q_INVOKABLE QVariantMap room(const QString &roomId) const;
    Q_INVOKABLE QVariantList events(const QString &roomId, int limit = 500) const;
    Q_INVOKABLE QVariantList participants(const QString &roomId) const;
    Q_INVOKABLE bool upsertParticipant(const QString &roomId, const QVariantMap &participant);
    Q_INVOKABLE QString postEvent(const QString &roomId, const QVariantMap &event);
    Q_INVOKABLE bool updateGrant(const QString &roomId, const QString &participantId,
                                 const QVariantMap &requestedGrant);
    Q_INVOKABLE QVariantMap preset(const QString &name, const QString &goal) const;
    Q_INVOKABLE QString compactContext(const QString &roomId, const QString &participantId,
                                       int maxChars = 12000) const;

signals:
    void roomsChanged();
    void currentRoomChanged();
    void eventsChanged();
    void participantsChanged();

private:
    void load();
    bool saveRooms() const;
    QString eventsPath(const QString &roomId) const;
    int roomIndex(const QString &roomId) const;
    static QVariantMap normalizedGrant(const QVariantMap &grant);
    static bool grantDoesNotEscalate(const QVariantMap &oldGrant, const QVariantMap &nextGrant);

    QString m_storageDir;
    QVariantList m_rooms;
    QString m_currentRoomId;
};
