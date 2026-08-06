#include "StudiaPlot.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {

// Timeout del sidecar. Un gráfico de 800 puntos tarda menos de un segundo; si
// se pasa de esto es que algo se colgó.
constexpr int kTimeoutMs = 30000;

QString pythonExe()
{
    static QString cache;
    static bool resuelto = false;
    if (resuelto)
        return cache;
    resuelto = true;
    for (const QString &c : {QStringLiteral("python"), QStringLiteral("python3"),
                             QStringLiteral("py")}) {
        const QString p = QStandardPaths::findExecutable(c);
        if (!p.isEmpty()) { cache = p; break; }
    }
    return cache;
}

}  // namespace


StudiaPlot::StudiaPlot(QObject *parent) : QObject(parent) {}

QString StudiaPlot::cacheDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                      + QStringLiteral("/studia/graficos");
    QDir().mkpath(d);
    return d;
}

QString StudiaPlot::rutaScript()
{
    // En desarrollo el script vive en el repo; instalado, junto al ejecutable.
    const QStringList candidatos{
        QCoreApplication::applicationDirPath() + QStringLiteral("/tools/studia/graficar.py"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../tools/studia/graficar.py"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../../../tools/studia/graficar.py"),
    };
    for (const QString &c : candidatos) {
        const QString limpio = QDir::cleanPath(c);
        if (QFileInfo::exists(limpio))
            return limpio;
    }
    return {};
}

bool StudiaPlot::disponible() const
{
    return motivoNoDisponible().isEmpty();
}

QString StudiaPlot::motivoNoDisponible() const
{
    if (pythonExe().isEmpty())
        return QStringLiteral("Falta Python.");
    if (rutaScript().isEmpty())
        return QStringLiteral("No se encontró tools/studia/graficar.py.");
    // matplotlib se comprueba una sola vez: importarlo tarda ~1 s.
    static int tieneMatplotlib = -1;
    if (tieneMatplotlib < 0) {
        QProcess p;
        p.start(pythonExe(), {QStringLiteral("-c"), QStringLiteral("import matplotlib")});
        p.waitForFinished(15000);
        tieneMatplotlib = (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) ? 1 : 0;
    }
    if (tieneMatplotlib == 0)
        return QStringLiteral("Falta matplotlib: instalalo con «pip install matplotlib».");
    return {};
}

QString StudiaPlot::sourceHash(const QString &source) const
{
    return QString::fromLatin1(
        QCryptographicHash::hash(source.trimmed().toUtf8(), QCryptographicHash::Md5).toHex());
}

QString StudiaPlot::cachedPath(const QString &source) const
{
    const QString png = cacheDir() + QLatin1Char('/') + sourceHash(source)
                        + QStringLiteral(".png");
    return QFileInfo::exists(png) ? png : QString();
}

void StudiaPlot::requestRender(const QString &source)
{
    const QString hash = sourceHash(source);
    const QString png = cacheDir() + QLatin1Char('/') + hash + QStringLiteral(".png");

    if (QFileInfo::exists(png)) {          // ya estaba: se avisa y listo
        emit renderReady(hash, png);
        return;
    }
    if (m_enVuelo.contains(hash))          // otro delegate ya lo pidió
        return;
    const QString motivo = motivoNoDisponible();
    if (!motivo.isEmpty()) {
        emit renderFailed(hash, motivo);
        return;
    }

    // El spec va a un archivo: pasarlo por línea de comandos rompería con los
    // saltos de línea y las comillas.
    const QString spec = cacheDir() + QLatin1Char('/') + hash + QStringLiteral(".spec");
    {
        QFile f(spec);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit renderFailed(hash, QStringLiteral("No se pudo escribir el gráfico en disco."));
            return;
        }
        f.write(source.toUtf8());
    }

    m_enVuelo.insert(hash);
    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::finished, this,
            [this, p, hash, png, spec](int code, QProcess::ExitStatus estado) {
        const QString salida = QString::fromUtf8(p->readAll()).trimmed();
        p->deleteLater();
        m_enVuelo.remove(hash);
        QFile::remove(spec);
        if (estado == QProcess::NormalExit && code == 0 && QFileInfo::exists(png)) {
            emit renderReady(hash, png);
        } else {
            const QString detalle = salida.section(QLatin1Char('\n'), -1).trimmed();
            emit renderFailed(hash, detalle.isEmpty()
                                        ? QStringLiteral("no se pudo generar el gráfico")
                                        : detalle);
        }
    });
    connect(p, &QProcess::errorOccurred, this, [this, hash](QProcess::ProcessError) {
        m_enVuelo.remove(hash);
        emit renderFailed(hash, QStringLiteral("no se pudo ejecutar el generador"));
    });
    p->start(pythonExe(), {rutaScript(),
                           QStringLiteral("--spec"), spec,
                           QStringLiteral("--salida"), png});
    // Red de seguridad: si el sidecar se cuelga, no queda el hash bloqueado.
    QTimer::singleShot(kTimeoutMs, p, [p]() {
        if (p->state() != QProcess::NotRunning)
            p->kill();
    });
}
