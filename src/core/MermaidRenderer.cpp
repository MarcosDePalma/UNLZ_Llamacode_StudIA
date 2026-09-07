#include "MermaidRenderer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QVariantMap>

namespace {

// Fence ```mermaid|svg ... ``` (lenguaje case-insensitive, ws opcional). El
// cierre es ``` solo en su línea. Multiline + dotall para capturar el cuerpo.
// group(1) = lenguaje (mermaid|svg), group(2) = cuerpo.
QRegularExpression fenceRe()
{
    static QRegularExpression re(
        QStringLiteral("```[ \\t]*(mermaid|svg)[ \\t]*\\r?\\n(.*?)\\r?\\n[ \\t]*```"),
        QRegularExpression::CaseInsensitiveOption |
        QRegularExpression::DotMatchesEverythingOption);
    return re;
}

QVariantMap seg(const QString &type, const QString &text)
{
    QVariantMap m;
    m.insert(QStringLiteral("type"), type);
    m.insert(QStringLiteral("text"), text);
    return m;
}

}  // namespace

MermaidRenderer::MermaidRenderer(QObject *parent) : QObject(parent) {}

QString MermaidRenderer::cacheDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                      + QStringLiteral("/mermaid");
    QDir().mkpath(d);
    return d;
}

QString MermaidRenderer::mmdcExe()
{
    static QString cached;
    static bool resolved = false;
    if (resolved) return cached;
    resolved = true;
    const QByteArray env = qgetenv("LLAMACODE_MMDC");
    if (!env.isEmpty()) {
        const QString p = QString::fromLocal8Bit(env);
        if (QFileInfo::exists(p)) { cached = p; return cached; }
        const QString found = QStandardPaths::findExecutable(p);
        if (!found.isEmpty()) { cached = found; return cached; }
    }
    // Windows: mmdc.cmd; findExecutable resuelve la extensión.
    cached = QStandardPaths::findExecutable(QStringLiteral("mmdc"));
    return cached;
}

bool MermaidRenderer::available() const
{
    return !mmdcExe().isEmpty();
}

QVariantList MermaidRenderer::splitSegments(const QString &content)
{
    QVariantList out;
    int last = 0;
    auto it = fenceRe().globalMatch(content);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int start = m.capturedStart(0);
        if (start > last) {
            const QString pre = content.mid(last, start - last);
            if (!pre.isEmpty()) out.append(seg(QStringLiteral("text"), pre));
        }
        const QString lang = m.captured(1).toLower();
        const QString body = m.captured(2).trimmed();
        if (!body.isEmpty()) out.append(seg(lang, body));
        last = m.capturedEnd(0);
    }
    if (last < content.size()) {
        const QString tail = content.mid(last);
        if (!tail.isEmpty()) out.append(seg(QStringLiteral("text"), tail));
    }
    if (out.isEmpty())
        out.append(seg(QStringLiteral("text"), content));
    return out;
}

QVariantList MermaidRenderer::segments(const QString &content) const
{
    return splitSegments(content);
}

QString MermaidRenderer::sourceHash(const QString &source) const
{
    return QString::fromLatin1(
        QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Md5).toHex());
}

QString MermaidRenderer::cachedPath(const QString &source) const
{
    const QString out = cacheDir() + QStringLiteral("/") + sourceHash(source)
                        + QStringLiteral(".png");
    return QFileInfo::exists(out) ? out : QString();
}

void MermaidRenderer::requestRender(const QString &source)
{
    const QString hash = sourceHash(source);
    const QString out = cacheDir() + QStringLiteral("/") + hash + QStringLiteral(".png");

    if (QFileInfo::exists(out)) {
        emit renderReady(hash, out);
        return;
    }
    if (m_pending.contains(hash))
        return;  // ya en vuelo

    const QString exe = mmdcExe();
    if (exe.isEmpty()) {
        emit renderFailed(hash, QStringLiteral(
            "mermaid-cli no encontrado — instalá: npm i -g @mermaid-js/mermaid-cli"));
        return;
    }

    const QString in = cacheDir() + QStringLiteral("/") + hash + QStringLiteral(".mmd");
    {
        QFile f(in);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit renderFailed(hash, QStringLiteral("no se pudo escribir el input temporal"));
            return;
        }
        f.write(source.toUtf8());
        f.close();
    }

    m_pending.insert(hash);
    auto *pc = new QProcess(this);
    connect(pc, &QProcess::finished, this,
            [this, pc, hash, out](int code, QProcess::ExitStatus status) {
        if (!m_pending.remove(hash)) return;  // ya manejado por errorOccurred
        const QString errTxt = QString::fromUtf8(pc->readAllStandardError()).trimmed();
        pc->deleteLater();
        if (status == QProcess::NormalExit && code == 0 && QFileInfo::exists(out)) {
            emit renderReady(hash, out);
        } else {
            emit renderFailed(hash, errTxt.isEmpty()
                ? QStringLiteral("mmdc falló al renderizar el diagrama") : errTxt);
        }
    });
    // FailedToStart no emite finished: manejarlo acá. m_pending dedupe contra
    // el caso Crashed (que emite ambos).
    connect(pc, &QProcess::errorOccurred, this,
            [this, pc, hash](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return;
        if (!m_pending.remove(hash)) return;
        pc->deleteLater();
        emit renderFailed(hash, QStringLiteral(
            "no se pudo iniciar mmdc (¿mermaid-cli instalado?)"));
    });

    // -b transparent: fondo transparente para integrar con la burbuja.
    pc->start(exe, {QStringLiteral("-i"), in, QStringLiteral("-o"), out,
                    QStringLiteral("-b"), QStringLiteral("transparent")});
}

QString MermaidRenderer::renderSvg(const QString &source)
{
    const QString hash = sourceHash(source);
    const QString out = cacheDir() + QStringLiteral("/") + hash
                        + QStringLiteral(".svg.png");
    if (QFileInfo::exists(out))
        return out;

    // QSvgRenderer es estático (sin scripting). Sólo refs raster locales podrían
    // cargarse; no hay stack de red, así que no hay fetch remoto.
    QSvgRenderer r(source.toUtf8());
    if (!r.isValid())
        return QString();

    QSize sz = r.defaultSize();
    if (sz.isEmpty())
        sz = QSize(512, 512);

    // x2 para nitidez al estirar a la burbuja; cap para no explotar memoria.
    QSize px = sz * 2;
    const int cap = 2048;
    if (px.width() > cap || px.height() > cap)
        px.scale(cap, cap, Qt::KeepAspectRatio);

    QImage img(px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    r.render(&p);
    p.end();

    return img.save(out, "PNG") ? out : QString();
}

bool MermaidRenderer::exportPng(const QString &source, const QString &destPath)
{
    if (destPath.trimmed().isEmpty())
        return false;
    // mermaid: PNG cacheado (".png"). svg: rasteriza a ".svg.png" si hace falta.
    QString png = cachedPath(source);
    if (png.isEmpty())
        png = renderSvg(source);
    if (png.isEmpty() || !QFileInfo::exists(png))
        return false;
    QFile::remove(destPath);  // QFile::copy no pisa destinos existentes
    return QFile::copy(png, destPath);
}

bool MermaidRenderer::exportSource(const QString &source, const QString &destPath)
{
    if (destPath.trimmed().isEmpty())
        return false;
    QFile f(destPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray bytes = source.toUtf8();
    return f.write(bytes) == bytes.size();
}
