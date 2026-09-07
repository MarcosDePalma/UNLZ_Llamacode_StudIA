#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include "core/agent/LlamaAgentBackend.h"

class AgentWireTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void dropsOrphanToolAndDemotesNonInitialSystem();
    void dropsDanglingAssistantAndAnchorsFirstUser();
    void dropsTransportErrorAssistantMessages();
    void parsesTextToolCallFallback();
    void cutsTextToolCallBurstAtSecondCall();
    void readsToolSupportFromPropsCaps();
    void fallsBackToTextToolsWhenServerRejectsNativeTools();
    void forceTextToolsSkipsNativeToolAttempt();
    void thirdIdenticalToolCallReplansWithoutExecuting();
    void stoppingDuringCompletionReleasesTurn();
    void differentProjectsRunTurnsConcurrently();
    void restartRepublishesPersistedMessages();
    void taskSessionIsEphemeralAndRestoresPrevious();
    void newSessionDropsPreviousEmptySession();
    void nativeSessionForkPersistsTreeMetadata();
    void midConversationNoteRole_neverSystem();
    void mergeToolCallDelta_assemblesAcrossChunks();
    void mergeToolCallDelta_parallelCallsByIndex();
    void visibleAnswer_stripsThinkButSalvagesWhenEmpty();
    void thinkingLeakGuard_isOptInAndControlsTemplate();
    void buildWarmupPayload_prefillsWithoutGenerating();
    void trimStaleImages_keepsOnlyLatestCapture();
    void buildObservationMessage_wrapsImagesAsUserMultimodal();
    void buildObservationMessage_emptyWhenNoImages();
    void developmentDisciplineSection_coversRegressionGuards();
    void testSafetyNetSection_coversRunnerDetectionAndQuality();
    void projectContextSection_coversIntentAndMemory();
    void stablePhasePrefix_keepsSystemPromptInvariant();
    void desktopPlaybookSection_coversKeyboardPathAndTextVerify();
    void desktopConfirmKeyBlockedAfterTypeEquals();
    void parsesNativeToolCallLeakFallback();
    void parsesKatCoderXmlToolCallFallback();
    void textToolsModeDoesNotDoubleReserveToolBudget();
    void compactionStallCounterTracksProgress();
    void compactionPreservesImmutableTranscript();
    void deterministicPruningDeduplicatesOnlyWorkingContext();
    void failureSpiralDetectsEquivalentErrorsAndResetsOnProgress();
    void toolCallSignatureNormalizesEquivalentJson();
    void toolWatchdogIsTypeAware();
    void streamRepetitionDetectsLongTripleBlockOnly();
    void textToolPayloadCapsGenerationAndStopsAtToolCall();
    void adaptiveSubagentLimit_respectsProfileContextAndVram();
    void isDestructiveAction_gatesShellDesktopMemory();
    void completionErrorClassification_isSelective();
    void sessionTitle_isBriefAndPromptDerived();
    void firstPromptTitlesAndPersistsSession();
    void startupRepairsDefaultTitleFromFirstPrompt();
};

void AgentWireTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void AgentWireTests::stablePhasePrefix_keepsSystemPromptInvariant()
{
    LlamaAgentBackend be;
    be.setStablePhasePrefix(true);
    be.setApprovalPolicy(QStringLiteral("ask"));
    const QString before = be.systemPromptForTest();
    const int toolsBefore = be.toolSchemasForTest().size();
    be.setApprovalPolicy(QStringLiteral("plan"));
    const QString after = be.systemPromptForTest();
    QCOMPARE(after, before);
    QCOMPARE(be.toolSchemasForTest().size(), toolsBefore);
    QVERIFY(after.contains(QStringLiteral("PROTOCOLO DE FASES")));
}

void AgentWireTests::failureSpiralDetectsEquivalentErrorsAndResetsOnProgress()
{
    LlamaAgentBackend be;
    const QString e1 = QStringLiteral("C:/work/a.cpp:123: fatal error: header not found");
    const QString e2 = QStringLiteral("C:/work/b.cpp:987: fatal error: header not found");
    QCOMPARE(LlamaAgentBackend::failureFingerprint(QStringLiteral("run_shell"), e1),
             LlamaAgentBackend::failureFingerprint(QStringLiteral("run_shell"), e2));
    QVERIFY(!be.recordToolOutcomeForTest(QStringLiteral("run_shell"), false, false, e1));
    QVERIFY(!be.recoveryLearningEligibleForTest());
    QVERIFY(!be.recordToolOutcomeForTest(QStringLiteral("run_shell"), false, false, e2));
    QVERIFY(be.recordToolOutcomeForTest(QStringLiteral("run_shell"), false, false, e1));
    QVERIFY(!be.recordToolOutcomeForTest(QStringLiteral("run_shell"), true, false,
                                         QStringLiteral("build ok")));
    QVERIFY(be.recoveryLearningEligibleForTest());
    QVERIFY(!be.recordToolOutcomeForTest(QStringLiteral("run_shell"), false, false, e1));
}

void AgentWireTests::streamRepetitionDetectsLongTripleBlockOnly()
{
    const QString prefix = QStringLiteral("Análisis previo. ");
    const QString block = QStringLiteral(
        "Necesito revisar el componente correcto antes de volver a editar el archivo; "
        "primero comprobaré el error y después cambiaré de estrategia. ");
    const QString loop = prefix + block + block + block;
    QCOMPARE(LlamaAgentBackend::repeatedSuffixStart(loop), prefix.size());
    QCOMPARE(LlamaAgentBackend::repeatedSuffixStart(prefix + block + block), -1);
    QCOMPARE(LlamaAgentBackend::repeatedSuffixStart(
                 QStringLiteral("sí sí sí"), 3, 80), -1);
}

void AgentWireTests::toolCallSignatureNormalizesEquivalentJson()
{
    const QString a = QStringLiteral(" { \"path\" : \"DOCUMENTACION.md\", \"limit\" : 300 } ");
    const QString b = QStringLiteral("{\"limit\":300,\"path\":\"DOCUMENTACION.md\"}");
    QCOMPARE(LlamaAgentBackend::toolCallSignature(QStringLiteral("read_file"), a),
             LlamaAgentBackend::toolCallSignature(QStringLiteral("READ_FILE"), b));
    QVERIFY(LlamaAgentBackend::toolCallSignature(QStringLiteral("read_file"), a)
            != LlamaAgentBackend::toolCallSignature(
                QStringLiteral("read_file"),
                QStringLiteral("{\"path\":\"otro.md\",\"limit\":300}")));
}

void AgentWireTests::toolWatchdogIsTypeAware()
{
    QCOMPARE(LlamaAgentBackend::toolWatchdogSeconds("write_file", {}, 15), 15);
    QCOMPARE(LlamaAgentBackend::toolWatchdogSeconds(
                 "run_shell", QJsonObject{{"timeout_s", 600}}, 15), 615);
    QCOMPARE(LlamaAgentBackend::toolWatchdogSeconds("web_search", {}, 15), 180);
}

void AgentWireTests::sessionTitle_isBriefAndPromptDerived()
{
    QCOMPARE(LlamaAgentBackend::suggestSessionTitle(
                 QStringLiteral("¿Podés revisar el error de compilación CMake?")),
             QStringLiteral("Revisar error compilación"));
    QCOMPARE(LlamaAgentBackend::suggestSessionTitle(QStringLiteral("para y con")),
             QStringLiteral("Sesión"));
    QVERIFY(LlamaAgentBackend::suggestSessionTitle(
                 QStringLiteral("crear una app de inventario para biblioteca"))
                 .split(QLatin1Char(' '), Qt::SkipEmptyParts).size() <= 3);
}

void AgentWireTests::firstPromptTitlesAndPersistsSession()
{
    const QString store = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                          + QStringLiteral("/agent_llamaagent");
    QDir(store).removeRecursively();
    QTemporaryDir cwd;
    QVERIFY(cwd.isValid());
    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.cwd = cwd.path();
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:1");
    ctx.modelId = QStringLiteral("test");
    LlamaAgentBackend backend;
    backend.start(ctx);
    backend.sendMessage(QStringLiteral("Revisá documentación marítima completa"));
    QCOMPARE(backend.currentSessionTitle(), QStringLiteral("Revisá documentación marítima"));
    backend.cancelGeneration();
    backend.stop();

    QFile index(store + QStringLiteral("/index.json"));
    QVERIFY(index.open(QIODevice::ReadOnly));
    const QJsonArray sessions = QJsonDocument::fromJson(index.readAll()).array();
    QCOMPARE(sessions.first().toObject().value(QStringLiteral("title")).toString(),
             QStringLiteral("Revisá documentación marítima"));
    QDir(store).removeRecursively();
}

void AgentWireTests::newSessionDropsPreviousEmptySession()
{
    // Crear una sesión y crear otra sin escribir nada: la primera no queda
    // listada (no tiene ni input ni output).
    const QString store = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                          + QStringLiteral("/agent_llamaagent");
    QDir(store).removeRecursively();
    QTemporaryDir cwd;
    QVERIFY(cwd.isValid());
    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.cwd = cwd.path();
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:1");
    ctx.modelId = QStringLiteral("test");
    LlamaAgentBackend backend;
    backend.start(ctx);
    const QString empty = backend.currentSessionId();
    backend.newSession();
    QCOMPARE(backend.sessions().size(), 1);
    QVERIFY(backend.currentSessionId() != empty);
    QVERIFY(!QFile::exists(store + QLatin1Char('/') + empty + QStringLiteral(".json")));

    // Con historia sí se conserva.
    backend.sendMessage(QStringLiteral("hola"));
    backend.cancelGeneration();          // cerrar el turno antes de cambiar de sesión
    backend.newSession();
    QCOMPARE(backend.sessions().size(), 2);
    backend.stop();
    QDir(store).removeRecursively();
}

void AgentWireTests::startupRepairsDefaultTitleFromFirstPrompt()
{
    const QString store = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                          + QStringLiteral("/agent_llamaagent");
    QDir(store).removeRecursively();
    QVERIFY(QDir().mkpath(store));
    const QString id = QStringLiteral("legacy-title");
    QFile index(store + QStringLiteral("/index.json"));
    QVERIFY(index.open(QIODevice::WriteOnly));
    index.write(QJsonDocument(QJsonArray{QJsonObject{
        {QStringLiteral("id"), id}, {QStringLiteral("title"), QStringLiteral("Sesión")}
    }}).toJson());
    index.close();
    QFile session(store + QLatin1Char('/') + id + QStringLiteral(".json"));
    QVERIFY(session.open(QIODevice::WriteOnly));
    session.write(QJsonDocument(QJsonObject{{QStringLiteral("messages"), QJsonArray{
        QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), QStringLiteral("Actualizar documentación completa")}}
    }}}).toJson());
    session.close();
    QTemporaryDir cwd;
    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent"); ctx.cwd = cwd.path();
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:1"); ctx.modelId = QStringLiteral("test");
    LlamaAgentBackend backend;
    backend.start(ctx);
    QCOMPARE(backend.currentSessionTitle(), QStringLiteral("Actualizar documentación completa"));
    backend.stop();
    QDir(store).removeRecursively();
}

void AgentWireTests::completionErrorClassification_isSelective()
{
    QCOMPARE(LlamaAgentBackend::classifyCompletionError(
                 400, QStringLiteral("prompt exceeds the context length")),
             LlamaAgentBackend::RetryContextOverflow);
    QCOMPARE(LlamaAgentBackend::classifyCompletionError(
                 429, QStringLiteral("rate limited")),
             LlamaAgentBackend::RetryTransient);
    QCOMPARE(LlamaAgentBackend::classifyCompletionError(
                 503, QStringLiteral("unavailable")),
             LlamaAgentBackend::RetryTransient);
    QCOMPARE(LlamaAgentBackend::classifyCompletionError(
                 401, QStringLiteral("invalid api key")),
             LlamaAgentBackend::RetryNone);
    QCOMPARE(LlamaAgentBackend::classifyCompletionError(
                 400, QStringLiteral("invalid tool schema")),
             LlamaAgentBackend::RetryNone);
}

static QJsonObject msg(const QString &role, const QString &content)
{
    return QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("content"), content}};
}

static QJsonObject assistantCall(const QString &id)
{
    return QJsonObject{
        {QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("content"), QString()},
        {QStringLiteral("tool_calls"), QJsonArray{QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("function"), QJsonObject{
                {QStringLiteral("name"), QStringLiteral("read_file")},
                {QStringLiteral("arguments"), QStringLiteral("{\"path\":\"a.txt\"")}
            }}
        }}}
    };
}

static QJsonObject toolResult(const QString &id)
{
    return QJsonObject{
        {QStringLiteral("role"), QStringLiteral("tool")},
        {QStringLiteral("tool_call_id"), id},
        {QStringLiteral("name"), QStringLiteral("read_file")},
        {QStringLiteral("content"), QStringLiteral("ok")}
    };
}

void AgentWireTests::dropsOrphanToolAndDemotesNonInitialSystem()
{
    QJsonArray in{
        msg(QStringLiteral("system"), QStringLiteral("base")),
        msg(QStringLiteral("user"), QStringLiteral("task")),
        toolResult(QStringLiteral("missing")),
        msg(QStringLiteral("system"), QStringLiteral("soft nudge"))
    };

    const QJsonArray out = LlamaAgentBackend::sanitizeApiMessagesForWire(in);
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[0].toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
    QCOMPARE(out[1].toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    QCOMPARE(out[2].toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    QCOMPARE(out[2].toObject().value(QStringLiteral("content")).toString(), QStringLiteral("soft nudge"));
}

void AgentWireTests::dropsDanglingAssistantAndAnchorsFirstUser()
{
    QJsonArray in{
        msg(QStringLiteral("system"), QStringLiteral("base")),
        msg(QStringLiteral("user"), QStringLiteral("original task")),
        assistantCall(QStringLiteral("c1")),
        msg(QStringLiteral("assistant"), QStringLiteral("tail without user"))
    };

    const QJsonArray out = LlamaAgentBackend::sanitizeApiMessagesForWire(in);
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[0].toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
    QCOMPARE(out[1].toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    QCOMPARE(out[1].toObject().value(QStringLiteral("content")).toString(), QStringLiteral("original task"));
    QCOMPARE(out[2].toObject().value(QStringLiteral("role")).toString(), QStringLiteral("assistant"));
    QVERIFY(!out[2].toObject().contains(QStringLiteral("tool_calls")));
}

void AgentWireTests::dropsTransportErrorAssistantMessages()
{
    QJsonArray in{
        msg(QStringLiteral("system"), QStringLiteral("base")),
        msg(QStringLiteral("user"), QStringLiteral("Que hora es?")),
        msg(QStringLiteral("assistant"), QStringLiteral("[error: Error transferring http://127.0.0.1:8081/v1/chat/completions - server replied: Bad Request]")),
        msg(QStringLiteral("user"), QStringLiteral("Hola"))
    };

    const QJsonArray out = LlamaAgentBackend::sanitizeApiMessagesForWire(in);
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[0].toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
    QCOMPARE(out[1].toObject().value(QStringLiteral("content")).toString(), QStringLiteral("Que hora es?"));
    QCOMPARE(out[2].toObject().value(QStringLiteral("content")).toString(), QStringLiteral("Hola"));
}

void AgentWireTests::parsesTextToolCallFallback()
{
    const QString content = QStringLiteral(
        "TOOL_CALL {\"name\":\"web_fetch\",\"arguments\":{\"url\":\"https://dolarhoy.com/\"}}");

    const QJsonObject call = LlamaAgentBackend::textToolCallFromContent(content);
    QVERIFY(!call.isEmpty());
    QVERIFY(call.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("textcall_")));
    QCOMPARE(call.value(QStringLiteral("type")).toString(), QStringLiteral("function"));

    const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
    QCOMPARE(fn.value(QStringLiteral("name")).toString(), QStringLiteral("web_fetch"));

    // `arguments` viaja como string JSON (spec OpenAI), NO como objeto: los
    // perfiles cloud lo exigen así, y para los locales llama.cpp lo deserializa
    // solo antes de renderizar el chat-template de Gemma4 (func_args_not_string).
    QVERIFY(fn.value(QStringLiteral("arguments")).isString());
    const QJsonObject args = QJsonDocument::fromJson(
        fn.value(QStringLiteral("arguments")).toString().toUtf8()).object();
    QCOMPARE(args.value(QStringLiteral("url")).toString(), QStringLiteral("https://dolarhoy.com/"));
}

void AgentWireTests::parsesKatCoderXmlToolCallFallback()
{
    const QString content = QStringLiteral(
        "<tool_call>\n<function=write_file>\n"
        "<parameter=path>solution.py</parameter>\n"
        "<parameter=content>def answer():\\n    return 42</parameter>\n"
        "</function>\n</tool_call>");

    const QJsonObject call = LlamaAgentBackend::textToolCallFromContent(content);
    QVERIFY(!call.isEmpty());
    const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
    QCOMPARE(fn.value(QStringLiteral("name")).toString(), QStringLiteral("write_file"));
    const QJsonObject args = QJsonDocument::fromJson(
        fn.value(QStringLiteral("arguments")).toString().toUtf8()).object();
    QCOMPARE(args.value(QStringLiteral("path")).toString(), QStringLiteral("solution.py"));
    QCOMPARE(args.value(QStringLiteral("content")).toString(),
             QStringLiteral("def answer():\\n    return 42"));

    // Formato observado en el smoke test real de KAT-Coder: el modelo puede
    // separar el tag de apertura, el valor y el cierre con saltos de línea.
    const QString streamed = QStringLiteral(
        "<tool_call>\n<function=get_weather>\n"
        "<parameter=city>\nBuenos Aires\n</parameter>\n"
        "</function>\n</tool_call>");
    const QJsonObject streamedCall = LlamaAgentBackend::textToolCallFromContent(streamed);
    QVERIFY(!streamedCall.isEmpty());
    const QJsonObject streamedArgs = QJsonDocument::fromJson(
        streamedCall.value(QStringLiteral("function")).toObject()
            .value(QStringLiteral("arguments")).toString().toUtf8()).object();
    QCOMPARE(streamedArgs.value(QStringLiteral("city")).toString(),
             QStringLiteral("Buenos Aires"));
}

// Ráfaga: el modelo escupe decenas de TOOL_CALL en UNA generación. Sólo el
// primero se ejecuta; el resto era texto basura en el chat y minutos quemados.
void AgentWireTests::cutsTextToolCallBurstAtSecondCall()
{
    const QString one = QStringLiteral(
        "TOOL_CALL {\"name\":\"list_dir\",\"arguments\":{\"path\":\"C:\\\\a\"}}");
    QCOMPARE(LlamaAgentBackend::secondTextToolCallStart(one), -1);
    // Primer call todavía incompleto (JSON sin cerrar): no cortar.
    QCOMPARE(LlamaAgentBackend::secondTextToolCallStart(
                 QStringLiteral("TOOL_CALL {\"name\":\"list_dir\",\"argu")), -1);

    const QString burst = one + QStringLiteral("\n") + one + QStringLiteral("\n") + one;
    const int cut = LlamaAgentBackend::secondTextToolCallStart(burst);
    QCOMPARE(cut, one.size() + 1);

    const QString head = burst.left(cut);
    const QJsonObject call = LlamaAgentBackend::textToolCallFromContent(head);
    QCOMPARE(call.value(QStringLiteral("function")).toObject()
                 .value(QStringLiteral("name")).toString(), QStringLiteral("list_dir"));
}

// llama.cpp nuevo publica en /props lo que dedujo del template
// (chat_template_caps). Manda sobre el regex jinja; sin caps, se usa el regex.
void AgentWireTests::readsToolSupportFromPropsCaps()
{
    bool have = false;
    const QJsonObject withCaps{
        {QStringLiteral("chat_template"), QStringLiteral("{{ messages }}")},
        {QStringLiteral("chat_template_caps"),
         QJsonObject{{QStringLiteral("supports_tool_calls"), true}}}};
    QVERIFY(LlamaAgentBackend::toolSupportFromProps(withCaps, &have));
    QVERIFY(have);

    const QJsonObject capsNoTools{
        {QStringLiteral("chat_template"), QStringLiteral("{% for tool in tools %}{{ tool | tojson }}")},
        {QStringLiteral("chat_template_caps"),
         QJsonObject{{QStringLiteral("supports_tool_calls"), false}}}};
    QVERIFY(!LlamaAgentBackend::toolSupportFromProps(capsNoTools, &have));

    // Binario viejo sin caps → cae al regex sobre el jinja.
    const QJsonObject legacy{
        {QStringLiteral("chat_template"), QStringLiteral("{{ message.tool_calls }}")}};
    QVERIFY(LlamaAgentBackend::toolSupportFromProps(legacy, &have));
    QVERIFY(have);

    have = true;
    QVERIFY(!LlamaAgentBackend::toolSupportFromProps(QJsonObject{}, &have));
    QVERIFY(!have);
}

void AgentWireTests::parsesNativeToolCallLeakFallback()
{
    // Regresión del bug "sumar 2+2": modelos como Gemma filtran su formato NATIVO
    // de tool-call como texto (<|tool_call>call:NAME{args}<tool_call|>). El nombre
    // va en call:NAME y los args en un objeto JSON aparte (puede ser {}). El parser
    // debe reconocerlo; antes veía el {} vacío, no encontraba "name" y la tool
    // nunca se ejecutaba (la Task terminaba "ok" sin operar la app).

    // Args vacíos: desktop_windows sin parámetros.
    {
        const QString content = QStringLiteral("<|tool_call>call:desktop_windows{}<tool_call|>");
        const QJsonObject call = LlamaAgentBackend::textToolCallFromContent(content);
        QVERIFY(!call.isEmpty());
        const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
        QCOMPARE(fn.value(QStringLiteral("name")).toString(), QStringLiteral("desktop_windows"));
    }

    // Con args: desktop_type con texto.
    {
        const QString content = QStringLiteral(
            "<|tool_call>call:desktop_type{\"text\":\"2+2\"}<tool_call|>");
        const QJsonObject call = LlamaAgentBackend::textToolCallFromContent(content);
        QVERIFY(!call.isEmpty());
        const QJsonObject fn = call.value(QStringLiteral("function")).toObject();
        QCOMPARE(fn.value(QStringLiteral("name")).toString(), QStringLiteral("desktop_type"));
        const QJsonObject args = QJsonDocument::fromJson(
            fn.value(QStringLiteral("arguments")).toString().toUtf8()).object();
        QCOMPARE(args.value(QStringLiteral("text")).toString(), QStringLiteral("2+2"));
    }

    // Prosa con "call:" pero sin el token nativo → NO debe disparar tool call.
    {
        const QString content = QStringLiteral("Ya hice la call: desktop_windows manualmente.");
        QVERIFY(LlamaAgentBackend::textToolCallFromContent(content).isEmpty());
    }
}

class FakeToolRejectingServer : public QTcpServer
{
public:
    int nativeRejects = 0;
    int textRequests = 0;
    int secondTextBodySize = 0;
    bool secondTextBodyWasTruncated = false;

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *sock = new QTcpSocket(this);
        sock->setSocketDescriptor(socketDescriptor);
        auto *buf = new QByteArray;
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, buf]() {
            buf->append(sock->readAll());
            const int headerEnd = buf->indexOf("\r\n\r\n");
            if (headerEnd < 0) return;
            const QByteArray headers = buf->left(headerEnd);
            int contentLength = 0;
            for (const QByteArray &line : headers.split('\n')) {
                const QByteArray trimmed = line.trimmed();
                if (trimmed.toLower().startsWith("content-length:"))
                    contentLength = trimmed.mid(15).trimmed().toInt();
            }
            if (buf->size() < headerEnd + 4 + contentLength) return;

            const QByteArray firstLine = headers.split('\n').value(0).trimmed();
            const QByteArray body = buf->mid(headerEnd + 4, contentLength);
            if (firstLine.startsWith("GET /props")) {
                writeJson(sock, QByteArrayLiteral("{\"n_ctx\":4096}"));
                return;
            }
            if (body.contains("\"tools\"")) {
                ++nativeRejects;
                writeRaw(sock, QByteArrayLiteral(
                    "HTTP/1.1 400 Bad Request\r\nContent-Length: 23\r\nConnection: close\r\n\r\nnative tools rejected"));
                return;
            }

            ++textRequests;
            if (textRequests == 1) {
                writeSse(sock, QStringLiteral(
                    "TOOL_CALL {\"name\":\"read_file\",\"arguments\":{\"path\":\"big.txt\"}}"));
            } else {
                QVERIFY(body.contains("TOOL_RESULT"));
                secondTextBodySize = body.size();
                secondTextBodyWasTruncated = body.contains("TOOL_RESULT truncado");
                writeSse(sock, QStringLiteral("FINAL: read_file ejecutado correctamente"));
            }
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }

private:
    static void writeJson(QTcpSocket *sock, const QByteArray &json)
    {
        writeRaw(sock, QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                       + QByteArray::number(json.size())
                       + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + json);
    }

    static void writeSse(QTcpSocket *sock, const QString &content)
    {
        const QByteArray escaped = QString(content).replace("\\", "\\\\").replace("\"", "\\\"").toUtf8();
        const QByteArray payload = QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"")
                                   + escaped + QByteArrayLiteral("\"}}]}\n\n")
                                   + QByteArrayLiteral("data: [DONE]\n\n");
        writeRaw(sock, QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: ")
                       + QByteArray::number(payload.size())
                       + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + payload);
    }

    static void writeRaw(QTcpSocket *sock, const QByteArray &data)
    {
        sock->write(data);
        sock->flush();
        sock->disconnectFromHost();
    }
};

void AgentWireTests::fallsBackToTextToolsWhenServerRejectsNativeTools()
{
    QTemporaryDir cwd;
    QVERIFY(cwd.isValid());
    QFile marker(cwd.path() + QStringLiteral("/marker.txt"));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write("ok");
    marker.close();
    QFile big(cwd.path() + QStringLiteral("/big.txt"));
    QVERIFY(big.open(QIODevice::WriteOnly));
    big.write(QByteArray(50000, 'x'));
    big.close();

    FakeToolRejectingServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.cwd = cwd.path();
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    ctx.modelId = QStringLiteral("test-model");
    ctx.ctxOverride = 4096;

    LlamaAgentBackend backend;
    backend.setEphemeralSessions(true);
    backend.start(ctx);
    QSignalSpy finished(&backend, &LlamaAgentBackend::turnFinished);
    backend.sendMessage(QStringLiteral("Listá el directorio actual usando tools."));

    QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 10000);
    QCOMPARE(server.nativeRejects, 2);
    QCOMPARE(server.textRequests, 2);
    // Lo que se testea es que el TOOL_RESULT viaje truncado (el archivo crudo son
    // 50k). El techo es holgado a propósito: el prompt del protocolo textual
    // cambia de tamaño y no debe romper este test.
    QVERIFY2(server.secondTextBodySize < 30000,
             qPrintable(QStringLiteral("body=%1").arg(server.secondTextBodySize)));
    QVERIFY(server.secondTextBodyWasTruncated);

    bool sawTool = false;
    bool sawFinal = false;
    for (const QVariant &v : backend.messages()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("role")).toString() == QLatin1String("toolcall")
            && m.value(QStringLiteral("name")).toString() == QLatin1String("read_file"))
            sawTool = true;
        if (m.value(QStringLiteral("content")).toString().contains(QStringLiteral("read_file ejecutado")))
            sawFinal = true;
    }
    QVERIFY(sawTool);
    QVERIFY(sawFinal);
    backend.stop();
}

// Server que NUNCA acepta tools nativas: si llega un body con "tools" es un bug
// (con forceTextTools el backend no debe intentar el path nativo). Responde el
// protocolo textual directamente.
// Regresión ULTRA-Q: el modelo insiste con la misma lectura aunque ya recibió el
// contenido y luego el stub de deduplicación. La tercera llamada se bloquea y el
// cuarto request simula que la IA acepta el replanteo y entrega un resultado final.
class FakeRepeatingToolServer : public QTcpServer
{
public:
    int completionRequests = 0;

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *sock = new QTcpSocket(this);
        sock->setSocketDescriptor(socketDescriptor);
        auto *buf = new QByteArray;
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, buf]() {
            buf->append(sock->readAll());
            const int headerEnd = buf->indexOf("\r\n\r\n");
            if (headerEnd < 0) return;
            const QByteArray headers = buf->left(headerEnd);
            int contentLength = 0;
            for (const QByteArray &line : headers.split('\n')) {
                const QByteArray trimmed = line.trimmed();
                if (trimmed.toLower().startsWith("content-length:"))
                    contentLength = trimmed.mid(15).trimmed().toInt();
            }
            if (buf->size() < headerEnd + 4 + contentLength) return;
            const QByteArray firstLine = headers.split('\n').value(0).trimmed();
            if (firstLine.startsWith("GET /props")) {
                writeJson(sock, QByteArrayLiteral("{\"n_ctx\":4096}"));
                return;
            }
            ++completionRequests;
            if (completionRequests <= 3)
                writeSse(sock, QStringLiteral(
                    "TOOL_CALL {\"name\":\"read_file\",\"arguments\":{\"path\":\"marker.txt\"}}"));
            else
                writeSse(sock, QStringLiteral("FINAL: read_file ejecutado correctamente"));
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }

private:
    static void writeJson(QTcpSocket *sock, const QByteArray &json)
    {
        writeRaw(sock, QByteArrayLiteral(
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
            + QByteArray::number(json.size())
            + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + json);
    }
    static void writeSse(QTcpSocket *sock, const QString &content)
    {
        const QByteArray escaped = QString(content).replace("\\", "\\\\")
                                       .replace("\"", "\\\"").toUtf8();
        const QByteArray payload = QByteArrayLiteral(
            "data: {\"choices\":[{\"delta\":{\"content\":\"")
            + escaped + QByteArrayLiteral("\"}}]}\n\ndata: [DONE]\n\n");
        writeRaw(sock, QByteArrayLiteral(
            "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: ")
            + QByteArray::number(payload.size())
            + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + payload);
    }
    static void writeRaw(QTcpSocket *sock, const QByteArray &data)
    {
        sock->write(data);
        sock->flush();
        sock->disconnectFromHost();
    }
};

class FakeTextOnlyServer : public QTcpServer
{
public:
    int nativeRequests = 0;
    int textRequests = 0;

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *sock = new QTcpSocket(this);
        sock->setSocketDescriptor(socketDescriptor);
        auto *buf = new QByteArray;
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, buf]() {
            buf->append(sock->readAll());
            const int headerEnd = buf->indexOf("\r\n\r\n");
            if (headerEnd < 0) return;
            const QByteArray headers = buf->left(headerEnd);
            int contentLength = 0;
            for (const QByteArray &line : headers.split('\n')) {
                const QByteArray trimmed = line.trimmed();
                if (trimmed.toLower().startsWith("content-length:"))
                    contentLength = trimmed.mid(15).trimmed().toInt();
            }
            if (buf->size() < headerEnd + 4 + contentLength) return;

            const QByteArray firstLine = headers.split('\n').value(0).trimmed();
            const QByteArray body = buf->mid(headerEnd + 4, contentLength);
            if (firstLine.startsWith("GET /props")) {
                writeJson(sock, QByteArrayLiteral("{\"n_ctx\":4096}"));
                return;
            }
            if (body.contains("\"tools\"")) {
                ++nativeRequests;
                writeRaw(sock, QByteArrayLiteral(
                    "HTTP/1.1 400 Bad Request\r\nContent-Length: 4\r\nConnection: close\r\n\r\nnope"));
                return;
            }
            ++textRequests;
            if (textRequests == 1)
                writeSse(sock, QStringLiteral(
                    "TOOL_CALL {\"name\":\"read_file\",\"arguments\":{\"path\":\"marker.txt\"}}"));
            else
                writeSse(sock, QStringLiteral("FINAL: read_file ejecutado correctamente"));
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }

private:
    static void writeJson(QTcpSocket *sock, const QByteArray &json)
    {
        writeRaw(sock, QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                       + QByteArray::number(json.size())
                       + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + json);
    }
    static void writeSse(QTcpSocket *sock, const QString &content)
    {
        const QByteArray escaped = QString(content).replace("\\", "\\\\").replace("\"", "\\\"").toUtf8();
        const QByteArray payload = QByteArrayLiteral("data: {\"choices\":[{\"delta\":{\"content\":\"")
                                   + escaped + QByteArrayLiteral("\"}}]}\n\n")
                                   + QByteArrayLiteral("data: [DONE]\n\n");
        writeRaw(sock, QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: ")
                       + QByteArray::number(payload.size())
                       + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + payload);
    }
    static void writeRaw(QTcpSocket *sock, const QByteArray &data)
    {
        sock->write(data);
        sock->flush();
        sock->disconnectFromHost();
    }
};

void AgentWireTests::forceTextToolsSkipsNativeToolAttempt()
{
    QTemporaryDir cwd;
    QVERIFY(cwd.isValid());
    QFile marker(cwd.path() + QStringLiteral("/marker.txt"));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write("ok");
    marker.close();

    FakeTextOnlyServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.cwd = cwd.path();
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    ctx.modelId = QStringLiteral("gemma-sin-tools");
    ctx.ctxOverride = 4096;

    LlamaAgentBackend backend;
    backend.setEphemeralSessions(true);
    backend.setForceTextTools(true);   // modelo "unsupported" → texto desde el primer request
    backend.start(ctx);
    QSignalSpy finished(&backend, &LlamaAgentBackend::turnFinished);
    backend.sendMessage(QStringLiteral("Leé marker.txt usando tools."));

    QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 10000);
    QCOMPARE(server.nativeRequests, 0);   // nunca intentó el payload de tools nativas
    QVERIFY(server.textRequests >= 2);

    bool sawTool = false, sawFinal = false;
    for (const QVariant &v : backend.messages()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("role")).toString() == QLatin1String("toolcall")
            && m.value(QStringLiteral("name")).toString() == QLatin1String("read_file"))
            sawTool = true;
        if (m.value(QStringLiteral("content")).toString().contains(QStringLiteral("read_file ejecutado")))
            sawFinal = true;
    }
    QVERIFY(sawTool);
    QVERIFY(sawFinal);
    backend.stop();
}

void AgentWireTests::thirdIdenticalToolCallReplansWithoutExecuting()
{
    QTemporaryDir cwd;
    QVERIFY(cwd.isValid());
    QFile marker(cwd.path() + QStringLiteral("/marker.txt"));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write("contenido estable");
    marker.close();

    FakeRepeatingToolServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.cwd = cwd.path();
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    ctx.modelId = QStringLiteral("loop-model");
    ctx.ctxOverride = 4096;

    LlamaAgentBackend backend;
    backend.setEphemeralSessions(true);
    backend.setForceTextTools(true);
    backend.start(ctx);
    QSignalSpy finished(&backend, &LlamaAgentBackend::turnFinished);
    backend.sendMessage(QStringLiteral("Revisá marker.txt."));

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
    // La tercera llamada se bloquea, pero su resultado correctivo vuelve a la IA.
    // El fake responde entonces con FINAL, simulando que eligió otro camino.
    QCOMPARE(server.completionRequests, 4);
    QVERIFY(!backend.isBusy());
    int executedReads = 0;
    bool sawReplannedFinal = false;
    for (const QVariant &message : backend.messages()) {
        const QVariantMap map = message.toMap();
        if (map.value(QStringLiteral("role")).toString() == QLatin1String("toolcall")
            && map.value(QStringLiteral("name")).toString() == QLatin1String("read_file"))
            ++executedReads;
        if (map.value(QStringLiteral("content")).toString()
                .contains(QStringLiteral("read_file ejecutado correctamente")))
            sawReplannedFinal = true;
    }
    QCOMPARE(executedReads, 2);
    QVERIFY(sawReplannedFinal);
    backend.stop();
}

// Regresión: si llama-server se reinicia con un stream en vuelo, stop() debe
// cerrar el turno y liberar todos los guards. Antes quedaba una burbuja
// "Pensando..." y Tasks/workflows esperaban turnFinished indefinidamente.
class FakeHangingServer : public QTcpServer
{
public:
    int postRequests = 0;
protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *sock = new QTcpSocket(this);
        sock->setSocketDescriptor(socketDescriptor);
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            const QByteArray request = sock->readAll();
            if (request.startsWith("GET /props")) {
                const QByteArray body = QByteArrayLiteral("{\"n_ctx\":4096}");
                sock->write(QByteArrayLiteral(
                    "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: ")
                    + QByteArray::number(body.size())
                    + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + body);
                sock->disconnectFromHost();
            }
            if (request.startsWith("POST /v1/chat/completions")
                && !sock->property("countedPost").toBool()) {
                sock->setProperty("countedPost", true);
                ++postRequests;
            }
            // POST /chat/completions queda abierto para simular el server que
            // desaparece/reinicia durante prompt processing.
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
};

void AgentWireTests::differentProjectsRunTurnsConcurrently()
{
    const QString store = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                          + QStringLiteral("/agent_llamaagent");
    QDir(store).removeRecursively();
    FakeHangingServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QTemporaryDir maritime;
    QTemporaryDir stellar;
    QVERIFY(maritime.isValid());
    QVERIFY(stellar.isValid());

    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.cwd = maritime.path();
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    ctx.modelId = QStringLiteral("test-model");
    ctx.ctxOverride = 4096;
    ctx.parallelSlots = 2;

    LlamaAgentBackend backend;
    backend.start(ctx);
    const QString maritimeId = backend.currentSessionId();
    // Las dos sesiones arrancan con historia: una sesión sin ningún mensaje no
    // sobrevive a que la dejes (se descarta como vacía). Cada semilla cuesta un
    // POST al server falso, de ahí que el conteo esperado sea 4.
    backend.sendMessage(QStringLiteral("semilla maritima"));
    QTRY_COMPARE_WITH_TIMEOUT(server.postRequests, 1, 3000);   // el POST no es sincrónico
    backend.cancelGeneration();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isBusy(), 3000);
    backend.newSessionInProject(stellar.path());
    const QString stellarId = backend.currentSessionId();
    QVERIFY(maritimeId != stellarId);
    backend.sendMessage(QStringLiteral("semilla stellar"));
    QTRY_COMPARE_WITH_TIMEOUT(server.postRequests, 2, 3000);   // esperar el POST antes de cortar
    backend.cancelGeneration();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isBusy(), 3000);

    backend.switchSession(maritimeId);
    backend.sendMessage(QStringLiteral("turno maritima"));
    QVERIFY(backend.isBusy());
    backend.switchSession(stellarId);
    backend.sendMessage(QStringLiteral("turno stellar"));

    QTRY_COMPARE_WITH_TIMEOUT(server.postRequests, 4, 3000);
    QVERIFY(backend.selectedSessionBusy());
    QCOMPARE(backend.currentSessionId(), stellarId);

    // PARAR en la conversación visible cancela sólo Stellar; Marítima sigue.
    backend.cancelGeneration();
    QVERIFY(!backend.selectedSessionBusy());
    backend.switchSession(maritimeId);
    QVERIFY(backend.selectedSessionBusy());
    backend.cancelGeneration();
    backend.stop();
    QDir(store).removeRecursively();
}

void AgentWireTests::stoppingDuringCompletionReleasesTurn()
{
    FakeHangingServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QTemporaryDir cwd;
    QVERIFY(cwd.isValid());

    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.cwd = cwd.path();
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
    ctx.modelId = QStringLiteral("test-model");
    ctx.ctxOverride = 4096;

    LlamaAgentBackend backend;
    backend.setEphemeralSessions(true);
    backend.start(ctx);
    QSignalSpy finished(&backend, &LlamaAgentBackend::turnFinished);
    QSignalSpy errors(&backend, &LlamaAgentBackend::errorOccurred);
    backend.sendMessage(QStringLiteral("trabajo largo"));
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections()
                            || backend.messages().size() >= 2, 1000);

    backend.stop();

    QCOMPARE(finished.count(), 1);
    QVERIFY(!backend.running());
    const QVariantMap last = backend.messages().last().toMap();
    QVERIFY(!last.value(QStringLiteral("typing")).toBool());
    QVERIFY(last.value(QStringLiteral("content")).toString()
                .contains(QStringLiteral("puede reintentarse")));

    // Reiniciar el mismo objeto y enviar otro turno no debe disparar el guard
    // fantasma "Hay un turno en curso".
    backend.start(ctx);
    backend.sendMessage(QStringLiteral("segundo intento"));
    QCOMPARE(errors.count(), 0);
    backend.stop();
}

void AgentWireTests::restartRepublishesPersistedMessages()
{
    const QString store = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                          + QStringLiteral("/agent_llamaagent");
    QDir(store).removeRecursively();
    QVERIFY(QDir().mkpath(store));

    const QString sessionId = QStringLiteral("restart-regression");
    const QJsonObject sessionMeta{
        {QStringLiteral("id"), sessionId},
        {QStringLiteral("title"), QStringLiteral("Persistida")},
        {QStringLiteral("created"), 1.0}
    };
    QFile indexFile(store + QStringLiteral("/index.json"));
    QVERIFY(indexFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    indexFile.write(QJsonDocument(QJsonArray{sessionMeta}).toJson());
    indexFile.close();

    QFile sessionFile(store + QStringLiteral("/") + sessionId + QStringLiteral(".json"));
    QVERIFY(sessionFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    sessionFile.write(QJsonDocument(QJsonObject{
        {QStringLiteral("id"), sessionId},
        {QStringLiteral("title"), QStringLiteral("Persistida")},
        {QStringLiteral("messages"), QJsonArray{QJsonObject{
            {QStringLiteral("role"), QStringLiteral("assistant")},
            {QStringLiteral("content"), QStringLiteral("mensaje persistido")}
        }}},
        {QStringLiteral("api"), QJsonArray{}}
    }).toJson());
    sessionFile.close();

    QTemporaryDir cwd;
    QVERIFY(cwd.isValid());
    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.cwd = cwd.path();
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:1");
    ctx.modelId = QStringLiteral("test-model");

    LlamaAgentBackend backend;
    backend.start(ctx);
    QCOMPARE(backend.messages().size(), 1);
    backend.stop();

    QSignalSpy messagesSpy(&backend, &LlamaAgentBackend::messagesChanged);
    QSignalSpy sessionsSpy(&backend, &LlamaAgentBackend::sessionsChanged);
    backend.start(ctx);

    QVERIFY(messagesSpy.count() >= 1);
    QVERIFY(sessionsSpy.count() >= 1);
    QCOMPARE(backend.messages().first().toMap().value(QStringLiteral("content")).toString(),
             QStringLiteral("mensaje persistido"));

    backend.stop();
    QVERIFY(sessionFile.open(QIODevice::ReadOnly));
    const QJsonObject migrated = QJsonDocument::fromJson(sessionFile.readAll()).object();
    QCOMPARE(migrated.value(QStringLiteral("snapshotVersion")).toInt(), 2);
    QVERIFY(!migrated.value(QStringLiteral("transcript")).toArray().isEmpty());
    QVERIFY(!migrated.value(QStringLiteral("workingContext")).toArray().isEmpty());
    QDir(store).removeRecursively();
}

void AgentWireTests::nativeSessionForkPersistsTreeMetadata()
{
    const QString store = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                          + QStringLiteral("/agent_llamaagent");
    QDir(store).removeRecursively();
    QVERIFY(QDir().mkpath(store));
    const QString parentId = QStringLiteral("fork-parent");
    QFile indexFile(store + QStringLiteral("/index.json"));
    QVERIFY(indexFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    indexFile.write(QJsonDocument(QJsonArray{QJsonObject{
        {QStringLiteral("id"), parentId}, {QStringLiteral("title"), QStringLiteral("Origen")},
        {QStringLiteral("created"), 1.0}, {QStringLiteral("projectDir"), QStringLiteral("P")}
    }}).toJson());
    indexFile.close();
    QFile source(store + QLatin1Char('/') + parentId + QStringLiteral(".json"));
    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    source.write(QJsonDocument(QJsonObject{
        {QStringLiteral("messages"), QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                        {QStringLiteral("content"), QStringLiteral("pregunta")}},
            QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                        {QStringLiteral("content"), QStringLiteral("respuesta")}}}},
        {QStringLiteral("api"), QJsonArray{
            msg(QStringLiteral("system"), QStringLiteral("s")),
            msg(QStringLiteral("user"), QStringLiteral("pregunta")),
            msg(QStringLiteral("assistant"), QStringLiteral("respuesta"))}}
    }).toJson());
    source.close();

    QTemporaryDir cwd;
    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.cwd = cwd.path();
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:1");
    ctx.modelId = QStringLiteral("test");
    LlamaAgentBackend backend;
    backend.start(ctx);
    backend.forkSessionAtMessage(0);

    QCOMPARE(backend.sessions().size(), 2);
    const QVariantMap child = backend.sessions().first().toMap();
    QCOMPARE(child.value(QStringLiteral("parentSessionId")).toString(), parentId);
    QCOMPARE(child.value(QStringLiteral("forkMessageIndex")).toInt(), 0);
    QCOMPARE(child.value(QStringLiteral("depth")).toInt(), 1);
    QCOMPARE(backend.messages().size(), 1);
    QCOMPARE(backend.messages().first().toMap().value(QStringLiteral("content")).toString(),
             QStringLiteral("pregunta"));
    const QString childPath = store + QLatin1Char('/')
        + child.value(QStringLiteral("id")).toString() + QStringLiteral(".json");
    QVERIFY(QFile::exists(childPath));
    QFile childFile(childPath);
    QVERIFY(childFile.open(QIODevice::ReadOnly));
    const QJsonObject childSnapshot = QJsonDocument::fromJson(childFile.readAll()).object();
    QCOMPARE(childSnapshot.value(QStringLiteral("snapshotVersion")).toInt(), 2);
    QVERIFY(!childSnapshot.value(QStringLiteral("transcript")).toArray().isEmpty());
    QVERIFY(!childSnapshot.value(QStringLiteral("workingContext")).toArray().isEmpty());
    backend.stop();
    QDir(store).removeRecursively();
}

void AgentWireTests::taskSessionIsEphemeralAndRestoresPrevious()
{
    // Regresión: correr una Task/Automatización NO debe dejar su sesión en el
    // panel "Agente". La sesión de la Task es efímera (no listada, no persistida)
    // y al terminar se restaura la sesión del usuario.
    const QString store = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                          + QStringLiteral("/agent_llamaagent");
    QDir(store).removeRecursively();
    QVERIFY(QDir().mkpath(store));

    const QString sessionId = QStringLiteral("user-session");
    QFile indexFile(store + QStringLiteral("/index.json"));
    QVERIFY(indexFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    indexFile.write(QJsonDocument(QJsonArray{QJsonObject{
        {QStringLiteral("id"), sessionId},
        {QStringLiteral("title"), QStringLiteral("Del usuario")},
        {QStringLiteral("created"), 1.0}
    }}).toJson());
    indexFile.close();

    QFile sessionFile(store + QStringLiteral("/") + sessionId + QStringLiteral(".json"));
    QVERIFY(sessionFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    sessionFile.write(QJsonDocument(QJsonObject{
        {QStringLiteral("id"), sessionId},
        {QStringLiteral("title"), QStringLiteral("Del usuario")},
        {QStringLiteral("messages"), QJsonArray{QJsonObject{
            {QStringLiteral("role"), QStringLiteral("assistant")},
            {QStringLiteral("content"), QStringLiteral("mensaje del usuario")}
        }}},
        {QStringLiteral("api"), QJsonArray{}}
    }).toJson());
    sessionFile.close();

    QTemporaryDir cwd;
    QVERIFY(cwd.isValid());
    AgentContext ctx;
    ctx.adapter = QStringLiteral("llamaagent");
    ctx.cwd = cwd.path();
    ctx.serverBaseUrl = QStringLiteral("http://127.0.0.1:1");
    ctx.modelId = QStringLiteral("test-model");

    LlamaAgentBackend backend;
    backend.start(ctx);
    const int listedBefore = backend.sessions().size();
    QCOMPARE(listedBefore, 1);

    // Arranca la sesión efímera de la Task: no debe sumarse al panel.
    backend.newTaskSession();
    QCOMPARE(backend.sessions().size(), listedBefore);

    // Termina la Task: se restaura la sesión del usuario con sus mensajes.
    backend.endTaskSession();
    QCOMPARE(backend.sessions().size(), listedBefore);
    QVERIFY(!backend.messages().isEmpty());
    QCOMPARE(backend.messages().first().toMap().value(QStringLiteral("content")).toString(),
             QStringLiteral("mensaje del usuario"));

    backend.stop();
    QDir(store).removeRecursively();
}

// Helper: arma un delta de tool_calls como el que manda el server en streaming.
static QJsonArray tcDelta(int index, const QJsonObject &fn,
                          const QString &id = QString())
{
    QJsonObject tc{{QStringLiteral("index"), index},
                   {QStringLiteral("function"), fn}};
    if (!id.isEmpty()) tc.insert(QStringLiteral("id"), id);
    return QJsonArray{tc};
}

// El resumen de compactación se inyecta EN MEDIO del hilo: nunca con role=system
// (los templates que hoistean system al tope le borran la posición y corren el
// prefijo del prompt-cache). deepseek-v4 usa el rol que DS entrenó para esto.
void AgentWireTests::midConversationNoteRole_neverSystem()
{
    const QStringList others{
        QStringLiteral(""), QStringLiteral("local"),
        QStringLiteral("gemma-4-31b-it"), QStringLiteral("gpt-4.1"),
        QStringLiteral("deepseek-v3.1"), QStringLiteral("deepseek-r1"),
    };
    for (const QString &m : others) {
        const QString role = LlamaAgentBackend::midConversationNoteRole(m);
        QVERIFY2(role != QLatin1String("system"), qPrintable(m));
        QCOMPARE(role, QStringLiteral("user"));
    }

    const QStringList dsV4{
        QStringLiteral("DeepSeek-V4-Flash-0731"),
        QStringLiteral("deepseek-ai/DeepSeek-V4-Flash-0731"),
        QStringLiteral("deepseek_v4"),
        QStringLiteral("DeepSeek V4"),
    };
    for (const QString &m : dsV4)
        QCOMPARE(LlamaAgentBackend::midConversationNoteRole(m),
                 QStringLiteral("latest_reminder"));

    // El sanitizer degrada el system no inicial con el mismo criterio, y el
    // system de índice 0 (el prompt real) queda intacto en ambos casos.
    const QJsonArray in{
        msg(QStringLiteral("system"), QStringLiteral("base")),
        msg(QStringLiteral("user"), QStringLiteral("task")),
        msg(QStringLiteral("system"), QStringLiteral("[Resumen del contexto previo]"))
    };
    const QJsonArray plain = LlamaAgentBackend::sanitizeApiMessagesForWire(in);
    QCOMPARE(plain.at(0).toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("system"));
    QCOMPARE(plain.at(2).toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("user"));

    const QJsonArray ds = LlamaAgentBackend::sanitizeApiMessagesForWire(
        in, QStringLiteral("DeepSeek-V4-Flash-0731"));
    QCOMPARE(ds.at(0).toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("system"));
    QCOMPARE(ds.at(2).toObject().value(QStringLiteral("role")).toString(),
             QStringLiteral("latest_reminder"));
}

void AgentWireTests::mergeToolCallDelta_assemblesAcrossChunks()
{
    QHash<int, QJsonObject> acc;
    // Chunk 1: id + name. Chunk 2 y 3: arguments parciales (se concatenan).
    LlamaAgentBackend::mergeToolCallDelta(acc,
        tcDelta(0, QJsonObject{{QStringLiteral("name"), QStringLiteral("read_file")}},
                QStringLiteral("call_1")));
    LlamaAgentBackend::mergeToolCallDelta(acc,
        tcDelta(0, QJsonObject{{QStringLiteral("arguments"), QStringLiteral("{\"path\":\"")}}));
    LlamaAgentBackend::mergeToolCallDelta(acc,
        tcDelta(0, QJsonObject{{QStringLiteral("arguments"), QStringLiteral("x.txt\"}")}}));

    QCOMPARE(acc.size(), 1);
    const QJsonObject call = acc.value(0);
    QCOMPARE(call.value(QStringLiteral("id")).toString(), QStringLiteral("call_1"));
    QCOMPARE(call.value(QStringLiteral("name")).toString(), QStringLiteral("read_file"));
    QCOMPARE(call.value(QStringLiteral("arguments")).toString(),
             QStringLiteral("{\"path\":\"x.txt\"}"));
    // Los arguments deben parsear a JSON válido tras el ensamblado.
    const QJsonDocument args = QJsonDocument::fromJson(
        call.value(QStringLiteral("arguments")).toString().toUtf8());
    QVERIFY(args.isObject());
    QCOMPARE(args.object().value(QStringLiteral("path")).toString(), QStringLiteral("x.txt"));
}

void AgentWireTests::mergeToolCallDelta_parallelCallsByIndex()
{
    QHash<int, QJsonObject> acc;
    // Dos tool_calls paralelas (index 0 y 1) intercaladas en el stream.
    LlamaAgentBackend::mergeToolCallDelta(acc,
        tcDelta(0, QJsonObject{{QStringLiteral("name"), QStringLiteral("grep")}}, QStringLiteral("a")));
    LlamaAgentBackend::mergeToolCallDelta(acc,
        tcDelta(1, QJsonObject{{QStringLiteral("name"), QStringLiteral("list_dir")}}, QStringLiteral("b")));
    LlamaAgentBackend::mergeToolCallDelta(acc,
        tcDelta(0, QJsonObject{{QStringLiteral("arguments"), QStringLiteral("{\"q\":1}")}}));
    LlamaAgentBackend::mergeToolCallDelta(acc,
        tcDelta(1, QJsonObject{{QStringLiteral("arguments"), QStringLiteral("{\"p\":2}")}}));

    QCOMPARE(acc.size(), 2);
    QCOMPARE(acc.value(0).value(QStringLiteral("name")).toString(), QStringLiteral("grep"));
    QCOMPARE(acc.value(0).value(QStringLiteral("arguments")).toString(), QStringLiteral("{\"q\":1}"));
    QCOMPARE(acc.value(1).value(QStringLiteral("name")).toString(), QStringLiteral("list_dir"));
    QCOMPARE(acc.value(1).value(QStringLiteral("arguments")).toString(), QStringLiteral("{\"p\":2}"));
}

// visibleAnswer: con Pensar OFF quita <think>…</think>, PERO si el modelo metió
// toda la respuesta dentro de <think> (Qwen con thinking off) rescata el interior
// en vez de devolver vacío — esa era la causa de los saludos sin respuesta.
void AgentWireTests::visibleAnswer_stripsThinkButSalvagesWhenEmpty()
{
    using B = LlamaAgentBackend;
    // Caso normal: think + respuesta afuera → queda sólo la respuesta.
    QCOMPARE(B::visibleAnswer(QStringLiteral("<think>razono</think>Hola!"), false),
             QStringLiteral("Hola!"));
    // Caso bug: TODO dentro de think, nada afuera → no devolver vacío, rescatar.
    QCOMPARE(B::visibleAnswer(QStringLiteral("<think>Hola, ¿qué tal?</think>"), false),
             QStringLiteral("Hola, ¿qué tal?"));
    // Think sin cerrar (cortado por n_predict) y sin texto afuera → rescatar interior.
    QCOMPARE(B::visibleAnswer(QStringLiteral("<think>respuesta cortada"), false),
             QStringLiteral("respuesta cortada"));
    // Pensar ON: deja el content tal cual (la UI muestra el <think>).
    QCOMPARE(B::visibleAnswer(QStringLiteral("<think>r</think>R"), true),
             QStringLiteral("<think>r</think>R"));
    // Texto sin think: intacto.
    QCOMPARE(B::visibleAnswer(QStringLiteral("respuesta directa"), false),
             QStringLiteral("respuesta directa"));
    // Política estándar (default): no adivina que la cola sea repetición.
    QCOMPARE(B::visibleAnswer(
                 QStringLiteral("Resultado correcto.\n</think>\nResultado correcto."), false),
             QStringLiteral("Resultado correcto.\n\nResultado correcto."));
    // Un cierre huérfano al principio no debe borrar la respuesta posterior.
    QCOMPARE(B::visibleAnswer(QStringLiteral("</think>Respuesta útil"), false),
             QStringLiteral("Respuesta útil"));
    // Realmente vacío (sólo etiquetas) → vacío, no inventa.
    QVERIFY(B::visibleAnswer(QStringLiteral("<think></think>"), false).isEmpty());
}

void AgentWireTests::thinkingLeakGuard_isOptInAndControlsTemplate()
{
    using B = LlamaAgentBackend;
    const QString leaked = QStringLiteral(
        "Resultado correcto.\n</think>\nResultado correcto.");

    QCOMPARE(B::visibleAnswer(leaked, false, true), QStringLiteral("Resultado correcto."));
    QCOMPARE(B::visibleAnswer(leaked, false, false),
             QStringLiteral("Resultado correcto.\n\nResultado correcto."));

    const QJsonObject standard = B::thinkingTemplateKwargs(false, false);
    QCOMPARE(standard.value(QStringLiteral("enable_thinking")).toBool(), false);
    QVERIFY(!standard.contains(QStringLiteral("preserve_thinking")));
    QVERIFY(!standard.contains(QStringLiteral("reasoning_effort")));

    const QJsonObject deepSeekHigh = B::thinkingTemplateKwargs(true, false,
                                                               QStringLiteral("HIGH"));
    QCOMPARE(deepSeekHigh.value(QStringLiteral("reasoning_effort")).toString(),
             QStringLiteral("high"));
    QCOMPARE(B::thinkingTemplateKwargs(true, false, QStringLiteral("medium"))
                 .value(QStringLiteral("reasoning_effort")).toString(),
             QStringLiteral("medium"));
    QCOMPARE(B::thinkingTemplateKwargs(true, false, QStringLiteral("XHIGH"))
                 .value(QStringLiteral("reasoning_effort")).toString(),
             QStringLiteral("xhigh"));
    QVERIFY(!B::thinkingTemplateKwargs(true, false, QStringLiteral("bogus"))
                 .contains(QStringLiteral("reasoning_effort")));
    QVERIFY(!B::thinkingTemplateKwargs(false, false, QStringLiteral("max"))
                 .contains(QStringLiteral("reasoning_effort")));

    const QJsonObject guarded = B::thinkingTemplateKwargs(false, true);
    QCOMPARE(guarded.value(QStringLiteral("preserve_thinking")).toBool(), false);
}

void AgentWireTests::buildWarmupPayload_prefillsWithoutGenerating()
{
    const QJsonArray msgs{
        msg(QStringLiteral("system"), QStringLiteral("sys prompt")),
        msg(QStringLiteral("user"), QStringLiteral("turno previo"))
    };
    const QJsonArray tools{QJsonObject{{QStringLiteral("type"), QStringLiteral("function")}}};

    const QJsonObject p = LlamaAgentBackend::buildWarmupPayload(
        msgs, tools, QStringLiteral("local"), 0.7, true, QStringLiteral("medium"));

    // Prefijo idéntico al turno real: messages+tools+kwargs de template.
    QCOMPARE(p.value(QStringLiteral("messages")).toArray(), msgs);
    QCOMPARE(p.value(QStringLiteral("tools")).toArray(), tools);
    QCOMPARE(p.value(QStringLiteral("temperature")).toDouble(), 0.7);
    QCOMPARE(p.value(QStringLiteral("chat_template_kwargs")).toObject()
                 .value(QStringLiteral("enable_thinking")).toBool(), true);
    QCOMPARE(p.value(QStringLiteral("chat_template_kwargs")).toObject()
                 .value(QStringLiteral("reasoning_effort")).toString(),
             QStringLiteral("medium"));
    // Pero SIN generar: 1 token, sin stream, y con cache_prompt para el KV.
    QCOMPARE(p.value(QStringLiteral("max_tokens")).toInt(), 1);
    QCOMPARE(p.value(QStringLiteral("stream")).toBool(), false);
    QCOMPARE(p.value(QStringLiteral("cache_prompt")).toBool(), true);

    // Temperatura negativa (no seteada) → no se manda; thinking off → budget 0.
    const QJsonObject p2 = LlamaAgentBackend::buildWarmupPayload(
        msgs, tools, QStringLiteral("local"), -1.0, false);
    QVERIFY(!p2.contains(QStringLiteral("temperature")));
    QCOMPARE(p2.value(QStringLiteral("reasoning_budget")).toInt(), 0);
}

void AgentWireTests::trimStaleImages_keepsOnlyLatestCapture()
{
    auto imgMsg = [](const QString &txt, const QString &uri) {
        return QJsonObject{
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("content"), QJsonArray{
                QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                            {QStringLiteral("text"), txt}},
                QJsonObject{{QStringLiteral("type"), QStringLiteral("image_url")},
                            {QStringLiteral("image_url"),
                             QJsonObject{{QStringLiteral("url"), uri}}}}}}};
    };
    const QJsonArray in{
        msg(QStringLiteral("system"), QStringLiteral("sys")),
        imgMsg(QStringLiteral("captura 1"), QStringLiteral("data:1")),
        msg(QStringLiteral("assistant"), QStringLiteral("veo la pantalla")),
        imgMsg(QStringLiteral("captura 2"), QStringLiteral("data:2")),
    };

    const QJsonArray out = LlamaAgentBackend::trimStaleImages(in, 1);
    QCOMPARE(out.size(), 4);
    // Captura vieja: image_url reemplazada por placeholder de texto.
    const QJsonArray parts1 = out[1].toObject().value(QStringLiteral("content")).toArray();
    for (const QJsonValue &p : parts1)
        QVERIFY(p.toObject().value(QStringLiteral("type")).toString() != QLatin1String("image_url"));
    QVERIFY(parts1.last().toObject().value(QStringLiteral("text")).toString()
                .contains(QStringLiteral("omitida")));
    // El texto original del mensaje se conserva.
    QCOMPARE(parts1.first().toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("captura 1"));
    // Última captura: intacta.
    const QJsonArray parts3 = out[3].toObject().value(QStringLiteral("content")).toArray();
    QCOMPARE(parts3.last().toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("image_url"));
    // Mensajes sin imagen: intactos (content string no se toca).
    QCOMPARE(out[2].toObject().value(QStringLiteral("content")).toString(),
             QStringLiteral("veo la pantalla"));

    // keepLast=0: ninguna imagen sobrevive (pre-append de una captura nueva).
    const QJsonArray none = LlamaAgentBackend::trimStaleImages(in, 0);
    const QJsonArray p3 = none[3].toObject().value(QStringLiteral("content")).toArray();
    for (const QJsonValue &p : p3)
        QVERIFY(p.toObject().value(QStringLiteral("type")).toString() != QLatin1String("image_url"));
}

void AgentWireTests::buildObservationMessage_wrapsImagesAsUserMultimodal()
{
    // Una captura (desktop_observe) → mensaje role=user con content multimodal:
    // [texto, image_url]. El image_url lleva la data-URI tal cual.
    const QString uriA = QStringLiteral("data:image/jpeg;base64,AAAA");
    const QString uriB = QStringLiteral("data:image/png;base64,BBBB");
    const QJsonObject m = LlamaAgentBackend::buildObservationMessage({uriA, QString(), uriB});

    QCOMPARE(m.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    const QJsonArray content = m.value(QStringLiteral("content")).toArray();
    // 1 bloque de texto + 2 imágenes (el "" se descarta).
    QCOMPARE(content.size(), 3);
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));
    QVERIFY(!content.at(0).toObject().value(QStringLiteral("text")).toString().isEmpty());

    QStringList urls;
    for (int i = 1; i < content.size(); ++i) {
        const QJsonObject part = content.at(i).toObject();
        QCOMPARE(part.value(QStringLiteral("type")).toString(), QStringLiteral("image_url"));
        urls << part.value(QStringLiteral("image_url")).toObject()
                    .value(QStringLiteral("url")).toString();
    }
    QCOMPARE(urls, QStringList({uriA, uriB}));   // orden preservado, "" omitido
}

void AgentWireTests::buildObservationMessage_emptyWhenNoImages()
{
    // Sin imágenes válidas → objeto vacío (el loop no inyecta nada).
    QVERIFY(LlamaAgentBackend::buildObservationMessage({}).isEmpty());
    QVERIFY(LlamaAgentBackend::buildObservationMessage({QString(), QString()}).isEmpty());
}

void AgentWireTests::developmentDisciplineSection_coversRegressionGuards()
{
    // Regresión: la guía anti-regresión debe seguir presente en el system prompt.
    // Si alguien la borra/vacía, este test cae. Verificamos los 4 pilares:
    // blast radius, cambio mínimo, preservar contratos, verificar/tests.
    const QString g = LlamaAgentBackend::developmentDisciplineSection();
    QVERIFY(!g.trimmed().isEmpty());

    // Pilar 1: conocer quién usa el código antes de tocarlo (blast radius).
    QVERIFY(g.contains(QStringLiteral("Blast radius"), Qt::CaseInsensitive));
    QVERIFY(g.contains(QStringLiteral("grep")));
    // Pilar 2: cambio mínimo, no tocar lo no relacionado.
    QVERIFY(g.contains(QStringLiteral("mínimo"), Qt::CaseInsensitive));
    // Pilar 3: preservar comportamiento/contratos existentes.
    QVERIFY(g.contains(QStringLiteral("comportamiento"), Qt::CaseInsensitive)
            || g.contains(QStringLiteral("contrato"), Qt::CaseInsensitive));
    // Pilar 4: correr tests/compilar al terminar.
    QVERIFY(g.contains(QStringLiteral("test"), Qt::CaseInsensitive));
    // Pilar 5: entregables ejecutables (scripts .bat/.sh) se corren antes de darlos.
    QVERIFY(g.contains(QStringLiteral(".bat")));
    QVERIFY(g.contains(QStringLiteral("EJECUTALO"), Qt::CaseInsensitive));
    // Pilar 6: validar condiciones de inicio Y runtime, no solo que arranque.
    QVERIFY(g.contains(QStringLiteral("runtime"), Qt::CaseInsensitive));
    QVERIFY(g.contains(QStringLiteral("end-to-end"), Qt::CaseInsensitive));

    // Cableado: buildSystemPrompt() la incluye. No es accesible directo (privada),
    // pero la sección es lo que se concatena tal cual → testear la sección cubre
    // el contenido shippeado. El cableado se verifica por inspección de start().
    QVERIFY(g.endsWith(QStringLiteral("\n\n")));   // separador para no pegarse al ESTILO
}

void AgentWireTests::testSafetyNetSection_coversRunnerDetectionAndQuality()
{
    // Regresión: la guía de "red de tests" debe seguir en el system prompt y
    // cubrir sus pilares. Si se borra/vacía o pierde un pilar, este test cae.
    const QString g = LlamaAgentBackend::testSafetyNetSection();
    QVERIFY(!g.trimmed().isEmpty());

    // Pilar 1: detectar el runner que ya usa el proyecto (varios ecosistemas).
    QVERIFY(g.contains(QStringLiteral("runner"), Qt::CaseInsensitive));
    QVERIFY(g.contains(QStringLiteral("package.json")));
    QVERIFY(g.contains(QStringLiteral("ctest")));
    QVERIFY(g.contains(QStringLiteral("pytest")));
    // Pilar 2: test caja-negra, NO asserts triviales (el reclamo del thread).
    QVERIFY(g.contains(QStringLiteral("caja-negra"), Qt::CaseInsensitive)
            || g.contains(QStringLiteral("caja negra"), Qt::CaseInsensitive));
    QVERIFY(g.contains(QStringLiteral("trivial"), Qt::CaseInsensitive));
    // Pilar 3: registrar el test para que el runner lo levante solo.
    QVERIFY(g.contains(QStringLiteral("Registr"), Qt::CaseInsensitive));
    // Pilar 4: correr el suite y no entregar en rojo.
    QVERIFY(g.contains(QStringLiteral("suite"), Qt::CaseInsensitive));
    QVERIFY(g.contains(QStringLiteral("rojo"), Qt::CaseInsensitive));
    // Pilar 5: fallback cuando el proyecto no tiene tests (no meter framework pesado).
    QVERIFY(g.contains(QStringLiteral("smoke"), Qt::CaseInsensitive));

    QVERIFY(g.endsWith(QStringLiteral("\n\n")));   // separa de la sección siguiente
}

void AgentWireTests::projectContextSection_coversIntentAndMemory()
{
    // Regresión: la guía de contexto de proyecto debe seguir en el prompt y cubrir
    // sus pilares (entender el porqué, co-cambios por git, dejar memoria durable).
    const QString g = LlamaAgentBackend::projectContextSection();
    QVERIFY(!g.trimmed().isEmpty());

    // Pilar 1: entender el porqué via git history antes de tocar; no romper workarounds.
    QVERIFY(g.contains(QStringLiteral("git blame"))
            || g.contains(QStringLiteral("git log")));
    QVERIFY(g.contains(QStringLiteral("workaround"), Qt::CaseInsensitive));
    // Pilar 2: co-cambios / acoplamiento sin import visible.
    QVERIFY(g.contains(QStringLiteral("Co-cambios"), Qt::CaseInsensitive)
            || g.contains(QStringLiteral("acopl"), Qt::CaseInsensitive));
    // Pilar 3: dejar decisiones durables en el archivo de memoria del proyecto.
    QVERIFY(g.contains(QStringLiteral(".llamacode/memory.md")));
    QVERIFY(g.contains(QStringLiteral("durable"), Qt::CaseInsensitive));
    // El archivo citado debe coincidir con el real que carga buildSystemPrompt.
    QVERIFY(g.contains(LlamaAgentBackend::memoryFilePath(QStringLiteral("x"))
                       .section(QLatin1Char('/'), -2)));   // ".llamacode/memory.md"

    QVERIFY(g.endsWith(QStringLiteral("\n\n")));
}

void AgentWireTests::desktopPlaybookSection_coversKeyboardPathAndTextVerify()
{
    // Regresión: el playbook de escritorio debe guiar el camino rápido (teclado)
    // y la verificación por TEXTO sin visión, para no flailar con capturas ciegas
    // (bug: "sumar 2+2 en la calculadora" tardaba y nunca completaba).
    const QString novis = LlamaAgentBackend::desktopPlaybookSection(false);
    QVERIFY(!novis.trimmed().isEmpty());

    // Pilar 1: abrir con desktop_launch, no run_shell.
    QVERIFY(novis.contains(QStringLiteral("desktop_launch")));
    // Pilar 2: camino rápido por teclado generalizable para calculadora.
    QVERIFY(novis.contains(QStringLiteral("desktop_type")));
    QVERIFY(novis.contains(QStringLiteral("<expresión>=")));
    QVERIFY(novis.contains(QStringLiteral("desktop_key ESC")));
    // Pilar 3: verificar por texto vía desktop_controls (UIA), no por captura.
    QVERIFY(novis.contains(QStringLiteral("desktop_controls")));
    // Pilar 4: sin visión, NO usar desktop_observe (evita el loop ciego).
    QVERIFY(novis.contains(QStringLiteral("desktop_observe")));
    QVERIFY(novis.contains(QStringLiteral("loop"), Qt::CaseInsensitive));
    QVERIFY(novis.endsWith(QStringLiteral("\n\n")));

    // Con visión, sí se ofrece desktop_observe como recurso extra.
    const QString vis = LlamaAgentBackend::desktopPlaybookSection(true);
    QVERIFY(vis.contains(QStringLiteral("mmproj")));
    QVERIFY(vis.contains(QStringLiteral("desktop_observe")));
    QVERIFY(vis.contains(QStringLiteral("0..1000")));
    QVERIFY(vis.contains(QStringLiteral("dividí x e y por")));
    QVERIFY(vis.contains(QStringLiteral("no hagas clic"), Qt::CaseInsensitive));
    QVERIFY(vis.contains(QStringLiteral("verificá el cambio"), Qt::CaseInsensitive));
    // La variante sin visión debe advertir explícitamente que NO puede ver.
    QVERIFY(novis.contains(QStringLiteral("NO")) );
    QVERIFY(novis != vis);
}

void AgentWireTests::desktopConfirmKeyBlockedAfterTypeEquals()
{
    QVERIFY(LlamaAgentBackend::redundantDesktopConfirmKey(
        QStringLiteral("desktop_type"), QStringLiteral("2+2="), QStringLiteral("ENTER")));
    QVERIFY(LlamaAgentBackend::redundantDesktopConfirmKey(
        QStringLiteral("desktop_type"), QStringLiteral("2+2="), QStringLiteral("=")));
    QVERIFY(!LlamaAgentBackend::redundantDesktopConfirmKey(
        QStringLiteral("desktop_type"), QStringLiteral("2+2"), QStringLiteral("ENTER")));
    QVERIFY(!LlamaAgentBackend::redundantDesktopConfirmKey(
        QStringLiteral("desktop_focus"), QStringLiteral("2+2="), QStringLiteral("ENTER")));
    QVERIFY(!LlamaAgentBackend::redundantDesktopConfirmKey(
        QStringLiteral("desktop_type"), QStringLiteral("hola="), QStringLiteral("TAB")));
}

void AgentWireTests::textToolsModeDoesNotDoubleReserveToolBudget()
{
    // Regresión del bug "sumar 2+2": con n_ctx chico (8192) y en modo text-tools
    // (fallback headless), la compactación disparaba en CADA turno porque el budget
    // reservaba el esquema OpenAI completo de tools (~miles de tok) que en ese modo
    // NO se manda como payload (va embebido por nombre en el system prompt, ya
    // contado). Resultado: se resumía el historial cada 1-2 tools y la Task se
    // rompía tras 4-5 acciones. Con el fix, el mismo historial NO gatilla
    // compactación en modo text-tools, pero SÍ en modo nativo (donde el esquema
    // realmente ocupa contexto).
    auto buildHistory = []() {
        QJsonArray msgs;
        msgs.append(QJsonObject{{"role", "system"}, {"content", QString(400, 'x')}});
        msgs.append(QJsonObject{{"role", "user"}, {"content", QString(400, 'y')}});
        // ~3000 tokens de cuerpo (≈12k chars) repartidos en varios turnos.
        for (int i = 0; i < 6; ++i) {
            msgs.append(QJsonObject{{"role", "assistant"}, {"content", QString(1000, 'a')}});
            msgs.append(QJsonObject{{"role", "user"}, {"content", QString(1000, 'b')}});
        }
        return msgs;
    };

    int head = 0, keepFrom = 0;

    // Modo nativo: el esquema de tools SÍ reserva contexto → budget chico → compacta.
    LlamaAgentBackend nativeBe;
    nativeBe.setCtxLimitForTest(8192);
    nativeBe.setApiMessagesForTest(buildHistory());
    QVERIFY2(nativeBe.planCompactionForTest(head, keepFrom),
             "modo nativo: con el esquema de tools reservado, este historial debe compactar");

    // Modo text-tools: no se reserva el esquema → budget amplio → NO compacta.
    LlamaAgentBackend textBe;
    textBe.setForceTextTools(true);
    textBe.setCtxLimitForTest(8192);
    textBe.setApiMessagesForTest(buildHistory());
    QVERIFY2(!textBe.planCompactionForTest(head, keepFrom),
             "modo text-tools: el mismo historial NO debe disparar compactación");
}

void AgentWireTests::compactionStallCounterTracksProgress()
{
    // Regresión del loop infinito de compactación (12GB, n_ctx=16384): cuando el
    // tramo compactable es chico y lo pesado es el head protegido (system+objetivo),
    // el resumen no baja tokens (2678→2678) y runCompletion recompactaba sin fin.
    // El contador de estancamiento sube cuando un pase NO reduce y se resetea
    // cuando sí; runCompletion corta al llegar a 2 (no lo ejercitamos acá porque
    // requiere red, pero sí la contabilidad que lo gobierna).
    auto history = []() {
        QJsonArray m;
        m.append(QJsonObject{{"role", "system"}, {"content", QString(400, 'x')}});
        m.append(QJsonObject{{"role", "user"},   {"content", QString(400, 'y')}});
        m.append(QJsonObject{{"role", "assistant"}, {"content", QString(1000, 'a')}});
        m.append(QJsonObject{{"role", "user"},      {"content", QString(1000, 'b')}});
        return m;
    };
    LlamaAgentBackend be;
    be.setCtxLimitForTest(8192);

    // Resumen tan grande como lo removido → sin reducción → stall sube.
    be.setApiMessagesForTest(history());
    be.applyCompactionForTest(2, 4, QString(2000, 'z'));
    QCOMPARE(be.compactStallForTest(), 1);
    be.setApiMessagesForTest(history());
    be.applyCompactionForTest(2, 4, QString(2000, 'z'));
    QCOMPARE(be.compactStallForTest(), 2);   // 2 → runCompletion deja de compactar

    // Resumen chico → reduce de verdad → resetea el contador.
    be.setApiMessagesForTest(history());
    be.applyCompactionForTest(2, 4, QStringLiteral("ok"));
    QCOMPARE(be.compactStallForTest(), 0);
}

void AgentWireTests::compactionPreservesImmutableTranscript()
{
    QJsonArray history{
        QJsonObject{{"role", "system"}, {"content", "system"}},
        QJsonObject{{"role", "user"}, {"content", "objetivo"}},
        QJsonObject{{"role", "assistant"}, {"content", QString(2000, 'a')}},
        QJsonObject{{"role", "user"}, {"content", QString(2000, 'b')}},
        QJsonObject{{"role", "assistant"}, {"content", "cola"}},
        QJsonObject{{"role", "user"}, {"content", "seguí"}}
    };
    LlamaAgentBackend be;
    be.setCtxLimitForTest(8192);
    be.setApiMessagesForTest(history);
    be.applyCompactionForTest(2, 4,
        QStringLiteral("{\"goal\":\"g\",\"completed\":[\"x\"],\"decisions\":[],"
                       "\"artifacts\":[],\"failed_approaches\":[],"
                       "\"open_questions\":[],\"next_action\":\"n\",\"evidence\":[]}"));

    QCOMPARE(be.transcriptMessagesForTest(), history);
    QVERIFY(be.apiMessagesForTest().size() < history.size());
    QVERIFY(be.apiMessagesForTest().at(2).toObject().value("content").toString()
                .contains(QStringLiteral("\"next_action\":\"n\"")));
}

void AgentWireTests::deterministicPruningDeduplicatesOnlyWorkingContext()
{
    auto assistantCall = [](const QString &id) {
        return QJsonObject{{"role", "assistant"}, {"content", ""},
            {"tool_calls", QJsonArray{QJsonObject{
                {"id", id}, {"type", "function"},
                {"function", QJsonObject{{"name", "grep"},
                    {"arguments", "{\"pattern\":\"needle\"}"}}}}}}};
    };
    QJsonArray history{
        QJsonObject{{"role", "system"}, {"content", "system"}},
        QJsonObject{{"role", "user"}, {"content", "objetivo"}},
        assistantCall("old"),
        QJsonObject{{"role", "tool"}, {"tool_call_id", "old"}, {"content", QString(1000, 'x')}},
        QJsonObject{{"role", "assistant"}, {"content", "intermedio"}},
        QJsonObject{{"role", "user"}, {"content", "otra vez"}},
        assistantCall("new"),
        QJsonObject{{"role", "tool"}, {"tool_call_id", "new"}, {"content", QString(1000, 'y')}}
    };
    LlamaAgentBackend be;
    be.setApiMessagesForTest(history);
    QCOMPARE(be.pruneWorkingContextForTest(), 1);

    QCOMPARE(be.transcriptMessagesForTest(), history);
    QCOMPARE(be.apiMessagesForTest().at(3).toObject().value("content").toString(),
             QStringLiteral("[resultado repetido podado; usar la llamada equivalente más reciente]"));
    QCOMPARE(be.apiMessagesForTest().at(7).toObject().value("content").toString(),
             QString(1000, 'y'));
}

void AgentWireTests::textToolPayloadCapsGenerationAndStopsAtToolCall()
{
    // Regresión del bug "sumar 2+2": tras arreglar parser y compactación, la Task
    // avanzaba hasta desktop_focus pero el turno siguiente colgaba ~5 min. El
    // modelo (Gemma) escupía su formato nativo <|tool_call>...<tool_call|> y SEGUÍA
    // generando hasta max_tokens (ctx/4 = 2048 → ~5 min de decode local). El payload
    // text-tools debe (a) capar max_tokens y (b) traer stop en los marcadores de
    // cierre para cortar apenas se emite el tool-call.
    LlamaAgentBackend be;
    QJsonObject nativePayload{
        {QStringLiteral("model"), QStringLiteral("local")},
        {QStringLiteral("messages"), QJsonArray{
            QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                        {QStringLiteral("content"), QStringLiteral("sys")}}}},
        {QStringLiteral("max_tokens"), 2048}};

    const QJsonObject p = be.buildTextToolPayloadForTest(nativePayload);

    // (a) max_tokens capado (no los 2048 originales que causaban el ramble largo).
    QVERIFY(p.value(QStringLiteral("max_tokens")).toInt() <= 1536);
    QVERIFY(p.value(QStringLiteral("max_tokens")).toInt() >= 256);

    // (b) stop cubre el marcador de cierre nativo que vimos en el log.
    const QJsonArray stop = p.value(QStringLiteral("stop")).toArray();
    QVERIFY(!stop.isEmpty());
    bool hasClose = false;
    for (const QJsonValue &v : stop)
        if (v.toString() == QStringLiteral("<tool_call|>")) hasClose = true;
    QVERIFY2(hasClose, "stop debe incluir el cierre <tool_call|>");

    // (c) el protocolo instruye NO razonar/usar <think> (evita el turno vacío que
    // rompía la Task 2+2 tras desktop_focus).
    const QString sysContent = p.value(QStringLiteral("messages")).toArray()
                                   .first().toObject().value(QStringLiteral("content")).toString();
    QVERIFY(sysContent.contains(QStringLiteral("<think>")));
    QVERIFY(sysContent.contains(QStringLiteral("Nunca respondas vacío")));
}

void AgentWireTests::adaptiveSubagentLimit_respectsProfileContextAndVram()
{
    using B = LlamaAgentBackend;
    QCOMPARE(B::adaptiveSubagentLimit(1, 8192, 24576), 1);
    QCOMPARE(B::adaptiveSubagentLimit(4, 8192, 24576), 4);
    QCOMPARE(B::adaptiveSubagentLimit(8, 8192, 49152), 5); // hard safety cap
    QCOMPARE(B::adaptiveSubagentLimit(4, 131072, 49152), 1);
    QCOMPARE(B::adaptiveSubagentLimit(4, 65536, 49152), 2);
    QCOMPARE(B::adaptiveSubagentLimit(4, 32768, 49152), 3);
    QCOMPARE(B::adaptiveSubagentLimit(4, 8192, 6144), 1);
    QCOMPARE(B::adaptiveSubagentLimit(4, 8192, 10240), 2);
    QCOMPARE(B::adaptiveSubagentLimit(4, 8192, 24576, 300), 1);
    QCOMPARE(B::adaptiveSubagentLimit(4, 8192, 24576, 700), 2);
    // Sin telemetría no inventa una restricción: manda el perfil configurado.
    QCOMPARE(B::adaptiveSubagentLimit(3, 8192, 0), 3);
}

void AgentWireTests::isDestructiveAction_gatesShellDesktopMemory()
{
    using B = LlamaAgentBackend;
    auto shell = [](const QString &cmd) {
        return QJsonObject{{QStringLiteral("command"), cmd}};
    };

    // ── Shell destructivo → true ──────────────────────────────────────────
    QVERIFY(B::isDestructiveAction("run_shell", shell("rm -rf build/")));
    QVERIFY(B::isDestructiveAction("run_shell", shell("rm -fr /tmp/x")));
    QVERIFY(B::isDestructiveAction("run_shell", shell("del /s /q C:\\data")));
    QVERIFY(B::isDestructiveAction("run_shell", shell("rmdir /s foo")));
    QVERIFY(B::isDestructiveAction("run_shell", shell("format C:")));
    QVERIFY(B::isDestructiveAction("run_shell", shell("dd if=/dev/zero of=/dev/sda")));
    QVERIFY(B::isDestructiveAction("run_shell", shell("git push --force origin main")));
    QVERIFY(B::isDestructiveAction("run_shell", shell("git reset --hard HEAD~3")));
    QVERIFY(B::isDestructiveAction("run_shell", shell("shutdown /s /t 0")));
    QVERIFY(B::isDestructiveAction("run_shell", shell("DROP TABLE users")));
    QVERIFY(B::isDestructiveAction("run_shell", shell("echo ok; rm -rf .")));
    // MCP-prefijado también clasifica por el nombre bare.
    QVERIFY(B::isDestructiveAction("mcp__fs__run_shell", shell("rm -rf x")));

    // ── Shell benigno → false ─────────────────────────────────────────────
    QVERIFY(!B::isDestructiveAction("run_shell", shell("ls -la")));
    QVERIFY(!B::isDestructiveAction("run_shell", shell("git push origin main")));
    QVERIFY(!B::isDestructiveAction("run_shell", shell("npm test")));
    QVERIFY(!B::isDestructiveAction("run_shell", shell("grep -rf pattern src")));

    // ── Desktop click destructivo → true / benigno → false ────────────────
    QVERIFY(B::isDestructiveAction("desktop_click_element",
                                   QJsonObject{{QStringLiteral("name"), QStringLiteral("Eliminar archivo")}}));
    QVERIFY(B::isDestructiveAction("desktop_click_element",
                                   QJsonObject{{QStringLiteral("name"), QStringLiteral("Delete")}}));
    QVERIFY(!B::isDestructiveAction("desktop_click_element",
                                    QJsonObject{{QStringLiteral("name"), QStringLiteral("Guardar")}}));

    // ── Desktop: resolución control_id → nombre vía texto de desktop_controls ──
    // Formato real del worker: 'controlId=<id>  [role]...  "<name>"' por línea.
    const QString controls =
        QStringLiteral("[desktop_controls: 2 control(es)]\n"
                       "Usá el controlId con desktop_click_element (mismo target_id).\n"
                       "controlId=42-7  [Button] invoke  80x24@(10,10)  \"Eliminar\"\n"
                       "controlId=42-8  [Button] invoke  80x24@(10,40)  \"Guardar\"");
    QVERIFY(B::isDestructiveAction("desktop_click_element",
                                   QJsonObject{{QStringLiteral("control_id"), QStringLiteral("42-7")}},
                                   controls));
    QVERIFY(!B::isDestructiveAction("desktop_click_element",
                                    QJsonObject{{QStringLiteral("control_id"), QStringLiteral("42-8")}},
                                    controls));
    // Sin texto de controles no puede resolver el label → no gatea (fail-open acá;
    // el gate del loop igual pasa m_lastDesktopResult real).
    QVERIFY(!B::isDestructiveAction("desktop_click_element",
                                    QJsonObject{{QStringLiteral("control_id"), QStringLiteral("42-7")}}));
    // Id que no matchea ninguna línea ni ningún nombre → no gatea.
    QVERIFY(!B::isDestructiveAction("desktop_click_element",
                                    QJsonObject{{QStringLiteral("control_id"), QStringLiteral("99-9")}},
                                    controls));

    // ── control_id que en realidad es un NOMBRE ───────────────────────────
    // clickElement resuelve por parecido cuando el id no existe, así que un
    // control_id="Eliminar" SÍ termina clickeando el botón destructivo. El gate
    // tiene que verlo: si sólo mirara el id exacto, el label quedaría vacío y la
    // acción pasaría como benigna sin aprobación humana.
    QVERIFY(B::isDestructiveAction("desktop_click_element",
                                   QJsonObject{{QStringLiteral("control_id"), QStringLiteral("Eliminar")}},
                                   controls));
    // Con las variantes que el modelo suele escribir (acelerador, elipsis, tildes).
    QVERIFY(B::isDestructiveAction("desktop_click_element",
                                   QJsonObject{{QStringLiteral("control_id"), QStringLiteral("&Eliminar...")}},
                                   controls));
    // Aun sin cache de controles: el propio nombre alcanza para clasificarlo.
    QVERIFY(B::isDestructiveAction("desktop_click_element",
                                   QJsonObject{{QStringLiteral("control_id"), QStringLiteral("Eliminar todo")}}));
    // Nombre benigno sigue sin gatear (el fix no vuelve todo destructivo).
    QVERIFY(!B::isDestructiveAction("desktop_click_element",
                                    QJsonObject{{QStringLiteral("control_id"), QStringLiteral("Guardar")}},
                                    controls));

    // ── desktop_click_text (OCR) gatea igual que el path UIA ──────────────
    // El texto pedido ES el label. Llegar al botón por OCR en vez de por UIA no
    // puede ser una forma de saltear la aprobación humana.
    QVERIFY(B::isDestructiveAction("desktop_click_text",
                                   QJsonObject{{QStringLiteral("text"), QStringLiteral("Eliminar")}}));
    QVERIFY(B::isDestructiveAction("desktop_click_text",
                                   QJsonObject{{QStringLiteral("text"), QStringLiteral("Vaciar papelera")}}));
    QVERIFY(!B::isDestructiveAction("desktop_click_text",
                                    QJsonObject{{QStringLiteral("text"), QStringLiteral("Guardar")}}));

    // ── Memory / graph delete → true; lectura → false ─────────────────────
    QVERIFY(B::isDestructiveAction("memory", QJsonObject{{QStringLiteral("action"), QStringLiteral("forget")}}));
    QVERIFY(B::isDestructiveAction("memory", QJsonObject{{QStringLiteral("action"), QStringLiteral("prune")}}));
    QVERIFY(!B::isDestructiveAction("memory", QJsonObject{{QStringLiteral("action"), QStringLiteral("prune")},
                                                          {QStringLiteral("dry_run"), true}}));
    QVERIFY(!B::isDestructiveAction("memory", QJsonObject{{QStringLiteral("action"), QStringLiteral("recall")}}));
    QVERIFY(B::isDestructiveAction("graph", QJsonObject{{QStringLiteral("action"), QStringLiteral("delete")}}));
    QVERIFY(!B::isDestructiveAction("graph", QJsonObject{{QStringLiteral("action"), QStringLiteral("query")}}));

    // ── Tools no cubiertas → false ────────────────────────────────────────
    QVERIFY(!B::isDestructiveAction("read_file", QJsonObject{{QStringLiteral("path"), QStringLiteral("x")}}));
    QVERIFY(!B::isDestructiveAction("write_file", QJsonObject{{QStringLiteral("path"), QStringLiteral("x")}}));
}

QTEST_MAIN(AgentWireTests)
#include "test_agent_wire.moc"
