#include "ControlApi.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QMetaObject>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrlQuery>
#include <QUuid>
#include <QVariant>

ControlApi::ControlApi(QObject *target, QObject *parent)
    : QObject(parent), m_target(target) {}

bool ControlApi::start(quint16 port, const QHostAddress &addr)
{
    if (port == 0) return false;
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ControlApi::onNewConnection);
    if (!m_server->listen(addr, port)) {
        qWarning("ControlApi: no se pudo escuchar en %s:%u", qPrintable(addr.toString()), port);
        return false;
    }
    qInfo("ControlApi: escuchando en http://%s:%u", qPrintable(addr.toString()), port);
    return true;
}

void ControlApi::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            // Acumular hasta tener headers completos + body (Content-Length).
            sock->setProperty("buf", sock->property("buf").toByteArray() + sock->readAll());
            QByteArray data = sock->property("buf").toByteArray();
            const int hdrEnd = data.indexOf("\r\n\r\n");
            if (hdrEnd < 0) return;
            const QByteArray headers = data.left(hdrEnd);
            int contentLen = 0;
            QHash<QByteArray, QByteArray> headerMap;
            const QList<QByteArray> lines = headers.split('\n');
            for (const QByteArray &l : lines) {
                const int colon = l.indexOf(':');
                if (colon <= 0) continue;
                const QByteArray name = l.left(colon).trimmed().toLower();
                const QByteArray value = l.mid(colon + 1).trimmed();
                headerMap.insert(name, value);
                if (name == "content-length")
                    contentLen = value.toInt();
            }
            const QByteArray body = data.mid(hdrEnd + 4);
            if (body.size() < contentLen) return;   // esperar resto

            const QByteArray reqLine = lines.isEmpty() ? QByteArray() : lines.first().trimmed();
            const QList<QByteArray> parts = reqLine.split(' ');
            if (parts.size() < 2) { sock->disconnectFromHost(); return; }
            const QByteArray method = parts.at(0);
            const QString path = QString::fromUtf8(parts.at(1));
            handleRequest(sock, method, path, body.left(contentLen), headerMap);
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

static QByteArray httpResponse(int code, const QByteArray &json)
{
    const char *reason = code == 200 ? "OK" : (code == 404 ? "Not Found" : "Bad Request");
    QByteArray r = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    r += "Content-Type: application/json\r\n";
    r += "Access-Control-Allow-Origin: *\r\n";
    r += "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n";
    r += json;
    return r;
}

// Helpers de introspección/errores (definidos más abajo; usados por handleRequest).
static QJsonArray childTargets(QObject *obj);
static QByteArray errorJson(const QString &msg, const QString &availKey, const QJsonArray &avail);

void ControlApi::handleRequest(QTcpSocket *sock, const QByteArray &method,
                               const QString &path, const QByteArray &body,
                               const QHash<QByteArray, QByteArray> &headers)
{
    QByteArray resp;
    const QString p = path.section('?', 0, 0);
    const QUrlQuery query(path.section('?', 1));
    const QString reqId = requestIdFor(path, body, headers);
    // target: query param (GET) o campo JSON (POST). Ruta de QObject* hijos.
    QString targetPath = query.queryItemValue(QStringLiteral("target"));
    if (method == "POST" && targetPath.isEmpty())
        targetPath = QJsonDocument::fromJson(body).object()
                         .value(QStringLiteral("target")).toString();

    // Error de target desconocido con la lista de targets válidos (descubrimiento).
    auto unknownTarget = [this]() {
        return errorJson(QStringLiteral("target desconocido"),
                         QStringLiteral("available"),
                         m_target ? childTargets(m_target) : QJsonArray{});
    };

    if (p == QLatin1String("/") || p == QLatin1String("/help")) {
        resp = httpResponse(200, withRequestId(jsonHelp(), reqId));
    } else if (p == QLatin1String("/health")) {
        resp = httpResponse(200, withRequestId("{\"ok\":true}", reqId));
    } else if (p == QLatin1String("/methods")) {
        resp = httpResponse(200, withRequestId(jsonMethods(targetPath), reqId));
    } else if (p == QLatin1String("/prop")) {
        QObject *t = resolveTarget(targetPath);
        resp = t ? httpResponse(200, withRequestId(jsonProperty(t, query.queryItemValue(QStringLiteral("name"))), reqId))
                 : httpResponse(400, withRequestId(unknownTarget(), reqId));
    } else if (p == QLatin1String("/setprop") && method == "POST") {
        bool ok = false;
        QObject *t = resolveTarget(targetPath);
        const QByteArray out = t ? setProperty(t, body, &ok) : unknownTarget();
        resp = httpResponse(ok ? 200 : 400, withRequestId(out, reqId));
    } else if (p == QLatin1String("/invoke") && method == "POST") {
        bool ok = false;
        QObject *t = resolveTarget(targetPath);
        const QByteArray out = t ? invokeMethod(t, body, &ok) : unknownTarget();
        resp = httpResponse(ok ? 200 : 400, withRequestId(out, reqId));
    } else {
        resp = httpResponse(404, withRequestId("{\"error\":\"endpoint desconocido; probá GET /help\"}", reqId));
    }

    sock->write(resp);
    sock->flush();
    sock->disconnectFromHost();
}

QString ControlApi::requestIdFor(const QString &path, const QByteArray &body,
                                 const QHash<QByteArray, QByteArray> &headers)
{
    const QUrlQuery query(path.section('?', 1));
    QString reqId = query.queryItemValue(QStringLiteral("reqId")).trimmed();
    if (reqId.isEmpty())
        reqId = QString::fromUtf8(headers.value("x-req-id")).trimmed();
    if (reqId.isEmpty())
        reqId = QString::fromUtf8(headers.value("reqid")).trimmed();
    if (reqId.isEmpty() && !body.isEmpty())
        reqId = QJsonDocument::fromJson(body).object()
                    .value(QStringLiteral("reqId")).toString().trimmed();
    if (reqId.isEmpty())
        reqId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return reqId;
}

QByteArray ControlApi::withRequestId(const QByteArray &json, const QString &reqId)
{
    QJsonObject o = QJsonDocument::fromJson(json).object();
    o.insert(QStringLiteral("reqId"), reqId);
    if (o.contains(QStringLiteral("error")) && !o.contains(QStringLiteral("ok")))
        o.insert(QStringLiteral("ok"), false);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QObject *ControlApi::resolveTarget(const QString &path) const
{
    QObject *cur = m_target;
    if (path.isEmpty()) return cur;
    const QStringList segs = path.split('.', Qt::SkipEmptyParts);
    for (const QString &seg : segs) {
        if (!cur) return nullptr;
        const QVariant v = cur->property(seg.toUtf8().constData());
        QObject *child = v.value<QObject *>();
        if (!child) return nullptr;
        cur = child;
    }
    return cur;
}

// Lista las propiedades que son QObject* (sub-targets navegables headless).
static QJsonArray childTargets(QObject *obj)
{
    QJsonArray out;
    const QMetaObject *mo = obj->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty pr = mo->property(i);
        const QVariant v = obj->property(pr.name());
        if (v.value<QObject *>())
            out.append(QString::fromUtf8(pr.name()));
    }
    return out;
}

// Nombres de propiedades del objeto (para "did-you-mean" en errores).
static QJsonArray propertyNames(QObject *obj)
{
    QJsonArray out;
    const QMetaObject *mo = obj->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i)
        out.append(QString::fromUtf8(mo->property(i).name()));
    return out;
}

// Nombres de métodos públicos invocables (con su aridad) para "did-you-mean".
static QJsonArray methodNames(QObject *obj)
{
    QJsonArray out;
    const QMetaObject *mo = obj->metaObject();
    // Recorrer desde 0 mantiene el descubrimiento consistente incluso cuando
    // el objeto expone métodos heredados o cuando moc reordena el offset de la
    // clase durante builds incrementales. Los métodos QObject base se filtran
    // abajo por su acceso/tipo y no afectan la invocación.
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.access() != QMetaMethod::Public) continue;
        if (m.methodType() != QMetaMethod::Method && m.methodType() != QMetaMethod::Slot)
            continue;
        out.append(QString::fromUtf8(m.name()) + QStringLiteral("/")
                   + QString::number(m.parameterCount()));
    }
    return out;
}

static QByteArray errorJson(const QString &msg, const QString &availKey, const QJsonArray &avail)
{
    QJsonObject o{{QStringLiteral("error"), msg}};
    if (!avail.isEmpty()) o[availKey] = avail;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray ControlApi::jsonHelp() const
{
    // Índice autodescriptivo: qué endpoints hay, cómo se llaman y un ejemplo de
    // flujo. Pensado para que un cliente (IA o persona) arranque sin leer el código.
    QJsonObject endpoints{
        {QStringLiteral("GET /"),        QStringLiteral("este índice de uso (== /help)")},
        {QStringLiteral("GET /help"),    QStringLiteral("este índice de uso")},
        {QStringLiteral("GET /health"),  QStringLiteral("{ok:true} si el server vive")},
        {QStringLiteral("GET /methods"), QStringLiteral("{methods,properties,targets} del target (?target=...)")},
        {QStringLiteral("GET /prop"),    QStringLiteral("?name=X[&target=...] → {name,value}")},
        {QStringLiteral("POST /setprop"),QStringLiteral("body {name,value[,target]} → {ok:true}")},
        {QStringLiteral("POST /invoke"), QStringLiteral("body {method,args[,target]} → {ok:true,result?}")},
    };
    QJsonObject notes{
        {QStringLiteral("target"),
         QStringLiteral("sub-objeto QObject* por ruta con puntos (query en GET, campo JSON en POST). "
                        "Vacío = AppController raíz. Ver 'targets' en /methods.")},
        {QStringLiteral("errores"),
         QStringLiteral("nombre desconocido (prop/método/target) → el error trae 'available' con los válidos; "
                        "aridad incorrecta → 'validArgCounts'.")},
        {QStringLiteral("respuestas"),
         QStringLiteral("/prop devuelve {name,value}; /invoke y /setprop devuelven {ok:true[,result]}; "
                        "los errores, {error[,available]}.")},
    };
    QJsonArray example{
        QStringLiteral("curl localhost:8765/methods                                  # descubrir métodos/props/targets"),
        QStringLiteral("curl -XPOST localhost:8765/invoke -d '{\"method\":\"launchMenu\",\"args\":[]}'   # ids de perfiles"),
        QStringLiteral("curl -XPOST localhost:8765/invoke -d '{\"method\":\"startServerAndAgent\",\"args\":[\"<launchId>\"]}'"),
        QStringLiteral("curl 'localhost:8765/prop?name=serverState'                  # esperar 'running'"),
        QStringLiteral("curl -XPOST localhost:8765/invoke -d '{\"method\":\"sendToAgent\",\"args\":[\"hola\"]}'"),
        QStringLiteral("curl 'localhost:8765/prop?name=agentMessages'                # leer la conversación"),
    };
    return QJsonDocument(QJsonObject{
        {QStringLiteral("service"), QStringLiteral("LlamaCode ControlApi (headless)")},
        {QStringLiteral("bind"), QStringLiteral("localhost only; puerto = env LLAMACODE_CONTROL_PORT (default 8765, 0=off)")},
        {QStringLiteral("endpoints"), endpoints},
        {QStringLiteral("notes"), notes},
        {QStringLiteral("exampleFlow"), example},
        {QStringLiteral("docs"), QStringLiteral("docs/HEADLESS.md")}
    }).toJson(QJsonDocument::Compact);
}

QByteArray ControlApi::jsonMethods(const QString &targetPath) const
{
    QObject *target = resolveTarget(targetPath);
    if (!target)
        return "{\"error\":\"target desconocido\"}";
    const QMetaObject *mo = target->metaObject();
    QJsonArray methods;
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.methodType() != QMetaMethod::Method && m.methodType() != QMetaMethod::Slot)
            continue;
        if (m.access() != QMetaMethod::Public) continue;
        QJsonArray params;
        const QList<QByteArray> names = m.parameterNames();
        for (int j = 0; j < m.parameterCount(); ++j)
            params.append(QString::fromUtf8(m.parameterTypeName(j)) + QStringLiteral(" ")
                          + QString::fromUtf8(j < names.size() ? names.at(j) : QByteArray()));
        methods.append(QJsonObject{
            {QStringLiteral("name"), QString::fromUtf8(m.name())},
            {QStringLiteral("returns"), QString::fromUtf8(m.typeName())},
            {QStringLiteral("params"), params}
        });
    }
    QJsonArray props;
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty pr = mo->property(i);
        props.append(QJsonObject{
            {QStringLiteral("name"), QString::fromUtf8(pr.name())},
            {QStringLiteral("type"), QString::fromUtf8(pr.typeName())}
        });
    }
    return QJsonDocument(QJsonObject{
        {QStringLiteral("methods"), methods},
        {QStringLiteral("properties"), props},
        {QStringLiteral("targets"), childTargets(target)}
    }).toJson(QJsonDocument::Compact);
}

QByteArray ControlApi::jsonProperty(QObject *target, const QString &name) const
{
    if (name.isEmpty())
        return errorJson(QStringLiteral("falta el parámetro 'name'"),
                         QStringLiteral("available"), propertyNames(target));
    const QVariant v = target->property(name.toUtf8().constData());
    if (!v.isValid())
        return errorJson(QStringLiteral("propiedad desconocida: ") + name,
                         QStringLiteral("available"), propertyNames(target));
    const QJsonValue jv = QJsonValue::fromVariant(v);
    return QJsonDocument(QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("value"), jv}
    }).toJson(QJsonDocument::Compact);
}

QByteArray ControlApi::setProperty(QObject *target, const QByteArray &jsonBody, bool *ok) const
{
    if (ok) *ok = false;
    const QJsonObject req = QJsonDocument::fromJson(jsonBody).object();
    const QString name = req.value(QStringLiteral("name")).toString();
    if (name.isEmpty()) return "{\"error\":\"falta 'name'\"}";
    const QVariant val = req.value(QStringLiteral("value")).toVariant();
    const bool done = target->setProperty(name.toUtf8().constData(), val);
    if (!done)
        return "{\"error\":\"propiedad no escribible o desconocida: " + name.toUtf8() + "\"}";
    if (ok) *ok = true;
    return "{\"ok\":true}";
}

QByteArray ControlApi::invokeMethod(QObject *targetObj, const QByteArray &jsonBody, bool *ok) const
{
    if (ok) *ok = false;
    const QJsonObject req = QJsonDocument::fromJson(jsonBody).object();
    const QString name = req.value(QStringLiteral("method")).toString();
    const QJsonArray args = req.value(QStringLiteral("args")).toArray();
    if (name.isEmpty())
        return "{\"error\":\"falta 'method'\"}";

    const QMetaObject *mo = targetObj->metaObject();
    QMetaMethod target;
    QJsonArray sameNameArities;   // aridades válidas para un nombre que sí existe
    for (int i = 0; i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.access() != QMetaMethod::Public) continue;
        if (QString::fromUtf8(m.name()) != name) continue;
        sameNameArities.append(m.parameterCount());
        if (m.parameterCount() != args.size()) continue;
        target = m;
        break;
    }
    if (!target.isValid()) {
        if (!sameNameArities.isEmpty())   // existe el método, pero otra aridad
            return errorJson(QStringLiteral("aridad incorrecta para '%1': se pasaron %2 args")
                                 .arg(name).arg(args.size()),
                             QStringLiteral("validArgCounts"), sameNameArities);
        return errorJson(QStringLiteral("método no encontrado: ") + name,
                         QStringLiteral("available"), methodNames(targetObj));
    }

    // Convertir args JSON al tipo de cada parámetro; mantener vivos los QVariant.
    QList<QVariant> store;
    store.reserve(args.size());
    QVariantList genArgsHolder;
    for (int i = 0; i < args.size(); ++i) {
        QVariant v = args.at(i).toVariant();
        const int t = target.parameterType(i);
        if (t != QMetaType::UnknownType && v.metaType().id() != t)
            v.convert(QMetaType(t));
        store.append(v);
    }

    // IMPORTANTE: mantener vivos los nombres de tipo; QGenericArgument guarda el
    // puntero const char*, no copia. Un QByteArray temporal dejaría un dangling.
    QList<QByteArray> typeNames;
    typeNames.reserve(store.size());
    for (int i = 0; i < store.size(); ++i)
        typeNames.append(target.parameterTypeName(i));

    QGenericArgument ga[8];
    for (int i = 0; i < store.size() && i < 8; ++i)
        ga[i] = QGenericArgument(typeNames[i].constData(), store[i].constData());

    // Tipo de retorno.
    const int retType = target.returnType();
    QVariant ret;
    bool invoked = false;
    if (retType == QMetaType::Void || retType == QMetaType::UnknownType) {
        invoked = target.invoke(targetObj, Qt::DirectConnection,
                                ga[0], ga[1], ga[2], ga[3], ga[4], ga[5], ga[6], ga[7]);
    } else {
        ret = QVariant(QMetaType(retType));
        const QByteArray retName = target.typeName();
        QGenericReturnArgument rga(retName.constData(), ret.data());
        invoked = target.invoke(targetObj, Qt::DirectConnection, rga,
                                ga[0], ga[1], ga[2], ga[3], ga[4], ga[5], ga[6], ga[7]);
    }
    if (!invoked)
        return "{\"error\":\"invoke falló\"}";

    if (ok) *ok = true;
    QJsonObject out{{QStringLiteral("ok"), true}};
    if (ret.isValid()) out[QStringLiteral("result")] = QJsonValue::fromVariant(ret);
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}
