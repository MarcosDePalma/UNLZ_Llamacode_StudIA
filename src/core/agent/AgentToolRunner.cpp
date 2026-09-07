#include "AgentToolRunner.h"
#include "McpClient.h"
#include <QRegularExpression>
#include <algorithm>
#include "LlamaAgentBackend.h"   // LlamaAgentBackend::makeDiff (static)
#include "MemoryStore.h"         // memoria por capas (hechos atómicos)
#include "GraphStore.h"          // knowledge graph (entidades + relaciones)
#include "CodeGraphIndexer.h"     // graph action='index': repo→GraphStore determinista
#include "BrowserTeach.h"        // skills de browser grabados (modo teach)
#include "PortableSkillStore.h"  // habilidades declarativas con carga progresiva
#include "AgentEventLog.h"       // tool recent_actions (tail del rastro del agente)
#include "WorkRegistry.h"         // estado de trabajo activo por proyecto
#include "ToolExecutionSafety.h"
#include "StructuredSourceView.h" // vista compacta segura y proyectable
#include "ProjectBrain.h"
#include "ContextIndex.h"
#include "HotspotAnalyzer.h"     // tool code_hotspots (archivos riesgosos)
#include "core/DocumentExtractor.h" // hybrid_search include_docs: pdf/office al índice
#include "WebFetchProvider.h"
#include "core/automation/DesktopAutomationBackend.h"
#include "core/automation/DesktopComputerUse.h"
#include "core/automation/DesktopRecoveryPolicy.h"
#include "core/automation/AutomationArtifactStore.h"
#include "core/mail/MailClient.h" // tools email_send/list/read

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <QJsonObject>
#include <QStandardPaths>
#include <QThread>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <cmath>
#include <cstring>

static const QString kMcpPrefix = QStringLiteral("mcp__");

namespace {

QString mcpLedgerPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/tool_receipts");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/mcp_idempotency.json");
}

QJsonObject loadMcpLedger()
{
    QFile file(mcpLedgerPath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void saveMcpLedger(QJsonObject ledger)
{
    // Limitar crecimiento: conservar las 1000 entradas más recientes.
    if (ledger.size() > 1000) {
        QStringList keys = ledger.keys();
        std::sort(keys.begin(), keys.end(), [&ledger](const QString &a, const QString &b) {
            return ledger.value(a).toObject().value(QStringLiteral("ts")).toString()
                 < ledger.value(b).toObject().value(QStringLiteral("ts")).toString();
        });
        while (ledger.size() > 1000 && !keys.isEmpty())
            ledger.remove(keys.takeFirst());
    }
    QSaveFile file(mcpLedgerPath());
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(ledger).toJson(QJsonDocument::Compact));
    file.commit();
}

const McpClient::ToolDef *findMcpTool(McpClient *client, const QString &name)
{
    if (!client) return nullptr;
    for (const McpClient::ToolDef &tool : client->tools())
        if (tool.name == name) return &tool;
    return nullptr;
}

bool mcpToolAcceptsEmptyArguments(const McpClient::ToolDef &tool)
{
    const QJsonArray required = tool.inputSchema.value(QStringLiteral("required")).toArray();
    return required.isEmpty();
}

bool isBrowserMcpTool(const QString &name)
{
    const QString lower = name.toLower();
    return lower.startsWith(QStringLiteral("browser_"))
        || lower.contains(QStringLiteral("browser"))
        || lower.contains(QStringLiteral("screenshot"));
}

QString imageExtension(const QString &mimeType)
{
    const QString mime = mimeType.toLower();
    if (mime.contains(QStringLiteral("png"))) return QStringLiteral("png");
    if (mime.contains(QStringLiteral("webp"))) return QStringLiteral("webp");
    if (mime.contains(QStringLiteral("gif"))) return QStringLiteral("gif");
    return QStringLiteral("jpg");
}

QString saveMcpImage(const QJsonObject &rawResult, const QString &prefix)
{
    const QJsonArray content = rawResult.value(QStringLiteral("content")).toArray();
    for (const QJsonValue &value : content) {
        const QJsonObject block = value.toObject();
        QString data = block.value(QStringLiteral("data")).toString();
        QString mime = block.value(QStringLiteral("mimeType")).toString();
        if (data.isEmpty() && block.value(QStringLiteral("resource")).isObject()) {
            const QJsonObject resource = block.value(QStringLiteral("resource")).toObject();
            data = resource.value(QStringLiteral("blob")).toString();
            mime = resource.value(QStringLiteral("mimeType")).toString(mime);
        }
        if (data.isEmpty()) continue;

        const QByteArray bytes = QByteArray::fromBase64(data.toLatin1());
        // Untrusted MCP servers should not be able to fill AppLocalData with an
        // unbounded payload just by returning an image block.
        if (bytes.isEmpty() || bytes.size() > 20 * 1024 * 1024) continue;
        const QString dir = AutomationArtifactStore::rootDir()
                            + QStringLiteral("/runtime-observations");
        QDir().mkpath(dir);
        const QString path = dir + QLatin1Char('/') + prefix + QStringLiteral("-%1.%2")
            .arg(QDateTime::currentMSecsSinceEpoch()).arg(imageExtension(mime));
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) continue;
        if (file.write(bytes) != bytes.size() || !file.commit()) continue;
        AutomationArtifactStore::cleanupRuntimeObservations();
        return path;
    }
    return {};
}

QString saveMcpSnapshot(const QJsonObject &rawResult, const QString &prefix)
{
    QJsonObject safe;
    QJsonArray safeContent;
    for (const QJsonValue &value : rawResult.value(QStringLiteral("content")).toArray()) {
        const QJsonObject block = value.toObject();
        const QString type = block.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text")) {
            safeContent.append(QJsonObject{{QStringLiteral("type"), type},
                                           {QStringLiteral("text"), block.value(QStringLiteral("text"))}});
        } else if (type == QLatin1String("resource")) {
            const QJsonObject resource = block.value(QStringLiteral("resource")).toObject();
            safeContent.append(QJsonObject{
                {QStringLiteral("type"), type},
                {QStringLiteral("uri"), resource.value(QStringLiteral("uri"))},
                {QStringLiteral("mimeType"), resource.value(QStringLiteral("mimeType"))}});
        } else if (type == QLatin1String("image")) {
            safeContent.append(QJsonObject{
                {QStringLiteral("type"), type},
                {QStringLiteral("mimeType"), block.value(QStringLiteral("mimeType"))}});
        }
    }
    if (!safeContent.isEmpty()) safe[QStringLiteral("content")] = safeContent;
    const QJsonValue structured = rawResult.value(QStringLiteral("structuredContent"));
    if (structured.isObject()) safe[QStringLiteral("structuredContent")] = structured;
    if (safe.isEmpty()) return {};

    QByteArray bytes = AutomationArtifactStore::redact(QString::fromUtf8(
        QJsonDocument(safe).toJson(QJsonDocument::Indented))).toUtf8();
    if (bytes.size() > 2 * 1024 * 1024) {
        bytes.truncate(2 * 1024 * 1024);
        bytes.append("\n[truncated]\n");
    }
    const QString dir = AutomationArtifactStore::rootDir()
                        + QStringLiteral("/runtime-observations");
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + prefix + QStringLiteral("-%1.json")
        .arg(QDateTime::currentMSecsSinceEpoch());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size() || !file.commit())
        return {};
    AutomationArtifactStore::cleanupRuntimeObservations();
    return path;
}

bool isMcpSnapshotTool(const McpClient::ToolDef &tool)
{
    const QString lower = tool.name.toLower();
    return lower.contains(QStringLiteral("accessibility"))
        || lower.contains(QStringLiteral("snapshot"))
        || lower.contains(QStringLiteral("aria"))
        || lower.contains(QStringLiteral("dom"))
        || lower.contains(QStringLiteral("page_source"))
        || lower.contains(QStringLiteral("page_content"));
}

} // namespace

// Carpetas que grep/glob NO recorren (ruido + lentitud). Aproxima a los defaults
// de opencode/aider; no parsea .gitignore completo.
static bool isIgnoredDir(const QString &name)
{
    static const QSet<QString> ig{
        QStringLiteral("node_modules"), QStringLiteral(".git"), QStringLiteral("build"),
        QStringLiteral("build2"), QStringLiteral("dist"), QStringLiteral(".venv"),
        QStringLiteral("venv"), QStringLiteral("__pycache__"), QStringLiteral(".next"),
        QStringLiteral(".turbo"), QStringLiteral("coverage"), QStringLiteral("target"),
        QStringLiteral(".cache"), QStringLiteral(".idea"), QStringLiteral(".vs"),
        QStringLiteral(".gradle"), QStringLiteral("bin"), QStringLiteral("obj")};
    return ig.contains(name);
}

// Recorre rootAbs recursivamente saltando dirs ignorados; junta rutas absolutas
// de archivos hasta maxFiles.
static void collectFiles(const QString &rootAbs, QStringList &out, int maxFiles)
{
    QStringList stack{rootAbs};
    while (!stack.isEmpty() && out.size() < maxFiles) {
        QDir d(stack.takeLast());
        const auto entries = d.entryInfoList(
            QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs, QDir::Name);
        for (const QFileInfo &fi : entries) {
            if (fi.isDir()) {
                if (!isIgnoredDir(fi.fileName())) stack << fi.absoluteFilePath();
            } else {
                out << fi.absoluteFilePath();
                if (out.size() >= maxFiles) break;
            }
        }
    }
}

// Traduce un glob ('*'=segmento, '**'=recursivo, '?'=1 char) a regex anclada.
static QRegularExpression globToRegex(const QString &glob)
{
    // Construido a mano (NO escape: escaparía '/'). Paths rel con '/'.
    QString rx = QStringLiteral("^");
    const int n = glob.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = glob.at(i);
        if (c == QLatin1Char('*')) {
            if (i + 1 < n && glob.at(i + 1) == QLatin1Char('*')) {
                if (i + 2 < n && glob.at(i + 2) == QLatin1Char('/')) { rx += QStringLiteral("(?:.*/)?"); i += 2; }
                else { rx += QStringLiteral(".*"); i += 1; }
            } else { rx += QStringLiteral("[^/]*"); }
        } else if (c == QLatin1Char('?')) {
            rx += QStringLiteral("[^/]");
        } else {
            if (QStringLiteral(".^$+(){}[]|\\").contains(c)) rx += QLatin1Char('\\');
            rx += c;
        }
    }
    rx += QLatin1Char('$');
    return QRegularExpression(rx);
}

static bool differsOnlyInWhitespace(const QString &content, const QString &needle)
{
    if (needle.trimmed().isEmpty()) return false;
    auto norm = [](const QString &s) {
        return QLatin1Char(' ') + s.simplified() + QLatin1Char(' ');
    };
    return norm(content).count(norm(needle)) == 1;
}

static void terminateProcessTree(QProcess *proc)
{
    if (!proc) return;
#ifdef Q_OS_WIN
    const qint64 pid = proc->processId();
    if (pid > 0) {
        QProcess killer;
        killer.setProcessChannelMode(QProcess::MergedChannels);
        killer.start(QStringLiteral("taskkill"),
                     {QStringLiteral("/PID"), QString::number(pid),
                      QStringLiteral("/T"), QStringLiteral("/F")});
        killer.waitForFinished(2000);
    }
#endif
    if (proc->state() != QProcess::NotRunning)
        proc->kill();
}

// ── Helpers web (compartidos por web_fetch / web_search / deep_research) ──
struct WebHit { QString title, url, snippet; };

static bool isBlockedWebAddress(const QHostAddress &address)
{
    if (address.isNull() || address.isLoopback() || address.isMulticast())
        return true;
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 ip = address.toIPv4Address();
        return (ip & 0xff000000U) == 0x00000000U       // 0.0.0.0/8
            || (ip & 0xff000000U) == 0x0a000000U       // 10/8
            || (ip & 0xffc00000U) == 0x64400000U       // 100.64/10
            || (ip & 0xff000000U) == 0x7f000000U       // 127/8
            || (ip & 0xffff0000U) == 0xa9fe0000U       // 169.254/16
            || (ip & 0xfff00000U) == 0xac100000U       // 172.16/12
            || (ip & 0xffffff00U) == 0xc0000000U       // 192.0.0/24
            || (ip & 0xffffff00U) == 0xc0000200U       // TEST-NET-1
            || (ip & 0xffff0000U) == 0xc0a80000U       // 192.168/16
            || (ip & 0xfffe0000U) == 0xc6120000U       // benchmark 198.18/15
            || (ip & 0xffffff00U) == 0xc6336400U       // TEST-NET-2
            || (ip & 0xffffff00U) == 0xcb007100U       // TEST-NET-3
            || (ip & 0xf0000000U) == 0xe0000000U;      // multicast/reservado
    }
    const Q_IPV6ADDR ip = address.toIPv6Address();
    bool ipv4Mapped = true;
    for (int i = 0; i < 10; ++i)
        ipv4Mapped = ipv4Mapped && ip[i] == 0;
    ipv4Mapped = ipv4Mapped && ip[10] == 0xffU && ip[11] == 0xffU;
    if (ipv4Mapped) {
        const quint32 v4 = (quint32(ip[12]) << 24) | (quint32(ip[13]) << 16)
                         | (quint32(ip[14]) << 8) | quint32(ip[15]);
        return isBlockedWebAddress(QHostAddress(v4));
    }
    return (ip[0] & 0xfeU) == 0xfcU                    // unique-local fc00::/7
        || (ip[0] == 0xfeU && (ip[1] & 0xc0U) == 0x80U) // link-local fe80::/10
        || ip[0] == 0xffU;                             // multicast
}

bool AgentToolRunner::isSafePublicWebUrl(const QString &raw, QString *error)
{
    const QUrl url = QUrl::fromUserInput(raw);
    const QString scheme = url.scheme().toLower();
    const QString host = url.host().trimmed().toLower();
    if (!url.isValid() || (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        || host.isEmpty()) {
        if (error) *error = QStringLiteral("sólo se permiten URLs http(s) absolutas");
        return false;
    }
    if (!url.userInfo().isEmpty()) {
        if (error) *error = QStringLiteral("no se permiten credenciales embebidas en la URL");
        return false;
    }
    if (host == QLatin1String("localhost") || host.endsWith(QLatin1String(".localhost"))
        || host.endsWith(QLatin1String(".local")) || host.endsWith(QLatin1String(".internal"))) {
        if (error) *error = QStringLiteral("destino local/interno bloqueado");
        return false;
    }

    QHostAddress literal;
    if (literal.setAddress(host)) {
        if (isBlockedWebAddress(literal)) {
            if (error) *error = QStringLiteral("dirección IP no pública bloqueada");
            return false;
        }
        return true;
    }

    const QHostInfo info = QHostInfo::fromName(host);
    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
        if (error) *error = QStringLiteral("no se pudo resolver el host");
        return false;
    }
    for (const QHostAddress &address : info.addresses()) {
        if (isBlockedWebAddress(address)) {
            if (error) *error = QStringLiteral("el host resuelve a una red no pública");
            return false;
        }
    }
    return true;
}

static QString decodeHtmlEntities(QString text)
{
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&amp;"),  QStringLiteral("&"));
    text.replace(QStringLiteral("&lt;"),   QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"),   QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#x27;"), QStringLiteral("'"));
    QRegularExpression numeric(QStringLiteral("&#(x?[0-9a-fA-F]+);"));
    auto it = numeric.globalMatch(text);
    QList<QPair<QString, QString>> replacements;
    while (it.hasNext()) {
        const auto match = it.next();
        bool ok = false;
        const QString token = match.captured(1);
        const uint value = token.startsWith(QLatin1Char('x'), Qt::CaseInsensitive)
            ? token.mid(1).toUInt(&ok, 16) : token.toUInt(&ok, 10);
        if (ok && value <= 0x10ffffU)
            replacements.append({match.captured(0), QString::fromUcs4(&value, 1)});
    }
    for (const auto &replacement : replacements)
        text.replace(replacement.first, replacement.second);
    return text;
}

QString AgentToolRunner::extractReadableWebText(const QString &html)
{
    QString text = html;
    text.remove(QRegularExpression(
        QStringLiteral("(?is)<(script|style|noscript|svg|canvas|template|nav|footer|aside|form)"
                       "[^>]*>.*?</\\1>")));

    // Preferir el contenido semántico principal cuando existe. El fallback al body
    // evita perder páginas viejas o HTML imperfecto.
    QRegularExpression mainRe(QStringLiteral("(?is)<(article|main)\\b[^>]*>(.*)</\\1>"));
    const auto mainMatch = mainRe.match(text);
    if (mainMatch.hasMatch())
        text = mainMatch.captured(2);
    else {
        const auto bodyMatch = QRegularExpression(
            QStringLiteral("(?is)<body\\b[^>]*>(.*)</body>")).match(text);
        if (bodyMatch.hasMatch()) text = bodyMatch.captured(1);
    }

    text.replace(QRegularExpression(
        QStringLiteral("(?is)<\\s*(br|hr)\\b[^>]*>")), QStringLiteral("\n"));
    text.replace(QRegularExpression(
        QStringLiteral("(?is)</\\s*(p|div|li|tr|section|article|main|h[1-6])\\s*>")),
        QStringLiteral("\n"));
    text.replace(QRegularExpression(QStringLiteral("(?s)<[^>]+>")), QStringLiteral(""));
    text = decodeHtmlEntities(text);
    text.replace(QRegularExpression(QStringLiteral("[ \t]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\n[ \t]*(?:\n[ \t]*)+")), QStringLiteral("\n\n"));
    return text.trimmed();
}

QStringList AgentToolRunner::webEscalationReasons(const QString &html, const QString &text,
                                                   const QString &transportError)
{
    QStringList reasons;
    if (!transportError.isEmpty()) reasons << QStringLiteral("transport_error");
    const QString sample = (html.left(120000) + QLatin1Char(' ') + text.left(12000)).toLower();
    static const QStringList challenges{
        QStringLiteral("cf-chl-"), QStringLiteral("cloudflare ray id"),
        QStringLiteral("checking your browser"), QStringLiteral("verify you are human"),
        QStringLiteral("attention required"), QStringLiteral("captcha"),
        QStringLiteral("datadome"), QStringLiteral("perimeterx"), QStringLiteral("px-captcha")};
    for (const QString &marker : challenges)
        if (sample.contains(marker)) {
            reasons << QStringLiteral("challenge");
            break;
        }
    static const QStringList jsMarkers{
        QStringLiteral("enable javascript"), QStringLiteral("javascript is required"),
        QStringLiteral("requires javascript"), QStringLiteral("please turn javascript on"),
        QStringLiteral("__next_data__"), QStringLiteral("id=\"__next\""),
        QStringLiteral("id=\"root\"></div>"), QStringLiteral("id=\"app\"></div>")};
    for (const QString &marker : jsMarkers)
        if (sample.contains(marker)) {
            reasons << QStringLiteral("javascript_required");
            break;
        }
    if (text.trimmed().isEmpty()) reasons << QStringLiteral("empty");
    else if (text.trimmed().size() < 280) reasons << QStringLiteral("thin_content");
    reasons.removeDuplicates();
    return reasons;
}

QString AgentToolRunner::summarizeBrowserNetworkEvidence(const QString &raw,
                                                         bool includeStatic)
{
    struct Endpoint {
        QString method;
        QString origin;
        QString path;
        QSet<int> statuses;
        QSet<QString> queryParameterNames;
        int count = 0;
    };
    QMap<QString, Endpoint> grouped;
    QMap<QString, int> transitions;
    QString previousKey;
    int ignoredStatic = 0;
    int ignoredInvalid = 0;

    // Playwright ha usado formatos como "GET https://... => [200] OK" y
    // "[GET] https://...". Buscar por línea mantiene la correlación sin conservar
    // headers, bodies, cookies ni valores de query.
    const QRegularExpression urlRx(QStringLiteral(R"(https?://[^\s"'<>]+)"),
                                   QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression methodRx(
        QStringLiteral(R"(\b(GET|POST|PUT|PATCH|DELETE|HEAD|OPTIONS)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression statusRx(QStringLiteral(R"((?:=>\s*)?\[?([1-5]\d\d)\]?)"));
    const QRegularExpression staticRx(
        QStringLiteral(R"(\.(?:css|js|mjs|png|jpe?g|gif|svg|ico|woff2?|ttf|map)(?:$|/))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression volatileSegmentRx(
        QStringLiteral(R"((?<=/)(?:\d{4,}|[0-9a-f]{8}-[0-9a-f-]{27,}|[A-Za-z0-9_-]{32,})(?=/|$))"),
        QRegularExpression::CaseInsensitiveOption);

    const QStringList lines = raw.left(2 * 1024 * 1024).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const auto urlMatch = urlRx.match(line);
        if (!urlMatch.hasMatch()) continue;
        QString urlText = urlMatch.captured();
        while (!urlText.isEmpty() && QStringLiteral(".,;:)]}").contains(urlText.back()))
            urlText.chop(1);
        QUrl url(urlText);
        if (!url.isValid() || url.host().isEmpty()) {
            ++ignoredInvalid;
            continue;
        }
        QString path = url.path(QUrl::FullyDecoded);
        if (path.isEmpty()) path = QStringLiteral("/");
        path.replace(volatileSegmentRx, QStringLiteral("{id}"));
        if (!includeStatic && staticRx.match(path).hasMatch()) {
            ++ignoredStatic;
            continue;
        }

        const auto methodMatch = methodRx.match(line.left(urlMatch.capturedStart()));
        const QString method = methodMatch.hasMatch()
                                   ? methodMatch.captured(1).toUpper()
                                   : QStringLiteral("GET");
        const QString origin = url.scheme().toLower() + QStringLiteral("://")
                               + url.host().toLower()
                               + (url.port() > 0 ? QStringLiteral(":%1").arg(url.port())
                                                 : QString());
        const QString key = method + QLatin1Char(' ') + origin + path;
        Endpoint &ep = grouped[key];
        ep.method = method;
        ep.origin = origin;
        ep.path = path;
        ++ep.count;
        const QUrlQuery query(url);
        for (const auto &item : query.queryItems(QUrl::FullyDecoded))
            if (!item.first.trimmed().isEmpty())
                ep.queryParameterNames.insert(item.first.left(80));
        const auto statusMatch = statusRx.match(line.mid(urlMatch.capturedEnd()));
        if (statusMatch.hasMatch()) ep.statuses.insert(statusMatch.captured(1).toInt());
        if (!previousKey.isEmpty() && previousKey != key)
            ++transitions[previousKey + QChar(0x1f) + key];
        previousKey = key;
    }

    QJsonArray endpoints;
    for (const Endpoint &ep : std::as_const(grouped)) {
        QJsonArray statuses;
        QList<int> sortedStatuses(ep.statuses.cbegin(), ep.statuses.cend());
        std::sort(sortedStatuses.begin(), sortedStatuses.end());
        for (int status : sortedStatuses) statuses.append(status);
        QStringList queryNames(ep.queryParameterNames.cbegin(), ep.queryParameterNames.cend());
        std::sort(queryNames.begin(), queryNames.end());
        QJsonArray pathParameters;
        if (ep.path.contains(QLatin1String("{id}"))) pathParameters.append(QStringLiteral("id"));
        endpoints.append(QJsonObject{
            {QStringLiteral("method"), ep.method},
            {QStringLiteral("origin"), ep.origin},
            {QStringLiteral("pathTemplate"), ep.path},
            {QStringLiteral("count"), ep.count},
            {QStringLiteral("statuses"), statuses},
            {QStringLiteral("pathParameters"), pathParameters},
            {QStringLiteral("queryParameterNames"), QJsonArray::fromStringList(queryNames)},
            {QStringLiteral("confidence"), ep.count > 1 ? 0.9 : 0.7}});
    }
    QJsonArray sequence;
    for (auto it = transitions.cbegin(); it != transitions.cend(); ++it) {
        const QStringList pair = it.key().split(QChar(0x1f));
        if (pair.size() == 2)
            sequence.append(QJsonObject{{QStringLiteral("from"), pair.at(0)},
                                        {QStringLiteral("to"), pair.at(1)},
                                        {QStringLiteral("count"), it.value()},
                                        {QStringLiteral("inference"), QStringLiteral("observed_order")}});
    }
    const QJsonObject result{
        {QStringLiteral("kind"), QStringLiteral("browser_network_evidence")},
        {QStringLiteral("contractVersion"), 1},
        {QStringLiteral("endpointCount"), endpoints.size()},
        {QStringLiteral("endpoints"), endpoints},
        {QStringLiteral("sequence"), sequence},
        {QStringLiteral("ignoredStatic"), ignoredStatic},
        {QStringLiteral("ignoredInvalid"), ignoredInvalid},
        {QStringLiteral("truncatedInput"), raw.size() > 2 * 1024 * 1024},
        {QStringLiteral("privacy"), QJsonObject{
             {QStringLiteral("queryValuesRetained"), false},
             {QStringLiteral("queryParameterNamesRetained"), true},
             {QStringLiteral("headersRetained"), false},
             {QStringLiteral("bodiesRetained"), false},
             {QStringLiteral("volatilePathSegmentsNormalized"), true}}}};
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

// GET sincrónico con timeout, redirecciones revalidadas y cuerpo acotado. Corre
// en el worker para que DNS y red nunca bloqueen la UI.
static QByteArray httpGetSync(const QUrl &initialUrl, QString *err, int timeoutMs = 20000,
                              bool allowConfiguredLocalEndpoint = false,
                              QUrl *finalUrl = nullptr)
{
    QUrl url = initialUrl;
    for (int redirect = 0; redirect <= 5; ++redirect) {
        QString validationError;
        // Un SearXNG local es una integración explícita del usuario. La excepción
        // sólo vale para el primer request configurado; todo redirect se revalida.
        if (!(redirect == 0 && allowConfiguredLocalEndpoint)
            && !AgentToolRunner::isSafePublicWebUrl(url.toString(), &validationError)) {
            if (err) *err = QStringLiteral("URL bloqueada: %1").arg(validationError);
            return {};
        }
        QNetworkAccessManager nam;
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QByteArrayLiteral("Mozilla/5.0 LlamaCode/0.1"));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
        QNetworkReply *reply = nam.get(req);
        QEventLoop loop;
        QTimer killer;
        killer.setSingleShot(true);
        QObject::connect(&killer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        killer.start(timeoutMs);
        loop.exec();
        if (reply->isRunning()) {
            reply->abort();
            reply->deleteLater();
            if (err) *err = QStringLiteral("timeout");
            return {};
        }
        const QVariant redirectTarget =
            reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirectTarget.isValid()) {
            url = url.resolved(redirectTarget.toUrl());
            reply->deleteLater();
            continue;
        }
        if (reply->error() != QNetworkReply::NoError) {
            if (err) *err = reply->errorString();
            reply->deleteLater();
            return {};
        }
        QByteArray body = reply->readAll();
        reply->deleteLater();
        constexpr qsizetype kMaxDownloadBytes = 2 * 1024 * 1024;
        if (body.size() > kMaxDownloadBytes)
            body.truncate(kMaxDownloadBytes);
        if (finalUrl) *finalUrl = url;
        return body;
    }
    if (err) *err = QStringLiteral("demasiadas redirecciones");
    return {};
}

// Descarga una URL y devuelve su texto limpiado (cap chars). "" si falla.
static QString fetchUrlText(const QString &url, int cap, QString *err = nullptr,
                            QString *rawHtml = nullptr, QString *finalUrl = nullptr)
{
    QUrl resolved;
    const QByteArray body = httpGetSync(QUrl(url), err, 20000, false, &resolved);
    if (body.isEmpty()) return {};
    if (finalUrl) *finalUrl = resolved.toString();
    if (rawHtml) *rawHtml = QString::fromUtf8(body);
    const QString text = AgentToolRunner::extractReadableWebText(QString::fromUtf8(body));
    return text.left(cap);
}

// Resuelve el redirect /l/?uddg= de DuckDuckGo a la URL destino.
static QString resolveDdgRedirect(QString raw)
{
    if (raw.startsWith(QLatin1String("//"))) raw = QStringLiteral("https:") + raw;
    QUrl ru(raw);
    if (ru.path().endsWith(QLatin1String("/l/")) || ru.path() == QLatin1String("/l")) {
        const QString uddg = QUrlQuery(ru).queryItemValue(QStringLiteral("uddg"), QUrl::FullyDecoded);
        if (!uddg.isEmpty()) return uddg;
    }
    return raw;
}

// Búsqueda web: SearXNG si LLAMACODE_SEARXNG_URL está seteado, si no DuckDuckGo HTML.
static QVector<WebHit> runWebSearch(const QString &query, int count, QString *err = nullptr)
{
    QVector<WebHit> hits;
    const QString searxng = qEnvironmentVariable("LLAMACODE_SEARXNG_URL").trimmed();
    if (!searxng.isEmpty()) {
        QUrl u(searxng.endsWith(QLatin1Char('/')) ? searxng + QStringLiteral("search")
                                                   : searxng + QStringLiteral("/search"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("q"), query);
        q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
        u.setQuery(q);
        const QByteArray body = httpGetSync(u, err, 20000, true);
        if (!body.isEmpty()) {
            const QJsonArray arr = QJsonDocument::fromJson(body).object()
                                       .value(QStringLiteral("results")).toArray();
            for (const QJsonValue &v : arr) {
                const QJsonObject o = v.toObject();
                hits.append({o.value(QStringLiteral("title")).toString(),
                             o.value(QStringLiteral("url")).toString(),
                             o.value(QStringLiteral("content")).toString()});
                if (hits.size() >= count) break;
            }
        }
    }
    if (hits.isEmpty()) {
        QUrl u(QStringLiteral("https://html.duckduckgo.com/html/"));
        QUrlQuery q; q.addQueryItem(QStringLiteral("q"), query); u.setQuery(q);
        const QByteArray body = httpGetSync(u, err);
        if (body.isEmpty()) return hits;
        const QString html = QString::fromUtf8(body);
        QRegularExpression reTitle(
            QStringLiteral("(?is)<a[^>]+class=\"[^\"]*result__a[^\"]*\"[^>]+href=\"([^\"]+)\"[^>]*>(.*?)</a>"));
        QRegularExpression reSnip(
            QStringLiteral("(?is)class=\"[^\"]*result__snippet[^\"]*\"[^>]*>(.*?)</a>"));
        auto snipIt = reSnip.globalMatch(html);
        auto titleIt = reTitle.globalMatch(html);
        while (titleIt.hasNext() && hits.size() < count) {
            const auto tm = titleIt.next();
            WebHit h;
            h.url = resolveDdgRedirect(tm.captured(1));
            h.title = AgentToolRunner::extractReadableWebText(tm.captured(2));
            if (snipIt.hasNext())
                h.snippet = AgentToolRunner::extractReadableWebText(snipIt.next().captured(1));
            if (!h.url.isEmpty()) hits.append(h);
        }
    }
    return hits;
}

// ── Embeddings + cache de vectores (RAG semántico vía /v1/embeddings) ──

// POST JSON sincrónico. Devuelve el body o {} con *err.
static QByteArray httpPostJson(const QUrl &url, const QByteArray &body, QString *err,
                               int timeoutMs = 60000, const QString &bearer = QString())
{
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    if (!bearer.isEmpty())
        req.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") + bearer.toUtf8());
    QNetworkReply *reply = nam.post(req, body);
    QEventLoop loop;
    QTimer killer; killer.setSingleShot(true);
    QObject::connect(&killer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    killer.start(timeoutMs);
    loop.exec();
    if (reply->isRunning()) { reply->abort(); reply->deleteLater(); if (err) *err = QStringLiteral("timeout"); return {}; }
    if (reply->error() != QNetworkReply::NoError) {
        const QByteArray b = reply->readAll(); QString e = reply->errorString();
        if (!b.isEmpty()) e += QStringLiteral(" · ") + QString::fromUtf8(b.left(200));
        reply->deleteLater(); if (err) *err = e; return {};
    }
    const QByteArray out = reply->readAll(); reply->deleteLater(); return out;
}

// Llama /v1/embeddings con un batch de textos → vectores. "" en *err si OK.
static QVector<QVector<float>> embedTexts(const QString &baseUrl, const QStringList &texts,
                                          QString *err, const QString &model,
                                          const QString &bearer)
{
    QVector<QVector<float>> out;
    if (baseUrl.isEmpty()) { if (err) *err = QStringLiteral("sin URL de server"); return out; }
    QJsonArray inputs;
    for (const QString &t : texts) inputs.append(t);
    const QJsonObject payload{
        {QStringLiteral("input"), inputs},
        {QStringLiteral("model"), model.isEmpty() ? QStringLiteral("llamacode-embed") : model}};
    const QByteArray body = httpPostJson(QUrl(baseUrl + QStringLiteral("/v1/embeddings")),
                                         QJsonDocument(payload).toJson(QJsonDocument::Compact), err,
                                         60000, bearer);
    if (body.isEmpty()) return out;
    const QJsonArray data = QJsonDocument::fromJson(body).object()
                                .value(QStringLiteral("data")).toArray();
    if (data.isEmpty()) { if (err) *err = QStringLiteral("respuesta sin embeddings (¿server sin --embeddings?)"); return out; }
    out.resize(data.size());
    for (const QJsonValue &dv : data) {
        const QJsonObject o = dv.toObject();
        const int idx = o.value(QStringLiteral("index")).toInt();
        const QJsonArray emb = o.value(QStringLiteral("embedding")).toArray();
        QVector<float> vec; vec.reserve(emb.size());
        for (const QJsonValue &ev : emb) vec.append(static_cast<float>(ev.toDouble()));
        if (idx >= 0 && idx < out.size()) out[idx] = vec; else out.append(vec);
    }
    return out;
}

// Conexión SQLite per-thread al cache de vectores. Tabla: key TEXT PK, dim INT, vec BLOB.
static QSqlDatabase embedCacheDb()
{
    const QString conn = QStringLiteral("embed_cache_%1")
                             .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    if (QSqlDatabase::contains(conn)) return QSqlDatabase::database(conn);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
    db.setDatabaseName(dir + QStringLiteral("/embeddings_cache.db"));
    if (db.open()) {
        QSqlQuery q(db);
        q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS vecs ("
                              "key TEXT PRIMARY KEY, dim INTEGER, vec BLOB)"));
    }
    return db;
}

static QByteArray vecToBlob(const QVector<float> &v)
{
    return QByteArray(reinterpret_cast<const char *>(v.constData()),
                      int(v.size() * sizeof(float)));
}

// El mismo texto puede producir vectores incompatibles en el server principal
// y en un sidecar, o al cambiar el modelo de embeddings. Namespacing evita que
// una migración de endpoint reutilice silenciosamente un vector viejo.
QString AgentToolRunner::embeddingCacheKeyForTest(const QString &baseUrl,
                                                  const QString &model,
                                                  const QString &text)
{
    const QByteArray namespaceBytes = (baseUrl + QLatin1Char('\n') + model).toUtf8();
    const QByteArray namespaceId = QCryptographicHash::hash(namespaceBytes,
                                                             QCryptographicHash::Md5)
                                       .toHex()
                                       .left(16);
    return QString::fromLatin1(namespaceId)
        + QLatin1Char(':')
        + QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex());
}

static QVector<float> blobToVec(const QByteArray &b)
{
    QVector<float> v(int(b.size() / sizeof(float)));
    memcpy(v.data(), b.constData(), v.size() * sizeof(float));
    return v;
}

static float cosineSim(const QVector<float> &a, const QVector<float> &b)
{
    if (a.size() != b.size() || a.isEmpty()) return 0.f;
    double dot = 0, na = 0, nb = 0;
    for (int i = 0; i < a.size(); ++i) { dot += double(a[i]) * b[i]; na += double(a[i]) * a[i]; nb += double(b[i]) * b[i]; }
    if (na == 0 || nb == 0) return 0.f;
    return float(dot / (std::sqrt(na) * std::sqrt(nb)));
}

// Llama /rerank (llama-server con --reranking, p.ej. Qwen3-Reranker) → score de
// relevancia por doc, alineado al orden de 'docs'. Vector vacío si el endpoint no
// existe o falla (el caller cae a la fusión sin reranker). "" en *err si OK.
static QVector<float> rerankTexts(const QString &baseUrl, const QString &query,
                                  const QStringList &docs, QString *err,
                                  const QString &model, const QString &bearer)
{
    QVector<float> out;
    if (baseUrl.isEmpty() || docs.isEmpty()) {
        if (err) *err = QStringLiteral("sin server/docs"); return out;
    }
    QJsonArray arr;
    for (const QString &d : docs) arr.append(d);
    const QJsonObject payload{
        {QStringLiteral("query"), query},
        {QStringLiteral("documents"), arr},
        {QStringLiteral("model"), model.isEmpty() ? QStringLiteral("llamacode-rerank") : model}};
    const QByteArray body = httpPostJson(QUrl(baseUrl + QStringLiteral("/rerank")),
                                         QJsonDocument(payload).toJson(QJsonDocument::Compact), err,
                                         60000, bearer);
    if (body.isEmpty()) return out;
    const QJsonArray results = QJsonDocument::fromJson(body).object()
                                   .value(QStringLiteral("results")).toArray();
    if (results.isEmpty()) {
        if (err) *err = QStringLiteral("respuesta sin results (¿server sin --reranking?)");
        return out;
    }
    out.resize(docs.size());
    for (const QJsonValue &rv : results) {
        const QJsonObject o = rv.toObject();
        const int idx = o.value(QStringLiteral("index")).toInt();
        const double score = o.value(QStringLiteral("relevance_score"))
                                 .toDouble(o.value(QStringLiteral("score")).toDouble());
        if (idx >= 0 && idx < out.size()) out[idx] = float(score);
    }
    return out;
}

// Extrae los "módulos" referenciados por un archivo de código (estilo dep-graph
// de archex, sin tree-sitter): #include de C/C++, import/require de JS/TS,
// import/from de Python, import de QML. Devuelve los nombres base (último
// segmento, sin extensión, en minúsculas) para casar contra archivos del repo.
static QSet<QString> extractImportRefs(const QString &text)
{
    QSet<QString> refs;
    static const QRegularExpression reC(
        QStringLiteral("#\\s*include\\s*[\"<]([^\">]+)[\">]"));
    static const QRegularExpression reFrom(
        QStringLiteral("(?:from|import|require)\\s*\\(?\\s*[\"']([^\"']+)[\"']"));
    static const QRegularExpression rePy(
        QStringLiteral("^\\s*(?:from|import)\\s+([\\w.]+)"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression reQml(
        QStringLiteral("^\\s*import\\s+([\\w.]+)"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression reCodeExt(
        QStringLiteral("\\.(h|hpp|hh|hxx|c|cc|cpp|cxx|js|jsx|mjs|ts|tsx|py|qml|java|go|rs|kt)$"),
        QRegularExpression::CaseInsensitiveOption);
    auto add = [&](const QString &raw) {
        QString s = raw;
        s.replace(QLatin1Char('\\'), QLatin1Char('/'));
        s = s.section(QLatin1Char('/'), -1).trimmed();   // basename (ruta) / módulo
        if (reCodeExt.match(s).hasMatch())
            s = s.section(QLatin1Char('.'), 0, -2);       // ruta: dropear extensión
        else if (s.contains(QLatin1Char('.')))
            s = s.section(QLatin1Char('.'), -1);          // módulo a.b.c → último tramo
        s = s.toLower();
        if (s.size() >= 2) refs.insert(s);
    };
    for (const QRegularExpression *re : {&reC, &reFrom, &rePy, &reQml}) {
        auto it = re->globalMatch(text);
        while (it.hasNext()) add(it.next().captured(1));
    }
    return refs;
}

AgentToolRunner::AgentToolRunner(QObject *parent) : QObject(parent) {}
AgentToolRunner::~AgentToolRunner() { shutdown(); }

void AgentToolRunner::setConfined(bool confined) { m_confined = confined; }
void AgentToolRunner::setReadOnly(bool readOnly) { m_readOnly = readOnly; }
void AgentToolRunner::setReadOnlyShell(bool allow) { m_readOnlyShell = allow; }
void AgentToolRunner::setAllowedRoots(const QStringList &roots)
{
    m_allowedRoots.clear();
    for (const QString &r : roots) {
        const QFileInfo info(r.trimmed());
        const QString c = QDir::cleanPath(info.canonicalFilePath().isEmpty()
                                          ? info.absoluteFilePath()
                                          : info.canonicalFilePath());
        if (!c.isEmpty()) m_allowedRoots << c;
    }
}
void AgentToolRunner::setServerBaseUrl(const QString &url) { m_serverBaseUrl = url; }
void AgentToolRunner::setAuxiliaryServerConfig(const QString &url, const QString &embeddingModel,
                                               const QString &rerankModel, const QString &bearer)
{
    m_auxiliaryBaseUrl = url.trimmed();
    m_auxiliaryEmbeddingModel = embeddingModel.trimmed();
    m_auxiliaryRerankModel = rerankModel.trimmed();
    m_auxiliaryBearer = bearer.trimmed();
}

QString AgentToolRunner::auxiliaryEndpointForTest(const QString &configured,
                                                  const QString &primary)
{
    const auto normalize = [](const QString &raw) {
        QString candidate = raw.trimmed();
        if (candidate.isEmpty()) return QString();
        const QUrl parsed(candidate);
        if (!parsed.isValid() || parsed.host().isEmpty()
            || (parsed.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) != 0
                && parsed.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0))
            return QString();
        while (candidate.endsWith(QLatin1Char('/'))) candidate.chop(1);
        // El contrato interno recibe la base sin /v1: embeddings agrega
        // /v1/embeddings y llama-server rerank usa /rerank.
        if (candidate.endsWith(QStringLiteral("/v1"), Qt::CaseInsensitive))
            candidate.chop(3);
        while (candidate.endsWith(QLatin1Char('/'))) candidate.chop(1);
        return candidate;
    };

    const QString primaryUrl = normalize(primary);
    const QString configuredUrl = normalize(configured);
    return configuredUrl.isEmpty() ? primaryUrl : configuredUrl;
}

QString AgentToolRunner::auxiliaryEndpoint() const
{
    const QString configured = !m_auxiliaryBaseUrl.trimmed().isEmpty()
        ? m_auxiliaryBaseUrl
        : qEnvironmentVariable("LLAMACODE_AUXILIARY_URL");
    return auxiliaryEndpointForTest(configured, m_serverBaseUrl);
}

QString AgentToolRunner::auxiliaryBearer() const
{
    return !m_auxiliaryBearer.trimmed().isEmpty()
        ? m_auxiliaryBearer.trimmed()
        : qEnvironmentVariable("LLAMACODE_AUXILIARY_KEY").trimmed();
}

QString AgentToolRunner::auxiliaryEmbeddingModel() const
{
    return !m_auxiliaryEmbeddingModel.trimmed().isEmpty()
        ? m_auxiliaryEmbeddingModel.trimmed()
        : qEnvironmentVariable("LLAMACODE_AUXILIARY_EMBED_MODEL",
                               QStringLiteral("llamacode-embed")).trimmed();
}

QString AgentToolRunner::auxiliaryRerankModel() const
{
    return !m_auxiliaryRerankModel.trimmed().isEmpty()
        ? m_auxiliaryRerankModel.trimmed()
        : qEnvironmentVariable("LLAMACODE_AUXILIARY_RERANK_MODEL",
                               QStringLiteral("llamacode-rerank")).trimmed();
}

void AgentToolRunner::setSessionId(const QString &sessionId)
{
    m_sessionId = sessionId;
    m_desktopLease = DesktopComputerUse::SessionLease{};
    m_desktopLease.leaseId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_desktopLease.sessionId = sessionId;
    // El límite duro de la Task sigue siendo superior autoridad; este lease
    // evita que un worker reutilizado acumule acciones de una sesión anterior.
    m_desktopLease.maxActions = 500;
    m_desktopLease.expiresAt = QDateTime::currentMSecsSinceEpoch() + 30 * 60 * 1000;
}
void AgentToolRunner::setMailAccounts(const QVariantList &accounts) { m_mailAccounts = accounts; }
void AgentToolRunner::setWebProviders(const QVariantList &providers) { m_webProviders = providers; }
void AgentToolRunner::setPortableSkillPolicy(const QStringList &include,
                                             const QStringList &exclude, bool declared)
{
    m_skillInclude = include;
    m_skillExclude = exclude;
    m_skillPolicyDeclared = declared;
}

bool AgentToolRunner::consumeWebRateLimit(const QString &host, qint64 nowMs, QString *error)
{
    QList<qint64> &recent = m_webRequestTimes[host.toLower()];
    while (!recent.isEmpty() && nowMs - recent.first() >= 60000)
        recent.removeFirst();
    if (recent.size() >= 30) {
        if (error) *error = QStringLiteral("rate limit para %1 (30 requests/min)").arg(host);
        return false;
    }
    recent.append(nowMs);
    return true;
}

static QString domReadabilityExpression()
{
    // Readability DOM determinista: elimina chrome, puntúa contenedores por texto
    // de párrafos y densidad de links, y devuelve el candidato principal.
    return QStringLiteral(
        "() => {"
        "const d=document.cloneNode(true);"
        "d.querySelectorAll('script,style,noscript,svg,canvas,template,nav,footer,aside,form,"
        "[aria-hidden=true]').forEach(n=>n.remove());"
        "const clean=s=>(s||'').replace(/\\s+/g,' ').trim();"
        "let best=null,bestScore=0;"
        "d.querySelectorAll('article,main,section,div').forEach(n=>{"
        "const ps=[...n.querySelectorAll(':scope > p, :scope > h1, :scope > h2, :scope > h3, :scope > ul, :scope > ol')];"
        "const text=clean(ps.map(p=>p.textContent).join('\\n'));"
        "if(text.length<180)return;"
        "const links=clean([...n.querySelectorAll('a')].map(a=>a.textContent).join(' ')).length;"
        "const score=text.length*(1-Math.min(.85,links/Math.max(1,text.length)))+ps.length*80;"
        "if(score>bestScore){bestScore=score;best=text;}});"
        "const fallback=clean((d.querySelector('article,main')||d.body)?.textContent||'');"
        "return JSON.stringify({title:document.title,text:best||fallback,url:location.href,score:bestScore});"
        "}");
}

QString AgentToolRunner::fetchViaPlaywright(const QString &url, QString *error)
{
    McpClient *browser = nullptr;
    QString navigateTool, extractTool;
    for (McpClient *client : std::as_const(m_mcp)) {
        QString nav, eval, snapshot;
        for (const McpClient::ToolDef &tool : client->tools()) {
            if (tool.name == QLatin1String("browser_navigate")) nav = tool.name;
            else if (tool.name == QLatin1String("browser_evaluate")) eval = tool.name;
            else if (tool.name == QLatin1String("browser_snapshot")) snapshot = tool.name;
        }
        if (!nav.isEmpty() && (!eval.isEmpty() || !snapshot.isEmpty())) {
            browser = client;
            navigateTool = nav;
            extractTool = !eval.isEmpty() ? eval : snapshot;
            break;
        }
    }
    if (!browser) {
        if (error) *error = QStringLiteral("Playwright MCP no está disponible");
        return {};
    }
    bool navOk = false;
    const QString navResult = browser->callTool(navigateTool, QJsonObject{{"url", url}}, &navOk);
    if (!navOk) {
        if (error) *error = QStringLiteral("navegación fallida: %1").arg(navResult.left(240));
        return {};
    }
    QRegularExpression urlRe(QStringLiteral("https?://[^\\s\\]\\)\"']+"));
    auto finalUrls = urlRe.globalMatch(navResult);
    QString finalUrl;
    while (finalUrls.hasNext()) finalUrl = finalUrls.next().captured(0);
    if (finalUrl.isEmpty()) {
        if (error) *error = QStringLiteral("Playwright no informó una URL final verificable");
        return {};
    }
    QString finalError;
    if (!isSafePublicWebUrl(finalUrl, &finalError)) {
        if (error) *error = QStringLiteral("URL final insegura: %1").arg(finalError);
        return {};
    }
    bool extractOk = false;
    QJsonObject args;
    if (extractTool == QLatin1String("browser_evaluate")) {
        args[QStringLiteral("function")] = domReadabilityExpression();
    }
    const QString result = browser->callTool(extractTool, args, &extractOk);
    if (!extractOk || result.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("extracción DOM fallida: %1").arg(result.left(240));
        return {};
    }
    return result.left(96 * 1024);
}

QString AgentToolRunner::fetchViaCamofox(const QString &url, QString *error)
{
    QVariantMap cfg;
    for (const QVariant &entry : std::as_const(m_webProviders)) {
        const QVariantMap candidate = entry.toMap();
        if (candidate.value(QStringLiteral("enabled"), true).toBool()
            && candidate.value(QStringLiteral("provider")).toString().toLower()
                   == QLatin1String("camofox")) {
            cfg = candidate;
            break;
        }
    }
    if (cfg.isEmpty()) {
        if (error) *error = QStringLiteral("Camofox no está configurado o está desactivado");
        return {};
    }
    QString base = cfg.value(QStringLiteral("baseUrl")).toString().trimmed();
    if (base.endsWith(QLatin1Char('/'))) base.chop(1);
    if (base.isEmpty()) {
        if (error) *error = QStringLiteral("Camofox no tiene Base URL");
        return {};
    }
    const QString key = cfg.value(QStringLiteral("apiKey")).toString();
    QString postError;
    const QJsonObject createBody{
        {QStringLiteral("userId"), QStringLiteral("llamacode")},
        {QStringLiteral("sessionKey"), QStringLiteral("web-fetch")},
        {QStringLiteral("url"), url}};
    const QByteArray create = httpPostJson(
        QUrl(base + QStringLiteral("/tabs")),
        QJsonDocument(createBody).toJson(QJsonDocument::Compact), &postError, 30000, key);
    const QString tabId = QJsonDocument::fromJson(create).object()
                              .value(QStringLiteral("tabId")).toString();
    if (tabId.isEmpty()) {
        if (error) *error = QStringLiteral("Camofox no pudo abrir la página: %1").arg(postError);
        return {};
    }

    QString expression = domReadabilityExpression();
    if (expression.startsWith(QStringLiteral("() => ")))
        expression = QStringLiteral("(") + expression + QStringLiteral(")()");
    const QJsonObject evalBody{
        {QStringLiteral("userId"), QStringLiteral("llamacode")},
        {QStringLiteral("expression"), expression}};
    const QByteArray evaluated = httpPostJson(
        QUrl(base + QStringLiteral("/tabs/") + tabId + QStringLiteral("/evaluate")),
        QJsonDocument(evalBody).toJson(QJsonDocument::Compact), &postError, 30000, key);

    // Cerrar siempre la pestaña; el resultado del cleanup no invalida la lectura.
    QNetworkAccessManager cleanupNam;
    QNetworkRequest cleanupReq(
        QUrl(base + QStringLiteral("/tabs/") + tabId + QStringLiteral("?userId=llamacode")));
    if (!key.isEmpty())
        cleanupReq.setRawHeader(QByteArrayLiteral("Authorization"),
                                QByteArrayLiteral("Bearer ") + key.toUtf8());
    QNetworkReply *cleanup = cleanupNam.deleteResource(cleanupReq);
    QEventLoop cleanupLoop;
    QTimer cleanupTimer;
    cleanupTimer.setSingleShot(true);
    QObject::connect(cleanup, &QNetworkReply::finished, &cleanupLoop, &QEventLoop::quit);
    QObject::connect(&cleanupTimer, &QTimer::timeout, &cleanupLoop, &QEventLoop::quit);
    cleanupTimer.start(3000);
    cleanupLoop.exec();
    if (cleanup->isRunning()) cleanup->abort();
    cleanup->deleteLater();

    const QJsonObject outer = QJsonDocument::fromJson(evaluated).object();
    QString encoded = outer.value(QStringLiteral("result")).toString();
    if (encoded.isEmpty()) {
        if (error) *error = QStringLiteral("Camofox no devolvió DOM: %1").arg(postError);
        return {};
    }
    const QJsonObject inner = QJsonDocument::fromJson(encoded.toUtf8()).object();
    const QString finalUrl = inner.value(QStringLiteral("url")).toString();
    QString finalError;
    if (finalUrl.isEmpty() || !isSafePublicWebUrl(finalUrl, &finalError)) {
        if (error) *error = finalUrl.isEmpty()
            ? QStringLiteral("Camofox no informó la URL final")
            : QStringLiteral("URL final insegura: %1").arg(finalError);
        return {};
    }
    const QString text = inner.value(QStringLiteral("text")).toString().trimmed();
    if (text.isEmpty()) {
        if (error) *error = QStringLiteral("Camofox devolvió contenido vacío");
        return {};
    }
    return text.left(96 * 1024);
}
void AgentToolRunner::setTeacherConfig(const QString &url, const QString &model, const QString &key)
{
    m_teacherUrl = url.trimmed();
    m_teacherModel = model.trimmed();
    m_teacherKey = key.trimmed();
}
void AgentToolRunner::setMasterCli(const QString &kind, const QString &cliName,
                                   const QString &cliPath, bool applyEdits, int timeoutSec)
{
    m_masterKind = kind.trimmed().isEmpty() ? QStringLiteral("none") : kind.trimmed();
    m_masterCliName = cliName.trimmed();
    m_masterCliPath = cliPath.trimmed();
    m_masterApplyEdits = applyEdits;
    m_masterTimeoutS = timeoutSec > 0 ? timeoutSec : 300;
}

void AgentToolRunner::setMasterChain(const QVariantList &chain)
{
    m_masterChain = chain;
}

void AgentToolRunner::setHoneyHandoff(bool on)
{
    m_honeyHandoff = on;
}

void AgentToolRunner::completeManagedRun(const QString &requestId,
                                         const QVariantMap &run)
{
    emit managedAgentRunCompleted(requestId, run);
}

QString AgentToolRunner::masterSystemPrompt(bool honey)
{
    if (honey)
        // Handoff denso (frugalidad): el maestro responde el mínimo accionable en
        // clave:valor, sin prosa ni JSON pretty. Mismo contenido, ~mitad de tokens.
        return QStringLiteral(
            "Sos un experto sénior asistiendo a otro agente de código. Respondé en "
            "formato DENSO clave:valor, una línea por dato (ej. cause: ..., fix: ..., "
            "files: a.cpp:42, b.h). Sin prosa, sin preámbulo, sin JSON pretty. Sólo "
            "lo accionable.");
    return QStringLiteral(
        "Sos un experto sénior asistiendo a otro agente de código. "
        "Respondé conciso, correcto y accionable.");
}

QString AgentToolRunner::runManagedMaster(const QString &cliName, const QString &cliPath,
                                          bool applyEdits, int timeoutSec,
                                          const QString &question, const QString &context,
                                          const QString &cwd, bool *ok)
{
    if (cliPath.isEmpty())
        return QStringLiteral("[ask_teacher: CLI maestro '%1' no encontrado en PATH. "
                              "Instalalo o configurá el maestro en el perfil.]").arg(cliName);
    QString prompt = question;
    if (!context.isEmpty())
        prompt = QStringLiteral("Contexto:\n%1\n\nProblema:\n%2").arg(context, question);
    if (!applyEdits)
        prompt += QStringLiteral("\n\nNO modifiques archivos. Devolvé sólo un plan/solución concreta.");
    else
        prompt += QStringLiteral("\n\nResolvé el problema en el proyecto (podés editar archivos). "
                                 "Al terminar resumí qué cambiaste y qué verificaste.");

    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const int timeout = timeoutSec > 0 ? timeoutSec : 300;
    const QVariantMap request{
        {QStringLiteral("requestId"), requestId},
        {QStringLiteral("runtime"), cliName},
        {QStringLiteral("cliPath"), cliPath},
        {QStringLiteral("prompt"), prompt},
        {QStringLiteral("workspace"), cwd},
        {QStringLiteral("ownerId"), m_sessionId},
        {QStringLiteral("taskId"), m_sessionId},
        {QStringLiteral("applyEdits"), applyEdits},
        {QStringLiteral("claimWorkspace"), applyEdits},
        {QStringLiteral("captureDeliverables"), applyEdits},
        {QStringLiteral("approvalMode"), applyEdits ? QStringLiteral("ask") : QStringLiteral("plan")},
        {QStringLiteral("timeoutSec"), timeout},
        {QStringLiteral("idleTimeoutSec"), qMax(60, qMin(timeout, 600))},
        {QStringLiteral("presentation"), QStringLiteral("managed_panel")},
        {QStringLiteral("visibleRequested"), false}
    };

    QEventLoop loop;
    QTimer timer;
    QVariantMap completion;
    bool received = false;
    const QMetaObject::Connection connection = connect(
        this, &AgentToolRunner::managedAgentRunCompleted, this,
        [&loop, &completion, &received, requestId](const QString &id,
                                                    const QVariantMap &run) {
            if (id != requestId) return;
            completion = run;
            received = true;
            loop.quit();
        });
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    emit managedAgentRunRequested(request);
    timer.start((timeout + 30) * 1000);
    loop.exec();
    disconnect(connection);
    if (!received) {
        emit managedAgentRunCancelRequested(requestId);
        if (ok) *ok = false;
        return QStringLiteral("[ask_teacher: el supervisor durable superó el timeout de %1s]")
            .arg(timeout);
    }
    const QString status = completion.value(QStringLiteral("status")).toString();
    if (status != QLatin1String("finished")) {
        if (ok) *ok = false;
        return QStringLiteral("[ask_teacher: el CLI %1 terminó con estado %2: %3]")
            .arg(cliName, status, completion.value(QStringLiteral("summary")).toString());
    }
    if (ok) *ok = true;
    QString answer = completion.value(QStringLiteral("stdoutTail")).toString().trimmed();
    if (answer.isEmpty()) answer = completion.value(QStringLiteral("summary")).toString();
    return QStringLiteral("[Respuesta del maestro %1 · corrida %2]\n%3")
        .arg(cliName, completion.value(QStringLiteral("runId")).toString(), answer);
}

// Invoca claude-code / codex mediante el supervisor durable. cwd = proyecto.
QString AgentToolRunner::runMasterCli(const QString &cliName, const QString &cliPath,
                                      bool applyEdits, int timeoutSec,
                                      const QString &question, const QString &context,
                                      const QString &cwd, bool *ok)
{
    if (cliName != QLatin1String("claude") && cliName != QLatin1String("codex"))
        return QStringLiteral("[ask_teacher: CLI maestro desconocido: %1]").arg(cliName);
    return runManagedMaster(cliName, cliPath, applyEdits, timeoutSec,
                            question, context, cwd, ok);
}

// Consulta HTTP OpenAI-compat a un maestro. ok=true sólo si hubo respuesta útil.
QString AgentToolRunner::runHttpTeacher(const QString &url, const QString &model,
                                        const QString &key, const QString &question,
                                        const QString &context, bool *ok)
{
    if (url.isEmpty())
        return QStringLiteral("[ask_teacher: endpoint del maestro vacío]");
    const QString mdl = model.isEmpty() ? QStringLiteral("default") : model;
    QString userMsg = question;
    if (!context.isEmpty())
        userMsg = QStringLiteral("Contexto:\n%1\n\nPregunta:\n%2").arg(context, question);
    const QJsonArray msgs{
        QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                    {QStringLiteral("content"), masterSystemPrompt(m_honeyHandoff)}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), userMsg}}};
    const QJsonObject payload{
        {QStringLiteral("model"), mdl},
        {QStringLiteral("messages"), msgs},
        {QStringLiteral("stream"), false}};
    const QUrl endpoint(url.endsWith(QLatin1Char('/'))
                            ? url + QStringLiteral("v1/chat/completions")
                            : url + QStringLiteral("/v1/chat/completions"));
    QString err;
    const QByteArray resp = httpPostJson(endpoint,
                                         QJsonDocument(payload).toJson(QJsonDocument::Compact),
                                         &err, 120000, key);
    if (resp.isEmpty())
        return QStringLiteral("[ask_teacher: error consultando al maestro: %1]").arg(err);
    const QJsonObject root = QJsonDocument::fromJson(resp).object();
    const QString answer = root.value(QStringLiteral("choices")).toArray().isEmpty()
        ? QString()
        : root.value(QStringLiteral("choices")).toArray().first().toObject()
              .value(QStringLiteral("message")).toObject()
              .value(QStringLiteral("content")).toString();
    if (answer.isEmpty())
        return QStringLiteral("[ask_teacher: respuesta vacía del maestro]");
    if (ok) *ok = true;
    return QStringLiteral("[Respuesta del modelo maestro]\n") + answer;
}

// Recorre la cadena de fallbacks en orden; corta y devuelve al primer éxito.
// Si todos fallan, devuelve el último error acumulado.
QString AgentToolRunner::runMasterChain(const QString &question, const QString &context,
                                        const QString &cwd, bool *ok)
{
    QString last = QStringLiteral("[ask_teacher: cadena de maestros vacía]");
    int level = 0;
    for (const QVariant &v : m_masterChain) {
        const QVariantMap e = v.toMap();
        const QString type = e.value(QStringLiteral("type")).toString();
        const QString lbl  = e.value(QStringLiteral("label")).toString();
        ++level;
        bool localOk = false;
        QString res;
        if (type == QLatin1String("cli")) {
            res = runMasterCli(e.value(QStringLiteral("cliName")).toString(),
                               e.value(QStringLiteral("cliPath")).toString(),
                               e.value(QStringLiteral("applyEdits"), true).toBool(),
                               e.value(QStringLiteral("timeoutSec"), 300).toInt(),
                               question, context, cwd, &localOk);
        } else { // http (incluye profile ya resuelto a http)
            res = runHttpTeacher(e.value(QStringLiteral("httpUrl")).toString(),
                                 e.value(QStringLiteral("httpModel")).toString(),
                                 e.value(QStringLiteral("httpKey")).toString(),
                                 question, context, &localOk);
        }
        if (localOk) {
            if (ok) *ok = true;
            return res;
        }
        last = QStringLiteral("[fallback %1%2 falló] %3")
                   .arg(level)
                   .arg(lbl.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(lbl))
                   .arg(res);
    }
    return last;
}

void AgentToolRunner::shutdown()
{
    if (m_shellTimer) m_shellTimer->stop();
    if (m_shellProc) {
        m_shellProc->disconnect(this);
        m_shellProc->kill();
        m_shellProc->waitForFinished(2000);
        delete m_shellProc;
        m_shellProc = nullptr;
        m_shellCallId.clear();
    }
    for (McpClient *c : std::as_const(m_mcp)) { c->shutdown(); delete c; }
    m_mcp.clear();
}

void AgentToolRunner::initServers(const QVariantList &cfg, const QString &cwd)
{
    shutdown();
    for (const QVariant &v : cfg) {
        const QVariantMap s = v.toMap();
        if (!s.value(QStringLiteral("enabled"), true).toBool()) continue;
        if (s.value(QStringLiteral("type"), QStringLiteral("local")).toString()
                != QLatin1String("local")) continue;
        const QString name = s.value(QStringLiteral("name")).toString();
        const QString cmd  = s.value(QStringLiteral("command")).toString();
        if (name.isEmpty() || cmd.isEmpty()) continue;

        auto *c = new McpClient(name);   // sin parent: vive en este hilo worker
        connect(c, &McpClient::logAppended, this, &AgentToolRunner::logAppended);
        if (c->start(cmd, cwd)) m_mcp.append(c);
        else delete c;
    }

    QVariantList defs;
    for (McpClient *c : m_mcp) {
        for (const McpClient::ToolDef &t : c->tools()) {
            defs.append(QVariantMap{
                {QStringLiteral("server"), c->serverName()},
                {QStringLiteral("name"), t.name},
                {QStringLiteral("description"), t.description},
                {QStringLiteral("annotations"), t.annotations.toVariantMap()},
                {QStringLiteral("safety"), ToolExecutionSafety::toVariantMap(
                     ToolExecutionSafety::fromMcpTool(t.name, t.description, t.annotations))},
                {QStringLiteral("schema"), QVariant::fromValue(
                     QString::fromUtf8(QJsonDocument(t.inputSchema).toJson(QJsonDocument::Compact)))}
            });
        }
    }
    emit serversReady(defs);
}

void AgentToolRunner::setCorrelationId(const QString &correlationId)
{
    m_correlationId = correlationId;
}

void AgentToolRunner::setLivePreviewEnabled(bool enabled)
{
    m_livePreviewEnabled = enabled;
}

QString AgentToolRunner::captureMcpPreview(McpClient *client, const QString &currentTool,
                                           const QJsonObject &rawResult)
{
    return captureMcpObservation(client, currentTool, rawResult)
        .value(QStringLiteral("imagePath")).toString();
}

QVariantMap AgentToolRunner::captureMcpObservation(McpClient *client,
                                                   const QString &currentTool,
                                                   const QJsonObject &rawResult)
{
    QVariantMap observation;
    QString path = saveMcpImage(rawResult, QStringLiteral("browser-after"));
    const QString lowerCurrent = currentTool.toLower();
    const bool currentIsSnapshot = lowerCurrent.contains(QStringLiteral("accessibility"))
        || lowerCurrent.contains(QStringLiteral("snapshot"))
        || lowerCurrent.contains(QStringLiteral("aria"))
        || lowerCurrent.contains(QStringLiteral("dom"))
        || lowerCurrent.contains(QStringLiteral("page_source"))
        || lowerCurrent.contains(QStringLiteral("page_content"));
    if (currentIsSnapshot) {
        const QString snapshot = saveMcpSnapshot(rawResult, QStringLiteral("browser-after"));
        if (!snapshot.isEmpty()) observation[QStringLiteral("snapshotPath")] = snapshot;
    }
    if (!path.isEmpty()) observation[QStringLiteral("imagePath")] = path;
    if (!m_livePreviewEnabled || !client
        || (!currentTool.isEmpty() && !isBrowserMcpTool(currentTool)))
        return observation;

    // Muchos servidores MCP entregan el DOM como texto y exponen la captura
    // como otra tool. Sólo la invocamos cuando su schema no exige parámetros;
    // así el inspector funciona con implementaciones distintas de Playwright
    // sin adivinar URLs, páginas o nombres de aplicaciones.
    for (const McpClient::ToolDef &tool : client->tools()) {
        const QString lower = tool.name.toLower();
        if (!lower.contains(QStringLiteral("screenshot"))
            || !mcpToolAcceptsEmptyArguments(tool))
            continue;
        if (!path.isEmpty() && !lowerCurrent.isEmpty()) break;
        bool ok = false;
        QJsonObject screenshotResult;
        client->callTool(tool.name, {}, &ok, &screenshotResult,
                         QString(), m_correlationId);
        if (ok) {
            path = saveMcpImage(screenshotResult, QStringLiteral("browser"));
            if (!path.isEmpty()) break;
        }
    }
    if (!path.isEmpty()) observation[QStringLiteral("imagePath")] = path;
    for (const McpClient::ToolDef &tool : client->tools()) {
        if (!isMcpSnapshotTool(tool) || !mcpToolAcceptsEmptyArguments(tool)) continue;
        bool ok = false;
        QJsonObject snapshotResult;
        client->callTool(tool.name, {}, &ok, &snapshotResult,
                         QString(), m_correlationId);
        if (ok) {
            const QString snapshot = saveMcpSnapshot(snapshotResult, QStringLiteral("browser"));
            if (!snapshot.isEmpty()) {
                observation[QStringLiteral("snapshotPath")] = snapshot;
                break;
            }
        }
    }
    return observation;
}

void AgentToolRunner::executeTool(const QString &callId, const QString &name,
                                  const QString &argsJson, const QString &cwd)
{
    const QJsonObject args = QJsonDocument::fromJson(argsJson.toUtf8()).object();

    const auto blockedInReadOnly = [this, &name, &args]() {
        if (!m_readOnly) return false;
        if (name == QLatin1String("run_shell")) return !m_readOnlyShell;
        if (name == QLatin1String("write_file") || name == QLatin1String("edit_file")
            || name == QLatin1String("email_send") || name == QLatin1String("task")
            || name == QLatin1String("mcp_call_tool")
            || name == QLatin1String("browser_skill_replay")
            || name.startsWith(QLatin1String("desktop_"))) return true;
        if (name == QLatin1String("memory")) {
            const QString action = args.value(QStringLiteral("action")).toString().toLower();
            return action == QLatin1String("save") || action == QLatin1String("forget")
                || (action == QLatin1String("prune")
                    && !args.value(QStringLiteral("dry_run")).toBool())
                || (action == QLatin1String("decay")
                    && !args.value(QStringLiteral("dry_run")).toBool());
        }
        if (name == QLatin1String("graph")) {
            const QString action = args.value(QStringLiteral("action")).toString().toLower();
            return action != QLatin1String("query") && action != QLatin1String("decisions");
        }
        if (name.startsWith(QLatin1String("mcp__"))) return true;
        return false;
    };
    if (blockedInReadOnly()) {
        QVariantMap out{{QStringLiteral("callId"), callId},
                        {QStringLiteral("name"), name},
                        {QStringLiteral("correlationId"), m_correlationId},
                        {QStringLiteral("arguments"), AutomationArtifactStore::redact(argsJson).left(8192)},
                        {QStringLiteral("result"), QStringLiteral(
                            "[modo solo lectura: la tool '%1' fue bloqueada; "
                            "el revisor/verificador no puede modificar archivos ni "
                            "producir efectos externos]").arg(name)},
                        {QStringLiteral("ok"), false}};
        emit toolExecuted(out);
        return;
    }

    // run_shell es ASÍNCRONO: spawnea y vuelve. La salida se streamea por
    // toolOutputChunk y el resultado final llega por toolExecuted al terminar.
    // No bloquea el hilo worker → el proceso es cancelable (cancelShell).
    if (name == QLatin1String("run_shell")) {
        const QString command = args.value(QStringLiteral("command")).toString();
        int timeoutS = args.value(QStringLiteral("timeout_s")).toInt(120);
        if (timeoutS <= 0) timeoutS = 120;
        if (timeoutS > 1800) timeoutS = 1800;
        startShell(callId, command, cwd, timeoutS);
        return;
    }

    QVariantMap out{{QStringLiteral("callId"), callId}, {QStringLiteral("name"), name}};
    out[QStringLiteral("correlationId")] = m_correlationId;
    out[QStringLiteral("arguments")] = AutomationArtifactStore::redact(argsJson).left(8192);
    const auto attachObservation = [&out](const QVariantMap &observation,
                                          const QString &prefix) {
        for (const QString &key : {QStringLiteral("imagePath"), QStringLiteral("snapshotPath")}) {
            const QString path = observation.value(key).toString();
            if (!path.isEmpty()) out[prefix + key.left(1).toUpper() + key.mid(1)] = path;
        }
    };
    bool ok = false;
    QString result;

    if (name == QLatin1String("mcp_search_tools")) {
        const QString query = args.value(QStringLiteral("query")).toString().trimmed().toLower();
        const QString serverFilter = args.value(QStringLiteral("server")).toString().trimmed();
        const int limit = qBound(1, args.value(QStringLiteral("limit")).toInt(5), 10);
        struct Match {
            int score;
            QString name;
            QString description;
            QJsonObject schema;
            QVariantMap safety;
        };
        QList<Match> matches;
        const QStringList terms = query.split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_-]+")), Qt::SkipEmptyParts);
        for (McpClient *c : std::as_const(m_mcp)) {
            if (!serverFilter.isEmpty() && c->serverName().compare(serverFilter, Qt::CaseInsensitive) != 0)
                continue;
            for (const McpClient::ToolDef &t : c->tools()) {
                const QString full = kMcpPrefix + c->serverName() + QStringLiteral("__") + t.name;
                const QString hay = (full + QLatin1Char(' ') + t.description).toLower();
                int score = query.isEmpty() ? 0 : (hay.contains(query) ? 100 : 0);
                for (const QString &term : terms) if (hay.contains(term)) score += 10;
                if (score > 0) matches.append({
                    score, full, t.description, t.inputSchema,
                    ToolExecutionSafety::toVariantMap(ToolExecutionSafety::fromMcpTool(
                        t.name, t.description, t.annotations))
                });
            }
        }
        std::sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) {
            return a.score != b.score ? a.score > b.score : a.name < b.name;
        });
        QJsonArray found;
        for (int i = 0; i < qMin(limit, matches.size()); ++i)
            found.append(QJsonObject{{"name", matches[i].name}, {"description", matches[i].description},
                                     {"inputSchema", matches[i].schema},
                                     {"safety", QJsonObject::fromVariantMap(matches[i].safety)}});
        result = QString::fromUtf8(QJsonDocument(QJsonObject{{"tools", found}, {"matched", found.size()}}).toJson(QJsonDocument::Compact));
        ok = true;
    } else if (name == QLatin1String("mcp_call_tool")) {
        const QString target = args.value(QStringLiteral("name")).toString();
        const QJsonObject inner = args.value(QStringLiteral("arguments")).toObject();
        if (!target.startsWith(kMcpPrefix)) {
            result = QStringLiteral("[mcp: nombre inválido; usá el nombre exacto devuelto por mcp_search_tools]");
        } else {
            const QString rest = target.mid(kMcpPrefix.size());
            const int sep = rest.indexOf(QStringLiteral("__"));
            McpClient *client = nullptr;
            QString bare;
            if (sep >= 0) {
                const QString server = rest.left(sep); bare = rest.mid(sep + 2);
                for (McpClient *c : std::as_const(m_mcp)) if (c->serverName() == server) { client = c; break; }
            }
            const McpClient::ToolDef *tool = findMcpTool(client, bare);
            if (!tool) {
                result = QStringLiteral("[mcp: server/tool no encontrado: %1]").arg(target);
            } else {
                const auto contract = ToolExecutionSafety::fromMcpTool(
                    tool->name, tool->description, tool->annotations);
                const QString hash = ToolExecutionSafety::payloadHash(
                    client->serverName(), bare, inner);
                const QString key = ToolExecutionSafety::idempotencyKey(m_correlationId, hash);
                out[QStringLiteral("payloadHash")] = hash;
                out[QStringLiteral("idempotencyKey")] = key;
                out[QStringLiteral("safety")] = ToolExecutionSafety::toVariantMap(contract);
                out[QStringLiteral("externalWrite")] = contract.effect != QLatin1String("read");

                QJsonObject ledger = loadMcpLedger();
                const QJsonObject prior = ledger.value(key).toObject();
                const QString priorStatus = prior.value(QStringLiteral("status")).toString();
                if (out.value(QStringLiteral("externalWrite")).toBool()
                    && (priorStatus == QLatin1String("executed")
                        || priorStatus == QLatin1String("verified"))) {
                    ok = true;
                    result = prior.value(QStringLiteral("result")).toString();
                    out[QStringLiteral("deduplicated")] = true;
                    out[QStringLiteral("receipt")] = prior.toVariantMap();
                } else {
                    if (m_livePreviewEnabled)
                        attachObservation(captureMcpObservation(client, target, {}),
                                          QStringLiteral("before"));
                    QJsonObject rawResult;
                    result = client->callTool(bare, inner, &ok, &rawResult,
                                              key, m_correlationId);
                    if (ok) {
                        attachObservation(captureMcpObservation(client, target, rawResult),
                                          QStringLiteral("after"));
                    }
                    const QJsonObject structured =
                        rawResult.value(QStringLiteral("structuredContent")).toObject();
                    const QJsonObject serverReceipt =
                        structured.value(QStringLiteral("receipt")).toObject();
                    QJsonObject receipt{
                        {QStringLiteral("status"), ok ? QStringLiteral("executed")
                                                     : QStringLiteral("failed")},
                        {QStringLiteral("server"), client->serverName()},
                        {QStringLiteral("tool"), bare},
                        {QStringLiteral("correlationId"), m_correlationId},
                        {QStringLiteral("payloadHash"), hash},
                        {QStringLiteral("idempotencyKey"), key},
                        {QStringLiteral("resultHash"), ToolExecutionSafety::resultHash(result)},
                        {QStringLiteral("result"), result.left(64 * 1024)},
                        {QStringLiteral("ts"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
                    };
                    // structuredContent.receipt permite prueba fuerte sin acoplarse
                    // a un proveedor: externalId, before/after y rollbackToken.
                    for (const QString &field : {
                             QStringLiteral("externalId"), QStringLiteral("before"),
                             QStringLiteral("after"), QStringLiteral("rollbackToken"),
                             QStringLiteral("verification")}) {
                        if (serverReceipt.contains(field))
                            receipt.insert(field, serverReceipt.value(field));
                    }
                    if (serverReceipt.value(QStringLiteral("status")).toString()
                            == QLatin1String("verified"))
                        receipt[QStringLiteral("status")] = QStringLiteral("verified");
                    if (ok && contract.effect == QLatin1String("read"))
                        receipt[QStringLiteral("status")] = QStringLiteral("verified");
                    out[QStringLiteral("receipt")] = receipt.toVariantMap();
                    if (ok && out.value(QStringLiteral("externalWrite")).toBool()) {
                        // Se persiste como executed para auditoría, pero sólo un recibo
                        // verified se deduplica automáticamente.
                        ledger.insert(key, receipt);
                        saveMcpLedger(ledger);
                    }
                }
            }
        }
    } else if (name.startsWith(kMcpPrefix)) {
        // mcp__<server>__<tool>
        const QString rest = name.mid(kMcpPrefix.size());
        const int sep = rest.indexOf(QStringLiteral("__"));
        McpClient *c = nullptr;
        QString bare;
        if (sep >= 0) {
            const QString server = rest.left(sep);
            bare = rest.mid(sep + 2);
            for (McpClient *cc : m_mcp)
                if (cc->serverName() == server) { c = cc; break; }
        }
        if (!c) result = QStringLiteral("[mcp: server/tool no encontrado: %1]").arg(name);
        else {
            if (m_livePreviewEnabled)
                attachObservation(captureMcpObservation(
                    c, c->serverName() + QLatin1Char('_') + bare, {}),
                                  QStringLiteral("before"));
            QJsonObject rawResult;
            result = c->callTool(bare, args, &ok, &rawResult,
                                 QString(), m_correlationId);
            if (ok) {
                attachObservation(captureMcpObservation(
                    c, c->serverName() + QLatin1Char('_') + bare, rawResult),
                                  QStringLiteral("after"));
            }
        }
    } else {
        if (m_livePreviewEnabled && name.startsWith(QLatin1String("desktop_"))) {
            const QString dir = AutomationArtifactStore::rootDir()
                                + QStringLiteral("/runtime-observations");
            QDir().mkpath(dir);
            const QString path = dir + QStringLiteral("/desktop-before-%1.jpg")
                .arg(QDateTime::currentMSecsSinceEpoch());
            QString captureError;
            const QString saved = DesktopAutomationBackend::saveCapture(
                args.value(QStringLiteral("scope_kind")).toString(QStringLiteral("screen")),
                args.value(QStringLiteral("target_id")).toString(), path, &captureError);
            if (!saved.isEmpty()) out[QStringLiteral("beforeImagePath")] = saved;
        }
        result = runNative(name, args, cwd, out, &ok);
        if (ok && m_livePreviewEnabled && name.startsWith(QLatin1String("desktop_"))
            && name != QLatin1String("desktop_observe")) {
            const QString dir = AutomationArtifactStore::rootDir()
                                + QStringLiteral("/runtime-observations");
            QDir().mkpath(dir);
            const QString path = dir + QStringLiteral("/desktop-%1.jpg")
                .arg(QDateTime::currentMSecsSinceEpoch());
            QString captureError;
            const QString saved = DesktopAutomationBackend::saveCapture(
                args.value(QStringLiteral("scope_kind")).toString(QStringLiteral("screen")),
                args.value(QStringLiteral("target_id")).toString(), path, &captureError);
            if (!saved.isEmpty()) {
                out[QStringLiteral("imagePath")] = saved;
                out[QStringLiteral("afterImagePath")] = saved;
                AutomationArtifactStore::cleanupRuntimeObservations();
            }
        }
        if (ok && m_livePreviewEnabled && name.startsWith(QLatin1String("desktop_"))
            && out.contains(QStringLiteral("imagePath"))
            && !out.contains(QStringLiteral("afterImagePath"))) {
            out[QStringLiteral("afterImagePath")] = out.value(QStringLiteral("imagePath"));
        }
    }

    if (DesktopComputerUse::isDesktopTool(name)) {
        if (!ok && DesktopRecoveryPolicy::shouldReobserve(result))
            result += QStringLiteral(
                "\n[recovery: stale/ambiguo; ejecutá desktop_snapshot o desktop_observe, "
                "re-resolvé el target y no repitas la misma acción]");
        QString strategy = out.value(QStringLiteral("desktopStrategy")).toString();
        if (strategy.isEmpty())
            strategy = name == QLatin1String("desktop_control_action")
                    || name == QLatin1String("desktop_click_element")
                ? QStringLiteral("uia") : QStringLiteral("native");
        const DesktopComputerUse::ActionReceipt receipt = DesktopComputerUse::makeReceipt(
            name, args, ok, result, m_sessionId, m_correlationId, strategy,
            args.value(QStringLiteral("snapshot_id")).toString(),
            out.value(QStringLiteral("desktopTarget")).toMap());
        out[QStringLiteral("desktopReceipt")] = receipt.toVariantMap();
        // El texto sigue siendo compacto para modelos pequeños; el receipt
        // completo viaja en el resultado estructurado y queda persistido por el
        // caller junto con la tool call.
        result += QStringLiteral("\nreceipt_id=%1 status=%2")
                      .arg(receipt.receiptId, receipt.status);
    }

    if (ok && out.value(QStringLiteral("isWrite")).toBool()) {
        const QString absPath = out.value(QStringLiteral("absPath")).toString();
        m_projectBrainDirtyPaths.insert(absPath);
        if (!absPath.isEmpty()) {
            const QString rel = QDir::fromNativeSeparators(QDir(cwd).relativeFilePath(absPath));
            ContextIndex::refresh(cwd, QStringList{rel});
        }
    }

    out[QStringLiteral("result")] = result;
    out[QStringLiteral("ok")] = ok;
    emit toolExecuted(out);
}

// Ejecución de tools nativas (idéntica a la vieja LlamaAgentBackend::executeTool,
// pero sin tocar UI: el write devuelve metadata para que el main arme diff/snapshot).
QString AgentToolRunner::runNative(const QString &name, const QJsonObject &args,
                                   const QString &cwd, QVariantMap &out, bool *ok)
{
    // Mouse, teclado, ventanas y capturas son recursos globales de la PC. Los
    // runtimes concurrentes pueden leer repos distintos en paralelo, pero toda
    // tool desktop_* se serializa para que sus observaciones y acciones no se
    // crucen entre conversaciones.
    static QMutex desktopMutex;
    QMutexLocker<QMutex> desktopLock(
        name.startsWith(QLatin1String("desktop_")) ? &desktopMutex : nullptr);
    if (ok) *ok = false;
    if (DesktopComputerUse::isDesktopActionTool(name)) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_desktopLease.leaseId.isEmpty()) {
            m_desktopLease.leaseId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_desktopLease.sessionId = m_sessionId;
            m_desktopLease.expiresAt = now + 30 * 60 * 1000;
        }
        if (now > m_desktopLease.expiresAt || !m_desktopLease.consume()) {
            return QStringLiteral("[desktop: lease expirado o límite de acciones alcanzado; "
                                  "detené el run o iniciá una sesión nueva]");
        }
        out[QStringLiteral("desktopLease")] = m_desktopLease.toVariantMap();
    }
    auto validateDesktopSnapshot = [&](const QString &kind, const QString &target,
                                       QString *error) {
        return DesktopAutomationBackend::validateSnapshot(
            kind, target, args.value(QStringLiteral("snapshot_id")).toString(), error);
    };
    HarnessSkillsModule skillPolicy;
    skillPolicy.set = m_skillPolicyDeclared;
    skillPolicy.include = m_skillInclude;
    skillPolicy.exclude = m_skillExclude;
    if (name == QLatin1String("skill_list")) {
        const QVariantList skills = PortableSkillStore::list(cwd, skillPolicy);
        out[QStringLiteral("skills")] = skills;
        out[QStringLiteral("count")] = skills.size();
        if (ok) *ok = true;
        if (skills.isEmpty())
            return QStringLiteral("[sin habilidades portables instaladas]");
        QStringList lines{QStringLiteral("Habilidades disponibles:")};
        for (const QVariant &item : skills) {
            const QVariantMap skill = item.toMap();
            lines << QStringLiteral("- %1 [%2]: %3")
                         .arg(skill.value(QStringLiteral("name")).toString(),
                              skill.value(QStringLiteral("scope")).toString(),
                              skill.value(QStringLiteral("description")).toString());
        }
        return lines.join(QLatin1Char('\n'));
    }
    if (name == QLatin1String("skill_load")) {
        const QVariantMap skill =
            PortableSkillStore::load(args.value(QStringLiteral("name")).toString(), cwd,
                                     skillPolicy);
        for (auto it = skill.cbegin(); it != skill.cend(); ++it) out[it.key()] = it.value();
        const bool loaded = skill.value(QStringLiteral("ok")).toBool();
        if (ok) *ok = loaded;
        if (!loaded)
            return QStringLiteral("[skill_load: %1]")
                .arg(skill.value(QStringLiteral("error")).toString());
        return QStringLiteral("[habilidad %1 · scope=%2]\n%3")
            .arg(skill.value(QStringLiteral("name")).toString(),
                 skill.value(QStringLiteral("scope")).toString(),
                 skill.value(QStringLiteral("instructions")).toString());
    }
    if (args.contains(QStringLiteral("_parse_error"))) {
        return QStringLiteral("[argumentos JSON inválidos para %1: %2. Probablemente "
                              "el servidor truncó un tool_call demasiado grande (%3 "
                              "chars). No repitas el mismo write_file/edit_file gigante: "
                              "dividí la escritura en partes con run_shell y heredocs "
                              "append, o hacé ediciones más chicas y verificá con "
                              "read_file/grep.]")
            .arg(name,
                 args.value(QStringLiteral("_parse_error")).toString(),
                 QString::number(args.value(QStringLiteral("_raw_chars")).toInt()));
    }
    if (name == QLatin1String("recent_actions")) {
        // Tail del propio rastro operacional (tool_calls/results/fallos) para que el
        // modelo relea qué intentó y qué falló, y se auto-corrija (loop log+tail).
        const int n = args.value(QStringLiteral("count")).toInt(20);
        const QString out = AgentEventLog::tail(cwd, m_sessionId, n);
        if (ok) *ok = true;
        return out;
    }
    if (name == QLatin1String("desktop_snapshot")) {
        const QString kind = args.value(QStringLiteral("scope_kind")).toString(
            QStringLiteral("window"));
        const QString target = args.value(QStringLiteral("target_id")).toString();
        QString error;
        const QVariantMap snap = DesktopAutomationBackend::snapshot(
            kind, target, args.value(QStringLiteral("query")).toString(),
            args.value(QStringLiteral("max")).toInt(120), false, &error);
        if (snap.isEmpty()) return QStringLiteral("[desktop_snapshot: %1]").arg(error);
        out[QStringLiteral("desktopSnapshot")] = snap;
        out[QStringLiteral("desktopTarget")] = snap.value(QStringLiteral("target"));
        if (ok) *ok = true;
        const QString json = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(snap)).toJson(QJsonDocument::Compact));
        return QStringLiteral("[desktop_snapshot: ok]\nsnapshot=%1").arg(json);
    }
    if (name == QLatin1String("desktop_windows")) {
        // Inventario estructurado de ventanas (título + pid + geometría). Estado
        // barato para orientarse y elegir un objetivo SIN gastar una captura (la
        // imagen queda como fallback vía desktop_observe).
        const QVariantList wins = DesktopAutomationBackend::windows();
        QStringList lines;
        for (const QVariant &v : wins) {
            const QVariantMap w = v.toMap();
            lines << QStringLiteral("id=%1  pid=%2  %3x%4@(%5,%6)  \"%7\"")
                         .arg(w.value(QStringLiteral("id")).toString(),
                              w.value(QStringLiteral("pid")).toString())
                         .arg(w.value(QStringLiteral("width")).toInt())
                         .arg(w.value(QStringLiteral("height")).toInt())
                         .arg(w.value(QStringLiteral("x")).toInt())
                         .arg(w.value(QStringLiteral("y")).toInt())
                         .arg(w.value(QStringLiteral("label")).toString());
            // El orden sigue el Z-order de Windows: las ventanas relevantes están
            // primero. Un inventario enorme se reinyecta al LLM y agrega segundos
            // de prefill sin mejorar la elección del target.
            if (lines.size() >= 16) break;
        }
        if (ok) *ok = true;
        if (lines.isEmpty())
            return QStringLiteral("[desktop_windows: no hay ventanas visibles "
                                  "(¿sesión sin escritorio interactivo?)]");
        return QStringLiteral("[desktop_windows: %1 ventana(s) visible(s)]\n"
                              "Usá el id con scope_kind='window' en desktop_observe/click.\n%2")
            .arg(lines.size()).arg(lines.join(QLatin1Char('\n')));
    }
    if (name == QLatin1String("desktop_controls")) {
        // Árbol de controles (UIA) de una ventana: nombre+rol+geometría+invocable.
        // DOM-aware: el modelo elige un control por NOMBRE, no por pixel.
        const QString target = args.value(QStringLiteral("target_id")).toString();
        const QString query = args.value(QStringLiteral("query")).toString();
        const int max = args.value(QStringLiteral("max")).toInt(120);
        QString error;
        const QVariantList rows = DesktopAutomationBackend::controls(target, query, max, &error);
        if (rows.isEmpty() && !error.isEmpty())
            return QStringLiteral("[desktop_controls: %1]").arg(error);
        QStringList lines;
        for (const QVariant &v : rows) {
            const QVariantMap c = v.toMap();
            lines << QStringLiteral("controlId=%1  [%2]%3%4  %5x%6@(%7,%8)  \"%9\"")
                         .arg(c.value(QStringLiteral("controlId")).toString(),
                              c.value(QStringLiteral("role")).toString(),
                              c.value(QStringLiteral("invokable")).toBool() ? QStringLiteral(" invoke") : QString(),
                              c.value(QStringLiteral("enabled")).toBool() ? QString() : QStringLiteral(" disabled"))
                         .arg(c.value(QStringLiteral("width")).toInt())
                         .arg(c.value(QStringLiteral("height")).toInt())
                         .arg(c.value(QStringLiteral("x")).toInt())
                         .arg(c.value(QStringLiteral("y")).toInt())
                         .arg(c.value(QStringLiteral("name")).toString());
        }
        QString snapshotError;
        const QVariantMap snap = DesktopAutomationBackend::snapshot(
            QStringLiteral("window"), target, query, max, false, &snapshotError);
        if (!snap.isEmpty()) {
            out[QStringLiteral("desktopSnapshotId")] = snap.value(QStringLiteral("snapshotId"));
            out[QStringLiteral("desktopSnapshot")] = snap;
            out[QStringLiteral("desktopTarget")] = snap.value(QStringLiteral("target"));
        }
        const QString snapshotId = snap.value(QStringLiteral("snapshotId")).toString();
        if (ok) *ok = true;
        if (lines.isEmpty())
            return QStringLiteral("[desktop_controls: 0 controles con nombre%1]\nsnapshot_id=%2")
                .arg(query.trimmed().isEmpty() ? QString()
                                               : QStringLiteral(" para \"%1\"").arg(query))
                .arg(snapshotId);
        return QStringLiteral("[desktop_controls: %1 control(es)]\n"
                              "snapshot_id=%2\nUsá el controlId con desktop_click_element "
                              "(mismo target_id).\n%3")
            .arg(lines.size()).arg(snapshotId).arg(lines.join(QLatin1Char('\n')));
    }
    if (name == QLatin1String("desktop_click_element")) {
        QString error;
        QVariantMap trace;
        const bool good = DesktopAutomationBackend::clickElement(
            args.value(QStringLiteral("target_id")).toString(),
            args.value(QStringLiteral("control_id")).toString(),
            args.value(QStringLiteral("snapshot_id")).toString(), &error, &trace);
        out[QStringLiteral("desktopStrategy")] = trace.value(QStringLiteral("strategy"),
                                                               QStringLiteral("uia"));
        out[QStringLiteral("desktopTarget")] = trace.value(QStringLiteral("target")).toMap();
        if (ok) *ok = good;
        if (!good) return QStringLiteral("[desktop_click_element: %1]").arg(error);
        const QString json = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(trace)).toJson(QJsonDocument::Compact));
        return QStringLiteral("[desktop_click_element: ok]\ntrace=%1").arg(json);
    }
    if (name == QLatin1String("desktop_control_action")) {
        QString error;
        QVariantMap trace;
        const QString target = args.value(QStringLiteral("target_id")).toString();
        const QString control = args.value(QStringLiteral("control_id")).toString();
        const bool good = DesktopAutomationBackend::validateSnapshot(
            QStringLiteral("window"), target,
            args.value(QStringLiteral("snapshot_id")).toString(), &error)
            && DesktopAutomationBackend::controlAction(
                target, control, args.value(QStringLiteral("action")).toString(),
                args.value(QStringLiteral("value")).toString(), &error, &trace);
        out[QStringLiteral("desktopStrategy")] = trace.value(QStringLiteral("strategy"),
                                                               QStringLiteral("uia-pattern"));
        out[QStringLiteral("desktopTarget")] = trace.value(QStringLiteral("target")).toMap();
        if (ok) *ok = good;
        if (!good) return QStringLiteral("[desktop_control_action: %1]").arg(error);
        const QString json = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(trace)).toJson(QJsonDocument::Compact));
        return QStringLiteral("[desktop_control_action: ok]\ntrace=%1").arg(json);
    }
    if (name == QLatin1String("desktop_click_text")) {
        QString error;
        const QString kind = args.value(QStringLiteral("scope_kind")).toString(QStringLiteral("screen"));
        const QString target = args.value(QStringLiteral("target_id")).toString();
        if (!validateDesktopSnapshot(kind, target, &error))
            return QStringLiteral("[desktop_click_text: %1]").arg(error);
        QVariantMap trace;
        const bool good = DesktopAutomationBackend::clickText(
            kind, target,
            args.value(QStringLiteral("text")).toString(),
            QStringLiteral("left"), 1, &error, &trace);
        out[QStringLiteral("desktopStrategy")] = QStringLiteral("ocr");
        out[QStringLiteral("desktopTarget")] = trace.value(QStringLiteral("target")).toMap();
        if (ok) *ok = good;
        if (!good) return QStringLiteral("[desktop_click_text: %1]").arg(error);
        const QString json = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(trace)).toJson(QJsonDocument::Compact));
        return QStringLiteral("[desktop_click_text: ok]\ntrace=%1").arg(json);
    }
    if (name == QLatin1String("desktop_find_image")) {
        QString error;
        const QString kind = args.value(QStringLiteral("scope_kind")).toString(QStringLiteral("screen"));
        const QString target = args.value(QStringLiteral("target_id")).toString();
        if (!validateDesktopSnapshot(kind, target, &error))
            return QStringLiteral("[desktop_find_image: %1]").arg(error);
        const QVariantMap match = DesktopAutomationBackend::findImage(
            kind, target,
            args.value(QStringLiteral("template_path")).toString(),
            args.value(QStringLiteral("threshold")).toDouble(0.88),
            args.value(QStringLiteral("min_scale")).toDouble(1.0),
            args.value(QStringLiteral("max_scale")).toDouble(1.0),
            args.value(QStringLiteral("require_unique")).toBool(true), &error);
        if (match.isEmpty()) return QStringLiteral("[desktop_find_image: %1]").arg(error);
        const bool found = match.value(QStringLiteral("found")).toBool()
                        && !match.value(QStringLiteral("ambiguous")).toBool();
        if (ok) *ok = found;
        const QString json = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(match)).toJson(QJsonDocument::Compact));
        return QStringLiteral("[desktop_find_image: %1]\nmatch=%2")
            .arg(found ? QStringLiteral("found") : error, json);
    }
    if (name == QLatin1String("desktop_click_image")) {
        QString error;
        const QString kind = args.value(QStringLiteral("scope_kind")).toString(QStringLiteral("screen"));
        const QString target = args.value(QStringLiteral("target_id")).toString();
        if (!validateDesktopSnapshot(kind, target, &error))
            return QStringLiteral("[desktop_click_image: %1]").arg(error);
        QVariantMap trace;
        const bool good = DesktopAutomationBackend::clickImage(
            kind, target,
            args.value(QStringLiteral("template_path")).toString(),
            args.value(QStringLiteral("threshold")).toDouble(0.88),
            args.value(QStringLiteral("min_scale")).toDouble(1.0),
            args.value(QStringLiteral("max_scale")).toDouble(1.0),
            args.value(QStringLiteral("button")).toString(QStringLiteral("left")),
            &error, &trace);
        out[QStringLiteral("desktopStrategy")] = QStringLiteral("template");
        out[QStringLiteral("desktopTarget")] = trace.value(QStringLiteral("target")).toMap();
        if (ok) *ok = good;
        if (!good) return QStringLiteral("[desktop_click_image: %1]").arg(error);
        const QString json = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(trace)).toJson(QJsonDocument::Compact));
        return QStringLiteral("[desktop_click_image: ok]\ntrace=%1").arg(json);
    }
    if (name == QLatin1String("desktop_wait_image")
        || name == QLatin1String("desktop_assert_image")) {
        QString error;
        const bool expected = args.value(name == QLatin1String("desktop_wait_image")
            ? QStringLiteral("appear") : QStringLiteral("should_exist")).toBool(true);
        const QString kind = args.value(QStringLiteral("scope_kind")).toString(QStringLiteral("screen"));
        const QString target = args.value(QStringLiteral("target_id")).toString();
        const QString path = args.value(QStringLiteral("template_path")).toString();
        const int timeout = args.value(QStringLiteral("timeout_ms")).toInt(
            name == QLatin1String("desktop_wait_image") ? 4000 : 1500);
        const double threshold = args.value(QStringLiteral("threshold")).toDouble(0.88);
        const double minScale = args.value(QStringLiteral("min_scale")).toDouble(1.0);
        const double maxScale = args.value(QStringLiteral("max_scale")).toDouble(1.0);
        const QVariantMap result = name == QLatin1String("desktop_wait_image")
            ? DesktopAutomationBackend::waitImage(kind, target, path, expected, timeout,
                                                   threshold, minScale, maxScale, &error)
            : DesktopAutomationBackend::assertImage(kind, target, path, expected, timeout,
                                                     threshold, minScale, maxScale, &error);
        const bool good = result.value(name == QLatin1String("desktop_wait_image")
            ? QStringLiteral("conditionMet") : QStringLiteral("pass")).toBool();
        if (ok) *ok = good;
        const QString json = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(result)).toJson(QJsonDocument::Compact));
        return QStringLiteral("[%1: %2]\nresult=%3")
            .arg(name, good ? QStringLiteral("ok") : error, json);
    }
    if (name == QLatin1String("desktop_observe")) {
        const QString kind = args.value(QStringLiteral("scope_kind")).toString(
            QStringLiteral("screen"));
        const QString target = args.value(QStringLiteral("target_id")).toString();
        QString snapshotError;
        const QVariantMap snap = DesktopAutomationBackend::snapshot(
            kind, target, QString(), 120, false, &snapshotError);
        if (snap.isEmpty()) return QStringLiteral("[desktop_observe: %1]").arg(snapshotError);
        const QString dir = AutomationArtifactStore::rootDir()
                            + QStringLiteral("/runtime-observations");
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/observe-%1.jpg")
            .arg(QDateTime::currentMSecsSinceEpoch());
        QString error;
        const QString saved = DesktopAutomationBackend::saveCapture(kind, target, path, &error);
        if (saved.isEmpty()) return QStringLiteral("[desktop_observe: %1]").arg(error);
        const QImage image(saved);
        // Path de la captura para que el loop la inyecte como imagen al contexto
        // (el modelo VE la observación que pidió, no sólo su ruta en texto).
        out[QStringLiteral("imagePath")] = saved;
        out[QStringLiteral("desktopSnapshotId")] = snap.value(QStringLiteral("snapshotId"));
        out[QStringLiteral("desktopSnapshot")] = snap;
        out[QStringLiteral("desktopTarget")] = snap.value(QStringLiteral("target"));
        if (ok) *ok = true;
        return QStringLiteral(
            "[desktop_observe]\nsnapshot_id=%4\nimage_path=%1\nwidth=%2\nheight=%3\n"
            "La captura es la observación actual; comparala con la evidencia Teach antes de actuar.")
            .arg(saved).arg(image.width()).arg(image.height())
            .arg(snap.value(QStringLiteral("snapshotId")).toString());
    }
    if (name == QLatin1String("desktop_click")) {
        QString error;
        const QString kind = args.value(QStringLiteral("scope_kind")).toString(QStringLiteral("screen"));
        const QString target = args.value(QStringLiteral("target_id")).toString();
        if (!validateDesktopSnapshot(kind, target, &error))
            return QStringLiteral("[desktop_click: %1]").arg(error);
        QVariantMap trace;
        const bool good = DesktopAutomationBackend::click(
            kind, target,
            args.value(QStringLiteral("x")).toDouble(),
            args.value(QStringLiteral("y")).toDouble(),
            args.value(QStringLiteral("button")).toString(QStringLiteral("left")),
            &error, &trace);
        out[QStringLiteral("desktopStrategy")] = QStringLiteral("foreground-pointer");
        out[QStringLiteral("desktopTarget")] = trace.value(QStringLiteral("target")).toMap();
        if (ok) *ok = good;
        if (!good) return QStringLiteral("[desktop_click: %1]").arg(error);
        const QString json = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(trace)).toJson(QJsonDocument::Compact));
        return QStringLiteral("[desktop_click: ok]\ntrace=%1").arg(json);
    }
    if (name == QLatin1String("desktop_stroke")) {
        QString error;
        const QString kind = args.value(QStringLiteral("scope_kind")).toString(QStringLiteral("screen"));
        const QString target = args.value(QStringLiteral("target_id")).toString();
        if (!validateDesktopSnapshot(kind, target, &error))
            return QStringLiteral("[desktop_stroke: %1]").arg(error);
        QVariantMap trace;
        QVariantList points;
        for (const QJsonValue &v : args.value(QStringLiteral("points")).toArray()) {
            const QJsonObject o = v.toObject();
            points << QVariantMap{{QStringLiteral("x"), o.value(QStringLiteral("x")).toDouble()},
                                  {QStringLiteral("y"), o.value(QStringLiteral("y")).toDouble()}};
        }
        const bool good = DesktopAutomationBackend::stroke(
            kind, target,
            points,
            args.value(QStringLiteral("button")).toString(QStringLiteral("left")),
            args.value(QStringLiteral("hold_ms")).toInt(8),
            &error, &trace);
        out[QStringLiteral("desktopStrategy")] = QStringLiteral("foreground-stroke");
        out[QStringLiteral("desktopTarget")] = trace.value(QStringLiteral("target")).toMap();
        if (ok) *ok = good;
        if (!good) return QStringLiteral("[desktop_stroke: %1]").arg(error);
        const QString json = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(trace)).toJson(QJsonDocument::Compact));
        return QStringLiteral("[desktop_stroke: ok]\ntrace=%1").arg(json);
    }
    if (name == QLatin1String("desktop_type")) {
        QString error;
        const bool good = DesktopAutomationBackend::typeText(
            args.value(QStringLiteral("text")).toString(), &error);
        if (ok) *ok = good;
        return good ? QStringLiteral("[desktop_type: ok]")
                    : QStringLiteral("[desktop_type: %1]").arg(error);
    }
    if (name == QLatin1String("desktop_key")) {
        QString error;
        QStringList modifiers;
        for (const QJsonValue &v : args.value(QStringLiteral("modifiers")).toArray())
            modifiers << v.toString();
        const bool good = DesktopAutomationBackend::pressKey(
            args.value(QStringLiteral("key")).toString(), modifiers, &error);
        if (ok) *ok = good;
        return good ? QStringLiteral("[desktop_key: ok]")
                    : QStringLiteral("[desktop_key: %1]").arg(error);
    }
    if (name == QLatin1String("desktop_scroll")) {
        QString error;
        const bool good = DesktopAutomationBackend::scroll(
            args.value(QStringLiteral("delta")).toInt(-120), &error);
        if (ok) *ok = good;
        return good ? QStringLiteral("[desktop_scroll: ok]")
                    : QStringLiteral("[desktop_scroll: %1]").arg(error);
    }
    if (name == QLatin1String("desktop_wait_for")) {
        QString error;
        const QVariantMap res = DesktopAutomationBackend::waitFor(
            args.value(QStringLiteral("target_id")).toString(),
            args.value(QStringLiteral("window_title")).toString(),
            args.value(QStringLiteral("query")).toString(),
            args.value(QStringLiteral("role")).toString(),
            args.value(QStringLiteral("timeout_ms")).toInt(8000),
            &error);
        const bool found = res.value(QStringLiteral("found")).toBool();
        if (ok) *ok = found;
        const QString json = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(res)).toJson(QJsonDocument::Compact));
        if (!found) return QStringLiteral("[desktop_wait_for: %1]\n%2")
                              .arg(error.isEmpty() ? QStringLiteral("no encontrado") : error, json);
        return QStringLiteral("[desktop_wait_for: ok]\n%1").arg(json);
    }
    if (name == QLatin1String("desktop_assert")) {
        QString error;
        const QVariantMap res = DesktopAutomationBackend::assertCondition(
            args.value(QStringLiteral("target_id")).toString(),
            args.value(QStringLiteral("window_title")).toString(),
            args.value(QStringLiteral("query")).toString(),
            args.value(QStringLiteral("role")).toString(),
            args.value(QStringLiteral("expect_text")).toString(),
            args.value(QStringLiteral("timeout_ms")).toInt(4000),
            &error);
        const bool pass = res.value(QStringLiteral("pass")).toBool();
        if (ok) *ok = pass;
        const QString json = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(res)).toJson(QJsonDocument::Compact));
        return pass ? QStringLiteral("[desktop_assert: PASS]\n%1").arg(json)
                    : QStringLiteral("[desktop_assert: FAIL] %1\n%2")
                          .arg(error.isEmpty() ? QStringLiteral("condición no cumplida") : error, json);
    }
    if (name == QLatin1String("desktop_launch")) {
        QString error;
        const bool good = DesktopAutomationBackend::launchApp(
            args.value(QStringLiteral("app")).toString(),
            args.value(QStringLiteral("args")).toString(), &error);
        if (ok) *ok = good;
        return good ? QStringLiteral("[desktop_launch: ok — la app se está abriendo. "
                                     "desktop_wait ~800ms y UNA desktop_windows para el id. "
                                     "Después, si la app se maneja con teclado (calc, notepad), "
                                     "desktop_focus <id> + desktop_type; NO repitas desktop_windows "
                                     "ni observes en loop.]")
                    : QStringLiteral("[desktop_launch: %1]").arg(error);
    }
    if (name == QLatin1String("desktop_focus")) {
        QString error;
        const bool good = DesktopAutomationBackend::focusWindow(
            args.value(QStringLiteral("target_id")).toString(), &error);
        if (ok) *ok = good;
        return good ? QStringLiteral("[desktop_focus: ok]")
                    : QStringLiteral("[desktop_focus: %1]").arg(error);
    }
    if (name == QLatin1String("desktop_resize")) {
        QString error;
        const bool good = DesktopAutomationBackend::setWindowSize(
            args.value(QStringLiteral("target_id")).toString(),
            args.value(QStringLiteral("width")).toInt(),
            args.value(QStringLiteral("height")).toInt(), &error);
        if (ok) *ok = good;
        return good ? QStringLiteral("[desktop_resize: ok]")
                    : QStringLiteral("[desktop_resize: %1]").arg(error);
    }
    if (name == QLatin1String("desktop_wait")) {
        const int ms = qBound(50, args.value(QStringLiteral("ms")).toInt(500), 10000);
        QThread::msleep(static_cast<unsigned long>(ms));
        if (ok) *ok = true;
        return QStringLiteral("[desktop_wait: %1 ms]").arg(ms);
    }
    const QDir base(cwd);
    auto canonicalPolicyPath = [](const QString &raw) {
        QFileInfo info(raw);
        if (info.exists() && !info.canonicalFilePath().isEmpty())
            return QDir::cleanPath(info.canonicalFilePath());
        QStringList tail{info.fileName()};
        QDir parent = info.absoluteDir();
        while (!parent.exists() && !parent.isRoot()) {
            tail.prepend(parent.dirName());
            parent.cdUp();
        }
        const QFileInfo parentInfo(parent.absolutePath());
        QString resolved = parentInfo.canonicalFilePath();
        if (resolved.isEmpty()) resolved = parentInfo.absoluteFilePath();
        for (const QString &part : std::as_const(tail))
            resolved = QDir(resolved).filePath(part);
        return QDir::cleanPath(resolved);
    };
    auto resolve = [&](const QString &rel) {
        return canonicalPolicyPath(base.absoluteFilePath(rel));
    };
    // Los modelos suelen envolver argumentos XML/JSON con espacios o saltos de
    // línea. Para rutas relativas esos bytes no forman parte del nombre pedido y
    // hacían que write_file creara/fallara contra un destino distinto al que el
    // agente pretendía (por ejemplo "\nsolution.py\n").
    auto normalizeToolPath = [](QString path) {
        path = path.trimmed();
        path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        return QDir::cleanPath(path);
    };
    // En modo "Super Agente" (no confinado) se permite cualquier ruta del disco.
    auto underRoot = [&](const QString &abs, const QString &root) {
#ifdef Q_OS_WIN
        constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
        constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
        return abs.compare(root, cs) == 0
            || abs.startsWith(root + QStringLiteral("/"), cs)
            || abs.startsWith(root + QStringLiteral("\\"), cs);
    };
    auto inProject = [&](const QString &abs) {
        if (!m_confined) return true;
        if (underRoot(abs, canonicalPolicyPath(base.absolutePath()))) return true;
        // Carpetas extra autorizadas por la Task (scope "folder").
        for (const QString &root : m_allowedRoots)
            if (underRoot(abs, root)) return true;
        return false;
    };
    // Mensaje accionable: sin la raiz permitida el modelo reintenta ".." en bucle.
    auto outsideMsg = [&](const QString &abs) {
        return QStringLiteral("[ruta fuera del proyecto: %1 · raíz permitida: %2 · "
                              "usá rutas relativas dentro del proyecto (\".\" es la raíz); "
                              "no reintentes con \"..\" ni rutas absolutas de afuera]")
            .arg(abs, canonicalPolicyPath(base.absolutePath()));
    };

    if (name == QLatin1String("read_file")) {
        const QString abs = resolve(normalizeToolPath(args.value(QStringLiteral("path")).toString()));
        if (!inProject(abs)) return outsideMsg(abs);
        QFile f(abs);
        if (!f.open(QIODevice::ReadOnly)) return QStringLiteral("[no se pudo abrir: %1]").arg(abs);
        const QByteArray raw = f.read(4 * 1024 * 1024);
        if (ok) *ok = true;

        const int offset = args.value(QStringLiteral("offset")).toInt(0);
        const int limit  = args.value(QStringLiteral("limit")).toInt(0);
        if (offset > 0 || limit > 0) {
            // Lectura parcial por líneas. NO genera huella de dedup (sería por
            // archivo, no por rango → falsos positivos).
            const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
            const int start = qBound(0, offset > 0 ? offset - 1 : 0, lines.size());
            const int count = (limit > 0) ? limit : (lines.size() - start);
            const QStringList slice = lines.mid(start, count);
            return QStringLiteral("[líneas %1-%2 de %3]\n%4")
                       .arg(start + 1).arg(start + slice.size()).arg(lines.size())
                       .arg(slice.join(QLatin1Char('\n')));
        }
        // Archivo completo → huella para read-dedup.
        out[QStringLiteral("readRel")] = base.relativeFilePath(abs);
        out[QStringLiteral("readFp")]  = QString::fromLatin1(
            QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex());
        if (args.value(QStringLiteral("compact")).toBool()) {
            const auto view = StructuredSourceView::build(QString::fromUtf8(raw), abs, true);
            if (view.safe) {
                out[QStringLiteral("structuredSource")] = true;
                out[QStringLiteral("structuredParser")] = view.parserBackend;
                out[QStringLiteral("parserValidated")] = view.parserValidated;
                out[QStringLiteral("originalBytes")] = view.originalBytes;
                out[QStringLiteral("compactBytes")] = view.compact.toUtf8().size();
                out[QStringLiteral("reductionPct")] = view.reductionPct();
                return QStringLiteral("[vista compacta segura · %1% menos · sólo lectura; "
                                      "para editar releé el rango exacto sin compact]\n%2")
                    .arg(QString::number(view.reductionPct(), 'f', 1), view.compact);
            }
            out[QStringLiteral("structuredSourceFallback")] = view.error;
        }
        return QString::fromUtf8(raw);
    }
    if (name == QLatin1String("project_brain")) {
        QStringList changed;
        for (const QJsonValue &value : args.value(QStringLiteral("changed_paths")).toArray())
            changed.append(value.toString());
        changed.append(m_projectBrainDirtyPaths.values());
        const int maxFiles = qBound(100, args.value(QStringLiteral("max_files")).toInt(4000), 20000);
        const QVariantMap brain = changed.isEmpty() ? ProjectBrain::refresh(cwd, maxFiles)
                                                    : ProjectBrain::update(cwd, changed, maxFiles);
        m_projectBrainDirtyPaths.clear();
        if (ok) *ok = !brain.contains(QStringLiteral("error"));
        return QString::fromUtf8(QJsonDocument::fromVariant(brain).toJson(QJsonDocument::Compact));
    }
    if (name == QLatin1String("context_status")) {
        const QVariantMap state = ContextIndex::status(cwd);
        for (auto it = state.cbegin(); it != state.cend(); ++it) out[it.key()] = it.value();
        if (ok) *ok = state.value(QStringLiteral("ok")).toBool();
        return QString::fromUtf8(QJsonDocument::fromVariant(state).toJson(QJsonDocument::Compact));
    }
    if (name == QLatin1String("work_status")) {
        const int maxClaims = qBound(1, args.value(QStringLiteral("max_claims")).toInt(8), 50);
        const QString status = WorkRegistry::formatActive(cwd, m_sessionId, maxClaims);
        if (ok) *ok = true;
        return status;
    }
    if (name == QLatin1String("context_scout")) {
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        const int budget = qBound(64, args.value(QStringLiteral("token_budget")).toInt(700), 16000);
        const int k = qBound(1, args.value(QStringLiteral("k")).toInt(8), 15);
        const QString relPath = normalizeToolPath(args.value(QStringLiteral("path")).toString());
        const QString scope = relPath.isEmpty() ? cwd : resolve(relPath);
        if (!inProject(scope)) return outsideMsg(scope);
        const QVariantMap scout = ContextIndex::scout(cwd, query, budget, k,
                                                       args.value(QStringLiteral("expand_graph")).toBool(true),
                                                       relPath);
        out[QStringLiteral("receipt")] = scout.value(QStringLiteral("receipt"));
        out[QStringLiteral("neighbors")] = scout.value(QStringLiteral("neighbors"));
        out[QStringLiteral("index")] = scout.value(QStringLiteral("index"));
        if (ok) *ok = scout.value(QStringLiteral("ok")).toBool();
        return ContextIndex::formatScout(scout);
    }
    if (name == QLatin1String("context_fetch")) {
        QVariantMap meta;
        const QString text = ContextIndex::fetch(cwd,
            args.value(QStringLiteral("handle")).toString(), &meta);
        const bool success = !text.startsWith(QStringLiteral("[context_fetch:"));
        for (auto it = meta.cbegin(); it != meta.cend(); ++it) out[it.key()] = it.value();
        if (ok) *ok = success;
        return success ? QStringLiteral("[context_fetch %1:%2-%3]\n%4")
            .arg(meta.value(QStringLiteral("path")).toString())
            .arg(meta.value(QStringLiteral("startLine")).toInt())
            .arg(meta.value(QStringLiteral("endLine")).toInt())
            .arg(text) : text;
    }
    if (name == QLatin1String("list_dir")) {
        const QString abs = resolve(normalizeToolPath(args.value(QStringLiteral("path")).toString()));
        if (!inProject(abs)) return outsideMsg(abs);
        QDir d(abs);
        if (!d.exists()) return QStringLiteral("[no existe: %1]").arg(abs);
        const bool recursive = args.value(QStringLiteral("recursive")).toBool();
        if (ok) *ok = true;
        if (!recursive) {
            QStringList outList;
            const auto entries = d.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name);
            for (const QFileInfo &fi : entries)
                outList << (fi.isDir() ? fi.fileName() + QStringLiteral("/") : fi.fileName());
            return outList.join(QLatin1Char('\n'));
        }
        // Recursivo: rutas relativas al cwd, saltando dirs ignorados, cap 1000.
        QStringList outList;
        QStringList stack{abs};
        while (!stack.isEmpty() && outList.size() < 1000) {
            QDir dd(stack.takeLast());
            const auto entries = dd.entryInfoList(
                QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs, QDir::Name);
            for (const QFileInfo &fi : entries) {
                const QString rel = base.relativeFilePath(fi.absoluteFilePath());
                if (fi.isDir()) {
                    outList << rel + QStringLiteral("/");
                    if (!isIgnoredDir(fi.fileName())) stack << fi.absoluteFilePath();
                } else {
                    outList << rel;
                }
                if (outList.size() >= 1000) break;
            }
        }
        outList.sort();
        return outList.join(QLatin1Char('\n'));
    }
    if (name == QLatin1String("web_fetch")) {
        const QString url = args.value(QStringLiteral("url")).toString();
        QString validationError;
        if (!isSafePublicWebUrl(url, &validationError))
            return QStringLiteral("[web_fetch: URL bloqueada: %1]").arg(validationError);
        const QString rateHost = QUrl(url).host().toLower();
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        QString rateError;
        if (!consumeWebRateLimit(rateHost, nowMs, &rateError))
            return QStringLiteral("[web_fetch: %1]").arg(rateError);
        const QString requested = args.value(QStringLiteral("provider"))
                                      .toString(QStringLiteral("auto")).trimmed().toLower();
        if (!QStringList{QStringLiteral("auto"), QStringLiteral("direct"),
                         QStringLiteral("playwright"), QStringLiteral("camofox")}
                 .contains(requested))
            return QStringLiteral("[web_fetch: provider inválido; usá auto, direct, playwright o camofox]");

        QString text, html, directError, finalPublicUrl = url;
        QString provider = requested;
        QStringList reasons;
        QStringList attempts;
        CallbackWebFetchProvider directProvider(QStringLiteral("direct"),
            [&](const QString &target) {
                WebFetchResult result;
                result.provider = QStringLiteral("direct");
                result.text = fetchUrlText(target, 48 * 1024, &result.error, &html,
                                           &finalPublicUrl);
                return result;
            });
        bool camofoxAvailable = false;
        for (const QVariant &entry : std::as_const(m_webProviders)) {
            const QVariantMap candidate = entry.toMap();
            if (candidate.value(QStringLiteral("enabled"), true).toBool()
                && candidate.value(QStringLiteral("provider")).toString().toLower()
                       == QLatin1String("camofox")) {
                camofoxAvailable = true;
                break;
            }
        }
        if (requested == QLatin1String("camofox") && !camofoxAvailable)
            return QStringLiteral(
                "[web_fetch camofox: Camofox no está configurado o está desactivado]");
        // Incluso al forzar navegador se resuelve primero toda la cadena HTTP con
        // la guarda nativa. Así el browser recibe el destino público final, no una
        // URL capaz de redirigir por HTTP a localhost/metadata.
        if (requested == QLatin1String("playwright") || requested == QLatin1String("camofox")) {
            QString preflightError, ignoredHtml;
            (void)fetchUrlText(url, 1, &preflightError, &ignoredHtml, &finalPublicUrl);
            if (!preflightError.isEmpty())
                return QStringLiteral("[web_fetch preflight: %1]").arg(preflightError);
        }
        CallbackWebFetchProvider playwrightProvider(QStringLiteral("playwright"),
            [&](const QString &target) {
                WebFetchResult result;
                result.provider = QStringLiteral("playwright");
                result.text = fetchViaPlaywright(target, &result.error);
                return result;
            });
        CallbackWebFetchProvider camofoxProvider(QStringLiteral("camofox"),
            [&](const QString &target) {
                WebFetchResult result;
                result.provider = QStringLiteral("camofox");
                result.text = fetchViaCamofox(target, &result.error);
                return result;
            });
        if (requested == QLatin1String("auto") || requested == QLatin1String("direct")) {
            const WebFetchResult fetched = directProvider.fetch(url);
            text = fetched.text;
            directError = fetched.error;
            reasons = webEscalationReasons(html, text, directError);
            attempts << QStringLiteral("direct");
            provider = QStringLiteral("direct");
            if (requested == QLatin1String("direct") && text.isEmpty())
                return QStringLiteral("[web_fetch direct: %1]")
                    .arg(directError.isEmpty() ? QStringLiteral("respuesta vacía") : directError);
        }
        const bool mustEscalate = requested != QLatin1String("direct")
            && (requested != QLatin1String("auto") || !reasons.isEmpty());
        if (mustEscalate
            && (requested == QLatin1String("auto") || requested == QLatin1String("playwright"))) {
            const WebFetchResult fetched = playwrightProvider.fetch(finalPublicUrl);
            attempts << QStringLiteral("playwright");
            if (!fetched.text.isEmpty()) {
                text = fetched.text;
                provider = QStringLiteral("playwright");
                reasons.clear();
            } else if (requested == QLatin1String("playwright")) {
                return QStringLiteral("[web_fetch playwright: %1]").arg(fetched.error);
            }
        }
        if (mustEscalate && provider != QLatin1String("playwright")
            && (requested == QLatin1String("auto") || requested == QLatin1String("camofox"))) {
            const WebFetchResult fetched = camofoxProvider.fetch(finalPublicUrl);
            attempts << QStringLiteral("camofox");
            if (!fetched.text.isEmpty()) {
                text = fetched.text;
                provider = QStringLiteral("camofox");
                reasons.clear();
            } else if (requested == QLatin1String("camofox")) {
                return QStringLiteral("[web_fetch camofox: %1]").arg(fetched.error);
            }
        }
        if (text.isEmpty()) {
            return QStringLiteral("[web_fetch: sin contenido; intentos=%1; evidencia=%2]")
                .arg(attempts.join(QLatin1Char(',')),
                     reasons.isEmpty() ? QStringLiteral("none") : reasons.join(QLatin1Char(',')));
        }
        if (ok) *ok = true;
        const QString evidence = reasons.isEmpty() ? QStringLiteral("none")
                                                    : reasons.join(QLatin1Char(','));
        return QStringLiteral("[web_fetch provider=%1 attempts=%2 evidence=%3]\n%4")
            .arg(provider, attempts.join(QLatin1Char(',')), evidence, text);
    }
    if (name == QLatin1String("web_search")) {
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) return QStringLiteral("[query vacía]");
        int count = args.value(QStringLiteral("count")).toInt();
        if (count <= 0) count = 5;
        count = qBound(1, count, 10);
        QString err;
        const QVector<WebHit> hits = runWebSearch(query, count, &err);
        if (hits.isEmpty())
            return QStringLiteral("[sin resultados para: %1%2]").arg(query,
                       err.isEmpty() ? QString() : QStringLiteral(" — ") + err);
        QStringList out;
        for (int i = 0; i < hits.size(); ++i)
            out << QStringLiteral("%1. %2\n   %3\n   %4")
                       .arg(i + 1).arg(hits[i].title, hits[i].url, hits[i].snippet);
        if (ok) *ok = true;
        return out.join(QStringLiteral("\n\n"));
    }
    if (name == QLatin1String("deep_research")) {
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) return QStringLiteral("[query vacía]");
        int maxPages = args.value(QStringLiteral("max_pages")).toInt();
        if (maxPages <= 0) maxPages = 5;
        maxPages = qBound(1, maxPages, 10);

        // Ángulos: el modelo puede pasar varias sub-consultas; si no, usa la query sola.
        QStringList queries;
        const QJsonArray angles = args.value(QStringLiteral("angles")).toArray();
        for (const QJsonValue &v : angles) {
            const QString a = v.toString().trimmed();
            if (!a.isEmpty()) queries << a;
        }
        if (queries.isEmpty()) queries << query;
        if (queries.size() > 4) queries = queries.mid(0, 4);   // cap

        // 1) Buscar por cada ángulo, juntar URLs únicas (orden de aparición).
        QStringList urls;
        QHash<QString, WebHit> meta;
        QStringList searchLog;
        for (const QString &q : queries) {
            const QVector<WebHit> hits = runWebSearch(q, 4);
            searchLog << QStringLiteral("· \"%1\" → %2 resultados").arg(q).arg(hits.size());
            for (const WebHit &h : hits) {
                if (h.url.isEmpty() || urls.contains(h.url)) continue;
                urls << h.url; meta.insert(h.url, h);
                if (urls.size() >= maxPages) break;
            }
            if (urls.size() >= maxPages) break;
        }
        if (urls.isEmpty())
            return QStringLiteral("[deep_research: sin resultados de búsqueda para: %1]").arg(query);

        // 2) Descargar y limpiar cada página (excerpt acotado por fuente).
        const int perPageCap = 3500;
        QStringList sources, bodies;
        for (int i = 0; i < urls.size(); ++i) {
            const WebHit &h = meta.value(urls[i]);
            sources << QStringLiteral("[%1] %2 — %3").arg(i + 1).arg(h.title, urls[i]);
            const QString text = fetchUrlText(urls[i], perPageCap);
            bodies << QStringLiteral("### [%1] %2\n%3")
                          .arg(i + 1).arg(h.title,
                               text.isEmpty() ? QStringLiteral("(no se pudo descargar)") : text);
        }

        // 3) Devolver dossier crudo; el MODELO sintetiza (es el LLM del loop).
        QString out;
        out += QStringLiteral("# Dossier de investigación: %1\n\n").arg(query);
        out += QStringLiteral("Búsquedas:\n%1\n\n").arg(searchLog.join(QLatin1Char('\n')));
        out += QStringLiteral("## Fuentes\n%1\n\n").arg(sources.join(QLatin1Char('\n')));
        out += QStringLiteral("## Contenido\n%1\n\n").arg(bodies.join(QStringLiteral("\n\n")));
        out += QStringLiteral("---\nSintetizá una respuesta a \"%1\" citando las fuentes por su número [n]. "
                              "Si algo no está cubierto, decilo.").arg(query);
        if (ok) *ok = true;
        return out.left(28 * 1024);
    }
    if (name == QLatin1String("search_docs")) {
        // RAG-lite: ranking por relevancia de keywords sobre chunks (sin embeddings).
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) return QStringLiteral("[query vacía]");
        int k = args.value(QStringLiteral("k")).toInt();
        if (k <= 0) k = 5;
        k = qBound(1, k, 15);
        const QString sub = normalizeToolPath(args.value(QStringLiteral("path")).toString());
        const QString rootAbs = resolve(sub);
        if (!inProject(rootAbs)) return outsideMsg(rootAbs);

        // Tokens de la consulta (lowercase, >=2 chars, únicos).
        QStringList terms;
        for (const QString &t : query.toLower().split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")),
                                                       Qt::SkipEmptyParts))
            if (t.size() >= 2 && !terms.contains(t)) terms << t;
        if (terms.isEmpty()) return QStringLiteral("[query sin términos útiles]");

        struct Chunk { QString file; int line; double score; QString text; };
        QVector<Chunk> top;   // mantenido chico (k)
        auto consider = [&](const Chunk &c) {
            if (c.score <= 0) return;
            if (top.size() < k) { top.append(c); }
            else {
                int wi = 0;
                for (int i = 1; i < top.size(); ++i) if (top[i].score < top[wi].score) wi = i;
                if (c.score > top[wi].score) top[wi] = c;
            }
        };

        QStringList files;
        collectFiles(rootAbs, files, 8000);
        const int chunkLines = 40;
        for (const QString &fp : files) {
            // Sólo archivos de texto de tamaño razonable.
            QFileInfo fi(fp);
            if (fi.size() > 1024 * 1024) continue;
            QFile f(fp);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray raw = f.read(1024 * 1024);
            if (raw.contains('\0')) continue;   // binario
            const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
            const QString rel = base.relativeFilePath(fp);
            for (int start = 0; start < lines.size(); start += chunkLines) {
                const QStringList slice = lines.mid(start, chunkLines);
                const QString chunk = slice.join(QLatin1Char('\n'));
                const QString low = chunk.toLower();
                if (low.isEmpty()) continue;
                // Score BM25-ish: por término, tf con saturación; bonus por cobertura.
                double score = 0; int distinct = 0;
                for (const QString &t : terms) {
                    int tf = low.count(t);
                    if (tf > 0) { distinct++; score += tf / (tf + 1.5); }
                }
                if (distinct == 0) continue;
                score *= (1.0 + 0.5 * (distinct - 1));            // recompensa multi-término
                score /= (1.0 + chunk.size() / 4000.0);           // normaliza por largo
                consider({rel, start + 1, score, chunk.trimmed().left(600)});
            }
        }
        if (top.isEmpty()) return QStringLiteral("[sin coincidencias para: %1]").arg(query);
        std::sort(top.begin(), top.end(), [](const Chunk &a, const Chunk &b) { return a.score > b.score; });
        QStringList out;
        for (const Chunk &c : top)
            out << QStringLiteral("%1:%2  (score %3)\n%4")
                       .arg(c.file).arg(c.line).arg(c.score, 0, 'f', 2).arg(c.text);
        if (ok) *ok = true;
        return out.join(QStringLiteral("\n\n──────\n"));
    }
    if (name == QLatin1String("semantic_search")) {
        // RAG semántico real: embeddings vía /v1/embeddings + cache de vectores SQLite.
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) return QStringLiteral("[query vacía]");
        const QString auxiliaryBase = auxiliaryEndpoint();
        const QString auxiliaryKey = auxiliaryBearer();
        const QString embeddingModel = auxiliaryEmbeddingModel();
        if (auxiliaryBase.isEmpty())
            return QStringLiteral("[semantic_search: no hay server activo]");
        int k = args.value(QStringLiteral("k")).toInt();
        if (k <= 0) k = 5;
        k = qBound(1, k, 15);
        const QString rootAbs = resolve(normalizeToolPath(args.value(QStringLiteral("path")).toString()));
        if (!inProject(rootAbs)) return outsideMsg(rootAbs);

        struct Ch { QString rel; int line; QString key; QString text; QVector<float> vec; };
        QVector<Ch> chunks;
        const int chunkLines = 40, maxChunks = 800;
        QStringList files;
        collectFiles(rootAbs, files, 8000);
        bool truncated = false;
        for (const QString &fp : files) {
            if (chunks.size() >= maxChunks) { truncated = true; break; }
            QFileInfo fi(fp);
            if (fi.size() > 1024 * 1024) continue;
            QFile f(fp);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray raw = f.read(1024 * 1024);
            if (raw.contains('\0')) continue;
            const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
            const QString rel = base.relativeFilePath(fp);
            for (int start = 0; start < lines.size() && chunks.size() < maxChunks; start += chunkLines) {
                const QString text = lines.mid(start, chunkLines).join(QLatin1Char('\n')).trimmed();
                if (text.size() < 16) continue;   // descartar fragmentos triviales
                const QString key = embeddingCacheKeyForTest(auxiliaryBase, embeddingModel, text);
                chunks.append({rel, start + 1, key, text, {}});
            }
        }
        if (chunks.isEmpty()) return QStringLiteral("[no hay archivos de texto para indexar]");

        // 1) Cargar vectores cacheados; juntar los faltantes.
        QSqlDatabase db = embedCacheDb();
        QHash<QString, QVector<float>> cache;
        if (db.isOpen()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT vec FROM vecs WHERE key=?"));
            for (const Ch &c : chunks) {
                if (cache.contains(c.key)) continue;
                q.addBindValue(c.key);
                if (q.exec() && q.next()) cache.insert(c.key, blobToVec(q.value(0).toByteArray()));
                q.finish();
            }
        }
        QStringList missKeys, missTexts;
        QSet<QString> seen;
        for (const Ch &c : chunks) {
            if (cache.contains(c.key) || seen.contains(c.key)) continue;
            seen.insert(c.key); missKeys << c.key; missTexts << c.text;
        }

        // 2) Embeber faltantes en batches; guardar en cache.
        int embedded = 0;
        for (int i = 0; i < missTexts.size(); i += 64) {
            const QStringList batch = missTexts.mid(i, 64);
            const QStringList bkeys = missKeys.mid(i, 64);
            QString err;
            const QVector<QVector<float>> vecs = embedTexts(auxiliaryBase, batch, &err,
                                                            embeddingModel, auxiliaryKey);
            if (vecs.isEmpty())
                return QStringLiteral("[semantic_search: el server no devolvió embeddings. "
                                      "Levantá un server con --embeddings (o un modelo de embeddings). "
                                      "Detalle: %1]").arg(err);
            if (db.isOpen()) db.transaction();
            for (int j = 0; j < vecs.size() && j < bkeys.size(); ++j) {
                cache.insert(bkeys[j], vecs[j]);
                if (db.isOpen()) {
                    QSqlQuery iq(db);
                    iq.prepare(QStringLiteral("INSERT OR REPLACE INTO vecs(key,dim,vec) VALUES(?,?,?)"));
                    iq.addBindValue(bkeys[j]);
                    iq.addBindValue(vecs[j].size());
                    iq.addBindValue(vecToBlob(vecs[j]));
                    iq.exec();
                }
                ++embedded;
            }
            if (db.isOpen()) db.commit();
        }

        // 3) Embeber la query y rankear por coseno.
        QString qerr;
        const QVector<QVector<float>> qv = embedTexts(auxiliaryBase, {query}, &qerr,
                                                      embeddingModel, auxiliaryKey);
        if (qv.isEmpty() || qv[0].isEmpty())
            return QStringLiteral("[semantic_search: no se pudo embeber la query: %1]").arg(qerr);
        const QVector<float> &qvec = qv[0];

        QVector<QPair<float, int>> scored;
        for (int i = 0; i < chunks.size(); ++i) {
            const QVector<float> v = cache.value(chunks[i].key);
            if (v.isEmpty()) continue;
            scored.append({cosineSim(qvec, v), i});
        }
        std::sort(scored.begin(), scored.end(), [](auto &a, auto &b) { return a.first > b.first; });

        QStringList out;
        for (int i = 0; i < scored.size() && out.size() < k; ++i) {
            const Ch &c = chunks[scored[i].second];
            out << QStringLiteral("%1:%2  (sim %3)\n%4")
                       .arg(c.rel).arg(c.line).arg(scored[i].first, 0, 'f', 3)
                       .arg(c.text.left(600));
        }
        if (out.isEmpty()) return QStringLiteral("[sin resultados semánticos]");
        if (ok) *ok = true;
        QString header = QStringLiteral("[%1 chunks · %2 embebidos nuevos%3]\n\n")
                             .arg(chunks.size()).arg(embedded)
                             .arg(truncated ? QStringLiteral(" · TRUNCADO a 800") : QString());
        return header + out.join(QStringLiteral("\n\n──────\n"));
    }
    if (name == QLatin1String("hybrid_search") || name == QLatin1String("repo_slice")) {
        // RAG HÍBRIDO: fusiona BM25 (keywords) + vectorial (embeddings) por
        // Reciprocal Rank Fusion, y RE-RANKEA el top con /rerank si el server lo
        // soporta. Es la mejor recuperación disponible: combiná esto antes de
        // razonar sobre el repo. Cae a fusión sin reranker si no hay endpoint.
        const bool repoSlice = name == QLatin1String("repo_slice");
        const QString query = args.value(QStringLiteral("query")).toString().trimmed();
        if (query.isEmpty()) return QStringLiteral("[query vacía]");
        const QString auxiliaryBase = auxiliaryEndpoint();
        const QString auxiliaryKey = auxiliaryBearer();
        const QString embeddingModel = auxiliaryEmbeddingModel();
        const QString rerankModel = auxiliaryRerankModel();
        if (args.value(QStringLiteral("mode")).toString().trimmed().toLower()
                == QLatin1String("scout")) {
            const int budget = qBound(64, args.value(QStringLiteral("token_budget")).toInt(700), 16000);
            const int scoutK = qBound(1, args.value(QStringLiteral("k")).toInt(8), 15);
            const QString relPath = normalizeToolPath(args.value(QStringLiteral("path")).toString());
            const QString scope = relPath.isEmpty() ? cwd : resolve(relPath);
            if (!inProject(scope)) return outsideMsg(scope);
            const QVariantMap scout = ContextIndex::scout(cwd, query, budget, scoutK,
                                                           args.value(QStringLiteral("expand_graph")).toBool(true),
                                                           relPath);
            out[QStringLiteral("receipt")] = scout.value(QStringLiteral("receipt"));
            out[QStringLiteral("neighbors")] = scout.value(QStringLiteral("neighbors"));
            out[QStringLiteral("index")] = scout.value(QStringLiteral("index"));
            if (ok) *ok = scout.value(QStringLiteral("ok")).toBool();
            return ContextIndex::formatScout(scout);
        }
        int k = args.value(QStringLiteral("k")).toInt();
        if (k <= 0) k = 6;
        k = qBound(1, k, 15);
        const QString rootAbs = resolve(normalizeToolPath(args.value(QStringLiteral("path")).toString()));
        if (!inProject(rootAbs)) return outsideMsg(rootAbs);

        // Términos de la query para BM25.
        QStringList terms;
        for (const QString &t : query.toLower().split(
                 QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")), Qt::SkipEmptyParts))
            if (t.size() >= 2 && !terms.contains(t)) terms << t;

        struct Ch { QString rel; int line; int endLine; QString key; QString text; double bm25; };
        QVector<Ch> chunks;
        const int chunkLines = 40, maxChunks = 800;
        QStringList files;
        collectFiles(rootAbs, files, 8000);
        bool truncated = false;

        // Trocea un cuerpo de texto en chunks de chunkLines y les calcula BM25.
        // Común al path de archivos de texto y al de documentos extraídos: para el
        // ranking un PDF convertido a markdown es texto como cualquier otro.
        auto addChunks = [&](const QString &rel, const QStringList &lines) {
            for (int start = 0; start < lines.size() && chunks.size() < maxChunks; start += chunkLines) {
                const int segLines = qMin(chunkLines, lines.size() - start);
                const QString text = lines.mid(start, chunkLines).join(QLatin1Char('\n')).trimmed();
                if (text.size() < 16) continue;
                const QString low = text.toLower();
                double bm = 0; int distinct = 0;
                for (const QString &t : terms) {
                    int tf = low.count(t);
                    if (tf > 0) { distinct++; bm += tf / (tf + 1.5); }
                }
                if (distinct > 0) {
                    bm *= (1.0 + 0.5 * (distinct - 1));
                    bm /= (1.0 + text.size() / 4000.0);
                }
                const QString key = embeddingCacheKeyForTest(auxiliaryBase, embeddingModel, text);
                chunks.append({rel, start + 1, start + segLines, key, text, bm});
            }
        };

        // include_docs se lee antes del barrido de texto: con docs prendidos un .html
        // lo indexa el extractor (markdown limpio, sin tags); apagado, se sigue
        // leyendo como texto plano igual que siempre — nada se pierde.
        const bool includeDocs = args.value(QStringLiteral("include_docs")).toBool();
        for (const QString &fp : files) {
            if (chunks.size() >= maxChunks) { truncated = true; break; }
            QFileInfo fi(fp);
            if (fi.size() > 1024 * 1024) continue;
            if (includeDocs && DocumentExtractor::isRich(fp)) continue;   // path de docs
            QFile f(fp);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray raw = f.read(1024 * 1024);
            if (raw.contains('\0')) continue;
            addChunks(base.relativeFilePath(fp),
                      QString::fromUtf8(raw).split(QLatin1Char('\n')));
        }

        // Documentos (pdf/office/epub/html) → misma pipeline BM25+vector+rerank.
        // OPT-IN (include_docs): la extracción cuesta un proceso Python por archivo
        // (markitdown), así que no se paga en cada búsqueda de código. El cache por
        // md5 de DocumentExtractor hace que la segunda corrida sea gratis.
        // Las líneas citadas son del TEXTO EXTRAÍDO, no del PDF: sirven para ubicar
        // el pasaje, no como página.
        int docsIndexed = 0, docsFailed = 0;
        QString docErr;
        if (includeDocs) {
            const int kMaxDocs = 25;
            for (const QString &fp : files) {
                if (docsIndexed + docsFailed >= kMaxDocs) { truncated = true; break; }
                if (chunks.size() >= maxChunks) { truncated = true; break; }
                if (!DocumentExtractor::isRich(fp)) continue;
                QString err;
                const QString text = DocumentExtractor::extract(fp, &err);
                if (text.trimmed().isEmpty()) {
                    ++docsFailed;
                    if (docErr.isEmpty() && !err.isEmpty()) docErr = err;
                    continue;
                }
                ++docsIndexed;
                addChunks(base.relativeFilePath(fp), text.split(QLatin1Char('\n')));
            }
        }

        if (chunks.isEmpty()) return QStringLiteral("[no hay archivos de texto para indexar]");

        // Ranking BM25.
        QVector<int> byBm25(chunks.size());
        for (int i = 0; i < chunks.size(); ++i) byBm25[i] = i;
        std::sort(byBm25.begin(), byBm25.end(),
                  [&](int a, int b) { return chunks[a].bm25 > chunks[b].bm25; });

        // Ranking vectorial (si hay server con embeddings). Reusa el cache SQLite.
        QVector<int> byVec;
        QString vecErr;
        if (!auxiliaryBase.isEmpty()) {
            QSqlDatabase db = embedCacheDb();
            QHash<QString, QVector<float>> cache;
            if (db.isOpen()) {
                QSqlQuery q(db);
                q.prepare(QStringLiteral("SELECT vec FROM vecs WHERE key=?"));
                for (const Ch &c : chunks) {
                    if (cache.contains(c.key)) continue;
                    q.addBindValue(c.key);
                    if (q.exec() && q.next()) cache.insert(c.key, blobToVec(q.value(0).toByteArray()));
                    q.finish();
                }
            }
            QStringList missKeys, missTexts; QSet<QString> seen;
            for (const Ch &c : chunks) {
                if (cache.contains(c.key) || seen.contains(c.key)) continue;
                seen.insert(c.key); missKeys << c.key; missTexts << c.text;
            }
            bool embedOk = true;
            for (int i = 0; i < missTexts.size() && embedOk; i += 64) {
                const QStringList batch = missTexts.mid(i, 64);
                const QStringList bkeys = missKeys.mid(i, 64);
                const QVector<QVector<float>> vecs = embedTexts(auxiliaryBase, batch, &vecErr,
                                                                embeddingModel, auxiliaryKey);
                if (vecs.isEmpty()) { embedOk = false; break; }
                if (db.isOpen()) db.transaction();
                for (int j = 0; j < vecs.size() && j < bkeys.size(); ++j) {
                    cache.insert(bkeys[j], vecs[j]);
                    if (db.isOpen()) {
                        QSqlQuery iq(db);
                        iq.prepare(QStringLiteral("INSERT OR REPLACE INTO vecs(key,dim,vec) VALUES(?,?,?)"));
                        iq.addBindValue(bkeys[j]); iq.addBindValue(vecs[j].size());
                        iq.addBindValue(vecToBlob(vecs[j])); iq.exec();
                    }
                }
                if (db.isOpen()) db.commit();
            }
            if (embedOk) {
                QString qerr;
                const QVector<QVector<float>> qv = embedTexts(auxiliaryBase, {query}, &qerr,
                                                              embeddingModel, auxiliaryKey);
                if (!qv.isEmpty() && !qv[0].isEmpty()) {
                    QVector<QPair<float, int>> scored;
                    for (int i = 0; i < chunks.size(); ++i) {
                        const QVector<float> v = cache.value(chunks[i].key);
                        if (!v.isEmpty()) scored.append({cosineSim(qv[0], v), i});
                    }
                    std::sort(scored.begin(), scored.end(),
                              [](auto &a, auto &b) { return a.first > b.first; });
                    for (const auto &p : scored) byVec.append(p.second);
                }
            }
        }

        // Reciprocal Rank Fusion (k0=60). Si no hubo vectorial, queda BM25 puro.
        const double k0 = 60.0;
        QHash<int, double> rrf;
        for (int r = 0; r < byBm25.size(); ++r) rrf[byBm25[r]] += 1.0 / (k0 + r + 1);
        for (int r = 0; r < byVec.size(); ++r)  rrf[byVec[r]]  += 1.0 / (k0 + r + 1);
        QVector<int> fused;
        fused.reserve(rrf.size());
        for (auto it = rrf.cbegin(); it != rrf.cend(); ++it) fused.append(it.key());
        std::sort(fused.begin(), fused.end(),
                  [&](int a, int b) { return rrf[a] > rrf[b]; });

        // Re-rank del top (hasta 30) con el reranker del server, si está.
        const int candN = qMin(fused.size(), 30);
        QVector<int> finalOrder = fused;
        QString rerankNote = byVec.isEmpty()
            ? QStringLiteral("BM25 (sin embeddings)") : QStringLiteral("BM25+vector RRF");
        if (candN > 1 && !auxiliaryBase.isEmpty()) {
            QStringList docs;
            for (int i = 0; i < candN; ++i) docs << chunks[fused[i]].text;
            QString rerr;
            const QVector<float> scores = rerankTexts(auxiliaryBase, query, docs, &rerr,
                                                      rerankModel, auxiliaryKey);
            if (scores.size() == candN) {
                QVector<int> idx(candN);
                for (int i = 0; i < candN; ++i) idx[i] = i;
                std::sort(idx.begin(), idx.end(),
                          [&](int a, int b) { return scores[a] > scores[b]; });
                finalOrder.clear();
                for (int i = 0; i < candN; ++i) finalOrder.append(fused[idx[i]]);
                for (int i = candN; i < fused.size(); ++i) finalOrder.append(fused[i]);
                rerankNote += QStringLiteral(" + rerank");
            }
        }

        // Empaquetado por presupuesto de tokens (estilo archex): si token_budget>0
        // se llena hasta el presupuesto (≈ chars/4) en vez de un k fijo. Devolver
        // contexto pre-presupuestado evita reventar la ventana del modelo local.
        const int tokenBudget = args.value(QStringLiteral("token_budget")).toInt();
        // Modo compacto (estilo FastContext): en vez de volcar el cuerpo del chunk al
        // contexto del solver, devolver sólo la cita span 'rel:Lini-Lfin' + un preview
        // de 1 línea. El agente principal lee después los spans que le interesan con
        // read_file. Ahorra tokens de exploración manteniendo provenance precisa.
        // repo_slice es el contrato pre-edición: devuelve evidencia navegable por
        // defecto. hybrid_search conserva su salida histórica con cuerpos.
        const bool compact = args.contains(QStringLiteral("compact"))
            ? args.value(QStringLiteral("compact")).toBool() : repoSlice;
        QStringList outL;
        QStringList outFiles;            // archivos ya incluidos (para el dep-graph)
        int usedTok = 0;
        bool budgetCut = false;
        QVariantList receiptReturned;
        QVariantList receiptSkipped;
        for (int i = 0; i < finalOrder.size(); ++i) {
            const Ch &c = chunks[finalOrder[i]];
            QString entry;
            if (compact) {
                // Preview = primera línea no vacía del chunk, recortada.
                QString preview;
                for (const QString &ln : c.text.split(QLatin1Char('\n'))) {
                    const QString t = ln.trimmed();
                    if (!t.isEmpty()) { preview = t.left(80); break; }
                }
                entry = QStringLiteral("%1:%2-%3  %4")
                            .arg(c.rel).arg(c.line).arg(c.endLine).arg(preview);
            } else {
                entry = QStringLiteral("%1:%2\n%3").arg(c.rel).arg(c.line).arg(c.text.left(600));
            }
            const int tok = entry.size() / 4 + 8;
            usedTok += tok;
            if (tokenBudget > 0) {
                if (!outL.isEmpty() && usedTok > tokenBudget) {
                    usedTok -= tok;
                    budgetCut = true;
                    receiptSkipped.append(QVariantMap{
                        {QStringLiteral("path"), c.rel},
                        {QStringLiteral("startLine"), c.line},
                        {QStringLiteral("endLine"), c.endLine},
                        {QStringLiteral("reason"), QStringLiteral("token_budget")}});
                    break;
                }
            } else if (outL.size() >= k) {
                usedTok -= tok;
                receiptSkipped.append(QVariantMap{
                    {QStringLiteral("path"), c.rel},
                    {QStringLiteral("reason"), QStringLiteral("k_limit")}});
                break;
            }
            outL << entry;
            receiptReturned.append(QVariantMap{
                {QStringLiteral("path"), c.rel},
                {QStringLiteral("startLine"), c.line},
                {QStringLiteral("endLine"), c.endLine},
                {QStringLiteral("score"), rrf.value(finalOrder[i])},
                {QStringLiteral("source"), rerankNote},
                {QStringLiteral("tokenEst"), tok}});
            if (!outFiles.contains(c.rel)) outFiles << c.rel;
        }

        // Expansión por dep-graph: a partir de los archivos en el resultado, mirar
        // qué módulos importan/incluyen y listar los vecinos del repo que casan por
        // nombre. Es "provenance" barata: el modelo ve qué archivos están a un salto.
        QString graphFooter;
        if (args.value(QStringLiteral("expand_graph")).toBool(true) && !outFiles.isEmpty()) {
            // Índice basename(sin ext)→rel de todos los archivos colectados.
            QHash<QString, QString> byBase;
            for (const QString &fp : files) {
                const QString rel = base.relativeFilePath(fp);
                const QString b = QFileInfo(rel).completeBaseName().toLower();
                if (!b.isEmpty() && !byBase.contains(b)) byBase.insert(b, rel);
            }
            QSet<QString> already(outFiles.cbegin(), outFiles.cend());
            QStringList neighbors;
            for (const QString &rel : outFiles) {
                QFile nf(base.absoluteFilePath(rel));
                if (!nf.open(QIODevice::ReadOnly)) continue;
                const QString full = QString::fromUtf8(nf.read(64 * 1024));
                for (const QString &refb : extractImportRefs(full)) {
                    const QString nrel = byBase.value(refb);
                    if (!nrel.isEmpty() && !already.contains(nrel)
                        && !neighbors.contains(nrel)) {
                        neighbors << nrel;
                        if (neighbors.size() >= 12) break;
                    }
                }
                if (neighbors.size() >= 12) break;
            }
            if (!neighbors.isEmpty())
                graphFooter = QStringLiteral("\n\n══════\nArchivos relacionados (dep-graph): ")
                                  + neighbors.join(QStringLiteral(", "));
        }

        if (ok) *ok = true;
        // Nota de docs: se informan también los que fallaron (con el primer motivo)
        // para que el agente sepa que hay fuentes NO indexadas y no concluya sobre
        // un corpus incompleto creyéndolo completo.
        QString docsNote;
        if (includeDocs) {
            docsNote = QStringLiteral(" · %1 docs").arg(docsIndexed);
            if (docsFailed > 0)
                docsNote += QStringLiteral(" (%1 sin extraer%2)")
                                .arg(docsFailed)
                                .arg(docErr.isEmpty() ? QString()
                                                      : QStringLiteral(": ") + docErr);
        }
        const QVariantMap receipt{
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("freshness"), QStringLiteral("live-scan")},
            {QStringLiteral("backend"), rerankNote},
            {QStringLiteral("returned"), receiptReturned},
            {QStringLiteral("skipped"), receiptSkipped},
            {QStringLiteral("graphOmitted"), QVariantList{}},
            {QStringLiteral("usedTokensEst"), usedTok},
            {QStringLiteral("remainingBudgetEst"), tokenBudget > 0 ? qMax(0, tokenBudget - usedTok) : 0},
            {QStringLiteral("budgetCut"), budgetCut},
            {QStringLiteral("recommendedNextAction"), receiptReturned.isEmpty()
                ? QStringLiteral("hybrid_search") : QStringLiteral("read_file")}};
        out[QStringLiteral("contextReceipt")] = receipt;
        const QString header = (repoSlice
            ? QStringLiteral("[repo_slice · evidencia previa a edición · %1 chunks · %2%3%4%5]\n\n")
            : QStringLiteral("[%1 chunks · %2%3%4%5]\n\n"))
            .arg(chunks.size()).arg(rerankNote)
            .arg(docsNote)
            .arg(truncated ? QStringLiteral(" · TRUNCADO a 800") : QString())
            .arg(tokenBudget > 0 ? QStringLiteral(" · ~%1 tok").arg(usedTok) : QString());
        const QString receiptText = QStringLiteral("\n\n── context-receipt ──\n")
            + QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(receipt))
                                    .toJson(QJsonDocument::Compact));
        return header + outL.join(compact ? QStringLiteral("\n")
                                          : QStringLiteral("\n\n──────\n"))
            + graphFooter + receiptText;
    }
    if (name == QLatin1String("verify_claims")) {
        // Anti-alucinación: por cada afirmación, busca evidencia en el proyecto y
        // en la memoria, y la etiqueta. NO reescribe el informe: devuelve un mapa
        // de respaldo para que el modelo redacte con cautela citando la fuente.
        QStringList claims;
        const QJsonValue cv = args.value(QStringLiteral("claims"));
        if (cv.isArray()) {
            for (const QJsonValue &v : cv.toArray())
                if (v.isString() && !v.toString().trimmed().isEmpty())
                    claims << v.toString().trimmed();
        } else {
            for (const QString &ln : cv.toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                if (!ln.trimmed().isEmpty()) claims << ln.trimmed();
        }
        if (claims.isEmpty()) return QStringLiteral("[verify_claims: 'claims' vacío]");
        const QString rootAbs = resolve(normalizeToolPath(args.value(QStringLiteral("path")).toString()));
        if (!inProject(rootAbs)) return outsideMsg(rootAbs);

        QStringList report;
        const QVector<MemoryStore::ClaimEvidence> evidence =
            MemoryStore::verifyClaims(cwd, claims, rootAbs, 8000);
        for (const MemoryStore::ClaimEvidence &item : evidence) {
            QString tag;
            if (item.status == QLatin1String("accredited"))
                tag = QStringLiteral("[ACREDITADO en %1]").arg(item.where);
            else if (item.status == QLatin1String("partial"))
                tag = QStringLiteral("[INFERIDO · parcial en %1]").arg(item.where);
            else
                tag = QStringLiteral("[NO ACREDITADO]");
            report << QStringLiteral("%1 %2  (cobertura %3)")
                          .arg(tag, item.claim).arg(item.coverage, 0, 'f', 2);
        }
        if (ok) *ok = true;
        return QStringLiteral("Verificación de evidencia (etiquetá las afirmaciones del "
                              "informe según esto; lo no acreditado va como hipótesis):\n\n")
               + report.join(QLatin1Char('\n'));
    }
    if (name == QLatin1String("grep")) {
        const QString pattern = args.value(QStringLiteral("pattern")).toString();
        const QString sub = normalizeToolPath(args.value(QStringLiteral("path")).toString());
        const QString rootAbs = resolve(sub);
        if (!inProject(rootAbs)) return outsideMsg(rootAbs);
        const QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid())
            return QStringLiteral("[regex inválida: %1]").arg(re.errorString());
        QStringList files;
        collectFiles(rootAbs, files, 8000);
        QStringList hits;
        for (const QString &fp : files) {
            if (hits.size() >= 100) break;
            QFile f(fp);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QByteArray raw = f.read(512 * 1024);
            if (raw.contains('\0')) continue;   // binario
            const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size(); ++i) {
                if (re.match(lines[i]).hasMatch()) {
                    hits << QStringLiteral("%1:%2: %3")
                            .arg(base.relativeFilePath(fp)).arg(i + 1).arg(lines[i].trimmed());
                    if (hits.size() >= 100) break;
                }
            }
        }
        if (ok) *ok = true;
        return hits.isEmpty() ? QStringLiteral("[sin coincidencias]") : hits.join(QLatin1Char('\n'));
    }
    if (name == QLatin1String("glob")) {
        const QString pattern = args.value(QStringLiteral("pattern")).toString();
        const QString sub = normalizeToolPath(args.value(QStringLiteral("path")).toString());
        const QString rootAbs = resolve(sub);
        if (!inProject(rootAbs)) return outsideMsg(rootAbs);
        const QRegularExpression re = globToRegex(pattern);
        // Si el patrón no tiene '/', matchea contra el nombre de archivo; si tiene,
        // contra la ruta relativa (con '/').
        const bool matchPath = pattern.contains(QLatin1Char('/'));
        QStringList files;
        collectFiles(rootAbs, files, 20000);
        QStringList matches;
        for (const QString &fp : files) {
            const QString rel = base.relativeFilePath(fp);
            const QString subject = matchPath ? rel : QFileInfo(fp).fileName();
            if (re.match(subject).hasMatch()) {
                matches << rel;
                if (matches.size() >= 300) break;
            }
        }
        matches.sort();
        if (ok) *ok = true;
        return matches.isEmpty() ? QStringLiteral("[sin coincidencias]")
                                 : matches.join(QLatin1Char('\n'));
    }
    if (name == QLatin1String("code_hotspots")) {
        HotspotAnalyzer::Options opts;
        if (args.contains(QStringLiteral("top")))
            opts.topN = qBound(1, args.value(QStringLiteral("top")).toInt(20), 200);
        if (args.contains(QStringLiteral("min_commits")))
            opts.minCommits = qMax(1, args.value(QStringLiteral("min_commits")).toInt(2));
        if (args.contains(QStringLiteral("since_days")))
            opts.sinceDays = qMax(0, args.value(QStringLiteral("since_days")).toInt(0));
        QString err;
        const auto hs = HotspotAnalyzer::analyzeRepo(base.absolutePath(), opts, &err);
        if (!err.isEmpty()) return QStringLiteral("[code_hotspots: %1]").arg(err);
        if (ok) *ok = true;
        return HotspotAnalyzer::formatReport(hs);
    }
    if (name == QLatin1String("review_overengineering")) {
        const QString rawScope = args.value(QStringLiteral("scope")).toString().trimmed().toLower();
        const QString scope = rawScope.isEmpty() ? QStringLiteral("working_tree") : rawScope;
        if (scope != QLatin1String("working_tree") && scope != QLatin1String("staged"))
            return QStringLiteral("[review_overengineering: scope inválido; usá working_tree o staged]");

        int maxChars = args.value(QStringLiteral("max_diff_chars")).toInt();
        if (!args.contains(QStringLiteral("max_diff_chars"))) maxChars = 120000;
        maxChars = qBound(1000, maxChars, 500000);
        const QStringList diffArgs = scope == QLatin1String("staged")
            ? QStringList{QStringLiteral("diff"), QStringLiteral("--cached"),
                          QStringLiteral("--no-ext-diff"), QStringLiteral("--unified=3")}
            : QStringList{QStringLiteral("diff"), QStringLiteral("HEAD"),
                          QStringLiteral("--no-ext-diff"), QStringLiteral("--unified=3")};

        auto runGit = [&base](const QStringList &argv, QByteArray *stdoutBytes,
                              QByteArray *stderrBytes, int *exitCode) {
            QProcess p;
            p.setWorkingDirectory(base.absolutePath());
            p.start(QStringLiteral("git"), argv);
            if (!p.waitForFinished(5000)) {
                p.kill();
                p.waitForFinished(500);
            }
            if (stdoutBytes) *stdoutBytes = p.readAllStandardOutput();
            if (stderrBytes) *stderrBytes = p.readAllStandardError();
            if (exitCode) *exitCode = p.exitCode();
            return p.exitStatus() == QProcess::NormalExit;
        };

        // No aceptar silenciosamente el cwd padre como si fuera un repo. Esto
        // mantiene el contrato headless determinista en workspaces temporales
        // y evita reportar cambios ajenos cuando el test o el agente corre
        // dentro de una carpeta anidada.
        QByteArray repoRoot;
        QByteArray repoError;
        int repoExit = -1;
        if (!runGit({QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")},
                    &repoRoot, &repoError, &repoExit) || repoExit != 0
            || repoRoot.trimmed().isEmpty()) {
            return QStringLiteral("[review_overengineering: no se pudo leer el diff git (%1)]")
                       .arg(QString::fromLocal8Bit(repoError).trimmed());
        }

        QByteArray diffBytes;
        QByteArray gitError;
        int gitExit = -1;
        if (!runGit(diffArgs, &diffBytes, &gitError, &gitExit) || gitExit != 0)
            return QStringLiteral("[review_overengineering: no se pudo leer el diff git (%1)]")
                       .arg(QString::fromLocal8Bit(gitError).trimmed());
        QString diff = QString::fromLocal8Bit(diffBytes);
        const bool truncated = diff.size() > maxChars;
        if (truncated) diff.truncate(maxChars);

        QByteArray statusBytes;
        runGit({QStringLiteral("status"), QStringLiteral("--short")}, &statusBytes, nullptr, nullptr);
        const QString status = QString::fromLocal8Bit(statusBytes).trimmed();

        int added = 0;
        int removed = 0;
        QStringList files;
        QStringList candidates;
        QString currentFile;
        const QRegularExpression fileRx(QStringLiteral("^\\+\\+\\+ b/(.+)$"));
        const QRegularExpression addedLineRx(
                                              QStringLiteral("^\\+(?!\\+).*\\b(?:TODO|FIXME|later|future|generic|factory|adapter|registry|configurable)\\b.*"),
                                              QRegularExpression::CaseInsensitiveOption);
        const QStringList lines = diff.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const auto fm = fileRx.match(line);
            if (fm.hasMatch()) {
                currentFile = fm.captured(1);
                if (!files.contains(currentFile)) files << currentFile;
                continue;
            }
            if (line.startsWith(QLatin1Char('+')) && !line.startsWith(QStringLiteral("+++"))) {
                ++added;
                if (addedLineRx.match(line).hasMatch() && !currentFile.isEmpty()) {
                    const QString detail = line.mid(1).trimmed().left(180);
                    candidates << QStringLiteral("%1: línea agregada contiene posible scaffolding o extensión especulativa: %2")
                                      .arg(currentFile, detail);
                }
                continue;
            }
            if (line.startsWith(QLatin1Char('-')) && !line.startsWith(QStringLiteral("---"))) {
                ++removed;
            }
        }
        QJsonArray candidateJson;
        for (const QString &candidate : std::as_const(candidates))
            candidateJson.append(candidate);
        QJsonArray fileJson;
        for (const QString &file : std::as_const(files)) fileJson.append(file);
        QJsonObject report{
            {QStringLiteral("readOnly"), true},
            {QStringLiteral("scope"), scope},
            {QStringLiteral("files"), fileJson},
            {QStringLiteral("metrics"), QJsonObject{
                {QStringLiteral("filesChanged"), files.size()},
                {QStringLiteral("addedLines"), added},
                {QStringLiteral("removedLines"), removed},
                {QStringLiteral("diffChars"), diff.size()},
                {QStringLiteral("truncated"), truncated},
                {QStringLiteral("workingTreeDirty"), !status.isEmpty()},
                {QStringLiteral("untrackedPresent"), status.contains(QRegularExpression(QStringLiteral("(^|\\n)\\?\\? ")))} }},
            {QStringLiteral("deleteList"), candidateJson},
            {QStringLiteral("guardrails"), QJsonArray{
                QStringLiteral("No se modificaron archivos."),
                QStringLiteral("Las sugerencias requieren revisión humana."),
                QStringLiteral("No se recomienda eliminar validación, seguridad, tests, accesibilidad ni manejo de errores.")}},
            {QStringLiteral("note"), status.isEmpty()
                ? QStringLiteral("El diff no contiene archivos no rastreados visibles en git status.")
                : QStringLiteral("git status detectó cambios adicionales; el diff no incluye automáticamente el contenido de archivos no rastreados.")}
        };
        if (ok) *ok = true;
        return QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact));
    }
    if (name == QLatin1String("write_file")) {
        const QString rel = normalizeToolPath(args.value(QStringLiteral("path")).toString());
        if (rel.isEmpty() || rel == QLatin1String("."))
            return QStringLiteral("[path vacío: especificá un archivo relativo válido]");
        const QString abs = resolve(rel);
        if (!inProject(abs)) return outsideMsg(abs);

        QFile prev(abs);
        const bool existed = prev.exists();
        QByteArray oldContent;
        if (existed && prev.open(QIODevice::ReadOnly)) { oldContent = prev.read(4 * 1024 * 1024); prev.close(); }

        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile f(abs);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QStringLiteral("[no se pudo escribir: %1]").arg(abs);
        const QByteArray data = args.value(QStringLiteral("content")).toString().toUtf8();
        f.write(data);
        f.close();
        if (ok) *ok = true;

        // Metadata para que el main arme el snapshot (revert) y el mensaje diff.
        out[QStringLiteral("isWrite")]      = true;
        out[QStringLiteral("relPath")]      = base.relativeFilePath(abs);
        out[QStringLiteral("absPath")]      = abs;
        out[QStringLiteral("existed")]      = existed;
        out[QStringLiteral("oldContentB64")] = QString::fromLatin1(oldContent.toBase64());
        out[QStringLiteral("diff")] = LlamaAgentBackend::makeDiff(QString::fromUtf8(oldContent),
                                                              QString::fromUtf8(data));
        return QStringLiteral("[escrito %1 bytes en %2]").arg(data.size()).arg(rel);
    }
    if (name == QLatin1String("edit_file")) {
        const QString rel = normalizeToolPath(args.value(QStringLiteral("path")).toString());
        if (rel.isEmpty() || rel == QLatin1String("."))
            return QStringLiteral("[path vacío: especificá un archivo relativo válido]");
        const QString abs = resolve(rel);
        if (!inProject(abs)) return outsideMsg(abs);
        QFile prev(abs);
        if (!prev.exists())
            return QStringLiteral("[no existe: %1 — usá write_file para crearlo]").arg(rel);
        if (!prev.open(QIODevice::ReadOnly))
            return QStringLiteral("[no se pudo abrir: %1]").arg(rel);
        const QByteArray oldContent = prev.read(8 * 1024 * 1024);
        prev.close();
        const QString oldText = QString::fromUtf8(oldContent);

        const QString oldS = args.value(QStringLiteral("old_string")).toString();
        const QString newS = args.value(QStringLiteral("new_string")).toString();
        const bool replaceAll = args.value(QStringLiteral("replace_all")).toBool();
        if (oldS.isEmpty())
            return QStringLiteral("[old_string vacío: especificá el texto exacto a reemplazar]");

        const int occurrences = oldText.count(oldS);
        if (occurrences == 0) {
            if (differsOnlyInWhitespace(oldText, oldS))
                return QStringLiteral("[no se encontró old_string exacto en %1, pero hay "
                                      "un bloque que coincide si se ignoran espacios/tabs/"
                                      "saltos. Leé el archivo y copiá los bytes exactos, "
                                      "incluyendo indentación.]").arg(rel);
            return QStringLiteral("[no se encontró old_string en %1. Copiá el texto EXACTO "
                                  "(con indentación) o leé el archivo primero.]").arg(rel);
        }
        if (occurrences > 1 && !replaceAll)
            return QStringLiteral("[old_string aparece %1 veces en %2: agregá más contexto "
                                  "para que sea único, o usá replace_all=true.]")
                       .arg(occurrences).arg(rel);

        QString newText = oldText;
        if (replaceAll) {
            newText.replace(oldS, newS);
        } else {
            const int idx = oldText.indexOf(oldS);
            newText = oldText.left(idx) + newS + oldText.mid(idx + oldS.size());
        }

        QFile f(abs);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QStringLiteral("[no se pudo escribir: %1]").arg(rel);
        const QByteArray data = newText.toUtf8();
        f.write(data);
        f.close();
        if (ok) *ok = true;

        // Misma metadata que write_file → reusa tarjeta de diff + snapshot/revert.
        out[QStringLiteral("isWrite")]       = true;
        out[QStringLiteral("relPath")]       = base.relativeFilePath(abs);
        out[QStringLiteral("absPath")]       = abs;
        out[QStringLiteral("existed")]       = true;
        out[QStringLiteral("oldContentB64")] = QString::fromLatin1(oldContent.toBase64());
        out[QStringLiteral("diff")] = LlamaAgentBackend::makeDiff(oldText, newText);
        return QStringLiteral("[editado %1 reemplazo(s) en %2]")
                   .arg(replaceAll ? occurrences : 1).arg(rel);
    }
    if (name == QLatin1String("memory")) {
        // Memoria PERSISTENTE por capas. Hechos atómicos con metadata en
        // .llamacode/memory.jsonl (scope/type/confidence + recall por relevancia).
        // Mantiene el viejo memory.md (append-only) para back-compat: 'recall' sin
        // query devuelve ambos.
        const QString action = args.value(QStringLiteral("action")).toString().trimmed().toLower();
        const QString scope = args.value(QStringLiteral("scope")).toString();
        if (action == QLatin1String("save") || action == QLatin1String("forget")
            || (action == QLatin1String("prune") || action == QLatin1String("decay"))
                && !args.value(QStringLiteral("dry_run")).toBool(false))
            out[QStringLiteral("isWrite")] = true;
        if (action == QLatin1String("save")) {
            const QString content = args.value(QStringLiteral("content")).toString().trimmed();
            if (content.isEmpty()) return QStringLiteral("[memory save: 'content' vacío]");
            const QString type = args.value(QStringLiteral("type")).toString();
            const double conf = args.value(QStringLiteral("confidence")).toDouble();
            const QString source = args.value(QStringLiteral("source")).toString();
            const double importance = args.value(QStringLiteral("importance")).toDouble();
            const double surprise = args.value(QStringLiteral("surprise")).toDouble();
            const QString verification = args.value(QStringLiteral("verification")).toString();
            const QString supersedes = args.value(QStringLiteral("supersedes")).toString();
            const QString res = MemoryStore::save(cwd, content, scope, type, conf, source,
                                                  importance, surprise, verification, supersedes);
            // Espejo en memory.md para inspección humana / compatibilidad.
            const QString mdPath = LlamaAgentBackend::memoryFilePath(cwd);
            QDir().mkpath(QFileInfo(mdPath).absolutePath());
            QFile md(mdPath);
            if (md.open(QIODevice::Append | QIODevice::Text)) {
                md.write((QStringLiteral("- (%1) %2\n")
                              .arg(QDateTime::currentDateTime().toString(Qt::ISODate), content)).toUtf8());
                md.close();
            }
            if (ok) *ok = true;
            return res;
        }
        if (action == QLatin1String("forget")) {
            // Olvido: marca stale (default) o borra hechos que matchean query/scope.
            const QString query = args.value(QStringLiteral("query")).toString();
            const QString mode = args.value(QStringLiteral("mode")).toString();
            const QString res = MemoryStore::forget(cwd, query, scope, mode);
            if (ok) *ok = true;
            return res;
        }
        if (action == QLatin1String("prune")) {
            // Poda anti-bloat: evicta hechos de bajo valor / redundantes.
            const int maxKeep = args.value(QStringLiteral("max_keep")).toInt();
            const QString mode = args.value(QStringLiteral("mode")).toString();
            const bool dryRun = args.value(QStringLiteral("dry_run")).toBool(false);
            const QString res = MemoryStore::prune(cwd, scope, maxKeep, mode, dryRun);
            if (ok) *ok = true;
            return res;
        }
        // recall (default): hechos estructurados (rankeados por query/scope si hay).
        const QString query = args.value(QStringLiteral("query")).toString();
        int k = args.value(QStringLiteral("k")).toInt();
        const QString facts = MemoryStore::recall(cwd, query, scope, k);
        if (ok) *ok = true;
        // Sin query ni scope: anexar el memory.md crudo para no perder lo viejo.
        if (query.trimmed().isEmpty() && scope.trimmed().isEmpty()) {
            QFile f(LlamaAgentBackend::memoryFilePath(cwd));
            if (f.open(QIODevice::ReadOnly)) {
                const QByteArray raw = f.read(256 * 1024);
                if (!raw.isEmpty())
                    return facts + QStringLiteral("\n\n── memory.md (legacy) ──\n")
                           + QString::fromUtf8(raw);
            }
        }
        return facts;
    }
    if (name == QLatin1String("graph")) {
        // KNOWLEDGE GRAPH: entidades + relaciones tipadas en .llamacode/graph.jsonl.
        // action='link' (default) conecta subj-[pred]->obj; 'add_entity' crea una
        // entidad; 'query' devuelve el vecindario de una entidad (depth 1..3;
        // packet opcional).
        const QString action = args.value(QStringLiteral("action")).toString().trimmed().toLower();
        if (action == QLatin1String("add_entity")) {
            const QString res = GraphStore::addEntity(
                cwd, args.value(QStringLiteral("name")).toString(),
                args.value(QStringLiteral("etype")).toString());
            if (ok) *ok = true;
            return res;
        }
        if (action == QLatin1String("index")) {
            // Pasada determinista repo→grafo (símbolos + imports). 'langs' opcional
            // (CSV o array) acota lenguajes; vacío = cpp/qml/js/ts/py.
            QStringList langs;
            const QJsonValue lv = args.value(QStringLiteral("langs"));
            if (lv.isArray()) {
                for (const QJsonValue &v : lv.toArray()) langs << v.toString();
            } else if (lv.isString()) {
                langs = lv.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
            }
            QString report;
            // 'files' explícito → reindexa esa lista. Si no, 'incremental' reindexa
            // sólo lo cambiado (git/mtime); por defecto, pasada completa.
            QStringList files;
            const QJsonValue fv = args.value(QStringLiteral("files"));
            if (fv.isArray()) {
                for (const QJsonValue &v : fv.toArray()) files << v.toString();
            } else if (fv.isString() && !fv.toString().trimmed().isEmpty()) {
                files = fv.toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
            }
            if (!files.isEmpty())
                CodeGraphIndexer::reindexFiles(cwd, files, langs, &report);
            else if (args.value(QStringLiteral("incremental")).toBool(false))
                CodeGraphIndexer::buildIncremental(cwd, langs, &report);
            else
                CodeGraphIndexer::build(cwd, langs, &report);
            if (ok) *ok = true;
            return report;
        }
        if (action == QLatin1String("query")) {
            const QString name = args.value(QStringLiteral("name")).toString();
            const int depth = args.value(QStringLiteral("depth")).toInt();
            const QString format = args.value(QStringLiteral("format")).toString()
                                       .trimmed().toLower();
            const QString res = format == QLatin1String("packet")
                ? QString::fromUtf8(QJsonDocument(GraphStore::queryPacket(cwd, name, depth))
                                        .toJson(QJsonDocument::Compact))
                : GraphStore::query(cwd, name, depth);
            if (ok) *ok = true;
            return res;
        }
        if (action == QLatin1String("decay")) {
            const int maxAgeDays = args.value(QStringLiteral("max_age_days")).toInt(90);
            const double minValue = args.value(QStringLiteral("min_value")).toDouble(0.28);
            const bool dryRun = args.value(QStringLiteral("dry_run")).toBool(false);
            const QString scope = args.value(QStringLiteral("scope")).toString();
            const QString res = MemoryStore::decay(cwd, scope, maxAgeDays, minValue, dryRun);
            if (ok) *ok = true;
            return res;
        }
        if (action == QLatin1String("doctor")) {
            const QJsonObject report = GraphStore::doctor(cwd);
            if (ok) *ok = report.value(QStringLiteral("ok")).toBool();
            return QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact));
        }
        if (action == QLatin1String("decide")) {
            // 'rejected' acepta array de objetos {alt,reason} o de strings sueltos.
            GraphStore::Rejected rejected;
            for (const QJsonValue &v : args.value(QStringLiteral("rejected")).toArray()) {
                if (v.isObject()) {
                    const QJsonObject ro = v.toObject();
                    rejected.append({ro.value(QStringLiteral("alt")).toString(),
                                     ro.value(QStringLiteral("reason")).toString()});
                } else {
                    rejected.append({v.toString(), QString()});
                }
            }
            const QString res = GraphStore::decide(
                cwd, args.value(QStringLiteral("topic")).toString(),
                args.value(QStringLiteral("chosen")).toString(), rejected,
                args.value(QStringLiteral("reason")).toString());
            if (ok) *ok = true;
            return res;
        }
        if (action == QLatin1String("decisions")) {
            const QString res = GraphStore::decisions(
                cwd, args.value(QStringLiteral("topic")).toString());
            if (ok) *ok = true;
            return res;
        }
        if (action == QLatin1String("verify")) {
            // Revisión/corrección de un edge existente: sube conf (default 1.0) y
            // marca prov=user, o lo tacha con drop=true.
            const double vconf = args.contains(QStringLiteral("confidence"))
                ? args.value(QStringLiteral("confidence")).toDouble() : 1.0;
            const QString res = GraphStore::reviewRelation(
                cwd, args.value(QStringLiteral("subj")).toString(),
                args.value(QStringLiteral("pred")).toString(),
                args.value(QStringLiteral("obj")).toString(), vconf,
                QStringLiteral("user"),
                args.value(QStringLiteral("drop")).toBool(false));
            if (ok) *ok = true;
            return res;
        }
        // link (default). Edge inferido por el LLM: entra unreviewed (conf<0)
        // salvo que el modelo pase 'confidence' explícito. 'edge_type' opcional
        // fuerza la taxonomía (REQUIRES/ENABLES/…); vacío = se infiere del pred.
        const double linkConf = args.contains(QStringLiteral("confidence"))
            ? args.value(QStringLiteral("confidence")).toDouble() : -1.0;
        const QString res = GraphStore::link(
            cwd, args.value(QStringLiteral("subj")).toString(),
            args.value(QStringLiteral("pred")).toString(),
            args.value(QStringLiteral("obj")).toString(),
            args.value(QStringLiteral("edge_type")).toString(),
            linkConf, QStringLiteral("llm"));
        if (ok) *ok = true;
        return res;
    }
    if (name == QLatin1String("ask_teacher")) {
        // Consulta puntual a un modelo MÁS capaz (endpoint OpenAI-compatible aparte).
        // Config por env: LLAMACODE_TEACHER_URL (req), _MODEL, _KEY (opcionales).
        const QString question = args.value(QStringLiteral("question")).toString().trimmed();
        if (question.isEmpty()) return QStringLiteral("[ask_teacher: 'question' vacía]");
        const QString ctxArg = args.value(QStringLiteral("context")).toString();
        // Cadena de fallbacks del perfil tiene prioridad: recorre niveles en orden.
        if (!m_masterChain.isEmpty())
            return runMasterChain(question, ctxArg, cwd, ok);
        // Maestro CLI legacy (claude-code / codex) si el perfil lo configuró.
        if (m_masterKind == QLatin1String("cli"))
            return runMasterCli(m_masterCliName, m_masterCliPath, m_masterApplyEdits,
                                m_masterTimeoutS, question, ctxArg, cwd, ok);
        // Config de UI (setTeacherConfig) tiene prioridad; si está vacía, env vars.
        const QString teacher = !m_teacherUrl.isEmpty()
            ? m_teacherUrl : qEnvironmentVariable("LLAMACODE_TEACHER_URL").trimmed();
        if (teacher.isEmpty())
            return QStringLiteral("[ask_teacher: configurá el endpoint del modelo maestro en "
                                  "Ajustes (o la env LLAMACODE_TEACHER_URL). Endpoint "
                                  "OpenAI-compatible, ej. https://api.openai.com o http://localhost:8081]");
        const QString model = !m_teacherModel.isEmpty() ? m_teacherModel
            : qEnvironmentVariable("LLAMACODE_TEACHER_MODEL", QStringLiteral("default"));
        const QString key = !m_teacherKey.isEmpty()
            ? m_teacherKey : qEnvironmentVariable("LLAMACODE_TEACHER_KEY").trimmed();
        const QString context = args.value(QStringLiteral("context")).toString();

        QString userMsg = question;
        if (!context.isEmpty())
            userMsg = QStringLiteral("Contexto:\n%1\n\nPregunta:\n%2").arg(context, question);
        const QJsonArray msgs{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                        {QStringLiteral("content"), masterSystemPrompt(m_honeyHandoff)}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), userMsg}}};
        const QJsonObject payload{
            {QStringLiteral("model"), model},
            {QStringLiteral("messages"), msgs},
            {QStringLiteral("stream"), false}};
        const QUrl url(teacher.endsWith(QLatin1Char('/'))
                           ? teacher + QStringLiteral("v1/chat/completions")
                           : teacher + QStringLiteral("/v1/chat/completions"));
        QString err;
        const QByteArray resp = httpPostJson(url, QJsonDocument(payload).toJson(QJsonDocument::Compact),
                                             &err, 120000, key);
        if (resp.isEmpty())
            return QStringLiteral("[ask_teacher: error consultando al maestro: %1]").arg(err);
        const QJsonObject root = QJsonDocument::fromJson(resp).object();
        const QString answer = root.value(QStringLiteral("choices")).toArray().isEmpty()
            ? QString()
            : root.value(QStringLiteral("choices")).toArray().first().toObject()
                  .value(QStringLiteral("message")).toObject()
                  .value(QStringLiteral("content")).toString();
        if (answer.isEmpty())
            return QStringLiteral("[ask_teacher: respuesta vacía del maestro]");
        if (ok) *ok = true;
        return QStringLiteral("[Respuesta del modelo maestro]\n") + answer;
    }
    // ── Browser teach: listar/reproducir skills grabados ──────────────────
    if (name == QLatin1String("browser_skill_list")) {
        const QStringList skills = BrowserTeach::listSkills();
        if (ok) *ok = true;
        if (skills.isEmpty())
            return QStringLiteral("[sin skills de browser grabados. El usuario los graba "
                                  "en Ajustes → Automatización de browser.]");
        return QStringLiteral("Skills de browser disponibles:\n- ") + skills.join(QStringLiteral("\n- "));
    }
    if (name == QLatin1String("browser_skill_replay")) {
        const QString skill = args.value(QStringLiteral("name")).toString();
        if (skill.trimmed().isEmpty())
            return QStringLiteral("[browser_skill_replay: falta 'name']");
        if (!BrowserTeach::hasSkill(skill))
            return QStringLiteral("[browser_skill_replay: skill no encontrado: %1. Usá "
                                  "browser_skill_list para ver los disponibles.]").arg(skill);
        const QStringList pa = BrowserTeach::replayProgramArgs(skill);
        if (pa.size() < 2)
            return QStringLiteral("[browser_skill_replay: skill inválido: %1]").arg(skill);
        QProcess proc;
        proc.setWorkingDirectory(BrowserTeach::skillsDir());   // resuelve 'playwright' local
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(pa.first(), pa.mid(1));
        if (!proc.waitForStarted(8000))
            return QStringLiteral("[browser_skill_replay: no se pudo iniciar node. ¿Node instalado?]");
        if (!proc.waitForFinished(180000)) {
            proc.kill(); proc.waitForFinished(2000);
            return QStringLiteral("[browser_skill_replay: timeout (180s) reproduciendo %1]").arg(skill);
        }
        const QString out = QString::fromUtf8(proc.readAll()).trimmed();
        const bool good = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
        if (ok) *ok = good;
        const QVariantMap trace{
            {QStringLiteral("surface"), QStringLiteral("browser")},
            {QStringLiteral("action"), QStringLiteral("skill_replay")},
            {QStringLiteral("target"), QVariantMap{
                {QStringLiteral("driver"), QStringLiteral("playwright")},
                {QStringLiteral("mode"), QStringLiteral("backgroundExecution")},
                {QStringLiteral("skill"), skill},
                {QStringLiteral("script"), pa.value(1)}}},
            {QStringLiteral("result"), QVariantMap{
                {QStringLiteral("exitCode"), proc.exitCode()},
                {QStringLiteral("ok"), good}}}};
        const QString json = QString::fromUtf8(QJsonDocument(
            QJsonObject::fromVariantMap(trace)).toJson(QJsonDocument::Compact));
        return QStringLiteral("[browser_skill_replay %1 · exit=%2]\ntrace=%3\n%4")
                   .arg(skill).arg(proc.exitCode()).arg(json, out.left(8000));
    }
    if (name == QLatin1String("browser_network_discover")) {
        McpClient *client = nullptr;
        const McpClient::ToolDef *networkTool = nullptr;
        for (McpClient *candidate : std::as_const(m_mcp)) {
            for (const McpClient::ToolDef &tool : candidate->tools()) {
                const QString normalized = tool.name.toLower();
                if (normalized == QLatin1String("browser_network_requests")
                    || (normalized.contains(QLatin1String("network"))
                        && normalized.contains(QLatin1String("request")))) {
                    client = candidate;
                    networkTool = &tool;
                    break;
                }
            }
            if (networkTool) break;
        }
        if (!client || !networkTool)
            return QStringLiteral("[browser_network_discover: el MCP de navegador activo "
                                  "no expone inspección de requests. Abrí una página con "
                                  "Playwright o actualizá playwright-mcp.]");

        const bool includeStatic = args.value(QStringLiteral("include_static")).toBool(false);
        QJsonObject networkArgs;
        const QJsonObject networkProperties =
            networkTool->inputSchema.value(QStringLiteral("properties")).toObject();
        if (networkProperties.contains(QStringLiteral("static")))
            networkArgs.insert(QStringLiteral("static"), includeStatic);
        else if (networkProperties.contains(QStringLiteral("includeStatic")))
            networkArgs.insert(QStringLiteral("includeStatic"), includeStatic);
        bool called = false;
        const QString raw = client->callTool(networkTool->name, networkArgs, &called,
                                             nullptr, {}, m_correlationId);
        if (!called)
            return QStringLiteral("[browser_network_discover: Playwright no pudo leer "
                                  "el tráfico]\n") + raw.left(2000);
        const QString summarized = summarizeBrowserNetworkEvidence(raw, includeStatic);
        const QString artifactId = args.value(QStringLiteral("artifact_id")).toString().trimmed();
        bool persisted = false;
        if (!artifactId.isEmpty()) {
            const QVariantMap evidence =
                QJsonDocument::fromJson(summarized.toUtf8()).object().toVariantMap();
            persisted = AutomationArtifactStore::appendNetworkDiscovery(
                artifactId, evidence, args.value(QStringLiteral("action")).toString());
        }
        if (ok) *ok = true;
        if (artifactId.isEmpty()) return summarized;
        QJsonObject response = QJsonDocument::fromJson(summarized.toUtf8()).object();
        response[QStringLiteral("artifactId")] = artifactId;
        response[QStringLiteral("persisted")] = persisted;
        return QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
    }
    if (name == QLatin1String("email_accounts")) {
        if (ok) *ok = true;
        if (m_mailAccounts.isEmpty())
            return QStringLiteral("[sin cuentas de correo configuradas. El usuario las "
                                  "agrega en Configuración → Correo.]");
        QStringList names;
        for (const QVariant &v : m_mailAccounts) {
            const QVariantMap a = v.toMap();
            names << QStringLiteral("%1 <%2>").arg(a.value(QStringLiteral("name")).toString(),
                                                   a.value(QStringLiteral("email")).toString());
        }
        return QStringLiteral("Cuentas de correo:\n- ") + names.join(QStringLiteral("\n- "));
    }

    if (name == QLatin1String("email_send") || name == QLatin1String("email_list")
        || name == QLatin1String("email_read")) {
        if (m_mailAccounts.isEmpty())
            return QStringLiteral("[%1: no hay cuentas de correo. Configuralas en "
                                  "Configuración → Correo.]").arg(name);
        // Resolver cuenta: por 'account' o la primera (default).
        const QString want = args.value(QStringLiteral("account")).toString().trimmed();
        QVariantMap acctMap = m_mailAccounts.first().toMap();
        if (!want.isEmpty()) {
            bool found = false;
            for (const QVariant &v : m_mailAccounts) {
                const QVariantMap a = v.toMap();
                if (a.value(QStringLiteral("name")).toString() == want
                    || a.value(QStringLiteral("email")).toString() == want) {
                    acctMap = a; found = true; break;
                }
            }
            if (!found)
                return QStringLiteral("[%1: cuenta '%2' no encontrada. Usá email_accounts.]")
                           .arg(name, want);
        }
        MailClient::Account acct;
        acct.name        = acctMap.value(QStringLiteral("name")).toString();
        acct.email       = acctMap.value(QStringLiteral("email")).toString();
        acct.displayName = acctMap.value(QStringLiteral("displayName")).toString();
        acct.provider    = acctMap.value(QStringLiteral("provider")).toString();
        acct.smtpHost    = acctMap.value(QStringLiteral("smtpHost")).toString();
        acct.smtpPort    = acctMap.value(QStringLiteral("smtpPort")).toInt();
        acct.smtpSecurity= acctMap.value(QStringLiteral("smtpSecurity")).toString();
        acct.recvProto   = acctMap.value(QStringLiteral("recvProto")).toString();
        acct.recvHost    = acctMap.value(QStringLiteral("recvHost")).toString();
        acct.recvPort    = acctMap.value(QStringLiteral("recvPort")).toInt();
        acct.recvSsl     = acctMap.value(QStringLiteral("recvSsl"), true).toBool();
        acct.user        = acctMap.value(QStringLiteral("user")).toString();
        acct.password    = acctMap.value(QStringLiteral("password")).toString();

        if (name == QLatin1String("email_send")) {
            const QString to = args.value(QStringLiteral("to")).toString().trimmed();
            const QString subject = args.value(QStringLiteral("subject")).toString();
            const QString body = args.value(QStringLiteral("body")).toString();
            const QString cc = args.value(QStringLiteral("cc")).toString();
            if (to.isEmpty()) return QStringLiteral("[email_send: falta 'to']");
            QString err;
            if (MailClient::sendSmtp(acct, to, cc, subject, body, &err)) {
                if (ok) *ok = true;
                return QStringLiteral("[email enviado a %1 · asunto: %2]").arg(to, subject);
            }
            return QStringLiteral("[email_send falló: %1]").arg(err);
        }
        if (name == QLatin1String("email_list")) {
            const QString folder = args.value(QStringLiteral("folder")).toString();
            int limit = args.value(QStringLiteral("limit")).toInt(10);
            const bool unread = args.value(QStringLiteral("unread_only")).toBool();
            QString err;
            const QVariantList msgs = MailClient::fetchInbox(acct, folder, limit, unread, &err);
            if (!err.isEmpty()) return QStringLiteral("[email_list falló: %1]").arg(err);
            if (ok) *ok = true;
            if (msgs.isEmpty()) return QStringLiteral("[sin correos]");
            QString s;
            for (const QVariant &v : msgs) {
                const QVariantMap m = v.toMap();
                s += QStringLiteral("uid:%1 | %2 | de: %3 | %4\n")
                         .arg(m.value(QStringLiteral("uid")).toString(),
                              m.value(QStringLiteral("date")).toString(),
                              m.value(QStringLiteral("from")).toString(),
                              m.value(QStringLiteral("subject")).toString());
            }
            return s.trimmed();
        }
        // email_read
        const QString uid = args.value(QStringLiteral("uid")).toString().trimmed();
        if (uid.isEmpty()) return QStringLiteral("[email_read: falta 'uid' (ver email_list)]");
        const QString folder = args.value(QStringLiteral("folder")).toString();
        QString err;
        const QString body = MailClient::readMessage(acct, folder, uid, &err);
        if (!err.isEmpty()) return QStringLiteral("[email_read falló: %1]").arg(err);
        if (ok) *ok = true;
        return body.left(20000);
    }

    // run_shell se maneja async en executeTool/startShell (no llega acá).
    return QStringLiteral("[tool desconocida: %1]").arg(name);
}

// ───────────────────────────── run_shell async ───────────────────────────
void AgentToolRunner::startShell(const QString &callId, const QString &command,
                                 const QString &cwd, int timeoutS)
{
    // Solo un shell a la vez (el loop ReAct es secuencial). Si quedó uno vivo,
    // matarlo sin emitir (no debería pasar).
    if (m_shellProc) { m_shellProc->kill(); m_shellProc->deleteLater(); m_shellProc = nullptr; }

    m_shellCallId = callId;
    m_shellOut.clear();
    m_shellTimeoutS = timeoutS;
    m_shellClock.start();

    emit toolStarted(QVariantMap{
        {QStringLiteral("callId"), callId},
        {QStringLiteral("name"), QStringLiteral("run_shell")},
        {QStringLiteral("kind"), QStringLiteral("shell")},
        {QStringLiteral("command"), command}});

    m_shellProc = new QProcess(this);   // vive en el hilo worker
    m_shellProc->setWorkingDirectory(cwd);
    m_shellProc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_shellProc, &QProcess::readyRead, this, &AgentToolRunner::onShellReadyRead);
    connect(m_shellProc, &QProcess::finished, this, &AgentToolRunner::onShellFinished);

#ifdef Q_OS_WIN
    // cmd con setNativeArguments: no re-citar args (rompe comillas anidadas).
    m_shellProc->setProgram(QStringLiteral("cmd"));
    m_shellProc->setNativeArguments(QStringLiteral("/c ") + command);
    m_shellProc->start();
#else
    m_shellProc->start(QStringLiteral("sh"), {QStringLiteral("-c"), command});
#endif
    if (!m_shellProc->waitForStarted(5000)) {
        m_shellProc->deleteLater();
        m_shellProc = nullptr;
        const QString cid = m_shellCallId; m_shellCallId.clear();
        emit toolExecuted(QVariantMap{
            {QStringLiteral("callId"), cid}, {QStringLiteral("name"), QStringLiteral("run_shell")},
            {QStringLiteral("result"), QStringLiteral("[no se pudo iniciar el comando]")},
            {QStringLiteral("ok"), false}});
        return;
    }

    if (!m_shellTimer) {
        m_shellTimer = new QTimer(this);
        m_shellTimer->setSingleShot(true);
        connect(m_shellTimer, &QTimer::timeout, this, &AgentToolRunner::onShellTimeout);
    }
    m_shellTimer->start(timeoutS * 1000);
}

void AgentToolRunner::onShellReadyRead()
{
    if (!m_shellProc) return;
    const QByteArray chunk = m_shellProc->readAll();
    if (chunk.isEmpty()) return;
    m_shellOut.append(chunk);
    // Cap de memoria de la salida acumulada (la tarjeta igual recorta).
    if (m_shellOut.size() > 2 * 1024 * 1024)
        m_shellOut = m_shellOut.right(2 * 1024 * 1024);
    emit toolOutputChunk(m_shellCallId, QString::fromUtf8(chunk));
}

void AgentToolRunner::onShellFinished()
{
    finishShell(false, false);
}

void AgentToolRunner::onShellTimeout()
{
    if (!m_shellProc) return;
    terminateProcessTree(m_shellProc); // dispara finished() → finishShell vía bandera
    finishShell(true, false);
}

void AgentToolRunner::cancelShell()
{
    if (!m_shellProc) return;
    terminateProcessTree(m_shellProc);
    finishShell(false, true);
}

void AgentToolRunner::finishShell(bool timedOut, bool cancelled)
{
    if (!m_shellProc) return;
    if (m_shellTimer) m_shellTimer->stop();

    // Drenar lo que quede y desconectar para no re-entrar (kill emite finished()).
    m_shellOut.append(m_shellProc->readAll());
    m_shellProc->disconnect(this);
    if (m_shellProc->state() != QProcess::NotRunning)
        m_shellProc->waitForFinished(2000);
    const int exitCode = m_shellProc->exitCode();
    m_shellProc->deleteLater();
    m_shellProc = nullptr;

    const QString cid = m_shellCallId;
    m_shellCallId.clear();
    const QString outStr = QString::fromUtf8(m_shellOut).left(64 * 1024);

    QString result;
    bool ok = true;
    if (cancelled) { result = QStringLiteral("[cancelado por el usuario]\n") + outStr; ok = false; }
    else if (timedOut) { result = QStringLiteral("[timeout tras %1s — proceso terminado]\n%2")
                                       .arg(m_shellTimeoutS).arg(outStr); ok = false; }
    else result = QStringLiteral("exit=%1\n%2").arg(exitCode).arg(outStr);

    emit toolExecuted(QVariantMap{
        {QStringLiteral("callId"), cid},
        {QStringLiteral("name"), QStringLiteral("run_shell")},
        {QStringLiteral("result"), result},
        {QStringLiteral("ok"), ok}});
}
