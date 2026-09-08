#pragma once
#include "StudiaEmbed.h"
#include "StudiaEmbedServer.h"
#include "StudiaHerramientas.h"
#include "StudiaIndex.h"
#include "StudiaPlot.h"
#include "StudiaPrompt.h"
#include "StudiaSessionStore.h"
#include "StudiaTexto.h"

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
    // Renderizador de los bloques ```grafico. Se expone acá para no sumar otra
    // propiedad global en main.cpp: es parte del módulo.
    Q_PROPERTY(StudiaPlot *graficos READ graficos                           CONSTANT)
    // Búsqueda semántica: URL del servidor de embeddings y estado del índice.
    Q_PROPERTY(QString  urlEmbeddings READ urlEmbeddings WRITE setUrlEmbeddings NOTIFY semanticaChanged)
    Q_PROPERTY(QVariantMap estadoSemantico READ estadoSemantico             NOTIFY semanticaChanged)
    Q_PROPERTY(bool     semanticaActiva READ semanticaActiva                NOTIFY semanticaChanged)
    // Materia activa. Cambiarla conmuta de conversacion (o crea una nueva).
    Q_PROPERTY(QString  materia      READ materia      WRITE setMateria     NOTIFY materiaChanged)
    Q_PROPERTY(bool     materiaElegida READ materiaElegida                  NOTIFY materiaChanged)
    // Temas de la materia actual: varias conversaciones que no se mezclan.
    Q_PROPERTY(QVariantList temas    READ temas                             NOTIFY sesionesChanged)
    Q_PROPERTY(QString  temaId       READ temaId                            NOTIFY sesionesChanged)
    Q_PROPERTY(QString  tituloTema   READ tituloTema                        NOTIFY sesionesChanged)
    // Un tema sin preguntas todavia no es un tema: abrir otro dejaria dos
    // vacios y ninguno con nombre. Hasta que no se pregunte algo, no se puede
    // crear el siguiente.
    Q_PROPERTY(bool     puedeCrearTema READ puedeCrearTema                  NOTIFY sesionesChanged)
    // Materias que tienen algun chat. El panel lateral navega entre MATERIAS;
    // entre los temas de una materia se navega con el boton "Temas".
    Q_PROPERTY(QVariantList materiasConChats READ materiasConChats          NOTIFY sesionesChanged)
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
    StudiaPlot *graficos() { return &m_graficos; }
    QString urlEmbeddings() const { return m_embed.url(); }
    void setUrlEmbeddings(const QString &u);
    // Ruta del corpus con el que trabajar. Primero la carpeta DATA_StudIA que
    // este al lado del indice abierto —la forma recomendada de instalar—; si no,
    // la empaquetada junto al ejecutable, y por ultimo la que se uso al indexar.
    QString carpetaCorpus() const;

    // Carpeta de documentos que corresponde a un indice: la DATA_StudIA hermana
    // del .db. Estatica y pura (no toca disco) para poder testearla.
    static QString carpetaJuntoAlIndice(const QString &dbPath);

    // Herramientas externas: cuales hay y que funcion habilita cada una. Sin
    // esto se apagaban en silencio y el estudiante no tenia como enterarse.
    Q_INVOKABLE QVariantList herramientas() const
    { return StudiaHerramientas::paraQml(); }
    Q_INVOKABLE int herramientasFaltantes() const
    { return StudiaHerramientas::faltantes(); }
    // Abre el instalador que las consigue. false si no se encuentra el script.
    Q_INVOKABLE bool instalarHerramientas() const;
    // Servidor que se va a usar realmente. Si no se configuró uno aparte, se
    // usa el MISMO llama-server del chat: alcanza con arrancarlo con
    // --embeddings y no hace falta levantar un segundo proceso. Un servidor
    // dedicado con un modelo de embeddings da mejor calidad, pero es opcional.
    QString urlEmbeddingsEfectiva() const {
        return m_embed.url().isEmpty() ? m_serverUrl : m_embed.url();
    }
    // ¿Se está usando búsqueda semántica? Necesita índice vectorizado Y servidor.
    bool semanticaActiva() const {
        return !urlEmbeddingsEfectiva().isEmpty() && m_index.tieneVectores();
    }
    QVariantMap estadoSemantico() const;

    QString materia() const { return m_materia; }
    void setMateria(const QString &m);
    bool materiaElegida() const { return !m_materia.trimmed().isEmpty(); }
    QVariantList temas() const { return m_sesiones.paraQml(m_materia); }
    QString temaId() const { return m_temaId; }
    QString tituloTema() const;
    bool puedeCrearTema() const;
    QVariantList materiasConChats() const { return m_sesiones.resumenPorMateria(); }

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
    // Indice que viaja con la aplicacion (<appdir>/StudIA/studia.db). "" si no
    // esta. Es lo que permite que quien recibe la app no configure nada.
    static QString indiceEmpaquetado();
    Q_INVOKABLE QString elegirIndice();

    // Conversacion
    Q_INVOKABLE void preguntar(const QString &texto);
    Q_INVOKABLE void detener();
    Q_INVOKABLE void limpiar();                            // vacia el tema actual

    // Temas
    Q_INVOKABLE void nuevoTema();
    Q_INVOKABLE void abrirTema(const QString &id);
    Q_INVOKABLE void borrarTema(const QString &id);
    Q_INVOKABLE bool renombrarTema(const QString &id, const QString &titulo);
    // Saca del historial todos los temas de una materia (la ✕ del panel).
    Q_INVOKABLE void borrarChatsDeMateria(const QString &materia);
    // Busqueda cruda, sin modelo. Para inspeccionar el indice y para medir la
    // calidad de recuperacion en la evaluacion.
    Q_INVOKABLE QVariantList buscar(const QString &consulta, int k = 6) const;
    Q_INVOKABLE bool abrirDocumento(const QString &ruta) const;
    // Donde esta REALMENTE el documento. El indice guarda rutas absolutas de la
    // maquina que lo genero; si el corpus viaja con la app, hay que reubicarlo.
    // "" si no se encuentra en ningun lado.
    Q_INVOKABLE QString reubicarDocumento(const QString &ruta) const;
    // Por que no se puede abrir, en castellano y para mostrar tal cual. "" si si
    // se puede. Distingue dos casos que al estudiante le importan de manera muy
    // distinta: que esta copia no traiga los documentos originales (normal, el
    // texto citado igual esta en el indice) o que ese archivo puntual falte.
    Q_INVOKABLE QString motivoDocumento(const QString &ruta) const;
    // Texto con el que la UI prellena la barra al elegir un modo del menu.
    Q_INVOKABLE QString prefijoDeModo(const QString &idModo) const;

    // ¿Este modo entrega la consigna con las respuestas plegadas? Decide si la
    // burbuja se muestra recien al terminar y si ofrece desplegarlas.
    Q_INVOKABLE bool modoOcultaRespuestas(const QString &idModo) const
    { return StudiaPrompt::modoPorId(idModo).ocultaRespuestas; }
    // Despliega la segunda parte de un mensaje. NO consulta al modelo: las
    // respuestas ya vinieron con la consigna, esto sólo las muestra.
    Q_INVOKABLE void revelarRespuestas(int indice);

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

    // Exporta las flashcards de una respuesta a un .txt separado por
    // tabulaciones, que Anki importa de fábrica. Devuelve la ruta escrita o ""
    // si la respuesta no traía tarjetas reconocibles o se canceló el diálogo.
    Q_INVOKABLE QString exportarFlashcards(const QString &respuesta);
    // ¿Esta respuesta tiene tarjetas exportables? Para mostrar u ocultar el
    // boton. Se pasa el modo porque una autoevaluacion tiene la misma forma
    // —preguntas y respuestas numeradas— y no son tarjetas de repaso.
    Q_INVOKABLE int contarFlashcards(const QString &respuesta,
                                     const QString &idModo = QString()) const;

    // Contenido de una burbuja como HTML con interlineado. La UI lo muestra en
    // un TextEdit para poder seleccionar un fragmento con el mouse; el HTML es
    // lo que permite conservar el interlineado, que TextEdit no tiene como
    // propiedad (ver StudiaTexto::aHtmlConInterlineado).
    Q_INVOKABLE QString htmlDe(const QString &texto, bool markdown) const
    { return StudiaTexto::aHtmlConInterlineado(texto, markdown); }

    // Fuentes para la UI, AGRUPADAS por documento: si un mismo PDF aportó las
    // páginas 11, 14 y 16 se muestra una sola entrada "doc · pág. 11, 14, 16"
    // en vez de repetir el nombre del archivo tres veces. Cada entrada trae:
    //   refs      "1, 3"        → los [n] del prompt que salieron de ahí
    //   documento, materia, ruta
    //   paginas   "11, 14, 16"  → vacío si el formato no tiene páginas
    static QVariantList agruparFuentes(const QVector<StudiaFragmento> &frags);

signals:
    void indiceChanged();
    void semanticaChanged();
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
    // Le pone nombre al tema con el titulo de la respuesta, salvo que StudIA se
    // haya abstenido: de una abstencion no se aprende de que trata el tema.
    void titularConRespuesta(const QString &respuesta);
    static bool esAbstencion(const QString &respuesta);
    // ¿En este tema ya se le pidieron al estudiante los datos del plan?
    //
    // Si StudIA ya respondio una vez en modo Plan, la proxima es la segunda
    // vuelta: el estudiante contesto y toca armarlo. Lo decide el sistema, que
    // tiene el historial, en vez de pedirle al modelo que lo deduzca.
    bool planYaPidioLosDatos() const;
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
    // Reparte los k fragmentos entre catedra y bibliografia propia. Si el vector
    // de la consulta no viene vacio, la busqueda es hibrida (lexica + semantica).
    QVector<StudiaFragmento> recuperar(const QString &consulta,
                                       const QVector<float> &vector) const;
    // Segunda mitad de preguntar(), una vez resuelto el vector de la consulta
    // (o descartado, si no hay busqueda semantica disponible).
    void continuarPregunta(const QVector<float> &vector);

    // Estado de la pregunta que espera su vector. preguntar() lo arma y
    // continuarPregunta() lo consume: entre medio hay una request HTTP.
    struct Pendiente {
        bool activo = false;
        QString pregunta;
        QString idModo;
        QString consulta;
        QVector<StudiaPrompt::Turno> historial;
        StudiaPrompt::Encuadre encuadre = StudiaPrompt::Encuadre::Autonoma;
        // Modo Plan: el estudiante ya paso sus datos, toca armar el plan.
        bool planConDatos = false;
    };
    Pendiente m_pendiente;

    StudiaIndex        m_index;    // corpus de la catedra (solo lectura)
    StudiaIndex        m_propio;   // bibliografia que subio el estudiante
    StudiaPlot         m_graficos;
    StudiaEmbed        m_embed;
    StudiaEmbedServer  m_servidorEmbed;   // lo levanta la app, ya no el .bat
    StudiaSessionStore m_sesiones;
    QString            m_errorIndice;
    QString            m_ultimoAdjunto;
    QStringList        m_colaAdjuntos;
    QProcess          *m_procAdjunto = nullptr;
    QString            m_materia;
    QString            m_temaId;      // tema abierto dentro de la materia
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
