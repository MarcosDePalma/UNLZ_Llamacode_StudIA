#include "EngineeringWorkflowCatalog.h"

#include <QVariant>

namespace {
QVariantMap step(const QString &type, const QVariantMap &values = {})
{
    QVariantMap out = values;
    out.insert(QStringLiteral("type"), type);
    return out;
}

QVariantMap definition(const QString &id, const QString &name,
                       const QString &description, const QVariantList &steps,
                       const QString &entry = QStringLiteral("inspect"),
                       int maxRepairs = 0)
{
    QVariantMap stepMap;
    for (const QVariant &value : steps) {
        const QVariantMap item = value.toMap();
        stepMap.insert(item.value(QStringLiteral("id")).toString(), item);
    }
    QVariantMap budget{{QStringLiteral("maxIterations"), 32},
                       {QStringLiteral("maxSeconds"), 3600}};
    if (maxRepairs > 0) budget.insert(QStringLiteral("maxRepairs"), maxRepairs);
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
            {QStringLiteral("description"), description},
            {QStringLiteral("schemaVersion"), 1}, {QStringLiteral("entry"), entry},
            {QStringLiteral("budget"), budget},
            {QStringLiteral("steps"), stepMap}};
}
}

QVariantList EngineeringWorkflowCatalog::workflows()
{
    return {
        workflow(QStringLiteral("investigate")),
        workflow(QStringLiteral("qa")),
        workflow(QStringLiteral("document-audit")),
        workflow(QStringLiteral("review")),
        workflow(QStringLiteral("autoprompt")),
        workflow(QStringLiteral("release-check"))
    };
}

QVariantList EngineeringWorkflowCatalog::safetyProfiles()
{
    return {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("normal")},
                    {QStringLiteral("name"), QStringLiteral("Normal")},
                    {QStringLiteral("approvalPolicy"), QStringLiteral("sensitive")},
                    {QStringLiteral("permScope"), QStringLiteral("project")},
                    {QStringLiteral("description"), QStringLiteral("Aprobaciones sensibles y alcance del proyecto.")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("investigation")},
                    {QStringLiteral("name"), QStringLiteral("Investigación")},
                    {QStringLiteral("approvalPolicy"), QStringLiteral("always")},
                    {QStringLiteral("permScope"), QStringLiteral("project")},
                    {QStringLiteral("description"), QStringLiteral("Prioriza lectura, hipótesis y aprobación antes de editar.")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("guarded")},
                    {QStringLiteral("name"), QStringLiteral("Guardado")},
                    {QStringLiteral("approvalPolicy"), QStringLiteral("always")},
                    {QStringLiteral("permScope"), QStringLiteral("folder")},
                    {QStringLiteral("description"), QStringLiteral("Restringe edición a carpetas autorizadas y pide aprobación.")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("production")},
                    {QStringLiteral("name"), QStringLiteral("Producción")},
                    {QStringLiteral("approvalPolicy"), QStringLiteral("always")},
                    {QStringLiteral("permScope"), QStringLiteral("project")},
                    {QStringLiteral("description"), QStringLiteral("Sin acciones externas o destructivas automáticas.")}}
    };
}

QVariantMap EngineeringWorkflowCatalog::workflow(const QString &id)
{
    if (id == QLatin1String("investigate")) {
        return definition(id, QStringLiteral("Investigar bug"),
            QStringLiteral("Reúne contexto, formula hipótesis y valida la causa antes de editar."), {
                step(QStringLiteral("agent"), {{QStringLiteral("id"), QStringLiteral("inspect")},
                    {QStringLiteral("prompt"), QStringLiteral("Inspeccioná el síntoma, el contexto del workspace y el grafo de código. No edites archivos.")},
                    {QStringLiteral("next"), QStringLiteral("hypothesis")}}),
                step(QStringLiteral("agent"), {{QStringLiteral("id"), QStringLiteral("hypothesis")},
                    {QStringLiteral("prompt"), QStringLiteral("Formulá hipótesis de causa raíz y proponé una prueba verificable. No edites archivos.")},
                    {QStringLiteral("next"), QStringLiteral("approval")}}),
                step(QStringLiteral("approval"), QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("approval")},
                    {QStringLiteral("prompt"), QStringLiteral("¿Confirmás ejecutar la estrategia de investigación y permitir cambios?")},
                    {QStringLiteral("accept"), QStringLiteral("verify")},
                    {QStringLiteral("reject"), QStringLiteral("stop")}}),
                step(QStringLiteral("agent"), {{QStringLiteral("id"), QStringLiteral("verify")},
                    {QStringLiteral("prompt"), QStringLiteral("Ejecutá la prueba de la hipótesis, aplicá el cambio mínimo y explicá la evidencia.")},
                    {QStringLiteral("next"), QStringLiteral("finish")}}),
                step(QStringLiteral("finish"), {{QStringLiteral("id"), QStringLiteral("finish")}})
            });
    }
    if (id == QLatin1String("qa")) {
        return definition(id, QStringLiteral("QA con regresión"),
            QStringLiteral("Ejecuta pruebas, corrige el problema, agrega regresión y verifica nuevamente."), {
                step(QStringLiteral("agent"), {{QStringLiteral("id"), QStringLiteral("preflight")},
                    {QStringLiteral("prompt"), QStringLiteral("Revisá el objetivo, el diff y las pruebas relevantes. Identificá cómo reproducirlo.")},
                    {QStringLiteral("next"), QStringLiteral("reproduce")}}),
                step(QStringLiteral("agent"), {{QStringLiteral("id"), QStringLiteral("reproduce")},
                    {QStringLiteral("prompt"), QStringLiteral("Reproducí el fallo y guardá evidencia concreta antes de modificar archivos.")},
                    {QStringLiteral("next"), QStringLiteral("fix")}}),
                step(QStringLiteral("agent"), {{QStringLiteral("id"), QStringLiteral("fix")},
                    {QStringLiteral("prompt"), QStringLiteral("Aplicá el cambio mínimo, agregá una prueba de regresión y ejecutala.")},
                    {QStringLiteral("next"), QStringLiteral("verify")}}),
                step(QStringLiteral("agent"), {{QStringLiteral("id"), QStringLiteral("verify")},
                    {QStringLiteral("prompt"), QStringLiteral("Ejecutá la regresión, la suite relevante y reportá evidencia antes/después.")},
                    {QStringLiteral("next"), QStringLiteral("finish")}}),
                step(QStringLiteral("finish"), {{QStringLiteral("id"), QStringLiteral("finish")}})
            }, QStringLiteral("preflight"));
    }
    if (id == QLatin1String("document-audit")) {
        return definition(id, QStringLiteral("Auditar documentación"),
            QStringLiteral("Compara cambios y comportamiento con README, AGENTS y documentación técnica."), {
                step(QStringLiteral("agent"), {{QStringLiteral("id"), QStringLiteral("inspect")},
                    {QStringLiteral("prompt"), QStringLiteral("Inspeccioná el diff y localizá documentación relacionada, referencias rotas y contradicciones.")},
                    {QStringLiteral("next"), QStringLiteral("report")}}),
                step(QStringLiteral("agent"), {{QStringLiteral("id"), QStringLiteral("report")},
                    {QStringLiteral("prompt"), QStringLiteral("Generá un informe con actualizado, obsoleto, faltante y contradictorio. No edites todavía.")},
                    {QStringLiteral("next"), QStringLiteral("approval")}}),
                step(QStringLiteral("approval"), {{QStringLiteral("id"), QStringLiteral("approval")},
                    {QStringLiteral("prompt"), QStringLiteral("¿Querés aplicar las actualizaciones documentales propuestas?")},
                    {QStringLiteral("accept"), QStringLiteral("update")}, {QStringLiteral("reject"), QStringLiteral("finish")}}),
                step(QStringLiteral("agent"), {{QStringLiteral("id"), QStringLiteral("update")},
                    {QStringLiteral("prompt"), QStringLiteral("Actualizá sólo la documentación necesaria y verificá que los enlaces y comandos sean válidos.")},
                    {QStringLiteral("next"), QStringLiteral("finish")}}),
                step(QStringLiteral("finish"), {{QStringLiteral("id"), QStringLiteral("finish")}})
            });
    }
    if (id == QLatin1String("review")) {
        return definition(id, QStringLiteral("Revisar cambios"),
            QStringLiteral("Busca bugs, complejidad innecesaria, cambios fuera de alcance y cobertura faltante."), {
                step(QStringLiteral("agent"), {{QStringLiteral("id"), QStringLiteral("inspect")},
                    {QStringLiteral("prompt"), QStringLiteral("Revisá el diff completo contra el objetivo. Priorizá fallos reales y evidencia.")},
                    {QStringLiteral("next"), QStringLiteral("challenge")}}),
                step(QStringLiteral("parallel"), {{QStringLiteral("id"), QStringLiteral("challenge")},
                    {QStringLiteral("prompt"), QStringLiteral("Realizá una revisión adversarial independiente del cambio.")},
                    {QStringLiteral("branches"), QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("correctness")}, {QStringLiteral("prompt"), QStringLiteral("Buscá errores funcionales y regresiones.")}}, QVariantMap{{QStringLiteral("id"), QStringLiteral("scope")}, {QStringLiteral("prompt"), QStringLiteral("Buscá complejidad, cambios ajenos y documentación o tests faltantes.")}}}},
                    {QStringLiteral("next"), QStringLiteral("finish")}}),
                step(QStringLiteral("finish"), {{QStringLiteral("id"), QStringLiteral("finish")}})
            });
    }
    if (id == QLatin1String("autoprompt")) {
        const QString gate = QStringLiteral(
            "La primera línea de la respuesta debe ser exactamente `LC_GATE: PASS`, "
            "`LC_GATE: FAIL` o `LC_GATE: BLOCKED`. PASS sólo si hay evidencia concreta; "
            "FAIL si falta un requisito o una prueba; BLOCKED si no podés continuar sin "
            "una decisión o permiso externo.");
        return definition(id, QStringLiteral("Autoprompt: planificar, construir y verificar"),
            QStringLiteral("Cierra el loop de una tarea compleja con plan, implementación, "
                           "revisión independiente, verificación y reparaciones acotadas."), {
                step(QStringLiteral("agent"), {
                    {QStringLiteral("id"), QStringLiteral("scope")},
                    {QStringLiteral("prompt"), QStringLiteral(
                        "Leé el objetivo, AGENTS.md/README.md y el estado del workspace. "
                        "Delimitá requisitos, riesgos, archivos candidatos y criterios de "
                        "aceptación. No edites archivos. %1").arg(gate)},
                    {QStringLiteral("verdictRequired"), true},
                    {QStringLiteral("next"), QStringLiteral("plan")},
                    {QStringLiteral("onFailure"), QStringLiteral("stop")},
                    {QStringLiteral("onBlocked"), QStringLiteral("stop")}}),
                step(QStringLiteral("agent"), {
                    {QStringLiteral("id"), QStringLiteral("plan")},
                    {QStringLiteral("prompt"), QStringLiteral(
                        "Diseñá un roadmap ejecutable, con pasos pequeños, pruebas y una "
                        "estrategia de verificación end-to-end. No edites archivos. %1").arg(gate)},
                    {QStringLiteral("verdictRequired"), true},
                    {QStringLiteral("next"), QStringLiteral("implement")},
                    {QStringLiteral("onFailure"), QStringLiteral("stop")},
                    {QStringLiteral("onBlocked"), QStringLiteral("stop")}}),
                step(QStringLiteral("agent"), {
                    {QStringLiteral("id"), QStringLiteral("implement")},
                    {QStringLiteral("prompt"), QStringLiteral(
                        "Implementá el roadmap con el cambio mínimo necesario. Agregá o "
                        "actualizá pruebas, ejecutalas y dejá evidencia reproducible. %1").arg(gate)},
                    {QStringLiteral("verdictRequired"), true},
                    {QStringLiteral("onSuccess"), QStringLiteral("review_verify")},
                    {QStringLiteral("onFailure"), QStringLiteral("repair")},
                    {QStringLiteral("onBlocked"), QStringLiteral("stop")}}),
                step(QStringLiteral("parallel"), {
                    {QStringLiteral("id"), QStringLiteral("review_verify")},
                    {QStringLiteral("prompt"), QStringLiteral("Cada rama debe producir su propio recibo de gate.")},
                    {QStringLiteral("verdictRequired"), true},
                    {QStringLiteral("branches"), QVariantList{
                        QVariantMap{{QStringLiteral("id"), QStringLiteral("reviewer")},
                                    {QStringLiteral("readOnly"), true},
                                    {QStringLiteral("prompt"), QStringLiteral(
                                        "Revisá de forma independiente el diff contra el objetivo. "
                                        "Buscá bugs, regresiones, complejidad innecesaria, cambios "
                                        "fuera de alcance y cobertura faltante. No modifiques nada. %1").arg(gate)}},
                        QVariantMap{{QStringLiteral("id"), QStringLiteral("verifier")},
                                    {QStringLiteral("readOnly"), true},
                                    {QStringLiteral("allowShell"), true},
                                    {QStringLiteral("prompt"), QStringLiteral(
                                        "Verificá criterios de aceptación y ejecutá sólo las pruebas "
                                        "necesarias. No edites archivos ni uses acciones externas. %1").arg(gate)}}}},
                    {QStringLiteral("onSuccess"), QStringLiteral("goal_check")},
                    {QStringLiteral("onFailure"), QStringLiteral("repair")},
                    {QStringLiteral("onBlocked"), QStringLiteral("stop")}}),
                step(QStringLiteral("repair"), {
                    {QStringLiteral("id"), QStringLiteral("repair")},
                    {QStringLiteral("prompt"), QStringLiteral(
                        "Repará únicamente los hallazgos del implementador/revisor/verificador. "
                        "No amplíes el alcance; ejecutá las pruebas afectadas y reportá la causa "
                        "de cada reparación. %1").arg(gate)},
                    {QStringLiteral("verdictRequired"), true},
                    {QStringLiteral("onSuccess"), QStringLiteral("review_verify")},
                    {QStringLiteral("onFailure"), QStringLiteral("review_verify")},
                    {QStringLiteral("onBlocked"), QStringLiteral("stop")}}),
                step(QStringLiteral("agent"), {
                    {QStringLiteral("id"), QStringLiteral("goal_check")},
                    {QStringLiteral("prompt"), QStringLiteral(
                        "Hacé una comprobación final fresca: contrastá cada requisito del objetivo "
                        "con el diff, las pruebas y la evidencia. No edites archivos. %1").arg(gate)},
                    {QStringLiteral("verdictRequired"), true},
                    {QStringLiteral("onSuccess"), QStringLiteral("finish")},
                    {QStringLiteral("onFailure"), QStringLiteral("repair")},
                    {QStringLiteral("onBlocked"), QStringLiteral("stop")}}),
                step(QStringLiteral("finish"), {{QStringLiteral("id"), QStringLiteral("finish")}})
            }, QStringLiteral("scope"), 3);
    }
    if (id == QLatin1String("release-check")) {
        return definition(id, QStringLiteral("Preparar release Debug"),
            QStringLiteral("Valida tests, build Debug, ejecutable y documentación antes de entregar."), {
                step(QStringLiteral("agent"), {{QStringLiteral("id"), QStringLiteral("check")},
                    {QStringLiteral("prompt"), QStringLiteral("Revisá branch, estado Git, archivos afectados, secretos y documentación.")},
                    {QStringLiteral("next"), QStringLiteral("tests")}}),
                step(QStringLiteral("tool"), {{QStringLiteral("id"), QStringLiteral("tests")},
                    {QStringLiteral("tool"), QStringLiteral("run_shell")},
                    {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("command"), QStringLiteral("tests.bat Debug")}}},
                    {QStringLiteral("next"), QStringLiteral("build")}}),
                step(QStringLiteral("tool"), {{QStringLiteral("id"), QStringLiteral("build")},
                    {QStringLiteral("tool"), QStringLiteral("run_shell")},
                    {QStringLiteral("arguments"), QVariantMap{{QStringLiteral("command"), QStringLiteral("build.bat Debug NOPAUSE")}}},
                    {QStringLiteral("next"), QStringLiteral("approval")}}),
                step(QStringLiteral("approval"), {{QStringLiteral("id"), QStringLiteral("approval")},
                    {QStringLiteral("prompt"), QStringLiteral("Tests y build terminaron. ¿Confirmás la entrega y el commit/push manual posterior?")},
                    {QStringLiteral("accept"), QStringLiteral("finish")}, {QStringLiteral("reject"), QStringLiteral("stop")}}),
                step(QStringLiteral("finish"), {{QStringLiteral("id"), QStringLiteral("finish")}})
            }, QStringLiteral("check"));
    }
    return {};
}

bool EngineeringWorkflowCatalog::isKnownWorkflow(const QString &id)
{
    return !workflow(id).isEmpty();
}

QVariantMap EngineeringWorkflowCatalog::installableTask(const QString &id)
{
    const QVariantMap wf = workflow(id);
    if (wf.isEmpty()) return {};
    return {{QStringLiteral("name"), wf.value(QStringLiteral("name"))},
            {QStringLiteral("description"), wf.value(QStringLiteral("description"))},
            {QStringLiteral("workflow"), wf},
            {QStringLiteral("approvalPolicy"), id == QLatin1String("release-check")
                ? QStringLiteral("always") : QStringLiteral("sensitive")},
            {QStringLiteral("safetyProfile"), id == QLatin1String("release-check")
                ? QStringLiteral("production") : QStringLiteral("normal")},
            {QStringLiteral("permScope"), QStringLiteral("project")},
            {QStringLiteral("silentUnlessError"), false},
            {QStringLiteral("executionMode"), QStringLiteral("auto")}};
}
