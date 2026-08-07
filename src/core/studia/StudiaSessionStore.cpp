#include "StudiaSessionStore.h"

#include "StudiaTexto.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
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

// Primera respuesta de StudIA en la sesion, o "" si todavia no hay ninguna.
QString primeraRespuesta(const StudiaSesion &s)
{
    for (const QVariant &v : s.mensajes) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("rol")).toString() == QLatin1String("asistente")) {
            const QString t = m.value(QStringLiteral("contenido")).toString().trimmed();
            if (!t.isEmpty())
                return t;
        }
    }
    return QString();
}

}  // namespace


QVariantMap StudiaSesion::toMap() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("materia"), materia},
        {QStringLiteral("titulo"), titulo},
        {QStringLiteral("tituloAuto"), tituloAuto},
        {QStringLiteral("creada"), creada},
        {QStringLiteral("usada"), usada},
        {QStringLiteral("mensajes"), int(mensajes.size())},
    };
}

QString StudiaSessionStore::tituloPorDefecto()
{
    return QStringLiteral("Tema nuevo");
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
            continue;   // sin materia el tema no tiene donde vivir: se descarta
        if (s.id.isEmpty())
            s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        for (const QJsonValue &mv : o.value(QStringLiteral("mensajes")).toArray())
            s.mensajes.append(mv.toObject().toVariantMap());

        // Migracion del formato viejo (una sesion por materia, titulada
        // "StudIA: <materia>"). Ese titulo no distingue nada cuando hay varios
        // temas, asi que se reemplaza por el titulo de la primera respuesta,
        // igual que se hace de ahora en mas.
        if (o.contains(QStringLiteral("tituloAuto"))) {
            s.tituloAuto = o.value(QStringLiteral("tituloAuto")).toBool(true);
        } else {
            s.tituloAuto = true;
            const QString t = StudiaTexto::tituloDeRespuesta(primeraRespuesta(s));
            s.titulo = t.isEmpty() ? tituloPorDefecto() : t;
        }
        if (s.titulo.isEmpty())
            s.titulo = tituloPorDefecto();
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
            {QStringLiteral("tituloAuto"), s.tituloAuto},
            {QStringLiteral("creada"), s.creada},
            {QStringLiteral("usada"), s.usada},
            {QStringLiteral("mensajes"), msgs},
        });
    }
    QFile f(rutaArchivo());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

StudiaSesion *StudiaSessionStore::porId(const QString &id)
{
    if (id.isEmpty())
        return nullptr;
    for (StudiaSesion &s : m_sesiones)
        if (s.id == id)
            return &s;
    return nullptr;
}

const StudiaSesion *StudiaSessionStore::porId(const QString &id) const
{
    if (id.isEmpty())
        return nullptr;
    for (const StudiaSesion &s : m_sesiones)
        if (s.id == id)
            return &s;
    return nullptr;
}

QString StudiaSessionStore::crear(const QString &materia)
{
    const QString m = materia.trimmed();
    if (m.isEmpty())
        return QString();
    StudiaSesion nueva;
    nueva.id      = QUuid::createUuid().toString(QUuid::WithoutBraces);
    nueva.materia = m;
    nueva.titulo  = tituloPorDefecto();
    nueva.creada  = double(QDateTime::currentMSecsSinceEpoch());
    nueva.usada   = nueva.creada;
    m_sesiones.append(nueva);
    return nueva.id;
}

QString StudiaSessionStore::ultimoDe(const QString &materia) const
{
    const QVector<StudiaSesion> v = deMateria(materia);
    return v.isEmpty() ? QString() : v.first().id;
}

bool StudiaSessionStore::borrar(const QString &id)
{
    for (int i = 0; i < m_sesiones.size(); ++i) {
        if (m_sesiones[i].id == id) {
            m_sesiones.remove(i);
            return true;
        }
    }
    return false;
}

bool StudiaSessionStore::limpiar(const QString &id)
{
    if (StudiaSesion *s = porId(id)) {
        s->mensajes.clear();
        // Vaciar el tema lo deja como recien creado: el titulo vuelve a ser
        // automatico para que la proxima pregunta lo renombre.
        if (s->tituloAuto)
            s->titulo = tituloPorDefecto();
        return true;
    }
    return false;
}

bool StudiaSessionStore::renombrar(const QString &id, const QString &titulo)
{
    const QString t = titulo.trimmed();
    if (t.isEmpty())
        return false;
    StudiaSesion *s = porId(id);
    if (!s || s->titulo == t)
        return false;
    s->titulo = t;
    s->tituloAuto = false;
    return true;
}

bool StudiaSessionStore::titular(const QString &id, const QString &titulo)
{
    const QString t = titulo.trimmed();
    if (t.isEmpty())
        return false;          // sin titulo se conserva el "Tema nuevo"
    StudiaSesion *s = porId(id);
    if (!s || !s->tituloAuto || s->titulo == t)
        return false;
    s->titulo = t;
    return true;
}

QVariantList StudiaSessionStore::resumenPorMateria() const
{
    // Se agrupa conservando, por materia, el uso mas reciente de sus temas.
    QVector<QVariantMap> filas;
    for (const StudiaSesion &s : m_sesiones) {
        int i = 0;
        for (; i < filas.size(); ++i)
            if (filas[i].value(QStringLiteral("materia")).toString() == s.materia)
                break;
        if (i == filas.size())
            filas.append(QVariantMap{{QStringLiteral("materia"), s.materia},
                                     {QStringLiteral("temas"), 0},
                                     {QStringLiteral("mensajes"), 0},
                                     {QStringLiteral("usada"), 0.0}});
        filas[i][QStringLiteral("temas")] =
            filas[i].value(QStringLiteral("temas")).toInt() + 1;
        filas[i][QStringLiteral("mensajes")] =
            filas[i].value(QStringLiteral("mensajes")).toInt() + int(s.mensajes.size());
        if (s.usada > filas[i].value(QStringLiteral("usada")).toDouble())
            filas[i][QStringLiteral("usada")] = s.usada;
    }
    std::sort(filas.begin(), filas.end(),
              [](const QVariantMap &a, const QVariantMap &b) {
                  return a.value(QStringLiteral("usada")).toDouble()
                       > b.value(QStringLiteral("usada")).toDouble();
              });
    QVariantList out;
    for (const QVariantMap &f : filas)
        out.append(f);
    return out;
}

int StudiaSessionStore::borrarMateria(const QString &materia)
{
    const QString m = materia.trimmed();
    int n = 0;
    for (int i = m_sesiones.size() - 1; i >= 0; --i) {
        if (m_sesiones[i].materia == m) {
            m_sesiones.remove(i);
            ++n;
        }
    }
    return n;
}

QVector<StudiaSesion> StudiaSessionStore::deMateria(const QString &materia) const
{
    const QString m = materia.trimmed();
    QVector<StudiaSesion> v;
    for (const StudiaSesion &s : m_sesiones)
        if (s.materia == m)
            v.append(s);
    std::sort(v.begin(), v.end(), [](const StudiaSesion &a, const StudiaSesion &b) {
        return a.usada > b.usada;
    });
    return v;
}

int StudiaSessionStore::cantidadDe(const QString &materia) const
{
    const QString m = materia.trimmed();
    int n = 0;
    for (const StudiaSesion &s : m_sesiones)
        if (s.materia == m)
            ++n;
    return n;
}

QVariantList StudiaSessionStore::paraQml(const QString &materia) const
{
    QVariantList out;
    for (const StudiaSesion &s : deMateria(materia))
        out.append(s.toMap());
    return out;
}

QStringList StudiaSessionStore::ultimasPreguntas(const QString &id, int cuantas) const
{
    QStringList out;
    const StudiaSesion *s = porId(id);
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
