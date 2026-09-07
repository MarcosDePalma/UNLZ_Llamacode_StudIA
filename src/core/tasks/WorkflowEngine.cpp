#include "WorkflowEngine.h"

#include <QDateTime>
#include <QJsonArray>
#include <QMetaType>
#include <QRegularExpression>
#include <QSet>

QString WorkflowEngine::validate(const QJsonObject &def)
{
    if (def.value(QStringLiteral("schemaVersion")).toInt(1) != 1)
        return QStringLiteral("schemaVersion no soportada");
    const QString entry = def.value(QStringLiteral("entry")).toString();
    const QJsonObject steps = def.value(QStringLiteral("steps")).toObject();
    if (entry.isEmpty() || !steps.contains(entry)) return QStringLiteral("entry inexistente");
    if (steps.size() > 128) return QStringLiteral("demasiados pasos");
    static const QSet<QString> types{QStringLiteral("agent"), QStringLiteral("tool"),
        QStringLiteral("approval"), QStringLiteral("condition"), QStringLiteral("parallel"),
        QStringLiteral("verify"), QStringLiteral("repair"), QStringLiteral("finish")};
    for (auto it = steps.begin(); it != steps.end(); ++it) {
        const QJsonObject step = it.value().toObject();
        if (!types.contains(step.value(QStringLiteral("type")).toString()))
            return QStringLiteral("tipo invalido en %1").arg(it.key());
        QStringList routeKeys{QStringLiteral("next"), QStringLiteral("onSuccess"),
                              QStringLiteral("onFailure"), QStringLiteral("onBlocked"),
                              QStringLiteral("onTrue"), QStringLiteral("onFalse")};
        if (step.value(QStringLiteral("type")).toString() == QLatin1String("approval")) {
            const QJsonObject choices = step.value(QStringLiteral("choices")).toObject();
            for (auto choice = choices.begin(); choice != choices.end(); ++choice)
                routeKeys << choice.key();
            for (const QString &choice : {QStringLiteral("accept"), QStringLiteral("reject")})
                if (step.contains(choice)) routeKeys << choice;
        }
        for (const QString &key : routeKeys) {
            const QString target = step.value(key).toString();
            const QString choiceTarget = step.value(QStringLiteral("choices")).toObject()
                                             .value(key).toString();
            const QString resolved = target.isEmpty() ? choiceTarget : target;
            if (!resolved.isEmpty() && resolved != QLatin1String("stop") && !steps.contains(resolved))
                return QStringLiteral("destino %1 inexistente en %2").arg(resolved, it.key());
        }
    }
    return {};
}

WorkflowEngine::State WorkflowEngine::start(const QJsonObject &def, const QString &id,
                                             const QVariantMap &variables)
{
    State s;
    s.workflowId = id;
    s.variables = variables;
    s.startedAtMs = QDateTime::currentMSecsSinceEpoch();
    s.error = validate(def);
    if (!s.error.isEmpty()) { s.status = Failed; return s; }
    s.currentStep = def.value(QStringLiteral("entry")).toString();
    s.status = Running;
    if (currentStep(def, s).value(QStringLiteral("type")).toString() == QLatin1String("approval"))
        s.status = WaitingApproval;
    return s;
}

QJsonObject WorkflowEngine::currentStep(const QJsonObject &def, const State &s)
{
    return def.value(QStringLiteral("steps")).toObject().value(s.currentStep).toObject();
}

QString WorkflowEngine::nextStep(const QJsonObject &step, const QString &route)
{
    if (!route.isEmpty()) {
        const QString routed = step.value(route).toString();
        if (!routed.isEmpty()) return routed;
        const QString choice = step.value(QStringLiteral("choices")).toObject()
                                   .value(route).toString();
        if (!choice.isEmpty()) return choice;
        return {};
    }
    return step.value(QStringLiteral("next")).toString();
}

QString WorkflowEngine::resultVerdict(const QVariant &result)
{
    // Tipo EXACTO, no canConvert: un QVariant(QString) dice que si a
    // QVariantMap/QVariantList (Qt ofrece conversiones laxas), asi que un
    // "LC_GATE: PASS" con texto abajo entraba por la rama agregadora y salia
    // "fail" en vez de llegar al regex de la primera linea.
    const int typeId = result.metaType().id();
    if (typeId == QMetaType::QVariantMap) {
        const QVariantMap map = result.toMap();
        if (!map.isEmpty()) {
            bool allPass = true;
            for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
                const QString verdict = resultVerdict(it.value());
                if (verdict == QLatin1String("blocked")) return verdict;
                if (verdict != QLatin1String("pass")) allPass = false;
            }
            return allPass ? QStringLiteral("pass") : QStringLiteral("fail");
        }
    }
    if (typeId == QMetaType::QVariantList || typeId == QMetaType::QStringList) {
        const QVariantList list = result.toList();
        if (!list.isEmpty()) {
            bool allPass = true;
            for (const QVariant &item : list) {
                const QString verdict = resultVerdict(item);
                if (verdict == QLatin1String("blocked")) return verdict;
                if (verdict != QLatin1String("pass")) allPass = false;
            }
            return allPass ? QStringLiteral("pass") : QStringLiteral("fail");
        }
    }
    const QString firstLine = result.toString().left(16 * 1024)
                                  .section(QLatin1Char('\n'), 0, 0).trimmed();
    static const QRegularExpression rx(
        QStringLiteral("^(?:LC_GATE|VERDICT)\\s*:\\s*(PASS|FAIL|BLOCKED)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = rx.match(firstLine);
    return match.hasMatch() ? match.captured(1).toLower() : QString();
}

bool WorkflowEngine::budgetExceeded(const QJsonObject &def, const State &s)
{
    const QJsonObject budget = def.value(QStringLiteral("budget")).toObject();
    const int maxIterations = budget.value(QStringLiteral("maxIterations")).toInt(64);
    const qint64 maxSeconds = budget.value(QStringLiteral("maxSeconds")).toInteger(3600);
    return s.iterations >= maxIterations ||
        (maxSeconds > 0 && QDateTime::currentMSecsSinceEpoch() - s.startedAtMs > maxSeconds * 1000);
}

bool WorkflowEngine::repairBudgetExceeded(const QJsonObject &def, const State &s)
{
    const QJsonObject budget = def.value(QStringLiteral("budget")).toObject();
    if (!budget.contains(QStringLiteral("maxRepairs"))) return false;
    return s.repairAttempts >= qMax(0, budget.value(QStringLiteral("maxRepairs")).toInt());
}

bool WorkflowEngine::completeStep(const QJsonObject &def, State *s, const QVariant &result,
                                  bool success, const QString &route)
{
    if (!s || (s->status != Running && s->status != WaitingApproval)) return false;
    const QJsonObject step = currentStep(def, *s);
    if (step.isEmpty()) { s->status = Failed; s->error = QStringLiteral("paso inexistente"); return false; }
    s->results[s->currentStep] = result;
    s->completedSteps.append(s->currentStep);
    s->lastVerdict = resultVerdict(result);
    ++s->iterations;
    if (budgetExceeded(def, *s)) {
        s->status = Failed; s->error = QStringLiteral("presupuesto agotado"); return false;
    }
    QString next;
    if (!route.isEmpty()) {
        next = nextStep(step, route);
    } else {
        next = nextStep(step, success ? QStringLiteral("onSuccess")
                                      : QStringLiteral("onFailure"));
        if (next.isEmpty()) next = nextStep(step, QString());
    }
    if (!success && next.isEmpty()) {
        s->status = Failed; s->error = QStringLiteral("paso fallido sin recuperacion"); return false;
    }
    if (next.isEmpty() || next == QLatin1String("stop") ||
        step.value(QStringLiteral("type")).toString() == QLatin1String("finish")) {
        s->currentStep.clear(); s->status = success ? Completed : Failed; return true;
    }
    if (def.value(QStringLiteral("steps")).toObject().value(next).toObject()
            .value(QStringLiteral("type")).toString() == QLatin1String("repair")) {
        if (repairBudgetExceeded(def, *s)) {
            s->status = Failed;
            s->error = QStringLiteral("presupuesto de reparaciones agotado");
            return false;
        }
        ++s->repairAttempts;
    }
    s->currentStep = next;
    s->status = currentStep(def, *s).value(QStringLiteral("type")).toString() == QLatin1String("approval")
        ? WaitingApproval : Running;
    return true;
}

bool WorkflowEngine::approve(const QJsonObject &def, State *s, const QString &choice,
                             const QString &userText)
{
    if (!s || s->status != WaitingApproval) return false;
    if (!userText.isEmpty()) s->variables[QStringLiteral("userText")] = userText;
    return completeStep(def, s, choice, true, choice);
}

QJsonObject WorkflowEngine::toJson(const State &s)
{
    return {{QStringLiteral("schemaVersion"), s.schemaVersion},
            {QStringLiteral("workflowId"), s.workflowId},
            {QStringLiteral("currentStep"), s.currentStep},
            {QStringLiteral("completedSteps"), QJsonArray::fromStringList(s.completedSteps)},
            {QStringLiteral("results"), QJsonObject::fromVariantMap(s.results)},
            {QStringLiteral("variables"), QJsonObject::fromVariantMap(s.variables)},
            {QStringLiteral("iterations"), s.iterations},
            {QStringLiteral("repairAttempts"), s.repairAttempts},
            {QStringLiteral("startedAtMs"), s.startedAtMs},
            {QStringLiteral("status"), statusName(s.status)},
            {QStringLiteral("error"), s.error},
            {QStringLiteral("lastVerdict"), s.lastVerdict}};
}

WorkflowEngine::State WorkflowEngine::fromJson(const QJsonObject &o, QString *error)
{
    State s;
    s.schemaVersion = o.value(QStringLiteral("schemaVersion")).toInt(1);
    if (s.schemaVersion != 1) { if (error) *error = QStringLiteral("snapshot no soportado"); s.status = Failed; return s; }
    s.workflowId = o.value(QStringLiteral("workflowId")).toString();
    s.currentStep = o.value(QStringLiteral("currentStep")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("completedSteps")).toArray()) s.completedSteps << v.toString();
    s.results = o.value(QStringLiteral("results")).toObject().toVariantMap();
    s.variables = o.value(QStringLiteral("variables")).toObject().toVariantMap();
    s.iterations = o.value(QStringLiteral("iterations")).toInt();
    s.repairAttempts = o.value(QStringLiteral("repairAttempts")).toInt();
    s.startedAtMs = o.value(QStringLiteral("startedAtMs")).toInteger();
    const QString status = o.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("running")) s.status = Running;
    else if (status == QLatin1String("waiting_approval")) s.status = WaitingApproval;
    else if (status == QLatin1String("completed")) s.status = Completed;
    else if (status == QLatin1String("failed")) s.status = Failed;
    else if (status == QLatin1String("cancelled")) s.status = Cancelled;
    else s.status = Ready;
    s.error = o.value(QStringLiteral("error")).toString();
    s.lastVerdict = o.value(QStringLiteral("lastVerdict")).toString();
    if (error) error->clear();
    return s;
}

QString WorkflowEngine::statusName(Status status)
{
    switch (status) {
    case Ready: return QStringLiteral("ready");
    case Running: return QStringLiteral("running");
    case WaitingApproval: return QStringLiteral("waiting_approval");
    case Completed: return QStringLiteral("completed");
    case Failed: return QStringLiteral("failed");
    case Cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("failed");
}
