#include "StudiaEmbedServer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>

namespace {

// Cuantas veces se pregunta si ya esta listo, y cada cuanto. bge-m3 tarda unos
// segundos en cargar; mas alla de esto es que algo no anda.
constexpr int kMaxIntentos = 40;
constexpr int kIntervaloMs = 500;

// ¿Hay algo escuchando en ese puerto? Se usa para no levantar un segundo
// servidor sobre uno que ya esta andando.
bool puertoOcupado(quint16 puerto)
{
    QTcpSocket s;
    s.connectToHost(QStringLiteral("127.0.0.1"), puerto);
    return s.waitForConnected(300);
}

// El primer archivo que matchee, buscando en varias carpetas.
QString primerArchivo(const QStringList &carpetas, const QStringList &patrones)
{
    for (const QString &c : carpetas) {
        QDir d(c);
        if (!d.exists())
            continue;
        const QFileInfoList fs = d.entryInfoList(patrones, QDir::Files, QDir::Name);
        if (!fs.isEmpty())
            return fs.first().absoluteFilePath();
    }
    return QString();
}

}  // namespace


StudiaEmbedServer::StudiaEmbedServer(QObject *parent) : QObject(parent) {}

StudiaEmbedServer::~StudiaEmbedServer() { detener(); }

QString StudiaEmbedServer::url()
{
    return QStringLiteral("http://127.0.0.1:%1").arg(kPuerto);
}

QString StudiaEmbedServer::carpetaEmpaquetada()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/StudIA");
}

QString StudiaEmbedServer::rutaModelo()
{
    // Primero el que viaja con la app: es el caso del estudiante que la recibe
    // ya armada. Despues el que haya bajado a mano.
    return primerArchivo({carpetaEmpaquetada() + QStringLiteral("/modelos"),
                          QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                              + QStringLiteral("/models"),
                          QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                              + QStringLiteral("/LlamaCode/models")},
                         {QStringLiteral("bge-m3*.gguf"),
                          QStringLiteral("*embed*.gguf")});
}

QString StudiaEmbedServer::rutaBinario()
{
    // El que instala LlamaCode queda en tools/llama.cpp-install-<fecha>/extract.
    // Se toma el mas reciente por nombre, que lleva la fecha adelante.
    const QString tools =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/tools");
    QDir d(tools);
    if (d.exists()) {
        const QStringList inst = d.entryList({QStringLiteral("llama.cpp-install-*")},
                                             QDir::Dirs, QDir::Name | QDir::Reversed);
        for (const QString &i : inst) {
            const QString exe = tools + QLatin1Char('/') + i
                                + QStringLiteral("/extract/llama-server.exe");
            if (QFileInfo::exists(exe))
                return exe;
        }
    }
    // Junto al ejecutable, para una copia portable.
    const QString local = QCoreApplication::applicationDirPath()
                          + QStringLiteral("/llama-server.exe");
    if (QFileInfo::exists(local))
        return local;
    return QStandardPaths::findExecutable(QStringLiteral("llama-server"));
}

void StudiaEmbedServer::marcar(bool activo, const QString &motivo)
{
    if (m_activo == activo && m_motivo == motivo)
        return;
    m_activo = activo;
    m_motivo = motivo;
    emit estadoCambiado();
    if (activo)
        emit listo();
}

void StudiaEmbedServer::iniciar()
{
    if (m_activo || m_proc)
        return;

    // Ya hay uno andando (el .bat, o una corrida anterior): se usa ese. Levantar
    // otro sobre el mismo puerto fallaria y dejaria un proceso zombi.
    if (puertoOcupado(kPuerto)) {
        marcar(true, QString());
        return;
    }

    const QString modelo = rutaModelo();
    if (modelo.isEmpty()) {
        marcar(false, QStringLiteral("Falta el modelo de embeddings (bge-m3). "
                                     "La búsqueda funciona igual, por palabras."));
        return;
    }
    const QString binario = rutaBinario();
    if (binario.isEmpty()) {
        marcar(false, QStringLiteral("Falta llama-server para la búsqueda semántica. "
                                     "La búsqueda funciona igual, por palabras."));
        return;
    }

    m_proc = new QProcess(this);
    m_proc->setProgram(binario);
    // --pooling cls es el que corresponde a bge-m3. Con otro el servidor
    // responde igual pero los vectores son peores, y nada lo avisa.
    // --n-gpu-layers 0: en CPU, para no pelearle la VRAM al modelo de chat.
    m_proc->setArguments({
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(kPuerto),
        QStringLiteral("--model"), modelo,
        QStringLiteral("--embeddings"),
        QStringLiteral("--pooling"), QStringLiteral("cls"),
        QStringLiteral("--ctx-size"), QStringLiteral("8192"),
        QStringLiteral("--batch-size"), QStringLiteral("2048"),
        QStringLiteral("--ubatch-size"), QStringLiteral("2048"),
        QStringLiteral("--n-gpu-layers"), QStringLiteral("0"),
    });
    m_proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
        if (m_activo || code != 0)
            marcar(false, QStringLiteral("El servidor de búsqueda semántica se cerró."));
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        marcar(false, QStringLiteral("No se pudo iniciar el servidor de búsqueda semántica."));
    });

    m_proc->start();
    m_intentos = 0;
    QTimer::singleShot(kIntervaloMs, this, &StudiaEmbedServer::comprobarSalud);
}

void StudiaEmbedServer::comprobarSalud()
{
    if (m_activo)
        return;
    if (!m_proc) {                       // se cayo mientras cargaba
        marcar(false, m_motivo);
        return;
    }
    if (puertoOcupado(kPuerto)) {
        marcar(true, QString());
        return;
    }
    if (++m_intentos >= kMaxIntentos) {
        marcar(false, QStringLiteral("El servidor de búsqueda semántica tardó demasiado "
                                     "en arrancar."));
        return;
    }
    QTimer::singleShot(kIntervaloMs, this, &StudiaEmbedServer::comprobarSalud);
}

void StudiaEmbedServer::detener()
{
    if (!m_proc)
        return;
    QProcess *p = m_proc;
    m_proc = nullptr;
    p->disconnect(this);
    p->terminate();
    if (!p->waitForFinished(3000))
        p->kill();
    p->deleteLater();
    m_activo = false;
}
