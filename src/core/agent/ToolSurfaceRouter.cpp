#include "ToolSurfaceRouter.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace {
QString normalized(QString value)
{
    value = value.toLower().trimmed();
    value.replace(QRegularExpression(QStringLiteral("[^a-záéíóúüñ0-9]+")),
                  QStringLiteral(" "));
    value.replace(QChar(0x00E1), QLatin1Char('a'));
    value.replace(QChar(0x00E9), QLatin1Char('e'));
    value.replace(QChar(0x00ED), QLatin1Char('i'));
    value.replace(QChar(0x00F3), QLatin1Char('o'));
    value.replace(QChar(0x00FA), QLatin1Char('u'));
    value.replace(QChar(0x00FC), QLatin1Char('u'));
    value.replace(QChar(0x00F1), QLatin1Char('n'));
    return value;
}

bool containsAny(const QString &text, const QStringList &needles)
{
    for (const QString &needle : needles)
        if (text.contains(needle)) return true;
    return false;
}
} // namespace

QVariantMap ToolSurfaceRouter::Decision::toVariantMap() const
{
    return {{QStringLiteral("groups"), groups},
            {QStringLiteral("confidence"), confidence},
            {QStringLiteral("fullSurface"), fullSurface},
            {QStringLiteral("reason"), reason}};
}

QString ToolSurfaceRouter::canonicalGroup(const QString &label)
{
    const QString group = normalized(label);
    if (group == QLatin1String("archivos")) return QStringLiteral("files");
    if (group == QLatin1String("busqueda")) return QStringLiteral("search");
    if (group == QLatin1String("codigo")) return QStringLiteral("code");
    if (group == QLatin1String("web")) return QStringLiteral("web");
    if (group == QLatin1String("conocimiento")) return QStringLiteral("knowledge");
    if (group == QLatin1String("browser")) return QStringLiteral("browser");
    if (group == QLatin1String("escritorio")) return QStringLiteral("desktop");
    if (group == QLatin1String("correo")) return QStringLiteral("mail");
    if (group == QLatin1String("multi agente")) return QStringLiteral("multiagent");
    if (group == QLatin1String("habilidades")) return QStringLiteral("skills");
    if (group == QLatin1String("plugins")) return QStringLiteral("plugins");
    if (group == QLatin1String("coordinacion")) return QStringLiteral("coordination");
    return {};
}

ToolSurfaceRouter::Decision ToolSurfaceRouter::decide(const QString &prompt,
                                                       const QVariantList &catalog)
{
    Decision out;
    const QString text = normalized(prompt);
    if (text.isEmpty()) {
        out.reason = QStringLiteral("prompt vacío");
        return out;
    }

    QSet<QString> groups{
        QStringLiteral("files"), QStringLiteral("search"),
        QStringLiteral("code"), QStringLiteral("knowledge"),
        QStringLiteral("coordination")};
    int signalCount = 0;
    auto add = [&groups, &signalCount](const QString &group) {
        if (!groups.contains(group)) ++signalCount;
        groups.insert(group);
    };

    if (containsAny(text, {QStringLiteral("web"), QStringLiteral("internet"),
                           QStringLiteral("url"), QStringLiteral("noticia"),
                           QStringLiteral("investig"), QStringLiteral("online"),
                           QStringLiteral("fuente externa"), QStringLiteral("buscar en la web")}))
        add(QStringLiteral("web"));
    if (containsAny(text, {QStringLiteral("navegador"), QStringLiteral("browser"),
                           QStringLiteral("sitio"), QStringLiteral("pagina"),
                           QStringLiteral("playwright"), QStringLiteral("dom")})) {
        add(QStringLiteral("web"));
        add(QStringLiteral("browser"));
    }
    if (containsAny(text, {QStringLiteral("escritorio"), QStringLiteral("ventana"),
                           QStringLiteral("click"), QStringLiteral("teclado"),
                           QStringLiteral("pantalla"), QStringLiteral("captura"),
                           QStringLiteral("ui"), QStringLiteral("aplicacion")}))
        add(QStringLiteral("desktop"));
    if (containsAny(text, {QStringLiteral("correo"), QStringLiteral("email"),
                           QStringLiteral("mail"), QStringLiteral("bandeja"),
                           QStringLiteral("inbox")}))
        add(QStringLiteral("mail"));
    if (containsAny(text, {QStringLiteral("subagente"), QStringLiteral("deleg"),
                           QStringLiteral("maestro"), QStringLiteral("otro modelo")}))
        add(QStringLiteral("multiagent"));
    if (containsAny(text, {QStringLiteral("mcp"), QStringLiteral("plugin"),
                           QStringLiteral("sidecar"), QStringLiteral("worker")}))
        add(QStringLiteral("plugins"));
    if (containsAny(text, {QStringLiteral("skill"), QStringLiteral("habilidad"),
                           QStringLiteral("procedimiento reutilizable")}))
        add(QStringLiteral("skills"));

    if (signalCount == 0 && !containsAny(text, {QStringLiteral("archivo"),
                                            QStringLiteral("repo"), QStringLiteral("proyecto"),
                                            QStringLiteral("codigo"), QStringLiteral("test"),
                                            QStringLiteral("memoria"), QStringLiteral("recorda"),
                                            QStringLiteral("recuerda")})) {
        out.reason = QStringLiteral("intención ambigua: superficie completa");
        return out;
    }

    QSet<QString> catalogGroups;
    for (const QVariant &entry : catalog)
        catalogGroups.insert(canonicalGroup(entry.toMap().value(QStringLiteral("group"))
                                  .toString()));
    const QStringList order{QStringLiteral("files"), QStringLiteral("search"),
                            QStringLiteral("code"), QStringLiteral("knowledge"),
                            QStringLiteral("coordination"), QStringLiteral("web"),
                            QStringLiteral("browser"), QStringLiteral("desktop"),
                            QStringLiteral("mail"), QStringLiteral("multiagent"),
                            QStringLiteral("skills"), QStringLiteral("plugins")};
    for (const QString &group : order)
        if (groups.contains(group) && catalogGroups.contains(group)) out.groups << group;
    if (out.groups.isEmpty()) {
        out.reason = QStringLiteral("sin grupos compatibles: superficie completa");
        return out;
    }
    out.confidence = qBound(0.35, 0.45 + 0.12 * signalCount, 0.92);
    out.fullSurface = false;
    out.reason = QStringLiteral("señales=%1").arg(signalCount);
    return out;
}

bool ToolSurfaceRouter::groupAllowed(const QString &group,
                                     const Decision &decision)
{
    return decision.fullSurface || decision.groups.contains(canonicalGroup(group));
}
