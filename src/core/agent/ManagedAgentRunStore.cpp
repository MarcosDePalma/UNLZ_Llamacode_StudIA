#include "ManagedAgentRunStore.h"

#include "core/tasks/RunHistoryStore.h"
#include "core/agent/AgentDeliverableStore.h"
#include "core/agent/WorkRegistry.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString asString(const QVariantMap &map, const QString &key)
{
    return map.value(key).toString().trimmed();
}

QString joinCommand(const QString &program, const QStringList &args,
                    const QString &prompt = QString())
{
    QStringList safe;
    safe.reserve(args.size());
    for (const QString &arg : args) {
        // Nunca se persiste el prompt como parte del comando visible.
        if ((!prompt.isEmpty() && arg == prompt) || arg.size() > 180
            || arg.contains(QRegularExpression(QStringLiteral("\\s"))))
            safe << QStringLiteral("<prompt>");
        else
            safe << arg;
    }
    return QDir::toNativeSeparators(program + QStringLiteral(" ") + safe.join(QLatin1Char(' ')));
}
}

ManagedAgentRunStore::ManagedAgentRunStore(RunHistoryStore *history, QObject *parent)
    : QObject(parent), m_history(history)
{
    m_housekeepingTimer = new QTimer(this);
    m_housekeepingTimer->setInterval(5000);
    connect(m_housekeepingTimer, &QTimer::timeout, this,
            &ManagedAgentRunStore::housekeeping);
    m_housekeepingTimer->start();
    loadPersistedRuns();
    cleanupRetention();
}

QString ManagedAgentRunStore::nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString ManagedAgentRunStore::newRunId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
}

QString ManagedAgentRunStore::tail(const QString &text, int maxChars)
{
    if (text.size() <= maxChars) return text;
    return QStringLiteral("…") + text.right(maxChars - 1);
}

QString ManagedAgentRunStore::runtimeName(const QVariantMap &request)
{
    return asString(request, QStringLiteral("runtime")).toLower();
}

bool ManagedAgentRunStore::validRuntime(const QString &runtime)
{
    return runtime == QLatin1String("claude") || runtime == QLatin1String("codex");
}

QString ManagedAgentRunStore::resolveCliPath(const QVariantMap &request)
{
    const QString explicitPath = asString(request, QStringLiteral("cliPath"));
    if (!explicitPath.isEmpty()) return explicitPath;
    const QString runtime = runtimeName(request);
    return QStandardPaths::findExecutable(runtime);
}

QVariantMap ManagedAgentRunStore::commandForRequest(const QVariantMap &request)
{
    const QString runtime = runtimeName(request);
    const QString prompt = request.value(QStringLiteral("prompt")).toString();
    const QString program = resolveCliPath(request);
    // Wrapper CLIs (o un probe de integración) pueden aportar argumentos de
    // entrada antes del contrato del runtime. En uso normal queda vacío.
    QStringList args = request.value(QStringLiteral("prefixArgs")).toStringList();
    QString permissionPosture = QStringLiteral("default");

    const QString requestedTransport = asString(request, QStringLiteral("promptTransport"));
    const QString promptTransport = requestedTransport.isEmpty()
        ? QStringLiteral("stdin") : requestedTransport;

    if (runtime == QLatin1String("claude")) {
        args << QStringLiteral("-p");
        if (promptTransport == QLatin1String("argv")) args << prompt;
        const QString model = asString(request, QStringLiteral("model"));
        if (!model.isEmpty()) args << QStringLiteral("--model") << model;
        const QString approval = asString(request, QStringLiteral("approvalMode"));
        if (request.value(QStringLiteral("applyEdits"), false).toBool()) {
            // acceptEdits permite que el CLI edite el workspace, pero conserva
            // el límite explícito del modo de aprobación. Nunca se agrega el
            // bypass de permisos automáticamente.
            args << QStringLiteral("--permission-mode")
                 << (approval == QLatin1String("plan")
                         ? QStringLiteral("plan")
                         : QStringLiteral("acceptEdits"));
            permissionPosture = approval == QLatin1String("plan")
                ? QStringLiteral("plan") : QStringLiteral("acceptEdits");
        } else {
            args << QStringLiteral("--permission-mode") << QStringLiteral("plan");
            permissionPosture = QStringLiteral("plan");
        }
    } else if (runtime == QLatin1String("codex")) {
        args << QStringLiteral("exec");
        const QString model = asString(request, QStringLiteral("model"));
        if (!model.isEmpty()) args << QStringLiteral("--model") << model;
        const bool dangerous = request.value(QStringLiteral("allowDangerous"), false).toBool();
        const QString approval = asString(request, QStringLiteral("approvalMode"));
        if (request.value(QStringLiteral("applyEdits"), false).toBool()
            && approval == QLatin1String("super") && dangerous) {
            args << QStringLiteral("--full-auto");
            permissionPosture = QStringLiteral("full-auto-explicit");
        } else if (request.value(QStringLiteral("applyEdits"), false).toBool()) {
            permissionPosture = QStringLiteral("default-with-edits-requested");
        } else {
            permissionPosture = QStringLiteral("default-read-only-intent");
        }
        if (promptTransport == QLatin1String("argv")) args << prompt;
    }

    return {
        {QStringLiteral("program"), program},
        {QStringLiteral("args"), args},
        {QStringLiteral("permissionPosture"), permissionPosture},
        {QStringLiteral("promptTransport"), promptTransport},
        {QStringLiteral("display"), joinCommand(program, args,
                                                  promptTransport == QLatin1String("argv")
                                                      ? prompt : QString())}
    };
}

QString ManagedAgentRunStore::storageRoot() const
{
    const QByteArray overridePath = qgetenv("LLAMACODE_MANAGED_RUNS_DIR");
    const QString root = overridePath.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
              + QStringLiteral("/managed-agent-runs")
        : QString::fromLocal8Bit(overridePath);
    QDir().mkpath(root);
    return root;
}

QString ManagedAgentRunStore::runDirectory(const QString &runId) const
{
    return storageRoot() + QLatin1Char('/') + RunHistoryStore::sanitize(runId);
}

QVariantList ManagedAgentRunStore::runs() const
{
    QVariantList result;
    for (auto it = m_runs.cbegin(); it != m_runs.cend(); ++it)
        result.append(it.value());
    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("startedAt")).toString()
             > b.toMap().value(QStringLiteral("startedAt")).toString();
    });
    return result;
}

bool ManagedAgentRunStore::writeTextFile(const QString &path, const QByteArray &data,
                                         bool append) const
{
    QFile file(path);
    if (!file.open((append ? QIODevice::Append : QIODevice::WriteOnly))) return false;
    return file.write(data) == data.size();
}

bool ManagedAgentRunStore::persist(const QVariantMap &run) const
{
    const QString dir = run.value(QStringLiteral("runDir")).toString();
    if (dir.isEmpty() || !QDir().mkpath(dir)) return false;
    QJsonObject object = QJsonObject::fromVariantMap(run);
    // El prompt vive en prompt.md y no se replica en el manifiesto ni en la
    // fila del store: reduce exposición accidental de instrucciones y secretos.
    object.remove(QStringLiteral("prompt"));
    QSaveFile file(dir + QStringLiteral("/manifest.json"));
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.commit();
}

void ManagedAgentRunStore::setError(const QString &message)
{
    if (message == m_lastError) return;
    m_lastError = message;
    emit lastErrorChanged();
}

void ManagedAgentRunStore::loadPersistedRuns()
{
    QDir root(storageRoot());
    const QStringList dirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : dirs) {
        QFile file(root.filePath(name + QStringLiteral("/manifest.json")));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QVariantMap run = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
        const QString id = run.value(QStringLiteral("runId")).toString();
        if (id.isEmpty()) continue;
        QVariantMap recovered = run;
        const QString status = recovered.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("prepared") || status == QLatin1String("starting")
            || status == QLatin1String("running") || status == QLatin1String("stopping")) {
            const QString workspace = recovered.value(QStringLiteral("workspace")).toString();
            const QString claimId = recovered.value(QStringLiteral("workClaimId")).toString();
            const QString claimSession = recovered.value(QStringLiteral("claimSessionId")).toString();
            if (!workspace.isEmpty() && !claimId.isEmpty() && !claimSession.isEmpty())
                WorkRegistry::release(workspace, claimId, claimSession,
                                      QStringLiteral("interrupted"));
            recovered[QStringLiteral("status")] = QStringLiteral("stale");
            recovered[QStringLiteral("closeoutStatus")] = QStringLiteral("interrupted");
            recovered[QStringLiteral("resultStatus")] = QStringLiteral("interrupted");
            recovered[QStringLiteral("finishedAt")] = nowUtc();
            recovered[QStringLiteral("summary")] = QStringLiteral(
                "La aplicación se cerró mientras la corrida estaba activa.");
            persist(recovered);
            recordHistory(recovered);
        }
        m_runs.insert(id, recovered);
    }
}

void ManagedAgentRunStore::updateRun(const QString &runId, const QVariantMap &run,
                                     bool persistManifest)
{
    m_runs.insert(runId, run);
    if (persistManifest) persist(run);
    emit runChanged(runId, run);
    emit runsChanged();
}

QString ManagedAgentRunStore::startRun(const QVariantMap &request)
{
    setError(QString());
    const QString runtime = runtimeName(request);
    const QString prompt = request.value(QStringLiteral("prompt")).toString().trimmed();
    const QString workspace = QFileInfo(asString(request, QStringLiteral("workspace"))).absoluteFilePath();
    if (!validRuntime(runtime)) {
        setError(QStringLiteral("runtime debe ser claude o codex"));
        return {};
    }
    if (prompt.isEmpty()) {
        setError(QStringLiteral("La corrida necesita un prompt no vacío"));
        return {};
    }
    if (prompt.size() > 200000) {
        setError(QStringLiteral("El prompt supera el límite de 200000 caracteres"));
        return {};
    }
    if (!QDir(workspace).exists()) {
        setError(QStringLiteral("El workspace no existe: %1").arg(workspace));
        return {};
    }
    const QVariantMap command = commandForRequest(request);
    const QString program = command.value(QStringLiteral("program")).toString();
    if (program.isEmpty()) {
        setError(QStringLiteral("No se encontró el CLI de %1 en PATH").arg(runtime));
        return {};
    }

    const QString id = newRunId();
    const bool applyEdits = request.value(QStringLiteral("applyEdits"), false).toBool();
    const bool claimWorkspace = request.value(QStringLiteral("claimWorkspace"), applyEdits).toBool();
    const bool captureDeliverables = request.value(QStringLiteral("captureDeliverables"), applyEdits).toBool();
    const int timeoutSec = qBound(60, request.value(QStringLiteral("timeoutSec"), 3600).toInt(), 24 * 60 * 60);
    const int idleTimeoutSec = qBound(30, request.value(QStringLiteral("idleTimeoutSec"), 600).toInt(), 4 * 60 * 60);
    const qint64 maxLogBytes = qBound<qint64>(64 * 1024,
        request.value(QStringLiteral("maxLogBytes"), 16 * 1024 * 1024).toLongLong(),
        256LL * 1024 * 1024);
    const QString dir = runDirectory(id);
    if (!QDir().mkpath(dir)) {
        setError(QStringLiteral("No se pudo crear el directorio de la corrida"));
        return {};
    }
    if (!writeTextFile(dir + QStringLiteral("/prompt.md"), prompt.toUtf8())) {
        setError(QStringLiteral("No se pudo guardar el prompt durable"));
        return {};
    }
    // Los artefactos existen desde el estado starting, incluso si el CLI no
    // emite ninguna línea antes de terminar.
    writeTextFile(dir + QStringLiteral("/stdout.log"), {});
    writeTextFile(dir + QStringLiteral("/stderr.log"), {});
    writeTextFile(dir + QStringLiteral("/verification.log"), {});

    QString claimId;
    const QString claimSession = QStringLiteral("managed-agent:%1").arg(id);
    if (claimWorkspace) {
        claimId = WorkRegistry::acquire(workspace, claimSession,
                                        QStringLiteral("managed-%1").arg(runtime),
                                        prompt.left(4096));
        QString conflict;
        if (claimId.isEmpty()
            || !WorkRegistry::claimPaths(workspace, claimId, claimSession,
                                         {QStringLiteral(".")}, &conflict)) {
            if (!claimId.isEmpty())
                WorkRegistry::release(workspace, claimId, claimSession,
                                      QStringLiteral("conflict"));
            setError(conflict.isEmpty()
                         ? QStringLiteral("No se pudo reservar el workspace para la corrida")
                         : conflict);
            QDir(dir).removeRecursively();
            return {};
        }
    }

    QJsonObject beforeSnapshot;
    QString snapshotError;
    if (captureDeliverables) {
        beforeSnapshot = AgentDeliverableStore::snapshot(workspace, &snapshotError);
        if (beforeSnapshot.isEmpty()) {
            // La corrida puede continuar, pero el closeout queda explícitamente
            // sin verificación de archivos y no se presenta como entregable.
            snapshotError = snapshotError.isEmpty()
                ? QStringLiteral("snapshot inicial vacío") : snapshotError;
        }
    }

    const QString presentation = asString(request, QStringLiteral("presentation")).isEmpty()
        ? QStringLiteral("managed_panel") : asString(request, QStringLiteral("presentation"));
    const bool visibleRequested = request.value(QStringLiteral("visibleRequested"), true).toBool();
    QVariantMap run{
        {QStringLiteral("runId"), id},
        {QStringLiteral("runtime"), runtime},
        {QStringLiteral("requestId"), asString(request, QStringLiteral("requestId"))},
        {QStringLiteral("cliPath"), program},
        {QStringLiteral("workspace"), workspace},
        {QStringLiteral("ownerId"), asString(request, QStringLiteral("ownerId"))},
        {QStringLiteral("taskId"), asString(request, QStringLiteral("taskId"))},
        {QStringLiteral("agentProfileId"), asString(request, QStringLiteral("agentProfileId"))},
        {QStringLiteral("runDir"), dir},
        {QStringLiteral("promptPath"), dir + QStringLiteral("/prompt.md")},
        {QStringLiteral("stdoutPath"), dir + QStringLiteral("/stdout.log")},
        {QStringLiteral("stderrPath"), dir + QStringLiteral("/stderr.log")},
        {QStringLiteral("manifestPath"), dir + QStringLiteral("/manifest.json")},
        {QStringLiteral("promptSha256"), QString::fromLatin1(
             QCryptographicHash::hash(prompt.toUtf8(), QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("promptBytes"), prompt.toUtf8().size()},
        {QStringLiteral("startedAt"), nowUtc()},
        {QStringLiteral("startedEpochMs"), static_cast<qlonglong>(QDateTime::currentMSecsSinceEpoch())},
        {QStringLiteral("lastActivityEpochMs"), static_cast<qlonglong>(QDateTime::currentMSecsSinceEpoch())},
        {QStringLiteral("status"), QStringLiteral("starting")},
        {QStringLiteral("summary"), QStringLiteral("Iniciando %1").arg(runtime)},
        {QStringLiteral("applyEdits"), applyEdits},
        {QStringLiteral("model"), asString(request, QStringLiteral("model"))},
        {QStringLiteral("allowDangerous"), request.value(QStringLiteral("allowDangerous"), false).toBool()},
        {QStringLiteral("prefixArgs"), request.value(QStringLiteral("prefixArgs")).toStringList()},
        {QStringLiteral("claimWorkspace"), claimWorkspace},
        {QStringLiteral("captureDeliverables"), captureDeliverables},
        {QStringLiteral("workClaimId"), claimId},
        {QStringLiteral("claimSessionId"), claimSession},
        {QStringLiteral("workspaceClaimStatus"), claimId.isEmpty()
             ? QStringLiteral("not_requested") : QStringLiteral("active")},
        {QStringLiteral("claimTtlSec"), 900},
        {QStringLiteral("timeoutSec"), timeoutSec},
        {QStringLiteral("idleTimeoutSec"), idleTimeoutSec},
        {QStringLiteral("maxLogBytes"), maxLogBytes},
        {QStringLiteral("verificationProgram"), asString(request, QStringLiteral("verifyProgram"))},
        {QStringLiteral("verificationArgs"), request.value(QStringLiteral("verifyArgs")).toStringList()},
        {QStringLiteral("verificationTimeoutSec"), qBound(30, request.value(QStringLiteral("verifyTimeoutSec"), 600).toInt(), 4 * 60 * 60)},
        {QStringLiteral("attempt"), request.value(QStringLiteral("attempt"), 1).toInt()},
        {QStringLiteral("retryOf"), asString(request, QStringLiteral("retryOf"))},
        {QStringLiteral("promptTransport"), command.value(QStringLiteral("promptTransport"))},
        {QStringLiteral("permissionPosture"), command.value(QStringLiteral("permissionPosture"))},
        {QStringLiteral("command"), command.value(QStringLiteral("display"))},
        {QStringLiteral("presentation"), presentation},
        {QStringLiteral("visibleRequested"), visibleRequested},
        {QStringLiteral("visibleProof"), visibleRequested && presentation == QLatin1String("managed_panel")
             ? QStringLiteral("managed_panel")
             : (visibleRequested && presentation == QLatin1String("console")
                    ? QStringLiteral("console_requested") : QStringLiteral("not_proven"))},
        {QStringLiteral("closeoutStatus"), QStringLiteral("pending")},
        {QStringLiteral("resultStatus"), QStringLiteral("pending")},
        {QStringLiteral("verificationStatus"), QStringLiteral("not_requested")},
        {QStringLiteral("verificationLogPath"), dir + QStringLiteral("/verification.log")},
        {QStringLiteral("stdoutTail"), QString()},
        {QStringLiteral("stderrTail"), QString()},
        {QStringLiteral("verificationTail"), QString()},
        {QStringLiteral("stdoutBytes"), static_cast<qlonglong>(0)},
        {QStringLiteral("stderrBytes"), static_cast<qlonglong>(0)},
        {QStringLiteral("verificationBytes"), static_cast<qlonglong>(0)},
        {QStringLiteral("logTruncated"), false}
    };
    if (!snapshotError.isEmpty()) run[QStringLiteral("deliverablesError")] = snapshotError;
    if (!beforeSnapshot.isEmpty()) m_beforeSnapshots.insert(id, beforeSnapshot);
    m_runs.insert(id, run);
    persist(run);
    emit runChanged(id, run);
    emit runsChanged();

    auto *process = new QProcess(this);
    process->setWorkingDirectory(workspace);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LLAMACODE_MANAGED_RUN"), QStringLiteral("1"));
    env.insert(QStringLiteral("LLAMACODE_RUN_ID"), id);
    env.insert(QStringLiteral("LLAMACODE_RUN_DIR"), dir);
    env.insert(QStringLiteral("LLAMACODE_RUN_MANIFEST"), dir + QStringLiteral("/manifest.json"));
    env.insert(QStringLiteral("LLAMACODE_RUN_PROMPT_FILE"), dir + QStringLiteral("/prompt.md"));
    process->setProcessEnvironment(env);
#ifdef Q_OS_WIN
    if (visibleRequested && presentation == QLatin1String("console")) {
        process->setCreateProcessArgumentsModifier(
            [](QProcess::CreateProcessArguments *arguments) {
                arguments->flags |= CREATE_NEW_CONSOLE;
            });
    }
#endif
    m_processes.insert(id, process);
    m_stopRequested.insert(id, false);

    connect(process, &QProcess::started, this, [this, id, process]() {
        if (!m_runs.contains(id)) return;
        QVariantMap run = m_runs.value(id);
        run[QStringLiteral("status")] = QStringLiteral("running");
        run[QStringLiteral("pid")] = static_cast<qlonglong>(process->processId());
        run[QStringLiteral("lastActivityEpochMs")] = static_cast<qlonglong>(QDateTime::currentMSecsSinceEpoch());
        run[QStringLiteral("summary")] = QStringLiteral("%1 está ejecutándose").arg(
            run.value(QStringLiteral("runtime")).toString());
        updateRun(id, run);
        if (run.value(QStringLiteral("promptTransport")).toString() == QLatin1String("stdin")) {
            QFile prompt(run.value(QStringLiteral("promptPath")).toString());
            if (prompt.open(QIODevice::ReadOnly)) {
                process->write(prompt.readAll());
                process->closeWriteChannel();
            } else {
                requestStop(id, QStringLiteral("failed"));
                setError(QStringLiteral("No se pudo entregar el prompt por stdin"));
            }
        }
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, id, process]() {
        appendOutput(id, QStringLiteral("stdout"), process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, id, process]() {
        appendOutput(id, QStringLiteral("stderr"), process->readAllStandardError());
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, id, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_processes.contains(id)) return;
        finishRun(id, -1, QStringLiteral("failed"), process->errorString());
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, id](int exitCode, QProcess::ExitStatus) {
        const QString reason = m_stopReasons.value(id);
        const bool stopped = m_stopRequested.value(id, false);
        finishRun(id, exitCode,
                  reason == QLatin1String("timed_out") ? QStringLiteral("timed_out")
                  : (reason == QLatin1String("failed") ? QStringLiteral("failed")
                  : (stopped ? QStringLiteral("cancelled")
                             : (exitCode == 0 ? QStringLiteral("finished")
                                              : QStringLiteral("failed")))));
    });

    process->start(program, command.value(QStringLiteral("args")).toStringList());
    return id;
}

void ManagedAgentRunStore::appendOutput(const QString &runId, const QString &channel,
                                         const QByteArray &data)
{
    if (data.isEmpty() || !m_runs.contains(runId)) return;
    const QVariantMap run = m_runs.value(runId);
    const bool verification = channel == QLatin1String("verification");
    const QString path = run.value(verification ? QStringLiteral("verificationLogPath")
                                : (channel == QLatin1String("stderr")
                                       ? QStringLiteral("stderrPath")
                                       : QStringLiteral("stdoutPath"))).toString();
    const QString bytesKey = verification ? QStringLiteral("verificationBytes")
        : (channel == QLatin1String("stderr")
               ? QStringLiteral("stderrBytes") : QStringLiteral("stdoutBytes"));
    const qint64 previous = run.value(bytesKey).toLongLong();
    const qint64 limit = run.value(QStringLiteral("maxLogBytes"),
                                   16 * 1024 * 1024).toLongLong();
    const qint64 remaining = qMax<qint64>(0, limit - previous);
    if (remaining > 0) {
        const QByteArray bounded = data.left(static_cast<qsizetype>(remaining));
        writeTextFile(path, bounded, true);
    }
    QVariantMap updated = run;
    const QString value = QString::fromUtf8(data);
    const QString key = verification ? QStringLiteral("verificationTail")
        : (channel == QLatin1String("stderr")
               ? QStringLiteral("stderrTail") : QStringLiteral("stdoutTail"));
    updated[key] = tail(updated.value(key).toString() + value);
    updated[bytesKey] = previous + qMin<qint64>(remaining, data.size());
    if (data.size() > remaining) updated[QStringLiteral("logTruncated")] = true;
    updated[QStringLiteral("lastActivityEpochMs")] =
        static_cast<qlonglong>(QDateTime::currentMSecsSinceEpoch());
    // El manifiesto se actualiza sólo con el tail acotado; el log completo vive
    // en su artefacto y no fuerza una escritura por cada token emitido.
    m_runs.insert(runId, updated);
    emit runChanged(runId, updated);
    emit logAppended(runId, channel, value);
}

QString ManagedAgentRunStore::log(const QString &runId) const
{
    const QVariantMap run = m_runs.value(runId);
    if (run.isEmpty()) return {};
    QString out;
    for (const QString &key : {QStringLiteral("stdoutPath"), QStringLiteral("stderrPath"),
                               QStringLiteral("verificationLogPath")}) {
        QFile file(run.value(key).toString());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QString content = QString::fromUtf8(file.readAll());
        if (!out.isEmpty()) out += QStringLiteral("\n");
        out += content;
    }
    return tail(out, 262144);
}

void ManagedAgentRunStore::finishRun(const QString &runId, int exitCode,
                                     const QString &status, const QString &error)
{
    if (!m_runs.contains(runId)) return;
    QVariantMap run = m_runs.value(runId);
    const QString current = run.value(QStringLiteral("status")).toString();
    if (current == QLatin1String("finished") || current == QLatin1String("failed")
        || current == QLatin1String("cancelled") || current == QLatin1String("timed_out")
        || current == QLatin1String("stale"))
        return;
    run[QStringLiteral("exitCode")] = exitCode;
    if (status == QLatin1String("finished") && exitCode == 0
        && !run.value(QStringLiteral("verificationProgram")).toString().isEmpty()) {
        run[QStringLiteral("status")] = QStringLiteral("verifying");
        run[QStringLiteral("verificationStatus")] = QStringLiteral("running");
        run[QStringLiteral("verificationStartedEpochMs")] =
            static_cast<qlonglong>(QDateTime::currentMSecsSinceEpoch());
        run[QStringLiteral("summary")] = QStringLiteral("Verificando el resultado");
        updateRun(runId, run);
        startVerification(runId);
        return;
    }
    if (!error.isEmpty()) run[QStringLiteral("error")] = error;
    completeRun(runId, status, error);
}

void ManagedAgentRunStore::startVerification(const QString &runId)
{
    const QVariantMap run = m_runs.value(runId);
    if (run.isEmpty()) return;
    auto *process = new QProcess(this);
    process->setWorkingDirectory(run.value(QStringLiteral("workspace")).toString());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LLAMACODE_MANAGED_RUN"), QStringLiteral("1"));
    env.insert(QStringLiteral("LLAMACODE_RUN_ID"), runId);
    env.insert(QStringLiteral("LLAMACODE_RUN_DIR"),
               run.value(QStringLiteral("runDir")).toString());
    env.insert(QStringLiteral("LLAMACODE_RUN_MANIFEST"),
               run.value(QStringLiteral("manifestPath")).toString());
    process->setProcessEnvironment(env);
    m_verifiers.insert(runId, process);
    m_verificationOutput.insert(runId, QString());
    connect(process, &QProcess::readyReadStandardOutput, this, [this, runId, process]() {
        const QByteArray chunk = process->readAllStandardOutput();
        m_verificationOutput[runId] = tail(m_verificationOutput.value(runId)
                                            + QString::fromUtf8(chunk), 65536);
        appendOutput(runId, QStringLiteral("verification"), chunk);
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, runId, process]() {
        const QByteArray chunk = process->readAllStandardError();
        m_verificationOutput[runId] = tail(m_verificationOutput.value(runId)
                                            + QString::fromUtf8(chunk), 65536);
        appendOutput(runId, QStringLiteral("verification"), chunk);
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, runId, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_verifiers.contains(runId))
            finishVerification(runId, -1, process->errorString());
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, runId](int exitCode, QProcess::ExitStatus) {
        finishVerification(runId, exitCode);
    });
    process->start(run.value(QStringLiteral("verificationProgram")).toString(),
                   run.value(QStringLiteral("verificationArgs")).toStringList());
}

void ManagedAgentRunStore::finishVerification(const QString &runId, int exitCode,
                                               const QString &error)
{
    if (!m_runs.contains(runId) || !m_verifiers.contains(runId)) return;
    const QString output = m_verificationOutput.take(runId);
    if (auto *process = m_verifiers.take(runId)) process->deleteLater();
    QVariantMap run = m_runs.value(runId);
    run[QStringLiteral("verificationExitCode")] = exitCode;
    run[QStringLiteral("verificationStatus")] = exitCode == 0 && error.isEmpty()
        ? QStringLiteral("passed") : QStringLiteral("failed");
    run[QStringLiteral("verificationOutput")]= output;
    if (!error.isEmpty()) run[QStringLiteral("verificationError")] = error;
    updateRun(runId, run);
    const QString reason = m_stopReasons.value(runId);
    if (reason == QLatin1String("cancelled") || reason == QLatin1String("timed_out")) {
        completeRun(runId, reason, reason == QLatin1String("timed_out")
                    ? QStringLiteral("La verificación superó el timeout")
                    : QStringLiteral("Verificación cancelada"));
    } else if (exitCode != 0 || !error.isEmpty()) {
        completeRun(runId, QStringLiteral("failed"),
                    error.isEmpty() ? QStringLiteral("La verificación terminó con código %1")
                                              .arg(exitCode) : error);
    } else {
        completeRun(runId, QStringLiteral("finished"));
    }
}

void ManagedAgentRunStore::completeRun(const QString &runId, const QString &status,
                                       const QString &error)
{
    if (!m_runs.contains(runId)) return;
    QVariantMap run = m_runs.value(runId);
    const QString current = run.value(QStringLiteral("status")).toString();
    if (current == QLatin1String("finished") || current == QLatin1String("failed")
        || current == QLatin1String("cancelled") || current == QLatin1String("timed_out")
        || current == QLatin1String("stale")) return;
    if (!error.isEmpty()) run[QStringLiteral("error")] = error;

    const bool wantsDeliverables = run.value(QStringLiteral("captureDeliverables")).toBool();
    const QJsonObject before = m_beforeSnapshots.take(runId);
    if (wantsDeliverables && !before.isEmpty()) {
        QString captureError;
        const QJsonObject manifest = AgentDeliverableStore::capture(
            runId, run.value(QStringLiteral("workspace")).toString(), before, &captureError);
        if (!manifest.isEmpty()) {
            run[QStringLiteral("deliverablesManifestPath")] =
                QDir(AgentDeliverableStore::rootDir()).filePath(runId + QStringLiteral("/manifest.json"));
            run[QStringLiteral("deliverablesChangedCount")] =
                manifest.value(QStringLiteral("changedCount")).toInt();
            const bool partial = manifest.value(QStringLiteral("beforeTruncated")).toBool()
                || manifest.value(QStringLiteral("afterTruncated")).toBool();
            run[QStringLiteral("deliverablesStatus")] = partial
                ? QStringLiteral("partial") : QStringLiteral("captured");
            if (!captureError.isEmpty()) run[QStringLiteral("deliverablesError")] = captureError;
        } else {
            run[QStringLiteral("deliverablesStatus")] = QStringLiteral("failed");
            if (!captureError.isEmpty()) run[QStringLiteral("deliverablesError")] = captureError;
        }
    } else if (wantsDeliverables) {
        run[QStringLiteral("deliverablesStatus")] = QStringLiteral("unavailable");
    } else {
        run[QStringLiteral("deliverablesStatus")] = QStringLiteral("not_requested");
    }

    run[QStringLiteral("status")] = status;
    run[QStringLiteral("finishedAt")] = nowUtc();
    const bool verified = run.value(QStringLiteral("verificationStatus")).toString()
        == QLatin1String("passed");
    const bool captured = run.value(QStringLiteral("deliverablesStatus")).toString()
        == QLatin1String("captured");
    run[QStringLiteral("resultStatus")] = status == QLatin1String("finished")
        ? (verified ? QStringLiteral("verified")
                    : (captured ? QStringLiteral("artifacts_captured")
                                 : QStringLiteral("completed_unverified")))
        : (status == QLatin1String("cancelled") ? QStringLiteral("cancelled")
                                                 : QStringLiteral("needs_attention"));
    run[QStringLiteral("closeoutStatus")] = verified
        ? QStringLiteral("verified")
        : (status == QLatin1String("finished") ? QStringLiteral("completed_unverified")
                                                : QStringLiteral("needs_attention"));
    if (!error.isEmpty()) run[QStringLiteral("summary")] = error;
    else if (status == QLatin1String("finished"))
        run[QStringLiteral("summary")] = verified
            ? QStringLiteral("%1 terminó y la verificación pasó").arg(run.value(QStringLiteral("runtime")).toString())
            : QStringLiteral("%1 terminó; resultado pendiente de revisión").arg(run.value(QStringLiteral("runtime")).toString());
    else if (status == QLatin1String("timed_out"))
        run[QStringLiteral("summary")] = QStringLiteral("%1 superó el límite de tiempo").arg(run.value(QStringLiteral("runtime")).toString());
    else
        run[QStringLiteral("summary")] = QStringLiteral("%1 terminó con estado %2")
            .arg(run.value(QStringLiteral("runtime")).toString(), status);

    const QString workspace = run.value(QStringLiteral("workspace")).toString();
    const QString claimId = run.value(QStringLiteral("workClaimId")).toString();
    const QString claimSession = run.value(QStringLiteral("claimSessionId")).toString();
    if (!workspace.isEmpty() && !claimId.isEmpty() && !claimSession.isEmpty())
        WorkRegistry::release(workspace, claimId, claimSession, status);
    if (!claimId.isEmpty()) run[QStringLiteral("workspaceClaimStatus")] = QStringLiteral("released");
    updateRun(runId, run);
    recordHistory(run);
    if (auto *process = m_processes.take(runId)) process->deleteLater();
    m_stopRequested.remove(runId);
    m_stopReasons.remove(runId);
    emit runFinished(runId, run);
    emit runsChanged();
}

void ManagedAgentRunStore::recordHistory(const QVariantMap &run)
{
    if (!m_history) return;
    QVariantMap metadata{
        {QStringLiteral("managedRunId"), run.value(QStringLiteral("runId"))},
        {QStringLiteral("runtime"), run.value(QStringLiteral("runtime"))},
        {QStringLiteral("workspace"), run.value(QStringLiteral("workspace"))},
        {QStringLiteral("promptPath"), run.value(QStringLiteral("promptPath"))},
        {QStringLiteral("manifestPath"), run.value(QStringLiteral("manifestPath"))},
        {QStringLiteral("stdoutPath"), run.value(QStringLiteral("stdoutPath"))},
        {QStringLiteral("stderrPath"), run.value(QStringLiteral("stderrPath"))},
        {QStringLiteral("verificationLogPath"), run.value(QStringLiteral("verificationLogPath"))},
        {QStringLiteral("deliverablesManifestPath"), run.value(QStringLiteral("deliverablesManifestPath"))},
        {QStringLiteral("deliverablesChangedCount"), run.value(QStringLiteral("deliverablesChangedCount"))},
        {QStringLiteral("deliverablesStatus"), run.value(QStringLiteral("deliverablesStatus"))},
        {QStringLiteral("permissionPosture"), run.value(QStringLiteral("permissionPosture"))},
        {QStringLiteral("visibleProof"), run.value(QStringLiteral("visibleProof"))},
        {QStringLiteral("exitCode"), run.value(QStringLiteral("exitCode"))},
        {QStringLiteral("resultStatus"), run.value(QStringLiteral("resultStatus"))},
        {QStringLiteral("verificationStatus"), run.value(QStringLiteral("verificationStatus"))},
        {QStringLiteral("verificationOutput"), run.value(QStringLiteral("verificationOutput"))},
        {QStringLiteral("attempt"), run.value(QStringLiteral("attempt"))}
    };
    QVariantMap record{
        {QStringLiteral("runId"), run.value(QStringLiteral("runId"))},
        {QStringLiteral("startedAt"), run.value(QStringLiteral("startedAt"))},
        {QStringLiteral("finishedAt"), run.value(QStringLiteral("finishedAt"))},
        {QStringLiteral("status"), run.value(QStringLiteral("status"))},
        {QStringLiteral("summary"), run.value(QStringLiteral("summary"))},
        {QStringLiteral("source"), QStringLiteral("managed_agent")},
        {QStringLiteral("log"), log(run.value(QStringLiteral("runId")).toString())},
        {QStringLiteral("metadata"), metadata},
        {QStringLiteral("report"), QVariantList{
             QVariantMap{{QStringLiteral("n"), 1}, {QStringLiteral("tool"),
                          QStringLiteral("managed_cli")}, {QStringLiteral("ok"),
                          run.value(QStringLiteral("resultStatus")).toString()
                              == QLatin1String("verified")},
                         {QStringLiteral("summary"), run.value(QStringLiteral("summary"))}}}}
    };
    const QString owner = run.value(QStringLiteral("ownerId")).toString();
    if (!owner.isEmpty()) m_history->append(owner, record);
    const QString taskId = run.value(QStringLiteral("taskId")).toString();
    if (!taskId.isEmpty() && taskId != owner) m_history->append(taskId, record);
}

QVariantMap ManagedAgentRunStore::run(const QString &runId) const
{
    return m_runs.value(runId);
}

bool ManagedAgentRunStore::stopRun(const QString &runId)
{
    if (!m_processes.contains(runId) && !m_verifiers.contains(runId)) return false;
    requestStop(runId, QStringLiteral("cancelled"));
    return true;
}

void ManagedAgentRunStore::requestStop(const QString &runId, const QString &reason)
{
    auto *process = m_processes.value(runId, nullptr);
    auto *verifier = m_verifiers.value(runId, nullptr);
    if (!process && !verifier) return;
    m_stopRequested[runId] = true;
    m_stopReasons[runId] = reason;
    QVariantMap run = m_runs.value(runId);
    if (run.isEmpty()) return;
    run[QStringLiteral("status")] = QStringLiteral("stopping");
    run[QStringLiteral("summary")] = reason == QLatin1String("timed_out")
        ? QStringLiteral("Se alcanzó el límite de tiempo; cancelando")
        : QStringLiteral("Cancelación solicitada");
    updateRun(runId, run);
#ifdef Q_OS_WIN
    if (process && process->processId() != 0) {
        QProcess::startDetached(QStringLiteral("taskkill"),
                                {QStringLiteral("/PID"), QString::number(process->processId()),
                                 QStringLiteral("/T"), QStringLiteral("/F")});
    } else if (process) {
        process->terminate();
    }
    if (verifier && verifier->processId() != 0) {
        QProcess::startDetached(QStringLiteral("taskkill"),
                                {QStringLiteral("/PID"), QString::number(verifier->processId()),
                                 QStringLiteral("/T"), QStringLiteral("/F")});
    } else if (verifier) {
        verifier->terminate();
    }
#else
    if (process) process->terminate();
    if (verifier) verifier->terminate();
    QTimer::singleShot(2500, this, [process, verifier]() {
        if (process && process->state() != QProcess::NotRunning) process->kill();
        if (verifier && verifier->state() != QProcess::NotRunning) verifier->kill();
    });
#endif
}

QString ManagedAgentRunStore::retryRun(const QString &runId)
{
    const QVariantMap old = m_runs.value(runId);
    const QString status = old.value(QStringLiteral("status")).toString();
    if (old.isEmpty() || (status != QLatin1String("stale")
                          && status != QLatin1String("failed")
                          && status != QLatin1String("cancelled")
                          && status != QLatin1String("timed_out"))) {
        setError(QStringLiteral("Sólo se pueden reintentar corridas terminadas con atención"));
        return {};
    }
    QFile prompt(old.value(QStringLiteral("promptPath")).toString());
    if (!prompt.open(QIODevice::ReadOnly)) {
        setError(QStringLiteral("No se pudo leer el prompt durable para reintentar"));
        return {};
    }
    QVariantMap request{
        {QStringLiteral("runtime"), old.value(QStringLiteral("runtime"))},
        {QStringLiteral("prompt"), QString::fromUtf8(prompt.readAll())},
        {QStringLiteral("workspace"), old.value(QStringLiteral("workspace"))},
        {QStringLiteral("cliPath"), old.value(QStringLiteral("cliPath"))},
        {QStringLiteral("ownerId"), old.value(QStringLiteral("ownerId"))},
        {QStringLiteral("taskId"), old.value(QStringLiteral("taskId"))},
        {QStringLiteral("agentProfileId"), old.value(QStringLiteral("agentProfileId"))},
        {QStringLiteral("applyEdits"), old.value(QStringLiteral("applyEdits"))},
        {QStringLiteral("approvalMode"), old.value(QStringLiteral("approvalMode"))},
        {QStringLiteral("model"), old.value(QStringLiteral("model"))},
        {QStringLiteral("allowDangerous"), old.value(QStringLiteral("allowDangerous"))},
        {QStringLiteral("prefixArgs"), old.value(QStringLiteral("prefixArgs"))},
        {QStringLiteral("promptTransport"), old.value(QStringLiteral("promptTransport"))},
        {QStringLiteral("claimWorkspace"), old.value(QStringLiteral("claimWorkspace"))},
        {QStringLiteral("captureDeliverables"), old.value(QStringLiteral("captureDeliverables"))},
        {QStringLiteral("timeoutSec"), old.value(QStringLiteral("timeoutSec"))},
        {QStringLiteral("idleTimeoutSec"), old.value(QStringLiteral("idleTimeoutSec"))},
        {QStringLiteral("maxLogBytes"), old.value(QStringLiteral("maxLogBytes"))},
        {QStringLiteral("verifyProgram"), old.value(QStringLiteral("verificationProgram"))},
        {QStringLiteral("verifyArgs"), old.value(QStringLiteral("verificationArgs"))},
        {QStringLiteral("verifyTimeoutSec"), old.value(QStringLiteral("verificationTimeoutSec"))},
        {QStringLiteral("attempt"), old.value(QStringLiteral("attempt"), 1).toInt() + 1},
        {QStringLiteral("retryOf"), runId}
    };
    return startRun(request);
}

void ManagedAgentRunStore::housekeeping()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QStringList ids = m_processes.keys();
    for (const QString &id : ids) {
        if (!m_runs.contains(id)) continue;
        const QVariantMap run = m_runs.value(id);
        const qint64 started = run.value(QStringLiteral("startedEpochMs")).toLongLong();
        const qint64 last = run.value(QStringLiteral("lastActivityEpochMs"), started).toLongLong();
        const int timeout = run.value(QStringLiteral("timeoutSec"), 3600).toInt();
        const int idle = run.value(QStringLiteral("idleTimeoutSec"), 600).toInt();
        if (started > 0 && now - started > static_cast<qint64>(timeout) * 1000) {
            requestStop(id, QStringLiteral("timed_out"));
            continue;
        }
        if (last > 0 && now - last > static_cast<qint64>(idle) * 1000) {
            requestStop(id, QStringLiteral("timed_out"));
            continue;
        }
        const QString workspace = run.value(QStringLiteral("workspace")).toString();
        const QString claim = run.value(QStringLiteral("workClaimId")).toString();
        const QString session = run.value(QStringLiteral("claimSessionId")).toString();
        if (!workspace.isEmpty() && !claim.isEmpty() && !session.isEmpty()
            && !WorkRegistry::heartbeat(workspace, claim, session,
                                        run.value(QStringLiteral("claimTtlSec"), 900).toInt())) {
            requestStop(id, QStringLiteral("failed"));
            QVariantMap updated = m_runs.value(id);
            updated[QStringLiteral("workspaceClaimStatus")] = QStringLiteral("lost");
            updated[QStringLiteral("error")] = QStringLiteral("Se perdió el claim del workspace");
            updateRun(id, updated);
        }
    }
    const QStringList verifierIds = m_verifiers.keys();
    for (const QString &id : verifierIds) {
        const QVariantMap run = m_runs.value(id);
        const qint64 started = run.value(QStringLiteral("verificationStartedEpochMs")).toLongLong();
        const int timeout = run.value(QStringLiteral("verificationTimeoutSec"), 600).toInt();
        if (started > 0 && now - started > static_cast<qint64>(timeout) * 1000)
            requestStop(id, QStringLiteral("timed_out"));
    }
    cleanupRetention();
}

void ManagedAgentRunStore::cleanupRetention()
{
    const int envKeep = qEnvironmentVariableIntValue("LLAMACODE_MANAGED_RUN_RETENTION");
    const int keep = qBound(5, envKeep > 0 ? envKeep : 40, 500);
    QList<QVariantMap> terminal;
    for (const QVariantMap &run : std::as_const(m_runs)) {
        const QString id = run.value(QStringLiteral("runId")).toString();
        const QString status = run.value(QStringLiteral("status")).toString();
        if (!m_processes.contains(id) && !m_verifiers.contains(id)
            && (status == QLatin1String("finished") || status == QLatin1String("failed")
                || status == QLatin1String("cancelled") || status == QLatin1String("timed_out")
                || status == QLatin1String("stale"))) terminal.append(run);
    }
    std::sort(terminal.begin(), terminal.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("finishedAt")).toString()
             < b.value(QStringLiteral("finishedAt")).toString();
    });
    while (terminal.size() > keep) {
        const QString id = terminal.takeFirst().value(QStringLiteral("runId")).toString();
        QDir(runDirectory(id)).removeRecursively();
        AgentDeliverableStore::remove(id);
        m_runs.remove(id);
    }
}

bool ManagedAgentRunStore::removeRun(const QString &runId)
{
    if (m_processes.contains(runId) || m_verifiers.contains(runId)) return false;
    const QVariantMap row = m_runs.take(runId);
    if (row.isEmpty()) return false;
    // El destino se deriva del id y no de un path arbitrario que pudiera
    // aparecer en un manifiesto local manipulado.
    QDir(runDirectory(runId)).removeRecursively();
    AgentDeliverableStore::remove(runId);
    emit runsChanged();
    return true;
}
