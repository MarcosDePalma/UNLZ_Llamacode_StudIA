#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;

// Núcleo determinista de Data Lab. No decide por el modelo: conserva documentos,
// genera contratos de extracción, valida registros y exporta resultados.
class DataLabStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
public:
    explicit DataLabStore(QObject *parent = nullptr);

    QVariantList jobs() const { return m_jobs; }
    Q_INVOKABLE QString createJob(const QString &name, const QString &schemaJson,
                                  const QStringList &files);
    Q_INVOKABLE bool deleteJob(const QString &jobId);
    Q_INVOKABLE QVariantMap job(const QString &jobId) const;
    Q_INVOKABLE QVariantMap jobMetrics(const QString &jobId) const;
    Q_INVOKABLE QVariantMap processJob(const QString &jobId);
    Q_INVOKABLE QVariantMap validateRecord(const QString &jobId, const QString &documentId,
                                           const QString &recordJson);
    Q_INVOKABLE QVariantMap validateRecords(const QString &jobId, const QString &documentId,
                                            const QString &recordsJson);
    Q_INVOKABLE QString extractionPrompt(const QString &jobId, const QString &documentId) const;
    Q_INVOKABLE void runExtraction(const QString &jobId, const QString &documentId,
                                   const QString &baseUrl, const QString &model = QString());
    Q_INVOKABLE QString exportJob(const QString &jobId, const QString &path,
                                  const QString &format = QStringLiteral("json")) const;
    Q_INVOKABLE QVariantMap scoreBenchmark(const QString &expectedJson,
                                           const QString &actualJson) const;
    Q_INVOKABLE void refresh();

    static QStringList validateSchema(const QVariantMap &schema);
    static QVariantMap validateRecordAgainstSchema(const QVariantMap &schema,
                                                   const QVariantMap &record);
    static QVariantMap normalizeRecord(const QVariantMap &schema,
                                       const QVariantMap &record);
    static QVariantMap parseModelResponse(const QString &text);
    static QString routeStage(const QVariantMap &document, const QVariantMap &validation = {});
    static QVariantMap arbitrateCandidates(const QVariantMap &schema,
                                            const QVariantMap &first,
                                            const QVariantMap &second);
    static QVariantMap scoreRecords(const QVariantList &expected,
                                    const QVariantList &actual);

signals:
    void jobsChanged();
    void jobChanged(const QString &jobId);
    void extractionStarted(const QString &jobId, const QString &documentId,
                           const QString &stage);
    void extractionFinished(const QString &jobId, const QString &documentId,
                            bool ok, const QString &message);

private:
    QString storageDir() const;
    QString jobPath(const QString &jobId) const;
    bool saveJob(const QVariantMap &job) const;
    QVariantMap readJob(const QString &jobId) const;
    static QString newId();
    void sendExtractionRequest(const QString &jobId, const QString &documentId,
                               const QString &baseUrl, const QString &model,
                               const QString &prompt, int attempt);
    QVariantList m_jobs;
    QNetworkAccessManager *m_nam = nullptr;
};
