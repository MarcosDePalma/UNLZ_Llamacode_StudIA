#pragma once
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

// Cliente del endpoint /v1/embeddings, para vectorizar la PREGUNTA en el
// momento de consultar. Los vectores de los fragmentos ya están en el índice:
// los calcula tools/studia/vectorizar.py de una vez.
//
// Es async a proposito: se llama desde el hilo de UI y bloquearlo por una
// request HTTP —aunque tarde 50 ms— congela la ventana.
//
// La URL apunta al servidor de embeddings, que normalmente NO es el mismo que
// genera texto: un modelo de embeddings chico en otro puerto.
class StudiaEmbed : public QObject
{
    Q_OBJECT
public:
    explicit StudiaEmbed(QObject *parent = nullptr);

    QString url() const { return m_url; }
    void setUrl(const QString &u) { m_url = u.trimmed(); }
    bool configurado() const { return !m_url.isEmpty(); }

    // Pide el vector de `texto`. Emite listo(texto, vector) o fallo(motivo).
    // Si ya hay una consulta en vuelo, la cancela: siempre interesa la ultima.
    void vectorizar(const QString &texto);
    void cancelar();

    // Normaliza a norma 1: asi el coseno es un producto escalar, igual que como
    // se guardan los vectores del indice.
    static QVector<float> normalizar(QVector<float> v);
    // Parsea la respuesta del endpoint. Estatica para poder testearla sin red.
    static QVector<float> parsearRespuesta(const QByteArray &json, QString *err);

signals:
    void listo(const QString &texto, const QVector<float> &vector);
    void fallo(const QString &motivo);

private:
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply         *m_reply = nullptr;
    QString                m_url;
    QString                m_textoEnVuelo;
};
