#pragma once
#include <QHash>
#include <QObject>
#include <QHostAddress>
#include <QString>

class QTcpServer;
class QTcpSocket;

struct ControlApiRequestContext
{
    QString reqId;
};

// Control API HTTP headless: espeja TODO AppController vía meta-object
// (Q_INVOKABLE + Q_PROPERTY ya expuestos a QML). Permite manejar la app sin GUI
// para tests automatizados. Sólo escucha en localhost.
//
// Endpoints:
//   GET  /              → índice de uso autodescriptivo (igual que /help)
//   GET  /help          → endpoints, formato de request/response y ejemplos
//   GET  /health        → {ok:true}
//   GET  /methods       → métodos invocables + propiedades + sub-targets (JSON)
//   GET  /prop?name=X   → {name,value} de la propiedad X
//   POST /setprop {name,value}    → escribe una propiedad; {ok:true}
//   POST /invoke {method,args}    → invoca un método; {ok:true, result?}
//
// Descubrimiento: ante un nombre desconocido (propiedad, método o target) la
// respuesta de error incluye la lista `available` de nombres válidos, así un
// cliente (IA o persona) puede corregir sin leer el código. Empezá por GET /help
// y GET /methods.
//
// Targeting de sub-objetos: todo endpoint acepta un `target` (query param en GET,
// campo JSON en POST) con una ruta de propiedades QObject* separadas por punto,
// p.ej. "profileManager" o "binaryRegistry". Sin target = AppController raíz.
// Esto expone headless TODO sub-objeto QObject hijo (registries, profileManager,
// catalog, …) sin tener que espejar sus métodos a mano. /methods sin target
// además lista los `targets` hijos disponibles para descubrimiento.
class ControlApi : public QObject
{
    Q_OBJECT
public:
    explicit ControlApi(QObject *target, QObject *parent = nullptr);
    bool start(quint16 port, const QHostAddress &addr = QHostAddress::LocalHost);

private slots:
    void onNewConnection();

private:
    void handleRequest(QTcpSocket *sock, const QByteArray &method,
                       const QString &path, const QByteArray &body,
                       const QHash<QByteArray, QByteArray> &headers);
    static QString requestIdFor(const QString &path, const QByteArray &body,
                                const QHash<QByteArray, QByteArray> &headers);
    static QByteArray withRequestId(const QByteArray &json, const QString &reqId);
    // Resuelve una ruta de props QObject* (dot-separated) desde m_target. Ruta
    // vacía = m_target. Devuelve nullptr si algún segmento no es un QObject* válido.
    QObject *resolveTarget(const QString &path) const;
    QByteArray jsonHelp() const;
    QByteArray jsonMethods(const QString &targetPath) const;
    QByteArray jsonProperty(QObject *target, const QString &name) const;
    QByteArray setProperty(QObject *target, const QByteArray &jsonBody, bool *ok) const;
    QByteArray invokeMethod(QObject *target, const QByteArray &jsonBody, bool *ok) const;

    QObject *m_target = nullptr;
    QTcpServer *m_server = nullptr;
};
