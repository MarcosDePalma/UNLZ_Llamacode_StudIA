#include "StudiaController.h"
#include "StudiaTexto.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>
#include <QWidget>

#include <algorithm>

namespace {

// Claves de QSettings.
const char *kClaveRuta    = "studia/rutaIndice";
const char *kClaveMateria = "studia/materia";
const char *kClaveEmbed   = "studia/urlEmbeddings";
// Donde dejo los documentos originales el copiador (installer\copiar_documentos).
// Puede apuntar a un disco externo: no hace falta que esten en C:.
const char *kClaveCorpus  = "studia/carpetaDocumentos";

// Destino por defecto de ese copiador. Es corto a proposito: Windows corta las
// rutas en 260 caracteres y las carpetas de la facultad son largas. Metiendo el
// corpus adentro de la carpeta de instalacion, 76 de los 2937 documentos no
// entrarian; aca afuera quedan 2.
const char *kCorpusCorto  = "C:/StudIA_Docs";

// Cuantos turnos previos se le pasan al modelo como contexto y cuanto se
// recorta cada uno. Alcanza para que entienda una repregunta sin inflar el
// prompt (el grueso del contexto son los fragmentos).
constexpr int kTurnosContexto     = 4;
constexpr int kMaxCharsTurno      = 700;
// Cuantas preguntas previas se miran para expandir una repregunta corta.
constexpr int kPreguntasParaExpandir = 3;

// Cuanto se afloja el umbral de abstencion en los modos flexibles. Medido sobre
// el corpus real con 30 preguntas: pasar de -7 a -5 recupera una pregunta
// legitima mas y deja entrar 2 ajenas de 10. Es un intercambio aceptable en
// conversacion —donde la instruccion del prompt actua de segunda barrera— pero
// no en los modos que generan material de estudio.
constexpr double kAflojarUmbralFlexible = 2.0;

}  // namespace


StudiaController::StudiaController(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
    m_sesiones.cargar();
    m_embed.setUrl(QSettings().value(QLatin1String(kClaveEmbed)).toString());
    // La vectorización de la pregunta llega async; ahí sigue la consulta. Si
    // falla se continúa igual, con búsqueda léxica: nunca deja al usuario sin
    // respuesta por un problema del servidor de embeddings.
    connect(&m_embed, &StudiaEmbed::listo, this,
            [this](const QString &, const QVector<float> &v) { continuarPregunta(v); });
    connect(&m_embed, &StudiaEmbed::fallo, this, [this](const QString &motivo) {
        emit errorOcurrido(QStringLiteral("Búsqueda semántica no disponible (%1). "
                                          "Se usó búsqueda por palabras.").arg(motivo));
        continuarPregunta({});
    });
    // El servidor de embeddings lo levanta la app: antes habia que arrancarlo a
    // mano y al reiniciar la PC la busqueda semantica quedaba apagada sin aviso.
    connect(&m_servidorEmbed, &StudiaEmbedServer::estadoCambiado, this, [this] {
        if (m_servidorEmbed.activo() && m_embed.url().isEmpty())
            m_embed.setUrl(StudiaEmbedServer::url());
        emit ajustesChanged();
    });
    m_servidorEmbed.iniciar();

    refrescarIndicePropio();
    const QString guardada = rutaGuardada();
    if (!guardada.isEmpty() && QFileInfo::exists(guardada))
        abrirIndice(guardada);
    else if (!indiceEmpaquetado().isEmpty())
        abrirIndice(indiceEmpaquetado());
    // Restaurar la ultima materia, si sigue existiendo en el indice.
    const QString mat = QSettings().value(QLatin1String(kClaveMateria)).toString();
    if (!mat.isEmpty() && materias().contains(mat))
        setMateria(mat);
}

StudiaController::~StudiaController() { detener(); }

// ── Indice ───────────────────────────────────────────────────────────────────

QString StudiaController::rutaGuardada() const
{
    return QSettings().value(QLatin1String(kClaveRuta)).toString();
}

bool StudiaController::instalarHerramientas() const
{
    const QString bat = StudiaHerramientas::rutaInstalador();
    if (bat.isEmpty())
        return false;
    // En una consola propia: el instalador tarda, pide confirmaciones y va
    // contando lo que hace. Meterlo en un proceso silencioso escondería
    // justamente lo que el estudiante necesita ver.
    return QProcess::startDetached(QStringLiteral("cmd.exe"),
                                   {QStringLiteral("/c"), QStringLiteral("start"),
                                    QStringLiteral(""), bat},
                                   QFileInfo(bat).absolutePath());
}

QString StudiaController::indiceEmpaquetado()
{
    const QString p = StudiaEmbedServer::carpetaEmpaquetada()
                      + QStringLiteral("/studia.db");
    return QFileInfo::exists(p) ? p : QString();
}

QString StudiaController::carpetaCorpus() const
{
    // De mas especifica a mas general. La primera que exista gana.
    const QStringList candidatas = {
        // La que viaja con la app: es la que de verdad esta en esta maquina.
        StudiaEmbedServer::carpetaEmpaquetada() + QStringLiteral("/DATA_StudIA"),
        // La que eligio el copiador. Va antes que el destino por defecto porque
        // puede ser un disco externo que el estudiante prefirio no copiar.
        QSettings().value(QLatin1String(kClaveCorpus)).toString(),
        QLatin1String(kCorpusCorto),
        // La que se uso al indexar: misma maquina que genero el indice.
        m_index.corpusRaiz(),
    };
    for (const QString &c : candidatas)
        if (!c.isEmpty() && QFileInfo(c).isDir())
            return c;
    return QString();
}

bool StudiaController::abrirIndice(const QString &dbPath)
{
    const bool ok = m_index.abrir(dbPath, &m_errorIndice);
    if (ok) {
        m_errorIndice.clear();
        QSettings().setValue(QLatin1String(kClaveRuta), dbPath);
        // Si la materia activa no existe en el indice nuevo, se deselecciona:
        // preguntar sin materia valida no tiene sentido.
        if (!m_materia.isEmpty() && !m_index.materias().contains(m_materia)) {
            m_materia.clear();
            emit materiaChanged();
            emit mensajesChanged();
        }
    }
    emit indiceChanged();
    return ok;
}

QString StudiaController::elegirIndice()
{
    QWidget *padre = QApplication::activeWindow();
    return QFileDialog::getOpenFileName(
        padre, QStringLiteral("Elegí el índice de StudIA"),
        QFileInfo(rutaGuardada()).absolutePath(),
        QStringLiteral("Índice de StudIA (*.db);;Todos los archivos (*)"));
}

// ── Materia y temas ──────────────────────────────────────────────────────────

void StudiaController::setMateria(const QString &m)
{
    const QString nueva = m.trimmed();
    if (nueva == m_materia)
        return;
    // Cambiar de materia interrumpe lo que se este generando: la respuesta
    // pertenece a la conversacion anterior.
    detener();
    m_materia = nueva;
    QSettings().setValue(QLatin1String(kClaveMateria), m_materia);
    m_temaId.clear();
    if (!m_materia.isEmpty()) {
        // Se retoma el ultimo tema de la materia; si no habia ninguno se abre
        // uno vacio, que es lo mismo que ver al elegir una materia por primera
        // vez.
        m_temaId = m_sesiones.ultimoDe(m_materia);
        if (m_temaId.isEmpty())
            m_temaId = m_sesiones.crear(m_materia);
        m_sesiones.guardar();
    }
    emit sesionesChanged();
    emit materiaChanged();
    emit mensajesChanged();
}

StudiaSesion *StudiaController::sesionActual()
{
    if (m_materia.trimmed().isEmpty())
        return nullptr;
    if (StudiaSesion *s = m_sesiones.porId(m_temaId))
        return s;
    // El tema se borro o todavia no se abrio ninguno: se abre uno.
    m_temaId = m_sesiones.crear(m_materia);
    return m_sesiones.porId(m_temaId);
}

QString StudiaController::tituloTema() const
{
    const StudiaSesion *s = m_sesiones.porId(m_temaId);
    return s ? s->titulo : QStringLiteral("StudIA");
}

bool StudiaController::puedeCrearTema() const
{
    // Con la materia recien elegida el tema esta vacio: crear otro dejaria dos
    // sin nombre y sin contenido. Recien cuando el actual tiene algo, tiene
    // sentido empezar uno nuevo.
    const StudiaSesion *s = m_sesiones.porId(m_temaId);
    return s && !s->mensajes.isEmpty();
}

QVariantList StudiaController::mensajes() const
{
    const StudiaSesion *s = m_sesiones.porId(m_temaId);
    return s ? s->mensajes : QVariantList{};
}

void StudiaController::limpiar()
{
    detener();
    if (m_sesiones.limpiar(m_temaId)) {
        m_sesiones.guardar();
        emit mensajesChanged();
        emit sesionesChanged();
    }
}

void StudiaController::nuevoTema()
{
    if (m_materia.trimmed().isEmpty() || !puedeCrearTema())
        return;
    detener();
    m_temaId = m_sesiones.crear(m_materia);
    m_sesiones.guardar();
    emit sesionesChanged();
    emit mensajesChanged();
}

void StudiaController::abrirTema(const QString &id)
{
    const StudiaSesion *s = m_sesiones.porId(id);
    if (!s || id == m_temaId)
        return;
    // Cambiar de tema interrumpe lo que se este generando: la respuesta
    // pertenece al tema anterior y no debe caer en el nuevo.
    detener();
    m_temaId = id;
    if (s->materia != m_materia) {
        m_materia = s->materia;
        QSettings().setValue(QLatin1String(kClaveMateria), m_materia);
        emit materiaChanged();
    }
    emit sesionesChanged();
    emit mensajesChanged();
}

void StudiaController::borrarTema(const QString &id)
{
    const bool eraElAbierto = (id == m_temaId);
    if (eraElAbierto)
        detener();
    if (!m_sesiones.borrar(id))
        return;
    // Si se borro el que estaba abierto se pasa al siguiente de la materia, y
    // si no queda ninguno se abre uno vacio: la pagina nunca queda sin chat.
    if (eraElAbierto) {
        m_temaId = m_sesiones.ultimoDe(m_materia);
        if (m_temaId.isEmpty() && !m_materia.isEmpty())
            m_temaId = m_sesiones.crear(m_materia);
        emit mensajesChanged();
    }
    m_sesiones.guardar();
    emit sesionesChanged();
}

bool StudiaController::renombrarTema(const QString &id, const QString &titulo)
{
    if (!m_sesiones.renombrar(id, titulo))
        return false;
    m_sesiones.guardar();
    emit sesionesChanged();
    return true;
}

void StudiaController::revelarRespuestas(int indice)
{
    // Es un cambio de VISTA, no una consulta nueva: las respuestas ya están en
    // el mensaje desde que se generó. Por eso no puede fallar ni tardar.
    StudiaSesion *s = sesionActual();
    if (!s || indice < 0 || indice >= s->mensajes.size())
        return;
    QVariantMap msg = s->mensajes[indice].toMap();
    if (msg.value(QStringLiteral("respuestas")).toString().isEmpty())
        return;                       // no hay segunda parte que desplegar
    msg[QStringLiteral("respuestasVisibles")] = true;
    s->mensajes[indice] = msg;
    m_sesiones.guardar();
    emit mensajesChanged();
}

bool StudiaController::planYaPidioLosDatos() const
{
    const StudiaSesion *s = m_sesiones.porId(m_temaId);
    if (!s)
        return false;
    for (const QVariant &v : s->mensajes) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("rol")).toString() != QLatin1String("asistente"))
            continue;
        if (m.value(QStringLiteral("modo")).toString() != QLatin1String("plan"))
            continue;
        // Una respuesta vacía es una generación que falló: no cuenta como
        // haberle pedido nada.
        if (!m.value(QStringLiteral("contenido")).toString().trimmed().isEmpty())
            return true;
    }
    return false;
}

void StudiaController::titularConRespuesta(const QString &respuesta)
{
    // Si StudIA se abstuvo no hay tema del que hablar: el chat sigue siendo
    // "Tema nuevo" y la proxima respuesta con contenido lo nombra.
    if (esAbstencion(respuesta))
        return;
    m_sesiones.titular(m_temaId, StudiaTexto::tituloDeRespuesta(respuesta));
}

bool StudiaController::esAbstencion(const QString &respuesta)
{
    const QString frase = StudiaPrompt::fraseAbstencion();
    if (frase.isEmpty())
        return false;
    // La respuesta ya paso por recortarTrasAbstencion, asi que cuando el
    // sistema se abstuvo termina en la frase; puede abrir con un "Lamentablemente".
    const int i = respuesta.indexOf(frase);
    return i >= 0 && i <= StudiaTexto::kMargenAbstencion;
}

void StudiaController::borrarChatsDeMateria(const QString &materia)
{
    const QString m = materia.trimmed();
    const bool eraLaActiva = (m == m_materia);
    if (eraLaActiva)
        detener();
    if (m_sesiones.borrarMateria(m) == 0)
        return;
    m_sesiones.guardar();
    // Si se borro la materia abierta se la suelta: si no, sesionActual() le
    // crearia un tema nuevo y el chat reaparecería como si no se hubiera
    // borrado nada.
    if (eraLaActiva) {
        m_materia.clear();
        m_temaId.clear();
        QSettings().setValue(QLatin1String(kClaveMateria), QString());
        emit materiaChanged();
        emit mensajesChanged();
    }
    emit sesionesChanged();
}

// ── Ajustes ──────────────────────────────────────────────────────────────────

void StudiaController::setModo(const QString &m)
{
    const QString id = StudiaPrompt::modoPorId(m).id.isEmpty()
                           ? StudiaPrompt::idModoLibre() : m;
    if (id == m_modo)
        return;
    m_modo = id;
    emit modoChanged();
}

QString StudiaController::prefijoDeModo(const QString &idModo) const
{
    const StudiaPrompt::Modo m = StudiaPrompt::modoPorId(idModo);
    if (m.id.isEmpty() || m.id == StudiaPrompt::idModoLibre())
        return {};
    return QStringLiteral("/%1/ ").arg(m.id);
}


void StudiaController::setModelo(const QString &m)
{
    if (m == m_modelo) return;
    m_modelo = m;
    emit serverUrlChanged();
}

void StudiaController::setFragmentosK(int k)
{
    const int v = qBound(2, k, 15);
    if (v == m_k) return;
    m_k = v;
    emit ajustesChanged();
}

void StudiaController::setUmbralAbstencion(double u)
{
    if (qFuzzyCompare(u, m_index.umbralAbstencion())) return;
    m_index.setUmbralAbstencion(u);
    emit ajustesChanged();
}

QString StudiaController::reubicarDocumento(const QString &ruta) const
{
    if (ruta.isEmpty())
        return QString();
    if (QFileInfo::exists(ruta))
        return ruta;                     // esta donde dice el indice

    // No esta: el indice se generó en otra maquina, o el corpus se mudo. Se
    // recalcula la parte relativa a la raiz con la que se indexo y se busca esa
    // misma cola dentro del corpus que haya aca.
    const QString raizVieja = QDir::fromNativeSeparators(m_index.corpusRaiz());
    const QString corpus = carpetaCorpus();
    if (raizVieja.isEmpty() || corpus.isEmpty())
        return QString();

    const QString actual = QDir::fromNativeSeparators(ruta);
    if (!actual.startsWith(raizVieja, Qt::CaseInsensitive))
        return QString();
    QString rel = actual.mid(raizVieja.size());
    while (rel.startsWith(QLatin1Char('/')))
        rel.remove(0, 1);

    const QString candidato = corpus + QLatin1Char('/') + rel;
    return QFileInfo::exists(candidato) ? candidato : QString();
}

QString StudiaController::motivoDocumento(const QString &ruta) const
{
    if (ruta.isEmpty())
        return tr("Esta cita no tiene un archivo asociado.");
    if (!reubicarDocumento(ruta).isEmpty())
        return QString();                // se puede abrir

    // No aparece. Son dos situaciones distintas y conviene no confundirlas: si
    // la copia se entrego sin los documentos originales esto es lo esperado y no
    // hay nada roto; si el corpus esta pero el archivo no, ahi si falta algo.
    const QString raiz = QDir::fromNativeSeparators(m_index.corpusRaiz());
    const bool esDeCatedra = !raiz.isEmpty()
                             && QDir::fromNativeSeparators(ruta)
                                    .startsWith(raiz, Qt::CaseInsensitive);
    if (esDeCatedra && carpetaCorpus().isEmpty())
        return tr("Esta copia de StudIA no incluye los documentos originales. "
                  "El texto citado sí está en el índice: es el que se usó para "
                  "responder.");
    return tr("No se encontró el archivo. Puede que lo hayan movido o borrado.");
}

bool StudiaController::abrirDocumento(const QString &ruta) const
{
    const QString real = reubicarDocumento(ruta);
    if (real.isEmpty())
        return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(real));
}

// ── Bibliografia propia (indice aparte) ──────────────────────────────────────

QString StudiaController::rutaIndicePropio() const
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                      + QStringLiteral("/studia");
    QDir().mkpath(d);
    return d + QStringLiteral("/mi_biblioteca.db");
}

QString StudiaController::rutaIngestor() const
{
    // En desarrollo el script vive en el repo; en una instalacion, junto al exe.
    const QStringList candidatos{
        QCoreApplication::applicationDirPath() + QStringLiteral("/tools/studia/ingest.py"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/studia/ingest.py"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../../tools/studia/ingest.py"),
    };
    for (const QString &c : candidatos) {
        const QString limpio = QDir::cleanPath(c);
        if (QFileInfo::exists(limpio))
            return limpio;
    }
    return {};
}

void StudiaController::refrescarIndicePropio()
{
    const QString ruta = rutaIndicePropio();
    if (QFileInfo::exists(ruta)) {
        m_propio.abrir(ruta);          // si falla queda cerrado: no es critico
        // El gate de abstencion esta calibrado para el corpus grande; en un
        // indice de pocos fragmentos dejaria afuera todo (ver setExigirEvidencia).
        m_propio.setExigirEvidencia(false);
    }
    emit bibliotecaChanged();
}

// Lanza el ingestor Python con los argumentos dados. Centraliza la validacion
// de Python/script y el manejo del proceso, comun a adjuntar y quitar.
void StudiaController::correrIngestor(const QStringList &args, const QString &queHace)
{
    if (m_procAdjunto)
        return;
    const QString script = rutaIngestor();
    if (script.isEmpty()) {
        emit errorOcurrido(QStringLiteral("No se encontró tools/studia/ingest.py, "
                                          "que es quien maneja el índice."));
        return;
    }
    const QString py = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (py.isEmpty()) {
        emit errorOcurrido(QStringLiteral("No se encontró Python, necesario para "
                                          "procesar documentos."));
        return;
    }
    // El indice propio se abre en modo lectura para consultar; hay que cerrarlo
    // mientras el ingestor escribe y volver a abrirlo al terminar.
    m_propio.cerrar();

    m_procAdjunto = new QProcess(this);
    m_procAdjunto->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_procAdjunto, &QProcess::finished, this,
            [this, queHace](int code, QProcess::ExitStatus) {
        const QString salida = QString::fromUtf8(m_procAdjunto->readAll()).trimmed();
        m_procAdjunto->deleteLater();
        m_procAdjunto = nullptr;
        refrescarIndicePropio();
        if (code != 0) {
            const QString detalle = salida.section(QLatin1Char('\n'), -1).trimmed();
            emit errorOcurrido(QStringLiteral("No se pudo %1: %2")
                                   .arg(queHace,
                                        detalle.isEmpty() ? QStringLiteral("error desconocido")
                                                          : detalle));
        }
        m_ultimoAdjunto.clear();
        emit bibliotecaChanged();
        // Si venían varios archivos, sigue con el próximo de la cola.
        procesarSiguienteAdjunto();
    });
    m_procAdjunto->start(py, QStringList{script} + args);
    emit bibliotecaChanged();
}

void StudiaController::quitarBibliografia(const QString &ruta)
{
    if (ruta.trimmed().isEmpty() || m_procAdjunto)
        return;
    m_ultimoAdjunto = QFileInfo(ruta).fileName();
    correrIngestor({QStringLiteral("--db"), rutaIndicePropio(),
                    QStringLiteral("--quitar"), ruta},
                   QStringLiteral("quitar «%1»").arg(m_ultimoAdjunto));
}

QStringList StudiaController::elegirArchivos()
{
    QWidget *padre = QApplication::activeWindow();
    return QFileDialog::getOpenFileNames(
        padre, QStringLiteral("Elegí los archivos a agregar a tu bibliografía"),
        QString(),
        QStringLiteral("Documentos (*.pdf *.docx *.pptx *.xlsx *.txt *.md);;"
                       "Todos los archivos (*)"));
}

void StudiaController::adjuntarBibliografia(const QStringList &rutas)
{
    if (rutas.isEmpty())
        return;
    if (!materiaElegida()) {
        emit errorOcurrido(QStringLiteral("Elegí primero la materia a la que "
                                          "pertenece esta bibliografía."));
        return;
    }
    QStringList validos;
    QStringList faltantes;
    for (const QString &r : rutas) {
        if (!r.trimmed().isEmpty() && QFileInfo::exists(r))
            validos << r;
        else if (!r.trimmed().isEmpty())
            faltantes << QFileInfo(r).fileName();
    }
    if (!faltantes.isEmpty())
        emit errorOcurrido(QStringLiteral("No se encontraron: %1")
                               .arg(faltantes.join(QStringLiteral(", "))));
    if (validos.isEmpty())
        return;

    // El ingestor escribe en la misma base: los archivos van de a uno.
    m_colaAdjuntos += validos;
    emit bibliotecaChanged();
    procesarSiguienteAdjunto();
}

void StudiaController::procesarSiguienteAdjunto()
{
    if (m_procAdjunto || m_colaAdjuntos.isEmpty())
        return;
    const QString ruta = m_colaAdjuntos.takeFirst();
    m_ultimoAdjunto = QFileInfo(ruta).fileName();
    correrIngestor({QStringLiteral("--db"), rutaIndicePropio(),
                    QStringLiteral("--archivo"), ruta,
                    QStringLiteral("--materia"), m_materia},
                   QStringLiteral("agregar «%1»").arg(m_ultimoAdjunto));
}

int StudiaController::contarFlashcards(const QString &respuesta,
                                       const QString &idModo) const
{
    // Una autoevaluación tiene la misma forma que una tanda de flashcards
    // —preguntas numeradas, la línea, respuestas numeradas—, pero no son
    // tarjetas de repaso: ofrecer exportarlas a Anki sería un error.
    if (!idModo.isEmpty()
        && StudiaPrompt::modoPorId(idModo).id != QLatin1String("flashcards"))
        return 0;
    return int(StudiaTexto::flashcards(respuesta).size());
}

QString StudiaController::exportarFlashcards(const QString &respuesta)
{
    const QVariantList tarjetas = StudiaTexto::flashcards(respuesta);
    if (tarjetas.isEmpty()) {
        emit errorOcurrido(QStringLiteral("No encontré tarjetas con formato de "
                                          "flashcard en esta respuesta."));
        return {};
    }
    const QString sugerido = QStringLiteral("flashcards-%1.txt")
        .arg(m_materia.isEmpty() ? QStringLiteral("studia")
                                 : QString(m_materia).replace(QLatin1Char(' '),
                                                              QLatin1Char('-')));
    QWidget *padre = QApplication::activeWindow();
    const QString destino = QFileDialog::getSaveFileName(
        padre, QStringLiteral("Guardar flashcards para Anki"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(sugerido),
        QStringLiteral("Texto separado por tabulaciones (*.txt)"));
    if (destino.isEmpty())
        return {};

    QFile f(destino);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        emit errorOcurrido(QStringLiteral("No se pudo escribir %1").arg(destino));
        return {};
    }
    // UTF-8 sin BOM: es lo que espera el importador de Anki.
    f.write(StudiaTexto::flashcardsATsv(tarjetas).toUtf8());
    f.close();
    return destino;
}

QVariantMap StudiaController::estadoSemantico() const
{
    return {
        {QStringLiteral("url"),        m_embed.url()},
        {QStringLiteral("vectores"),   m_index.cantidadVectores()},
        {QStringLiteral("fragmentos"), m_index.totalFragmentos()},
        {QStringLiteral("dimension"),  m_index.dimensionVectores()},
        {QStringLiteral("activa"),     semanticaActiva()},
    };
}

void StudiaController::setUrlEmbeddings(const QString &u)
{
    if (u.trimmed() == m_embed.url())
        return;
    m_embed.setUrl(u);
    QSettings().setValue(QLatin1String(kClaveEmbed), m_embed.url());
    emit semanticaChanged();
}

void StudiaController::setServerUrl(const QString &u)
{
    if (u == m_serverUrl) return;
    m_serverUrl = u;
    emit serverUrlChanged();
    // Sin servidor de embeddings propio, la semántica depende del del chat.
    emit semanticaChanged();
}

QVector<StudiaFragmento> StudiaController::recuperar(const QString &consulta,
                                                     const QVector<float> &vector) const
{
    QVector<StudiaFragmento> frags =
        m_index.buscarHibrido(consulta, vector, m_k, m_materia);

    // La bibliografia propia se consulta APARTE y se le reservan unos pocos
    // lugares. Los scores BM25 de dos indices distintos no son comparables
    // entre si (dependen del tamano del corpus), asi que no se fusionan por
    // score: se asignan cupos y se marca el origen de cada fragmento.
    if (m_propio.abierto()) {
        const int cupo = qMax(1, m_k / 3);
        QVector<StudiaFragmento> propios = m_propio.buscar(consulta, cupo, m_materia);
        for (StudiaFragmento &f : propios)
            f.propio = true;
        if (!propios.isEmpty()) {
            // Se le hace lugar recortando los de la catedra, no ampliando el
            // total: el presupuesto de contexto del modelo es el mismo.
            while (frags.size() + propios.size() > m_k && !frags.isEmpty())
                frags.removeLast();
            frags += propios;
        }
    }
    return frags;
}

QVariantList StudiaController::buscar(const QString &consulta, int k) const
{
    QVariantList out;
    for (const StudiaFragmento &f : m_index.buscar(consulta, k, m_materia)) {
        out.append(QVariantMap{
            {QStringLiteral("documento"), f.documento},
            {QStringLiteral("materia"),   f.materia},
            {QStringLiteral("anio"),      f.anio},
            {QStringLiteral("pagina"),    f.pagina},
            {QStringLiteral("score"),     f.score},
            {QStringLiteral("ruta"),      f.ruta},
            {QStringLiteral("texto"),     f.texto},
        });
    }
    return out;
}

// ── Fuentes ──────────────────────────────────────────────────────────────────

QVariantList StudiaController::agruparFuentes(const QVector<StudiaFragmento> &frags)
{
    // Se conserva el orden de aparicion: el documento del fragmento [1] va
    // primero. La clave es la ruta (dos materias pueden tener archivos con el
    // mismo nombre).
    QStringList orden;
    QHash<QString, QVariantMap> datos;
    QHash<QString, QList<int>> refs;
    QHash<QString, QList<int>> paginas;

    for (int i = 0; i < frags.size(); ++i) {
        const StudiaFragmento &f = frags[i];
        const QString clave = f.ruta.isEmpty() ? f.documento : f.ruta;
        if (!datos.contains(clave)) {
            orden << clave;
            datos.insert(clave, QVariantMap{
                {QStringLiteral("documento"), f.documento},
                {QStringLiteral("materia"),   f.materia},
                {QStringLiteral("ruta"),      f.ruta},
                {QStringLiteral("propio"),    f.propio},
            });
        }
        refs[clave].append(i + 1);
        if (f.pagina > 0 && !paginas[clave].contains(f.pagina))
            paginas[clave].append(f.pagina);
    }

    auto unir = [](QList<int> nums) {
        std::sort(nums.begin(), nums.end());
        QStringList s;
        for (int n : nums) s << QString::number(n);
        return s.join(QStringLiteral(", "));
    };

    QVariantList salida;
    for (const QString &clave : orden) {
        QVariantMap m = datos.value(clave);
        m.insert(QStringLiteral("refs"), unir(refs.value(clave)));
        m.insert(QStringLiteral("paginas"), unir(paginas.value(clave)));
        salida.append(m);
    }
    return salida;
}

// ── Conversacion ─────────────────────────────────────────────────────────────

void StudiaController::agregarMensaje(const QString &rol, const QString &contenido,
                                      const QVariantList &fuentes, bool escribiendo,
                                      const QString &modo)
{
    StudiaSesion *s = sesionActual();
    if (!s)
        return;
    s->mensajes.append(QVariantMap{
        {QStringLiteral("rol"), rol},
        {QStringLiteral("contenido"), contenido},
        {QStringLiteral("escribiendo"), escribiendo},
        {QStringLiteral("fuentes"), fuentes},
        {QStringLiteral("modo"), modo},
    });
    s->usada = double(QDateTime::currentMSecsSinceEpoch());
    emit mensajesChanged();
    // La lista de temas muestra la cantidad de mensajes y se ordena por uso, y
    // el boton "nuevo tema" se habilita con el primer mensaje: los tres
    // dependen de esto.
    emit sesionesChanged();
}

QVector<StudiaPrompt::Turno> StudiaController::historialReciente() const
{
    QVector<StudiaPrompt::Turno> out;
    // El historial sale del TEMA abierto, no de la materia: es lo que mantiene
    // separadas dos conversaciones de la misma asignatura.
    const StudiaSesion *s = m_sesiones.porId(m_temaId);
    if (!s)
        return out;
    // Se toman los ultimos turnos completos, sin contar el mensaje que se acaba
    // de agregar (lo excluye el llamador enviando el historial antes).
    const int desde = qMax(0, int(s->mensajes.size()) - kTurnosContexto);
    for (int i = desde; i < s->mensajes.size(); ++i) {
        const QVariantMap m = s->mensajes.at(i).toMap();
        const QString contenido = m.value(QStringLiteral("contenido")).toString().trimmed();
        if (contenido.isEmpty())
            continue;
        out.append({m.value(QStringLiteral("rol")).toString(),
                    contenido.left(kMaxCharsTurno)});
    }
    return out;
}

void StudiaController::preguntar(const QString &texto)
{
    if (m_reply)
        return;

    // El menu de modos prellena "/flashcards/ ..."; tambien se puede tipear.
    QString idModo, pregunta;
    StudiaPrompt::separarModo(texto, &idModo, &pregunta);
    if (pregunta.isEmpty())
        return;

    if (!m_index.abierto()) {
        // Sin sesion no hay donde escribir el aviso: se emite como error.
        emit errorOcurrido(QStringLiteral("No hay un índice documental abierto. "
                                          "Elegí el archivo del índice para empezar."));
        return;
    }
    if (!materiaElegida()) {
        emit errorOcurrido(QStringLiteral("Elegí primero la materia sobre la que "
                                          "querés estudiar."));
        return;
    }

    // El contexto se arma ANTES de sumar la pregunta nueva.
    const QVector<StudiaPrompt::Turno> historial = historialReciente();
    const QStringList previas =
        m_sesiones.ultimasPreguntas(m_temaId, kPreguntasParaExpandir);
    // Y el turno del plan también: si StudIA ya pidió los datos en este tema,
    // esta pregunta es la respuesta del estudiante y toca armarlo.
    const bool planConDatos = planYaPidioLosDatos();

    agregarMensaje(QStringLiteral("usuario"), pregunta, {}, false, idModo);

    // Encuadre: ¿se sostiene sola, continúa la anterior, o cambia a un tema que
    // el corpus no cubre? Se decide preguntándole al índice cuáles de sus
    // términos existen, no contando palabras.
    const int total = m_index.totalFragmentos();
    const QStringList propios = StudiaIndex::terminosConsulta(pregunta);
    int existentes = 0, discriminantes = 0;
    for (const QString &t : propios) {
        const int df = m_index.frecuenciaDocumental(t);
        if (df > 0)
            ++existentes;
        if (StudiaIndex::esDiscriminante(df, total))
            ++discriminantes;
    }
    const StudiaPrompt::Encuadre encuadre =
        StudiaPrompt::encuadrar(int(propios.size()), existentes, discriminantes);

    const QString consulta =
        StudiaPrompt::consultaConContexto(pregunta, previas, encuadre);

    // Si hay búsqueda semántica, primero hay que vectorizar la pregunta. Es una
    // request HTTP: se hace async y la segunda mitad sigue en continuarPregunta.
    m_pendiente = {true, pregunta, idModo, consulta, historial, encuadre,
                   planConDatos};
    if (semanticaActiva()) {
        // Si no hay servidor de embeddings propio, se usa el del chat.
        m_embed.setUrl(urlEmbeddingsEfectiva());
        m_embed.vectorizar(consulta);
        return;
    }
    continuarPregunta({});
}

void StudiaController::continuarPregunta(const QVector<float> &vector)
{
    if (!m_pendiente.activo)
        return;
    const Pendiente p = m_pendiente;
    m_pendiente = {};

    // Los modos flexibles (conversacion, explicacion, ejercicio) buscan con un
    // criterio mas permisivo; los que generan material de estudio conservan el
    // umbral calibrado.
    const StudiaPrompt::Modo modoActual = StudiaPrompt::modoPorId(p.idModo);
    const double umbralPrevio = m_index.umbralAbstencion();
    if (!modoActual.exigente)
        m_index.setUmbralAbstencion(umbralPrevio + kAflojarUmbralFlexible);
    const QVector<StudiaFragmento> frags = recuperar(p.consulta, vector);
    m_index.setUmbralAbstencion(umbralPrevio);

    const QString pregunta = p.pregunta;
    const QString idModo = p.idModo;
    const QVector<StudiaPrompt::Turno> historial = p.historial;

    if (frags.isEmpty()) {
        // Sin material nuevo, pero puede que el pedido se refiera a algo que ya
        // esta en la conversacion ("repetí la ecuación anterior", "dame las
        // respuestas"). Se responde con eso antes de abstenerse cuando:
        //
        //  - el modo es flexible (conversar es su razon de ser), o
        //  - es una CONTINUACION: StudIA escribio esas preguntas, no puede
        //    contestar que no sabe nada de ellas, o
        //  - la pregunta es DEPENDIENTE: no aporta terminos propios, o sea que
        //    se apoya en lo anterior. Buscar en el indice algo que no nombra y
        //    abstenerse porque no aparece seria absurdo.
        //
        // La garantia de no inventar sigue en pie: la conversacion previa salio
        // de fragmentos reales, y el prompt de esta rama lo deja explicito.
        if (StudiaPrompt::respondeDesdeLaConversacion(
                modoActual.exigente, p.encuadre, historial)) {
            agregarMensaje(QStringLiteral("asistente"), QString(), {}, true, idModo);
            StudiaSesion *s = sesionActual();
            m_idxRespuesta = s ? int(s->mensajes.size()) - 1 : -1;
            generar(StudiaPrompt::sistema(m_materia, idModo, p.planConDatos),
                    StudiaPrompt::usuarioSoloConversacion(pregunta, historial,
                                                          idModo, p.planConDatos));
            return;
        }
        // Sin evidencia ni contexto no se consulta al modelo: se responde la
        // abstencion. Asi la garantia no depende de que el modelo obedezca.
        agregarMensaje(QStringLiteral("asistente"), StudiaPrompt::fraseAbstencion());
        m_sesiones.guardar();
        return;
    }

    agregarMensaje(QStringLiteral("asistente"), QString(),
                   agruparFuentes(frags), true, idModo);
    StudiaSesion *s = sesionActual();
    m_idxRespuesta = s ? int(s->mensajes.size()) - 1 : -1;

    generar(StudiaPrompt::sistema(m_materia, idModo, p.planConDatos),
            StudiaPrompt::usuario(pregunta, frags, historial, idModo,
                                  p.planConDatos));
}

void StudiaController::generar(const QString &promptSistema, const QString &promptUsuario)
{
    if (m_serverUrl.trimmed().isEmpty()) {
        cerrarStream(false, QStringLiteral("El servidor no está activo. "
                                           "Arrancalo desde la sección Lanzar."));
        return;
    }

    QJsonArray msgs;
    msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                            {QStringLiteral("content"), promptSistema}});
    msgs.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                            {QStringLiteral("content"), promptUsuario}});

    QJsonObject payload{
        {QStringLiteral("model"), m_modelo},
        {QStringLiteral("messages"), msgs},
        {QStringLiteral("stream"), true},
        // Temperatura baja: la tarea es explicar documentación, no inventar.
        {QStringLiteral("temperature"), 0.3},
        // Sin razonamiento interno: queremos la respuesta directa.
        {QStringLiteral("reasoning_budget"), 0},
        {QStringLiteral("chat_template_kwargs"),
         QJsonObject{{QStringLiteral("enable_thinking"), false}}},
    };

    QNetworkRequest req(QUrl(m_serverUrl + QStringLiteral("/v1/chat/completions")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));

    m_sseBuf.clear();
    m_acumulado.clear();
    m_reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    emit generandoChanged();

    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_reply) return;
        m_sseBuf.append(m_reply->readAll());
        while (true) {
            const int nl = m_sseBuf.indexOf('\n');
            if (nl < 0) break;
            const QByteArray linea = m_sseBuf.left(nl).trimmed();
            m_sseBuf.remove(0, nl + 1);
            if (!linea.startsWith("data: ")) continue;
            const QByteArray data = linea.mid(6).trimmed();
            if (data == "[DONE]") continue;
            const QJsonDocument d = QJsonDocument::fromJson(data);
            if (!d.isObject()) continue;
            const QJsonArray choices = d.object().value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty()) continue;
            const QString trozo = choices.first().toObject()
                                      .value(QStringLiteral("delta")).toObject()
                                      .value(QStringLiteral("content")).toString();
            if (trozo.isEmpty()) continue;
            m_acumulado += trozo;
            StudiaSesion *s = sesionActual();
            if (s && m_idxRespuesta >= 0 && m_idxRespuesta < s->mensajes.size()) {
                // El modelo emite LaTeX aunque el prompt se lo prohiba; se
                // convierte a texto legible antes de mostrarlo y de guardarlo.
                // Si el modelo declaró abstención al empezar, se corta ahí: no
                // se muestra el formato que a veces completa igual después.
                const QString crudo = StudiaTexto::recortarTrasAbstencion(
                    m_acumulado, StudiaPrompt::fraseAbstencion());
                const QString visible = StudiaTexto::latexALegible(crudo);
                const QVariantList bloques = StudiaTexto::enBloques(crudo);
                QVariantMap msg = s->mensajes[m_idxRespuesta].toMap();
                msg[QStringLiteral("contenido")] = visible;
                msg[QStringLiteral("bloques")] = bloques;
                s->mensajes[m_idxRespuesta] = msg;
                emit textoParcial(m_idxRespuesta, visible, bloques);
            }
        }
    });

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) return;
        const bool ok = m_reply->error() == QNetworkReply::NoError;
        QString err = ok ? QString() : m_reply->errorString();
        if (!ok) {
            const QString cuerpo = QString::fromUtf8(m_reply->readAll()).trimmed();
            if (!cuerpo.isEmpty())
                err += QStringLiteral(" · %1").arg(cuerpo.left(500));
        }
        cerrarStream(ok, err);
    });
}

void StudiaController::cerrarStream(bool ok, const QString &err)
{
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    StudiaSesion *s = sesionActual();
    if (s && m_idxRespuesta >= 0 && m_idxRespuesta < s->mensajes.size()) {
        QVariantMap msg = s->mensajes[m_idxRespuesta].toMap();
        msg[QStringLiteral("escribiendo")] = false;
        if (!ok && m_acumulado.isEmpty()) {
            msg[QStringLiteral("contenido")] = QStringLiteral("[error: %1]").arg(err);
            msg[QStringLiteral("fuentes")] = QVariantList{};
        } else if (!m_acumulado.isEmpty()) {
            // Conversión final sobre el texto completo: durante el streaming
            // una fórmula puede quedar a medio escribir.
            const QString crudo = StudiaTexto::recortarTrasAbstencion(
                m_acumulado, StudiaPrompt::fraseAbstencion());
            msg[QStringLiteral("contenido")] = StudiaTexto::latexALegible(crudo);

            // Autoevaluación y Flashcards vienen en dos partes separadas por
            // una línea: arriba la consigna, abajo las respuestas. Se parte acá,
            // una sola vez, y la UI muestra la primera hasta que el estudiante
            // despliega la segunda. No hace falta volver a consultar al modelo.
            const QString idModoMsg = msg.value(QStringLiteral("modo")).toString();
            if (StudiaPrompt::modoPorId(idModoMsg).ocultaRespuestas) {
                const StudiaTexto::ConsignaPartida par =
                    StudiaTexto::partirConsigna(crudo);
                msg[QStringLiteral("bloques")] = StudiaTexto::enBloques(par.consigna);
                msg[QStringLiteral("bloquesRespuestas")] =
                    par.respuestas.isEmpty() ? QVariantList{}
                                             : StudiaTexto::enBloques(par.respuestas);
                msg[QStringLiteral("consigna")] = par.consigna;
                msg[QStringLiteral("respuestas")] = par.respuestas;
            } else {
                msg[QStringLiteral("bloques")] = StudiaTexto::enBloques(crudo);
            }
            if (crudo.size() < m_acumulado.size())
                msg[QStringLiteral("fuentes")] = QVariantList{};   // se abstuvo
        }
        s->mensajes[m_idxRespuesta] = msg;
        // El nombre del tema sale del titulo de la respuesta, no de la
        // pregunta: el modelo encabeza con el concepto y la pregunta suele ser
        // coloquial. Se hace aca, con la respuesta ya completa.
        titularConRespuesta(msg.value(QStringLiteral("contenido")).toString());
    }
    m_idxRespuesta = -1;
    m_acumulado.clear();
    m_sseBuf.clear();
    m_sesiones.guardar();
    emit mensajesChanged();
    emit sesionesChanged();
    emit generandoChanged();
    if (!ok && !err.isEmpty())
        emit errorOcurrido(err);
}

void StudiaController::detener()
{
    if (!m_reply)
        return;
    QNetworkReply *r = m_reply;
    m_reply = nullptr;
    r->abort();
    r->deleteLater();
    StudiaSesion *s = sesionActual();
    if (s && m_idxRespuesta >= 0 && m_idxRespuesta < s->mensajes.size()) {
        QVariantMap msg = s->mensajes[m_idxRespuesta].toMap();
        msg[QStringLiteral("escribiendo")] = false;
        s->mensajes[m_idxRespuesta] = msg;
    }
    m_idxRespuesta = -1;
    m_acumulado.clear();
    m_sseBuf.clear();
    m_sesiones.guardar();
    emit mensajesChanged();
    emit generandoChanged();
}
