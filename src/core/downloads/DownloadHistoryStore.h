#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>

// DownloadHistoryStore — historial persistente de descargas finalizadas (modelos,
// mmproj, draft, binarios) y cualquier otra descarga que haga la app. Cada
// registro guarda qué se bajó, de dónde, su estado final (done | error |
// canceled), tamaño y marcas de tiempo.
//
// Persistencia: <AppLocalData>/download_history.json (array, más nuevo al final).
// Respeta QStandardPaths::setTestModeEnabled para aislamiento en tests. Se capa a
// los últimos kMax registros.
class DownloadHistoryStore : public QObject
{
    Q_OBJECT
public:
    static constexpr int kMax = 200;

    explicit DownloadHistoryStore(QObject *parent = nullptr);

    // Agrega un registro. Claves usadas: kind (model|binary|other), name, repo,
    // path, state (done|error|canceled), detail, sizeMb. Completa id/finishedAt
    // si faltan.
    Q_INVOKABLE void append(const QVariantMap &record);
    // Historial completo, más nuevo primero.
    Q_INVOKABLE QVariantList history() const;
    // Borra todo el historial.
    Q_INVOKABLE void clear();

    static QJsonObject toJson(const QVariantMap &r);
    static QVariantMap fromJson(const QJsonObject &obj);

signals:
    void changed();

private:
    QString storagePath() const;
    QVariantList load() const;                 // orden de disco (más nuevo al final)
    void save(const QVariantList &rows) const;
};
