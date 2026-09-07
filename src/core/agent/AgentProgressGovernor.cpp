#include "AgentProgressGovernor.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

AgentProgressGovernor::AgentProgressGovernor(const Policy &policy)
{
    setPolicy(policy);
}

void AgentProgressGovernor::setPolicy(const Policy &policy)
{
    m_policy.initialCredits = qMax(2, policy.initialCredits);
    m_policy.maxCredits = qMax(m_policy.initialCredits, policy.maxCredits);
    m_policy.replanAfter = qMax(2, policy.replanAfter);
    m_policy.stopAfterReplan = qMax(2, policy.stopAfterReplan);
    reset();
}

void AgentProgressGovernor::reset(const QString &objective)
{
    m_credits = m_policy.initialCredits;
    m_stagnant = 0;
    m_replans = 0;
    m_evidence.clear();
    m_semanticSuccess.clear();
    m_exactWriteSuccess.clear();
    m_contentFirstPath.clear();
    m_lastWriteContentHash.clear();
    m_failedSignatures.clear();
    m_expectedArtifacts.clear();
    m_completedArtifacts.clear();
    const QString lower = objective.toLower();
    m_multiLanguageRequested = lower.contains(QStringLiteral("multilenguaje"))
        || lower.contains(QStringLiteral("multi-language"))
        || lower.contains(QStringLiteral("multiple languages"))
        || lower.contains(QStringLiteral("cada lenguaje"))
        || lower.contains(QStringLiteral("all languages"));
    static const QRegularExpression artifact(
        QStringLiteral(R"((?:^|[\s`'\"])([\w./\\-]+\.[a-z0-9]{1,8})(?=$|[\s`'\",:;)]))"),
        QRegularExpression::CaseInsensitiveOption);
    auto it = artifact.globalMatch(objective);
    while (it.hasNext())
        m_expectedArtifacts.insert(QDir::cleanPath(it.next().captured(1).toLower()));
}

static QString normalizedTarget(QString path)
{
    path = QDir::cleanPath(path.trimmed()).toLower();
    const QFileInfo fi(path);
    QString name = fi.fileName();
    // Variantes multilenguaje del mismo experimento son una sola intención.
    static const QRegularExpression disposableExt(
        QStringLiteral(R"(\.(?:py|cpp|c|cc|h|hpp|go|java|js|ts|rs|rb|php|pl|lua|sh|bat|cmd|ps1|md|txt)$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (name.startsWith(QStringLiteral("run_test"))
        || name.startsWith(QStringLiteral("test_run"))
        || name.startsWith(QStringLiteral("tmp_test")))
        name.replace(disposableExt, QStringLiteral(".*"));
    const QString dir = fi.path() == QLatin1String(".") ? QString() : fi.path() + QLatin1Char('/');
    return dir + name;
}

QString AgentProgressGovernor::semanticKey(const QString &tool, const QString &arguments)
{
    QJsonParseError error;
    const QJsonObject args = QJsonDocument::fromJson(arguments.toUtf8(), &error).object();
    QString target;
    for (const QString &key : {QStringLiteral("path"), QStringLiteral("file"),
                               QStringLiteral("pattern"), QStringLiteral("url")}) {
        target = args.value(key).toString();
        if (!target.isEmpty()) break;
    }
    QString family = tool.toLower();
    if (family == QLatin1String("write_file") || family == QLatin1String("edit_file"))
        family = QStringLiteral("write");
    else if (family == QLatin1String("read_file") || family == QLatin1String("list_dir")
             || family == QLatin1String("glob") || family == QLatin1String("grep"))
        family = QStringLiteral("inspect");
    else if (family == QLatin1String("run_shell"))
        family = QStringLiteral("execute");
    if (target.isEmpty() && error.error == QJsonParseError::NoError)
        target = args.value(QStringLiteral("command")).toString().left(160);
    return family + QLatin1Char('|') + normalizedTarget(target);
}

AgentProgressGovernor::Decision AgentProgressGovernor::record(
    const QString &tool, const QString &arguments, bool ok, const QString &result, bool isWrite)
{
    Decision d;
    d.semanticKey = semanticKey(tool, arguments);
    const QJsonObject parsedArgs = QJsonDocument::fromJson(arguments.toUtf8()).object();
    const QString exactWriteTarget = isWrite
        ? QDir::cleanPath(parsedArgs.value(QStringLiteral("path")).toString().toLower())
        : QString();
    if (m_multiLanguageRequested && isWrite) {
        d.semanticKey = QStringLiteral("write|")
            + exactWriteTarget;
    }
    if (ok && isWrite) {
        if (m_expectedArtifacts.contains(exactWriteTarget))
            m_completedArtifacts.insert(exactWriteTarget);
    }
    const QByteArray evidenceBytes = (d.semanticKey + QLatin1Char('|')
        + result.trimmed().left(4096)).toUtf8();
    const QString evidence = QString::fromLatin1(
        QCryptographicHash::hash(evidenceBytes, QCryptographicHash::Sha256).toHex());
    const bool newEvidence = ok && !result.trimmed().isEmpty() && !m_evidence.contains(evidence);
    const bool newSemanticWrite = ok && isWrite && !m_semanticSuccess.contains(d.semanticKey);
    bool writeContentChanged = false;

    // Re-escribir lo MISMO con otro nombre no es progreso. La clave semántica de
    // una escritura incluye el path, así que sin este chequeo alcanzaba con
    // renombrar para que cada vuelta contara como evidencia nueva: el contador de
    // estancamiento se reseteaba y encima sumaba créditos. Medido en un benchmark
    // real: 1.436 archivos .py en una sola tarea (is_prime.py, prime_check.py,
    // prime_checker.py, prime_checker_v10.py…) y 704 en otra con nombres ya
    // degenerados (add.py, add_only.py, add_only_only.py).
    bool duplicateElsewhere = false;
    if (ok && isWrite) {
        const bool hasContentField = parsedArgs.contains(QStringLiteral("content"));
        const QString content = parsedArgs.value(QStringLiteral("content")).toString();
        if (!hasContentField) {
            // edit_file suele transportar un patch/diff en otros campos; en ese
            // protocolo el resultado cambia cuando realmente hubo una edición.
            writeContentChanged = m_exactWriteSuccess.contains(exactWriteTarget)
                && newEvidence;
        } else if (!content.trimmed().isEmpty()) {
            const QString contentHash = QString::fromLatin1(
                QCryptographicHash::hash(content.trimmed().toUtf8(),
                                         QCryptographicHash::Sha256).toHex());
            writeContentChanged = m_lastWriteContentHash.value(exactWriteTarget) != contentHash;
            m_lastWriteContentHash.insert(exactWriteTarget, contentHash);
            const QString firstPath = m_contentFirstPath.value(contentHash);
            if (firstPath.isEmpty())
                m_contentFirstPath.insert(contentHash, exactWriteTarget);
            else if (firstPath != exactWriteTarget)
                duplicateElsewhere = true;   // mismo contenido, archivo nuevo
        }
    }

    d.progress = newEvidence && !duplicateElsewhere
        && (!isWrite || newSemanticWrite || writeContentChanged);

    if (d.progress) {
        m_evidence.insert(evidence);
        m_semanticSuccess.insert(d.semanticKey);
        if (isWrite) m_exactWriteSuccess.insert(exactWriteTarget);
        m_stagnant = 0;
        m_credits = qMin(m_policy.maxCredits, m_credits + 2);
        d.reason = QStringLiteral("evidencia nueva");
    } else if (!ok && m_failedSignatures.contains(evidence)) {
        // Mismo fallo, misma acción: el modelo esta en bucle. Doble costo para
        // llegar al replanteo/corte sin gastar diez turnos.
        ++m_stagnant;
        m_credits -= 2;
        d.repeatedFailure = true;
        d.reason = QStringLiteral("acción fallida ya intentada (bucle)");
    } else {
        ++m_stagnant;
        --m_credits;
        if (!ok) m_failedSignatures.insert(evidence);
        d.reason = ok ? QStringLiteral("acción equivalente sin evidencia nueva")
                      : QStringLiteral("acción fallida");
    }

    // Techo duro: da igual cuánto "progreso" declare el presupuesto elástico, si el
    // turno ya generó decenas de archivos distintos está en un bucle de renombres.
    if (m_policy.maxDistinctWrites > 0
        && m_exactWriteSuccess.size() > m_policy.maxDistinctWrites) {
        d.action = Stop;
        d.reason = QStringLiteral("demasiados archivos distintos en un turno (%1): bucle de renombres")
                       .arg(m_exactWriteSuccess.size());
    } else if (m_replans == 0 && (m_stagnant >= m_policy.replanAfter || m_credits <= 0)) {
        ++m_replans;
        m_stagnant = 0;
        m_credits = qMax(2, m_policy.initialCredits / 2);
        d.action = Replan;
        d.reason = QStringLiteral("presupuesto estancado; requiere estrategia distinta");
    } else if (m_replans > 0 && m_stagnant >= m_policy.stopAfterReplan) {
        d.action = Stop;
        d.reason = QStringLiteral("sin progreso después del replanteo");
    }
    d.credits = m_credits;
    d.stagnant = m_stagnant;
    d.objectiveSatisfied = !m_expectedArtifacts.isEmpty()
        && m_completedArtifacts.size() == m_expectedArtifacts.size();
    return d;
}
