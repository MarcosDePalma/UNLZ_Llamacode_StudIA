#include "StudiaHerramientas.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QVariantMap>

namespace StudiaHerramientas {

namespace {

QString python()
{
    for (const QString &c : {QStringLiteral("python"), QStringLiteral("python3"),
                             QStringLiteral("py")}) {
        const QString p = QStandardPaths::findExecutable(c);
        if (!p.isEmpty())
            return p;
    }
    return QString();
}

// ¿Se puede importar ese modulo? Es la unica forma honesta de saberlo: que el
// paquete este instalado no garantiza que importe (falta una DLL, arquitectura
// equivocada, instalacion a medias).
bool importa(const QString &modulo)
{
    const QString py = python();
    if (py.isEmpty())
        return false;
    QProcess p;
    p.start(py, {QStringLiteral("-c"), QStringLiteral("import %1").arg(modulo)});
    if (!p.waitForFinished(8000))
        return false;
    return p.exitCode() == 0;
}

bool hayMermaid()
{
    if (!QStandardPaths::findExecutable(QStringLiteral("mmdc")).isEmpty())
        return true;
    // npm -g deja el .cmd en AppData\Roaming\npm, que no siempre esta en el
    // PATH del proceso que abrio la app.
    const QString cmd = QDir::homePath()
                        + QStringLiteral("/AppData/Roaming/npm/mmdc.cmd");
    return QFileInfo::exists(cmd);
}

}  // namespace

QString rutaTesseract()
{
    const QString enPath = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (!enPath.isEmpty())
        return enPath;
    for (const QString &d : {QStringLiteral("C:/Program Files/Tesseract-OCR"),
                             QStringLiteral("C:/Program Files (x86)/Tesseract-OCR"),
                             QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                                 + QStringLiteral("/AppData/Local/Programs/Tesseract-OCR")}) {
        const QString e = d + QStringLiteral("/tesseract.exe");
        if (QFileInfo::exists(e))
            return e;
    }
    return QString();
}

QString rutaInstalador()
{
    const QStringList candidatas = {
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/StudIA/instalar_dependencias.bat"),
        QCoreApplication::applicationDirPath()
            + QStringLiteral("/../../tools/studia/instalar_dependencias.bat"),
        QDir::currentPath() + QStringLiteral("/tools/studia/instalar_dependencias.bat"),
    };
    for (const QString &c : candidatas) {
        const QString limpia = QDir::cleanPath(c);
        if (QFileInfo::exists(limpia))
            return limpia;
    }
    return QString();
}

QVector<Herramienta> revisar()
{
    const bool hayPython = !python().isEmpty();
    QVector<Herramienta> v;
    v.append({QStringLiteral("python"), QStringLiteral("Python"),
              QStringLiteral("adjuntar bibliografía propia"), hayPython});
    v.append({QStringLiteral("matplotlib"), QStringLiteral("matplotlib"),
              QStringLiteral("gráficos de funciones"),
              hayPython && importa(QStringLiteral("matplotlib"))});
    v.append({QStringLiteral("mermaid"), QStringLiteral("mermaid-cli"),
              QStringLiteral("diagramas"), hayMermaid()});
    v.append({QStringLiteral("tesseract"), QStringLiteral("Tesseract"),
              QStringLiteral("leer PDFs escaneados"), !rutaTesseract().isEmpty()});
    return v;
}

QVariantList paraQml()
{
    QVariantList out;
    for (const Herramienta &h : revisar()) {
        out.append(QVariantMap{{QStringLiteral("id"), h.id},
                               {QStringLiteral("nombre"), h.nombre},
                               {QStringLiteral("habilita"), h.habilita},
                               {QStringLiteral("presente"), h.presente}});
    }
    return out;
}

int faltantes()
{
    int n = 0;
    for (const Herramienta &h : revisar())
        if (!h.presente)
            ++n;
    return n;
}

}  // namespace StudiaHerramientas
