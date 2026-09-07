#include "TaskStore.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QDateTime>
#include <QUuid>
#include <QRegularExpression>

TaskStore::TaskStore(QObject *parent) : QAbstractListModel(parent)
{
    load();
}

int TaskStore::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_items.size();
}

QVariant TaskStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const QVariantMap &t = m_items.at(index.row());
    switch (role) {
    case IdRole:              return t.value("id");
    case NameRole:            return t.value("name");
    case DescriptionRole:     return t.value("description");
    case ProfileIdRole:       return t.value("profileId");
    case StepsRole:           return t.value("steps");
    case StepCountRole:       return t.value("steps").toList().size();
    case ScheduleEnabledRole: return t.value("scheduleEnabled", false);
    case ScheduleCronRole:    return t.value("scheduleCron");
    case ScheduleSpecRole:    return t.value("scheduleSpec");
    case PermScopeRole:       return t.value("permScope", QStringLiteral("project"));
    case PermFoldersRole:     return t.value("permFolders");
    case CreatedAtRole:       return t.value("createdAt");
    case UpdatedAtRole:       return t.value("updatedAt");
    case LastRunAtRole:       return t.value("lastRunAt");
    case LastRunStatusRole:   return t.value("lastRunStatus");
    case LastRunSummaryRole:  return t.value("lastRunSummary");
    case PrePromptRole:       return t.value("prePrompt");
    case PostPromptRole:      return t.value("postPrompt");
    case SilentUnlessErrorRole: return t.value("silentUnlessError", false);
    case ExecutionModeRole:     return t.value("executionMode", QStringLiteral("auto"));
    case ApprovalPolicyRole:    return t.value("approvalPolicy", QStringLiteral("sensitive"));
    case SafetyProfileRole:     return t.value("safetyProfile", QStringLiteral("normal"));
    case TeachArtifactIdRole:   return t.value("teachArtifactId");
    case TeachFormatVersionRole:return t.value("teachFormatVersion", 1);
    case TrainedAtRole:         return t.value("trainedAt");
    case ScopeKindRole:         return t.value("scopeKind", QStringLiteral("screen"));
    case ScopeTargetIdRole:     return t.value("scopeTargetId");
    case ScopeLabelRole:        return t.value("scopeLabel");
    case ScopeWidthRole:        return t.value("scopeWidth", 0);
    case ScopeHeightRole:       return t.value("scopeHeight", 0);
    case ScopeDpiRole:          return t.value("scopeDpi", 96.0);
    case TimeoutSecRole:        return t.value("timeoutSec", 300);
    case MaxActionsRole:        return t.value("maxActions", 50);
    case MaxRetriesRole:        return t.value("maxRetries", 2);
    case AutomationStatusRole:  return t.value("automationStatus", QStringLiteral("untrained"));
    case LoopEnabledRole:       return t.value("loopEnabled", false);
    case LoopGoalRole:          return t.value("loopGoal");
    case LoopMaxIterationsRole: return t.value("loopMaxIterations", 5);
    case LoopMaxSecondsRole:    return t.value("loopMaxSeconds", 0);
    case VerifyProfileIdRole:   return t.value("verifyProfileId");
    case AutoDifficultyRoutingRole: return t.value("autoDifficultyRouting", false);
    default:                  return {};
    }
}

QHash<int, QByteArray> TaskStore::roleNames() const
{
    return {
        { IdRole,              "id" },
        { NameRole,            "name" },
        { DescriptionRole,     "description" },
        { ProfileIdRole,       "profileId" },
        { StepsRole,           "steps" },
        { StepCountRole,       "stepCount" },
        { ScheduleEnabledRole, "scheduleEnabled" },
        { ScheduleCronRole,    "scheduleCron" },
        { ScheduleSpecRole,    "scheduleSpec" },
        { PermScopeRole,       "permScope" },
        { PermFoldersRole,     "permFolders" },
        { CreatedAtRole,       "createdAt" },
        { UpdatedAtRole,       "updatedAt" },
        { LastRunAtRole,       "lastRunAt" },
        { LastRunStatusRole,   "lastRunStatus" },
        { LastRunSummaryRole,  "lastRunSummary" },
        { PrePromptRole,       "prePrompt" },
        { PostPromptRole,      "postPrompt" },
        { SilentUnlessErrorRole, "silentUnlessError" },
        { ExecutionModeRole,     "executionMode" },
        { ApprovalPolicyRole,    "approvalPolicy" },
        { SafetyProfileRole,     "safetyProfile" },
        { TeachArtifactIdRole,   "teachArtifactId" },
        { TeachFormatVersionRole,"teachFormatVersion" },
        { TrainedAtRole,         "trainedAt" },
        { ScopeKindRole,         "scopeKind" },
        { ScopeTargetIdRole,     "scopeTargetId" },
        { ScopeLabelRole,        "scopeLabel" },
        { ScopeWidthRole,        "scopeWidth" },
        { ScopeHeightRole,       "scopeHeight" },
        { ScopeDpiRole,          "scopeDpi" },
        { TimeoutSecRole,        "timeoutSec" },
        { MaxActionsRole,        "maxActions" },
        { MaxRetriesRole,        "maxRetries" },
        { AutomationStatusRole,  "automationStatus" },
        { LoopEnabledRole,       "loopEnabled" },
        { LoopGoalRole,          "loopGoal" },
        { LoopMaxIterationsRole, "loopMaxIterations" },
        { LoopMaxSecondsRole,    "loopMaxSeconds" },
        { VerifyProfileIdRole,   "verifyProfileId" },
        { AutoDifficultyRoutingRole, "autoDifficultyRouting" },
    };
}

const QString TaskStore::kGoalMetMarker    = QStringLiteral("GOAL_MET");
const QString TaskStore::kGoalNotMetMarker = QStringLiteral("GOAL_NOT_MET");

QString TaskStore::sanitize(const QString &name)
{
    QString s = name.trimmed().toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]+")), QStringLiteral("-"));
    s.replace(QRegularExpression(QStringLiteral("-+")), QStringLiteral("-"));
    while (s.startsWith('-')) s.remove(0, 1);
    while (s.endsWith('-')) s.chop(1);
    return s;
}

int TaskStore::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).value("id").toString() == id) return i;
    return -1;
}

QString TaskStore::save(const QString &id, const QVariantMap &def)
{
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    int row = id.isEmpty() ? -1 : indexOfId(id);

    QVariantMap t = (row >= 0) ? m_items.at(row) : QVariantMap{};
    t["name"]            = def.value("name", t.value("name"));
    t["description"]     = def.value("description", t.value("description"));
    t["profileId"]       = def.value("profileId", t.value("profileId"));
    t["prePrompt"]       = def.value("prePrompt", t.value("prePrompt"));
    t["postPrompt"]      = def.value("postPrompt", t.value("postPrompt"));
    t["silentUnlessError"] = def.value("silentUnlessError", t.value("silentUnlessError", false));
    t["steps"]           = def.value("steps", t.value("steps", QVariantList{}));
    t["workflow"]        = def.value("workflow", t.value("workflow", QVariantMap{}));
    t["scheduleEnabled"] = def.value("scheduleEnabled", t.value("scheduleEnabled", false));
    t["scheduleCron"]    = def.value("scheduleCron", t.value("scheduleCron"));
    t["scheduleSpec"]    = def.value("scheduleSpec", t.value("scheduleSpec", QVariantMap{}));
    t["permScope"]       = def.value("permScope", t.value("permScope", QStringLiteral("project")));
    t["permFolders"]     = def.value("permFolders", t.value("permFolders", QVariantList{}));
    t["executionMode"]   = def.value("executionMode", t.value("executionMode", QStringLiteral("auto")));
    t["approvalPolicy"]  = def.value("approvalPolicy", t.value("approvalPolicy", QStringLiteral("sensitive")));
    t["safetyProfile"]   = def.value("safetyProfile", t.value("safetyProfile", QStringLiteral("normal")));
    t["teachArtifactId"] = def.value("teachArtifactId", t.value("teachArtifactId"));
    t["teachFormatVersion"] = def.value("teachFormatVersion", t.value("teachFormatVersion", 1));
    t["trainedAt"]       = def.value("trainedAt", t.value("trainedAt"));
    t["scopeKind"]       = def.value("scopeKind", t.value("scopeKind", QStringLiteral("screen")));
    t["scopeTargetId"]   = def.value("scopeTargetId", t.value("scopeTargetId"));
    t["scopeLabel"]      = def.value("scopeLabel", t.value("scopeLabel"));
    t["scopeWidth"]      = def.value("scopeWidth", t.value("scopeWidth", 0));
    t["scopeHeight"]     = def.value("scopeHeight", t.value("scopeHeight", 0));
    t["scopeDpi"]        = def.value("scopeDpi", t.value("scopeDpi", 96.0));
    t["timeoutSec"]      = qBound(30, def.value("timeoutSec", t.value("timeoutSec", 300)).toInt(), 3600);
    t["maxActions"]      = qBound(1, def.value("maxActions", t.value("maxActions", 50)).toInt(), 500);
    t["maxRetries"]      = qBound(0, def.value("maxRetries", t.value("maxRetries", 2)).toInt(), 10);
    t["automationStatus"] = def.value("automationStatus",
                                      t.value("automationStatus", QStringLiteral("untrained")));
    t["loopEnabled"]     = def.value("loopEnabled", t.value("loopEnabled", false));
    t["loopGoal"]        = def.value("loopGoal", t.value("loopGoal"));
    t["loopMaxIterations"] = qBound(1, def.value("loopMaxIterations",
                                       t.value("loopMaxIterations", 5)).toInt(), 1000);
    t["loopMaxSeconds"] = qBound(0, def.value("loopMaxSeconds",
                                      t.value("loopMaxSeconds", 0)).toInt(), 86400);
    t["verifyProfileId"] = def.value("verifyProfileId", t.value("verifyProfileId"));
    t["autoDifficultyRouting"] = def.value("autoDifficultyRouting",
                                             t.value("autoDifficultyRouting", false));
    // Data-driven (RPA por lote): dataset embebido (texto) o por archivo. El mismo
    // flujo corre una vez por fila, con {{var}} sustituido por los valores de la fila.
    t["datasetInline"]   = def.value("datasetInline", t.value("datasetInline"));
    t["datasetFormat"]   = def.value("datasetFormat", t.value("datasetFormat"));
    t["datasetPath"]     = def.value("datasetPath", t.value("datasetPath"));
    // On-error del lote data-driven: "continue" (sigue con la próxima fila) o
    // "abort" (corta el lote). maxRetries (arriba) ya rige los reintentos por fila.
    t["datasetOnError"]  = def.value("datasetOnError",
                                     t.value("datasetOnError", QStringLiteral("continue")));
    // Trigger de arranque desatendido: "manual" (default) o "fileWatch" (corre al
    // cambiar triggerPath, con debounce). Cron sigue en scheduleEnabled/scheduleCron.
    t["triggerType"]     = def.value("triggerType", t.value("triggerType", QStringLiteral("manual")));
    t["triggerPath"]     = def.value("triggerPath", t.value("triggerPath"));
    t["triggerDebounceMs"] = def.value("triggerDebounceMs", t.value("triggerDebounceMs", 1500));
    t["triggerHotkey"]   = def.value("triggerHotkey", t.value("triggerHotkey"));
    // Tipo de entrenamiento del replay: "literal" (reproducción determinista, tal
    // cual el Teach; liviano) o "adaptive" (el agente entiende título+descripción+
    // teach y decide cada paso; robusto ante cambios de UI, más pesado).
    t["trainingType"]    = def.value("trainingType", t.value("trainingType", QStringLiteral("literal")));
    t["discoverNetwork"] = def.value("discoverNetwork", t.value("discoverNetwork", false));
    t["updatedAt"]       = now;

    QString outId;
    if (row >= 0) {
        outId = id;
        m_items[row] = t;
        const QModelIndex mi = index(row);
        emit dataChanged(mi, mi);
    } else {
        outId = sanitize(def.value("name").toString());
        if (outId.isEmpty() || indexOfId(outId) >= 0)
            outId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        t["id"]        = outId;
        t["createdAt"] = now;
        beginInsertRows({}, m_items.size(), m_items.size());
        m_items.append(t);
        endInsertRows();
        emit countChanged();
    }
    save();
    emit changed();
    return outId;
}

bool TaskStore::remove(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0) return false;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    save();
    emit countChanged();
    emit changed();
    return true;
}

QVariantMap TaskStore::get(const QString &id) const
{
    const int row = indexOfId(id);
    return (row >= 0) ? m_items.at(row) : QVariantMap{};
}

QVariantList TaskStore::all() const
{
    QVariantList out;
    for (const QVariantMap &t : m_items) out.append(t);
    return out;
}

QString TaskStore::duplicate(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0) return {};
    QVariantMap t = m_items.at(row);
    t.remove("id");
    t["name"] = t.value("name").toString() + QStringLiteral(" (copia)");
    return save({}, t);
}

void TaskStore::markRun(const QString &id, const QString &status, const QString &summary)
{
    const int row = indexOfId(id);
    if (row < 0) return;
    m_items[row]["lastRunStatus"] = status;
    m_items[row]["lastRunAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (!summary.isEmpty())
        m_items[row]["lastRunSummary"] = summary;
    const QModelIndex mi = index(row);
    emit dataChanged(mi, mi);
    save();
    emit changed();
}

void TaskStore::markWorkflowState(const QString &id, const QVariantMap &state)
{
    const int row = indexOfId(id);
    if (row < 0) return;
    m_items[row][QStringLiteral("workflowState")] = state;
    const QModelIndex mi = index(row);
    emit dataChanged(mi, mi);
    save();
    emit changed();
}

void TaskStore::refresh()
{
    beginResetModel();
    load();
    endResetModel();
    emit countChanged();
    emit changed();
}

QString TaskStore::composePrompt(const QVariantMap &task)
{
    QStringList out;
    const QString name = task.value("name").toString().trimmed();
    const QString desc = task.value("description").toString().trimmed();

    out << QStringLiteral("Ejecutá la siguiente Task guardada de forma autónoma.");
    const QString pre = task.value("prePrompt").toString().trimmed();
    if (!pre.isEmpty()) {
        out << QString();
        out << QStringLiteral("Preprompt operativo:");
        out << pre;
    }
    if (!name.isEmpty())
        out << QStringLiteral("Task: %1").arg(name);
    if (!desc.isEmpty())
        out << QStringLiteral("Objetivo: %1").arg(desc);

    const QString permScope = task.value("permScope", QStringLiteral("project")).toString();
    if (permScope == QLatin1String("folder")) {
        const QStringList folders = task.value("permFolders").toStringList();
        if (!folders.isEmpty())
            out << QStringLiteral("Carpeta(s) de trabajo permitida(s) (escribí y leé acá, "
                                  "usá rutas absolutas): %1").arg(folders.join(QStringLiteral(", ")));
    } else if (permScope == QLatin1String("full")) {
        out << QStringLiteral("Tenés acceso a todo el disco; usá la ruta absoluta que indique el objetivo.");
    }

    const QVariantList steps = task.value("steps").toList();
    if (!steps.isEmpty()) {
        out << QString();
        out << QStringLiteral("Pasos de referencia (grabados en una corrida previa):");
        int i = 1;
        for (const QVariant &sv : steps) {
            const QVariantMap s = sv.toMap();
            const QString kind = s.value("kind").toString();
            const QString intent = s.value("intent").toString().trimmed();
            const QString ref = s.value("ref").toString().trimmed();
            QString line = QStringLiteral("%1. [%2] %3").arg(i++)
                               .arg(kind.isEmpty() ? QStringLiteral("instruction") : kind, intent);
            if (!ref.isEmpty())
                line += QStringLiteral(" (ref: %1)").arg(ref);
            out << line;
        }
    }

    out << QString();
    out << QStringLiteral("IMPORTANTE: los pasos son una guía, no un guion literal. "
                          "Entendé QUÉ se busca y POR QUÉ en cada paso. Si un botón, "
                          "elemento o archivo cambió de lugar o de nombre, adaptate y "
                          "logrueá el objetivo igual usando tus herramientas. Si algo "
                          "es ambiguo o riesgoso, explicá qué hiciste al terminar. "
                          "Si el objetivo requiere consultar una web, ejecutar comandos, "
                          "leer archivos o usar otra fuente externa, usá herramientas y "
                          "no respondas de memoria. Si no podés usar herramientas o no "
                          "podés verificar el dato, decilo explícitamente como error.");
    return out.join(QLatin1Char('\n'));
}

QString TaskStore::composePostPrompt(const QVariantMap &task)
{
    const QString post = task.value("postPrompt").toString().trimmed();
    if (post.isEmpty()) return {};
    QStringList out;
    out << QStringLiteral("Postprompt de verificación de la Task recién ejecutada.");
    out << QStringLiteral("Revisá el resultado anterior con criterio agéntico. Si detectás un problema, explicá el error concreto y qué habría que corregir; si está correcto, resumí la evidencia de éxito.");
    out << QString();
    out << post;
    return out.join(QLatin1Char('\n'));
}

TaskStore::LoopDecision TaskStore::decideLoop(const QVariantMap &task, int iteration,
                                              const QString &lastStatus,
                                              const QString &lastSummary,
                                              qint64 elapsedSeconds)
{
    if (!task.value("loopEnabled", false).toBool())
        return { false, QStringLiteral("bucle deshabilitado") };

    // No insistir sobre una corrida que terminó en error: el bucle reintenta
    // hacia un objetivo, no enmascara fallas duras de ejecución.
    if (lastStatus == QLatin1String("error"))
        return { false, QStringLiteral("se detuvo por error en la corrida") };

    const qint64 maxSeconds = qMax<qint64>(0, task.value("loopMaxSeconds", 0).toLongLong());
    if (maxSeconds > 0 && elapsedSeconds >= 0 && elapsedSeconds >= maxSeconds)
        return { false, QStringLiteral("se alcanzó el máximo de tiempo (%1 s)").arg(maxSeconds) };

    // El agente declaró el objetivo cumplido. GOAL_NOT_MET contiene GOAL_MET como
    // subcadena, así que descartamos primero el negativo.
    const QString sum = lastSummary.toUpper();
    if (!sum.contains(kGoalNotMetMarker) && sum.contains(kGoalMetMarker))
        return { false, QStringLiteral("objetivo cumplido") };

    const int maxIter = qBound(1, task.value("loopMaxIterations", 5).toInt(), 1000);
    if (iteration >= maxIter)
        return { false, QStringLiteral("se alcanzó el máximo de iteraciones (%1)").arg(maxIter) };

    return { true, QStringLiteral("objetivo no cumplido, reintentando (iteración %1/%2)")
                       .arg(iteration + 1).arg(maxIter) };
}

QString TaskStore::composeLoopGoalPrompt(const QVariantMap &task)
{
    if (!task.value("loopEnabled", false).toBool())
        return {};
    const QString goal = task.value("loopGoal").toString().trimmed();
    if (goal.isEmpty())
        return {};

    QStringList out;
    out << QStringLiteral("Evaluá si el objetivo del bucle ya se cumplió tras la corrida anterior.");
    out << QStringLiteral("Objetivo de éxito del bucle: %1").arg(goal);
    out << QString();
    out << QStringLiteral("Verificá con tus herramientas (no respondas de memoria). "
                          "Respondé en la PRIMERA línea EXACTAMENTE uno de estos marcadores:");
    out << QStringLiteral("  %1   — el objetivo está cumplido, no hace falta repetir.")
               .arg(kGoalMetMarker);
    out << QStringLiteral("  %1 — el objetivo todavía NO se cumplió; habrá otra iteración.")
               .arg(kGoalNotMetMarker);
    out << QStringLiteral("Luego, en las líneas siguientes, explicá brevemente la evidencia "
                          "y, si falta, qué conviene ajustar en la próxima iteración.");
    return out.join(QLatin1Char('\n'));
}

QString TaskStore::composeLoopProgress(const QString &priorVerdict, int completedIterations)
{
    QString note = priorVerdict.trimmed();
    if (note.isEmpty()) return {};

    // Sacar el/los marcador(es) GOAL_* de la primera línea (la verificación los pone
    // ahí); el resto de esa línea + las siguientes son la evidencia/qué ajustar.
    const int nl = note.indexOf(QLatin1Char('\n'));
    QString firstLine = (nl < 0) ? note : note.left(nl);
    const QString restLines = (nl < 0) ? QString() : note.mid(nl + 1);
    firstLine.remove(kGoalNotMetMarker, Qt::CaseInsensitive);
    firstLine.remove(kGoalMetMarker, Qt::CaseInsensitive);
    note = (firstLine.trimmed() + QLatin1Char('\n') + restLines).trimmed();
    if (note.isEmpty()) return {};

    if (note.size() > 1500) note = note.left(1500) + QStringLiteral("…");

    QStringList out;
    out << QStringLiteral("Progreso acumulado del bucle (ya completaste %1 iteración(es)). "
                          "Estado al final de la verificación previa:")
               .arg(qMax(1, completedIterations));
    out << note;
    out << QStringLiteral("Continuá DESDE este estado: NO rehagas lo ya logrado; "
                          "concentrate en lo que la verificación marcó como pendiente.");
    return out.join(QLatin1Char('\n'));
}

QString TaskStore::verifyProfileFor(const QVariantMap &task, const QString &execProfileId)
{
    const QString verify = task.value("verifyProfileId").toString().trimmed();
    if (verify.isEmpty() || verify == execProfileId.trimmed())
        return {};
    return verify;
}

QJsonObject TaskStore::toJson(const QVariantMap &task)
{
    QJsonObject o;
    o["id"]              = task.value("id").toString();
    o["name"]            = task.value("name").toString();
    o["description"]     = task.value("description").toString();
    o["profileId"]       = task.value("profileId").toString();
    o["prePrompt"]       = task.value("prePrompt").toString();
    o["postPrompt"]      = task.value("postPrompt").toString();
    o["silentUnlessError"] = task.value("silentUnlessError", false).toBool();
    o["scheduleEnabled"] = task.value("scheduleEnabled", false).toBool();
    o["scheduleCron"]    = task.value("scheduleCron").toString();
    o["scheduleSpec"]    = QJsonObject::fromVariantMap(task.value("scheduleSpec").toMap());
    o["permScope"]       = task.value("permScope", QStringLiteral("project")).toString();
    o["permFolders"]     = QJsonArray::fromStringList(task.value("permFolders").toStringList());
    o["createdAt"]       = task.value("createdAt").toString();
    o["updatedAt"]       = task.value("updatedAt").toString();
    o["lastRunAt"]       = task.value("lastRunAt").toString();
    o["lastRunStatus"]   = task.value("lastRunStatus").toString();
    o["lastRunSummary"]  = task.value("lastRunSummary").toString();
    o["executionMode"]   = task.value("executionMode", QStringLiteral("auto")).toString();
    o["approvalPolicy"]  = task.value("approvalPolicy", QStringLiteral("sensitive")).toString();
    o["safetyProfile"]   = task.value("safetyProfile", QStringLiteral("normal")).toString();
    o["teachArtifactId"] = task.value("teachArtifactId").toString();
    o["teachFormatVersion"] = task.value("teachFormatVersion", 1).toInt();
    o["trainedAt"]       = task.value("trainedAt").toString();
    o["scopeKind"]       = task.value("scopeKind", QStringLiteral("screen")).toString();
    o["scopeTargetId"]   = task.value("scopeTargetId").toString();
    o["scopeLabel"]      = task.value("scopeLabel").toString();
    o["scopeWidth"]      = task.value("scopeWidth", 0).toInt();
    o["scopeHeight"]     = task.value("scopeHeight", 0).toInt();
    o["scopeDpi"]        = task.value("scopeDpi", 96.0).toDouble();
    o["timeoutSec"]      = task.value("timeoutSec", 300).toInt();
    o["maxActions"]      = task.value("maxActions", 50).toInt();
    o["maxRetries"]      = task.value("maxRetries", 2).toInt();
    o["automationStatus"] = task.value("automationStatus", QStringLiteral("untrained")).toString();
    o["loopEnabled"]     = task.value("loopEnabled", false).toBool();
    o["loopGoal"]        = task.value("loopGoal").toString();
    o["loopMaxIterations"] = task.value("loopMaxIterations", 5).toInt();
    o["loopMaxSeconds"] = task.value("loopMaxSeconds", 0).toInt();
    o["verifyProfileId"] = task.value("verifyProfileId").toString();
    o["autoDifficultyRouting"] = task.value("autoDifficultyRouting", false).toBool();
    o["datasetInline"]   = task.value("datasetInline").toString();
    o["datasetFormat"]   = task.value("datasetFormat").toString();
    o["datasetPath"]     = task.value("datasetPath").toString();
    o["datasetOnError"]  = task.value("datasetOnError", QStringLiteral("continue")).toString();
    o["triggerType"]     = task.value("triggerType", QStringLiteral("manual")).toString();
    o["triggerPath"]     = task.value("triggerPath").toString();
    o["triggerDebounceMs"] = task.value("triggerDebounceMs", 1500).toInt();
    o["triggerHotkey"]   = task.value("triggerHotkey").toString();
    o["trainingType"]    = task.value("trainingType", QStringLiteral("literal")).toString();
    o["workflow"]        = QJsonObject::fromVariantMap(task.value("workflow").toMap());
    o["workflowState"]   = QJsonObject::fromVariantMap(task.value("workflowState").toMap());

    QJsonArray steps;
    for (const QVariant &sv : task.value("steps").toList()) {
        const QVariantMap s = sv.toMap();
        QJsonObject js;
        js["kind"]   = s.value("kind").toString();
        js["intent"] = s.value("intent").toString();
        js["ref"]    = s.value("ref").toString();
        steps.append(js);
    }
    o["steps"] = steps;
    return o;
}

QVariantMap TaskStore::fromJson(const QJsonObject &obj)
{
    QVariantMap t;
    t["id"]              = obj.value("id").toString();
    t["name"]            = obj.value("name").toString();
    t["description"]     = obj.value("description").toString();
    t["profileId"]       = obj.value("profileId").toString();
    t["prePrompt"]       = obj.value("prePrompt").toString();
    t["postPrompt"]      = obj.value("postPrompt").toString();
    t["silentUnlessError"] = obj.value("silentUnlessError").toBool(false);
    t["scheduleEnabled"] = obj.value("scheduleEnabled").toBool(false);
    t["scheduleCron"]    = obj.value("scheduleCron").toString();
    t["scheduleSpec"]    = obj.value("scheduleSpec").toObject().toVariantMap();
    t["permScope"]       = obj.contains("permScope") ? obj.value("permScope").toString()
                                                     : QStringLiteral("project");
    {
        QVariantList folders;
        for (const QJsonValue &fv : obj.value("permFolders").toArray())
            folders.append(fv.toString());
        t["permFolders"] = folders;
    }
    t["createdAt"]       = obj.value("createdAt").toString();
    t["updatedAt"]       = obj.value("updatedAt").toString();
    t["lastRunAt"]       = obj.value("lastRunAt").toString();
    t["lastRunStatus"]   = obj.value("lastRunStatus").toString();
    t["lastRunSummary"]  = obj.value("lastRunSummary").toString();
    t["executionMode"]   = obj.contains("executionMode")
        ? obj.value("executionMode").toString() : QStringLiteral("auto");
    t["approvalPolicy"]  = obj.contains("approvalPolicy")
        ? obj.value("approvalPolicy").toString() : QStringLiteral("sensitive");
    t["safetyProfile"]   = obj.contains("safetyProfile")
        ? obj.value("safetyProfile").toString() : QStringLiteral("normal");
    t["teachArtifactId"] = obj.value("teachArtifactId").toString();
    t["teachFormatVersion"] = obj.value("teachFormatVersion").toInt(1);
    t["trainedAt"]       = obj.value("trainedAt").toString();
    t["scopeKind"]       = obj.value("scopeKind").toString(QStringLiteral("screen"));
    t["scopeTargetId"]   = obj.value("scopeTargetId").toString();
    t["scopeLabel"]      = obj.value("scopeLabel").toString();
    t["scopeWidth"]      = obj.value("scopeWidth").toInt(0);
    t["scopeHeight"]     = obj.value("scopeHeight").toInt(0);
    t["scopeDpi"]        = obj.value("scopeDpi").toDouble(96.0);
    t["timeoutSec"]      = qBound(30, obj.value("timeoutSec").toInt(300), 3600);
    t["maxActions"]      = qBound(1, obj.value("maxActions").toInt(50), 500);
    t["maxRetries"]      = qBound(0, obj.value("maxRetries").toInt(2), 10);
    t["automationStatus"] = obj.value("automationStatus").toString(
        t.value("teachArtifactId").toString().isEmpty()
            ? QStringLiteral("untrained") : QStringLiteral("ready"));
    t["loopEnabled"]     = obj.value("loopEnabled").toBool(false);
    t["loopGoal"]        = obj.value("loopGoal").toString();
    t["loopMaxIterations"] = qBound(1, obj.value("loopMaxIterations").toInt(5), 1000);
    t["loopMaxSeconds"] = qBound(0, obj.value("loopMaxSeconds").toInt(0), 86400);
    t["verifyProfileId"] = obj.value("verifyProfileId").toString();
    t["autoDifficultyRouting"] = obj.value("autoDifficultyRouting").toBool(false);
    t["datasetInline"]   = obj.value("datasetInline").toString();
    t["datasetFormat"]   = obj.value("datasetFormat").toString();
    t["datasetPath"]     = obj.value("datasetPath").toString();
    t["datasetOnError"]  = obj.value("datasetOnError").toString(QStringLiteral("continue"));
    t["triggerType"]     = obj.value("triggerType").toString(QStringLiteral("manual"));
    t["triggerPath"]     = obj.value("triggerPath").toString();
    t["triggerDebounceMs"] = obj.value("triggerDebounceMs").toInt(1500);
    t["triggerHotkey"]   = obj.value("triggerHotkey").toString();
    t["trainingType"]    = obj.value("trainingType").toString(QStringLiteral("literal"));
    t["workflow"]        = obj.value("workflow").toObject().toVariantMap();
    t["workflowState"]   = obj.value("workflowState").toObject().toVariantMap();

    QVariantList steps;
    for (const QJsonValue &sv : obj.value("steps").toArray()) {
        const QJsonObject js = sv.toObject();
        QVariantMap s;
        s["kind"]   = js.value("kind").toString();
        s["intent"] = js.value("intent").toString();
        s["ref"]    = js.value("ref").toString();
        steps.append(s);
    }
    t["steps"] = steps;
    return t;
}

QString TaskStore::storagePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/tasks");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/tasks.json");
}

void TaskStore::load()
{
    m_items.clear();
    QFile f(storagePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    bool recoveredInterruptedRun = false;
    for (const QJsonValue &v : doc.array()) {
        QVariantMap task = fromJson(v.toObject());
        // `running` sólo es válido dentro de la vida del AppController que lanzó
        // la corrida. Si estamos cargando desde disco, aquella instancia ya no
        // existe: convertir el estado huérfano evita botones eternamente en
        // "Ejecutando..." después de crash, cierre o rebuild.
        if (task.value(QStringLiteral("lastRunStatus")).toString()
            == QLatin1String("running")) {
            const QString workflowStatus = task.value(QStringLiteral("workflowState")).toMap()
                                               .value(QStringLiteral("status")).toString();
            const bool resumable = workflowStatus == QLatin1String("running")
                                || workflowStatus == QLatin1String("waiting_approval");
            task[QStringLiteral("lastRunStatus")] = resumable
                ? QStringLiteral("resumable") : QStringLiteral("error");
            task[QStringLiteral("lastRunSummary")] = resumable
                ? QStringLiteral("Workflow interrumpido; se reanudará al iniciar el agente.")
                : QStringLiteral("La ejecución anterior fue interrumpida al cerrarse o reiniciarse LlamaCode.");
            recoveredInterruptedRun = true;
        }
        m_items.append(task);
    }
    if (recoveredInterruptedRun) save();
}

void TaskStore::save() const
{
    QJsonArray arr;
    for (const QVariantMap &t : m_items)
        arr.append(toJson(t));
    QFile f(storagePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.close();
}
