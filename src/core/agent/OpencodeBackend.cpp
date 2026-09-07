#include "OpencodeBackend.h"
#include "AgentLifecycle.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QSettings>
#include <QDateTime>
#include <QUuid>
#include <algorithm>

static int estimateTokens(const QString &text)
{
    const int n = text.trimmed().size();
    if (n <= 0) return 0;
    return (n + 3) / 4;
}

OpencodeBackend::OpencodeBackend(QObject *parent) : IAgentBackend(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

OpencodeBackend::~OpencodeBackend()
{
    if (m_proc) { m_proc->kill(); m_proc->deleteLater(); }
}

bool OpencodeBackend::running() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

QString OpencodeBackend::currentProjectDir() const
{
    return m_proc ? m_proc->workingDirectory() : QString();
}

QString OpencodeBackend::projectNameFromDir(const QString &dir)
{
    const QStringList parts = QDir::toNativeSeparators(dir).split(QDir::separator(), Qt::SkipEmptyParts);
    if (parts.size() >= 2) return parts[parts.size()-2] + QStringLiteral("/") + parts.last();
    return parts.isEmpty() ? QStringLiteral("(sin proyecto)") : parts.last();
}

QVariantList &OpencodeBackend::messagesForSession(const QString &sessionId)
{
    return m_sessionMessages[sessionId];
}

int &OpencodeBackend::assistantIndexForSession(const QString &sessionId)
{
    return m_sessionAssistantIndices[sessionId];
}

void OpencodeBackend::publishSessionMessages(const QString &sessionId)
{
    if (sessionId != m_sessionId) return;
    m_messages = messagesForSession(sessionId);
    m_curAsstIdx = assistantIndexForSession(sessionId);
    emit messagesChanged();
}

void OpencodeBackend::start(const AgentContext &ctx)
{
    if (running()) return;
    m_ctx = ctx;
    m_stopping = false;
    m_correlationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    launchProcess();
}

void OpencodeBackend::emitSessionLifecycle()
{
    if (m_sessionId.isEmpty() || m_sessionId == m_lifecycleSessionId) return;
    m_lifecycleSessionId = m_sessionId;
    emit agentLifecycleEvent(AgentLifecycle::sessionStart(
        m_sessionId, currentProjectDir(), lifecycleCorrelation(m_sessionId),
        m_ctx.harnessProfileId.isEmpty() ? m_ctx.launchProfileId : m_ctx.harnessProfileId,
        QStringLiteral("opencode"), 1));
}

QString OpencodeBackend::lifecycleCorrelation(const QString &sessionId) const
{
    return m_sessionCorrelations.value(sessionId, m_correlationId);
}

QString OpencodeBackend::compactJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void OpencodeBackend::emitToolRequest(const QString &sessionId, const QString &callId,
                                      const QString &tool, const QString &arguments,
                                      const QStringList &paths)
{
    if (sessionId.isEmpty() || callId.isEmpty()) return;
    const QString key = sessionId + QLatin1Char(':') + callId;
    if (m_lifecycleRequested.contains(key)) return;
    m_lifecycleRequested.insert(key);
    m_lifecycleTools.insert(key, {{QStringLiteral("sessionId"), sessionId},
                                  {QStringLiteral("callId"), callId},
                                  {QStringLiteral("tool"), tool},
                                  {QStringLiteral("arguments"), arguments},
                                  {QStringLiteral("paths"), paths}});
    emit agentLifecycleEvent(AgentLifecycle::toolEvent(
        QStringLiteral("tool.request"), sessionId, currentProjectDir(),
        lifecycleCorrelation(sessionId), callId, tool, arguments, paths));
}

void OpencodeBackend::emitToolStart(const QString &sessionId, const QString &callId,
                                    const QString &tool, const QString &arguments,
                                    const QStringList &paths)
{
    if (sessionId.isEmpty() || callId.isEmpty()) return;
    const QString key = sessionId + QLatin1Char(':') + callId;
    emitToolRequest(sessionId, callId, tool, arguments, paths);
    if (m_lifecycleStarted.contains(key) || m_lifecycleFinished.contains(key)) return;
    m_lifecycleStarted.insert(key);
    emit agentLifecycleEvent(AgentLifecycle::toolEvent(
        QStringLiteral("tool.start"), sessionId, currentProjectDir(),
        lifecycleCorrelation(sessionId), callId, tool, arguments, paths));
}

void OpencodeBackend::emitToolFinish(const QString &sessionId, const QString &callId,
                                     const QString &tool, bool ok, const QString &result,
                                     const QStringList &paths)
{
    if (sessionId.isEmpty() || callId.isEmpty()) return;
    const QString key = sessionId + QLatin1Char(':') + callId;
    emitToolRequest(sessionId, callId, tool, {}, paths);
    if (m_lifecycleFinished.contains(key)) return;
    m_lifecycleFinished.insert(key);
    const QString kind = toolKind(tool);
    emit agentLifecycleEvent(AgentLifecycle::toolResult(
        sessionId, currentProjectDir(), lifecycleCorrelation(sessionId), callId,
        tool, ok, kind == QLatin1String("write"), kind == QLatin1String("shell"),
        result, paths));
}

void OpencodeBackend::finishOpenLifecycleTools(const QString &sessionId, const QString &reason)
{
    const auto open = m_lifecycleTools;
    for (auto it = open.cbegin(); it != open.cend(); ++it) {
        if (m_lifecycleFinished.contains(it.key())) continue;
        const QVariantMap data = it.value();
        if (!sessionId.isEmpty()
            && data.value(QStringLiteral("sessionId")).toString() != sessionId)
            continue;
        emitToolFinish(data.value(QStringLiteral("sessionId")).toString(),
                       data.value(QStringLiteral("callId")).toString(),
                       data.value(QStringLiteral("tool")).toString(), false,
                       reason, data.value(QStringLiteral("paths")).toStringList());
    }
}

void OpencodeBackend::launchProcess()
{
    // Liberar el puerto 4096 si quedó ocupado por un server previo.
#ifdef Q_OS_WIN
    QProcess::execute(QStringLiteral("cmd"),
        {QStringLiteral("/c"),
         QStringLiteral("for /f \"tokens=5\" %a in ('netstat -ano ^| findstr :4096 ^| findstr LISTENING') do taskkill /PID %a /F")});
#else
    QProcess::execute(QStringLiteral("sh"),
        {QStringLiteral("-c"), QStringLiteral("fuser -k 4096/tcp 2>/dev/null || true")});
#endif

    m_proc = new QProcess(this);
    m_proc->setProcessEnvironment(m_ctx.env);
    if (!m_ctx.cwd.isEmpty() && QFileInfo(m_ctx.cwd).isDir())
        m_proc->setWorkingDirectory(m_ctx.cwd);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        if (!m_proc) return;
        const QString chunk = QString::fromUtf8(m_proc->readAllStandardOutput());
        emit logAppended(chunk);
        if (m_sessionId.isEmpty() && chunk.contains(QLatin1String("server listening")))
            initSession();
    });
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_proc) return;
        const QString chunk = QString::fromUtf8(m_proc->readAllStandardError());
        emit logAppended(chunk);
        if (m_sessionId.isEmpty() && chunk.contains(QLatin1String("server listening")))
            initSession();
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        finishOpenLifecycleTools(QString(), QStringLiteral("[OpenCode finalizó el proceso]"));
        emit logAppended(QStringLiteral("\n[agent exited with code %1]\n").arg(code));
        if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }
        if (m_eventReply) { m_eventReply->abort(); m_eventReply->deleteLater(); m_eventReply = nullptr; }
        m_sessionId.clear();
        m_lifecycleSessionId.clear();
        m_messages.clear();
        m_curAsstIdx = -1;
        m_sessionMessages.clear();
        m_sessionAssistantIndices.clear();
        emit messagesChanged();
        m_stopping = false;
        emit runningChanged();
    });

    const QStringList args{
        QStringLiteral("serve"),
        QStringLiteral("--hostname"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QStringLiteral("4096")
    };
    emit logAppended(QStringLiteral("[opencode headless server mode]\n"));
    if (!m_ctx.cwd.isEmpty())
        emit logAppended(QStringLiteral("[cwd: %1]\n").arg(QDir::toNativeSeparators(m_ctx.cwd)));
    m_proc->start(m_ctx.exePath, args);
    if (!m_proc->waitForStarted(5000)) {
        emit logAppended(QStringLiteral("[Error: no se pudo iniciar opencode]\n"));
        m_proc->deleteLater();
        m_proc = nullptr;
        return;
    }
    emit runningChanged();
}

void OpencodeBackend::stop()
{
    m_stopping = true;
    finishOpenLifecycleTools(QString(), QStringLiteral("[OpenCode detenido por el usuario]"));
    if (m_eventReply) { m_eventReply->abort(); m_eventReply->deleteLater(); m_eventReply = nullptr; }
    m_sessionId.clear();
    m_lifecycleSessionId.clear();
    if (!m_proc) { emit runningChanged(); return; }
#ifdef Q_OS_WIN
    const qint64 pid = m_proc->processId();
    if (pid > 0)
        QProcess::execute(QStringLiteral("taskkill"),
            {QStringLiteral("/PID"), QString::number(pid),
             QStringLiteral("/T"), QStringLiteral("/F")});
#endif
    m_proc->terminate();
    if (!m_proc->waitForFinished(2000))
        m_proc->kill();
}

void OpencodeBackend::initSession()
{
    loadSessionList([this]() { resumeOrCreateSession(); });
}

void OpencodeBackend::loadSessionList(std::function<void()> then)
{
    auto *reply = m_nam->get(QNetworkRequest(QUrl(m_attachUrl + QStringLiteral("/session"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, then]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
            QVector<QJsonObject> sorted;
            sorted.reserve(arr.size());
            for (const QJsonValue &v : arr) sorted.append(v.toObject());
            std::sort(sorted.begin(), sorted.end(), [](const QJsonObject &a, const QJsonObject &b) {
                return a.value(QStringLiteral("time")).toObject().value(QStringLiteral("created")).toDouble()
                     > b.value(QStringLiteral("time")).toObject().value(QStringLiteral("created")).toDouble();
            });
            m_sessions.clear();
            for (const QJsonObject &s : sorted) {
                const QString dir = s.value(QStringLiteral("directory")).toString();
                AgentSession e;
                e.id          = s.value(QStringLiteral("id")).toString();
                e.title       = s.value(QStringLiteral("title")).toString();
                e.created     = s.value(QStringLiteral("time")).toObject().value(QStringLiteral("created")).toDouble();
                e.projectId   = s.value(QStringLiteral("projectID")).toString();
                e.projectName = projectNameFromDir(dir);
                e.projectDir  = dir;
                m_sessions.append(e.toMap());
            }
            emit sessionsChanged();
        }
        if (then) then();
    });
}

void OpencodeBackend::resumeOrCreateSession()
{
    if (m_forceNew) { m_forceNew = false; doCreateSession(); return; }
    QSettings st;
    const QString savedId = st.value(QStringLiteral("opencode/lastSessionId")).toString();
    if (!savedId.isEmpty()) {
        for (const QVariant &v : std::as_const(m_sessions)) {
            if (v.toMap().value(QStringLiteral("id")).toString() == savedId) {
                m_sessionId = savedId;
                m_sessionTitle = v.toMap().value(QStringLiteral("title")).toString();
                emitSessionLifecycle();
                emit sessionsChanged();
                emit logAppended(QStringLiteral("[opencode session resumed]\n"));
                loadSessionMessages(savedId);
                subscribeEvents();
                return;
            }
        }
    }
    doCreateSession();
}

void OpencodeBackend::doCreateSession()
{
    QNetworkRequest req(QUrl(m_attachUrl + QStringLiteral("/session")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    auto *reply = m_nam->post(req, QByteArrayLiteral("{}"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit logAppended(QStringLiteral("[error: failed to create opencode session: %1]\n")
                                 .arg(reply->errorString()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_sessionId    = obj.value(QStringLiteral("id")).toString();
        m_sessionTitle = obj.value(QStringLiteral("title")).toString();
        emitSessionLifecycle();
        QSettings().setValue(QStringLiteral("opencode/lastSessionId"), m_sessionId);
        const QString cwd = currentProjectDir();
        AgentSession e;
        e.id          = m_sessionId;
        e.title       = m_sessionTitle;
        e.created     = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
        e.projectId   = obj.value(QStringLiteral("projectID")).toString();
        e.projectName = projectNameFromDir(cwd);
        e.projectDir  = cwd;
        m_sessions.prepend(e.toMap());
        m_sessionMessages.insert(m_sessionId, {});
        m_sessionAssistantIndices.insert(m_sessionId, -1);
        emit sessionsChanged();
        emit logAppended(QStringLiteral("[opencode session ready]\n"));
        subscribeEvents();
    });
}

void OpencodeBackend::loadSessionMessages(const QString &sessionId)
{
    const QUrl url(m_attachUrl + QStringLiteral("/session/") + sessionId + QStringLiteral("/message"));
    auto *reply = m_nam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, sessionId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const QJsonArray msgs = QJsonDocument::fromJson(reply->readAll()).array();
        QVariantList loaded;
        for (const QJsonValue &mv : msgs) {
            const QJsonObject msg = mv.toObject();
            const QString role = msg.value(QStringLiteral("info")).toObject().value(QStringLiteral("role")).toString();
            QString text;
            for (const QJsonValue &pv : msg.value(QStringLiteral("parts")).toArray()) {
                const QJsonObject part = pv.toObject();
                if (part.value(QStringLiteral("type")).toString() == QLatin1String("text"))
                    text += part.value(QStringLiteral("text")).toString();
            }
            if (role.isEmpty() || text.isEmpty()) continue;
            AgentMessage e; e.role = role; e.content = text; e.typing = false;
            QVariantMap mm = e.toMap();
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            mm[QStringLiteral("createdAt")] = static_cast<double>(now);
            mm[QStringLiteral("completedAt")] = static_cast<double>(now);
            mm[QStringLiteral("elapsedMs")] = 0;
            mm[QStringLiteral("tokens")] = estimateTokens(text);
            mm[QStringLiteral("tps")] = 0.0;
            loaded.append(mm);
        }
        m_sessionMessages.insert(sessionId, loaded);
        m_sessionAssistantIndices.insert(sessionId, -1);
        publishSessionMessages(sessionId);
    });
}

void OpencodeBackend::sendMessage(const QString &text)
{
    if (!running()) return;
    emit logAppended(QStringLiteral("> %1\n").arg(text));

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    AgentMessage um; um.role = QStringLiteral("user"); um.content = text;
    QVariantMap umMap = um.toMap();
    umMap[QStringLiteral("createdAt")] = static_cast<double>(nowMs);
    umMap[QStringLiteral("completedAt")] = static_cast<double>(nowMs);
    umMap[QStringLiteral("elapsedMs")] = 0;
    umMap[QStringLiteral("tokens")] = estimateTokens(text);
    umMap[QStringLiteral("tps")] = 0.0;
    const QString sessionId = m_sessionId;
    QVariantList &messages = messagesForSession(sessionId);
    messages.append(umMap);
    AgentMessage am; am.role = QStringLiteral("assistant"); am.typing = true;
    QVariantMap amMap = am.toMap();
    amMap[QStringLiteral("createdAt")] = static_cast<double>(nowMs);
    amMap[QStringLiteral("elapsedMs")] = 0;
    amMap[QStringLiteral("tokens")] = 0;
    amMap[QStringLiteral("tps")] = 0.0;
    messages.append(amMap);
    assistantIndexForSession(sessionId) = messages.size() - 1;
    publishSessionMessages(sessionId);

    if (m_sessionId.isEmpty()) {
        emit logAppended(QStringLiteral("[waiting: opencode session not ready yet]\n"));
        return;
    }
    m_correlationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sessionCorrelations[sessionId] = m_correlationId;
    emit agentLifecycleEvent(AgentLifecycle::promptSubmit(
        sessionId, currentProjectDir(), m_correlationId, text, 0));
    QNetworkRequest req(QUrl(m_attachUrl + QStringLiteral("/session/") + sessionId
                             + QStringLiteral("/prompt_async")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    const QJsonObject partObj{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}};
    const QJsonObject payload{{QStringLiteral("parts"), QJsonArray{partObj}}};
    auto *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit logAppended(QStringLiteral("[error sending message: %1]\n").arg(reply->errorString()));
        }
    });
}

void OpencodeBackend::newSession()
{
    if (!running()) return;
    m_messages.clear();
    m_curAsstIdx = -1;
    m_sessionId.clear();
    m_sessionTitle.clear();
    emit messagesChanged();
    emit sessionsChanged();
    doCreateSession();
}

void OpencodeBackend::newSessionInProject(const QString &projectDir)
{
    if (projectDir.isEmpty()) { newSession(); return; }
    if (QDir::cleanPath(currentProjectDir()) == QDir::cleanPath(projectDir)) {
        newSession();
        return;
    }
    // Reiniciar el server en el nuevo cwd y forzar sesión nueva.
    m_forceNew = true;
    m_ctx.cwd = projectDir;
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(this, &IAgentBackend::runningChanged, this, [this, conn]() {
        if (running()) return;
        QObject::disconnect(*conn);
        QTimer::singleShot(300, this, [this]() { launchProcess(); });
    });
    stop();
}

void OpencodeBackend::switchSession(const QString &sessionId)
{
    if (sessionId == m_sessionId) return;
    m_sessionId = sessionId;
    m_messages = messagesForSession(sessionId);
    m_curAsstIdx = assistantIndexForSession(sessionId);
    for (const QVariant &v : std::as_const(m_sessions)) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == sessionId) {
            m_sessionTitle = m.value(QStringLiteral("title")).toString();
            break;
        }
    }
    QSettings().setValue(QStringLiteral("opencode/lastSessionId"), sessionId);
    emit sessionsChanged();
    emit messagesChanged();
    // Si ya llegó streaming para esta sesión, su cache es la fuente de verdad;
    // una lectura HTTP tardía no debe pisar la respuesta que sigue en curso.
    if (!m_sessionMessages.contains(sessionId))
        loadSessionMessages(sessionId);
}

void OpencodeBackend::refreshSessions()
{
    loadSessionList(nullptr);
}

void OpencodeBackend::renameSession(const QString &sessionId, const QString &title)
{
    const QString t = title.trimmed();
    if (sessionId.isEmpty() || t.isEmpty()) return;
    QNetworkRequest req(QUrl(m_attachUrl + QStringLiteral("/session/") + sessionId));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("title"), t}}).toJson(QJsonDocument::Compact);
    auto *reply = m_nam->sendCustomRequest(req, QByteArrayLiteral("PATCH"), body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, sessionId, t]() {
        reply->deleteLater();
        for (int i = 0; i < m_sessions.size(); ++i) {
            QVariantMap m = m_sessions[i].toMap();
            if (m.value(QStringLiteral("id")).toString() == sessionId) {
                m[QStringLiteral("title")] = t; m_sessions[i] = m; break;
            }
        }
        if (sessionId == m_sessionId) m_sessionTitle = t;
        emit sessionsChanged();
        loadSessionList(nullptr);
    });
}

void OpencodeBackend::deleteSession(const QString &sessionId)
{
    if (sessionId.isEmpty()) return;
    QNetworkRequest req(QUrl(m_attachUrl + QStringLiteral("/session/") + sessionId));
    auto *reply = m_nam->deleteResource(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, sessionId]() {
        reply->deleteLater();
        for (int i = 0; i < m_sessions.size(); ++i) {
            if (m_sessions[i].toMap().value(QStringLiteral("id")).toString() == sessionId) {
                m_sessions.removeAt(i); break;
            }
        }
        if (sessionId == m_sessionId) {
            m_sessionId.clear(); m_sessionTitle.clear();
            m_messages.clear(); m_curAsstIdx = -1;
            QSettings().remove(QStringLiteral("opencode/lastSessionId"));
            emit messagesChanged();
        }
        emit sessionsChanged();
        loadSessionList(nullptr);
    });
}

void OpencodeBackend::forkSession(const QString &sessionId)
{
    if (sessionId.isEmpty()) return;
    QNetworkRequest req(QUrl(m_attachUrl + QStringLiteral("/session")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("parentID"), sessionId}}).toJson(QJsonDocument::Compact);
    auto *reply = m_nam->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit logAppended(QStringLiteral("[error fork sesión: %1]\n").arg(reply->errorString()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const QString newId = obj.value(QStringLiteral("id")).toString();
        loadSessionList([this, newId]() { if (!newId.isEmpty()) switchSession(newId); });
    });
}

void OpencodeBackend::subscribeEvents()
{
    // Es un único stream global que OpenCode multiplexa por sessionID. Crear
    // otro al abrir una sesión reemplazaba m_eventReply y dejaba la respuesta
    // previa sin consumidor efectivo.
    if (m_eventReply) return;
    QNetworkRequest req(QUrl(m_attachUrl + QStringLiteral("/event")));
    req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("text/event-stream"));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    m_eventReply = m_nam->get(req);

    connect(m_eventReply, &QNetworkReply::readyRead, this, [this]() {
        if (!m_eventReply) return;
        const QByteArray data = m_eventReply->readAll();
        for (const QByteArray &raw : data.split('\n')) {
            const QByteArray line = raw.trimmed();
            if (!line.startsWith("data: ")) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(line.mid(6));
            if (doc.isNull()) continue;
            const QJsonObject obj = doc.object();
            const QString type = obj.value(QStringLiteral("type")).toString();
            const QJsonObject props = obj.value(QStringLiteral("properties")).toObject();
            // OpenCode multiplexa las sesiones por el mismo SSE. Nunca usar la
            // sesión visible como destino de un evento: durante una generación
            // el usuario puede crear o abrir otra sesión.
            const QString eventSessionId = props.value(QStringLiteral("sessionID")).toString();
            if (type == QLatin1String("message.part.updated")) {
                QJsonObject part = props.value(QStringLiteral("part")).toObject();
                if (part.isEmpty()) part = props;
                const QString partSessionId = part.value(QStringLiteral("sessionID")).toString().isEmpty()
                    ? eventSessionId : part.value(QStringLiteral("sessionID")).toString();
                const QString callId = part.value(QStringLiteral("callID")).toString(
                    part.value(QStringLiteral("callId")).toString());
                const QString tool = part.value(QStringLiteral("tool")).toString(
                    part.value(QStringLiteral("name")).toString());
                if (part.value(QStringLiteral("type")).toString() == QLatin1String("tool")
                    && !partSessionId.isEmpty() && !callId.isEmpty()) {
                    QJsonObject state = part.value(QStringLiteral("state")).toObject();
                    QJsonObject input = state.value(QStringLiteral("input")).toObject();
                    if (input.isEmpty()) input = part.value(QStringLiteral("input")).toObject();
                    const QString args = compactJson(input);
                    const QStringList paths = AgentLifecycle::changedPathsFromToolInput(tool, input);
                    emitToolRequest(partSessionId, callId, tool, args, paths);
                    const QString status = state.value(QStringLiteral("status")).toString(
                        part.value(QStringLiteral("status")).toString()).toLower();
                    if (status == QLatin1String("running") || status == QLatin1String("calling")
                        || status == QLatin1String("completed") || status == QLatin1String("error"))
                        emitToolStart(partSessionId, callId, tool, args, paths);
                    if (status == QLatin1String("completed") || status == QLatin1String("error")) {
                        const QString result = state.value(QStringLiteral("output")).toString(
                            state.value(QStringLiteral("error")).toString(
                                part.value(QStringLiteral("output")).toString()));
                        emitToolFinish(partSessionId, callId, tool,
                                       status == QLatin1String("completed"), result, paths);
                    }
                }
            } else if (type == QLatin1String("message.part.delta")) {
                if (props.value(QStringLiteral("field")).toString() == QLatin1String("text")) {
                    const QString delta = props.value(QStringLiteral("delta")).toString();
                    if (!delta.isEmpty()) {
                        emit logAppended(delta);
                        const int index = assistantIndexForSession(eventSessionId);
                        QVariantList &messages = messagesForSession(eventSessionId);
                        if (!eventSessionId.isEmpty() && index >= 0 && index < messages.size()) {
                            auto msg = messages[index].toMap();
                            const QString content = msg[QStringLiteral("content")].toString() + delta;
                            msg[QStringLiteral("content")] = content;
                            const qint64 startedAt = static_cast<qint64>(msg.value(QStringLiteral("createdAt")).toDouble());
                            const qint64 elapsedMs = qMax<qint64>(0, QDateTime::currentMSecsSinceEpoch() - startedAt);
                            const int toks = estimateTokens(content);
                            msg[QStringLiteral("tokens")] = toks;
                            msg[QStringLiteral("elapsedMs")] = static_cast<int>(elapsedMs);
                            msg[QStringLiteral("tps")] = (elapsedMs > 0 && toks > 0)
                                ? (1000.0 * static_cast<double>(toks) / static_cast<double>(elapsedMs))
                                : 0.0;
                            messages[index] = msg;
                            publishSessionMessages(eventSessionId);
                        }
                    }
                }
            } else if (type == QLatin1String("session.updated")) {
                const QJsonObject info = props.value(QStringLiteral("info")).toObject();
                const QString title = info.value(QStringLiteral("title")).toString();
                const QString sid   = info.value(QStringLiteral("id")).toString();
                if (!title.isEmpty() && sid == m_sessionId) {
                    m_sessionTitle = title;
                    for (int i = 0; i < m_sessions.size(); ++i) {
                        auto sm = m_sessions[i].toMap();
                        if (sm.value(QStringLiteral("id")).toString() == sid) {
                            sm[QStringLiteral("title")] = title; m_sessions[i] = sm; break;
                        }
                    }
                    emit sessionsChanged();
                }
            } else if (type == QLatin1String("session.status")) {
                const QString status = props.value(QStringLiteral("status"))
                                            .toObject().value(QStringLiteral("type")).toString();
                if (status == QLatin1String("idle")) {
                    finishOpenLifecycleTools(eventSessionId, QStringLiteral(
                        "[OpenCode informó sesión idle antes de cerrar la tool]"));
                    emit logAppended(QStringLiteral("\n"));
                    const int index = assistantIndexForSession(eventSessionId);
                    QVariantList &messages = messagesForSession(eventSessionId);
                    if (!eventSessionId.isEmpty() && index >= 0 && index < messages.size()) {
                        auto msg = messages[index].toMap();
                        msg[QStringLiteral("typing")] = false;
                        const qint64 doneAt = QDateTime::currentMSecsSinceEpoch();
                        const qint64 startedAt = static_cast<qint64>(msg.value(QStringLiteral("createdAt")).toDouble());
                        const qint64 elapsedMs = qMax<qint64>(0, doneAt - startedAt);
                        const QString content = msg.value(QStringLiteral("content")).toString();
                        const int toks = estimateTokens(content);
                        msg[QStringLiteral("completedAt")] = static_cast<double>(doneAt);
                        msg[QStringLiteral("tokens")] = toks;
                        msg[QStringLiteral("elapsedMs")] = static_cast<int>(elapsedMs);
                        msg[QStringLiteral("tps")] = (elapsedMs > 0 && toks > 0)
                            ? (1000.0 * static_cast<double>(toks) / static_cast<double>(elapsedMs))
                            : 0.0;
                        messages[index] = msg;
                        publishSessionMessages(eventSessionId);
                    }
                    assistantIndexForSession(eventSessionId) = -1;
                    if (eventSessionId == m_sessionId) m_curAsstIdx = -1;
                }
            } else if (type == QLatin1String("permission.asked")) {
                const QString permId = props.value(QStringLiteral("id")).toString();
                const QString sid    = props.value(QStringLiteral("sessionID")).toString();
                const QString ptype  = props.value(QStringLiteral("type")).toString();
                const QString kind   = toolKind(ptype);
                const QJsonObject meta = props.value(QStringLiteral("metadata")).toObject();
                const QString args = compactJson(meta);
                const QStringList paths = AgentLifecycle::changedPathsFromToolInput(ptype, meta);
                emitToolRequest(sid, permId, ptype, args, paths);

                // Política de aprobación.
                const bool autoAll  = (m_approvalMode == QLatin1String("auto"));
                const bool askAll   = (m_approvalMode == QLatin1String("manual"));
                const bool autoRead = (m_approvalMode == QLatin1String("ask")
                                       && kind == QLatin1String("read"));
                if (autoAll || autoRead) {
                    emitToolStart(sid, permId, ptype, args, paths);
                    respondPermission(sid, permId, QStringLiteral("always"));
                } else {
                    Q_UNUSED(askAll)
                    // Pedir al usuario: guardar pendiente + emitir señal con detalle.
                    m_pendingPerm.insert(permId, sid);
                    QString detail = meta.value(QStringLiteral("command")).toString();
                    if (detail.isEmpty()) detail = meta.value(QStringLiteral("filePath")).toString();
                    if (detail.isEmpty()) detail = meta.value(QStringLiteral("filepath")).toString();
                    if (detail.isEmpty()) detail = meta.value(QStringLiteral("url")).toString();
                    const QString diff = meta.value(QStringLiteral("diff")).toString();
                    emit toolApprovalNeeded(QVariantMap{
                        {QStringLiteral("id"),      permId},
                        {QStringLiteral("sessionId"), sid},
                        {QStringLiteral("tool"),    ptype},
                        {QStringLiteral("kind"),    kind},
                        {QStringLiteral("title"),   props.value(QStringLiteral("title")).toString()},
                        {QStringLiteral("detail"),  detail},
                        {QStringLiteral("diff"),    diff}
                    });
                }
            } else if (type.contains(QLatin1String("error"))) {
                const QString errMsg = props.value(QStringLiteral("message")).toString();
                if (!errMsg.isEmpty()) {
                    const QString callId = props.value(QStringLiteral("callID")).toString(
                        props.value(QStringLiteral("callId")).toString());
                    if (!callId.isEmpty())
                        emitToolFinish(eventSessionId, callId,
                                       props.value(QStringLiteral("tool")).toString(), false,
                                       errMsg, {});
                    emit logAppended(QStringLiteral("[error: %1]\n").arg(errMsg));
                    const int index = assistantIndexForSession(eventSessionId);
                    QVariantList &messages = messagesForSession(eventSessionId);
                    if (!eventSessionId.isEmpty() && index >= 0 && index < messages.size()) {
                        auto msg = messages[index].toMap();
                        msg[QStringLiteral("content")] = QStringLiteral("[error: %1]").arg(errMsg);
                        msg[QStringLiteral("typing")] = false;
                        const qint64 doneAt = QDateTime::currentMSecsSinceEpoch();
                        const qint64 startedAt = static_cast<qint64>(msg.value(QStringLiteral("createdAt")).toDouble());
                        const qint64 elapsedMs = qMax<qint64>(0, doneAt - startedAt);
                        const QString content = msg.value(QStringLiteral("content")).toString();
                        const int toks = estimateTokens(content);
                        msg[QStringLiteral("completedAt")] = static_cast<double>(doneAt);
                        msg[QStringLiteral("tokens")] = toks;
                        msg[QStringLiteral("elapsedMs")] = static_cast<int>(elapsedMs);
                        msg[QStringLiteral("tps")] = (elapsedMs > 0 && toks > 0)
                            ? (1000.0 * static_cast<double>(toks) / static_cast<double>(elapsedMs))
                            : 0.0;
                        messages[index] = msg;
                        publishSessionMessages(eventSessionId);
                    }
                    assistantIndexForSession(eventSessionId) = -1;
                    if (eventSessionId == m_sessionId) m_curAsstIdx = -1;
                }
            }
        }
    });

    // Mantener vivo: el server opencode se apaga sin suscriptores de eventos.
    connect(m_eventReply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *r = m_eventReply;
        if (!r) return;
        m_eventReply = nullptr;
        r->deleteLater();
        if (m_stopping) return;
        if (running())
            QTimer::singleShot(200, this, [this]() {
                if (!m_stopping && running()) subscribeEvents();
            });
    });
    connect(m_eventReply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError) {
        if (!m_eventReply) return;
        if (!m_stopping) emit logAppended(QStringLiteral("[opencode event stream reconectando...]\n"));
    });
}

void OpencodeBackend::respondPermission(const QString &sessionId, const QString &permissionId,
                                       const QString &response)
{
    if (sessionId.isEmpty() || permissionId.isEmpty()) return;
    QNetworkRequest req(QUrl(m_attachUrl + QStringLiteral("/session/") + sessionId
                             + QStringLiteral("/permissions/") + permissionId));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject body{{QStringLiteral("response"), response}};
    auto *reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError)
            emit logAppended(QStringLiteral("[error: permission response failed: %1]\n").arg(reply->errorString()));
        reply->deleteLater();
    });
}

void OpencodeBackend::approveTool(const QString &id, bool always)
{
    const QString sid = m_pendingPerm.take(id);
    if (sid.isEmpty()) return;
    const QVariantMap data = m_lifecycleTools.value(sid + QLatin1Char(':') + id);
    emitToolStart(sid, id, data.value(QStringLiteral("tool")).toString(),
                  data.value(QStringLiteral("arguments")).toString(),
                  data.value(QStringLiteral("paths")).toStringList());
    respondPermission(sid, id, always ? QStringLiteral("always") : QStringLiteral("once"));
}

void OpencodeBackend::rejectTool(const QString &id)
{
    const QString sid = m_pendingPerm.take(id);
    if (sid.isEmpty()) return;
    const QVariantMap data = m_lifecycleTools.value(sid + QLatin1Char(':') + id);
    emitToolFinish(sid, id, data.value(QStringLiteral("tool")).toString(), false,
                   QStringLiteral("[el usuario rechazó la acción]"),
                   data.value(QStringLiteral("paths")).toStringList());
    respondPermission(sid, id, QStringLiteral("reject"));
}

QString OpencodeBackend::toolKind(const QString &type)
{
    const QString t = type.toLower();
    if (t.contains(QLatin1String("bash")) || t.contains(QLatin1String("shell"))
            || t.contains(QLatin1String("command")) || t.contains(QLatin1String("exec")))
        return QStringLiteral("shell");
    if (t.contains(QLatin1String("edit")) || t.contains(QLatin1String("write"))
            || t.contains(QLatin1String("patch")) || t.contains(QLatin1String("create"))
            || t.contains(QLatin1String("delete")) || t.contains(QLatin1String("remove")))
        return QStringLiteral("write");
    return QStringLiteral("read");
}
