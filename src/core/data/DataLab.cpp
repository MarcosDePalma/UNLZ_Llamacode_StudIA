#include "DataLab.h"

#include "core/DocumentExtractor.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QTimer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QUuid>

namespace {
QString typeName(const QVariantMap &field)
{
    return field.value(QStringLiteral("type"), QStringLiteral("string")).toString().trimmed().toLower();
}

QVariantList errorsToList(const QStringList &errors)
{
    QVariantList out;
    for (const QString &error : errors) out.append(error);
    return out;
}

QString fileHash(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
}
} // namespace

DataLabStore::DataLabStore(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    refresh();
}

QString DataLabStore::storageDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/data-lab/jobs");
    QDir().mkpath(dir);
    return dir;
}

QString DataLabStore::jobPath(const QString &jobId) const
{
    return storageDir() + QLatin1Char('/') + jobId + QStringLiteral(".json");
}

QString DataLabStore::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool DataLabStore::saveJob(const QVariantMap &job) const
{
    const QString id = job.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) return false;
    QSaveFile f(jobPath(id));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject::fromVariantMap(job)).toJson(QJsonDocument::Indented));
    return f.commit();
}

QVariantMap DataLabStore::readJob(const QString &jobId) const
{
    QFile f(jobPath(jobId));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
}

void DataLabStore::refresh()
{
    QVariantList out;
    QDir dir(storageDir());
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    for (const QString &file : files) {
        QFile f(dir.filePath(file));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QVariantMap job = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
        if (!job.value(QStringLiteral("id")).toString().isEmpty()) {
            QVariantMap summary = job;
            summary.remove(QStringLiteral("documents"));
            out.append(summary);
        }
    }
    m_jobs = out;
    emit jobsChanged();
}

QString DataLabStore::createJob(const QString &name, const QString &schemaJson,
                                const QStringList &files)
{
    const QJsonDocument schemaDoc = QJsonDocument::fromJson(schemaJson.toUtf8());
    if (!schemaDoc.isObject()) return {};
    const QVariantMap schema = schemaDoc.object().toVariantMap();
    if (!validateSchema(schema).isEmpty()) return {};

    QVariantList docs;
    QSet<QString> seenHashes;
    for (const QString &path : files) {
        if (!QFileInfo::exists(path) || !QFileInfo(path).isFile()) continue;
        const QString hash = fileHash(path);
        if (hash.isEmpty() || seenHashes.contains(hash)) continue;
        seenHashes.insert(hash);
        docs.append(QVariantMap{
            {QStringLiteral("id"), newId()},
            {QStringLiteral("path"), QFileInfo(path).absoluteFilePath()},
            {QStringLiteral("status"), QStringLiteral("pending")},
            {QStringLiteral("hash"), hash}
        });
    }
    if (docs.isEmpty()) return {};

    const QString id = newId();
    const QVariantMap job{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name.trimmed().isEmpty() ? QStringLiteral("Data Lab") : name.trimmed()},
        {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("schema"), schema},
        {QStringLiteral("documents"), docs},
        {QStringLiteral("status"), QStringLiteral("created")}
    };
    if (!saveJob(job)) return {};
    refresh();
    return id;
}

bool DataLabStore::deleteJob(const QString &jobId)
{
    if (jobId.isEmpty() || !QFile::remove(jobPath(jobId))) return false;
    refresh();
    return true;
}

QVariantMap DataLabStore::job(const QString &jobId) const
{
    return readJob(jobId);
}

QVariantMap DataLabStore::jobMetrics(const QString &jobId) const
{
    const QVariantMap current = readJob(jobId);
    if (current.isEmpty()) return {};
    int pending = 0;
    int extracted = 0;
    int valid = 0;
    int review = 0;
    int failed = 0;
    for (const QVariant &value : current.value(QStringLiteral("documents")).toList()) {
        const QString status = value.toMap().value(QStringLiteral("status")).toString();
        if (status == QLatin1String("pending")) ++pending;
        else if (status == QLatin1String("extracted")) ++extracted;
        else if (status == QLatin1String("valid")) ++valid;
        else if (status == QLatin1String("needs_review")) ++review;
        else if (status == QLatin1String("failed")) ++failed;
    }
    return {{QStringLiteral("total"), current.value(QStringLiteral("documents")).toList().size()},
            {QStringLiteral("pending"), pending}, {QStringLiteral("extracted"), extracted},
            {QStringLiteral("valid"), valid}, {QStringLiteral("needsReview"), review},
            {QStringLiteral("failed"), failed},
            {QStringLiteral("schemaFields"), current.value(QStringLiteral("schema")).toMap()
                 .value(QStringLiteral("fields")).toMap().size()}};
}

QVariantMap DataLabStore::processJob(const QString &jobId)
{
    QVariantMap current = readJob(jobId);
    if (current.isEmpty()) return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("job inexistente")}};

    QVariantList docs;
    int extracted = 0;
    int failed = 0;
    for (const QVariant &value : current.value(QStringLiteral("documents")).toList()) {
        QVariantMap doc = value.toMap();
        const QString currentHash = fileHash(doc.value(QStringLiteral("path")).toString());
        if (!currentHash.isEmpty() && currentHash == doc.value(QStringLiteral("hash")).toString()
            && !doc.value(QStringLiteral("text")).toString().isEmpty()) {
            docs.append(doc);
            ++extracted;
            continue;
        }
        doc[QStringLiteral("hash")] = currentHash;
        QString error;
        const QString text = DocumentExtractor::extract(doc.value(QStringLiteral("path")).toString(), &error);
        if (text.isEmpty()) {
            doc[QStringLiteral("status")] = QStringLiteral("failed");
            doc[QStringLiteral("error")] = error.isEmpty() ? QStringLiteral("sin contenido extraíble") : error;
            ++failed;
        } else {
            doc[QStringLiteral("status")] = QStringLiteral("extracted");
            doc[QStringLiteral("text")] = text;
            doc[QStringLiteral("textChars")] = text.size();
            ++extracted;
        }
        docs.append(doc);
    }
    current[QStringLiteral("documents")] = docs;
    current[QStringLiteral("status")] = failed > 0 && extracted == 0 ? QStringLiteral("failed") : QStringLiteral("extracted");
    current[QStringLiteral("processedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    saveJob(current);
    refresh();
    emit jobChanged(jobId);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("extracted"), extracted}, {QStringLiteral("failed"), failed}};
}

QString DataLabStore::extractionPrompt(const QString &jobId, const QString &documentId) const
{
    const QVariantMap current = readJob(jobId);
    const QVariantMap schema = current.value(QStringLiteral("schema")).toMap();
    for (const QVariant &value : current.value(QStringLiteral("documents")).toList()) {
        const QVariantMap doc = value.toMap();
        if (doc.value(QStringLiteral("id")).toString() != documentId) continue;
        return QStringLiteral(
            "Extraé datos del documento entre las etiquetas DOCUMENTO. Respondé únicamente un objeto JSON válido. "
            "No inventes valores: usa null cuando el dato no esté presente. Respetá exactamente este esquema: %1\n\n"
            "DOCUMENTO (%2):\n%3\n\n"
            "Reglas: no agregues markdown ni explicaciones; si un dato es ambiguo usa null.")
            .arg(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(schema)).toJson(QJsonDocument::Compact)),
                 doc.value(QStringLiteral("path")).toString(), doc.value(QStringLiteral("text")).toString());
    }
    return {};
}

QVariantMap DataLabStore::parseModelResponse(const QString &text)
{
    QString candidate = text.trimmed();
    if (candidate.startsWith(QStringLiteral("```"))) {
        const int firstNewline = candidate.indexOf('\n');
        const int closing = candidate.lastIndexOf(QStringLiteral("```"));
        if (firstNewline >= 0 && closing > firstNewline)
            candidate = candidate.mid(firstNewline + 1, closing - firstNewline - 1).trimmed();
    }
    QJsonDocument doc = QJsonDocument::fromJson(candidate.toUtf8());
    if (doc.isArray()) {
        QVariantList records;
        for (const QJsonValue &value : doc.array()) {
            if (!value.isObject())
                return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("el array contiene un elemento no-objeto")}};
            records.append(value.toObject().toVariantMap());
        }
        return {{QStringLiteral("ok"), true}, {QStringLiteral("records"), records}};
    }
    if (!doc.isObject()) {
        const int start = candidate.indexOf('{');
        const int end = candidate.lastIndexOf('}');
        if (start >= 0 && end > start)
            doc = QJsonDocument::fromJson(candidate.mid(start, end - start + 1).toUtf8());
    }
    if (!doc.isObject())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("la respuesta no contiene un objeto JSON válido")}};
    return {{QStringLiteral("ok"), true}, {QStringLiteral("record"), doc.object().toVariantMap()}};
}

QString DataLabStore::routeStage(const QVariantMap &document, const QVariantMap &validation)
{
    if (!validation.isEmpty() && validation.value(QStringLiteral("status")).toString() != QLatin1String("valid"))
        return QStringLiteral("DATA-REPAIR");
    const int chars = document.value(QStringLiteral("textChars"), document.value(QStringLiteral("text")).toString().size()).toInt();
    const QString text = document.value(QStringLiteral("text")).toString().toLower();
    if (text.contains('|') || text.contains(QStringLiteral("\t")) || chars > 120000)
        return QStringLiteral("DATA-QUALITY");
    return QStringLiteral("DATA-FAST");
}

QVariantMap DataLabStore::arbitrateCandidates(const QVariantMap &schema,
                                               const QVariantMap &first,
                                               const QVariantMap &second)
{
    const QVariantMap a = validateRecordAgainstSchema(schema, first);
    const QVariantMap b = validateRecordAgainstSchema(schema, second);
    const bool aValid = a.value(QStringLiteral("status")).toString() == QLatin1String("valid");
    const bool bValid = b.value(QStringLiteral("status")).toString() == QLatin1String("valid");
    if (aValid && bValid && a.value(QStringLiteral("record")) == b.value(QStringLiteral("record")))
        return {{QStringLiteral("status"), QStringLiteral("valid")},
                {QStringLiteral("record"), a.value(QStringLiteral("record"))},
                {QStringLiteral("agreement"), true}};
    if (aValid && !bValid) return a;
    if (bValid && !aValid) return b;
    return {{QStringLiteral("status"), QStringLiteral("needs_review")},
            {QStringLiteral("errors"), QVariantList{QStringLiteral("candidatos incompatibles; requiere revisión")}},
            {QStringLiteral("candidateA"), a.value(QStringLiteral("record"))},
            {QStringLiteral("candidateB"), b.value(QStringLiteral("record"))},
            {QStringLiteral("agreement"), false}};
}

void DataLabStore::runExtraction(const QString &jobId, const QString &documentId,
                                 const QString &baseUrl, const QString &model)
{
    const QVariantMap current = readJob(jobId);
    const QString prompt = extractionPrompt(jobId, documentId);
    if (current.isEmpty() || prompt.isEmpty() || baseUrl.trimmed().isEmpty()) {
        emit extractionFinished(jobId, documentId, false, QStringLiteral("job, documento o endpoint inválido"));
        return;
    }
    QVariantMap doc;
    for (const QVariant &value : current.value(QStringLiteral("documents")).toList()) {
        if (value.toMap().value(QStringLiteral("id")).toString() == documentId) { doc = value.toMap(); break; }
    }
    const QString stage = routeStage(doc);
    emit extractionStarted(jobId, documentId, stage);
    sendExtractionRequest(jobId, documentId, baseUrl, model, prompt, 0);
}

void DataLabStore::sendExtractionRequest(const QString &jobId, const QString &documentId,
                                         const QString &baseUrl, const QString &model,
                                         const QString &prompt, int attempt)
{
    const QString endpoint = baseUrl.trimmed().remove(QRegularExpression(QStringLiteral("/+$")))
                             + QStringLiteral("/v1/chat/completions");
    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(120000);
    const QJsonObject payload{
        {QStringLiteral("model"), model.isEmpty() ? QStringLiteral("datalab") : model},
        {QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                              {QStringLiteral("content"), prompt}}}},
        {QStringLiteral("temperature"), 0.0},
        {QStringLiteral("stream"), false}
    };
    QNetworkReply *reply = m_nam->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, jobId, documentId,
                                                     baseUrl, model, prompt, attempt]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool retryable = reply->error() != QNetworkReply::NoError
            && (status == 0 || status == 408 || status == 429 || status >= 500);
        const QByteArray body = reply->readAll();
        if (retryable && attempt == 0) {
            reply->deleteLater();
            QTimer::singleShot(250, this, [this, jobId, documentId, baseUrl, model, prompt]() {
                sendExtractionRequest(jobId, documentId, baseUrl, model, prompt, 1);
            });
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            const QString message = reply->errorString().isEmpty() ? QStringLiteral("error HTTP %1").arg(status) : reply->errorString();
            reply->deleteLater();
            emit extractionFinished(jobId, documentId, false, message);
            return;
        }
        const QJsonObject response = QJsonDocument::fromJson(body).object();
        const QString text = response.value(QStringLiteral("choices")).toArray().at(0).toObject()
            .value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
        const QVariantMap parsed = parseModelResponse(text);
        if (!parsed.value(QStringLiteral("ok")).toBool()) {
            if (attempt == 0) {
                reply->deleteLater();
                const QString repair = prompt + QStringLiteral("\n\nLa respuesta anterior no fue JSON válido. Respondé nuevamente sólo con un objeto JSON válido.");
                QTimer::singleShot(0, this, [this, jobId, documentId, baseUrl, model, repair]() {
                    sendExtractionRequest(jobId, documentId, baseUrl, model, repair, 1);
                });
                return;
            }
            reply->deleteLater();
            emit extractionFinished(jobId, documentId, false, parsed.value(QStringLiteral("error")).toString());
            return;
        }
        QVariantMap result;
        if (parsed.contains(QStringLiteral("records"))) {
            result = validateRecords(jobId, documentId,
                                     QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(parsed.value(QStringLiteral("records")).toList())).toJson(QJsonDocument::Compact)));
        } else {
            result = validateRecord(jobId, documentId,
                                    QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(parsed.value(QStringLiteral("record")).toMap())).toJson(QJsonDocument::Compact)));
        }
        reply->deleteLater();
        const bool ok = result.value(QStringLiteral("status")).toString() == QLatin1String("valid");
        emit extractionFinished(jobId, documentId, ok,
                                ok ? QStringLiteral("registro válido") : QStringLiteral("registro requiere revisión"));
    });
}

QVariantMap DataLabStore::normalizeRecord(const QVariantMap &schema, const QVariantMap &record)
{
    QVariantMap out = record;
    const QVariantMap fields = schema.value(QStringLiteral("fields")).toMap();
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        if (!out.contains(it.key()) || !out.value(it.key()).isValid()) continue;
        const QString type = typeName(it.value().toMap());
        if (type == QLatin1String("string")) {
            out[it.key()] = out.value(it.key()).toString().trimmed();
        } else if (type == QLatin1String("number")) {
            QString s = out.value(it.key()).toString().trimmed();
            s.remove(QRegularExpression(QStringLiteral("[^0-9,.-]")));
            if (s.contains(',') && s.contains('.')) s.remove('.');
            s.replace(',', '.');
            bool ok = false;
            const double n = s.toDouble(&ok);
            if (ok) out[it.key()] = n;
        } else if (type == QLatin1String("boolean")) {
            const QString s = out.value(it.key()).toString().trimmed().toLower();
            if (s == QLatin1String("true") || s == QLatin1String("sí") || s == QLatin1String("si")) out[it.key()] = true;
            else if (s == QLatin1String("false") || s == QLatin1String("no")) out[it.key()] = false;
        }
    }
    return out;
}

QStringList DataLabStore::validateSchema(const QVariantMap &schema)
{
    QStringList errors;
    const QVariantMap fields = schema.value(QStringLiteral("fields")).toMap();
    if (fields.isEmpty()) errors << QStringLiteral("schema.fields debe contener al menos un campo");
    const QSet<QString> types = {QStringLiteral("string"), QStringLiteral("number"), QStringLiteral("date"), QStringLiteral("enum"), QStringLiteral("boolean")};
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        const QVariantMap field = it.value().toMap();
        const QString type = typeName(field);
        if (!types.contains(type)) errors << QStringLiteral("tipo inválido para %1").arg(it.key());
        if (type == QLatin1String("enum") && field.value(QStringLiteral("values")).toList().isEmpty())
            errors << QStringLiteral("enum sin values para %1").arg(it.key());
        if (field.contains(QStringLiteral("min")) && field.contains(QStringLiteral("max"))
            && field.value(QStringLiteral("min")).toDouble() > field.value(QStringLiteral("max")).toDouble())
            errors << QStringLiteral("min mayor que max para %1").arg(it.key());
        const QString pattern = field.value(QStringLiteral("pattern")).toString();
        if (!pattern.isEmpty() && !QRegularExpression(pattern).isValid())
            errors << QStringLiteral("pattern inválido para %1").arg(it.key());
    }
    return errors;
}

QVariantMap DataLabStore::validateRecordAgainstSchema(const QVariantMap &schema,
                                                       const QVariantMap &record)
{
    const QVariantMap normalized = normalizeRecord(schema, record);
    QStringList errors;
    const QVariantMap fields = schema.value(QStringLiteral("fields")).toMap();
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        const QVariantMap field = it.value().toMap();
        const QVariant value = normalized.value(it.key());
        if (field.value(QStringLiteral("required")).toBool() && (!value.isValid() || value.toString().trimmed().isEmpty()))
            errors << QStringLiteral("%1: campo obligatorio ausente").arg(it.key());
        if (!value.isValid() || value.toString().trimmed().isEmpty()) continue;
        const QString type = typeName(field);
        if (type == QLatin1String("number")) {
            bool numberOk = false;
            value.toString().toDouble(&numberOk);
            if (!numberOk) errors << QStringLiteral("%1: no es número").arg(it.key());
        }
        if (type == QLatin1String("date") && !QDate::fromString(value.toString(), Qt::ISODate).isValid()) errors << QStringLiteral("%1: fecha inválida, use YYYY-MM-DD").arg(it.key());
        if (type == QLatin1String("enum") && !field.value(QStringLiteral("values")).toStringList().contains(value.toString())) errors << QStringLiteral("%1: valor fuera del enum").arg(it.key());
        if (type == QLatin1String("string")) {
            const int length = value.toString().size();
            if (field.contains(QStringLiteral("minLength")) && length < field.value(QStringLiteral("minLength")).toInt())
                errors << QStringLiteral("%1: longitud menor al mínimo").arg(it.key());
            if (field.contains(QStringLiteral("maxLength")) && length > field.value(QStringLiteral("maxLength")).toInt())
                errors << QStringLiteral("%1: longitud mayor al máximo").arg(it.key());
            const QString pattern = field.value(QStringLiteral("pattern")).toString();
            if (!pattern.isEmpty() && !QRegularExpression(pattern).match(value.toString()).hasMatch())
                errors << QStringLiteral("%1: no cumple el patrón").arg(it.key());
        }
        if (type == QLatin1String("number")) {
            const double number = value.toDouble();
            if (field.contains(QStringLiteral("min")) && number < field.value(QStringLiteral("min")).toDouble())
                errors << QStringLiteral("%1: menor al mínimo").arg(it.key());
            if (field.contains(QStringLiteral("max")) && number > field.value(QStringLiteral("max")).toDouble())
                errors << QStringLiteral("%1: mayor al máximo").arg(it.key());
        }
    }
    return {{QStringLiteral("status"), errors.isEmpty() ? QStringLiteral("valid") : QStringLiteral("needs_review")},
            {QStringLiteral("errors"), errorsToList(errors)},
            {QStringLiteral("record"), normalized},
            {QStringLiteral("score"), fields.isEmpty() ? 0.0 : (1.0 - double(errors.size()) / double(fields.size()))}};
}

QVariantMap DataLabStore::validateRecord(const QString &jobId, const QString &documentId,
                                         const QString &recordJson)
{
    QVariantMap current = readJob(jobId);
    const QJsonDocument doc = QJsonDocument::fromJson(recordJson.toUtf8());
    if (current.isEmpty() || !doc.isObject())
        return {{QStringLiteral("status"), QStringLiteral("failed")}, {QStringLiteral("errors"), QVariantList{QStringLiteral("JSON inválido")}}};
    QVariantMap result;
    QVariantList documents;
    for (const QVariant &value : current.value(QStringLiteral("documents")).toList()) {
        QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("id")).toString() == documentId) {
            result = validateRecordAgainstSchema(current.value(QStringLiteral("schema")).toMap(), doc.object().toVariantMap());
            QVariantMap evidence;
            const QString sourceText = item.value(QStringLiteral("text")).toString();
            const QVariantMap normalized = result.value(QStringLiteral("record")).toMap();
            for (auto field = normalized.cbegin(); field != normalized.cend(); ++field) {
                const QString valueText = field.value().toString();
                const int offset = valueText.isEmpty() ? -1 : sourceText.indexOf(valueText, 0, Qt::CaseInsensitive);
                evidence[field.key()] = QVariantMap{
                    {QStringLiteral("file"), item.value(QStringLiteral("path"))},
                    {QStringLiteral("offset"), offset},
                    {QStringLiteral("matched"), offset >= 0}
                };
            }
            result[QStringLiteral("evidence")] = evidence;
            item[QStringLiteral("record")] = result.value(QStringLiteral("record"));
            item[QStringLiteral("validation")] = result;
            item[QStringLiteral("status")] = result.value(QStringLiteral("status"));
        }
        documents.append(item);
    }
    current[QStringLiteral("documents")] = documents;
    saveJob(current);
    refresh();
    emit jobChanged(jobId);
    return result;
}

QVariantMap DataLabStore::validateRecords(const QString &jobId, const QString &documentId,
                                          const QString &recordsJson)
{
    QVariantMap current = readJob(jobId);
    const QJsonDocument doc = QJsonDocument::fromJson(recordsJson.toUtf8());
    if (current.isEmpty() || !doc.isArray())
        return {{QStringLiteral("status"), QStringLiteral("failed")},
                {QStringLiteral("errors"), QVariantList{QStringLiteral("se esperaba un array JSON")}}};
    QVariantList validations;
    QVariantList normalizedRecords;
    bool allValid = true;
    for (const QJsonValue &value : doc.array()) {
        if (!value.isObject()) {
            allValid = false;
            validations.append(QVariantMap{{QStringLiteral("status"), QStringLiteral("needs_review")},
                                           {QStringLiteral("errors"), QVariantList{QStringLiteral("registro no es objeto")}}});
            continue;
        }
        const QVariantMap validation = validateRecordAgainstSchema(
            current.value(QStringLiteral("schema")).toMap(), value.toObject().toVariantMap());
        validations.append(validation);
        normalizedRecords.append(validation.value(QStringLiteral("record")));
        allValid = allValid && validation.value(QStringLiteral("status")).toString() == QLatin1String("valid");
    }
    QVariantMap result{{QStringLiteral("status"), allValid ? QStringLiteral("valid") : QStringLiteral("needs_review")},
                       {QStringLiteral("records"), normalizedRecords},
                       {QStringLiteral("validations"), validations}};
    QVariantList documents;
    for (const QVariant &value : current.value(QStringLiteral("documents")).toList()) {
        QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("id")).toString() == documentId) {
            item[QStringLiteral("records")] = normalizedRecords;
            item[QStringLiteral("validations")] = validations;
            item[QStringLiteral("status")] = result.value(QStringLiteral("status"));
        }
        documents.append(item);
    }
    current[QStringLiteral("documents")] = documents;
    saveJob(current);
    refresh();
    emit jobChanged(jobId);
    return result;
}

QString DataLabStore::exportJob(const QString &jobId, const QString &path, const QString &format) const
{
    const QVariantMap current = readJob(jobId);
    if (current.isEmpty() || path.isEmpty()) return {};
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    const QVariantList docs = current.value(QStringLiteral("documents")).toList();
    if (format.toLower() == QLatin1String("sqlite") || format.toLower() == QLatin1String("db")) {
        f.close();
        const QString connection = QStringLiteral("datalab_export_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (!db.open()) { QSqlDatabase::removeDatabase(connection); return {}; }
        const QStringList fields = current.value(QStringLiteral("schema")).toMap().value(QStringLiteral("fields")).toMap().keys();
        QStringList columns;
        QStringList quotedFields;
        for (const QString &field : fields) {
            QString escaped = field;
            escaped.replace('"', "\"\"");
            quotedFields << QStringLiteral("\"%1\"").arg(escaped);
            columns << QStringLiteral("\"%1\" TEXT").arg(escaped);
        }
        QSqlQuery query(db);
        if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS records (%1)").arg(columns.join(",")))) {
            db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connection); return {};
        }
        QStringList placeholders;
        for (int i = 0; i < fields.size(); ++i) placeholders << QStringLiteral("?");
        query.prepare(QStringLiteral("INSERT INTO records (%1) VALUES (%2)")
                      .arg(quotedFields.join(","), placeholders.join(",")));
        for (const QVariant &value : docs) {
            query.clear();
            query.prepare(QStringLiteral("INSERT INTO records (%1) VALUES (%2)")
                          .arg(quotedFields.join(","), placeholders.join(",")));
            const QVariantMap record = value.toMap().value(QStringLiteral("record")).toMap();
            for (const QString &field : fields) query.addBindValue(record.value(field));
            if (!query.exec()) { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connection); return {}; }
        }
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection);
    } else if (format.toLower() == QLatin1String("csv")) {
        const QStringList fields = current.value(QStringLiteral("schema")).toMap().value(QStringLiteral("fields")).toMap().keys();
        f.write((fields.join(',') + "\n").toUtf8());
        for (const QVariant &value : docs) {
            const QVariantMap record = value.toMap().value(QStringLiteral("record")).toMap();
            QStringList row;
            for (const QString &field : fields) {
                QString cell = record.value(field).toString();
                cell.replace('"', "\"\"");
                row << QStringLiteral("\"%1\"").arg(cell);
            }
            f.write((row.join(',') + "\n").toUtf8());
        }
    } else {
        f.write(QJsonDocument(QJsonObject::fromVariantMap(current)).toJson(QJsonDocument::Indented));
    }
    f.close();
    return QFileInfo(path).absoluteFilePath();
}

QVariantMap DataLabStore::scoreRecords(const QVariantList &expected,
                                       const QVariantList &actual)
{
    const int rows = qMax(expected.size(), actual.size());
    int exactRows = 0;
    int correctFields = 0;
    int totalFields = 0;
    for (int i = 0; i < rows; ++i) {
        const QVariantMap expectedRow = i < expected.size() ? expected.at(i).toMap() : QVariantMap();
        const QVariantMap actualRow = i < actual.size() ? actual.at(i).toMap() : QVariantMap();
        bool exact = !expectedRow.isEmpty() && expectedRow == actualRow;
        if (exact) ++exactRows;
        for (auto it = expectedRow.cbegin(); it != expectedRow.cend(); ++it) {
            ++totalFields;
            if (actualRow.value(it.key()) == it.value()) ++correctFields;
        }
    }
    return {{QStringLiteral("expectedRows"), expected.size()},
            {QStringLiteral("actualRows"), actual.size()},
            {QStringLiteral("rowsExact"), exactRows},
            {QStringLiteral("fieldsCorrect"), correctFields},
            {QStringLiteral("fieldsTotal"), totalFields},
            {QStringLiteral("rowAccuracy"), rows > 0 ? double(exactRows) / rows : 0.0},
            {QStringLiteral("fieldAccuracy"), totalFields > 0 ? double(correctFields) / totalFields : 0.0}};
}

QVariantMap DataLabStore::scoreBenchmark(const QString &expectedJson,
                                         const QString &actualJson) const
{
    const QJsonDocument expectedDoc = QJsonDocument::fromJson(expectedJson.toUtf8());
    const QJsonDocument actualDoc = QJsonDocument::fromJson(actualJson.toUtf8());
    auto asList = [](const QJsonDocument &doc) {
        QVariantList out;
        if (doc.isArray()) {
            for (const QJsonValue &value : doc.array()) if (value.isObject()) out.append(value.toObject().toVariantMap());
        } else if (doc.isObject()) {
            out.append(doc.object().toVariantMap());
        }
        return out;
    };
    if ((!expectedDoc.isArray() && !expectedDoc.isObject())
        || (!actualDoc.isArray() && !actualDoc.isObject()))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("dataset JSON inválido")}};
    QVariantMap result = scoreRecords(asList(expectedDoc), asList(actualDoc));
    result[QStringLiteral("ok")] = true;
    return result;
}
