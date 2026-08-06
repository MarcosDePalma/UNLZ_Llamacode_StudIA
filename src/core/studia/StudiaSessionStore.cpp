#include "StudiaSessionStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>

namespace {

QString directorio()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                      + QStringLiteral("/studia");
    QDir().mkpath(d);
    return d;
}

}  // namespace


QVariantMap StudiaSesion::toMap() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("materia"), materia},
        {QStringLiteral("titulo"), titulo},
        {QStringLiteral("creada"), creada},
        {QStringLiteral("usada"), usada},
        {QStringLiteral("mensajes"), int(mensajes.size())},
    };
}

QString StudiaSessionStore::tituloDe(const QString &materia)
{
    const QString m = materia.trimmed();
    return m.isEmpty() ? QStringLiteral("StudIA")
                       : QStringLiteral("StudIA: %1").arg(m);
}

QString StudiaSessionStore::rutaArchivo() const
{
    return directorio() + QStringLiteral("/sesiones.json");
}

void StudiaSessionStore::cargar()
{
    if (m_cargado)
        return;
    m_cargado = true;
    QFile f(rutaArchivo());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        StudiaSesion s;
        s.id      = o.value(QStringLiteral("id")).toString();
        s.materia = o.value(QStringLiteral("materia")).toString();
        s.titulo  = o.value(QStringLiteral("titulo")).toString();
        s.creada  = o.value(QStringLiteral("creada")).toDouble();
        s.usada   = o.value(QStringLiteral("usada")).toDouble();
        if (s.materia.isEmpty())
            continue;   // sin materia la sesion no tiene identidad: se descarta
        if (s.titulo.isEmpty())
            s.titulo = tituloDe(s.materia);
        for (const QJsonValue &mv : o.value(QStringLiteral("mensajes")).toArray())
            s.mensajes.append(mv.toObject().toVariantMap());
        m_sesiones.append(s);
    }
}

void StudiaSessionStore::guardar() const
{
    QJsonArray arr;
    for (const StudiaSesion &s : m_sesiones) {
        QJsonArray msgs;
        for (const QVariant &m : s.mensajes) {
            QVariantMap vm = m.toMap();
            // El flag de "escribiendo" es estado de UI en vuelo; no se persiste
            // como true o al reabrir quedaria una burbuja tipeando para siempre.
            vm[QStringLiteral("escribiendo")] = false;
            msgs.append(QJsonObject::fromVariantMap(vm));
        }
        arr.append(QJsonObject{
            {QStringLiteral("id"), s.id},
            {QStringLiteral("materia"), s.materia},
            {QStringLiteral("titulo"), s.titulo},
            {QStringLiteral("creada"), s.creada},
            {QStringLiteral("usada"), s.usada},
            {QStringLiteral("mensajes"), msgs},
        });
    }
    QFile f(rutaArchivo());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

StudiaSesion *StudiaSessionStore::buscar(const QString &materia)
{
    const QString m = materia.trimmed();
    for (StudiaSesion &s : m_sesiones)
        if (s.materia == m)
            return &s;
    return nullptr;
}

const StudiaSesion *StudiaSessionStore::buscar(const QString &materia) const
{
    const QString m = materia.trimmed();
    for (const StudiaSesion &s : m_sesiones)
        if (s.materia == m)
            return &s;
    return nullptr;
}

StudiaSesion &StudiaSessionStore::obtenerOCrear(const QString &materia)
{
    const QString m = materia.trimmed();
    if (StudiaSesion *s = buscar(m))
        return *s;
    StudiaSesion nueva;
    nueva.id      = QUuid::createUuid().toString(QUuid::WithoutBraces);
    nueva.materia = m;
    nueva.titulo  = tituloDe(m);
    nueva.creada  = double(QDateTime::currentMSecsSinceEpoch());
    nueva.usada   = nueva.creada;
    m_sesiones.append(nueva);
    return m_sesiones.last();
}

bool StudiaSessionStore::borrar(const QString &materia)
{
    const QString m = materia.trimmed();
    for (int i = 0; i < m_sesiones.size(); ++i) {
        if (m_sesiones[i].materia == m) {
            m_sesiones.remove(i);
            return true;
        }
    }
    return false;
}

bool StudiaSessionStore::limpiar(const QString &materia)
{
    if (StudiaSesion *s = buscar(materia)) {
        s->mensajes.clear();
        return true;
    }
    return false;
}

QVector<StudiaSesion> StudiaSessionStore::ordenadasPorUso() const
{
    QVector<StudiaSesion> v = m_sesiones;
    std::sort(v.begin(), v.end(), [](const StudiaSesion &a, const StudiaSesion &b) {
        return a.usada > b.usada;
    });
    return v;
}

QVariantList StudiaSessionStore::paraQml() const
{
    QVariantList out;
    for (const StudiaSesion &s : ordenadasPorUso())
        out.append(s.toMap());
    return out;
}

QStringList StudiaSessionStore::ultimasPreguntas(const QString &materia, int cuantas) const
{
    QStringList out;
    const StudiaSesion *s = buscar(materia);
    if (!s || cuantas <= 0)
        return out;
    // Se recorre de atras hacia adelante: la mas reciente primero.
    for (int i = s->mensajes.size() - 1; i >= 0 && out.size() < cuantas; --i) {
        const QVariantMap m = s->mensajes.at(i).toMap();
        if (m.value(QStringLiteral("rol")).toString() != QLatin1String("usuario"))
            continue;
        const QString t = m.value(QStringLiteral("contenido")).toString().trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}
