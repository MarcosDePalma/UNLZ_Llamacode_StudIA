#include "DocumentExtractor.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QProcess>
#include <QThread>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

namespace {

// Tope de caracteres que se inlinean al contexto (presupuesto). Documentos más
// grandes se truncan con una nota.
constexpr int kMaxChars = 240000;

const QSet<QString> &imageExts()
{
    static const QSet<QString> s = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp"),
        QStringLiteral("tif"), QStringLiteral("tiff")
    };
    return s;
}

// Formatos "ricos" que requieren parser → sidecar markitdown.
const QSet<QString> &richExts()
{
    static const QSet<QString> s = {
        QStringLiteral("pdf"),
        QStringLiteral("docx"), QStringLiteral("doc"),
        QStringLiteral("xlsx"), QStringLiteral("xls"),
        QStringLiteral("pptx"), QStringLiteral("ppt"),
        QStringLiteral("odt"),  QStringLiteral("ods"), QStringLiteral("odp"),
        QStringLiteral("rtf"),  QStringLiteral("epub"),
        QStringLiteral("html"), QStringLiteral("htm")
    };
    return s;
}

// Script Python embebido. Se escribe a disco una vez (AppLocalData) y se corre
// con el intérprete del sistema. Imprime el markdown a stdout.
const char *kExtractScript = R"PY(
import sys
try:
    from markitdown import MarkItDown
except Exception:
    sys.stderr.write("MARKITDOWN_MISSING")
    sys.exit(2)
if len(sys.argv) < 2:
    sys.stderr.write("NO_PATH")
    sys.exit(3)
try:
    md = MarkItDown()
    res = md.convert(sys.argv[1])
    out = res.text_content or ""
    sys.stdout.buffer.write(out.encode("utf-8", "replace"))
    sys.exit(0)
except Exception as e:
    sys.stderr.write(str(e))
    sys.exit(1)
)PY";

QString appDataDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(d);
    return d;
}

// Resuelve el intérprete Python (cacheado). Env LLAMACODE_PYTHON pisa todo.
QString pythonExe()
{
    static QString cached;
    static bool resolved = false;
    if (resolved) return cached;
    resolved = true;
    const QByteArray env = qgetenv("LLAMACODE_PYTHON");
    if (!env.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(env))) {
        cached = QString::fromLocal8Bit(env);
        return cached;
    }
    for (const QString &cand : {QStringLiteral("python"), QStringLiteral("python3"),
                                QStringLiteral("py")}) {
        const QString p = QStandardPaths::findExecutable(cand);
        if (!p.isEmpty()) { cached = p; return cached; }
    }
    return cached;  // vacío → no hay python
}

// Escribe el script embebido a AppLocalData una vez (idempotente).
QString ensureScript()
{
    const QString path = appDataDir() + QStringLiteral("/extract_doc.py");
    QFile f(path);
    const QByteArray want = QByteArray(kExtractScript);
    bool needWrite = true;
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        needWrite = (f.readAll() != want);
        f.close();
    }
    if (needWrite && f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(want);
        f.close();
    }
    return path;
}

QString budget(const QString &s)
{
    if (s.size() <= kMaxChars) return s;
    return s.left(kMaxChars) +
           QStringLiteral("\n\n[... documento truncado a %1 caracteres ...]").arg(kMaxChars);
}

// Lectura de texto plano. Si tiene bytes nulos → binario → "".
QString readPlain(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray raw = f.read(4 * 1024 * 1024);  // cap 4MB
    if (raw.contains('\0')) return {};
    return QString::fromUtf8(raw);
}

QByteArray fileMd5(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Md5);
    if (!h.addData(&f)) return {};
    return h.result().toHex();
}

// Conexión de cache per-thread (markitdown puede correr en worker o main).
QSqlDatabase cacheDb()
{
    const QString conn = QStringLiteral("doc_extract_%1")
                             .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    if (QSqlDatabase::contains(conn)) return QSqlDatabase::database(conn);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(appDataDir() + QStringLiteral("/doc_extract_cache.db"));
    if (db.open()) {
        QSqlQuery q(db);
        q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS doc_md ("
                              "key TEXT PRIMARY KEY, md TEXT)"));
    }
    return db;
}

QString cacheGet(const QByteArray &key)
{
    if (key.isEmpty()) return {};
    QSqlDatabase db = cacheDb();
    if (!db.isOpen()) return {};
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT md FROM doc_md WHERE key=?"));
    q.addBindValue(QString::fromLatin1(key));
    if (q.exec() && q.next()) return q.value(0).toString();
    return {};
}

void cachePut(const QByteArray &key, const QString &md)
{
    if (key.isEmpty()) return;
    QSqlDatabase db = cacheDb();
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO doc_md(key, md) VALUES(?, ?)"));
    q.addBindValue(QString::fromLatin1(key));
    q.addBindValue(md);
    q.exec();
}

// Extrae un documento rico vía sidecar markitdown.
QString extractRich(const QString &path, QString *err)
{
    const QByteArray key = fileMd5(path);
    const QString hit = cacheGet(key);
    if (!hit.isEmpty()) return hit;

    const QString py = pythonExe();
    if (py.isEmpty()) {
        if (err) *err = QStringLiteral("Python no encontrado (necesario para extraer %1)")
                            .arg(QFileInfo(path).suffix().toUpper());
        return {};
    }
    const QString script = ensureScript();

    QProcess pc;
    pc.start(py, {script, path});
    if (!pc.waitForStarted(10000)) {
        if (err) *err = QStringLiteral("no se pudo iniciar Python");
        return {};
    }
    // Documentos grandes: hasta 120s.
    pc.waitForFinished(120000);
    const int code = pc.exitCode();
    const QString stderrTxt = QString::fromUtf8(pc.readAllStandardError()).trimmed();

    if (pc.exitStatus() != QProcess::NormalExit) {
        if (err) *err = QStringLiteral("la extracción se interrumpió (timeout/crash)");
        return {};
    }
    if (code == 2 || stderrTxt.contains(QLatin1String("MARKITDOWN_MISSING"))) {
        if (err) *err = QStringLiteral("markitdown no instalado — corré: pip install \"markitdown[all]\"");
        return {};
    }
    if (code != 0) {
        if (err) *err = stderrTxt.isEmpty() ? QStringLiteral("fallo de extracción") : stderrTxt;
        return {};
    }
    const QString md = QString::fromUtf8(pc.readAllStandardOutput());
    if (md.trimmed().isEmpty()) {
        if (err) *err = QStringLiteral("sin texto extraíble (¿PDF escaneado? requeriría OCR)");
        return {};
    }
    const QString out = budget(md);
    cachePut(key, out);
    return out;
}

}  // namespace

namespace DocumentExtractor {

bool isImage(const QString &path)
{
    return imageExts().contains(QFileInfo(path).suffix().toLower());
}

bool isRich(const QString &path)
{
    return richExts().contains(QFileInfo(path).suffix().toLower());
}

QString extract(const QString &path, QString *err)
{
    if (err) err->clear();
    const QString ext = QFileInfo(path).suffix().toLower();

    if (imageExts().contains(ext))
        return {};  // visión, no se extrae texto

    if (richExts().contains(ext))
        return extractRich(path, err);

    // Texto plano / código: lectura directa.
    const QString t = readPlain(path);
    if (!t.isEmpty())
        return budget(t);

    // Vacío o binario desconocido → intentar markitdown como fallback.
    return extractRich(path, err);
}

}  // namespace DocumentExtractor
