#include "StudiaEmbed.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <cmath>

StudiaEmbed::StudiaEmbed(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

QVector<float> StudiaEmbed::normalizar(QVector<float> v)
{
    double n = 0;
    for (float x : v)
        n += double(x) * x;
    n = std::sqrt(n);
    if (n <= 0)
        return v;
    for (float &x : v)
        x = float(x / n);
    return v;
}

QVector<float> StudiaEmbed::parsearRespuesta(const QByteArray &json, QString *err)
{
    if (err) err->clear();
    const QJsonDocument d = QJsonDocument::fromJson(json);
    if (!d.isObject()) {
        if (err) *err = QStringLiteral("respuesta ilegible del servidor");
        return {};
    }
    const QJsonArray data = d.object().value(QStringLiteral("data")).toArray();
    if (data.isEmpty()) {
        // llama-server sin --embeddings responde 200 con un error adentro.
        const QString msg = d.object().value(QStringLiteral("error"))
                                .toObject().value(QStringLiteral("message")).toString();
        if (err) {
            *err = msg.isEmpty()
                ? QStringLiteral("el servidor no devolvió embeddings "
                                 "(¿lo arrancaste con --embeddings?)")
                : msg;
        }
        return {};
    }
    const QJsonArray emb = data.first().toObject()
                               .value(QStringLiteral("embedding")).toArray();
    QVector<float> v;
    v.reserve(emb.size());
    for (const QJsonValue &x : emb)
        v.append(float(x.toDouble()));
    if (v.isEmpty() && err)
        *err = QStringLiteral("el vector vino vacío");
    return v;
}

void StudiaEmbed::cancelar()
{
    if (!m_reply)
        return;
    QNetworkReply *r = m_reply;
    m_reply = nullptr;
    r->abort();
    r->deleteLater();
}

void StudiaEmbed::vectorizar(const QString &texto)
{
    if (!configurado()) {
        emit fallo(QStringLiteral("no hay servidor de embeddings configurado"));
        return;
    }
    cancelar();                       // sólo importa la última consulta
    m_textoEnVuelo = texto;

    QNetworkRequest req(QUrl(m_url + QStringLiteral("/v1/embeddings")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    const QJsonObject payload{
        {QStringLiteral("input"), texto},
        {QStringLiteral("model"), QStringLiteral("studia-embed")}};

    m_reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QNetworkReply *r = m_reply;
    connect(r, &QNetworkReply::finished, this, [this, r]() {
        if (m_reply != r) {           // fue cancelada por otra más nueva
            r->deleteLater();
            return;
        }
        const QByteArray cuerpo = r->readAll();
        const bool ok = r->error() == QNetworkReply::NoError;
        const QString errRed = r->errorString();
        m_reply = nullptr;
        r->deleteLater();

        if (!ok) {
            emit fallo(errRed);
            return;
        }
        QString err;
        const QVector<float> v = parsearRespuesta(cuerpo, &err);
        if (v.isEmpty()) {
            emit fallo(err);
            return;
        }
        emit listo(m_textoEnVuelo, normalizar(v));
    });
}
