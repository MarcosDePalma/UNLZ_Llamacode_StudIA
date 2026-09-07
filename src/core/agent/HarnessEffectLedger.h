#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

struct HarnessEffectRecord {
    QString effectId;
    QString state;
    QString kind;
    QString sessionId;
    QString correlationId;
    QString payloadHash;
    QString resultHash;
    QString detail;
    qint64 updatedAt = 0;

    QJsonObject toJson() const;
    static HarnessEffectRecord fromJson(const QJsonObject &object);
};

// Ledger explícito para efectos externos. uncertain/parked son estados válidos:
// un proceso limpio no puede demostrar que una acción de red o UI no ocurrió.
class HarnessEffectLedger final {
public:
    bool open(const QString &path, QString *error = nullptr);
    bool prepare(const HarnessEffectRecord &record);
    bool transition(const QString &effectId, const QString &state,
                    const QString &detail = QString(), const QString &resultHash = QString());
    HarnessEffectRecord record(const QString &effectId) const;
    QJsonArray records() const;
    QString path() const { return m_path; }

private:
    bool load(QString *error);
    bool append(const HarnessEffectRecord &record);
    QString m_path;
    QJsonArray m_records;
};
