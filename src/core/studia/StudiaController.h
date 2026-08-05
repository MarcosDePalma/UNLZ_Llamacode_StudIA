#pragma once
#include "StudiaIndex.h"

#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;

// Cerebro de StudIA: une el indice documental con el modelo local.
//
// Flujo de una pregunta:
//   1. Se buscan los fragmentos mas relevantes en el indice (BM25/FTS5).
//   2. Si NO hay fragmentos, se responde con la frase de abstencion y NUNCA se
//      llama al modelo. Es la garantia mas fuerte contra alucinaciones: sin
//      documentacion no hay respuesta posible.
//   3. Si los hay, se arma un prompt con los fragmentos numerados y se pide al
//      llama-server que redacte citando [n].
//
// No lanza procesos: usa el `llama-server` que ya levanto LlamaCode. La URL se
// setea desde QML con la propiedad `serverUrl` (App.serverBaseUrl).
class StudiaController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString  rutaIndice   READ rutaIndice                        NOTIFY indiceChanged)
    Q_PROPERTY(bool     indiceListo  READ indiceListo                       NOTIFY indiceChanged)
    Q_PROPERTY(QString  errorIndice  READ errorIndice                       NOTIFY indiceChanged)
    Q_PROPERTY(QVariantMap estadisticas READ estadisticas                   NOTIFY indiceChanged)
    Q_PROPERTY(QStringList materias   READ materias                         NOTIFY indiceChanged)
    Q_PROPERTY(QString  materiaFiltro READ materiaFiltro WRITE setMateriaFiltro NOTIFY materiaFiltroChanged)
    Q_PROPERTY(QString  serverUrl    READ serverUrl    WRITE setServerUrl   NOTIFY serverUrlChanged)
    Q_PROPERTY(QString  modelo       READ modelo       WRITE setModelo      NOTIFY serverUrlChanged)
    Q_PROPERTY(QVariantList mensajes READ mensajes                          NOTIFY mensajesChanged)
    Q_PROPERTY(bool     generando    READ generando                         NOTIFY generandoChanged)
    Q_PROPERTY(int      fragmentosK  READ fragmentosK  WRITE setFragmentosK NOTIFY fragmentosKChanged)
    // Expuesto para poder recalibrarlo con otro corpus sin recompilar
    // (ver StudiaIndex::setUmbralAbstencion).
    Q_PROPERTY(double   umbralAbstencion READ umbralAbstencion WRITE setUmbralAbstencion NOTIFY fragmentosKChanged)

public:
    explicit StudiaController(QObject *parent = nullptr);
    ~StudiaController() override;

    // Frase exacta con la que el sistema se abstiene. Es una constante del
    // modulo: la UI la muestra y las pruebas de evaluacion la buscan tal cual.
    static QString fraseAbstencion();

    QString rutaIndice() const { return m_index.rutaDb(); }
    bool indiceListo() const { return m_index.abierto(); }
    QString errorIndice() const { return m_errorIndice; }
    QVariantMap estadisticas() const { return m_index.estadisticas(); }
    QStringList materias() const { return m_index.materias(); }
    QString materiaFiltro() const { return m_materiaFiltro; }
    void setMateriaFiltro(const QString &m);
    QString serverUrl() const { return m_serverUrl; }
    void setServerUrl(const QString &u);
    QString modelo() const { return m_modelo; }
    void setModelo(const QString &m);
    QVariantList mensajes() const { return m_mensajes; }
    bool generando() const { return m_reply != nullptr; }
    int fragmentosK() const { return m_k; }
    void setFragmentosK(int k);
    double umbralAbstencion() const { return m_index.umbralAbstencion(); }
    void setUmbralAbstencion(double u);

    // Abre un indice y lo recuerda para la proxima sesion (QSettings).
    Q_INVOKABLE bool abrirIndice(const QString &dbPath);
    // Ultima ruta usada (QSettings). Vacio si nunca se configuro.
    Q_INVOKABLE QString rutaGuardada() const;
    // Dialogo nativo para elegir el archivo .db. Devuelve "" si se cancela.
    Q_INVOKABLE QString elegirIndice();

    // Hace la pregunta: recupera, arma el prompt y dispara la generacion.
    Q_INVOKABLE void preguntar(const QString &texto);
    Q_INVOKABLE void detener();
    Q_INVOKABLE void limpiar();
    // Busqueda cruda, sin modelo. Sirve para inspeccionar que recupera el
    // indice y para medir la calidad de recuperacion en la evaluacion.
    Q_INVOKABLE QVariantList buscar(const QString &consulta, int k = 6) const;
    // Abre el documento original con la aplicacion asociada del sistema.
    Q_INVOKABLE bool abrirDocumento(const QString &ruta) const;

    // --- Puro, testeable sin red ni base ---
    // Prompt de sistema con las reglas del asistente academico.
    static QString promptSistema();
    // Bloque de usuario: fragmentos numerados + la pregunta.
    static QString construirPrompt(const QString &pregunta,
                                   const QVector<StudiaFragmento> &frags);

    // Fuentes para la UI, AGRUPADAS por documento: si un mismo PDF aportó las
    // páginas 11, 14 y 16 se muestra una sola entrada "doc · pág. 11, 14, 16"
    // en vez de repetir el nombre del archivo tres veces. Cada entrada trae:
    //   refs      "1, 3"        → los [n] del prompt que salieron de ahí
    //   documento, materia, ruta
    //   paginas   "11, 14, 16"  → vacío si el formato no tiene páginas
    static QVariantList agruparFuentes(const QVector<StudiaFragmento> &frags);

signals:
    void indiceChanged();
    void materiaFiltroChanged();
    void serverUrlChanged();
    void mensajesChanged();
    void generandoChanged();
    void fragmentosKChanged();
    // Actualizacion incremental del mensaje `indice` durante el streaming, para
    // que la UI refresque una sola burbuja y no reconstruya la lista entera.
    void textoParcial(int indice, const QString &contenido);
    void errorOcurrido(const QString &mensaje);

private:
    void agregarMensaje(const QString &rol, const QString &contenido,
                        const QVariantList &fuentes = {}, bool escribiendo = false);
    void generar(const QString &promptUsuario);
    void cerrarStream(bool ok, const QString &err);

    StudiaIndex   m_index;
    QString       m_errorIndice;
    QString       m_materiaFiltro;
    QString       m_serverUrl;
    QString       m_modelo = QStringLiteral("studia");
    int           m_k = 6;
    QVariantList  m_mensajes;      // {rol, contenido, escribiendo, fuentes}
    int           m_idxRespuesta = -1;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply         *m_reply = nullptr;
    QByteArray             m_sseBuf;
    QString                m_acumulado;
};
