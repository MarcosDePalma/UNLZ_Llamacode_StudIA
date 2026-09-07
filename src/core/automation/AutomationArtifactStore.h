#pragma once

#include <QtGlobal>
#include <QVariantList>
#include <QVariantMap>

class AutomationArtifactStore
{
public:
    // v3 agrega pre/postcondiciones, locators semánticos y metadatos de
    // recuperación. Los artefactos v2 siguen siendo legibles y reproducibles.
    static constexpr int FormatVersion = 3;
    static QString rootDir();
    // Capturas del Inspector: límite conservador por cantidad y tamaño para
    // evitar que una corrida larga consuma indefinidamente AppLocalData.
    static bool cleanupRuntimeObservations(int maxFiles = 120,
                                           qint64 maxBytes = 96 * 1024 * 1024);
    static bool clearRuntimeObservations();
    static QString artifactDir(const QString &id);
    static QString create(const QVariantMap &task, const QVariantMap &scope,
                          const QVariantList &events, const QStringList &evidence,
                          const QString &browserScript = QString());
    static QVariantMap manifest(const QString &id);
    static QVariantMap recipe(const QString &id);
    static QVariantList timeline(const QString &id);
    static QVariantList templates(const QString &id);
    static bool removeTemplate(const QString &id, const QString &fileName);
    static bool replaceTemplate(const QString &id, const QString &fileName,
                                const QString &sourcePath);
    static bool addTemplateVariant(const QString &id, const QString &fileName,
                                   const QString &sourcePath);
    static QString importBrowserSkill(const QString &skillName, const QVariantMap &task);
    static bool appendLearning(const QString &id, const QString &summary, const QString &log);
    static bool appendNetworkDiscovery(const QString &id, const QVariantMap &evidence,
                                       const QString &action = QString());
    static QVariantList networkDiscoveries(const QString &id);
    static bool setNetworkDiscoveryReview(const QString &id, const QString &signature,
                                          const QString &status);
    static bool clearNetworkDiscoveries(const QString &id);
    static bool removeEvidence(const QString &id, const QString &fileName);
    static QString redact(const QString &text);
};
