#include "LlamaAgentBackend.h"
#include "ReasoningWire.h"
#include "AgentToolRunner.h"
#include "ContextPreflight.h"
#include "ContextIndex.h"
#include "ProjectBrain.h"
#include "CodeGraphIndexer.h"
#include "KnowledgePacket.h"
#include "GraphStore.h"
#include "SubAgentRunner.h"
#include "AgentEfficiency.h"
#include "AgentEventLog.h"
#include "WorkRegistry.h"
#include "ToolExecutionSafety.h"
#include "HarnessWorkerProtocol.h"
#include "MemoryStore.h"          // consolidación de memoria (background)
#include "core/DocumentExtractor.h"
#include "core/ToolCallingSupport.h"
#include "core/automation/FuzzyMatch.h"
#include "core/automation/DesktopComputerUse.h"
#include "core/automation/AutomationArtifactStore.h"

#include <QJsonArray>

#include <QCryptographicHash>
#include <QCoreApplication>

#include <QDateTime>
#include <QThread>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QPointer>
#include <QRegularExpression>
#include <QDebug>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

struct AgentDurableCaptureResult {
    QJsonObject manifest;
    QString error;
};

struct AgentDurableRunState {
    QString runId;
    QString sessionId;
    QString workspace;
    QString runStoreRoot;
    QString leaseToken;
    QJsonObject beforeSnapshot;
    QString status;
    QString detail;
    QTimer *captureLeaseTimer = nullptr;
    bool snapshotReady = false;
    bool finishRequested = false;
    bool captureStarted = false;
};

static int estimateTokens(const QString &text)
{
    const int n = text.trimmed().size();
    if (n <= 0) return 0;
    return (n + 3) / 4;
}

// Finaliza las métricas de una burbuja de asistente (tiempo/tokens/tps).
// El tps mide VELOCIDAD DE GENERACIÓN: el cronómetro arranca en el primer token
// (genStartMs), no cuando se creó la burbuja. Así no se contamina con el
// prompt-processing del modelo (TTFT), que en local puede tardar minutos y
// hundía el tps. Fallback a createdAt si nunca llegó a streamear.
// srvTokens/srvGenMs: métricas REALES del server (timings.predicted_n /
// predicted_ms). Si están (>0) se usan para tokens y para el tps; el tps queda
// como velocidad de GENERACIÓN pura (excluye prompt-processing por definición).
// Sin ellas, fallback al estimado: tokens=chars/4 y tps sobre el wall desde el
// primer token (genStartMs) — que SÍ incluye stalls/TTFT residual y subestima.
static void finalizeMsgMetrics(QVariantMap &m, int srvTokens = 0, double srvGenMs = 0.0)
{
    const qint64 doneAt = QDateTime::currentMSecsSinceEpoch();
    qint64 startMs = static_cast<qint64>(m.value(QStringLiteral("genStartMs")).toDouble());
    if (startMs <= 0)
        startMs = static_cast<qint64>(m.value(QStringLiteral("createdAt")).toDouble());
    const qint64 wallMs = qMax<qint64>(0, doneAt - startMs);
    const int toks = (srvTokens > 0) ? srvTokens
                                     : estimateTokens(m.value(QStringLiteral("content")).toString());
    // Tiempo para el tps: el de generación del server si lo tenemos; si no, wall.
    const double genMs = (srvGenMs > 0.0) ? srvGenMs : static_cast<double>(wallMs);
    m[QStringLiteral("completedAt")] = static_cast<double>(doneAt);
    m[QStringLiteral("tokens")] = toks;
    m[QStringLiteral("elapsedMs")] = static_cast<int>(wallMs);   // wall honesto (incluye TTFT)
    m[QStringLiteral("tps")] = (genMs > 0.0 && toks > 0)
        ? (1000.0 * static_cast<double>(toks) / genMs)
        : 0.0;
}

// Quita bloques <think>...</think> antes de mandar el historial al modelo: el
// razonamiento es solo para mostrar; reenviarlo confunde el tool-calling.
static QString stripThinkForContext(const QString &s)
{
    QString out = s;
    out.remove(QRegularExpression(QStringLiteral("<think>[\\s\\S]*?</think>"),
                                  QRegularExpression::CaseInsensitiveOption));
    out.remove(QRegularExpression(QStringLiteral("</?think>"),
                                  QRegularExpression::CaseInsensitiveOption));
    return out.trimmed();
}

// Cuando "Pensar" está apagado, algunos modelos igual streamean tags <think>
// dentro de content. La UI y el historial deben quedarse sólo con la respuesta.
static QString stripThinkForOutput(const QString &s, bool truncateOrphanTail)
{
    QString out = s;
    out.remove(QRegularExpression(QStringLiteral("<think>[\\s\\S]*?</think>"),
                                  QRegularExpression::CaseInsensitiveOption));
    const QRegularExpression openRe(QStringLiteral("<think\\b[^>]*>"),
                                    QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression closeRe(QStringLiteral("</think>"),
                                     QRegularExpression::CaseInsensitiveOption);
    // Algunos templates emiten una respuesta final válida y luego filtran un
    // `</think>` huérfano antes de repetirla. Con thinking apagado ese cierre no
    // puede aportar semántica. Si ya hay una respuesta sustancial, conservar el
    // prefijo evita mostrar/persistir la cola repetida; si aparece al principio,
    // se elimina abajo y se conserva lo que siga.
    const QRegularExpressionMatch orphanClose = closeRe.match(out);
    if (truncateOrphanTail && orphanClose.hasMatch()
        && !out.left(orphanClose.capturedStart()).trimmed().isEmpty()
        && !out.left(orphanClose.capturedStart()).contains(
            QRegularExpression(QStringLiteral("<think\\b"),
                               QRegularExpression::CaseInsensitiveOption))) {
        out.truncate(orphanClose.capturedStart());
    }
    QRegularExpressionMatch open = openRe.match(out);
    while (open.hasMatch()) {
        const QRegularExpressionMatch close = closeRe.match(out, open.capturedEnd());
        if (!close.hasMatch()) {
            out.truncate(open.capturedStart());
            break;
        }
        out.remove(open.capturedStart(), close.capturedEnd() - open.capturedStart());
        open = openRe.match(out);
    }
    out.remove(closeRe);
    out.remove(openRe);
    return out.trimmed();
}

QString LlamaAgentBackend::visibleAnswer(const QString &content, bool thinkingEnabled,
                                         bool thinkingLeakGuard)
{
    if (thinkingEnabled) return content;
    const QString stripped = stripThinkForOutput(content, thinkingLeakGuard);
    if (!stripped.isEmpty()) return stripped;
    // El modelo metió TODO dentro de <think> y no dejó respuesta afuera: rescatar el
    // interior (sin las etiquetas) para no mostrar una burbuja vacía.
    QString inner = content;
    inner.remove(QRegularExpression(QStringLiteral("</?think\\b[^>]*>"),
                                    QRegularExpression::CaseInsensitiveOption));
    return inner.trimmed();
}

QJsonObject LlamaAgentBackend::thinkingTemplateKwargs(bool thinkingEnabled,
                                                      bool thinkingLeakGuard,
                                                      const QString &reasoningEffort)
{
    return ReasoningWire::templateKwargs(thinkingEnabled, thinkingLeakGuard,
                                         reasoningEffort);
}

static const QString kMcpPrefix = QStringLiteral("mcp__");

// Data-URI base64 si el archivo es imagen soportada por mmproj; "" si no.
static QString imageDataUri(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    QString mime;
    if (ext == QLatin1String("png")) mime = QStringLiteral("image/png");
    else if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg")) mime = QStringLiteral("image/jpeg");
    else if (ext == QLatin1String("webp")) mime = QStringLiteral("image/webp");
    else if (ext == QLatin1String("gif")) mime = QStringLiteral("image/gif");
    else if (ext == QLatin1String("bmp")) mime = QStringLiteral("image/bmp");
    else return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(f.readAll().toBase64()));
}

// Arma el mensaje user multimodal con las capturas observadas (data-URIs ya
// codificadas). content = [text, image_url...]. Objeto vacío si no hay imágenes.
QJsonObject LlamaAgentBackend::buildObservationMessage(const QStringList &imageDataUris)
{
    QJsonArray parts;
    int n = 0;
    for (const QString &uri : imageDataUris) {
        if (uri.isEmpty()) continue;
        parts.append(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("image_url")},
            {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), uri}}}});
        ++n;
    }
    if (n == 0) return {};
    QJsonArray content;
    content.append(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("text")},
        {QStringLiteral("text"), n == 1
            ? QStringLiteral("Captura de la observación que pediste. Mirala y decidí el "
                             "próximo paso a partir de lo que VES, no de suposiciones.")
            : QStringLiteral("Capturas de las observaciones que pediste (%1). Miralas y "
                             "decidí el próximo paso a partir de lo que VES.").arg(n)}});
    for (const QJsonValue &p : parts) content.append(p);
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                       {QStringLiteral("content"), content}};
}

// Texto de un archivo (UTF-8), "" si binario/imagen.
static QString readAttachText(const QString &path)
{
    // Documentos (txt/pdf/docx/xlsx/pptx/html/…) → markdown vía DocumentExtractor
    // (markitdown para formatos ricos). Las imágenes ya se manejan por visión.
    QString err;
    const QString doc = DocumentExtractor::extract(path, &err);
    if (!doc.isEmpty()) return doc;
    if (!err.isEmpty())
        return QStringLiteral("[no se pudo extraer %1: %2]")
                   .arg(QFileInfo(path).fileName(), err);
    return {};
}

// Recorta la salida de una tool ANTES de meterla al contexto (m_apiMessages).
// La tarjeta de la UI conserva la salida completa; al modelo le mandamos una
// versión acotada. Idea tomada de los "tool budgets" de caveman-code: en local
// (gen ~40 tok/s + SWA que reprocesa todo el prompt cada iter) cada línea de
// salida que no aporta es contexto que se re-evalúa una y otra vez.
static QString budgetToolOutput(const QString &name, const QString &raw)
{
    QString s = raw;
    // 1) Sacar secuencias de escape ANSI (colores/cursor de build/test/git).
    s.remove(QRegularExpression(QStringLiteral("\x1B\\[[0-9;?]*[A-Za-z]")));
    // 2) Colapsar runs de líneas en blanco a una sola.
    s.replace(QRegularExpression(QStringLiteral("\n[ \t]*(?:\n[ \t]*)+")),
              QStringLiteral("\n\n"));
    // 3) Cap por líneas según la tool. run_shell mantiene más cola (los errores
    //    suelen estar al final). El resto, cap razonable.
    int cap;
    if (name == QLatin1String("read_file"))      cap = 300;
    else if (name == QLatin1String("grep"))      cap = 120;
    else if (name == QLatin1String("list_dir"))  cap = 200;
    else if (name == QLatin1String("run_shell")) cap = 120;
    else                                          cap = 200;   // mcp/otras
    QStringList lines = s.split(QLatin1Char('\n'));
    if (lines.size() > cap) {
        const int head = cap * 2 / 3;
        const int tail = cap - head;
        QStringList kept = lines.mid(0, head);
        kept << QStringLiteral("… [%1 líneas omitidas para ahorrar contexto] …")
                    .arg(lines.size() - cap);
        kept += lines.mid(lines.size() - tail);
        s = kept.join(QLatin1Char('\n'));
    }
    // 4) Tope duro de caracteres como red de seguridad.
    if (s.size() > 24000)
        s = s.left(24000) + QStringLiteral("\n… [truncado]");
    return s;
}

static QString budgetTextToolOutput(const QString &name, const QString &raw)
{
    QString s = budgetToolOutput(name, raw);
    const int cap = (name == QLatin1String("web_fetch")
                     || name == QLatin1String("web_search")
                     || name == QLatin1String("deep_research"))
        ? 6000
        : 9000;
    if (s.size() > cap)
        s = s.left(cap) + QStringLiteral("\n... [TOOL_RESULT truncado para modo compatible textual]");
    return s;
}

static QString toolArgumentsToString(const QJsonValue &v)
{
    if (v.isString()) return v.toString();
    if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isArray())  return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    return {};
}

static QString extractBalancedJsonObject(const QString &s, int start)
{
    int depth = 0;
    bool inString = false;
    bool escape = false;
    int objectStart = -1;
    for (int i = qMax(0, start); i < s.size(); ++i) {
        const QChar ch = s.at(i);
        if (objectStart < 0) {
            if (ch == QLatin1Char('{')) {
                objectStart = i;
                depth = 1;
            }
            continue;
        }
        if (inString) {
            if (escape) {
                escape = false;
            } else if (ch == QLatin1Char('\\')) {
                escape = true;
            } else if (ch == QLatin1Char('"')) {
                inString = false;
            }
            continue;
        }
        if (ch == QLatin1Char('"')) {
            inString = true;
        } else if (ch == QLatin1Char('{')) {
            ++depth;
        } else if (ch == QLatin1Char('}')) {
            if (--depth == 0)
                return s.mid(objectStart, i - objectStart + 1);
        }
    }
    return {};
}

static bool isToolMessage(const QJsonObject &m)
{
    return m.value(QStringLiteral("role")).toString() == QLatin1String("tool");
}

static bool isAssistantWithToolCalls(const QJsonObject &m)
{
    return m.value(QStringLiteral("role")).toString() == QLatin1String("assistant")
           && !m.value(QStringLiteral("tool_calls")).toArray().isEmpty();
}

static QSet<QString> toolCallIds(const QJsonObject &assistant)
{
    QSet<QString> ids;
    const QJsonArray calls = assistant.value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue &v : calls) {
        const QString id = v.toObject().value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) ids.insert(id);
    }
    return ids;
}

static QJsonArray dropDanglingAssistantToolCalls(const QJsonArray &messages)
{
    QSet<QString> answered;
    for (const QJsonValue &v : messages) {
        const QJsonObject o = v.toObject();
        if (isToolMessage(o)) {
            const QString id = o.value(QStringLiteral("tool_call_id")).toString();
            if (!id.isEmpty()) answered.insert(id);
        }
    }

    QJsonArray out;
    for (const QJsonValue &v : messages) {
        const QJsonObject o = v.toObject();
        if (isAssistantWithToolCalls(o)) {
            const QSet<QString> ids = toolCallIds(o);
            if (ids.isEmpty()) continue;
            bool complete = true;
            for (const QString &id : ids) {
                if (!answered.contains(id)) { complete = false; break; }
            }
            if (!complete) continue;
        }
        out.append(v);
    }
    return out;
}

static QJsonArray dropOrphanToolMessages(const QJsonArray &messages)
{
    QSet<QString> seen;
    QJsonArray out;
    for (const QJsonValue &v : messages) {
        const QJsonObject o = v.toObject();
        if (isAssistantWithToolCalls(o))
            seen.unite(toolCallIds(o));
        if (isToolMessage(o)) {
            const QString id = o.value(QStringLiteral("tool_call_id")).toString();
            if (id.isEmpty() || !seen.contains(id))
                continue;
        }
        out.append(v);
    }
    return out;
}

QJsonArray LlamaAgentBackend::sanitizeApiMessagesForWire(const QJsonArray &messages,
                                                         const QString &modelId)
{
    if (messages.isEmpty()) return messages;

    QJsonArray cleaned;
    for (const QJsonValue &v : messages) {
        const QJsonObject o = v.toObject();
        const QString role = o.value(QStringLiteral("role")).toString();
        const QString content = o.value(QStringLiteral("content")).toString().trimmed();
        if (role == QLatin1String("assistant") &&
            content.startsWith(QStringLiteral("[error: Error transferring ")))
            continue;
        cleaned.append(v);
    }

    QJsonArray out = dropDanglingAssistantToolCalls(cleaned);
    out = dropOrphanToolMessages(out);

    bool hasUser = false;
    for (const QJsonValue &v : out) {
        if (v.toObject().value(QStringLiteral("role")).toString() == QLatin1String("user")) {
            hasUser = true;
            break;
        }
    }
    if (!hasUser) {
        for (const QJsonValue &v : cleaned) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("role")).toString() == QLatin1String("user")) {
                QJsonArray anchored;
                anchored.append(o);
                for (const QJsonValue &kept : std::as_const(out))
                    anchored.append(kept);
                out = anchored;
                break;
            }
        }
    }

    bool sawFirstSystem = false;
    for (int i = 0; i < out.size(); ++i) {
        QJsonObject o = out[i].toObject();
        if (o.value(QStringLiteral("role")).toString() != QLatin1String("system"))
            continue;
        if (i == 0 && !sawFirstSystem) {
            sawFirstSystem = true;
            continue;
        }
        o[QStringLiteral("role")] = midConversationNoteRole(modelId);
        out.replace(i, o);
    }

    return out;
}

LlamaAgentBackend::LlamaAgentBackend(QObject *parent) : IAgentBackend(parent)
{
    m_nam = new QNetworkAccessManager(this);
    m_toolWatchdog = new QTimer(this);
    m_toolWatchdog->setSingleShot(true);
    connect(m_toolWatchdog, &QTimer::timeout, this, [this]() {
        if (m_execCallId.isEmpty()) return;
        const QString callId = m_execCallId;
        const QString tool = m_execToolName;
        const QString args = m_execArguments;
        m_execCallId.clear();
        ++m_toolFail;
        ++m_stagnationEvents;
        const QString message = QStringLiteral(
            "[tool-timeout: '%1' no produjo resultado ni actividad durante %2 s; "
            "worker reiniciado y turno cerrado de forma segura]")
                .arg(tool).arg(m_execWatchdogSec);
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("tool_timeout"),
                              {{QStringLiteral("tool"), tool},
                               {QStringLiteral("toolCallId"), callId},
                               {QStringLiteral("semanticKey"),
                                AgentProgressGovernor::semanticKey(tool, args)},
                               {QStringLiteral("idleSec"), m_execWatchdogSec}});
        appendToolResult(callId, tool, message);
        // A benchmark backend is ephemeral and is about to be stopped by its
        // owner after this turn.  Do not tear down its QThread synchronously
        // from the GUI thread: if the tool is still inside a native call,
        // terminate()+delete can destroy Qt objects while their worker stack
        // is unwinding (the daemon used to crash in Qt6Core here).  The stale
        // result is ignored because m_execCallId was cleared above, and the
        // normal backend stop path performs the orderly teardown.
        if (!m_ephemeralSessions)
            restartWorkerAfterTimeout();
        finishTurn(message, true, QStringLiteral("failed"));
    });
    m_runLeaseTimer = new QTimer(this);
    m_runLeaseTimer->setInterval(30000);
    connect(m_runLeaseTimer, &QTimer::timeout, this, [this]() {
        if (m_runId.isEmpty() || m_runLeaseToken.isEmpty()) return;
        QString error;
        if (!m_agentRunStore.heartbeat(m_runId, m_runLeaseToken, kRunLeaseMs, &error)) {
            emit logAppended(QStringLiteral("[run] heartbeat rechazado: %1\n").arg(error));
            m_runLeaseTimer->stop();
        }
    });
    connect(this, &IAgentBackend::turnFinished, this, [this]() {
        if (!m_isSessionRuntime) pumpSessionRuntimes();
    });
}

LlamaAgentBackend::~LlamaAgentBackend() { stop(); teardownWorker(); }

// ───────────────────────────── Ciclo de vida ─────────────────────────────
void LlamaAgentBackend::start(const AgentContext &ctx)
{
    // A backend can be reused after a server/profile swap. Tear down the
    // previous external lane before adopting the new profile; the builtin path
    // remains the default when the module is absent.
    failExternalWorkerCalls(QStringLiteral(
        "[worker externo detenido por cambio de perfil]"));
    stopHarnessWorker();
    m_ctx = ctx;
    m_harnessWorkerModule = ctx.harnessWorker;
    m_harnessEngineId = HarnessEngine::effectiveId(
        HarnessRuntimeModule{ctx.harnessEngineId, ctx.harnessEngineVersion,
                              QStringLiteral("legacy"), false, true});
    m_harnessEngineVersion = m_harnessEngineId == QLatin1String("legacy")
                                 ? 1 : qMax(1, ctx.harnessEngineVersion);
    m_harnessProfileId = ctx.harnessProfileId;
    m_harnessSpecHash = ctx.harnessSpecHash;
    m_harnessActivationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Sin proyecto válido NO se cae al home: con auto-aprobación las tools
    // trabajarían sobre C:\Users\<user> (.ssh, .claude, perfiles). Workspace
    // aislado y aviso visible; el cwd real de la sesión lo repone loadFromDisk.
    m_cwd = safeProjectDir(ctx.cwd);
    if (m_cwd.isEmpty()) {
        m_cwd = fallbackWorkspaceDir();
        emit logAppended(QStringLiteral("[proyecto no definido o inseguro (%1): usando "
                                        "workspace aislado %2]\n")
                             .arg(QDir::toNativeSeparators(ctx.cwd.trimmed()),
                                  QDir::toNativeSeparators(m_cwd)));
    }
    if (m_contextPolicy.indexPolicy == QLatin1String("eager")) {
        const QVariantMap state = ContextIndex::refresh(m_cwd);
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("observation"),
                              QJsonObject{{QStringLiteral("source"), QStringLiteral("context_index")},
                                          {QStringLiteral("mode"), QStringLiteral("eager")},
                                          {QStringLiteral("state"), QJsonObject::fromVariantMap(state)}});
    }
    m_running = true;
    m_executionPaused = false;
    m_executionStep = false;
    if (!m_ephemeralSessions)
        loadFromDisk();     // recupera sesiones previas; activa la primera
    ensureSession();        // si no había ninguna, crea una
    configureDurableRunStore();
    emit agentLifecycleEvent(AgentLifecycle::sessionStart(
        m_sessionId, m_cwd, m_correlationId, m_harnessProfileId,
        m_harnessEngineId, m_harnessEngineVersion));
    if (m_harnessEngineId == QLatin1String("next")) {
        QStringList requested;
        for (const QVariant &entry : toolCatalog())
            requested << entry.toMap().value(QStringLiteral("name")).toString();
        QStringList allowed = requested;
        for (const QString &disabled : m_disabledTools)
            allowed.removeAll(disabled);
        m_harnessCapabilities = HarnessCapabilitySnapshot::admit(
            m_harnessActivationId, m_harnessEngineId, m_harnessProfileId,
            m_harnessEngineVersion, requested, allowed);
        configureHarnessEventLog();
        QFile capabilityFile(QDir(storageDir()).filePath(QStringLiteral("capabilities.json")));
        if (capabilityFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
            capabilityFile.write(QJsonDocument(m_harnessCapabilities.toJson()).toJson());
        m_harnessEventLog.append(QStringLiteral("backend/start"),
                                 QJsonObject{{QStringLiteral("engine"), m_harnessEngineId},
                                             {QStringLiteral("version"), m_harnessEngineVersion},
                                             {QStringLiteral("profileId"), m_harnessProfileId},
                                             {QStringLiteral("specHash"), m_harnessSpecHash}});
    }
    // Al reactivar esta misma instancia, las sesiones ya están en memoria:
    // loadFromDisk()/ensureSession() no emiten señales. Republicar siempre el
    // estado evita que el espejo de AppController quede vacío al arrancar.
    emit sessionsChanged();
    emit messagesChanged();
    fetchContextLimit();
    ensureWorker();         // hilo worker (persiste toda la vida del backend)
    // (Re)configurar el worker en cada start (async, no bloquea UI).
    QMetaObject::invokeMethod(m_worker, "setLivePreviewEnabled", Qt::QueuedConnection,
                              Q_ARG(bool, m_livePreviewEnabled));
    QMetaObject::invokeMethod(m_worker, "setConfined", Qt::QueuedConnection,
                              Q_ARG(bool, m_approvalMode != QLatin1String("super")));
    QMetaObject::invokeMethod(m_worker, "setServerBaseUrl", Qt::QueuedConnection,
                              Q_ARG(QString, m_ctx.serverBaseUrl));
    QMetaObject::invokeMethod(m_worker, "setAuxiliaryServerConfig", Qt::QueuedConnection,
                              Q_ARG(QString, m_auxiliaryUrl),
                              Q_ARG(QString, m_auxiliaryEmbeddingModel),
                              Q_ARG(QString, m_auxiliaryRerankModel),
                              Q_ARG(QString, m_auxiliaryBearer));
    QMetaObject::invokeMethod(m_worker, "setSessionId", Qt::QueuedConnection,
                              Q_ARG(QString, m_sessionId));
    QMetaObject::invokeMethod(m_worker, "setTeacherConfig", Qt::QueuedConnection,
                              Q_ARG(QString, m_teacherUrl), Q_ARG(QString, m_teacherModel),
                              Q_ARG(QString, m_teacherKey));
    QMetaObject::invokeMethod(m_worker, "setMasterCli", Qt::QueuedConnection,
                              Q_ARG(QString, m_masterKind), Q_ARG(QString, m_masterCliName),
                              Q_ARG(QString, m_masterCliPath), Q_ARG(bool, m_masterApplyEdits),
                              Q_ARG(int, m_masterTimeoutS));
    QMetaObject::invokeMethod(m_worker, "setMasterChain", Qt::QueuedConnection,
                              Q_ARG(QVariantList, m_masterChain));
    QMetaObject::invokeMethod(m_worker, "setHoneyHandoff", Qt::QueuedConnection,
                              Q_ARG(bool, m_directives.contains(QStringLiteral("honey"))));
    QMetaObject::invokeMethod(m_worker, "setMailAccounts", Qt::QueuedConnection,
                              Q_ARG(QVariantList, m_mailAccounts));
    QMetaObject::invokeMethod(m_worker, "setWebProviders", Qt::QueuedConnection,
                              Q_ARG(QVariantList, m_webProviders));
    QMetaObject::invokeMethod(m_worker, "setPortableSkillPolicy", Qt::QueuedConnection,
                              Q_ARG(QStringList, m_skillPolicy.include),
                              Q_ARG(QStringList, m_skillPolicy.exclude),
                              Q_ARG(bool, m_skillPolicy.set));
    QMetaObject::invokeMethod(m_worker, "initServers", Qt::QueuedConnection,
                              Q_ARG(QVariantList, m_mcpConfig), Q_ARG(QString, m_cwd));
    startHarnessWorker();
    emit runningChanged();
    emit logAppended(QStringLiteral("[LlamaAgent backend listo · cwd: %1]\n")
                         .arg(QDir::toNativeSeparators(m_cwd)));
}

void LlamaAgentBackend::applyHeaders(QNetworkRequest &req) const
{
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    if (!m_ctx.apiKey.isEmpty())
        req.setRawHeader(QByteArrayLiteral("Authorization"),
                         QByteArrayLiteral("Bearer ") + m_ctx.apiKey.toUtf8());
}

void LlamaAgentBackend::fetchContextLimit()
{
    // Provider cloud: no expone /props → usar el ctx fijado en el perfil (o default).
    if (!m_ctx.apiKey.isEmpty()) {
        m_ctxLimit = m_ctx.ctxOverride > 0 ? m_ctx.ctxOverride : 32768;
        emit contextUsage(0, m_ctxLimit);
        return;
    }
    auto *reply = m_nam->get(QNetworkRequest(QUrl(m_ctx.serverBaseUrl + QStringLiteral("/props"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        int nctx = root.value(QStringLiteral("default_generation_settings"))
                       .toObject().value(QStringLiteral("n_ctx")).toInt(-1);
        if (nctx < 0) nctx = root.value(QStringLiteral("n_ctx")).toInt(-1);
        if (nctx > 0) { m_ctxLimit = nctx; emit contextUsage(0, m_ctxLimit); }
        // chat_template: señal directa de si el GGUF sabe emitir tool calls.
        // llama.cpp nuevo publica además `chat_template_caps`, que es lo que el
        // server realmente evaluó del template: si está, manda sobre el regex.
        bool have = false;
        const bool tools = toolSupportFromProps(root, &have);
        emit chatTemplateDetected(have, tools);
    });
}

// Tokens estimados de un único mensaje de la API (content + args de tool_calls).
static int msgTokensOf(const QJsonObject &m)
{
    int chars = m.value(QStringLiteral("content")).toString().size();
    for (const QJsonValue &partValue : m.value(QStringLiteral("content")).toArray()) {
        const QJsonObject part = partValue.toObject();
        chars += part.value(QStringLiteral("text")).toString().size();
        if (part.value(QStringLiteral("type")).toString() == QLatin1String("image_url"))
            chars += 4096; // aproximación conservadora del presupuesto visual
    }
    int t = (chars + 3) / 4;
    const QJsonArray tcs = m.value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue &v : tcs) {
        const QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
        t += (fn.value(QStringLiteral("name")).toString().size()
              + fn.value(QStringLiteral("arguments")).toString().size() + 3) / 4;
    }
    return t + 4;   // overhead por mensaje (roles, separadores del template)
}

static int toolSchemaTokensOf(const QJsonArray &tools)
{
    if (tools.isEmpty()) return 0;

    // Qwen/llama.cpp templates expand OpenAI tool schemas into a verbose prompt
    // block. A raw char/4 estimate is too optimistic for 4k contexts.
    const int jsonChars = QString::fromUtf8(
        QJsonDocument(tools).toJson(QJsonDocument::Compact)).size();
    return ((jsonChars + 2) / 3) + (tools.size() * 96) + 256;
}

int LlamaAgentBackend::estimateApiTokens() const
{
    return estimateMessageTokens(m_apiMessages);
}

QString LlamaAgentBackend::suggestSessionTitle(const QString &firstPrompt)
{
    static const QSet<QString> ignored{
        QStringLiteral("a"), QStringLiteral("al"), QStringLiteral("and"),
        QStringLiteral("con"), QStringLiteral("de"), QStringLiteral("del"),
        QStringLiteral("el"), QStringLiteral("en"), QStringLiteral("for"),
        QStringLiteral("la"), QStringLiteral("las"), QStringLiteral("lo"),
        QStringLiteral("los"), QStringLiteral("me"), QStringLiteral("mi"),
        QStringLiteral("para"), QStringLiteral("podés"), QStringLiteral("puedes"),
        QStringLiteral("please"), QStringLiteral("por"),
        QStringLiteral("que"), QStringLiteral("quiero"), QStringLiteral("the"),
        QStringLiteral("to"), QStringLiteral("un"), QStringLiteral("una"),
        QStringLiteral("un"), QStringLiteral("una"), QStringLiteral("y"),
        QStringLiteral("you")};
    const QStringList words = firstPrompt.split(
        QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_-]+")), Qt::SkipEmptyParts);
    QStringList title;
    for (const QString &word : words) {
        if (ignored.contains(word.toLower())) continue;
        title.append(word.left(32));
        if (title.size() == 3) break;
    }
    if (title.isEmpty()) return QStringLiteral("Sesión");
    QString result = title.join(QLatin1Char(' '));
    result[0] = result.at(0).toUpper();
    return result;
}

int LlamaAgentBackend::estimateMessageTokens(const QJsonArray &messages) const
{
    int total = 0;
    for (const QJsonValue &v : messages)
        total += msgTokensOf(v.toObject());
    return total;
}

void LlamaAgentBackend::appendApiMessage(const QJsonObject &message)
{
    m_apiMessages.append(message);
    m_transcriptMessages.append(message);
}

void LlamaAgentBackend::replaceSystemMessage(const QJsonObject &message)
{
    if (m_apiMessages.isEmpty()) m_apiMessages.append(message);
    else m_apiMessages.replace(0, message);
    if (m_transcriptMessages.isEmpty()) m_transcriptMessages.append(message);
    else m_transcriptMessages.replace(0, message);
}

void LlamaAgentBackend::refreshSystemPromptContext()
{
    if (m_apiMessages.isEmpty()) return;
    QJsonObject sys = m_apiMessages.first().toObject();
    if (sys.value(QStringLiteral("role")).toString() != QLatin1String("system")) return;
    const QString rebuilt = buildSystemPrompt();
    if (sys.value(QStringLiteral("content")).toString() == rebuilt) return;
    sys[QStringLiteral("content")] = rebuilt;
    replaceSystemMessage(sys);
}

bool LlamaAgentBackend::isProtectedContextMessage(const QJsonObject &message) const
{
    if (message.value(QStringLiteral("_lc_protected")).toBool()) return true;
    if (message.value(QStringLiteral("role")).toString() == QLatin1String("system"))
        return true;
    static const QSet<QString> protectedTools{
        QStringLiteral("write_file"), QStringLiteral("edit_file"),
        QStringLiteral("run_shell"), QStringLiteral("task"),
        QStringLiteral("skill_load"), QStringLiteral("memory"),
        QStringLiteral("context_checkpoint"), QStringLiteral("worker_call")
    };
    for (const QJsonValue &v : message.value(QStringLiteral("tool_calls")).toArray()) {
        const QString name = v.toObject().value(QStringLiteral("function")).toObject()
                                 .value(QStringLiteral("name")).toString();
        if (protectedTools.contains(name)) return true;
    }
    return false;
}

// Poda barata y determinista. Mantiene la estructura assistant.tool_calls → tool,
// pero reemplaza resultados repetidos y argumentos fallidos antiguos por recibos.
// Nunca toca el transcript persistido.
int LlamaAgentBackend::pruneWorkingContext()
{
    if (!m_contextPolicy.prune) return 0;
    if (m_apiMessages.size() < 8) return 0;
    const int beforeTokens = estimateApiTokens();
    QHash<QString, int> newestSignature;
    int changed = 0;
    for (int i = m_apiMessages.size() - 1; i >= 0; --i) {
        QJsonObject assistant = m_apiMessages.at(i).toObject();
        const QJsonArray calls = assistant.value(QStringLiteral("tool_calls")).toArray();
        if (calls.isEmpty() || isProtectedContextMessage(assistant)) continue;
        QJsonArray rewrittenCalls = calls;
        bool assistantChanged = false;
        for (int ci = 0; ci < calls.size(); ++ci) {
            QJsonObject call = calls.at(ci).toObject();
            QJsonObject fn = call.value(QStringLiteral("function")).toObject();
            const QString id = call.value(QStringLiteral("id")).toString();
            const QString name = fn.value(QStringLiteral("name")).toString();
            const QString args = fn.value(QStringLiteral("arguments")).toString();
            const QString signature = name + QLatin1Char('\n') + args;
            int resultIndex = -1;
            for (int j = i + 1; j < m_apiMessages.size(); ++j) {
                const QJsonObject candidate = m_apiMessages.at(j).toObject();
                if (candidate.value(QStringLiteral("role")).toString() != QLatin1String("tool"))
                    break;
                if (candidate.value(QStringLiteral("tool_call_id")).toString() == id) {
                    resultIndex = j;
                    break;
                }
            }
            if (resultIndex < 0) continue;
            QJsonObject result = m_apiMessages.at(resultIndex).toObject();
            const QString content = result.value(QStringLiteral("content")).toString();
            const bool error = content.contains(QStringLiteral("error"), Qt::CaseInsensitive)
                               || content.contains(QStringLiteral("fall"), Qt::CaseInsensitive)
                               || content.contains(QStringLiteral("rechaz"), Qt::CaseInsensitive);
            if (error && m_apiMessages.size() - resultIndex > 4 && args.size() > 256) {
                fn[QStringLiteral("arguments")] =
                    QStringLiteral("{\"_pruned\":\"argumentos antiguos de llamada fallida\"}");
                call[QStringLiteral("function")] = fn;
                rewrittenCalls.replace(ci, call);
                assistantChanged = true;
                ++changed;
            } else if (newestSignature.contains(signature)) {
                result[QStringLiteral("content")] =
                    QStringLiteral("[resultado repetido podado; usar la llamada equivalente más reciente]");
                m_apiMessages.replace(resultIndex, result);
                ++changed;
            } else {
                newestSignature.insert(signature, resultIndex);
            }
        }
        if (assistantChanged) {
            assistant[QStringLiteral("tool_calls")] = rewrittenCalls;
            m_apiMessages.replace(i, assistant);
        }
    }
    if (changed > 0) {
        ++m_contextPrunedMessages;
        m_contextPrunedTokens += qMax(0, beforeTokens - estimateApiTokens());
        emit logAppended(QStringLiteral("[contexto: poda determinista aplicó %1 reemplazos]\n")
                             .arg(changed));
        emit contextManaged(estimateApiTokens(), estimateMessageTokens(m_transcriptMessages),
                            m_contextPrunedTokens, m_contextPrunedMessages);
    }
    return changed;
}

QString LlamaAgentBackend::normalizeCompactionSummary(const QString &summary) const
{
    QString cleaned = stripThinkForContext(summary).trimmed();
    if (cleaned.startsWith(QStringLiteral("```"))) {
        const int firstNl = cleaned.indexOf(QLatin1Char('\n'));
        const int lastFence = cleaned.lastIndexOf(QStringLiteral("```"));
        if (firstNl >= 0 && lastFence > firstNl)
            cleaned = cleaned.mid(firstNl + 1, lastFence - firstNl - 1).trimmed();
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(cleaned.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && doc.isObject())
        return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    return cleaned.left(12000);
}

// Serializa un mensaje de la API a texto legible para el resumen.
static QString serializeMsgForSummary(const QJsonObject &m)
{
    const QString role = m.value(QStringLiteral("role")).toString();
    QString out;
    const QString content = m.value(QStringLiteral("content")).toString();
    if (!content.isEmpty())
        out += role + QStringLiteral(": ") + content.left(8000) + QLatin1Char('\n');
    const QJsonArray tcs = m.value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue &v : tcs) {
        const QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
        out += QStringLiteral("  → tool %1(%2)\n")
                   .arg(fn.value(QStringLiteral("name")).toString(),
                        fn.value(QStringLiteral("arguments")).toString().left(2000));
    }
    return out;
}

// Decide si hay un tramo intermedio a compactar y devuelve [head, keepFrom).
bool LlamaAgentBackend::planCompaction(int &head, int &keepFrom) const
{
    if (!m_contextPolicy.compaction) return false;
    if (m_ctxLimit <= 0) return false;
    const int n = m_apiMessages.size();
    if (n <= 4) return false;

    const int outReserve   = qMin(32768, m_ctxLimit / 4);
    // En modo text-tools (fallback headless) las tools NO viajan como payload
    // `tools` aparte: van embebidas por NOMBRE en el system prompt, y eso ya lo
    // cuenta estimateApiTokens() vía message[0]. Reservar acá el esquema OpenAI
    // completo (~miles de tokens) sería doble conteo y colapsaría el budget →
    // compactación en CADA turno (bug: la Task se rompía tras 4-5 tools). Sólo
    // reservamos el esquema cuando efectivamente se manda como payload nativo.
    const int toolsReserve = usingTextTools() ? 0
                                              : toolSchemaTokensOf(buildToolSchemas());
    const int budget = qMax(256, int(m_ctxLimit * m_contextPolicy.compactionTrigger)
                                     - outReserve - toolsReserve);
    if (budget <= 0) return false;

    if (estimateApiTokens() <= budget) return false;

    head = qMin(2, n);                           // system[0] + objetivo[1]
    const int tailBudget = int(budget * m_contextPolicy.tailRatio);  // margen de crecimiento
    int acc = 0; keepFrom = n;
    for (int i = n - 1; i >= head; --i) {
        acc += msgTokensOf(m_apiMessages[i].toObject());
        if (acc > tailBudget) { keepFrom = i + 1; break; }
        keepFrom = i;
    }
    // No empezar la cola con un 'tool' huérfano (debe seguir a su assistant.tool_calls).
    while (keepFrom < n
           && m_apiMessages[keepFrom].toObject().value(QStringLiteral("role")).toString()
              == QLatin1String("tool"))
        ++keepFrom;

    if ((keepFrom - head) <= 0) return false;

    // Compactar invalida el prompt-cache desde `head`. Sólo conviene si el tramo
    // liberado se amortiza en las siguientes iteraciones. Bajo presión dura
    // (>=97% del presupuesto real) se prioriza evitar overflow.
    int removable = 0;
    for (int i = head; i < keepFrom; ++i)
        removable += msgTokensOf(m_apiMessages.at(i).toObject());
    const int current = estimateApiTokens();
    const int expectedSummary = qMin(2048, qMax(128, removable / 5));
    const int savings = qMax(0, removable - expectedSummary);
    const int cacheRebuild = qMax(current, m_lastPromptTokens);
    const bool hardPressure = current >= int(m_ctxLimit * 0.97) - outReserve - toolsReserve;
    return hardPressure || savings * 3 > cacheRebuild;
}

// Reemplaza m_apiMessages[head..keepFrom) por un único mensaje de resumen.
// Si summary está vacío (falló el modelo), usa una nota de poda como fallback.
void LlamaAgentBackend::applyCompaction(int head, int keepFrom, const QString &summary)
{
    const int n = m_apiMessages.size();
    if (head < 0 || keepFrom > n || keepFrom <= head) return;
    const int dropped = keepFrom - head;
    const int before = estimateApiTokens();

    QString body = normalizeCompactionSummary(summary);
    const bool summarized = !body.isEmpty();
    if (!summarized)
        body = QStringLiteral("[Se omitieron %1 mensajes intermedios para no exceder el "
                              "contexto (resumen no disponible).]").arg(dropped);

    QJsonArray neu;
    for (int i = 0; i < head; ++i) neu.append(m_apiMessages[i]);
    neu.append(QJsonObject{
        {QStringLiteral("role"), midConversationNoteRole(m_ctx.modelId)},
        // Ya no es role=system, así que no lo cubre el atajo de
        // isProtectedContextMessage: marcarlo explícito.
        {QStringLiteral("_lc_protected"), true},
        {QStringLiteral("content"),
         QStringLiteral("[Resumen del contexto previo (%1 mensajes compactados para no "
                        "exceder n_ctx=%2)]:\n%3").arg(dropped).arg(m_ctxLimit, 0, 10).arg(body)}});
    for (int i = keepFrom; i < n; ++i) neu.append(m_apiMessages[i]);

    m_apiMessages = neu;
    const int after = estimateApiTokens();
    m_contextPrunedTokens += qMax(0, before - after);
    // Anti-loop: si el resumen no redujo tokens de forma apreciable (el tramo
    // compactable es chico y lo pesado es el head protegido: system+objetivo),
    // contá el intento fallido. Tras 2 seguidos, runCompletion deja de compactar
    // y sigue: el prompt igual entra en n_ctx real (el "over budget" es una
    // reserva conservadora, no un overflow del server).
    if (after < before - 8) {
        m_compactStall = 0;
        ++m_contextPrunedMessages;
    } else {
        ++m_compactStall;
    }
    emit logAppended(QStringLiteral("[compactación %1: %2 msgs · ~%3→%4 tok (n_ctx=%5)]\n")
                         .arg(summarized ? QStringLiteral("vía modelo") : QStringLiteral("poda"))
                         .arg(dropped).arg(before).arg(after).arg(m_ctxLimit));
    emit contextUsage(after, m_ctxLimit);
    emit contextManaged(after, estimateMessageTokens(m_transcriptMessages),
                        m_contextPrunedTokens, m_contextPrunedMessages);
}

// Dispara el request de resumen del tramo [head, keepFrom). Al completar,
// aplica la compactación y reanuda el turno (runCompletion).
void LlamaAgentBackend::startCompaction(int head, int keepFrom)
{
    QString convo;
    for (int i = head; i < keepFrom; ++i)
        convo += serializeMsgForSummary(m_apiMessages[i].toObject());

    const QString sys = QStringLiteral(
        "Sos un compactador de memoria de trabajo. Convertí el tramo en UN objeto JSON "
        "válido y compacto con estas claves: goal:string, completed:string[], "
        "decisions:string[], artifacts:[{path:string,change:string,verified:bool}], "
        "failed_approaches:string[], open_questions:string[], next_action:string, "
        "evidence:string[]. Preservá restricciones del usuario, rutas, cambios no "
        "verificados, resultados de tests/build y errores todavía relevantes. Omití "
        "salidas repetidas y detalles ya cerrados. No inventes. Respondé sólo JSON.");

    QJsonObject payload{
        {QStringLiteral("model"), m_ctx.modelId.isEmpty() ? QStringLiteral("local") : m_ctx.modelId},
        {QStringLiteral("messages"), QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), sys}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), convo}}}},
        {QStringLiteral("stream"), false},
        {QStringLiteral("temperature"), 0.2},
        {QStringLiteral("max_tokens"), qMin(2048, m_ctxLimit > 0 ? m_ctxLimit / 8 : 2048)},
        {QStringLiteral("cache_prompt"), true}
    };

    const QString url = m_ctx.serverBaseUrl + QStringLiteral("/v1/chat/completions");
    QNetworkRequest req((QUrl(url)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));

    applyHeaders(req);

    m_compacting = true;
    emit logAppended(QStringLiteral("[compactando contexto vía modelo: resumiendo %1 mensajes…]\n")
                         .arg(keepFrom - head));

    m_compactReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_compactReply, &QNetworkReply::finished, this, [this, head, keepFrom]() {
        QNetworkReply *r = m_compactReply;
        if (!r) return;                          // abortado por cancel/stop
        m_compactReply = nullptr;
        m_compacting = false;
        r->deleteLater();

        if (!m_running) return;                  // backend detenido durante la compactación

        QString summary;
        if (r->error() == QNetworkReply::NoError) {
            const QJsonObject root = QJsonDocument::fromJson(r->readAll()).object();
            const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
            if (!choices.isEmpty())
                summary = choices.first().toObject()
                              .value(QStringLiteral("message")).toObject()
                              .value(QStringLiteral("content")).toString();
            summary = stripThinkForContext(summary);
        }
        // summary vacío → applyCompaction usa fallback de poda.
        applyCompaction(head, keepFrom, summary);

        if (m_running) runCompletion();          // reanudar el turno ya compactado
    });
}

void LlamaAgentBackend::consolidateMemory(bool recoveredSkill)
{
    // Consolidación de background: extrae hechos DURABLES del transcript actual y
    // los guarda en la memoria estructurada (source="consolidation"). Captura por la
    // ausencia de save/link en vivo del modelo chico. Async, no bloquea la UI.
    if (m_ephemeralSessions) return;                 // sesiones efímeras: no persistir
    if (!m_memoryPolicy.consolidateOnLeave) return;  // apagado por el perfil (modulo memory)
    if (m_consolidateReply) return;                  // ya hay una corriendo
    if (m_cwd.isEmpty() || m_sessionId.isEmpty()) return;

    const int n = m_apiMessages.size();
    if (n < 4) return;                               // muy poco para extraer algo útil
    if (m_consolidatedLen.value(m_sessionId) >= n) return;  // sin novedades desde la última

    // Snapshot: la sesión puede cambiar/limpiarse mientras la request está en vuelo.
    const QString cwd = m_cwd;
    const QString sid = m_sessionId;
    QString convo;
    for (int i = 0; i < n; ++i)
        convo += serializeMsgForSummary(m_apiMessages[i].toObject());
    if (convo.trimmed().isEmpty()) return;

    QString sys = QStringLiteral(
        "Sos un consolidador de memoria de un agente de coding. Leé la conversación y "
        "extraé SOLO hechos DURABLES que sirvan en futuras sesiones de ESTE proyecto: "
        "preferencias del usuario, decisiones de diseño tomadas, convenciones, datos no "
        "obvios del repo, bugs conocidos. NO incluyas pasos efímeros, charla, ni cosas "
        "deducibles leyendo el código. Respondé SOLO con un array JSON; cada item: "
        "{\"content\":string, \"scope\":\"project\"|\"personal\", "
        "\"type\":\"preference\"|\"decision\"|\"fact\"|\"bug\"|\"skill\", \"confidence\":0..1, "
        "\"importance\":0..1, \"surprise\":0..1, "
        "\"verification\":\"user\"|\"test\"|\"tool\"|\"inferred\"}. "
        "Si no hay nada durable, respondé []. Máximo 10 items, cada content una sola frase.");
    if (recoveredSkill) {
        sys += QStringLiteral(
            " Este turno tuvo fallos de herramientas y luego progreso exitoso antes de "
            "terminar. Extraé como máximo 3 items type=\"skill\" que expliquen la habilidad "
            "reutilizable aprendida: precondición o síntoma, estrategia que finalmente "
            "funcionó y cómo verificarla. Generalizá sólo cuando la evidencia lo permita; "
            "no copies intentos fallidos como receta, rutas absolutas, secretos ni datos "
            "efímeros. Para estos items usá verification=\"tool\".");
    }

    QJsonObject payload{
        {QStringLiteral("model"), m_ctx.modelId.isEmpty() ? QStringLiteral("local") : m_ctx.modelId},
        {QStringLiteral("messages"), QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")}, {QStringLiteral("content"), sys}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), convo}}}},
        {QStringLiteral("stream"), false},
        {QStringLiteral("temperature"), 0.1},
        {QStringLiteral("max_tokens"), qMin(1024, m_ctxLimit > 0 ? m_ctxLimit / 8 : 1024)},
        {QStringLiteral("cache_prompt"), true}
    };

    const QString url = m_ctx.serverBaseUrl + QStringLiteral("/v1/chat/completions");
    QNetworkRequest req((QUrl(url)));
    applyHeaders(req);

    m_consolidatedLen[sid] = n;   // marcar ya (evita re-disparos aunque falle)
    emit logAppended(QStringLiteral("[consolidando memoria: analizando %1 mensajes…]\n").arg(n));

    m_consolidateReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_consolidateReply, &QNetworkReply::finished, this, [this, cwd, sid]() {
        QNetworkReply *r = m_consolidateReply;
        if (!r) return;
        m_consolidateReply = nullptr;
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) return;

        const QJsonObject root = QJsonDocument::fromJson(r->readAll()).object();
        const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) return;
        QString out = choices.first().toObject().value(QStringLiteral("message"))
                          .toObject().value(QStringLiteral("content")).toString();
        out = stripThinkForContext(out).trimmed();

        // Aislar el array JSON (el modelo puede envolverlo en texto/```).
        const int lb = out.indexOf(QLatin1Char('['));
        const int rb = out.lastIndexOf(QLatin1Char(']'));
        if (lb < 0 || rb <= lb) return;
        const QJsonArray facts = QJsonDocument::fromJson(
            out.mid(lb, rb - lb + 1).toUtf8()).array();
        if (facts.isEmpty()) return;

        int saved = 0;
        int discarded = 0;
        int partial = 0;
        QVector<QPair<QString, QString>> acceptedFacts;
        for (const QJsonValue &fv : facts) {
            const QJsonObject f = fv.toObject();
            const QString content = f.value(QStringLiteral("content")).toString().trimmed();
            if (content.isEmpty()) continue;
            const QString scope = f.value(QStringLiteral("scope"))
                                      .toString(QStringLiteral("project")).trimmed().toLower();
            const QString type = f.value(QStringLiteral("type"))
                                     .toString(QStringLiteral("fact")).trimmed().toLower();
            const QString modelVerification = f.value(QStringLiteral("verification"))
                                                  .toString(QStringLiteral("inferred"))
                                                  .trimmed().toLower();
            const QVector<MemoryStore::ClaimEvidence> evidence =
                MemoryStore::verifyClaims(cwd, QStringList{content}, cwd, 8000);
            const MemoryStore::ClaimEvidence proof = evidence.isEmpty()
                ? MemoryStore::ClaimEvidence{}
                : evidence.first();
            const bool trusted = modelVerification == QLatin1String("user")
                || modelVerification == QLatin1String("test")
                || modelVerification == QLatin1String("tool");
            if (!trusted && proof.status == QLatin1String("unaccredited")) {
                ++discarded;
                continue;
            }

            double confidence = f.value(QStringLiteral("confidence")).toDouble(0.6);
            QString verification = modelVerification;
            QString source = type == QLatin1String("skill")
                ? QStringLiteral("recovery_learning") : QStringLiteral("consolidation");
            if (!trusted) {
                if (proof.status == QLatin1String("partial")) {
                    confidence = qMin(confidence <= 0.0 ? 0.6 : confidence, 0.55);
                    ++partial;
                } else if (proof.status == QLatin1String("accredited")) {
                    confidence = qMax(confidence, 0.65);
                    verification = QStringLiteral("tool");
                    source += QStringLiteral("+verify_claims");
                }
            }
            MemoryStore::save(cwd, content, scope, type, confidence, source,
                              f.value(QStringLiteral("importance")).toDouble(0.0),
                              f.value(QStringLiteral("surprise")).toDouble(0.0),
                              verification);
            acceptedFacts.append({type, content});
            if (++saved >= 10) break;
        }
        const int inferredLinks = GraphStore::inferConsolidationLinks(
            cwd, acceptedFacts, sid, m_correlationId);
        if (saved > 0 || discarded > 0)
            emit logAppended(QStringLiteral(
                "[memoria consolidada: +%1 hecho(s), %2 descartado(s), %3 parcial(es), %4 edge(s) inferido(s)]\n")
                                 .arg(saved).arg(discarded).arg(partial).arg(inferredLinks));
    });
}

void LlamaAgentBackend::stop()
{
    m_toolWatchdog->stop();
    if (!m_isSessionRuntime) {
        const auto runtimes = m_sessionRuntimes;
        m_sessionRuntimes.clear();
        for (LlamaAgentBackend *runtime : runtimes) {
            if (!runtime) continue;
            runtime->stop();
            runtime->deleteLater();
        }
    }
    // Un restart/swap del llama-server puede ocurrir mientras hay una completion,
    // una tool o una aprobación en vuelo. Antes se abortaban los objetos async,
    // pero nunca se cerraba el turno: la UI conservaba "Pensando..." y los
    // consumidores de turnFinished (Tasks/workflows) esperaban para siempre.
    const bool interruptedTurn = m_reply || m_compactReply || m_compacting
                                 || !m_awaitId.isEmpty() || !m_execCallId.isEmpty()
                                 || !m_pendingCalls.isEmpty() || !m_externalWorkerCalls.isEmpty()
                                 || subsActive()
                                 || m_curAsstIdx >= 0;
    emit desktopActivityChanged(false, QString(), QString());
    failExternalWorkerCalls(QStringLiteral(
        "[worker externo detenido: la llamada no llegó a ejecutarse]"));
    stopHarnessWorker();
    if (m_compactReply) {
        QNetworkReply *cr = m_compactReply; m_compactReply = nullptr;
        cr->disconnect(this); cr->abort(); cr->deleteLater();
    }
    m_compacting = false;
    if (m_warmupReply) {
        QNetworkReply *wr = m_warmupReply; m_warmupReply = nullptr;
        wr->disconnect(this); wr->abort(); wr->deleteLater();
    }
    if (m_consolidateReply) {
        QNetworkReply *cr = m_consolidateReply; m_consolidateReply = nullptr;
        cr->disconnect(this); cr->abort(); cr->deleteLater();
    }
    if (m_reply) {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    m_pendingCalls = {};
    m_awaitId.clear();
    m_awaitCall = {};
    m_execCallId.clear();
    cancelAllSubs();
    if (!m_msgQueue.isEmpty()) { m_msgQueue.clear(); emit queueChanged(); }
    if (interruptedTurn) {
        emit logAppended(QStringLiteral(
            "[turn] interrumpido porque el backend o servidor se detuvo; liberando estado\n"));
        finishTurn(QStringLiteral(
            "[error: el servidor o backend se reinició durante la respuesta; "
            "el turno fue interrumpido y puede reintentarse.]"), false,
                   QStringLiteral("interrupted"));
    } else {
        // Defensa para estados transitorios heredados de builds anteriores.
        releaseWorkClaim();
        setTyping(false);
        m_curAsstIdx = -1;
    }
    saveCurrentSession();
    if (!m_isSessionRuntime) persistIndex();
    // Apagar servers MCP pero mantener vivo el hilo worker (se destruye en ~).
    if (m_worker) QMetaObject::invokeMethod(m_worker, "shutdown", Qt::QueuedConnection);
    if (m_running) { m_running = false; emit runningChanged(); }
}

void LlamaAgentBackend::cancelGeneration()
{
    m_toolWatchdog->stop();
    if (!m_isSessionRuntime) {
        if (LlamaAgentBackend *runtime = viewRuntime()) {
            runtime->cancelGeneration();
            pumpSessionRuntimes();
            emit sessionsChanged();
            return;
        }
    }
    const bool hadTurn = isBusy();
    emit desktopActivityChanged(false, QString(), QString());
    if (m_compactReply) {
        QNetworkReply *cr = m_compactReply;
        m_compactReply = nullptr;
        cr->disconnect(this);
        cr->abort();
        cr->deleteLater();
    }
    m_compacting = false;
    if (m_reply) {
        // abort() emite finished()/readyRead SINCRÓNICAMENTE. Si dejamos m_reply
        // seteado y las conexiones vivas, los handlers de stream re-entran acá
        // (use-after / doble proceso) → crash. Anular y desconectar ANTES de abort.
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    m_pendingCalls = {};
    m_awaitId.clear();
    setTyping(false);
    m_curAsstIdx = -1;
    m_execCallId.clear();   // ignorar resultado de tool tardío
    // Matar el run_shell async en vuelo y cerrar su tarjeta en vivo.
    if (m_worker) QMetaObject::invokeMethod(m_worker, "cancelShell", Qt::QueuedConnection);
    finalizeLiveToolCard(true);
    cancelAllSubs();
    // PARAR = detener todo, incluida la cola de mensajes pendientes.
    if (!m_msgQueue.isEmpty()) { m_msgQueue.clear(); emit queueChanged(); }
    releaseWorkClaim();
    if (!m_isSessionRuntime) {
        pumpSessionRuntimes();
        emit sessionsChanged();
    }
    if (hadTurn)
        finishTurn(QStringLiteral("[turno cancelado por el usuario]"), false,
                   QStringLiteral("cancelled"));
}

void LlamaAgentBackend::ensureSession()
{
    if (!m_sessionId.isEmpty()) return;
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sessionTitle = QStringLiteral("Sesión");
    AgentSession s;
    s.id = m_sessionId;
    s.title = m_sessionTitle;
    s.created = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    s.projectDir = m_cwd;
    s.projectName = QFileInfo(m_cwd).fileName();
    s.harnessEngineId = m_harnessEngineId;
    s.harnessEngineVersion = m_harnessEngineVersion;
    s.harnessProfileId = m_harnessProfileId;
    s.harnessSpecHash = m_harnessSpecHash;
    // Las sesiones efímeras (Tasks/Automatizaciones, sub-agentes) NO se listan ni
    // persisten: corren aisladas y no deben aparecer en el panel de Sesiones.
    if (!m_ephemeralSessions)
        m_sessions.prepend(s.toMap());

    m_apiMessages = QJsonArray{ QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), buildSystemPrompt()}
    } };
    m_transcriptMessages = m_apiMessages;
    m_contextPrunedMessages = 0;
    m_contextPrunedTokens = 0;
    m_messages.clear();
    m_readFingerprints.clear();
    m_checkpoints.clear();
    if (!m_msgQueue.isEmpty()) { m_msgQueue.clear(); emit queueChanged(); }
    if (!m_ephemeralSessions) {
        persistIndex();
        persistSession(m_sessionId);
        emit sessionsChanged();
    }
    emit messagesChanged();
}

void LlamaAgentBackend::configureHarnessEventLog()
{
    if (m_ephemeralSessions || m_sessionId.isEmpty()) return;
    const QString path = QDir(storageDir()).filePath(
        QStringLiteral("events/%1.jsonl").arg(m_sessionId));
    QString error;
    if (!m_harnessEventLog.open(path, &error)) {
        emit logAppended(QStringLiteral("[harness event log deshabilitado: %1]\n").arg(error));
    }
    error.clear();
    if (!m_harnessEffectLedger.open(QDir(storageDir()).filePath(QStringLiteral("effects.jsonl")),
                                    &error)) {
        emit logAppended(QStringLiteral("[harness effect ledger deshabilitado: %1]\n").arg(error));
    }
}

void LlamaAgentBackend::configureDurableRunStore()
{
    if (m_ephemeralSessions || m_sessionId.isEmpty()) return;
    QString error;
    const QString root = QDir(storageDir()).filePath(QStringLiteral("agent_runs"));
    if (!m_agentRunStore.open(root, &error)) {
        emit logAppended(QStringLiteral("[run store deshabilitado: %1]\n").arg(error));
        return;
    }
    if (m_runOwnerId.isEmpty()) {
        m_runOwnerId = QStringLiteral("pid:%1/backend:%2")
            .arg(QCoreApplication::applicationPid())
            .arg(QString::number(reinterpret_cast<quintptr>(this), 16));
    }
    const int recovered = m_agentRunStore.recoverStaleRuns(0, &error);
    if (!error.isEmpty())
        emit logAppended(QStringLiteral("[run recovery parcial: %1]\n").arg(error));
    if (recovered > 0)
        emit logAppended(QStringLiteral(
            "[run recovery: %1 corrida(s) quedaron uncertain; no se reejecutan automáticamente]\n")
                             .arg(recovered));
}

void LlamaAgentBackend::beginDurableRun(const QString &objective)
{
    if (m_ephemeralSessions || m_sessionId.isEmpty() || m_correlationId.isEmpty()
        || !m_runId.isEmpty() || m_agentRunStore.root().isEmpty())
        return;

    QString error;
    const QString accepted = m_agentRunStore.accept(
        m_correlationId, m_sessionId, m_correlationId, m_cwd, objective,
        QJsonObject{}, &error);
    if (accepted.isEmpty()) {
        emit logAppended(QStringLiteral("[run] no se pudo aceptar la corrida: %1\n").arg(error));
        return;
    }
    QString token;
    if (!m_agentRunStore.claim(accepted, m_runOwnerId, kRunLeaseMs, &token, &error)) {
        emit logAppended(QStringLiteral("[run] no se pudo reclamar la corrida: %1\n").arg(error));
        return;
    }
    m_runId = accepted;
    m_runLeaseToken = token;
    if (m_runLeaseTimer) m_runLeaseTimer->start();
    const auto state = QSharedPointer<AgentDurableRunState>::create();
    state->runId = m_runId;
    state->sessionId = m_sessionId;
    state->workspace = m_cwd;
    state->runStoreRoot = m_agentRunStore.root();
    state->leaseToken = token;
    m_runAsyncState = state;
    auto *watcher = new QFutureWatcher<QJsonObject>(this);
    connect(watcher, &QFutureWatcher<QJsonObject>::finished, this,
            [this, watcher, state]() {
                state->beforeSnapshot = watcher->result();
                state->snapshotReady = true;
                watcher->deleteLater();
                if (state->finishRequested)
                    scheduleDurableCapture(state);
            });
    const QString workspace = m_cwd;
    watcher->setFuture(QtConcurrent::run([workspace]() {
        return AgentDeliverableStore::snapshot(workspace);
    }));
    AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("run_started"),
                          QJsonObject{{QStringLiteral("runId"), m_runId},
                                      {QStringLiteral("correlationId"), m_correlationId}});
}

void LlamaAgentBackend::finishDurableRun(const QString &status, const QString &detail)
{
    if (m_runId.isEmpty()) return;
    const QString runId = m_runId;
    const QString token = m_runLeaseToken;
    const auto state = m_runAsyncState;
    if (state) {
        state->status = status;
        state->detail = detail;
        state->finishRequested = true;
        if (!state->captureLeaseTimer) {
            auto *leaseTimer = new QTimer(this);
            state->captureLeaseTimer = leaseTimer;
            leaseTimer->setInterval(30000);
            connect(leaseTimer, &QTimer::timeout, this, [this, state]() {
                if (!state->finishRequested) return;
                AgentRunStore store;
                QString heartbeatError;
                if (!store.open(state->runStoreRoot, &heartbeatError)
                    || !store.heartbeat(state->runId, state->leaseToken,
                                        kRunLeaseMs, &heartbeatError)) {
                    emit logAppended(QStringLiteral(
                        "[run] heartbeat de captura rechazado para %1: %2\n")
                                         .arg(state->runId, heartbeatError));
                    if (state->captureLeaseTimer)
                        state->captureLeaseTimer->stop();
                }
            });
            leaseTimer->start();
        }
        // No publicar completed antes de tener snapshot y manifiesto: si el
        // worker termina durante el snapshot, la corrida queda running por un
        // instante y se cierra junto con la evidencia, o recovery la marca
        // uncertain si el lease expira.
        if (!state->snapshotReady)
            emit logAppended(QStringLiteral(
                "[run] cierre diferido %1: esperando snapshot de integridad\n").arg(runId));
        else
            scheduleDurableCapture(state);

        // El callback conserva `state` y su lease. Liberamos la identidad del
        // runtime para no impedir una nueva solicitud mientras la captura
        // finaliza; si tarda más que el lease, queda uncertain.
        if (m_runLeaseTimer) m_runLeaseTimer->stop();
        m_runId.clear();
        m_runLeaseToken.clear();
        m_runAsyncState.clear();
        return;
    }

    // Defensa para estados antiguos sin state async: nunca presentar una
    // corrida completed sin recibo de entregables.
    const QString safeStatus = status == QLatin1String("completed")
        ? QStringLiteral("failed") : status;
    QString error;
    if (!m_agentRunStore.transition(
            runId, token, safeStatus,
            detail + QStringLiteral(" [cierre endurecido: snapshot no disponible]"),
            QJsonObject{{QStringLiteral("deliverablesError"),
                         QStringLiteral("snapshot inicial no disponible")}}, &error)) {
        // No se reclama de nuevo: si el lease expiró, un efecto puede haber
        // quedado ambiguo y debe permanecer visible como uncertain.
        emit logAppended(QStringLiteral("[run] no se pudo cerrar %1: %2\n").arg(runId, error));
    }
    if (m_runLeaseTimer) m_runLeaseTimer->stop();
    m_runId.clear();
    m_runLeaseToken.clear();
}

void LlamaAgentBackend::scheduleDurableCapture(
    const QSharedPointer<AgentDurableRunState> &state)
{
    if (!state || !state->finishRequested || !state->snapshotReady
        || state->captureStarted)
        return;
    state->captureStarted = true;
    const QString runId = state->runId;
    const QString workspace = state->workspace;
    const QJsonObject beforeSnapshot = state->beforeSnapshot;
    const QString runStoreRoot = state->runStoreRoot;
    auto *watcher = new QFutureWatcher<AgentDurableCaptureResult>(this);
    connect(watcher, &QFutureWatcher<AgentDurableCaptureResult>::finished, this,
            [this, watcher, state, runId, runStoreRoot]() {
                const AgentDurableCaptureResult captured = watcher->result();
                if (state->captureLeaseTimer) {
                    state->captureLeaseTimer->stop();
                    state->captureLeaseTimer->deleteLater();
                    state->captureLeaseTimer = nullptr;
                }
                QJsonObject metadata;
                if (!captured.manifest.isEmpty()) {
                    QJsonObject summary = captured.manifest;
                    summary.remove(QStringLiteral("entries"));
                    summary[QStringLiteral("manifestPath")] = QDir(AgentDeliverableStore::rootDir())
                        .filePath(QStringLiteral("%1/manifest.json").arg(runId));
                    metadata[QStringLiteral("deliverables")] = summary;
                }
                if (!captured.error.isEmpty())
                    metadata[QStringLiteral("deliverablesError")] = captured.error;

                QString terminalStatus = state->status;
                if (captured.manifest.isEmpty()
                    && terminalStatus == QLatin1String("completed")) {
                    terminalStatus = QStringLiteral("failed");
                    metadata[QStringLiteral("deliverablesError")] =
                        captured.error.isEmpty()
                            ? QStringLiteral("manifiesto de entregables vacío")
                            : captured.error;
                }
                AgentRunStore store;
                QString error;
                const bool transitioned = store.open(runStoreRoot, &error)
                    && store.transition(runId, state->leaseToken, terminalStatus,
                                        state->detail, metadata, &error);
                if (!transitioned) {
                    // El lease puede haber expirado durante una captura pesada;
                    // recovery lo dejará uncertain en vez de publicar un cierre
                    // terminal sin evidencia.
                    emit logAppended(QStringLiteral(
                        "[run] no se pudo cerrar %1 con evidencia: %2\n")
                                         .arg(runId, error));
                }
                if (transitioned && !captured.manifest.isEmpty())
                    AgentEventLog::append(state->workspace, state->sessionId,
                                          QStringLiteral("deliverables_captured"),
                                          QJsonObject{{QStringLiteral("runId"), runId},
                                                      {QStringLiteral("changedCount"),
                                                       captured.manifest.value(QStringLiteral("changedCount"))}});
                watcher->deleteLater();
            });
    watcher->setFuture(QtConcurrent::run([runId, workspace, beforeSnapshot]() {
        AgentDurableCaptureResult result;
        if (workspace.isEmpty() || beforeSnapshot.isEmpty()) {
            result.error = QStringLiteral("snapshot inicial no disponible; no se copian entregables");
            return result;
        }
        result.manifest = AgentDeliverableStore::capture(
            runId, workspace, beforeSnapshot, &result.error);
        return result;
    }));
}

QString LlamaAgentBackend::memoryFilePath(const QString &cwd)
{
    return QDir::cleanPath(cwd + QStringLiteral("/.llamacode/memory.md"));
}

QString LlamaAgentBackend::safeProjectDir(const QString &dir)
{
    const QString trimmed = dir.trimmed();
    if (trimmed.isEmpty()) return QString();
    const QFileInfo info(trimmed);
    if (!info.isDir()) return QString();
    const QString abs = QDir::cleanPath(info.absoluteFilePath());
    if (QDir(abs).isRoot()) return QString();
    const QString home = QDir::cleanPath(QDir::homePath());
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    // El home y su carpeta padre (C:\Users) no son proyectos: ahí adentro viven
    // credenciales y configuración de todas las herramientas del usuario.
    if (abs.compare(home, cs) == 0) return QString();
    QDir homeParent(home);
    if (homeParent.cdUp() && abs.compare(QDir::cleanPath(homeParent.absolutePath()), cs) == 0)
        return QString();
    return abs;
}

QString LlamaAgentBackend::fallbackWorkspaceDir()
{
    const QString dir = QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/workspace"));
    QDir().mkpath(dir);
    return dir;
}

QString LlamaAgentBackend::developmentDisciplineSection()
{
    return QStringLiteral(
        "NO ROMPER LO EXISTENTE (regresiones): cuando modifiques código que ya "
        "existe, el objetivo es agregar/cambiar lo pedido SIN romper flujos que ya "
        "andaban.\n"
        "- Blast radius primero: antes de editar una función/símbolo, buscá quién la "
        "usa (grep del nombre) y leé esos call sites. No edites a ciegas.\n"
        "- Cambio mínimo y local: tocá SOLO lo necesario. No reescribas, renombres ni "
        "reformatees código no relacionado. No cambies firmas, APIs públicas ni el "
        "comportamiento actual salvo que la tarea lo pida explícitamente.\n"
        "- Preservá contratos: si una función ya devuelve/hace algo, los casos que ya "
        "funcionaban deben seguir igual. Mantené compatibilidad hacia atrás.\n"
        "- Verificá al terminar (esta verificación SÍ vale): si el proyecto tiene "
        "tests o un script de build/test (tests.bat, ctest, npm test, etc.) corrélo "
        "UNA vez después del cambio. Si no hay tests, al menos compilá. Si el cambio "
        "es de comportamiento y no había test que lo cubra, agregá uno.\n"
        "- Si tu cambio podría afectar OTRO flujo, decilo explícito en el resumen "
        "final (qué tocaste y qué podría impactar).\n"
        "- ENTREGABLES: si le das un script/comando para correr (.bat, .sh, .ps1), "
        "EJECUTALO vos tal cual primero y confirmá que no falla. Si no lo podés correr "
        "acá, decilo.\n"
        "- INICIO Y RUNTIME: no basta con que arranque; probá un caso end-to-end "
        "(estado inicial válido, ej. spawnear DENTRO del mapa; interacción sin "
        "crashear).\n\n");
}

QString LlamaAgentBackend::testSafetyNetSection()
{
    return QStringLiteral(
        "RED DE TESTS (atrapar regresiones): tu defensa contra romper flujos viejos "
        "son los tests del proyecto. Por cada feature o cambio de comportamiento, "
        "dejá un test que lo cubra y corré el suite.\n"
        "- Detectá el runner que YA usa el proyecto antes de inventar uno. Buscá, en "
        "este orden, lo que exista: scripts test en package.json (npm/pnpm/yarn "
        "test), tests.bat o test.sh en la raíz, CMake + ctest, pytest/pyproject/tox, "
        "go test ./..., cargo test, gradle/maven test, target test en Makefile. Usá "
        "ESE comando. NO instales un framework nuevo si ya hay uno.\n"
        "- Test caja-negra: alimentá input real y asertá el output esperado. NADA de "
        "asserts triviales (assert(true), 1==1, ni tests que reimplementan la función "
        "que prueban). Cubrí el camino feliz + al menos un caso borde o de error.\n"
        "- Registralo donde el runner lo levante solo (carpeta tests/, sufijo "
        "*_test/_spec, alta en CMake/índice, etc.) para que corra siempre, no solo a "
        "mano.\n"
        "- Corré el suite UNA vez al terminar. Verde = listo. Rojo = arreglá antes de "
        "cerrar; no entregues en rojo.\n"
        "- Si el proyecto NO tiene tests: no metas un framework pesado sin permiso. "
        "Hacé un smoke check mínimo (ejecutá el flujo y verificá la salida) y ofrecé "
        "configurar tests.\n\n");
}

QString LlamaAgentBackend::projectContextSection()
{
    return QStringLiteral(
        "CONTEXTO DEL PROYECTO (entendé el porqué, dejá memoria): no leés el código "
        "una vez y listo; entendé cómo encaja antes de tocarlo.\n"
        "- Asumí intención: si una parte parece rara, redundante o un workaround, "
        "probablemente está así a propósito. Buscá el PORQUÉ antes de cambiarla: "
        "comentarios cercanos, `git log`/`git blame` del archivo, y la memoria del "
        "proyecto (.llamacode/memory.md o AGENTS.md). NO 'arregles' un workaround "
        "deliberado sin entender la razón: lo volverías a romper.\n"
        "- Co-cambios: archivos que cambian juntos suelen estar acoplados aunque no "
        "haya import visible. Si tocás uno, mirá con `git log --name-only` qué otros "
        "cambian con él y revisalos.\n"
        "- Antes de una tanda de cambios, la tool code_hotspots te dice qué archivos "
        "son frágiles (mucho churn git y SIN test): son los que más probablemente "
        "escondan una regresión. Reforzá esos con tests antes de tocarlos.\n"
        "- Repo grande que no conocés: corré `graph` action='index' UNA vez (mapea "
        "símbolos e imports, determinista y barato) y después navegá con `graph` "
        "action='query' en vez de re-leer archivos para entender cómo se conecta todo.\n"
        "- Fuente de verdad: priorizá código y tests actuales, después decisiones "
        "vigentes con evidencia, luego memoria verificada y por último inferencias o "
        "historial. Una nota vieja nunca gana contra el estado comprobable del repo.\n"
        "- Trabajo paralelo: antes de editar usá `work_status`. Si una claim de otra "
        "sesión reclama la misma ruta, no fuerces la escritura: explicá el conflicto "
        "y coordiná un handoff o elegí otra superficie.\n"
        "- Mutaciones opacas: toda tool shell/MCP que pueda crear, modificar o borrar "
        "archivos debe declarar `changed_paths` con rutas relativas concretas. No "
        "intentes ocultar una mutación dentro de un comando genérico: el host la "
        "rechaza si no puede coordinarla y re-sincronizar el contexto.\n"
        "- Memoria: el mantenimiento automático marca stale sólo hechos viejos y de "
        "bajo valor; decisiones verificadas/importantes no se descartan. Usá `decay` "
        "con `dry_run=true` si necesitás inspeccionar candidatos.\n"
        "- Dejá memoria: cuando descubras o decidas algo durable y NO obvio (por qué "
        "existe un patrón, una restricción, un acoplamiento, una decisión de diseño), "
        "anotá 1-2 líneas en .llamacode/memory.md para que la próxima sesión arranque "
        "sabiéndolo y no lo deshaga. No anotes lo obvio del código ni lo efímero de "
        "esta tarea.\n\n");
}

QString LlamaAgentBackend::efficiencySection()
{
    return QStringLiteral(
        "EFICIENCIA (importante): Resolvé la tarea en la MENOR cantidad de pasos/tool "
        "calls posible. Hacé lo justo que pidió el usuario, sin sobre-ingeniería ni "
        "features extra. Para CREAR un archivo: write_file UNA vez. Para MODIFICAR un "
        "archivo existente usá edit_file (reemplazo puntual de un fragmento), NO "
        "reescribas todo el archivo con write_file: es mucho más lento. Para leer "
        "archivos grandes usá read_file con offset/limit. Buscá con grep (regex) y "
        "glob. No "
        "verifiques de más: no re-leas ni re-ejecutes pruebas que ya pasaron, no "
        "corras el mismo comando varias veces. Una sola verificación rápida alcanza si "
        "hace falta. Cuando la tarea está hecha, terminá: no sigas iterando.\n\n");
}

QString LlamaAgentBackend::styleSection()
{
    return QStringLiteral(
        "ESTILO: respondé en fragmentos técnicos concisos. Sin relleno, sin "
        "cortesías, sin repetir lo que ya dijiste o lo que es obvio del código. "
        "Preferí listas y comandos antes que prosa. No expliques lo que vas a hacer "
        "antes de hacerlo: usá la tool directamente. El texto que generás también "
        "cuesta tiempo (generación local lenta): cada palabra de más es latencia.");
}

QString LlamaAgentBackend::desktopPlaybookSection(bool visionReady)
{
    QString s = QStringLiteral(
        "AUTOMATIZACIÓN DE ESCRITORIO (apps nativas de Windows): seguí este "
        "camino, es el más rápido y confiable.\n"
        "1) Abrir: desktop_launch con la app (ej. 'calc', 'notepad'). NO uses "
        "run_shell para apps GUI (se cuelga).\n"
        "2) Esperar y ubicar: desktop_wait ~800 ms, después desktop_windows para "
        "tomar el id de la ventana. Una sola vez; no repitas el inventario en loop.\n"
        "3) Teclado primero (camino rápido): para apps que se manejan con teclado "
        "(calculadora, notepad, campos de texto) NO clickees botones. Enfocá con "
        "desktop_focus <id> y escribí con desktop_type. Para calculadora: primero "
        "desktop_key ESC para limpiar; después detectá la expresión del objetivo y "
        "mandala completa con '=' en una sola acción (desktop_type \"<expresión>=\"). "
        "No la partas y no presiones ENTER después del '='. Fin. desktop_type/desktop_key "
        "van a la ventana en foco.\n"
        "4) Botones sin equivalente de teclado: desktop_controls <id> lista los "
        "controles por NOMBRE (árbol UIA, sin captura); tomá el controlId y "
        "accionalo con desktop_click_element. Clic semántico por nombre, NO por "
        "pixel.\n"
        "5) VERIFICAR el resultado: leé el estado con desktop_controls — el texto "
        "de los controles (ej. el visor ACTUAL de la calculadora, 'Se muestra X') "
        "trae el valor en su nombre, sin necesidad de visión. No aceptes "
        "Historial/Memoria como resultado final. ");
    if (visionReady) {
        s += QStringLiteral(
            "Este perfil tiene visión (--mmproj): también podés usar desktop_observe "
            "para VER la pantalla cuando el texto UIA no alcance. Para grounding visual, "
            "desktop_click/desktop_stroke aceptan EXCLUSIVAMENTE coordenadas normalizadas "
            "0..1: si razonaste en una grilla 0..1000, dividí x e y por 1000 antes de "
            "llamar la tool. Nunca sustituyas el objetivo pedido por un control parecido: "
            "si no está visible o hay ambigüedad, no hagas clic; volvé a observar o "
            "informá que no se encontró. Después de un clic visual, capturá o consultá "
            "UIA una vez y verificá el cambio esperado antes de seguir.\n");
    } else {
        s += QStringLiteral(
            "Este perfil NO tiene visión (sin --mmproj): desktop_observe saca una "
            "captura que NO podés ver, así que NO la uses para leer números ni para "
            "verificar — entrarías en un loop ciego. Verificá siempre por texto con "
            "desktop_controls.\n");
    }
    s += QStringLiteral(
        "Regla anti-loop: una acción, una verificación por texto, terminá. Si algo "
        "no avanza, cambiá de enfoque (teclado ↔ desktop_controls); no repitas la "
        "misma tool sin cambios.\n\n");
    return s;
}

QString LlamaAgentBackend::honeySection()
{
    return QStringLiteral(
        "FRUGALIDAD (Honey): reducí lo que GENERÁS, no lo que el usuario pidió.\n"
        "- Escalera YAGNI: preguntá 1) ¿hace falta?, 2) ¿ya existe en este código? "
        "3) ¿lo resuelve la stdlib?, 4) ¿hay una API nativa?, 5) ¿ya existe una "
        "dependencia instalada?, 6) ¿cuál es el bloque mínimo correcto? Parate en "
        "el primer escalón que satisface la tarea.\n"
        "- No agregues scaffolding 'por si después': evitá abstracciones, parámetros, "
        "branches, factories, adapters, configuración o handlers especulativos que "
        "nadie pidió. Reutilizá código y contratos existentes antes de crear otro.\n"
        "- Lazy, nunca negligente: esta regla NO permite quitar validación, seguridad, "
        "tests, accesibilidad, manejo de errores, permisos, aprobación humana ni "
        "verificación del resultado. Si son necesarios, forman parte del mínimo.\n"
        "- Respuesta-primero: la respuesta o el resultado va primero; sin preámbulo, "
        "sin narrar el código que ya se lee solo, sin hedging.\n"
        "- Handoffs densos: cuando le pasás trabajo a otro agente (supervisor, "
        "sub-tarea), usá clave:valor compacto en líneas, NO JSON pretty-printed ni "
        "prosa. Mismos datos, la mitad de tokens.\n\n");
}

QString LlamaAgentBackend::antiBiasSection()
{
    return QStringLiteral(
        "ANTI-SESGO (razonamiento): basá la respuesta ESTRICTAMENTE en las premisas "
        "dadas, no en una pregunta parecida que hayas visto antes. ¿Cuál es la "
        "intención real del usuario? Leé la consigna palabra por palabra y no agregues "
        "supuestos.\n"
        "- Si te encontrás pensando 'lo usual', 'lo estándar', 'lo típico' o 'el "
        "clásico', es sesgo: ese análisis queda ANULADO y hay que re-examinar desde la "
        "consigna literal.\n"
        "- Buscá el mejor resultado que cumpla la premisa primaria del usuario y que "
        "ninguna restricción EXPLÍCITA prohíba (no inventes restricciones que no están "
        "escritas).\n"
        "- Respondé apenas se cumple la premisa primaria; no re-derives un check que ya "
        "pasaste ni sobre-pienses una tarea trivial.\n\n");
}

QVariantList LlamaAgentBackend::directiveCatalog()
{
    auto mk = [](const char *key, const char *name, const char *desc) {
        return QVariant(QVariantMap{
            {QStringLiteral("key"), QString::fromUtf8(key)},
            {QStringLiteral("name"), QString::fromUtf8(name)},
            {QStringLiteral("description"), QString::fromUtf8(desc)}});
    };
    return {
        mk("discipline", "Anti-regresión",
           "No romper lo existente: blast radius, cambio mínimo, preservar contratos, verificar al terminar."),
        mk("testNet", "Red de tests",
           "Por cada feature/cambio dejar un test caja-negra con el runner del proyecto y correr el suite."),
        mk("projectContext", "Contexto del proyecto",
           "Entender el porqué antes de tocar, revisar co-cambios por git y dejar memoria de lo durable."),
        mk("efficiency", "Eficiencia",
           "Resolver en la menor cantidad de pasos/tool calls; sin sobre-ingeniería ni verificación de más."),
        mk("style", "Estilo conciso",
           "Respuestas en fragmentos técnicos concisos, sin relleno ni cortesías; listas y comandos."),
        mk("honey", "Frugalidad (Honey)",
           "Reduce lo que el modelo emite: código YAGNI sin scaffolding especulativo, respuesta-primero y handoffs agente↔agente densos. Off por defecto (opt-in)."),
        mk("antiBias", "Anti-sesgo",
           "Endurece el razonamiento: basar la respuesta en las premisas dadas, tratar 'usual/estándar/típico/clásico' como sesgo a re-examinar, responder al cumplir la premisa sin sobre-pensar. Off por defecto (opt-in)."),
    };
}

void LlamaAgentBackend::setDirectives(const QStringList &keys)
{
    m_directives = QSet<QString>(keys.cbegin(), keys.cend());
    m_directivesSet = true;
    // Propagar honey al worker: cambia el formato de respuesta del maestro
    // (ask_teacher) a denso clave:valor. Los sub-agentes lo reciben en launchSub.
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "setHoneyHandoff", Qt::QueuedConnection,
                                  Q_ARG(bool, m_directives.contains(QStringLiteral("honey"))));
    if (!m_apiMessages.isEmpty()) {
        QJsonObject sys = m_apiMessages.first().toObject();
        if (sys.value(QStringLiteral("role")).toString() == QLatin1String("system")) {
            sys[QStringLiteral("content")] = buildSystemPrompt();
            replaceSystemMessage(sys);
        }
    }
}

QString LlamaAgentBackend::buildSystemPrompt() const
{
#ifdef Q_OS_WIN
    const QString os = QStringLiteral("Windows");
    const QString shell = QStringLiteral(
        "El shell de run_shell es cmd.exe de Windows: usá sintaxis de Windows "
        "(mkdir sin -p, dir, copy, type, del, '&&' para encadenar). NO uses "
        "sintaxis de Unix/bash (mkdir -p, ls, cat, rm). NO asumas Linux ni WSL.");
#else
    const QString os = QStringLiteral("Linux/Unix");
    const QString shell = QStringLiteral("El shell de run_shell es sh (sintaxis POSIX).");
#endif
    const bool super = (m_approvalMode == QLatin1String("super"));
    const QString scope = super
        ? QStringLiteral("Modo SUPER AGENTE: tenés acceso a TODO el disco. Podés "
              "leer/escribir cualquier carpeta usando rutas absolutas (ej. %1) o "
              "relativas al cwd. No se piden permisos.")
              .arg(QDir::toNativeSeparators(QDir::homePath()))
        : QStringLiteral("Las rutas de las tools son relativas al cwd y están "
              "CONFINADAS a él: no podés leer/escribir fuera del proyecto. Si el "
              "usuario pide una ruta absoluta fuera del cwd, trabajá dentro del "
              "proyecto y avisale.");
    QString base = QStringLiteral(
        "Sos un agente de coding. Sistema operativo: %1. Directorio de trabajo "
        "(cwd): %2. Tenés herramientas para leer/escribir archivos, listar, buscar y "
        "ejecutar comandos de shell. Usá las tools cuando necesites información real; "
        "no inventes contenido de archivos. %3 %4 Respondé en el idioma del usuario.\n\n")
        .arg(os, QDir::toNativeSeparators(m_cwd), scope, shell);

    // Directiva activa: sin setear (m_directivesSet=false) = TODAS, para no
    // regresionar; con perfil aplicado, solo las elegidas.
    auto dirOn = [this](const char *key) {
        return !m_directivesSet || m_directives.contains(QString::fromLatin1(key));
    };
    if (dirOn("efficiency")) base += efficiencySection();

    if (m_stablePhasePrefix)
        base += QStringLiteral(
            "PROTOCOLO DE FASES: [FASE=PLAN] impone solo lectura y prohibe editar o "
            "ejecutar shell; [FASE=EJECUCION] habilita ejecutar el plan y verificar. "
            "El runner aplica la politica y no puede evadirse.\n\n");
    else if (m_approvalMode == QLatin1String("plan"))
        base += QStringLiteral(
            "MODO PLAN (read-only): NO podés editar archivos ni correr comandos; solo "
            "leer/buscar. Investigá lo necesario y entregá un PLAN claro y accionable "
            "(pasos, archivos a tocar, riesgos). write_file/edit_file/run_shell están "
            "deshabilitadas.\n\n");

    if (m_hitlDestructive && !super)
        base += QStringLiteral(
            "GUARDRAIL DE ACCIONES DESTRUCTIVAS: las acciones irreversibles (borrado "
            "recursivo, format, git push --force / reset --hard, DROP/TRUNCATE de DB, "
            "borrar memoria) SIEMPRE requieren aprobación humana explícita, aun en modo "
            "automático — no es un error, es una pausa esperada. Preferí alternativas no "
            "destructivas (mover a papelera en vez de borrar, rama nueva en vez de "
            "--force). Si necesitás una acción destructiva, ejecutá la tool igual y "
            "esperá la aprobación del usuario; no intentes evadir el freno.\n\n");

    if (dirOn("discipline"))     base += developmentDisciplineSection();
    if (dirOn("testNet"))        base += testSafetyNetSection();
    // Playbook de escritorio: sólo si las tools desktop_* están disponibles
    // (no deshabilitadas por el perfil). Sin esto el modelo flailea con capturas
    // ciegas al operar apps nativas (ej. "sumar 2+2 en la calculadora").
    if (!m_disabledTools.contains(QStringLiteral("desktop_launch")))
        base += desktopPlaybookSection(m_visionReady);
    if (dirOn("projectContext")) base += projectContextSection();
    if (dirOn("style"))          base += styleSection();
    // Honey es opt-in puro: NO entra en el default "todas on" (m_directivesSet
    // false) porque es agresivo y en modelos chicos locales puede recortar el
    // razonamiento. Sólo si el perfil la elige explícitamente.
    if (m_directives.contains(QStringLiteral("honey"))) base += honeySection();
    // Anti-sesgo: opt-in puro como honey. Endurece el razonamiento pero alarga el
    // prompt; en modelos chicos puede inducir trap-paranoia, así que sólo si el
    // perfil la elige explícitamente (no entra en el default "todas on").
    if (m_directives.contains(QStringLiteral("antiBias"))) base += antiBiasSection();

    // Memoria estructurada: hechos vigentes de MemoryStore. Cuántos —y si van—
    // lo decide el módulo `memory` del spec; antes eran 12 fijos y sin apagar.
    if (m_memoryPolicy.structuredEnabled && m_memoryPolicy.structuredFacts > 0) {
        const QString structuredMem = MemoryStore::recall(m_cwd, QString(), QString(),
                                                          m_memoryPolicy.structuredFacts);
        if (!structuredMem.startsWith(QLatin1Char('[')))
            base += QStringLiteral("\n\n--- Memoria estructurada relevante ---\n") + structuredMem;
        const int personalFacts = qMax(1, m_memoryPolicy.structuredFacts / 3);
        const QString personalMem = MemoryStore::recall(
            m_cwd, QString(), QStringLiteral("personal"), personalFacts);
        if (!personalMem.startsWith(QLatin1Char('[')))
            base += QStringLiteral("\n\n--- Memoria personal relevante ---\n") + personalMem;
    }

    // Memoria legacy/instrucciones: .llamacode/memory.md o AGENTS.md.
    if (m_memoryPolicy.projectMemory && m_memoryPolicy.projectMemoryMaxChars > 0) {
        QString mem;
        for (const QString &cand : {memoryFilePath(m_cwd),
                                    QDir::cleanPath(m_cwd + QStringLiteral("/AGENTS.md"))}) {
            QFile f(cand);
            if (f.open(QIODevice::ReadOnly)) {
                mem = QString::fromUtf8(f.read(m_memoryPolicy.projectMemoryMaxChars)).trimmed();
                if (!mem.isEmpty()) break;
            }
        }
        if (!mem.isEmpty())
            base += QStringLiteral("\n\n--- Memoria del proyecto ---\n") + mem;
    }
    if (m_knowledgePolicy.enabled && m_knowledgePolicy.citeSources) {
        base += QStringLiteral(
            "\n\n--- Protocolo de conocimiento ---\n"
            "Cuando uses hechos o relaciones del knowledge packet, distinguí "
            "verified de unreviewed/inferred. Citá las fuentes con el formato "
            "ruta:Línea-Línea cuando estén disponibles; si no hay fuente exacta, "
            "declaralo como inferencia. No conviertas un edge unreviewed en un hecho.\n");
    }
    if (!m_systemExtra.trimmed().isEmpty())
        base += QStringLiteral("\n\n--- Instrucciones del agente ---\n") + m_systemExtra.trimmed();
    if (!m_thinkingEnabled)
        base += QStringLiteral(
            "\n\nRAZONAMIENTO INTERNO: no muestres razonamiento interno. "
            "Respondé sólo con la respuesta final. No emitas etiquetas "
            "<think> ni </think>.");

    // Directivas de usuario (packs .md del harness modular). Van al final, tras
    // las built-in y la memoria: son el ajuste más específico. Cada una puede
    // declarar un gate `when` con hechos del contexto actual.
    if (!m_customDirectives.isEmpty()) {
        const QVariantMap facts = directiveFacts(super);
        for (const QVariant &v : m_customDirectives) {
            const QVariantMap d = v.toMap();
            const QString body = d.value(QStringLiteral("body")).toString().trimmed();
            if (body.isEmpty()) continue;
            if (!directiveConditionMet(d.value(QStringLiteral("when")).toString(), facts))
                continue;
            base += QStringLiteral("\n\n--- %1 ---\n")
                        .arg(d.value(QStringLiteral("slug")).toString())
                    + body;
        }
    }

    // Tope del prompt compuesto: no truncamos el prompt (cortar a la mitad una
    // instrucción es peor que un prompt largo), pero dejamos el aviso para que
    // el editor y el log lo muestren. maxChars<=0 = sin tope.
    if (m_promptMaxChars > 0 && base.size() > m_promptMaxChars) {
        logFromConst(QStringLiteral("[prompt: %1 chars supera el tope del perfil (%2); "
                                    "revisá directivas y memoria del proyecto]\n")
                         .arg(base.size()).arg(m_promptMaxChars));
    }
    return base;
}

// buildSystemPrompt es const (la llaman getters y hooks de test) pero el aviso
// de presupuesto tiene que llegar al log de la UI. Emitir una señal no cambia el
// estado lógico del backend, así que el const_cast queda acotado acá.
void LlamaAgentBackend::logFromConst(const QString &text) const
{
    emit const_cast<LlamaAgentBackend *>(this)->logAppended(text);
}

void LlamaAgentBackend::setAgentTuning(const QString &systemExtra, double temperature)
{
    m_systemExtra = systemExtra;
    m_temperature = temperature;
    // Si ya hay sesión activa, refrescar el system prompt (índice 0).
    if (!m_apiMessages.isEmpty()) {
        QJsonObject sys = m_apiMessages.first().toObject();
        if (sys.value(QStringLiteral("role")).toString() == QLatin1String("system")) {
            sys[QStringLiteral("content")] = buildSystemPrompt();
            replaceSystemMessage(sys);
        }
    }
}

void LlamaAgentBackend::setThinkingEnabled(bool enabled)
{
    m_thinkingEnabled = enabled;
    if (!m_apiMessages.isEmpty()) {
        QJsonObject sys = m_apiMessages.first().toObject();
        if (sys.value(QStringLiteral("role")).toString() == QLatin1String("system")) {
            sys[QStringLiteral("content")] = buildSystemPrompt();
            replaceSystemMessage(sys);
        }
    }
}

// Glob → regex anclada (mismo criterio que el worker: ** = recursivo).
static QRegularExpression permGlobToRegex(const QString &glob)
{
    // Construido a mano (NO QRegularExpression::escape: escapa '/' y rompe los
    // tokens de glob). '/' y '\' = cualquier separador. Matchea rutas rel y abs.
    QString rx = QStringLiteral("^");
    const int n = glob.size();
    int i = 0;
    while (i < n) {
        const QChar c = glob.at(i);
        if (c == QLatin1Char('*') && i + 1 < n && glob.at(i + 1) == QLatin1Char('*')) {
            if (i + 2 < n && (glob.at(i + 2) == QLatin1Char('/') || glob.at(i + 2) == QLatin1Char('\\'))) {
                rx += QStringLiteral("(?:.*[/\\\\])?");
                i += 3;   // **/ -> cero o mas dirs
                continue;
            }
            rx += QStringLiteral(".*");
            i += 2;        // ** -> cualquier cosa
            continue;
        } else if (c == QLatin1Char('*')) {
            rx += QStringLiteral("[^/\\\\]*");
            i += 1;        // * -> segmento
            continue;
        } else if (c == QLatin1Char('?')) {
            rx += QStringLiteral("[^/\\\\]");
            i += 1;
            continue;
        } else if (c == QLatin1Char('/') || c == QLatin1Char('\\')) {
            rx += QStringLiteral("[/\\\\]");
            i += 1;        // separador: slash o backslash
            continue;
        }
        if (QStringLiteral(".^$+(){}[]|").contains(c)) rx += QLatin1Char('\\');
        rx += c;
        i += 1;
    }
    rx += QLatin1Char('$');
    return QRegularExpression(rx);
}

// Reglas: una por línea. "allow|deny|ask [kind:]<glob>". kind ∈ read|write|shell.
// Ej: "deny **/.env", "allow write:src/**", "ask shell:rm *". '#' = comentario.
void LlamaAgentBackend::setPermissionRules(const QString &rules)
{
    m_permRules.clear();
    const QStringList lines = rules.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const int sp = line.indexOf(QRegularExpression(QStringLiteral("\\s")));
        if (sp < 0) continue;
        const QString act = line.left(sp).toLower();
        QString rest = line.mid(sp).trimmed();
        PermAction action;
        if (act == QLatin1String("deny")) action = PermDeny;
        else if (act == QLatin1String("allow")) action = PermAllow;
        else if (act == QLatin1String("ask")) action = PermAsk;
        else continue;
        QString kind;
        const int colon = rest.indexOf(QLatin1Char(':'));
        if (colon > 0) {
            const QString k = rest.left(colon).toLower();
            if (k == QLatin1String("read") || k == QLatin1String("write")
                || k == QLatin1String("shell") || k == QLatin1String("mcp")) {
                kind = k;
                rest = rest.mid(colon + 1).trimmed();
            }
        }
        if (rest.isEmpty()) continue;
        m_permRules.append(PermRule{action, kind, permGlobToRegex(rest), rest});
    }
    emit logAppended(QStringLiteral("[permisos: %1 regla(s) cargadas]\n").arg(m_permRules.size()));
}

void LlamaAgentBackend::setApprovalPolicy(const QString &mode)
{
    const QString previousMode = m_approvalMode;
    m_approvalMode = mode;
    // "super" = sin confinamiento al cwd (acceso a todo el disco).
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "setConfined", Qt::QueuedConnection,
                                  Q_ARG(bool, mode != QLatin1String("super")));
    if (m_stablePhasePrefix && previousMode != mode && !m_apiMessages.isEmpty()) {
        const QString control = mode == QLatin1String("plan")
            ? QStringLiteral("[FASE=PLAN] Solo explorar y planificar. Prohibido editar o ejecutar shell.")
            : QStringLiteral("[FASE=EJECUCION] Ejecutar el plan aprobado y verificar el resultado.");
        appendApiMessage(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                     {QStringLiteral("content"), control},
                                     {QStringLiteral("_lc_protected"), true}});
        return;
    }
    // Compatibilidad: modo legacy reconstruye el system prompt.
    if (!m_apiMessages.isEmpty()) {
        QJsonObject sys = m_apiMessages.first().toObject();
        if (sys.value(QStringLiteral("role")).toString() == QLatin1String("system")) {
            sys[QStringLiteral("content")] = buildSystemPrompt();
            replaceSystemMessage(sys);
        }
    }
}

void LlamaAgentBackend::setTaskScope(const QString &scope, const QStringList &folders)
{
    if (!m_worker) return;
    const bool full = (scope == QLatin1String("full"));
    QMetaObject::invokeMethod(m_worker, "setConfined", Qt::QueuedConnection,
                              Q_ARG(bool, !full));
    const QStringList roots = (scope == QLatin1String("folder")) ? folders : QStringList{};
    QMetaObject::invokeMethod(m_worker, "setAllowedRoots", Qt::QueuedConnection,
                              Q_ARG(QStringList, roots));
    emit logAppended(QStringLiteral("[task scope: %1%2]\n")
                         .arg(scope, roots.isEmpty() ? QString()
                                                     : QStringLiteral(" → ") + roots.join(QStringLiteral(", "))));
}

void LlamaAgentBackend::clearTaskScope()
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "setConfined", Qt::QueuedConnection,
                              Q_ARG(bool, m_approvalMode != QLatin1String("super")));
    QMetaObject::invokeMethod(m_worker, "setAllowedRoots", Qt::QueuedConnection,
                              Q_ARG(QStringList, QStringList{}));
}

void LlamaAgentBackend::setTaskAutoApprove(bool on) { m_taskAutoApprove = on; }

// ───────────────────────────── Conversación ──────────────────────────────
void LlamaAgentBackend::sendMessage(const QString &text)
{
    sendMessageImpl(text, text);
}

void LlamaAgentBackend::setReasoningPolicy(const QString &effort, int budget)
{
    const QString normalized = effort.trimmed().toLower();
    m_reasoningEffort = ReasoningWire::normalizeEffort(normalized);
    m_reasoningBudget = qMax(-1, budget);
}

void LlamaAgentBackend::setProgressPolicy(const AgentProgressGovernor::Policy &policy,
                                          int quickToolTimeoutSec)
{
    m_progressGovernor.setPolicy(policy);
    m_quickToolTimeoutSec = qBound(5, quickToolTimeoutSec, 120);
}

// Política del loop (módulo `loop` del HarnessSpec). Aplica también al
// governor: créditos y replan viven ahí, y así una sola llamada deja el turno
// entero configurado.
void LlamaAgentBackend::setLoopPolicy(const HarnessLoopModule &loop)
{
    m_loopPolicy = loop;
    m_loopPolicy.set = true;
    m_quickToolTimeoutSec = qBound(5, loop.quickToolTimeoutSec, 120);
    m_progressGovernor.setPolicy(AgentProgressGovernor::Policy{
        loop.credits, loop.maxCredits, loop.replanAfter, loop.stopAfter,
        loop.maxDistinctWrites});
    // Propagar a los runtimes por sesión ya creados (cada conversación
    // concurrente es una instancia completa; sin esto la política nueva sólo
    // aplicaría a las sesiones futuras).
    for (LlamaAgentBackend *rt : std::as_const(m_sessionRuntimes))
        if (rt) rt->setLoopPolicy(loop);
}

void LlamaAgentBackend::setMemoryPolicy(const HarnessMemoryModule &memory)
{
    m_memoryPolicy = memory;
    m_memoryPolicy.set = true;
    for (LlamaAgentBackend *rt : std::as_const(m_sessionRuntimes))
        if (rt) rt->setMemoryPolicy(memory);
    // Con sesion viva el system prompt tiene que rehacerse: cambiar cuantos
    // hechos se inyectan y que recien aplique en la proxima sesion seria una
    // perilla que miente.
    if (!m_apiMessages.isEmpty()) {
        QJsonObject sys = m_apiMessages.first().toObject();
        if (sys.value(QStringLiteral("role")).toString() == QLatin1String("system")) {
            sys[QStringLiteral("content")] = buildSystemPrompt();
            replaceSystemMessage(sys);
        }
    }
}

void LlamaAgentBackend::setKnowledgePolicy(const HarnessKnowledgeModule &knowledge)
{
    m_knowledgePolicy = knowledge;
    m_knowledgePolicy.set = true;
    if (!m_apiMessages.isEmpty()) {
        QJsonObject sys = m_apiMessages.first().toObject();
        if (sys.value(QStringLiteral("role")).toString() == QLatin1String("system")) {
            sys[QStringLiteral("content")] = buildSystemPrompt();
            replaceSystemMessage(sys);
        }
    }
    for (LlamaAgentBackend *rt : std::as_const(m_sessionRuntimes))
        if (rt) rt->setKnowledgePolicy(knowledge);
}

void LlamaAgentBackend::setContextPolicy(const HarnessContextModule &context)
{
    m_contextPolicy = context;
    m_contextPolicy.set = true;
    if (m_contextPolicy.indexPolicy == QLatin1String("eager") && !m_cwd.isEmpty()) {
        const QVariantMap state = ContextIndex::refresh(m_cwd);
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("observation"),
                              QJsonObject{{QStringLiteral("source"), QStringLiteral("context_index")},
                                          {QStringLiteral("mode"), QStringLiteral("eager")},
                                          {QStringLiteral("state"), QJsonObject::fromVariantMap(state)}});
    }
    for (LlamaAgentBackend *rt : std::as_const(m_sessionRuntimes))
        if (rt) rt->setContextPolicy(context);
}

void LlamaAgentBackend::setToolProtocol(const QString &mode)
{
    m_toolProtocol = (mode == QLatin1String("native") || mode == QLatin1String("text"))
                         ? mode : QStringLiteral("auto");
    // "text" es el mismo camino que ya usa AppController para modelos sin
    // tool-template; el spec lo vuelve declarativo en vez de heurístico.
    if (m_toolProtocol == QLatin1String("text")) m_forceTextTools = true;
    else if (m_toolProtocol == QLatin1String("native")) m_forceTextTools = false;
    for (LlamaAgentBackend *rt : std::as_const(m_sessionRuntimes))
        if (rt) rt->setToolProtocol(mode);
}

void LlamaAgentBackend::setCustomDirectives(const QVariantList &directives)
{
    m_customDirectives = directives;
    for (LlamaAgentBackend *rt : std::as_const(m_sessionRuntimes))
        if (rt) rt->setCustomDirectives(directives);
    if (!m_apiMessages.isEmpty()) {
        QJsonObject sys = m_apiMessages.first().toObject();
        if (sys.value(QStringLiteral("role")).toString() == QLatin1String("system")) {
            sys[QStringLiteral("content")] = buildSystemPrompt();
            replaceSystemMessage(sys);
        }
    }
}

// Hechos disponibles para el gate `when` de una directiva. La UI los enumera
// (LcHarnessEditor.directiveFacts) para que el usuario no los adivine: un hecho
// desconocido NO cumple, así que un typo deja la directiva fuera en silencio.
// Las claves salen de acá y de ningún otro lado — test_harness_modules fija que
// el catálogo y los hechos reales no diverjan.
QStringList LlamaAgentBackend::directiveFactKeys()
{
    return {QStringLiteral("tools.desktop"), QStringLiteral("tools.shell"),
            QStringLiteral("tools.web"),     QStringLiteral("tools.task"),
            QStringLiteral("vision"),        QStringLiteral("super"),
            QStringLiteral("project.hasGit")};
}

QVariantMap LlamaAgentBackend::directiveFacts(bool super) const
{
    return {{QStringLiteral("tools.desktop"),
             !m_disabledTools.contains(QStringLiteral("desktop_launch"))},
            {QStringLiteral("tools.shell"),
             !m_disabledTools.contains(QStringLiteral("run_shell"))},
            {QStringLiteral("tools.web"),
             !m_disabledTools.contains(QStringLiteral("web_fetch"))},
            {QStringLiteral("tools.task"), !m_disabledTools.contains(QStringLiteral("task"))},
            {QStringLiteral("vision"), m_visionReady},
            {QStringLiteral("super"), super},
            {QStringLiteral("project.hasGit"),
             !m_cwd.isEmpty() && QFileInfo::exists(m_cwd + QStringLiteral("/.git"))}};
}

// Gate declarativo de una directiva de usuario. Sintaxis mínima a propósito:
// una lista de hechos separados por coma, todos deben cumplirse; `!` niega.
// Hechos conocidos los pone buildSystemPrompt (tools.desktop, tools.shell,
// tools.web, vision, project.hasGit). Un hecho desconocido NO cumple: una
// directiva mal escrita no se inyecta en silencio a destiempo.
bool LlamaAgentBackend::directiveConditionMet(const QString &when, const QVariantMap &facts)
{
    const QString cond = when.trimmed();
    if (cond.isEmpty()) return true;
    const QStringList parts = cond.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &raw : parts) {
        QString key = raw.trimmed();
        bool negate = false;
        if (key.startsWith(QLatin1Char('!'))) { negate = true; key = key.mid(1).trimmed(); }
        if (key.isEmpty()) continue;
        const bool value = facts.value(key).toBool();
        if (value == negate) return false;
    }
    return true;
}

void LlamaAgentBackend::sendMessageWithVisibleText(const QString &apiText,
                                                   const QString &visibleText)
{
    sendMessageImpl(apiText, visibleText);
}

QString LlamaAgentBackend::visibleUserTextForTest(const QString &apiText,
                                                  const QString &visibleText)
{
    const QString visible = visibleText.trimmed();
    return visible.isEmpty() ? apiText.trimmed() : visible;
}

void LlamaAgentBackend::sendMessageImpl(const QString &text, const QString &visibleText)
{
    const QString trimmed = text.trimmed();
    const QString visibleTrimmed = visibleUserTextForTest(trimmed, visibleText);
    const QStringList attachments = m_pendingAttachments;
    m_pendingAttachments.clear();
    if (!m_running || (trimmed.isEmpty() && attachments.isEmpty())) return;
    if (!m_isSessionRuntime && !m_viewSessionId.isEmpty()) {
        LlamaAgentBackend *runtime = ensureSessionRuntime(m_viewSessionId);
        if (!runtime) return;
        copyRuntimeConfigurationTo(runtime);
        if (runtime->m_worker)
            QMetaObject::invokeMethod(runtime->m_worker, "setConfined", Qt::QueuedConnection,
                                      Q_ARG(bool, runtime->m_approvalMode != QLatin1String("super")));
        runtime->setPendingAttachments(attachments);
        if (activeRuntimeCount() >= qMax(1, m_ctx.parallelSlots) && !runtime->isBusy()) {
            if (!trimmed.isEmpty() || !attachments.isEmpty()) {
                runtime->m_msgQueue.append(trimmed);
                emit runtime->queueChanged();
                emit logAppended(QStringLiteral(
                    "[session %1: turno en cola por capacidad de slots]\n")
                                     .arg(m_viewSessionId));
            }
            return;
        }
        runtime->sendMessageImpl(trimmed, visibleTrimmed);
        return;
    }
    // Bloquear si hay turno en curso, una tool esperando aprobación, o una
    // compactación async en vuelo. Sin esto, mandar durante la compactación
    // corrompe m_apiMessages (reentrancy) → crash en Qt6Core.
    // Si el usuario estaba mirando otra sesión mientras el turno anterior
    // terminaba, activarla recién ahora: navegar no interrumpe ni reutiliza el
    // contexto de la sesión que seguía trabajando.
    if (!isBusy()) activateViewedSessionIfIdle();
    if (m_reply || m_compactReply || m_compacting || !m_awaitId.isEmpty()) {
        emit errorOccurred(QStringLiteral("Hay un turno en curso."));
        return;
    }
    ensureSession();
    if (m_adaptiveToolRouting)
        m_toolSurfaceDecision = ToolSurfaceRouter::decide(visibleTrimmed, toolCatalog());
    else
        m_toolSurfaceDecision = ToolSurfaceRouter::Decision{};
    m_correlationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_lifecycleFinishedToolIds.clear();
    beginDurableRun(visibleTrimmed);
    if (m_workClaimId.isEmpty()) {
        const QString agentId = m_harnessProfileId.trimmed().isEmpty()
            ? QStringLiteral("llamacode-agent") : m_harnessProfileId.trimmed();
        m_workClaimId = WorkRegistry::acquire(
            m_cwd, m_sessionId, agentId, visibleTrimmed, {}, QString(), QString(), 900);
        if (!m_workClaimId.isEmpty()) {
            AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("work_claim"),
                                  QJsonObject{{QStringLiteral("action"), QStringLiteral("acquire")},
                                              {QStringLiteral("claimId"), m_workClaimId},
                                              {QStringLiteral("goal"), visibleTrimmed.left(4096)}});
        }
    }
    ensureWorker();
    QMetaObject::invokeMethod(m_worker, "setCorrelationId", Qt::QueuedConnection,
                              Q_ARG(QString, m_correlationId));
    m_compactStall = 0;   // nuevo turno de usuario → reintentar compactar si hiciera falta
    m_transportRetries = 0;
    m_contextRecoveries = 0;
    pushCheckpoint();   // snapshot ANTES de agregar el nuevo turno (para rollback)

    // Contenido a mostrar en la UI: texto + chips de adjuntos.
    QString display = visibleTrimmed;
    for (const QString &p : attachments)
        display += QStringLiteral("\n📎 ") + QFileInfo(p).fileName();

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QVariantMap userMsg{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), display},
        {QStringLiteral("typing"), false},
        {QStringLiteral("createdAt"), static_cast<double>(nowMs)},
        {QStringLiteral("completedAt"), static_cast<double>(nowMs)},
        {QStringLiteral("elapsedMs"), 0},
        {QStringLiteral("tokens"), estimateTokens(display)},
        {QStringLiteral("tps"), 0.0}};
    if (!attachments.isEmpty()) userMsg[QStringLiteral("attachments")] = attachments;
    m_messages.append(userMsg);
    // Titular después de insertar el primer mensaje: así la persistencia y
    // cualquier cambio de vista observan el prompt que originó el nombre.
    autoTitleCurrentSession(visibleTrimmed);
    AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("observation"),
                          QJsonObject{{QStringLiteral("source"), QStringLiteral("user")},
                                      {QStringLiteral("correlationId"), m_correlationId},
                                      {QStringLiteral("text"), visibleTrimmed.left(4096)},
                                      {QStringLiteral("attachments"), attachments.size()}});

    // Contenido para la API: si hay adjuntos, mensaje multimodal (texto + imágenes
    // inline + docs de texto inlineados); si no, string plano.
    const QString apiText = trimmed;
    emit agentLifecycleEvent(AgentLifecycle::promptSubmit(
        m_sessionId, m_cwd, m_correlationId, visibleTrimmed, attachments.size()));
    // Preflight de contexto: slice de archivos candidatos ANTES del primer
    // request, para que el modelo no gaste vueltas descubriendo el repo. Estaba
    // detrás de un #ifdef de debug (código muerto en release); ahora lo gobierna
    // el módulo `context` del HarnessSpec, off por defecto = comportamiento previo.
    if (m_contextPolicy.preflight && m_contextPolicy.indexPolicy != QLatin1String("off")
        && attachments.isEmpty() && !m_cwd.isEmpty()) {
        const QString preflight = ContextPreflight::build(
            m_cwd, visibleTrimmed, m_contextPolicy.scoutK,
            m_contextPolicy.scoutBudget, m_contextPolicy.graphExpansion,
            m_knowledgePolicy.enabled && m_knowledgePolicy.preflight,
            m_knowledgePolicy.maxFacts, m_knowledgePolicy.maxEdges,
            m_knowledgePolicy.maxChars, m_sessionId);
        appendApiMessage(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                     {QStringLiteral("content"), preflight},
                                     {QStringLiteral("_lc_protected"), true}});
        emit agentLifecycleEvent(AgentLifecycle::contextPreflight(
            m_sessionId, m_cwd, m_correlationId, preflight.size(), QStringLiteral("prompt")));
        emit logAppended(QStringLiteral("[preflight: contexto inicial preparado]\n"));
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("observation"),
                              QJsonObject{{QStringLiteral("source"), QStringLiteral("context_preflight")},
                                          {QStringLiteral("mode"), QStringLiteral("spec")},
                                          {QStringLiteral("candidates"), preflight}});
    }
    if (attachments.isEmpty()) {
        appendApiMessage(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("content"), apiText}});
    } else {
        QString textPart = apiText;
        QJsonArray images;
        for (const QString &p : attachments) {
            // Los @-mentions vienen relativos al cwd; el picker, absolutos.
            const QString path = QFileInfo(p).isAbsolute()
                ? p : QDir(m_cwd).absoluteFilePath(p);
            const QString uri = imageDataUri(path);
            if (!uri.isEmpty()) {
                images.append(QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("image_url")},
                    {QStringLiteral("image_url"), QJsonObject{{QStringLiteral("url"), uri}}}});
            } else {
                const QString doc = readAttachText(path);
                if (!doc.isEmpty())
                    textPart += QStringLiteral("\n\n--- %1 ---\n%2")
                                    .arg(QFileInfo(path).fileName(), doc);
            }
        }
        QJsonArray parts;
        if (!textPart.trimmed().isEmpty())
            parts.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                     {QStringLiteral("text"), textPart}});
        for (const QJsonValue &iv : images) parts.append(iv);
        appendApiMessage(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("content"), parts}});
    }

    m_messages.append(QVariantMap{
        {QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("content"), QString()},
        {QStringLiteral("typing"), true},
        {QStringLiteral("status"), QStringLiteral("Pensando...")},
        {QStringLiteral("createdAt"), static_cast<double>(nowMs)},
        {QStringLiteral("elapsedMs"), 0},
        {QStringLiteral("tokens"), 0},
        {QStringLiteral("tps"), 0.0}});
    m_curAsstIdx = m_messages.size() - 1;
    emit messagesChanged();

    m_turnIters = 0;
    m_emptyTextRetries = 0;
    m_lastCallSignature.clear();
    m_sameCallStreak = 0;
    m_replannedCallSigs.clear();
    m_turnHadDifficulty = false;
    m_turnRecovered = false;
    m_progressGovernor.reset(visibleTrimmed);
    m_escalatedSigs.clear();
    if (!m_taskAutoApprove)
        m_desktopLaunchApps.clear();
    m_pendingObservations.clear();   // descartar capturas de un turno previo abortado
    runCompletion();
}

// Snapshot del estado actual (antes de un turno de usuario) para poder rebobinar.
void LlamaAgentBackend::pushCheckpoint()
{
    m_checkpoints.append(Checkpoint{
        static_cast<int>(m_apiMessages.size()), static_cast<int>(m_transcriptMessages.size()),
        static_cast<int>(m_messages.size()), m_editSnapshots.keys()});
}

QVariantMap LlamaAgentBackend::efficiencySummary() const
{
    return AgentEfficiency::summarize(m_efficiencyRequests);
}

QJsonArray LlamaAgentBackend::checkpointsToJson() const
{
    QJsonArray out;
    for (const Checkpoint &cp : m_checkpoints)
        out.append(QJsonObject{{QStringLiteral("apiLen"), cp.apiLen},
                               {QStringLiteral("transcriptLen"), cp.transcriptLen},
                               {QStringLiteral("msgLen"), cp.msgLen},
                               {QStringLiteral("editKeys"), QJsonArray::fromStringList(cp.editKeys)}});
    return out;
}

void LlamaAgentBackend::restoreCheckpoints(const QJsonArray &saved)
{
    m_checkpoints.clear();
    for (const QJsonValue &v : saved) {
        const QJsonObject o = v.toObject();
        Checkpoint cp{o.value(QStringLiteral("apiLen")).toInt(),
                      o.value(QStringLiteral("transcriptLen"))
                          .toInt(o.value(QStringLiteral("apiLen")).toInt()),
                      o.value(QStringLiteral("msgLen")).toInt(), {}};
        for (const QJsonValue &key : o.value(QStringLiteral("editKeys")).toArray())
            cp.editKeys.append(key.toString());
        if (cp.apiLen >= 1 && cp.apiLen <= m_apiMessages.size()
            && cp.msgLen >= 0 && cp.msgLen <= m_messages.size())
            m_checkpoints.append(cp);
    }
    // Compatibilidad con sesiones previas: reconstruir un punto conservador por
    // cada turno user visible, sin inventar snapshots de archivos.
    if (m_checkpoints.isEmpty()) {
        int apiCursor = 1;
        for (int mi = 0; mi < m_messages.size(); ++mi) {
            if (m_messages.at(mi).toMap().value(QStringLiteral("role")).toString()
                != QLatin1String("user")) continue;
            while (apiCursor < m_apiMessages.size()
                   && m_apiMessages.at(apiCursor).toObject().value(QStringLiteral("role")).toString()
                          != QLatin1String("user")) ++apiCursor;
            m_checkpoints.append({qMin(apiCursor, int(m_apiMessages.size())),
                                  qMin(apiCursor, int(m_transcriptMessages.size())), mi, {}});
            ++apiCursor;
        }
    }
}

// Rebobina la conversación al estado previo al mensaje de usuario en `msgIndex`.
void LlamaAgentBackend::rollbackToMessage(int msgIndex)
{
    if (!m_running) return;
    if (isBusy()) { emit errorOccurred(QStringLiteral("No se puede rebobinar con un turno en curso.")); return; }

    // Buscar el checkpoint cuyo msgLen == msgIndex (el tomado justo antes de ese
    // mensaje de usuario).
    int ci = -1;
    for (int i = 0; i < m_checkpoints.size(); ++i)
        if (m_checkpoints[i].msgLen == msgIndex) { ci = i; break; }
    if (ci < 0) { emit errorOccurred(QStringLiteral("No hay checkpoint para ese mensaje.")); return; }

    const Checkpoint cp = m_checkpoints[ci];
    if (cp.msgLen > m_messages.size() || cp.apiLen > m_apiMessages.size()
        || cp.transcriptLen > m_transcriptMessages.size()) return;

    // Revertir archivos editados DESPUÉS del checkpoint (los que no estaban en
    // editKeys). Sólo se puede restaurar al contenido original (1er snapshot).
    const QSet<QString> before(cp.editKeys.begin(), cp.editKeys.end());
    const QStringList nowKeys = m_editSnapshots.keys();
    for (const QString &abs : nowKeys)
        if (!before.contains(abs))
            revertEdit(abs);   // restaura original + saca el snapshot

    // Truncar conversación (UI + API) al estado del checkpoint.
    while (m_messages.size() > cp.msgLen) m_messages.removeLast();
    while (m_apiMessages.size() > cp.apiLen) m_apiMessages.removeLast();
    while (m_transcriptMessages.size() > cp.transcriptLen) m_transcriptMessages.removeLast();

    // Descartar checkpoints desde éste en adelante.
    while (m_checkpoints.size() > ci) m_checkpoints.removeLast();

    m_curAsstIdx = -1;
    emit logAppended(QStringLiteral("[rebobinado al mensaje %1 (%2 msgs, %3 ctx)]\n")
                         .arg(msgIndex).arg(m_messages.size()).arg(m_apiMessages.size()));
    emit messagesChanged();
    saveCurrentSession();
}

void LlamaAgentBackend::editMessage(int msgIndex, const QString &newText)
{
    if (!m_running) return;
    if (isBusy()) { emit errorOccurred(QStringLiteral("No se puede editar con un turno en curso.")); return; }
    if (msgIndex < 0 || msgIndex >= m_messages.size()) return;

    // Reescribe el contenido del mensaje y descarta todo lo posterior (UI).
    QVariantMap m = m_messages[msgIndex].toMap();
    m[QStringLiteral("content")] = newText;
    m[QStringLiteral("typing")] = false;
    m_messages[msgIndex] = m;
    while (m_messages.size() > msgIndex + 1) m_messages.removeLast();

    // Rebuildea m_apiMessages: conserva los system del inicio y reconstruye los
    // turnos como texto plano (user/assistant). Esto descarta tool_calls/tool
    // results del contexto, pero garantiza un historial válido para el server.
    QJsonArray neu;
    for (const QJsonValue &v : std::as_const(m_apiMessages)) {
        if (v.toObject().value(QStringLiteral("role")).toString() == QLatin1String("system"))
            neu.append(v);
        else
            break;
    }
    for (const QVariant &uv : std::as_const(m_messages)) {
        const QVariantMap um = uv.toMap();
        const QString role = um.value(QStringLiteral("role")).toString();
        const QString content = um.value(QStringLiteral("content")).toString();
        if ((role == QLatin1String("user") || role == QLatin1String("assistant")) && !content.isEmpty())
            neu.append(QJsonObject{{QStringLiteral("role"), role},
                                   {QStringLiteral("content"), content}});
    }
    m_apiMessages = neu;
    m_transcriptMessages = neu;

    // Descartar checkpoints que apunten más allá del estado actual.
    while (!m_checkpoints.isEmpty() && m_checkpoints.last().msgLen > m_messages.size())
        m_checkpoints.removeLast();

    m_curAsstIdx = -1;
    emit logAppended(QStringLiteral("[mensaje %1 editado; contexto recortado a %2 msgs / %3 ctx]\n")
                         .arg(msgIndex).arg(m_messages.size()).arg(m_apiMessages.size()));
    emit messagesChanged();
    saveCurrentSession();
}

// ¿Hay algo en vuelo? (request, compactación, tool ejecutándose o esperando
// aprobación, o tool_calls pendientes de procesar).
bool LlamaAgentBackend::isBusy() const
{
    return m_reply || m_compactReply || m_compacting
           || !m_awaitId.isEmpty() || !m_execCallId.isEmpty()
           || !m_pendingCalls.isEmpty() || subsActive();
}

bool LlamaAgentBackend::selectedSessionBusy() const
{
    if (LlamaAgentBackend *runtime = viewRuntime()) return runtime->isBusy();
    return isBusy();
}

int LlamaAgentBackend::queuedCount() const
{
    if (LlamaAgentBackend *runtime = viewRuntime()) return runtime->m_msgQueue.size();
    return m_msgQueue.size();
}

QStringList LlamaAgentBackend::queuedMessages() const
{
    if (LlamaAgentBackend *runtime = viewRuntime()) return runtime->m_msgQueue;
    return m_msgQueue;
}

QVariantList LlamaAgentBackend::sessions() const
{
    QVariantList decorated = m_sessions;
    for (int i = 0; i < decorated.size(); ++i) {
        QVariantMap session = decorated.at(i).toMap();
        const QString id = session.value(QStringLiteral("id")).toString();
        const LlamaAgentBackend *runtime = m_sessionRuntimes.value(id, nullptr);
        const bool active = runtime ? runtime->isBusy()
                                    : (id == m_sessionId && isBusy());
        const int queued = runtime ? runtime->m_msgQueue.size()
                                   : (id == m_sessionId ? m_msgQueue.size() : 0);
        session[QStringLiteral("runtimeState")] = active
            ? QStringLiteral("running")
            : (queued > 0 ? QStringLiteral("queued") : QStringLiteral("idle"));
        session[QStringLiteral("queuedCount")] = queued;
        decorated[i] = session;
    }
    return decorated;
}

// Encolar: si está ocupado, guarda y se enviará al terminar el turno. Si no,
// envía ya.
void LlamaAgentBackend::queueMessage(const QString &text)
{
    if (!m_isSessionRuntime) {
        if (LlamaAgentBackend *runtime = viewRuntime()) {
            runtime->queueMessage(text);
            return;
        }
    }
    const QString t = text.trimmed();
    if (!m_running || t.isEmpty()) return;
    if (!isBusy()) { sendMessage(t); return; }
    m_msgQueue << t;
    emit queueChanged();
    emit logAppended(QStringLiteral("[encolado (%1 en cola): %2]\n")
                         .arg(m_msgQueue.size()).arg(t.left(80)));
}

// Steering: interrumpe el turno actual (cancela tools/aprobación, repara el
// historial) y envía el mensaje nuevo de inmediato.
void LlamaAgentBackend::steerMessage(const QString &text)
{
    if (!m_isSessionRuntime) {
        if (LlamaAgentBackend *runtime = viewRuntime()) {
            runtime->steerMessage(text);
            return;
        }
    }
    const QString t = text.trimmed();
    if (!m_running || t.isEmpty()) return;
    if (isBusy()) {
        finishDurableRun(QStringLiteral("cancelled"),
                         QStringLiteral("reemplazado por steering del usuario"));
        interruptForSteer();
        emit logAppended(QStringLiteral("[steering: turno interrumpido por nuevo mensaje]\n"));
    }
    sendMessage(t);
}

// Envía el próximo mensaje encolado, si lo hay y ya no hay nada en vuelo.
void LlamaAgentBackend::flushQueue()
{
    if (!m_running || m_msgQueue.isEmpty() || isBusy()) return;
    const QString t = m_msgQueue.takeFirst();
    emit queueChanged();
    sendMessage(t);
}

void LlamaAgentBackend::clearQueue()
{
    if (!m_isSessionRuntime) {
        if (LlamaAgentBackend *runtime = viewRuntime()) {
            runtime->clearQueue();
            return;
        }
    }
    if (m_msgQueue.isEmpty()) return;
    m_msgQueue.clear();
    emit queueChanged();
}

// Aborta request/compactación, descarta la burbuja parcial y deja m_apiMessages
// consistente (cada assistant.tool_calls con su respuesta) para que el próximo
// request no sea inválido.
void LlamaAgentBackend::interruptForSteer()
{
    if (m_compactReply) {
        QNetworkReply *cr = m_compactReply; m_compactReply = nullptr;
        cr->disconnect(this); cr->abort(); cr->deleteLater();
    }
    m_compacting = false;
    if (m_reply) {
        // Anular y desconectar ANTES de abort() (abort emite finished/readyRead
        // sincrónico → reentrancy). Igual criterio que cancelGeneration.
        QNetworkReply *r = m_reply; m_reply = nullptr;
        r->disconnect(this); r->abort(); r->deleteLater();
    }
    // Matar el run_shell async en vuelo y cerrar su tarjeta en vivo.
    if (m_worker) QMetaObject::invokeMethod(m_worker, "cancelShell", Qt::QueuedConnection);
    finalizeLiveToolCard(true);
    cancelAllSubs();
    // Cerrar la burbuja en curso (descarta si está vacía; si tenía texto parcial
    // lo deja como mensaje finalizado).
    if (m_curAsstIdx >= 0) closeAssistantBubble();
    m_curAsstIdx = -1;
    // Reparar tool_calls colgados ANTES de limpiar el estado pendiente.
    repairDanglingToolCalls();
    m_pendingCalls = {};
    m_awaitId.clear();
    m_awaitCall = {};
    m_execCallId.clear();   // ignora resultado tardío de una tool en vuelo
    emit desktopActivityChanged(false, QString(), QString());
    setTyping(false);
}

// Cierra la tarjeta de run_shell "en vivo" si quedó abierta (al cancelar/interrumpir).
void LlamaAgentBackend::finalizeLiveToolCard(bool cancelled)
{
    if (m_liveToolMsgIdx < 0 || m_liveToolMsgIdx >= m_messages.size()) {
        m_liveToolCallId.clear(); m_liveToolMsgIdx = -1; return;
    }
    QVariantMap card = m_messages[m_liveToolMsgIdx].toMap();
    card[QStringLiteral("typing")] = false;
    if (cancelled) {
        card[QStringLiteral("ok")] = false;
        const QString o = card.value(QStringLiteral("output")).toString();
        card[QStringLiteral("output")] = o + (o.isEmpty() ? QString() : QStringLiteral("\n"))
                                         + QStringLiteral("[cancelado por el usuario]");
    }
    m_messages[m_liveToolMsgIdx] = card;
    emit messagesChanged();
    m_liveToolCallId.clear();
    m_liveToolMsgIdx = -1;
}

// Si el último assistant tiene tool_calls sin su mensaje 'tool' de respuesta,
// agrega stubs "[interrumpido]" para que el historial quede válido.
void LlamaAgentBackend::repairDanglingToolCalls()
{
    int lastAsst = -1;
    for (int i = m_apiMessages.size() - 1; i >= 0; --i) {
        const QJsonObject o = m_apiMessages[i].toObject();
        const QString role = o.value(QStringLiteral("role")).toString();
        if (role == QLatin1String("assistant")
            && o.contains(QStringLiteral("tool_calls"))) { lastAsst = i; break; }
        if (role == QLatin1String("user")) return;   // no hay turno de tools abierto
    }
    if (lastAsst < 0) return;

    QSet<QString> unanswered;
    for (const QJsonValue &v : m_apiMessages[lastAsst].toObject()
                                  .value(QStringLiteral("tool_calls")).toArray())
        unanswered.insert(v.toObject().value(QStringLiteral("id")).toString());
    for (int i = lastAsst + 1; i < m_apiMessages.size(); ++i) {
        const QJsonObject o = m_apiMessages[i].toObject();
        if (o.value(QStringLiteral("role")).toString() == QLatin1String("tool"))
            unanswered.remove(o.value(QStringLiteral("tool_call_id")).toString());
    }
    for (const QString &id : std::as_const(unanswered))
        if (!id.isEmpty())
            appendToolResult(id, QString(),
                             QStringLiteral("[interrumpido por el usuario]"));
}

QJsonArray LlamaAgentBackend::trimStaleImages(const QJsonArray &messages, int keepLast)
{
    // Índices (desde el final) de mensajes que contienen image_url.
    auto hasImage = [](const QJsonObject &m) {
        const QJsonValue c = m.value(QStringLiteral("content"));
        if (!c.isArray()) return false;
        const QJsonArray parts = c.toArray();
        for (const QJsonValue &p : parts)
            if (p.toObject().value(QStringLiteral("type")).toString()
                    == QLatin1String("image_url"))
                return true;
        return false;
    };
    QJsonArray out;
    // Recorrer de atrás hacia adelante para saber cuáles son los últimos keepLast.
    QList<int> imageIdx;
    for (int i = messages.size() - 1; i >= 0; --i)
        if (hasImage(messages.at(i).toObject())) imageIdx.append(i);
    for (int i = 0; i < messages.size(); ++i) {
        QJsonObject m = messages.at(i).toObject();
        const int fromEnd = imageIdx.indexOf(i);   // posición entre los con-imagen
        if (fromEnd < 0 || fromEnd < keepLast) { out.append(m); continue; }
        // Mensaje con imagen vieja: reemplazar las partes image_url por texto.
        QJsonArray parts = m.value(QStringLiteral("content")).toArray();
        QJsonArray np;
        bool stripped = false;
        for (const QJsonValue &pv : parts) {
            const QJsonObject p = pv.toObject();
            if (p.value(QStringLiteral("type")).toString() == QLatin1String("image_url")) {
                stripped = true;
                continue;
            }
            np.append(p);
        }
        if (stripped)
            np.append(QJsonObject{
                {QStringLiteral("type"), QStringLiteral("text")},
                {QStringLiteral("text"),
                 QStringLiteral("[captura de pantalla omitida: desactualizada; pedí otra si la necesitás]")}});
        m[QStringLiteral("content")] = np;
        out.append(m);
    }
    return out;
}

// Payload del warmup (pura, testeable): mismo prefijo que un turno real
// (messages+tools+kwargs de template) pero sin generar (max_tokens=1, sin stream).
QJsonObject LlamaAgentBackend::buildWarmupPayload(const QJsonArray &wireMessages,
                                                  const QJsonArray &tools,
                                                  const QString &modelId,
                                                  double temperature,
                                                  bool thinkingEnabled,
                                                  const QString &reasoningEffort)
{
    QJsonObject payload{
        {QStringLiteral("model"), modelId},
        {QStringLiteral("messages"), wireMessages},
        {QStringLiteral("tools"), tools},
        {QStringLiteral("tool_choice"), QStringLiteral("auto")},
        {QStringLiteral("parallel_tool_calls"), true},
        {QStringLiteral("parse_tool_calls"), false},
        {QStringLiteral("max_tokens"), 1},
        {QStringLiteral("stream"), false},
        {QStringLiteral("cache_prompt"), true}
    };
    if (temperature >= 0.0) payload.insert(QStringLiteral("temperature"), temperature);
    payload.insert(QStringLiteral("reasoning_budget"), thinkingEnabled ? -1 : 0);
    payload.insert(QStringLiteral("chat_template_kwargs"),
                   ReasoningWire::templateKwargs(thinkingEnabled, false,
                                                 reasoningEffort));
    return payload;
}

// Precalienta el prompt-cache del server: mismo prefijo que el próximo turno
// (system+tools+historial, SIN el mensaje nuevo del usuario) con max_tokens=1 y
// cache_prompt=true. llama.cpp reusa el KV por prefijo más largo → cuando llegue
// el turno real solo evalúa el sufijo nuevo. Fire-and-forget: el resultado se
// descarta. Lo dispara Ingi Charla al detectar que el usuario empezó a hablar.
void LlamaAgentBackend::prefillWarmup()
{
    if (!m_contextPolicy.warmup) return;
    if (!m_running || m_reply || m_warmupReply || m_compactReply || m_compacting
        || !m_awaitId.isEmpty())
        return;
    ensureSession();
    // Misma poda que runCompletion: el prefijo cacheado debe coincidir byte a byte.
    m_apiMessages = trimStaleImages(m_apiMessages, m_contextPolicy.keepLastImages);
    const QJsonArray wire = sanitizeApiMessagesForWire(m_apiMessages, m_ctx.modelId);
    if (wire.isEmpty()) return;

    QJsonObject payload = buildWarmupPayload(
        wire, buildToolSchemas(),
        m_ctx.modelId.isEmpty() ? QStringLiteral("local") : m_ctx.modelId,
        m_temperature, m_thinkingEnabled, m_reasoningEffort);
    if (usingTextTools()) {
        payload = buildTextToolPayload(payload);
        payload[QStringLiteral("max_tokens")] = 1;
        payload[QStringLiteral("stream")] = false;
        payload[QStringLiteral("cache_prompt")] = true;
    }

    const QString url = m_ctx.serverBaseUrl + QStringLiteral("/v1/chat/completions");
    QNetworkRequest req((QUrl(url)));
    applyHeaders(req);
    m_warmupReply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    emit logAppended(QStringLiteral("[warmup] prefill del prompt-cache (msgs=%1)\n")
                         .arg(wire.size()));
    qInfo().noquote() << QStringLiteral("[charla] warmup: prefill enviado (msgs=%1, url=%2)")
                             .arg(wire.size()).arg(url);
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    connect(m_warmupReply, &QNetworkReply::finished, this, [this, t0]() {
        if (!m_warmupReply) return;
        const bool ok = (m_warmupReply->error() == QNetworkReply::NoError);
        qInfo().noquote() << QStringLiteral("[charla] warmup: %1 en %2 ms%3")
                                 .arg(ok ? QStringLiteral("listo") : QStringLiteral("FALLÓ"))
                                 .arg(QDateTime::currentMSecsSinceEpoch() - t0)
                                 .arg(ok ? QString()
                                         : QStringLiteral(" (") + m_warmupReply->errorString()
                                               + QLatin1Char(')'));
        m_warmupReply->deleteLater();
        m_warmupReply = nullptr;      // resultado descartado; solo importa el KV
    });
}

void LlamaAgentBackend::runCompletion()
{
    if (!m_running) return;
    if (m_compacting) return;                    // esperando el resumen; se reanuda al terminar

    // Auto-compactación vía modelo ANTES de contar la iteración. En Tasks
    // autónomas cortas (desktop/browser) la compactación intermedia degrada el
    // estado operativo: resume justo después de tools como desktop_windows/focus
    // y modelos chicos pierden qué ventana/acción sigue. Ahí preferimos mantener
    // el rastro literal y apoyarnos en el catálogo recortado de tools.
    if (!m_taskAutoApprove) {
        pruneWorkingContext();
        int head = 0, keepFrom = 0;
        if (m_compactStall < 2 && planCompaction(head, keepFrom)) {
            startCompaction(head, keepFrom); return;
        }
        if (m_compactStall >= 2 && planCompaction(head, keepFrom)) {
            // Compactar no reduce más (head protegido pesado + budget conservador).
            // No entrar en loop infinito: seguir con el contexto actual, que igual
            // entra en n_ctx real. Log una sola vez por racha.
            if (m_compactStall == 2) {
                ++m_compactStall;   // marcar avisado (>=3) para no repetir el log
                emit logAppended(QStringLiteral(
                    "[compactación sin progreso; sigo con el contexto actual (entra en n_ctx)]\n"));
            }
        }
    }

    if (++m_turnIters > kMaxTurnIters) {
        finishTurn(QStringLiteral("[corté el turno: se alcanzó el límite de %1 iteraciones de tools]")
                       .arg(kMaxTurnIters), true, QStringLiteral("failed"));
        return;
    }

    // Reflejar uso de contexto estimado en la UI (se actualiza con 'usage' real
    // del server si llega en el chunk final).
    if (m_ctxLimit > 0) emit contextUsage(estimateApiTokens(), m_ctxLimit);

    // Poda persistente de capturas viejas: solo la última imagen queda en el
    // historial (las anteriores → placeholder de texto). Sin esto, sesiones con
    // varias desktop_observe acumulaban 50k+ tokens de prompt (minutos de prefill).
    m_apiMessages = trimStaleImages(m_apiMessages, m_contextPolicy.keepLastImages);

    QJsonArray wireMessages = sanitizeApiMessagesForWire(m_apiMessages, m_ctx.modelId);
    const QByteArray beforeWire = QJsonDocument(m_apiMessages).toJson(QJsonDocument::Compact);
    const QByteArray afterWire = QJsonDocument(wireMessages).toJson(QJsonDocument::Compact);
    if (afterWire != beforeWire) {
        emit logAppended(QStringLiteral("[turn] historial saneado para wire: %1 → %2 mensajes\n")
                             .arg(m_apiMessages.size()).arg(wireMessages.size()));
        m_apiMessages = wireMessages;
    }

    // Reserva de salida acotada al ctx del perfil (evita pedir más de lo que entra).
    const int outReserve = (m_ctxLimit > 0) ? qMin(32768, m_ctxLimit / 4) : 32768;

    QJsonObject payload{
        {QStringLiteral("model"), m_ctx.modelId.isEmpty() ? QStringLiteral("local") : m_ctx.modelId},
        {QStringLiteral("messages"), wireMessages},
        {QStringLiteral("tools"), buildToolSchemas()},
        {QStringLiteral("tool_choice"), QStringLiteral("auto")},
        // Habilitado para que el modelo pueda lanzar varias `task` en paralelo.
        // El loop serializa los tools normales igual (uno por onToolExecuted).
        {QStringLiteral("parallel_tool_calls"), true},
        // Parsear tool calls del lado server puede devolver 500 si el modelo
        // emite argumentos JSON parciales durante el streaming. Lo manejamos
        // del lado cliente para tolerar errores y permitir reintentos.
        {QStringLiteral("parse_tool_calls"), false},
        // Cap de salida alto: el perfil server puede traer --predict 4096, que
        // trunca un write_file con archivo grande a mitad del JSON de args →
        // tool_call inválido → reintentos. max_tokens en el request pisa al
        // default del server y deja completar el contenido del archivo.
        {QStringLiteral("max_tokens"), outReserve},
        {QStringLiteral("stream"), true},
        // Reutilizar el KV/prompt-cache del server entre iteraciones del loop de
        // tools. En cada follow-up el prefijo (system+tools+historial) es estable,
        // así el server procesa sólo el sufijo nuevo en vez de re-evaluar todo el
        // contexto. No dependemos del default del fork (que puede venir en false).
        {QStringLiteral("cache_prompt"), true},
        // Pedir el bloque `usage` en el chunk final del stream → tokens reales de
        // generación (en vez de estimar chars/4) para métricas/tps fiables.
        {QStringLiteral("stream_options"), QJsonObject{{QStringLiteral("include_usage"), true}}}
    };
    if (m_temperature >= 0.0) payload.insert(QStringLiteral("temperature"), m_temperature);
    if (m_seed >= 0) payload.insert(QStringLiteral("seed"), m_seed);
    // Razonamiento controlado por el toggle global de la app, no por el perfil.
    payload.insert(QStringLiteral("reasoning_budget"),
                   m_thinkingEnabled ? m_reasoningBudget : 0);
    payload.insert(QStringLiteral("chat_template_kwargs"),
                   thinkingTemplateKwargs(m_thinkingEnabled, m_thinkingLeakGuard,
                                          m_reasoningEffort));

    // Crear la burbuja del asistente (typing) ANTES de disparar el request, no
    // recién al primer token. En turnos de follow-up (tras una tool) la burbuja
    // previa quedó cerrada, así que durante el hueco "request enviado → primer
    // token" no había ningún mensaje con typing=true: la UI parecía idle y el
    // botón seguía en "Enviar", dejando mandar un mensaje que el backend rechaza
    // con "Hay un turno en curso". Con la burbuja creada ya, el botón pasa a
    // "PARAR" y se ve el indicador de generación. Si la respuesta resulta ser
    // sólo tool-calls (sin texto), closeAssistantBubble descarta la burbuja vacía.
    ensureAssistantBubble();
    setAssistantStatus(m_turnIters <= 1 ? QStringLiteral("Pensando...")
                                        : QStringLiteral("Revisando resultados..."));

    emit logAppended(QStringLiteral("[turn] requesting completion (iter=%1, msgs=%2, stream)\n")
                         .arg(m_turnIters).arg(m_apiMessages.size()));
    postCompletionRequest(usingTextTools() ? buildTextToolPayload(payload) : payload,
                          usingTextTools() ? TextTools : NativeFull);
}

QJsonObject LlamaAgentBackend::buildTextToolPayload(const QJsonObject &nativePayload) const
{
    QJsonArray messages = nativePayload.value(QStringLiteral("messages")).toArray();

    QStringList tools;
    const QJsonArray schemas = buildToolSchemas();
    for (const QJsonValue &v : schemas) {
        const QJsonObject fn = v.toObject().value(QStringLiteral("function")).toObject();
        const QString name = fn.value(QStringLiteral("name")).toString();
        if (name.isEmpty()) continue;
        const QJsonArray required = fn.value(QStringLiteral("parameters")).toObject()
                                        .value(QStringLiteral("required")).toArray();
        QStringList reqs;
        for (const QJsonValue &rv : required) reqs << rv.toString();
        tools << (reqs.isEmpty()
                  ? name
                  : QStringLiteral("%1(%2)").arg(name, reqs.join(QStringLiteral(", "))));
    }

    const QString protocol = QStringLiteral(
        "MODO COMPATIBLE SIN TOOL-CALLING NATIVO.\n"
        "El servidor local rechazo el payload OpenAI tools, pero la app puede ejecutar "
        "tools si las pedis por texto.\n"
        "Tools disponibles: %1.\n"
        "Para usar una tool respondé SOLO una línea con este formato exacto:\n"
        "TOOL_CALL {\"name\":\"web_fetch\",\"arguments\":{\"url\":\"https://example.com\"}}\n"
        "UN SOLO TOOL_CALL por turno: no listes varias ni adivines rutas; esperá "
        "el TOOL_RESULT.\n"
        "No agregues explicación junto al TOOL_CALL. NO razones en voz alta ni uses "
        "etiquetas <think>: tu PRIMERA línea debe ser el TOOL_CALL (o el resultado "
        "final si ya terminaste). Nunca respondas vacío. La app ejecutará la tool y te "
        "devolverá TOOL_RESULT como mensaje del usuario. Luego continuá. Cuando el "
        "objetivo esté cumplido, respondé normalmente con el resultado final.")
        .arg(tools.join(QStringLiteral("; ")));

    if (!messages.isEmpty()
        && messages.first().toObject().value(QStringLiteral("role")).toString() == QLatin1String("system")) {
        QJsonObject first = messages.first().toObject();
        first[QStringLiteral("content")] =
            first.value(QStringLiteral("content")).toString() + QStringLiteral("\n\n") + protocol;
        messages[0] = first;
    } else {
        messages.prepend(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                     {QStringLiteral("content"), protocol}});
    }

    // Backstop de generación: en modo texto, un tool-call es UNA línea corta. Sin
    // límite el modelo puede rambear hasta max_tokens (con ctx/4=2048 en un perfil
    // de 8k eso son ~5 min de generación local → la Task parecía colgada). Capamos
    // a un valor holgado para un tool-call o una respuesta final corta, sin
    // estrangular una respuesta de chat legítima.
    int maxTok = nativePayload.value(QStringLiteral("max_tokens")).toInt(2048);
    maxTok = qBound(256, maxTok, 1536);

    QJsonObject payload{
        {QStringLiteral("model"), nativePayload.value(QStringLiteral("model"))},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("max_tokens"), maxTok},
        {QStringLiteral("stream"), true},
        // Cortar apenas el modelo cierra el tool-call. Algunos modelos (Gemma)
        // escupen su formato nativo <|tool_call>call:x{...}<tool_call|> y SIGUEN
        // generando después del cierre (ramble hasta max_tokens). Parar en los
        // marcadores de cierre corta al instante; el parser no necesita el cierre.
        {QStringLiteral("stop"), QJsonArray{
            QStringLiteral("<tool_call|>"), QStringLiteral("<|tool_call|>"),
            QStringLiteral("</tool_call>"), QStringLiteral("<end_of_turn>")}}
    };
    if (nativePayload.contains(QStringLiteral("temperature")))
        payload.insert(QStringLiteral("temperature"), nativePayload.value(QStringLiteral("temperature")));
    return payload;
}

void LlamaAgentBackend::postCompletionRequest(QJsonObject payload, CompletionMode mode)
{
    if (!m_running) return;
    const QString url = m_ctx.serverBaseUrl + QStringLiteral("/v1/chat/completions");
    QNetworkRequest req((QUrl(url)));
    applyHeaders(req);

    if (mode == NativeCompat) {
        payload.remove(QStringLiteral("parallel_tool_calls"));
        payload.remove(QStringLiteral("parse_tool_calls"));
        payload.remove(QStringLiteral("stream_options"));
        payload.remove(QStringLiteral("cache_prompt"));
        payload.remove(QStringLiteral("reasoning_budget"));
        payload.remove(QStringLiteral("chat_template_kwargs"));
    }

    resetStreamState();
    m_streamIdleTimedOut = false;
    m_reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    emit sessionsChanged();
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() { handleStreamData(); });
    connect(m_reply, &QNetworkReply::finished, this, [this, payload, mode]() {
        if (m_streamIdleTimer) m_streamIdleTimer->stop();
        QNetworkReply *r = m_reply;
        if (!r) return;
        const bool ok = r->error() == QNetworkReply::NoError;
        QString err = m_streamIdleTimedOut
            ? QStringLiteral("el servidor no envió datos del stream durante %1s")
                  .arg(streamIdleTimeoutMs() / 1000)
            : r->errorString();
        QString body;
        if (!ok) {
            body = QString::fromUtf8(r->readAll()).trimmed();
            // En streaming el body ya lo drenó handleStreamData; recuperarlo de ahí.
            if (body.isEmpty() && !m_streamErrBody.isEmpty())
                body = QString::fromUtf8(m_streamErrBody).trimmed();
            if (!body.isEmpty())
                err += QStringLiteral(" · %1").arg(body.left(1000));
        }
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_reply = nullptr;
        r->deleteLater();
        // Cuerpo del 400: es la razón real por la que el server rechaza el payload
        // (template sin tools, schema que rompe el grammar, binario viejo…). Sin
        // esto el fallback funcionaba pero la causa raíz quedaba invisible.
        const QString why = body.isEmpty() ? QString()
                                           : QStringLiteral(" · motivo: %1").arg(body.left(400));
        if (!ok && mode == NativeFull && status == 400) {
            emit logAppended(QStringLiteral("[turn] server rechazó payload completo (400); "
                                            "reintentando modo compatible sin campos opcionales%1\n").arg(why));
            postCompletionRequest(payload, NativeCompat);
            return;
        }
        if (!ok && mode == NativeCompat && status == 400
            && m_toolProtocol != QLatin1String("native")) {
            emit logAppended(QStringLiteral("[turn] server rechazó OpenAI tools (400); "
                                            "reintentando protocolo textual de tools headless%1\n").arg(why));
            m_textToolFallback = true;
            postCompletionRequest(buildTextToolPayload(payload), TextTools);
            return;
        }
        if (!ok && mode == NativeCompat && status == 400) {
            // El perfil declaró protocolo nativo: NO degradar a texto en silencio.
            // Que el error se vea es el punto — un perfil con un modelo sin
            // tool-template tiene que corregirse, no disimularse.
            emit logAppended(QStringLiteral("[turn] server rechazó OpenAI tools (400) y el perfil "
                                            "exige protocolo nativo; sin fallback textual%1\n").arg(why));
        }
        const RetryClass retryClass = classifyCompletionError(status, err + QLatin1Char(' ') + body);
        if (!ok && retryClass == RetryContextOverflow && m_contextRecoveries < 2) {
            ++m_contextRecoveries;
            emit logAppended(QStringLiteral(
                "[turn] overflow de contexto; compactación de emergencia (%1/2)\n")
                                 .arg(m_contextRecoveries));
            closeAssistantBubble();
            int head = 0, keepFrom = 0;
            if (planCompaction(head, keepFrom)) {
                startCompaction(head, keepFrom);
            } else if (m_apiMessages.size() > 7) {
                applyCompaction(2, m_apiMessages.size() - 4, QString());
                QTimer::singleShot(0, this, [this]() { if (m_running) runCompletion(); });
            } else {
                handleStreamFinished(false, err);
            }
            return;
        }
        if (!ok && retryClass == RetryTransient
            && m_transportRetries < m_loopPolicy.transportRetries) {
            const int attempt = ++m_transportRetries;
            const int delayMs = qMin(kMaxTransportRetryDelayMs,
                                     500 * (1 << qMin(attempt - 1, 4)));
            emit logAppended(QStringLiteral(
                "[turn] error transitorio HTTP %1; llama-agent conserva el turno "
                "mientras llama-server reinicia (reintento %2/%3 en %4 ms)\n")
                                 .arg(status).arg(attempt).arg(m_loopPolicy.transportRetries)
                                 .arg(delayMs));
            closeAssistantBubble();
            QTimer::singleShot(delayMs, this, [this, payload, mode]() {
                if (m_running && !m_reply) postCompletionRequest(payload, mode);
            });
            return;
        }
        handleStreamFinished(ok, err);
    });
    resetStreamIdleWatchdog();
}

void LlamaAgentBackend::resetStreamState()
{
    m_sseBuf.clear();
    m_streamErrBody.clear();
    m_streamContent.clear();
    m_streamReason.clear();
    m_streamRepetitionDetected = false;
    m_streamToolCallCut = false;
    m_streamToolCalls.clear();
    m_genTokens = 0;
    m_genMs = 0.0;
    // Base = lo que el bubble ya muestra (texto previo + marcadores 🔧 de tools).
    m_streamBase.clear();
    if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size())
        m_streamBase = m_messages[m_curAsstIdx].toMap().value(QStringLiteral("content")).toString();
}

// Precedencia: spec del perfil > env LLAMACODE_STREAM_IDLE_TIMEOUT > 3600s.
// El spec gana porque es la decisión explícita del usuario para ESTE perfil; la
// env queda como override de máquina/CI.
int LlamaAgentBackend::streamIdleTimeoutMs() const
{
    if (m_loopPolicy.streamIdleTimeoutSec > 0)
        return qBound(30, m_loopPolicy.streamIdleTimeoutSec, 24 * 60 * 60) * 1000;
    const QByteArray env = qgetenv("LLAMACODE_STREAM_IDLE_TIMEOUT");
    bool ok = false;
    int seconds = QString::fromLatin1(env).trimmed().toInt(&ok);
    if (!ok || seconds <= 0)
        seconds = 3600;
    seconds = qBound(30, seconds, 24 * 60 * 60);
    return seconds * 1000;
}

void LlamaAgentBackend::resetStreamIdleWatchdog()
{
    if (!m_reply) return;
    if (!m_streamIdleTimer) {
        m_streamIdleTimer = new QTimer(this);
        m_streamIdleTimer->setSingleShot(true);
        connect(m_streamIdleTimer, &QTimer::timeout, this, [this]() {
            if (!m_reply) return;
            m_streamIdleTimedOut = true;
            emit logAppended(QStringLiteral("[turn] stream sin actividad por %1s; abortando request\n")
                                 .arg(streamIdleTimeoutMs() / 1000));
            m_reply->abort();
        });
    }
    m_streamIdleTimer->start(streamIdleTimeoutMs());
}

// Ver el comentario de la declaración en el header: los templates que hoistean
// system al tope rompen tanto la posición de la nota como el prompt-cache.
QString LlamaAgentBackend::midConversationNoteRole(const QString &modelId)
{
    static const QRegularExpression dsV4(
        QStringLiteral("deepseek[^a-z0-9]*v4"), QRegularExpression::CaseInsensitiveOption);
    if (dsV4.match(modelId).hasMatch())
        return QStringLiteral("latest_reminder");
    return QStringLiteral("user");
}

void LlamaAgentBackend::mergeToolCallDelta(QHash<int, QJsonObject> &acc,
                                          const QJsonArray &deltaToolCalls)
{
    for (const QJsonValue &tv : deltaToolCalls) {
        const QJsonObject tc = tv.toObject();
        const int idx = tc.value(QStringLiteral("index")).toInt(0);
        QJsonObject a = acc.value(idx);
        if (tc.contains(QStringLiteral("id")))
            a[QStringLiteral("id")] = tc.value(QStringLiteral("id")).toString();
        const QJsonObject fn = tc.value(QStringLiteral("function")).toObject();
        if (fn.contains(QStringLiteral("name")))
            a[QStringLiteral("name")] = fn.value(QStringLiteral("name")).toString();
        if (fn.contains(QStringLiteral("arguments")))
            a[QStringLiteral("arguments")] =
                a.value(QStringLiteral("arguments")).toString()
                + fn.value(QStringLiteral("arguments")).toString();
        acc.insert(idx, a);
    }
}

// Une los deltas incrementales de tool_calls (patrón OpenAI streaming): por
// cada 'index' se acumulan id, function.name y function.arguments (string).
void LlamaAgentBackend::handleStreamData()
{
    if (!m_reply) return;
    // Respuesta de error (4xx/5xx): NO es SSE, es un JSON de error de una sola vez.
    // Si lo parseáramos como stream, las líneas sin "data: " se descartan y el
    // motivo del fallo se pierde. Lo acumulamos crudo para loguearlo en finished.
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400) {
        m_streamErrBody.append(m_reply->readAll());
        resetStreamIdleWatchdog();
        return;
    }
    m_sseBuf.append(m_reply->readAll());
    resetStreamIdleWatchdog();
    while (true) {
        const int nl = m_sseBuf.indexOf('\n');
        if (nl < 0) break;
        const QByteArray line = m_sseBuf.left(nl).trimmed();
        m_sseBuf.remove(0, nl + 1);
        if (!line.startsWith("data: ")) continue;
        const QByteArray data = line.mid(6).trimmed();
        if (data == "[DONE]") continue;
        const QJsonDocument d = QJsonDocument::fromJson(data);
        if (!d.isObject()) continue;
        const QJsonObject obj = d.object();
        // Algunos servers mandan 'usage' en el chunk final (stream_options).
        const QJsonObject usage = obj.value(QStringLiteral("usage")).toObject();
        const int used = usage.value(QStringLiteral("total_tokens")).toInt(-1);
        if (used >= 0) emit contextUsage(used, m_ctxLimit);
        const int promptTok = usage.value(QStringLiteral("prompt_tokens")).toInt(-1);
        if (promptTok >= 0) m_lastPromptTokens = promptTok;
        // Métricas reales de generación. `usage.completion_tokens` = tokens
        // generados (exacto). `timings` (llama.cpp) trae predicted_n/predicted_ms
        // → tiempo de generación puro, sin prompt-processing. Preferir timings.
        const int compTok = usage.value(QStringLiteral("completion_tokens")).toInt(-1);
        if (compTok >= 0) m_genTokens = compTok;
        const QJsonObject timings = obj.value(QStringLiteral("timings")).toObject();
        if (!timings.isEmpty() || !obj.value(QStringLiteral("usage")).toObject().isEmpty()) {
            const QString phase = m_approvalMode == QLatin1String("plan")
                ? QStringLiteral("plan") : QStringLiteral("execute");
            m_efficiencyRequests.append(
                AgentEfficiency::Request::fromResponse(obj, phase).toVariant());
        }
        if (!timings.isEmpty()) {
            const int pn = timings.value(QStringLiteral("predicted_n")).toInt(-1);
            const double pms = timings.value(QStringLiteral("predicted_ms")).toDouble(-1.0);
            if (pn >= 0)  m_genTokens = pn;
            if (pms > 0.0) m_genMs = pms;
        }
        if (promptTok >= 0 || !timings.isEmpty())
            emit contextManaged(estimateApiTokens(), estimateMessageTokens(m_transcriptMessages),
                                m_contextPrunedTokens, m_contextPrunedMessages);

        const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) continue;
        const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
        if (m_thinkingEnabled)
            m_streamReason += delta.value(QStringLiteral("reasoning_content")).toString();
        m_streamContent += delta.value(QStringLiteral("content")).toString();

        // Modo texto: un turno = UN tool-call. Algunos modelos escupen una
        // ráfaga de decenas de TOOL_CALL en una sola generación (paths
        // inventados incluidos): sólo el primero se ejecuta y el resto queda
        // como texto basura en el transcript, quemando minutos de generación.
        // Cortamos apenas empieza el segundo.
        if (usingTextTools() && !m_streamToolCallCut) {
            const int cut = secondTextToolCallStart(m_streamContent);
            if (cut > 0) {
                m_streamContent.truncate(cut);
                m_streamToolCallCut = true;
                emit logAppended(QStringLiteral(
                    "[turn] ráfaga de TOOL_CALL en modo texto; corto en el primero\n"));
                if (m_reply) m_reply->abort();
            }
        }

        // Loop dentro de UNA generación: algunos modelos repiten un párrafo hasta
        // agotar n_predict, por lo que el anti-loop de tool calls nunca llega a
        // observarlo. Exigimos tres copias exactas de un bloque largo para evitar
        // cortar listas, código corto o énfasis legítimo.
        if (!m_streamRepetitionDetected) {
            QString *candidate = !m_streamContent.isEmpty() ? &m_streamContent
                                                             : &m_streamReason;
            const int repeatedAt = repeatedSuffixStart(*candidate);
            if (repeatedAt >= 0) {
                const int repeatedLen = candidate->size() - repeatedAt;
                candidate->truncate(repeatedAt + repeatedLen / 3);
                *candidate += QStringLiteral(
                    "\n\n[generación detenida: el modelo repitió el mismo bloque tres veces]");
                m_streamRepetitionDetected = true;
                emit logAppended(QStringLiteral(
                    "[anti-loop] repetición textual detectada durante el stream; abortando generación\n"));
                AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                                      QJsonObject{{QStringLiteral("reason"),
                                                   QStringLiteral("stream_repetition")},
                                                  {QStringLiteral("repeatedChars"), repeatedLen}});
                if (m_reply) m_reply->abort();
            }
        }

        mergeToolCallDelta(m_streamToolCalls, delta.value(QStringLiteral("tool_calls")).toArray());

        // Progreso de tool_calls en streaming. Cuando el modelo está generando
        // una tool (p.ej. write_file con un archivo grande), los tokens llegan
        // como delta.tool_calls.arguments, NO como delta.content. Sin esto el
        // bubble no mostraba NADA durante toda la generación —que a ~40 tok/s
        // para un archivo de ~6k tokens son ~2-3 min— y la UI parecía colgada.
        // Mostramos un indicador en vivo con la tool y el tamaño acumulado.
        const bool toolStreaming = !m_streamToolCalls.isEmpty();

        // Abrir una burbuja nueva si la anterior se cerró tras una tool.
        if ((!m_streamContent.isEmpty() || (m_thinkingEnabled && !m_streamReason.isEmpty()) || toolStreaming)
            && m_curAsstIdx < 0)
            ensureAssistantBubble();

        // Mostrar en vivo: base + <think>razonamiento</think> + respuesta.
        QString full = m_streamBase;
        if (m_thinkingEnabled && !m_streamReason.isEmpty())
            full += QStringLiteral("<think>") + m_streamReason + QStringLiteral("</think>\n");
        full += visibleAnswer(m_streamContent, m_thinkingEnabled, m_thinkingLeakGuard);
        // Chars acumulados de args de tool en streaming. Cuando el modelo genera una
        // tool (p.ej. write_file con un archivo grande) los tokens llegan como
        // delta.tool_calls.arguments, NO como content. Hay que contarlos para el
        // medidor de tokens/tps; sin esto la UI mostraba "0 tokens / 0 tps" y parecía
        // colgada durante toda la generación de la tool.
        int totalArgChars = 0;
        QString firstToolName;
        if (toolStreaming) {
            const QList<int> ks = m_streamToolCalls.keys();
            for (int k : ks) {
                const QJsonObject tc = m_streamToolCalls.value(k);
                totalArgChars += tc.value(QStringLiteral("arguments")).toString().size();
                if (firstToolName.isEmpty())
                    firstToolName = tc.value(QStringLiteral("name")).toString();
            }
        }
        // Indicador transitorio mientras se generan args de tool (sin texto aún).
        // Se limpia en handleStreamFinished antes de cerrar/finalizar el bubble,
        // así no queda pegado en el chat ni envenena m_streamContent.
        if (toolStreaming && m_streamContent.isEmpty()) {
            const QString toolName = firstToolName.isEmpty() ? QStringLiteral("tool") : firstToolName;
            if (!full.isEmpty()) full += QLatin1Char('\n');
            full += QStringLiteral("⏳ preparando `%1`… (~%2 tokens generados)")
                        .arg(toolName).arg((totalArgChars + 3) / 4);
        }
        if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
            QVariantMap m = m_messages[m_curAsstIdx].toMap();
            // Marca el inicio real de la generación (primer token) para medir tps
            // sin contar el prompt-processing previo.
            if (m.value(QStringLiteral("genStartMs")).toDouble() <= 0.0)
                m[QStringLiteral("genStartMs")] =
                    static_cast<double>(QDateTime::currentMSecsSinceEpoch());
            m[QStringLiteral("content")] = full;
            // Tokens generados REALES = content + razonamiento (si se muestra) + args
            // de tool. No usar estimateTokens(full): incluiría base/indicador y NO los
            // args (que viajan aparte). Así el contador avanza aunque sólo haya tool.
            const int genChars = m_streamContent.size()
                + (m_thinkingEnabled ? m_streamReason.size() : 0) + totalArgChars;
            m[QStringLiteral("tokens")] = (genChars + 3) / 4;
            m_messages[m_curAsstIdx] = m;
            // Throttle a ~30 fps. Durante el stream NO emitimos messagesChanged
            // (reconstruye todo el ListView por cada token → jank). Mandamos sólo
            // el delta de texto de ESTA burbuja vía streamingText; la UI refresca
            // un único delegate. El estado final/estructural lo cierra
            // handleStreamFinished/finishTurn con messagesChanged.
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastUiEmitMs >= 33) {
                m_lastUiEmitMs = now;
                // La UI puede estar mostrando otra sesión; ese delta pertenece
                // al turno activo y no debe reemplazar el historial visible.
                if (m_viewSessionId.isEmpty())
                    emit streamingText(m_curAsstIdx, full);
            }
        }
    }
}

void LlamaAgentBackend::handleStreamFinished(bool ok, const QString &err)
{
    if (!ok && !m_streamRepetitionDetected && !m_streamToolCallCut) {
        finishTurn(QStringLiteral("[error: %1]").arg(err), false,
                   QStringLiteral("failed"));
        return;
    }
    if (ok) {
        // El presupuesto de retry es por request lógico, no por toda la cadena
        // ReAct: una tool exitosa no debe consumir los reintentos de la siguiente.
        m_transportRetries = 0;
        m_contextRecoveries = 0;
    }

    // Quitar el indicador "⏳ preparando…" de tool en streaming: dejar el bubble
    // con el contenido REAL del modelo antes de cerrarlo/finalizarlo. Sin esto el
    // texto transitorio quedaría pegado en el chat y haría que closeAssistantBubble
    // no descarte una burbuja que en realidad está vacía (sólo tool_calls).
    if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
        QString clean = m_streamBase;
        if (m_thinkingEnabled && !m_streamReason.isEmpty())
            clean += QStringLiteral("<think>") + m_streamReason + QStringLiteral("</think>\n");
        clean += visibleAnswer(m_streamContent, m_thinkingEnabled, m_thinkingLeakGuard);
        QVariantMap m = m_messages[m_curAsstIdx].toMap();
        m[QStringLiteral("content")] = clean;
        m_messages[m_curAsstIdx] = m;
    }

    // Reensamblar tool_calls ordenados por index.
    QJsonArray toolCalls;
    QList<int> idxs = m_streamToolCalls.keys();
    std::sort(idxs.begin(), idxs.end());
    for (int i : idxs) {
        const QJsonObject acc = m_streamToolCalls.value(i);
        if (acc.value(QStringLiteral("name")).toString().trimmed().isEmpty())
            continue;
        QString argStr = acc.value(QStringLiteral("arguments")).toString();

        // CRÍTICO: si el modelo truncó los argumentos (p.ej. write_file con
        // contenido enorme que pega contra n_predict), el string queda como JSON
        // inválido/no terminado. Persistir ESE string gigante en m_apiMessages
        // envenena la sesión: el siguiente request lo reenvía y el server al
        // renderizar el tool_call por jinja crashea (0xC0000409). Sanitizamos a
        // un objeto chico que conserva la causa: el executor devolverá una
        // instrucción autocorrectiva en vez de esconder el problema.
        QJsonParseError perr;
        QJsonDocument::fromJson(argStr.toUtf8(), &perr);
        if (perr.error != QJsonParseError::NoError && !argStr.trimmed().isEmpty()) {
            emit logAppended(QStringLiteral(
                "[turn] tool_call con args JSON inválidos (%1, %2 chars) → saneado con _parse_error\n")
                .arg(perr.errorString()).arg(argStr.size()));
            argStr = QString::fromUtf8(QJsonDocument(QJsonObject{
                {QStringLiteral("_parse_error"), perr.errorString()},
                {QStringLiteral("_raw_chars"), argStr.size()}
            }).toJson(QJsonDocument::Compact));
        }

        toolCalls.append(QJsonObject{
            {QStringLiteral("id"), acc.value(QStringLiteral("id")).toString()},
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), QJsonObject{
                {QStringLiteral("name"), acc.value(QStringLiteral("name")).toString()},
                {QStringLiteral("arguments"), argStr}
            }}
        });
    }

    // Normalizar rutas antes de guardar el mensaje assistant.tool_calls en el
    // historial que vuelve al chat-template. Algunos modelos (KAT-Coder en
    // particular) emiten "\nsolution.py\n"; aunque el executor pueda tolerarlo,
    // dejar esos bytes en el historial hace que el modelo los repita y ciertos
    // templates fallen al renderizar la siguiente vuelta. La normalización es
    // sólo para tools locales con path; no altera contenido de archivos ni URLs.
    auto normalizeLocalToolCallPaths = [](QJsonArray calls) {
        static const QSet<QString> pathTools{
            QStringLiteral("read_file"), QStringLiteral("list_dir"),
            QStringLiteral("grep"), QStringLiteral("glob"),
            QStringLiteral("search_docs"), QStringLiteral("semantic_search"),
            QStringLiteral("hybrid_search"), QStringLiteral("repo_slice"),
            QStringLiteral("context_scout"),
            QStringLiteral("verify_claims"), QStringLiteral("write_file"),
            QStringLiteral("edit_file"), QStringLiteral("review_overengineering")};
        for (int i = 0; i < calls.size(); ++i) {
            QJsonObject call = calls.at(i).toObject();
            QJsonObject fn = call.value(QStringLiteral("function")).toObject();
            const QString name = fn.value(QStringLiteral("name")).toString();
            if (!pathTools.contains(name)) continue;
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(
                fn.value(QStringLiteral("arguments")).toString().toUtf8(), &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) continue;
            QJsonObject args = doc.object();
            if (!args.contains(QStringLiteral("path"))) continue;
            QString path = args.value(QStringLiteral("path")).toString().trimmed();
            path.replace(QLatin1Char('\\'), QLatin1Char('/'));
            path = QDir::cleanPath(path);
            if (path.isEmpty()) path = QStringLiteral(".");
            args[QStringLiteral("path")] = path;
            fn[QStringLiteral("arguments")] = QString::fromUtf8(
                QJsonDocument(args).toJson(QJsonDocument::Compact));
            call[QStringLiteral("function")] = fn;
            calls.replace(i, call);
        }
        return calls;
    };
    toolCalls = normalizeLocalToolCallPaths(toolCalls);

    // El historial de API NO lleva <think> (solo display lo lleva).
    const QString apiContent = stripThinkForContext(m_streamContent);

    // Some model templates (notably KAT-Coder's official XML format) emit a
    // native-looking tool call in `content` when llama.cpp is run with
    // --skip-chat-parsing. Parse that fallback in both modes; otherwise the
    // XML is mistaken for the assistant's final code and never executes.
    if (toolCalls.isEmpty()) {
        const QJsonObject textCall = textToolCallFromContent(apiContent);
        if (!textCall.isEmpty()) {
            if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
                QVariantMap m = m_messages[m_curAsstIdx].toMap();
                m[QStringLiteral("content")] = QString();
                m_messages[m_curAsstIdx] = m;
            }
            closeAssistantBubble();
            appendApiMessage(QJsonObject{
                {QStringLiteral("role"), QStringLiteral("assistant")},
                {QStringLiteral("content"), apiContent}});
            toolCalls.append(textCall);
            toolCalls = normalizeLocalToolCallPaths(toolCalls);
        }
    }

    if (toolCalls.isEmpty()) {
        // Turno text-tools VACÍO: ni tool-call parseable ni respuesta visible (el
        // modelo se fue en <think> o cortó por stop sin emitir nada útil). Sin esto
        // la Task muere "sin respuesta final" a mitad de camino (bug 2+2: tras
        // desktop_focus el modelo devolvía vacío y la automatización terminaba en
        // error). Un nudge correctivo lo hace re-emitir la próxima acción; acotado
        // para no loopear.
        static constexpr int kMaxEmptyTextRetries = 2;
        if (usingTextTools() && apiContent.trimmed().isEmpty()
            && m_emptyTextRetries < kMaxEmptyTextRetries) {
            ++m_emptyTextRetries;
            emit logAppended(QStringLiteral("[turn] respuesta vacía en modo text-tools; "
                                            "nudge para re-emitir acción (%1/%2)\n")
                                 .arg(m_emptyTextRetries).arg(kMaxEmptyTextRetries));
            closeAssistantBubble();
            QString guidance = QStringLiteral(
                "No emitiste ninguna acción. NO razones ni uses <think>: "
                "respondé SOLO una línea con el próximo TOOL_CALL para avanzar "
                "el objetivo (o el resultado final si ya terminaste).");
            if (m_lastDesktopTool == QLatin1String("desktop_focus")) {
                guidance += QStringLiteral(
                    "\nÚltimo estado: desktop_focus salió ok. Si la app acepta teclado, "
                    "ahora escribí la entrada completa con desktop_type; no vuelvas a "
                    "desktop_windows ni observes.");
            } else if (m_lastDesktopTool == QLatin1String("desktop_type")) {
                guidance += QStringLiteral(
                    "\nÚltimo estado: desktop_type salió ok. Si la entrada ya incluía la "
                    "acción final (por ejemplo '='), verificá con desktop_controls; si no, "
                    "emití la tecla faltante con desktop_key.");
            } else if (m_lastDesktopTool == QLatin1String("desktop_key")) {
                guidance += QStringLiteral(
                    "\nÚltimo estado: desktop_key salió ok. Verificá el estado actual con "
                    "desktop_controls, no repitas la tecla a ciegas.");
            } else if (m_lastDesktopTool == QLatin1String("desktop_windows")) {
                guidance += QStringLiteral(
                    "\nÚltimo estado: ya tenés el inventario de ventanas. Elegí el id de la "
                    "ventana objetivo y seguí con desktop_focus; no repitas desktop_windows.");
            }
            if (!m_lastDesktopResult.isEmpty())
                guidance += QStringLiteral("\nResultado de la última tool:\n%1")
                                .arg(m_lastDesktopResult.left(1200));
            appendApiMessage(QJsonObject{
                {QStringLiteral("role"), QStringLiteral("user")},
                {QStringLiteral("content"), guidance}});
            runCompletion();
            return;
        }
        if (!apiContent.isEmpty())
            appendApiMessage(QJsonObject{
                {QStringLiteral("role"), QStringLiteral("assistant")},
                {QStringLiteral("content"), apiContent}});
        finishTurn(QString());   // bubble ya tiene el texto; solo finalizar.
        return;
    }
    // Hubo acción/respuesta: resetear el contador de reintentos por vacío.
    m_emptyTextRetries = 0;

    emit logAppended(QStringLiteral("[turn] model requested %1 tool call(s)\n").arg(toolCalls.size()));
    // Cerrar la burbuja de texto previa: las tools van como tarjetas aparte y el
    // próximo texto del modelo abrirá una burbuja nueva.
    closeAssistantBubble();
    if (!usingTextTools()) {
        appendApiMessage(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("assistant")},
            {QStringLiteral("content"), apiContent},
            {QStringLiteral("tool_calls"), toolCalls}});
    }
    m_pendingCalls = toolCalls;
    processPendingCalls();
}

void LlamaAgentBackend::processPendingCalls()
{
    if (m_executionPaused) return;
    if (subsActive()) return;   // esperando que terminen los sub-agentes

    if (m_pendingCalls.isEmpty()) {
        // Todas las tools resueltas. Si alguna devolvió una captura, inyectarla
        // ahora como mensaje user multimodal (después de TODOS los tool_results,
        // para no romper el contrato OpenAI tool_call→tool_result).
        if (!m_pendingObservations.isEmpty()) {
            const QJsonObject obs = buildObservationMessage(m_pendingObservations);
            if (!obs.isEmpty()) appendApiMessage(obs);
            m_pendingObservations.clear();
        }
        // Todas las tools resueltas → re-consultar al modelo con los resultados.
        runCompletion();
        return;
    }

    // ── Subagents: extraer TODAS las tool_calls `task` y lanzarlas en paralelo.
    {
        QJsonArray taskCalls, rest;
        for (const QJsonValue &v : std::as_const(m_pendingCalls)) {
            if (v.toObject().value(QStringLiteral("function")).toObject()
                    .value(QStringLiteral("name")).toString() == QLatin1String("task"))
                taskCalls.append(v);
            else
                rest.append(v);
        }
        if (!taskCalls.isEmpty()) {
            for (const QJsonValue &v : std::as_const(taskCalls)) {
                const QJsonObject call = v.toObject();
                const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
                const QString args = toolArgumentsToString(fn.value(QStringLiteral("arguments")));
                emit agentLifecycleEvent(AgentLifecycle::toolEvent(
                    QStringLiteral("tool.request"), m_sessionId, m_cwd, m_correlationId,
                    call.value(QStringLiteral("id")).toString(), QStringLiteral("task"), args,
                    AgentLifecycle::changedPathsFromToolInput(
                        QStringLiteral("task"), QJsonDocument::fromJson(args.toUtf8()).object())));
            }
            m_pendingCalls = rest;       // los no-task se procesan al terminar los subs
            spawnTasks(taskCalls);
            return;
        }
    }

    QJsonObject call       = m_pendingCalls.first().toObject();
    QJsonObject fn         = call.value(QStringLiteral("function")).toObject();
    QString name           = fn.value(QStringLiteral("name")).toString();
    QString kind           = toolKind(name);
    const QString id       = call.value(QStringLiteral("id")).toString();
    QString argStr         = toolArgumentsToString(fn.value(QStringLiteral("arguments")));
    const QJsonObject requestArgs = QJsonDocument::fromJson(argStr.toUtf8()).object();
    emit agentLifecycleEvent(AgentLifecycle::toolEvent(
        QStringLiteral("tool.request"), m_sessionId, m_cwd, m_correlationId,
        id, name, argStr, AgentLifecycle::changedPathsFromToolInput(name, requestArgs)));
    ensureAssistantBubble();
    setAssistantStatus(toolStatusText(name, kind));

    if (m_harnessEngineId == QLatin1String("next")) {
        bool builtin = false;
        for (const QVariant &entry : toolCatalog()) {
            if (entry.toMap().value(QStringLiteral("name")).toString() == name) {
                builtin = true;
                break;
            }
        }
        if (builtin && !m_harnessCapabilities.canUse(name)) {
            ++m_toolFail;
            m_pendingCalls.removeFirst();
            const QString refusal = QStringLiteral(
                "[capability_refused: la activación Next no concedió '%1']").arg(name);
            AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                                  QJsonObject{{QStringLiteral("tool"), name},
                                              {QStringLiteral("toolCallId"), id},
                                              {QStringLiteral("reason"),
                                               QStringLiteral("capability_revoked_or_denied")}});
            appendToolResult(id, name, refusal);
            processPendingCalls();
            return;
        }
    }

    // ── Robustez: anti-loop ──────────────────────────────────────────────
    const QString sig = toolCallSignature(name, argStr);
    if (sig == m_lastCallSignature) {
        ++m_sameCallStreak;
    } else {
        m_lastCallSignature = sig;
        m_sameCallStreak = 1;
    }
    const int sigCnt = m_sameCallStreak;
    AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("tool_call"),
                          QJsonObject{{QStringLiteral("tool"), name},
                                      {QStringLiteral("correlationId"), m_correlationId},
                                      {QStringLiteral("toolCallId"), id},
                                      {QStringLiteral("toolKind"), kind},
                                      {QStringLiteral("args"), argStr.left(8192)},
                                      {QStringLiteral("repeatCount"), sigCnt}});
    const QString effectId = m_sessionId + QLatin1Char(':') + id;
    if (m_harnessEngineId == QLatin1String("next") && !id.isEmpty()) {
        HarnessEffectRecord effect;
        effect.effectId = effectId;
        effect.kind = kind;
        effect.sessionId = m_sessionId;
        effect.correlationId = m_correlationId;
        effect.payloadHash = QString::fromLatin1(
            QCryptographicHash::hash(argStr.toUtf8(), QCryptographicHash::Sha256).toHex());
        m_harnessEffectLedger.prepare(effect);
    }

    // ── Escalado automático al maestro ───────────────────────────────────
    // Si el agente repite la misma tool sin progreso y el perfil habilitó escalado
    // auto/both, transformamos ESTE tool_call en un ask_teacher (mismo id → el
    // tool_result queda consistente con el assistant message) y dejamos que el
    // maestro resuelva. Una sola vez por firma (anti-recursión).
    if (name != QLatin1String("ask_teacher") && masterAutoEnabled()
        && sigCnt >= m_masterAutoAfterFails && !m_escalatedSigs.contains(sig)) {
        m_escalatedSigs.insert(sig);
        const QString question = QStringLiteral(
            "El agente local se atascó: repitió la tool '%1' sin progreso. "
            "Resolvé el problema subyacente.").arg(name);
        const QString context = QStringLiteral("Tool repetida: %1\nArgs: %2").arg(name, argStr);
        QJsonObject newArgs{{QStringLiteral("question"), question},
                            {QStringLiteral("context"), context}};
        fn[QStringLiteral("name")] = QStringLiteral("ask_teacher");
        fn[QStringLiteral("arguments")] =
            QString::fromUtf8(QJsonDocument(newArgs).toJson(QJsonDocument::Compact));
        call[QStringLiteral("function")] = fn;
        m_pendingCalls[0] = call;
        emit logAppended(QStringLiteral("[escalando al maestro: tool '%1' atascada]\n").arg(name));
        // Reasignar locales y continuar con la ejecución normal (ask_teacher).
        name = QStringLiteral("ask_teacher");
        kind = toolKind(name);
        argStr = toolArgumentsToString(fn.value(QStringLiteral("arguments")));
    } else if (sigCnt >= m_loopPolicy.sameCallLimit) {
        // La tercera llamada consecutiva no se ejecuta. Inyectamos el tool_result
        // para conservar el contrato assistant/tool y se lo devolvemos al modelo:
        // debe intentar una estrategia distinta en vez de dejar trabajo bloqueado.
        // Si ignora una vez el replanteo y repite exactamente lo mismo, recién ahí
        // cerramos para mantener un límite duro de consumo.
        ++m_toolFail;
        m_pendingCalls.removeFirst();
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                              QJsonObject{{QStringLiteral("tool"), name},
                                          {QStringLiteral("toolCallId"), id},
                                          {QStringLiteral("reason"), QStringLiteral("anti_loop")},
                                          {QStringLiteral("repeatCount"), sigCnt}});
        const bool alreadyReplanned = m_replannedCallSigs.contains(sig);
        const QString notice = alreadyReplanned
            ? QStringLiteral(
                "[anti-loop: la llamada a '%1' volvió a repetirse aun después del "
                "replanteo obligatorio; turno detenido para limitar consumo.]").arg(name)
            : QStringLiteral(
                "[anti-loop: no ejecutes otra vez '%1' con estos argumentos: ya se "
                "obtuvo el mismo resultado sin progreso. REPLANIFICACIÓN OBLIGATORIA: "
                "usá la evidencia disponible y elegí ahora una herramienta, argumentos "
                "o enfoque materialmente diferente; si el objetivo ya está resuelto, "
                "entregá el resultado final.]").arg(name);
        appendToolResult(id, name, notice);
        if (alreadyReplanned) {
            emit logAppended(QStringLiteral("[anti-loop] el modelo ignoró el replanteo; "
                                            "cerrando turno\n"));
            finishTurn(notice, true, QStringLiteral("failed"));
            return;
        }
        m_replannedCallSigs.insert(sig);
        ++m_replanEvents;
        emit logAppended(QStringLiteral("[anti-loop] tercera tool idéntica bloqueada; "
                                        "devolviendo evidencia al modelo para replantear\n"));
        processPendingCalls();
        return;
    }

    // ── Robustez: tool desconocida ───────────────────────────────────────
    static const QStringList known{
        QStringLiteral("read_file"), QStringLiteral("list_dir"), QStringLiteral("grep"),
        QStringLiteral("write_file"), QStringLiteral("edit_file"),
        QStringLiteral("glob"), QStringLiteral("run_shell"), QStringLiteral("web_fetch"),
        QStringLiteral("web_search"), QStringLiteral("deep_research"),
        QStringLiteral("search_docs"), QStringLiteral("semantic_search"),
        QStringLiteral("hybrid_search"), QStringLiteral("repo_slice"),
        QStringLiteral("context_status"), QStringLiteral("work_status"),
        QStringLiteral("context_scout"),
        QStringLiteral("context_fetch"), QStringLiteral("verify_claims"),
        QStringLiteral("review_overengineering"),
        QStringLiteral("memory"), QStringLiteral("graph"), QStringLiteral("context_checkpoint"),
        QStringLiteral("ask_teacher"), QStringLiteral("task"),
        QStringLiteral("browser_skill_list"), QStringLiteral("browser_skill_replay"),
        QStringLiteral("skill_list"), QStringLiteral("skill_load"),
        QStringLiteral("browser_network_discover"),
        QStringLiteral("recent_actions"), QStringLiteral("desktop_snapshot"),
        QStringLiteral("desktop_windows"),
        QStringLiteral("desktop_controls"), QStringLiteral("desktop_click_element"),
        QStringLiteral("desktop_control_action"),
        QStringLiteral("desktop_find_image"), QStringLiteral("desktop_click_image"),
        QStringLiteral("desktop_wait_image"), QStringLiteral("desktop_assert_image"),
        QStringLiteral("desktop_observe"), QStringLiteral("desktop_click"),
        QStringLiteral("desktop_stroke"),
        QStringLiteral("desktop_type"), QStringLiteral("desktop_key"),
        QStringLiteral("desktop_scroll"), QStringLiteral("desktop_wait_for"),
        QStringLiteral("desktop_assert"), QStringLiteral("desktop_focus"),
        QStringLiteral("desktop_wait"), QStringLiteral("desktop_launch"),
        QStringLiteral("email_accounts"), QStringLiteral("email_send"),
        QStringLiteral("email_list"), QStringLiteral("email_read"),
        QStringLiteral("mcp_search_tools"), QStringLiteral("mcp_call_tool")};
    if (!known.contains(name) && !name.startsWith(kMcpPrefix)) {
        ++m_toolFail;
        m_pendingCalls.removeFirst();
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                              QJsonObject{{QStringLiteral("tool"), name},
                                          {QStringLiteral("toolCallId"), id},
                                          {QStringLiteral("reason"), QStringLiteral("unknown_tool")}});
        appendToolResult(id, name, QStringLiteral("[error: tool desconocida '%1']").arg(name));
        processPendingCalls();
        return;
    }

    // ── Robustez: args JSON malformado ───────────────────────────────────
    QJsonParseError perr;
    const QJsonObject args = QJsonDocument::fromJson(argStr.toUtf8(), &perr).object();
    if (perr.error != QJsonParseError::NoError && !argStr.trimmed().isEmpty()) {
        if (name == QLatin1String("desktop_launch") && !m_desktopLaunchApps.isEmpty()) {
            ++m_toolFail;
            m_pendingCalls.removeFirst();
            AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                                  QJsonObject{{QStringLiteral("tool"), name},
                                              {QStringLiteral("toolCallId"), id},
                                              {QStringLiteral("reason"), QStringLiteral("invalid_json_args_after_launch")},
                                              {QStringLiteral("error"), perr.errorString()}});
            const QStringList launched = m_desktopLaunchApps.values();
            appendToolResult(id, name, QStringLiteral(
                "[desktop_launch bloqueado: ya lanzaste app(s) en esta Task (%1). "
                "No abras otra instancia. Usá desktop_windows una sola vez para tomar "
                "el id de la ventana existente, luego desktop_focus y continuá con "
                "desktop_type/desktop_controls.]")
                .arg(launched.join(QStringLiteral(", "))));
            processPendingCalls();
            return;
        }
        ++m_toolFail;
        m_pendingCalls.removeFirst();
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                              QJsonObject{{QStringLiteral("tool"), name},
                                          {QStringLiteral("toolCallId"), id},
                                          {QStringLiteral("reason"), QStringLiteral("invalid_json_args")},
                                          {QStringLiteral("error"), perr.errorString()}});
        appendToolResult(id, name, QStringLiteral(
            "[error: argumentos JSON inválidos (%1). Reintentá con JSON válido para %2.]")
            .arg(perr.errorString(), name));
        processPendingCalls();
        return;
    }

    if (name == QLatin1String("desktop_launch")) {
        const QString app = args.value(QStringLiteral("app")).toString().trimmed().toLower();
        if (!app.isEmpty() && m_desktopLaunchApps.contains(app)) {
            ++m_toolFail;
            m_pendingCalls.removeFirst();
            AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                                  QJsonObject{{QStringLiteral("tool"), name},
                                              {QStringLiteral("toolCallId"), id},
                                              {QStringLiteral("reason"), QStringLiteral("duplicate_desktop_launch")},
                                              {QStringLiteral("app"), app}});
            appendToolResult(id, name, QStringLiteral(
                "[desktop_launch bloqueado: '%1' ya fue lanzada en esta Task. "
                "No abras otra instancia. Continuá con la ventana existente: "
                "desktop_windows si aún no tenés id, desktop_focus si ya lo tenés, "
                "y después desktop_type/desktop_controls para completar y verificar.]")
                .arg(app));
            processPendingCalls();
            return;
        }
    }
    if (name == QLatin1String("desktop_key")
        && redundantDesktopConfirmKey(m_lastDesktopTool, m_lastDesktopTypeText,
                                      args.value(QStringLiteral("key")).toString())) {
        ++m_toolFail;
        m_pendingCalls.removeFirst();
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                              QJsonObject{{QStringLiteral("tool"), name},
                                          {QStringLiteral("toolCallId"), id},
                                          {QStringLiteral("reason"), QStringLiteral("redundant_desktop_confirm_key")},
                                          {QStringLiteral("previousType"), m_lastDesktopTypeText},
                                          {QStringLiteral("key"), args.value(QStringLiteral("key")).toString()}});
        appendToolResult(id, name, QStringLiteral(
            "[desktop_key bloqueado: la última desktop_type ya terminó con '=' (%1). "
            "No presiones ENTER/= otra vez porque Calculadora repite la operación. "
            "Verificá ahora con desktop_controls y el visor ACTUAL ('Se muestra X').]")
            .arg(m_lastDesktopTypeText.left(80)));
        processPendingCalls();
        return;
    }

    // ── Robustez: validación de args requeridos ──────────────────────────
    QStringList missing;
    for (const QString &req : requiredArgs(name))
        if (!args.contains(req) || args.value(req).toString().isEmpty())
            missing << req;
    if (!missing.isEmpty()) {
        ++m_toolFail;
        m_pendingCalls.removeFirst();
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                              QJsonObject{{QStringLiteral("tool"), name},
                                          {QStringLiteral("toolCallId"), id},
                                          {QStringLiteral("reason"), QStringLiteral("missing_args")},
                                          {QStringLiteral("missing"), QJsonArray::fromStringList(missing)}});
        appendToolResult(id, name, QStringLiteral(
            "[error: faltan argumentos requeridos para %1: %2]")
            .arg(name, missing.join(QStringLiteral(", "))));
        processPendingCalls();
        return;
    }

    // Checkpoint explícito de fin de fase: el modelo entrega un recibo
    // estructurado y el harness decide si el ahorro justifica invalidar el KV.
    if (name == QLatin1String("context_checkpoint")) {
        m_pendingCalls.removeFirst();
        const QString summary = normalizeCompactionSummary(
            args.value(QStringLiteral("summary")).toString());
        QJsonParseError checkpointError;
        const QJsonObject checkpoint = QJsonDocument::fromJson(
            summary.toUtf8(), &checkpointError).object();
        if (checkpointError.error != QJsonParseError::NoError
            || checkpoint.value(QStringLiteral("goal")).toString().isEmpty()
            || checkpoint.value(QStringLiteral("next_action")).toString().isEmpty()) {
            appendToolResult(id, name,
                QStringLiteral("[checkpoint rechazado: summary debe ser JSON válido con goal y next_action]"));
            processPendingCalls();
            return;
        }
        const int head = qMin(2, int(m_apiMessages.size()));
        const int keepFrom = qMax(head, int(m_apiMessages.size()) - 4);
        int removable = 0;
        for (int i = head; i < keepFrom; ++i)
            removable += msgTokensOf(m_apiMessages.at(i).toObject());
        const int before = estimateApiTokens();
        const int estimatedAfter = before - removable + qMax(64, (summary.size() + 3) / 4);
        if (keepFrom > head && estimatedAfter * 10 <= before * 7) {
            applyCompaction(head, keepFrom, summary);
            appendToolResult(id, name,
                QStringLiteral("[checkpoint aceptado: fase cerrada; contexto de trabajo reducido de ~%1 a ~%2 tokens]")
                    .arg(before).arg(estimateApiTokens()));
        } else {
            appendToolResult(id, name,
                QStringLiteral("[checkpoint registrado sin compactar: el ahorro estimado no alcanza 30%]"));
        }
        processPendingCalls();
        return;
    }

    // memory: 'save'/'forget' mutan el archivo de memoria → tratar como write
    // (requiere aprobación). 'recall' (o sin action) es lectura pura → auto.
    if (name == QLatin1String("memory")) {
        const QString a = args.value(QStringLiteral("action")).toString().toLower();
        if (a == QLatin1String("save") || a == QLatin1String("forget"))
            kind = QStringLiteral("write");
        // prune muta salvo dry_run (que sólo reporta).
        if (a == QLatin1String("prune") && !args.value(QStringLiteral("dry_run")).toBool(false))
            kind = QStringLiteral("write");
        if (a == QLatin1String("decay") && !args.value(QStringLiteral("dry_run")).toBool(false))
            kind = QStringLiteral("write");
    }
    // graph: 'query'/'decisions'/'doctor' sólo leen; el resto muta.
    if (name == QLatin1String("graph")) {
        const QString a = args.value(QStringLiteral("action")).toString().toLower();
        if (a != QLatin1String("query") && a != QLatin1String("decisions")
            && a != QLatin1String("doctor"))
            kind = QStringLiteral("write");
    }
    // La envoltura lazy no debe esconder la política del tool MCP real. Buscar es
    // lectura; al llamar, clasificamos y evaluamos guardrails con nombre/args internos.
    if (name == QLatin1String("mcp_search_tools"))
        kind = QStringLiteral("read");
    QVariantMap discoveredMcpSafety;
    if (name == QLatin1String("mcp_call_tool")) {
        kind = toolKind(args.value(QStringLiteral("name")).toString());
        const QString target = args.value(QStringLiteral("name")).toString();
        for (const QVariant &value : std::as_const(m_mcpTools)) {
            const QVariantMap def = value.toMap();
            const QString full = QStringLiteral("mcp__%1__%2")
                                     .arg(def.value(QStringLiteral("server")).toString(),
                                          def.value(QStringLiteral("name")).toString());
            if (full == target) {
                discoveredMcpSafety = def.value(QStringLiteral("safety")).toMap();
                break;
            }
        }
        if (discoveredMcpSafety.isEmpty())
            discoveredMcpSafety = ToolExecutionSafety::toVariantMap(
                ToolExecutionSafety::fromMcpTool(target, QString(), {}));
        if (discoveredMcpSafety.value(QStringLiteral("effect")).toString()
                == QLatin1String("read"))
            kind = QStringLiteral("read");
    }

    // ── Guard de mutaciones declaradas ───────────────────────────────────
    // La clasificación de arriba incluye memory/graph y la envoltura MCP; por
    // eso este gate vive después de resolver el efecto real de la llamada. Un
    // shell o MCP que puede escribir sin declarar rutas es opaco para el índice
    // y para WorkRegistry: se rechaza antes de ejecutar.
    const QJsonObject mutationArgs = args;
    QString guardTarget = name;
    if (name == QLatin1String("mcp_call_tool"))
        guardTarget = args.value(QStringLiteral("name")).toString();
    auto mcpTargetMayMutate = [this](const QString &target) {
        if (!target.startsWith(QLatin1String("mcp__"))) return false;
        for (const QVariant &value : std::as_const(m_mcpTools)) {
            const QVariantMap def = value.toMap();
            const QString full = QStringLiteral("mcp__%1__%2")
                                     .arg(def.value(QStringLiteral("server")).toString(),
                                          def.value(QStringLiteral("name")).toString());
            if (full != target) continue;
            const QVariantMap safety = def.value(QStringLiteral("safety")).toMap();
            return safety.value(QStringLiteral("effect")).toString().toLower()
                       != QLatin1String("read")
                || safety.value(QStringLiteral("externalWrite")).toBool();
        }
        // Tool MCP no registrada: tratarla como opaca y exigir declaración.
        return true;
    };
    const bool mcpMutation = (name == QLatin1String("mcp_call_tool")
                              && !guardTarget.startsWith(QLatin1String("mcp__")))
        || mcpTargetMayMutate(guardTarget);
    QString shellCommand = mutationArgs.value(QStringLiteral("command")).toString();
    if (shellCommand.isEmpty())
        shellCommand = mutationArgs.value(QStringLiteral("arguments")).toObject()
                           .value(QStringLiteral("command")).toString();
    const bool shellMutation = (kind == QLatin1String("shell")
                                || guardTarget == QLatin1String("run_shell")
                                || guardTarget == QLatin1String("shell"))
        && AgentLifecycle::shellCommandMayMutate(shellCommand);
    QStringList mutationPaths = AgentLifecycle::changedPathsFromToolInput(name, mutationArgs);
    // memory/graph son mutaciones internas conocidas: tienen una ruta estable
    // y también deben coordinarse, pero no se obliga al modelo a inventarla.
    if (name == QLatin1String("memory")
        && !mutationPaths.contains(QStringLiteral(".llamacode/memory.jsonl")))
        mutationPaths << QStringLiteral(".llamacode/memory.jsonl");
    if (name == QLatin1String("graph")
        && !mutationPaths.contains(QStringLiteral(".llamacode/graph.jsonl")))
        mutationPaths << QStringLiteral(".llamacode/graph.jsonl");
    const bool hasOutsideMutationPath = std::any_of(
        mutationPaths.cbegin(), mutationPaths.cend(), [](const QString &path) {
            return QDir::isAbsolutePath(path) || path == QLatin1String("..")
                || path.startsWith(QStringLiteral("../"));
        });
    if (hasOutsideMutationPath) {
        ++m_toolFail;
        m_pendingCalls.removeFirst();
        const QString refusal = QStringLiteral(
            "[mutation_guard: '%1' bloqueada porque declaró una ruta fuera del workspace; "
            "usá sólo rutas relativas dentro del proyecto]").arg(name);
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                              QJsonObject{{QStringLiteral("tool"), name},
                                          {QStringLiteral("toolCallId"), id},
                                          {QStringLiteral("reason"), QStringLiteral("mutation_path_outside_workspace")},
                                          {QStringLiteral("paths"), QJsonArray::fromStringList(mutationPaths)}});
        appendToolCard(name, kind, false, mutationPaths.join(", "), refusal);
        appendToolResult(id, name, refusal);
        processPendingCalls();
        return;
    }
    const bool requiresMutationPaths = kind == QLatin1String("write")
        || mcpMutation || shellMutation;
    const bool shouldClaim = requiresMutationPaths
        || (!mutationPaths.isEmpty() && kind == QLatin1String("shell"));
    if (requiresMutationPaths && mutationPaths.isEmpty()) {
        ++m_toolFail;
        m_pendingCalls.removeFirst();
        const QString refusal = QStringLiteral(
            "[mutation_guard: '%1' bloqueada porque no declaró changed_paths; "
            "indicá las rutas relativas que puede crear, modificar o borrar]").arg(name);
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                              QJsonObject{{QStringLiteral("tool"), name},
                                          {QStringLiteral("toolCallId"), id},
                                          {QStringLiteral("reason"), QStringLiteral("mutation_paths_required")}});
        appendToolCard(name, kind, false, QString(), refusal);
        appendToolResult(id, name, refusal);
        processPendingCalls();
        return;
    }
    if (shouldClaim && !mutationPaths.isEmpty() && !m_cwd.isEmpty()
        && !m_workClaimId.isEmpty()) {
        QString conflict;
        const bool claimed = WorkRegistry::claimPaths(
            m_cwd, m_workClaimId, m_sessionId, mutationPaths, &conflict);
        if (!claimed && conflict.isEmpty())
            conflict = WorkRegistry::formatConflicts(m_cwd, m_sessionId, mutationPaths);
        if (!claimed && !conflict.isEmpty()) {
            ++m_toolFail;
            m_pendingCalls.removeFirst();
            const QString refusal = conflict
                + QStringLiteral("\n[edición bloqueada: elegí otra ruta, esperá el handoff "
                                 "de la otra sesión o pedí al usuario que resuelva el conflicto]");
            AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                                  QJsonObject{{QStringLiteral("tool"), name},
                                              {QStringLiteral("toolCallId"), id},
                                              {QStringLiteral("reason"), QStringLiteral("active_work_conflict")},
                                              {QStringLiteral("paths"), QJsonArray::fromStringList(mutationPaths)}});
            appendToolCard(name, kind, false, mutationPaths.join(", "), refusal);
            appendToolResult(id, name, refusal);
            processPendingCalls();
            return;
        }
    }

    // PLAN MODE: bloquear cualquier tool que mute (write/shell/mcp). Las read
    // ya están filtradas del schema, pero defendemos por si el modelo igual la pide.
    if (m_approvalMode == QLatin1String("plan")
        && kind != QLatin1String("read")) {
        ++m_toolFail;
        m_pendingCalls.removeFirst();
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("rejected_alternative"),
                              QJsonObject{{QStringLiteral("tool"), name},
                                          {QStringLiteral("toolCallId"), id},
                                          {QStringLiteral("reason"), QStringLiteral("plan_mode")},
                                          {QStringLiteral("alternative"), argStr.left(8192)}});
        appendToolResult(id, name, QStringLiteral(
            "[MODO PLAN: '%1' bloqueada (solo lectura). Proponé el cambio en texto; "
            "el usuario saldrá de plan para ejecutarlo.]").arg(name));
        processPendingCalls();
        return;
    }

    // ── Permisos por patrón (antes de la política global) ─────────────────
    // subject = ruta (read/write) o comando (shell). Primera regla que matchea gana.
    bool forceAsk = false;
    {
        QString subject = (kind == QLatin1String("shell"))
            ? args.value(QStringLiteral("command")).toString()
            : args.value(QStringLiteral("path")).toString();
        if (subject.isEmpty()) subject = args.value(QStringLiteral("pattern")).toString();
        emit logAppended(QStringLiteral("[perm-eval] kind=%1 subject='%2' rules=%3\n")
                             .arg(kind, subject).arg(m_permRules.size()));
        if (!subject.isEmpty()) {
            for (const PermRule &r : std::as_const(m_permRules)) {
                const bool kindOk = r.kind.isEmpty() || r.kind == kind;
                const bool rxOk = r.rx.match(subject).hasMatch();
                emit logAppended(QStringLiteral("[perm-eval]   glob='%1' rx=[%2] valid=%3 kindOk=%4 rxOk=%5\n")
                                     .arg(r.glob, r.rx.pattern()).arg(r.rx.isValid()).arg(kindOk).arg(rxOk));
                if (!r.kind.isEmpty() && r.kind != kind) continue;
                if (!r.rx.match(subject).hasMatch()) continue;
                if (r.action == PermDeny) {
                    ++m_toolFail;
                    m_pendingCalls.removeFirst();
                    const QString denied = QStringLiteral(
                        "[permiso denegado por regla '%1': '%2' no está permitido]")
                        .arg(r.glob, subject);
                    AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("rejected_alternative"),
                                          QJsonObject{{QStringLiteral("tool"), name},
                                                      {QStringLiteral("toolCallId"), id},
                                                      {QStringLiteral("reason"), QStringLiteral("permission_rule")},
                                                      {QStringLiteral("rule"), r.glob},
                                                      {QStringLiteral("alternative"), subject}});
                    appendToolCard(name, kind, false, subject, denied);
                    appendToolResult(id, name, denied);
                    processPendingCalls();
                    return;
                }
                if (r.action == PermAllow) {
                    emit logAppended(QStringLiteral("[permiso: regla '%1' permite %2]\n").arg(r.glob, name));
                    approveAndContinue(id, QStringLiteral("once"));
                    return;
                }
                forceAsk = true;   // PermAsk: pedir aprobación aunque el modo sea auto
                break;
            }
        }
    }

    const bool autoAll  = (m_taskAutoApprove                              // Task en curso: auto-aprobar
                           || m_approvalMode == QLatin1String("auto")
                           || m_approvalMode == QLatin1String("super")
                           || m_approvalMode == QLatin1String("plan"));   // plan = todo read → auto
    const bool autoRead = (m_approvalMode == QLatin1String("ask") && kind == QLatin1String("read"));
    const bool always   = m_alwaysAllowed.contains(kind);

    // email_send es acción externa irreversible: gateada por defecto en CUALQUIER
    // modo (incluso auto/super), salvo que el usuario active "auto-enviar".
    const bool emailGated = (kind == QLatin1String("email") && !m_mailAutoSend);
    const bool emailAuto  = (kind == QLatin1String("email") && m_mailAutoSend);

    // Guardrail Zero-Autonomy: acción destructiva/irreversible fuerza aprobación
    // humana aunque el modo sea auto o haya Task auto-approve. Excepción: "super"
    // (autonomía total). Análogo a emailGated pero cross-kind.
    const QString guardName = name == QLatin1String("mcp_call_tool")
                                  ? args.value(QStringLiteral("name")).toString() : name;
    const QJsonObject guardArgs = name == QLatin1String("mcp_call_tool")
                                      ? args.value(QStringLiteral("arguments")).toObject() : args;
    const bool destructiveGated = (m_hitlDestructive
                                   && m_approvalMode != QLatin1String("super")
                                   && (isDestructiveAction(guardName, guardArgs, m_lastDesktopResult)
                                       || discoveredMcpSafety.value(
                                              QStringLiteral("destructive")).toBool()));
    const QVariantMap mcpSafety = discoveredMcpSafety;
    const bool mcpApprovalGated = !mcpSafety.isEmpty()
                                  && mcpSafety.value(QStringLiteral("approvalRequired"), true).toBool();
    if (destructiveGated)
        emit logAppended(QStringLiteral("[guardrail] '%1' es destructiva → aprobación requerida\n").arg(name));

    if (!forceAsk && !emailGated && !destructiveGated && !mcpApprovalGated
        && (autoAll || autoRead || always || emailAuto)) {
        emit logAppended(QStringLiteral("[tool] auto-approving %1\n").arg(name));
        approveAndContinue(call.value(QStringLiteral("id")).toString(), QStringLiteral("once"));
        return;
    }

    // Pedir aprobación al usuario.
    m_awaitId   = call.value(QStringLiteral("id")).toString();
    m_awaitCall = call;
    QString detail = args.value(QStringLiteral("command")).toString();
    m_awaitPayloadHash.clear();
    if (name == QLatin1String("mcp_call_tool")) {
        const QString target = args.value(QStringLiteral("name")).toString();
        const QString rest = target.startsWith(QStringLiteral("mcp__"))
                                 ? target.mid(5) : target;
        const int sep = rest.indexOf(QStringLiteral("__"));
        const QString server = sep >= 0 ? rest.left(sep) : QString();
        const QString tool = sep >= 0 ? rest.mid(sep + 2) : target;
        m_awaitPayloadHash = ToolExecutionSafety::payloadHash(
            server, tool, args.value(QStringLiteral("arguments")).toObject());
        detail = QStringLiteral("%1 · payload %2").arg(target, m_awaitPayloadHash.left(12));
    }
    if (detail.isEmpty()) detail = args.value(QStringLiteral("path")).toString();
    if (detail.isEmpty()) detail = args.value(QStringLiteral("pattern")).toString();
    if (name == QLatin1String("email_send"))
        detail = QStringLiteral("a: %1 · asunto: %2")
                     .arg(args.value(QStringLiteral("to")).toString(),
                          args.value(QStringLiteral("subject")).toString());
    QString diff;
    if (name == QLatin1String("write_file") || name == QLatin1String("edit_file")) {
        const QString rel = QDir::cleanPath(args.value(QStringLiteral("path")).toString().trimmed());
        detail = rel;
        const QString abs = QDir::cleanPath(QDir(m_cwd).absoluteFilePath(rel));
        QString oldText;
        QFile prev(abs);
        if (prev.open(QIODevice::ReadOnly)) oldText = QString::fromUtf8(prev.read(4 * 1024 * 1024));
        QString newText;
        if (name == QLatin1String("write_file")) {
            newText = args.value(QStringLiteral("content")).toString();
        } else {
            // edit_file: previsualizar el reemplazo (igual lógica que el worker).
            const QString oldS = args.value(QStringLiteral("old_string")).toString();
            const QString newS = args.value(QStringLiteral("new_string")).toString();
            newText = oldText;
            if (!oldS.isEmpty()) {
                if (args.value(QStringLiteral("replace_all")).toBool()) {
                    newText.replace(oldS, newS);
                } else {
                    const int idx = oldText.indexOf(oldS);
                    if (idx >= 0)
                        newText = oldText.left(idx) + newS + oldText.mid(idx + oldS.size());
                }
            }
        }
        diff = makeDiff(oldText, newText);
    }
    emit toolApprovalNeeded(QVariantMap{
        {QStringLiteral("id"),        m_awaitId},
        {QStringLiteral("sessionId"), m_sessionId},
        {QStringLiteral("tool"),      name},
        {QStringLiteral("kind"),      kind},
        {QStringLiteral("title"),     name},
        {QStringLiteral("detail"),    detail},
        {QStringLiteral("diff"),      diff},
        {QStringLiteral("correlationId"), m_correlationId},
        {QStringLiteral("payloadHash"), m_awaitPayloadHash},
        {QStringLiteral("safety"), mcpSafety},
        // Motivo del freno: el guardrail Zero-Autonomy tiene prioridad en el card
        // (una destructiva puede además ser write). "" = aprobación normal.
        {QStringLiteral("reason"),    destructiveGated ? QStringLiteral("destructive")
                                    : emailGated       ? QStringLiteral("email")
                                    : mcpApprovalGated ? QStringLiteral("external_write")
                                                       : QString()}
    });
    ensureAssistantBubble();
    setAssistantStatus(QStringLiteral("Esperando aprobación para %1...").arg(name));
    emit logAppended(QStringLiteral("[tool] waiting approval for %1\n").arg(name));
}

void LlamaAgentBackend::approveAndContinue(const QString &id, const QString &response)
{
    if (m_pendingCalls.isEmpty()) return;
    const QJsonObject call = m_pendingCalls.first().toObject();
    if (call.value(QStringLiteral("id")).toString() != id) return;
    m_pendingCalls.removeFirst();

    const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
    const QString name   = fn.value(QStringLiteral("name")).toString();
    const QString argStr = toolArgumentsToString(fn.value(QStringLiteral("arguments")));
    if (response != QLatin1String("reject") && !m_awaitPayloadHash.isEmpty()) {
        const QJsonObject approvalArgs = QJsonDocument::fromJson(argStr.toUtf8()).object();
        const QString target = approvalArgs.value(QStringLiteral("name")).toString();
        const QString rest = target.startsWith(QStringLiteral("mcp__"))
                                 ? target.mid(5) : target;
        const int sep = rest.indexOf(QStringLiteral("__"));
        const QString currentHash = ToolExecutionSafety::payloadHash(
            sep >= 0 ? rest.left(sep) : QString(),
            sep >= 0 ? rest.mid(sep + 2) : target,
            approvalArgs.value(QStringLiteral("arguments")).toObject());
        if (currentHash != m_awaitPayloadHash) {
            const QString error = QStringLiteral(
                "[aprobación invalidada: el payload MCP cambió antes de ejecutar]");
            AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                                  {{QStringLiteral("tool"), name},
                                   {QStringLiteral("toolCallId"), id},
                                   {QStringLiteral("correlationId"), m_correlationId},
                                   {QStringLiteral("reason"), QStringLiteral("approval_payload_mismatch")}});
            m_awaitId.clear();
            m_awaitCall = {};
            m_awaitPayloadHash.clear();
            appendToolResult(id, name, error);
            processPendingCalls();
            return;
        }
    }
    m_awaitId.clear();
    m_awaitCall = {};
    m_awaitPayloadHash.clear();

    if (m_executionStep) {
        m_executionStep = false;
        m_executionPaused = true;
    }

    // Comando/ruta a mostrar en la tarjeta de la tool.
    const QJsonObject a = QJsonDocument::fromJson(argStr.toUtf8()).object();
    m_execCommand = a.value(QStringLiteral("command")).toString();
    if (m_execCommand.isEmpty()) m_execCommand = a.value(QStringLiteral("path")).toString();
    if (m_execCommand.isEmpty()) m_execCommand = a.value(QStringLiteral("pattern")).toString();
    if (m_execCommand.isEmpty() && name == QLatin1String("desktop_launch"))
        m_execCommand = a.value(QStringLiteral("app")).toString().trimmed().toLower();
    if (m_execCommand.isEmpty() && name == QLatin1String("desktop_type"))
        m_execCommand = a.value(QStringLiteral("text")).toString();
    if (m_execCommand.isEmpty() && name == QLatin1String("desktop_key"))
        m_execCommand = a.value(QStringLiteral("key")).toString().trimmed();

    // Rechazo: no se ejecuta nada; resume sincrónico.
    if (response == QLatin1String("reject")) {
        ++m_toolFail;
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("rejected_alternative"),
                              QJsonObject{{QStringLiteral("tool"), name},
                                          {QStringLiteral("toolCallId"), id},
                                          {QStringLiteral("reason"), QStringLiteral("user_rejected")},
                                          {QStringLiteral("alternative"), m_execCommand}});
        appendToolCard(name, toolKind(name), false, m_execCommand,
                       QStringLiteral("[el usuario rechazó esta acción]"));
        m_execCommand.clear();
        appendToolResult(id, name, QStringLiteral("[el usuario rechazó esta acción]"));
        processPendingCalls();
        return;
    }

    // External plugin lane. It is deliberately separate from AgentToolRunner:
    // the model-visible call is gated by the existing approval path, while the
    // worker driver owns framing, timeout and cancellation for the child.
    if (name == QLatin1String("worker_call")) {
        const QString operation = a.value(QStringLiteral("operation")).toString().trimmed();
        const QJsonValue payloadValue = a.value(QStringLiteral("payload"));
        if (!m_harnessWorker || !m_harnessWorkerReady || operation.isEmpty()
            || !payloadValue.isObject()) {
            const QString failure = QStringLiteral(
                "[worker externo no disponible: activá un perfil Node/Python válido y esperá su autenticación]");
            ++m_toolFail;
            appendToolCard(name, toolKind(name), false, operation, failure);
            appendToolResult(id, name, failure);
            processPendingCalls();
            return;
        }
        const QString driverCallId = QStringLiteral("worker-%1")
                                         .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        m_externalWorkerCalls.insert(driverCallId, id);
        m_externalWorkerDescriptions.insert(driverCallId, operation);
        if (m_harnessEngineId == QLatin1String("next"))
            m_harnessEffectLedger.transition(m_sessionId + QLatin1Char(':') + id,
                                             QStringLiteral("dispatching"));
        emit agentLifecycleEvent(AgentLifecycle::toolEvent(
            QStringLiteral("tool.start"), m_sessionId, m_cwd, m_correlationId,
            id, name, argStr));
        if (!m_harnessWorker->call(
                driverCallId,
                QJsonObject{{QStringLiteral("operation"), operation},
                            {QStringLiteral("payload"), payloadValue.toObject()}})
            && m_externalWorkerCalls.contains(driverCallId)) {
            m_externalWorkerCalls.remove(driverCallId);
            m_externalWorkerDescriptions.remove(driverCallId);
            const QString failure = QStringLiteral("[worker externo: no se pudo despachar la operación]");
            ++m_toolFail;
            appendToolCard(name, toolKind(name), false, operation, failure);
            appendToolResult(id, name, failure);
            processPendingCalls();
        }
        return;
    }

    // Ejecución en el worker (no bloquea UI). Resume en onToolExecuted().
    ensureWorker();
    m_execCallId = id;
    m_execToolName = name;
    m_execArguments = argStr;
    const QString effectId = m_sessionId + QLatin1Char(':') + id;
    const int watchdogSec = toolWatchdogSeconds(name, a, m_quickToolTimeoutSec,
                                                m_loopPolicy.webToolTimeoutSec);
    m_execWatchdogSec = watchdogSec;
    if (m_harnessEngineId == QLatin1String("next"))
        m_harnessEffectLedger.transition(effectId, QStringLiteral("dispatching"));
    m_toolWatchdog->start(watchdogSec * 1000);
    if (name.startsWith(QLatin1String("desktop_")))
        emit desktopActivityChanged(true, name, m_execCommand);
    ensureAssistantBubble();
    setAssistantStatus(toolStatusText(name, toolKind(name), m_execCommand));
    emit agentLifecycleEvent(AgentLifecycle::toolEvent(
        QStringLiteral("tool.start"), m_sessionId, m_cwd, m_correlationId,
        id, name, argStr, AgentLifecycle::changedPathsFromToolInput(
            name, QJsonDocument::fromJson(argStr.toUtf8()).object())));
    QMetaObject::invokeMethod(m_worker, "executeTool", Qt::QueuedConnection,
                              Q_ARG(QString, id), Q_ARG(QString, name),
                              Q_ARG(QString, argStr), Q_ARG(QString, m_cwd));
}

void LlamaAgentBackend::resyncContextAfterMutation(const QString &reason,
                                                    const QStringList &knownPaths)
{
    if (m_cwd.isEmpty()) return;

    const QDir base(m_cwd);
    QStringList safePaths;
    for (const QString &raw : knownPaths) {
        const QFileInfo info(raw);
        const QString absolute = info.isAbsolute()
            ? info.absoluteFilePath() : base.absoluteFilePath(raw);
        const QString rel = QDir::fromNativeSeparators(base.relativeFilePath(absolute));
        if (rel.isEmpty() || rel == QLatin1String(".")
            || rel == QLatin1String("..") || rel.startsWith(QStringLiteral("../")))
            continue;
        if (!safePaths.contains(rel)) safePaths.append(rel);
    }

    const QVariantMap before = ProjectBrain::load(m_cwd);
    const bool fullScan = safePaths.isEmpty();
    const QVariantMap after = fullScan
        ? ProjectBrain::refresh(m_cwd)
        : ProjectBrain::update(m_cwd, safePaths);
    if (after.contains(QStringLiteral("error"))) return;

    QStringList changed = ProjectBrain::changedPaths(before, after);
    if (!fullScan) {
        for (const QString &path : safePaths)
            if (!changed.contains(path)) changed.append(path);
        changed.sort(Qt::CaseInsensitive);
    }
    if (changed.isEmpty()) return;

    const QVariantMap contextState = ContextIndex::refresh(m_cwd, changed);
    QString graphReport;
    bool hasDeleted = false;
    for (const QString &path : changed) {
        if (!QFileInfo::exists(base.absoluteFilePath(path))) {
            hasDeleted = true;
            break;
        }
    }
    if (fullScan || hasDeleted)
        CodeGraphIndexer::buildIncremental(m_cwd, {}, &graphReport);
    else
        CodeGraphIndexer::reindexFiles(m_cwd, changed, {}, &graphReport);

    emit logAppended(QStringLiteral(
        "[preflight: índices resincronizados tras %1 (%2 archivo(s))]\n")
                         .arg(reason).arg(changed.size()));
    emit logAppended(graphReport + QLatin1Char('\n'));
    AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("observation"),
                          QJsonObject{{QStringLiteral("source"), QStringLiteral("context_resync")},
                                      {QStringLiteral("reason"), reason},
                                      {QStringLiteral("paths"), QJsonArray::fromStringList(changed)},
                                      {QStringLiteral("contextIndex"),
                                       QJsonObject::fromVariantMap(contextState)},
                                      {QStringLiteral("graphReport"), graphReport}});
    emit agentLifecycleEvent(AgentLifecycle::contextResync(
        m_sessionId, m_cwd, m_correlationId, reason, changed, graphReport));
}

void LlamaAgentBackend::onToolExecuted(const QVariantMap &result)
{
    const QString callId = result.value(QStringLiteral("callId")).toString();
    if (m_workerCapabilityCalls.contains(callId)) {
        finishWorkerCapability(result);
        return;
    }
    if (callId.isEmpty() || callId != m_execCallId) return;   // resultado tardío/ajeno
    m_execCallId.clear();
    m_toolWatchdog->stop();

    const QString name = result.value(QStringLiteral("name")).toString();
    const bool ok      = result.value(QStringLiteral("ok")).toBool();
    QString res        = result.value(QStringLiteral("result")).toString();
    const bool isWrite = result.value(QStringLiteral("isWrite")).toBool();
    const bool externalWrite = result.value(QStringLiteral("externalWrite")).toBool();
    if (!m_workClaimId.isEmpty())
        WorkRegistry::heartbeat(m_cwd, m_workClaimId, m_sessionId);
    const QString executedArgs = m_execArguments;
    m_execToolName.clear();
    m_execArguments.clear();
    if (m_harnessEngineId == QLatin1String("next")) {
        const QString resultHash = QString::fromLatin1(
            QCryptographicHash::hash(res.toUtf8(), QCryptographicHash::Sha256).toHex());
        m_harnessEffectLedger.transition(
            m_sessionId + QLatin1Char(':') + callId,
            QStringLiteral("settled"), ok ? QString() : QStringLiteral("tool_failed"),
            resultHash);
    }
    const QJsonObject args = QJsonDocument::fromJson(executedArgs.toUtf8()).object();
    const QStringList changedPaths = AgentLifecycle::changedPathsFromToolInput(name, args);
    const bool nativeFileWrite = isWrite
        && (name == QLatin1String("write_file") || name == QLatin1String("edit_file"));
    const bool shellMutation = name == QLatin1String("run_shell")
        && (AgentLifecycle::shellCommandMayMutate(
                args.value(QStringLiteral("command")).toString())
            || !changedPaths.isEmpty());
    const bool indexedExternalMutation = externalWrite && !changedPaths.isEmpty();
    if (ok && (nativeFileWrite || shellMutation || indexedExternalMutation)) {
        const QString reason = shellMutation ? QStringLiteral("run_shell")
            : (nativeFileWrite ? QStringLiteral("file_tool")
                               : QStringLiteral("external_tool"));
        resyncContextAfterMutation(reason, changedPaths);

        if (nativeFileWrite && !changedPaths.isEmpty()) {
            const QString path = changedPaths.first();
            if (!m_workClaimId.isEmpty())
                WorkRegistry::addPaths(m_cwd, m_workClaimId, m_sessionId, {path});
            const QString graphInference = GraphStore::inferToolTouch(
                m_cwd, name, path, m_sessionId,
                result.value(QStringLiteral("correlationId")).toString().isEmpty()
                    ? m_correlationId
                    : result.value(QStringLiteral("correlationId")).toString());
            emit logAppended(graphInference + QLatin1Char('\n'));
        }
    }
    emitToolLifecycleFinish(callId, name, ok, isWrite, externalWrite, res, changedPaths);
    if (name.startsWith(QLatin1String("desktop_")))
        emit desktopActivityChanged(false, name, m_execCommand);
    if (ok) ++m_toolOk; else ++m_toolFail;
    const bool failureSpiral = recordToolOutcome(name, ok, isWrite || externalWrite, res);
    if (name.startsWith(QLatin1String("desktop_"))) {
        m_lastDesktopTool = name;
        m_lastDesktopResult = res;
    }
    if (name == QLatin1String("desktop_type") && ok) {
        m_lastDesktopTypeText = m_execCommand;
    } else if (name.startsWith(QLatin1String("desktop_")) && name != QLatin1String("desktop_controls")) {
        m_lastDesktopTypeText.clear();
    }
    if (ok && name == QLatin1String("desktop_launch") && !m_execCommand.trimmed().isEmpty())
        m_desktopLaunchApps.insert(m_execCommand.trimmed().toLower());
    AgentEventLog::append(m_cwd, m_sessionId, ok ? QStringLiteral("tool_result")
                                                 : QStringLiteral("failure"),
                          QJsonObject{{QStringLiteral("tool"), name},
                                      {QStringLiteral("toolCallId"), callId},
                                      {QStringLiteral("correlationId"),
                                       result.value(QStringLiteral("correlationId")).toString()},
                                      {QStringLiteral("ok"), ok},
                                      {QStringLiteral("isWrite"), isWrite},
                                      {QStringLiteral("externalWrite"), externalWrite},
                                      {QStringLiteral("payloadHash"),
                                       result.value(QStringLiteral("payloadHash")).toString()},
                                      {QStringLiteral("idempotencyKey"),
                                       result.value(QStringLiteral("idempotencyKey")).toString()},
                                      {QStringLiteral("deduplicated"),
                                       result.value(QStringLiteral("deduplicated")).toBool()},
                                      {QStringLiteral("receipt"),
                                       QJsonObject::fromVariantMap(
                                           result.value(QStringLiteral("receipt")).toMap())},
                                      {QStringLiteral("desktopReceipt"),
                                       QJsonObject::fromVariantMap(
                                           result.value(QStringLiteral("desktopReceipt")).toMap())},
                                      {QStringLiteral("detail"), m_execCommand},
                                      {QStringLiteral("result"), res.left(8192)}});

    // Read-dedup: si el modelo re-lee un archivo que ya leyó y NO cambió, no
    // reenviamos el contenido (el server reprocesa todo el prompt cada iter por
    // SWA → re-leer 300 líneas idénticas es puro desperdicio). Devolvemos un stub.
    bool dedup = false;
    if (m_contextPolicy.readDedup && name == QLatin1String("read_file") && ok) {
        const QString rel = result.value(QStringLiteral("readRel")).toString();
        const QString fp  = result.value(QStringLiteral("readFp")).toString();
        if (!rel.isEmpty() && !fp.isEmpty()) {
            if (m_readFingerprints.value(rel) == fp) {
                res = QStringLiteral("[read_file: '%1' ya leído antes y sin cambios; "
                                     "contenido omitido para ahorrar contexto]").arg(rel);
                dedup = true;
            } else {
                m_readFingerprints.insert(rel, fp);
            }
        }
    }

    // run_shell async: ya hay una tarjeta "en vivo" (onToolStarted) con la salida
    // streameada → finalizarla en vez de crear otra.
    if (callId == m_liveToolCallId && m_liveToolMsgIdx >= 0
        && m_liveToolMsgIdx < m_messages.size()) {
        QVariantMap card = m_messages[m_liveToolMsgIdx].toMap();
        card[QStringLiteral("ok")] = ok;
        card[QStringLiteral("typing")] = false;
        card[QStringLiteral("arguments")] = AutomationArtifactStore::redact(
            executedArgs).left(8192);
        // Mostrar la salida real final (ya recortada por el worker).
        card[QStringLiteral("output")] = res.left(64 * 1024);
        m_messages[m_liveToolMsgIdx] = card;
        emit messagesChanged();
        m_liveToolCallId.clear();
        m_liveToolMsgIdx = -1;
    } else if (!isWrite) {
        // write_file/edit_file → tarjeta de diff (abajo). El resto → tarjeta propia.
        appendToolCard(name, toolKind(name), ok, m_execCommand, res);
        if (!m_messages.isEmpty()) {
            QVariantMap card = m_messages.last().toMap();
            card[QStringLiteral("arguments")] = AutomationArtifactStore::redact(
                executedArgs).left(8192);
            for (const QString &key : {QStringLiteral("imagePath"),
                                       QStringLiteral("beforeImagePath"),
                                       QStringLiteral("afterImagePath"),
                                       QStringLiteral("snapshotPath"),
                                       QStringLiteral("beforeSnapshotPath"),
                                       QStringLiteral("afterSnapshotPath")}) {
                if (result.contains(key)) card[key] = result.value(key);
            }
            m_messages[m_messages.size() - 1] = card;
            emit messagesChanged();
        }
        if (!m_messages.isEmpty()
            && (!result.value(QStringLiteral("receipt")).toMap().isEmpty()
                || !result.value(QStringLiteral("desktopReceipt")).toMap().isEmpty())) {
            QVariantMap card = m_messages.last().toMap();
            card[QStringLiteral("correlationId")] =
                result.value(QStringLiteral("correlationId")).toString();
            card[QStringLiteral("payloadHash")] =
                result.value(QStringLiteral("payloadHash")).toString();
            card[QStringLiteral("idempotencyKey")] =
                result.value(QStringLiteral("idempotencyKey")).toString();
            card[QStringLiteral("deduplicated")] =
                result.value(QStringLiteral("deduplicated")).toBool();
            card[QStringLiteral("receipt")] = result.value(QStringLiteral("receipt")).toMap();
            if (!result.value(QStringLiteral("desktopReceipt")).toMap().isEmpty())
                card[QStringLiteral("desktopReceipt")] =
                    result.value(QStringLiteral("desktopReceipt")).toMap();
            m_messages[m_messages.size() - 1] = card;
            emit messagesChanged();
        }
    }
    m_execCommand.clear();

    // write_file: snapshot (revert) + entrada de diff en el chat.
    if (isWrite) {
        const QString abs = result.value(QStringLiteral("absPath")).toString();
        const QByteArray oldContent = QByteArray::fromBase64(
            result.value(QStringLiteral("oldContentB64")).toString().toLatin1());
        if (!m_editSnapshots.contains(abs))
            m_editSnapshots.insert(abs, EditSnapshot{result.value(QStringLiteral("existed")).toBool(),
                                                     oldContent});
        m_messages.append(QVariantMap{
            {QStringLiteral("role"),    QStringLiteral("diff")},
            {QStringLiteral("path"),    result.value(QStringLiteral("relPath")).toString()},
            {QStringLiteral("absPath"), abs},
            {QStringLiteral("diff"),    result.value(QStringLiteral("diff")).toString()},
            {QStringLiteral("arguments"), AutomationArtifactStore::redact(executedArgs).left(8192)},
            {QStringLiteral("beforeImagePath"), result.value(QStringLiteral("beforeImagePath"))},
            {QStringLiteral("afterImagePath"), result.value(QStringLiteral("afterImagePath"))},
            {QStringLiteral("typing"),  false}});
        emit messagesChanged();
    }

    // Al contexto va la versión acotada (salvo que ya sea un stub de dedup).
    QString contextResult = dedup ? res : budgetToolOutput(name, res);
    const AgentProgressGovernor::Decision progress = m_progressGovernor.record(
        name, executedArgs, ok, res, isWrite || externalWrite);
    if (progress.progress) ++m_progressEvents; else ++m_stagnationEvents;
    AgentEventLog::append(m_cwd, m_sessionId,
                          progress.progress ? QStringLiteral("progress")
                                            : QStringLiteral("stagnation"),
                          {{QStringLiteral("tool"), name},
                           {QStringLiteral("toolCallId"), callId},
                           {QStringLiteral("semanticKey"), progress.semanticKey},
                           {QStringLiteral("credits"), progress.credits},
                           {QStringLiteral("stagnant"), progress.stagnant},
                           {QStringLiteral("reason"), progress.reason}});
    if (progress.action == AgentProgressGovernor::Replan) {
        ++m_replanEvents;
        contextResult += QStringLiteral(
            "\n[REPLANIFICACIÓN OBLIGATORIA: el presupuesto elástico detectó actividad "
            "sin avance. Resumí objetivo, evidencia obtenida y pendiente; elegí una "
            "estrategia materialmente distinta o finalizá si ya cumpliste. No generes "
            "más variantes superficiales del mismo artefacto.]");
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("replan"),
                              {{QStringLiteral("semanticKey"), progress.semanticKey},
                               {QStringLiteral("reason"), progress.reason}});
    }
    if (progress.objectiveSatisfied) {
        contextResult += QStringLiteral(
            "\n[Los artefactos nombrados explícitamente en el objetivo ya existen. "
            "Hacé como máximo una verificación relevante y finalizá; no agregues "
            "scaffolding ni variantes no solicitadas.]");
    }
    if (failureSpiral) {
        contextResult += QStringLiteral(
            "\n[anti-loop: %1 ejecuciones consecutivas terminaron con el mismo "
            "problema aunque hayan cambiado los comandos o argumentos. No sigas "
            "probando variantes a ciegas: releé el error, comprobá la causa raíz "
            "y cambiá de estrategia.]").arg(m_loopPolicy.failureSpiral);
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                              QJsonObject{{QStringLiteral("tool"), name},
                                          {QStringLiteral("toolCallId"), callId},
                                          {QStringLiteral("reason"), QStringLiteral("failure_spiral")},
                                          {QStringLiteral("repeatCount"), m_equivalentFailures},
                                          {QStringLiteral("fingerprint"), m_failureFingerprint}});
    }
    appendToolResult(callId, name, contextResult);

    if (progress.action == AgentProgressGovernor::Stop) {
        const QString notice = QStringLiteral(
            "[turno detenido: no hubo progreso verificable después de replantear; "
            "se preservaron todos los resultados y archivos obtenidos]");
        AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("failure"),
                              {{QStringLiteral("reason"), QStringLiteral("semantic_stagnation")},
                               {QStringLiteral("semanticKey"), progress.semanticKey}});
        finishTurn(notice, true, QStringLiteral("failed"));
        return;
    }

    if (progress.action == AgentProgressGovernor::Replan && !m_pendingCalls.isEmpty()) {
        // Mantener el contrato OpenAI: toda tool_call del mensaje assistant recibe
        // tool_result, pero no ejecutamos el resto de un lote ya clasificado como
        // espiral. El siguiente completion debe producir un plan nuevo.
        for (const QJsonValue &pendingValue : std::as_const(m_pendingCalls)) {
            const QJsonObject pending = pendingValue.toObject();
            const QJsonObject fn = pending.value(QStringLiteral("function")).toObject();
            appendToolResult(pending.value(QStringLiteral("id")).toString(),
                             fn.value(QStringLiteral("name")).toString(),
                             QStringLiteral("[omitida: replanteo obligatorio antes de continuar]"));
        }
        m_pendingCalls = {};
    }

    // Captura visual (desktop_observe / screenshots): si el server tiene visión,
    // encolar la imagen para inyectarla al contexto al cerrar el turno de tools.
    // Así el modelo VE lo que pidió observar (loop de debug visual recursivo).
    const QString imagePath = result.value(QStringLiteral("imagePath")).toString();
    if (!imagePath.isEmpty() && m_visionReady && !m_textToolFallback) {
        const QString uri = imageDataUri(imagePath);
        if (!uri.isEmpty()) m_pendingObservations << uri;
    }

    processPendingCalls();
}

int LlamaAgentBackend::repeatedSuffixStart(const QString &text, int repeats,
                                           int minBlockChars)
{
    if (repeats < 2 || minBlockChars < 1 || text.size() < repeats * minBlockChars)
        return -1;
    const int maxBlock = qMin(2048, text.size() / repeats);
    // Buscar de mayor a menor conserva la unidad semántica más amplia (párrafo)
    // cuando también existen subcadenas periódicas dentro de ella.
    for (int block = maxBlock; block >= minBlockChars; --block) {
        const int start = text.size() - repeats * block;
        const QStringView unit(text.constData() + start, block);
        bool same = true;
        for (int copy = 1; copy < repeats; ++copy) {
            if (QStringView(text.constData() + start + copy * block, block) != unit) {
                same = false;
                break;
            }
        }
        if (same) return start;
    }
    return -1;
}

QString LlamaAgentBackend::failureFingerprint(const QString &tool, const QString &result)
{
    QString normalized = result.toLower().trimmed();
    normalized.replace(QRegularExpression(QStringLiteral("[a-z]:[/\\\\][^\\r\\n:]+")),
                       QStringLiteral("<path>"));
    normalized.replace(QRegularExpression(QStringLiteral("(?:[/\\\\][^\\s:]+){2,}")),
                       QStringLiteral("<path>"));
    normalized.replace(QRegularExpression(QStringLiteral("\\b(?:0x[0-9a-f]+|\\d+)\\b")),
                       QStringLiteral("#"));
    normalized.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    // La cola suele contener la causa; acotarla evita que logs previos distintos
    // oculten un error final idéntico.
    normalized = normalized.right(2048);
    return tool.section(QLatin1Char('_'), 0, 0) + QLatin1Char('|')
        + QString::fromLatin1(QCryptographicHash::hash(normalized.toUtf8(),
                                                       QCryptographicHash::Sha256).toHex());
}

QString LlamaAgentBackend::toolCallSignature(const QString &tool, const QString &arguments)
{
    QJsonParseError error;
    const QJsonDocument parsed = QJsonDocument::fromJson(arguments.trimmed().toUtf8(), &error);
    QByteArray canonical;
    if (error.error == QJsonParseError::NoError && !parsed.isNull()) {
        canonical = parsed.toJson(QJsonDocument::Compact);
    } else {
        QString fallback = arguments.trimmed();
        fallback.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        canonical = fallback.toUtf8();
    }
    return tool.trimmed().toLower() + QLatin1Char('|') + QString::fromUtf8(canonical);
}

int LlamaAgentBackend::toolWatchdogSeconds(const QString &tool,
                                           const QJsonObject &arguments,
                                           int quickTimeoutSec, int webTimeoutSec)
{
    if (tool == QLatin1String("run_shell"))
        return qBound(5, arguments.value(QStringLiteral("timeout_s")).toInt(120) + 15, 1815);
    if (tool.startsWith(QLatin1String("mcp__")) || tool == QLatin1String("mcp_call_tool")
        || tool.contains(QStringLiteral("research")) || tool.startsWith(QLatin1String("web_")))
        return qBound(10, webTimeoutSec, 1800);
    return qBound(5, quickTimeoutSec, 120);
}

bool LlamaAgentBackend::recordToolOutcome(const QString &tool, bool ok, bool isWrite,
                                          const QString &result)
{
    if (!ok) m_turnHadDifficulty = true;
    else if (m_turnHadDifficulty) m_turnRecovered = true;
    if (ok || isWrite) {
        m_failureFingerprint.clear();
        m_equivalentFailures = 0;
        return false;
    }
    const QString fp = failureFingerprint(tool, result);
    if (fp == m_failureFingerprint) ++m_equivalentFailures;
    else {
        m_failureFingerprint = fp;
        m_equivalentFailures = 1;
    }
    return m_equivalentFailures == m_loopPolicy.failureSpiral;
}

// run_shell async: arranca → crear tarjeta "en vivo" (typing) con el comando.
void LlamaAgentBackend::onToolStarted(const QVariantMap &info)
{
    const QString callId = info.value(QStringLiteral("callId")).toString();
    if (callId.isEmpty() || callId != m_execCallId) return;   // ajeno/tardío
    const QString name = info.value(QStringLiteral("name")).toString();
    const QString cmd  = info.value(QStringLiteral("command")).toString();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_messages.append(QVariantMap{
        {QStringLiteral("role"),    QStringLiteral("toolcall")},
        {QStringLiteral("name"),    name},
        {QStringLiteral("kind"),    info.value(QStringLiteral("kind"))},
        {QStringLiteral("ok"),      true},
        {QStringLiteral("command"), cmd},
        {QStringLiteral("output"),  QString()},
        {QStringLiteral("typing"),  true},          // en ejecución
        {QStringLiteral("createdAt"),   static_cast<double>(nowMs)},
        {QStringLiteral("completedAt"), static_cast<double>(nowMs)},
        {QStringLiteral("elapsedMs"), 0},
        {QStringLiteral("tokens"), 0},
        {QStringLiteral("tps"), 0.0}});
    m_liveToolMsgIdx = m_messages.size() - 1;
    m_liveToolCallId = callId;
    m_lastToolEmitMs = 0;
    emit messagesChanged();
}

// run_shell async: chunk de salida → anexar a la tarjeta en vivo (throttle).
void LlamaAgentBackend::onToolOutputChunk(const QString &callId, const QString &chunk)
{
    if (callId != m_liveToolCallId || m_liveToolMsgIdx < 0
        || m_liveToolMsgIdx >= m_messages.size()) return;
    // Actividad real renueva el watchdog: un build largo con salida no se corta.
    if (m_toolWatchdog->isActive())
        m_toolWatchdog->start(m_toolWatchdog->interval());
    QVariantMap card = m_messages[m_liveToolMsgIdx].toMap();
    QString out = card.value(QStringLiteral("output")).toString() + chunk;
    if (out.size() > 64 * 1024) out = out.right(64 * 1024);   // cola visible
    card[QStringLiteral("output")] = out;
    m_messages[m_liveToolMsgIdx] = card;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastToolEmitMs >= 100) {        // ~10 fps
        m_lastToolEmitMs = now;
        emit messagesChanged();
    }
}

// ───────────────────────────── Subagents (tool `task`) ───────────────────
static QString runGitCap(const QStringList &args, const QString &dir, int *exitCode = nullptr)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    p.start(QStringLiteral("git"), args);
    if (!p.waitForStarted(5000)) { if (exitCode) *exitCode = -1; return QStringLiteral("git no disponible"); }
    p.waitForFinished(20000);
    if (exitCode) *exitCode = p.exitCode();
    return QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
}

// ¿git instalado? (cacheado). Subagents lo requieren para aislar en worktrees.
static bool gitInstalled()
{
    static int cached = -1;   // -1 desconocido, 0 no, 1 sí
    if (cached < 0)
        cached = QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty() ? 0 : 1;
    return cached == 1;
}

// Aísla al sub-agente en una git worktree (rama nueva desde HEAD). Inicializa el
// repo si el cwd aún no lo es. Devuelve "" si git NO está instalado (el caller
// pide instalarlo). isolated=true si quedó aislado.
QString LlamaAgentBackend::createWorktree(const QString &callId, bool &isolated)
{
    isolated = false;
    // Aislamiento apagado por perfil: el sub-agente comparte el cwd del padre.
    // Es más rápido (no hay worktree ni merge) y a veces es lo que se quiere en un
    // repo chico, pero pierde el aislamiento de escrituras concurrentes.
    if (!m_escalationPolicy.isolateSubagents) return QString();
    if (!gitInstalled()) return QString();   // → launchSub pide instalar git

    const QString sid = QString(callId).remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]"))).left(10);
    int ex = 0;
    runGitCap({QStringLiteral("rev-parse"), QStringLiteral("--is-inside-work-tree")}, m_cwd, &ex);
    if (ex != 0) {
        // cwd no es repo git → inicializarlo + commit base (necesario para worktree).
        runGitCap({QStringLiteral("init")}, m_cwd, &ex);
        runGitCap({QStringLiteral("-c"), QStringLiteral("user.email=agent@llamacode"),
                   QStringLiteral("-c"), QStringLiteral("user.name=llamacode"),
                   QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("--no-verify"),
                   QStringLiteral("-m"), QStringLiteral("llamacode: init para subagents")}, m_cwd, &ex);
        // Si había archivos sin trackear, snapshot inicial para que HEAD los tenga.
        runGitCap({QStringLiteral("add"), QStringLiteral("-A")}, m_cwd);
        runGitCap({QStringLiteral("-c"), QStringLiteral("user.email=agent@llamacode"),
                   QStringLiteral("-c"), QStringLiteral("user.name=llamacode"),
                   QStringLiteral("commit"), QStringLiteral("--no-verify"),
                   QStringLiteral("-m"), QStringLiteral("llamacode: snapshot inicial")}, m_cwd, &ex);
    }

    const QString base = QDir::tempPath() + QStringLiteral("/llamacode-worktrees");
    QDir().mkpath(base);
    const QString path = QDir::cleanPath(base + QStringLiteral("/") + sid);
    QDir(path).removeRecursively();
    const QString branch = QStringLiteral("llamacode-sub/") + sid;
    runGitCap({QStringLiteral("worktree"), QStringLiteral("prune")}, m_cwd);
    runGitCap({QStringLiteral("worktree"), QStringLiteral("add"), QStringLiteral("-b"), branch,
               path, QStringLiteral("HEAD")}, m_cwd, &ex);
    if (ex != 0) return QString();   // falló crear la worktree
    isolated = true;
    m_subBranch.insert(callId, branch);
    return path;
}

// Commit en la worktree + merge a la rama actual (abort si conflicto, preservando
// la rama). Limpia worktree.
QString LlamaAgentBackend::mergeAndCleanupWorktree(const QString &callId, bool ok, bool isolated)
{
    if (!isolated) return QString();
    const QString path = m_subWorktree.value(callId);
    const QString branch = m_subBranch.value(callId);
    QString note;
    bool preserveBranch = false;

    if (ok && !path.isEmpty()) {
        runGitCap({QStringLiteral("add"), QStringLiteral("-A")}, path);
        int cex = 0;
        runGitCap({QStringLiteral("-c"), QStringLiteral("user.email=sub@llamacode"),
                   QStringLiteral("-c"), QStringLiteral("user.name=subagent"),
                   QStringLiteral("commit"), QStringLiteral("--no-verify"),
                   QStringLiteral("-m"), QStringLiteral("subagent ") + callId.left(8)}, path, &cex);
        if (cex == 0) {
            int mex = 0;
            const QString mout = runGitCap({QStringLiteral("merge"), QStringLiteral("--no-edit"),
                                            QStringLiteral("--no-ff"), branch}, m_cwd, &mex);
            if (mex == 0) {
                note = QStringLiteral("\n[✓ cambios del sub-agente mergeados a la rama actual]");
            } else {
                // Conflicto: repo LIMPIO (abort) + preservar rama para merge manual.
                runGitCap({QStringLiteral("merge"), QStringLiteral("--abort")}, m_cwd);
                preserveBranch = true;
                note = QStringLiteral("\n[⚠ conflicto de merge — abortado, repo intacto. "
                                      "Los cambios quedaron en la rama '%1': mergeala a mano.\n%2]")
                           .arg(branch, mout.left(300));
            }
        } else {
            note = QStringLiteral("\n[el sub-agente no dejó cambios]");
        }
    }
    if (!path.isEmpty())
        runGitCap({QStringLiteral("worktree"), QStringLiteral("remove"), QStringLiteral("--force"), path}, m_cwd);
    if (!branch.isEmpty() && !preserveBranch)
        runGitCap({QStringLiteral("branch"), QStringLiteral("-D"), branch}, m_cwd);
    return note;
}

// Encola las task y lanza hasta kMaxParallelSubs en paralelo.
void LlamaAgentBackend::spawnTasks(const QJsonArray &taskCalls)
{
    for (const QJsonValue &v : taskCalls) m_subQueue.append(v);
    const int limit = subagentLimit();
    emit logAppended(QStringLiteral("[subagents: %1 encoladas (máx %2 en paralelo)]\n")
                         .arg(taskCalls.size()).arg(limit));
    pumpSubs();
}

// Lanza subs de la cola hasta llenar el cap. Si no queda nada activo, continúa el turno.
void LlamaAgentBackend::pumpSubs()
{
    const int limit = subagentLimit();
    while (m_subs.size() < limit && !m_subQueue.isEmpty()) {
        const QJsonObject call = m_subQueue.takeAt(0).toObject();
        launchSub(call);
    }
    if (!subsActive()) processPendingCalls();   // todas resueltas/ inválidas → seguir
}

void LlamaAgentBackend::launchSub(const QJsonObject &call)
{
    const QString id = call.value(QStringLiteral("id")).toString();
    const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
    const QString argStr = toolArgumentsToString(fn.value(QStringLiteral("arguments")));
    const QJsonObject a = QJsonDocument::fromJson(argStr.toUtf8()).object();
    const QString prompt = a.value(QStringLiteral("prompt")).toString();
    const QString desc = a.value(QStringLiteral("description")).toString();

    if (prompt.trimmed().isEmpty()) {
        ++m_toolFail;
        appendToolResult(id, QStringLiteral("task"), QStringLiteral("[error: 'prompt' vacío para task]"));
        return;   // no arranca runner; pumpSubs sigue
    }

    bool isolated = false;
    const QString wt = createWorktree(id, isolated);
    if (wt.isEmpty()) {
        // git no instalado (o falló la worktree) → pedir instalación, no ejecutar.
        ++m_toolFail;
        emit gitRequired();
        appendToolCard(QStringLiteral("task"), QStringLiteral("task"), false,
                       desc.isEmpty() ? prompt.left(60) : desc,
                       QStringLiteral("[Git no está instalado. Los subagents necesitan git para "
                                      "aislar el trabajo en una worktree. Instalá git y reintentá.]"));
        appendToolResult(id, QStringLiteral("task"),
                         QStringLiteral("[error: subagents requieren git instalado. Se le pidió al "
                                        "usuario instalarlo. Resolvé la subtarea vos directamente "
                                        "en vez de usar task.]"));
        return;
    }
    m_subWorktree.insert(id, wt);
    m_subIsolated.insert(id, isolated);

    emit agentLifecycleEvent(AgentLifecycle::toolEvent(
        QStringLiteral("tool.start"), m_sessionId, m_cwd, m_correlationId,
        id, QStringLiteral("task"), argStr));

    appendToolCard(QStringLiteral("task"), QStringLiteral("task"), true,
                   desc.isEmpty() ? prompt.left(60) : desc,
                   isolated ? QStringLiteral("[worktree git aislada]\n")
                            : QStringLiteral("[sin aislamiento: mismo cwd que el agente]\n"));
    if (!m_messages.isEmpty()) {
        QVariantMap card = m_messages.last().toMap();
        card[QStringLiteral("typing")] = true;
        m_messages[m_messages.size() - 1] = card;
        m_subMsgIdx.insert(id, m_messages.size() - 1);
    }
    emit messagesChanged();

    auto *sub = new SubAgentRunner(id, m_ctx.serverBaseUrl, m_ctx.modelId,
                                   wt, prompt, m_temperature,
                                   m_directives.contains(QStringLiteral("honey")), this);
    // Propagar el guardrail: en modo super el agente principal ya no gatea nada,
    // así que tampoco lo imponemos al sub-árbol (autonomía total, coherente).
    sub->setHitlDestructive(m_hitlDestructive && m_approvalMode != QLatin1String("super"));
    connect(sub, &SubAgentRunner::finished, this, &LlamaAgentBackend::onSubFinished);
    connect(sub, &SubAgentRunner::progressed, this, &LlamaAgentBackend::onSubProgress);
    m_subs.insert(id, sub);
    sub->start();
}

void LlamaAgentBackend::onSubProgress(const QString &id, const QString &note)
{
    const int idx = m_subMsgIdx.value(id, -1);
    if (idx < 0 || idx >= m_messages.size()) return;
    QVariantMap card = m_messages[idx].toMap();
    QString out = card.value(QStringLiteral("output")).toString() + note + QStringLiteral("\n");
    if (out.size() > 16 * 1024) out = out.right(16 * 1024);
    card[QStringLiteral("output")] = out;
    m_messages[idx] = card;
    emit messagesChanged();
}

void LlamaAgentBackend::onSubFinished(const QString &id, const QString &result, bool ok)
{
    if (!m_subs.contains(id)) return;
    SubAgentRunner *sub = m_subs.take(id);
    if (sub) sub->deleteLater();

    const bool isolated = m_subIsolated.value(id, false);
    const QString mergeNote = mergeAndCleanupWorktree(id, ok, isolated);

    const int idx = m_subMsgIdx.value(id, -1);
    if (idx >= 0 && idx < m_messages.size()) {
        QVariantMap card = m_messages[idx].toMap();
        card[QStringLiteral("typing")] = false;
        card[QStringLiteral("ok")] = ok;
        QString out = card.value(QStringLiteral("output")).toString()
                      + QStringLiteral("\n") + result + mergeNote;
        card[QStringLiteral("output")] = out.left(64 * 1024);
        m_messages[idx] = card;
    }
    emit messagesChanged();

    if (ok) ++m_toolOk; else ++m_toolFail;
    appendToolResult(id, QStringLiteral("task"), (result + mergeNote).left(16 * 1024),
                     ok, false, false);

    m_subWorktree.remove(id); m_subBranch.remove(id);
    m_subIsolated.remove(id); m_subMsgIdx.remove(id);
    pumpSubs();   // lanza el siguiente encolado; si no queda nada → sigue el turno
}

// Cancela y limpia todos los sub-agentes en vuelo + la cola (PARAR/interrupt/stop).
void LlamaAgentBackend::cancelAllSubs()
{
    m_subQueue = QJsonArray();
    const auto ids = m_subs.keys();
    for (const QString &id : ids) {
        SubAgentRunner *sub = m_subs.take(id);
        if (sub) { disconnect(sub, nullptr, this, nullptr); sub->cancel(); sub->deleteLater(); }
        mergeAndCleanupWorktree(id, false, m_subIsolated.value(id, false));
        m_subWorktree.remove(id); m_subBranch.remove(id);
        m_subIsolated.remove(id); m_subMsgIdx.remove(id);
    }
}

void LlamaAgentBackend::emitToolLifecycleFinish(const QString &id, const QString &name,
                                                bool ok, bool isWrite, bool externalWrite,
                                                const QString &content,
                                                const QStringList &paths)
{
    if (id.isEmpty()) return;
    const QString key = m_correlationId + QLatin1Char(':') + id;
    if (m_lifecycleFinishedToolIds.contains(key)) return;
    m_lifecycleFinishedToolIds.insert(key);
    emit agentLifecycleEvent(AgentLifecycle::toolResult(
        m_sessionId, m_cwd, m_correlationId, id, name, ok, isWrite,
        externalWrite, content, paths));
}

void LlamaAgentBackend::appendToolResult(const QString &id, const QString &name,
                                         const QString &content, bool ok, bool isWrite,
                                         bool externalWrite, const QStringList &paths)
{
    emitToolLifecycleFinish(id, name, ok, isWrite, externalWrite, content, paths);
    if (m_textToolFallback) {
        const QString compact = budgetTextToolOutput(name, content);
        appendApiMessage(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("content"), QStringLiteral("TOOL_RESULT %1 %2:\n%3")
                                        .arg(id, name, compact)}
        });
        return;
    }
    appendApiMessage(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("tool")},
        {QStringLiteral("tool_call_id"), id},
        {QStringLiteral("content"), content}
    });
}

QStringList LlamaAgentBackend::requiredArgs(const QString &name)
{
    if (name == QLatin1String("context_checkpoint"))
        return {QStringLiteral("summary")};
    if (name == QLatin1String("mcp_search_tools")) return {QStringLiteral("query")};
    if (name == QLatin1String("mcp_call_tool")) return {QStringLiteral("name")};
    if (name == QLatin1String("read_file"))  return {QStringLiteral("path")};
    if (name == QLatin1String("grep"))       return {QStringLiteral("pattern")};
    if (name == QLatin1String("glob"))       return {QStringLiteral("pattern")};
    if (name == QLatin1String("write_file")) return {QStringLiteral("path"), QStringLiteral("content")};
    // edit_file: new_string puede ser vacío (borrar texto) → NO requerirlo aquí
    // (el chequeo de requeridos rechaza strings vacíos).
    if (name == QLatin1String("edit_file")) return {QStringLiteral("path"), QStringLiteral("old_string")};
    if (name == QLatin1String("run_shell"))  return {QStringLiteral("command")};
    if (name == QLatin1String("web_fetch"))  return {QStringLiteral("url")};
    if (name == QLatin1String("web_search")) return {QStringLiteral("query")};
    if (name == QLatin1String("deep_research")) return {QStringLiteral("query")};
    if (name == QLatin1String("search_docs")) return {QStringLiteral("query")};
    if (name == QLatin1String("semantic_search")) return {QStringLiteral("query")};
    if (name == QLatin1String("hybrid_search")) return {QStringLiteral("query")};
    if (name == QLatin1String("repo_slice")) return {QStringLiteral("query")};
    if (name == QLatin1String("context_scout")) return {QStringLiteral("query")};
    if (name == QLatin1String("context_fetch")) return {QStringLiteral("handle")};
    // verify_claims: 'claims' puede ser array → no se valida acá (toString() de un
    // array da "" y lo rechazaría); la propia tool valida.
    if (name == QLatin1String("task"))       return {QStringLiteral("prompt")};
    if (name == QLatin1String("ask_teacher")) return {QStringLiteral("question")};
    if (name == QLatin1String("browser_skill_replay")) return {QStringLiteral("name")};
    if (name == QLatin1String("desktop_launch")) return {QStringLiteral("app")};
    if (name == QLatin1String("desktop_focus")) return {QStringLiteral("target_id")};
    if (name == QLatin1String("desktop_resize"))
        return {QStringLiteral("target_id"), QStringLiteral("width"), QStringLiteral("height")};
    if (name == QLatin1String("desktop_controls")) return {QStringLiteral("target_id")};
    if (name == QLatin1String("desktop_click_element"))
        return {QStringLiteral("target_id"), QStringLiteral("control_id")};
    if (name == QLatin1String("desktop_click_text"))
        return {QStringLiteral("target_id"), QStringLiteral("text")};
    if (name == QLatin1String("desktop_find_image")
        || name == QLatin1String("desktop_click_image")
        || name == QLatin1String("desktop_wait_image")
        || name == QLatin1String("desktop_assert_image"))
        return {QStringLiteral("target_id"), QStringLiteral("template_path")};
    if (name == QLatin1String("desktop_type")) return {QStringLiteral("text")};
    if (name == QLatin1String("desktop_key")) return {QStringLiteral("key")};
    return {};   // list_dir, memory, browser_skill_list: args opcionales
}

int LlamaAgentBackend::adaptiveSubagentLimit(int parallelSlots, int ctxTokens,
                                             double vramTotalMb, double vramFreeMb)
{
    // Un perfil de un slot todavía puede delegar, pero lo hará secuencialmente.
    int limit = qBound(1, parallelSlots, kAbsoluteMaxParallelSubs);

    // Contextos largos multiplican el KV vivo por secuencia. Estos cortes son
    // deliberadamente conservadores y funcionan también sin telemetría de GPU.
    if (ctxTokens >= 131072) limit = qMin(limit, 1);
    else if (ctxTokens >= 65536) limit = qMin(limit, 2);
    else if (ctxTokens >= 32768) limit = qMin(limit, 3);

    // Tier de hardware: evita llenar todos los slots en GPUs chicas aunque el
    // perfil haya sido importado con un --parallel demasiado optimista.
    if (vramTotalMb > 0.0) {
        if (vramTotalMb < 8192.0) limit = qMin(limit, 1);
        else if (vramTotalMb < 12288.0) limit = qMin(limit, 2);
        else if (vramTotalMb < 20480.0) limit = qMin(limit, 3);
        else if (vramTotalMb < 32768.0) limit = qMin(limit, 4);

        // Sólo recortar por headroom cuando queda extremadamente poco. El modelo
        // y el KV suelen estar ya reservados, por eso no usamos porcentajes altos.
        if (vramFreeMb > 0.0 && vramFreeMb < 384.0) limit = 1;
        else if (vramFreeMb > 0.0 && vramFreeMb < 768.0) limit = qMin(limit, 2);
    }
    return qMax(1, limit);
}

int LlamaAgentBackend::subagentLimit() const
{
    // Tres topes en cascada: el del perfil (spec), el adaptativo por
    // slots/contexto/VRAM, y el absoluto del harness. Gana el menor.
    const int adaptive = adaptiveSubagentLimit(m_ctx.parallelSlots,
                                               m_ctx.ctxOverride > 0 ? m_ctx.ctxOverride : m_ctxLimit,
                                               m_ctx.vramTotalMb, m_ctx.vramFreeMb);
    const int fromSpec = qBound(1, m_escalationPolicy.maxParallelSubagents,
                                kAbsoluteMaxParallelSubs);
    return qMin(adaptive, fromSpec);
}

void LlamaAgentBackend::setEscalationPolicy(const HarnessEscalationModule &escalation)
{
    m_escalationPolicy = escalation;
    m_escalationPolicy.set = true;
    if (!escalation.masterEscalation.isEmpty())
        m_masterEscalation = escalation.masterEscalation;
    if (escalation.masterAutoAfterFails > 0)
        m_masterAutoAfterFails = escalation.masterAutoAfterFails;
    for (LlamaAgentBackend *rt : std::as_const(m_sessionRuntimes))
        if (rt) rt->setEscalationPolicy(escalation);
}

bool LlamaAgentBackend::redundantDesktopConfirmKey(const QString &previousTool,
                                                   const QString &previousTypeText,
                                                   const QString &key)
{
    if (previousTool != QLatin1String("desktop_type"))
        return false;
    const QString typed = previousTypeText.trimmed();
    if (typed.isEmpty() || !typed.endsWith(QLatin1Char('=')))
        return false;
    const QString k = key.trimmed().toLower();
    return k == QLatin1String("=")
           || k == QLatin1String("enter")
           || k == QLatin1String("return");
}

bool LlamaAgentBackend::toolSupportFromProps(const QJsonObject &props, bool *haveTemplate)
{
    const QString tmpl = props.value(QStringLiteral("chat_template")).toString();
    const QJsonObject caps = props.value(QStringLiteral("chat_template_caps")).toObject();
    if (haveTemplate) *haveTemplate = !tmpl.isEmpty() || !caps.isEmpty();
    // `chat_template_caps` es lo que el propio server dedujo del template; el
    // regex sobre el jinja es la señal de reserva para binarios viejos.
    const QJsonValue capTools = caps.value(QStringLiteral("supports_tool_calls"));
    if (capTools.isBool()) return capTools.toBool();
    return ToolCallingSupport::templateMentionsTools(tmpl);
}

int LlamaAgentBackend::secondTextToolCallStart(const QString &content)
{
    static const QRegularExpression marker(QStringLiteral("\\bTOOL_CALL\\b"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch first = marker.match(content);
    if (!first.hasMatch()) return -1;
    // El primer TOOL_CALL tiene que estar completo (JSON balanceado) antes de
    // cortar: si no, el "segundo" podría ser texto dentro de los argumentos.
    if (extractBalancedJsonObject(content, first.capturedEnd()).isEmpty()) return -1;
    const QRegularExpressionMatch second = marker.match(content, first.capturedEnd());
    if (!second.hasMatch()) return -1;
    return second.capturedStart();
}

QJsonObject LlamaAgentBackend::textToolCallFromContent(const QString &content)
{
    // El nombre y los args de la tool pueden venir en dos formatos:
    //  (a) El que instruimos en buildTextToolPayload:
    //        TOOL_CALL {"name":"x","arguments":{...}}
    //  (b) El formato NATIVO que filtran algunos modelos (ej. Gemma) cuando su
    //      chat-template escupe sus tokens de tool-call como texto:
    //        <|tool_call>call:x{...}<tool_call|>
    //      Acá el nombre va en `call:x` y los args son un objeto JSON aparte
    //      (puede ser {}). El parser viejo sólo cubría (a) y descartaba (b)
    //      porque el {} no traía "name" → la tool nunca se ejecutaba y la
    //      llamada se filtraba como texto final (bug: "sumar 2+2" terminaba
    //      "ok" sin operar la calculadora).
    QString name;
    QJsonObject args;
    bool haveName = false;

    // Formato (a): marcador TOOL_CALL + objeto JSON con name/tool.
    const QRegularExpression marker(QStringLiteral("\\bTOOL_CALL\\b\\s*:?\\s*"),
                                    QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = marker.match(content);
    if (m.hasMatch()) {
        const QString json = extractBalancedJsonObject(content, m.capturedEnd());
        if (!json.isEmpty()) {
            QJsonParseError perr;
            const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8(), &perr).object();
            if (perr.error == QJsonParseError::NoError && !obj.isEmpty()) {
                name = obj.value(QStringLiteral("name")).toString(
                    obj.value(QStringLiteral("tool")).toString()).trimmed();
                if (!name.isEmpty()) {
                    haveName = true;
                    QJsonValue argsValue = obj.value(QStringLiteral("arguments"));
                    if (argsValue.isUndefined()) argsValue = obj.value(QStringLiteral("args"));
                    args = argsValue.toObject();
                    if (argsValue.isString()) {
                        QJsonParseError argErr;
                        args = QJsonDocument::fromJson(argsValue.toString().toUtf8(), &argErr).object();
                        if (argErr.error != QJsonParseError::NoError) args = {};
                    }
                }
            }
        }
    }

    // Formato (b): sólo si el texto trae el token nativo `tool_call` (evita
    // falsos positivos con prosa que use "call:"). Nombre en `call:NAME`, args
    // en el primer objeto JSON que le siga (opcional).
    if (!haveName && content.contains(QStringLiteral("tool_call"), Qt::CaseInsensitive)) {
        const QRegularExpression callRe(
            QStringLiteral("\\bcall\\s*:\\s*([A-Za-z_][A-Za-z0-9_]*)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch cm = callRe.match(content);
        if (cm.hasMatch()) {
            name = cm.captured(1).trimmed();
            haveName = !name.isEmpty();
            const QString json = extractBalancedJsonObject(content, cm.capturedEnd());
            if (!json.isEmpty()) {
                QJsonParseError perr;
                const QJsonObject obj = QJsonDocument::fromJson(json.toUtf8(), &perr).object();
                if (perr.error == QJsonParseError::NoError) args = obj;
            }
        }
    }

    // Formato KAT-Coder/Qwen3.5 oficial:
    // <tool_call><function=write_file><parameter=path>...</parameter>...
    // </function></tool_call>. El template de KAT lo usa para tool-calling,
    // mientras que llama.cpp lo entrega como texto cuando no activa un parser
    // nativo qwen3_coder. Convertirlo al contrato OpenAI evita que el contenido
    // XML termine escrito como si fuera código fuente.
    if (!haveName && content.contains(QStringLiteral("<tool_call"), Qt::CaseInsensitive)) {
        const QRegularExpression functionRe(
            QStringLiteral("<function\\s*=\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*>([\\s\\S]*?)</function>"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch fm = functionRe.match(content);
        if (fm.hasMatch()) {
            name = fm.captured(1).trimmed();
            haveName = !name.isEmpty();
            const QString body = fm.captured(2);
            const QRegularExpression parameterRe(
                QStringLiteral("<parameter\\s*=\\s*([^>]+)>([\\s\\S]*?)</parameter>"),
                QRegularExpression::CaseInsensitiveOption);
            auto stripParameterWrapperNewline = [](QString value) {
                // KAT/Qwen sometimes place a formatting newline immediately
                // inside the parameter tags. Remove only that wrapper newline;
                // preserve meaningful leading/trailing spaces in file content.
                if (value.startsWith(QStringLiteral("\r\n"))) value.remove(0, 2);
                else if (value.startsWith(QLatin1Char('\n'))
                         || value.startsWith(QLatin1Char('\r'))) value.remove(0, 1);
                if (value.endsWith(QStringLiteral("\r\n"))) value.chop(2);
                else if (value.endsWith(QLatin1Char('\n'))
                         || value.endsWith(QLatin1Char('\r'))) value.chop(1);
                return value;
            };
            QRegularExpressionMatchIterator it = parameterRe.globalMatch(body);
            while (it.hasNext()) {
                const QRegularExpressionMatch pm = it.next();
                args.insert(pm.captured(1).trimmed(),
                            stripParameterWrapperNewline(pm.captured(2)));
            }
        }
    }

    if (!haveName) return {};

    return QJsonObject{
        {QStringLiteral("id"), QStringLiteral("textcall_%1").arg(QUuid::createUuid().toString(QUuid::Id128))},
        {QStringLiteral("type"), QStringLiteral("function")},
        {QStringLiteral("function"), QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("arguments"),
             QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))}
        }}
    };
}

void LlamaAgentBackend::approveTool(const QString &id, bool always)
{
    if (!m_isSessionRuntime && id != m_awaitId) {
        for (LlamaAgentBackend *runtime : std::as_const(m_sessionRuntimes)) {
            if (runtime && runtime->m_awaitId == id) {
                runtime->approveTool(id, always);
                return;
            }
        }
    }
    if (id != m_awaitId) return;
    if (always) m_alwaysAllowed.insert(toolKind(
        m_awaitCall.value(QStringLiteral("function")).toObject()
            .value(QStringLiteral("name")).toString()));
    approveAndContinue(id, QStringLiteral("once"));
}

void LlamaAgentBackend::rejectTool(const QString &id)
{
    if (!m_isSessionRuntime && id != m_awaitId) {
        for (LlamaAgentBackend *runtime : std::as_const(m_sessionRuntimes)) {
            if (runtime && runtime->m_awaitId == id) {
                runtime->rejectTool(id);
                return;
            }
        }
    }
    if (id != m_awaitId) return;
    approveAndContinue(id, QStringLiteral("reject"));
}

// ───────────────────────────── Display helpers ───────────────────────────
// Crea una burbuja de asistente nueva si no hay una abierta (lazy).
void LlamaAgentBackend::ensureAssistantBubble()
{
    if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) return;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_messages.append(QVariantMap{
        {QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("content"), QString()},
        {QStringLiteral("typing"), true},
        {QStringLiteral("status"), QStringLiteral("Pensando...")},
        {QStringLiteral("createdAt"), static_cast<double>(nowMs)},
        {QStringLiteral("elapsedMs"), 0},
        {QStringLiteral("tokens"), 0},
        {QStringLiteral("tps"), 0.0}});
    m_curAsstIdx = m_messages.size() - 1;
    emit messagesChanged();
}

// Cierra la burbuja actual: descarta si está vacía, si no la finaliza. Así el
// texto del LLM y las tarjetas de tools quedan en mensajes separados.
void LlamaAgentBackend::closeAssistantBubble()
{
    if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
        QVariantMap m = m_messages[m_curAsstIdx].toMap();
        if (m.value(QStringLiteral("content")).toString().trimmed().isEmpty()) {
            m_messages.removeAt(m_curAsstIdx);
        } else {
            m[QStringLiteral("typing")] = false;
            m.remove(QStringLiteral("status"));
            // Finalizar métricas: en modo agente esta es la vía habitual de cierre
            // (la respuesta va seguida de tool calls), así que el cálculo de
            // tiempo/tps debe hacerse aquí igual que en finishTurn/setTyping.
            finalizeMsgMetrics(m, m_genTokens, m_genMs);
            m_messages[m_curAsstIdx] = m;
        }
        emit messagesChanged();
    }
    m_curAsstIdx = -1;
}

// Tarjeta independiente para una ejecución de tool (separada del texto LLM).
void LlamaAgentBackend::appendToolCard(const QString &name, const QString &kind, bool ok,
                                       const QString &command, const QString &output)
{
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    m_messages.append(QVariantMap{
        {QStringLiteral("role"),    QStringLiteral("toolcall")},
        {QStringLiteral("name"),    name},
        {QStringLiteral("kind"),    kind},
        {QStringLiteral("ok"),      ok},
        {QStringLiteral("command"), command},
        {QStringLiteral("output"),  output.left(64 * 1024)},
        {QStringLiteral("typing"),  false},
        {QStringLiteral("createdAt"),   static_cast<double>(nowMs)},
        {QStringLiteral("completedAt"), static_cast<double>(nowMs)},
        {QStringLiteral("elapsedMs"), 0},
        {QStringLiteral("tokens"), 0},
        {QStringLiteral("tps"), 0.0}});
    emit messagesChanged();
}

void LlamaAgentBackend::setAssistantStatus(const QString &status)
{
    if (m_curAsstIdx < 0 || m_curAsstIdx >= m_messages.size()) return;
    QVariantMap m = m_messages[m_curAsstIdx].toMap();
    if (!m.value(QStringLiteral("typing")).toBool()) return;
    if (!m.value(QStringLiteral("content")).toString().isEmpty()) return;
    if (m.value(QStringLiteral("status")).toString() == status) return;
    m[QStringLiteral("status")] = status;
    m_messages[m_curAsstIdx] = m;
    emit messagesChanged();
}

QString LlamaAgentBackend::toolStatusText(const QString &name, const QString &kind,
                                          const QString &detail)
{
    QString action;
    if (name == QLatin1String("write_file") || name == QLatin1String("edit_file"))
        action = QStringLiteral("Escribiendo archivo");
    else if (name == QLatin1String("run_shell"))
        action = QStringLiteral("Ejecutando comando");
    else if (name == QLatin1String("read_file"))
        action = QStringLiteral("Leyendo archivo");
    else if (name == QLatin1String("list_dir") || name == QLatin1String("glob")
             || name == QLatin1String("grep"))
        action = QStringLiteral("Buscando en el proyecto");
    else if (name.startsWith(kMcpPrefix))
        action = QStringLiteral("Usando MCP");
    else if (kind == QLatin1String("write"))
        action = QStringLiteral("Aplicando cambio");
    else if (kind == QLatin1String("shell"))
        action = QStringLiteral("Ejecutando comando");
    else
        action = QStringLiteral("Usando herramienta");

    QString tail = detail.trimmed();
    if (tail.size() > 90)
        tail = tail.left(87) + QStringLiteral("...");
    if (!tail.isEmpty())
        return QStringLiteral("%1: %2").arg(action, tail);
    return QStringLiteral("%1: %2...").arg(action, name);
}

void LlamaAgentBackend::appendAssistantText(const QString &text)
{
    if (m_curAsstIdx < 0 || m_curAsstIdx >= m_messages.size()) return;
    QVariantMap m = m_messages[m_curAsstIdx].toMap();
    const QString content = m.value(QStringLiteral("content")).toString() + text;
    if (m.value(QStringLiteral("genStartMs")).toDouble() <= 0.0)
        m[QStringLiteral("genStartMs")] =
            static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    m[QStringLiteral("content")] = content;
    m.remove(QStringLiteral("status"));
    finalizeMsgMetrics(m);
    m_messages[m_curAsstIdx] = m;
    emit messagesChanged();
}

void LlamaAgentBackend::setTyping(bool typing)
{
    if (m_curAsstIdx < 0 || m_curAsstIdx >= m_messages.size()) return;
    QVariantMap m = m_messages[m_curAsstIdx].toMap();
    m[QStringLiteral("typing")] = typing;
    if (!typing) {
        m.remove(QStringLiteral("status"));
        finalizeMsgMetrics(m);
    }
    m_messages[m_curAsstIdx] = m;
    emit messagesChanged();
}

void LlamaAgentBackend::releaseWorkClaim()
{
    if (m_workClaimId.isEmpty()) return;
    const QString claimId = m_workClaimId;
    m_workClaimId.clear();
    const bool released = WorkRegistry::release(m_cwd, claimId, m_sessionId);
    AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("work_claim"),
                          QJsonObject{{QStringLiteral("action"), QStringLiteral("release")},
                                      {QStringLiteral("claimId"), claimId},
                                      {QStringLiteral("released"), released}});
}

void LlamaAgentBackend::finishTurn(const QString &finalText, bool persistFinalToApi,
                                   const QString &terminalStatus)
{
    if (m_executionStep) {
        m_executionStep = false;
        m_executionPaused = true;
    }
    // Si hay texto final pero la burbuja se cerró tras una tool, abrir una nueva.
    if (!finalText.isEmpty() && m_curAsstIdx < 0)
        ensureAssistantBubble();
    // Largo REAL mostrado al usuario. En el path de streaming finishTurn se llama con
    // finalText vacío (la burbuja ya tiene el texto), así que loguear finalText.size()
    // daba SIEMPRE 0 y parecía "respuesta vacía" cuando no lo era. Medir la burbuja.
    int shownChars = finalText.size();
    if (m_curAsstIdx >= 0 && m_curAsstIdx < m_messages.size()) {
        QVariantMap m = m_messages[m_curAsstIdx].toMap();
        QString cur = m.value(QStringLiteral("content")).toString();
        if (!finalText.isEmpty()) {
            if (!cur.isEmpty()) cur += QStringLiteral("\n\n");
            cur += finalText;
        }
        m[QStringLiteral("content")] = cur;
        m[QStringLiteral("typing")]  = false;
        m.remove(QStringLiteral("status"));
        finalizeMsgMetrics(m, m_genTokens, m_genMs);
        m_messages[m_curAsstIdx] = m;
        shownChars = cur.size();
        emit messagesChanged();
    }
    if (persistFinalToApi && !finalText.isEmpty())
        appendApiMessage(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("assistant")},
            {QStringLiteral("content"), finalText}});
    emit logAppended(QStringLiteral("[turn] %1 (finalTextChars=%2)\n")
                         .arg(terminalStatus, QString::number(shownChars)));
    AgentEventLog::append(m_cwd, m_sessionId, QStringLiteral("assistant_final"),
                          QJsonObject{{QStringLiteral("chars"), shownChars},
                                      {QStringLiteral("toolOk"), m_toolOk},
                                      {QStringLiteral("toolFail"), m_toolFail},
                                      {QStringLiteral("progressEvents"), m_progressEvents},
                                      {QStringLiteral("stagnationEvents"), m_stagnationEvents},
                                       {QStringLiteral("replanEvents"), m_replanEvents},
                                       {QStringLiteral("text"), finalText.left(4096)}});
    finishDurableRun(terminalStatus.trimmed().isEmpty()
                         ? QStringLiteral("completed") : terminalStatus.trimmed().toLower(),
                     finalText.left(4096));
    m_curAsstIdx = -1;
    m_pendingCalls = {};
    m_awaitId.clear();
    m_awaitCall = {};
    m_execCallId.clear();
    m_toolWatchdog->stop();

    // Salud: tasa de éxito de tools en la sesión.
    const int total = m_toolOk + m_toolFail;
    if (total > 0)
        emit logAppended(QStringLiteral("[salud: %1/%2 tools ok (%3%)]\n")
                             .arg(m_toolOk).arg(total)
                             .arg(qRound(100.0 * m_toolOk / total)));

    releaseWorkClaim();
    saveCurrentSession();   // persistir al cerrar el turno

    // Aprendizaje por recuperación, inspirado en el patrón de skills de Hermes:
    // sólo reflexiona cuando hubo dificultad real + progreso posterior + cierre
    // del turno. La consolidación es async y no demora la respuesta al usuario.
    if (m_turnHadDifficulty && m_turnRecovered)
        consolidateMemory(true);

    emit turnFinished();
    emit sessionsChanged();

    // Turno cerrado → si hay mensajes encolados, enviar el próximo. Async (cola)
    // para no anidar runCompletion dentro del stack del turno que recién terminó.
    if (!m_msgQueue.isEmpty())
        QMetaObject::invokeMethod(this, "flushQueue", Qt::QueuedConnection);
}

// ───────────────────────────── Tools ─────────────────────────────────────
QString LlamaAgentBackend::toolKind(const QString &name)
{
    if (name == QLatin1String("mcp_search_tools") || name == QLatin1String("mcp_call_tool"))
        return QStringLiteral("mcp");
    if (name.startsWith(kMcpPrefix)) {
        const QString bare = name.section(QStringLiteral("__"), -1);
        if (bare == QLatin1String("write_file") || bare == QLatin1String("edit_file")
            || bare == QLatin1String("create_directory") || bare == QLatin1String("move_file"))
            return QStringLiteral("write");
        if (bare == QLatin1String("run_shell") || bare == QLatin1String("shell"))
            return QStringLiteral("shell");
        return QStringLiteral("read");
    }
    if (name == QLatin1String("run_shell")) return QStringLiteral("shell");
    if (name == QLatin1String("task")) return QStringLiteral("task");
    if (name == QLatin1String("write_file") || name == QLatin1String("edit_file"))
        return QStringLiteral("write");
    if (name == QLatin1String("worker_call")) return QStringLiteral("external");
    if (name == QLatin1String("email_send")) return QStringLiteral("email");
    if (DesktopComputerUse::isDesktopReadTool(name)) return QStringLiteral("read");
    if (DesktopComputerUse::isDesktopActionTool(name)) return QStringLiteral("desktop");
    return QStringLiteral("read");
}

bool LlamaAgentBackend::isDestructiveAction(const QString &name, const QJsonObject &args,
                                            const QString &desktopControlsText)
{
    const QString bare = name.startsWith(kMcpPrefix)
                             ? name.section(QStringLiteral("__"), -1)
                             : name;

    // An external plugin operation is intentionally opaque to the host. It is
    // therefore treated as an irreversible external effect unless the user is
    // explicitly in Super mode; this keeps auto profiles from silently granting
    // a third-party worker write-like authority.
    if (bare == QLatin1String("worker_call")) return true;

    // ── Shell destructivo ─────────────────────────────────────────────────
    if (bare == QLatin1String("run_shell") || bare == QLatin1String("shell")) {
        const QString cmd = args.value(QStringLiteral("command")).toString();
        // Regex case-insensitive de comandos irreversibles habituales (win + posix).
        static const QRegularExpression rx(
            QStringLiteral(
                "(?:^|[;&|]|\\s)(?:"
                "rm\\s+(?:-[a-z]*\\s+)*-[a-z]*(?:r[a-z]*f|f[a-z]*r)[a-z]*|" // rm -rf / rm -fr
                "del\\s+/|erase\\s+/|rmdir\\s+/s|rd\\s+/s|"                 // del /s, rmdir /s
                "format\\b|mkfs|dd\\s+if=|"                                  // format, mkfs, dd
                "git\\s+push\\s+.*(?:--force|-f)\\b|"                        // git push --force
                "git\\s+(?:reset\\s+--hard|clean\\s+-[a-z]*f)|"             // git reset --hard / clean -f
                "shutdown\\b|reboot\\b|"                                     // apagado
                "drop\\s+(?:table|database)|truncate\\s+table|"             // SQL destructivo
                ">\\s*/dev/sd|:\\s*>|>\\s*/dev/null\\s+2>&1\\s*;\\s*rm"      // wipes varios
                ")"),
            QRegularExpression::CaseInsensitiveOption);
        if (rx.match(cmd).hasMatch()) return true;
        return false;
    }

    // ── Desktop click sobre control destructivo ───────────────────────────
    // Marcadores compartidos por los dos caminos que llegan a un click con label
    // (UIA y OCR): el mismo botón tiene que gatear igual sin importar por dónde se
    // lo alcanzó.
    auto desktopLabelIsDestructive = [](const QString &s) {
        if (s.isEmpty()) return false;
        static const QStringList markers{
            QStringLiteral("delete"), QStringLiteral("eliminar"), QStringLiteral("borrar"),
            QStringLiteral("format"), QStringLiteral("formatear"), QStringLiteral("wipe"),
            QStringLiteral("uninstall"), QStringLiteral("desinstalar"),
            QStringLiteral("factory reset"), QStringLiteral("restablecer"),
            QStringLiteral("vaciar"), QStringLiteral("empty trash"), QStringLiteral("shred")};
        for (const QString &m : markers)
            if (s.contains(m)) return true;
        return false;
    };

    // desktop_click_element recibe un control_id normalmente opaco (un RuntimeId).
    // Resolvemos el nombre del control desde la salida cacheada del último
    // desktop_controls, que lista una línea por control:
    // 'controlId=<id> [role]...  "<name>"'.
    // desktop_click (coords x/y a ciegas) no expone label → no clasificable acá.
    if (bare == QLatin1String("desktop_click_element")) {
        const QString cid = args.value(QStringLiteral("control_id")).toString();
        QString label = args.value(QStringLiteral("name")).toString();
        // Candidatos a clasificar. Se evalúan TODOS y alcanza con que uno sea
        // destructivo: ante ambigüedad conviene pedir aprobación de más, no de menos.
        QStringList labels;
        QStringList cachedNames;
        if (!cid.isEmpty() && !desktopControlsText.isEmpty()) {
            const auto lines = desktopControlsText.split(QLatin1Char('\n'));
            for (const QString &ln : lines) {
                if (!ln.startsWith(QLatin1String("controlId=")) ) continue;
                // El nombre es el último tramo entrecomillado de la línea.
                const int q1 = ln.indexOf(QLatin1Char('"'));
                const int q2 = ln.lastIndexOf(QLatin1Char('"'));
                const QString nm = (q1 >= 0 && q2 > q1) ? ln.mid(q1 + 1, q2 - q1 - 1) : QString();
                cachedNames << nm;
                // Match exacto del id hasta el primer espacio.
                if (ln.mid(10).section(QLatin1Char(' '), 0, 0) == cid && label.isEmpty())
                    label = nm;
            }
        }
        if (!label.isEmpty()) labels << label;
        // clickElement acepta un NOMBRE cuando el id no existe (resuelve por
        // matching difuso). Entonces el propio control_id puede ser el label, y el
        // control al que va a parar es el que ese matching elija: clasificar ambos,
        // o el gate se saltearía justo cuando el modelo escribe "Eliminar todo".
        if (label.isEmpty() && !cid.isEmpty()) {
            labels << cid;
            const FuzzyMatch::Match m = FuzzyMatch::extractOne(cid, cachedNames);
            if (m.ok()) labels << cachedNames.at(m.index);
        }
        const QString s = labels.join(QLatin1Char(' ')).toLower();
        if (s.isEmpty()) return false;
        return desktopLabelIsDestructive(s);
    }

    // desktop_click_text clickea un texto leído por OCR: el propio `text` ES el
    // label, así que se clasifica igual. Sin esto, el mismo botón "Eliminar" que
    // gatea vía UIA pasaría libre sólo por haberse llegado por OCR.
    if (bare == QLatin1String("desktop_click_text"))
        return desktopLabelIsDestructive(args.value(QStringLiteral("text")).toString().toLower());

    // Las acciones semánticas nuevas no deben depender sólo de un label visible.
    // El modelo puede pasar metadata del target y UIA puede marcar un campo como
    // password; en ambos casos se fuerza aprobación antes de escribir o alternar.
    if (bare == QLatin1String("desktop_control_action")) {
        const QString action = args.value(QStringLiteral("action")).toString().toLower();
        const QVariantMap target = args.value(QStringLiteral("target")).toObject().toVariantMap();
        const QString label = args.value(QStringLiteral("name")).toString()
                              + QLatin1Char(' ')
                              + args.value(QStringLiteral("control_id")).toString();
        if (desktopLabelIsDestructive(label)
            || action == QLatin1String("delete") || action == QLatin1String("remove")
            || action == QLatin1String("format") || action == QLatin1String("uninstall"))
            return true;
        if (DesktopComputerUse::isSensitiveTarget(target)
            || args.value(QStringLiteral("sensitive")).toBool()
            || args.value(QStringLiteral("password")).toBool())
            return true;
    }
    if (bare == QLatin1String("desktop_type")) {
        if (args.value(QStringLiteral("sensitive")).toBool()
            || args.value(QStringLiteral("password")).toBool()
            || DesktopComputerUse::isSensitiveTarget(
                   args.value(QStringLiteral("target")).toObject().toVariantMap()))
            return true;
    }
    if (bare == QLatin1String("desktop_click")) {
        return desktopLabelIsDestructive(args.value(QStringLiteral("label")).toString())
            || desktopLabelIsDestructive(args.value(QStringLiteral("target_name")).toString());
    }

    // ── Memory / DB delete ────────────────────────────────────────────────
    if (bare == QLatin1String("memory")) {
        const QString a = args.value(QStringLiteral("action")).toString().toLower();
        if (a == QLatin1String("forget")) return true;
        if (a == QLatin1String("prune") && !args.value(QStringLiteral("dry_run")).toBool(false))
            return true;
        if (a == QLatin1String("decay") && !args.value(QStringLiteral("dry_run")).toBool(false))
            return true;
        return false;
    }
    if (bare == QLatin1String("graph")) {
        const QString a = args.value(QStringLiteral("action")).toString().toLower();
        if (a == QLatin1String("delete") || a == QLatin1String("forget")
            || a == QLatin1String("prune"))
            return true;
    }

    return false;
}

QJsonArray LlamaAgentBackend::toolSchemas()
{
    auto fn = [](const QString &name, const QString &desc, const QJsonObject &props,
                 const QJsonArray &required) {
        QJsonObject params{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), props}
        };
        if (!required.isEmpty()) {
            params.insert(QStringLiteral("required"), required);
        }
        return QJsonObject{
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), QJsonObject{
                {QStringLiteral("name"), name},
                {QStringLiteral("description"), desc},
                {QStringLiteral("parameters"), params}
            }}
        };
    };
    auto strProp = [](const QString &d) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                           {QStringLiteral("description"), d}};
    };
    auto intProp = [](const QString &d) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                           {QStringLiteral("description"), d}};
    };
    auto boolProp = [](const QString &d) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                           {QStringLiteral("description"), d}};
    };
    return QJsonArray{
        fn(QStringLiteral("read_file"),
           QStringLiteral("Lee un archivo de texto del proyecto. Para archivos grandes, "
                          "leé sólo el tramo que necesites con offset/limit en vez de todo."),
           QJsonObject{
               {QStringLiteral("path"), strProp(QStringLiteral("Ruta relativa al proyecto."))},
               {QStringLiteral("offset"), intProp(QStringLiteral("Línea inicial (1-based). Opcional."))},
               {QStringLiteral("limit"), intProp(QStringLiteral("Cantidad de líneas a leer desde offset. Opcional."))},
               {QStringLiteral("compact"), boolProp(QStringLiteral("Vista compacta segura para explorar C/C++/JS/TS/Java/Rust. Hace fallback exacto si no puede validarla; nunca usar como texto de edición."))}},
           QJsonArray{QStringLiteral("path")}),
        fn(QStringLiteral("list_dir"), QStringLiteral("Lista archivos y carpetas de un directorio."),
           QJsonObject{
               {QStringLiteral("path"), strProp(QStringLiteral("Ruta relativa (vacío = raíz)."))},
               {QStringLiteral("recursive"), boolProp(QStringLiteral("Listar recursivo (ignora node_modules/.git/etc). Default false."))}},
           QJsonArray{}),
        fn(QStringLiteral("project_brain"),
           QStringLiteral("Regenera y devuelve un índice persistente y liviano del proyecto: rutas, tamaños, fechas y distribución por extensión. No copia contenido fuente."),
           QJsonObject{
               {QStringLiteral("max_files"), intProp(QStringLiteral("Máximo de archivos a indexar (1-20000, default 4000)."))},
               {QStringLiteral("changed_paths"), QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("array")},
                    {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                   {QStringLiteral("description"), QStringLiteral("Rutas modificadas por watcher para actualización incremental sin recorrer todo el repo.")}}}},
           QJsonArray{}),
        fn(QStringLiteral("context_status"),
           QStringLiteral("Devuelve la frescura y las métricas del índice local de contexto. "
                          "Es sólo lectura y no fuerza una indexación completa."),
           QJsonObject{}, QJsonArray{}),
        fn(QStringLiteral("work_status"),
           QStringLiteral("Devuelve el trabajo activo de otras sesiones/agentes del mismo "
                          "proyecto: objetivos, rutas reclamadas y claims vigentes. Usala "
                          "antes de editar si hay trabajo paralelo."),
           QJsonObject{{QStringLiteral("max_claims"), intProp(
               QStringLiteral("Máximo de claims a devolver (1-50, default 8)."))}},
           QJsonArray{}),
        fn(QStringLiteral("context_scout"),
           QStringLiteral("Exploración compacta del repositorio: rankea chunks persistidos por "
                          "keywords, devuelve archivo:rango, previews, handles, vecinos y un "
                          "recibo de frescura/presupuesto. Usala antes de editar; después "
                          "context_fetch o read_file para obtener el código exacto."),
           QJsonObject{
               {QStringLiteral("query"), strProp(QStringLiteral("Objetivo concreto de la tarea o bug."))},
               {QStringLiteral("token_budget"), intProp(QStringLiteral("Presupuesto aproximado de tokens. Default 700."))},
               {QStringLiteral("k"), intProp(QStringLiteral("Máximo de rangos (default 8, máximo 15)."))},
               {QStringLiteral("expand_graph"), boolProp(QStringLiteral("Incluir vecinos por imports/includes. Default true."))},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio opcional."))}},
           QJsonArray{QStringLiteral("query")}),
        fn(QStringLiteral("context_fetch"),
           QStringLiteral("Obtiene el rango exacto identificado por context_scout. Valida el "
                          "hash del archivo y rechaza handles obsoletos."),
           QJsonObject{{QStringLiteral("handle"), strProp(QStringLiteral("Handle ctx:... devuelto por context_scout."))}},
           QJsonArray{QStringLiteral("handle")}),
        fn(QStringLiteral("grep"),
           QStringLiteral("Busca una EXPRESIÓN REGULAR en los archivos del proyecto (recursivo). "
                          "Ignora node_modules/.git/build/dist/venv/__pycache__ y binarios."),
           QJsonObject{
               {QStringLiteral("pattern"), strProp(QStringLiteral("Regex a buscar (sintaxis estilo PCRE)."))},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio opcional."))}},
           QJsonArray{QStringLiteral("pattern")}),
        fn(QStringLiteral("glob"),
           QStringLiteral("Lista archivos que matchean un patrón glob (recursivo). Ej: '**/*.qml', "
                          "'src/**/*.cpp', '*.json'. Usá '**' para cualquier subcarpeta."),
           QJsonObject{
               {QStringLiteral("pattern"), strProp(QStringLiteral("Patrón glob. '*'=segmento, '**'=recursivo, '?'=1 char."))},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio base opcional."))}},
           QJsonArray{QStringLiteral("pattern")}),
        fn(QStringLiteral("code_hotspots"),
           QStringLiteral("Lista los archivos MÁS RIESGOSOS del repo combinando historial git "
                          "(cuántos commits lo tocaron, cuántos autores) con cobertura de test. "
                          "Devuelve un score 1-10 por archivo y el motivo. Usalo ANTES de una "
                          "tanda de cambios para saber qué archivos son frágiles (mucho churn y "
                          "SIN test = donde más probable que metas una regresión). Requiere repo git."),
           QJsonObject{
               {QStringLiteral("top"), intProp(QStringLiteral("Cuántos archivos devolver. Default 20."))},
               {QStringLiteral("min_commits"), intProp(QStringLiteral("Ignora archivos con menos commits. Default 2."))},
               {QStringLiteral("since_days"), intProp(QStringLiteral("Ventana del historial en días. Default 0 = todo."))}},
           QJsonArray{}),
        fn(QStringLiteral("review_overengineering"),
           QStringLiteral("Revisa el diff actual de forma READ-ONLY y devuelve métricas y una "
                          "delete-list explicable de posibles abstracciones, scaffolding o cambios "
                          "especulativos. No modifica archivos, no ejecuta tests y no reemplaza la "
                          "revisión humana. Usala después de implementar y antes de cerrar la tarea."),
           QJsonObject{
               {QStringLiteral("scope"), strProp(QStringLiteral("'working_tree' (default) o 'staged'."))},
               {QStringLiteral("max_diff_chars"), intProp(QStringLiteral("Límite del diff leído; default 120000, máximo 500000."))}},
           QJsonArray{}),
        fn(QStringLiteral("write_file"), QStringLiteral("Escribe (crea/sobrescribe) un archivo de texto. "
                          "Para CAMBIOS PUNTUALES en un archivo existente preferí edit_file (mucho más rápido)."),
           QJsonObject{
               {QStringLiteral("path"), strProp(QStringLiteral("Ruta relativa al proyecto."))},
               {QStringLiteral("content"), strProp(QStringLiteral("Contenido completo del archivo."))}},
           QJsonArray{QStringLiteral("path"), QStringLiteral("content")}),
        fn(QStringLiteral("edit_file"),
           QStringLiteral("Edita un archivo existente reemplazando un fragmento EXACTO de texto. "
                          "old_string debe aparecer una sola vez (incluí contexto suficiente) salvo "
                          "que uses replace_all. new_string vacío = borrar el fragmento. Preferí esto "
                          "a reescribir el archivo entero con write_file."),
           QJsonObject{
               {QStringLiteral("path"), strProp(QStringLiteral("Ruta relativa al proyecto."))},
               {QStringLiteral("old_string"), strProp(QStringLiteral("Texto exacto a reemplazar (con su indentación)."))},
               {QStringLiteral("new_string"), strProp(QStringLiteral("Texto nuevo (vacío = borrar)."))},
               {QStringLiteral("replace_all"), boolProp(QStringLiteral("Reemplazar TODAS las apariciones. Default false."))}},
           QJsonArray{QStringLiteral("path"), QStringLiteral("old_string")}),
        fn(QStringLiteral("run_shell"),
           QStringLiteral("Ejecuta un comando de shell en el directorio del proyecto. "
                          "Para builds/tests largos pasá timeout_s alto (default 120, máx 1800). "
                          "Si el comando puede mutar archivos, declaralos en changed_paths; "
                          "las mutaciones opacas sin rutas son rechazadas por coordinación."),
           QJsonObject{
               {QStringLiteral("command"), strProp(QStringLiteral("Comando a ejecutar."))},
               {QStringLiteral("timeout_s"), intProp(QStringLiteral("Timeout en segundos (default 120, máx 1800)."))},
               {QStringLiteral("changed_paths"), QJsonObject{
                   {QStringLiteral("type"), QStringLiteral("array")},
                   {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                   {QStringLiteral("description"), QStringLiteral("Rutas relativas que el comando espera crear, modificar o borrar. Obligatorio para comandos mutantes.")}}}},
           QJsonArray{QStringLiteral("command")}),
        fn(QStringLiteral("web_fetch"),
           QStringLiteral("Obtiene una URL pública con pipeline direct → Playwright MCP → Camofox. "
                          "Sólo escala si detecta error, challenge, shell JS o contenido insuficiente; "
                          "devuelve proveedor, intentos y evidencia. Bloquea redes privadas y redirects "
                          "inseguros. 'provider' fuerza un proveedor para diagnóstico."),
           QJsonObject{
               {QStringLiteral("url"), strProp(QStringLiteral("URL completa (http:// o https://)."))},
               {QStringLiteral("provider"), QJsonObject{
                   {QStringLiteral("type"), QStringLiteral("string")},
                   {QStringLiteral("enum"), QJsonArray{QStringLiteral("auto"),
                                                        QStringLiteral("direct"),
                                                        QStringLiteral("playwright"),
                                                        QStringLiteral("camofox")}},
                   {QStringLiteral("description"), QStringLiteral("Proveedor (default auto).")}}}},
           QJsonArray{QStringLiteral("url")}),
        fn(QStringLiteral("web_search"),
           QStringLiteral("Busca en la web y devuelve los mejores resultados (título, URL, snippet). "
                          "Usalo para encontrar páginas/docs relevantes; después usá web_fetch sobre "
                          "las URLs que sirvan. Proveedor: DuckDuckGo (sin key) o SearXNG si está "
                          "configurado el env LLAMACODE_SEARXNG_URL."),
           QJsonObject{
               {QStringLiteral("query"), strProp(QStringLiteral("Términos de búsqueda."))},
               {QStringLiteral("count"), intProp(QStringLiteral("Cantidad de resultados (default 5, máx 10)."))}},
           QJsonArray{QStringLiteral("query")}),
        fn(QStringLiteral("deep_research"),
           QStringLiteral("Investigación web profunda: busca por varios ángulos, descarga las mejores "
                          "páginas y devuelve un DOSSIER (fuentes numeradas + contenido limpio) para que "
                          "VOS lo sintetices citando [n]. Usalo para preguntas que requieren cruzar varias "
                          "fuentes. Pasá 'angles' con sub-consultas distintas para mejor cobertura."),
           QJsonObject{
               {QStringLiteral("query"), strProp(QStringLiteral("Pregunta/tema a investigar."))},
               {QStringLiteral("angles"), QJsonObject{
                   {QStringLiteral("type"), QStringLiteral("array")},
                   {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                   {QStringLiteral("description"), QStringLiteral("Sub-consultas/ángulos distintos (opcional, máx 4).")}}},
               {QStringLiteral("max_pages"), intProp(QStringLiteral("Páginas a descargar (default 5, máx 10)."))}},
           QJsonArray{QStringLiteral("query")}),
        fn(QStringLiteral("search_docs"),
           QStringLiteral("Búsqueda semántica-lite en los archivos del proyecto: rankea fragmentos por "
                          "relevancia a la consulta (keywords) y devuelve los top-k con archivo:línea. "
                          "Mejor que grep para 'dónde se maneja X' cuando no sabés el término exacto. "
                          "Para recuperar contexto del repo."),
           QJsonObject{
               {QStringLiteral("query"), strProp(QStringLiteral("Qué buscás (lenguaje natural o keywords)."))},
               {QStringLiteral("k"), intProp(QStringLiteral("Cantidad de fragmentos (default 5, máx 15)."))},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio a acotar (opcional)."))}},
           QJsonArray{QStringLiteral("query")}),
        fn(QStringLiteral("semantic_search"),
           QStringLiteral("Búsqueda SEMÁNTICA en los archivos del proyecto usando embeddings del propio "
                          "server (cosine similarity, cache de vectores en SQLite). Encuentra código por "
                          "SIGNIFICADO, no por palabras exactas. Requiere un server con embeddings "
                          "(--embeddings). Si no, usá search_docs. args: query/k/path."),
           QJsonObject{
               {QStringLiteral("query"), strProp(QStringLiteral("Qué buscás (lenguaje natural)."))},
               {QStringLiteral("k"), intProp(QStringLiteral("Cantidad de fragmentos (default 5, máx 15)."))},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio a acotar (opcional)."))}},
           QJsonArray{QStringLiteral("query")}),
        fn(QStringLiteral("hybrid_search"),
           QStringLiteral("Búsqueda HÍBRIDA (la mejor): fusiona BM25 (keywords) + vectorial "
                          "(embeddings) por Reciprocal Rank Fusion y RE-RANKEA con el reranker del "
                          "server si está disponible. Preferila a search_docs/semantic_search para "
                          "recuperar contexto del repo. Cae a BM25 si no hay embeddings. Puede "
                          "empaquetar por presupuesto de tokens y expandir con vecinos del dep-graph "
                          "(archivos que el resultado importa/incluye). Con compact=true devuelve "
                          "sólo citas 'rel:Lini-Lfin' + preview de 1 línea (estilo FastContext): "
                          "explorás barato y leés después los spans con read_file. Con "
                          "include_docs=true suma los PDF/Office/EPUB del proyecto al mismo índice "
                          "(manuales, normativa, specs), no sólo código. mode=scout devuelve sólo "
                          "handles y recibo para hacer context_fetch después. args: query/k/path/"
                          "token_budget/expand_graph/compact/include_docs/mode."),
           QJsonObject{
               {QStringLiteral("query"), strProp(QStringLiteral("Qué buscás (lenguaje natural)."))},
               {QStringLiteral("k"), intProp(QStringLiteral("Cantidad de fragmentos (default 6, máx 15). Ignorado si hay token_budget."))},
               {QStringLiteral("token_budget"), intProp(QStringLiteral("Presupuesto aprox de tokens; llena hasta el límite en vez de k fijo (0=off)."))},
               {QStringLiteral("expand_graph"), boolProp(QStringLiteral("Listar archivos relacionados vía imports/includes. Default true."))},
               {QStringLiteral("mode"), strProp(QStringLiteral("scout = handles/previews/recibo sin cuerpos; omitido = búsqueda completa."))},
               {QStringLiteral("compact"), boolProp(QStringLiteral("Devolver sólo citas 'rel:Lini-Lfin' + preview, sin cuerpo. Ahorra tokens de exploración. Default false."))},
               {QStringLiteral("include_docs"), boolProp(QStringLiteral("Indexar también PDF/Office/EPUB/HTML vía extractor (máx 25 docs, lento la 1ª vez, después cacheado). Default false."))},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio a acotar (opcional)."))}},
           QJsonArray{QStringLiteral("query")}),
        fn(QStringLiteral("repo_slice"),
           QStringLiteral("Prepara evidencia compacta ANTES DE EDITAR: rankea el repo con "
                          "BM25+embeddings/RRF/reranker cuando están disponibles y devuelve "
                          "archivo:Lini-Lfin, preview y vecinos del dep-graph. Usala al comenzar "
                          "una tarea de código y después leé sólo los spans relevantes. Cae a "
                          "BM25 local sin server."),
           QJsonObject{
               {QStringLiteral("query"), strProp(QStringLiteral("Objetivo concreto de la tarea o bug."))},
               {QStringLiteral("k"), intProp(QStringLiteral("Resultados si token_budget=0 (default 6, máx 15)."))},
               {QStringLiteral("token_budget"), intProp(QStringLiteral("Presupuesto aproximado de tokens."))},
               {QStringLiteral("expand_graph"), boolProp(QStringLiteral("Incluir vecinos por imports/includes. Default true."))},
               {QStringLiteral("mode"), strProp(QStringLiteral("scout = handles/previews/recibo sin cuerpos; omitido = slice completo."))},
               {QStringLiteral("compact"), boolProp(QStringLiteral("Sólo citas+rango+preview. Default true."))},
               {QStringLiteral("include_docs"), boolProp(QStringLiteral("Sumar PDF/Office/EPUB/HTML al índice (máx 25). Default false."))},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio opcional."))}},
           QJsonArray{QStringLiteral("query")}),
        fn(QStringLiteral("verify_claims"),
           QStringLiteral("Anti-alucinación: por cada afirmación busca respaldo en los archivos del "
                          "proyecto y en la memoria, y la etiqueta [ACREDITADO]/[INFERIDO]/[NO "
                          "ACREDITADO] con su fuente. NO reescribe nada: usá el resultado para "
                          "redactar con cautela y marcar como hipótesis lo no acreditado. Ideal "
                          "para informes (p.ej. periciales) antes de afirmar hechos."),
           QJsonObject{
               {QStringLiteral("claims"), QJsonObject{
                   {QStringLiteral("type"), QStringLiteral("array")},
                   {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                   {QStringLiteral("description"), QStringLiteral("Afirmaciones a verificar (también acepta texto con una por línea).")}}},
               {QStringLiteral("path"), strProp(QStringLiteral("Subdirectorio a acotar (opcional)."))}},
           QJsonArray{QStringLiteral("claims")}),
        fn(QStringLiteral("context_checkpoint"),
           QStringLiteral("Cierra una fase terminada. Compacta sólo si ahorra >=30%; "
                          "nunca con acciones pendientes. Conserva el transcript."),
           QJsonObject{{QStringLiteral("summary"), strProp(
               QStringLiteral("JSON compacto: goal, completed[], decisions[], artifacts[], "
                              "open_questions[], next_action."))}},
           QJsonArray{QStringLiteral("summary")}),
        fn(QStringLiteral("memory"),
           QStringLiteral("Memoria PERSISTENTE por CAPAS (sobrevive entre sesiones). "
                          "action='save' guarda un hecho atómico con metadata y PROVENANCE; "
                          "action='recall' (default) lo recupera (rankeado por relevancia, "
                          "confianza y recencia; ignora hechos olvidados); action='forget' marca "
                          "hechos obsoletos. scope='session|project|personal', "
                          "type='preference|decision|fact|bug'. En recall/forget pasá 'query' para "
                          "matchear por keywords y/o 'scope' para filtrar la capa. Usala para "
                          "preferencias del usuario, decisiones de diseño y datos no obvios del repo; "
                          "'forget' cuando un hecho quedó desactualizado (memoria stale es peor que nada). "
                          "action='prune' poda anti-bloat: evicta los hechos de menor valor "
                          "(confianza·recencia·tipo vs largo) y los casi-duplicados, dejando hasta "
                          "'max_keep'. Usalo cuando la memoria crezca demasiado; con dry_run=true "
                          "primero para ver qué se iría. action='decay' marca stale de forma "
                          "automática los hechos antiguos y de bajo valor, protegiendo decisiones "
                          "verificadas, importantes o reutilizadas."),
           QJsonObject{
               {QStringLiteral("action"), strProp(QStringLiteral("'save' | 'recall' (default) | 'forget' | 'prune' | 'decay'."))},
               {QStringLiteral("content"), strProp(QStringLiteral("Hecho a guardar (sólo action='save')."))},
               {QStringLiteral("scope"), strProp(QStringLiteral("Capa: 'session'|'project'|'personal' (default 'project')."))},
               {QStringLiteral("type"), strProp(QStringLiteral("'preference'|'decision'|'fact'|'bug' (sólo save)."))},
               {QStringLiteral("query"), strProp(QStringLiteral("Keywords: rankea en recall, selecciona en forget."))},
               {QStringLiteral("source"), strProp(QStringLiteral("Provenance: de dónde salió el hecho (ej. 'user', archivo). Sólo save."))},
               {QStringLiteral("mode"), strProp(QStringLiteral("forget/prune: 'stale' (default, conserva historial) o 'delete'."))},
               {QStringLiteral("confidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                   {QStringLiteral("description"), QStringLiteral("Confianza 0..1 (sólo save, default 0.8).")}}},
               {QStringLiteral("importance"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                   {QStringLiteral("description"), QStringLiteral("Importancia durable 0..1; alta para reglas y decisiones críticas.")}}},
               {QStringLiteral("surprise"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                   {QStringLiteral("description"), QStringLiteral("Sorpresa 0..1; alta si corrige o contradice una suposición previa.")}}},
               {QStringLiteral("verification"), strProp(QStringLiteral("Origen de verificación: 'user'|'test'|'tool'|'inferred'."))},
               {QStringLiteral("supersedes"), strProp(QStringLiteral("Id del recuerdo anterior que este hecho reemplaza."))},
               {QStringLiteral("max_keep"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                   {QStringLiteral("description"), QStringLiteral("prune: máximo de hechos a conservar por capa (default 50).")}}},
               {QStringLiteral("dry_run"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                   {QStringLiteral("description"), QStringLiteral("prune/decay: si true, sólo reporta qué cambiaría sin tocar nada.")}}},
               {QStringLiteral("max_age_days"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                   {QStringLiteral("minimum"), 30}, {QStringLiteral("maximum"), 3650},
                   {QStringLiteral("description"), QStringLiteral("decay: edad mínima para considerar un hecho (default 90 días).")}}},
               {QStringLiteral("min_value"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                   {QStringLiteral("minimum"), 0.05}, {QStringLiteral("maximum"), 0.75},
                   {QStringLiteral("description"), QStringLiteral("decay: valor máximo para marcar stale (default 0.28).")}}}},
           QJsonArray{}),
        fn(QStringLiteral("graph"),
           QStringLiteral("KNOWLEDGE GRAPH del proyecto (entidades + relaciones tipadas, persiste "
                          "entre sesiones). Complementa 'memory': ahí van hechos sueltos, acá CÓMO se "
                          "conectan. action='link' (default) conecta subj-[pred]->obj (auto-crea las "
                          "entidades); 'add_entity' crea una entidad con etype; 'query' devuelve el "
                          "vecindario de una entidad (depth=1 directos, 2 incluye vecinos de vecinos). "
                          "etype='file|module|decision|bug|person|concept'. Usalo para mapear qué "
                          "módulo depende de cuál, qué decisión causó qué bug, quién pidió qué. "
                          "action='decide' registra una decisión (topic+chosen+reason) conservando las "
                          "alternativas RECHAZADAS con su motivo (audit trail, no se borran); "
                          "'decisions' devuelve ese log (topic vacío = todas). 'doctor' devuelve "
                          "salud, edges huérfanos y fuentes obsoletas. Registrá las decisiones "
                          "de diseño no triviales con sus rechazos para no re-evaluarlas después. "
                          "action='index' SIEMBRA el grafo automáticamente: recorre el repo y extrae "
                          "símbolos (clases/funciones) e imports/includes como relaciones "
                          "file-[defines]->símbolo y file-[imports]->file (determinista, sin LLM). "
                          "Corré 'index' UNA vez en un repo grande sin mapear y después usá 'query' "
                          "para navegar en vez de re-leer archivos (contexto barato). Para mantener "
                          "el mapa vivo sin re-escanear todo: 'index' con incremental=true reindexa "
                          "sólo lo cambiado desde la última pasada (git/mtime, borra edges viejos); o "
                          "pasá 'files' (lista) para refrescar archivos puntuales tras editarlos."),
           QJsonObject{
               {QStringLiteral("action"), strProp(QStringLiteral("'link' (default) | 'add_entity' | 'query' | 'verify' | 'decide' | 'decisions' | 'doctor' | 'index'."))},
               {QStringLiteral("langs"), strProp(QStringLiteral("index: lenguajes a indexar (CSV, ej. 'cpp,qml'). Vacío = cpp/qml/js/ts/py."))},
               {QStringLiteral("incremental"), boolProp(QStringLiteral("index: reindexar sólo lo cambiado desde la última pasada (default false = todo)."))},
               {QStringLiteral("files"), strProp(QStringLiteral("index: lista de rutas a reindexar puntualmente (CSV). Ignora incremental."))},
               {QStringLiteral("subj"), strProp(QStringLiteral("Entidad origen (sólo link)."))},
               {QStringLiteral("pred"), strProp(QStringLiteral("Predicado/relación, ej. 'depende_de' (sólo link)."))},
               {QStringLiteral("obj"), strProp(QStringLiteral("Entidad destino (sólo link)."))},
               {QStringLiteral("edge_type"), strProp(QStringLiteral("link: tipo de arista (REQUIRES|ENABLES|IMPLEMENTS|DEFINES|CALLS|IMPORTS|RELATES_TO). Vacío = se infiere del pred. Usá REQUIRES para dependencia dura, RELATES_TO para asociación blanda."))},
               {QStringLiteral("confidence"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                   {QStringLiteral("description"), QStringLiteral("confianza [0,1] del edge (link y verify). En link: omitir si no estás seguro → 'unreviewed'. En verify: sube la confianza del edge (default 1.0).")}}},
               {QStringLiteral("drop"), boolProp(QStringLiteral("verify: true tacha (borra) ese edge puntual en vez de revisarlo (edge equivocado). Ubica el edge por subj/pred/obj."))},
               {QStringLiteral("name"), strProp(QStringLiteral("Nombre de entidad (add_entity y query)."))},
               {QStringLiteral("etype"), strProp(QStringLiteral("Tipo de entidad (sólo add_entity)."))},
               {QStringLiteral("depth"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                   {QStringLiteral("description"), QStringLiteral("Saltos en query: 1 (default) a 3.")}}},
               {QStringLiteral("format"), strProp(QStringLiteral("query: 'markdown' (default) o 'packet' para devolver nodos, edges, fuentes y recibo JSON."))},
               {QStringLiteral("topic"), strProp(QStringLiteral("Tema de la decisión (decide; filtro substring en decisions)."))},
               {QStringLiteral("chosen"), strProp(QStringLiteral("Opción elegida (sólo decide)."))},
               {QStringLiteral("reason"), strProp(QStringLiteral("Motivo de la elección (sólo decide)."))},
               {QStringLiteral("rejected"), QJsonObject{
                   {QStringLiteral("type"), QStringLiteral("array")},
                   {QStringLiteral("description"), QStringLiteral("Alternativas rechazadas (sólo decide). Array de objetos {alt, reason} o de strings.")},
                   {QStringLiteral("items"), QJsonObject{
                       {QStringLiteral("type"), QStringLiteral("object")},
                       {QStringLiteral("properties"), QJsonObject{
                           {QStringLiteral("alt"), strProp(QStringLiteral("La alternativa descartada."))},
                           {QStringLiteral("reason"), strProp(QStringLiteral("Por qué se descartó."))}
                       }}
                   }}
               }}
           },
           QJsonArray{}),
        fn(QStringLiteral("ask_teacher"),
           QStringLiteral("Consultá a un modelo MÁS capaz (endpoint aparte) una sub-pregunta difícil: "
                          "diseño, algoritmo tricky, bug que no resolvés. Devuelve su respuesta. "
                          "Requiere la env LLAMACODE_TEACHER_URL configurada. Usalo con criterio "
                          "(es más lento/caro): pasá 'context' con lo mínimo necesario."),
           QJsonObject{
               {QStringLiteral("question"), strProp(QStringLiteral("La pregunta concreta para el modelo maestro."))},
               {QStringLiteral("context"), strProp(QStringLiteral("Contexto relevante (código, error). Opcional."))}},
           QJsonArray{QStringLiteral("question")}),
        fn(QStringLiteral("task"),
           QStringLiteral("Delega una SUBTAREA independiente a un sub-agente autónomo que trabaja en "
                          "una copia aislada del proyecto (git worktree). Devuelve un resumen de lo que "
                          "hizo y sus cambios se mergean al terminar. Podés invocar VARIAS task en el "
                          "mismo turno para correrlas EN PARALELO. Usalo para subtareas separables "
                          "(ej. 'implementá X en módulo A' y 'agregá tests a B'). El prompt debe ser "
                          "autocontenido: el sub-agente NO ve esta conversación."),
           QJsonObject{
               {QStringLiteral("description"), strProp(QStringLiteral("Título corto de la subtarea (para la tarjeta)."))},
               {QStringLiteral("prompt"), strProp(QStringLiteral("Instrucción completa y autocontenida para el sub-agente."))}},
           QJsonArray{QStringLiteral("prompt")}),
        fn(QStringLiteral("browser_skill_list"),
           QStringLiteral("Lista los SKILLS de browser grabados por el usuario (modo teach: "
                          "secuencias de navegación grabadas, reproducibles). Usalo antes de "
                          "browser_skill_replay para saber qué hay disponible."),
           QJsonObject{},
           QJsonArray{}),
        fn(QStringLiteral("skill_list"),
           QStringLiteral("Lista habilidades portables disponibles para este proyecto. Devuelve "
                          "sólo nombre, alcance y descripción; usá skill_load para cargar las "
                          "instrucciones de una habilidad relevante."),
           QJsonObject{},
           QJsonArray{}),
        fn(QStringLiteral("skill_load"),
           QStringLiteral("Carga bajo demanda las instrucciones completas de una habilidad "
                          "portable. Sus instrucciones orientan el trabajo pero nunca amplían "
                          "los permisos de las tools."),
           QJsonObject{
               {QStringLiteral("name"), strProp(QStringLiteral("Nombre kebab-case de la habilidad."))}},
           QJsonArray{QStringLiteral("name")}),
        fn(QStringLiteral("browser_skill_replay"),
           QStringLiteral("Reproduce un SKILL de browser grabado (Playwright). Ejecuta la "
                          "secuencia que el usuario grabó (login, navegación, formulario...) y "
                          "devuelve su salida. Para tareas web repetibles ya enseñadas; para "
                          "navegación nueva/ad-hoc usá las tools del MCP Playwright."),
           QJsonObject{
               {QStringLiteral("name"), strProp(QStringLiteral("Nombre del skill (ver browser_skill_list)."))}},
           QJsonArray{QStringLiteral("name")}),
        fn(QStringLiteral("browser_network_discover"),
           QStringLiteral("Inspecciona los requests observados por el browser Playwright "
                          "activo y devuelve evidencia agrupada por método, origen y path. "
                          "No conserva query strings, headers, cookies ni bodies. Usalo "
                          "después de realizar una acción autorizada para descubrir qué "
                          "contrato web produjo el cambio, antes de concluir que no hay API."),
           QJsonObject{
               {QStringLiteral("include_static"), boolProp(
                    QStringLiteral("Incluye assets CSS/JS/imágenes; default false."))},
               {QStringLiteral("artifact_id"), strProp(
                    QStringLiteral("Artefacto Teach donde guardar el descubrimiento; opcional."))},
               {QStringLiteral("action"), strProp(
                    QStringLiteral("Acción visible que precedió a estos requests; opcional."))}},
           QJsonArray{}),
        fn(QStringLiteral("recent_actions"),
           QStringLiteral("Relee tu propio rastro reciente (tool_calls, resultados y FALLOS de "
                          "esta sesión) para auto-corregirte: ver qué intentaste, qué se repitió "
                          "sin progreso y qué falló, antes de reintentar. Útil cuando algo no "
                          "avanza o perdés el hilo en una tarea larga."),
           QJsonObject{
               {QStringLiteral("count"), intProp(QStringLiteral("Cuántos eventos traer (default 20, máx 200)."))}},
           QJsonArray{}),
        fn(QStringLiteral("desktop_snapshot"),
           QStringLiteral("Obtiene un snapshot compacto y determinista del alcance: ventanas, "
                          "controles UIA, automationId, patrones, estados y fingerprint. "
                          "Usá el snapshot_id devuelto al actuar; si quedó stale, la acción "
                          "se rechaza y debés observar de nuevo. No captura píxeles."),
           QJsonObject{
               {QStringLiteral("scope_kind"), strProp(QStringLiteral("'screen' o 'window'."))},
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id de pantalla o ventana."))},
               {QStringLiteral("query"), strProp(QStringLiteral("Filtro opcional de controles."))},
                {QStringLiteral("max"), intProp(QStringLiteral("Máximo de controles; default 120."))}},
           QJsonArray{QStringLiteral("target_id")}),
        fn(QStringLiteral("desktop_windows"),
           QStringLiteral("Inventario ESTRUCTURADO de ventanas visibles (id, título, pid, "
                          "geometría) — estado barato para orientarte y elegir un objetivo SIN "
                          "gastar una captura. Usá el id devuelto con scope_kind='window' en "
                          "desktop_observe/desktop_click. La captura (desktop_observe) queda como "
                          "fallback cuando necesitás VER el contenido."),
           QJsonObject{},
           QJsonArray{}),
        fn(QStringLiteral("desktop_controls"),
           QStringLiteral("Árbol de CONTROLES de una ventana vía UI Automation (DOM del "
                          "escritorio): nombre, rol, geometría e invocable de cada control. "
                          "Elegí el control por NOMBRE y operalo con desktop_click_element — "
                          "más robusto que clickear por pixel. target_id = id de ventana "
                          "(desktop_windows). 'query' filtra por substring del nombre. "
                          "También sirve para VERIFICAR sin visión: el texto de los controles "
                          "(ej. el visor de la calculadora) trae el valor en su nombre."),
           QJsonObject{
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id de la ventana (ver desktop_windows)."))},
               {QStringLiteral("query"), strProp(QStringLiteral("Filtro por nombre (substring, opcional)."))},
               {QStringLiteral("max"), intProp(QStringLiteral("Máximo de controles (default 120)."))}},
           QJsonArray{QStringLiteral("target_id")}),
        fn(QStringLiteral("desktop_click_element"),
           QStringLiteral("Clickea un control por su controlId (de desktop_controls): usa el "
                          "patrón Invoke si existe (clic semántico), si no clickea el centro "
                          "del control. Pasá el mismo target_id de la ventana."),
           QJsonObject{
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id de la ventana."))},
               {QStringLiteral("snapshot_id"), strProp(QStringLiteral("Fingerprint de desktop_snapshot/desktop_controls; opcional pero recomendado."))},
               {QStringLiteral("control_id"), strProp(QStringLiteral(
                    "controlId devuelto por desktop_controls. Si no lo tenés a mano podés "
                    "pasar el nombre visible del control y se resuelve por parecido, pero "
                    "el controlId es exacto: preferilo siempre."))}},
           QJsonArray{QStringLiteral("target_id"), QStringLiteral("control_id")} ),
        fn(QStringLiteral("desktop_control_action"),
           QStringLiteral("Ejecuta una acción semántica UI Automation sobre un control del "
                          "snapshot sin robar foco cuando el patrón está disponible. Acciones: "
                          "invoke, toggle, set_value, select, expand, collapse, range_set, "
                          "scroll_into_view, read. Usá snapshot_id para evitar actuar sobre "
                          "un árbol obsoleto."),
           QJsonObject{
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id de la ventana."))},
               {QStringLiteral("control_id"), strProp(QStringLiteral("controlId del snapshot."))},
               {QStringLiteral("snapshot_id"), strProp(QStringLiteral("Fingerprint del snapshot."))},
               {QStringLiteral("action"), strProp(QStringLiteral("invoke|toggle|set_value|select|expand|collapse|range_set|scroll_into_view|read"))},
               {QStringLiteral("value"), strProp(QStringLiteral("Valor para set_value o range_set."))},
               {QStringLiteral("sensitive"), boolProp(QStringLiteral("Marca explícita de campo sensible."))}},
           QJsonArray{QStringLiteral("target_id"), QStringLiteral("control_id"), QStringLiteral("action")} ),
        fn(QStringLiteral("desktop_click_text"),
           QStringLiteral("ÚLTIMO RECURSO: clickea un texto LEÍDO de la pantalla por OCR. "
                          "Usalo SÓLO si desktop_controls no devuelve el control (apps que "
                          "no exponen su árbol: canvas, juegos, algunas Electron). Es a "
                          "ciegas: no sabe si lo que lee es un botón. Si desktop_controls "
                          "lista el control, usá desktop_click_element — es exacto."),
           QJsonObject{
               {QStringLiteral("scope_kind"), strProp(QStringLiteral("'screen' o 'window'."))},
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id de la pantalla o ventana."))},
               {QStringLiteral("snapshot_id"), strProp(QStringLiteral("Fingerprint del snapshot opcional."))},
               {QStringLiteral("text"), strProp(QStringLiteral("Texto visible a clickear."))}},
           QJsonArray{QStringLiteral("target_id"), QStringLiteral("text")}),
        fn(QStringLiteral("desktop_find_image"),
           QStringLiteral("Busca una plantilla visual dentro de una pantalla o ventana sin "
                          "hacer click. Fallback para canvas, iconos y escritorios remotos "
                          "cuando UI Automation y OCR no exponen el objetivo. Devuelve rect "
                          "normalizado, escala, confianza y ambigüedad."),
           QJsonObject{
               {QStringLiteral("scope_kind"), strProp(QStringLiteral("'screen' o 'window'."))},
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id del alcance."))},
               {QStringLiteral("snapshot_id"), strProp(QStringLiteral("Fingerprint del snapshot opcional."))},
               {QStringLiteral("template_path"), strProp(QStringLiteral("Ruta de la imagen de referencia."))},
               {QStringLiteral("threshold"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                          {QStringLiteral("minimum"), 0.5},
                                                          {QStringLiteral("maximum"), 1.0}}},
               {QStringLiteral("min_scale"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                          {QStringLiteral("minimum"), 0.5},
                                                          {QStringLiteral("maximum"), 2.0}}},
               {QStringLiteral("max_scale"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                          {QStringLiteral("minimum"), 0.5},
                                                          {QStringLiteral("maximum"), 2.0}}},
               {QStringLiteral("require_unique"), boolProp(QStringLiteral("Rechazar alternativas equivalentes; default true."))}},
           QJsonArray{QStringLiteral("target_id"), QStringLiteral("template_path")}),
        fn(QStringLiteral("desktop_click_image"),
           QStringLiteral("Localiza una plantilla visual y clickea su centro sólo si hay una "
                          "coincidencia segura y única. Se abstiene si la ventana cambia entre "
                          "captura y acción. Preferí antes UIA y OCR."),
           QJsonObject{
               {QStringLiteral("scope_kind"), strProp(QStringLiteral("'screen' o 'window'."))},
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id del alcance."))},
               {QStringLiteral("snapshot_id"), strProp(QStringLiteral("Fingerprint del snapshot opcional."))},
               {QStringLiteral("template_path"), strProp(QStringLiteral("Ruta de la imagen de referencia."))},
               {QStringLiteral("threshold"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                          {QStringLiteral("minimum"), 0.5},
                                                          {QStringLiteral("maximum"), 1.0}}},
               {QStringLiteral("min_scale"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                          {QStringLiteral("minimum"), 0.5},
                                                          {QStringLiteral("maximum"), 2.0}}},
               {QStringLiteral("max_scale"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                          {QStringLiteral("minimum"), 0.5},
                                                          {QStringLiteral("maximum"), 2.0}}},
               {QStringLiteral("button"), QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("enum"), QJsonArray{QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("middle")}}}}},
           QJsonArray{QStringLiteral("target_id"), QStringLiteral("template_path")}),
        fn(QStringLiteral("desktop_wait_image"),
           QStringLiteral("Espera de forma acotada a que una plantilla visual aparezca o desaparezca."),
           QJsonObject{
               {QStringLiteral("scope_kind"), strProp(QStringLiteral("'screen' o 'window'."))},
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id del alcance."))},
               {QStringLiteral("template_path"), strProp(QStringLiteral("Ruta de la plantilla."))},
               {QStringLiteral("appear"), boolProp(QStringLiteral("true=esperar aparición; false=desaparición."))},
               {QStringLiteral("timeout_ms"), intProp(QStringLiteral("Timeout; default 4000, máximo 60000."))},
               {QStringLiteral("threshold"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                          {QStringLiteral("minimum"), 0.5},
                                                          {QStringLiteral("maximum"), 1.0}}},
               {QStringLiteral("min_scale"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
               {QStringLiteral("max_scale"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}}},
           QJsonArray{QStringLiteral("target_id"), QStringLiteral("template_path")}),
        fn(QStringLiteral("desktop_assert_image"),
           QStringLiteral("Verifica sin modificar el escritorio que una plantilla exista o esté ausente."),
           QJsonObject{
               {QStringLiteral("scope_kind"), strProp(QStringLiteral("'screen' o 'window'."))},
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id del alcance."))},
               {QStringLiteral("template_path"), strProp(QStringLiteral("Ruta de la plantilla."))},
               {QStringLiteral("should_exist"), boolProp(QStringLiteral("Condición esperada; default true."))},
               {QStringLiteral("timeout_ms"), intProp(QStringLiteral("Timeout corto; default 1500."))},
               {QStringLiteral("threshold"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                          {QStringLiteral("minimum"), 0.5},
                                                          {QStringLiteral("maximum"), 1.0}}},
               {QStringLiteral("min_scale"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
               {QStringLiteral("max_scale"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}}},
           QJsonArray{QStringLiteral("target_id"), QStringLiteral("template_path")}),
        fn(QStringLiteral("desktop_observe"),
           QStringLiteral("Captura el alcance actual como evidencia visual. Usala cuando UIA/OCR "
                          "no alcancen, antes de un clic por coordenadas y una vez después para "
                          "verificar el cambio. No afirmes que un control existe si no se ve con "
                          "claridad; ante ausencia o ambigüedad, abstenete. Devuelve además "
                          "snapshot_id, fingerprint y controles UIA para poder actuar con stale guard."),
           QJsonObject{
               {QStringLiteral("scope_kind"), strProp(QStringLiteral("'screen' o 'window'."))},
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id del alcance guardado en la receta."))}},
           QJsonArray{QStringLiteral("target_id")}),
        fn(QStringLiteral("desktop_click"),
           QStringLiteral("Hace click en coordenadas NORMALIZADAS 0..1 dentro del alcance. "
                          "NO acepta la grilla visual 0..1000: dividí ambas coordenadas por 1000. "
                          "Acepta button='left'|'right'|'middle' y devuelve trace con "
                          "pointer/target. Observá primero, no uses coordenadas como replay ciego "
                          "y verificá el resultado después."),
            QJsonObject{
                {QStringLiteral("scope_kind"), strProp(QStringLiteral("'screen' o 'window'."))},
                {QStringLiteral("target_id"), strProp(QStringLiteral("Id del alcance."))},
                {QStringLiteral("snapshot_id"), strProp(QStringLiteral("Fingerprint del snapshot opcional."))},
                {QStringLiteral("x"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                    {QStringLiteral("minimum"), 0.0},
                                                    {QStringLiteral("maximum"), 1.0}}},
                {QStringLiteral("y"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                    {QStringLiteral("minimum"), 0.0},
                                                    {QStringLiteral("maximum"), 1.0}}},
                {QStringLiteral("button"), QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("enum"), QJsonArray{QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("middle")}},
                    {QStringLiteral("description"), QStringLiteral("Botón de mouse; default left.")}}}},
            QJsonArray{QStringLiteral("target_id"), QStringLiteral("x"), QStringLiteral("y")}),
        fn(QStringLiteral("desktop_stroke"),
           QStringLiteral("Arrastra el mouse por una TRAZA continua (dibujar en Paint, pintar, "
                          "sliders, gestos/swipes): aprieta el botón en el primer punto, recorre "
                          "la secuencia y suelta en el último. Puntos NORMALIZADOS 0..1 en el "
                          "alcance. Interpola para que la línea salga continua. Usá esto en vez "
                          "de varios desktop_click cuando el usuario dibujó o arrastró."),
            QJsonObject{
                {QStringLiteral("scope_kind"), strProp(QStringLiteral("'screen' o 'window'."))},
                {QStringLiteral("target_id"), strProp(QStringLiteral("Id del alcance."))},
                {QStringLiteral("snapshot_id"), strProp(QStringLiteral("Fingerprint del snapshot opcional."))},
                {QStringLiteral("points"), QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("array")},
                    {QStringLiteral("description"), QStringLiteral("Lista de {x,y} normalizados 0..1 (mínimo 2).")},
                    {QStringLiteral("items"), QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("object")},
                        {QStringLiteral("properties"), QJsonObject{
                            {QStringLiteral("x"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                                {QStringLiteral("minimum"), 0.0},
                                                                {QStringLiteral("maximum"), 1.0}}},
                            {QStringLiteral("y"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                                {QStringLiteral("minimum"), 0.0},
                                                                {QStringLiteral("maximum"), 1.0}}}}}}}}},
                {QStringLiteral("button"), QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("enum"), QJsonArray{QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("middle")}},
                    {QStringLiteral("description"), QStringLiteral("Botón; default left.")}}},
                {QStringLiteral("hold_ms"), intProp(QStringLiteral("Pausa por segmento en ms (default 8)."))}},
            QJsonArray{QStringLiteral("target_id"), QStringLiteral("points")}),
        fn(QStringLiteral("desktop_type"),
           QStringLiteral("Escribe texto en la VENTANA EN FOCO del escritorio (enfocá antes con "
                          "desktop_focus). Camino RÁPIDO para apps con teclado: calculadora "
                          "(ej. desktop_type \"2+2\" y luego desktop_key \"=\"), notepad, campos "
                          "de texto. Más rápido y confiable que clickear botones uno a uno."),
           QJsonObject{{QStringLiteral("text"), strProp(QStringLiteral("Texto a escribir."))}},
           QJsonArray{QStringLiteral("text")}),
        fn(QStringLiteral("desktop_key"),
           QStringLiteral("Presiona una tecla o combinación en la ventana en foco (ENTER, TAB, "
                          "'=', etc.). Complementa desktop_type para confirmar/ejecutar."),
           QJsonObject{
               {QStringLiteral("key"), strProp(QStringLiteral("ENTER, TAB, ESC, F1..F12 o carácter."))},
               {QStringLiteral("modifiers"), QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("array")},
                    {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}}},
           QJsonArray{QStringLiteral("key")}),
        fn(QStringLiteral("desktop_scroll"),
           QStringLiteral("Desplaza la rueda del mouse; -120 baja, 120 sube."),
           QJsonObject{{QStringLiteral("delta"), intProp(QStringLiteral("Delta de rueda."))}},
           QJsonArray{}),
        fn(QStringLiteral("desktop_launch"),
           QStringLiteral("Abre una app del escritorio DESPRENDIDA (no bloquea). USÁ ESTO para "
                          "abrir programas (calc, notepad, una ruta .exe, un verbo del shell como "
                          "ms-settings:) en vez de run_shell: run_shell se cuelga esperando que una "
                          "app GUI termine. Tras lanzar, esperá (desktop_wait) y verificá con "
                          "desktop_windows/desktop_observe antes de escribir (desktop_type) o "
                          "clickear. 'args' es opcional (argumentos extra)."),
           QJsonObject{
               {QStringLiteral("app"), strProp(QStringLiteral("Programa/comando a abrir (ej. 'calc', 'notepad', ruta .exe)."))},
               {QStringLiteral("args"), strProp(QStringLiteral("Argumentos extra (opcional)."))}},
           QJsonArray{QStringLiteral("app")}),
        fn(QStringLiteral("desktop_focus"),
           QStringLiteral("Enfoca/restaura una ventana enseñada."),
           QJsonObject{{QStringLiteral("target_id"), strProp(QStringLiteral("Id de ventana."))}},
           QJsonArray{QStringLiteral("target_id")}),
        fn(QStringLiteral("desktop_resize"),
           QStringLiteral("Restaura el tamaño exterior enseñado de una ventana sin moverla. "
                          "Usala cuando la receta indique ancho/alto y la ventana actual difiera."),
           QJsonObject{
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id de ventana."))},
               {QStringLiteral("width"), intProp(QStringLiteral("Ancho exterior enseñado en píxeles."))},
               {QStringLiteral("height"), intProp(QStringLiteral("Alto exterior enseñado en píxeles."))}},
           QJsonArray{QStringLiteral("target_id"), QStringLiteral("width"), QStringLiteral("height")}),
        fn(QStringLiteral("desktop_wait"),
           QStringLiteral("Espera brevemente antes de volver a observar."),
           QJsonObject{{QStringLiteral("ms"), intProp(QStringLiteral("Milisegundos, máximo 10000."))}},
           QJsonArray{}),
        fn(QStringLiteral("desktop_wait_for"),
           QStringLiteral("Espera (poll) hasta que aparezca una CONDICIÓN, sin dormir un tiempo "
                          "fijo. PREFERILO sobre desktop_wait: sincroniza el replay ante latencia "
                          "de la UI. Casos: (a) 'window_title' → espera a que exista una ventana "
                          "con ese título; (b) 'target_id' + 'query'/'role' → espera a que aparezca "
                          "un control (nombre contiene query, rol coincide) en esa ventana. "
                          "Devuelve found + datos del match (usá su windowId/controlId después)."),
           QJsonObject{
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id de ventana donde esperar el control (opcional)."))},
               {QStringLiteral("window_title"), strProp(QStringLiteral("Substring del título de ventana a esperar (opcional)."))},
               {QStringLiteral("query"), strProp(QStringLiteral("Substring del nombre del control a esperar (opcional)."))},
               {QStringLiteral("role"), strProp(QStringLiteral("Rol del control: button/edit/text/... (opcional)."))},
               {QStringLiteral("timeout_ms"), intProp(QStringLiteral("Máximo a esperar (default 8000, tope 60000)."))}},
           QJsonArray{}),
        fn(QStringLiteral("desktop_assert"),
           QStringLiteral("VERIFICA una condición comprobable y devuelve PASS/FAIL (no una "
                          "opinión). Usalo al final de una Task o tras un paso clave para "
                          "confirmar el objetivo. Casos: 'expect_text' → pasa si ese texto "
                          "aparece en algún control (de la ventana 'target_id'/'window_title', o "
                          "de cualquiera si no se indica); o 'query'/'role'/'window_title' → pasa "
                          "si existe esa ventana/control. Espera hasta 'timeout_ms'."),
           QJsonObject{
               {QStringLiteral("expect_text"), strProp(QStringLiteral("Texto que debe estar presente (opcional)."))},
               {QStringLiteral("target_id"), strProp(QStringLiteral("Id de ventana donde verificar (opcional)."))},
               {QStringLiteral("window_title"), strProp(QStringLiteral("Substring del título de ventana (opcional)."))},
               {QStringLiteral("query"), strProp(QStringLiteral("Substring del nombre de control a existir (opcional)."))},
               {QStringLiteral("role"), strProp(QStringLiteral("Rol del control esperado (opcional)."))},
               {QStringLiteral("timeout_ms"), intProp(QStringLiteral("Máximo a esperar (default 4000, tope 60000)."))}},
           QJsonArray{}),
        fn(QStringLiteral("email_accounts"),
           QStringLiteral("Lista las cuentas de correo configuradas por el usuario (nombre + "
                          "dirección, sin contraseñas). Usalo para saber qué cuentas hay antes "
                          "de email_send/email_list si hay varias."),
           QJsonObject{},
           QJsonArray{}),
        fn(QStringLiteral("email_send"),
           QStringLiteral("Envía un correo (SMTP). Por defecto REQUIERE aprobación del usuario "
                          "(es una acción externa irreversible). Pasá destinatario(s), asunto y "
                          "cuerpo (texto plano). 'account' es opcional: si no, usa la cuenta por "
                          "defecto. Múltiples destinatarios separados por coma."),
           QJsonObject{
               {QStringLiteral("to"), strProp(QStringLiteral("Destinatario(s), separados por coma."))},
               {QStringLiteral("subject"), strProp(QStringLiteral("Asunto del correo."))},
               {QStringLiteral("body"), strProp(QStringLiteral("Cuerpo del mensaje (texto plano)."))},
               {QStringLiteral("cc"), strProp(QStringLiteral("Copia (CC), opcional."))},
               {QStringLiteral("account"), strProp(QStringLiteral("Nombre/email de la cuenta (opcional)."))}},
           QJsonArray{QStringLiteral("to"), QStringLiteral("subject"), QStringLiteral("body")}),
        fn(QStringLiteral("email_list"),
           QStringLiteral("Lista los correos recientes de la bandeja (IMAP/POP3): devuelve "
                          "uid, fecha, remitente y asunto de cada uno. Usá el uid con email_read "
                          "para leer el cuerpo. 'account' opcional (default: primera cuenta)."),
           QJsonObject{
               {QStringLiteral("folder"), strProp(QStringLiteral("Carpeta IMAP (default 'INBOX')."))},
               {QStringLiteral("limit"), intProp(QStringLiteral("Cantidad de correos (default 10)."))},
               {QStringLiteral("unread_only"), boolProp(QStringLiteral("Solo no leídos (IMAP). Default false."))},
               {QStringLiteral("account"), strProp(QStringLiteral("Nombre/email de la cuenta (opcional)."))}},
           QJsonArray{}),
        fn(QStringLiteral("email_read"),
           QStringLiteral("Lee el cuerpo completo de un correo por su uid (obtenido con "
                          "email_list). 'account'/'folder' opcionales."),
           QJsonObject{
               {QStringLiteral("uid"), strProp(QStringLiteral("UID del correo (ver email_list)."))},
               {QStringLiteral("folder"), strProp(QStringLiteral("Carpeta IMAP (default 'INBOX')."))},
               {QStringLiteral("account"), strProp(QStringLiteral("Nombre/email de la cuenta (opcional)."))}},
           QJsonArray{QStringLiteral("uid")})
    };
}

// Metadata de las tools built-in para la UI de habilitar/deshabilitar. approxTokens
// = costo aproximado del schema en el prompt (para estimar ahorro de contexto).
QVariantList LlamaAgentBackend::toolCatalog()
{
    // fromUtf8, NO fromLatin1: los literales del catalogo llevan acentos
    // ("Busqueda", "Revision", "Codigo", "Coordinacion") y el .cpp se compila con
    // /utf-8. Decodificarlos como Latin-1 los partia en mojibake ("BAusqueda"),
    // visible en los grupos de la UI de tools y en los packs del harness.
    auto mk = [](const char *name, const char *group, const char *desc, int tok) {
        return QVariantMap{
            {QStringLiteral("name"), QString::fromUtf8(name)},
            {QStringLiteral("group"), QString::fromUtf8(group)},
            {QStringLiteral("description"), QString::fromUtf8(desc)},
            {QStringLiteral("approxTokens"), tok}};
    };
    return QVariantList{
        mk("read_file", "Archivos", "Lee un archivo de texto (offset/limit).", 90),
        mk("list_dir",  "Archivos", "Lista archivos y carpetas.", 80),
        mk("project_brain", "Conocimiento", "Índice persistente de estructura y metadata del proyecto.", 95),
        mk("context_status", "Búsqueda", "Estado y frescura del índice local de contexto.", 65),
        mk("work_status", "Coordinación", "Trabajo activo y rutas reclamadas por otras sesiones.", 80),
        mk("glob",      "Archivos", "Lista archivos por patrón glob.", 110),
        mk("grep",      "Búsqueda", "Busca una regex en el proyecto.", 100),
        mk("code_hotspots", "Búsqueda", "Archivos riesgosos: churn git + autores + sin test (score 1-10).", 140),
        mk("review_overengineering", "Revisión", "Auditoría read-only del diff con delete-list explicable.", 150),
        mk("search_docs", "Búsqueda", "Ranking de fragmentos por keywords (semántica-lite).", 120),
        mk("semantic_search", "Búsqueda", "Búsqueda por significado vía embeddings del server.", 130),
        mk("hybrid_search", "Búsqueda", "Híbrida BM25+vector con reranker (RAG, la mejor).", 150),
        mk("repo_slice", "Búsqueda", "Slice compacto con evidencia previo a editar.", 150),
        mk("context_scout", "Búsqueda", "Scout persistente con handles, vecinos y recibo de presupuesto.", 120),
        mk("context_fetch", "Búsqueda", "Fetch exacto de un rango con validación de hash.", 75),
        mk("verify_claims", "Conocimiento", "Verifica afirmaciones contra el repo/memoria (anti-alucinación).", 160),
        mk("web_search","Web", "Busca en la web (DuckDuckGo/SearXNG).", 140),
        mk("web_fetch", "Web", "Descarga una URL y devuelve su texto.", 90),
        mk("deep_research", "Web", "Investigación web multi-ángulo (dossier de fuentes).", 200),
        mk("write_file","Código",   "Crea o sobrescribe un archivo.", 90),
        mk("edit_file", "Código",   "Reemplazo exacto en un archivo existente.", 160),
        mk("run_shell", "Código",   "Ejecuta un comando de shell.", 110),
        mk("memory",    "Conocimiento", "Memoria persistente del proyecto (save/recall/forget).", 110),
        mk("context_checkpoint", "Coordinación",
           "Cierra una fase y compacta memoria de trabajo con transcript intacto.", 95),
        mk("graph",     "Conocimiento", "Knowledge graph: relaciones, citas, query, index y doctor.", 150),
        mk("ask_teacher", "Multi-Agente", "Consulta a un modelo más capaz (endpoint aparte).", 130),
        mk("task",      "Multi-Agente", "Delega una subtarea a un sub-agente en worktree.", 180),
        mk("skill_list", "Habilidades", "Lista habilidades portables por metadata.", 65),
        mk("skill_load", "Habilidades", "Carga instrucciones de una habilidad bajo demanda.", 85),
        mk("browser_skill_list", "Browser", "Lista skills de browser grabados (teach).", 70),
        mk("browser_skill_replay", "Browser", "Reproduce un skill de browser grabado (Playwright).", 100),
        mk("browser_network_discover", "Browser", "Resume requests Playwright sin secretos ni cuerpos.", 105),
        mk("recent_actions", "Conocimiento", "Relee tu rastro reciente (tool_calls/fallos) para auto-corregirte.", 90),
        mk("desktop_snapshot", "Escritorio", "Snapshot determinista con fingerprint y stale guard.", 125),
        mk("desktop_windows", "Escritorio", "Inventario estructurado de ventanas (barato, sin captura).", 80),
        mk("desktop_controls", "Escritorio", "Árbol de controles de una ventana (UIA, DOM-aware).", 150),
        mk("desktop_click_element", "Escritorio", "Click a un control por nombre/id (UIA), no por pixel.", 110),
        mk("desktop_control_action", "Escritorio", "Acción semántica UIA sin foco cuando el patrón existe.", 120),
        mk("desktop_find_image", "Escritorio", "Localiza una plantilla visual con confianza y ambigüedad.", 135),
        mk("desktop_click_image", "Escritorio", "Localiza y clickea una plantilla visual única.", 145),
        mk("desktop_wait_image", "Escritorio", "Espera aparición o desaparición de una plantilla.", 120),
        mk("desktop_assert_image", "Escritorio", "Verifica una condición visual sin actuar.", 120),
        mk("desktop_observe", "Escritorio", "Captura el alcance visual enseñado.", 90),
        mk("desktop_click", "Escritorio", "Click visual con coordenadas normalizadas.", 100),
        mk("desktop_stroke", "Escritorio", "Arrastra una traza continua (dibujar/pintar/swipe).", 95),
        mk("desktop_type", "Escritorio", "Escribe en el control enfocado.", 80),
        mk("desktop_key", "Escritorio", "Presiona teclas o combinaciones.", 90),
        mk("desktop_scroll", "Escritorio", "Desplaza el escritorio.", 70),
        mk("desktop_focus", "Escritorio", "Enfoca una ventana.", 70),
        mk("desktop_resize", "Escritorio", "Restaura el tamaño enseñado de una ventana.", 72),
        mk("desktop_wait", "Escritorio", "Espera antes de observar.", 60),
        mk("desktop_wait_for", "Escritorio", "Espera una condición (ventana/control) sin dormir fijo.", 85),
        mk("desktop_assert", "Escritorio", "Verifica una condición comprobable (PASS/FAIL).", 88),
        mk("desktop_launch", "Escritorio", "Abre una app desprendida (no bloquea como run_shell).", 95),
        mk("email_accounts", "Correo", "Lista las cuentas de correo configuradas.", 60),
        mk("email_send", "Correo", "Envía un correo (SMTP). Requiere aprobación por defecto.", 150),
        mk("email_list", "Correo", "Lista correos recientes (IMAP/POP3).", 130),
        mk("email_read", "Correo", "Lee el cuerpo de un correo por uid.", 100),
        mk("worker_call", "Plugins", "Invoca una operación de un worker Node/Python externo; requiere aprobación.", 135),
    };
}

void LlamaAgentBackend::setDisabledTools(const QStringList &names)
{
    m_disabledTools = QSet<QString>(names.cbegin(), names.cend());
}

void LlamaAgentBackend::setAdaptiveToolRouting(bool enabled)
{
    m_adaptiveToolRouting = enabled;
    if (!enabled) m_toolSurfaceDecision = ToolSurfaceRouter::Decision{};
}

void LlamaAgentBackend::setTeacherConfig(const QString &url, const QString &model, const QString &key)
{
    m_teacherUrl = url; m_teacherModel = model; m_teacherKey = key;
    if (m_running && m_worker)
        QMetaObject::invokeMethod(m_worker, "setTeacherConfig", Qt::QueuedConnection,
                                  Q_ARG(QString, url), Q_ARG(QString, model), Q_ARG(QString, key));
}

void LlamaAgentBackend::setAuxiliaryServerConfig(const QString &url,
                                                 const QString &embeddingModel,
                                                 const QString &rerankModel,
                                                 const QString &bearer)
{
    m_auxiliaryUrl = url.trimmed();
    m_auxiliaryEmbeddingModel = embeddingModel.trimmed();
    m_auxiliaryRerankModel = rerankModel.trimmed();
    m_auxiliaryBearer = bearer.trimmed();
    if (m_running && m_worker)
        QMetaObject::invokeMethod(m_worker, "setAuxiliaryServerConfig", Qt::QueuedConnection,
                                  Q_ARG(QString, m_auxiliaryUrl),
                                  Q_ARG(QString, m_auxiliaryEmbeddingModel),
                                  Q_ARG(QString, m_auxiliaryRerankModel),
                                  Q_ARG(QString, m_auxiliaryBearer));
}

void LlamaAgentBackend::setMasterCli(const QString &kind, const QString &cliName,
                                     const QString &cliPath, const QString &escalation,
                                     int autoAfterFails, bool applyEdits, int timeoutSec)
{
    m_masterKind = kind.isEmpty() ? QStringLiteral("none") : kind;
    m_masterCliName = cliName;
    m_masterCliPath = cliPath;
    m_masterEscalation = escalation.isEmpty() ? QStringLiteral("manual") : escalation;
    m_masterAutoAfterFails = autoAfterFails > 0 ? autoAfterFails : 3;
    m_masterApplyEdits = applyEdits;
    m_masterTimeoutS = timeoutSec > 0 ? timeoutSec : 300;
    if (m_running && m_worker)
        QMetaObject::invokeMethod(m_worker, "setMasterCli", Qt::QueuedConnection,
                                  Q_ARG(QString, m_masterKind), Q_ARG(QString, m_masterCliName),
                                  Q_ARG(QString, m_masterCliPath), Q_ARG(bool, m_masterApplyEdits),
                                  Q_ARG(int, m_masterTimeoutS));
}

void LlamaAgentBackend::setMasterChain(const QVariantList &chain, const QString &escalation,
                                       int autoAfterFails)
{
    m_masterChain = chain;
    m_masterEscalation = escalation.isEmpty() ? QStringLiteral("manual") : escalation;
    m_masterAutoAfterFails = autoAfterFails > 0 ? autoAfterFails : 3;
    if (m_running && m_worker)
        QMetaObject::invokeMethod(m_worker, "setMasterChain", Qt::QueuedConnection,
                                  Q_ARG(QVariantList, m_masterChain));
}

void LlamaAgentBackend::completeManagedAgentRun(const QString &requestId,
                                                const QVariantMap &run)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "completeManagedRun", Qt::QueuedConnection,
                              Q_ARG(QString, requestId), Q_ARG(QVariantMap, run));
}

bool LlamaAgentBackend::escalateToMaster(const QString &problem)
{
    if (!masterConfigured()) return false;
    const QString p = problem.trimmed();
    QString msg = QStringLiteral(
        "Usá la tool ask_teacher para que el maestro resuelva el problema en el que "
        "estás trabado. Pasale en 'context' el código/errores mínimos necesarios.");
    if (!p.isEmpty())
        msg += QStringLiteral("\n\nProblema concreto:\n%1").arg(p);
    if (isBusy()) steerMessage(msg);
    else sendMessage(msg);
    return true;
}

// ───────────────────────────── MCP / Worker ──────────────────────────────
void LlamaAgentBackend::setMcpServers(const QVariantList &servers)
{
    m_mcpConfig = servers;
    if (m_running && m_worker)
        QMetaObject::invokeMethod(m_worker, "initServers", Qt::QueuedConnection,
                                  Q_ARG(QVariantList, m_mcpConfig), Q_ARG(QString, m_cwd));
}

void LlamaAgentBackend::setMailAccounts(const QVariantList &accounts)
{
    m_mailAccounts = accounts;
    if (m_running && m_worker)
        QMetaObject::invokeMethod(m_worker, "setMailAccounts", Qt::QueuedConnection,
                                  Q_ARG(QVariantList, m_mailAccounts));
}

void LlamaAgentBackend::setWebProviders(const QVariantList &providers)
{
    m_webProviders = providers;
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "setWebProviders", Qt::QueuedConnection,
                                  Q_ARG(QVariantList, m_webProviders));
}

void LlamaAgentBackend::setPortableSkillPolicy(const HarnessSkillsModule &policy)
{
    m_skillPolicy = policy;
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "setPortableSkillPolicy", Qt::QueuedConnection,
                                  Q_ARG(QStringList, m_skillPolicy.include),
                                  Q_ARG(QStringList, m_skillPolicy.exclude),
                                  Q_ARG(bool, m_skillPolicy.set));
}

void LlamaAgentBackend::setLivePreviewEnabled(bool enabled)
{
    if (m_livePreviewEnabled == enabled && !m_worker) return;
    m_livePreviewEnabled = enabled;
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "setLivePreviewEnabled", Qt::QueuedConnection,
                              Q_ARG(bool, enabled));
}

void LlamaAgentBackend::setExecutionPaused(bool paused)
{
    m_executionPaused = paused;
    if (!paused && m_running)
        processPendingCalls();
}

void LlamaAgentBackend::stepExecution()
{
    if (!m_running) return;
    m_executionStep = true;
    m_executionPaused = false;
    processPendingCalls();
}

void LlamaAgentBackend::ensureWorker()
{
    if (m_worker) return;
    m_workerThread = new QThread(this);
    m_worker = new AgentToolRunner;          // sin parent: se mueve al hilo
    m_worker->moveToThread(m_workerThread);
    connect(m_worker, &AgentToolRunner::logAppended, this, &LlamaAgentBackend::logAppended);
    connect(m_worker, &AgentToolRunner::serversReady, this, &LlamaAgentBackend::onServersReady);
    connect(m_worker, &AgentToolRunner::toolExecuted, this, &LlamaAgentBackend::onToolExecuted);
    connect(m_worker, &AgentToolRunner::toolStarted, this, &LlamaAgentBackend::onToolStarted);
    connect(m_worker, &AgentToolRunner::toolOutputChunk, this, &LlamaAgentBackend::onToolOutputChunk);
    connect(m_worker, &AgentToolRunner::managedAgentRunRequested, this,
            [this](const QVariantMap &request) {
        emit managedAgentRunRequested(request);
    });
    connect(m_worker, &AgentToolRunner::managedAgentRunCancelRequested, this,
            [this](const QString &requestId) {
        emit managedAgentRunCancelRequested(requestId);
    });
    m_workerThread->start();
}

void LlamaAgentBackend::configureWorker()
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, "setLivePreviewEnabled", Qt::QueuedConnection,
                              Q_ARG(bool, m_livePreviewEnabled));
    QMetaObject::invokeMethod(m_worker, "setConfined", Qt::QueuedConnection,
                              Q_ARG(bool, m_approvalMode != QLatin1String("super")));
    QMetaObject::invokeMethod(m_worker, "setServerBaseUrl", Qt::QueuedConnection,
                              Q_ARG(QString, m_ctx.serverBaseUrl));
    QMetaObject::invokeMethod(m_worker, "setAuxiliaryServerConfig", Qt::QueuedConnection,
                              Q_ARG(QString, m_auxiliaryUrl),
                              Q_ARG(QString, m_auxiliaryEmbeddingModel),
                              Q_ARG(QString, m_auxiliaryRerankModel),
                              Q_ARG(QString, m_auxiliaryBearer));
    QMetaObject::invokeMethod(m_worker, "setSessionId", Qt::QueuedConnection,
                              Q_ARG(QString, m_sessionId));
    QMetaObject::invokeMethod(m_worker, "setTeacherConfig", Qt::QueuedConnection,
                              Q_ARG(QString, m_teacherUrl), Q_ARG(QString, m_teacherModel),
                              Q_ARG(QString, m_teacherKey));
    QMetaObject::invokeMethod(m_worker, "setMasterChain", Qt::QueuedConnection,
                              Q_ARG(QVariantList, m_masterChain));
    QMetaObject::invokeMethod(m_worker, "setMailAccounts", Qt::QueuedConnection,
                              Q_ARG(QVariantList, m_mailAccounts));
    QMetaObject::invokeMethod(m_worker, "setWebProviders", Qt::QueuedConnection,
                              Q_ARG(QVariantList, m_webProviders));
    QMetaObject::invokeMethod(m_worker, "setPortableSkillPolicy", Qt::QueuedConnection,
                              Q_ARG(QStringList, m_skillPolicy.include),
                              Q_ARG(QStringList, m_skillPolicy.exclude),
                              Q_ARG(bool, m_skillPolicy.set));
    QMetaObject::invokeMethod(m_worker, "initServers", Qt::QueuedConnection,
                              Q_ARG(QVariantList, m_mcpConfig), Q_ARG(QString, m_cwd));
}

void LlamaAgentBackend::restartWorkerAfterTimeout()
{
    if (m_workerThread) {
        disconnect(m_worker, nullptr, this, nullptr);
        m_workerThread->requestInterruption();
        m_workerThread->quit();
        if (!m_workerThread->wait(300)) {
            m_workerThread->terminate();
            m_workerThread->wait(2000);
        }
        delete m_worker;
        m_worker = nullptr;
        delete m_workerThread;
        m_workerThread = nullptr;
    }
    ensureWorker();
    configureWorker();
}

// Solo al destruir el backend. Apaga MCP en el hilo worker (afinidad correcta de
// QProcess), para el hilo y borra todo de forma determinista.
void LlamaAgentBackend::teardownWorker()
{
    if (!m_workerThread) return;
    if (m_worker) {
        disconnect(m_worker, nullptr, this, nullptr);   // sin señales tardías hacia this
        QMetaObject::invokeMethod(m_worker, "shutdown", Qt::BlockingQueuedConnection);
    }
    m_workerThread->quit();
    if (!m_workerThread->wait(8000)) {                  // worker bloqueado en una tool
        m_workerThread->terminate();
        m_workerThread->wait(2000);
    }
    delete m_worker;          // hilo parado + sin QProcess hijos → seguro
    delete m_workerThread;
    m_worker = nullptr;
    m_workerThread = nullptr;
    m_mcpTools.clear();
}

void LlamaAgentBackend::onServersReady(const QVariantList &toolDefs)
{
    m_mcpTools = toolDefs;   // cache para buildToolSchemas
    emit logAppended(QStringLiteral("[mcp] %1 tool(s) descubiertas\n").arg(toolDefs.size()));
}

// Built-in + catálogo MCP lazy. El catálogo completo queda fuera del contexto:
// el modelo descubre schemas puntuales con mcp_search_tools y los invoca mediante
// mcp_call_tool. Esto mantiene plano el costo aunque se conecten muchos servers.
QJsonArray LlamaAgentBackend::buildToolSchemas() const
{
    // Filtra del array las tools deshabilitadas por el usuario (built-in y MCP).
    auto dropDisabled = [this](QJsonArray in) {
        if (m_disabledTools.isEmpty()) return in;
        QJsonArray out;
        for (const QJsonValue &v : in) {
            const QString n = v.toObject().value(QStringLiteral("function"))
                                  .toObject().value(QStringLiteral("name")).toString();
            if (!m_disabledTools.contains(n)) out.append(v);
        }
        return out;
    };

    // PLAN MODE: solo lectura. Excluir write_file/edit_file/run_shell y MCP
    // (no podemos saber si una tool MCP muta), dejar read_file/list_dir/grep/
    // glob/web_fetch.
    if (!m_stablePhasePrefix && m_approvalMode == QLatin1String("plan")) {
        static const QSet<QString> planAllowed{
            QStringLiteral("read_file"), QStringLiteral("list_dir"),
            QStringLiteral("grep"), QStringLiteral("glob"), QStringLiteral("web_fetch"),
            QStringLiteral("web_search"), QStringLiteral("deep_research"),
            QStringLiteral("search_docs"), QStringLiteral("semantic_search"),
            QStringLiteral("hybrid_search"), QStringLiteral("repo_slice"),
            QStringLiteral("context_status"), QStringLiteral("work_status"),
            QStringLiteral("context_scout"),
            QStringLiteral("context_fetch"), QStringLiteral("verify_claims"),
            QStringLiteral("browser_skill_list"), QStringLiteral("skill_list"),
            QStringLiteral("skill_load")};
        QJsonArray ro;
        for (const QJsonValue &v : toolSchemas()) {
            const QString n = v.toObject().value(QStringLiteral("function"))
                                  .toObject().value(QStringLiteral("name")).toString();
            if (planAllowed.contains(n)) ro.append(v);
        }
        return dropDisabled(ro);
    }

    QJsonArray all = toolSchemas();
    // The external lane is discovered only after nonce authentication. This
    // keeps legacy profiles identical and prevents the model from seeing a
    // callable tool while its worker is absent or still starting.
    if (m_harnessWorkerReady) {
        QJsonObject operation;
        operation.insert(QStringLiteral("type"), QStringLiteral("string"));
        operation.insert(QStringLiteral("minLength"), 1);
        operation.insert(QStringLiteral("description"),
                         QStringLiteral("Operación registrada por el plugin."));
        QJsonObject payload;
        payload.insert(QStringLiteral("type"), QStringLiteral("object"));
        payload.insert(QStringLiteral("description"),
                       QStringLiteral("Argumentos de la operación."));
        payload.insert(QStringLiteral("additionalProperties"), true);
        QJsonObject workerProperties;
        workerProperties.insert(QStringLiteral("operation"), operation);
        workerProperties.insert(QStringLiteral("payload"), payload);
        QJsonObject workerParameters;
        workerParameters.insert(QStringLiteral("type"), QStringLiteral("object"));
        workerParameters.insert(QStringLiteral("properties"), workerProperties);
        workerParameters.insert(QStringLiteral("required"),
                                QJsonArray{QStringLiteral("operation"), QStringLiteral("payload")});
        workerParameters.insert(QStringLiteral("additionalProperties"), false);
        QJsonObject workerFunction;
        workerFunction.insert(QStringLiteral("name"), QStringLiteral("worker_call"));
        workerFunction.insert(QStringLiteral("description"), QStringLiteral(
            "Invoca una operación del plugin Node/Python seleccionado. "
            "Es una acción externa y requiere aprobación humana salvo Super mode."));
        workerFunction.insert(QStringLiteral("parameters"), workerParameters);
        all.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
                               {QStringLiteral("function"), workerFunction}});
    }
    if (m_mcpToolsEnabled && !m_mcpTools.isEmpty()) {
        const QJsonObject searchProperties{
            {"query", QJsonObject{{"type", "string"}, {"description", "Qué capacidad necesitás"}}},
            {"server", QJsonObject{{"type", "string"}, {"description", "Filtro opcional por servidor"}}},
            {"limit", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 10}, {"default", 5}}}};
        const QJsonObject searchParameters{{"type", "object"}, {"properties", searchProperties},
                                           {"required", QJsonArray{"query"}}, {"additionalProperties", false}};
        all.append(QJsonObject{{"type", "function"}, {"function", QJsonObject{
            {"name", "mcp_search_tools"},
            {"description", "Busca tools MCP por intención. Llamala antes de mcp_call_tool; devuelve nombres exactos y schemas de entrada."},
            {"parameters", searchParameters}}}});

        const QJsonObject callProperties{
            {"name", QJsonObject{{"type", "string"}}},
            {"arguments", QJsonObject{{"type", "object"}, {"additionalProperties", true}}},
            {"changed_paths", QJsonObject{{"type", "array"},
                {"items", QJsonObject{{"type", "string"}}},
                {"description", "Rutas relativas que la operación MCP puede mutar; obligatorias si no es de sólo lectura."}}}};
        const QJsonObject callParameters{{"type", "object"}, {"properties", callProperties},
                                         {"required", QJsonArray{"name"}}, {"additionalProperties", false}};
        all.append(QJsonObject{{"type", "function"}, {"function", QJsonObject{
            {"name", "mcp_call_tool"},
            {"description", "Ejecuta una tool MCP descubierta previamente. Copiá el nombre exacto, anidá sus parámetros dentro de arguments y declará changed_paths si puede mutar archivos."},
            {"parameters", callParameters}}}});
    }
    QJsonArray filtered = dropDisabled(all);
    // El primer request puede llevar una superficie acotada; después de que el
    // modelo haya empezado a interactuar se expande automáticamente. Esto evita
    // que un clasificador imperfecto bloquee una capacidad en un follow-up.
    if (!m_adaptiveToolRouting || m_toolSurfaceDecision.fullSurface || m_turnIters > 1)
        return filtered;

    QHash<QString, QString> groupByTool;
    for (const QVariant &entry : toolCatalog()) {
        const QVariantMap item = entry.toMap();
        groupByTool.insert(item.value(QStringLiteral("name")).toString(),
                           ToolSurfaceRouter::canonicalGroup(
                               item.value(QStringLiteral("group")).toString()));
    }
    QJsonArray routed;
    for (const QJsonValue &value : filtered) {
        const QJsonObject fn = value.toObject().value(QStringLiteral("function")).toObject();
        const QString name = fn.value(QStringLiteral("name")).toString();
        // El descubrimiento MCP sigue siendo lazy: sus dos meta-tools siempre
        // deben estar disponibles aunque el grupo concreto todavía no se sepa.
        if (name == QLatin1String("mcp_search_tools")
            || name == QLatin1String("mcp_call_tool")) {
            routed.append(value);
            continue;
        }
        const QString group = groupByTool.value(name);
        if (group.isEmpty() || m_toolSurfaceDecision.groups.contains(group))
            routed.append(value);
    }
    return routed;
}

bool LlamaAgentBackend::harnessWorkerConfiguredForTest() const
{
    const QString lane = m_harnessWorkerModule.lane.trimmed().toLower();
    return m_harnessWorkerModule.set
        && (lane == QLatin1String("node") || lane == QLatin1String("python"));
}

void LlamaAgentBackend::startHarnessWorker()
{
    if (!harnessWorkerConfiguredForTest()) return;

    HarnessWorkerModule module = m_harnessWorkerModule;
    if (module.entrypoint.trimmed().isEmpty()) {
        emit logAppended(QStringLiteral(
            "[harness worker rechazado: el entrypoint del perfil está vacío]\n"));
        return;
    }
    if (module.workingDirectory.trimmed().isEmpty()) {
        module.workingDirectory = m_cwd;
    } else {
        const QString safe = safeProjectDir(module.workingDirectory);
        if (safe.isEmpty()) {
            emit logAppended(QStringLiteral(
                "[harness worker rechazado: workingDirectory inseguro o inexistente]\n"));
            return;
        }
        module.workingDirectory = safe;
    }

    m_harnessWorker = new HarnessWorkerDriver(this);
    connect(m_harnessWorker, &HarnessWorkerDriver::authenticatedChanged,
            this, &LlamaAgentBackend::onHarnessWorkerAuthenticated);
    connect(m_harnessWorker, &HarnessWorkerDriver::callResult,
            this, &LlamaAgentBackend::onHarnessWorkerCallResult);
    connect(m_harnessWorker, &HarnessWorkerDriver::capabilityCallRequested,
            this, &LlamaAgentBackend::onHarnessWorkerCapabilityCall);
    connect(m_harnessWorker, &HarnessWorkerDriver::workerError,
            this, &LlamaAgentBackend::onHarnessWorkerError);
    connect(m_harnessWorker, &HarnessWorkerDriver::workerExited,
            this, &LlamaAgentBackend::onHarnessWorkerExited);

    // These are the only host-dispatched capabilities in this first vertical
    // slice. All are read-only; network access remains an independent sandbox
    // switch and is never granted through the capability broker.
    const QStringList allowedCapabilities{
        QStringLiteral("fs.read"), QStringLiteral("memory.read"),
        QStringLiteral("graph.read"), QStringLiteral("context.read")};
    QString error;
    if (!HarnessWorkerFactory::start(
            *m_harnessWorker, module, m_cwd, m_harnessActivationId,
            m_harnessEngineId, m_harnessProfileId,
            qMax(1, m_harnessEngineVersion), allowedCapabilities, &error)) {
        emit logAppended(QStringLiteral("[harness worker no iniciado: %1]\n").arg(error));
        delete m_harnessWorker;
        m_harnessWorker = nullptr;
        return;
    }
    emit logAppended(QStringLiteral(
        "[harness worker externo iniciando · lane=%1 · sandbox=%2 · cwd=%3]\n")
                         .arg(module.lane, module.sandbox,
                              QDir::toNativeSeparators(module.workingDirectory)));
}

void LlamaAgentBackend::stopHarnessWorker()
{
    m_harnessWorkerReady = false;
    failWorkerCapabilities(QStringLiteral("worker stopped by the host"));
    if (!m_harnessWorker) {
        m_externalWorkerCalls.clear();
        m_externalWorkerDescriptions.clear();
        return;
    }
    disconnect(m_harnessWorker, nullptr, this, nullptr);
    m_harnessWorker->stop();
    delete m_harnessWorker;
    m_harnessWorker = nullptr;
    m_externalWorkerCalls.clear();
    m_externalWorkerDescriptions.clear();
}

void LlamaAgentBackend::failWorkerCapabilities(const QString &reason)
{
    if (m_harnessWorker) {
        const auto pending = m_workerCapabilityCalls;
        for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
            m_harnessWorker->respondCapabilityCall(
                it.value(), {}, QStringLiteral("capability_host_unavailable"), reason);
        }
    }
    m_workerCapabilityCalls.clear();
    m_workerCapabilityNames.clear();
}

void LlamaAgentBackend::failExternalWorkerCalls(const QString &reason)
{
    const auto pending = m_externalWorkerCalls;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        const QString modelCallId = it.value();
        const QString operation = m_externalWorkerDescriptions.value(it.key());
        if (m_harnessEngineId == QLatin1String("next"))
            m_harnessEffectLedger.transition(
                m_sessionId + QLatin1Char(':') + modelCallId,
                QStringLiteral("settled"), QStringLiteral("worker_unavailable"));
        appendToolCard(QStringLiteral("worker_call"), QStringLiteral("external"), false,
                       operation, reason);
        appendToolResult(modelCallId, QStringLiteral("worker_call"), reason,
                         false, false, true);
    }
    m_externalWorkerCalls.clear();
    m_externalWorkerDescriptions.clear();
}

void LlamaAgentBackend::onHarnessWorkerAuthenticated(bool authenticated)
{
    m_harnessWorkerReady = authenticated;
    emit logAppended(authenticated
        ? QStringLiteral("[harness worker autenticado: worker_call habilitada]\n")
        : QStringLiteral("[harness worker perdió autenticación]\n"));
}

void LlamaAgentBackend::onHarnessWorkerCallResult(const QString &callId,
                                                  const QJsonObject &payload)
{
    if (!m_externalWorkerCalls.contains(callId)) return;
    const QString modelCallId = m_externalWorkerCalls.take(callId);
    const QString operation = m_externalWorkerDescriptions.take(callId);
    const QJsonObject error = payload.value(QStringLiteral("error")).toObject();
    const bool ok = error.isEmpty();
    const QString output = QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact));
    if (m_harnessEngineId == QLatin1String("next")) {
        const QString resultHash = QString::fromLatin1(
            QCryptographicHash::hash(output.toUtf8(), QCryptographicHash::Sha256).toHex());
        m_harnessEffectLedger.transition(
            m_sessionId + QLatin1Char(':') + modelCallId,
            QStringLiteral("settled"), ok ? QString() : QStringLiteral("worker_failed"),
            resultHash);
    }
    if (ok) ++m_toolOk; else ++m_toolFail;
    recordToolOutcome(QStringLiteral("worker_call"), ok, false, output);
    appendToolCard(QStringLiteral("worker_call"), QStringLiteral("external"), ok,
                   operation, output);
    AgentEventLog::append(m_cwd, m_sessionId, ok ? QStringLiteral("tool_result")
                                                 : QStringLiteral("failure"),
                          QJsonObject{{QStringLiteral("tool"), QStringLiteral("worker_call")},
                                      {QStringLiteral("toolCallId"), modelCallId},
                                      {QStringLiteral("operation"), operation},
                                      {QStringLiteral("externalWrite"), true},
                                      {QStringLiteral("result"), output.left(8192)}});
    appendToolResult(modelCallId, QStringLiteral("worker_call"), output,
                     ok, false, true);
    processPendingCalls();
}

void LlamaAgentBackend::onHarnessWorkerError(const QString &message)
{
    m_harnessWorkerReady = false;
    emit logAppended(QStringLiteral("[harness worker error: %1]\n").arg(message));
    failWorkerCapabilities(message);
    if (!m_externalWorkerCalls.isEmpty()) {
        failExternalWorkerCalls(QStringLiteral(
            "[worker externo falló antes de devolver un resultado: %1]").arg(message));
        processPendingCalls();
    }
}

void LlamaAgentBackend::onHarnessWorkerExited(int exitCode)
{
    m_harnessWorkerReady = false;
    emit logAppended(QStringLiteral("[harness worker finalizado · exit=%1]\n").arg(exitCode));
    if (!m_externalWorkerCalls.isEmpty()) {
        failExternalWorkerCalls(QStringLiteral(
            "[worker externo finalizó antes de devolver un resultado]") );
        processPendingCalls();
    }
}

void LlamaAgentBackend::onHarnessWorkerCapabilityCall(const QString &requestId,
                                                      const QString &capability,
                                                      const QString &operation,
                                                      const QJsonObject &payload)
{
    dispatchWorkerCapability(requestId, capability, operation, payload);
}

void LlamaAgentBackend::dispatchWorkerCapability(const QString &requestId,
                                                 const QString &capability,
                                                 const QString &operation,
                                                 const QJsonObject &payload)
{
    if (!m_harnessWorker || !m_harnessWorkerReady || !m_worker) return;

    QString tool;
    QJsonObject args = payload;
    if (capability == QLatin1String("fs.read")
        && (operation == QLatin1String("read") || operation == QLatin1String("read_file"))) {
        tool = QStringLiteral("read_file");
    } else if (capability == QLatin1String("memory.read")
               && operation == QLatin1String("recall")) {
        tool = QStringLiteral("memory");
        args.insert(QStringLiteral("action"), QStringLiteral("recall"));
    } else if (capability == QLatin1String("graph.read")
               && operation == QLatin1String("query")) {
        tool = QStringLiteral("graph");
        args.insert(QStringLiteral("action"), QStringLiteral("query"));
    } else if (capability == QLatin1String("context.read")
               && operation == QLatin1String("status")) {
        tool = QStringLiteral("context_status");
    }

    if (tool.isEmpty()) {
        m_harnessWorker->respondCapabilityCall(
            requestId, {}, QStringLiteral("capability_operation_unsupported"),
            QStringLiteral("the host only exposes read-only operations for this capability"));
        return;
    }

    const QString callId = QStringLiteral("worker-cap-%1")
                               .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_workerCapabilityCalls.insert(callId, requestId);
    m_workerCapabilityNames.insert(callId, capability);
    QMetaObject::invokeMethod(m_worker, "executeTool", Qt::QueuedConnection,
                              Q_ARG(QString, callId), Q_ARG(QString, tool),
                              Q_ARG(QString, QString::fromUtf8(
                                  QJsonDocument(args).toJson(QJsonDocument::Compact))),
                              Q_ARG(QString, m_cwd));
}

void LlamaAgentBackend::finishWorkerCapability(const QVariantMap &result)
{
    const QString callId = result.value(QStringLiteral("callId")).toString();
    const QString requestId = m_workerCapabilityCalls.take(callId);
    const QString capability = m_workerCapabilityNames.take(callId);
    if (requestId.isEmpty() || !m_harnessWorker) return;

    const bool ok = result.value(QStringLiteral("ok")).toBool();
    const QString text = result.value(QStringLiteral("result")).toString();
    QJsonObject payload;
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (ok && parseError.error == QJsonParseError::NoError && parsed.isObject())
        payload = parsed.object();
    else if (ok)
        payload.insert(QStringLiteral("value"), text);
    if (ok) {
        m_harnessWorker->respondCapabilityCall(requestId, payload);
    } else {
        m_harnessWorker->respondCapabilityCall(
            requestId, {}, QStringLiteral("capability_operation_failed"),
            QStringLiteral("%1: %2").arg(capability, text.left(2048)));
    }
}

// Diff unificado simple: recorta prefijo/sufijo de líneas comunes y marca el
// bloque cambiado con '-' (viejo) y '+' (nuevo). No es LCS, pero alcanza para ver.
QString LlamaAgentBackend::makeDiff(const QString &oldText, const QString &newText)
{
    const QStringList o = oldText.split(QLatin1Char('\n'));
    const QStringList n = newText.split(QLatin1Char('\n'));
    int p = 0;
    while (p < o.size() && p < n.size() && o[p] == n[p]) ++p;
    int s = 0;
    while (s < (o.size() - p) && s < (n.size() - p)
           && o[o.size() - 1 - s] == n[n.size() - 1 - s]) ++s;

    QStringList out;
    const int ctx = 2;
    for (int i = qMax(0, p - ctx); i < p; ++i) out << QStringLiteral("  ") + o[i];
    for (int i = p; i < o.size() - s; ++i)     out << QStringLiteral("- ") + o[i];
    for (int i = p; i < n.size() - s; ++i)     out << QStringLiteral("+ ") + n[i];
    for (int i = o.size() - s; i < qMin(o.size(), o.size() - s + ctx); ++i)
        out << QStringLiteral("  ") + o[i];
    if (out.isEmpty()) out << QStringLiteral("  (sin cambios)");
    return out.join(QLatin1Char('\n'));
}

void LlamaAgentBackend::revertEdit(const QString &path)
{
    // path puede venir relativo (UI) o absoluto. Resolver contra cwd.
    const QString abs = QFileInfo(path).isAbsolute()
        ? QDir::cleanPath(path)
        : QDir::cleanPath(QDir(m_cwd).absoluteFilePath(path));
    if (!m_editSnapshots.contains(abs)) {
        emit errorOccurred(QStringLiteral("No hay snapshot para revertir: %1").arg(path));
        return;
    }
    const EditSnapshot snap = m_editSnapshots.value(abs);
    if (!snap.existed) {
        QFile::remove(abs);
    } else {
        QFile f(abs);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(snap.oldContent);
            f.close();
        }
    }
    m_editSnapshots.remove(abs);
    emit logAppended(QStringLiteral("[revertido: %1]\n").arg(path));

    // Marcar la entrada de diff como revertida en el chat.
    for (int i = 0; i < m_messages.size(); ++i) {
        QVariantMap m = m_messages[i].toMap();
        if (m.value(QStringLiteral("role")).toString() == QLatin1String("diff")
                && m.value(QStringLiteral("absPath")).toString() == abs
                && !m.value(QStringLiteral("reverted")).toBool()) {
            m[QStringLiteral("reverted")] = true;
            m_messages[i] = m;
        }
    }
    emit messagesChanged();
}

// ───────────────────────────── Sesiones + persistencia ───────────────────
void LlamaAgentBackend::newSession()
{
    saveCurrentSession();
    // Cambiar/crear una sesión debe ser sólo una operación de UI y persistencia.
    // No iniciar consolidación contra un transcript potencialmente grande acá:
    // ese request coincidía con el reset del modelo QML y podía cerrar la app.
    m_sessionId.clear();
    m_messages.clear();
    m_apiMessages = {};
    m_transcriptMessages = {};
    m_curAsstIdx = -1;
    m_desktopLaunchApps.clear();
    ensureSession();        // crea sesión nueva + system prompt + persiste
    // La sesión que se acaba de dejar, si nunca tuvo mensajes, no queda en el panel.
    pruneEmptySessions(m_sessionId);
}

void LlamaAgentBackend::newTaskSession()
{
    // La Task/Automatización corre en una sesión EFÍMERA y aislada: no se lista
    // en el panel de Sesiones ni se persiste. Se guarda la sesión real del
    // usuario y se recuerda para restaurarla al terminar (endTaskSession), así
    // volver a "Agente" no muestra la corrida de la Task como sesión propia.
    saveCurrentSession();
    m_preTaskSessionId = m_sessionId;
    m_preTaskEphemeral = m_ephemeralSessions;
    m_ephemeralSessions = true;
    m_sessionId.clear();
    m_messages.clear();
    m_apiMessages = {};
    m_transcriptMessages = {};
    m_curAsstIdx = -1;
    m_desktopLaunchApps.clear();
    ensureSession();
}

void LlamaAgentBackend::endTaskSession()
{
    // Descarta la sesión efímera de la Task y restaura la sesión del usuario
    // previa. No persiste ni consolida la sesión de la Task (m_sessionId vacío
    // antes de setCurrentSession → saveCurrentSession es no-op).
    m_ephemeralSessions = m_preTaskEphemeral;
    const QString prev = m_preTaskSessionId;
    m_preTaskSessionId.clear();
    m_sessionId.clear();
    m_messages.clear();
    m_apiMessages = {};
    m_transcriptMessages = {};
    m_curAsstIdx = -1;
    m_desktopLaunchApps.clear();
    m_readFingerprints.clear();
    m_checkpoints.clear();
    if (!prev.isEmpty())
        setCurrentSession(prev);
    else
        ensureSession();
    emit sessionsChanged();
    emit messagesChanged();
}

void LlamaAgentBackend::newSessionInProject(const QString &projectDir)
{
    const QString safe = safeProjectDir(projectDir);
    if (!safe.isEmpty()) m_cwd = safe;
    newSession();
}

bool LlamaAgentBackend::updateQueuedMessage(int index, const QString &text)
{
    if (!m_isSessionRuntime) {
        if (LlamaAgentBackend *runtime = viewRuntime())
            return runtime->updateQueuedMessage(index, text);
    }
    const QString updated = text.trimmed();
    if (index < 0 || index >= m_msgQueue.size() || updated.isEmpty()) return false;
    if (m_msgQueue[index] == updated) return true;
    m_msgQueue[index] = updated;
    emit queueChanged();
    return true;
}

bool LlamaAgentBackend::removeQueuedMessage(int index)
{
    if (!m_isSessionRuntime) {
        if (LlamaAgentBackend *runtime = viewRuntime())
            return runtime->removeQueuedMessage(index);
    }
    if (index < 0 || index >= m_msgQueue.size()) return false;
    m_msgQueue.removeAt(index);
    emit queueChanged();
    return true;
}

void LlamaAgentBackend::autoTitleCurrentSession(const QString &firstPrompt)
{
    if (m_ephemeralSessions || m_sessionId.isEmpty()
        || m_sessionTitle != QStringLiteral("Sesión")) return;
    const QString title = suggestSessionTitle(firstPrompt);
    if (title == QStringLiteral("Sesión")) return;
    m_sessionTitle = title;
    for (int i = 0; i < m_sessions.size(); ++i) {
        QVariantMap session = m_sessions.at(i).toMap();
        if (session.value(QStringLiteral("id")).toString() != m_sessionId) continue;
        session[QStringLiteral("title")] = title;
        m_sessions[i] = session;
        break;
    }
    persistIndex();
    persistSession(m_sessionId);
    emit sessionsChanged();
}

LlamaAgentBackend *LlamaAgentBackend::viewRuntime() const
{
    if (m_viewSessionId.isEmpty()) return nullptr;
    return m_sessionRuntimes.value(m_viewSessionId, nullptr);
}

void LlamaAgentBackend::copyRuntimeConfigurationTo(LlamaAgentBackend *runtime) const
{
    if (!runtime) return;
    runtime->m_approvalMode = m_approvalMode;
    runtime->m_systemExtra = m_systemExtra;
    runtime->m_temperature = m_temperature;
    runtime->m_thinkingEnabled = m_thinkingEnabled;
    runtime->m_thinkingLeakGuard = m_thinkingLeakGuard;
    runtime->m_stablePhasePrefix = m_stablePhasePrefix;
    runtime->m_directives = m_directives;
    runtime->m_directivesSet = m_directivesSet;
    runtime->m_permRules = m_permRules;
    runtime->m_disabledTools = m_disabledTools;
    runtime->m_teacherUrl = m_teacherUrl;
    runtime->m_teacherModel = m_teacherModel;
    runtime->m_teacherKey = m_teacherKey;
    runtime->m_masterKind = m_masterKind;
    runtime->m_masterCliName = m_masterCliName;
    runtime->m_masterCliPath = m_masterCliPath;
    runtime->m_masterEscalation = m_masterEscalation;
    runtime->m_masterAutoAfterFails = m_masterAutoAfterFails;
    runtime->m_masterApplyEdits = m_masterApplyEdits;
    runtime->m_masterTimeoutS = m_masterTimeoutS;
    runtime->m_masterChain = m_masterChain;
    runtime->m_mcpConfig = m_mcpConfig;
    runtime->m_mailAccounts = m_mailAccounts;
    runtime->m_webProviders = m_webProviders;
    runtime->m_mailAutoSend = m_mailAutoSend;
    runtime->m_hitlDestructive = m_hitlDestructive;
    runtime->m_mcpToolsEnabled = m_mcpToolsEnabled;
    runtime->m_forceTextTools = m_forceTextTools;
    runtime->m_visionReady = m_visionReady;
    runtime->m_alwaysAllowed = m_alwaysAllowed;
    runtime->m_harnessEngineId = m_harnessEngineId;
    runtime->m_harnessEngineVersion = m_harnessEngineVersion;
    runtime->m_harnessProfileId = m_harnessProfileId;
    runtime->m_harnessSpecHash = m_harnessSpecHash;
    runtime->m_harnessWorkerModule = m_harnessWorkerModule;
}

void LlamaAgentBackend::syncRuntimeSession(const QString &sessionId,
                                           LlamaAgentBackend *runtime)
{
    if (!runtime) return;
    if (m_viewSessionId == sessionId) {
        m_viewMessages = runtime->messages();
        m_viewSessionTitle = runtime->currentSessionTitle();
        m_viewProjectDir = runtime->currentProjectDir();
    }
    for (const QVariant &candidate : runtime->sessions()) {
        const QVariantMap updated = candidate.toMap();
        if (updated.value(QStringLiteral("id")).toString() != sessionId) continue;
        for (int i = 0; i < m_sessions.size(); ++i) {
            if (m_sessions.at(i).toMap().value(QStringLiteral("id")).toString() == sessionId) {
                m_sessions[i] = updated;
                break;
            }
        }
        break;
    }
}

LlamaAgentBackend *LlamaAgentBackend::ensureSessionRuntime(const QString &sessionId)
{
    if (sessionId.isEmpty() || m_isSessionRuntime) return nullptr;
    if (LlamaAgentBackend *existing = m_sessionRuntimes.value(sessionId, nullptr))
        return existing;

    QString projectDir;
    for (const QVariant &value : std::as_const(m_sessions)) {
        const QVariantMap session = value.toMap();
        if (session.value(QStringLiteral("id")).toString() == sessionId) {
            projectDir = session.value(QStringLiteral("projectDir")).toString();
            break;
        }
    }
    if (projectDir.isEmpty()) return nullptr;

    auto *runtime = new LlamaAgentBackend(this);
    runtime->m_isSessionRuntime = true;
    copyRuntimeConfigurationTo(runtime);
    m_sessionRuntimes.insert(sessionId, runtime);

    connect(runtime, &IAgentBackend::messagesChanged, this, [this, sessionId, runtime]() {
        syncRuntimeSession(sessionId, runtime);
        if (m_viewSessionId == sessionId) emit messagesChanged();
        emit sessionsChanged();
    });
    connect(runtime, &IAgentBackend::streamingText, this,
            [this, sessionId](int index, const QString &content) {
        if (m_viewSessionId == sessionId) emit streamingText(index, content);
    });
    connect(runtime, &IAgentBackend::queueChanged, this, [this, sessionId]() {
        if (m_viewSessionId == sessionId) emit queueChanged();
    });
    connect(runtime, &IAgentBackend::sessionsChanged, this, [this, sessionId, runtime]() {
        syncRuntimeSession(sessionId, runtime);
        emit sessionsChanged();
    });
    connect(runtime, &IAgentBackend::toolApprovalNeeded, this,
            [this, sessionId](QVariantMap call) {
        call[QStringLiteral("sessionId")] = sessionId;
        emit toolApprovalNeeded(call);
    });
    connect(runtime, &IAgentBackend::turnFinished, this, [this, sessionId, runtime]() {
        syncRuntimeSession(sessionId, runtime);
        pumpSessionRuntimes();
        emit sessionsChanged();
    });
    connect(runtime, &IAgentBackend::logAppended, this,
            [this, sessionId](const QString &chunk) {
        emit logAppended(QStringLiteral("[session %1] %2").arg(sessionId.left(8), chunk));
    });
    connect(runtime, &IAgentBackend::errorOccurred, this, &IAgentBackend::errorOccurred);
    connect(runtime, &IAgentBackend::desktopActivityChanged,
            this, &IAgentBackend::desktopActivityChanged);
    connect(runtime, &IAgentBackend::contextUsage, this, &IAgentBackend::contextUsage);
    connect(runtime, &IAgentBackend::contextManaged, this, &IAgentBackend::contextManaged);
    connect(runtime, &IAgentBackend::gitRequired, this, &IAgentBackend::gitRequired);

    AgentContext runtimeContext = m_ctx;
    runtimeContext.cwd = projectDir;
    runtime->start(runtimeContext);
    runtime->switchSession(sessionId);
    syncRuntimeSession(sessionId, runtime);
    return runtime;
}

int LlamaAgentBackend::activeRuntimeCount() const
{
    auto cost = [](const LlamaAgentBackend *runtime) {
        if (!runtime || !runtime->isBusy()) return 0;
        // Un turno que espera varios subagentes ocupa tantos slots de inferencia
        // como runners activos (al menos uno para estados tool/aprobación).
        return qMax(1, static_cast<int>(runtime->m_subs.size()));
    };
    int count = cost(this);
    for (LlamaAgentBackend *runtime : m_sessionRuntimes) count += cost(runtime);
    return count;
}

void LlamaAgentBackend::pumpSessionRuntimes()
{
    const int capacity = qMax(1, m_ctx.parallelSlots);
    while (activeRuntimeCount() < capacity) {
        LlamaAgentBackend *next = nullptr;
        for (LlamaAgentBackend *runtime : std::as_const(m_sessionRuntimes)) {
            if (runtime && !runtime->isBusy() && !runtime->m_msgQueue.isEmpty()) {
                next = runtime;
                break;
            }
        }
        if (!next) break;
        next->flushQueue();
    }
}

void LlamaAgentBackend::switchSession(const QString &sessionId)
{
    if (sessionId.isEmpty()) return;
    if (!m_isSessionRuntime && m_sessionRuntimes.contains(sessionId)) {
        showSessionWhileTurnRuns(sessionId);
        if (LlamaAgentBackend *runtime = m_sessionRuntimes.value(sessionId))
            syncRuntimeSession(sessionId, runtime);
        return;
    }
    if (sessionId == m_sessionId) {
        if (m_viewSessionId.isEmpty()) return;
        m_viewSessionId.clear();
        m_viewSessionTitle.clear();
        m_viewProjectDir.clear();
        m_viewMessages.clear();
        emit sessionsChanged();
        emit messagesChanged();
        return;
    }
    if (isBusy()) {
        showSessionWhileTurnRuns(sessionId);
        return;
    }
    setCurrentSession(sessionId);
}

void LlamaAgentBackend::showSessionWhileTurnRuns(const QString &sessionId)
{
    m_viewSessionId = sessionId;
    m_viewSessionTitle.clear();
    m_viewProjectDir.clear();
    m_viewMessages.clear();
    for (const QVariant &v : std::as_const(m_sessions)) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("id")).toString() != sessionId) continue;
        m_viewSessionTitle = s.value(QStringLiteral("title")).toString();
        m_viewProjectDir = s.value(QStringLiteral("projectDir")).toString();
        break;
    }
    if (LlamaAgentBackend *runtime = m_sessionRuntimes.value(sessionId, nullptr)) {
        m_viewSessionTitle = runtime->currentSessionTitle();
        m_viewProjectDir = runtime->currentProjectDir();
        m_viewMessages = runtime->messages();
        emit sessionsChanged();
        emit messagesChanged();
        return;
    }
    QFile f(sessionFilePath(sessionId));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray msgs = QJsonDocument::fromJson(f.readAll())
                                    .object().value(QStringLiteral("messages")).toArray();
        f.close();
        for (const QJsonValue &mv : msgs) {
            QVariantMap message = mv.toObject().toVariantMap();
            message[QStringLiteral("typing")] = false;
            m_viewMessages.append(message);
        }
    }
    emit sessionsChanged();
    emit messagesChanged();
}

void LlamaAgentBackend::activateViewedSessionIfIdle()
{
    if (m_viewSessionId.isEmpty() || isBusy()) return;
    const QString sessionId = m_viewSessionId;
    m_viewSessionId.clear();
    m_viewSessionTitle.clear();
    m_viewProjectDir.clear();
    m_viewMessages.clear();
    setCurrentSession(sessionId);
}

void LlamaAgentBackend::renameSession(const QString &sessionId, const QString &title)
{
    const QString t = title.trimmed();
    if (sessionId.isEmpty() || t.isEmpty()) return;
    if (sessionId == m_sessionId) m_sessionTitle = t;
    for (int i = 0; i < m_sessions.size(); ++i) {
        QVariantMap s = m_sessions[i].toMap();
        if (s.value(QStringLiteral("id")).toString() == sessionId) {
            s[QStringLiteral("title")] = t;
            m_sessions[i] = s;
            break;
        }
    }
    persistIndex();
    persistSession(sessionId);
    emit sessionsChanged();
}

void LlamaAgentBackend::deleteSession(const QString &sessionId)
{
    if (sessionId.isEmpty()) return;
    if (!m_isSessionRuntime) {
        if (LlamaAgentBackend *runtime = m_sessionRuntimes.take(sessionId)) {
            runtime->stop();
            runtime->deleteLater();
        }
        if (m_viewSessionId == sessionId) {
            m_viewSessionId.clear();
            m_viewMessages.clear();
        }
    }
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i].toMap().value(QStringLiteral("id")).toString() == sessionId) {
            m_sessions.removeAt(i);
            break;
        }
    }
    removeSessionFile(sessionId);
    if (sessionId == m_sessionId) {
        m_sessionId.clear();
        m_messages.clear();
        m_apiMessages = {};
        m_transcriptMessages = {};
        m_curAsstIdx = -1;
        if (!m_sessions.isEmpty())
            setCurrentSession(m_sessions.first().toMap().value(QStringLiteral("id")).toString());
        else
            ensureSession();
    }
    persistIndex();
    emit sessionsChanged();
}

bool LlamaAgentBackend::sessionHasNoMessages(const QString &sessionId) const
{
    if (sessionId.isEmpty()) return false;
    if (sessionId == m_sessionId) return m_messages.isEmpty();
    QFile f(sessionFilePath(sessionId));
    if (!f.open(QIODevice::ReadOnly)) return true;   // sin archivo = sin historia
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    return obj.value(QStringLiteral("messages")).toArray().isEmpty();
}

void LlamaAgentBackend::pruneEmptySessions(const QString &keepId)
{
    if (m_ephemeralSessions || m_isSessionRuntime) return;
    QStringList doomed;
    for (const QVariant &v : std::as_const(m_sessions)) {
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        if (id.isEmpty() || id == keepId) continue;
        // Una sesión con runtime propio puede tener un turno en vuelo cuyo
        // primer mensaje todavía no tocó disco: no se toca.
        if (m_sessionRuntimes.contains(id) || id == m_viewSessionId) continue;
        if (id == m_sessionId) continue;   // la activa se descarta recién al dejarla
        if (!sessionHasNoMessages(id)) continue;
        doomed << id;
    }
    for (const QString &id : std::as_const(doomed))
        deleteSession(id);
}

LlamaAgentBackend::RetryClass
LlamaAgentBackend::classifyCompletionError(int httpStatus, const QString &errorText)
{
    const QString e = errorText.toLower();
    static const QStringList contextMarkers{
        QStringLiteral("context_length"), QStringLiteral("context length"),
        QStringLiteral("maximum context"), QStringLiteral("too many tokens"),
        QStringLiteral("n_ctx"), QStringLiteral("prompt is too long"),
        QStringLiteral("exceeds the context")
    };
    if (httpStatus == 413) return RetryContextOverflow;
    for (const QString &marker : contextMarkers)
        if (e.contains(marker)) return RetryContextOverflow;
    if (httpStatus == 408 || httpStatus == 425 || httpStatus == 429
        || httpStatus == 500 || httpStatus == 502 || httpStatus == 503
        || httpStatus == 504)
        return RetryTransient;
    if (httpStatus == 0
        && (e.contains(QStringLiteral("timeout"))
            || e.contains(QStringLiteral("timed out"))
            || e.contains(QStringLiteral("temporary"))
            || e.contains(QStringLiteral("connection reset"))
            || e.contains(QStringLiteral("remote host closed"))))
        return RetryTransient;
    return RetryNone;
}

void LlamaAgentBackend::forkSession(const QString &sessionId)
{
    forkSessionImpl(sessionId, -1);
}

void LlamaAgentBackend::forkSessionAtMessage(int msgIndex)
{
    forkSessionImpl(m_sessionId, msgIndex);
}

void LlamaAgentBackend::forkSessionImpl(const QString &sessionId, int msgIndex)
{
    if (sessionId.isEmpty() || isBusy()) {
        if (isBusy()) emit errorOccurred(QStringLiteral("No se puede bifurcar con un turno en curso."));
        return;
    }

    if (sessionId == m_sessionId) saveCurrentSession();

    QFile sourceFile(sessionFilePath(sessionId));
    if (!sourceFile.open(QIODevice::ReadOnly)) return;
    const QJsonObject source = QJsonDocument::fromJson(sourceFile.readAll()).object();
    sourceFile.close();
    QJsonArray messages = source.value(QStringLiteral("messages")).toArray();
    QJsonArray transcript = source.value(QStringLiteral("transcript")).toArray();
    if (transcript.isEmpty()) transcript = source.value(QStringLiteral("api")).toArray();
    QJsonArray api = source.value(QStringLiteral("workingContext")).toArray();
    if (api.isEmpty()) api = transcript;
    if (msgIndex >= 0) {
        if (msgIndex >= messages.size()) return;
        while (messages.size() > msgIndex + 1) messages.removeLast();
        // Una rama desde un mensaje usa un transcript textual válido. No copiamos
        // tool_calls parciales ni resultados posteriores al punto elegido.
        QJsonArray rebuilt;
        for (const QJsonValue &v : api) {
            if (v.toObject().value(QStringLiteral("role")).toString() == QLatin1String("system"))
                rebuilt.append(v);
            else
                break;
        }
        for (const QJsonValue &v : messages) {
            const QJsonObject m = v.toObject();
            const QString role = m.value(QStringLiteral("role")).toString();
            if (role == QLatin1String("user") || role == QLatin1String("assistant"))
                rebuilt.append(QJsonObject{{QStringLiteral("role"), role},
                                           {QStringLiteral("content"),
                                            m.value(QStringLiteral("content")).toString()}});
        }
        api = rebuilt;
        transcript = rebuilt;
    }

    QVariantMap parent;
    for (const QVariant &v : std::as_const(m_sessions)) {
        if (v.toMap().value(QStringLiteral("id")).toString() == sessionId) {
            parent = v.toMap();
            break;
        }
    }
    const QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVariantMap child = parent;
    child[QStringLiteral("id")] = newId;
    child[QStringLiteral("title")] =
        QStringLiteral("%1 · rama").arg(parent.value(QStringLiteral("title"),
                                                     QStringLiteral("Sesión")).toString());
    child[QStringLiteral("created")] =
        static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    child[QStringLiteral("parentSessionId")] = sessionId;
    child[QStringLiteral("forkMessageIndex")] = msgIndex;
    child[QStringLiteral("depth")] = parent.value(QStringLiteral("depth")).toInt() + 1;
    m_sessions.prepend(child);
    persistIndex();

    QJsonObject forked{
        {QStringLiteral("id"), newId},
        {QStringLiteral("title"), child.value(QStringLiteral("title")).toString()},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("api"), transcript},
        {QStringLiteral("transcript"), transcript},
        {QStringLiteral("workingContext"), api},
        {QStringLiteral("checkpoints"), QJsonArray{}},
        {QStringLiteral("snapshotVersion"), 2},
        {QStringLiteral("parentSessionId"), sessionId},
        {QStringLiteral("forkMessageIndex"), msgIndex}
    };
    QFile target(sessionFilePath(newId));
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_sessions.removeFirst();
        persistIndex();
        return;
    }
    target.write(QJsonDocument(forked).toJson());
    target.close();
    setCurrentSession(newId);
    emit logAppended(QStringLiteral("[sesión bifurcada: %1 → %2, mensaje=%3]\n")
                         .arg(sessionId, newId).arg(msgIndex));
}

void LlamaAgentBackend::deleteProject(const QString &projectDir)
{
    if (projectDir.isEmpty()) return;
    const QString target = QDir::cleanPath(projectDir);
    bool deletedCurrent = false;
    QVariantList kept;
    for (const QVariant &v : std::as_const(m_sessions)) {
        const QVariantMap s = v.toMap();
        const QString pd = QDir::cleanPath(s.value(QStringLiteral("projectDir")).toString());
        if (pd == target) {
            const QString sid = s.value(QStringLiteral("id")).toString();
            removeSessionFile(sid);
            if (sid == m_sessionId) deletedCurrent = true;
        } else {
            kept.append(v);
        }
    }
    if (kept.size() == m_sessions.size()) return;   // nada que borrar
    m_sessions = kept;
    if (deletedCurrent) {
        m_sessionId.clear();
        m_messages.clear();
        m_apiMessages = {};
        m_transcriptMessages = {};
        m_curAsstIdx = -1;
        // Si quedan sesiones de OTROS proyectos, abrir una. Si no queda ninguna,
        // dejar estado vacío: NO recrear sesión (recrear en el cwd borrado haría
        // reaparecer el proyecto). Se creará una al próximo mensaje/sesión nueva.
        if (!m_sessions.isEmpty())
            setCurrentSession(m_sessions.first().toMap().value(QStringLiteral("id")).toString());
        else
            emit messagesChanged();
    }
    persistIndex();
    emit sessionsChanged();
}

void LlamaAgentBackend::refreshSessions() { emit sessionsChanged(); }

void LlamaAgentBackend::setCurrentSession(const QString &sessionId)
{
    saveCurrentSession();   // vuelca la sesión actual antes de cambiar
    // La consolidación corre al cerrar turnos con recuperación, no al navegar.
    // Así abrir otra sesión nunca compite con el reset de la conversación visible.
    m_sessionId = sessionId;
    if (m_harnessEngineId == QLatin1String("next")) {
        configureHarnessEventLog();
        m_harnessEventLog.append(QStringLiteral("session/switch"),
                                 QJsonObject{{QStringLiteral("sessionId"), m_sessionId},
                                             {QStringLiteral("engine"), m_harnessEngineId}});
    }
    if (m_worker) QMetaObject::invokeMethod(m_worker, "setSessionId", Qt::QueuedConnection,
                                            Q_ARG(QString, m_sessionId));
    m_curAsstIdx = -1;
    m_messages.clear();
    m_apiMessages = {};
    m_transcriptMessages = {};
    m_readFingerprints.clear();
    m_checkpoints.clear();
    if (!m_msgQueue.isEmpty()) { m_msgQueue.clear(); emit queueChanged(); }

    QString snapshotProjectDir;
    QFile f(sessionFilePath(sessionId));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        snapshotProjectDir = obj.value(QStringLiteral("projectDir")).toString();
        m_apiMessages = obj.value(QStringLiteral("workingContext")).toArray();
        m_transcriptMessages = obj.value(QStringLiteral("transcript")).toArray();
        // Compatibilidad con snapshots v1: `api` era a la vez transcript y
        // contexto de trabajo.
        if (m_apiMessages.isEmpty())
            m_apiMessages = obj.value(QStringLiteral("api")).toArray();
        if (m_transcriptMessages.isEmpty())
            m_transcriptMessages = obj.value(QStringLiteral("api")).toArray();
        const QJsonObject stats = obj.value(QStringLiteral("contextStats")).toObject();
        m_contextPrunedMessages = stats.value(QStringLiteral("prunedMessages")).toInt();
        m_contextPrunedTokens = static_cast<qint64>(
            stats.value(QStringLiteral("prunedTokens")).toDouble());
        const QJsonArray msgs = obj.value(QStringLiteral("messages")).toArray();
        for (const QJsonValue &mv : msgs) {
            QVariantMap mm = mv.toObject().toVariantMap();
            mm[QStringLiteral("typing")] = false;
            m_messages.append(mm);
        }
        restoreCheckpoints(obj.value(QStringLiteral("checkpoints")).toArray());
    }
    QString indexProjectDir;
    for (const QVariant &v : std::as_const(m_sessions)) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("id")).toString() == sessionId) {
            m_sessionTitle = s.value(QStringLiteral("title")).toString();
            indexProjectDir = s.value(QStringLiteral("projectDir")).toString();
            break;
        }
    }
    // El índice manda, pero si no tiene el dato (índice viejo o sesión que
    // nunca se indexó) vale el del snapshot. Sin esto la sesión hereda el cwd
    // del backend y el agente termina trabajando en otro proyecto.
    const QString previousCwd = m_cwd;
    QString restored = safeProjectDir(indexProjectDir);
    if (restored.isEmpty()) restored = safeProjectDir(snapshotProjectDir);
    if (!restored.isEmpty()) m_cwd = restored;
    if (m_cwd != previousCwd) refreshSystemPromptContext();
    // Si la sesión no tenía system prompt (vacía), garantizar uno.
    if (m_apiMessages.isEmpty())
        m_apiMessages = QJsonArray{ QJsonObject{
            {QStringLiteral("role"), QStringLiteral("system")},
            {QStringLiteral("content"), buildSystemPrompt()}} };
    if (m_transcriptMessages.isEmpty()) m_transcriptMessages = m_apiMessages;
    pruneEmptySessions(sessionId);
    emit sessionsChanged();
    emit messagesChanged();
}

// ───────────────────────────── Persistencia a disco ──────────────────────
QString LlamaAgentBackend::storageDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/")
                        + HarnessEngine::storageNamespace(m_harnessEngineId);
    QDir().mkpath(dir);
    return dir;
}

QString LlamaAgentBackend::sessionFilePath(const QString &sessionId) const
{
    return storageDir() + QStringLiteral("/") + sessionId + QStringLiteral(".json");
}

void LlamaAgentBackend::loadFromDisk()
{
    if (m_ephemeralSessions) return;
    if (!m_sessions.isEmpty()) return;
    QJsonArray arr;
    QFile f(storageDir() + QStringLiteral("/index.json"));
    if (f.open(QIODevice::ReadOnly)) {
        arr = QJsonDocument::fromJson(f.readAll()).array();
        f.close();
    }
    // Las corridas de benchmark crean workspaces aislados llamados
    // "<perfil>__ws" o "<perfil>__ws_pN". Builds viejas (pre-ephemeral) las
    // persistieron en este índice global y ensucian el sidebar del Agente.
    // Filtrarlas al cargar y purgar el índice si había alguna.
    static const QRegularExpression benchWs(QStringLiteral("__ws(_p\\d+)?$"));
    bool indexChanged = false;
    for (const QJsonValue &v : arr) {
        QVariantMap s = v.toObject().toVariantMap();
        const QString sid = s.value(QStringLiteral("id")).toString();
        if (sid.isEmpty()) continue;
        const QString pn = s.value(QStringLiteral("projectName")).toString();
        if (benchWs.match(pn).hasMatch()) {
            removeSessionFile(sid);   // borrar también el .json huérfano
            indexChanged = true;
            continue;
        }
        // Migración de sesiones creadas por builds donde el autotítulo se
        // perdía: si ya existe un primer prompt, derivar el nombre del archivo.
        if (s.value(QStringLiteral("title")).toString().trimmed().isEmpty()
            || s.value(QStringLiteral("title")).toString() == QStringLiteral("Sesión")) {
            QFile sessionFile(sessionFilePath(sid));
            if (sessionFile.open(QIODevice::ReadOnly)) {
                const QJsonArray messages = QJsonDocument::fromJson(sessionFile.readAll())
                                                .object().value(QStringLiteral("messages")).toArray();
                sessionFile.close();
                for (const QJsonValue &messageValue : messages) {
                    const QJsonObject message = messageValue.toObject();
                    if (message.value(QStringLiteral("role")).toString() != QLatin1String("user"))
                        continue;
                    const QString title = suggestSessionTitle(
                        message.value(QStringLiteral("content")).toString());
                    if (title != QStringLiteral("Sesión")) {
                        s[QStringLiteral("title")] = title;
                        indexChanged = true;
                    }
                    break;
                }
            }
        }
        m_sessions.append(s);
    }
    // Reconstrucción: snapshots en disco que el índice no lista (índice viejo,
    // perdido, o sesión guardada por un runtime paralelo). Sin esto la sesión
    // desaparece del panel y, peor, pierde su projectDir.
    QSet<QString> indexed;
    for (const QVariant &v : std::as_const(m_sessions))
        indexed.insert(v.toMap().value(QStringLiteral("id")).toString());
    const QFileInfoList snapshots = QDir(storageDir()).entryInfoList(
        QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    for (const QFileInfo &fi : snapshots) {
        const QString sid = fi.completeBaseName();
        if (sid == QLatin1String("index") || indexed.contains(sid)) continue;
        QFile snapshot(fi.absoluteFilePath());
        if (!snapshot.open(QIODevice::ReadOnly)) continue;
        const QJsonObject obj = QJsonDocument::fromJson(snapshot.readAll()).object();
        snapshot.close();
        if (obj.value(QStringLiteral("id")).toString() != sid) continue;
        if (obj.value(QStringLiteral("messages")).toArray().isEmpty()) continue;
        const QString projectDir = obj.value(QStringLiteral("projectDir")).toString();
        if (benchWs.match(QFileInfo(projectDir).fileName()).hasMatch()) continue;
        QVariantMap recovered;
        recovered[QStringLiteral("id")] = sid;
        recovered[QStringLiteral("title")] = obj.value(QStringLiteral("title")).toString();
        recovered[QStringLiteral("created")] =
            static_cast<double>(fi.birthTime().isValid()
                                    ? fi.birthTime().toMSecsSinceEpoch()
                                    : fi.lastModified().toMSecsSinceEpoch());
        recovered[QStringLiteral("projectDir")] = projectDir;
        recovered[QStringLiteral("projectName")] =
            obj.value(QStringLiteral("projectName")).toString();
        recovered[QStringLiteral("projectId")] = QString();
        m_sessions.append(recovered);
        indexed.insert(sid);
        indexChanged = true;
    }
    if (indexChanged) {
        std::sort(m_sessions.begin(), m_sessions.end(),
                  [](const QVariant &a, const QVariant &b) {
                      return a.toMap().value(QStringLiteral("created")).toDouble()
                          > b.toMap().value(QStringLiteral("created")).toDouble();
                  });
        persistIndex();
    }
    // Sesiones sin ningún mensaje que quedaron de corridas anteriores: no sobreviven.
    pruneEmptySessions(QString());
    if (m_sessions.isEmpty()) return;
    // Cargar la primera como activa.
    const QString sid = m_sessions.first().toMap().value(QStringLiteral("id")).toString();
    setCurrentSession(sid);
}

void LlamaAgentBackend::persistIndex() const
{
    if (m_ephemeralSessions) return;
    QJsonArray arr;
    for (const QVariant &v : m_sessions)
        arr.append(QJsonObject::fromVariantMap(v.toMap()));
    QFile f(storageDir() + QStringLiteral("/index.json"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(arr).toJson());
        f.close();
    }
}

void LlamaAgentBackend::persistSession(const QString &sessionId) const
{
    if (m_ephemeralSessions) return;
    if (sessionId.isEmpty() || sessionId != m_sessionId) return;  // solo la activa tiene datos en RAM
    QString title;
    QString projectDir = m_cwd;
    for (const QVariant &v : m_sessions) {
        const QVariantMap s = v.toMap();
        if (s.value(QStringLiteral("id")).toString() == sessionId) {
            title = s.value(QStringLiteral("title")).toString();
            const QString pd = s.value(QStringLiteral("projectDir")).toString();
            if (!pd.isEmpty()) projectDir = pd;
            break;
        }
    }
    QJsonArray msgs;
    for (const QVariant &mv : m_messages) {
        QVariantMap m = mv.toMap();
        m.remove(QStringLiteral("typing"));
        msgs.append(QJsonObject::fromVariantMap(m));
    }
    QJsonObject obj{
        {QStringLiteral("id"), sessionId},
        {QStringLiteral("title"), title},
        // El cwd vive también en el snapshot: si el índice se pierde o queda
        // viejo, la sesión sigue sabiendo sobre qué proyecto trabajaba.
        {QStringLiteral("projectDir"), projectDir},
        {QStringLiteral("projectName"), QFileInfo(projectDir).fileName()},
        {QStringLiteral("messages"), msgs},
        {QStringLiteral("api"), m_transcriptMessages}, // compatibilidad con v1
        {QStringLiteral("transcript"), m_transcriptMessages},
        {QStringLiteral("workingContext"), m_apiMessages},
        {QStringLiteral("contextStats"), QJsonObject{
            {QStringLiteral("prunedMessages"), m_contextPrunedMessages},
            {QStringLiteral("prunedTokens"), static_cast<double>(m_contextPrunedTokens)}}},
        {QStringLiteral("checkpoints"), checkpointsToJson()},
        {QStringLiteral("harness"), QJsonObject{
            {QStringLiteral("engine"), m_harnessEngineId},
            {QStringLiteral("version"), m_harnessEngineVersion},
            {QStringLiteral("profileId"), m_harnessProfileId},
            {QStringLiteral("specHash"), m_harnessSpecHash}}},
        {QStringLiteral("snapshotVersion"), 2}
    };
    QFile f(sessionFilePath(sessionId));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(obj).toJson());
        f.close();
    }
}

void LlamaAgentBackend::saveCurrentSession()
{
    if (m_ephemeralSessions) return;
    if (m_sessionId.isEmpty()) return;
    persistSession(m_sessionId);
}

void LlamaAgentBackend::persistAll() const
{
    if (m_ephemeralSessions) return;
    persistIndex();
    persistSession(m_sessionId);
}

void LlamaAgentBackend::removeSessionFile(const QString &sessionId) const
{
    if (!sessionId.isEmpty()) QFile::remove(sessionFilePath(sessionId));
}
