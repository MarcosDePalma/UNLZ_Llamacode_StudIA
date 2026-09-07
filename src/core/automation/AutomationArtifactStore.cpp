#include "AutomationArtifactStore.h"
#include "core/agent/BrowserTeach.h"
#include "core/tasks/TaskStore.h"
#include "DesktopRecoveryPolicy.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

namespace {
bool writeJson(const QString &path, const QVariantMap &value)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write(QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Indented));
    return true;
}
QVariantMap readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
}

QVariantMap imageMetadata(const QString &path)
{
    const QImage image(path);
    if (image.isNull()) return {};
    QFile file(path);
    QByteArray hash;
    if (file.open(QIODevice::ReadOnly))
        hash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
    return QVariantMap{{QStringLiteral("width"), image.width()},
                       {QStringLiteral("height"), image.height()},
                       {QStringLiteral("sha256"), QString::fromLatin1(hash)},
                       {QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
}

bool sameTemplateFile(const QVariantMap &locator, const QString &safe)
{
    return locator.value(QStringLiteral("type")).toString() == QLatin1String("image")
        && QFileInfo(locator.value(QStringLiteral("file")).toString()).fileName() == safe;
}

QVariantMap mergedMetadata(QVariantMap locator, const QVariantMap &metadata)
{
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it)
        locator[it.key()] = it.value();
    return locator;
}

void updateManifestTemplateCount(const QString &id, int count)
{
    QVariantMap manifest = readJson(AutomationArtifactStore::artifactDir(id)
                                    + QStringLiteral("/manifest.json"));
    if (manifest.isEmpty()) return;
    manifest[QStringLiteral("templateCount")] = count;
    manifest[QStringLiteral("updatedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    writeJson(AutomationArtifactStore::artifactDir(id) + QStringLiteral("/manifest.json"), manifest);
}
}

QString AutomationArtifactStore::rootDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                        + QStringLiteral("/automations");
    QDir().mkpath(dir);
    return dir;
}

bool AutomationArtifactStore::cleanupRuntimeObservations(int maxFiles, qint64 maxBytes)
{
    maxFiles = qBound(1, maxFiles, 10000);
    maxBytes = qBound<qint64>(1 * 1024 * 1024, maxBytes, 1024LL * 1024 * 1024);
    QDir dir(rootDir() + QStringLiteral("/runtime-observations"));
    if (!dir.exists()) return true;
    QFileInfoList files = dir.entryInfoList(QDir::Files);
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });
    qint64 keptBytes = 0;
    int keptFiles = 0;
    bool ok = true;
    for (const QFileInfo &info : files) {
        const bool keep = keptFiles < maxFiles
            && keptBytes + info.size() <= maxBytes;
        if (keep) {
            ++keptFiles;
            keptBytes += info.size();
            continue;
        }
        ok = QFile::remove(info.absoluteFilePath()) && ok;
    }
    return ok;
}

bool AutomationArtifactStore::clearRuntimeObservations()
{
    QDir dir(rootDir() + QStringLiteral("/runtime-observations"));
    if (!dir.exists()) return true;
    bool ok = true;
    for (const QFileInfo &info : dir.entryInfoList(QDir::Files))
        ok = QFile::remove(info.absoluteFilePath()) && ok;
    return ok;
}

QString AutomationArtifactStore::artifactDir(const QString &id)
{
    return rootDir() + QLatin1Char('/') + TaskStore::sanitize(id);
}

QString AutomationArtifactStore::create(const QVariantMap &task, const QVariantMap &scope,
                                        const QVariantList &events, const QStringList &evidence,
                                        const QString &browserScript)
{
    QString id = TaskStore::sanitize(task.value(QStringLiteral("id")).toString());
    if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    const QString dir = artifactDir(id);
    QDir().mkpath(dir + QStringLiteral("/evidence"));
    QDir().mkpath(dir + QStringLiteral("/templates"));
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QVariantMap manifest{
        {QStringLiteral("formatVersion"), FormatVersion},
        {QStringLiteral("recipeFormatVersion"), FormatVersion},
        {QStringLiteral("id"), id},
        {QStringLiteral("taskId"), task.value(QStringLiteral("id"))},
        {QStringLiteral("name"), task.value(QStringLiteral("name"))},
        {QStringLiteral("objective"), task.value(QStringLiteral("description"))},
        {QStringLiteral("executionMode"), task.value(QStringLiteral("executionMode"), QStringLiteral("auto"))},
        {QStringLiteral("approvalPolicy"), task.value(QStringLiteral("approvalPolicy"), QStringLiteral("sensitive"))},
        {QStringLiteral("scope"), scope},
        {QStringLiteral("requiresVision"), task.value(QStringLiteral("executionMode")) == QLatin1String("desktop")},
        {QStringLiteral("trainedAt"), now},
        {QStringLiteral("status"), QStringLiteral("ready")}};
    QVariantList steps;
    int index = 1;
    for (const QVariant &value : events) {
        QVariantMap event = value.toMap();
        event[QStringLiteral("index")] = index++;
        event[QStringLiteral("schemaVersion")] = FormatVersion;
        // v3 conserva los campos legacy, pero explicita el contrato que usan
        // el resolver y el repair runner. Teach viejo puede seguir escribiendo
        // sólo kind/intent/locator sin perder compatibilidad.
        event = DesktopRecoveryPolicy::contractForStep(event);
        if (event.contains(QStringLiteral("text")))
            event[QStringLiteral("text")] = redact(event.value(QStringLiteral("text")).toString());
        steps.append(event);
    }
    QVariantList templateRows;
    QSet<QString> seenTemplates;
    for (const QVariant &value : steps) {
        const QVariantMap step = value.toMap();
        QVariantList locators = step.value(QStringLiteral("locators")).toList();
        if (step.value(QStringLiteral("kind")).toString() == QLatin1String("visual_reference"))
            locators << step.value(QStringLiteral("locator"));
        for (const QVariant &lv : locators) {
            const QVariantMap locator = lv.toMap();
            if (locator.value(QStringLiteral("type")).toString() != QLatin1String("image")) continue;
            const QString file = QFileInfo(locator.value(QStringLiteral("file")).toString()).fileName();
            if (file.isEmpty() || seenTemplates.contains(file)) continue;
            seenTemplates.insert(file);
            QVariantMap row = locator;
            row[QStringLiteral("file")] = QStringLiteral("templates/") + file;
            templateRows << row;
        }
    }
    manifest[QStringLiteral("templateCount")] = templateRows.size();
    QVariantMap recipe{
        {QStringLiteral("formatVersion"), FormatVersion},
        {QStringLiteral("objective"), task.value(QStringLiteral("description"))},
        {QStringLiteral("prompt"), task.value(QStringLiteral("prePrompt"))},
        {QStringLiteral("steps"), steps},
        {QStringLiteral("evidence"), evidence},
        {QStringLiteral("templates"), templateRows},
        {QStringLiteral("learnings"), QVariantList{}},
        {QStringLiteral("networkDiscoveryEnabled"),
         task.value(QStringLiteral("discoverNetwork"), false)},
        {QStringLiteral("networkDiscoveries"), QVariantList{}},
        {QStringLiteral("successCriteria"), task.value(QStringLiteral("postPrompt"))}};
    recipe[QStringLiteral("schema")]=QStringLiteral("desktop-stateful-recipe-v3");
    recipe[QStringLiteral("compatibility")] = QVariantMap{
        {QStringLiteral("reads"), QVariantList{2, 3}},
        {QStringLiteral("writes"), 3}};
    // La captura del paso verification es la referencia visual canónica del
    // resultado enseñado (no una captura intermedia de una acción).
    for (auto it = steps.crbegin(); it != steps.crend(); ++it) {
        const QVariantMap step = it->toMap();
        if (step.value(QStringLiteral("kind")).toString() == QLatin1String("verification")) {
            recipe[QStringLiteral("finalReference")] = step.value(QStringLiteral("evidence"));
            break;
        }
    }
    if (!writeJson(dir + QStringLiteral("/manifest.json"), manifest)
        || !writeJson(dir + QStringLiteral("/recipe.json"), recipe))
        return {};
    if (!browserScript.isEmpty()) {
        QFile::remove(dir + QStringLiteral("/browser.mjs"));
        QFile::copy(browserScript, dir + QStringLiteral("/browser.mjs"));
    }
    return id;
}

QVariantMap AutomationArtifactStore::manifest(const QString &id)
{
    return readJson(artifactDir(id) + QStringLiteral("/manifest.json"));
}

QVariantMap AutomationArtifactStore::recipe(const QString &id)
{
    return readJson(artifactDir(id) + QStringLiteral("/recipe.json"));
}

QVariantList AutomationArtifactStore::timeline(const QString &id)
{
    return recipe(id).value(QStringLiteral("steps")).toList();
}

QVariantList AutomationArtifactStore::templates(const QString &id)
{
    return recipe(id).value(QStringLiteral("templates")).toList();
}

bool AutomationArtifactStore::removeTemplate(const QString &id, const QString &fileName)
{
    const QString safe = QFileInfo(fileName).fileName();
    if (safe.isEmpty()) return false;
    QVariantMap r = recipe(id);
    if (r.isEmpty()) return false;
    QVariantList kept;
    QStringList removedFiles{safe};
    const QString primaryPath = QStringLiteral("templates/") + safe;
    for (const QVariant &v : r.value(QStringLiteral("templates")).toList()) {
        const QVariantMap row = v.toMap();
        const QString rowFile = QFileInfo(row.value(QStringLiteral("file")).toString()).fileName();
        if (rowFile == safe || row.value(QStringLiteral("variantOf")).toString() == primaryPath) {
            if (!removedFiles.contains(rowFile)) removedFiles << rowFile;
        }
        else
            kept << row;
    }
    QVariantList rewrittenSteps;
    for (const QVariant &v : r.value(QStringLiteral("steps")).toList()) {
        QVariantMap step = v.toMap();
        QVariantList locators;
        for (const QVariant &lv : step.value(QStringLiteral("locators")).toList()) {
            const QString locatorFile = QFileInfo(lv.toMap().value(QStringLiteral("file")).toString()).fileName();
            if (!removedFiles.contains(locatorFile)) locators << lv;
        }
        if (step.contains(QStringLiteral("locators"))) step[QStringLiteral("locators")] = locators;
        const QVariantMap single = step.value(QStringLiteral("locator")).toMap();
        if (!single.isEmpty() && removedFiles.contains(
                QFileInfo(single.value(QStringLiteral("file")).toString()).fileName()))
            continue; // referencia visual huérfana: eliminar el paso completo
        rewrittenSteps << step;
    }
    r[QStringLiteral("steps")] = rewrittenSteps;
    r[QStringLiteral("templates")] = kept;
    bool removed = true;
    for (const QString &file : removedFiles)
        removed = QFile::remove(artifactDir(id) + QStringLiteral("/templates/") + file) && removed;
    const bool saved = writeJson(artifactDir(id) + QStringLiteral("/recipe.json"), r);
    if (saved) updateManifestTemplateCount(id, kept.size());
    return saved && removed;
}

bool AutomationArtifactStore::replaceTemplate(const QString &id, const QString &fileName,
                                               const QString &sourcePath)
{
    const QString safe = QFileInfo(fileName).fileName();
    QImage probe(sourcePath);
    if (safe.isEmpty() || probe.isNull()) return false;
    const QString destination = artifactDir(id) + QStringLiteral("/templates/") + safe;
    QDir().mkpath(QFileInfo(destination).absolutePath());
    QFile::remove(destination);
    if (!probe.save(destination, "PNG")) return false;
    const QVariantMap metadata = imageMetadata(destination);
    QVariantMap recipeMap = recipe(id);
    QVariantList templateRows;
    for (const QVariant &v : recipeMap.value(QStringLiteral("templates")).toList()) {
        QVariantMap row = v.toMap();
        if (QFileInfo(row.value(QStringLiteral("file")).toString()).fileName() == safe)
            row = mergedMetadata(row, metadata);
        templateRows << row;
    }
    QVariantList steps;
    for (const QVariant &v : recipeMap.value(QStringLiteral("steps")).toList()) {
        QVariantMap step = v.toMap();
        QVariantList locators;
        for (const QVariant &lv : step.value(QStringLiteral("locators")).toList()) {
            QVariantMap locator = lv.toMap();
            if (sameTemplateFile(locator, safe)) locator = mergedMetadata(locator, metadata);
            locators << locator;
        }
        if (step.contains(QStringLiteral("locators"))) step[QStringLiteral("locators")] = locators;
        QVariantMap single = step.value(QStringLiteral("locator")).toMap();
        if (sameTemplateFile(single, safe)) step[QStringLiteral("locator")] = mergedMetadata(single, metadata);
        steps << step;
    }
    recipeMap[QStringLiteral("templates")] = templateRows;
    recipeMap[QStringLiteral("steps")] = steps;
    return writeJson(artifactDir(id) + QStringLiteral("/recipe.json"), recipeMap);
}

bool AutomationArtifactStore::addTemplateVariant(const QString &id, const QString &fileName,
                                                  const QString &sourcePath)
{
    const QString safe = QFileInfo(fileName).fileName();
    const QImage source(sourcePath);
    QVariantMap r = recipe(id);
    if (safe.isEmpty() || source.isNull() || r.isEmpty()) return false;
    const QString stem = QFileInfo(safe).completeBaseName();
    int index = 1;
    QString variantFile;
    do variantFile = QStringLiteral("%1-variant-%2.png").arg(stem).arg(index++);
    while (QFile::exists(artifactDir(id) + QStringLiteral("/templates/") + variantFile));
    const QString destination = artifactDir(id) + QStringLiteral("/templates/") + variantFile;
    if (!source.save(destination, "PNG")) return false;
    QVariantMap row{{QStringLiteral("type"), QStringLiteral("image")},
                    {QStringLiteral("file"), QStringLiteral("templates/") + variantFile},
                    {QStringLiteral("variantOf"), QStringLiteral("templates/") + safe},
                    {QStringLiteral("threshold"), 0.88},
                    {QStringLiteral("minScale"), 0.8},
                    {QStringLiteral("maxScale"), 1.25},
                    {QStringLiteral("requireUnique"), true}};
    row = mergedMetadata(row, imageMetadata(destination));
    QVariantList rows = r.value(QStringLiteral("templates")).toList();
    rows << row;
    r[QStringLiteral("templates")] = rows;
    const bool saved = writeJson(artifactDir(id) + QStringLiteral("/recipe.json"), r);
    if (saved) updateManifestTemplateCount(id, rows.size());
    return saved;
}

QString AutomationArtifactStore::importBrowserSkill(const QString &skillName, const QVariantMap &task)
{
    if (!BrowserTeach::hasSkill(skillName)) return {};
    QVariantList events{
        QVariantMap{{QStringLiteral("kind"), QStringLiteral("browser")},
                    {QStringLiteral("intent"), QStringLiteral("Reproducir y adaptar la demostración importada")},
                    {QStringLiteral("ref"), skillName},
                    {QStringLiteral("target"), QVariantMap{
                        {QStringLiteral("surface"), QStringLiteral("browser")},
                        {QStringLiteral("mode"), QStringLiteral("backgroundExecution")},
                        {QStringLiteral("driver"), QStringLiteral("playwright")},
                        {QStringLiteral("skill"), skillName},
                        {QStringLiteral("script"), BrowserTeach::skillPath(skillName)}}},
                    {QStringLiteral("verification"), QStringLiteral("Verificar el objetivo de la Task")}}};
    QVariantMap browserTask = task;
    browserTask[QStringLiteral("executionMode")] = QStringLiteral("browserBackground");
    return create(browserTask, QVariantMap{{QStringLiteral("kind"), QStringLiteral("browser")}},
                  events, {}, BrowserTeach::skillPath(skillName));
}

bool AutomationArtifactStore::appendLearning(const QString &id, const QString &summary,
                                             const QString &log)
{
    QVariantMap r = recipe(id);
    if (r.isEmpty()) return false;
    QVariantList learnings = r.value(QStringLiteral("learnings")).toList();
    const QString cleanSummary = redact(summary).simplified().left(900);
    if (cleanSummary.isEmpty()) return false;

    // Dedup: una Task que corre bien siempre genera el MISMO resumen de éxito
    // ("¡Tarea completada! 2+2=4..."). Sin dedup, cada corrida apila un learning
    // casi idéntico (hasta 12) que augmentPrompt reinyecta en cada prompt futuro
    // → bloat que crece sin fin y frena el proc del primer turno. Firma
    // normalizada (sin dígitos/puntuación/emoji): si ya existe un learning con la
    // misma firma, no agregamos otro. Las adaptaciones reales (UI cambió) tienen
    // texto distinto → firma distinta → sí se guardan.
    auto signature = [](const QString &s) {
        static const QRegularExpression nonAlpha(
            QStringLiteral("[^\\p{L} ]"), QRegularExpression::UseUnicodePropertiesOption);
        return QString(s).toLower().remove(nonAlpha).simplified().left(160);
    };
    const QString newSig = signature(cleanSummary);
    for (const QVariant &v : learnings) {
        if (signature(v.toMap().value(QStringLiteral("summary")).toString()) == newSig)
            return true;   // repetido: no-op silencioso (no es error)
    }
    QStringList toolSignals;
    const QString safeLog = redact(log);
    static const QStringList markers{
        QStringLiteral("desktop_click_element"), QStringLiteral("desktop_controls"),
        QStringLiteral("desktop_find_image"), QStringLiteral("desktop_click_image"),
        QStringLiteral("desktop_wait_image"), QStringLiteral("desktop_assert_image"),
        QStringLiteral("desktop_observe"), QStringLiteral("desktop_windows"),
        QStringLiteral("mcp__playwright"), QStringLiteral("browser_"),
        QStringLiteral("run_shell")};
    for (const QString &marker : markers)
        if (safeLog.contains(marker) && !toolSignals.contains(marker)) toolSignals << marker;
    QVariantMap item{
        {QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("summary"), cleanSummary},
        {QStringLiteral("toolSignals"), toolSignals},
        {QStringLiteral("note"), QStringLiteral("Aprendizaje generado automáticamente tras una ejecución exitosa; úsalo como guía semántica, no como replay rígido.")}};
    learnings.append(item);
    while (learnings.size() > 12) learnings.removeFirst();
    r[QStringLiteral("learnings")] = learnings;
    r[QStringLiteral("updatedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return writeJson(artifactDir(id) + QStringLiteral("/recipe.json"), r);
}

bool AutomationArtifactStore::appendNetworkDiscovery(const QString &id,
                                                      const QVariantMap &evidence,
                                                      const QString &action)
{
    QVariantMap r = recipe(id);
    if (r.isEmpty()
        || evidence.value(QStringLiteral("kind")).toString()
               != QLatin1String("browser_network_evidence"))
        return false;
    QVariantList endpoints;
    for (const QVariant &value : evidence.value(QStringLiteral("endpoints")).toList()) {
        const QVariantMap endpoint = value.toMap();
        if (endpoint.value(QStringLiteral("method")).toString().isEmpty()
            || endpoint.value(QStringLiteral("origin")).toString().isEmpty()
            || endpoint.value(QStringLiteral("pathTemplate")).toString().isEmpty())
            continue;
        endpoints << QVariantMap{
            {QStringLiteral("method"), endpoint.value(QStringLiteral("method"))},
            {QStringLiteral("origin"), endpoint.value(QStringLiteral("origin"))},
            {QStringLiteral("pathTemplate"), endpoint.value(QStringLiteral("pathTemplate"))},
            {QStringLiteral("count"), endpoint.value(QStringLiteral("count"))},
            {QStringLiteral("statuses"), endpoint.value(QStringLiteral("statuses"))},
            {QStringLiteral("pathParameters"), endpoint.value(QStringLiteral("pathParameters"))},
            {QStringLiteral("queryParameterNames"), endpoint.value(QStringLiteral("queryParameterNames"))},
            {QStringLiteral("confidence"), endpoint.value(QStringLiteral("confidence"))}};
    }
    if (endpoints.isEmpty()) return false;

    QVariantList discoveries = r.value(QStringLiteral("networkDiscoveries")).toList();
    QVariantMap item{
        {QStringLiteral("at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("action"), redact(action).simplified().left(240)},
        {QStringLiteral("endpoints"), endpoints},
        {QStringLiteral("sequence"), evidence.value(QStringLiteral("sequence"))},
        {QStringLiteral("privacy"), evidence.value(QStringLiteral("privacy"))},
        {QStringLiteral("source"), QStringLiteral("playwright-mcp")},
        {QStringLiteral("reviewStatus"), QStringLiteral("pending")}};
    const QString signature = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(QJsonArray::fromVariantList(endpoints)).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
    item[QStringLiteral("signature")] = signature;
    for (int i = discoveries.size() - 1; i >= 0; --i) {
        if (discoveries.at(i).toMap().value(QStringLiteral("signature")).toString() == signature) {
            discoveries.removeAt(i);
            break;
        }
    }
    discoveries << item;
    while (discoveries.size() > 20) discoveries.removeFirst();
    r[QStringLiteral("networkDiscoveries")] = discoveries;
    r[QStringLiteral("updatedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return writeJson(artifactDir(id) + QStringLiteral("/recipe.json"), r);
}

QVariantList AutomationArtifactStore::networkDiscoveries(const QString &id)
{
    return recipe(id).value(QStringLiteral("networkDiscoveries")).toList();
}

bool AutomationArtifactStore::setNetworkDiscoveryReview(const QString &id,
                                                        const QString &signature,
                                                        const QString &status)
{
    if (status != QLatin1String("approved") && status != QLatin1String("rejected"))
        return false;
    QVariantMap r = recipe(id);
    if (r.isEmpty() || signature.trimmed().isEmpty()) return false;
    QVariantList discoveries;
    bool found = false;
    for (const QVariant &value : r.value(QStringLiteral("networkDiscoveries")).toList()) {
        QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("signature")).toString() == signature) {
            item[QStringLiteral("reviewStatus")] = status;
            item[QStringLiteral("reviewedAt")] =
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            found = true;
        }
        discoveries << item;
    }
    if (!found) return false;
    r[QStringLiteral("networkDiscoveries")] = discoveries;
    r[QStringLiteral("updatedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return writeJson(artifactDir(id) + QStringLiteral("/recipe.json"), r);
}

bool AutomationArtifactStore::clearNetworkDiscoveries(const QString &id)
{
    QVariantMap r = recipe(id);
    if (r.isEmpty()) return false;
    r[QStringLiteral("networkDiscoveries")] = QVariantList{};
    r[QStringLiteral("updatedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return writeJson(artifactDir(id) + QStringLiteral("/recipe.json"), r);
}

bool AutomationArtifactStore::removeEvidence(const QString &id, const QString &fileName)
{
    const QString safe = QFileInfo(fileName).fileName();
    if (safe.isEmpty()) return false;
    return QFile::remove(artifactDir(id) + QStringLiteral("/evidence/") + safe);
}

QString AutomationArtifactStore::redact(const QString &text)
{
    QString out = text;
    static const QRegularExpression jsonSecrets(
        QStringLiteral("(?i)\\\"(password|passwd|token|secret|api[_ -]?key)"
                       "\\\"\\s*:\\s*\\\"[^\\\"]*\\\""));
    out.replace(jsonSecrets, QStringLiteral("\"\\1\":\"[REDACTED]\""));
    static const QRegularExpression secrets(
        QStringLiteral("(?i)(password|passwd|token|secret|api[_ -]?key)\\s*[:=]\\s*\\S+"));
    out.replace(secrets, QStringLiteral("\\1=[REDACTED]"));
    return out;
}
