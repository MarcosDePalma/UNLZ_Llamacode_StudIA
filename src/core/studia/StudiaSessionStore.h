#pragma once
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

// Un TEMA: una conversacion de StudIA dentro de una materia.
//
// Hay varios por materia. La idea es que el estudiante pueda preguntar sobre
// ecuaciones en un tema y sobre integrales en otro sin que se le mezclen: cada
// tema arrastra SOLO su propio historial, asi que una repregunta corta ("¿y por
// que?") se resuelve contra lo que se hablo en ese tema y no contra otro.
//
// La identidad es el `id`, no la materia: por eso pueden convivir varios de la
// misma materia. El `titulo` lo pone el sistema con la primera pregunta y el
// estudiante lo puede cambiar; `tituloAuto` recuerda si todavia es el
// automatico, para no pisar el que escribio a mano.
struct StudiaSesion {
    QString id;
    QString materia;
    QString titulo;              // nombre del tema
    bool    tituloAuto = true;   // false en cuanto el estudiante lo renombra
    double  creada = 0;          // epoch ms
    double  usada = 0;           // epoch ms del ultimo mensaje
    QVariantList mensajes;       // {rol, contenido, escribiendo, fuentes, modo}

    QVariantMap toMap() const;
};

// Persistencia de las conversaciones en AppLocalData/LlamaCode/studia/.
// Un unico JSON: son pocas y de tamano acotado.
class StudiaSessionStore
{
public:
    // Nombre de un tema recien creado, mientras no haya una respuesta que le de
    // uno. Un tema donde StudIA se abstuvo se queda con este: no aprendimos
    // nada del tema como para nombrarlo.
    static QString tituloPorDefecto();

    void cargar();                 // lee del disco (idempotente)
    void guardar() const;          // vuelca al disco
    QString rutaArchivo() const;

    StudiaSesion *porId(const QString &id);
    const StudiaSesion *porId(const QString &id) const;

    // Crea un tema nuevo en esa materia. Devuelve su id ("" si no hay materia).
    QString crear(const QString &materia);
    // Id del tema mas usado recientemente de la materia, o "" si no hay ninguno.
    QString ultimoDe(const QString &materia) const;

    bool borrar(const QString &id);
    bool limpiar(const QString &id);          // vacia los mensajes, conserva el tema
    // Renombre manual: marca el titulo como propio del estudiante. Un titulo
    // vacio no se acepta (dejaria una fila sin nombre en la lista).
    bool renombrar(const QString &id, const QString &titulo);
    // Titulo automatico. No hace nada si el estudiante ya le puso nombre a
    // mano —eso gana siempre— ni si el titulo viene vacio.
    bool titular(const QString &id, const QString &titulo);

    // Temas de una materia, del mas usado recientemente al mas viejo.
    QVector<StudiaSesion> deMateria(const QString &materia) const;
    QVariantList paraQml(const QString &materia) const;
    int cantidad() const { return m_sesiones.size(); }
    int cantidadDe(const QString &materia) const;

    // Una fila por MATERIA que tenga algun tema, de la mas usada a la mas
    // vieja: {materia, temas, mensajes, usada}. Es lo que lista el panel
    // lateral, que navega entre materias y no entre temas.
    QVariantList resumenPorMateria() const;
    // Borra TODOS los temas de una materia. Devuelve cuantos saco.
    int borrarMateria(const QString &materia);

    // Ultimas preguntas del usuario EN ESE TEMA, de la mas reciente a la mas
    // vieja (para expandir la consulta de una repregunta corta).
    QStringList ultimasPreguntas(const QString &id, int cuantas) const;

private:
    QVector<StudiaSesion> m_sesiones;
    bool m_cargado = false;
};
