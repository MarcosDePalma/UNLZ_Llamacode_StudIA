#pragma once
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

// Una conversacion de StudIA. Hay UNA por materia: al cambiar de materia en el
// selector se cambia de sesion, para que no se mezclen los conceptos ni el
// contexto de dos asignaturas distintas.
struct StudiaSesion {
    QString id;
    QString materia;      // clave de negocio: identifica la sesion
    QString titulo;       // "StudIA: Economía 1"
    double  creada = 0;   // epoch ms
    double  usada = 0;    // epoch ms del ultimo mensaje
    QVariantList mensajes;   // {rol, contenido, escribiendo, fuentes, modo}

    QVariantMap toMap() const;
};

// Persistencia de las conversaciones en AppLocalData/LlamaCode/studia/.
// Un unico JSON: son pocas sesiones (una por materia) y de tamano acotado.
class StudiaSessionStore
{
public:
    // Titulo canonico de la sesion de una materia.
    static QString tituloDe(const QString &materia);

    void cargar();                 // lee del disco (idempotente)
    void guardar() const;          // vuelca al disco
    QString rutaArchivo() const;

    // Devuelve la sesion de esa materia, creandola si no existia.
    StudiaSesion &obtenerOCrear(const QString &materia);
    // Puntero a la sesion de la materia, o nullptr si no existe.
    StudiaSesion *buscar(const QString &materia);
    const StudiaSesion *buscar(const QString &materia) const;
    // Borra la conversacion de una materia (la sesion desaparece de la lista).
    bool borrar(const QString &materia);
    // Vacia los mensajes pero conserva la sesion.
    bool limpiar(const QString &materia);

    // Sesiones ordenadas de la mas usada recientemente a la mas vieja.
    QVector<StudiaSesion> ordenadasPorUso() const;
    QVariantList paraQml() const;
    int cantidad() const { return m_sesiones.size(); }

    // Ultimas preguntas del usuario en esa materia, de la mas reciente a la mas
    // vieja (para expandir la consulta de una repregunta corta).
    QStringList ultimasPreguntas(const QString &materia, int cuantas) const;

private:
    QVector<StudiaSesion> m_sesiones;
    bool m_cargado = false;
};
