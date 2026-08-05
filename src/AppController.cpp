#include "AppController.h"
#include "core/agent/BrowserTeach.h"
#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  define VC_EXTRA_LEAN
#  include <windows.h>
#else
#  include <signal.h>
#endif
#include <QProcess>
#include <QTcpServer>
#include <QHostAddress>
#include <QDateTime>
#include <QClipboard>
#include <QGuiApplication>
#include <QApplication>
#include <QWidget>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QClipboard>
#include "core/agent/OpencodeBackend.h"
#include "core/agent/RawChatBackend.h"
#include "core/voice/VoiceController.h"
#include "core/voice/VoiceTypes.h"
#include "core/voice/VoiceServerManager.h"
#include "core/agent/LlamaAgentBackend.h"
#include "core/mail/MailClient.h"
#include "core/agent/McpClient.h"
#include "core/eval/EvalSuite.h"
#include <QtConcurrent>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QDesktopServices>
#include <QFileDialog>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QCoreApplication>
#include <QUuid>
#include <QDirIterator>
#include <functional>
#include <QSet>
#include <memory>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QVersionNumber>
#include <algorithm>
#include <cmath>

namespace {
QHostAddress bindAddressForHost(const QString &host)
{
    if (host == QLatin1String("0.0.0.0") || host.isEmpty())
        return QHostAddress(QHostAddress::Any);
    if (host == QLatin1String("localhost"))
        return QHostAddress(QHostAddress::LocalHost);
    return QHostAddress(host);
}

bool canListenOnPort(const QString &host, quint16 port, QString *error = nullptr)
{
    QTcpServer probe;
    const bool ok = probe.listen(bindAddressForHost(host), port);
    if (!ok && error)
        *error = probe.errorString();
    probe.close();
    return ok;
}

int nextAvailablePort(const QString &host, int requestedPort)
{
    const int start = std::max(1, requestedPort + 1);
    const int end = std::min(65535, requestedPort + 200);
    for (int port = start; port <= end; ++port) {
        if (canListenOnPort(host, static_cast<quint16>(port)))
            return port;
    }
    return 0;
}

QJsonObject readBundledUpdateFlag()
{
    QFile f(QStringLiteral(":/assets/update/latest.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.object();
}
}

struct ResearchHit {
    QString title;
    QString url;
    QString snippet;
};

// Agrupa sesiones por proyecto (secciones contiguas), proyecto más reciente
// arriba, y dentro de cada proyecto la sesión más nueva primero. Necesario
// porque ListView.section sólo agrupa filas contiguas: si la lista no viene
// ordenada por projectName, el mismo proyecto aparece partido en secciones.
static QVariantList groupSessionsByProject(const QVariantList &in)
{
    QVector<QVariantMap> rows;
    rows.reserve(in.size());
    for (const QVariant &v : in) rows.append(v.toMap());

    auto stamp = [](const QVariantMap &s) {
        const double u = s.value(QStringLiteral("updated")).toDouble();
        return u > 0.0 ? u : s.value(QStringLiteral("created")).toDouble();
    };

    QHash<QString, double> projLatest;
    for (const auto &s : std::as_const(rows)) {
        const QString p = s.value(QStringLiteral("projectName")).toString();
        const double u = stamp(s);
        if (u > projLatest.value(p, 0.0)) projLatest[p] = u;
    }
    std::stable_sort(rows.begin(), rows.end(), [&](const QVariantMap &a, const QVariantMap &b) {
        const QString pa = a.value(QStringLiteral("projectName")).toString();
        const QString pb = b.value(QStringLiteral("projectName")).toString();
        if (pa != pb) {
            const double la = projLatest.value(pa);
            const double lb = projLatest.value(pb);
            // Desempate por nombre: si los timestamps de proyecto empatan, igual
            // hay que forzar contigüidad o las secciones quedan intercaladas.
            if (la != lb) return la > lb;
            return pa < pb;
        }
        return stamp(a) > stamp(b);
    });

    QVariantList out;
    out.reserve(rows.size());
    for (const auto &s : std::as_const(rows)) out.append(s);
    return out;
}

static bool isSupportedImageAttachment(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("png") || ext == QLatin1String("jpg")
           || ext == QLatin1String("jpeg") || ext == QLatin1String("webp")
           || ext == QLatin1String("gif") || ext == QLatin1String("bmp");
}

static QString researchCleanHtmlToText(QString text)
{
    text.remove(QRegularExpression(QStringLiteral("(?is)<(script|style)[^>]*>.*?</\\1>")));
    text.remove(QRegularExpression(QStringLiteral("(?s)<[^>]+>")));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#x27;"), QStringLiteral("'"));
    text.replace(QRegularExpression(QStringLiteral("[ \t]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\n[ \t]*(?:\n[ \t]*)+")), QStringLiteral("\n\n"));
    return text.trimmed();
}

static QString researchResolveDdgRedirect(QString raw)
{
    if (raw.startsWith(QLatin1String("//"))) raw = QStringLiteral("https:") + raw;
    QUrl ru(raw);
    if (ru.path().endsWith(QLatin1String("/l/")) || ru.path() == QLatin1String("/l")) {
        const QString uddg = QUrlQuery(ru).queryItemValue(QStringLiteral("uddg"), QUrl::FullyDecoded);
        if (!uddg.isEmpty()) return uddg;
    }
    return raw;
}

static QVector<ResearchHit> researchParseDdg(const QString &html, int count)
{
    QVector<ResearchHit> hits;
    QRegularExpression reTitle(
        QStringLiteral("(?is)<a[^>]+class=\"[^\"]*result__a[^\"]*\"[^>]+href=\"([^\"]+)\"[^>]*>(.*?)</a>"));
    QRegularExpression reSnip(
        QStringLiteral("(?is)class=\"[^\"]*result__snippet[^\"]*\"[^>]*>(.*?)</a>"));
    auto snipIt = reSnip.globalMatch(html);
    auto titleIt = reTitle.globalMatch(html);
    while (titleIt.hasNext() && hits.size() < count) {
        const auto tm = titleIt.next();
        ResearchHit h;
        h.url = researchResolveDdgRedirect(tm.captured(1));
        h.title = researchCleanHtmlToText(tm.captured(2));
        if (snipIt.hasNext()) h.snippet = researchCleanHtmlToText(snipIt.next().captured(1));
        if (!h.url.isEmpty()) hits.append(h);
    }
    return hits;
}

static QStringList researchQueriesFor(const QString &topic, const QString &mode)
{
    QStringList queries;
    queries << topic;
    if (mode == QLatin1String("compare")) {
        queries << topic + QStringLiteral(" comparison benchmarks alternatives");
        queries << topic + QStringLiteral(" pros cons limitations");
    } else if (mode == QLatin1String("product")) {
        queries << topic + QStringLiteral(" official pricing features docs");
        queries << topic + QStringLiteral(" review limitations competitors");
    } else if (mode == QLatin1String("howto")) {
        queries << topic + QStringLiteral(" official docs tutorial setup");
        queries << topic + QStringLiteral(" common errors troubleshooting");
    } else if (mode == QLatin1String("factcheck")) {
        queries << topic + QStringLiteral(" source evidence fact check");
        queries << topic + QStringLiteral(" controversy correction");
    } else {
        queries << topic + QStringLiteral(" official docs latest");
        queries << topic + QStringLiteral(" analysis risks limitations");
    }
    queries.removeDuplicates();
    return queries.mid(0, 4);
}

static QString researchModeTitle(const QString &mode)
{
    if (mode == QLatin1String("product")) return QStringLiteral("Product");
    if (mode == QLatin1String("compare")) return QStringLiteral("Compare");
    if (mode == QLatin1String("howto")) return QStringLiteral("How-to");
    if (mode == QLatin1String("factcheck")) return QStringLiteral("Fact-check");
    return QStringLiteral("Auto");
}

AppController::AppController(QObject *parent) : QObject(parent)
{
    // Reenviar progreso/fin de descarga de modelos STT a QML.
    connect(&m_voiceServers, &VoiceServerManager::installProgress,
            this, &AppController::voiceInstallProgress);
    connect(&m_voiceServers, &VoiceServerManager::installFinished,
            this, &AppController::voiceInstallFinished);
    connect(&m_voiceServers, &VoiceServerManager::binaryInstalled, this,
            [this](const QString &kind, bool ok, const QString &path, const QString &msg) {
        if (ok) {
            if (kind == QLatin1String("piper")) setVoicePiperPath(path);
            else setVoiceWhisperServerPath(path);
        }
        emit voiceBinaryInstalled(kind, ok, ok ? path : msg);
    });
    QSettings s;
    m_language = s.value(QStringLiteral("language"), QStringLiteral("es")).toString();
    m_agentApprovalMode = s.value(QStringLiteral("agent/approvalMode"), QStringLiteral("ask")).toString();
    m_agentThinkingEnabled = s.value(QStringLiteral("thinking/enabled"),
                                     s.value(QStringLiteral("agent/thinkingEnabled"), false)).toBool();
    m_chatThinkingEnabled = s.value(QStringLiteral("chat/thinkingEnabledV2"), false).toBool();
    m_launchThinkingEnabled = s.value(QStringLiteral("thinking/serverEnabled"),
                                      m_agentThinkingEnabled).toBool();
    m_mermaidEnabled = s.value(QStringLiteral("chat/mermaidEnabled"), true).toBool();
    m_browserAutomationEnabled = s.value(QStringLiteral("browser/automationEnabled"), false).toBool();
    m_browserMcpCommand = s.value(QStringLiteral("browser/mcpCommand"),
                                  QStringLiteral("npx @playwright/mcp@latest")).toString();
    if (m_browserMcpCommand.trimmed().isEmpty())
        m_browserMcpCommand = QStringLiteral("npx @playwright/mcp@latest");
    m_agentSystemPrompt = s.value(QStringLiteral("agent/systemPrompt")).toString();
    m_agentPermRules    = s.value(QStringLiteral("agent/permRules")).toString();
    m_agentTemperature  = s.value(QStringLiteral("agent/temperature"), -1.0).toDouble();
    m_agentDisabledTools = s.value(QStringLiteral("agent/disabledTools")).toStringList();
    m_agentTeacherUrl   = s.value(QStringLiteral("agent/teacherUrl")).toString();
    m_agentTeacherModel = s.value(QStringLiteral("agent/teacherModel")).toString();
    m_agentTeacherKey   = s.value(QStringLiteral("agent/teacherKey")).toString();
    m_mailAutoSend      = s.value(QStringLiteral("agent/mailAutoSend"), false).toBool();
    m_gitAvailable      = !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();

    killManagedOrphans();
    createJobObject();
    ensureChatBackend();
    const QString logDir = runtimeLogDir();
    m_serverLogFilePath = logDir + QStringLiteral("/server.log");
    m_agentLogFilePath = logDir + QStringLiteral("/agent.log");
    appendFileLog(m_serverLogFilePath, QStringLiteral("\n=== session %1 ===\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));
    appendFileLog(m_agentLogFilePath, QStringLiteral("\n=== session %1 ===\n")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)));

    connect(&m_binaries, &BinaryRegistry::countChanged, this, &AppController::setupStateChanged);
    connect(&m_catalog, &ModelCatalog::countChanged, this, &AppController::setupStateChanged);
    connect(&m_profiles, &ProfileManager::launchesChanged, this, &AppController::setupStateChanged);

    // Scheduler de Tasks (cron in-app). taskDue→runTask. El toggle global persiste.
    m_scheduler = new TaskScheduler(&m_tasks, this);
    connect(m_scheduler, &TaskScheduler::taskDue, this, [this](const QString &id) {
        runTask(id);
    });
    if (s.value(QStringLiteral("tasks/schedulerEnabled"), false).toBool())
        m_scheduler->setEnabled(true);
    // El escaneo pesado (binaries/roots/hardware/catálogo + migraciones) se difiere
    // a runStartupScan(), que QML invoca tras pintar el popup de carga. Antes corría
    // acá en el constructor y congelaba ~3s antes de mostrar la ventana.
}

void AppController::runStartupScan()
{
    m_binaries.refresh();
    m_roots.refresh();
    rescanHardware();

    // Diagnóstico de catálogo: comparar lo cargado en memoria contra las filas
    // reales en el .db, para detectar fallos de carga (→ rescans con ids nuevos
    // que orfanan los modelId de los perfiles).
    {
        const QString dbp = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/model_catalog.db");
        int dbRows = -1; QString dbErr; bool opened = false;
        {
            const QString cn = QStringLiteral("diagcat_%1").arg(QCoreApplication::applicationPid());
            QSqlDatabase d = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), cn);
            d.setDatabaseName(dbp);
            opened = d.open();
            if (opened) {
                QSqlQuery q(QStringLiteral("SELECT COUNT(*) FROM catalog_models"), d);
                if (q.next()) dbRows = q.value(0).toInt(); else dbErr = q.lastError().text();
                d.close();
            } else {
                dbErr = d.lastError().text();
            }
            QSqlDatabase::removeDatabase(cn);
        }
        appendServerEvent(QStringLiteral("lifecycle"),
            QStringLiteral("catalog diag: inMemory=%1 dbRows=%2 dbOpen=%3 path=%4 err=%5 driversAvail=%6")
                .arg(QString::number(m_catalog.count()),
                     QString::number(dbRows),
                     opened ? QStringLiteral("yes") : QStringLiteral("NO"),
                     dbp,
                     dbErr.isEmpty() ? QStringLiteral("-") : dbErr,
                     QSqlDatabase::drivers().join(QLatin1Char(','))));
    }

    // If folders are registered but no models were found yet (e.g. a root added
    // in "manual" mode), scan them once on startup so the catalog is populated.
    if (m_roots.count() > 0 && m_catalog.count() == 0)
        m_roots.scanAll();

    // Migración única a ids de catálogo DETERMINISTAS (UUIDv5 por ruta). Las filas
    // existentes traen ids aleatorios legacy; forzamos un rescan para reescribirlas
    // con el id estable por ruta (converge en addBatch). Sin esto, scanAll sólo
    // corre con catálogo vacío y los ids viejos nunca se actualizan.
    if (QString(readSetting(QStringLiteral("catalog/idSchemeV5"), QString()).toString()) != QLatin1String("done")) {
        if (m_roots.count() > 0)
            m_roots.scanAll();
        writeSetting(QStringLiteral("catalog/idSchemeV5"), QStringLiteral("done"));
        appendServerEvent(QStringLiteral("lifecycle"),
                          QStringLiteral("catalog: migrado a ids deterministas (rescan forzado)."));
    }
    refreshResearchReports();
}

// --- Router mode (hot-swap) ---------------------------------------------
// Convierte una lista plana de args (["--ctx-size","65536","--flash-attn","on",...])
// en líneas de sección .ini ("ctx-size = 65536\nflash-attn = on\n").
// Omite --host/--port (los gestiona el router) y normaliza -m/--model a "model".
static QString argsToIniSection(const QStringList &args)
{
    QString out;
    for (int i = 0; i < args.size(); ++i) {
        QString tok = args.at(i);
        if (!tok.startsWith(QLatin1Char('-'))) continue;        // valor suelto: ya consumido
        QString key = tok;
        while (key.startsWith(QLatin1Char('-'))) key.remove(0, 1);
        if (key.isEmpty()) continue;
        if (key == QLatin1String("m")) key = QStringLiteral("model");
        // host/port los fija el router por sección hija; jinja va en el global
        if (key == QLatin1String("host") || key == QLatin1String("port")
            || key == QLatin1String("jinja") || key == QLatin1String("alias")) {
            if (i + 1 < args.size() && !args.at(i + 1).startsWith(QLatin1Char('-'))) ++i;
            continue;
        }
        QString val = QStringLiteral("true");
        if (i + 1 < args.size() && !args.at(i + 1).startsWith(QLatin1Char('-'))) {
            val = args.at(++i);
        }
        out += key + QStringLiteral(" = ") + val + QStringLiteral("\n");
    }
    return out;
}

static QString slugify(const QString &name)
{
    QString s;
    for (const QChar &c : name) {
        if (c.isLetterOrNumber()) s += c.toLower();
        else if (!s.isEmpty() && s.back() != QLatin1Char('-')) s += QLatin1Char('-');
    }
    while (s.endsWith(QLatin1Char('-'))) s.chop(1);
    return s.isEmpty() ? QStringLiteral("model") : s;
}

QString AppController::generateRouterPreset(const QStringList &launchProfileIds)
{
    if (launchProfileIds.isEmpty()) {
        emit serverError(QStringLiteral("Router: seleccioná al menos un perfil."));
        return {};
    }

    QString host = QStringLiteral("127.0.0.1");
    int port = 8080;
    QString body;
    QStringList names;
    QSet<QString> usedNames;
    QString routerBinary;

    for (const QString &id : launchProfileIds) {
        const EffectiveProfileBuilder::Context ctx = buildContext(id);
        const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);
        if (!ep.isValid()) {
            emit serverError(QStringLiteral("Router: perfil inválido (%1): %2")
                                 .arg(id, ep.blockingErrors.join(QStringLiteral("; "))));
            return {};
        }
        // El router usa un único binario; tomamos el del primer perfil y avisamos si difieren.
        if (routerBinary.isEmpty()) {
            routerBinary = ep.binaryPath;
            host = ctx.backend.host.isEmpty() ? host : ctx.backend.host;
            port = ctx.backend.port > 0 ? ctx.backend.port : port;
        } else if (ep.binaryPath != routerBinary) {
            appendServerEvent(QStringLiteral("lifecycle"),
                QStringLiteral("Router: el perfil '%1' usa otro binario; se ignora, el router usa uno solo.")
                    .arg(ctx.launch.name));
        }

        QString name = slugify(ctx.launch.name);
        QString base = name; int n = 2;
        while (usedNames.contains(name)) name = base + QStringLiteral("-") + QString::number(n++);
        usedNames.insert(name);
        names << name;

        body += QStringLiteral("[") + name + QStringLiteral("]\n");
        body += argsToIniSection(ep.effectiveArgs);
        body += QStringLiteral("\n");
    }

    QString iniText = QStringLiteral("version = 1\n\n[*]\n");
    iniText += QStringLiteral("host = ") + host + QStringLiteral("\n");
    iniText += QStringLiteral("port = ") + QString::number(port) + QStringLiteral("\n");
    iniText += QStringLiteral("jinja = true\n\n");
    iniText += body;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/profiles");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/router_preset.ini");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit serverError(QStringLiteral("Router: no pude escribir %1").arg(path));
        return {};
    }
    f.write(iniText.toUtf8());
    f.close();

    m_routerModelNames = names;
    appendServerEvent(QStringLiteral("lifecycle"),
        QStringLiteral("Router preset generado: %1 modelos [%2]")
            .arg(names.size()).arg(names.join(QStringLiteral(", "))));
    return path;
}

void AppController::setRouterActiveModel(const QString &name)
{
    if (m_routerActiveModel == name) return;
    m_routerActiveModel = name;
    appendServerEvent(QStringLiteral("lifecycle"),
                      QStringLiteral("Router: modelo activo = %1").arg(name));
    emit routerStateChanged();
}

void AppController::startRouter(const QStringList &launchProfileIds, int modelsMax)
{
    if (serverRunning()) {
        emit serverError(QStringLiteral("Server already running. Stop it first."));
        return;
    }
    // Redetectar flags del binario del primer perfil (mismo motivo que startServer).
    {
        const auto launch  = m_profiles.resolveLaunch(launchProfileIds.value(0));
        const auto backend = m_profiles.resolveBackend(launch.backendProfileId);
        if (!backend.binaryId.isEmpty())
            m_binaries.detectCapabilitiesSync(backend.binaryId);
    }

    const QString iniPath = generateRouterPreset(launchProfileIds);
    if (iniPath.isEmpty()) return;   // generateRouterPreset ya emitió el error

    const EffectiveProfileBuilder::Context ctx0 = buildContext(launchProfileIds.first());
    const EffectiveProfile ep0 = EffectiveProfileBuilder::build(ctx0);
    const QString binaryPath = ep0.binaryPath;
    const QString host = ctx0.backend.host.isEmpty() ? QStringLiteral("127.0.0.1") : ctx0.backend.host;
    const int port = ctx0.backend.port > 0 ? ctx0.backend.port : 8080;

    QFileInfo bi(binaryPath);
    if (binaryPath.isEmpty() || !bi.exists() || !bi.isFile()) {
        emit serverError(QStringLiteral("Router: binario inexistente: %1").arg(binaryPath));
        return;
    }

    const QStringList args = {
        QStringLiteral("--models-preset"), iniPath,
        QStringLiteral("--models-max"), QString::number(modelsMax < 1 ? 1 : modelsMax),
        QStringLiteral("--host"), host,
        QStringLiteral("--port"), QString::number(port),
        QStringLiteral("--jinja"),
    };

    m_proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LLAMACODE_MANAGED"), QStringLiteral("1"));
    env.insert(QStringLiteral("LLAMACODE_ROLE"),    QStringLiteral("router"));
    env.insert(QStringLiteral("LLAMACODE_APP_PID"), QString::number(QCoreApplication::applicationPid()));
    m_proc->setProcessEnvironment(env);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        appendServerEvent(QStringLiteral("stdout"), QString::fromUtf8(m_proc->readAllStandardOutput()));
    });
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        appendServerEvent(QStringLiteral("stderr"), QString::fromUtf8(m_proc->readAllStandardError()));
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        appendServerEvent(QStringLiteral("lifecycle"),
                          QStringLiteral("Router exited with code %1").arg(code));
        clearServiceState(QStringLiteral("server"));
        stopHealthPolling();
        m_serverStopping = false;
        m_serverReady    = false;
        m_serverIsRouter = false;
        m_proc->deleteLater();
        m_proc = nullptr;
        emit serverRunningChanged();
        emit serverReadyChanged();
        emit routerStateChanged();
    });

    appendServerEvent(QStringLiteral("lifecycle"),
        QStringLiteral("Router: %1 %2").arg(binaryPath, args.join(QLatin1Char(' '))));
    m_proc->setProgram(binaryPath);
    m_proc->setArguments(args);
    if (!bi.absolutePath().isEmpty()) m_proc->setWorkingDirectory(bi.absolutePath());
    m_serverIsRouter = true;
    if (m_routerActiveModel.isEmpty() && !m_routerModelNames.isEmpty())
        m_routerActiveModel = m_routerModelNames.first();
    m_proc->start();
    emit serverRunningChanged();
    emit routerStateChanged();
}

QVariantMap AppController::launchPortStatus(const QString &launchProfileId)
{
    computeEffectiveProfile(launchProfileId);

    QVariantMap result;
    result[QStringLiteral("valid")] = m_effectiveProfile.value(QStringLiteral("isValid"), false).toBool();
    result[QStringLiteral("available")] = false;
    result[QStringLiteral("blocked")] = false;
    result[QStringLiteral("port")] = 8080;
    result[QStringLiteral("suggestedPort")] = 0;
    result[QStringLiteral("host")] = QStringLiteral("127.0.0.1");

    if (!result.value(QStringLiteral("valid")).toBool()) {
        result[QStringLiteral("message")] =
            m_effectiveProfile.value(QStringLiteral("blockingErrors")).toStringList().join(QStringLiteral("; "));
        return result;
    }

    const QStringList args = m_effectiveProfile.value(QStringLiteral("effectiveArgs")).toStringList();
    const int portIdx = args.indexOf(QStringLiteral("--port"));
    const quint16 port = (portIdx >= 0 && portIdx + 1 < args.size())
        ? static_cast<quint16>(args[portIdx + 1].toInt())
        : 8080;
    const int hostIdx = args.indexOf(QStringLiteral("--host"));
    const QString host = (hostIdx >= 0 && hostIdx + 1 < args.size())
        ? args[hostIdx + 1]
        : QStringLiteral("127.0.0.1");

    result[QStringLiteral("port")] = port;
    result[QStringLiteral("host")] = host;

    QHostAddress addr(host);
    if (host == QStringLiteral("0.0.0.0") || host.isEmpty())
        addr = QHostAddress(QHostAddress::Any);
    else if (host == QStringLiteral("localhost"))
        addr = QHostAddress(QHostAddress::LocalHost);

    QTcpServer probe;
    QString firstError;
    const quint32 maxPort = qMin<quint32>(65535, static_cast<quint32>(port) + 100);
    for (quint32 candidate = port; candidate <= maxPort; ++candidate) {
        if (probe.listen(addr, static_cast<quint16>(candidate))) {
            probe.close();
            if (candidate == port) {
                result[QStringLiteral("available")] = true;
                result[QStringLiteral("suggestedPort")] = port;
            } else {
                result[QStringLiteral("blocked")] = true;
                result[QStringLiteral("suggestedPort")] = static_cast<int>(candidate);
                result[QStringLiteral("message")] = QStringLiteral("El puerto %1 está ocupado.").arg(port);
            }
            return result;
        }
        if (candidate == port)
            firstError = probe.errorString();
    }

    result[QStringLiteral("blocked")] = true;
    result[QStringLiteral("message")] =
        QStringLiteral("El puerto %1 está ocupado y no encontré un puerto libre hasta %2. %3")
            .arg(port)
            .arg(maxPort)
            .arg(firstError);
    return result;
}

bool AppController::setLaunchBackendPort(const QString &launchProfileId, int port)
{
    if (port <= 0 || port > 65535) {
        emit serverError(QStringLiteral("Puerto inválido: %1").arg(port));
        return false;
    }

    const LaunchProfile launch = m_profiles.resolveLaunch(launchProfileId);
    if (launch.id.isEmpty()) {
        emit serverError(QStringLiteral("Perfil de lanzamiento no encontrado."));
        return false;
    }

    const BackendProfile backend = m_profiles.resolveBackend(launch.backendProfileId);
    if (backend.id.isEmpty()) {
        emit serverError(QStringLiteral("Backend del perfil no encontrado."));
        return false;
    }

    const bool ok = m_profiles.updateBackend(backend.id, backend.name, backend.binaryId,
                                             backend.host, port, backend.baseArgs);
    if (ok)
        computeEffectiveProfile(launchProfileId);
    else
        emit serverError(QStringLiteral("No se pudo actualizar el puerto del backend."));
    return ok;
}

void AppController::startServer(const QString &launchProfileId)
{
    if (serverRunning()) {
        appendServerEvent(QStringLiteral("lifecycle"),
                          QStringLiteral("startServer abort: servidor ya en ejecución (pid=%1)")
                              .arg(m_proc ? QString::number(m_proc->processId()) : QStringLiteral("?")));
        emit serverError("Server already running. Stop it first.");
        return;
    }
    m_serverIsRouter = false;   // modo normal: un modelo por server

    // Repoblar flags soportados del binario justo antes de armar el perfil.
    // Si el registro quedó con flags stale/parciales, EffectiveProfileBuilder::addFlag
    // dropea flags de runtime (--flash-attn/--mlock/--cache-type-k/...) → server roto.
    {
        const auto launch  = m_profiles.resolveLaunch(launchProfileId);
        const auto backend = m_profiles.resolveBackend(launch.backendProfileId);
        if (!backend.binaryId.isEmpty()) {
            if (!m_binaries.detectCapabilitiesSync(backend.binaryId))
                appendServerEvent(QStringLiteral("lifecycle"),
                                  QStringLiteral("Aviso: no se pudieron redetectar flags del binario; uso los guardados."));
        }
    }

    computeEffectiveProfile(launchProfileId);

    if (!m_effectiveProfile.value("isValid", false).toBool()) {
        const QStringList errors = m_effectiveProfile.value("blockingErrors").toStringList();
        // Diagnóstico: dump resolución modelo/catálogo para distinguir
        // "catálogo vacío" vs "modelId no resuelve".
        const auto ctx = buildContext(launchProfileId);
        appendServerEvent(QStringLiteral("lifecycle"),
            QStringLiteral("startServer abort: perfil inválido (%1): %2 | diag: catalogCount=%3 "
                           "modelProfileId=%4 model.modelId=%5 catalogModel.id=%6 isAvailable=%7")
                .arg(launchProfileId,
                     errors.join(QStringLiteral("; ")),
                     QString::number(m_catalog.count()),
                     ctx.launch.modelProfileId,
                     ctx.model.modelId,
                     ctx.catalogModel.id,
                     ctx.catalogModel.id.isEmpty() ? QStringLiteral("n/a")
                                                   : (ctx.catalogModel.isAvailable ? QStringLiteral("1")
                                                                                   : QStringLiteral("0"))));
        emit serverError("Cannot start: " + errors.join("; "));
        return;
    }

    const QString binaryPath = m_effectiveProfile["binaryPath"].toString();
    QStringList args = m_effectiveProfile["effectiveArgs"].toStringList();
    const QVariantMap envMap = m_effectiveProfile["effectiveEnv"].toMap();
    m_serverGpuRequested = false;
    const int nglIdx = args.indexOf(QStringLiteral("--n-gpu-layers"));
    if (nglIdx >= 0 && nglIdx + 1 < args.size())
        m_serverGpuRequested = args[nglIdx + 1].toInt() != 0;
    m_serverGpuDeviceSeen = false;
    m_serverCpuDeviceSeen = false;
    m_serverGpuFallbackWarned = false;

    // Diagnóstico temprano del binario: existe, es archivo y es ejecutable.
    {
        QFileInfo bi(binaryPath);
        if (binaryPath.isEmpty() || !bi.exists() || !bi.isFile()) {
            appendServerEvent(QStringLiteral("lifecycle"),
                              QStringLiteral("startServer abort: binario inexistente: %1")
                                  .arg(binaryPath.isEmpty() ? QStringLiteral("(vacío)") : binaryPath));
            emit serverError(QStringLiteral(
                "El binario del perfil no existe: %1. Revisá la ruta en la página Binarios.")
                                 .arg(binaryPath.isEmpty() ? QStringLiteral("(vacío)") : binaryPath));
            return;
        }
        if (!bi.isExecutable()) {
            appendServerEvent(QStringLiteral("lifecycle"),
                              QStringLiteral("startServer abort: binario no ejecutable: %1").arg(binaryPath));
            emit serverError(QStringLiteral(
                "El binario no es ejecutable: %1.").arg(binaryPath));
            return;
        }
    }

    // Pre-check de colisión de puerto. La UI llama launchPortStatus() antes de
    // iniciar y pide confirmación si hay que cambiar de puerto; acá sólo evitamos
    // que una invocación directa lance un server que va a fallar al bindear.
    {
        const int portIdx = args.indexOf(QStringLiteral("--port"));
        const quint16 port = (portIdx >= 0 && portIdx + 1 < args.size())
                                 ? static_cast<quint16>(args[portIdx + 1].toInt()) : 8080;
        const int hostIdx = args.indexOf(QStringLiteral("--host"));
        const QString host = (hostIdx >= 0 && hostIdx + 1 < args.size())
                                 ? args[hostIdx + 1] : QStringLiteral("127.0.0.1");
        QString listenError;
        if (!canListenOnPort(host, port, &listenError)) {
            const int suggestedPort = nextAvailablePort(host, port);
            appendServerEvent(QStringLiteral("lifecycle"),
                              QStringLiteral("startServer abort: puerto %1 (%2) ocupado: %3")
                                  .arg(QString::number(port), host, listenError));
            const bool wantsAgent = !m_pendingAutoAgentLaunchId.isEmpty();
            if (suggestedPort > 0) {
                emit serverPortCollision(launchProfileId, host, port, suggestedPort, wantsAgent);
            }
            const QString msg = suggestedPort > 0
                ? QStringLiteral("El puerto %1 ya está en uso. Podés cambiar el perfil al puerto %2 y reintentar.")
                      .arg(port).arg(suggestedPort)
                : QStringLiteral("El puerto %1 ya está en uso. Cerrá el proceso que lo ocupa o cambiá el puerto del perfil.")
                      .arg(port);
            emit serverError(msg);
            return;
        }
    }

    m_proc = new QProcess(this);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = envMap.begin(); it != envMap.end(); ++it)
        env.insert(it.key(), it.value().toString());
    env.insert(QStringLiteral("LLAMACODE_MANAGED"), QStringLiteral("1"));
    env.insert(QStringLiteral("LLAMACODE_ROLE"),    QStringLiteral("server"));
    env.insert(QStringLiteral("LLAMACODE_APP_PID"), QString::number(QCoreApplication::applicationPid()));
    m_proc->setProcessEnvironment(env);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        appendServerEvent(QStringLiteral("stdout"), QString::fromUtf8(m_proc->readAllStandardOutput()));
    });
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        appendServerEvent(QStringLiteral("stderr"), QString::fromUtf8(m_proc->readAllStandardError()));
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
        appendServerEvent(QStringLiteral("lifecycle"),
                          QStringLiteral("Server exited with code %1").arg(code));
        if (code == -1073740791) {
            emit serverError(QStringLiteral(
                "llama-server crasheó (0xC0000409). Revisá el perfil de lanzamiento activo (args/runtime)."));
        }
        clearServiceState(QStringLiteral("server"));
        // ¿Salida iniciada por el usuario (stopServer) o crash inesperado?
        const bool userStop = m_serverStopping;
        const bool crashed = !userStop &&
                             (status == QProcess::CrashExit || code != 0);
        const QString crashLaunchId = m_activeLaunchId;
        if (!m_pendingAutoAgentLaunchId.isEmpty()) {
            m_pendingAutoAgentLaunchId.clear();
            m_agentStarting = false;
            emit agentStartingChanged();
        }
        if (m_stopKillTimer) { m_stopKillTimer->stop(); m_stopKillTimer->deleteLater(); m_stopKillTimer = nullptr; }
        stopHealthPolling();
        stopVramPolling();
        m_serverStopping = false;
        m_serverReady    = false;
        m_proc->deleteLater();
        m_proc = nullptr;
        emit serverRunningChanged();
        emit serverReadyChanged();

        // Watchdog: auto-restart en crash, con backoff y tope de intentos.
        if (crashed && !crashLaunchId.isEmpty()) {
            if (m_serverRestartCount < kMaxServerRestarts) {
                m_serverRestartCount++;
                const int delayMs = 2000 * m_serverRestartCount; // 2s, 4s, 6s
                setServerState(QStringLiteral("restarting"));
                appendServerEvent(QStringLiteral("lifecycle"),
                    QStringLiteral("Watchdog: reintento %1/%2 en %3ms")
                        .arg(m_serverRestartCount).arg(kMaxServerRestarts).arg(delayMs));
                if (!m_serverRestartTimer) {
                    m_serverRestartTimer = new QTimer(this);
                    m_serverRestartTimer->setSingleShot(true);
                }
                m_serverRestartTimer->disconnect();
                connect(m_serverRestartTimer, &QTimer::timeout, this, [this, crashLaunchId]() {
                    if (serverRunning()) return; // ya arrancó por otra vía
                    startServer(crashLaunchId);
                });
                m_serverRestartTimer->start(delayMs);
            } else {
                setServerState(QStringLiteral("failed"));
                appendServerEvent(QStringLiteral("lifecycle"),
                    QStringLiteral("Watchdog: agotados %1 reintentos; server en estado failed.")
                        .arg(kMaxServerRestarts));
                emit serverError(QStringLiteral(
                    "El servidor crasheó %1 veces seguidas. Auto-reinicio detenido. Revisá el perfil.")
                        .arg(kMaxServerRestarts));
            }
        } else {
            setServerState(QStringLiteral("stopped"));
        }
    });

    // Capacidad de visión del modelo activo: el server cargó un mmproj.
    const bool vision = args.contains(QStringLiteral("--mmproj"));
    if (vision != m_serverHasVision) { m_serverHasVision = vision; emit serverHasVisionChanged(); }

    m_activeLaunchId = launchProfileId;
    writeSetting(QStringLiteral("lastLaunchId"), launchProfileId);  // recordar último usado

    // Power limit de GPU: override del perfil o global de Ajustes (antes de cargar
    // el modelo, para que el server arranque ya con el límite aplicado).
    applyConfiguredPowerLimit(m_profiles.resolveLaunch(launchProfileId));
    appendServerEvent(QStringLiteral("lifecycle"),
                      QStringLiteral("Starting: %1 %2").arg(binaryPath, args.join(' ')));

    m_serverReady = false;
    m_proc->start(binaryPath, args);
    if (m_proc->waitForStarted(5000)) {
        assignToJobObject(m_proc->processId());
        const int portIdx = args.indexOf(QStringLiteral("--port"));
        QVariantMap extra;
        extra[QStringLiteral("launch_id")] = launchProfileId;
        extra[QStringLiteral("port")] = (portIdx >= 0 && portIdx + 1 < args.size())
                                            ? args[portIdx + 1].toInt() : 8080;
        writeServiceState(QStringLiteral("server"), m_proc->processId(), extra);
        startHealthPolling();
        startVramPolling();
        setServerState(QStringLiteral("running"));
    }
    emit serverRunningChanged();
    emit serverReadyChanged();
    emit activeLaunchIdChanged();
}

void AppController::startHealthPolling()
{
    stopHealthPolling();
    m_healthPollTimer = new QTimer(this);
    m_healthPollTimer->setInterval(2000);
    connect(m_healthPollTimer, &QTimer::timeout, this, [this]() {
        if (!serverRunning()) { stopHealthPolling(); return; }
        if (!m_nam) m_nam = new QNetworkAccessManager(this);
        auto *reply = m_nam->get(QNetworkRequest(QUrl(serverBaseUrl() + "/health")));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool ok = reply->error() == QNetworkReply::NoError && status == 200;
            const QString errStr = reply->errorString();
            reply->deleteLater();
            if (!ok) {
                appendServerEvent(QStringLiteral("health"),
                                  QStringLiteral("health fail status=%1 error=%2")
                                      .arg(status).arg(errStr));
            }
            if (ok && !m_serverReady) {
                m_serverReady = true;
                m_serverRestartCount = 0; // arrancó sano: resetear watchdog
                setServerState(QStringLiteral("running"));
                appendServerEvent(QStringLiteral("health"), QStringLiteral("Server ready - model loaded"));
                emit serverReadyChanged();
                stopHealthPolling();
                fetchChatThinkingSupport();
                if (!m_pendingAutoAgentLaunchId.isEmpty() && !agentRunning()) {
                    const QString launchId = m_pendingAutoAgentLaunchId;
                    m_pendingAutoAgentLaunchId.clear();
                    emit agentStartingChanged();
                    appendAgentEvent(QStringLiteral("lifecycle"),
                                     QStringLiteral("Servidor listo; iniciando agente del perfil."));
                    startAgent(launchId);
                }
            }
        });
    });
    m_healthPollTimer->start();
}

void AppController::startServerAndAgent(const QString &launchProfileId)
{
    const auto ctx = buildContext(launchProfileId);
    const QString adapter = ctx.harness.adapter.trimmed();
    const bool hasAgent = !adapter.isEmpty()
                          && adapter != QLatin1String("none")
                          && adapter != QLatin1String("raw");

    if (serverRunning()) {
        if (hasAgent && !agentRunning())
            startAgent(launchProfileId);
        return;
    }

    m_pendingAutoAgentLaunchId = hasAgent ? launchProfileId : QString();
    if (hasAgent) {
        m_agentStarting = true;
        emit agentStartingChanged();
        appendAgentEvent(QStringLiteral("lifecycle"),
                         QStringLiteral("Agente programado para iniciar cuando el servidor este listo."));
    }

    startServer(launchProfileId);

    if (!serverRunning()) {
        m_pendingAutoAgentLaunchId.clear();
        if (m_agentStarting) {
            m_agentStarting = false;
            emit agentStartingChanged();
        }
        return;
    }

    if (m_serverReady && !m_pendingAutoAgentLaunchId.isEmpty() && !agentRunning()) {
        const QString launchId = m_pendingAutoAgentLaunchId;
        m_pendingAutoAgentLaunchId.clear();
        emit agentStartingChanged();
        startAgent(launchId);
    }
}

bool AppController::useSuggestedServerPort(const QString &launchProfileId, int port,
                                           bool startAgent)
{
    if (port <= 0 || port > 65535) {
        emit serverError(QStringLiteral("Puerto inválido: %1.").arg(port));
        return false;
    }
    if (serverRunning()) {
        emit serverError(QStringLiteral("Server already running. Stop it first."));
        return false;
    }

    const LaunchProfile launch = m_profiles.resolveLaunch(launchProfileId);
    if (launch.id.isEmpty() || launch.backendProfileId.isEmpty()) {
        emit serverError(QStringLiteral("No se pudo resolver el backend del perfil."));
        return false;
    }
    const BackendProfile backend = m_profiles.resolveBackend(launch.backendProfileId);
    if (backend.id.isEmpty()) {
        emit serverError(QStringLiteral("No se pudo resolver el backend del perfil."));
        return false;
    }
    if (!canListenOnPort(backend.host, static_cast<quint16>(port))) {
        emit serverError(QStringLiteral("El puerto %1 también está en uso.").arg(port));
        return false;
    }
    if (!m_profiles.updateBackendPort(backend.id, port)) {
        emit serverError(QStringLiteral("No se pudo actualizar el puerto del perfil."));
        return false;
    }

    computeEffectiveProfile(launchProfileId);
    appendServerEvent(QStringLiteral("lifecycle"),
                      QStringLiteral("Perfil actualizado: backend %1 usa puerto %2.")
                          .arg(backend.name, QString::number(port)));
    if (startAgent)
        startServerAndAgent(launchProfileId);
    else
        startServer(launchProfileId);
    return true;
}

void AppController::fetchChatThinkingSupport()
{
    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    auto *reply = m_nam->get(QNetworkRequest(QUrl(serverBaseUrl() + "/props")));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        bool supported = false;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            const QJsonObject params = root.value(QStringLiteral("default_generation_settings"))
                                           .toObject().value(QStringLiteral("params")).toObject();
            const QString chatFormat = params.value(QStringLiteral("chat_format")).toString();
            // Content-only = sin template (jinja) → no soporta thinking.
            supported = !chatFormat.isEmpty()
                        && chatFormat.compare(QStringLiteral("Content-only"), Qt::CaseInsensitive) != 0;
        }
        if (supported != m_chatThinkingSupported) {
            m_chatThinkingSupported = supported;
            emit chatThinkingSupportedChanged();
        }
    });
}

void AppController::stopHealthPolling()
{
    if (m_healthPollTimer) {
        m_healthPollTimer->stop();
        m_healthPollTimer->deleteLater();
        m_healthPollTimer = nullptr;
    }
}

void AppController::setServerState(const QString &s)
{
    if (m_serverState == s) return;
    m_serverState = s;
    emit serverStateChanged();
}

void AppController::startVramPolling()
{
    stopVramPolling();
    m_vramPollTimer = new QTimer(this);
    m_vramPollTimer->setInterval(2000);
    connect(m_vramPollTimer, &QTimer::timeout, this, [this]() {
        if (!serverRunning()) { stopVramPolling(); return; }
        pollServerStats();
    });
    m_vramPollTimer->start();
    pollServerStats(); // primera muestra inmediata
}

void AppController::stopVramPolling()
{
    if (m_vramPollTimer) {
        m_vramPollTimer->stop();
        m_vramPollTimer->deleteLater();
        m_vramPollTimer = nullptr;
    }
    if (m_vramProc) {
        m_vramProc->disconnect();
        m_vramProc->kill();
        m_vramProc->deleteLater();
        m_vramProc = nullptr;
    }
    if (!m_serverStats.isEmpty()) {
        m_serverStats.clear();
        emit serverStatsChanged();
    }
}

// Lee VRAM por GPU vía nvidia-smi (async, no bloquea la GUI). Publica m_serverStats:
//   { gpus: [{index,name,totalMb,usedMb,pct}], totalMb, usedMb, pct }
void AppController::pollServerStats()
{
    if (m_vramProc) return; // muestra anterior aún en curso; saltear
    const QString nvidiaSmi = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
    if (nvidiaSmi.isEmpty()) return;
    m_vramProc = new QProcess(this);
    connect(m_vramProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        if (!m_vramProc) return;
        const QString text = QString::fromUtf8(m_vramProc->readAllStandardOutput());
        m_vramProc->deleteLater();
        m_vramProc = nullptr;

        QVariantList gpus;
        double sumTotal = 0, sumUsed = 0, sumDraw = 0, sumLimit = 0;
        const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList p = line.split(QLatin1Char(','));
            if (p.size() < 4) continue;
            bool iOk = false, tOk = false, uOk = false;
            const int idx = p.at(0).trimmed().toInt(&iOk);
            const QString name = p.at(1).trimmed();
            const double totalMb = p.at(2).trimmed().toDouble(&tOk);
            const double usedMb  = p.at(3).trimmed().toDouble(&uOk);
            if (!iOk || !tOk || !uOk) continue;
            // power.draw / power.limit pueden venir "[N/A]" en GPUs sin telemetría.
            const double drawW  = p.size() > 4 ? p.at(4).trimmed().toDouble() : 0.0;
            const double limitW = p.size() > 5 ? p.at(5).trimmed().toDouble() : 0.0;
            QVariantMap g;
            g[QStringLiteral("index")]   = idx;
            g[QStringLiteral("name")]    = name;
            g[QStringLiteral("totalMb")] = totalMb;
            g[QStringLiteral("usedMb")]  = usedMb;
            g[QStringLiteral("pct")]     = totalMb > 0 ? (usedMb / totalMb * 100.0) : 0.0;
            g[QStringLiteral("drawW")]   = drawW;
            g[QStringLiteral("limitW")]  = limitW;
            g[QStringLiteral("powerPct")] = limitW > 0 ? (drawW / limitW * 100.0) : 0.0;
            gpus.append(g);
            sumTotal += totalMb;
            sumUsed  += usedMb;
            sumDraw  += drawW;
            sumLimit += limitW;
        }
        QVariantMap stats;
        stats[QStringLiteral("gpus")]    = gpus;
        stats[QStringLiteral("totalMb")] = sumTotal;
        stats[QStringLiteral("usedMb")]  = sumUsed;
        stats[QStringLiteral("pct")]     = sumTotal > 0 ? (sumUsed / sumTotal * 100.0) : 0.0;
        stats[QStringLiteral("drawW")]   = sumDraw;
        stats[QStringLiteral("limitW")]  = sumLimit;
        stats[QStringLiteral("powerPct")] = sumLimit > 0 ? (sumDraw / sumLimit * 100.0) : 0.0;
        m_serverStats = stats;
        emit serverStatsChanged();
    });
    m_vramProc->start(nvidiaSmi,
                      {QStringLiteral("--query-gpu=index,name,memory.total,memory.used,"
                                      "power.draw,power.limit"),
                       QStringLiteral("--format=csv,noheader,nounits")});
}

// Parser estático: cada línea CSV es
//   index, name, power.limit, power.default_limit, power.min_limit, power.max_limit, power.draw
// (formato csv,noheader,nounits). Tolera campos "[N/A]" → 0.
QVariantList AppController::parseGpuPowerCsv(const QString &csv)
{
    QVariantList gpus;
    const QStringList lines = csv.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList p = line.split(QLatin1Char(','));
        if (p.size() < 6) continue;
        bool iOk = false;
        const int idx = p.at(0).trimmed().toInt(&iOk);
        if (!iOk) continue;
        auto w = [&](int i) { return i < p.size() ? p.at(i).trimmed().toDouble() : 0.0; };
        QVariantMap g;
        g[QStringLiteral("index")]    = idx;
        g[QStringLiteral("name")]     = p.at(1).trimmed();
        g[QStringLiteral("currentW")] = w(2);
        g[QStringLiteral("defaultW")] = w(3);
        g[QStringLiteral("minW")]     = w(4);
        g[QStringLiteral("maxW")]     = w(5);
        g[QStringLiteral("drawW")]    = p.size() > 6 ? w(6) : 0.0;
        gpus.append(g);
    }
    return gpus;
}

QVariantMap AppController::gpuPowerInfo() const
{
    QVariantMap result;
    const QString nvidiaSmi = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
    if (nvidiaSmi.isEmpty()) {
        result[QStringLiteral("available")] = false;
        result[QStringLiteral("gpus")] = QVariantList();
        return result;
    }
    QProcess p;
    p.start(nvidiaSmi,
            {QStringLiteral("--query-gpu=index,name,power.limit,power.default_limit,"
                            "power.min_limit,power.max_limit,power.draw"),
             QStringLiteral("--format=csv,noheader,nounits")});
    QVariantList gpus;
    if (p.waitForFinished(4000))
        gpus = parseGpuPowerCsv(QString::fromUtf8(p.readAllStandardOutput()));
    result[QStringLiteral("available")] = !gpus.isEmpty();
    result[QStringLiteral("gpus")] = gpus;
    return result;
}

QString AppController::setGpuPowerLimit(int watts, int gpuIndex)
{
    const QString nvidiaSmi = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
    if (nvidiaSmi.isEmpty())
        return QStringLiteral("nvidia-smi no encontrado (¿GPU NVIDIA / drivers instalados?).");
    if (watts <= 0)
        return QStringLiteral("Valor de power limit inválido: %1 W").arg(watts);

    // Persistir como setting global (re-aplicado al iniciar la app / server).
    writeSetting(QStringLiteral("gpuPowerLimitW"), watts);

    QStringList args;
    if (gpuIndex >= 0) args << QStringLiteral("-i") << QString::number(gpuIndex);
    args << QStringLiteral("-pl") << QString::number(watts);

#ifdef Q_OS_WIN
    // En Windows fijar el power limit requiere privilegios de administrador. Se
    // relanza nvidia-smi elevado vía powershell Start-Process -Verb RunAs (UAC).
    const QString argStr = QStringLiteral("'") + args.join(QStringLiteral("','")) + QStringLiteral("'");
    const QString script = QStringLiteral(
        "$p = Start-Process -FilePath '%1' -ArgumentList %2 -Verb RunAs -Wait -PassThru; exit $p.ExitCode")
            .arg(nvidiaSmi, argStr);
    QProcess ps;
    ps.start(QStringLiteral("powershell"),
             {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
              QStringLiteral("Bypass"), QStringLiteral("-Command"), script});
    if (!ps.waitForFinished(30000))
        return QStringLiteral("Timeout al aplicar power limit (UAC no respondido).");
    if (ps.exitCode() != 0)
        return QStringLiteral("nvidia-smi falló (¿UAC cancelado o valor fuera de rango?). "
                              "El valor debe estar entre min/max que reporta la placa.");
#else
    QProcess proc;
    proc.start(nvidiaSmi, args);
    if (!proc.waitForFinished(15000))
        return QStringLiteral("Timeout al aplicar power limit.");
    if (proc.exitCode() != 0)
        return QStringLiteral("nvidia-smi falló: %1 (¿requiere sudo o valor fuera de rango?).")
            .arg(QString::fromUtf8(proc.readAllStandardError()).trimmed());
#endif
    appendServerEvent(QStringLiteral("lifecycle"),
                      QStringLiteral("GPU power limit fijado en %1 W%2")
                          .arg(watts)
                          .arg(gpuIndex >= 0 ? QStringLiteral(" (GPU %1)").arg(gpuIndex)
                                             : QString()));
    return QString();
}

void AppController::applyConfiguredPowerLimit(const LaunchProfile &launch)
{
    int watts = launch.powerLimitW;
    if (watts <= 0)
        watts = readSetting(QStringLiteral("gpuPowerLimitW"), 0).toInt();
    if (watts <= 0) return;   // sin override ni global → no tocar la placa

    if (QStandardPaths::findExecutable(QStringLiteral("nvidia-smi")).isEmpty()) {
        appendServerEvent(QStringLiteral("lifecycle"),
                          QStringLiteral("Power limit %1 W solicitado pero nvidia-smi no está disponible.")
                              .arg(watts));
        return;
    }
    const QString err = setGpuPowerLimit(watts, -1);
    if (!err.isEmpty())
        appendServerEvent(QStringLiteral("lifecycle"),
                          QStringLiteral("No se pudo aplicar power limit: %1").arg(err));
}

void AppController::stopServer()
{
    if (!m_proc || m_serverStopping) return;
    // Parada explícita: cancelar watchdog para que no auto-reinicie.
    if (m_serverRestartTimer) m_serverRestartTimer->stop();
    m_serverRestartCount = 0;
    if (m_agentStarting || !m_pendingAutoAgentLaunchId.isEmpty()) {
        m_agentStarting = false;
        m_pendingAutoAgentLaunchId.clear();
        emit agentStartingChanged();
    }
    stopHealthPolling();
    stopVramPolling();
    m_serverReady    = false;
    m_serverStopping = true;
    if (m_serverHasVision) { m_serverHasVision = false; emit serverHasVisionChanged(); }
    if (m_chatThinkingSupported) { m_chatThinkingSupported = false; emit chatThinkingSupportedChanged(); }
    emit serverReadyChanged();
    emit serverRunningChanged();
    appendServerEvent(QStringLiteral("lifecycle"), QStringLiteral("Stopping server..."));
    m_proc->terminate();
    // Kill after 5s if process hasn't exited
    m_stopKillTimer = new QTimer(this);
    m_stopKillTimer->setSingleShot(true);
    connect(m_stopKillTimer, &QTimer::timeout, this, [this]() {
        if (m_proc) m_proc->kill();
        m_stopKillTimer->deleteLater();
        m_stopKillTimer = nullptr;
    });
    m_stopKillTimer->start(5000);
}

void AppController::computeEffectiveProfile(const QString &launchProfileId)
{
    const auto ctx = buildContext(launchProfileId);
    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);

    m_effectiveProfile.clear();
    m_effectiveProfile["launchId"] = launchProfileId;
    m_effectiveProfile["isValid"] = ep.isValid();
    m_effectiveProfile["binaryPath"] = ep.binaryPath;
    m_effectiveProfile["commandLine"] = ep.commandLine;
    m_effectiveProfile["effectiveArgs"] = ep.effectiveArgs;
    m_effectiveProfile["warnings"] = ep.warnings;
    m_effectiveProfile["blockingErrors"] = ep.blockingErrors;

    QVariantMap envMap;
    for (auto it = ep.effectiveEnv.begin(); it != ep.effectiveEnv.end(); ++it)
        envMap[it.key()] = it.value();
    m_effectiveProfile["effectiveEnv"] = envMap;

    emit effectiveProfileChanged();
}

void AppController::computeEffectiveProfilePreview(const QString &launchProfileId,
                                                   const QVariantMap &overrides)
{
    EffectiveProfileBuilder::Context ctx;

    ctx.backend.host = overrides.value("host", "127.0.0.1").toString();
    ctx.backend.port = overrides.value("port", 8080).toInt();
    const QString binaryId = overrides.value("binaryId").toString();
    ctx.backend.binaryId = binaryId;
    ctx.binary = m_binaries.findById(binaryId);

    ctx.model.modelId    = overrides.value("modelId").toString();
    ctx.model.mmprojId   = overrides.value("mmprojId").toString();
    ctx.model.draftModelId = overrides.value("draftModelId").toString();
    ctx.model.specType       = overrides.value("specType").toString();
    ctx.model.specDraftNMax  = overrides.value("specDraftNMax", 0).toInt();
    ctx.model.specDraftNgl   = overrides.value("specDraftNgl").toString();
    ctx.model.specDraftTypeK = overrides.value("specDraftTypeK").toString();
    ctx.model.specDraftTypeV = overrides.value("specDraftTypeV").toString();
    ctx.catalogModel = m_catalog.findById(ctx.model.modelId);
    ctx.mmprojModel  = m_catalog.findById(ctx.model.mmprojId);
    ctx.draftModel   = m_catalog.findById(ctx.model.draftModelId);

    ctx.runtime.ctx           = overrides.value("ctx", 4096).toInt();
    ctx.runtime.batch         = overrides.value("batch", 512).toInt();
    ctx.runtime.ubatch        = overrides.value("ubatch", 512).toInt();
    ctx.runtime.threads       = overrides.value("threads", -1).toInt();
    ctx.runtime.gpuLayers     = overrides.value("gpuLayers", -1).toInt();
    ctx.runtime.flashAttention= overrides.value("flashAttention", false).toBool();
    ctx.runtime.mmap          = overrides.value("mmap", true).toBool();
    ctx.runtime.mlock         = overrides.value("mlock", false).toBool();
    ctx.runtime.contBatching  = overrides.value("contBatching", true).toBool();
    ctx.runtime.cacheType     = overrides.value("cacheType", "f16").toString();
    ctx.runtime.parallelSlots = overrides.value("parallelSlots", 1).toInt();

    ctx.launch.extraArgs = overrides.value("extraArgs").toStringList();
    ctx.reasoningEnabled = overrides.value(QStringLiteral("thinkingEnabled"),
                                           m_launchThinkingEnabled).toBool();

    const EffectiveProfile ep = EffectiveProfileBuilder::build(ctx);

    m_effectiveProfile.clear();
    m_effectiveProfile["launchId"] = launchProfileId;
    m_effectiveProfile["isValid"] = ep.isValid();
    m_effectiveProfile["binaryPath"] = ep.binaryPath;
    m_effectiveProfile["commandLine"] = ep.commandLine;
    m_effectiveProfile["effectiveArgs"] = ep.effectiveArgs;
    m_effectiveProfile["warnings"] = ep.warnings;
    m_effectiveProfile["blockingErrors"] = ep.blockingErrors;

    QVariantMap envMap;
    for (auto it = ep.effectiveEnv.begin(); it != ep.effectiveEnv.end(); ++it)
        envMap[it.key()] = it.value();
    m_effectiveProfile["effectiveEnv"] = envMap;

    emit effectiveProfileChanged();
}

void AppController::clearLog()
{
    m_log.clear();
    emit serverLogChanged();
}

void AppController::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void AppController::openContainingFolder(const QString &path)
{
    if (path.trimmed().isEmpty()) return;
    const QFileInfo fi(path);
    const QString dir = fi.absolutePath();
    if (dir.isEmpty()) return;

#ifdef Q_OS_WIN
    // explorer /select,<archivo> abre la carpeta y selecciona el archivo.
    if (fi.exists()) {
        const QString nativeFile = QDir::toNativeSeparators(fi.absoluteFilePath());
        if (QProcess::startDetached(QStringLiteral("explorer.exe"),
                                    {QStringLiteral("/select,") + nativeFile}))
            return;
    }
#endif

    bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
#ifdef Q_OS_WIN
    if (!ok)
        ok = QProcess::startDetached(QStringLiteral("explorer.exe"),
                                     {QDir::toNativeSeparators(dir)});
#endif
    if (!ok)
        emit serverError(QStringLiteral("No se pudo abrir la carpeta: %1")
                             .arg(QDir::toNativeSeparators(dir)));
}

void AppController::installOfficialBinary()
{
    if (m_installingOfficialBinary || m_installerProc) {
        emit serverError("Binary install already in progress.");
        return;
    }

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString toolsDir = appData + "/tools";
    QDir().mkpath(toolsDir);

    const QString script = QStringLiteral(R"PS(
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try {
    $headers = @{ 'User-Agent' = 'LlamaCode' }
    Write-Output 'STATUS: Consultando release latest de llama.cpp...'
    $api = 'https://api.github.com/repos/ggml-org/llama.cpp/releases/latest'
    $rel = Invoke-RestMethod -Uri $api -Headers $headers
    $tag = [string]$rel.tag_name
    $assets = @($rel.assets)
    $pick = $null
    $hasNvidia = $false
    try { $null = Get-Command nvidia-smi -ErrorAction Stop; $hasNvidia = $true } catch {}
    if ($hasNvidia) {
        $pick = $assets | Where-Object { $_.name -match 'bin-win-cuda.*x64.*\.zip$' -and $_.name -notmatch '^cudart-' } | Select-Object -First 1
    }
    if (-not $pick) { $pick = $assets | Where-Object { $_.name -match 'bin-win-(avx2|cpu|openblas).*x64.*\.zip$' -and $_.name -notmatch '^cudart-' } | Select-Object -First 1 }
    if (-not $pick) { $pick = $assets | Where-Object { $_.name -match 'win.*x64.*\.zip$' -and $_.name -notmatch '^cudart-' } | Select-Object -First 1 }
    if (-not $pick) { throw 'No suitable Windows x64 binary asset found in latest release.' }

    Write-Output ('STATUS: Descargando asset ' + $pick.name + ' ...')
    $dest = '%1'
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    $runId = [DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff')
    $runDir = Join-Path $dest ('llama.cpp-install-' + $runId)
    New-Item -ItemType Directory -Force -Path $runDir | Out-Null
    $zip = Join-Path $runDir $pick.name
    $extract = Join-Path $runDir 'extract'
    if (Test-Path $extract) { Remove-Item -LiteralPath $extract -Recurse -Force -ErrorAction SilentlyContinue }
    try {
        Invoke-WebRequest -Uri $pick.browser_download_url -Headers $headers -OutFile $zip
    } catch {
        Start-Sleep -Milliseconds 500
        Invoke-WebRequest -Uri $pick.browser_download_url -Headers $headers -OutFile $zip
    }
    Write-Output 'STATUS: Extrayendo binarios...'
    Expand-Archive -LiteralPath $zip -DestinationPath $extract -Force
    if ($pick.name -match 'cuda-([0-9.]+)-x64\.zip$') {
        $cudaVer = $Matches[1]
        $rtRx = '^cudart-.*cuda-' + [regex]::Escape($cudaVer) + '-x64\.zip$'
        $cudart = $assets | Where-Object { $_.name -match $rtRx } | Select-Object -First 1
        if ($cudart) {
            Write-Output ('STATUS: Descargando runtime CUDA ' + $cudart.name + ' ...')
            $rtZip = Join-Path $runDir $cudart.name
            Invoke-WebRequest -Uri $cudart.browser_download_url -Headers $headers -OutFile $rtZip
            Write-Output 'STATUS: Extrayendo runtime CUDA...'
            Expand-Archive -LiteralPath $rtZip -DestinationPath $extract -Force
        } else {
            Write-Output ('STATUS: No encontré runtime CUDA separado para ' + $cudaVer + '; continúo.')
        }
    }
    $exe = Get-ChildItem -Path $extract -Recurse -Filter 'llama-server.exe' | Select-Object -First 1
    if (-not $exe) { throw 'llama-server.exe not found after extraction.' }
    Write-Output 'STATUS: Registrando binario en LlamaCode...'
    if ($tag) { Write-Output ('TAG: ' + $tag) }
    Write-Output $exe.FullName
} catch {
    Write-Output ('ERROR: ' + $_.Exception.Message)
    exit 1
}
)PS").arg(QDir::toNativeSeparators(toolsDir).replace("'", "''"));

    m_installerProc = new QProcess(this);
    m_installingOfficialBinary = true;
    m_cancelingOfficialBinaryInstall = false;
    m_timeoutOfficialBinaryInstall = false;
    m_lastInstallProgressAt = QDateTime::currentDateTimeUtc();
    m_officialBinaryInstallStatus = "Iniciando instalación...";
    m_officialBinaryInstallLog.clear();
    emit installingOfficialBinaryChanged();
    emit officialBinaryInstallStatusChanged();
    emit officialBinaryInstallLogChanged();

    if (!m_installWatchdog) {
        m_installWatchdog = new QTimer(this);
        m_installWatchdog->setInterval(5000);
        connect(m_installWatchdog, &QTimer::timeout, this, [this]() {
            if (!m_installingOfficialBinary || !m_installerProc)
                return;
            if (m_lastInstallProgressAt.secsTo(QDateTime::currentDateTimeUtc()) > 900) {
                m_timeoutOfficialBinaryInstall = true;
                m_officialBinaryInstallStatus = "Sin avance por 15 minutos. Cancelando instalación...";
                emit officialBinaryInstallStatusChanged();
                m_installerProc->kill();
            }
        });
    }
    m_installWatchdog->start();

    connect(m_installerProc, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString chunk = QString::fromUtf8(m_installerProc->readAllStandardOutput());
        if (!chunk.trimmed().isEmpty())
            m_lastInstallProgressAt = QDateTime::currentDateTimeUtc();
        if (!chunk.isEmpty()) {
            m_officialBinaryInstallLog.append(chunk);
            emit officialBinaryInstallLogChanged();
        }
        const QStringList lines = chunk.split('\n', Qt::SkipEmptyParts);
        for (const QString &rawLine : lines) {
            const QString line = rawLine.trimmed();
            if (line.startsWith("STATUS: ")) {
                m_officialBinaryInstallStatus = line.mid(8).trimmed();
                emit officialBinaryInstallStatusChanged();
            }
        }
    });
    connect(m_installerProc, &QProcess::readyReadStandardError, this, [this]() {
        const QString chunk = QString::fromUtf8(m_installerProc->readAllStandardError());
        if (!chunk.trimmed().isEmpty())
            m_lastInstallProgressAt = QDateTime::currentDateTimeUtc();
        if (!chunk.isEmpty()) {
            m_officialBinaryInstallLog.append(chunk);
            emit officialBinaryInstallLogChanged();
        }
    });

    connect(m_installerProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        // stdout was already consumed by readyReadStandardOutput into m_officialBinaryInstallLog
        const QString stdOut = m_officialBinaryInstallLog;
        const QString stdErr = QString::fromUtf8(m_installerProc->readAllStandardError()).trimmed();

        bool ok = false;
        QString installedPath;
        QString message;
        if (exitCode == 0) {
            // Last non-empty line that isn't a STATUS/ERROR/TAG prefix is the exe
            // path; the TAG line (si vino) trae el build tag del release (ej. b9274).
            const QStringList lines = stdOut.split('\n', Qt::SkipEmptyParts);
            QString releaseTag;
            for (int i = lines.size() - 1; i >= 0; --i) {
                const QString t = lines[i].trimmed();
                if (t.startsWith("TAG:")) {
                    if (releaseTag.isEmpty()) releaseTag = t.mid(4).trimmed();
                    continue;
                }
                if (!t.startsWith("STATUS:") && !t.startsWith("ERROR:") && !t.isEmpty()
                    && installedPath.isEmpty()) {
                    installedPath = t;
                }
            }
            if (QFileInfo::exists(installedPath)) {
                // Nombre y versionHint con el tag real para distinguir builds: los
                // binarios viejos NUNCA se reemplazan (un perfil puede rendir mejor
                // en uno previo), así que el catálogo es append-only y necesita que
                // cada entrada sea identificable.
                const QString tag = releaseTag.isEmpty() ? QStringLiteral("latest") : releaseTag;
                const QString name = releaseTag.isEmpty()
                    ? QStringLiteral("llama-server (official latest)")
                    : QStringLiteral("llama-server (official %1)").arg(releaseTag);
                QString backend = BinaryRegistry::detectBackend(installedPath);
                if (backend.isEmpty()) backend = QStringLiteral("cpu");
                const QString id = m_binaries.add(installedPath, name, "official", backend, tag);
                if (!id.isEmpty()) {
                    ok = true;
                    message = "Official llama.cpp binary installed and registered.";
                }
            }
        }
        if (!ok) {
            if (m_cancelingOfficialBinaryInstall) {
                message = "Automatic install canceled by user.";
            } else if (m_timeoutOfficialBinaryInstall) {
                message = "Automatic install timed out (no progress for 15 minutes).";
            } else {
                message = "Automatic install failed.";
                const QStringList outLines = stdOut.split('\n', Qt::SkipEmptyParts);
                for (const QString &line : outLines) {
                    const QString t = line.trimmed();
                    if (t.startsWith("ERROR: ")) {
                        message += " " + t.mid(7).trimmed();
                        break;
                    }
                }
                if (!stdErr.isEmpty()) {
                    QString cleaned = stdErr;
                    const QStringList errLines = stdErr.split('\n', Qt::SkipEmptyParts);
                    if (!errLines.isEmpty()) {
                        cleaned = errLines.first().trimmed();
                        cleaned.remove(QRegularExpression("^\\s*\\+\\s*"));
                    }
                    message += " " + cleaned;
                }
            }
        }

        m_installerProc->deleteLater();
        m_installerProc = nullptr;
        if (m_installWatchdog)
            m_installWatchdog->stop();
        m_installingOfficialBinary = false;
        m_officialBinaryInstallStatus = ok ? "Instalación completada." : message;
        if (!ok && m_officialBinaryInstallLog.trimmed().isEmpty()) {
            m_officialBinaryInstallLog = message + "\n";
            emit officialBinaryInstallLogChanged();
        }
        m_cancelingOfficialBinaryInstall = false;
        m_timeoutOfficialBinaryInstall = false;
        emit installingOfficialBinaryChanged();
        emit officialBinaryInstallStatusChanged();
        emit setupStateChanged();
        emit officialBinaryInstallFinished(ok, message, installedPath);
    });

    m_installerProc->start("powershell", {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script});
}

void AppController::cancelOfficialBinaryInstall()
{
    if (!m_installerProc || !m_installingOfficialBinary)
        return;
    m_cancelingOfficialBinaryInstall = true;
    m_installerProc->kill();
    m_officialBinaryInstallStatus = "Instalación cancelada.";
    emit officialBinaryInstallStatusChanged();
}

QString AppController::resolveFlag(const QString &binaryId, const QString &flag) const
{
    const LlamaBinary bin = m_binaries.findById(binaryId);
    return bin.resolveFlag(flag);
}

void AppController::smokeTestServer(const QString &launchProfileId)
{
    if (m_smokeTestProc) return;

    computeEffectiveProfile(launchProfileId);
    if (!m_effectiveProfile.value("isValid", false).toBool()) {
        const QStringList errors = m_effectiveProfile.value("blockingErrors").toStringList();
        emit smokeTestFinished(false, "Perfil inválido: " + errors.join("; "));
        return;
    }

    const QString binaryPath = m_effectiveProfile["binaryPath"].toString();
    const QStringList args   = m_effectiveProfile["effectiveArgs"].toStringList();

    m_smokeTestProc = new QProcess(this);
    m_smokeTestLog.clear();
    m_smokeTestDone = false;

    connect(m_smokeTestProc, &QProcess::readyReadStandardOutput, this, [this]() {
        m_smokeTestLog += QString::fromUtf8(m_smokeTestProc->readAllStandardOutput());
    });
    connect(m_smokeTestProc, &QProcess::readyReadStandardError, this, [this]() {
        m_smokeTestLog += QString::fromUtf8(m_smokeTestProc->readAllStandardError());
    });
    connect(m_smokeTestProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        m_smokeTestProc->deleteLater();
        m_smokeTestProc = nullptr;
        if (m_smokeTestTimer) m_smokeTestTimer->stop();
        finishSmokeTest(false,
            m_smokeTestLog.trimmed().isEmpty()
                ? QStringLiteral("Proceso terminó con código %1").arg(code)
                : m_smokeTestLog.trimmed());
    });

    m_smokeTestProc->start(binaryPath, args);

    if (!m_smokeTestTimer) {
        m_smokeTestTimer = new QTimer(this);
        m_smokeTestTimer->setSingleShot(true);
        connect(m_smokeTestTimer, &QTimer::timeout, this, [this]() {
            if (!m_smokeTestProc) return;
            QProcess *proc = m_smokeTestProc;
            m_smokeTestProc = nullptr;
            proc->disconnect(this);
            proc->terminate();
            proc->deleteLater();
            finishSmokeTest(true, "Servidor arrancó correctamente.");
        });
    }
    m_smokeTestTimer->start(5000);
}

void AppController::finishSmokeTest(bool passed, const QString &output)
{
    if (m_smokeTestDone) return;
    m_smokeTestDone = true;
    emit smokeTestFinished(passed, output);
}

void AppController::appendLog(const QString &text)
{
    m_log.append(text);
    // Keep log under 200KB
    if (m_log.size() > 200000)
        m_log = m_log.right(180000);
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    appendFileLog(m_serverLogFilePath, QStringLiteral("[%1][server/ui] %2").arg(ts, text));
    if (!text.endsWith(QLatin1Char('\n')))
        appendFileLog(m_serverLogFilePath, QStringLiteral("\n"));
    emit serverLogChanged();
}

QString AppController::runtimeLogDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/logs");
    QDir().mkpath(dir);
    return dir;
}

void AppController::rotateLogIfNeeded(const QString &path) const
{
    QFile f(path);
    if (!f.exists()) return;
    if (f.size() < 2 * 1024 * 1024) return;
    const QString backup = path + QStringLiteral(".1");
    QFile::remove(backup);
    QFile::rename(path, backup);
}

void AppController::appendFileLog(const QString &path, const QString &line) const
{
    rotateLogIfNeeded(path);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << line;
    f.close();
}

void AppController::appendServerEvent(const QString &source, const QString &text)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString msg = QStringLiteral("[%1][server/%2] %3").arg(ts, source, text);
    m_log += msg;
    if (!m_log.endsWith(QLatin1Char('\n'))) m_log += QLatin1Char('\n');
    if (m_log.size() > 200000) m_log = m_log.right(180000);
    appendFileLog(m_serverLogFilePath, msg);
    if (!msg.endsWith(QLatin1Char('\n'))) appendFileLog(m_serverLogFilePath, QStringLiteral("\n"));
    if (source == QLatin1String("stdout") || source == QLatin1String("stderr"))
        detectServerLogPatterns(text);
    emit serverLogChanged();
}

void AppController::detectServerLogPatterns(const QString &text)
{
    const QString lower = text.toLower();
    if (lower.contains(QStringLiteral("- cuda")) ||
        lower.contains(QStringLiteral("- vulkan")) ||
        lower.contains(QStringLiteral("- hip")) ||
        lower.contains(QStringLiteral("- sycl")) ||
        lower.contains(QStringLiteral("- opencl")) ||
        lower.contains(QStringLiteral("- metal"))) {
        m_serverGpuDeviceSeen = true;
    }
    if (lower.contains(QStringLiteral("- cpu")))
        m_serverCpuDeviceSeen = true;
    if (m_serverGpuRequested && m_serverCpuDeviceSeen && !m_serverGpuDeviceSeen
        && !m_serverGpuFallbackWarned
        && (lower.contains(QStringLiteral("common_params_fit_impl"))
            || lower.contains(QStringLiteral("system_info:")))) {
        m_serverGpuFallbackWarned = true;
        const QString msg = QStringLiteral(
            "El perfil pidió GPU layers, pero llama.cpp sólo reportó CPU en device_info. "
            "Reinstalá el binario latest CUDA o revisá que el runtime CUDA esté junto a llama-server.exe.");
        appendServerEvent(QStringLiteral("diag"), QStringLiteral("[warn] %1").arg(msg));
        emit serverDiagnostic(QStringLiteral("warn"), msg);
    }

    // Patrones (regex, case-insensitive). level → señal serverDiagnostic.
    struct Pat { const char *rx; const char *level; const char *msg; };
    static const Pat pats[] = {
        {"out of memory|cudaMalloc failed|failed to allocate|ggml_backend.*alloc.*fail",
         "error", "Memoria insuficiente (OOM) al cargar el modelo. Bajá ctx/gpu-layers o usá un quant menor."},
        {"bind.*address already in use|error.*bind|failed to listen|EADDRINUSE",
         "error", "El puerto está ocupado: el server no pudo bindear."},
        {"unknown argument|invalid argument|error while parsing|unrecognized.*option",
         "error", "Argumento inválido en la línea de comando del server."},
        {"failed to load model|error loading model|unable to load model|llama_load_model_from_file.*fail",
         "error", "No se pudo cargar el modelo (archivo corrupto, incompatible o ruta inválida)."},
        {"all slots are idle|HTTP server is listening|model loaded|server is listening",
         "info", "Server escuchando / modelo cargado."},
        {"slot.*context shift|context shift|n_past.*truncat",
         "warn", "Context shift: el prompt superó el ctx y se truncó."},
    };
    static QHash<QString, qint64> lastEmit;  // throttle por patrón (no spamear)
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &p : pats) {
        static QHash<const char*, QRegularExpression> cache;
        if (!cache.contains(p.rx))
            cache.insert(p.rx, QRegularExpression(QString::fromLatin1(p.rx),
                                                  QRegularExpression::CaseInsensitiveOption));
        if (cache[p.rx].match(text).hasMatch()) {
            const QString key = QString::fromLatin1(p.rx);
            if (now - lastEmit.value(key, 0) < 3000) continue;  // máx 1 cada 3s por patrón
            lastEmit[key] = now;
            appendServerEvent(QStringLiteral("diag"),
                              QStringLiteral("[%1] %2").arg(QString::fromLatin1(p.level),
                                                            QString::fromLatin1(p.msg)));
            emit serverDiagnostic(QString::fromLatin1(p.level), QString::fromLatin1(p.msg));
        }
    }
}

QString AppController::serverLogByLevel(const QString &level) const
{
    if (level.isEmpty() || level == QLatin1String("all")) return m_log;
    const QStringList lines = m_log.split(QLatin1Char('\n'));
    QStringList out;
    for (const QString &ln : lines) {
        if (level == QLatin1String("error")) {
            if (ln.contains(QLatin1String("[diag] [error]")) ||
                ln.contains(QLatin1String("/stderr]")) ||
                ln.contains(QLatin1String("[error]"), Qt::CaseInsensitive))
                out << ln;
        } else if (level == QLatin1String("warn")) {
            if (ln.contains(QLatin1String("[warn]"), Qt::CaseInsensitive)) out << ln;
        } else {
            // fuente exacta: stdout/stderr/lifecycle/health/diag
            if (ln.contains(QStringLiteral("/%1]").arg(level)) ||
                ln.contains(QStringLiteral("server/%1]").arg(level)))
                out << ln;
        }
    }
    return out.join(QLatin1Char('\n'));
}

void AppController::appendAgentEvent(const QString &source, const QString &text)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString msg = QStringLiteral("[%1][agent/%2] %3").arg(ts, source, text);
    m_agentLog += msg;
    if (!m_agentLog.endsWith(QLatin1Char('\n'))) m_agentLog += QLatin1Char('\n');
    if (m_agentLog.size() > 300000) m_agentLog = m_agentLog.right(250000);
    appendFileLog(m_agentLogFilePath, msg);
    if (!msg.endsWith(QLatin1Char('\n'))) appendFileLog(m_agentLogFilePath, QStringLiteral("\n"));
    emit agentLogChanged();
}

QVariantList AppController::buildMasterChain(const MasterConfig &mc)
{
    // Resuelve cada nivel de la cadena a un QVariantMap listo para el worker:
    // - cli: cliPath resuelto en PATH.
    // - http: key resuelta vía SecretStore (httpKeyRef es una referencia).
    // - profile: se resuelve el backend del LaunchProfile referenciado a un http
    //   (cloud → cloudBaseUrl/model/key; local → http://host:port).
    QVariantList chain;
    for (const MasterFallback &f : mc.fallbacks) {
        QVariantMap e;
        e[QStringLiteral("label")]      = f.label;
        e[QStringLiteral("applyEdits")] = f.applyEdits;
        e[QStringLiteral("timeoutSec")] = f.timeoutSec;
        if (f.type == QLatin1String("cli")) {
            e[QStringLiteral("type")]    = QStringLiteral("cli");
            e[QStringLiteral("cliName")] = f.cliName;
            e[QStringLiteral("cliPath")] = m_masterCli.resolvePath(f.cliName);
        } else if (f.type == QLatin1String("profile")) {
            const auto pctx = buildContext(f.profileId);
            QString url, model, key;
            if (pctx.backend.isCloud()) {
                url   = pctx.backend.cloudBaseUrl.trimmed();
                model = pctx.backend.cloudModel.trimmed();
                key   = m_secrets.resolve(pctx.backend.cloudKeyRef.trimmed());
            } else {
                const QString host = pctx.backend.host.isEmpty()
                    ? QStringLiteral("127.0.0.1") : pctx.backend.host;
                url   = QStringLiteral("http://%1:%2").arg(host).arg(pctx.backend.port);
                model = pctx.catalogModel.id;
            }
            e[QStringLiteral("type")]      = QStringLiteral("http");
            e[QStringLiteral("httpUrl")]   = url;
            e[QStringLiteral("httpModel")] = model;
            e[QStringLiteral("httpKey")]   = key;
        } else { // http
            e[QStringLiteral("type")]      = QStringLiteral("http");
            e[QStringLiteral("httpUrl")]   = f.httpUrl.trimmed();
            e[QStringLiteral("httpModel")] = f.httpModel.trimmed();
            e[QStringLiteral("httpKey")]   = m_secrets.resolve(f.httpKeyRef.trimmed());
        }
        chain.append(e);
    }
    return chain;
}

EffectiveProfileBuilder::Context AppController::buildContext(const QString &launchProfileId)
{
    EffectiveProfileBuilder::Context ctx;
    ctx.launch = m_profiles.resolveLaunch(launchProfileId);
    ctx.backend = m_profiles.resolveBackend(ctx.launch.backendProfileId);
    ctx.model = m_profiles.resolveModelProfile(ctx.launch.modelProfileId);
    ctx.runtime = m_profiles.resolveRuntime(ctx.launch.runtimePresetId);
    ctx.harness = m_profiles.resolveHarness(ctx.launch.harnessProfileId);
    ctx.workspace = m_profiles.resolveWorkspace(ctx.launch.workspaceProfileId);
    ctx.binary = m_binaries.findById(ctx.backend.binaryId);
    ctx.catalogModel = m_catalog.findById(ctx.model.modelId);
    ctx.mmprojModel = m_catalog.findById(ctx.model.mmprojId);
    ctx.draftModel = m_catalog.findById(ctx.model.draftModelId);
    ctx.reasoningEnabled = m_launchThinkingEnabled;
    return ctx;
}

QString AppController::cloudKeyRefForProfile(const QString &launchProfileId)
{
    const auto ctx = buildContext(launchProfileId);
    return ctx.backend.isCloud() ? ctx.backend.cloudKeyRef.trimmed() : QString();
}

bool AppController::isHarnessInstalled(const QString &adapter) const
{
    if (adapter == QLatin1String("none") || adapter.isEmpty()) return true;
    if (adapter == QLatin1String("llamaagent")) return true;  // backend interno, sin binario
#ifdef Q_OS_WIN
    const QString exe = adapter + QStringLiteral(".cmd");
    if (!QStandardPaths::findExecutable(exe).isEmpty()) return true;
#endif
    return !QStandardPaths::findExecutable(adapter).isEmpty();
}

void AppController::installHarness(const QString &adapter)
{
    if (m_installingHarness || adapter.isEmpty() || adapter == QLatin1String("none")) return;

    static const QMap<QString, QString> installCmds = {
        {QStringLiteral("opencode"),  QStringLiteral("npm install -g opencode@latest")},
        {QStringLiteral("smallcode"), QStringLiteral("npm install -g smallcode@latest")},
        {QStringLiteral("pi"),        QStringLiteral("npm install -g @mariozechner/pi-coding-agent")},
    };
    const QString cmd = installCmds.value(adapter);
    if (cmd.isEmpty()) return;

    m_installingHarness = true;
    m_harnessInstallStatus = QString();
    emit harnessStatusChanged();

    m_harnessProc = new QProcess(this);
    connect(m_harnessProc, &QProcess::readyReadStandardOutput, this, [this]() {
        m_harnessInstallStatus = QString::fromUtf8(m_harnessProc->readAllStandardOutput()).trimmed();
        emit harnessStatusChanged();
    });
    connect(m_harnessProc, &QProcess::readyReadStandardError, this, [this]() {
        const QString s = QString::fromUtf8(m_harnessProc->readAllStandardError()).trimmed();
        if (!s.isEmpty()) { m_harnessInstallStatus = s; emit harnessStatusChanged(); }
    });
    connect(m_harnessProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, adapter](int code, QProcess::ExitStatus) {
        m_harnessProc->deleteLater();
        m_harnessProc = nullptr;
        m_installingHarness = false;
        const bool ok = (code == 0) && isHarnessInstalled(adapter);
        m_harnessInstallStatus = ok
            ? QStringLiteral("✓ %1 installed").arg(adapter)
            : QStringLiteral("✗ Install failed (code %1)").arg(code);
        emit harnessStatusChanged();
        emit harnessInstallFinished(ok, adapter, m_harnessInstallStatus);
    });

    // Split "npm install -g xxx" into program + args
    const QStringList parts = cmd.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    m_harnessProc->start(parts.first(), parts.mid(1));
}

static bool s_isPidRunning(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    const bool running = (WaitForSingleObject(h, 0) == WAIT_TIMEOUT);
    CloseHandle(h);
    return running;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

void AppController::ensurePiConfig(const QString &openaiBaseUrl)
{
    // ~/.pi/agent/models.json — registra/actualiza el provider "llamacode"
    const QString dir = QDir::homePath() + QStringLiteral("/.pi/agent");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/models.json");

    QJsonObject root;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(in.readAll()).object();
        in.close();
    }

    QJsonObject providers = root.value(QStringLiteral("providers")).toObject();
    QJsonObject llamacode;
    llamacode[QStringLiteral("baseUrl")] = openaiBaseUrl;
    llamacode[QStringLiteral("api")]     = QStringLiteral("openai-completions");
    llamacode[QStringLiteral("apiKey")]  = QStringLiteral("llamacode");
    llamacode[QStringLiteral("models")]  = QJsonArray{ QJsonObject{{QStringLiteral("id"), QStringLiteral("local")}} };
    providers[QStringLiteral("llamacode")] = llamacode;
    root[QStringLiteral("providers")] = providers;

    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.close();
    }
}

void AppController::sendPiMessage(const QString &text)
{
    if (m_piMsgProc) {
        appendAgentEvent(QStringLiteral("pi/lifecycle"), QStringLiteral("pi ocupado - esperá la respuesta anterior"));
        return;
    }

    auto estimateTokens = [](const QString &s) -> int {
        const int n = s.trimmed().size();
        return n <= 0 ? 0 : (n + 3) / 4;
    };
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    // Mensaje del usuario + placeholder del asistente (streaming visual)
    QVariantMap userMsg{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), text},
                        {QStringLiteral("typing"), false},
                        {QStringLiteral("createdAt"), static_cast<double>(nowMs)},
                        {QStringLiteral("completedAt"), static_cast<double>(nowMs)},
                        {QStringLiteral("elapsedMs"), 0},
                        {QStringLiteral("tokens"), estimateTokens(text)},
                        {QStringLiteral("tps"), 0.0}};
    m_agentMessages.append(userMsg);
    QVariantMap asstMsg{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), QString()},
                        {QStringLiteral("typing"), true},
                        {QStringLiteral("createdAt"), static_cast<double>(nowMs)},
                        {QStringLiteral("elapsedMs"), 0},
                        {QStringLiteral("tokens"), 0},
                        {QStringLiteral("tps"), 0.0}};
    m_agentMessages.append(asstMsg);
    m_currentAssistantIdx = m_agentMessages.size() - 1;
    emit agentMessagesChanged();

    m_piMsgProc = new QProcess(this);
    m_piMsgProc->setProcessEnvironment(m_piEnv);
    if (!m_piCwd.isEmpty())
        m_piMsgProc->setWorkingDirectory(m_piCwd);

    // -p print mode + sesión persistente para mantener continuidad entre mensajes.
    const QStringList args{
        QStringLiteral("-p"),
        QStringLiteral("--session"),  m_piSessionPath,
        QStringLiteral("--provider"), QStringLiteral("llamacode"),
        QStringLiteral("--model"),    QStringLiteral("llamacode/local"),
        text
    };

    connect(m_piMsgProc, &QProcess::readyReadStandardOutput, this, [this, estimateTokens]() {
        if (!m_piMsgProc) return;
        const QString chunk = QString::fromUtf8(m_piMsgProc->readAllStandardOutput());
        if (chunk.isEmpty() || m_currentAssistantIdx < 0
                || m_currentAssistantIdx >= m_agentMessages.size()) return;
        QVariantMap m = m_agentMessages[m_currentAssistantIdx].toMap();
        const QString content = m.value(QStringLiteral("content")).toString() + chunk;
        m[QStringLiteral("content")] = content;
        m[QStringLiteral("typing")]  = true;
        const qint64 startedAt = static_cast<qint64>(m.value(QStringLiteral("createdAt")).toDouble());
        const qint64 elapsedMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAt);
        const int toks = estimateTokens(content);
        m[QStringLiteral("tokens")] = toks;
        m[QStringLiteral("elapsedMs")] = static_cast<int>(elapsedMs);
        m[QStringLiteral("tps")] = (elapsedMs > 0 && toks > 0)
            ? (1000.0 * static_cast<double>(toks) / static_cast<double>(elapsedMs))
            : 0.0;
        m_agentMessages[m_currentAssistantIdx] = m;
        emit agentMessagesChanged();
    });
    connect(m_piMsgProc, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_piMsgProc) return;
        const QString s = QString::fromUtf8(m_piMsgProc->readAllStandardError());
        if (!s.isEmpty()) appendAgentEvent(QStringLiteral("pi/stderr"), s);
    });
    connect(m_piMsgProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, estimateTokens](int code, QProcess::ExitStatus) {
        if (m_currentAssistantIdx >= 0 && m_currentAssistantIdx < m_agentMessages.size()) {
            QVariantMap m = m_agentMessages[m_currentAssistantIdx].toMap();
            if (code != 0 && m.value(QStringLiteral("content")).toString().isEmpty())
                m[QStringLiteral("content")] = QStringLiteral("[pi terminó con código %1]").arg(code);
            m[QStringLiteral("typing")] = false;
            const qint64 doneAt = QDateTime::currentMSecsSinceEpoch();
            const qint64 startedAt = static_cast<qint64>(m.value(QStringLiteral("createdAt")).toDouble());
            const qint64 elapsedMs = qMax<qint64>(0, doneAt - startedAt);
            const QString content = m.value(QStringLiteral("content")).toString();
            const int toks = estimateTokens(content);
            m[QStringLiteral("completedAt")] = static_cast<double>(doneAt);
            m[QStringLiteral("tokens")] = toks;
            m[QStringLiteral("elapsedMs")] = static_cast<int>(elapsedMs);
            m[QStringLiteral("tps")] = (elapsedMs > 0 && toks > 0)
                ? (1000.0 * static_cast<double>(toks) / static_cast<double>(elapsedMs))
                : 0.0;
            m_agentMessages[m_currentAssistantIdx] = m;
            emit agentMessagesChanged();
        }
        m_piMsgProc->deleteLater();
        m_piMsgProc = nullptr;
    });

    m_piMsgProc->start(m_piExe, args);
    if (!m_piMsgProc->waitForStarted(5000)) {
        appendAgentEvent(QStringLiteral("pi/lifecycle"), QStringLiteral("Error: no se pudo iniciar pi"));
        m_piMsgProc->deleteLater();
        m_piMsgProc = nullptr;
    }
}

IAgentBackend *AppController::ensureAgentBackend(const QString &adapter)
{
    if (m_agentBackend && m_agentBackend->adapter() == adapter)
        return m_agentBackend;
    if (m_agentBackend) { m_agentBackend->deleteLater(); m_agentBackend = nullptr; }

    IAgentBackend *b = nullptr;
    if (adapter == QLatin1String("opencode"))
        b = new OpencodeBackend(this);
    else if (adapter == QLatin1String("llamaagent"))
        b = new LlamaAgentBackend(this);
    if (!b) return nullptr;

    if (auto *cb = qobject_cast<LlamaAgentBackend *>(b)) {
        cb->setThinkingEnabled(m_agentThinkingEnabled);

        // Servers MCP: global + proyecto (el de proyecto pisa por nombre).
        const QString proj = currentAgentProjectDir();
        QMap<QString, QVariant> merged;
        for (const QVariant &v : listMcpServers(QStringLiteral("global"), QString()))
            merged.insert(v.toMap().value(QStringLiteral("name")).toString(), v);
        if (!proj.isEmpty())
            for (const QVariant &v : listMcpServers(QStringLiteral("project"), proj))
                merged.insert(v.toMap().value(QStringLiteral("name")).toString(), v);
        injectBrowserMcp(merged, m_activeLaunchId);
        cb->setMcpServers(merged.values());
    }

    // Reflejar estado del backend en los mirrors expuestos a QML.
    connect(b, &IAgentBackend::messagesChanged, this, [this, b]() {
        m_agentMessages = b->messages();
        // Llega la lista autoritativa → terminar cualquier override de streaming
        // (el contenido final ya está en m_agentMessages).
        if (m_agentStreamingIndex != -1) {
            m_agentStreamingIndex = -1;
            m_agentStreamingText.clear();
            emit agentStreamingChanged();
        }
        emit agentMessagesChanged();
    });
    // Streaming incremental: refresca SÓLO la burbuja activa, sin re-bindear toda
    // la lista (evita re-instanciar todos los delegates por token).
    connect(b, &IAgentBackend::streamingText, this, [this](int idx, const QString &content) {
        m_agentStreamingIndex = idx;
        m_agentStreamingText  = content;
        emit agentStreamingChanged();
    });
    connect(b, &IAgentBackend::queueChanged, this, [this, b]() {
        m_agentQueuedCount = b->queuedCount();
        emit agentQueueChanged();
    });
    connect(b, &IAgentBackend::gitRequired, this, [this]() {
        recheckGit();
        emit gitRequiredForSubagents();   // la UI ofrece instalar git
    });
    connect(b, &IAgentBackend::sessionsChanged, this, [this, b]() {
        m_agentSessions = groupSessionsByProject(b->sessions());
        m_opencodeSessionId = b->currentSessionId();
        m_opencodeSessionTitle = b->currentSessionTitle();
        emit agentSessionsChanged();
    });
    connect(b, &IAgentBackend::logAppended, this, [this](const QString &chunk) {
        appendAgentEvent(QStringLiteral("backend"), chunk);
    });
    connect(b, &IAgentBackend::turnFinished, this, &AppController::onAgentTurnFinished);
    connect(b, &IAgentBackend::runningChanged, this, [this, b]() {
        if (m_agentStarting) {
            m_agentStarting = false;
            emit agentStartingChanged();
        }
        if (b->running() && !m_pendingScheduledTaskId.isEmpty())
            QTimer::singleShot(0, this, [this]() { dispatchPendingScheduledTask(); });
        if (!b->running()) {
            m_activeAgentAdapter.clear();
            if (!m_agentPendingTool.isEmpty()) {
                m_agentPendingTool.clear();
                emit agentPendingToolChanged();
            }
            m_agentContextUsed = 0;
            emit agentContextChanged();
        }
        emit agentRunningChanged();
    });
    connect(b, &IAgentBackend::errorOccurred, this, [this](const QString &m) {
        if (m_agentStarting) {
            m_agentStarting = false;
            emit agentStartingChanged();
        }
        appendAgentEvent(QStringLiteral("error"), m);
        emit serverError(m);
    });
    connect(b, &IAgentBackend::toolApprovalNeeded, this, [this](const QVariantMap &toolCall) {
        m_agentPendingTool = toolCall;
        emit agentPendingToolChanged();
    });
    connect(b, &IAgentBackend::contextUsage, this, [this](int used, int limit) {
        m_agentContextUsed = used;
        if (limit > 0) m_agentContextLimit = limit;
        emit agentContextChanged();
    });

    b->setApprovalPolicy(m_agentApprovalMode);
    b->setPermissionRules(m_agentPermRules);
    b->setAgentTuning(m_agentSystemPrompt, m_agentTemperature >= 0.0 ? m_agentTemperature : m_resolvedProfileTemperature);
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(b)) {
        cb->setDisabledTools(m_agentDisabledTools);
        cb->setTeacherConfig(m_agentTeacherUrl, m_agentTeacherModel, m_agentTeacherKey);
        cb->setMailAccounts(mailAccountsResolved());
        cb->setMailAutoSend(m_mailAutoSend);
    }
    m_agentBackend = b;
    return b;
}

void AppController::setAgentPermRules(const QString &rules)
{
    if (rules == m_agentPermRules) return;
    m_agentPermRules = rules;
    writeSetting(QStringLiteral("agent/permRules"), rules);
    if (m_agentBackend) m_agentBackend->setPermissionRules(rules);
    emit agentTuningChanged();
}

void AppController::setAgentSystemPrompt(const QString &p)
{
    if (p == m_agentSystemPrompt) return;
    m_agentSystemPrompt = p;
    writeSetting(QStringLiteral("agent/systemPrompt"), p);
    if (m_agentBackend) m_agentBackend->setAgentTuning(m_agentSystemPrompt, m_agentTemperature >= 0.0 ? m_agentTemperature : m_resolvedProfileTemperature);
    emit agentTuningChanged();
}

void AppController::setAgentTemperature(double t)
{
    if (qFuzzyCompare(t, m_agentTemperature)) return;
    m_agentTemperature = t;
    writeSetting(QStringLiteral("agent/temperature"), t);
    if (m_agentBackend) m_agentBackend->setAgentTuning(m_agentSystemPrompt, m_agentTemperature >= 0.0 ? m_agentTemperature : m_resolvedProfileTemperature);
    emit agentTuningChanged();
}

void AppController::setAgentApprovalMode(const QString &mode)
{
    const QString m = mode.trimmed().isEmpty() ? QStringLiteral("ask") : mode;
    if (m == m_agentApprovalMode) return;
    m_agentApprovalMode = m;
    writeSetting(QStringLiteral("agent/approvalMode"), m);
    if (m_agentBackend) m_agentBackend->setApprovalPolicy(m);
    emit agentApprovalModeChanged();
}

void AppController::setAgentThinkingEnabled(bool enabled)
{
    setThinkingEnabled(enabled);
}

void AppController::setMermaidEnabled(bool enabled)
{
    if (enabled == m_mermaidEnabled) return;
    m_mermaidEnabled = enabled;
    writeSetting(QStringLiteral("chat/mermaidEnabled"), enabled);
    emit mermaidEnabledChanged();
}

void AppController::setBrowserAutomationEnabled(bool enabled)
{
    if (enabled == m_browserAutomationEnabled) return;
    m_browserAutomationEnabled = enabled;
    writeSetting(QStringLiteral("browser/automationEnabled"), enabled);
    emit browserAutomationChanged();
    // Re-inyectar al backend agente vivo (si lo hay).
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend)) {
        const QString proj = currentAgentProjectDir();
        QMap<QString, QVariant> merged;
        for (const QVariant &v : listMcpServers(QStringLiteral("global"), QString()))
            merged.insert(v.toMap().value(QStringLiteral("name")).toString(), v);
        if (!proj.isEmpty())
            for (const QVariant &v : listMcpServers(QStringLiteral("project"), proj))
                merged.insert(v.toMap().value(QStringLiteral("name")).toString(), v);
        injectBrowserMcp(merged, m_activeLaunchId);
        cb->setMcpServers(merged.values());
    }
}

void AppController::setBrowserMcpCommand(const QString &cmd)
{
    const QString c = cmd.trimmed().isEmpty()
        ? QStringLiteral("npx @playwright/mcp@latest") : cmd.trimmed();
    if (c == m_browserMcpCommand) return;
    m_browserMcpCommand = c;
    writeSetting(QStringLiteral("browser/mcpCommand"), c);
    emit browserAutomationChanged();
}

// ── Browser teach: grabar/listar/borrar skills ──────────────────────────
QStringList AppController::listBrowserSkills() const
{
    return BrowserTeach::listSkills();
}

bool AppController::browserSkillExists(const QString &name) const
{
    return BrowserTeach::hasSkill(name);
}

bool AppController::removeBrowserSkill(const QString &name)
{
    const bool ok = BrowserTeach::removeSkill(name);
    if (ok) emit browserSkillsChanged();
    return ok;
}

QString AppController::recordBrowserSkill(const QString &name, const QString &url)
{
    if (m_browserRecordProc)
        return QStringLiteral("Ya hay una grabación en curso.");
    const QString cmd = BrowserTeach::recordCommand(name, url);
    if (cmd.isEmpty())
        return QStringLiteral("Nombre de skill inválido.");

    const QString dir = BrowserTeach::skillsDir();
    // Asegura el runtime npm local (playwright) la primera vez: necesario para que
    // el replay (node skill.mjs con cwd=dir) resuelva el módulo. codegen usa npx.
    QString full;
#ifdef Q_OS_WIN
    full = QStringLiteral("if not exist node_modules\\playwright ( npm init -y >nul 2>&1 & npm i playwright ) & %1").arg(cmd);
#else
    full = QStringLiteral("[ -d node_modules/playwright ] || { npm init -y >/dev/null 2>&1; npm i playwright; }; %1").arg(cmd);
#endif

    auto *p = new QProcess(this);
    p->setWorkingDirectory(dir);
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, p](int, QProcess::ExitStatus) {
                m_browserRecordProc = nullptr;
                p->deleteLater();
                emit browserSkillsChanged();
            });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError) {
        if (m_browserRecordProc == p) { m_browserRecordProc = nullptr; emit browserSkillsChanged(); }
    });
#ifdef Q_OS_WIN
    p->start(QStringLiteral("cmd"), {QStringLiteral("/c"), full});
#else
    p->start(QStringLiteral("sh"), {QStringLiteral("-c"), full});
#endif
    if (!p->waitForStarted(8000)) {
        p->deleteLater();
        return QStringLiteral("No se pudo iniciar la grabación (¿Node/npx instalado?).");
    }
    m_browserRecordProc = p;
    emit browserSkillsChanged();   // refleja browserRecording()=true
    return QString();
}

// Resuelve el override por perfil sobre el toggle global e inserta el server MCP
// "playwright" en el mapa mergeado. Nombre fijo "playwright" → si el usuario ya
// definió uno propio con ese nombre, el suyo gana (no se pisa).
bool AppController::browserMcpEffective(const QString &override, bool globalEnabled)
{
    if (override == QLatin1String("on"))  return true;
    if (override == QLatin1String("off")) return false;
    return globalEnabled;   // "inherit" o cualquier otro valor → toggle global
}

void AppController::injectBrowserMcp(QMap<QString, QVariant> &merged,
                                     const QString &launchId) const
{
    QString override = QStringLiteral("inherit");
    if (!launchId.isEmpty()) {
        const LaunchProfile lp = m_profiles.resolveLaunch(launchId);
        if (!lp.id.isEmpty() && !lp.browserAutomation.isEmpty())
            override = lp.browserAutomation;
    }
    if (!browserMcpEffective(override, m_browserAutomationEnabled)) return;
    if (merged.contains(QStringLiteral("playwright"))) return;  // respeta el del usuario
    merged.insert(QStringLiteral("playwright"), QVariantMap{
        {QStringLiteral("name"), QStringLiteral("playwright")},
        {QStringLiteral("type"), QStringLiteral("local")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("command"), m_browserMcpCommand}});
}

void AppController::setThinkingEnabled(bool enabled)
{
    if (enabled == m_agentThinkingEnabled) return;
    m_agentThinkingEnabled = enabled;
    m_launchThinkingEnabled = enabled;
    writeSetting(QStringLiteral("thinking/enabled"), enabled);
    writeSetting(QStringLiteral("agent/thinkingEnabled"), enabled);
    writeSetting(QStringLiteral("thinking/serverEnabled"), enabled);
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        cb->setThinkingEnabled(enabled);
    if (serverRunning()) {
        const QString msg = enabled
            ? QStringLiteral("Pensar activado: el cambio duro de reasoning se aplica al reiniciar el modelo.")
            : QStringLiteral("Pensar desactivado: el cambio duro de reasoning se aplica al reiniciar el modelo.");
        appendServerEvent(QStringLiteral("lifecycle"), msg);
    }
    emit agentThinkingChanged();
    emit thinkingChanged();
}

void AppController::applyThinkingChange(bool enabled,
                                        const QString &surface,
                                        const QString &restartMode)
{
    const QString s = surface.trimmed().toLower();
    const QString mode = restartMode.trimmed().toLower();
    const bool withAgent = (s == QLatin1String("agent"))
                           || agentRunning()
                           || m_agentStarting
                           || !m_activeAgentAdapter.isEmpty();

    if (s == QLatin1String("chat"))
        setChatThinkingEnabled(enabled);
    else
        setThinkingEnabled(enabled);

    if (mode == QLatin1String("now")) {
        restartActiveLaunchForThinking(withAgent, true);
    } else if (mode == QLatin1String("after-response")) {
        const bool busy = (s == QLatin1String("chat")) ? m_chatGenerating
                                                       : (m_agentStreamingIndex >= 0);
        if (!busy) {
            restartActiveLaunchForThinking(withAgent, false);
            return;
        }
        m_restartThinkingAfterResponse = true;
        m_restartThinkingWithAgent = withAgent;
        appendServerEvent(QStringLiteral("lifecycle"),
                          QStringLiteral("Reinicio del modelo programado al terminar la respuesta actual."));
    }
}

void AppController::restartActiveLaunchForThinking(bool withAgent, bool cancelActiveGeneration)
{
    const QString launchId = m_activeLaunchId;
    if (launchId.isEmpty()) {
        emit serverError(QStringLiteral("No hay un perfil activo para reiniciar el modelo."));
        return;
    }

    m_restartThinkingAfterResponse = false;
    if (cancelActiveGeneration) {
        if (m_chatGenerating)
            stopChatGeneration();
        if (m_agentStreamingIndex >= 0)
            cancelAgentGeneration();
    }

    if (withAgent && (agentRunning() || m_agentStarting || !m_activeAgentAdapter.isEmpty()))
        stopAgent();

    auto startAgain = [this, launchId, withAgent]() {
        appendServerEvent(QStringLiteral("lifecycle"),
                          QStringLiteral("Reiniciando modelo para aplicar thinking=%1.")
                              .arg(m_launchThinkingEnabled ? QStringLiteral("on")
                                                           : QStringLiteral("off")));
        if (withAgent)
            startServerAndAgent(launchId);
        else
            startServer(launchId);
    };

    if (!serverRunning()) {
        QTimer::singleShot(0, this, startAgain);
        return;
    }

    auto *conn = new QMetaObject::Connection;
    *conn = connect(this, &AppController::serverRunningChanged, this,
                    [this, conn, startAgain]() {
        if (serverRunning() || m_serverStopping)
            return;
        disconnect(*conn);
        delete conn;
        QTimer::singleShot(0, this, startAgain);
    });
    stopServer();
}

void AppController::setAgentTeacherUrl(const QString &url)
{
    if (url == m_agentTeacherUrl) return;
    m_agentTeacherUrl = url;
    writeSetting(QStringLiteral("agent/teacherUrl"), url);
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        cb->setTeacherConfig(m_agentTeacherUrl, m_agentTeacherModel, m_agentTeacherKey);
    emit agentTeacherChanged();
}

void AppController::setAgentTeacherModel(const QString &model)
{
    if (model == m_agentTeacherModel) return;
    m_agentTeacherModel = model;
    writeSetting(QStringLiteral("agent/teacherModel"), model);
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        cb->setTeacherConfig(m_agentTeacherUrl, m_agentTeacherModel, m_agentTeacherKey);
    emit agentTeacherChanged();
}

void AppController::setAgentTeacherKey(const QString &key)
{
    if (key == m_agentTeacherKey) return;
    m_agentTeacherKey = key;
    writeSetting(QStringLiteral("agent/teacherKey"), key);
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        cb->setTeacherConfig(m_agentTeacherUrl, m_agentTeacherModel, m_agentTeacherKey);
    emit agentTeacherChanged();
}

QStringList AppController::masterCliList() const
{
    return MasterCli::supported();
}

QVariantMap AppController::masterCliStatus(const QString &name, bool force)
{
    return m_masterCli.status(name, force);
}

QString AppController::masterCliInstallCommand(const QString &name) const
{
    return MasterCli::installCommand(name);
}

QVariantList AppController::agentToolCatalog() const
{
    QVariantList out = LlamaAgentBackend::toolCatalog();
    for (QVariant &v : out) {
        QVariantMap m = v.toMap();
        m.insert(QStringLiteral("enabled"),
                 !m_agentDisabledTools.contains(m.value(QStringLiteral("name")).toString()));
        v = m;
    }
    return out;
}

void AppController::setAgentToolEnabled(const QString &name, bool enabled)
{
    const bool isDisabled = m_agentDisabledTools.contains(name);
    if (enabled == !isDisabled) return;               // sin cambio
    if (enabled) m_agentDisabledTools.removeAll(name);
    else if (!isDisabled) m_agentDisabledTools.append(name);
    writeSetting(QStringLiteral("agent/disabledTools"), m_agentDisabledTools);
    if (auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        cb->setDisabledTools(m_agentDisabledTools);   // efectivo en el próximo turno
    emit agentToolsChanged();
}

void AppController::approveAgentTool(const QString &id, bool always)
{
    if (m_agentBackend) m_agentBackend->approveTool(id, always);
    if (m_agentPendingTool.value(QStringLiteral("id")).toString() == id) {
        m_agentPendingTool.clear();
        emit agentPendingToolChanged();
    }
}

void AppController::rejectAgentTool(const QString &id)
{
    if (m_agentBackend) m_agentBackend->rejectTool(id);
    if (m_agentPendingTool.value(QStringLiteral("id")).toString() == id) {
        m_agentPendingTool.clear();
        emit agentPendingToolChanged();
    }
}

void AppController::revertAgentEdit(const QString &path)
{
    if (m_agentBackend) m_agentBackend->revertEdit(path);
}

QString AppController::readAgentMemory(const QString &projectDir) const
{
    const QString dir = projectDir.isEmpty() ? currentAgentProjectDir() : projectDir;
    if (dir.isEmpty()) return QString();
    QFile f(LlamaAgentBackend::memoryFilePath(dir));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.read(256 * 1024));
}

bool AppController::writeAgentMemory(const QString &projectDir, const QString &text)
{
    const QString dir = projectDir.isEmpty() ? currentAgentProjectDir() : projectDir;
    if (dir.isEmpty()) { emit serverError(QStringLiteral("No hay proyecto activo para la memoria.")); return false; }
    const QString path = LlamaAgentBackend::memoryFilePath(dir);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit serverError(QStringLiteral("No se pudo escribir la memoria: %1").arg(path));
        return false;
    }
    f.write(text.toUtf8());
    return true;
}

IAgentBackend *AppController::ensureChatBackend()
{
    if (m_chatBackend) return m_chatBackend;
    auto *b = new RawChatBackend(this);
    if (auto *raw = qobject_cast<RawChatBackend *>(b)) {
        raw->setThinkingEnabled(m_chatThinkingEnabled);
    }
    connect(b, &IAgentBackend::messagesChanged, this, [this, b]() {
        m_chatMessages = b->messages();
        if (m_chatStreamingIndex != -1) {
            m_chatStreamingIndex = -1;
            m_chatStreamingText.clear();
            emit chatStreamingChanged();
        }
        bool generating = false;
        for (const QVariant &v : std::as_const(m_chatMessages)) {
            if (v.toMap().value(QStringLiteral("typing")).toBool()) {
                generating = true;
                break;
            }
        }
        const bool wasGenerating = m_chatGenerating;
        m_chatGenerating = generating;
        emit chatMessagesChanged();
        emit chatGeneratingChanged();
        // Modo Charla: avisar "pensando" al empezar y hablar la respuesta al cerrar.
        if (m_voice && m_charlaActive) {
            if (generating && !wasGenerating)
                m_voice->notifyThinking();
            else if (!generating && wasGenerating) {
                QString reply;
                for (const QVariant &v : std::as_const(m_chatMessages)) {
                    const QVariantMap mm = v.toMap();
                    const QString role = mm.value(QStringLiteral("role")).toString();
                    if (role == QLatin1String("assistant") || role == QLatin1String("ai")) {
                        const QString c = mm.value(QStringLiteral("content")).toString();
                        if (!c.trimmed().isEmpty()) reply = c;
                    }
                }
                m_voice->speak(reply);
            }
        }
        if (!generating && wasGenerating && m_restartThinkingAfterResponse)
            restartActiveLaunchForThinking(m_restartThinkingWithAgent, false);
    });
    connect(b, &IAgentBackend::streamingText, this, [this](int idx, const QString &content) {
        m_chatStreamingIndex = idx;
        m_chatStreamingText = content;
        emit chatStreamingChanged();
    });
    connect(b, &IAgentBackend::queueChanged, this, [this, b]() {
        m_chatQueuedCount = b->queuedCount();
        emit chatQueueChanged();
    });
    connect(b, &IAgentBackend::sessionsChanged, this, [this, b]() {
        m_chatSessions = groupSessionsByProject(b->sessions());
        m_chatSessionId = b->currentSessionId();
        m_chatSessionTitle = b->currentSessionTitle();
        emit chatSessionsChanged();
    });
    connect(b, &IAgentBackend::errorOccurred, this, [this](const QString &m) {
        emit serverError(m);
    });

    AgentContext c;
    c.adapter = QStringLiteral("raw");
    c.serverBaseUrl = serverBaseUrl();
    c.modelId = routedModelId(QStringLiteral("chat"));
    b->start(c);
    m_chatBackend = b;
    return m_chatBackend;
}

void AppController::startAgent(const QString &launchProfileId)
{
    if (agentRunning()) return;
    m_agentStopping = false;

    const auto ctx = buildContext(launchProfileId);
    const QString adapter = ctx.harness.adapter;
    if (adapter.isEmpty() || adapter == QLatin1String("none")) {
        if (m_agentStarting) { m_agentStarting = false; emit agentStartingChanged(); }
        appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: no harness configured for this profile"));
        return;
    }
    if (adapter == QLatin1String("raw")) {
        if (m_agentStarting) { m_agentStarting = false; emit agentStartingChanged(); }
        appendAgentEvent(QStringLiteral("lifecycle"),
                         QStringLiteral("Error: 'raw' no soporta tool-calling. Usá 'opencode' para modo Agente."));
        emit serverError(QStringLiteral("El harness 'raw' no soporta tool-calling en modo Agente."));
        return;
    }

    if (!m_agentStarting) {
        m_agentStarting = true;
        emit agentStartingChanged();
    }

    // Backend propio: sin binario externo, corre dentro de la app.
    if (adapter == QLatin1String("llamaagent")) {
        const bool cloud = ctx.backend.isCloud();
        // Provider cloud: resolver la API key por su ref (env var → store). Si no se
        // encuentra, pedirla a la UI y abortar el arranque (se reintenta tras setSecret).
        QString cloudKey;
        if (cloud) {
            const QString ref = ctx.backend.cloudKeyRef.trimmed();
            cloudKey = m_secrets.resolve(ref);
            if (cloudKey.isEmpty()) {
                m_agentStarting = false;
                emit agentStartingChanged();
                appendAgentEvent(QStringLiteral("lifecycle"),
                                 QStringLiteral("Cloud: falta API key (%1) — ingresala para continuar.")
                                     .arg(ref.isEmpty() ? QStringLiteral("sin ref") : ref));
                emit cloudSecretRequired(launchProfileId, ref);
                return;
            }
        }
        // Necesita el llama-server corriendo (usa su API OpenAI). Sin server → refused.
        // Si está corriendo pero el modelo aún carga, igual arranca: la UI muestra
        // "Modelo cargando" y el usuario espera al ready antes de enviar.
        // Provider cloud: no hay server local, el agente pega directo al endpoint.
        if (!cloud && !serverRunning()) {
            const QString msg = QStringLiteral(
                "El harness 'LlamaAgent' necesita el servidor corriendo. Iniciá el modelo en 'Lanzar' primero.");
            appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: %1").arg(msg));
            emit serverError(msg);
            m_agentStarting = false;
            emit agentStartingChanged();
            return;
        }

        // Parse profile temperature if agent temperature is default (< 0)
        m_resolvedProfileTemperature = -1.0;
        const QStringList &args = ctx.launch.extraArgs;
        for (int i = 0; i < args.size() - 1; ++i) {
            if (args[i] == QStringLiteral("--temp") || args[i] == QStringLiteral("-t")) {
                bool ok = false;
                double val = args[i+1].toDouble(&ok);
                if (ok) {
                    m_resolvedProfileTemperature = val;
                    break;
                }
            }
        }

        IAgentBackend *b = ensureAgentBackend(adapter);
        if (!b) {
            appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: backend LlamaAgent no disponible"));
            m_agentStarting = false;
            emit agentStartingChanged();
            return;
        }
        b->setAgentTuning(m_agentSystemPrompt, m_agentTemperature >= 0.0 ? m_agentTemperature : m_resolvedProfileTemperature);
        if (auto *cb = qobject_cast<LlamaAgentBackend *>(b)) {
            const MasterConfig &mc = ctx.launch.master;
            if (mc.isConfigured()) {
                cb->setMasterChain(buildMasterChain(mc), mc.escalation, mc.autoAfterFails);
            }
            // cadena vacía: queda el fallback global de setTeacherConfig (ensureAgentBackend).
        }
        const QString agentCwd = m_agentCwdOverride.isEmpty()
            ? ctx.workspace.cwd.trimmed() : m_agentCwdOverride;
        AgentContext c;
        c.adapter       = adapter;
        c.cwd           = (!agentCwd.isEmpty() && QFileInfo(agentCwd).isDir()) ? agentCwd : QString();
        if (cloud) {
            c.serverBaseUrl = ctx.backend.cloudBaseUrl.trimmed();
            c.modelId       = ctx.backend.cloudModel.trimmed();
            c.apiKey        = cloudKey;
            c.ctxOverride   = ctx.backend.cloudCtx;
        } else {
            c.serverBaseUrl = serverBaseUrl();
            c.modelId       = routedModelId(ctx.catalogModel.id);
        }
        m_agentCwdOverride.clear();
        m_activeAgentAdapter = adapter;
        m_agentInTerminal    = false;
        m_currentAssistantIdx = -1;
        b->start(c);
        // start() restaura la sesión de forma síncrona. No vaciar antes el mirror:
        // ese estado intermedio deja al ListView con el contentY de un historial
        // largo pero sin delegates, y puede terminar mostrando un viewport negro.
        m_agentMessages = b->messages();
        emit agentMessagesChanged();
        return;
    }

    QString exe;
    exe = QStandardPaths::findExecutable(adapter);
    if (exe.isEmpty()) {
        appendAgentEvent(QStringLiteral("lifecycle"),
                         QStringLiteral("Error: '%1' not found in PATH — install it first").arg(adapter));
        m_agentStarting = false;
        emit agentStartingChanged();
        return;
    }

    // Build env string list for the launcher
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString baseUrl = serverBaseUrl();
    env.insert(QStringLiteral("OPENAI_BASE_URL"),   baseUrl + QStringLiteral("/v1"));
    env.insert(QStringLiteral("OPENAI_API_BASE"),   baseUrl + QStringLiteral("/v1"));
    env.insert(QStringLiteral("OPENAI_API_KEY"),    QStringLiteral("llamacode"));
    env.insert(QStringLiteral("ANTHROPIC_API_KEY"), QStringLiteral("llamacode"));
    if (adapter == QLatin1String("opencode"))
        env.insert(QStringLiteral("OPENCODE_API_URL"), baseUrl);
    else if (adapter == QLatin1String("smallcode")) {
        env.insert(QStringLiteral("SMALLCODE_API_URL"),  baseUrl);
        env.insert(QStringLiteral("SMALLCODE_BASE_URL"), baseUrl + QStringLiteral("/v1"));
    }
    else if (adapter == QLatin1String("pi")) {
        // pi usa un provider OpenAI-compatible definido en ~/.pi/agent/models.json
        ensurePiConfig(baseUrl + QStringLiteral("/v1"));
        env.insert(QStringLiteral("OPENAI_BASE_URL"), baseUrl + QStringLiteral("/v1"));
        env.insert(QStringLiteral("PI_SKIP_VERSION_CHECK"), QStringLiteral("1"));
    }
    for (auto it = ctx.harness.env.cbegin(); it != ctx.harness.env.cend(); ++it)
        env.insert(it.key(), it.value());
    env.insert(QStringLiteral("LLAMACODE_MANAGED"), QStringLiteral("1"));
    env.insert(QStringLiteral("LLAMACODE_ROLE"),    QStringLiteral("harness-") + adapter);
    env.insert(QStringLiteral("LLAMACODE_APP_PID"), QString::number(QCoreApplication::applicationPid()));

    const QString agentCwd = m_agentCwdOverride.isEmpty()
        ? ctx.workspace.cwd.trimmed() : m_agentCwdOverride;

    // pi: integración estructurada por mensaje (print mode), sin proceso persistente.
    if (adapter == QLatin1String("pi")) {
        const QString sessDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                + QStringLiteral("/pi-sessions");
        QDir().mkpath(sessDir);
        env.insert(QStringLiteral("PI_CODING_AGENT_SESSION_DIR"), sessDir);

        m_piExe          = exe;
        m_piEnv          = env;
        m_piCwd          = (!agentCwd.isEmpty() && QFileInfo(agentCwd).isDir()) ? agentCwd : QString();
        m_piSessionPath  = sessDir + QStringLiteral("/llamacode-")
                           + QString::number(QDateTime::currentMSecsSinceEpoch()) + QStringLiteral(".json");
        m_agentCwdOverride.clear();
        m_piActive            = true;
        m_agentStarting       = false;
        m_activeAgentAdapter  = adapter;
        m_agentInTerminal     = false;
        m_agentMessages.clear();
        m_currentAssistantIdx = -1;
        appendAgentEvent(QStringLiteral("pi/lifecycle"), QStringLiteral("pi listo - modo print por mensaje"));
        emit agentMessagesChanged();
        emit agentStartingChanged();
        emit agentRunningChanged();
        return;
    }

    // Backends estructurados (proceso propio o sin proceso): delegados a IAgentBackend.
    if (adapter == QLatin1String("opencode")) {
        IAgentBackend *b = ensureAgentBackend(adapter);
        if (!b) {
            appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: backend %1 no disponible").arg(adapter));
            m_agentStarting = false;
            emit agentStartingChanged();
            return;
        }
        AgentContext c;
        c.adapter         = adapter;
        c.launchProfileId = launchProfileId;
        c.exePath         = exe;
        c.cwd             = (!agentCwd.isEmpty() && QFileInfo(agentCwd).isDir()) ? agentCwd : QString();
        c.serverBaseUrl   = baseUrl;
        c.modelId         = ctx.catalogModel.id;
        c.env             = env;
        m_agentCwdOverride.clear();
        m_activeAgentAdapter = adapter;
        m_agentInTerminal    = false;
        m_currentAssistantIdx = -1;
        b->start(c);
        m_agentMessages = b->messages();
        emit agentMessagesChanged();
        return;
    }

    m_activeAgentAdapter = adapter;
    m_agentInTerminal = false;
    m_agentPid = 0;
    m_agentProc = new QProcess(this);
    m_agentProc->setProcessEnvironment(env);

    const QString cwd = m_agentCwdOverride.isEmpty()
        ? ctx.workspace.cwd.trimmed() : m_agentCwdOverride;
    m_agentCwdOverride.clear();
    if (!cwd.isEmpty() && QFileInfo(cwd).isDir())
        m_agentProc->setWorkingDirectory(cwd);

    connect(m_agentProc, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_agentProc) return;
        appendAgentEvent(QStringLiteral("proc/stdout"),
                         QString::fromUtf8(m_agentProc->readAllStandardOutput()));
    });
    connect(m_agentProc, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_agentProc) return;
        appendAgentEvent(QStringLiteral("proc/stderr"),
                         QString::fromUtf8(m_agentProc->readAllStandardError()));
    });
    connect(m_agentProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        appendAgentEvent(QStringLiteral("lifecycle"),
                         QStringLiteral("agent exited with code %1").arg(code));
        if (m_agentProc) {
            m_agentProc->deleteLater();
            m_agentProc = nullptr;
        }
        m_agentMessages.clear();
        m_currentAssistantIdx = -1;
        emit agentMessagesChanged();
        clearServiceState(QStringLiteral("harness"));
        m_activeAgentAdapter.clear();
        m_agentPid = 0;
        m_agentInTerminal = false;
        m_agentStopping = false;
        emit agentRunningChanged();
    });

    // Path genérico: adapters basados en stdin (p.ej. smallcode).
    // opencode se maneja vía OpencodeBackend (delegado arriba) y nunca llega acá.
    QString program = exe;
    QStringList programArgs = ctx.harness.args;

    appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("starting %1").arg(adapter));
    if (!cwd.isEmpty())
        appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("cwd: %1")
                         .arg(QDir::toNativeSeparators(cwd)));
    m_agentProc->start(program, programArgs);
    if (!m_agentProc->waitForStarted(5000)) {
        appendAgentEvent(QStringLiteral("lifecycle"), QStringLiteral("Error: failed to start agent process"));
        if (m_agentStarting) {
            m_agentStarting = false;
            emit agentStartingChanged();
        }
        m_agentProc->deleteLater();
        m_agentProc = nullptr;
        m_activeAgentAdapter.clear();
        return;
    }
    if (m_agentStarting) {
        m_agentStarting = false;
        emit agentStartingChanged();
    }
    m_agentPid = m_agentProc->processId();
    assignToJobObject(m_agentPid);
    QVariantMap agentExtra;
    agentExtra[QStringLiteral("adapter")] = adapter;
    writeServiceState(QStringLiteral("harness"), m_agentPid, agentExtra);
    appendAgentEvent(QStringLiteral("lifecycle"),
                     QStringLiteral("%1 running (PID %2)").arg(adapter).arg(m_agentPid));
    emit agentRunningChanged();
}

void AppController::cancelAgentGeneration()
{
    if (m_agentBackend)
        m_agentBackend->cancelGeneration();
}

void AppController::stopAgent()
{
    m_agentStopping = true;
    if (m_agentStarting || !m_pendingAutoAgentLaunchId.isEmpty()) {
        m_agentStarting = false;
        m_pendingAutoAgentLaunchId.clear();
        emit agentStartingChanged();
    }
    if (m_agentBackend && m_agentBackend->running()) {
        m_agentBackend->stop();
        return;
    }

    if (m_piActive) {
        if (m_piMsgProc) {
            m_piMsgProc->kill();
            m_piMsgProc->deleteLater();
            m_piMsgProc = nullptr;
        }
        m_piActive = false;
        m_activeAgentAdapter.clear();
        appendAgentEvent(QStringLiteral("pi/lifecycle"), QStringLiteral("pi detenido"));
        emit agentRunningChanged();
        return;
    }

    if (m_agentInTerminal && m_agentPid) {
#ifdef Q_OS_WIN
        // Kill the terminal process tree (includes child opencode process)
        QProcess::execute(QStringLiteral("taskkill"),
            {QStringLiteral("/PID"), QString::number(m_agentPid),
             QStringLiteral("/T"), QStringLiteral("/F")});
#else
        ::kill(static_cast<pid_t>(m_agentPid), SIGTERM);
#endif
        m_agentPid = 0;
        m_activeAgentAdapter.clear();
        m_agentInTerminal = false;
        if (m_agentPollTimer) m_agentPollTimer->stop();
        emit agentRunningChanged();
        return;
    }
    if (!m_agentProc) return;
#ifdef Q_OS_WIN
    // Matar el árbol completo (opencode serve lanza un hijo node que retiene el puerto).
    const qint64 pid = m_agentProc->processId();
    if (pid > 0)
        QProcess::execute(QStringLiteral("taskkill"),
            {QStringLiteral("/PID"), QString::number(pid),
             QStringLiteral("/T"), QStringLiteral("/F")});
#endif
    m_agentProc->terminate();
    if (!m_agentProc->waitForFinished(2000))
        m_agentProc->kill();
}

bool AppController::agentMasterConfigured() const
{
    auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend);
    return cb && cb->masterConfigured();
}

bool AppController::escalateToMaster(const QString &problem)
{
    auto *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend);
    if (!cb || !cb->running()) return false;
    return cb->escalateToMaster(problem);
}

void AppController::sendToAgent(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (m_agentBackend && m_agentBackend->running()) {
        m_agentBackend->sendMessage(text);
        return;
    }
    if (m_piActive) {
        appendAgentEvent(QStringLiteral("input"), QStringLiteral("> %1").arg(text));
        sendPiMessage(text);
        return;
    }

    if (!m_agentProc || m_agentProc->state() != QProcess::Running) return;

    appendAgentEvent(QStringLiteral("input"), QStringLiteral("> %1").arg(text));

    // Adapters genéricos basados en stdin (opencode usa OpencodeBackend, delegado arriba).
    m_agentProc->write((text + QLatin1Char('\n')).toUtf8());
}

void AppController::runTask(const QString &id)
{
    const QVariantMap task = m_tasks.get(id);
    if (task.isEmpty()) {
        emit serverError(QStringLiteral("Task no encontrada."));
        return;
    }

    const bool agentUp = (m_agentBackend && m_agentBackend->running())
                         || m_piActive
                         || (m_agentProc && m_agentProc->state() == QProcess::Running);
    if (agentUp) {
        // Agente ya corriendo: usarlo tal cual, sin apagarlo.
        m_runningTaskId = id;
        m_tasks.markRun(id, QStringLiteral("running"));
        sendToAgent(TaskStore::composePrompt(task));
        return;
    }

    // No hay agente: auto-iniciar con el perfil de la Task (o el último activo),
    // ejecutar al quedar listo y apagarlo al terminar el turno.
    QString launchId = task.value("profileId").toString();
    if (launchId.isEmpty()) launchId = m_activeLaunchId;
    if (launchId.isEmpty()) {
        m_tasks.markRun(id, QStringLiteral("error"));
        appendAgentEvent(QStringLiteral("lifecycle"),
                         QStringLiteral("Task '%1' sin perfil y sin perfil activo: no se puede auto-iniciar el agente.").arg(id));
        emit serverError(QStringLiteral("Task sin perfil de agente: no se pudo auto-iniciar. Asigná un perfil en la Task."));
        return;
    }

    m_pendingScheduledTaskId = id;
    m_pendingScheduledLaunchId = launchId;
    m_scheduledAutoStop = true;
    m_tasks.markRun(id, QStringLiteral("running"));
    appendAgentEvent(QStringLiteral("lifecycle"),
                     QStringLiteral("Auto-iniciando servidor+agente para la Task '%1'.").arg(id));
    startServerAndAgent(launchId);
}

void AppController::dispatchPendingScheduledTask()
{
    if (m_pendingScheduledTaskId.isEmpty()) return;
    if (!(m_agentBackend && m_agentBackend->running())) return;
    const QString id = m_pendingScheduledTaskId;
    m_pendingScheduledTaskId.clear();
    const QVariantMap task = m_tasks.get(id);
    if (task.isEmpty()) { m_scheduledAutoStop = false; return; }
    m_runningTaskId = id;
    sendToAgent(TaskStore::composePrompt(task));
}

void AppController::onAgentTurnFinished()
{
    if (!m_runningTaskId.isEmpty()) {
        m_tasks.markRun(m_runningTaskId, QStringLiteral("ok"));
        m_runningTaskId.clear();
    }
    if (m_scheduledAutoStop) {
        m_scheduledAutoStop = false;
        appendAgentEvent(QStringLiteral("lifecycle"),
                         QStringLiteral("Scheduler: Task terminada; apagando agente y servidor auto-iniciados."));
        stopAgent();
        stopServer();
    }
    if (m_restartThinkingAfterResponse)
        restartActiveLaunchForThinking(m_restartThinkingWithAgent, false);
}

void AppController::setTasksSchedulerEnabled(bool on)
{
    if (!m_scheduler || m_scheduler->enabled() == on) return;
    m_scheduler->setEnabled(on);
    QSettings().setValue(QStringLiteral("tasks/schedulerEnabled"), on);
    emit tasksSchedulerChanged();
}

QString AppController::previewTaskPrompt(const QString &id) const
{
    return TaskStore::composePrompt(m_tasks.get(id));
}

QString AppController::recordTaskBrowserStep(const QString &skillName, const QString &url)
{
    const QString slug = TaskStore::sanitize(skillName);
    if (slug.isEmpty()) {
        emit serverError(QStringLiteral("Nombre de paso inválido."));
        return {};
    }
    const QString err = recordBrowserSkill(slug, url);
    if (!err.isEmpty()) {
        emit serverError(err);
        return {};
    }
    return slug;
}

void AppController::sendToAgentWithAttachments(const QString &text, const QStringList &paths)
{
    QStringList filtered;
    for (const QString &p : paths) {
        if (!m_serverHasVision && isSupportedImageAttachment(p)) {
            appendAgentEvent(QStringLiteral("attachments"),
                             QStringLiteral("Imagen omitida sin vision: %1").arg(QFileInfo(p).fileName()));
            continue;
        }
        filtered.append(p);
    }

    if (text.trimmed().isEmpty() && filtered.isEmpty()) return;
    if (auto *la = qobject_cast<LlamaAgentBackend *>(m_agentBackend)) {
        if (m_agentBackend->running()) {
            la->setPendingAttachments(filtered);
            la->sendMessage(text);
            return;
        }
    }
    sendToAgent(text);   // backend sin soporte de adjuntos → texto solo
}

// Filtro del diálogo de adjuntos. Documentos ricos (pdf/office/html/epub) los
// extrae DocumentExtractor (markitdown); texto/código directo; imágenes por
// visión (solo si el server tiene mmproj).
static QString attachmentFilter(bool withImages)
{
    static const QString kImages = QStringLiteral("*.png *.jpg *.jpeg *.webp *.gif *.bmp *.tif *.tiff");
    static const QString kDocs = QStringLiteral(
        "*.pdf *.docx *.doc *.xlsx *.xls *.pptx *.ppt *.odt *.ods *.odp *.rtf *.epub *.html *.htm");
    static const QString kText = QStringLiteral(
        "*.txt *.md *.json *.csv *.tsv *.log *.py *.js *.ts *.tsx *.jsx *.cpp *.h *.hpp *.c "
        "*.cs *.java *.go *.rs *.rb *.php *.sh *.sql *.xml *.yaml *.yml *.toml *.ini *.qml");

    const QString supported = withImages
        ? QStringLiteral("%1 %2 %3").arg(kImages, kDocs, kText)
        : QStringLiteral("%1 %2").arg(kDocs, kText);

    QString f = QStringLiteral("Soportados (%1);;Documentos (%2);;").arg(supported, kDocs);
    if (withImages)
        f += QStringLiteral("Imágenes (%1);;").arg(kImages);
    f += QStringLiteral("Texto/código (%1);;Todos (*.*)").arg(kText);
    return f;
}

QStringList AppController::pickAgentAttachments()
{
    QWidget *parent = QApplication::activeWindow();
    // Sin visión: ocultar imágenes del filtro (no las puede usar).
    return QFileDialog::getOpenFileNames(
        parent, QStringLiteral("Adjuntar archivos"), QDir::homePath(),
        attachmentFilter(m_serverHasVision));
}

void AppController::steerAgent(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (m_agentBackend && m_agentBackend->running()) { m_agentBackend->steerMessage(text); return; }
    sendToAgent(text);   // sin turno activo → envío normal
}

void AppController::queueAgent(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (m_agentBackend && m_agentBackend->running()) { m_agentBackend->queueMessage(text); return; }
    sendToAgent(text);   // sin turno activo → envío normal
}

void AppController::clearAgentQueue()
{
    if (m_agentBackend) m_agentBackend->clearQueue();
}

void AppController::recheckGit()
{
    const bool avail = !QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty();
    if (avail != m_gitAvailable) { m_gitAvailable = avail; emit gitAvailableChanged(); }
}

void AppController::installGit()
{
    if (m_gitInstallProc) return;   // ya en curso
#ifdef Q_OS_WIN
    m_gitInstallProc = new QProcess(this);
    connect(m_gitInstallProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        appendAgentEvent(QStringLiteral("git"),
                         QStringLiteral("Instalación de git terminó (code %1)").arg(code));
        m_gitInstallProc->deleteLater();
        m_gitInstallProc = nullptr;
        recheckGit();                 // re-detectar (puede requerir reabrir la terminal/PATH)
        emit gitAvailableChanged();   // refrescar installingGit
    });
    appendAgentEvent(QStringLiteral("git"), QStringLiteral("Instalando Git vía winget…"));
    emit gitAvailableChanged();       // installingGit = true
    m_gitInstallProc->start(QStringLiteral("winget"),
        {QStringLiteral("install"), QStringLiteral("--id"), QStringLiteral("Git.Git"),
         QStringLiteral("--exact"), QStringLiteral("--source"), QStringLiteral("winget"),
         QStringLiteral("--accept-source-agreements"), QStringLiteral("--accept-package-agreements")});
    if (!m_gitInstallProc->waitForStarted(5000)) {
        emit serverError(QStringLiteral("No se pudo iniciar winget para instalar Git."));
        m_gitInstallProc->deleteLater(); m_gitInstallProc = nullptr;
        emit gitAvailableChanged();
    }
#else
    emit serverError(QStringLiteral("Instalá git con el gestor de paquetes de tu sistema."));
#endif
}

void AppController::rollbackAgentToMessage(int msgIndex)
{
    if (auto *la = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        la->rollbackToMessage(msgIndex);
}

void AppController::rollbackChatToMessage(int msgIndex)
{
    if (auto *raw = qobject_cast<RawChatBackend *>(ensureChatBackend()))
        raw->rollbackToMessage(msgIndex);
}

void AppController::editAgentMessage(int msgIndex, const QString &newText)
{
    if (auto *la = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        la->editMessage(msgIndex, newText);
}

void AppController::editChatMessage(int msgIndex, const QString &newText)
{
    if (auto *raw = qobject_cast<RawChatBackend *>(ensureChatBackend()))
        raw->editMessage(msgIndex, newText);
}

void AppController::clearAgentLog()
{
    m_agentLog.clear();
    m_agentMessages.clear();
    m_currentAssistantIdx = -1;
    emit agentLogChanged();
    emit agentMessagesChanged();
}

// Sesiones de agente: delegadas al backend activo (IAgentBackend).
// El path legacy (opencode por HTTP directo desde AppController) fue removido;
// OpencodeBackend es ahora el único dueño de sesiones/stream/proceso.
void AppController::newOpencodeSession()
{
    if (m_agentBackend) m_agentBackend->newSession();
}

void AppController::switchOpencodeSession(const QString &sessionId)
{
    if (m_agentBackend) m_agentBackend->switchSession(sessionId);
}

void AppController::refreshOpencodeSessionList()
{
    if (m_agentBackend) m_agentBackend->refreshSessions();
}

void AppController::newOpencodeSessionInProject(const QString &projectDir)
{
    if (m_agentBackend) m_agentBackend->newSessionInProject(projectDir);
}

void AppController::renameOpencodeSession(const QString &sessionId, const QString &title)
{
    if (m_agentBackend) m_agentBackend->renameSession(sessionId, title);
}

void AppController::deleteOpencodeSession(const QString &sessionId)
{
    if (m_agentBackend) m_agentBackend->deleteSession(sessionId);
}

void AppController::deleteOpencodeProject(const QString &projectDir)
{
    if (m_agentBackend) m_agentBackend->deleteProject(projectDir);
}

void AppController::forkOpencodeSession(const QString &sessionId)
{
    if (m_agentBackend) m_agentBackend->forkSession(sessionId);
}

QString AppController::currentAgentProjectDir() const
{
    if (m_agentBackend) return m_agentBackend->currentProjectDir();
    return m_agentProc ? m_agentProc->workingDirectory() : QString();
}

QStringList AppController::agentProjectFiles(const QString &query) const
{
    const QString root = currentAgentProjectDir();
    if (root.isEmpty() || !QFileInfo(root).isDir()) return {};
    static const QSet<QString> ignored{
        QStringLiteral("node_modules"), QStringLiteral(".git"), QStringLiteral("build"),
        QStringLiteral("build2"), QStringLiteral("dist"), QStringLiteral(".venv"),
        QStringLiteral("venv"), QStringLiteral("__pycache__"), QStringLiteral(".next"),
        QStringLiteral(".turbo"), QStringLiteral("coverage"), QStringLiteral("target"),
        QStringLiteral(".cache"), QStringLiteral(".idea"), QStringLiteral(".vs")};
    const QString q = query.trimmed().toLower();
    const QDir base(root);
    QStringList out;
    QStringList stack{root};
    int scanned = 0;
    while (!stack.isEmpty() && out.size() < 50 && scanned < 20000) {
        QDir d(stack.takeLast());
        const auto entries = d.entryInfoList(
            QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs, QDir::Name);
        for (const QFileInfo &fi : entries) {
            ++scanned;
            if (fi.isDir()) {
                if (!ignored.contains(fi.fileName())) stack << fi.absoluteFilePath();
                continue;
            }
            const QString rel = base.relativeFilePath(fi.absoluteFilePath());
            if (q.isEmpty() || rel.toLower().contains(q)) {
                out << rel;
                if (out.size() >= 50) break;
            }
        }
    }
    out.sort();
    return out;
}

// ───────────────────────── Opencode config helpers ─────────────────────────
QString AppController::ocGlobalConfigDir() const
{
    const QByteArray xdg = qgetenv("XDG_CONFIG_HOME");
    const QString base = xdg.isEmpty() ? (QDir::homePath() + QStringLiteral("/.config"))
                                       : QString::fromUtf8(xdg);
    return base + QStringLiteral("/opencode");
}

QString AppController::ocConfigFilePath(const QString &scope, const QString &projectDir) const
{
    if (scope == QLatin1String("project")) {
        if (projectDir.isEmpty()) return QString();
        return QDir::cleanPath(projectDir) + QStringLiteral("/opencode.json");
    }
    return ocGlobalConfigDir() + QStringLiteral("/opencode.json");
}

QString AppController::ocCommandDir(const QString &scope, const QString &projectDir) const
{
    if (scope == QLatin1String("project")) {
        if (projectDir.isEmpty()) return QString();
        return QDir::cleanPath(projectDir) + QStringLiteral("/.opencode/command");
    }
    return ocGlobalConfigDir() + QStringLiteral("/command");
}

QJsonObject AppController::ocReadConfigObj(const QString &scope, const QString &projectDir) const
{
    const QString path = ocConfigFilePath(scope, projectDir);
    if (path.isEmpty()) return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool AppController::ocWriteConfigObj(const QString &scope, const QString &projectDir, const QJsonObject &obj)
{
    const QString path = ocConfigFilePath(scope, projectDir);
    if (path.isEmpty()) return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

QString AppController::opencodeConfigPath(const QString &scope, const QString &projectDir) const
{
    return QDir::toNativeSeparators(ocConfigFilePath(scope, projectDir));
}

QString AppController::readOpencodeConfig(const QString &scope, const QString &projectDir) const
{
    const QString path = ocConfigFilePath(scope, projectDir);
    if (path.isEmpty()) return QStringLiteral("{}");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("{\n  \"$schema\": \"https://opencode.ai/config.json\"\n}");
    const QByteArray raw = f.readAll();
    if (raw.trimmed().isEmpty()) return QStringLiteral("{}");
    return QString::fromUtf8(raw);
}

bool AppController::writeOpencodeConfig(const QString &scope, const QString &projectDir, const QString &jsonText)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit serverError(QStringLiteral("opencode.json inválido: %1").arg(err.errorString()));
        return false;
    }
    const QString path = ocConfigFilePath(scope, projectDir);
    if (path.isEmpty()) { emit serverError(QStringLiteral("Sin proyecto seleccionado.")); return false; }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit serverError(QStringLiteral("No se pudo escribir %1").arg(path));
        return false;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

// ───────────────────────── MCP servers ─────────────────────────
QVariantList AppController::listMcpServers(const QString &scope, const QString &projectDir) const
{
    QVariantList out;
    const QJsonObject mcp = ocReadConfigObj(scope, projectDir).value(QStringLiteral("mcp")).toObject();
    for (auto it = mcp.begin(); it != mcp.end(); ++it) {
        const QJsonObject s = it.value().toObject();
        QVariantMap m;
        m[QStringLiteral("name")]    = it.key();
        m[QStringLiteral("type")]    = s.value(QStringLiteral("type")).toString(QStringLiteral("local"));
        m[QStringLiteral("enabled")] = s.value(QStringLiteral("enabled")).toBool(true);
        if (s.contains(QStringLiteral("url")))
            m[QStringLiteral("url")] = s.value(QStringLiteral("url")).toString();
        if (s.contains(QStringLiteral("command"))) {
            QStringList cmd;
            for (const QJsonValue &cv : s.value(QStringLiteral("command")).toArray())
                cmd << cv.toString();
            m[QStringLiteral("command")] = cmd.join(QLatin1Char(' '));
        }
        out.append(m);
    }
    return out;
}

bool AppController::setMcpServer(const QString &scope, const QString &projectDir,
                                 const QString &name, const QVariantMap &def)
{
    if (name.trimmed().isEmpty()) return false;
    QJsonObject root = ocReadConfigObj(scope, projectDir);
    QJsonObject mcp = root.value(QStringLiteral("mcp")).toObject();

    QJsonObject s;
    const QString type = def.value(QStringLiteral("type"), QStringLiteral("local")).toString();
    s[QStringLiteral("type")]    = type;
    s[QStringLiteral("enabled")] = def.value(QStringLiteral("enabled"), true).toBool();
    if (type == QLatin1String("remote")) {
        s[QStringLiteral("url")] = def.value(QStringLiteral("url")).toString();
    } else {
        QJsonArray cmd;
        const QStringList parts = def.value(QStringLiteral("command")).toString()
                                     .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &p : parts) cmd.append(p);
        s[QStringLiteral("command")] = cmd;
    }
    mcp[name] = s;
    root[QStringLiteral("mcp")] = mcp;
    return ocWriteConfigObj(scope, projectDir, root);
}

bool AppController::removeMcpServer(const QString &scope, const QString &projectDir, const QString &name)
{
    QJsonObject root = ocReadConfigObj(scope, projectDir);
    QJsonObject mcp = root.value(QStringLiteral("mcp")).toObject();
    if (!mcp.contains(name)) return false;
    mcp.remove(name);
    root[QStringLiteral("mcp")] = mcp;
    return ocWriteConfigObj(scope, projectDir, root);
}

bool AppController::toggleMcpServer(const QString &scope, const QString &projectDir,
                                    const QString &name, bool enabled)
{
    QJsonObject root = ocReadConfigObj(scope, projectDir);
    QJsonObject mcp = root.value(QStringLiteral("mcp")).toObject();
    if (!mcp.contains(name)) return false;
    QJsonObject s = mcp.value(name).toObject();
    s[QStringLiteral("enabled")] = enabled;
    mcp[name] = s;
    root[QStringLiteral("mcp")] = mcp;
    return ocWriteConfigObj(scope, projectDir, root);
}

// ───────────────────────── Cuentas de correo ─────────────────────────
// Almacén: mismo config JSON global que MCP, bajo clave "mail". El password
// NUNCA se serializa: va a SecretStore con ref "mail/<name>".
static MailClient::Account mailAccountFromMap(const QVariantMap &m)
{
    MailClient::Account a;
    a.name         = m.value(QStringLiteral("name")).toString();
    a.email        = m.value(QStringLiteral("email")).toString();
    a.displayName  = m.value(QStringLiteral("displayName")).toString();
    a.provider     = m.value(QStringLiteral("provider")).toString();
    a.smtpHost     = m.value(QStringLiteral("smtpHost")).toString();
    a.smtpPort     = m.value(QStringLiteral("smtpPort")).toInt();
    a.smtpSecurity = m.value(QStringLiteral("smtpSecurity")).toString();
    a.recvProto    = m.value(QStringLiteral("recvProto")).toString();
    a.recvHost     = m.value(QStringLiteral("recvHost")).toString();
    a.recvPort     = m.value(QStringLiteral("recvPort")).toInt();
    a.recvSsl      = m.value(QStringLiteral("recvSsl"), true).toBool();
    a.user         = m.value(QStringLiteral("user")).toString();
    return a;
}

QVariantList AppController::listMailAccounts() const
{
    QVariantList out;
    const QJsonObject mail = ocReadConfigObj(QStringLiteral("global"), QString())
                                 .value(QStringLiteral("mail")).toObject();
    for (auto it = mail.begin(); it != mail.end(); ++it) {
        const QJsonObject s = it.value().toObject();
        QVariantMap m;
        m[QStringLiteral("name")]         = it.key();
        m[QStringLiteral("email")]        = s.value(QStringLiteral("email")).toString();
        m[QStringLiteral("displayName")]  = s.value(QStringLiteral("displayName")).toString();
        m[QStringLiteral("provider")]     = s.value(QStringLiteral("provider")).toString();
        m[QStringLiteral("smtpHost")]     = s.value(QStringLiteral("smtpHost")).toString();
        m[QStringLiteral("smtpPort")]     = s.value(QStringLiteral("smtpPort")).toInt();
        m[QStringLiteral("smtpSecurity")] = s.value(QStringLiteral("smtpSecurity")).toString();
        m[QStringLiteral("recvProto")]    = s.value(QStringLiteral("recvProto")).toString();
        m[QStringLiteral("recvHost")]     = s.value(QStringLiteral("recvHost")).toString();
        m[QStringLiteral("recvPort")]     = s.value(QStringLiteral("recvPort")).toInt();
        m[QStringLiteral("recvSsl")]      = s.value(QStringLiteral("recvSsl")).toBool(true);
        m[QStringLiteral("user")]         = s.value(QStringLiteral("user")).toString();
        m[QStringLiteral("hasPassword")]  = m_secrets.has(QStringLiteral("mail/") + it.key());
        out.append(m);
    }
    return out;
}

bool AppController::setMailAccount(const QString &name, const QVariantMap &def)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return false;
    QJsonObject root = ocReadConfigObj(QStringLiteral("global"), QString());
    QJsonObject mail = root.value(QStringLiteral("mail")).toObject();

    QJsonObject s;
    s[QStringLiteral("email")]        = def.value(QStringLiteral("email")).toString();
    s[QStringLiteral("displayName")]  = def.value(QStringLiteral("displayName")).toString();
    s[QStringLiteral("provider")]     = def.value(QStringLiteral("provider")).toString();
    s[QStringLiteral("smtpHost")]     = def.value(QStringLiteral("smtpHost")).toString();
    s[QStringLiteral("smtpPort")]     = def.value(QStringLiteral("smtpPort")).toInt();
    s[QStringLiteral("smtpSecurity")] = def.value(QStringLiteral("smtpSecurity")).toString();
    s[QStringLiteral("recvProto")]    = def.value(QStringLiteral("recvProto")).toString();
    s[QStringLiteral("recvHost")]     = def.value(QStringLiteral("recvHost")).toString();
    s[QStringLiteral("recvPort")]     = def.value(QStringLiteral("recvPort")).toInt();
    s[QStringLiteral("recvSsl")]      = def.value(QStringLiteral("recvSsl"), true).toBool();
    s[QStringLiteral("user")]         = def.value(QStringLiteral("user")).toString();
    mail[n] = s;
    root[QStringLiteral("mail")] = mail;

    // El password (si vino) va al SecretStore, no al JSON.
    const QString pass = def.value(QStringLiteral("password")).toString();
    if (!pass.isEmpty()) m_secrets.set(QStringLiteral("mail/") + n, pass);

    const bool okw = ocWriteConfigObj(QStringLiteral("global"), QString(), root);
    if (okw) pushMailAccountsToAgent();
    return okw;
}

bool AppController::removeMailAccount(const QString &name)
{
    QJsonObject root = ocReadConfigObj(QStringLiteral("global"), QString());
    QJsonObject mail = root.value(QStringLiteral("mail")).toObject();
    if (!mail.contains(name)) return false;
    mail.remove(name);
    root[QStringLiteral("mail")] = mail;
    m_secrets.remove(QStringLiteral("mail/") + name);
    const bool okw = ocWriteConfigObj(QStringLiteral("global"), QString(), root);
    if (okw) pushMailAccountsToAgent();
    return okw;
}

QVariantList AppController::mailAccountsResolved() const
{
    QVariantList out = listMailAccounts();
    for (QVariant &v : out) {
        QVariantMap m = v.toMap();
        m[QStringLiteral("password")] = m_secrets.resolve(
            QStringLiteral("mail/") + m.value(QStringLiteral("name")).toString());
        m.remove(QStringLiteral("hasPassword"));
        v = m;
    }
    return out;
}

QString AppController::testMailAccount(const QString &name) const
{
    for (const QVariant &v : listMailAccounts()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("name")).toString() != name) continue;
        MailClient::Account a = mailAccountFromMap(m);
        a.password = m_secrets.resolve(QStringLiteral("mail/") + name);
        if (a.password.isEmpty())
            return QStringLiteral("No hay contraseña guardada para esta cuenta.");
        QString err;
        if (MailClient::testAccount(a, &err)) return QString();
        return err.isEmpty() ? QStringLiteral("Falló la prueba.") : err;
    }
    return QStringLiteral("Cuenta no encontrada.");
}

void AppController::setMailAutoSend(bool on)
{
    if (on == m_mailAutoSend) return;
    m_mailAutoSend = on;
    writeSetting(QStringLiteral("agent/mailAutoSend"), on);
    if (LlamaAgentBackend *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend))
        cb->setMailAutoSend(on);
    emit mailAutoSendChanged();
}

void AppController::pushMailAccountsToAgent()
{
    if (LlamaAgentBackend *cb = qobject_cast<LlamaAgentBackend *>(m_agentBackend)) {
        cb->setMailAccounts(mailAccountsResolved());
        cb->setMailAutoSend(m_mailAutoSend);
    }
}

// ───────────────────────── Integrations ──────────────────────────────
QString AppController::integrationsFilePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/integrations.json");
}

QJsonArray AppController::readApiServices() const
{
    QFile f(integrationsFilePath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).array();
}

bool AppController::writeApiServices(const QJsonArray &arr)
{
    QFile f(integrationsFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(arr).toJson());
    f.close();
    emit integrationsChanged();
    return true;
}

QVariantList AppController::integrations() const
{
    QVariantList out;
    // MCP servers desde el config opencode global (single source: lo consume el agente).
    for (const QVariant &mv : listMcpServers(QStringLiteral("global"), QString())) {
        const QVariantMap m = mv.toMap();
        const QString type = m.value(QStringLiteral("type")).toString();
        QVariantMap it;
        it[QStringLiteral("id")]      = QStringLiteral("mcp:") + m.value(QStringLiteral("name")).toString();
        it[QStringLiteral("type")]    = QStringLiteral("mcp");
        it[QStringLiteral("name")]    = m.value(QStringLiteral("name"));
        it[QStringLiteral("enabled")] = m.value(QStringLiteral("enabled"), true);
        it[QStringLiteral("summary")] = type == QLatin1String("remote")
            ? m.value(QStringLiteral("url")).toString()
            : m.value(QStringLiteral("command")).toString();
        it[QStringLiteral("config")]  = m;
        out.append(it);
    }
    // API services desde integrations.json.
    for (const QJsonValue &v : readApiServices()) {
        const QJsonObject o = v.toObject();
        QVariantMap it;
        it[QStringLiteral("id")]      = QStringLiteral("api:") + o.value(QStringLiteral("id")).toString();
        it[QStringLiteral("type")]    = QStringLiteral("api_service");
        it[QStringLiteral("name")]    = o.value(QStringLiteral("name")).toString();
        it[QStringLiteral("enabled")] = o.value(QStringLiteral("enabled")).toBool(true);
        it[QStringLiteral("summary")] = o.value(QStringLiteral("baseUrl")).toString();
        QVariantMap cfg;
        cfg[QStringLiteral("baseUrl")] = o.value(QStringLiteral("baseUrl")).toString();
        cfg[QStringLiteral("hasKey")]  = !o.value(QStringLiteral("apiKey")).toString().isEmpty();
        it[QStringLiteral("config")]   = cfg;
        out.append(it);
    }
    return out;
}

bool AppController::saveMcpIntegration(const QString &name, const QString &type,
                                       const QString &commandOrUrl)
{
    if (name.trimmed().isEmpty()) return false;
    QVariantMap def;
    def[QStringLiteral("type")]    = type.isEmpty() ? QStringLiteral("local") : type;
    def[QStringLiteral("enabled")] = true;
    if (type == QLatin1String("remote")) def[QStringLiteral("url")]     = commandOrUrl;
    else                                 def[QStringLiteral("command")] = commandOrUrl;
    const bool ok = setMcpServer(QStringLiteral("global"), QString(), name.trimmed(), def);
    if (ok) emit integrationsChanged();
    return ok;
}

bool AppController::saveApiService(const QString &id, const QString &name,
                                   const QString &baseUrl, const QString &apiKey, bool enabled)
{
    if (name.trimmed().isEmpty()) return false;
    QJsonArray arr = readApiServices();
    if (id.isEmpty()) {
        arr.append(QJsonObject{
            {QStringLiteral("id"),      QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("name"),    name.trimmed()},
            {QStringLiteral("baseUrl"), baseUrl.trimmed()},
            {QStringLiteral("apiKey"),  apiKey},
            {QStringLiteral("enabled"), enabled}});
        return writeApiServices(arr);
    }
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject o = arr[i].toObject();
        if (o.value(QStringLiteral("id")).toString() == id) {
            o[QStringLiteral("name")]    = name.trimmed();
            o[QStringLiteral("baseUrl")] = baseUrl.trimmed();
            // key vacío en edición = conservar la existente.
            if (!apiKey.isEmpty()) o[QStringLiteral("apiKey")] = apiKey;
            o[QStringLiteral("enabled")] = enabled;
            arr[i] = o;
            return writeApiServices(arr);
        }
    }
    return false;
}

bool AppController::removeIntegration(const QString &id)
{
    if (id.startsWith(QStringLiteral("mcp:"))) {
        const bool ok = removeMcpServer(QStringLiteral("global"), QString(), id.mid(4));
        if (ok) emit integrationsChanged();
        return ok;
    }
    if (id.startsWith(QStringLiteral("api:"))) {
        const QString aid = id.mid(4);
        QJsonArray arr = readApiServices();
        for (int i = 0; i < arr.size(); ++i)
            if (arr[i].toObject().value(QStringLiteral("id")).toString() == aid) {
                arr.removeAt(i);
                return writeApiServices(arr);
            }
    }
    return false;
}

bool AppController::setIntegrationEnabled(const QString &id, bool enabled)
{
    if (id.startsWith(QStringLiteral("mcp:"))) {
        const bool ok = toggleMcpServer(QStringLiteral("global"), QString(), id.mid(4), enabled);
        if (ok) emit integrationsChanged();
        return ok;
    }
    if (id.startsWith(QStringLiteral("api:"))) {
        const QString aid = id.mid(4);
        QJsonArray arr = readApiServices();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject o = arr[i].toObject();
            if (o.value(QStringLiteral("id")).toString() == aid) {
                o[QStringLiteral("enabled")] = enabled; arr[i] = o;
                return writeApiServices(arr);
            }
        }
    }
    return false;
}

void AppController::testIntegration(const QString &id)
{
    if (id.startsWith(QStringLiteral("api:"))) {
        const QString aid = id.mid(4);
        QString baseUrl, apiKey;
        for (const QJsonValue &v : readApiServices()) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("id")).toString() == aid) {
                baseUrl = o.value(QStringLiteral("baseUrl")).toString();
                apiKey  = o.value(QStringLiteral("apiKey")).toString();
                break;
            }
        }
        if (baseUrl.isEmpty()) { emit integrationTestResult(id, false, QStringLiteral("URL vacía")); return; }
        if (!m_nam) m_nam = new QNetworkAccessManager(this);
        QNetworkRequest req((QUrl(baseUrl)));
        if (!apiKey.isEmpty())
            req.setRawHeader(QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
        QNetworkReply *reply = m_nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString err = reply->errorString();
            const bool neterr = reply->error() != QNetworkReply::NoError && status == 0;
            reply->deleteLater();
            if (neterr) emit integrationTestResult(id, false, err);
            else        emit integrationTestResult(id, status > 0 && status < 500,
                                                   QStringLiteral("HTTP %1").arg(status));
        });
        return;
    }
    if (id.startsWith(QStringLiteral("mcp:"))) {
        const QString name = id.mid(4);
        // Buscar command/url del MCP en el config global.
        QString command, type;
        for (const QVariant &mv : listMcpServers(QStringLiteral("global"), QString())) {
            const QVariantMap m = mv.toMap();
            if (m.value(QStringLiteral("name")).toString() == name) {
                type = m.value(QStringLiteral("type")).toString();
                command = (type == QLatin1String("remote"))
                    ? m.value(QStringLiteral("url")).toString()
                    : m.value(QStringLiteral("command")).toString();
                break;
            }
        }
        if (type == QLatin1String("remote")) {
            emit integrationTestResult(id, false,
                QStringLiteral("Test de MCP remoto no soportado todavía (solo stdio local)."));
            return;
        }
        if (command.isEmpty()) { emit integrationTestResult(id, false, QStringLiteral("Sin comando")); return; }
        const QString cwd = currentAgentProjectDir();
        // Handshake en un hilo aparte (McpClient hace I/O bloqueante con QProcess).
        QPointer<AppController> self(this);
        (void)QtConcurrent::run([self, id, command, cwd]() {
            McpClient client(QStringLiteral("test"));
            const bool ready = client.start(command, cwd);
            int n = ready ? client.tools().size() : 0;
            QStringList names;
            for (const auto &t : client.tools()) { names << t.name; if (names.size() >= 8) break; }
            client.shutdown();
            const QString msg = ready
                ? QStringLiteral("OK · %1 tools%2").arg(n)
                      .arg(names.isEmpty() ? QString() : QStringLiteral(": ") + names.join(QStringLiteral(", ")))
                : QStringLiteral("Falló el handshake (revisá el comando).");
            if (!self) return;
            QMetaObject::invokeMethod(self, [self, id, ready, msg]() {
                if (self) emit self->integrationTestResult(id, ready, msg);
            }, Qt::QueuedConnection);
        });
        return;
    }
    emit integrationTestResult(id, false, QStringLiteral("Tipo desconocido"));
}

// ───────────────────────── Skills / comandos ─────────────────────────
QVariantList AppController::listOpencodeCommands(const QString &scope, const QString &projectDir) const
{
    QVariantList out;
    const QString dir = ocCommandDir(scope, projectDir);
    if (dir.isEmpty()) return out;
    QDir d(dir);
    const QStringList files = d.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        QVariantMap m;
        m[QStringLiteral("name")] = QFileInfo(f).completeBaseName();
        m[QStringLiteral("path")] = QDir::toNativeSeparators(d.absoluteFilePath(f));
        out.append(m);
    }
    return out;
}

QString AppController::readOpencodeCommand(const QString &scope, const QString &projectDir, const QString &name) const
{
    const QString dir = ocCommandDir(scope, projectDir);
    if (dir.isEmpty()) return QString();
    QFile f(dir + QLatin1Char('/') + name + QStringLiteral(".md"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

bool AppController::writeOpencodeCommand(const QString &scope, const QString &projectDir,
                                         const QString &name, const QString &content)
{
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty()) return false;
    const QString dir = ocCommandDir(scope, projectDir);
    if (dir.isEmpty()) { emit serverError(QStringLiteral("Sin proyecto seleccionado.")); return false; }
    QDir().mkpath(dir);
    QFile f(dir + QLatin1Char('/') + cleanName + QStringLiteral(".md"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(content.toUtf8());
    return true;
}

bool AppController::deleteOpencodeCommand(const QString &scope, const QString &projectDir, const QString &name)
{
    const QString dir = ocCommandDir(scope, projectDir);
    if (dir.isEmpty()) return false;
    return QFile::remove(dir + QLatin1Char('/') + name + QStringLiteral(".md"));
}

QString AppController::pickDirectory(const QString &title)
{
    QWidget *parent = QApplication::activeWindow();
    const QString dir = QFileDialog::getExistingDirectory(
        parent,
        title.isEmpty() ? QStringLiteral("Seleccionar carpeta del proyecto") : title,
        QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    return dir;
}

void AppController::changeAgentProject(const QString &directory)
{
    if (directory.isEmpty()) return;
    m_agentCwdOverride = directory;

    // Con backend activo, el reinicio en el nuevo cwd lo maneja el backend.
    if (m_agentBackend && m_agentBackend->running()) {
        m_agentBackend->newSessionInProject(directory);
        return;
    }
    if (!agentRunning()) return;

    // Restart agent with new CWD — remember launch ID
    m_pendingAgentLaunchId = m_activeAgentAdapter.isEmpty()
        ? m_activeLaunchId : m_pendingAgentLaunchId;

    // When current agent stops, restart with override
    const QString restartLaunchId = m_activeLaunchId;
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(this, &AppController::agentRunningChanged, this, [this, restartLaunchId, conn]() {
        if (agentRunning()) return;  // wait until fully stopped
        QObject::disconnect(*conn);
        if (!m_agentCwdOverride.isEmpty() && !restartLaunchId.isEmpty()) {
            QTimer::singleShot(300, this, [this, restartLaunchId]() {
                startAgent(restartLaunchId);
            });
        }
    });

    stopAgent();
}

QString AppController::agentNativeLogDir(const QString &adapter) const
{
    const QString a = adapter.trimmed().isEmpty() ? QStringLiteral("agent") : adapter.trimmed();
    const QString xdgData = qEnvironmentVariable(
        "XDG_DATA_HOME",
        QDir::homePath() + QStringLiteral("/.local/share"));

    QStringList candidates;
    candidates << (xdgData + QStringLiteral("/") + a + QStringLiteral("/log"));
#ifdef Q_OS_WIN
    candidates << (QDir::homePath() + QStringLiteral("/AppData/Roaming/")
                   + a + QStringLiteral("/log"));
#endif
    candidates << (QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                   + QStringLiteral("/") + a + QStringLiteral("/log"));

    for (const QString &p : std::as_const(candidates)) {
        if (QFileInfo::exists(p) && QFileInfo(p).isDir())
            return QDir::toNativeSeparators(p);
    }
    return QDir::toNativeSeparators(candidates.first());
}

void AppController::openAgentLogDir(const QString &adapter)
{
    QString dir = agentNativeLogDir(adapter);
    if (dir.isEmpty()) return;
    QDir d(dir);
    if (!d.exists())
        QDir().mkpath(dir);

    bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
#ifdef Q_OS_WIN
    if (!ok) {
        ok = QProcess::startDetached(QStringLiteral("explorer.exe"),
                                     {QDir::toNativeSeparators(dir)});
    }
#endif
    if (!ok) {
        emit serverError(QStringLiteral("No se pudo abrir el log nativo: %1")
                             .arg(QDir::toNativeSeparators(dir)));
    }
}

void AppController::openRuntimeLogDir()
{
    const QString dir = runtimeLogDir();
    bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
#ifdef Q_OS_WIN
    if (!ok) {
        ok = QProcess::startDetached(QStringLiteral("explorer.exe"),
                                     {QDir::toNativeSeparators(dir)});
    }
#endif
    if (!ok) {
        emit serverError(QStringLiteral("No se pudo abrir la carpeta de logs: %1")
                             .arg(QDir::toNativeSeparators(dir)));
    }
}

void AppController::setLanguage(const QString &lang)
{
    if (m_language == lang) return;
    m_language = lang;
    QSettings s;
    s.setValue(QStringLiteral("language"), lang);
    emit languageChanged();
}

QString AppController::l(const QString &key) const
{
    const auto &t = translations();
    const auto langIt = t.find(m_language);
    if (langIt != t.end()) {
        const auto it = langIt->find(key);
        if (it != langIt->end()) return *it;
    }
    const auto esIt = t.find(QStringLiteral("es"));
    if (esIt != t.end()) {
        const auto it = esIt->find(key);
        if (it != esIt->end()) return *it;
    }
    return key;
}

QVariant AppController::readSetting(const QString &key, const QVariant &defaultValue) const
{
    QSettings s;
    return s.value(key, defaultValue);
}

void AppController::writeSetting(const QString &key, const QVariant &value)
{
    QSettings s;
    s.setValue(key, value);
}

void AppController::checkForUpdates()
{
    if (m_updateReply)
        return;

    const QUrl url(QStringLiteral("https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/assets/update/latest.json"));
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    m_updateReply = m_nam->get(req);
    connect(m_updateReply, &QNetworkReply::finished, this, [this]() {
        QPointer<QNetworkReply> reply = m_updateReply;
        m_updateReply = nullptr;

        QJsonObject flag;
        if (reply && reply->error() == QNetworkReply::NoError) {
            flag = QJsonDocument::fromJson(reply->readAll()).object();
        }
        if (reply)
            reply->deleteLater();
        if (flag.isEmpty())
            flag = readBundledUpdateFlag();
        applyUpdateFlag(flag);
    });
}

void AppController::applyUpdateFlag(const QJsonObject &flag)
{
    QVariantMap info;
    const QString latest = flag.value(QStringLiteral("version")).toString().trimmed();
    const bool flagged = flag.value(QStringLiteral("newVersion")).toBool(false);
    const QVersionNumber currentV = QVersionNumber::fromString(version());
    const QVersionNumber latestV = QVersionNumber::fromString(latest);
    const bool newer = flagged && !latest.isEmpty()
        && QVersionNumber::compare(latestV, currentV) > 0;

    QSettings s;
    const QString skipUntil = s.value(QStringLiteral("updates/skipUntilVersion")).toString();
    if (!newer || skipUntil == latest) {
        if (m_updateAvailable || !m_updateInfo.isEmpty()) {
            m_updateAvailable = false;
            m_updateInfo.clear();
            emit updateCheckChanged();
        }
        return;
    }

    info[QStringLiteral("version")] = latest;
    info[QStringLiteral("title")] = flag.value(QStringLiteral("title")).toString(QStringLiteral("Nueva version disponible"));
    info[QStringLiteral("summary")] = flag.value(QStringLiteral("summary")).toString();
    info[QStringLiteral("updateUrl")] = flag.value(QStringLiteral("updateUrl")).toString();
    QVariantList changelog;
    const QJsonArray arr = flag.value(QStringLiteral("changelog")).toArray();
    for (const QJsonValue &v : arr) {
        const QString item = v.toString().trimmed();
        if (!item.isEmpty())
            changelog.append(item);
    }
    info[QStringLiteral("changelog")] = changelog;

    m_updateAvailable = true;
    m_updateInfo = info;
    emit updateCheckChanged();
}

void AppController::handleUpdateDecision(const QString &decision)
{
    const QString versionToUpdate = m_updateInfo.value(QStringLiteral("version")).toString();
    if (decision == QLatin1String("skipVersion")) {
        writeSetting(QStringLiteral("updates/skipUntilVersion"), versionToUpdate);
    } else if (decision == QLatin1String("updateNow")) {
        writeSetting(QStringLiteral("updates/skipUntilVersion"), QString());
        QString scriptUrl = m_updateInfo.value(QStringLiteral("updateUrl")).toString();
        if (scriptUrl.isEmpty())
            scriptUrl = QStringLiteral("https://raw.githubusercontent.com/guideahon/UNLZ_Llamacode/main/scripts/bootstrap.ps1");
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("powershell"),
                                {QStringLiteral("-NoProfile"),
                                 QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                                 QStringLiteral("-Command"),
                                 QStringLiteral("irm '%1' | iex").arg(scriptUrl)});
#else
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/guideahon/UNLZ_Llamacode")));
#endif
    }

    m_updateAvailable = false;
    m_updateInfo.clear();
    emit updateCheckChanged();
}

QJsonObject AppController::exportFileSet(const QString &root, const QStringList &relativePaths) const
{
    QJsonArray files;
    const QDir base(root);
    const QString basePath = QFileInfo(root).absoluteFilePath();

    auto addFile = [&](const QString &absolutePath) {
        QFileInfo fi(absolutePath);
        if (!fi.exists() || !fi.isFile()) return;
        const QString rel = base.relativeFilePath(fi.absoluteFilePath());
        if (rel.startsWith(QStringLiteral(".."))) return;
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) return;
        files.append(QJsonObject{
            {QStringLiteral("path"), rel},
            {QStringLiteral("base64"), QString::fromLatin1(f.readAll().toBase64())}
        });
    };

    for (const QString &rel : relativePaths) {
        const QString cleanRel = QDir::cleanPath(rel);
        const QFileInfo fi(base.filePath(cleanRel));
        if (!fi.exists()) continue;
        if (fi.isDir()) {
            QDirIterator it(fi.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) addFile(it.next());
        } else {
            addFile(fi.absoluteFilePath());
        }
    }

    return QJsonObject{
        {QStringLiteral("rootName"), QFileInfo(basePath).fileName()},
        {QStringLiteral("files"), files}
    };
}

bool AppController::importFileSet(const QString &root, const QJsonObject &set, QStringList *written)
{
    const QDir base(root);
    const QString basePath = QFileInfo(root).absoluteFilePath();
    QDir().mkpath(basePath);

    for (const QJsonValue &v : set.value(QStringLiteral("files")).toArray()) {
        const QJsonObject o = v.toObject();
        const QString rel = QDir::cleanPath(o.value(QStringLiteral("path")).toString());
        if (rel.isEmpty() || rel.startsWith(QStringLiteral("..")) || QDir::isAbsolutePath(rel))
            continue;

        const QString path = base.filePath(rel);
        const QString abs = QFileInfo(path).absoluteFilePath();
        if (!abs.startsWith(basePath, Qt::CaseInsensitive))
            continue;

        QByteArray bytes = QByteArray::fromBase64(o.value(QStringLiteral("base64")).toString().toLatin1());
        QDir().mkpath(QFileInfo(abs).absolutePath());

        if (QFileInfo(abs).exists()) {
            QJsonParseError newErr;
            const QJsonDocument incoming = QJsonDocument::fromJson(bytes, &newErr);
            QFile existing(abs);
            if (newErr.error == QJsonParseError::NoError && existing.open(QIODevice::ReadOnly)) {
                QJsonParseError oldErr;
                const QJsonDocument current = QJsonDocument::fromJson(existing.readAll(), &oldErr);
                existing.close();

                if (oldErr.error == QJsonParseError::NoError && incoming.isArray() && current.isArray()) {
                    QJsonArray merged = current.array();
                    QSet<QString> ids;
                    for (const QJsonValue &mv : std::as_const(merged)) {
                        const QString id = mv.toObject().value(QStringLiteral("id")).toString();
                        if (!id.isEmpty()) ids.insert(id);
                    }
                    for (const QJsonValue &mv : incoming.array()) {
                        const QJsonObject obj = mv.toObject();
                        const QString id = obj.value(QStringLiteral("id")).toString();
                        if (!id.isEmpty() && ids.contains(id)) continue;
                        merged.append(mv);
                        if (!id.isEmpty()) ids.insert(id);
                    }
                    bytes = QJsonDocument(merged).toJson(QJsonDocument::Indented);
                } else if (oldErr.error == QJsonParseError::NoError && incoming.isObject() && current.isObject()
                           && QFileInfo(rel).fileName() == QLatin1String("opencode.json")) {
                    QJsonObject merged = current.object();
                    const QJsonObject inc = incoming.object();
                    for (auto it = inc.begin(); it != inc.end(); ++it) {
                        if (it.key() == QLatin1String("mcp") && it.value().isObject()) {
                            QJsonObject mcp = merged.value(QStringLiteral("mcp")).toObject();
                            const QJsonObject incMcp = it.value().toObject();
                            for (auto mi = incMcp.begin(); mi != incMcp.end(); ++mi)
                                if (!mcp.contains(mi.key())) mcp.insert(mi.key(), mi.value());
                            merged[QStringLiteral("mcp")] = mcp;
                        } else if (!merged.contains(it.key())) {
                            merged.insert(it.key(), it.value());
                        }
                    }
                    bytes = QJsonDocument(merged).toJson(QJsonDocument::Indented);
                }
            }
        }

        QFile f(abs);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(bytes);
        f.close();
        if (written) written->append(abs);
    }
    return true;
}

QString AppController::exportUserData()
{
    const QString defName = QStringLiteral("llamacode_backup_%1.json")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("Exportar datos de LlamaCode"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(defName),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return QString();
    return exportUserDataTo(path);
}

QString AppController::exportUserDataTo(const QString &path)
{
    if (path.trimmed().isEmpty()) { emit serverError(QStringLiteral("Falta la ruta de exportación.")); return QString(); }
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString appLocal = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

    QVariantMap settings;
    QSettings s;
    for (const QString &key : s.allKeys())
        settings.insert(key, s.value(key));

    QJsonObject root;
    root[QStringLiteral("app")] = QStringLiteral("LlamaCode");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("exportedAt")] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    root[QStringLiteral("settings")] = QJsonObject::fromVariantMap(settings);
    root[QStringLiteral("appData")] = exportFileSet(appData, {
        QStringLiteral("binary_registry.json"),
        QStringLiteral("model_roots.json"),
        QStringLiteral("profiles")
    });
    root[QStringLiteral("appLocalData")] = exportFileSet(appLocal, {
        QStringLiteral("integrations.json"),
        QStringLiteral("chat"),
        QStringLiteral("chat_raw"),
        QStringLiteral("agent_llamaagent"),
        QStringLiteral("benchmarks")
    });
    root[QStringLiteral("opencode")] = exportFileSet(ocGlobalConfigDir(), {
        QStringLiteral("opencode.json"),
        QStringLiteral("command")
    });

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit serverError(QStringLiteral("No se pudo escribir el backup: %1").arg(path));
        return QString();
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.close();
    emit serverError(QStringLiteral("Backup exportado: %1").arg(QDir::toNativeSeparators(path)));
    return path;
}

QString AppController::importUserData()
{
    const QString path = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("Importar datos de LlamaCode"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return QString();
    return importUserDataFrom(path);
}

QString AppController::importUserDataFrom(const QString &path)
{
    if (path.trimmed().isEmpty()) { emit serverError(QStringLiteral("Falta la ruta del backup.")); return QString(); }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit serverError(QStringLiteral("No se pudo leer el backup."));
        return QString();
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        emit serverError(QStringLiteral("Backup JSON inválido: %1").arg(err.errorString()));
        return QString();
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("app")).toString() != QLatin1String("LlamaCode")) {
        emit serverError(QStringLiteral("El archivo no parece ser un backup de LlamaCode."));
        return QString();
    }

    QStringList written;
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString appLocal = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!importFileSet(appData, root.value(QStringLiteral("appData")).toObject(), &written) ||
        !importFileSet(appLocal, root.value(QStringLiteral("appLocalData")).toObject(), &written) ||
        !importFileSet(ocGlobalConfigDir(), root.value(QStringLiteral("opencode")).toObject(), &written)) {
        emit serverError(QStringLiteral("No se pudo importar todo el backup."));
        return QString();
    }

    QSettings s;
    const QJsonObject settings = root.value(QStringLiteral("settings")).toObject();
    for (auto it = settings.begin(); it != settings.end(); ++it)
        s.setValue(it.key(), it.value().toVariant());

    reloadPersistentStateAfterImportOrWipe();
    emit serverError(QStringLiteral("Backup importado: %1 archivo(s).").arg(written.size()));
    return path;
}

bool AppController::removePathForWipe(const QString &path)
{
    if (path.trimmed().isEmpty()) return true;
    QFileInfo fi(path);
    if (!fi.exists()) return true;
    if (fi.isDir()) {
        QDir d(fi.absoluteFilePath());
        return d.removeRecursively();
    }
    return QFile::remove(fi.absoluteFilePath());
}

QVariantList AppController::wipeCategories() const
{
    return {
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("chats")},
                    {QStringLiteral("title"), QStringLiteral("Wipe all chats")},
                    {QStringLiteral("description"), QStringLiteral("Every local chat and agent session stored by LlamaCode.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("memory")},
                    {QStringLiteral("title"), QStringLiteral("Wipe project memory")},
                    {QStringLiteral("description"), QStringLiteral("Deletes .llamacode/memory.md for the current agent project.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("skills")},
                    {QStringLiteral("title"), QStringLiteral("Wipe all skills")},
                    {QStringLiteral("description"), QStringLiteral("Deletes global opencode command skills from the LlamaCode-managed command folder.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("integrations")},
                    {QStringLiteral("title"), QStringLiteral("Wipe integrations")},
                    {QStringLiteral("description"), QStringLiteral("Deletes API service entries and global MCP server config.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("profiles")},
                    {QStringLiteral("title"), QStringLiteral("Wipe profiles")},
                    {QStringLiteral("description"), QStringLiteral("Deletes backend, model, runtime, harness, workspace, and launch profiles.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("benchmarks")},
                    {QStringLiteral("title"), QStringLiteral("Wipe benchmarks")},
                    {QStringLiteral("description"), QStringLiteral("Deletes saved benchmark results and run summaries.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("logs")},
                    {QStringLiteral("title"), QStringLiteral("Wipe logs")},
                    {QStringLiteral("description"), QStringLiteral("Deletes runtime server and agent logs.")}},
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("settings")},
                    {QStringLiteral("title"), QStringLiteral("Wipe settings")},
                    {QStringLiteral("description"), QStringLiteral("Clears QSettings preferences such as language, last launch, and agent tuning.")}}
    };
}

bool AppController::wipeUserData(const QString &kind, const QString &confirmation)
{
    if (confirmation.trimmed() != QLatin1String("WIPE")) {
        emit serverError(QStringLiteral("Escribí WIPE para confirmar el borrado."));
        return false;
    }

    const QString k = kind.trimmed().toLower();
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString appLocal = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    bool ok = true;

    if (k == QLatin1String("chats")) {
        ok = removePathForWipe(appLocal + QStringLiteral("/chat"))
          && removePathForWipe(appLocal + QStringLiteral("/chat_raw"))
          && removePathForWipe(appLocal + QStringLiteral("/agent_llamaagent"))
          && removePathForWipe(appData + QStringLiteral("/pi-sessions"));
        m_chatSessions.clear();
        m_chatMessages.clear();
        m_chatSessionId.clear();
        m_chatSessionTitle.clear();
        m_agentSessions.clear();
        m_agentMessages.clear();
        emit chatSessionsChanged();
        emit chatMessagesChanged();
        emit agentSessionsChanged();
        emit agentMessagesChanged();
    } else if (k == QLatin1String("memory")) {
        const QString dir = currentAgentProjectDir();
        ok = !dir.isEmpty() && removePathForWipe(LlamaAgentBackend::memoryFilePath(dir));
        if (dir.isEmpty())
            emit serverError(QStringLiteral("No hay proyecto activo para borrar memoria."));
    } else if (k == QLatin1String("skills")) {
        ok = removePathForWipe(ocGlobalConfigDir() + QStringLiteral("/command"));
    } else if (k == QLatin1String("integrations")) {
        ok = removePathForWipe(integrationsFilePath());
        QJsonObject cfg = ocReadConfigObj(QStringLiteral("global"), QString());
        cfg.remove(QStringLiteral("mcp"));
        ok = ok && ocWriteConfigObj(QStringLiteral("global"), QString(), cfg);
        emit integrationsChanged();
    } else if (k == QLatin1String("profiles")) {
        ok = removePathForWipe(appData + QStringLiteral("/profiles"));
        m_profiles.reloadFromDisk();
        emit effectiveProfileChanged();
    } else if (k == QLatin1String("benchmarks")) {
        ok = removePathForWipe(appLocal + QStringLiteral("/benchmarks"));
        m_benchmarkResults.clear();
        emit benchmarkResultsChanged();
    } else if (k == QLatin1String("logs")) {
        ok = removePathForWipe(runtimeLogDir());
        m_log.clear();
        m_agentLog.clear();
        emit serverLogChanged();
        emit agentLogChanged();
    } else if (k == QLatin1String("settings")) {
        QSettings s;
        s.clear();
        m_language = QStringLiteral("es");
        m_agentApprovalMode = QStringLiteral("ask");
        m_agentThinkingEnabled = false;
        m_agentSystemPrompt.clear();
        m_agentPermRules.clear();
        m_agentTemperature = -1.0;
        emit languageChanged();
        emit agentApprovalModeChanged();
        emit agentThinkingChanged();
        emit thinkingChanged();
        emit agentTuningChanged();
    } else {
        emit serverError(QStringLiteral("Categoría desconocida: %1").arg(kind));
        return false;
    }

    if (!ok) {
        emit serverError(QStringLiteral("No se pudo completar el borrado de %1.").arg(k));
        return false;
    }

    reloadPersistentStateAfterImportOrWipe();
    emit serverError(QStringLiteral("Borrado completado: %1.").arg(k));
    return true;
}

void AppController::reloadPersistentStateAfterImportOrWipe()
{
    m_binaries.refresh();
    m_roots.refresh();
    loadChatSessions();
    m_profiles.reloadFromDisk();
    loadBenchmarkResults();
    loadCustomBenchmarks();
    emit integrationsChanged();
    emit setupStateChanged();
}

// ── Chat sessions ─────────────────────────────────────────────────────────────

QString AppController::chatStorageDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/chat");
    QDir().mkpath(dir);
    return dir;
}

void AppController::loadChatSessions()
{
    QFile f(chatStorageDir() + QStringLiteral("/index.json"));
    QJsonArray arr;
    if (f.open(QIODevice::ReadOnly)) arr = QJsonDocument::fromJson(f.readAll()).array();
    m_chatSessions.clear();
    QVector<QVariantMap> sorted;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        QVariantMap s;
        s[QStringLiteral("id")]          = o.value(QStringLiteral("id")).toString();
        s[QStringLiteral("title")]       = o.value(QStringLiteral("title")).toString();
        s[QStringLiteral("projectId")]   = o.value(QStringLiteral("projectId")).toString();
        s[QStringLiteral("projectName")] = o.value(QStringLiteral("projectName")).toString();
        s[QStringLiteral("created")]     = o.value(QStringLiteral("created")).toDouble();
        s[QStringLiteral("updated")]     = o.value(QStringLiteral("updated")).toDouble();
        sorted.append(s);
    }
    // Draft (sesión activa sin persistir) ANTES de agrupar, así entra en el grupo
    // de su proyecto en vez de aparecer como sección duplicada al tope.
    injectDraftSession(sorted);
    // Group by project (contiguous sections), most-recently-used project first,
    // and within a project newest chat first.
    QHash<QString, double> projLatest;
    for (const auto &s : std::as_const(sorted)) {
        const QString p = s.value(QStringLiteral("projectName")).toString();
        const double u = s.value(QStringLiteral("updated")).toDouble();
        if (u > projLatest.value(p, 0.0)) projLatest[p] = u;
    }
    std::sort(sorted.begin(), sorted.end(), [&](const QVariantMap &a, const QVariantMap &b) {
        const QString pa = a.value(QStringLiteral("projectName")).toString();
        const QString pb = b.value(QStringLiteral("projectName")).toString();
        if (pa != pb) return projLatest.value(pa) > projLatest.value(pb);
        return a.value(QStringLiteral("updated")).toDouble() > b.value(QStringLiteral("updated")).toDouble();
    });
    for (const auto &s : sorted) m_chatSessions.append(s);
    emit chatSessionsChanged();
}

// Active session may not be persisted yet (no messages sent). Show it in the
// list anyway so the chat / new project is visible immediately. Se agrega al
// vector ANTES del agrupado para que caiga en el grupo de su proyecto.
void AppController::injectDraftSession(QVector<QVariantMap> &sessions)
{
    if (m_chatSessionId.isEmpty()) return;
    for (const auto &s : std::as_const(sessions))
        if (s.value(QStringLiteral("id")).toString() == m_chatSessionId)
            return; // already persisted

    const QString projId = m_chatProjectIdOverride.isEmpty()
        ? m_activeLaunchId : m_chatProjectIdOverride;
    const QString projName = [&]() -> QString {
        if (!m_chatProjectNameOverride.isEmpty()) return m_chatProjectNameOverride;
        const auto profiles = m_profiles.launchProfiles();
        for (int i = 0; i < profiles->rowCount(); ++i) {
            const auto idx2 = profiles->index(i, 0);
            if (profiles->data(idx2, 257).toString() == projId)
                return profiles->data(idx2, Qt::DisplayRole).toString();
        }
        return projId.isEmpty() ? QStringLiteral("Sin proyecto") : projId;
    }();

    const double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    QVariantMap s;
    s[QStringLiteral("id")]          = m_chatSessionId;
    s[QStringLiteral("title")]       = m_chatSessionTitle;
    s[QStringLiteral("projectId")]   = projId;
    s[QStringLiteral("projectName")] = projName;
    s[QStringLiteral("created")]     = now;
    s[QStringLiteral("updated")]     = now;
    sessions.append(s);
}

void AppController::saveChatSession()
{
    if (m_chatSessionId.isEmpty()) return;

    // Update index
    QFile idxFile(chatStorageDir() + QStringLiteral("/index.json"));
    QJsonArray idx;
    if (idxFile.open(QIODevice::ReadOnly)) { idx = QJsonDocument::fromJson(idxFile.readAll()).array(); idxFile.close(); }
    bool found = false;
    const double now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    for (int i = 0; i < idx.size(); ++i) {
        QJsonObject o = idx[i].toObject();
        if (o.value(QStringLiteral("id")).toString() == m_chatSessionId) {
            o[QStringLiteral("title")]   = m_chatSessionTitle;
            o[QStringLiteral("updated")] = now;
            idx[i] = o; found = true; break;
        }
    }
    if (!found) {
        const QString projId = m_chatProjectIdOverride.isEmpty()
            ? m_activeLaunchId : m_chatProjectIdOverride;
        const QString projName = [&]() -> QString {
            if (!m_chatProjectNameOverride.isEmpty()) return m_chatProjectNameOverride;
            const auto profiles = m_profiles.launchProfiles();
            for (int i = 0; i < profiles->rowCount(); ++i) {
                const auto idx2 = profiles->index(i, 0);
                if (profiles->data(idx2, 257).toString() == projId)
                    return profiles->data(idx2, Qt::DisplayRole).toString();
            }
            return projId.isEmpty() ? QStringLiteral("Sin proyecto") : projId;
        }();
        m_chatProjectIdOverride.clear();
        m_chatProjectNameOverride.clear();
        idx.append(QJsonObject{
            {QStringLiteral("id"),          m_chatSessionId},
            {QStringLiteral("title"),       m_chatSessionTitle},
            {QStringLiteral("projectId"),   projId},
            {QStringLiteral("projectName"), projName},
            {QStringLiteral("created"),     now},
            {QStringLiteral("updated"),     now}
        });
    }
    if (idxFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        idxFile.write(QJsonDocument(idx).toJson());
        idxFile.close();
    }

    // Save session file with messages
    QJsonArray msgsJson;
    for (const QVariant &mv : std::as_const(m_chatMessages)) {
        const QVariantMap m = mv.toMap();
        msgsJson.append(QJsonObject{
            {QStringLiteral("role"),    m.value(QStringLiteral("role")).toString()},
            {QStringLiteral("content"), m.value(QStringLiteral("content")).toString()}
        });
    }
    QFile sf(chatStorageDir() + QStringLiteral("/") + m_chatSessionId + QStringLiteral(".json"));
    if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        sf.write(QJsonDocument(QJsonObject{
            {QStringLiteral("id"),       m_chatSessionId},
            {QStringLiteral("title"),    m_chatSessionTitle},
            {QStringLiteral("messages"), msgsJson}
        }).toJson());

    loadChatSessions();
}

void AppController::loadChatSessionMessages(const QString &id)
{
    QFile f(chatStorageDir() + QStringLiteral("/") + id + QStringLiteral(".json"));
    if (!f.open(QIODevice::ReadOnly)) { emit chatMessagesChanged(); return; }
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    m_chatMessages.clear();
    for (const QJsonValue &v : obj.value(QStringLiteral("messages")).toArray()) {
        const QJsonObject m = v.toObject();
        QVariantMap entry;
        entry[QStringLiteral("role")]    = m.value(QStringLiteral("role")).toString();
        entry[QStringLiteral("content")] = m.value(QStringLiteral("content")).toString();
        entry[QStringLiteral("typing")]  = false;
        m_chatMessages.append(entry);
    }
    emit chatMessagesChanged();
}

QString AppController::exportChatSession(const QString &id, const QString &format)
{
    const QString sid = id.isEmpty() ? m_chatSessionId : id;
    if (sid.isEmpty()) return QString();

    QFile f(chatStorageDir() + QStringLiteral("/") + sid + QStringLiteral(".json"));
    if (!f.open(QIODevice::ReadOnly)) { emit serverError(QStringLiteral("La sesión no tiene archivo guardado.")); return QString(); }
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    const QString title = obj.value(QStringLiteral("title")).toString(QStringLiteral("chat"));
    const bool asJson = (format.compare(QLatin1String("json"), Qt::CaseInsensitive) == 0);

    // Nombre de archivo sugerido: título saneado.
    QString safe = title;
    safe.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    if (safe.trimmed().isEmpty()) safe = QStringLiteral("chat");
    const QString ext = asJson ? QStringLiteral("json") : QStringLiteral("md");
    const QString defName = safe + QStringLiteral(".") + ext;

    const QString path = QFileDialog::getSaveFileName(
        nullptr, QStringLiteral("Exportar conversación"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(defName),
        asJson ? QStringLiteral("JSON (*.json)") : QStringLiteral("Markdown (*.md)"));
    if (path.isEmpty()) return QString();
    return exportChatSessionTo(sid, format, path);
}

QString AppController::exportChatSessionTo(const QString &id, const QString &format,
                                           const QString &path)
{
    const QString sid = id.isEmpty() ? m_chatSessionId : id;
    if (sid.isEmpty()) { emit serverError(QStringLiteral("No hay sesión que exportar.")); return QString(); }
    if (path.trimmed().isEmpty()) { emit serverError(QStringLiteral("Falta la ruta de exportación.")); return QString(); }

    QFile f(chatStorageDir() + QStringLiteral("/") + sid + QStringLiteral(".json"));
    if (!f.open(QIODevice::ReadOnly)) { emit serverError(QStringLiteral("La sesión no tiene archivo guardado.")); return QString(); }
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    const QString title = obj.value(QStringLiteral("title")).toString(QStringLiteral("chat"));
    const bool asJson = (format.compare(QLatin1String("json"), Qt::CaseInsensitive) == 0);

    QByteArray out;
    if (asJson) {
        out = QJsonDocument(obj).toJson();
    } else {
        QString md = QStringLiteral("# %1\n\n").arg(title);
        for (const QJsonValue &v : obj.value(QStringLiteral("messages")).toArray()) {
            const QJsonObject m = v.toObject();
            const QString role = m.value(QStringLiteral("role")).toString();
            const QString who = role == QLatin1String("user") ? QStringLiteral("🧑 Usuario")
                              : role == QLatin1String("assistant") ? QStringLiteral("🤖 Asistente")
                              : role;
            md += QStringLiteral("## %1\n\n%2\n\n").arg(who, m.value(QStringLiteral("content")).toString());
        }
        out = md.toUtf8();
    }

    QFile of(path);
    if (!of.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit serverError(QStringLiteral("No se pudo escribir el archivo de exportación."));
        return QString();
    }
    of.write(out);
    of.close();
    return path;
}

QVariantList AppController::searchChatHistory(const QString &query) const
{
    QVariantList results;
    const QString q = query.trimmed();
    if (q.isEmpty()) return results;

    for (const QVariant &sv : m_chatSessions) {
        const QVariantMap s = sv.toMap();
        const QString id = s.value(QStringLiteral("id")).toString();
        const QString title = s.value(QStringLiteral("title")).toString();
        bool match = title.contains(q, Qt::CaseInsensitive);
        QString snippet;

        QFile f(chatStorageDir() + QStringLiteral("/") + id + QStringLiteral(".json"));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
            for (const QJsonValue &v : obj.value(QStringLiteral("messages")).toArray()) {
                const QString content = v.toObject().value(QStringLiteral("content")).toString();
                const int idx = content.indexOf(q, 0, Qt::CaseInsensitive);
                if (idx >= 0) {
                    match = true;
                    const int from = qMax(0, idx - 40);
                    snippet = content.mid(from, 120).simplified();
                    break;
                }
            }
        }
        if (match) {
            QVariantMap r;
            r[QStringLiteral("id")]          = id;
            r[QStringLiteral("title")]       = title;
            r[QStringLiteral("projectName")] = s.value(QStringLiteral("projectName"));
            r[QStringLiteral("snippet")]     = snippet;
            results.append(r);
        }
    }
    return results;
}

void AppController::newChatSession()
{
    if (IAgentBackend *b = ensureChatBackend())
        b->newSession();
}

void AppController::newChatSessionInProject(const QString &projectId, const QString &projectName)
{
    if (IAgentBackend *b = ensureChatBackend())
        b->newSessionInProject(projectId + QStringLiteral("|") + projectName + QStringLiteral("|"));
}

void AppController::switchChatSession(const QString &id)
{
    if (IAgentBackend *b = ensureChatBackend())
        b->switchSession(id);
}

void AppController::deleteChatSession(const QString &id)
{
    if (IAgentBackend *b = ensureChatBackend())
        b->deleteSession(id);
}

void AppController::deleteChatProject(const QString &projectName)
{
    IAgentBackend *b = ensureChatBackend();
    if (!b) return;
    const QVariantList sessions = b->sessions();
    for (const QVariant &v : sessions) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("projectName")).toString() == projectName)
            b->deleteSession(s.value(QStringLiteral("id")).toString());
    }
}

QVariantList AppController::chatProjects() const
{
    if (m_chatBackend) {
        QVariantList out;
        QSet<QString> seen;
        for (const QVariant &v : m_chatBackend->sessions()) {
            const QVariantMap s = v.toMap();
            const QString name = s.value(QStringLiteral("projectName")).toString();
            if (name.isEmpty() || seen.contains(name)) continue;
            seen.insert(name);
            out.append(QVariantMap{
                {QStringLiteral("projectId"),   s.value(QStringLiteral("projectId")).toString()},
                {QStringLiteral("projectName"), name}
            });
        }
        return out;
    }
    QVariantList out;
    QSet<QString> seen;
    for (const QVariant &v : std::as_const(m_chatSessions)) {
        const QVariantMap s = v.toMap();
        const QString name = s.value(QStringLiteral("projectName")).toString();
        if (name.isEmpty() || seen.contains(name)) continue;
        seen.insert(name);
        out.append(QVariantMap{
            {QStringLiteral("projectId"),   s.value(QStringLiteral("projectId")).toString()},
            {QStringLiteral("projectName"), name}
        });
    }
    return out;
}

void AppController::moveChatToProject(const QString &id, const QString &projectId, const QString &projectName)
{
    IAgentBackend *b = ensureChatBackend();
    if (!b || id.isEmpty()) return;
    if (auto *raw = qobject_cast<RawChatBackend *>(b))
        raw->updateSessionProject(id, projectId, projectName);
}

void AppController::renameChatSession(const QString &id, const QString &title)
{
    if (IAgentBackend *b = ensureChatBackend())
        b->renameSession(id, title);
}

void AppController::renameChatProject(const QString &oldName, const QString &newName)
{
    IAgentBackend *b = ensureChatBackend();
    if (!b) return;
    const QString nn = newName.trimmed();
    if (oldName.isEmpty() || nn.isEmpty() || nn == oldName) return;
    if (auto *raw = qobject_cast<RawChatBackend *>(b))
        raw->renameProject(oldName, nn);
}

void AppController::sendChatMessage(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    IAgentBackend *b = ensureChatBackend();
    if (!b) return;
    AgentContext c;
    c.adapter = QStringLiteral("raw");
    c.serverBaseUrl = serverBaseUrl();
    c.modelId = routedModelId(QStringLiteral("chat"));
    b->stop();
    b->start(c);
    b->sendMessage(text);
}

void AppController::sendChatMessageWithAttachments(const QString &text, const QStringList &paths)
{
    if (text.trimmed().isEmpty() && paths.isEmpty()) return;
    IAgentBackend *b = ensureChatBackend();
    if (!b) return;
    AgentContext c;
    c.adapter = QStringLiteral("raw");
    c.serverBaseUrl = serverBaseUrl();
    c.modelId = routedModelId(QStringLiteral("chat"));
    b->stop();
    b->start(c);
    if (auto *raw = qobject_cast<RawChatBackend *>(b))
        raw->setPendingAttachments(paths);
    b->sendMessage(text);
}

void AppController::steerChat(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (IAgentBackend *b = ensureChatBackend()) b->steerMessage(text);
}

void AppController::queueChat(const QString &text)
{
    if (text.trimmed().isEmpty()) return;
    if (IAgentBackend *b = ensureChatBackend()) b->queueMessage(text);
}

void AppController::clearChatQueue()
{
    if (m_chatBackend) m_chatBackend->clearQueue();
}

QString AppController::pasteClipboardImage()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + QStringLiteral("/llamacode-paste");
    QDir().mkpath(dir);

    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (mime) {
        const QList<QUrl> urls = mime->urls();
        for (const QUrl &url : urls) {
            if (!url.isLocalFile()) continue;
            const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
            if (QImageReader(path).canRead())
                return path;
        }
        if (mime->hasImage()) {
            const QImage mimeImg = qvariant_cast<QImage>(mime->imageData());
            if (!mimeImg.isNull()) {
                const QString path = dir + QStringLiteral("/paste-")
                                     + QString::number(QDateTime::currentMSecsSinceEpoch())
                                     + QStringLiteral(".png");
                if (mimeImg.save(path, "PNG")) return path;
            }
        }
    }

    const QImage img = QGuiApplication::clipboard()->image();
    if (img.isNull()) return {};
    const QString path = dir + QStringLiteral("/paste-")
                         + QString::number(QDateTime::currentMSecsSinceEpoch())
                         + QStringLiteral(".png");
    if (!img.save(path, "PNG")) return {};
    return path;
}

QStringList AppController::pickChatAttachments()
{
    QWidget *parent = QApplication::activeWindow();
    return QFileDialog::getOpenFileNames(
        parent, QStringLiteral("Adjuntar archivos"), QDir::homePath(),
        attachmentFilter(m_serverHasVision));
}

void AppController::stopChatGeneration()
{
    if (IAgentBackend *b = ensureChatBackend())
        b->cancelGeneration();
}

void AppController::setChatThinkingEnabled(bool enabled)
{
    if (enabled == m_chatThinkingEnabled) return;
    m_chatThinkingEnabled = enabled;
    m_launchThinkingEnabled = enabled;
    writeSetting(QStringLiteral("chat/thinkingEnabledV2"), enabled);
    writeSetting(QStringLiteral("thinking/serverEnabled"), enabled);
    if (auto *raw = qobject_cast<RawChatBackend *>(m_chatBackend))
        raw->setThinkingEnabled(enabled);
    emit chatThinkingChanged();
    if (serverRunning()) {
        appendServerEvent(QStringLiteral("lifecycle"),
                          enabled
                              ? QStringLiteral("Pensar en chat activado: el cambio duro se aplica al reiniciar el modelo.")
                              : QStringLiteral("Pensar en chat desactivado: el cambio duro se aplica al reiniciar el modelo."));
    }
}

// ── Managed-process lifecycle ─────────────────────────────────────────────────

QString AppController::serviceStatePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/services.json");
}

void AppController::writeServiceState(const QString &role, qint64 pid, const QVariantMap &extra)
{
    const QString path = serviceStatePath();
    QJsonObject root;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    root[QStringLiteral("app_pid")] = static_cast<qint64>(QCoreApplication::applicationPid());

    QJsonObject entry;
    entry[QStringLiteral("pid")]     = pid;
    entry[QStringLiteral("started")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    for (auto it = extra.cbegin(); it != extra.cend(); ++it)
        entry[it.key()] = QJsonValue::fromVariant(it.value());
    root[role] = entry;

    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(QJsonDocument(root).toJson());
}

void AppController::clearServiceState(const QString &role)
{
    const QString path = serviceStatePath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    root.remove(role);
    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(QJsonDocument(root).toJson());
}

void AppController::killManagedOrphans()
{
    const QString path = serviceStatePath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    // If the app that spawned these is still alive, don't kill them
    const qint64 ownerPid = root.value(QStringLiteral("app_pid")).toInteger();
    if (ownerPid > 0 && ownerPid != QCoreApplication::applicationPid()) {
#ifdef Q_OS_WIN
        HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(ownerPid));
        const bool ownerAlive = h && (WaitForSingleObject(h, 0) == WAIT_TIMEOUT);
        if (h) CloseHandle(h);
#else
        const bool ownerAlive = (::kill(static_cast<pid_t>(ownerPid), 0) == 0);
#endif
        if (ownerAlive) return;  // Previous owner still running — leave its processes alone
    }

    // Owner is dead — kill orphans
    const QStringList roles = root.keys();
    for (const QString &role : roles) {
        if (role == QLatin1String("app_pid")) continue;
        const qint64 pid = root.value(role).toObject().value(QStringLiteral("pid")).toInteger();
        if (pid <= 0) continue;
#ifdef Q_OS_WIN
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        if (h) {
            if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT)  // still running
                TerminateProcess(h, 1);
            CloseHandle(h);
        }
#else
        ::kill(static_cast<pid_t>(pid), SIGTERM);
#endif
    }

    // Clear state — will be repopulated as new processes start
    QFile::remove(path);
}

void AppController::createJobObject()
{
#ifdef Q_OS_WIN
    m_jobObject = CreateJobObjectW(nullptr, nullptr);
    if (m_jobObject) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(static_cast<HANDLE>(m_jobObject),
                                JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
#endif
}

void AppController::assignToJobObject(qint64 pid)
{
#ifdef Q_OS_WIN
    if (!m_jobObject) return;
    HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, static_cast<DWORD>(pid));
    if (h) {
        AssignProcessToJobObject(static_cast<HANDLE>(m_jobObject), h);
        CloseHandle(h);
    }
#else
    Q_UNUSED(pid)
#endif
}

struct TrEntry { const char *key, *es, *en, *zh, *fr, *it, *de; };

static const TrEntry k_tr[] = {
    // Nav
    {"nav.launch",   "Lanzar",        "Launch",       "启动",       "Lancer",        "Avvia",          "Starten"},
    {"nav.profiles", "Perfiles",       "Profiles",     "配置",       "Profils",        "Profili",        "Profile"},
    {"nav.models",   "Modelos",        "Models",       "模型",       "Modèles",   "Modelli",        "Modelle"},
    {"nav.binaries", "Binarios",       "Binaries",     "二进制", "Binaires",       "Binari",         "Binärdateien"},
    {"nav.chat",      "Chat",           "Chat",         "聊天",       "Discussion",     "Chat",           "Chat"},
    {"nav.benchmark", "Benchmark",     "Benchmark",    "基准测试",   "Benchmark",      "Benchmark",      "Benchmark"},
    {"nav.research",  "Research",       "Research",     "研究",       "Recherche",      "Ricerca",        "Recherche"},
    {"nav.tasks",     "Tasks",          "Tasks",        "任务",       "Tâches",         "Attività",       "Aufgaben"},
    {"nav.charla",    "Charla",         "Talk",         "对话",       "Parler",         "Parla",          "Sprechen"},
    {"nav.studia",    "StudIA",         "StudIA",       "StudIA",     "StudIA",         "StudIA",         "StudIA"},
    {"nav.settings",  "Configuración", "Settings",     "设置",       "Paramètres",     "Impostazioni",   "Einstellungen"},
    // Launch page
    {"launch.title",       "Lanzar",          "Launch",          "启动",       "Lancer",          "Avvia",               "Starten"},
    {"launch.running",     "Servidor activo", "Server running",  "服务器运行中", "Serveur actif",   "Server in esecuzione","Server läuft"},
    {"launch.stopped",     "Servidor detenido","Server stopped", "服务器已停止", "Serveur arrêté","Server fermo",   "Server gestoppt"},
    {"launch.profile",     "Perfil de lanzamiento","Launch Profile","启动配置","Profil de lancement","Profilo di avvio","Startprofil"},
    {"launch.select",      "— seleccionar —", "— select —",      "— 选择 —",   "— sélectionner —","— seleziona —", "— auswählen —"},
    {"launch.cmdPreview",  "Vista previa del comando","Command Preview","命令预览","Aperçu de la commande","Anteprima comando","Befehlsvorschau"},
    {"launch.startServer", "Iniciar servidor","Start Server",    "启动服务器","Démarrer le serveur","Avvia server","Server starten"},
    {"launch.startServerAndAgent", "Iniciar servidor + agente","Start Server + Agent","启动服务器+智能体","Démarrer le serveur + agent","Avvia server + agente","Server + Agent starten"},
    {"launch.startServerOnly", "Iniciar solo servidor","Start Server Only","仅启动服务器","Démarrer le serveur seul","Avvia solo server","Nur Server starten"},
    {"launch.endpointLabel", "Endpoint OpenAI (para otros agentes)","OpenAI endpoint (for other agents)","OpenAI 端点（用于其他智能体）","Point de terminaison OpenAI (pour d'autres agents)","Endpoint OpenAI (per altri agenti)","OpenAI-Endpunkt (für andere Agenten)"},
    {"launch.stopServer",  "Detener servidor","Stop Server",     "停止服务器","Arrêter le serveur","Ferma server","Server stoppen"},
    {"launch.preview",     "Vista previa",   "Preview",          "预览",        "Aperçu",     "Anteprima",          "Vorschau"},
    {"launch.showLog",     "Ver log",        "Show log",         "显示日志","Voir le journal","Mostra log",       "Log anzeigen"},
    {"launch.hideLog",     "Ocultar log",    "Hide log",         "隐藏日志","Masquer le journal","Nascondi log", "Log ausblenden"},
    {"launch.clear",       "Limpiar",        "Clear",            "清除",        "Effacer",         "Cancella",           "Leeren"},
    {"launch.copyLogs",    "Copiar logs",    "Copy logs",        "复制日志","Copier les journaux","Copia log",  "Logs kopieren"},
    {"launch.serverLog",   "Log del servidor","Server Log",      "服务器日志","Journal du serveur","Log del server","Server-Log"},
    {"launch.binary",      "Binario",        "Binary",           "二进制文件","Binaire","Binario",          "Binärdatei"},
    {"launch.valid",       "Válido",    "Valid",            "有效",        "Valide",          "Valido",             "Gültig"},
    {"launch.yes",         "Sí",        "Yes",              "是",              "Oui",             "Sì",            "Ja"},
    {"launch.no",          "No",             "No",               "否",              "Non",             "No",                 "Nein"},
    // Profiles page
    {"profiles.title",         "Perfiles",         "Profiles",           "配置文件","Profils",           "Profili",              "Profile"},
    {"profiles.subtitle",      "Editor completo de perfil","Full profile editor","完整配置编辑器","Éditeur de profil complet","Editor completo profilo","Vollständiger Profil-Editor"},
    {"profiles.smokeTesting",  "Testeando...",     "Testing...",         "测试中...", "Test en cours...",   "Test in corso...",     "Teste..."},
    {"profiles.smokeTest",     "Smoke-Test",       "Smoke Test",         "冒烟测试","Test rapide",       "Smoke Test",           "Smoke-Test"},
    {"profiles.new",           "Nuevo",            "New",                "新建",       "Nouveau",            "Nuovo",                "Neu"},
    {"profiles.duplicate",     "Duplicar",         "Duplicate",          "复制",       "Dupliquer",          "Duplica",              "Duplizieren"},
    {"profiles.rename",        "Renombrar",        "Rename",             "重命名",  "Renommer",           "Rinomina",             "Umbenennen"},
    {"profiles.cancel",        "Cancelar",         "Cancel",             "取消",       "Annuler",            "Annulla",              "Abbrechen"},
    {"profiles.save",          "Guardar",          "Save",               "保存",       "Enregistrer",        "Salva",                "Speichern"},
    {"profiles.delete",        "Eliminar",         "Delete",             "删除",       "Supprimer",          "Elimina",              "Löschen"},
    {"profiles.deleteTitle",   "Eliminar perfil",  "Delete profile",     "删除配置","Supprimer le profil","Elimina profilo",  "Profil löschen"},
    {"profiles.renameTitle",   "Renombrar perfil", "Rename profile",     "重命名配置","Renommer le profil","Rinomina profilo","Profil umbenennen"},
    {"profiles.smokeTestPassed","✓ Smoke-Test pasó","✓ Smoke Test passed","✓ 测试通过","✓ Test réussi","✓ Test superato","✓ Test bestanden"},
    {"profiles.smokeTestFailed","✗ Smoke-Test falló","✗ Smoke Test failed","✗ 测试失败","✗ Test échoué","✗ Test fallito","✗ Test fehlgeschlagen"},
    {"profiles.extraArgs",     "Args manuales adicionales","Manual extra args","手动额外参数","Arguments supplémentaires","Args aggiuntivi manuali","Manuelle Zusatzargumente"},
    {"profiles.envOverrides",  "envOverrides (JSON)","envOverrides (JSON)","环境变量覆盖","Remplacements env (JSON)","envOverrides (JSON)","envOverrides (JSON)"},
    // Binaries page
    {"binaries.title",        "Binarios",         "Binaries",            "二进制文件","Binaires",         "Binari",               "Binärdateien"},
    {"binaries.addBinary",    "Agregar binario",  "Add Binary",          "添加二进制","Ajouter un binaire","Aggiungi binario",     "Binärdatei hinzufügen"},
    {"binaries.addAction",    "+ Agregar binario","+ Add Binary",        "+ 添加二进制","+ Ajouter binaire","+ Aggiungi binario",  "+ Binärdatei hinzufügen"},
    {"binaries.path",         "Ruta",             "Path",                "路径",        "Chemin",            "Percorso",             "Pfad"},
    {"binaries.name",         "Nombre",           "Name",                "名称",        "Nom",               "Nome",                 "Name"},
    {"binaries.flavor",       "Variante",         "Flavor",              "版本",        "Variante",          "Variante",             "Variante"},
    {"binaries.backend",      "Backend",          "Backend",             "后端",        "Backend",           "Backend",              "Backend"},
    {"binaries.status",       "Estado",           "Status",              "状态",        "Statut",            "Stato",                "Status"},
    {"binaries.flags",        "Flags",            "Flags",               "标志",        "Indicateurs",       "Flag",                 "Flags"},
    {"binaries.found",        "✓ Encontrado","✓ Found",        "✓ 已找到","✓ Trouvé","✓ Trovato",   "✓ Gefunden"},
    {"binaries.notFound",     "✗ No encontrado","✗ Not found", "✗ 未找到","✗ Introuvable","✗ Non trovato","✗ Nicht gefunden"},
    {"binaries.detected",     "detectados",       "detected",            "已检测",  "détectés","rilevati",            "erkannt"},
    {"binaries.notDetected",  "No detectado",     "Not detected",        "未检测",  "Non détecté","Non rilevato",     "Nicht erkannt"},
    {"binaries.browse",       "Explorar",         "Browse",              "浏览",        "Parcourir",         "Sfoglia",              "Durchsuchen"},
    {"binaries.rename",       "Renombrar",        "Rename",              "重命名",   "Renommer",          "Rinomina",             "Umbenennen"},
    {"binaries.detectCaps",   "Detectar capacidades","Detect Capabilities","检测功能","Détecter les capacités","Rileva capacità","Fähigkeiten erkennen"},
    {"binaries.remove",       "Eliminar",         "Remove",              "移除",        "Supprimer",         "Rimuovi",              "Entfernen"},
    {"binaries.selectBinary",   "Seleccionar un binario","Select a binary","选择二进制","Sélectionner un binaire","Seleziona un binario","Binärdatei auswählen"},
    {"binaries.registered",     "registrados",      "registered",          "已注册",  "enregistrés",  "registrati",           "registriert"},
    {"binaries.downloadLatest", "↓ Descargar latest","↓ Download Latest",  "↓ 下载最新","↓ Télécharger latest","↓ Scarica latest","↓ Neueste herunterladen"},
    {"binaries.downloading",    "Descargando...",   "Downloading...",      "下载中...", "Téléchargement...","Download in corso...","Herunterladen..."},
    // Models page
    {"models.title",       "Modelos",           "Model Roots",           "模型目录","Racines des modèles","Radici modelli",  "Modell-Verzeichnisse"},
    {"models.addRoot",     "Agregar directorio","Add Model Root",        "添加目录","Ajouter une racine","Aggiungi radice",       "Verzeichnis hinzufügen"},
    {"models.addAction",   "+ Agregar directorio","+ Add Root",          "+ 添加目录","+ Ajouter racine","+ Aggiungi radice",    "+ Verzeichnis hinzufügen"},
    {"models.scanning",    "Escaneando...",     "Scanning…",        "扫描中...", "Analyse en cours…","Scansione in corso…","Scannen…"},
    {"models.scan",        "Escanear",         "Scan",                  "扫描",          "Analyser",          "Scansiona",            "Scannen"},
    {"models.scanAll",     "Escanear todo",    "Scan All",              "扫描全部","Tout analyser",   "Scansiona tutto",      "Alles scannen"},
    {"models.removeRoot",  "Eliminar directorio","Remove Root",         "删除目录","Supprimer la racine","Rimuovi radice",     "Verzeichnis entfernen"},
    {"models.filterFamily","Filtrar por familia...","Filter by family…","按族过滤...","Filtrer par famille…","Filtra per famiglia…","Nach Familie filtern…"},
    {"models.visionOnly",  "Solo visión", "Vision only",           "仅视觉",   "Vision uniquement", "Solo visione",         "Nur Vision"},
    {"models.selectRoot",  "Seleccionar un directorio","Select a root to view models","选择目录查看模型","Sélectionner une racine","Seleziona una radice","Verzeichnis auswählen"},
    {"models.noModels",    "Sin modelos. Hacer Scan.","No models found. Click Scan.","未找到模型，点击扫描","Aucun modèle. Cliquer Scan.","Nessun modello. Clicca Scan.","Keine Modelle. Scan klicken."},
    {"models.path",        "Ruta",             "Path",                  "路径",          "Chemin",            "Percorso",             "Pfad"},
    {"models.label",       "Etiqueta",         "Label",                 "标签",          "Étiquette",    "Etichetta",            "Bezeichnung"},
    {"models.scanMode",    "Modo de escaneo",  "Scan mode",             "扫描模式","Mode d'analyse",  "Modalità scansione","Scan-Modus"},
    {"models.roots",       "roots",            "roots",                 "目录",          "racines",           "radici",               "Verzeichnisse"},
    {"models.modelsCount", "modelos",          "models",                "模型",          "modèles",      "modelli",              "Modelle"},
    // Chat
    {"chat.serverStopped", "servidor detenido","server stopped",        "服务器已停止","serveur arrêté","server fermo","Server gestoppt"},
    {"chat.startMessage",  "Escribe un mensaje para empezar","Write a message to start","写一条消息开始","Escribez un message pour commencer","Scrivi un messaggio per iniziare","Schreib eine Nachricht zum Starten"},
    {"chat.startServer",   "Iniciá el servidor para chatear","Start the server to chat","启动服务器开始聊天","Démarrez le serveur pour discuter","Avvia il server per chattare","Starte den Server zum Chatten"},
    {"chat.clear",         "Limpiar",          "Clear",                 "清除",          "Effacer",           "Cancella",             "Leeren"},
    {"chat.generating",    "Generando...",     "Generating...",         "生成中...", "Génération...","È in corso...", "Generiere..."},
    {"chat.placeholder",   "Escribe un mensaje...","Write a message...","写一条消息...","Saisissez un message...","Scrivi un messaggio...","Nachricht schreiben..."},
    {"chat.stop",          "■  Parar",    "■  Stop",          "■  停止", "■  Arrêter","■  Ferma",        "■  Stopp"},
    {"chat.send",          "Enviar",           "Send",                  "发送",          "Envoyer",           "Invia",                "Senden"},
    // CommandPreview
    {"cmd.copy",           "Copiar",           "Copy",                  "复制",          "Copier",            "Copia",                "Kopieren"},
    {"cmd.copied",         "¡Copiado!",   "Copied!",               "已复制!",   "Copié !",      "Copiato!",             "Kopiert!"},
    {"cmd.noProfile",      "(sin perfil seleccionado)","(no profile selected)","(未选择配置)","(aucun profil sélectionné)","(nessun profilo selezionato)","(kein Profil ausgewählt)"},
    // Common
    {"common.ok",          "Aceptar",          "OK",                    "确定",          "OK",                "OK",                   "OK"},
    {"common.cancel",      "Cancelar",         "Cancel",                "取消",          "Annuler",           "Annulla",              "Abbrechen"},
    {"common.close",       "Cerrar",           "Close",                 "关闭",          "Fermer",            "Chiudi",               "Schließen"},
    {"common.browse",      "Explorar",         "Browse",                "浏览",          "Parcourir",         "Sfoglia",              "Durchsuchen"},
    {"common.deleteConfirm","¿Eliminar \"%1\"? Esta acción no se puede deshacer.","Delete \"%1\"? This action cannot be undone.","删除 \"%1\"？此操作无法撤销。","Supprimer \"%1\" ? Cette action est irréversible.","Eliminare \"%1\"? Questa azione non può essere annullata.","\"%1\" löschen? Diese Aktion kann nicht rückgängig gemacht werden."},
    {"common.selectPlaceholder","— seleccionar —","— select —",        "— 选择 —",      "— sélectionner —","— seleziona —",       "— auswählen —"},
    // Setup popup
    {"setup.title",        "Configuración inicial","Initial Setup","初始设置","Configuration initiale","Configurazione iniziale","Ersteinrichtung"},
    {"setup.description",  "No hay binarios ni modelos registrados. Necesitás instalar/localizar un llama-server y descargar al menos un modelo GGUF.",
                           "No binaries or models registered. You need to install/locate a llama-server and download at least one GGUF model.",
                           "没有注册的二进制文件或模型。需要安装/定位llama-server并下载至少一个GGUF模型。",
                           "Aucun binaire ni modèle enregistré. Vous devez installer/localiser un llama-server et télécharger au moins un modèle GGUF.",
                           "Nessun binario o modello registrato. È necessario installare/localizzare un llama-server e scaricare almeno un modello GGUF.",
                           "Keine Binärdateien oder Modelle registriert. Sie müssen einen llama-server installieren/finden und mindestens ein GGUF-Modell herunterladen."},
    {"setup.locateBinary", "Localizar binario","Locate binary",         "定位二进制","Localiser le binaire","Individua binario","Binärdatei suchen"},
    {"setup.installing",   "Instalando...",   "Installing...",          "安装中...", "Installation en cours...","Installazione in corso...","Installiere..."},
    {"setup.installBinary","Instalar binario","Install binary",         "安装二进制","Installer le binaire","Installa binario","Binärdatei installieren"},
    {"setup.cancel",       "Cancelar",        "Cancel",                 "取消",          "Annuler",           "Annulla",              "Abbrechen"},
    {"setup.locateModel", "Localizar carpeta","Locate folder",          "定位文件夹","Localiser le dossier","Individua cartella","Ordner suchen"},
    {"setup.downloadModel","Descargar modelo (GGUF)","Download model (GGUF)","下载模型(GGUF)","Télécharger modèle (GGUF)","Scarica modello (GGUF)","Modell herunterladen (GGUF)"},
    {"setup.goToModels",   "Ir a Modelos",    "Go to Model Roots",      "前往模型目录","Aller aux Racines modèles","Vai a Radici modelli","Zu Modell-Verzeichnissen"},
    {"setup.tip",          "El popup se cierra automáticamente cuando exista al menos 1 binario y 1 modelo.",
                           "Popup closes automatically when at least 1 binary and 1 model exist.",
                           "当至少存在1个二进制文件和1个模型时，弹窗自动关闭。",
                           "La fenêtre se ferme automatiquement quand au moins 1 binaire et 1 modèle existent.",
                           "Il popup si chiude automaticamente quando esistono almeno 1 binario e 1 modello.",
                           "Das Popup schließt sich automatisch, wenn mindestens 1 Binärdatei und 1 Modell vorhanden sind."},
    {"setup.installLog",   "Log de instalación","Installation Log", "安装日志","Journal d'installation","Log di installazione","Installationsprotokoll"},
    {"setup.copyLog",      "Copiar log",      "Copy log",               "复制日志","Copier le journal",  "Copia log",            "Log kopieren"},
    {"setup.viewLog",      "Ver log",         "View log",               "查看日志","Voir le journal",    "Vedi log",             "Log anzeigen"},
    // Agent page
    {"agent.title",        "Agente",          "Agent",             "代理",           "Agent",             "Agente",              "Agent"},
    {"agent.start",        "Iniciar agente",  "Start agent",       "启动代理",   "Démarrer l'agent","Avvia agente",       "Agent starten"},
    {"agent.stop",         "Detener agente",  "Stop agent",        "停止代理",   "Arrêter l'agent", "Ferma agente",       "Agent stoppen"},
    {"agent.running",      "Agente activo",   "Agent running",     "代理运行中", "Agent actif",      "Agente attivo",      "Agent läuft"},
    {"agent.stopped",      "Agente detenido", "Agent stopped",     "代理已停止", "Agent arrêté","Agente fermo",       "Agent gestoppt"},
    {"agent.log",          "Log del agente",  "Agent log",         "代理日志",   "Journal de l'agent","Log dell'agente","Agent-Log"},
    {"agent.input",        "Escribe un comando o prompt...", "Write a command or prompt...", "输入命令或提示...", "Saisir une commande...", "Scrivi un comando...", "Befehl eingeben..."},
    {"agent.send",         "Enviar",          "Send",              "发送",           "Envoyer",           "Invia",               "Senden"},
    {"agent.noHarness",    "Sin harness configurado para este perfil","No harness configured for this profile","此配置文件未配置执行框架","Aucun harness configuré pour ce profil","Nessun harness configurato per questo profilo","Kein Harness für dieses Profil konfiguriert"},
    {"agent.serverWarn",   "Servidor no activo — el agente puede no conectarse","Server not running — agent may not connect","服务器未运行 — 代理可能无法连接","Serveur inactif — l'agent peut ne pas se connecter","Server non attivo — l'agente potrebbe non connettersi","Server nicht aktiv — Agent verbindet sich möglicherweise nicht"},
    {"agent.profile",      "Perfil",          "Profile",           "配置文件",   "Profil",            "Profilo",             "Profil"},
    {"agent.clear",        "Limpiar",         "Clear",             "清除",           "Effacer",           "Cancella",            "Leeren"},
    {"agent.notInstalled", "Harness no instalado. Instalar en la sección Perfiles.", "Harness not installed. Install it in the Profiles section.", "执行框架未安装，请在配置文件页安装。","Harness non installé. Installer dans la section Profils.","Harness non installato. Installa nella sezione Profili.","Harness nicht installiert. In der Profile-Sektion installieren."},
    // Harness
    {"harness.title",        "Harness",         "Harness",            "执行框架",  "Harness",           "Harness",             "Harness"},
    {"harness.none",         "Ninguno",         "None",               "无",            "Aucun",             "Nessuno",             "Keiner"},
    {"harness.installed",    "Instalado",       "Installed",          "已安装",    "Installé",     "Installato",          "Installiert"},
    {"harness.notInstalled", "No instalado",    "Not installed",      "未安装",    "Non installé","Non installato",     "Nicht installiert"},
    {"harness.install",      "Instalar",        "Install",            "安装",          "Installer",         "Installa",            "Installieren"},
    {"harness.installing",   "Instalando...",   "Installing...",      "安装中...", "Installation...","Installazione...","Installiere..."},
    {"harness.cancelInstall","Cancelar",        "Cancel",             "取消",          "Annuler",           "Annulla",             "Abbrechen"},
    // Settings page
    {"settings.title",      "Configuración","Settings",           "设置",           "Paramètres",   "Impostazioni",         "Einstellungen"},
    {"settings.appearance", "Apariencia",      "Appearance",            "外观",           "Apparence",         "Aspetto",              "Erscheinungsbild"},
    {"settings.theme",      "Tema",            "Theme",                 "主题",           "Thème",        "Tema",                 "Design"},
    {"settings.dark",       "Oscuro",          "Dark",                  "深色",           "Sombre",            "Scuro",                "Dunkel"},
    {"settings.light",      "Claro",           "Light",                 "浅色",           "Clair",             "Chiaro",               "Hell"},
    {"settings.oled",       "OLED",            "OLED",                  "OLED",                   "OLED",              "OLED",                 "OLED"},
    {"settings.language",   "Idioma",          "Language",              "语言",           "Langue",            "Lingua",               "Sprache"},
    {"settings.system",     "Sistema",         "System",                "系统",           "Système",           "Sistema",              "System"},
    {"settings.minimizeToTray", "Minimizar a la bandeja", "Minimize to tray", "最小化到托盘", "Réduire dans la barre", "Riduci a icona", "In Infobereich minimieren"},
    {"settings.minimizeToTrayDesc", "Al cerrar, la app se oculta en los íconos de notificación en vez de cerrarse. Click derecho en el ícono para abrirla o salir.", "On close, the app hides in the notification tray instead of quitting. Right-click the icon to reopen or quit.", "关闭时，应用会隐藏到通知托盘而不是退出。右键单击图标可重新打开或退出。", "À la fermeture, l'application se réduit dans la zone de notification au lieu de quitter. Clic droit sur l'icône pour rouvrir ou quitter.", "Alla chiusura, l'app si nasconde nell'area di notifica invece di uscire. Clic destro sull'icona per riaprire o uscire.", "Beim Schließen wird die App im Infobereich versteckt statt beendet. Rechtsklick auf das Symbol zum Öffnen oder Beenden."},
    {"tray.open",           "Abrir",           "Open",                  "打开",           "Ouvrir",            "Apri",                 "Öffnen"},
    {"tray.quit",           "Salir",           "Quit",                  "退出",           "Quitter",           "Esci",                 "Beenden"},
};

const QHash<QString, QHash<QString, QString>> &AppController::translations()
{
    static QHash<QString, QHash<QString, QString>> t = []() {
        QHash<QString, QHash<QString, QString>> h;
        for (const auto &e : k_tr) {
            h[QStringLiteral("es")][QLatin1String(e.key)] = QString::fromUtf8(e.es);
            h[QStringLiteral("en")][QLatin1String(e.key)] = QString::fromUtf8(e.en);
            h[QStringLiteral("zh")][QLatin1String(e.key)] = QString::fromUtf8(e.zh);
            h[QStringLiteral("fr")][QLatin1String(e.key)] = QString::fromUtf8(e.fr);
            h[QStringLiteral("it")][QLatin1String(e.key)] = QString::fromUtf8(e.it);
            h[QStringLiteral("de")][QLatin1String(e.key)] = QString::fromUtf8(e.de);
        }
        return h;
    }();
    return t;
}

// ── Benchmark ─────────────────────────────────────────────────────────────────

struct BenchTaskDef {
    QString id;
    QString category;
    QString prompt;
    int maxTokens;
    bool isSpeed;
    std::function<bool(const QString &)> eval;
    QVariantMap acceptance;
};

static bool benchEvalJson(const QString &r)
{
    const QString tr = r.trimmed();
    int s = tr.indexOf('{'), e = tr.lastIndexOf('}');
    if (s < 0 || e <= s) return false;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(tr.mid(s, e - s + 1).toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const auto o = doc.object();
    return o.contains("name") && o.contains("age") && o.contains("active");
}

static QVector<BenchTaskDef> buildBenchTasks(const QString &mode)
{
    const bool full = (mode == QStringLiteral("full"));
    QVector<BenchTaskDef> t;

    // ── Speed tasks (streaming, measure TTFT + TPS) ────────────────────────────
    t.append({"speed_short", "speed",
        "Write a Python function to check if a number is prime.",
        512, true, nullptr});

    t.append({"speed_medium", "speed",
        "You are a senior Python engineer. Review the following code and suggest "
        "specific improvements for performance, readability, and correctness.\n\n"
        "```python\ndef process_data(items):\n    result = []\n    for i in range(len(items)):\n"
        "        if items[i] > 0:\n            result.append(items[i] * 2)\n"
        "        elif items[i] == 0:\n            result.append(0)\n"
        "        else:\n            result.append(abs(items[i]))\n"
        "    seen = {}\n    final = []\n    for x in result:\n"
        "        if x not in seen:\n            seen[x] = True\n            final.append(x)\n"
        "    total = 0\n    for x in final:\n        total = total + x\n    return total, final\n```\n\n"
        "Provide: 1) A rewritten version using idiomatic Python, 2) Big-O analysis of original vs new, "
        "3) Any edge cases the original misses. Be concise and specific.",
        1024, true, nullptr});

    if (full) {
        t.append({"speed_long", "speed",
            "You are an expert software architect. Perform a comprehensive security review of this "
            "authentication system. List ALL vulnerabilities found, categorize by OWASP Top 10, "
            "and provide specific remediation code for each.\n\n"
            "=== FILE: src/api/auth.py ===\n"
            "import jwt\nimport hashlib\nimport time\nfrom functools import wraps\n"
            "from flask import request, jsonify\n\n"
            "SECRET_KEY = 'hardcoded-secret-do-not-use-in-prod'\nALGORITHM = 'HS256'\n\n"
            "def generate_token(user_id: int, role: str) -> str:\n"
            "    payload = {'user_id': user_id, 'role': role, 'exp': time.time() + 3600}\n"
            "    return jwt.encode(payload, SECRET_KEY, algorithm=ALGORITHM)\n\n"
            "def verify_token(token: str) -> dict:\n"
            "    try:\n        return jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])\n"
            "    except: return None\n\n"
            "def require_auth(f):\n    @wraps(f)\n    def decorated(*args, **kwargs):\n"
            "        token = request.headers.get('Authorization','').replace('Bearer ','')\n"
            "        payload = verify_token(token)\n"
            "        if not payload: return jsonify({'error':'Unauthorized'}), 401\n"
            "        request.user = payload\n        return f(*args, **kwargs)\n"
            "    return decorated\n",
            2048, true, nullptr});

        t.append({"speed_prose", "speed",
            "Write an 800-word essay about paper money during the Tang Dynasty in China. "
            "Cover its origins, how it worked, economic effects, and eventual decline. "
            "Write in clear, flowing prose paragraphs. Do not use bullet points or headers.",
            2048, true, nullptr});

        t.append({"speed_json", "speed",
            "You are given this list of programming concepts:\n"
            "[\"recursion\", \"for loop\", \"while loop\", \"memoization\", \"dynamic programming\", "
            "\"merge sort\", \"quick sort\", \"bubble sort\", \"binary search\", \"linear search\", "
            "\"hash map\", \"linked list\", \"binary tree\", \"graph traversal\", \"stack\", \"queue\", "
            "\"BFS\", \"DFS\", \"greedy algorithm\", \"divide and conquer\"]\n\n"
            "Group these into categories. Return ONLY a valid JSON object where keys are category "
            "names and values are arrays of items. No explanation, no markdown, no code fences.",
            512, true, nullptr});
    }

    // ── Quality tasks (non-streaming, eval response) ───────────────────────────
    t.append({"python_prime", "coding",
        "Write a Python function is_prime(n: int) -> bool with proper type hints. "
        "Handle edge cases (n<=1, n=2). No explanation, just code.",
        300, false,
        [](const QString &r) {
            return r.contains("def is_prime") && r.contains("return") &&
                   (r.contains("n <= 1") || r.contains("n < 2") || r.contains("n == 1"));
        }});

    t.append({"math_arithmetic", "math",
        "Calculate: (17 * 23) + (456 / 8) - 12. Show each step. Give the final numeric answer.",
        512, false,
        [](const QString &r) { return r.contains("436") || r.contains("436.0"); }});

    t.append({"code_refactor", "coding",
        "Rewrite this as a one-liner using list comprehension:\n"
        "result = []\nfor x in range(10):\n    if x % 2 == 0:\n        result.append(x**2)\n"
        "Return only the one-liner, no explanation.",
        100, false,
        [](const QString &r) {
            return r.contains("[") && r.contains("for") && r.contains("if") &&
                   (r.contains("**2") || r.contains("x*x") || r.contains("pow("));
        }});

    t.append({"reasoning_logic", "reasoning",
        "All A are B. All B are C. Is all A are C? Answer YES or NO, then explain in one sentence.",
        150, false,
        [](const QString &r) {
            const QString u = r.toUpper().trimmed();
            return u.startsWith("YES") || u.left(20).contains("YES");
        }});

    t.append({"json_output", "instruction",
        "Return ONLY valid JSON: {\"name\":\"Alice\",\"age\":30,\"active\":true}. "
        "No markdown, no explanation, no code fences.",
        80, false, benchEvalJson});

    if (full) {
        t.append({"coding_sort", "coding",
            "Implement merge sort in Python. Function signature: def merge_sort(arr: list) -> list. "
            "Include the merge helper function. No explanation, just code.",
            500, false,
            [](const QString &r) {
                return r.contains("def merge_sort") && r.contains("def merge") && r.contains("return");
            }});

        t.append({"summary_strict", "instruction",
            "Summarize what a neural network is in EXACTLY 2 sentences. No more, no less.",
            200, false,
            [](const QString &r) { return r.trimmed().length() > 20; }});
    }

    return t;
}

struct RecommendedModelDef {
    QString name;
    QString repo;
    QString fileName;
    QString family;
    QString capabilities;
    QString params;
    QString quant;
    double sizeGb;
    double minRamGb;
    double recommendedRamGb;
    double minVramGb;
    int ctxK;
    QString notes;
};

static double moeActiveParamsFromName(const QString &name, double fallbackParamsB)
{
    QRegularExpression re(QStringLiteral("A(\\d+(?:\\.\\d+)?)B"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(name);
    if (m.hasMatch()) {
        bool ok = false;
        const double active = m.captured(1).toDouble(&ok);
        if (ok && active > 0)
            return active;
    }
    return qMin(fallbackParamsB, qMax(1.0, fallbackParamsB * 0.12));
}

static double catalogParamsB(const QJsonObject &model)
{
    const double raw = model.value(QStringLiteral("parameters_raw")).toDouble();
    if (raw > 0)
        return raw / 1000000000.0;

    QString pc = model.value(QStringLiteral("parameter_count")).toString().trimmed().toUpper();
    QRegularExpression re(QStringLiteral("^([\\d.]+)\\s*([BKMGT]?)$"));
    const QRegularExpressionMatch m = re.match(pc);
    if (!m.hasMatch())
        return 0;
    bool ok = false;
    const double val = m.captured(1).toDouble(&ok);
    if (!ok)
        return 0;
    const QString suffix = m.captured(2);
    if (suffix == QLatin1String("B")) return val;
    if (suffix == QLatin1String("M")) return val / 1000.0;
    if (suffix == QLatin1String("K")) return val / 1000000.0;
    if (suffix == QLatin1String("T")) return val * 1000.0;
    if (val >= 1000000.0) return val / 1000000000.0;
    return val / 1000.0;
}

static QString catalogFamily(const QString &name, const QString &arch)
{
    const QString s = (name + QLatin1Char(' ') + arch).toLower();
    if (s.contains(QStringLiteral("qwen"))) return QStringLiteral("Qwen");
    if (s.contains(QStringLiteral("mistral")) || s.contains(QStringLiteral("mixtral"))) return QStringLiteral("Mistral");
    if (s.contains(QStringLiteral("deepseek"))) return QStringLiteral("DeepSeek");
    if (s.contains(QStringLiteral("llama"))) return QStringLiteral("Llama");
    if (s.contains(QStringLiteral("gemma"))) return QStringLiteral("Gemma");
    if (s.contains(QStringLiteral("phi"))) return QStringLiteral("Phi");
    if (s.contains(QStringLiteral("gpt-oss")) || s.contains(QStringLiteral("openai"))) return QStringLiteral("GPT OSS");
    if (s.contains(QStringLiteral("olmo"))) return QStringLiteral("Olmo");
    return name.section(QLatin1Char('/'), 0, 0);
}

static QString catalogCapabilities(const QJsonObject &model)
{
    QStringList caps;
    const QString combined = (model.value(QStringLiteral("name")).toString() + QLatin1Char(' ') +
                              model.value(QStringLiteral("use_case")).toString() + QLatin1Char(' ') +
                              model.value(QStringLiteral("pipeline_tag")).toString()).toLower();
    if (combined.contains(QStringLiteral("code"))) caps << QStringLiteral("code");
    if (combined.contains(QStringLiteral("reason")) || combined.contains(QStringLiteral("thinking"))) caps << QStringLiteral("reasoning");
    if (combined.contains(QStringLiteral("vision")) || combined.contains(QStringLiteral("multimodal")) || combined.contains(QStringLiteral("vl"))) caps << QStringLiteral("vision");
    if (combined.contains(QStringLiteral("embed"))) caps << QStringLiteral("embedding");
    if (combined.contains(QStringLiteral("chat")) || combined.contains(QStringLiteral("instruct")) || combined.contains(QStringLiteral("instruction"))) caps << QStringLiteral("chat");
    if (model.value(QStringLiteral("is_moe")).toBool() || model.value(QStringLiteral("active_parameters")).toDouble() > 0) caps << QStringLiteral("moe");
    const QJsonArray explicitCaps = model.value(QStringLiteral("capabilities")).toArray();
    for (const QJsonValue &v : explicitCaps) {
        const QString cap = v.toString().toLower();
        if (cap == QLatin1String("tool_use") || cap == QLatin1String("function_calling"))
            caps << QStringLiteral("tool_use");
        else if (cap == QLatin1String("vision"))
            caps << QStringLiteral("vision");
        else if (cap == QLatin1String("mtp"))
            caps << QStringLiteral("mtp");
    }
    if (caps.isEmpty()) caps << QStringLiteral("general");
    caps.removeDuplicates();
    return caps.join(QLatin1Char(','));
}

static QString catalogUseCase(const QString &caps)
{
    if (caps.contains(QStringLiteral("code"))) return QStringLiteral("coding");
    if (caps.contains(QStringLiteral("reasoning"))) return QStringLiteral("reasoning");
    if (caps.contains(QStringLiteral("vision"))) return QStringLiteral("multimodal");
    if (caps.contains(QStringLiteral("embedding"))) return QStringLiteral("embedding");
    if (caps.contains(QStringLiteral("tool_use"))) return QStringLiteral("agentic");
    if (caps.contains(QStringLiteral("chat"))) return QStringLiteral("chat");
    return QStringLiteral("general");
}

static QString guessGgufFileName(const QString &repo, const QString &modelName, const QString &quant)
{
    QString base = repo.section(QLatin1Char('/'), -1);
    base.remove(QRegularExpression(QStringLiteral("[-_]?GGUF$"), QRegularExpression::CaseInsensitiveOption));
    if (base.isEmpty())
        base = modelName.section(QLatin1Char('/'), -1);
    const QString q = quant.isEmpty() ? QStringLiteral("Q4_K_M") : quant;
    const bool unslothMtp = repo.contains(QStringLiteral("unsloth/"), Qt::CaseInsensitive)
        && base.contains(QStringLiteral("-MTP"), Qt::CaseInsensitive);
    if (unslothMtp)
        base.remove(QRegularExpression(QStringLiteral("[-_]?MTP$"), QRegularExpression::CaseInsensitiveOption));
    const bool unslothDynamicQwen36 = repo.contains(QStringLiteral("unsloth/"), Qt::CaseInsensitive)
        && base.contains(QStringLiteral("Qwen3.6"), Qt::CaseInsensitive)
        && q.startsWith(QStringLiteral("Q"), Qt::CaseInsensitive)
        && !q.startsWith(QStringLiteral("UD-"), Qt::CaseInsensitive);
    return QStringLiteral("%1-%2%3.gguf").arg(base, unslothDynamicQwen36 ? QStringLiteral("UD-") : QString(), q);
}

static QUrl huggingFaceResolveUrl(const QString &repo, const QString &fileName)
{
    return QUrl(QStringLiteral("https://huggingface.co/%1/resolve/main/%2")
                    .arg(repo, QString::fromLatin1(QUrl::toPercentEncoding(fileName, "/"))));
}

static QUrl huggingFaceApiUrl(const QString &repo)
{
    return QUrl(QStringLiteral("https://huggingface.co/api/models/%1")
                    .arg(QString::fromLatin1(QUrl::toPercentEncoding(repo, "/"))));
}

static QString requestedQuantToken(const QString &fileName)
{
    const QString base = QFileInfo(fileName).fileName();
    const QRegularExpression re(QStringLiteral("(UD-)?(IQ[0-9]_[A-Z0-9]+|Q[0-9]_[A-Z](?:_[A-Z])?|Q[0-9]_[0-9]|Q[0-9]|MXFP4_MOE|NVFP4|FP4|BF16|F16|Q8_0)\\.gguf$"),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(base);
    if (m.hasMatch())
        return m.captured(0).chopped(5).toUpper();
    return QStringLiteral("Q4_K_M");
}

static int ggufSiblingRank(const QString &sibling, const QString &requestedFileName)
{
    const QString s = QFileInfo(sibling).fileName().toUpper();
    const QString requestedBase = QFileInfo(requestedFileName).fileName().toUpper();
    if (s == requestedBase)
        return 0;

    const QString quant = requestedQuantToken(requestedFileName);
    QStringList preferred;
    if (!quant.startsWith(QStringLiteral("UD-")))
        preferred << QStringLiteral("UD-%1").arg(quant);
    preferred << quant;
    preferred << QStringLiteral("UD-Q4_K_M") << QStringLiteral("Q4_K_M")
              << QStringLiteral("UD-IQ4_XS") << QStringLiteral("IQ4_XS")
              << QStringLiteral("UD-IQ4_NL") << QStringLiteral("IQ4_NL")
              << QStringLiteral("UD-Q4_K_S") << QStringLiteral("Q4_K_S")
              << QStringLiteral("UD-Q3_K_M") << QStringLiteral("Q3_K_M")
              << QStringLiteral("UD-Q5_K_M") << QStringLiteral("Q5_K_M")
              << QStringLiteral("MXFP4_MOE");
    preferred.removeDuplicates();

    for (int i = 0; i < preferred.size(); ++i) {
        const QString token = preferred.at(i).toUpper();
        if (s.endsWith(QStringLiteral("-%1.GGUF").arg(token)) || s.endsWith(QStringLiteral("_%1.GGUF").arg(token)))
            return 10 + i;
    }
    return s.endsWith(QStringLiteral(".GGUF")) ? 100 : 1000;
}

static QString chooseGgufSibling(const QStringList &siblings, const QString &requestedFileName)
{
    QString best;
    int bestRank = 1000;
    for (const QString &s : siblings) {
        if (!s.endsWith(QStringLiteral(".gguf"), Qt::CaseInsensitive))
            continue;
        const int rank = ggufSiblingRank(s, requestedFileName);
        if (rank < bestRank || (rank == bestRank && s.size() < best.size())) {
            best = s;
            bestRank = rank;
        }
    }
    return best;
}

bool AppController::isGgufRecommendationCandidate(const QString &name, bool isGguf,
                                                  bool hasGgufSources)
{
    const QString n = name.toLower();
    if (n.contains(QStringLiteral("mlx")))
        return false;
    if (n.contains(QStringLiteral("awq")) || n.contains(QStringLiteral("gptq")) ||
        n.contains(QStringLiteral("exl2")) || n.contains(QStringLiteral("bnb")))
        return false;
    return hasGgufSources || isGguf || n.contains(QStringLiteral("gguf"));
}

QString AppController::recommendedGgufFileName(const QString &repo, const QString &modelName,
                                               const QString &quant)
{
    return guessGgufFileName(repo, modelName, quant);
}

QString AppController::resolveRecommendedGgufFileName(const QStringList &siblings,
                                                      const QString &requestedFileName)
{
    return chooseGgufSibling(siblings, requestedFileName);
}

static double quantBpp(const QString &quant)
{
    const QString q = quant.toUpper();
    if (q.contains(QStringLiteral("Q8")) || q.contains(QStringLiteral("FP8")) || q.contains(QStringLiteral("INT8"))) return 1.05;
    if (q.contains(QStringLiteral("Q6"))) return 0.80;
    if (q.contains(QStringLiteral("Q5"))) return 0.68;
    if (q.contains(QStringLiteral("Q3"))) return 0.48;
    if (q.contains(QStringLiteral("Q2"))) return 0.37;
    if (q.contains(QStringLiteral("BF16")) || q.contains(QStringLiteral("F16"))) return 2.0;
    if (q.contains(QStringLiteral("AWQ")) || q.contains(QStringLiteral("GPTQ")) || q.contains(QStringLiteral("Q4")) || q.contains(QStringLiteral("FP4")) || q.contains(QStringLiteral("NVFP4")) || q.contains(QStringLiteral("MXFP4"))) return 0.58;
    return 0.58;
}

// Prefer the catalog's measured/curated runtime footprint when present. The
// synthetic fallback is intentionally conservative, but it must not overrule the
// catalog and falsely mark common 7B/8B Q4 GGUF models as spilling out of 8 GB VRAM.
static double estimateCatalogMemoryGb(const QJsonObject &model, double paramsB, const QString &quant, int ctx)
{
    const double catalogRecommendedGb = model.value(QStringLiteral("recommended_ram_gb")).toDouble();
    if (catalogRecommendedGb > 0)
        return catalogRecommendedGb;

    const double weightsGb = paramsB * quantBpp(quant);
    const double kvGb = 7.5e-6 * paramsB * qMax(2048, ctx);
    const double overheadGb = 0.7 + weightsGb * 0.05;
    return weightsGb + kvGb + overheadGb;
}

static double estimateMoeActiveGpuGb(const QJsonObject &model, double activeParamsB, const QString &quant, int ctx)
{
    double activeGb = model.value(QStringLiteral("active_parameters")).toDouble() > 0
        ? activeParamsB * quantBpp(quant)
        : 0.0;
    if (activeGb <= 0)
        activeGb = activeParamsB * quantBpp(quant);
    const double kvGb = 7.5e-6 * qMax(0.2, activeParamsB) * qMax(2048, ctx);
    const double routerAndSharedGb = qMax(0.45, activeGb * 0.18);
    return activeGb + kvGb + routerAndSharedGb;
}

// Context used for *sizing*. Reddit feedback: don't size at a token 4k window —
// real agent use is 16-64k. Size at a realistic target capped by the model's own
// max, floored so tiny defaults don't understate the KV cost.
static int sizingContext(int modelMaxCtx)
{
    const int target = 32768;
    const int hardMax = qMax(2048, modelMaxCtx);
    // Floor at 8k, but never above the model's own max (qBound asserts if min>max).
    const int floor = qMin(8192, hardMax);
    return qBound(floor, qMin(target, hardMax), hardMax);
}

// gpuFraction = share of the model that fits in VRAM (1.0 = fully on GPU, 0 = all
// CPU). For partial offload the effective bandwidth is a harmonic-ish blend of GPU
// and CPU memory speed — the CPU-resident layers gate throughput hard.
static double catalogSpeedTps(const QString &gpuName, double activeParamsB, double requiredGb,
                              const QString &runMode, double gpuFraction = 1.0)
{
    double bw = 220.0;
    const QString g = gpuName.toLower();
    if (g.contains(QStringLiteral("3090"))) bw = 936.0;
    else if (g.contains(QStringLiteral("4090"))) bw = 1008.0;
    else if (g.contains(QStringLiteral("4080"))) bw = 717.0;
    else if (g.contains(QStringLiteral("3080"))) bw = 760.0;
    else if (g.contains(QStringLiteral("3060"))) bw = 360.0;
    else if (g.contains(QStringLiteral("a6000"))) bw = 768.0;
    const double cpuBw = 70.0;
    if (runMode == QLatin1String("cpu_only")) {
        bw = cpuBw;
    } else if (runMode == QLatin1String("partial_offload")) {
        // Per-token cost is dominated by the slowest tier it must stream through.
        const double frac = qBound(0.0, gpuFraction, 1.0);
        bw = 1.0 / (frac / qMax(bw, cpuBw) + (1.0 - frac) / cpuBw);
    } else if (runMode == QLatin1String("moe_active_gpu")) {
        // MoE still keeps cold experts in RAM, but token work mostly streams the
        // active experts from VRAM.
        bw = bw * 0.72;
    }
    const double denom = qMax(0.2, activeParamsB * qMax(0.35, requiredGb / qMax(0.2, activeParamsB)));
    return qMax(0.0, (bw / denom) * 0.55);
}

// Normalize a catalog model name to a benchmark-table key: drop the provider
// prefix, strip quant/format suffixes (AWQ, FP8, GGUF, Q4_K_M, -4bit…) and
// role tags (instruct/it/base/thinking…), collapse separators to single spaces.
// Must mirror the key shape used in assets/benchmarks/aa_intelligence.json.
static QString benchmarkKey(const QString &rawName)
{
    QString s = rawName.section(QLatin1Char('/'), -1).toLower();
    const auto strip = [&s](const QString &pat) {
        s.remove(QRegularExpression(pat, QRegularExpression::CaseInsensitiveOption));
    };
    // GGUF k-quant tiers (q4_k_m, q5_k, q8_0, iq4_xs…)
    strip(QStringLiteral("[-_.]?(q[0-9](_k(_[a-z])?|_[0-9])?|iq[0-9][a-z0-9]*)\\b"));
    // Prequantized / float formats, with optional bit-width
    strip(QStringLiteral("[-_.]?(awq|gptq|gguf|mlx|exl2|bnb|nvfp4|mxfp4|fp4|fp8|fp16|bf16|f16|f32|int4|int8|w4a16|w8a8|w8a16|nf4)([-_]?[0-9]{1,2}(bit)?)?\\b"));
    // Bare bit-width tags (4bit, 8bit)
    strip(QStringLiteral("[-_.]?[0-9]{1,2}bit\\b"));
    // Date-stamp tokens (2507, 2501, 2512…)
    strip(QStringLiteral("[-_.]?2[0-9]{3}\\b"));
    // Role / variant tags
    strip(QStringLiteral("[-_](instruct|it|base|chat|thinking|reasoning|captioner|preview|distill|hf|mtp)\\b"));
    // Collapse separators
    s.replace(QRegularExpression(QStringLiteral("[-_/]")), QStringLiteral(" "));
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return s.trimmed();
}

// Quality penalty per quant tier — ported from Odysseus services/hwfit/models.py
// QUANT_QUALITY_PENALTY so quant choice nudges quality the same way the cookbook does.
static double quantQualityPenalty(const QString &quant)
{
    const QString q = quant.toUpper();
    if (q.contains(QStringLiteral("Q2"))) return -12.0;
    if (q.contains(QStringLiteral("Q3"))) return -8.0;
    if (q.contains(QStringLiteral("Q4"))) return -5.0;
    if (q.contains(QStringLiteral("Q5"))) return -2.0;
    if (q.contains(QStringLiteral("Q6"))) return -1.0;
    if (q.contains(QStringLiteral("NF4"))) return -4.0;
    if (q.contains(QStringLiteral("NVFP4")) || q.contains(QStringLiteral("MXFP4")) || q.contains(QStringLiteral("FP4"))) return -3.0;
    if (q.contains(QStringLiteral("AWQ"))) return q.contains(QStringLiteral("4")) ? -4.0 : -1.0;
    if (q.contains(QStringLiteral("GPTQ"))) return q.contains(QStringLiteral("4")) ? -4.0 : -1.0;
    if (q.contains(QStringLiteral("INT4")) || q.contains(QStringLiteral("W4"))) return -4.0;
    // FP8 / INT8 / BF16 / F16 / Q8 → no penalty
    return 0.0;
}

static double catalogPopularityScore(const QJsonObject &model)
{
    const double downloads = qMax(0.0, model.value(QStringLiteral("hf_downloads")).toDouble());
    const double likes = qMax(0.0, model.value(QStringLiteral("hf_likes")).toDouble());
    const double downloadScore = qMin(70.0, std::log10(downloads + 1.0) * 11.0);
    const double likeScore = qMin(30.0, std::log10(likes + 1.0) * 12.0);
    return qBound(0.0, downloadScore + likeScore, 100.0);
}

// Newer family gets a quality boost — ported from Odysseus _architecture_bonus.
static int architectureBonus(const QString &name, const QString &arch)
{
    const QString text = (name + QLatin1Char(' ') + arch).toLower();
    if (text.contains(QStringLiteral("qwen3.6")) || text.contains(QStringLiteral("qwen3_6"))) return 9;
    if (text.contains(QStringLiteral("qwen3.5")) || text.contains(QStringLiteral("qwen3_5"))) return 8;
    if (text.contains(QStringLiteral("qwen3-next")) || text.contains(QStringLiteral("qwen3_next"))) return 6;
    if (text.contains(QStringLiteral("qwen3"))) return 4;
    if (text.contains(QStringLiteral("qwen2.5")) || text.contains(QStringLiteral("qwen2_5"))) return 2;
    return 0;
}

// Composite score: quality/speed/fit/context. Quality intentionally dominates so
// a GPU with enough headroom is not filled with tiny models just because their
// estimated tokens/s is high. No popularity term, and coder-specialised models
// are penalised in a general scan so they don't dominate.
static int catalogScore(const QJsonObject &model, double paramsB, double requiredGb, double ramGb, double vramGb,
                        const QString &quant, const QString &caps, int ctx, const QString &runMode, double tps,
                        double benchmarkQuality, double cookbookPriority)
{
    // ── quality ──
    // Prefer a real benchmark (Artificial Analysis Intelligence Index, remapped
    // to a 0-100 quality) when we have one for this model; the quant tier still
    // costs a little quality. Otherwise fall back to the param/family heuristic.
    double heuristicQuality;
    if (paramsB < 1) heuristicQuality = 30;
    else if (paramsB < 3) heuristicQuality = 45;
    else if (paramsB < 7) heuristicQuality = 60;
    else if (paramsB < 10) heuristicQuality = 75;
    else if (paramsB < 20) heuristicQuality = 82;
    else if (paramsB < 40) heuristicQuality = 89;
    else heuristicQuality = 95;

    const QString name = model.value(QStringLiteral("name")).toString().toLower();
    if (name.contains(QStringLiteral("qwen"))) heuristicQuality += 2;
    if (name.contains(QStringLiteral("deepseek"))) heuristicQuality += 3;
    if (name.contains(QStringLiteral("llama"))) heuristicQuality += 2;
    if (name.contains(QStringLiteral("mistral")) || name.contains(QStringLiteral("mixtral"))) heuristicQuality += 1;
    if (name.contains(QStringLiteral("gemma"))) heuristicQuality += 1;

    heuristicQuality += architectureBonus(name, model.value(QStringLiteral("architecture")).toString());
    heuristicQuality += quantQualityPenalty(quant);

    // Use-case adjustment (default scan == "general"). Coder models are useful
    // generally but must not dominate the default list — penalise like Odysseus.
    const QString uc = catalogUseCase(caps);
    if (uc == QLatin1String("coding"))
        heuristicQuality -= 10;
    else if (uc == QLatin1String("agentic"))
        heuristicQuality += 4;

    double quality = heuristicQuality;
    if (benchmarkQuality >= 0) {
        // Benchmarks are a quality signal, not a veto. Keep the local hardware-fit
        // heuristic as a floor so a small bundled AA entry does not hide an official
        // 7B/8B Q4 model that fits cleanly in 8 GB VRAM.
        quality = qMax(heuristicQuality, benchmarkQuality + quantQualityPenalty(quant));
    }
    quality = qBound(0.0, quality, 100.0);

    // ── speed ── target 40 t/s at general use case
    const double speedScore = qBound(0.0, (tps / 40.0) * 100.0, 100.0);

    // ── fit ── tiered ratio of required vs budget. For partial offload, include
    // usable system RAM; otherwise 7B/8B models that run fine on 8 GB NVIDIA
    // cards get a zero fit score only because the 32k KV cache spills slightly.
    const double usableRamGb = ramGb * 0.9;
    const double budget = (runMode == QLatin1String("partial_offload") || runMode == QLatin1String("moe_active_gpu"))
                        ? (runMode == QLatin1String("moe_active_gpu") ? vramGb : (vramGb + usableRamGb))
                        : vramGb > 0 ? vramGb
                        : usableRamGb;
    double fitScore = 0;
    if (budget > 0 && requiredGb <= budget) {
        const double ratio = requiredGb / budget;
        if (ratio <= 0.5) fitScore = 60 + (ratio / 0.5) * 40;
        else if (ratio <= 0.8) fitScore = 100;
        else if (ratio <= 0.9) fitScore = 70;
        else fitScore = 50;
        if (runMode == QLatin1String("partial_offload"))
            fitScore = qMax(55.0, fitScore - 10.0);
        else if (runMode == QLatin1String("moe_active_gpu"))
            fitScore = qMax(78.0, fitScore);
    }

    // ── context ── modern agent target is 32k+, not a token 4k window
    const double ctxScore = ctx >= 32768 ? 100
                          : ctx >= 16384 ? 85
                          : ctx >= 8192  ? 70
                          : ctx >= 4096  ? 50
                          : 30;

    const double popularityScore = catalogPopularityScore(model);
    const double sourceScore = cookbookPriority >= 0 ? cookbookPriority : popularityScore;
    double composite = quality * 0.55
                     + speedScore * 0.15
                     + fitScore * 0.15
                     + ctxScore * 0.10
                     + sourceScore * 0.05;
    if (cookbookPriority >= 0)
        composite += qMin(6.0, cookbookPriority / 18.0);
    if (vramGb >= 7.0 && paramsB >= 7.0 && paramsB <= 10.0)
        composite += 5.0; // 7B/8B Q4 is the useful sweet spot for 8 GB NVIDIA cards.
    else if (vramGb >= 7.0 && paramsB < 4.0)
        composite -= 3.0; // keep small/fast models available, but not as default winners.

    if (vramGb >= 7.0 && paramsB >= 7.0 && paramsB <= 10.0
            && runMode == QLatin1String("gpu")
            && model.value(QStringLiteral("name")).toString().startsWith(QStringLiteral("Qwen/")))
        composite += 2.0; // prefer official Qwen 7B/8B picks over derivatives on 8 GB GPUs.
    if (vramGb >= 7.0 && paramsB >= 7.0 && paramsB <= 10.0
            && runMode == QLatin1String("gpu")
            && model.value(QStringLiteral("name")).toString().contains(QStringLiteral("Qwen3-8B"), Qt::CaseInsensitive))
        composite += 2.0; // current local default: Qwen3 8B Q4 fits 8 GB VRAM cleanly at 32k.
    if (vramGb >= 7.0 && vramGb <= 9.5 && ramGb <= 20.0 && paramsB >= 7.0 && paramsB <= 9.0
            && ctx >= 16384 && (caps.contains(QStringLiteral("tool_use")) || caps.contains(QStringLiteral("chat")))) {
        composite += 7.0; // 8 GB notebooks benefit from agentic 7B/8B headroom for KV/cache.
        if (model.value(QStringLiteral("name")).toString().contains(QStringLiteral("Qwen2.5-7B-Instruct"), Qt::CaseInsensitive))
            composite += 4.0;
        if (model.value(QStringLiteral("name")).toString().contains(QStringLiteral("Hermes-3-Llama-3.1-8B"), Qt::CaseInsensitive))
            composite += 3.0;
    }
    if (vramGb > 0 && vramGb < 4.0
            && model.value(QStringLiteral("name")).toString().contains(QStringLiteral("Qwen3.5-2B"), Qt::CaseInsensitive))
        composite += 14.0; // tiny GPUs should see the practical 2B MTP option near the top.
    if (vramGb >= 7.0 && vramGb <= 9.5 && ramGb <= 20.0
            && runMode == QLatin1String("moe_active_gpu") && requiredGb > ramGb * 0.9)
        composite -= 8.0; // keep big MoE visible, but behind lean 7B/8B picks on 16 GB RAM.

    if (runMode == QLatin1String("partial_offload") && tps < 5.0)
        composite = qMin(composite, 68.0);
    else if (runMode == QLatin1String("partial_offload") && tps < 10.0)
        composite = qMin(composite, 74.0);

    return qBound(0, qRound(composite), 100);
}

// Parse a version number from the name so equal-score rows break ties toward the
// newer release (Qwen3.6 > Qwen3.5). Ported from Odysseus _version_key.
static double catalogVersionKey(const QString &name)
{
    if (name.isEmpty()) return 0.0;
    QRegularExpression re(QStringLiteral("[A-Za-z](\\d+(?:\\.\\d+)?)(?![A-Za-z])"));
    auto it = re.globalMatch(name);
    while (it.hasNext()) {
        const QString val = it.next().captured(1);
        bool ok = false;
        const double f = val.toDouble(&ok);
        if (!ok) continue;
        if (!val.contains(QLatin1Char('.')) && f >= 100) continue; // param count, not version
        return f;
    }
    return 0.0;
}

static QString recommendationLane(const QString &caps, const QString &runMode, double paramsB)
{
    const QString useCase = catalogUseCase(caps);
    if (useCase == QLatin1String("coding"))
        return QStringLiteral("Código");
    if (useCase == QLatin1String("reasoning"))
        return QStringLiteral("Reasoning");
    if (runMode == QLatin1String("partial_offload") && paramsB >= 8.5 && paramsB <= 10.5)
        return QStringLiteral("Calidad");
    return QStringLiteral("General");
}

// ── Public methods ─────────────────────────────────────────────────────────────

void AppController::rescanHardware()
{
    QVariantMap hw;
    hw[QStringLiteral("cpuThreads")] = QThread::idealThreadCount();

    double ramGb = 0;
#ifdef Q_OS_WIN
    {
        ULONGLONG kb = 0;
        if (GetPhysicallyInstalledSystemMemory(&kb))
            ramGb = kb / 1024.0 / 1024.0;
        if (ramGb <= 0) {
            MEMORYSTATUSEX st;
            st.dwLength = sizeof(st);
            if (GlobalMemoryStatusEx(&st))
                ramGb = st.ullTotalPhys / 1024.0 / 1024.0 / 1024.0;
        }
    }
#endif
    hw[QStringLiteral("ramGb")] = ramGb;

    double vramGb = 0;
    QString gpuName;
    const QString nvidiaSmi = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
    if (!nvidiaSmi.isEmpty()) {
        QProcess p;
        p.start(nvidiaSmi, {QStringLiteral("--query-gpu=name,memory.total"),
                            QStringLiteral("--format=csv,noheader,nounits")});
        if (p.waitForFinished(1800)) {
            const QString firstLine = QString::fromUtf8(p.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts).value(0).trimmed();
            const int comma = firstLine.lastIndexOf(',');
            if (comma > 0) {
                gpuName = firstLine.left(comma).trimmed();
                vramGb = firstLine.mid(comma + 1).trimmed().toDouble() / 1024.0;
            }
        }
    }
    hw[QStringLiteral("gpuName")] = gpuName.isEmpty() ? QStringLiteral("GPU no detectada") : gpuName;
    hw[QStringLiteral("vramGb")] = vramGb;
    hw[QStringLiteral("backendHint")] = vramGb >= 6 ? QStringLiteral("GPU") : QStringLiteral("CPU");
    hw[QStringLiteral("summary")] = QStringLiteral("%1 hilos · %2 GB RAM · %3")
        .arg(QThread::idealThreadCount())
        .arg(ramGb > 0 ? QString::number(ramGb, 'f', 1) : QStringLiteral("?"))
        .arg(vramGb > 0
                 ? QStringLiteral("%1 (%2 GB VRAM)").arg(gpuName).arg(QString::number(vramGb, 'f', 1))
                 : QStringLiteral("sin VRAM NVIDIA detectada"));

    m_hardwareSummary = hw;
    emit hardwareSummaryChanged();
    rebuildModelRecommendations();
}

void AppController::rebuildModelRecommendations()
{
    if (!m_benchmarkLoaded)
        loadBenchmarkScores();
    maybeFetchBenchmarks();

    const double ramGb = m_hardwareSummary.value(QStringLiteral("ramGb")).toDouble();
    const double vramGb = m_hardwareSummary.value(QStringLiteral("vramGb")).toDouble();
    const QString gpuName = m_hardwareSummary.value(QStringLiteral("gpuName")).toString();

    QVariantList rows;
    QFile catalog(QStringLiteral(":/assets/hwfit/hf_models.json"));
    if (catalog.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(catalog.readAll()).array();
        rows.reserve(arr.size());
        for (const QJsonValue &value : arr) {
            const QJsonObject m = value.toObject();
            const QString name = m.value(QStringLiteral("name")).toString();
            const QJsonArray ggufSources = m.value(QStringLiteral("gguf_sources")).toArray();
            if (!isGgufRecommendationCandidate(name,
                                               m.value(QStringLiteral("is_gguf")).toBool(),
                                               !ggufSources.isEmpty())) {
                continue;
            }
            const double paramsB = catalogParamsB(m);
            if (name.isEmpty() || paramsB <= 0)
                continue;

            QString repo;
            QString fileName;
            if (!ggufSources.isEmpty()) {
                const QJsonObject src = ggufSources.first().toObject();
                repo = src.value(QStringLiteral("repo")).toString();
                fileName = src.value(QStringLiteral("file")).toString();
            }
            const QString lowerName = name.toLower();
            if (repo.isEmpty() && (m.value(QStringLiteral("is_gguf")).toBool() || lowerName.contains(QStringLiteral("gguf"))))
                repo = name;
            // This app downloads/runs llama.cpp GGUF models. Catalog entries for
            // MLX/AWQ/GPTQ/etc. can look tiny and score well, but they are not
            // runnable by llama.cpp on Windows/NVIDIA unless a GGUF source exists.
            if (repo.isEmpty())
                continue;

            const QString quant = m.value(QStringLiteral("quantization")).toString(QStringLiteral("Q4_K_M"));
            if (fileName.isEmpty())
                fileName = guessGgufFileName(repo, name, quant);
            const int modelMaxCtx = qMax(1024, m.value(QStringLiteral("context_length")).toInt(4096));
            const int ctx = sizingContext(modelMaxCtx);
            const double requiredGb = estimateCatalogMemoryGb(m, paramsB, quant, ctx);
            const double activeRaw = m.value(QStringLiteral("active_parameters")).toDouble();
            const bool isMoe = m.value(QStringLiteral("is_moe")).toBool();
            const double activeB = (isMoe && activeRaw > 0)
                ? activeRaw / 1000000000.0
                : paramsB;
            const double activeGpuGb = isMoe ? estimateMoeActiveGpuGb(m, activeB, quant, ctx) : requiredGb;
            const bool moeActiveFitsGpu = isMoe && vramGb > 0
                && activeGpuGb <= qMax(0.1, vramGb * 0.92)
                && requiredGb <= vramGb + ramGb * 0.9;

            QString runMode;
            QString fit;
            double gpuFraction = 1.0;  // share resident in VRAM (drives partial-offload speed)
            // ~90% of system RAM is usable for weights; the rest is OS/app headroom.
            const double usableRamGb = ramGb * 0.9;
            if (vramGb > 0 && requiredGb <= vramGb) {
                runMode = QStringLiteral("gpu");
                fit = (m.value(QStringLiteral("recommended_ram_gb")).toDouble(requiredGb) <= vramGb)
                    ? QStringLiteral("Perfecto")
                    : QStringLiteral("Bueno");
            } else if (moeActiveFitsGpu) {
                runMode = QStringLiteral("moe_active_gpu");
                gpuFraction = 0.9;
                fit = QStringLiteral("Razonable");
            } else if (vramGb > 0 && requiredGb <= vramGb + usableRamGb) {
                // Spill: weights split across VRAM + system RAM (llama.cpp -ngl partial).
                runMode = QStringLiteral("partial_offload");
                gpuFraction = qBound(0.0, vramGb / requiredGb, 1.0);
                fit = gpuFraction >= 0.6 ? QStringLiteral("Bueno") : QStringLiteral("Marginal");
            } else if (vramGb <= 0 && requiredGb <= usableRamGb) {
                runMode = QStringLiteral("cpu_only");
                gpuFraction = 0.0;
                fit = usableRamGb >= requiredGb * 1.2 ? QStringLiteral("Bueno") : QStringLiteral("Marginal");
            } else {
                runMode = QStringLiteral("no_fit");
                fit = QStringLiteral("No entra");
            }

            const QString caps = catalogCapabilities(m);
            const double scoreRequiredGb = runMode == QLatin1String("moe_active_gpu") ? activeGpuGb : requiredGb;
            const double tps = runMode == QLatin1String("no_fit")
                ? 0.0
                : catalogSpeedTps(gpuName, activeB, scoreRequiredGb, runMode, gpuFraction);
            const QString qualityKey = benchmarkKey(name);
            const double benchQuality = m_benchmarkQuality.value(qualityKey, -1.0);
            const double cookbookPriority = m_cookbookPriority.value(qualityKey, -1.0);
            const int score = catalogScore(m, paramsB, scoreRequiredGb, ramGb, vramGb, quant, caps, ctx, runMode, tps,
                                           benchQuality, cookbookPriority);

            QVariantMap row;
            row[QStringLiteral("name")] = name;
            row[QStringLiteral("repo")] = repo.isEmpty() ? name : repo;
            row[QStringLiteral("fileName")] = fileName;
            row[QStringLiteral("family")] = catalogFamily(name, m.value(QStringLiteral("architecture")).toString());
            row[QStringLiteral("capabilities")] = caps;
            row[QStringLiteral("useCase")] = catalogUseCase(caps);
            row[QStringLiteral("recommendationLane")] = recommendationLane(caps, runMode, paramsB);
            row[QStringLiteral("params")] = m.value(QStringLiteral("parameter_count")).toString(QStringLiteral("%1B").arg(QString::number(paramsB, 'f', 1)));
            row[QStringLiteral("quant")] = quant;
            row[QStringLiteral("sizeGb")] = requiredGb;
            row[QStringLiteral("requiredGb")] = requiredGb;
            row[QStringLiteral("activeGpuGb")] = activeGpuGb;
            row[QStringLiteral("minRamGb")] = m.value(QStringLiteral("min_ram_gb")).toDouble();
            row[QStringLiteral("recommendedRamGb")] = m.value(QStringLiteral("recommended_ram_gb")).toDouble();
            row[QStringLiteral("minVramGb")] = m.value(QStringLiteral("min_vram_gb")).toDouble();
            row[QStringLiteral("ctxK")] = qRound(ctx / 1000.0);
            row[QStringLiteral("context")] = ctx;
            QString runLabel = runMode;
            if (runMode == QLatin1String("gpu")) runLabel = QStringLiteral("GPU (todo en VRAM)");
            else if (runMode == QLatin1String("moe_active_gpu"))
                runLabel = QStringLiteral("MoE activo en GPU");
            else if (runMode == QLatin1String("partial_offload"))
                runLabel = QStringLiteral("VRAM+RAM (%1%% en GPU)").arg(qRound(gpuFraction * 100.0));
            else if (runMode == QLatin1String("cpu_only")) runLabel = QStringLiteral("CPU (todo en RAM)");
            else if (runMode == QLatin1String("no_fit")) runLabel = QStringLiteral("No entra");
            const QString moeNote = runMode == QLatin1String("moe_active_gpu")
                ? QStringLiteral(" · activo ~%1 GB").arg(QString::number(activeGpuGb, 'f', 1))
                : QString();
            row[QStringLiteral("notes")] = QStringLiteral("%1 · %2 t/s est.%3 · ~%4 GB @ %5k ctx · %6 downloads")
                .arg(runLabel)
                .arg(QString::number(tps, 'f', 1))
                .arg(moeNote)
                .arg(QString::number(requiredGb, 'f', 1))
                .arg(qRound(ctx / 1000.0))
                .arg(qRound(m.value(QStringLiteral("hf_downloads")).toDouble()));
            row[QStringLiteral("fit")] = fit;
            row[QStringLiteral("score")] = score;
            row[QStringLiteral("sourcePriority")] = cookbookPriority;
            row[QStringLiteral("downloadable")] = !repo.isEmpty() && !fileName.isEmpty();
            row[QStringLiteral("downloadUrl")] = (!repo.isEmpty() && !fileName.isEmpty())
                ? QStringLiteral("https://huggingface.co/%1/resolve/main/%2?download=true")
                    .arg(repo, QString::fromLatin1(QUrl::toPercentEncoding(fileName, "/")))
                : QString();
            rows.append(row);
        }
    }

    const auto hasRecommendation = [&rows](const QString &repo, const QString &fileName) {
        for (const QVariant &v : rows) {
            const QVariantMap row = v.toMap();
            if (row.value(QStringLiteral("repo")).toString() == repo &&
                row.value(QStringLiteral("fileName")).toString() == fileName)
                return true;
        }
        return false;
    };
    const auto appendCurated = [&](const RecommendedModelDef &m) {
        if (hasRecommendation(m.repo, m.fileName))
            return;
        QJsonObject model;
        model[QStringLiteral("name")] = m.name;
        model[QStringLiteral("architecture")] = m.family == QLatin1String("Qwen")
            ? QStringLiteral("qwen3_5")
            : m.family.toLower();
        model[QStringLiteral("parameter_count")] = m.params;
        model[QStringLiteral("parameters_raw")] = m.params.section(QLatin1Char('B'), 0, 0).toDouble() * 1000000000.0;
        model[QStringLiteral("use_case")] = m.capabilities;
        model[QStringLiteral("pipeline_tag")] = QStringLiteral("text-generation");
        model[QStringLiteral("hf_downloads")] = 0;

        const double paramsB = catalogParamsB(model);
        const int ctx = m.ctxK * 1024;
        const double requiredGb = m.sizeGb > 0 ? m.sizeGb : estimateCatalogMemoryGb(model, paramsB, m.quant, ctx);
        const QString caps = m.capabilities;
        const bool isMoe = caps.contains(QStringLiteral("moe"));
        const double activeB = isMoe ? moeActiveParamsFromName(m.name + QLatin1Char(' ') + m.params, paramsB) : paramsB;
        if (isMoe) {
            model[QStringLiteral("is_moe")] = true;
            model[QStringLiteral("active_parameters")] = activeB * 1000000000.0;
        }
        const double activeGpuGb = isMoe ? estimateMoeActiveGpuGb(model, activeB, m.quant, ctx) : requiredGb;
        const bool moeActiveFitsGpu = isMoe && vramGb > 0
            && activeGpuGb <= qMax(0.1, vramGb * 0.92)
            && requiredGb <= vramGb + ramGb * 0.9;
        QString runMode;
        QString fit;
        double gpuFraction = 1.0;
        const double usableRamGb = ramGb * 0.9;
        if (vramGb > 0 && requiredGb <= vramGb) {
            runMode = QStringLiteral("gpu");
            fit = QStringLiteral("Perfecto");
        } else if (moeActiveFitsGpu) {
            runMode = QStringLiteral("moe_active_gpu");
            gpuFraction = 0.9;
            fit = QStringLiteral("Razonable");
        } else if (vramGb > 0 && requiredGb <= vramGb + usableRamGb) {
            runMode = QStringLiteral("partial_offload");
            gpuFraction = qBound(0.0, vramGb / requiredGb, 1.0);
            fit = gpuFraction >= 0.6 ? QStringLiteral("Bueno") : QStringLiteral("Marginal");
        } else if (vramGb <= 0 && requiredGb <= usableRamGb) {
            runMode = QStringLiteral("cpu_only");
            gpuFraction = 0.0;
            fit = usableRamGb >= requiredGb * 1.2 ? QStringLiteral("Bueno") : QStringLiteral("Marginal");
        } else {
            runMode = QStringLiteral("no_fit");
            fit = QStringLiteral("No entra");
        }
        const double scoreRequiredGb = runMode == QLatin1String("moe_active_gpu") ? activeGpuGb : requiredGb;
        const double tps = runMode == QLatin1String("no_fit")
            ? 0.0
            : catalogSpeedTps(gpuName, activeB, scoreRequiredGb, runMode, gpuFraction);
        const int score = qMin(100, catalogScore(model, paramsB, scoreRequiredGb, ramGb, vramGb,
                                                 m.quant, caps, ctx, runMode, tps, -1.0, -1.0) + 8);

        QString runLabel = runMode;
        if (runMode == QLatin1String("gpu")) runLabel = QStringLiteral("GPU (todo en VRAM)");
        else if (runMode == QLatin1String("moe_active_gpu"))
            runLabel = QStringLiteral("MoE activo en GPU");
        else if (runMode == QLatin1String("partial_offload"))
            runLabel = QStringLiteral("VRAM+RAM (%1%% en GPU)").arg(qRound(gpuFraction * 100.0));
        else if (runMode == QLatin1String("cpu_only")) runLabel = QStringLiteral("CPU (todo en RAM)");
        else if (runMode == QLatin1String("no_fit")) runLabel = QStringLiteral("No entra");

        QVariantMap row;
        row[QStringLiteral("name")] = m.name;
        row[QStringLiteral("repo")] = m.repo;
        row[QStringLiteral("fileName")] = m.fileName;
        row[QStringLiteral("family")] = m.family;
        row[QStringLiteral("capabilities")] = m.capabilities;
        row[QStringLiteral("useCase")] = catalogUseCase(caps);
        row[QStringLiteral("params")] = m.params;
        row[QStringLiteral("quant")] = m.quant;
        row[QStringLiteral("sizeGb")] = requiredGb;
        row[QStringLiteral("requiredGb")] = requiredGb;
        row[QStringLiteral("activeGpuGb")] = activeGpuGb;
        row[QStringLiteral("minRamGb")] = m.minRamGb;
        row[QStringLiteral("recommendedRamGb")] = m.recommendedRamGb;
        row[QStringLiteral("minVramGb")] = m.minVramGb;
        row[QStringLiteral("ctxK")] = m.ctxK;
        row[QStringLiteral("context")] = ctx;
        const QString moeNote = runMode == QLatin1String("moe_active_gpu")
            ? QStringLiteral(" · activo ~%1 GB").arg(QString::number(activeGpuGb, 'f', 1))
            : QString();
        row[QStringLiteral("notes")] = QStringLiteral("%1 · %2 t/s est.%3 · ~%4 GB @ %5k ctx · %6")
            .arg(runLabel)
            .arg(QString::number(tps, 'f', 1))
            .arg(moeNote)
            .arg(QString::number(requiredGb, 'f', 1))
            .arg(m.ctxK)
            .arg(m.notes.isEmpty() ? QStringLiteral("curado") : m.notes);
        row[QStringLiteral("fit")] = fit;
        row[QStringLiteral("score")] = score;
        row[QStringLiteral("downloadable")] = true;
        row[QStringLiteral("downloadUrl")] = QStringLiteral("https://huggingface.co/%1/resolve/main/%2?download=true")
            .arg(m.repo, QString::fromLatin1(QUrl::toPercentEncoding(m.fileName, "/")));
        rows.append(row);
    };

    const QVector<RecommendedModelDef> curated = {
        {QStringLiteral("Qwen3.5 2B MTP"), QStringLiteral("unsloth/Qwen3.5-2B-MTP-GGUF"),
         QStringLiteral("Qwen3.5-2B-Q4_K_M.gguf"), QStringLiteral("Qwen"), QStringLiteral("chat,reasoning,tool_use,mtp"),
         QStringLiteral("2B"), QStringLiteral("Q4_K_M"), 0, 2, 6, 1.2, 32, QStringLiteral("curated tiny-gpu")},
        {QStringLiteral("Qwen2.5 7B Instruct"), QStringLiteral("bartowski/Qwen2.5-7B-Instruct-GGUF"),
         QStringLiteral("Qwen2.5-7B-Instruct-Q4_K_M.gguf"), QStringLiteral("Qwen"), QStringLiteral("chat,tool_use"),
         QStringLiteral("7.6B"), QStringLiteral("Q4_K_M"), 0, 5, 8, 4, 32, QStringLiteral("curated 8gb-agentic")},
        {QStringLiteral("Hermes 3 Llama 3.1 8B"), QStringLiteral("bartowski/Hermes-3-Llama-3.1-8B-GGUF"),
         QStringLiteral("Hermes-3-Llama-3.1-8B-IQ4_XS.gguf"), QStringLiteral("Llama"), QStringLiteral("chat,tool_use"),
         QStringLiteral("8B"), QStringLiteral("IQ4_XS"), 4.45, 5, 8, 4, 32, QStringLiteral("curated 8gb-tool-json")},
        {QStringLiteral("Qwen3.6 35B A3B Q3"), QStringLiteral("unsloth/Qwen3.6-35B-A3B-GGUF"),
         QStringLiteral("Qwen3.6-35B-A3B-UD-Q3_K_M.gguf"), QStringLiteral("Qwen"), QStringLiteral("chat,tool_use,moe"),
         QStringLiteral("35B A3B"), QStringLiteral("Q3_K_M"), 18.5, 16, 24, 8, 32, QStringLiteral("experimental moe 8gb")},
        {QStringLiteral("Qwen3.6 35B A3B Q2"), QStringLiteral("unsloth/Qwen3.6-35B-A3B-GGUF"),
         QStringLiteral("Qwen3.6-35B-A3B-UD-Q2_K_XL.gguf"), QStringLiteral("Qwen"), QStringLiteral("chat,tool_use,moe"),
         QStringLiteral("35B A3B"), QStringLiteral("Q2_K_XL"), 14.5, 12, 16, 6, 32, QStringLiteral("experimental moe 8gb")},
        {QStringLiteral("Qwen3.5 9B"), QStringLiteral("unsloth/Qwen3.5-9B-GGUF"),
         QStringLiteral("Qwen3.5-9B-Q4_K_M.gguf"), QStringLiteral("Qwen"), QStringLiteral("chat,reasoning,tool_use"),
         QStringLiteral("9B"), QStringLiteral("Q4_K_M"), 0, 8, 16, 6, 32, QStringLiteral("curated")},
        {QStringLiteral("Qwen3.5 9B MTP"), QStringLiteral("unsloth/Qwen3.5-9B-MTP-GGUF"),
         QStringLiteral("Qwen3.5-9B-MTP-Q4_K_M.gguf"), QStringLiteral("Qwen"), QStringLiteral("chat,reasoning,tool_use,mtp"),
         QStringLiteral("9.7B"), QStringLiteral("Q4_K_M"), 0, 10, 20, 7, 32, QStringLiteral("curated")},
        {QStringLiteral("Qwen3 8B"), QStringLiteral("Qwen/Qwen3-8B-GGUF"),
         QStringLiteral("Qwen3-8B-Q4_K_M.gguf"), QStringLiteral("Qwen"), QStringLiteral("chat,reasoning,tool_use"),
         QStringLiteral("8B"), QStringLiteral("Q4_K_M"), 0, 8, 16, 6, 32, QStringLiteral("curated")}
    };
    for (const RecommendedModelDef &m : curated)
        appendCurated(m);

    if (rows.isEmpty()) {
        const QVector<RecommendedModelDef> fallback = {
            {QStringLiteral("Qwen3 Coder 30B A3B Instruct"), QStringLiteral("unsloth/Qwen3-Coder-30B-A3B-Instruct-GGUF"),
             QStringLiteral("Qwen3-Coder-30B-A3B-Instruct-Q4_K_M.gguf"), QStringLiteral("Qwen"), QStringLiteral("code,moe"),
             QStringLiteral("30B MoE"), QStringLiteral("Q4_K_M"), 18.5, 48, 64, 18, 256, QStringLiteral("fallback local")},
            {QStringLiteral("Qwen2.5 Coder 32B Instruct"), QStringLiteral("bartowski/Qwen2.5-Coder-32B-Instruct-GGUF"),
             QStringLiteral("Qwen2.5-Coder-32B-Instruct-Q4_K_M.gguf"), QStringLiteral("Qwen"), QStringLiteral("code,chat"),
             QStringLiteral("32B"), QStringLiteral("Q4_K_M"), 20.0, 48, 64, 24, 32, QStringLiteral("fallback local")}
        };
        for (const RecommendedModelDef &m : fallback) {
            const QString fit = (ramGb >= m.recommendedRamGb && (vramGb <= 0 || vramGb >= m.minVramGb))
                ? QStringLiteral("Perfecto")
                : (ramGb >= m.minRamGb ? QStringLiteral("Bueno") : QStringLiteral("Marginal"));
            const int score = qBound(0, int(70 + m.sizeGb), 100);
            QVariantMap row;
            row[QStringLiteral("name")] = m.name;
            row[QStringLiteral("repo")] = m.repo;
            row[QStringLiteral("fileName")] = m.fileName;
            row[QStringLiteral("family")] = m.family;
            row[QStringLiteral("capabilities")] = m.capabilities;
            row[QStringLiteral("useCase")] = catalogUseCase(m.capabilities);
            row[QStringLiteral("recommendationLane")] = recommendationLane(m.capabilities, QStringLiteral("partial_offload"), 30.0);
            row[QStringLiteral("params")] = m.params;
            row[QStringLiteral("quant")] = m.quant;
            row[QStringLiteral("sizeGb")] = m.sizeGb;
            row[QStringLiteral("ctxK")] = m.ctxK;
            row[QStringLiteral("notes")] = m.notes;
            row[QStringLiteral("fit")] = fit;
            row[QStringLiteral("score")] = score;
            row[QStringLiteral("downloadable")] = true;
            rows.append(row);
        }
    }

    std::sort(rows.begin(), rows.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap ma = a.toMap();
        const QVariantMap mb = b.toMap();
        const int sa = ma.value(QStringLiteral("score")).toInt();
        const int sb = mb.value(QStringLiteral("score")).toInt();
        if (sa != sb)
            return sa > sb;
        // Tie → newer version first (Qwen3.6 > Qwen3.5)
        return catalogVersionKey(ma.value(QStringLiteral("name")).toString()) >
               catalogVersionKey(mb.value(QStringLiteral("name")).toString());
    });

    m_modelRecommendations = rows;
    emit modelRecommendationsChanged();
}

// ── Model quality benchmarks (Artificial Analysis Intelligence Index) ───────────

// Endpoint for the weekly live refresh. Expected JSON is either our own shape
// ({"models": {key: quality0to100}}) or an AA-style list
// ({"data": [{"name": "...", "intelligence_index": <0..70>}]}). Any failure is
// silently ignored — the bundled table stays in effect.
static const char *kBenchmarkFetchUrl = "https://artificialanalysis.ai/api/v2/data/llms/models";

QString AppController::benchmarkCachePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    return QDir(dir).filePath(QStringLiteral("benchmarks/aa_intelligence.json"));
}

void AppController::loadBenchmarkScores()
{
    m_benchmarkQuality.clear();
    m_cookbookPriority.clear();

    const auto ingest = [this](const QByteArray &bytes) {
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        // Our shape: {"models": {key: quality}}
        const QJsonObject models = root.value(QStringLiteral("models")).toObject();
        for (auto it = models.begin(); it != models.end(); ++it) {
            const double q = it.value().toDouble(-1);
            if (q >= 0)
                m_benchmarkQuality.insert(it.key().trimmed().toLower(), q);
        }
        // AA-style shape: {"data": [{"name", "intelligence_index"}]} — remap the
        // 0..~70 index onto a 0..100 quality and key by the normalized name.
        const QJsonArray data = root.value(QStringLiteral("data")).toArray();
        for (const QJsonValue &v : data) {
            const QJsonObject o = v.toObject();
            const QString name = o.value(QStringLiteral("name")).toString();
            const double idx = o.value(QStringLiteral("intelligence_index")).toDouble(-1);
            if (name.isEmpty() || idx < 0)
                continue;
            m_benchmarkQuality.insert(benchmarkKey(name), qBound(0.0, idx * (100.0 / 70.0), 100.0));
        }
    };
    const auto ingestPriority = [this](const QByteArray &bytes) {
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        const QJsonObject models = root.value(QStringLiteral("models")).toObject();
        for (auto it = models.begin(); it != models.end(); ++it) {
            const double q = it.value().toDouble(-1);
            if (q >= 0)
                m_cookbookPriority.insert(it.key().trimmed().toLower(), q);
        }
    };

    // Bundled table first (offline floor)…
    QFile bundled(QStringLiteral(":/assets/benchmarks/aa_intelligence.json"));
    if (bundled.open(QIODevice::ReadOnly))
        ingest(bundled.readAll());

    QFile localPriorities(QStringLiteral(":/assets/benchmarks/local_cookbook_priorities.json"));
    if (localPriorities.open(QIODevice::ReadOnly))
        ingestPriority(localPriorities.readAll());

    // …then overlay the fetched cache when present (fresher data wins).
    QFile cache(benchmarkCachePath());
    if (cache.open(QIODevice::ReadOnly))
        ingest(cache.readAll());

    m_benchmarkLoaded = true;
}

void AppController::maybeFetchBenchmarks()
{
    if (m_benchmarkFetchReply)
        return; // already in flight

    // Weekly cadence: skip if the cache exists and is younger than 7 days.
    const QFileInfo cacheInfo(benchmarkCachePath());
    if (cacheInfo.exists() && cacheInfo.lastModified().daysTo(QDateTime::currentDateTime()) < 7)
        return;

    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    QNetworkRequest req{QUrl(QString::fromLatin1(kBenchmarkFetchUrl))};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LlamaCode/1.0"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_benchmarkFetchReply = m_nam->get(req);

    connect(m_benchmarkFetchReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_benchmarkFetchReply;
        m_benchmarkFetchReply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return; // keep bundled table

        const QByteArray bytes = reply->readAll();
        if (QJsonDocument::fromJson(bytes).isNull())
            return; // not JSON — ignore

        // Persist to cache, then reload so the overlay takes effect immediately.
        const QString path = benchmarkCachePath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile out(path);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            out.write(bytes);
            out.close();
            loadBenchmarkScores();
            rebuildModelRecommendations();
        }
    });
}

QString AppController::modelDownloadDir() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                  + QStringLiteral("/models");
    QDir().mkpath(dir);
    return dir;
}

void AppController::openModelRecommendation(const QString &repo)
{
    if (repo.trimmed().isEmpty()) return;
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://huggingface.co/%1").arg(repo)));
}

QVariantMap AppController::modelDownloadItemToMap(const ModelDownloadItem &item) const
{
    QVariantMap m;
    m[QStringLiteral("id")] = item.id;
    m[QStringLiteral("repo")] = item.repo;
    m[QStringLiteral("fileName")] = item.fileName;
    m[QStringLiteral("path")] = item.outPath;
    m[QStringLiteral("state")] = item.state;
    m[QStringLiteral("status")] = item.status;
    m[QStringLiteral("progress")] = item.progress;
    m[QStringLiteral("receivedMb")] = item.received / 1024 / 1024;
    m[QStringLiteral("totalMb")] = item.total / 1024 / 1024;
    m[QStringLiteral("active")] = item.id == m_activeModelDownloadId;
    return m;
}

QVariantList AppController::modelDownloadQueue() const
{
    QVariantList out;
    for (const ModelDownloadItem &item : m_modelDownloadQueue)
        out.append(modelDownloadItemToMap(item));
    return out;
}

int AppController::modelDownloadIndexById(const QString &id) const
{
    for (int i = 0; i < m_modelDownloadQueue.size(); ++i)
        if (m_modelDownloadQueue[i].id == id)
            return i;
    return -1;
}

void AppController::emitModelDownloadChanged()
{
    m_modelDownloadProgress = 0;
    m_modelDownloadStatus.clear();
    if (m_modelDownloadReply && !m_activeModelDownloadId.isEmpty()) {
        const int idx = modelDownloadIndexById(m_activeModelDownloadId);
        if (idx >= 0) {
            m_modelDownloadProgress = m_modelDownloadQueue[idx].progress;
            m_modelDownloadStatus = m_modelDownloadQueue[idx].status;
            m_modelDownloadPath = m_modelDownloadQueue[idx].outPath;
        }
    } else if (!m_modelDownloadQueue.isEmpty()) {
        const ModelDownloadItem &item = m_modelDownloadQueue.first();
        m_modelDownloadProgress = item.progress;
        m_modelDownloadStatus = item.status;
        m_modelDownloadPath = item.outPath;
    }
    emit modelDownloadChanged();
}

void AppController::scanModelDownloadRoot()
{
    const QString dir = modelDownloadDir();
    QString existingRootId;
    for (int i = 0; i < m_roots.rowCount(); ++i) {
        const QModelIndex mi = m_roots.index(i, 0);
        if (QDir::cleanPath(m_roots.data(mi, ModelRootRegistry::PathRole).toString()) == QDir::cleanPath(dir)) {
            existingRootId = m_roots.data(mi, ModelRootRegistry::IdRole).toString();
            break;
        }
    }
    if (existingRootId.isEmpty())
        m_roots.add(dir, QStringLiteral("Modelos descargados"), QStringLiteral("startup"), {});
    else
        m_roots.scan(existingRootId);
}

QString AppController::createRecommendedLaunchProfile()
{
    if (m_binaries.count() <= 0) {
        emit serverError(QStringLiteral("Instalá o registrá un binario llama-server primero."));
        return {};
    }
    if (m_catalog.count() <= 0) {
        emit serverError(QStringLiteral("Descargá o registrá un modelo GGUF primero."));
        return {};
    }

    const QModelIndex binIndex = m_binaries.index(0, 0);
    const QString binaryId = m_binaries.data(binIndex, BinaryRegistry::IdRole).toString();
    const QString binaryName = m_binaries.data(binIndex, BinaryRegistry::NameRole).toString();

    QString modelId;
    QString modelName;
    for (int row = 0; row < m_catalog.rowCount(); ++row) {
        const QVariantMap m = m_catalog.getAt(row);
        if (!m.value(QStringLiteral("isAvailable"), true).toBool())
            continue;
        modelId = m.value(QStringLiteral("id")).toString();
        modelName = m.value(QStringLiteral("fileName")).toString();
        break;
    }
    if (binaryId.isEmpty() || modelId.isEmpty()) {
        emit serverError(QStringLiteral("No se pudo resolver binario/modelo para crear el perfil."));
        return {};
    }

    QString base = QFileInfo(modelName).completeBaseName();
    base.remove(QRegularExpression(QStringLiteral("[-_](Q[0-9].*|IQ[0-9].*|BF16|F16|F32)$"),
                                   QRegularExpression::CaseInsensitiveOption));
    if (base.isEmpty())
        base = QStringLiteral("Perfil local");

    const QString backendId = m_profiles.addBackend(
        QStringLiteral("%1 · Backend").arg(base),
        binaryId,
        QStringLiteral("127.0.0.1"),
        8080);
    const QString modelProfileId = m_profiles.addModelProfile(
        QStringLiteral("%1 · Model").arg(base),
        modelId,
        QString(),
        QString());
    const QString runtimeId = m_profiles.addRuntimePreset(
        QStringLiteral("%1 · Runtime").arg(base),
        32768,
        512,
        -1,
        true,
        true);
    const QString launchId = m_profiles.addLaunchProfile(base, backendId, modelProfileId, runtimeId);
    if (launchId.isEmpty()) {
        emit serverError(QStringLiteral("No se pudo crear el perfil de lanzamiento."));
        return {};
    }

    writeSetting(QStringLiteral("lastLaunchId"), launchId);
    computeEffectiveProfile(launchId);
    appendServerEvent(QStringLiteral("lifecycle"),
                      QStringLiteral("Perfil inicial creado: %1 con %2.").arg(base, binaryName));
    emit setupStateChanged();
    emit activeLaunchIdChanged();
    return launchId;
}

void AppController::downloadRecommendedModel(const QString &repo, const QString &fileName)
{
    const QString cleanRepo = repo.trimmed();
    const QString cleanFile = fileName.trimmed();
    if (cleanRepo.isEmpty() || cleanFile.isEmpty())
        return;

    const QString dir = modelDownloadDir();
    const QString outPath = dir + QLatin1Char('/') + QFileInfo(cleanFile).fileName();
    const QString partPath = outPath + QStringLiteral(".part");

    for (const ModelDownloadItem &item : std::as_const(m_modelDownloadQueue)) {
        if (item.outPath == outPath) {
            m_modelDownloadStatus = QStringLiteral("Ya está en la cola: %1").arg(QFileInfo(outPath).fileName());
            emitModelDownloadChanged();
            return;
        }
    }

    ModelDownloadItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.repo = cleanRepo;
    item.fileName = cleanFile;
    item.outPath = outPath;
    item.partPath = partPath;
    item.state = QStringLiteral("queued");
    item.status = QStringLiteral("En cola: %1").arg(QFileInfo(cleanFile).fileName());
    item.resolvedFileName = false;
    m_modelDownloadQueue.append(item);
    emitModelDownloadChanged();
    startNextModelDownload();
}

void AppController::startNextModelDownload()
{
    if (m_modelDownloadReply)
        return;
    for (int i = 0; i < m_modelDownloadQueue.size(); ++i) {
        if (m_modelDownloadQueue[i].state == QLatin1String("queued")) {
            startModelDownload(i);
            return;
        }
    }
    m_activeModelDownloadId.clear();
    emitModelDownloadChanged();
}

void AppController::startModelDownload(int index)
{
    if (index < 0 || index >= m_modelDownloadQueue.size() || m_modelDownloadReply)
        return;
    if (!m_nam) m_nam = new QNetworkAccessManager(this);

    ModelDownloadItem &item = m_modelDownloadQueue[index];
    item.pauseRequested = false;
    item.cancelRequested = false;
    item.progress = 0;
    item.received = QFileInfo(item.partPath).exists() ? QFileInfo(item.partPath).size() : 0;
    item.total = 0;
    item.resumeOffset = 0;

    if (!item.resolvedFileName) {
        item.state = QStringLiteral("resolving");
        item.status = QStringLiteral("Buscando GGUF real en Hugging Face...");
        m_activeModelDownloadId = item.id;
        emitModelDownloadChanged();

        QNetworkRequest apiReq(huggingFaceApiUrl(item.repo));
        apiReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        apiReq.setTransferTimeout(30000);
        m_modelDownloadReply = m_nam->get(apiReq);
        connect(m_modelDownloadReply, &QNetworkReply::finished, this, [this]() {
            QNetworkReply *reply = m_modelDownloadReply;
            m_modelDownloadReply = nullptr;
            const QString id = m_activeModelDownloadId;
            const int idx = modelDownloadIndexById(id);
            if (idx < 0) {
                if (reply) reply->deleteLater();
                startNextModelDownload();
                return;
            }

            ModelDownloadItem &cur = m_modelDownloadQueue[idx];
            if (cur.pauseRequested) {
                cur.state = QStringLiteral("paused");
                cur.status = QStringLiteral("Pausada: %1").arg(QFileInfo(cur.fileName).fileName());
                m_activeModelDownloadId.clear();
                if (reply) reply->deleteLater();
                emitModelDownloadChanged();
                startNextModelDownload();
                return;
            }
            if (cur.cancelRequested) {
                QFile::remove(cur.partPath);
                m_modelDownloadQueue.removeAt(idx);
                m_activeModelDownloadId.clear();
                if (reply) reply->deleteLater();
                emitModelDownloadChanged();
                startNextModelDownload();
                return;
            }

            QStringList siblings;
            const bool ok = reply && reply->error() == QNetworkReply::NoError;
            if (ok) {
                const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
                const QJsonArray files = root.value(QStringLiteral("siblings")).toArray();
                for (const QJsonValue &v : files) {
                    const QString name = v.toObject().value(QStringLiteral("rfilename")).toString();
                    if (!name.isEmpty())
                        siblings.append(name);
                }
            }
            const QString chosen = chooseGgufSibling(siblings, cur.fileName);
            const QString oldFile = cur.fileName;
            if (!chosen.isEmpty() && chosen != cur.fileName) {
                cur.fileName = chosen;
                cur.outPath = modelDownloadDir() + QLatin1Char('/') + QFileInfo(chosen).fileName();
                cur.partPath = cur.outPath + QStringLiteral(".part");
                cur.status = QStringLiteral("Archivo real: %1").arg(QFileInfo(chosen).fileName());
            } else if (!ok) {
                cur.status = QStringLiteral("No se pudo consultar Hugging Face; usando %1").arg(QFileInfo(cur.fileName).fileName());
            } else {
                cur.status = QStringLiteral("Archivo confirmado: %1").arg(QFileInfo(cur.fileName).fileName());
            }
            cur.resolvedFileName = true;
            cur.state = QStringLiteral("queued");
            m_activeModelDownloadId.clear();
            if (reply) reply->deleteLater();

            if (oldFile != cur.fileName) {
                for (int i = 0; i < m_modelDownloadQueue.size(); ++i) {
                    if (i != idx && m_modelDownloadQueue.at(i).outPath == cur.outPath) {
                        cur.state = QStringLiteral("error");
                        cur.status = QStringLiteral("Ya está en la cola: %1").arg(QFileInfo(cur.outPath).fileName());
                        emitModelDownloadChanged();
                        startNextModelDownload();
                        return;
                    }
                }
            }

            emitModelDownloadChanged();
            QTimer::singleShot(0, this, [this]() { startNextModelDownload(); });
        });
        return;
    }

    item.state = QStringLiteral("verifying");
    item.status = QStringLiteral("Verificando %1...").arg(QFileInfo(item.fileName).fileName());
    m_activeModelDownloadId = item.id;
    emitModelDownloadChanged();

    const QUrl url = huggingFaceResolveUrl(item.repo, item.fileName);

    if (QFileInfo::exists(item.outPath)) {
        QNetworkRequest headReq(url);
        headReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        headReq.setTransferTimeout(0);

        m_modelDownloadReply = m_nam->head(headReq);
        connect(m_modelDownloadReply, &QNetworkReply::finished, this, [this, url]() {
            QNetworkReply *reply = m_modelDownloadReply;
            m_modelDownloadReply = nullptr;
            const QString id = m_activeModelDownloadId;
            const int idx = modelDownloadIndexById(id);
            if (idx < 0) {
                if (reply) reply->deleteLater();
                startNextModelDownload();
                return;
            }
            ModelDownloadItem &cur = m_modelDownloadQueue[idx];

            if (cur.pauseRequested) {
                cur.state = QStringLiteral("paused");
                cur.status = QStringLiteral("Pausada: %1").arg(QFileInfo(cur.fileName).fileName());
                m_activeModelDownloadId.clear();
                emitModelDownloadChanged();
                startNextModelDownload();
                return;
            }
            if (cur.cancelRequested) {
                QFile::remove(cur.partPath);
                m_modelDownloadQueue.removeAt(idx);
                m_activeModelDownloadId.clear();
                emitModelDownloadChanged();
                startNextModelDownload();
                return;
            }

            const qint64 localBytes = QFileInfo(cur.outPath).size();
            const qint64 remoteBytes = reply
                ? reply->header(QNetworkRequest::ContentLengthHeader).toLongLong()
                : -1;
            const bool checkedComplete = remoteBytes > 0 && localBytes >= remoteBytes;
            const bool suspiciousSmall = localBytes > 0 && localBytes < 256LL * 1024LL * 1024LL;

            if (reply)
                reply->deleteLater();

            if (checkedComplete || (remoteBytes <= 0 && !suspiciousSmall)) {
                QFile::remove(cur.partPath);
                cur.progress = 100;
                cur.received = localBytes;
                cur.total = remoteBytes > 0 ? remoteBytes : localBytes;
                finishModelDownloadItem(id, QStringLiteral("done"),
                                        QStringLiteral("Ya existe: %1").arg(cur.outPath), 100, false);
                scanModelDownloadRoot();
                return;
            }

            QFile::remove(cur.outPath);
            cur.status = remoteBytes > 0
                ? QStringLiteral("Archivo incompleto (%1/%2 MB). Reiniciando descarga...")
                    .arg(localBytes / 1024 / 1024)
                    .arg(remoteBytes / 1024 / 1024)
                : QStringLiteral("Archivo incompleto eliminado. Reiniciando descarga...");
            cur.state = QStringLiteral("queued");
            emitModelDownloadChanged();
            QTimer::singleShot(0, this, [this]() { startNextModelDownload(); });
        });
        return;
    }

    const qint64 resumeOffset = QFileInfo(item.partPath).exists() ? QFileInfo(item.partPath).size() : 0;
    auto *file = new QFile(item.partPath, this);
    if (!file->open(resumeOffset > 0 ? (QIODevice::WriteOnly | QIODevice::Append) : QIODevice::WriteOnly)) {
        file->deleteLater();
        finishModelDownloadItem(item.id, QStringLiteral("error"),
                                QStringLiteral("No se pudo escribir el modelo en %1").arg(item.partPath),
                                0, false);
        return;
    }

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(0);
    if (resumeOffset > 0)
        req.setRawHeader("Range", QString("bytes=%1-").arg(resumeOffset).toLatin1());

    m_modelDownloadFile = file;
    item.resumeOffset = resumeOffset;
    item.received = resumeOffset;
    item.state = QStringLiteral("downloading");
    item.status = resumeOffset > 0
        ? QStringLiteral("Reanudando %1...").arg(QFileInfo(item.fileName).fileName())
        : QStringLiteral("Descargando %1...").arg(QFileInfo(item.fileName).fileName());
    emitModelDownloadChanged();

    m_modelDownloadReply = m_nam->get(req);
    connect(m_modelDownloadReply, &QNetworkReply::metaDataChanged, this, [this]() {
        const int idx = modelDownloadIndexById(m_activeModelDownloadId);
        if (idx < 0 || !m_modelDownloadReply || !m_modelDownloadFile)
            return;
        ModelDownloadItem &cur = m_modelDownloadQueue[idx];
        const int status = m_modelDownloadReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (cur.resumeOffset > 0 && status == 200) {
            // El servidor ignoró Range: reiniciar el .part para no duplicar bytes.
            m_modelDownloadFile->resize(0);
            cur.resumeOffset = 0;
            cur.received = 0;
        }
    });
    connect(m_modelDownloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_modelDownloadFile && m_modelDownloadReply)
            m_modelDownloadFile->write(m_modelDownloadReply->readAll());
    });
    connect(m_modelDownloadReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const int idx = modelDownloadIndexById(m_activeModelDownloadId);
        if (idx < 0) return;
        ModelDownloadItem &cur = m_modelDownloadQueue[idx];
        const qint64 base = cur.resumeOffset;
        const qint64 fullReceived = base + qMax<qint64>(0, received);
        const qint64 fullTotal = total > 0 ? base + total : 0;
        cur.received = fullReceived;
        cur.total = fullTotal;
        if (fullTotal > 0)
            cur.progress = qBound(0, int((fullReceived * 100) / fullTotal), 100);
        cur.status = fullTotal > 0
            ? QStringLiteral("Descargando modelo... %1/%2 MB")
                .arg(fullReceived / 1024 / 1024).arg(fullTotal / 1024 / 1024)
            : QStringLiteral("Descargando modelo... %1 MB").arg(fullReceived / 1024 / 1024);
        emitModelDownloadChanged();
    });
    connect(m_modelDownloadReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_modelDownloadReply;
        QFile *file = m_modelDownloadFile;
        m_modelDownloadReply = nullptr;
        m_modelDownloadFile = nullptr;
        const QString id = m_activeModelDownloadId;
        m_activeModelDownloadId.clear();

        const int httpStatus = reply
            ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            : 0;
        const bool httpOk = httpStatus == 0 || (httpStatus >= 200 && httpStatus < 400);
        const bool ok = reply && reply->error() == QNetworkReply::NoError && httpOk;
        const QString err = reply ? reply->errorString() : QStringLiteral("respuesta inválida");
        if (file) {
            file->write(reply ? reply->readAll() : QByteArray());
            file->close();
            file->deleteLater();
        }
        if (reply)
            reply->deleteLater();

        const int idx = modelDownloadIndexById(id);
        if (idx < 0) {
            startNextModelDownload();
            return;
        }
        const ModelDownloadItem cur = m_modelDownloadQueue[idx];

        if (cur.pauseRequested) {
            m_modelDownloadQueue[idx].state = QStringLiteral("paused");
            m_modelDownloadQueue[idx].status = QStringLiteral("Pausada: %1").arg(QFileInfo(cur.fileName).fileName());
            emitModelDownloadChanged();
            startNextModelDownload();
            return;
        }
        if (cur.cancelRequested) {
            QFile::remove(cur.partPath);
            m_modelDownloadQueue.removeAt(idx);
            emitModelDownloadChanged();
            startNextModelDownload();
            return;
        }

        if (!ok) {
            QFile::remove(cur.partPath);
            const QString detail = httpStatus > 0
                ? QStringLiteral("%1 (HTTP %2)").arg(err).arg(httpStatus)
                : err;
            finishModelDownloadItem(id, QStringLiteral("error"),
                                    QStringLiteral("Error descargando modelo: %1").arg(detail), 0, false);
            emit serverError(m_modelDownloadStatus);
            return;
        }

        QFile::remove(cur.outPath);
        if (!QFile::rename(cur.partPath, cur.outPath)) {
            QFile::remove(cur.partPath);
            finishModelDownloadItem(id, QStringLiteral("error"),
                                    QStringLiteral("No se pudo finalizar el modelo: %1").arg(cur.outPath), 0, false);
            emit serverError(m_modelDownloadStatus);
            return;
        }

        finishModelDownloadItem(id, QStringLiteral("done"),
                                QStringLiteral("Modelo descargado: %1").arg(cur.outPath), 100, false);
        scanModelDownloadRoot();
        emit setupStateChanged();
    });
}

void AppController::finishModelDownloadItem(const QString &id, const QString &state,
                                            const QString &status, int progress, bool removePart)
{
    const int idx = modelDownloadIndexById(id);
    if (idx < 0) {
        startNextModelDownload();
        return;
    }
    ModelDownloadItem &item = m_modelDownloadQueue[idx];
    item.state = state;
    item.status = status;
    if (progress >= 0)
        item.progress = progress;
    if (removePart)
        QFile::remove(item.partPath);
    emitModelDownloadChanged();
    startNextModelDownload();
}

void AppController::pauseModelDownload(const QString &id)
{
    const int idx = modelDownloadIndexById(id);
    if (idx < 0) return;
    ModelDownloadItem &item = m_modelDownloadQueue[idx];
    if (item.id == m_activeModelDownloadId && m_modelDownloadReply) {
        item.pauseRequested = true;
        item.status = QStringLiteral("Pausando %1...").arg(QFileInfo(item.fileName).fileName());
        emitModelDownloadChanged();
        m_modelDownloadReply->abort();
        return;
    }
    if (item.state == QLatin1String("queued")) {
        item.state = QStringLiteral("paused");
        item.status = QStringLiteral("Pausada: %1").arg(QFileInfo(item.fileName).fileName());
        emitModelDownloadChanged();
    }
}

void AppController::resumeModelDownload(const QString &id)
{
    const int idx = modelDownloadIndexById(id);
    if (idx < 0) return;
    ModelDownloadItem &item = m_modelDownloadQueue[idx];
    if (item.state != QLatin1String("paused") && item.state != QLatin1String("error"))
        return;
    item.state = QStringLiteral("queued");
    item.pauseRequested = false;
    item.cancelRequested = false;
    item.status = QStringLiteral("En cola: %1").arg(QFileInfo(item.fileName).fileName());
    emitModelDownloadChanged();
    startNextModelDownload();
}

void AppController::cancelModelDownload(const QString &id)
{
    const int idx = modelDownloadIndexById(id);
    if (idx < 0) return;
    ModelDownloadItem &item = m_modelDownloadQueue[idx];
    if (item.id == m_activeModelDownloadId && m_modelDownloadReply) {
        item.cancelRequested = true;
        item.status = QStringLiteral("Cancelando %1...").arg(QFileInfo(item.fileName).fileName());
        emitModelDownloadChanged();
        m_modelDownloadReply->abort();
        return;
    }
    QFile::remove(item.partPath);
    m_modelDownloadQueue.removeAt(idx);
    emitModelDownloadChanged();
}

void AppController::moveModelDownload(const QString &id, int delta)
{
    if (delta == 0) return;
    const int idx = modelDownloadIndexById(id);
    if (idx < 0) return;
    const int target = qBound(0, idx + (delta < 0 ? -1 : 1), m_modelDownloadQueue.size() - 1);
    if (idx == target) return;
    if (m_modelDownloadQueue[idx].id == m_activeModelDownloadId
        || m_modelDownloadQueue[target].id == m_activeModelDownloadId) {
        return;
    }
    m_modelDownloadQueue.move(idx, target);
    emitModelDownloadChanged();
}

void AppController::clearBenchmarkResults()
{
    // Delete persisted index + per-result files
    const QString dir = benchmarkStorageDir();
    QFile fi(dir + "/index.json");
    if (fi.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(fi.readAll()).array();
        fi.close();
        for (const QJsonValue &v : arr)
            QFile::remove(dir + "/" + v.toObject().value("id").toString() + ".json");
    }
    QFile::remove(dir + "/index.json");

    m_benchmarkResults.clear();
    emit benchmarkResultsChanged();
}

void AppController::removeBenchmarkResult(int index)
{
    if (index < 0 || index >= m_benchmarkResults.size()) return;

    const QString id = m_benchmarkResults.at(index).toMap().value("id").toString();
    m_benchmarkResults.removeAt(index);
    emit benchmarkResultsChanged();

    if (id.isEmpty()) return;

    // Remove from persisted index + delete its file
    const QString dir = benchmarkStorageDir();
    const QString idxPath = dir + "/index.json";
    QFile fi(idxPath);
    if (fi.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(fi.readAll()).array();
        fi.close();
        QJsonArray kept;
        for (const QJsonValue &v : arr)
            if (v.toObject().value("id").toString() != id)
                kept.append(v);
        if (fi.open(QIODevice::WriteOnly | QIODevice::Truncate))
            fi.write(QJsonDocument(kept).toJson());
    }
    QFile::remove(dir + "/" + id + ".json");
}

void AppController::removeBenchmarkResultById(const QString &id)
{
    if (id.isEmpty()) return;
    for (int i = 0; i < m_benchmarkResults.size(); ++i) {
        if (m_benchmarkResults.at(i).toMap().value(QStringLiteral("id")).toString() == id) {
            removeBenchmarkResult(i);
            return;
        }
    }
}

void AppController::cancelBenchmark()
{
    if (!m_benchmarkRunning) return;
    m_benchmarkCanceled = true;
    m_benchmarkStatus = "Cancelando...";
    emit benchmarkStatusChanged();
    // Abort the in-flight HTTP request (health poll / warm-up / task) so the
    // current callback fires immediately instead of waiting for it to finish.
    if (m_benchmarkActiveReply)
        m_benchmarkActiveReply->abort();
    // Stop the headless agent (agent target) so its turn ends promptly.
    if (m_benchmarkAgent)
        m_benchmarkAgent->cancelGeneration();
    // Tear down the server now; the chain finalizes at the next checkpoint.
    stopServer();
}

// ── Heurísticas para benchmarks personalizados ───────────────────────────────

// "Solo código": la respuesta es esencialmente código, sin prosa explicativa.
// Heurística: si hay fences ```…```, mide cuánto texto queda fuera de ellos;
// si no hay fences, mira si las líneas parecen código (indentación / símbolos).
static bool benchCodeOnly(const QString &resp)
{
    const QString r = resp.trimmed();
    if (r.isEmpty()) return false;

    static const QRegularExpression fence(QStringLiteral("```[^\\n]*\\n.*?```"),
                                          QRegularExpression::DotMatchesEverythingOption);
    QString outside = r;
    bool hadFence = false;
    auto it = fence.globalMatch(r);
    while (it.hasNext()) { hadFence = true; outside.remove(it.next().captured(0)); }

    if (hadFence) {
        // Prosa fuera de los bloques de código → no es "solo código".
        const QString stray = outside.trimmed();
        return stray.length() <= 40;   // tolera algún "Here:" mínimo
    }

    // Sin fences: heurística de líneas-código.
    const QStringList lines = r.split('\n', Qt::SkipEmptyParts);
    int codey = 0;
    static const QRegularExpression codeHint(
        QStringLiteral("(^\\s+|[;{}=()\\[\\]]|\\b(def|class|import|from|return|for|while|if|var|let|const|function)\\b)"));
    for (const QString &l : lines)
        if (codeHint.match(l).hasMatch()) codey++;
    return lines.size() > 0 && (double)codey / lines.size() >= 0.7;
}

// Dependencias Python: extrae imports top-level y marca las que no son stdlib.
// No verifica existencia real en PyPI (offline) → "invented" = no-stdlib.
static QVariantMap benchPythonDeps(const QString &resp)
{
    static const QSet<QString> stdlib = {
        "abc","argparse","array","asyncio","base64","bisect","calendar","collections",
        "contextlib","copy","csv","datetime","decimal","enum","functools","glob","gzip",
        "hashlib","heapq","hmac","html","http","io","itertools","json","logging","math",
        "multiprocessing","os","pathlib","pickle","queue","random","re","shutil","signal",
        "socket","sqlite3","statistics","string","struct","subprocess","sys","tempfile",
        "textwrap","threading","time","timeit","traceback","types","typing","unittest",
        "urllib","uuid","warnings","weakref","xml","zipfile","zlib","dataclasses","secrets",
        "inspect","operator","platform","getpass","shlex","fnmatch","difflib","pprint"
    };
    static const QRegularExpression imp(
        QStringLiteral("(?m)^\\s*(?:import\\s+([A-Za-z_][\\w]*)|from\\s+([A-Za-z_][\\w]*))"));

    QSet<QString> nonStd;
    QSet<QString> all;
    auto it = imp.globalMatch(resp);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString mod = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
        if (mod.isEmpty()) continue;
        all.insert(mod);
        if (!stdlib.contains(mod)) nonStd.insert(mod);
    }
    QStringList list(nonStd.begin(), nonStd.end());
    list.sort();
    return QVariantMap{
        {"hasCode", !all.isEmpty()},
        {"invented", !nonStd.isEmpty()},
        {"count", (int)nonStd.size()},
        {"list", list}
    };
}

// Convert user-defined custom tasks (QVariantList of maps) into BenchTaskDef.
// Custom tasks have no auto-eval; quality tasks just record the response.
static QVector<BenchTaskDef> buildCustomBenchTasks(const QVariantList &custom)
{
    QVector<BenchTaskDef> t;
    int n = 0;
    for (const QVariant &v : custom) {
        const QVariantMap m = v.toMap();
        const QString prompt = m.value("prompt").toString();
        if (prompt.trimmed().isEmpty()) continue;
        BenchTaskDef d;
        d.id        = m.value("id").toString();
        if (d.id.isEmpty()) d.id = QStringLiteral("task_%1").arg(++n);
        d.isSpeed   = m.value("isSpeed", true).toBool();
        d.category  = m.value("category", d.isSpeed ? "speed" : "custom").toString();
        d.prompt    = prompt;
        d.maxTokens = m.value("maxTokens", 8192).toInt();
        d.eval      = nullptr;
        d.acceptance = m.value("acceptance").toMap();
        t.append(d);
    }
    return t;
}

void AppController::startBenchmark(const QStringList &profileIds, const QString &mode, int passes,
                                   const QString &target, int timeoutSec)
{
    m_benchHardTimeoutSec = qMax(0, timeoutSec);
    runBenchmarkInternal(profileIds, mode, {}, QStringLiteral("standard"), qMax(1, passes), target);
}

void AppController::startCustomBenchmark(const QStringList &profileIds, const QString &customId, int passes,
                                         const QString &target, int timeoutSec)
{
    m_benchHardTimeoutSec = qMax(0, timeoutSec);
    QVariantMap def;
    for (const QVariant &v : std::as_const(m_customBenchmarks)) {
        const QVariantMap m = v.toMap();
        if (m.value("id").toString() == customId) { def = m; break; }
    }
    if (def.isEmpty()) return;
    const QString label = def.value("name").toString().isEmpty()
                              ? QStringLiteral("custom") : def.value("name").toString();
    runBenchmarkInternal(profileIds, QStringLiteral("custom"),
                         def.value("prompts").toList(), label, qMax(1, passes), target);
}

void AppController::openBenchmarkFolder(const QString &path)
{
    if (path.trimmed().isEmpty()) return;
    const QFileInfo fi(path);
    if (!fi.exists()) {
        emit serverError(QStringLiteral("La carpeta del benchmark ya no existe:\n%1").arg(path));
        return;
    }
    const QString native = QDir::toNativeSeparators(fi.absoluteFilePath());
#ifdef Q_OS_WIN
    // QDesktopServices::openUrl can hand Explorer a forward-slash file:// URL it
    // refuses ("Ubicación no disponible"); launch Explorer directly with a
    // native path instead.
    if (QProcess::startDetached(QStringLiteral("explorer.exe"), {native}))
        return;
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absoluteFilePath()));
}

void AppController::runBenchmarkInternal(const QStringList &profileIds, const QString &mode,
                                         const QVariantList &customTasks, const QString &runLabel,
                                         int passes, const QString &target)
{
    if (m_benchmarkRunning || profileIds.isEmpty()) return;
    const bool agentTarget = (target == QLatin1String("agent"));

    // Self-heal: if the model catalog failed to load this session (e.g. the DB
    // was briefly locked by a previous instance at startup), every profile would
    // resolve to "No model selected" and fail. Reload before running.
    if (m_catalog.count() == 0)
        m_catalog.reload();

    m_benchmarkRunning  = true;
    m_benchmarkCanceled = false;
    m_benchmarkProgress = 0;
    m_benchmarkStatus   = "Iniciando...";
    emit benchmarkRunningChanged();
    emit benchmarkProgressChanged();
    emit benchmarkStatusChanged();

    loadBenchmarkResults();

    const bool isCustom = !customTasks.isEmpty();
    const QString benchmarkName = isCustom
        ? runLabel
        : (mode == QLatin1String("short") ? QStringLiteral("Corta")
                                          : QStringLiteral("Completa"));
    const QVector<BenchTaskDef> tasks = isCustom
                                            ? buildCustomBenchTasks(customTasks)
                                            : buildBenchTasks(mode);
    passes = qMax(1, passes);
    const int totalSteps = profileIds.size() * tasks.size() * passes;

    // ── Isolated run folder: benchmark/<label>_<timestamp>/ ───────────────────
    auto sanitize = [](QString s) {
        s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
        return s.left(40);
    };
    const QString stamp   = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString runDir  = benchmarkRunsDir() + "/" + sanitize(runLabel) + "_" + stamp;
    QDir().mkpath(runDir);

    // Write run metadata up front
    {
        QJsonObject meta;
        meta["label"]      = runLabel;
        meta["mode"]       = mode;
        meta["startedAt"]  = QDateTime::currentDateTime().toString(Qt::ISODate);
        meta["timestamp"]  = (double)QDateTime::currentMSecsSinceEpoch();
        QJsonArray profArr;
        for (const QString &pid : profileIds) {
            const QVariantMap pd = m_profiles.getLaunchProfile(pid);
            QJsonObject po;
            po["profileId"]   = pid;
            po["profileName"] = pd.value("name").toString();
            profArr.append(po);
        }
        meta["profiles"] = profArr;
        QJsonArray taskArr;
        for (const BenchTaskDef &t : tasks) {
            QJsonObject to;
            to["id"]        = t.id;
            to["category"]  = t.category;
            to["isSpeed"]   = t.isSpeed;
            to["maxTokens"] = t.maxTokens;
            to["prompt"]    = t.prompt;
            if (!t.acceptance.isEmpty())
                to["acceptance"] = QJsonObject::fromVariantMap(t.acceptance);
            taskArr.append(to);
        }
        meta["tasks"] = taskArr;
        meta["passes"] = passes;
        QFile mf(runDir + "/metadata.json");
        if (mf.open(QIODevice::WriteOnly | QIODevice::Truncate))
            mf.write(QJsonDocument(meta).toJson());
    }
    const auto runDirShared = std::make_shared<QString>(runDir);
    auto stepsDone = std::make_shared<int>(0);

    // Recursive per-profile sequence
    auto processNext = std::make_shared<std::function<void(int)>>();
    *processNext = [=](int idx) {
        if (m_benchmarkCanceled || idx >= profileIds.size()) {
            m_benchmarkRunning  = false;
            m_benchmarkProgress = 100;
            m_benchmarkStatus   = m_benchmarkCanceled ? "Cancelado." : "Completado.";
            emit benchmarkRunningChanged();
            emit benchmarkProgressChanged();
            emit benchmarkStatusChanged();
            return;
        }

        const QString profileId   = profileIds.at(idx);
        const QVariantMap profData = m_profiles.getLaunchProfile(profileId);
        const QString profNameRaw = profData.value("name").toString();
        const QString profName    = profNameRaw.isEmpty() ? profileId : profNameRaw;

        auto startAttempts = std::make_shared<int>(0);
        auto runProfile = std::make_shared<std::function<void()>>();
        *runProfile = [=]() {
            const qint64 loadStartMs = QDateTime::currentMSecsSinceEpoch();
            m_benchmarkStatus = QString("[%1/%2] %3 — iniciando servidor...")
                .arg(idx + 1).arg(profileIds.size()).arg(profName);
            emit benchmarkStatusChanged();

            startServer(profileId);
            if (!serverRunning()) {
                if (m_benchmarkCanceled) { (*processNext)(idx + 1); return; }
                // Auto-recovery: a stale/leftover llama-server (holding port 8021
                // or VRAM from a previous hung run) makes startServer fail. Clean
                // up (stop our proc, kill stray servers, let VRAM free) and retry.
                // The time spent in failed attempts is NOT counted toward the pass.
                static const int kMaxStartRetries = 2;
                if (*startAttempts < kMaxStartRetries) {
                    (*startAttempts)++;
                    m_benchmarkStatus =
                        QString("[%1/%2] %3 — no arrancó; limpiando y reintentando (%4/%5)...")
                            .arg(idx + 1).arg(profileIds.size()).arg(profName)
                            .arg(*startAttempts).arg(kMaxStartRetries);
                    emit benchmarkStatusChanged();
                    // Reload the model catalog in case it failed to load (transient
                    // DB lock) — that surfaces as "No model selected" / invalid profile.
                    if (m_catalog.count() == 0)
                        m_catalog.reload();
                    stopServer();
                    benchmarkKillStrayServers();
                    benchmarkWaitServerStopped(10000, [=]() {
                        // Extra delay so the GPU fully releases VRAM before retry.
                        QTimer::singleShot(3000, this, [=]() { (*runProfile)(); });
                    });
                    return;
                }
                // Retries exhausted → record the failure.
                const double elapsed =
                    (QDateTime::currentMSecsSinceEpoch() - loadStartMs) / 1000.0;
                const QString detail = benchmarkServerLogTail();
                for (int p = 1; p <= passes; ++p) {
                    saveBenchmarkFailureResult(
                        profileId, profName, p, passes, mode,
                        agentTarget ? QStringLiteral("agent") : QStringLiteral("model"),
                        benchmarkName, runLabel, *runDirShared,
                        QStringLiteral("server-start"),
                        QStringLiteral("No se pudo iniciar el servidor tras limpiar y reintentar."),
                        detail, elapsed);
                }
                (*stepsDone) += tasks.size() * passes;
                m_benchmarkProgress = qMin(99, (*stepsDone * 100) / qMax(1, totalSteps));
                emit benchmarkProgressChanged();
                (*processNext)(idx + 1);
                return;
            }
            const QString url = serverBaseUrl();

            // Large models (big ctx + mlock + no-mmap) can take minutes to load
            // on a cold cache; 150 × 2s = 5 min before giving up.
            const QString loadPrefix = QString("[%1/%2] %3").arg(idx+1).arg(profileIds.size()).arg(profName);
            benchmarkWaitServerReady(150, 150, url, loadPrefix, [=](bool ready) {
                if (!ready || m_benchmarkCanceled) {
                    if (!m_benchmarkCanceled) {
                        const QString detail = benchmarkServerLogTail();
                        const double elapsed =
                            (QDateTime::currentMSecsSinceEpoch() - loadStartMs) / 1000.0;
                        for (int p = 1; p <= passes; ++p) {
                            saveBenchmarkFailureResult(
                                profileId, profName, p, passes, mode,
                                agentTarget ? QStringLiteral("agent") : QStringLiteral("model"),
                                benchmarkName, runLabel, *runDirShared,
                                QStringLiteral("server-load"),
                                QStringLiteral("No se pudo cargar o iniciar el servidor para esta pasada."),
                                detail, elapsed);
                        }
                        (*stepsDone) += tasks.size() * passes;
                        m_benchmarkProgress = qMin(99, (*stepsDone * 100) / qMax(1, totalSteps));
                        emit benchmarkProgressChanged();
                        stopServer();
                    }
                    (*processNext)(idx + 1);
                    return;
                }

                // ── Agent target: drive a headless agent that uses tools to build
                // real files, instead of capturing raw model text. ──
                if (agentTarget) {
                    QVariantList agentTasks;
                    for (const BenchTaskDef &t : tasks) {
                        QVariantMap tm;
                        tm[QStringLiteral("id")] = t.id;
                        tm[QStringLiteral("prompt")] = t.prompt;
                        tm[QStringLiteral("acceptance")] = t.acceptance;
                        agentTasks.append(tm);
                    }
                    runAgentBenchmark(profileId, profName, idx, profileIds.size(),
                                      agentTasks, passes, mode, runLabel, *runDirShared,
                                      [=]() {
                                          stopServer();
                                          benchmarkWaitServerStopped(10000, [=]() {
                                              (*processNext)(idx + 1);
                                          });
                                      });
                    return;
                }

                // Warm-up request (discard result)
                m_benchmarkStatus = QString("[%1/%2] %3 — calentando...").arg(idx+1).arg(profileIds.size()).arg(profName);
                emit benchmarkStatusChanged();

                benchmarkRequest(url, "Say hi.", 32, true, [=](QVariantMap) {
                    // Run all tasks sequentially, repeated `passes` times per profile.
                    auto taskResults = std::make_shared<QVariantList>();
                    auto passNo = std::make_shared<int>(1);
                    auto passStartMs = std::make_shared<qint64>(QDateTime::currentMSecsSinceEpoch());
                    auto runTask = std::make_shared<std::function<void(int)>>();

                    *runTask = [=](int ti) {
                        if (m_benchmarkCanceled || ti >= tasks.size()) {
                            // Measure resources then store result
                            benchmarkMeasureResources([=](double ramMb, double vramMb) {
                                int passed = 0, qualTotal = 0;
                                double tpsSum = 0, ttftSum = 0; int speedCount = 0;
                                bool failed = false;
                                QString failureMessage;
                                QString failureDetail;
                                for (const QVariant &v : *taskResults) {
                                    const QVariantMap r = v.toMap();
                                    if (r.value("failed").toBool()) {
                                        failed = true;
                                        if (failureMessage.isEmpty())
                                            failureMessage = r.value("failureMessage").toString();
                                        if (failureDetail.isEmpty())
                                            failureDetail = r.value("failureDetail").toString();
                                    }
                                    if (r.value("type") == "quality") {
                                        qualTotal++;
                                        if (r.value("passed").toBool()) passed++;
                                    } else {
                                        tpsSum  += r.value("tps").toDouble();
                                        ttftSum += r.value("ttft_ms").toDouble();
                                        speedCount++;
                                    }
                                }
                                const double avgTps  = speedCount > 0 ? tpsSum  / speedCount : 0;
                                const double avgTtft = speedCount > 0 ? ttftSum / speedCount : 0;

                                const QString rowName = passes > 1
                                    ? QString("%1 · pasada %2/%3").arg(profName).arg(*passNo).arg(passes)
                                    : profName;

                                QVariantMap result;
                                result["profileId"]    = profileId;
                                result["profileName"]  = rowName;
                                result["pass"]         = *passNo;
                                result["passesTotal"]  = passes;
                                result["mode"]         = mode;
                                result["target"]       = QStringLiteral("model");
                                result["benchmarkName"] = benchmarkName;
                                result["timestamp"]    = (double)QDateTime::currentMSecsSinceEpoch();
                                result["qualityScore"] = passed;
                                result["qualityTotal"] = qualTotal;
                                result["firstAttemptScore"] = passed;
                                result["firstAttemptTotal"] = qualTotal;
                                result["finalScore"] = passed;
                                result["finalTotal"] = qualTotal;
                                result["repairAttempts"] = 0;
                                result["avgTps"]       = avgTps;
                                result["avgTtftMs"]    = avgTtft;
                                result["ramMb"]        = ramMb;
                                result["vramMb"]       = vramMb;
                                result["elapsedSec"]   =
                                    (QDateTime::currentMSecsSinceEpoch() - *passStartMs) / 1000.0;
                                result["timeToFirstAttempt"] = result["elapsedSec"];
                                result["totalTime"] = result["elapsedSec"];
                                result["passedAfterRepair"] = false;
                                result["tasks"]        = *taskResults;
                                result["failed"]       = failed;
                                if (failed) {
                                    result["failureStage"] = QStringLiteral("request");
                                    result["failureMessage"] =
                                        failureMessage.isEmpty()
                                            ? QStringLiteral("Falló una request del benchmark.")
                                            : failureMessage;
                                    result["failureDetail"] =
                                        failureDetail.isEmpty() ? benchmarkServerLogTail() : failureDetail;
                                }
                                result["id"]           = QUuid::createUuid().toString(QUuid::WithoutBraces);

                                result["runLabel"] = runLabel;
                                result["runDir"]   = *runDirShared;

                                m_benchmarkResults.append(result);
                                emit benchmarkResultsChanged();
                                saveBenchmarkResult(result);

                                // Also drop a per-profile file into the isolated run folder
                                {
                                    QString fname = profName;
                                    fname.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")),
                                                  QStringLiteral("_"));
                                    QFile pf(*runDirShared + "/" + fname.left(60) + "_"
                                             + result.value("id").toString() + ".json");
                                    if (pf.open(QIODevice::WriteOnly | QIODevice::Truncate))
                                        pf.write(QJsonDocument(
                                            QJsonObject::fromVariantMap(result)).toJson());
                                }

                                // Más pasadas para este perfil → reusar servidor cargado.
                                if (!m_benchmarkCanceled && *passNo < passes) {
                                    (*passNo)++;
                                    taskResults->clear();
                                    *passStartMs = QDateTime::currentMSecsSinceEpoch();
                                    m_benchmarkStatus = QString("[%1/%2] %3 — pasada %4/%5...")
                                        .arg(idx+1).arg(profileIds.size()).arg(profName)
                                        .arg(*passNo).arg(passes);
                                    emit benchmarkStatusChanged();
                                    (*runTask)(0);
                                    return;
                                }

                                stopServer();
                                benchmarkWaitServerStopped(10000, [=]() {
                                    (*processNext)(idx + 1);
                                });
                            });
                            return;
                        }

                        const BenchTaskDef &task = tasks.at(ti);
                        m_benchmarkStatus = QString("[%1/%2] %3 — %4...")
                            .arg(idx+1).arg(profileIds.size()).arg(profName).arg(task.id);
                        emit benchmarkStatusChanged();

                        // Stream every benchmark task so llama-server idleness is observable.
                        const QString resultType = task.isSpeed
                            ? QStringLiteral("speed")
                            : QStringLiteral("quality");
                        benchmarkRequest(url, task.prompt, task.maxTokens, true, [=](QVariantMap res) {
                            (*stepsDone)++;
                            m_benchmarkProgress = qMin(99, (*stepsDone * 100) / qMax(1, totalSteps));
                            emit benchmarkProgressChanged();

                            res["id"]       = task.id;
                            res["category"] = task.category;
                            if (!task.isSpeed && task.eval)
                                res["passed"] = task.eval(res.value("response").toString());
                            // EvalSuite: scoring por substrings esperados en la respuesta
                            // (tareas de texto sin auto-eval; pasa si están TODOS).
                            else if (!task.isSpeed) {
                                const QVariantList subs = task.acceptance
                                    .value(QStringLiteral("expectSubstrings")).toList();
                                if (!subs.isEmpty()) {
                                    const QString hay = res.value("response").toString().toLower();
                                    bool all = true;
                                    for (const QVariant &sv : subs) {
                                        const QString needle = sv.toString().trimmed();
                                        if (needle.isEmpty()) continue;
                                        if (!hay.contains(needle.toLower())) { all = false; break; }
                                    }
                                    res["passed"] = all;
                                }
                            }

                            if (isCustom) {
                                const QString resp = res.value("response").toString();
                                res["isSpeedTask"] = task.isSpeed;
                                res["codeOnly"]    = benchCodeOnly(resp);
                                const QVariantMap deps = benchPythonDeps(resp);
                                res["hasCode"]       = deps.value("hasCode");
                                res["inventedDeps"]  = deps.value("invented");
                                res["depCount"]      = deps.value("count");
                                res["depList"]       = deps.value("list");
                            }

                            taskResults->append(res);
                            (*runTask)(ti + 1);
                        }, resultType);
                    };
                    (*runTask)(0);
                });
            });
        };

        if (serverRunning()) {
            stopServer();
            benchmarkWaitServerStopped(8000, [runProfile]() { (*runProfile)(); });
        } else {
            (*runProfile)();
        }
    };
    (*processNext)(0);
}

void AppController::runAgentBenchmark(const QString &profileId, const QString &profName,
                                     int idx, int total, const QVariantList &benchTasks,
                                     int passes, const QString &mode, const QString &runLabel,
                                     const QString &runDir, std::function<void()> onProfileDone)
{
    const auto ctx = buildContext(profileId);

    // Profile temperature (from --temp / -t), default if absent.
    double temp = -1.0;
    {
        const QStringList &args = ctx.launch.extraArgs;
        for (int i = 0; i + 1 < args.size(); ++i)
            if (args[i] == QLatin1String("--temp") || args[i] == QLatin1String("-t")) {
                bool ok = false; const double v = args[i+1].toDouble(&ok);
                if (ok) { temp = v; break; }
            }
    }

    auto sanitize = [](QString s) {
        s.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
        return s.left(60);
    };
    auto requiredFileName = [](const QString &prompt) {
        static const QRegularExpression rx(
            QStringLiteral("(?:Archivo requerido|Required file)\\s*:\\s*`?([^`\\r\\n]+)`?"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = rx.match(prompt);
        if (m.hasMatch()) {
            const QString name = QFileInfo(m.captured(1).trimmed()).fileName();
            if (!name.isEmpty()) return name;
        }
        return QStringLiteral("agent_output.txt");
    };
    auto responseArtifact = [](QString text) {
        static const QRegularExpression fenced(
            QStringLiteral("```(?:[A-Za-z0-9_+.-]+)?\\s*\\n([\\s\\S]*?)\\n```"));
        const QRegularExpressionMatch m = fenced.match(text);
        if (m.hasMatch()) text = m.captured(1);
        text = text.trimmed();
        if (text.isEmpty()) return QString();

        // Do not materialize natural-language summaries as source files.
        const QString head = text.left(300).toLower();
        const bool looksLikePython =
            head.startsWith(QStringLiteral("#!")) ||
            head.contains(QStringLiteral("import ")) ||
            head.contains(QStringLiteral("from ")) ||
            head.contains(QStringLiteral("def ")) ||
            head.contains(QStringLiteral("class ")) ||
            head.contains(QStringLiteral("@dataclass"));
        const bool looksLikeSummary =
            head.contains(QStringLiteral("archivo creado")) ||
            head.contains(QStringLiteral("archivos creados")) ||
            head.contains(QStringLiteral("compilación")) ||
            head.contains(QStringLiteral("compilacion")) ||
            head.contains(QStringLiteral("resumen"));
        return (looksLikePython && !looksLikeSummary) ? text : QString();
    };
    auto agentPrompt = [](const QString &prompt) {
        return QStringLiteral(
            "MODO AGENTE BENCHMARK:\n"
            "- Trabaja en el directorio actual usando herramientas de archivo.\n"
            "- Debes crear/modificar los archivos pedidos en disco; no alcanza con responder codigo en el chat.\n"
            "- Si el prompt pide \"responder solamente con codigo\", interpretalo como: el archivo final debe contener solamente ese codigo.\n"
            "- Al terminar, responde breve indicando que archivos creaste y si compilaste/probaste.\n\n"
            "TAREA ORIGINAL:\n%1").arg(prompt);
    };
    auto estimateTokensLocal = [](const QString &s) {
        const int n = s.trimmed().size();
        return n <= 0 ? 0 : (n + 3) / 4;
    };
    auto runAcceptanceCommand = [](const QString &workspace, const QVariantMap &cmd) {
        QVariantMap out;
        const QString name = cmd.value(QStringLiteral("name")).toString();
        const QString command = cmd.value(QStringLiteral("command")).toString();
        const int timeoutMs = qMax(1000, cmd.value(QStringLiteral("timeoutMs"), 30000).toInt());
        const QString expectedStdout = cmd.value(QStringLiteral("expectedStdout")).toString();
        out[QStringLiteral("name")] = name.isEmpty() ? command : name;
        out[QStringLiteral("command")] = command;
        out[QStringLiteral("timeoutMs")] = timeoutMs;

        if (command.trimmed().isEmpty()) {
            out[QStringLiteral("passed")] = false;
            out[QStringLiteral("exitCode")] = -1;
            out[QStringLiteral("output")] = QStringLiteral("Comando vacio.");
            return out;
        }

        QProcess p;
        p.setWorkingDirectory(workspace);
#ifdef Q_OS_WIN
        p.start(QStringLiteral("powershell"),
                {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                 QStringLiteral("Bypass"), QStringLiteral("-Command"), command});
#else
        p.start(QStringLiteral("sh"), {QStringLiteral("-lc"), command});
#endif
        const bool finished = p.waitForFinished(timeoutMs);
        if (!finished) {
            p.kill();
            p.waitForFinished(3000);
        }
        const QString stdoutText = QString::fromUtf8(p.readAllStandardOutput());
        const QString stderrText = QString::fromUtf8(p.readAllStandardError());
        const QString combined = (stdoutText + (stderrText.isEmpty() ? QString() : QStringLiteral("\n") + stderrText)).trimmed();
        const bool exitOk = finished && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
        const bool stdoutOk = expectedStdout.isEmpty() || combined.contains(expectedStdout);
        out[QStringLiteral("passed")] = exitOk && stdoutOk;
        out[QStringLiteral("exitCode")] = finished ? p.exitCode() : -1;
        out[QStringLiteral("timedOut")] = !finished;
        out[QStringLiteral("output")] = combined.left(12000);
        if (!expectedStdout.isEmpty())
            out[QStringLiteral("expectedStdout")] = expectedStdout;
        return out;
    };
    QStringList prompts;
    for (const QVariant &tv : benchTasks) {
        const QString prompt = tv.toMap().value(QStringLiteral("prompt")).toString();
        if (!prompt.trimmed().isEmpty())
            prompts << prompt;
    }

    auto passNo = std::make_shared<int>(1);
    auto runOnePass = std::make_shared<std::function<void()>>();

    *runOnePass = [=]() {
        if (m_benchmarkCanceled) { onProfileDone(); return; }

        // Isolated workspace for this profile/pass.
        const QString wsName = sanitize(profName) + (passes > 1 ? QString("__ws_p%1").arg(*passNo)
                                                                : QStringLiteral("__ws"));
        const QString workspace = runDir + "/" + wsName;
        QDir().mkpath(workspace);

        auto *agent = new LlamaAgentBackend(this);
        m_benchmarkAgent = agent;
        agent->setEphemeralSessions(true);
        agent->setThinkingEnabled(m_agentThinkingEnabled);
        agent->setApprovalPolicy(QStringLiteral("super"));   // auto-approve every tool
        agent->setPermissionRules(m_agentPermRules);
        agent->setAgentTuning(m_agentSystemPrompt, temp);
        agent->setTeacherConfig(m_agentTeacherUrl, m_agentTeacherModel, m_agentTeacherKey);
        agent->setDisabledTools({}); // benchmark agent must be able to write/test files

        QMap<QString, QVariant> mergedMcp;
        for (const QVariant &v : listMcpServers(QStringLiteral("global"), QString()))
            mergedMcp.insert(v.toMap().value(QStringLiteral("name")).toString(), v);
        for (const QVariant &v : listMcpServers(QStringLiteral("project"), workspace))
            mergedMcp.insert(v.toMap().value(QStringLiteral("name")).toString(), v);
        injectBrowserMcp(mergedMcp, m_activeLaunchId);
        agent->setMcpServers(mergedMcp.values());

        AgentContext c;
        c.adapter       = QStringLiteral("llamaagent");
        c.cwd           = workspace;
        c.serverBaseUrl = serverBaseUrl();
        c.modelId       = routedModelId(ctx.catalogModel.id);
        agent->start(c);
        agent->newSessionInProject(workspace);
        agent->setMcpServers(mergedMcp.values());

        const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
        auto promptIdx = std::make_shared<int>(0);
        auto finished  = std::make_shared<bool>(false);
        auto timedOut  = std::make_shared<bool>(false);
        auto passFailed = std::make_shared<bool>(false);
        auto failureMessage = std::make_shared<QString>();
        auto failureDetail = std::make_shared<QString>();
        auto toolsReady = std::make_shared<bool>(mergedMcp.isEmpty());
        auto turnStartMs = std::make_shared<qint64>(0);
        auto turnFirstMs = std::make_shared<qint64>(-1);
        auto turnMetrics = std::make_shared<QVariantList>();
        auto repairAttempts = std::make_shared<int>(0);
        auto firstAttemptScore = std::make_shared<int>(-1);
        auto firstAttemptTotal = std::make_shared<int>(0);
        auto timeToFirstAttempt = std::make_shared<double>(0.0);
        const int maxRepairAttempts = 2;
        // Sin idle-timeout por defecto: solo corta el timeout duro configurable
        // por el usuario (0 = sin límite). 0 aquí deshabilita el idle-watchdog.
        const int idleTimeoutMs = 0;
        auto lastActivityMs = std::make_shared<qint64>(QDateTime::currentMSecsSinceEpoch());
        auto peakRamMb = std::make_shared<double>(0.0);
        auto peakVramMb = std::make_shared<double>(0.0);
        auto sampleResources = std::make_shared<std::function<void()>>();
        *sampleResources = [=]() {
            if (*finished) return;
            const QPair<double, double> resources = benchmarkMeasureResourcesNow();
            *peakRamMb = qMax(*peakRamMb, resources.first);
            *peakVramMb = qMax(*peakVramMb, resources.second);
            QTimer::singleShot(5000, this, [=]() { (*sampleResources)(); });
        };
        QTimer::singleShot(1000, this, [=]() { (*sampleResources)(); });

        // Finalize this pass: collect files, score, persist, tear down.
        auto finalize = std::make_shared<std::function<void()>>();
        *finalize = [=]() {
            if (*finished) return;
            *finished = true;

            const bool canceled = m_benchmarkCanceled;

            QString finalText;
            QString fallbackArtifact;
            const QVariantList msgs = agent->messages();
            QVariantList assistantMetrics;
            double tpsSum = 0.0;
            double ttftSum = 0.0;
            int tpsCount = 0;
            int ttftCount = 0;
            auto includeMetric = [&](const QVariantMap &metric) {
                const double tps = metric.value(QStringLiteral("tps")).toDouble();
                const double ttft = metric.contains(QStringLiteral("ttft_ms"))
                                        ? metric.value(QStringLiteral("ttft_ms")).toDouble()
                                        : -1.0;
                if (tps > 0.0) {
                    tpsSum += tps;
                    tpsCount++;
                }
                if (ttft >= 0.0) {
                    ttftSum += ttft;
                    ttftCount++;
                }
                assistantMetrics.append(metric);
            };
            for (auto it = msgs.crbegin(); it != msgs.crend(); ++it)
                if (it->toMap().value("role").toString() == QLatin1String("assistant")) {
                    finalText = it->toMap().value("content").toString();
                    const QString art = responseArtifact(finalText);
                    if (!art.isEmpty() && fallbackArtifact.isEmpty())
                        fallbackArtifact = art;
                    break;
                }
            if (*timedOut)
                finalText = QStringLiteral("[timeout] corrida cortada por exceder el tiempo máximo.");
            for (const QVariant &mv : msgs) {
                const QVariantMap mm = mv.toMap();
                if (mm.value(QStringLiteral("role")).toString() != QLatin1String("assistant"))
                    continue;
                const QString art = responseArtifact(mm.value(QStringLiteral("content")).toString());
                if (!art.isEmpty()) fallbackArtifact = art;
            }
            for (const QVariant &mv : msgs) {
                const QVariantMap mm = mv.toMap();
                if (mm.value(QStringLiteral("role")).toString() != QLatin1String("assistant"))
                    continue;
                const double tps = mm.value(QStringLiteral("tps")).toDouble();
                const double createdAt = mm.value(QStringLiteral("createdAt")).toDouble();
                const double genStartMs = mm.value(QStringLiteral("genStartMs")).toDouble();
                QVariantMap metric;
                metric[QStringLiteral("tokens")] = mm.value(QStringLiteral("tokens"));
                metric[QStringLiteral("elapsedMs")] = mm.value(QStringLiteral("elapsedMs"));
                metric[QStringLiteral("tps")] = tps;
                if (genStartMs > 0.0 && createdAt > 0.0) {
                    const double ttft = qMax(0.0, genStartMs - createdAt);
                    metric[QStringLiteral("ttft_ms")] = ttft;
                }
                includeMetric(metric);
            }
            // Prefer backend/server generation metrics for t/s. The turn-level
            // fallback measures a whole agent turn and can include tool execution,
            // file IO, tests and follow-up requests, so it is only useful when the
            // backend did not expose generation metrics at all.
            if (tpsCount == 0) {
                for (const QVariant &mv : *turnMetrics)
                    includeMetric(mv.toMap());
            }

            // Files the agent produced in the workspace.
            QStringList files;
            QDirIterator di(workspace, QDir::Files, QDirIterator::Subdirectories);
            while (di.hasNext()) {
                di.next();
                files << QDir(workspace).relativeFilePath(di.filePath());
            }
            if (files.isEmpty()) {
                const QString artifact = fallbackArtifact;
                if (!artifact.isEmpty()) {
                    const QString outName = requiredFileName(prompts.isEmpty() ? QString() : prompts.first());
                    QFile out(QDir(workspace).filePath(outName));
                    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                        out.write(artifact.toUtf8());
                        out.close();
                        files << outName;
                    }
                }
            }

            // Acceptance criteria from custom benchmark definitions.
            QVariantList acceptanceRows;
            int qScore = 0, qTotal = 0;
            for (const QVariant &tv : benchTasks) {
                const QVariantMap task = tv.toMap();
                const QString taskId = task.value(QStringLiteral("id")).toString();
                const QVariantMap acceptance = task.value(QStringLiteral("acceptance")).toMap();
                if (acceptance.isEmpty())
                    continue;

                const QVariantList expectedFiles = acceptance.value(QStringLiteral("files")).toList();
                for (const QVariant &fv : expectedFiles) {
                    const QString rel = fv.toString().trimmed();
                    if (rel.isEmpty()) continue;
                    const bool ok = QFileInfo(QDir(workspace).filePath(rel)).exists();
                    QVariantMap row;
                    row[QStringLiteral("taskId")] = taskId;
                    row[QStringLiteral("type")] = QStringLiteral("file");
                    row[QStringLiteral("name")] = rel;
                    row[QStringLiteral("passed")] = ok;
                    row[QStringLiteral("output")] = ok
                        ? QStringLiteral("Archivo encontrado.")
                        : QStringLiteral("Archivo esperado no encontrado.");
                    acceptanceRows.append(row);
                    qTotal++;
                    if (ok) qScore++;
                }

                const QVariantList commands = acceptance.value(QStringLiteral("commands")).toList();
                for (const QVariant &cv : commands) {
                    QVariantMap row = runAcceptanceCommand(workspace, cv.toMap());
                    row[QStringLiteral("taskId")] = taskId;
                    row[QStringLiteral("type")] = QStringLiteral("command");
                    acceptanceRows.append(row);
                    qTotal++;
                    if (row.value(QStringLiteral("passed")).toBool()) qScore++;
                }

                // Substrings esperados en la respuesta del agente (EvalSuite: tareas
                // de texto sin archivos/comandos, p.ej. periciales/docs). Match
                // case-insensitive sobre finalText.
                const QVariantList expectSubs = acceptance.value(QStringLiteral("expectSubstrings")).toList();
                const QString hay = finalText.toLower();
                for (const QVariant &sv : expectSubs) {
                    const QString needle = sv.toString().trimmed();
                    if (needle.isEmpty()) continue;
                    const bool ok = hay.contains(needle.toLower());
                    QVariantMap row;
                    row[QStringLiteral("taskId")] = taskId;
                    row[QStringLiteral("type")] = QStringLiteral("substring");
                    row[QStringLiteral("name")] = needle;
                    row[QStringLiteral("passed")] = ok;
                    row[QStringLiteral("output")] = ok
                        ? QStringLiteral("Texto presente en la respuesta.")
                        : QStringLiteral("Texto esperado ausente en la respuesta.");
                    acceptanceRows.append(row);
                    qTotal++;
                    if (ok) qScore++;
                }
            }

            // Fallback auto-score: py_compile any produced .py when the benchmark
            // did not define explicit acceptance criteria.
            const QStringList pys = files.filter(QRegularExpression(QStringLiteral("\\.py$")));
            if (qTotal == 0 && !pys.isEmpty()) {
                qTotal = 1;
                const QString py = QStandardPaths::findExecutable(QStringLiteral("python"));
                if (!py.isEmpty()) {
                    QProcess pc;
                    pc.setWorkingDirectory(workspace);
                    QStringList a; a << QStringLiteral("-m") << QStringLiteral("py_compile") << pys;
                    pc.start(py, a);
                    if (pc.waitForFinished(30000) && pc.exitStatus() == QProcess::NormalExit
                        && pc.exitCode() == 0)
                        qScore = 1;
                }
            }

            const double elapsed = (QDateTime::currentMSecsSinceEpoch() - startMs) / 1000.0;

            if (*firstAttemptScore < 0) {
                *firstAttemptScore = qScore;
                *firstAttemptTotal = qTotal;
                *timeToFirstAttempt = elapsed;
            }

            const bool acceptanceFailed = qTotal > 0 && qScore < qTotal;
            if (!canceled && !*timedOut && !*passFailed && acceptanceFailed
                    && *repairAttempts < maxRepairAttempts) {
                (*repairAttempts)++;
                *finished = false;
                QVariantList failedRows;
                for (const QVariant &rv : acceptanceRows) {
                    const QVariantMap row = rv.toMap();
                    if (!row.value(QStringLiteral("passed")).toBool())
                        failedRows.append(row);
                }
                const QString failedJson = QString::fromUtf8(
                    QJsonDocument(QJsonArray::fromVariantList(failedRows))
                        .toJson(QJsonDocument::Indented));
                const QString fileList = files.isEmpty()
                    ? QStringLiteral("(sin archivos detectados)")
                    : files.join(QStringLiteral("\n"));
                const QString repair = QStringLiteral(
                    "MODO REPARACION BENCHMARK:\n"
                    "La implementacion anterior fallo criterios de aceptacion. "
                    "No reinicies desde cero si no hace falta: inspecciona los archivos existentes, "
                    "corrige la causa concreta y vuelve a ejecutar/verificar los checks relevantes.\n\n"
                    "Intento de reparacion: %1/%2\n\n"
                    "Archivos detectados:\n%3\n\n"
                    "Checks fallidos y salidas:\n%4\n\n"
                    "Tareas originales:\n%5\n\n"
                    "Al terminar, responde breve indicando que corregiste y que pruebas corriste.")
                    .arg(*repairAttempts)
                    .arg(maxRepairAttempts)
                    .arg(fileList)
                    .arg(failedJson)
                    .arg(prompts.join(QStringLiteral("\n\n---\n\n")));
                m_benchmarkStatus = QString("[%1/%2] %3 — reparando fallos %4/%5...")
                    .arg(idx+1).arg(total).arg(profName)
                    .arg(*repairAttempts).arg(maxRepairAttempts);
                emit benchmarkStatusChanged();
                *turnStartMs = QDateTime::currentMSecsSinceEpoch();
                *turnFirstMs = -1;
                *lastActivityMs = *turnStartMs;
                agent->sendMessage(repair);
                return;
            }

            const QPair<double, double> resources = benchmarkMeasureResourcesNow();
            const double ramMb = qMax(resources.first, *peakRamMb);
            const double vramMb = qMax(resources.second, *peakVramMb);
            const QString rowName = passes > 1
                ? QString("%1 · pasada %2/%3").arg(profName).arg(*passNo).arg(passes) : profName;

            QVariantMap result;
            result["profileId"]    = profileId;
            result["profileName"]  = rowName;
            result["pass"]         = *passNo;
            result["passesTotal"]  = passes;
            result["mode"]         = mode;
            result["target"]       = QStringLiteral("agent");
            result["benchmarkName"] = (mode == QLatin1String("short") ? QStringLiteral("Corta")
                                      : mode == QLatin1String("full") ? QStringLiteral("Completa")
                                      : runLabel);
            result["timestamp"]    = (double)QDateTime::currentMSecsSinceEpoch();
            result["qualityScore"] = qScore;
            result["qualityTotal"] = qTotal;
            result["firstAttemptScore"] = *firstAttemptScore >= 0 ? *firstAttemptScore : qScore;
            result["firstAttemptTotal"] = *firstAttemptScore >= 0 ? *firstAttemptTotal : qTotal;
            result["finalScore"] = qScore;
            result["finalTotal"] = qTotal;
            result["repairAttempts"] = *repairAttempts;
            result["avgTps"]       = tpsCount > 0 ? tpsSum / tpsCount : 0.0;
            result["avgTtftMs"]    = ttftCount > 0 ? ttftSum / ttftCount : 0.0;
            result["ramMb"]        = ramMb;
            result["vramMb"]       = vramMb;
            result["elapsedSec"]   = elapsed;
            result["timeToFirstAttempt"] = *timeToFirstAttempt > 0.0 ? *timeToFirstAttempt : elapsed;
            result["totalTime"] = elapsed;
            result["passedAfterRepair"] = *repairAttempts > 0 && qTotal > 0 && qScore >= qTotal;
            result["response"]     = finalText;
            result["agentFiles"]   = files;
            result["agentMetrics"] = assistantMetrics;
            result["acceptance"]   = acceptanceRows;
            result["timedOut"]     = *timedOut;
            result["failed"]       = *passFailed || *timedOut || (qTotal > 0 && qScore < qTotal);
            if (result.value(QStringLiteral("failed")).toBool()) {
                result["failureStage"] = *timedOut
                    ? QStringLiteral("agent-idle-timeout")
                    : (*passFailed ? QStringLiteral("agent") : QStringLiteral("acceptance"));
                result["failureMessage"] = failureMessage->isEmpty()
                    ? (qTotal > 0 && qScore < qTotal
                        ? QStringLiteral("Fallaron criterios de aceptacion.")
                        : finalText)
                    : *failureMessage;
                result["failureDetail"] = failureDetail->isEmpty()
                    ? (acceptanceRows.isEmpty()
                        ? benchmarkServerLogTail()
                        : QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(acceptanceRows))
                                                .toJson(QJsonDocument::Indented)))
                    : *failureDetail;
            }
            result["workspace"]    = workspace;
            result["id"]           = QUuid::createUuid().toString(QUuid::WithoutBraces);
            result["runLabel"]     = runLabel;
            result["runDir"]       = runDir;

            if (!canceled) {
                m_benchmarkResults.append(result);
                emit benchmarkResultsChanged();
                saveBenchmarkResult(result);
                {
                    QFile pf(runDir + "/" + sanitize(profName) + "_"
                             + result.value("id").toString() + ".json");
                    if (pf.open(QIODevice::WriteOnly | QIODevice::Truncate))
                        pf.write(QJsonDocument(QJsonObject::fromVariantMap(result)).toJson());
                }
            }

            if (m_benchmarkAgent == agent) m_benchmarkAgent = nullptr;
            agent->stop();
            agent->deleteLater();

            if (!canceled && !*timedOut && *passNo < passes) {
                (*passNo)++;
                (*runOnePass)();
            } else {
                onProfileDone();
            }
        };

        // Send prompts one after another; advance when the agent goes idle.
        auto sendNext = std::make_shared<std::function<void()>>();
        *sendNext = [=]() {
            if (!*toolsReady) return;
            if (m_benchmarkCanceled || *promptIdx >= prompts.size()) { (*finalize)(); return; }
            m_benchmarkStatus = QString("[%1/%2] %3 — agente: prompt %4/%5...")
                .arg(idx+1).arg(total).arg(profName).arg(*promptIdx + 1).arg(prompts.size());
            emit benchmarkStatusChanged();
            *turnStartMs = QDateTime::currentMSecsSinceEpoch();
            *turnFirstMs = -1;
            *lastActivityMs = *turnStartMs;
            agent->sendMessage(agentPrompt(prompts.at(*promptIdx)));
            (*promptIdx)++;
        };

        connect(agent, &IAgentBackend::streamingText, this, [=](int, const QString &content) {
            if (!content.isEmpty())
                *lastActivityMs = QDateTime::currentMSecsSinceEpoch();
            if (*finished || *turnStartMs <= 0 || *turnFirstMs >= 0) return;
            *turnFirstMs = QDateTime::currentMSecsSinceEpoch();
        });
        connect(agent, &IAgentBackend::messagesChanged, this, [=]() {
            if (!*finished)
                *lastActivityMs = QDateTime::currentMSecsSinceEpoch();
        });

        // running() is true for the whole backend lifetime (start→stop), NOT per
        // turn. Turn completion is marked by finishTurn() logging "[turn] completed".
        connect(agent, &IAgentBackend::logAppended, this, [=](const QString &chunk) {
            if (*finished) return;
            if (!chunk.trimmed().isEmpty())
                *lastActivityMs = QDateTime::currentMSecsSinceEpoch();
            if (!*toolsReady && chunk.contains(QLatin1String("[mcp]"))
                    && chunk.contains(QLatin1String("descubiertas"))) {
                *toolsReady = true;
                (*sendNext)();
                return;
            }
            if (!chunk.contains(QLatin1String("[turn] completed"))) return;
            if (*turnStartMs > 0) {
                const qint64 doneMs = QDateTime::currentMSecsSinceEpoch();
                QString latestAssistant;
                const QVariantList currentMsgs = agent->messages();
                for (auto it = currentMsgs.crbegin(); it != currentMsgs.crend(); ++it) {
                    const QVariantMap mm = it->toMap();
                    if (mm.value(QStringLiteral("role")).toString() == QLatin1String("assistant")) {
                        latestAssistant = mm.value(QStringLiteral("content")).toString();
                        break;
                    }
                }
                const int toks = estimateTokensLocal(latestAssistant);
                const qint64 firstMs = (*turnFirstMs >= 0) ? *turnFirstMs : *turnStartMs;
                const double ttft = qMax<qint64>(0, firstMs - *turnStartMs);
                const double genSec = qMax(0.001, (doneMs - firstMs) / 1000.0);
                QVariantMap metric;
                metric[QStringLiteral("tokens")] = toks;
                metric[QStringLiteral("elapsedMs")] = static_cast<int>(qMax<qint64>(0, doneMs - *turnStartMs));
                metric[QStringLiteral("ttft_ms")] = ttft;
                metric[QStringLiteral("tps")] = toks > 0 ? toks / genSec : 0.0;
                turnMetrics->append(metric);
                *turnStartMs = 0;
                *turnFirstMs = -1;
            }
            if (m_benchmarkCanceled) { (*finalize)(); return; }
            if (*promptIdx >= prompts.size()) (*finalize)();
            else (*sendNext)();
        });
        connect(agent, &IAgentBackend::errorOccurred, this, [=](const QString &msg) {
            *passFailed = true;
            *failureMessage = msg;
            *failureDetail = benchmarkServerLogTail();
            (*finalize)();
        });

        // Idle watchdog: no hard wall-clock limit. Fail only if the agent/server
        // stops producing stream/log/message activity for a sustained interval.
        auto idlePoll = std::make_shared<std::function<void()>>();
        *idlePoll = [=]() {
            if (*finished) return;
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (idleTimeoutMs > 0 && now - *lastActivityMs >= idleTimeoutMs) {
                *timedOut = true;
                *passFailed = true;
                *failureMessage = QStringLiteral("llama-server/agente sin actividad (idle-timeout).");
                *failureDetail = benchmarkServerLogTail();
                agent->cancelGeneration();
                (*finalize)();
                return;
            }
            QTimer::singleShot(5000, this, [=]() { (*idlePoll)(); });
        };
        if (idleTimeoutMs > 0)
            QTimer::singleShot(5000, this, [=]() { (*idlePoll)(); });

        // Timeout duro (wall-clock) por corrida: si esta corrida supera el límite
        // configurado, se corta SOLO esta (finalize avanza a la siguiente).
        if (m_benchHardTimeoutSec > 0) {
            QTimer::singleShot(m_benchHardTimeoutSec * 1000, this, [=]() {
                if (*finished) return;
                *timedOut = true;
                *passFailed = true;
                *failureMessage = QStringLiteral("Error de timeout");
                *failureDetail = benchmarkServerLogTail();
                agent->cancelGeneration();
                (*finalize)();
            });
        }

        auto cancelPoll = std::make_shared<std::function<void()>>();
        *cancelPoll = [=]() {
            if (*finished) return;
            if (m_benchmarkCanceled) {
                agent->cancelGeneration();
                (*finalize)();
                return;
            }
            QTimer::singleShot(200, this, [=]() { (*cancelPoll)(); });
        };
        QTimer::singleShot(200, this, [=]() { (*cancelPoll)(); });

        // Kick off the first prompt after tools are ready. If there are no MCP
        // servers, built-in tools are available immediately.
        QTimer::singleShot(300, this, [=]() { (*sendNext)(); });
        QTimer::singleShot(5000, this, [=]() {
            if (!*finished && !*toolsReady) {
                *toolsReady = true;
                (*sendNext)();
            }
        });
    };

    (*runOnePass)();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void AppController::benchmarkWaitServerReady(int attemptsLeft, int totalAttempts, const QString &url,
                                              const QString &statusPrefix,
                                              std::function<void(bool)> onResult,
                                              qint64 waitStartMs,
                                              qint64 lastActivityMs,
                                              qint64 lastLogSize)
{
    if (m_benchmarkCanceled) { onResult(false); return; }   // bail fast on cancel
    if (attemptsLeft < totalAttempts && !serverRunning()) { onResult(false); return; }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (waitStartMs <= 0) waitStartMs = now;
    if (lastActivityMs <= 0) lastActivityMs = now;
    QFileInfo logInfo(m_serverLogFilePath);
    const qint64 currentLogSize = logInfo.exists() ? logInfo.size() : -1;
    if (currentLogSize != lastLogSize) {
        lastActivityMs = now;
        lastLogSize = currentLogSize;
    }
    if (now - lastActivityMs >= 3 * 60 * 1000) {
        onResult(false);
        return;
    }

    // Surface load progress: model load can take minutes for big-ctx profiles.
    const int elapsedSec = static_cast<int>((now - waitStartMs) / 1000);
    m_benchmarkStatus = QString("%1 — cargando modelo (puede tardar)... %2s")
                            .arg(statusPrefix).arg(elapsedSec);
    emit benchmarkStatusChanged();

    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    auto *reply = m_nam->get(QNetworkRequest(QUrl(url + "/health")));
    m_benchmarkActiveReply = reply;
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (m_benchmarkActiveReply == reply) m_benchmarkActiveReply = nullptr;
        const bool ok = reply->error() == QNetworkReply::NoError &&
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        reply->deleteLater();
        if (m_benchmarkCanceled) { onResult(false); return; }
        if (ok) { onResult(true); return; }
        if (!serverRunning()) { onResult(false); return; }
        QTimer::singleShot(2000, this, [=]() {
            benchmarkWaitServerReady(attemptsLeft - 1, totalAttempts, url, statusPrefix,
                                     onResult, waitStartMs, lastActivityMs, lastLogSize);
        });
    });
}

void AppController::benchmarkWaitServerStopped(int remainingMs, std::function<void()> onStopped)
{
    if (!serverRunning() || remainingMs <= 0) { onStopped(); return; }
    QTimer::singleShot(300, this, [=]() {
        benchmarkWaitServerStopped(remainingMs - 300, onStopped);
    });
}

// Force-kill any leftover llama-server processes that survived a previous run and
// may be holding the port / VRAM, blocking a fresh server start. Used by the
// benchmark auto-recovery path. Synchronous and best-effort.
void AppController::benchmarkKillStrayServers()
{
#ifdef Q_OS_WIN
    QProcess::execute(QStringLiteral("taskkill"),
                      {QStringLiteral("/F"), QStringLiteral("/T"),
                       QStringLiteral("/IM"), QStringLiteral("llama-server.exe")});
#else
    QProcess::execute(QStringLiteral("pkill"),
                      {QStringLiteral("-9"), QStringLiteral("-f"), QStringLiteral("llama-server")});
#endif
    appendServerEvent(QStringLiteral("lifecycle"),
                      QStringLiteral("Auto-recovery: killed stray llama-server processes."));
}

void AppController::benchmarkRequest(const QString &url, const QString &prompt,
                                      int maxTokens, bool streaming,
                                      std::function<void(QVariantMap)> onDone,
                                      const QString &resultType)
{
    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(url + "/v1/chat/completions"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(0);

    QJsonObject payload;
    payload["model"]      = "benchmark";
    payload["messages"]   = QJsonArray{QJsonObject{{"role","user"},{"content",prompt}}};
    payload["stream"]     = streaming;
    payload["max_tokens"] = maxTokens;
    payload["temperature"]= 0.0;
    payload["top_p"]      = 1.0;
    if (streaming)
        payload["stream_options"] = QJsonObject{{"include_usage", true}};

    auto *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_benchmarkActiveReply = reply;   // so cancelBenchmark() can abort it
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    // Sin idle-timeout por defecto (0 = deshabilitado). Solo corta el timeout
    // duro configurable por el usuario.
    const int idleTimeoutMs = 0;
    auto requestDone = std::make_shared<bool>(false);
    auto idleTimedOut = std::make_shared<bool>(false);
    auto lastActivityMs = std::make_shared<qint64>(startMs);
    auto idlePoll = std::make_shared<std::function<void()>>();
    *idlePoll = [=]() {
        if (*requestDone) return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (idleTimeoutMs > 0 && now - *lastActivityMs >= idleTimeoutMs) {
            *idleTimedOut = true;
            if (reply->isRunning())
                reply->abort();
            return;
        }
        QTimer::singleShot(5000, this, [=]() { (*idlePoll)(); });
    };
    if (idleTimeoutMs > 0)
        QTimer::singleShot(5000, this, [=]() { (*idlePoll)(); });

    // Timeout duro (wall-clock) por corrida.
    auto hardTimedOut = std::make_shared<bool>(false);
    if (m_benchHardTimeoutSec > 0) {
        QTimer::singleShot(m_benchHardTimeoutSec * 1000, this, [=]() {
            if (*requestDone) return;
            *hardTimedOut = true;
            if (reply->isRunning()) reply->abort();
        });
    }

    if (streaming) {
        struct SpeedState { QByteArray buf; qint64 ttftMs = -1; int chunks = 0;
                            int tokens = 0; QString response; };
        auto state = std::make_shared<SpeedState>();

        connect(reply, &QNetworkReply::readyRead, this, [=]() {
            *lastActivityMs = QDateTime::currentMSecsSinceEpoch();
            state->buf.append(reply->readAll());
            int nl;
            while ((nl = state->buf.indexOf('\n')) >= 0) {
                const QByteArray line = state->buf.left(nl).trimmed();
                state->buf.remove(0, nl + 1);
                if (!line.startsWith("data: ")) continue;
                const QByteArray json = line.mid(6);
                if (json == "[DONE]") continue;
                const QJsonObject obj = QJsonDocument::fromJson(json).object();
                // Final usage chunk (choices empty, usage populated)
                const QJsonObject usage = obj.value("usage").toObject();
                if (!usage.isEmpty())
                    state->tokens = usage.value("completion_tokens").toInt(state->tokens);
                const QString delta = obj.value("choices").toArray().first().toObject()
                    .value("delta").toObject().value("content").toString();
                if (!delta.isEmpty()) {
                    if (state->ttftMs < 0) state->ttftMs = QDateTime::currentMSecsSinceEpoch() - startMs;
                    state->chunks++;
                    state->response += delta;
                }
            }
        });

        connect(reply, &QNetworkReply::finished, this, [=]() {
            *requestDone = true;
            const qint64 totalMs = QDateTime::currentMSecsSinceEpoch() - startMs;
            const int tokens = state->tokens > 0 ? state->tokens : state->chunks;
            double tps = 0;
            if (state->ttftMs >= 0 && tokens > 0)
                tps = tokens / qMax(0.001, (totalMs - state->ttftMs) / 1000.0);
            const bool failed = reply->error() != QNetworkReply::NoError;
            QVariantMap r;
            r["type"]       = resultType.isEmpty() ? QStringLiteral("speed") : resultType;
            r["ttft_ms"]    = state->ttftMs;
            r["tps"]        = tps;
            r["chunks"]     = state->chunks;
            r["tokens"]     = tokens;
            r["elapsed_ms"] = totalMs;
            r["response"]   = state->response;
            r["failed"]     = failed;
            if (failed) {
                r["failureMessage"] = *hardTimedOut
                    ? QStringLiteral("Error de timeout")
                    : (*idleTimedOut
                        ? QStringLiteral("llama-server sin actividad (idle-timeout).")
                        : reply->errorString());
                r["failureDetail"] = QString::fromUtf8(reply->readAll());
            }
            if (m_benchmarkActiveReply == reply) m_benchmarkActiveReply = nullptr;
            reply->deleteLater();
            onDone(r);
        });
    } else {
        connect(reply, &QNetworkReply::readyRead, this, [=]() {
            *lastActivityMs = QDateTime::currentMSecsSinceEpoch();
        });
        connect(reply, &QNetworkReply::finished, this, [=]() {
            *requestDone = true;
            const qint64 totalMs = QDateTime::currentMSecsSinceEpoch() - startMs;
            QString response;
            int tokens = 0;
            QString failureDetail;
            if (reply->error() == QNetworkReply::NoError) {
                const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
                response = obj.value("choices").toArray().first().toObject()
                    .value("message").toObject().value("content").toString();
                tokens = obj.value("usage").toObject().value("completion_tokens").toInt();
            } else {
                failureDetail = QString::fromUtf8(reply->readAll());
            }
            QVariantMap r;
            r["type"]       = "quality";
            r["elapsed_ms"] = totalMs;
            r["tokens"]     = tokens;
            r["response"]   = response;
            r["failed"]     = reply->error() != QNetworkReply::NoError;
            if (r.value("failed").toBool()) {
                r["failureMessage"] = *hardTimedOut
                    ? QStringLiteral("Error de timeout")
                    : (*idleTimedOut
                        ? QStringLiteral("llama-server sin actividad (idle-timeout).")
                        : reply->errorString());
                r["failureDetail"] = failureDetail;
            }
            if (m_benchmarkActiveReply == reply) m_benchmarkActiveReply = nullptr;
            reply->deleteLater();
            onDone(r);
        });
    }
}

static QStringList parseCsvLine(const QString &line)
{
    QStringList out;
    QString current;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"')) {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                current += ch;
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == QLatin1Char(',') && !quoted) {
            out << current;
            current.clear();
        } else {
            current += ch;
        }
    }
    out << current;
    return out;
}

QPair<double, double> AppController::benchmarkMeasureResourcesNow() const
{
    const qint64 serverPid = m_proc ? m_proc->processId() : 0;
    const QString processName = m_proc
        ? QFileInfo(m_proc->program()).fileName()
        : QStringLiteral("llama-server.exe");
    double ramMb = 0.0;
    double vramMb = 0.0;

#ifdef Q_OS_WIN
    auto readCimRam = [&](qint64 rootPid, const QString &imageName) {
        QProcess ps;
        QString script;
        if (rootPid > 0) {
            script = QStringLiteral(
                "$root=%1;"
                "$procs=Get-CimInstance Win32_Process | "
                "Select-Object ProcessId,ParentProcessId,WorkingSetSize,PageFileUsage;"
                "$ids=[System.Collections.Generic.HashSet[int]]::new();"
                "$queue=[System.Collections.Generic.Queue[int]]::new();"
                "[void]$ids.Add([int]$root); $queue.Enqueue([int]$root);"
                "while($queue.Count -gt 0){"
                "  $p=$queue.Dequeue();"
                "  foreach($c in $procs | Where-Object { $_.ParentProcessId -eq $p }){"
                "    if($ids.Add([int]$c.ProcessId)){ $queue.Enqueue([int]$c.ProcessId) }"
                "  }"
                "}"
                "$sel=$procs | Where-Object { $ids.Contains([int]$_.ProcessId) };"
                "$ws=($sel | Measure-Object -Property WorkingSetSize -Sum).Sum;"
                "$pf=($sel | Measure-Object -Property PageFileUsage -Sum).Sum;"
                "if($null -eq $ws){$ws=0}; if($null -eq $pf){$pf=0};"
                "[Console]::Out.WriteLine(([double]$ws/1MB).ToString([Globalization.CultureInfo]::InvariantCulture)+','+"
                "(([double]$pf/1024).ToString([Globalization.CultureInfo]::InvariantCulture)))")
                .arg(rootPid);
        } else {
            QString safeName = imageName.isEmpty()
                ? QStringLiteral("llama-server.exe")
                : imageName;
            safeName.replace(QLatin1Char('\''), QStringLiteral("''"));
            script = QStringLiteral(
                "$name='%1';"
                "$sel=Get-CimInstance Win32_Process | Where-Object { $_.Name -eq $name };"
                "$ws=($sel | Measure-Object -Property WorkingSetSize -Sum).Sum;"
                "$pf=($sel | Measure-Object -Property PageFileUsage -Sum).Sum;"
                "if($null -eq $ws){$ws=0}; if($null -eq $pf){$pf=0};"
                "[Console]::Out.WriteLine(([double]$ws/1MB).ToString([Globalization.CultureInfo]::InvariantCulture)+','+"
                "(([double]$pf/1024).ToString([Globalization.CultureInfo]::InvariantCulture)))")
                .arg(safeName);
        }
        ps.start(QStringLiteral("powershell"),
                 {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                  QStringLiteral("Bypass"), QStringLiteral("-Command"), script});
        if (!ps.waitForFinished(5000))
            return 0.0;
        const QString out = QString::fromUtf8(ps.readAllStandardOutput()).trimmed();
        const QStringList parts = out.split(QLatin1Char(','));
        if (parts.size() < 2)
            return 0.0;
        bool wsOk = false;
        bool pfOk = false;
        const double wsMb = parts.at(0).trimmed().toDouble(&wsOk);
        const double privateMb = parts.at(1).trimmed().toDouble(&pfOk);
        return qMax(wsOk ? wsMb : 0.0, pfOk ? privateMb : 0.0);
    };

    if (serverPid > 0)
        ramMb = readCimRam(serverPid, QString());
    if (ramMb <= 0.0 && !processName.isEmpty())
        ramMb = readCimRam(0, processName);
    if (ramMb <= 0.0)
        ramMb = readCimRam(0, QStringLiteral("llama-server.exe"));
#endif

    const QString nvidiaSmi = QStandardPaths::findExecutable(QStringLiteral("nvidia-smi"));
    if (!nvidiaSmi.isEmpty()) {
        QProcess nsmi;
        nsmi.start(nvidiaSmi,
                   {QStringLiteral("--query-compute-apps=pid,used_memory"),
                    QStringLiteral("--format=csv,noheader,nounits")});
        if (nsmi.waitForFinished(3000)) {
            const QString text = QString::fromUtf8(nsmi.readAllStandardOutput());
            for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
                const QStringList parts = line.split(QLatin1Char(','));
                if (parts.size() < 2)
                    continue;
                bool pidOk = false;
                const qint64 pid = parts.at(0).trimmed().toLongLong(&pidOk);
                if (serverPid > 0 && (!pidOk || pid != serverPid))
                    continue;
                bool memOk = false;
                const double mb = parts.at(1).trimmed().toDouble(&memOk);
                if (memOk)
                    vramMb += mb;
            }
        }
        if (vramMb <= 0.0) {
            QProcess gpu;
            gpu.start(nvidiaSmi,
                      {QStringLiteral("--query-gpu=memory.used"),
                       QStringLiteral("--format=csv,noheader,nounits")});
            if (gpu.waitForFinished(3000)) {
                const QString text = QString::fromUtf8(gpu.readAllStandardOutput());
                for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
                    bool ok = false;
                    const double mb = line.trimmed().toDouble(&ok);
                    if (ok)
                        vramMb += mb;
                }
            }
        }
    }

    return {ramMb, vramMb};
}

void AppController::benchmarkMeasureResources(std::function<void(double, double)> onDone)
{
    const QPair<double, double> resources = benchmarkMeasureResourcesNow();
    QTimer::singleShot(0, this, [=]() {
        onDone(resources.first, resources.second);
    });
}

// ── Persistence ───────────────────────────────────────────────────────────────

QString AppController::benchmarkStorageDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + "/benchmarks";
    QDir().mkpath(dir);
    return dir;
}

QString AppController::benchmarkServerLogTail(int maxBytes) const
{
    QFile f(m_serverLogFilePath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    if (maxBytes > 0 && f.size() > maxBytes)
        f.seek(f.size() - maxBytes);
    return QString::fromUtf8(f.readAll()).trimmed();
}

void AppController::saveBenchmarkFailureResult(const QString &profileId, const QString &profileName,
                                               int pass, int passes, const QString &mode,
                                               const QString &target, const QString &benchmarkName,
                                               const QString &runLabel, const QString &runDir,
                                               const QString &stage, const QString &message,
                                               const QString &detail, double elapsedSec)
{
    const QString rowName = passes > 1
        ? QString("%1 · pasada %2/%3").arg(profileName).arg(pass).arg(passes)
        : profileName;

    QVariantMap result;
    result[QStringLiteral("profileId")] = profileId;
    result[QStringLiteral("profileName")] = rowName;
    result[QStringLiteral("pass")] = pass;
    result[QStringLiteral("passesTotal")] = passes;
    result[QStringLiteral("mode")] = mode;
    result[QStringLiteral("target")] = target;
    result[QStringLiteral("benchmarkName")] = benchmarkName;
    result[QStringLiteral("timestamp")] = (double)QDateTime::currentMSecsSinceEpoch();
    result[QStringLiteral("qualityScore")] = 0;
    result[QStringLiteral("qualityTotal")] = 0;
    result[QStringLiteral("firstAttemptScore")] = 0;
    result[QStringLiteral("firstAttemptTotal")] = 0;
    result[QStringLiteral("finalScore")] = 0;
    result[QStringLiteral("finalTotal")] = 0;
    result[QStringLiteral("repairAttempts")] = 0;
    result[QStringLiteral("avgTps")] = 0.0;
    result[QStringLiteral("avgTtftMs")] = 0.0;
    result[QStringLiteral("ramMb")] = 0.0;
    result[QStringLiteral("vramMb")] = 0.0;
    result[QStringLiteral("elapsedSec")] = elapsedSec;
    result[QStringLiteral("timeToFirstAttempt")] = elapsedSec;
    result[QStringLiteral("totalTime")] = elapsedSec;
    result[QStringLiteral("passedAfterRepair")] = false;
    result[QStringLiteral("tasks")] = QVariantList{};
    result[QStringLiteral("failed")] = true;
    result[QStringLiteral("failureStage")] = stage;
    result[QStringLiteral("failureMessage")] = message;
    result[QStringLiteral("failureDetail")] = detail;
    result[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    result[QStringLiteral("runLabel")] = runLabel;
    result[QStringLiteral("runDir")] = runDir;

    m_benchmarkResults.append(result);
    emit benchmarkResultsChanged();
    saveBenchmarkResult(result);

    if (!runDir.isEmpty()) {
        QString fname = profileName;
        fname.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_"));
        QFile pf(runDir + "/" + fname.left(60) + "_"
                 + result.value(QStringLiteral("id")).toString() + "_failed.json");
        if (pf.open(QIODevice::WriteOnly | QIODevice::Truncate))
            pf.write(QJsonDocument(QJsonObject::fromVariantMap(result)).toJson());
    }
}

void AppController::saveBenchmarkResult(const QVariantMap &result)
{
    const QString dir = benchmarkStorageDir();
    const QString existingId = result.value("id").toString();
    const QString id  = existingId.isEmpty()
                            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                            : existingId;

    // Update index
    const QString idxPath = dir + "/index.json";
    QJsonArray idx;
    QFile fi(idxPath);
    if (fi.open(QIODevice::ReadOnly)) { idx = QJsonDocument::fromJson(fi.readAll()).array(); fi.close(); }

    QJsonObject summary;
    summary["id"]           = id;
    summary["profileId"]    = result.value("profileId").toString();
    // profileName is kept ONLY as a fallback for profiles that no longer exist;
    // the history display name is resolved live by profileId at load time.
    summary["profileName"]  = result.value("profileName").toString();
    summary["pass"]         = result.value("pass").toInt();
    summary["passesTotal"]  = result.value("passesTotal").toInt();
    summary["mode"]         = result.value("mode").toString();
    summary["benchmarkName"] = result.value("benchmarkName").toString();
    summary["timestamp"]    = result.value("timestamp").toDouble();
    summary["qualityScore"] = result.value("qualityScore").toInt();
    summary["qualityTotal"] = result.value("qualityTotal").toInt();
    summary["firstAttemptScore"] = result.value("firstAttemptScore").toInt();
    summary["firstAttemptTotal"] = result.value("firstAttemptTotal").toInt();
    summary["finalScore"] = result.value("finalScore").toInt();
    summary["finalTotal"] = result.value("finalTotal").toInt();
    summary["repairAttempts"] = result.value("repairAttempts").toInt();
    summary["timeToFirstAttempt"] = result.value("timeToFirstAttempt").toDouble();
    summary["totalTime"] = result.value("totalTime").toDouble();
    summary["passedAfterRepair"] = result.value("passedAfterRepair").toBool();
    summary["avgTps"]       = result.value("avgTps").toDouble();
    summary["avgTtftMs"]    = result.value("avgTtftMs").toDouble();
    summary["elapsedSec"]   = result.value("elapsedSec").toDouble();
    summary["ramMb"]        = result.value("ramMb").toDouble();
    summary["vramMb"]       = result.value("vramMb").toDouble();
    summary["target"]       = result.value("target").toString();
    summary["runLabel"]     = result.value("runLabel").toString();
    summary["runDir"]       = result.value("runDir").toString();
    summary["workspace"]    = result.value("workspace").toString();
    summary["timedOut"]     = result.value("timedOut").toBool();
    summary["acceptance"]   = QJsonArray::fromVariantList(result.value("acceptance").toList());
    summary["failed"]       = result.value("failed").toBool();
    summary["failureStage"] = result.value("failureStage").toString();
    summary["failureMessage"] = result.value("failureMessage").toString();
    summary["failureDetail"] = result.value("failureDetail").toString();
    idx.append(summary);

    if (fi.open(QIODevice::WriteOnly | QIODevice::Truncate))
        fi.write(QJsonDocument(idx).toJson());

    // Full result file
    QFile rf(dir + "/" + id + ".json");
    if (rf.open(QIODevice::WriteOnly | QIODevice::Truncate))
        rf.write(QJsonDocument(QJsonObject::fromVariantMap(result)).toJson());
}

void AppController::loadBenchmarkResults()
{
    const QString idxPath = benchmarkStorageDir() + "/index.json";
    QFile f(idxPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    // Merge: add only entries not already in m_benchmarkResults
    QSet<QString> existing;
    for (const QVariant &v : std::as_const(m_benchmarkResults))
        existing.insert(v.toMap().value("id").toString());
    for (const QJsonValue &v : arr) {
        QVariantMap m = v.toObject().toVariantMap();
        const QString id = m.value(QStringLiteral("id")).toString();
        const bool needsFullResult =
            m.value(QStringLiteral("benchmarkName")).toString().isEmpty()
            || m.value(QStringLiteral("runLabel")).toString().isEmpty()
            || m.value(QStringLiteral("target")).toString().isEmpty()
            || (m.value(QStringLiteral("failed")).toBool()
                && m.value(QStringLiteral("failureDetail")).toString().isEmpty());
        if (!id.isEmpty() && needsFullResult) {
            QFile rf(benchmarkStorageDir() + "/" + id + ".json");
            if (rf.open(QIODevice::ReadOnly)) {
                const QVariantMap full =
                    QJsonDocument::fromJson(rf.readAll()).object().toVariantMap();
                for (auto it = full.cbegin(); it != full.cend(); ++it)
                    if (!m.contains(it.key()) || m.value(it.key()).toString().isEmpty())
                        m.insert(it.key(), it.value());
            }
        }
        // Resolve the display name LIVE from the profile ID so renaming a launch
        // profile is reflected in benchmark history (we match by ID, not by the
        // name stored at run time). Keep the "· pasada X/Y" suffix; fall back to
        // the stored name when the profile no longer exists.
        const QString pid = m.value(QStringLiteral("profileId")).toString();
        const QString liveName = pid.isEmpty() ? QString()
                                               : m_profiles.resolveLaunch(pid).name;
        if (!liveName.isEmpty()) {
            // Prefer structured pass fields; fall back to parsing the legacy
            // "· pasada X/Y" suffix from the stored name for older rows.
            QString suffix;
            const int pass = m.value(QStringLiteral("pass")).toInt();
            const int passesTotal = m.value(QStringLiteral("passesTotal")).toInt();
            if (pass > 0 && passesTotal > 0) {
                suffix = QStringLiteral(" · pasada %1/%2").arg(pass).arg(passesTotal);
            } else {
                static const QRegularExpression passRe(
                    QStringLiteral("\\s*·\\s*pasada\\s*\\d+\\s*/\\s*\\d+\\s*$"));
                const QString stored = m.value(QStringLiteral("profileName")).toString();
                const auto match = passRe.match(stored);
                if (match.hasMatch()) suffix = stored.mid(match.capturedStart());
            }
            m["profileName"] = liveName + suffix;
        }
        if (!existing.contains(m.value("id").toString()))
            m_benchmarkResults.append(m);
    }
    emit benchmarkResultsChanged();
}

// ── Custom benchmarks ───────────────────────────────────────────────────────

QString AppController::benchmarkRunsDir() const
{
    // Isolated, timestamped run folders. Persisted under the user's app-data dir
    // (NOT applicationDirPath — that lives inside build/ and is wiped on rebuild,
    // which destroyed past runs). Migrates any legacy folders from the old spot.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + "/benchmark-runs";
    QDir().mkpath(dir);

    const QString legacy = QCoreApplication::applicationDirPath() + "/benchmark";
    if (legacy != dir && QDir(legacy).exists()) {
        const QDir ld(legacy);
        const auto entries = ld.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
        for (const QString &e : entries) {
            const QString dst = dir + "/" + e;
            if (!QFileInfo::exists(dst))
                QDir().rename(ld.filePath(e), dst);
        }
    }
    return dir;
}

QString AppController::customBenchmarkDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + "/benchmarks/custom";
    QDir().mkpath(dir);
    return dir;
}

void AppController::seedBundledCustomBenchmarks() const
{
    const QString dstDir = customBenchmarkDir();
    QDirIterator it(QStringLiteral(":/assets/benchmarks/custom"),
                    QStringList{QStringLiteral("*.json")},
                    QDir::Files);
    while (it.hasNext()) {
        const QString srcPath = it.next();
        const QString dstPath = QDir(dstDir).filePath(QFileInfo(srcPath).fileName());
        if (QFileInfo::exists(dstPath))
            continue;

        QFile src(srcPath);
        if (!src.open(QIODevice::ReadOnly))
            continue;
        QFile dst(dstPath);
        if (!dst.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            continue;
        dst.write(src.readAll());
    }
}

void AppController::loadCustomBenchmarks()
{
    seedBundledCustomBenchmarks();
    m_customBenchmarks.clear();
    const QDir dir(customBenchmarkDir());
    const auto files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        QFile jf(dir.filePath(f));
        if (!jf.open(QIODevice::ReadOnly)) continue;
        const QJsonObject o = QJsonDocument::fromJson(jf.readAll()).object();
        if (!o.isEmpty()) m_customBenchmarks.append(o.toVariantMap());
    }
    emit customBenchmarksChanged();
}

QString AppController::saveCustomBenchmark(const QVariantMap &def)
{
    QVariantMap m = def;
    QString id = m.value("id").toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m["id"] = id;
    }
    QFile f(customBenchmarkDir() + "/" + id + ".json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(QJsonObject::fromVariantMap(m)).toJson());
        f.close();   // flush before reload, else loadCustomBenchmarks reads empty file
    }
    loadCustomBenchmarks();
    return id;
}

QString AppController::importEvalSuite(const QString &path)
{
    m_lastEvalImportError.clear();
    QString err;
    const EvalSuite suite = EvalSuite::loadFromFile(path, &err);
    if (suite.isEmpty()) {
        m_lastEvalImportError = err.isEmpty() ? QStringLiteral("suite vacía") : err;
        return {};
    }

    // EvalSuite → schema de custom benchmark. El acceptance por substrings se mapea a
    // acceptance.expectSubstrings, auto-puntuable en ambos paths (agente: contra el
    // texto final; modelo/chat: contra la respuesta). Coding puede luego agregar
    // files/commands editando el benchmark.
    QVariantList prompts;
    for (const EvalTask &t : suite.tasks) {
        QVariantMap acc;
        acc["files"] = QVariantList{};
        acc["commands"] = QVariantList{};
        acc["expectSubstrings"] = QVariant(t.acceptance);
        QVariantMap p;
        p["id"] = t.id;
        p["prompt"] = t.prompt;
        p["isSpeed"] = false;
        p["maxTokens"] = 8000;
        p["category"] = t.category;
        p["weight"] = t.weight;
        p["attachments"] = QVariant(t.attachments);
        p["acceptance"] = acc;
        prompts.append(p);
    }

    QVariantMap def;
    def["name"] = suite.name.isEmpty()
        ? QStringLiteral("EvalSuite importada") : (QStringLiteral("Eval · ") + suite.name);
    def["description"] = suite.description;
    def["prompts"] = prompts;
    def["source"] = QStringLiteral("evalsuite");
    return saveCustomBenchmark(def);
}

void AppController::deleteCustomBenchmark(const QString &id)
{
    if (id.isEmpty()) return;
    QFile::remove(customBenchmarkDir() + "/" + id + ".json");
    loadCustomBenchmarks();
}

// ── Deep Research ─────────────────────────────────────────────────────────────

QString AppController::researchStorageDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/research");
    QDir().mkpath(dir);
    return dir;
}

void AppController::setResearchState(bool running, int progress, const QString &status)
{
    m_researchRunning = running;
    m_researchProgress = qBound(0, progress, 100);
    m_researchStatus = status;
    emit researchChanged();
}

void AppController::refreshResearchReports()
{
    const QString idxPath = researchStorageDir() + QStringLiteral("/index.json");
    QFile f(idxPath);
    QVariantList out;
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        QList<QJsonObject> objs;
        for (const QJsonValue &v : arr) objs.append(v.toObject());
        std::sort(objs.begin(), objs.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a.value(QStringLiteral("timestamp")).toDouble()
                   > b.value(QStringLiteral("timestamp")).toDouble();
        });
        for (const QJsonObject &o : objs)
            out.append(o.toVariantMap());
    }
    m_researchReports = out;
    emit researchReportsChanged();
}

void AppController::saveResearchReport(const QVariantMap &summary, const QString &markdown,
                                       const QJsonObject &full)
{
    const QString dir = researchStorageDir();
    const QString id = summary.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) return;

    QFile md(dir + QLatin1Char('/') + id + QStringLiteral(".md"));
    if (md.open(QIODevice::WriteOnly | QIODevice::Truncate))
        md.write(markdown.toUtf8());

    QFile json(dir + QLatin1Char('/') + id + QStringLiteral(".json"));
    if (json.open(QIODevice::WriteOnly | QIODevice::Truncate))
        json.write(QJsonDocument(full).toJson(QJsonDocument::Indented));

    const QString idxPath = dir + QStringLiteral("/index.json");
    QJsonArray idx;
    QFile idxFile(idxPath);
    if (idxFile.open(QIODevice::ReadOnly)) {
        idx = QJsonDocument::fromJson(idxFile.readAll()).array();
        idxFile.close();
    }
    QJsonArray kept;
    for (const QJsonValue &v : idx)
        if (v.toObject().value(QStringLiteral("id")).toString() != id)
            kept.append(v);
    kept.prepend(QJsonObject::fromVariantMap(summary));
    if (idxFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        idxFile.write(QJsonDocument(kept).toJson(QJsonDocument::Indented));

    refreshResearchReports();
}

// ── Auto-tuning de parámetros de inferencia ─────────────────────────────────
namespace {

// Espacio de búsqueda por defecto: flags ampliamente soportados por llama-server.
// cache-type-k/v marcados como qualityRisk: el gate de calidad impide que el
// optimizador colapse al quant más bajo solo por velocidad.
QVector<TunableParam> buildTuneParams(bool hasDraft = false, bool cpuOnly = false)
{
    using tuner::ParamSpec;
    if (cpuOnly) {
        return {
            {ParamSpec::categorical("ngl", {"0"}), "-ngl", false},
            {ParamSpec::categorical("threads", {"2", "4", "8", "12", "16"}), "--threads", false},
            {ParamSpec::categorical("batch", {"128", "256", "512", "1024"}), "-b", false},
            {ParamSpec::categorical("ubatch", {"64", "128", "256", "512"}), "-ub", false},
            {ParamSpec::categorical("cache-type-k", {"f16", "q8_0", "q4_0"}, true),
             "--cache-type-k", false},
            {ParamSpec::categorical("cache-type-v", {"f16", "q8_0", "q4_0"}, true),
             "--cache-type-v", false},
        };
    }
    QVector<TunableParam> params = {
        {ParamSpec::categorical("ngl", {"0", "20", "40", "99"}), "-ngl", false},
        {ParamSpec::categorical("batch", {"256", "512", "1024", "2048"}), "-b", false},
        // ubatch alto solía costar mucha VRAM (el compute graph escala con -ub).
        // La máscara FA en f16 (llama.cpp #23764, ~2x menos con MTP) lo abarata, así
        // que ahora exploramos 1024/2048: con flash-attn on suelen rendir más TPS sin
        // OOM. Los trials que igual no entren fallan/puntúan bajo y el TPE los descarta.
        {ParamSpec::categorical("ubatch", {"128", "256", "512", "1024", "2048"}), "-ub", false},
        {ParamSpec::categorical("flash-attn", {"off", "on"}), "--flash-attn", true},
        {ParamSpec::categorical("cache-type-k", {"f16", "q8_0", "q4_0"}, true),
         "--cache-type-k", false},
        {ParamSpec::categorical("cache-type-v", {"f16", "q8_0", "q4_0"}, true),
         "--cache-type-v", false},
    };
    // Si hay draft model (spec decoding / MTP), afinar spec-draft-n-max: el sweet
    // spot del acceptance/throughput varía por modelo (p.ej. 26B→1, 12B→2-3).
    if (hasDraft) {
        params.append({ParamSpec::intRange("spec-draft-n-max", 1, 5, 1),
                       "--spec-draft-n-max", false});
    }
    return params;
}

QString siblingToolPath(const QString &binaryPath, const QString &baseName)
{
    const QFileInfo info(binaryPath);
#ifdef Q_OS_WIN
    const QString exe = baseName + QStringLiteral(".exe");
#else
    const QString exe = baseName;
#endif
    const QString p = info.absoluteDir().absoluteFilePath(exe);
    return QFileInfo::exists(p) ? p : QString();
}

QString defaultPerplexityCorpusPath()
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/Moby Dick.txt"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/benchmarks/perplexity.txt"),
        QDir::current().absoluteFilePath(QStringLiteral("assets/benchmarks/perplexity.txt")),
        QDir::current().absoluteFilePath(QStringLiteral("README.md")),
    };
    for (const QString &p : candidates)
        if (QFileInfo::exists(p)) return p;
    return QString();
}

// Quita flags (y su valor adyacente, salvo switches) de una lista de args.
QStringList stripFlags(const QStringList &args, const QSet<QString> &valueFlags,
                       const QSet<QString> &switchFlags)
{
    QStringList out;
    for (int i = 0; i < args.size(); ++i) {
        const QString &a = args[i];
        if (switchFlags.contains(a)) continue;          // switch sin valor
        if (valueFlags.contains(a)) { ++i; continue; }  // flag + valor → saltar ambos
        out << a;
    }
    return out;
}

}  // namespace

void AppController::startAutoTune(const QString &launchProfileId, int maxTrials,
                                  double qualityGate, int nPredict, const QString &mode)
{
    if (m_autoTuneRunning) {
        emit serverError(QStringLiteral("Auto-tune ya en curso."));
        return;
    }
    if (serverRunning()) {
        emit serverError(QStringLiteral("Detené el servidor antes de auto-tunear."));
        return;
    }

    computeEffectiveProfile(launchProfileId);
    if (!m_effectiveProfile.value(QStringLiteral("isValid"), false).toBool()) {
        emit serverError(QStringLiteral("Perfil inválido: no se puede auto-tunear."));
        return;
    }

    const QString binaryPath = m_effectiveProfile.value(QStringLiteral("binaryPath")).toString();
    const QStringList effArgs = m_effectiveProfile.value(QStringLiteral("effectiveArgs")).toStringList();
    const QVariantMap effEnv = m_effectiveProfile.value(QStringLiteral("effectiveEnv")).toMap();

    const QString normalizedMode = mode.trimmed().toLower();
    const bool cpuOnly = normalizedMode == QLatin1String("cpu")
                         || normalizedMode == QLatin1String("cpu_only");
    const bool hasDraft = !cpuOnly && (effArgs.contains(QStringLiteral("--draft-model"))
                          || effArgs.contains(QStringLiteral("-md")));
    QVector<TunableParam> params = buildTuneParams(hasDraft, cpuOnly);

    // baseArgs = args efectivos menos host/port y menos los flags que vamos a
    // afinar (con sus aliases), para no duplicarlos.
    const QSet<QString> valueFlags = {
        QStringLiteral("--host"), QStringLiteral("--port"),
        QStringLiteral("-ngl"), QStringLiteral("--n-gpu-layers"), QStringLiteral("--gpu-layers"),
        QStringLiteral("--threads"),
        QStringLiteral("-b"), QStringLiteral("--batch-size"),
        QStringLiteral("-ub"), QStringLiteral("--ubatch-size"),
        QStringLiteral("--cache-type-k"), QStringLiteral("-ctk"),
        QStringLiteral("--cache-type-v"), QStringLiteral("-ctv"),
        QStringLiteral("--spec-draft-n-max"),
    };
    const QSet<QString> switchFlags = {
        QStringLiteral("--flash-attn"), QStringLiteral("-fa"),
    };
    const QStringList baseArgs = stripFlags(effArgs, valueFlags, switchFlags);

    TunerJob job;
    job.binaryPath = binaryPath;
    for (auto it = effEnv.begin(); it != effEnv.end(); ++it)
        job.env.insert(it.key(), it.value().toString());
    job.baseArgs = baseArgs;
    job.host = QStringLiteral("127.0.0.1");
    job.port = 18099;  // puerto scratch, no colisiona con el server principal
    job.evalPrompt = QStringLiteral(
        "Write a Python function is_prime(n: int) -> bool with type hints. "
        "Handle edge cases (n<=1, n=2). Return only code.");
    job.nPredict = nPredict > 0 ? nPredict : 256;
    job.acceptance = {QStringLiteral("def is_prime"), QStringLiteral("return")};
    job.params = params;
    job.cpuOnly = cpuOnly;
    job.perplexityBinaryPath = siblingToolPath(binaryPath, QStringLiteral("llama-perplexity"));
    job.perplexityCorpusPath = defaultPerplexityCorpusPath();
    job.usePerplexityGate = !job.perplexityBinaryPath.isEmpty() && !job.perplexityCorpusPath.isEmpty();
    job.perplexityThresholdPct = 3.0;
    job.settings.maxTrials = qMax(1, maxTrials);
    job.settings.startupTrials = qMax(1, qMin(8, job.settings.maxTrials / 2));
    job.settings.qualityGate = qualityGate;
    job.settings.seed = 0;

    m_autoTuneLaunchId = launchProfileId;
    m_autoTuneRunning = true;
    m_autoTuneProgress = 0;
    m_autoTuneStatus = QStringLiteral("Iniciando auto-tune%1 (%2 trials)%3…")
                           .arg(cpuOnly ? QStringLiteral(" CPU") : QString())
                           .arg(job.settings.maxTrials)
                           .arg(job.usePerplexityGate ? QStringLiteral(" + PPL") : QString());
    emit autoTuneChanged();

    m_tuneThread = new QThread(this);
    m_tuneWorker = new TunerWorker(job);   // sin parent: se mueve de hilo
    m_tuneWorker->moveToThread(m_tuneThread);

    connect(m_tuneThread, &QThread::started, m_tuneWorker, &TunerWorker::run);

    connect(m_tuneWorker, &TunerWorker::trial, this,
            [this](int index, int total, double tps, double quality, const QString &summary) {
                m_autoTuneProgress = total > 0 ? (index * 100 / total) : 0;
                m_autoTuneStatus = QStringLiteral("Trial %1/%2 — %3 tok/s, calidad %4 [%5]")
                                       .arg(index).arg(total)
                                       .arg(tps, 0, 'f', 1).arg(quality, 0, 'f', 2)
                                       .arg(summary);
                emit autoTuneChanged();
                emit autoTuneTrial(index, total, tps, quality, summary);
            });

    connect(m_tuneWorker, &TunerWorker::finished, this,
            [this](bool ok, const QStringList &bestArgs, double tps, double quality) {
                onAutoTuneFinished(ok, bestArgs, tps, quality);
            });

    // Limpieza del hilo al terminar.
    connect(m_tuneWorker, &TunerWorker::finished, m_tuneThread, &QThread::quit);
    connect(m_tuneThread, &QThread::finished, this, [this]() {
        if (m_tuneWorker) { m_tuneWorker->deleteLater(); m_tuneWorker = nullptr; }
        if (m_tuneThread) { m_tuneThread->deleteLater(); m_tuneThread = nullptr; }
    });

    m_tuneThread->start();
}

void AppController::cancelAutoTune()
{
    if (!m_autoTuneRunning || !m_tuneWorker) return;
    m_autoTuneStatus = QStringLiteral("Cancelando auto-tune…");
    emit autoTuneChanged();
    m_tuneWorker->cancel();   // setea atomic; corta tras el trial en curso
}

void AppController::onAutoTuneFinished(bool ok, const QStringList &bestArgs,
                                       double throughput, double quality)
{
    m_autoTuneRunning = false;
    m_autoTuneProgress = 100;

    QString mergedSummary;
    if (ok && !bestArgs.isEmpty() && !m_autoTuneLaunchId.isEmpty()) {
        // No sobrescribir el perfil original: clonarlo en uno nuevo "-tuned" con
        // la mejor config fusionada en extraArgs (reemplazando flags previos de
        // los mismos parámetros).
        const LaunchProfile src = m_profiles.resolveLaunch(m_autoTuneLaunchId);
        const QString srcDisplay = src.alias.isEmpty() ? src.name : src.alias;

        // addLaunchProfile ya quita el prefijo "N_" y asigna uno nuevo.
        const QString newId = m_profiles.addLaunchProfile(
            src.name + QStringLiteral("-tuned"),
            src.backendProfileId, src.modelProfileId, src.runtimePresetId);

        const QSet<QString> valueFlags = {
            QStringLiteral("-ngl"), QStringLiteral("--n-gpu-layers"), QStringLiteral("--gpu-layers"),
            QStringLiteral("--threads"),
            QStringLiteral("-b"), QStringLiteral("--batch-size"),
            QStringLiteral("-ub"), QStringLiteral("--ubatch-size"),
            QStringLiteral("--cache-type-k"), QStringLiteral("-ctk"),
            QStringLiteral("--cache-type-v"), QStringLiteral("-ctv"),
            QStringLiteral("--spec-draft-n-max"),
        };
        const QSet<QString> switchFlags = {
            QStringLiteral("--flash-attn"), QStringLiteral("-fa"),
        };
        QStringList extra = stripFlags(src.extraArgs, valueFlags, switchFlags);
        extra += bestArgs;

        QVariantMap np = m_profiles.getLaunchProfile(newId);
        np[QStringLiteral("extraArgs")] = extra;
        np[QStringLiteral("harnessProfileId")] = src.harnessProfileId;
        np[QStringLiteral("workspaceProfileId")] = src.workspaceProfileId;
        np[QStringLiteral("alias")] = QStringLiteral("Auto-tuned: %1").arg(srcDisplay);
        m_profiles.updateLaunchProfile(np);
        m_profiles.saveProfiles();

        mergedSummary = bestArgs.join(QLatin1Char(' '));
        m_autoTuneStatus = QStringLiteral("Auto-tune OK: %1 tok/s, calidad %2. "
                                          "Perfil nuevo creado con: %3")
                               .arg(throughput, 0, 'f', 1).arg(quality, 0, 'f', 2)
                               .arg(mergedSummary);
    } else {
        m_autoTuneStatus = ok ? QStringLiteral("Auto-tune sin cambios aplicables.")
                              : QStringLiteral("Auto-tune sin config válida (¿server no arrancó?).");
    }

    emit autoTuneChanged();
    emit autoTuneFinished(ok, mergedSummary, throughput, quality);
}

void AppController::startResearch(const QString &topic, const QString &mode, int maxPages)
{
    const QString cleanTopic = topic.trimmed();
    if (cleanTopic.isEmpty()) return;
    if (m_researchRunning) {
        emit serverError(QStringLiteral("Ya hay una investigación en curso."));
        return;
    }
    if (!serverRunning() || !serverReady()) {
        emit serverError(QStringLiteral("Deep Research necesita el servidor listo para sintetizar el reporte."));
        return;
    }

    if (!m_nam) m_nam = new QNetworkAccessManager(this);
    maxPages = qBound(2, maxPages <= 0 ? 6 : maxPages, 10);

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString normalizedMode = mode.trimmed().isEmpty() ? QStringLiteral("auto") : mode.trimmed();
    const QStringList queries = researchQueriesFor(cleanTopic, normalizedMode);
    auto hits = std::make_shared<QVector<ResearchHit>>();
    auto sourceTexts = std::make_shared<QStringList>();
    auto sources = std::make_shared<QJsonArray>();
    auto searchLogs = std::make_shared<QStringList>();
    auto addHit = [hits](const ResearchHit &h) {
        if (h.url.isEmpty()) return;
        for (const ResearchHit &existing : std::as_const(*hits))
            if (existing.url == h.url) return;
        hits->append(h);
    };

    setResearchState(true, 1, QStringLiteral("Preparando búsqueda..."));

    auto fail = [this](const QString &message) {
        if (m_researchReply) {
            m_researchReply->deleteLater();
            m_researchReply = nullptr;
        }
        setResearchState(false, 0, message);
        emit serverError(message);
    };

    auto synthesize = std::make_shared<std::function<void()>>();
    auto fetchNext = std::make_shared<std::function<void(int)>>();
    auto searchNext = std::make_shared<std::function<void(int)>>();

    *synthesize = [=]() {
        setResearchState(true, 82, QStringLiteral("Sintetizando reporte..."));

        QStringList sourceLines;
        for (int i = 0; i < sources->size(); ++i) {
            const QJsonObject s = sources->at(i).toObject();
            sourceLines << QStringLiteral("[%1] %2 — %3")
                               .arg(i + 1)
                               .arg(s.value(QStringLiteral("title")).toString(),
                                    s.value(QStringLiteral("url")).toString());
        }

        QString dossier;
        dossier += QStringLiteral("# Dossier: %1\n\n").arg(cleanTopic);
        dossier += QStringLiteral("Mode: %1\n\n").arg(researchModeTitle(normalizedMode));
        dossier += QStringLiteral("## Search log\n%1\n\n").arg(searchLogs->join(QLatin1Char('\n')));
        dossier += QStringLiteral("## Sources\n%1\n\n").arg(sourceLines.join(QLatin1Char('\n')));
        dossier += QStringLiteral("## Extracts\n%1\n").arg(sourceTexts->join(QStringLiteral("\n\n")));

        const QString sys = QStringLiteral(
            "Sos un investigador técnico. Sintetizá fuentes web en español. "
            "Separá hechos confirmados de inferencias, citá fuentes como [1], [2], "
            "marcá contradicciones y no inventes evidencia.");
        const QString user = QStringLiteral(
            "Tema: %1\nModo: %2\n\n"
            "Usá el dossier de fuentes de abajo y devolvé un reporte Markdown con: "
            "Resumen ejecutivo, Hallazgos clave, Evidencia, Riesgos/limitaciones, "
            "Próximos pasos. Para Product/Compare agregá matriz de recomendación; "
            "para How-to pasos; para Fact-check veredictos.\n\n%3")
            .arg(cleanTopic, researchModeTitle(normalizedMode), dossier.left(30000));

        QJsonObject payload{
            {QStringLiteral("model"), QStringLiteral("research")},
            {QStringLiteral("messages"), QJsonArray{
                QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                            {QStringLiteral("content"), sys}},
                QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                            {QStringLiteral("content"), user}}}},
            {QStringLiteral("stream"), false},
            {QStringLiteral("temperature"), 0.2},
            {QStringLiteral("max_tokens"), 2200},
            {QStringLiteral("cache_prompt"), true}
        };
        payload.insert(QStringLiteral("reasoning_budget"), m_agentThinkingEnabled ? -1 : 0);
        payload.insert(QStringLiteral("chat_template_kwargs"),
                       QJsonObject{{QStringLiteral("enable_thinking"), m_agentThinkingEnabled}});

        QNetworkRequest req(QUrl(serverBaseUrl() + QStringLiteral("/v1/chat/completions")));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        req.setTransferTimeout(180000);
        m_researchReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
        connect(m_researchReply, &QNetworkReply::finished, this, [=]() {
            QNetworkReply *reply = m_researchReply;
            m_researchReply = nullptr;
            if (!reply) return;
            const QByteArray raw = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            const QString err = reply->errorString();
            reply->deleteLater();
            if (!m_researchRunning) return;

            QString report;
            if (ok) {
                const QJsonObject root = QJsonDocument::fromJson(raw).object();
                const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
                if (!choices.isEmpty())
                    report = choices.first().toObject()
                                 .value(QStringLiteral("message")).toObject()
                                 .value(QStringLiteral("content")).toString().trimmed();
            }
            if (report.isEmpty()) {
                report = QStringLiteral("# Deep Research: %1\n\n"
                                        "> No se pudo sintetizar con el modelo (%2). "
                                        "Se guarda el dossier crudo.\n\n%3")
                             .arg(cleanTopic, ok ? QStringLiteral("respuesta vacía") : err, dossier);
            }

            const double ts = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
            const QString title = cleanTopic.left(96);
            QVariantMap summary{
                {QStringLiteral("id"), id},
                {QStringLiteral("title"), title},
                {QStringLiteral("topic"), cleanTopic},
                {QStringLiteral("mode"), normalizedMode},
                {QStringLiteral("modeLabel"), researchModeTitle(normalizedMode)},
                {QStringLiteral("timestamp"), ts},
                {QStringLiteral("sourceCount"), sources->size()},
                {QStringLiteral("path"), researchStorageDir() + QLatin1Char('/') + id + QStringLiteral(".md")}
            };
            QJsonObject full{
                {QStringLiteral("id"), id},
                {QStringLiteral("topic"), cleanTopic},
                {QStringLiteral("mode"), normalizedMode},
                {QStringLiteral("timestamp"), ts},
                {QStringLiteral("sources"), *sources},
                {QStringLiteral("dossier"), dossier},
                {QStringLiteral("report"), report}
            };
            saveResearchReport(summary, report, full);
            setResearchState(false, 100, QStringLiteral("Reporte guardado."));
        });
    };

    *fetchNext = [=](int index) {
        const int wanted = qMin(maxPages, hits->size());
        if (index >= wanted) {
            if (sources->isEmpty()) {
                fail(QStringLiteral("No se pudieron descargar fuentes útiles."));
                return;
            }
            (*synthesize)();
            return;
        }

        const ResearchHit h = hits->at(index);
        setResearchState(true, 35 + (index * 45 / qMax(1, wanted)),
                         QStringLiteral("Leyendo fuente %1/%2...").arg(index + 1).arg(wanted));
        QNetworkRequest req(QUrl(h.url));
        req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Mozilla/5.0 LlamaCode/0.1"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(25000);
        m_researchReply = m_nam->get(req);
        connect(m_researchReply, &QNetworkReply::finished, this, [=]() {
            QNetworkReply *reply = m_researchReply;
            m_researchReply = nullptr;
            if (!reply) return;
            const QByteArray raw = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!m_researchRunning) return;

            QString text;
            if (ok && !raw.isEmpty())
                text = researchCleanHtmlToText(QString::fromUtf8(raw)).left(4200);
            if (!text.trimmed().isEmpty()) {
                sources->append(QJsonObject{
                    {QStringLiteral("title"), h.title.isEmpty() ? h.url : h.title},
                    {QStringLiteral("url"), h.url},
                    {QStringLiteral("snippet"), h.snippet}
                });
                sourceTexts->append(QStringLiteral("### [%1] %2\n%3")
                                        .arg(sources->size())
                                        .arg(h.title.isEmpty() ? h.url : h.title,
                                             text));
            }
            (*fetchNext)(index + 1);
        });
    };

    *searchNext = [=](int index) {
        if (index >= queries.size() || hits->size() >= maxPages) {
            if (hits->isEmpty()) {
                fail(QStringLiteral("No se encontraron resultados para la investigación."));
                return;
            }
            (*fetchNext)(0);
            return;
        }

        const QString query = queries.at(index);
        setResearchState(true, 5 + (index * 25 / qMax(1, queries.size())),
                         QStringLiteral("Buscando: %1").arg(query.left(80)));

        const QString searxng = qEnvironmentVariable("LLAMACODE_SEARXNG_URL").trimmed();
        QUrl url;
        if (!searxng.isEmpty()) {
            url = QUrl(searxng.endsWith(QLatin1Char('/'))
                           ? searxng + QStringLiteral("search")
                           : searxng + QStringLiteral("/search"));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("q"), query);
            q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
            url.setQuery(q);
        } else {
            url = QUrl(QStringLiteral("https://html.duckduckgo.com/html/"));
            QUrlQuery q;
            q.addQueryItem(QStringLiteral("q"), query);
            url.setQuery(q);
        }

        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Mozilla/5.0 LlamaCode/0.1"));
        req.setTransferTimeout(25000);
        m_researchReply = m_nam->get(req);
        connect(m_researchReply, &QNetworkReply::finished, this, [=]() {
            QNetworkReply *reply = m_researchReply;
            m_researchReply = nullptr;
            if (!reply) return;
            const QByteArray raw = reply->readAll();
            const bool ok = reply->error() == QNetworkReply::NoError;
            reply->deleteLater();
            if (!m_researchRunning) return;

            int addedBefore = hits->size();
            if (ok && !raw.isEmpty()) {
                if (!searxng.isEmpty()) {
                    const QJsonArray arr = QJsonDocument::fromJson(raw).object()
                                               .value(QStringLiteral("results")).toArray();
                    for (const QJsonValue &v : arr) {
                        const QJsonObject o = v.toObject();
                        addHit({o.value(QStringLiteral("title")).toString(),
                                o.value(QStringLiteral("url")).toString(),
                                o.value(QStringLiteral("content")).toString()});
                        if (hits->size() >= maxPages) break;
                    }
                } else {
                    const QVector<ResearchHit> parsed = researchParseDdg(QString::fromUtf8(raw), 6);
                    for (const ResearchHit &h : parsed) {
                        addHit(h);
                        if (hits->size() >= maxPages) break;
                    }
                }
            }
            searchLogs->append(QStringLiteral("- \"%1\" -> %2 new result(s)")
                                   .arg(query).arg(hits->size() - addedBefore));
            (*searchNext)(index + 1);
        });
    };

    (*searchNext)(0);
}

void AppController::cancelResearch()
{
    if (m_researchReply) {
        QNetworkReply *reply = m_researchReply;
        m_researchReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (m_researchRunning)
        setResearchState(false, 0, QStringLiteral("Investigación cancelada."));
}

QString AppController::readResearchReport(const QString &id) const
{
    if (id.trimmed().isEmpty()) return QString();
    QFile f(researchStorageDir() + QLatin1Char('/') + id + QStringLiteral(".md"));
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

void AppController::openResearchReport(const QString &id)
{
    if (id.trimmed().isEmpty()) return;
    const QString path = researchStorageDir() + QLatin1Char('/') + id + QStringLiteral(".md");
    if (!QFileInfo::exists(path)) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void AppController::deleteResearchReport(const QString &id)
{
    if (id.trimmed().isEmpty()) return;
    const QString dir = researchStorageDir();
    QFile::remove(dir + QLatin1Char('/') + id + QStringLiteral(".md"));
    QFile::remove(dir + QLatin1Char('/') + id + QStringLiteral(".json"));

    const QString idxPath = dir + QStringLiteral("/index.json");
    QFile f(idxPath);
    QJsonArray kept;
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        f.close();
        for (const QJsonValue &v : arr)
            if (v.toObject().value(QStringLiteral("id")).toString() != id)
                kept.append(v);
    }
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(kept).toJson(QJsonDocument::Indented));
    refreshResearchReports();
}

// ── Modo Charla (voz-a-voz) ──────────────────────────────────────────────────

QVariantMap AppController::voiceConfig(const QString &profileId) const
{
    return m_profiles.getLaunchVoice(profileId);
}

void AppController::setVoiceConfig(const QString &profileId, const QVariantMap &cfg)
{
    m_profiles.setLaunchVoice(profileId, cfg);
    if (m_voice && profileId == m_activeLaunchId) applyVoiceConfig();
}

void AppController::ensureVoice()
{
    if (m_voice) return;
    m_voice = new VoiceController(this);
    connect(m_voice, &VoiceController::transcriptReady, this, [this](const QString &text) {
        m_voicePartial.clear();
        emit voicePartialChanged();
        sendChatMessage(text);
    });
    connect(m_voice, &VoiceController::partialTranscript, this, [this](const QString &text) {
        m_voicePartial = text;
        emit voicePartialChanged();
    });
    connect(m_voice, &VoiceController::stateChanged, this, &AppController::voiceStateChanged);
    connect(m_voice, &VoiceController::errorChanged, this, &AppController::voiceStateChanged);
    connect(m_voice, &VoiceController::levelChanged, this, &AppController::voiceLevelChanged);
}

void AppController::applyVoiceConfig()
{
    if (!m_voice) return;
    // La Charla usa la config de voz del perfil activo (el que lanzó el server).
    VoiceConfig c = VoiceConfig::fromJson(
        QJsonObject::fromVariantMap(m_profiles.getLaunchVoice(m_activeLaunchId)));
    // STT gestionado: apuntar al server local que lanza la app (whisper.cpp).
    if (!c.sttManagedEngine.isEmpty()) {
        const QVariantMap eng = VoiceServerManager::sttEngine(c.sttManagedEngine);
        const int port = eng.value("defaultPort", 8081).toInt();
        c.sttProvider = QStringLiteral("local");
        c.sttBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
        c.sttEndpointPath = VoiceServerManager::endpointPath(c.sttManagedEngine);
    }
    const QString sttKey = c.sttKeyRef.isEmpty() ? QString() : m_secrets.resolve(c.sttKeyRef);
    const QString ttsKey = c.ttsKeyRef.isEmpty() ? QString() : m_secrets.resolve(c.ttsKeyRef);
    m_voice->setConfig(c, sttKey, ttsKey);
    m_voice->setInputDevice(voiceInputDevice());
    // TTS piper (process-mode): resolver binario + voz instalada.
    if (c.ttsMode == QLatin1String("piper"))
        m_voice->setTtsPiper(voicePiperPath(),
                             VoiceServerManager::ttsModelPath(c.ttsManagedVoice));
}

QVariantList AppController::audioInputDevices() const
{
    return VoiceController::inputDevices();
}

QString AppController::voiceInputDevice() const
{
    return readSetting(QStringLiteral("voiceInputDevice")).toString();
}

void AppController::setVoiceInputDevice(const QString &id)
{
    writeSetting(QStringLiteral("voiceInputDevice"), id);
    if (m_voice) m_voice->setInputDevice(id);
}

void AppController::startMicTest()
{
    ensureVoice();
    applyVoiceConfig();
    m_voice->micTest();
}

void AppController::stopMicTest()
{
    if (m_voice) m_voice->stopMicTest();
}

void AppController::startCharla()
{
    ensureChatBackend();   // la voz reusa el backend de chat (sesiones/stream)
    ensureVoice();
    // Si el perfil activo usa un STT gestionado, lanzar whisper-server primero.
    const VoiceConfig c = VoiceConfig::fromJson(
        QJsonObject::fromVariantMap(m_profiles.getLaunchVoice(m_activeLaunchId)));
    if (!c.sttManagedEngine.isEmpty()) {
        if (!m_voiceServers.modelInstalled(c.sttManagedEngine)) {
            emit serverError(QStringLiteral("Modelo STT no instalado: %1. Instalalo desde Charla.")
                             .arg(c.sttManagedEngine));
            return;     // no arrancar la escucha: el STT no funcionaría
        }
        startManagedStt(c);
    }
    applyVoiceConfig();
    m_charlaActive = true;
    m_voice->start();
}

void AppController::stopCharla()
{
    m_charlaActive = false;
    if (m_voice) m_voice->stop();
    stopManagedStt();
}

// ── STT gestionado (whisper.cpp) ─────────────────────────────────────────────

QVariantList AppController::voiceSttCatalog() const { return VoiceServerManager::sttCatalog(); }

bool AppController::voiceModelInstalled(const QString &engineId) const
{
    return m_voiceServers.modelInstalled(engineId);
}

void AppController::installVoiceModel(const QString &engineId)
{
    m_voiceServers.installModel(engineId);
}

void AppController::cancelVoiceModelInstall() { m_voiceServers.cancelInstall(); }

QString AppController::voiceWhisperServerPath() const
{
    return readSetting(QStringLiteral("voiceWhisperServerPath")).toString();
}

void AppController::setVoiceWhisperServerPath(const QString &path)
{
    writeSetting(QStringLiteral("voiceWhisperServerPath"), path);
}

QString AppController::pickVoiceWhisperServer()
{
    const QString p = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("Seleccionar binario whisper-server"), QString(),
#ifdef Q_OS_WIN
        QStringLiteral("Ejecutables (*.exe);;Todos (*)"));
#else
        QStringLiteral("Todos (*)"));
#endif
    if (!p.isEmpty()) setVoiceWhisperServerPath(p);
    return p;
}

QVariantList AppController::voiceTtsCatalog() const { return VoiceServerManager::ttsCatalog(); }

bool AppController::voiceTtsVoiceInstalled(const QString &voiceId) const
{
    return m_voiceServers.ttsVoiceInstalled(voiceId);
}

void AppController::installVoiceTts(const QString &voiceId)
{
    m_voiceServers.installTtsVoice(voiceId);
}

QString AppController::voicePiperPath() const
{
    return readSetting(QStringLiteral("voicePiperPath")).toString();
}

void AppController::setVoicePiperPath(const QString &path)
{
    writeSetting(QStringLiteral("voicePiperPath"), path);
}

QString AppController::pickVoicePiper()
{
    const QString p = QFileDialog::getOpenFileName(
        nullptr, QStringLiteral("Seleccionar binario piper"), QString(),
#ifdef Q_OS_WIN
        QStringLiteral("Ejecutables (*.exe);;Todos (*)"));
#else
        QStringLiteral("Todos (*)"));
#endif
    if (!p.isEmpty()) setVoicePiperPath(p);
    return p;
}

void AppController::installVoiceBinary(const QString &kind, const QString &urlOverride)
{
    m_voiceServers.installBinary(kind, urlOverride);
}

QString AppController::voiceBinaryDefaultUrl(const QString &kind) const
{
    return VoiceServerManager::defaultBinaryUrl(kind);
}

void AppController::startManagedStt(const VoiceConfig &c)
{
    stopManagedStt();
    if (!m_voiceServers.modelInstalled(c.sttManagedEngine)) {
        emit serverError(QStringLiteral("Modelo STT no instalado: %1. Instalalo desde Charla.")
                         .arg(c.sttManagedEngine));
        return;
    }
    // Resolver el binario whisper-server (setting o PATH).
    QString prog = voiceWhisperServerPath();
    if (prog.isEmpty()) prog = QStringLiteral("whisper-server");
    const QVariantMap eng = VoiceServerManager::sttEngine(c.sttManagedEngine);
    const int port = eng.value("defaultPort", 8081).toInt();
    const QStringList args = VoiceServerManager::buildWhisperArgs(
        VoiceServerManager::modelPath(c.sttManagedEngine),
        QStringLiteral("127.0.0.1"), port, c.sttLanguage);

    m_sttProc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LLAMACODE_MANAGED"), QStringLiteral("1"));
    env.insert(QStringLiteral("LLAMACODE_ROLE"), QStringLiteral("voice-stt"));
    m_sttProc->setProcessEnvironment(env);
    connect(m_sttProc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit serverError(QStringLiteral("No se pudo lanzar whisper-server. Configurá su ruta en Charla."));
    });
    m_sttProc->start(prog, args);
    if (m_sttProc->waitForStarted(4000))
        assignToJobObject(m_sttProc->processId());
}

void AppController::stopManagedStt()
{
    if (!m_sttProc) return;
    m_sttProc->terminate();
    if (!m_sttProc->waitForFinished(2000)) m_sttProc->kill();
    m_sttProc->deleteLater();
    m_sttProc = nullptr;
}

void AppController::charlaListen()
{
    if (m_voice) m_voice->startListening();
}

QString AppController::voiceState() const { return m_voice ? m_voice->stateStr() : QStringLiteral("idle"); }
bool    AppController::voiceActive() const { return m_voice && m_voice->active(); }
double  AppController::voiceLevel() const { return m_voice ? m_voice->level() : 0.0; }
QString AppController::voiceError() const { return m_voice ? m_voice->lastError() : QString(); }
