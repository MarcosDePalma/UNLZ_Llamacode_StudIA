#pragma once
#include "StudiaIndex.h"
#include "StudiaPrompt.h"
#include "StudiaSessionStore.h"

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

// Fachada de StudIA hacia QML. Orquesta las tres piezas del modulo y mantiene
// el streaming contra el llama-server:
//
//   StudiaIndex        de donde sale la documentacion (busqueda BM25)
//   StudiaPrompt       que se le dice al modelo (reglas, modos, contexto)
//   StudiaSessionStore una conversacion por materia, persistida
//
// La materia es OBLIGATORIA: sin materia elegida no se puede preguntar. Cada
// materia tiene su propia conversacion, para que no se mezclen conceptos ni
// contexto entre asignaturas.
//
// No lanza procesos: usa el `llama-server` que ya levanto LlamaCode. La URL la
// setea QML desde `App.serverBaseUrl`.
class StudiaController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString  rutaIndice   READ rutaIndice                        NOTIFY indiceChanged)
    Q_PROPERTY(bool     indiceListo  READ indiceListo                       NOTIFY indiceChanged)
    Q_PROPERTY(QString  errorIndice  READ errorIndice                       NOTIFY indiceChanged)
    Q_PROPERTY(QVariantMap estadisticas READ estadisticas                   NOTIFY indiceChanged)
    // Contadores de la materia activa (no del corpus entero).
    Q_PROPERTY(QVariantMap estadisticasMateria READ estadisticasMateria     NOTIFY materiaChanged)
    Q_PROPERTY(QStringList materias   READ materias                         NOTIFY indiceChanged)
    // Bibliografia propia: indice APARTE, nunca se mezcla con el de la cátedra.
    Q_PROPERTY(QVariantMap estadisticasPropias READ estadisticasPropias     NOTIFY bibliotecaChanged)
    Q_PROPERTY(bool     adjuntando   READ adjuntando                        NOTIFY bibliotecaChanged)
    Q_PROPERTY(QString  ultimoAdjunto READ ultimoAdjunto                    NOTIFY bibliotecaChanged)
    // Cuántos quedan en la cola de adjuntos (0 = ninguno pendiente).
    Q_PROPERTY(int      adjuntosPendientes READ adjuntosPendientes          NOTIFY bibliotecaChanged)
    // Documentos que subió el estudiante para la materia activa.
    Q_PROPERTY(QVariantList bibliografiaPropia READ bibliografiaPropia      NOTIFY bibliotecaChanged)
    // Materia activa. Cambiarla conmuta de conversacion (o crea una nueva).
    Q_PROPERTY(QString  materia      READ materia      WRITE setMateria     NOTIFY materiaChanged)
    Q_PROPERTY(bool     materiaElegida READ materiaElegida                  NOTIFY materiaChanged)
    Q_PROPERTY(QString  tituloSesion READ tituloSesion                      NOTIFY materiaChanged)
    Q_PROPERTY(QVariantList sesiones READ sesiones                          NOTIFY sesionesChanged)
    // Modo de tutor activo ("libre", "resumen", "flashcards", ...).
    Q_PROPERTY(QString  modo         READ modo         WRITE setModo        NOTIFY modoChanged)
    Q_PROPERTY(QVariantList modos    READ modos                             CONSTANT)
    Q_PROPERTY(QString  serverUrl    READ serverUrl    WRITE setServerUrl   NOTIFY serverUrlChanged)
    Q_PROPERTY(QString  modelo       READ modelo       WRITE setModelo      NOTIFY serverUrlChanged)
    Q_PROPERTY(QVariantList mensajes READ mensajes                          NOTIFY mensajesChanged)
    Q_PROPERTY(bool     generando    READ generando                         NOTIFY generandoChanged)
    Q_PROPERTY(int      fragmentosK  READ fragmentosK  WRITE setFragmentosK NOTIFY ajustesChanged)
    // Expuesto para recalibrarlo con otro corpus sin recompilar
    // (ver StudiaIndex::setUmbralAbstencion).
    Q_PROPERTY(double   umbralAbstencion READ umbralAbstencion WRITE setUmbralAbstencion NOTIFY ajustesChanged)

public:
    explicit StudiaController(QObject *parent = nullptr);
    ~StudiaController() override;

    static QString fraseAbstencion() { return StudiaPrompt::fraseAbstencion(); }

    QString rutaIndice() const { return m_index.rutaDb(); }
    bool indiceListo() const { return m_index.abierto(); }
    QString errorIndice() const { return m_errorIndice; }
    QVariantMap estadisticas() const { return m_index.estadisticas(); }
    QVariantMap estadisticasMateria() const { return m_index.estadisticas(m_materia); }
    QStringList materias() const { return m_index.materias(); }
    QVariantMap estadisticasPropias() const { return m_propio.estadisticas(m_materia); }
    bool adjuntando() const { return m_procAdjunto != nullptr; }
    QString ultimoAdjunto() const { return m_ultimoAdjunto; }
    int adjuntosPendientes() const { return int(m_colaAdjuntos.size()); }
    QVariantList bibliografiaPropia() const { return m_propio.documentos(m_materia); }

    QString materia() const { return m_materia; }
    void setMateria(const QString &m);
    bool materiaElegida() const { return !m_materia.trimmed().isEmpty(); }
    QString tituloSesion() const { return StudiaSessionStore::tituloDe(m_materia); }
    QVariantList sesiones() const { return m_sesiones.paraQml(); }

    QString modo() const { return m_modo; }
    void setModo(const QString &m);
    QVariantList modos() const { return StudiaPrompt::modosParaQml(); }

    QString serverUrl() const { return m_serverUrl; }
    void setServerUrl(const QString &u);
    QString modelo() const { return m_modelo; }
    void setModelo(const QString &m);
    QVariantList mensajes() const;
    bool generando() const { return m_reply != nullptr; }
    int fragmentosK() const { return m_k; }
    void setFragmentosK(int k);
    double umbralAbstencion() const { return m_index.umbralAbstencion(); }
    void setUmbralAbstencion(double u);

    // Indice
    Q_INVOKABLE bool abrirIndice(const QString &dbPath);
    Q_INVOKABLE QString rutaGuardada() const;
    Q_INVOKABLE QString elegirIndice();

    // Conversacion
    Q_INVOKABLE void preguntar(const QString &texto);
    Q_INVOKABLE void detener();
    Q_INVOKABLE void limpiar();                            // vacia la sesion actual
    Q_INVOKABLE void borrarSesion(const QString &materia);
    // Busqueda cruda, sin modelo. Para inspeccionar el indice y para medir la
    // calidad de recuperacion en la evaluacion.
    Q_INVOKABLE QVariantList buscar(const QString &consulta, int k = 6) const;
    Q_INVOKABLE bool abrirDocumento(const QString &ruta) const;
    // Texto con el que la UI prellena la barra al elegir un modo del menu.
    Q_INVOKABLE QString prefijoDeModo(const QString &idModo) const;

    // Bibliografia propia (boton del clip). Va a un indice SEPARADO del de la
    // carpeta DATA: lo que sube el estudiante nunca se mezcla con el material
    // de la catedra, y regenerar uno no pisa al otro.
    // Dialogo de seleccion MULTIPLE; lista vacia si se cancela.
    Q_INVOKABLE QStringList elegirArchivos();
    // Encola los archivos y los procesa de a uno (el ingestor escribe en la
    // misma base, no pueden ir en paralelo).
    Q_INVOKABLE void adjuntarBibliografia(const QStringList &rutas);
    // Saca un documento de la bibliografia propia. Sólo toca ese índice; el de
    // la carpeta DATA nunca se modifica.
    Q_INVOKABLE void quitarBibliografia(const QString &ruta);
    Q_INVOKABLE QString rutaIndicePropio() const;
    // Ruta del ingestor Python. Vacia si no se encuentra el script.
    Q_INVOKABLE QString rutaIngestor() const;

    // Fuentes para la UI, AGRUPADAS por documento: si un mismo PDF aportó las
    // páginas 11, 14 y 16 se muestra una sola entrada "doc · pág. 11, 14, 16"
    // en vez de repetir el nombre del archivo tres veces. Cada entrada trae:
    //   refs      "1, 3"        → los [n] del prompt que salieron de ahí
    //   documento, materia, ruta
    //   paginas   "11, 14, 16"  → vacío si el formato no tiene páginas
    static QVariantList agruparFuentes(const QVector<StudiaFragmento> &frags);

signals:
    void indiceChanged();
    void bibliotecaChanged();
    void materiaChanged();
    void sesionesChanged();
    void modoChanged();
    void serverUrlChanged();
    void mensajesChanged();
    void generandoChanged();
    void ajustesChanged();
    // Actualizacion incremental del mensaje `indice` durante el streaming, para
    // que la UI refresque una sola burbuja y no reconstruya la lista entera.
    void textoParcial(int indice, const QString &contenido, const QVariantList &bloques);
    void errorOcurrido(const QString &mensaje);

private:
    StudiaSesion *sesionActual();
    void agregarMensaje(const QString &rol, const QString &contenido,
                        const QVariantList &fuentes = {}, bool escribiendo = false,
                        const QString &modo = QString());
    void generar(const QString &promptSistema, const QString &promptUsuario);
    void cerrarStream(bool ok, const QString &err);
    // Ultimos turnos de la sesion actual, recortados, para darle contexto al
    // modelo sin inflar el prompt.
    QVector<StudiaPrompt::Turno> historialReciente() const;

    // Abre (o crea) el indice de bibliografia propia si hay algo que abrir.
    void refrescarIndicePropio();
    // Lanza el ingestor Python. `queHace` describe la accion para el mensaje de
    // error ("agregar «x.pdf»"). Comun a adjuntar y quitar.
    void correrIngestor(const QStringList &args, const QString &queHace);
    // Toma el siguiente archivo de la cola de adjuntos, si hay.
    void procesarSiguienteAdjunto();
    // Reparte los k fragmentos entre catedra y bibliografia propia.
    QVector<StudiaFragmento> recuperar(const QString &consulta) const;

    StudiaIndex        m_index;    // corpus de la catedra (solo lectura)
    StudiaIndex        m_propio;   // bibliografia que subio el estudiante
    StudiaSessionStore m_sesiones;
    QString            m_errorIndice;
    QString            m_ultimoAdjunto;
    QStringList        m_colaAdjuntos;
    QProcess          *m_procAdjunto = nullptr;
    QString            m_materia;
    QString            m_modo = QStringLiteral("libre");
    QString            m_serverUrl;
    QString            m_modelo = QStringLiteral("studia");
    int                m_k = 6;
    int                m_idxRespuesta = -1;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply         *m_reply = nullptr;
    QByteArray             m_sseBuf;
    QString                m_acumulado;
};
