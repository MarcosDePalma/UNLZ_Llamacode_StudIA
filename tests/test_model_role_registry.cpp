#include <QtTest>
#include <QFile>
#include <QStandardPaths>

#include "core/ModelRoleRegistry.h"

class ModelRoleRegistryTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void exposesSafeDefaults();
    void resolvesPrimaryAndFallback();
    void persistsAllowedOverrides();
};

void ModelRoleRegistryTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(ModelRoleRegistry::storagePath());
}

void ModelRoleRegistryTests::cleanupTestCase()
{
    QFile::remove(ModelRoleRegistry::storagePath());
}

void ModelRoleRegistryTests::exposesSafeDefaults()
{
    ModelRoleRegistry registry;
    const QVariantMap stt = registry.role(QStringLiteral("stt"));
    QVERIFY(!stt.isEmpty());
    QCOMPARE(stt.value(QStringLiteral("jobClass")).toString(), QStringLiteral("voice"));
    QVERIFY(stt.value(QStringLiteral("resourceKey")).toString() == QStringLiteral("stt"));
    QVERIFY(registry.role(QStringLiteral("unknown-app-widget")).isEmpty());
}

void ModelRoleRegistryTests::resolvesPrimaryAndFallback()
{
    ModelRoleRegistry registry;
    QVERIFY(registry.setRole(QStringLiteral("fast_agent"),
                             {{QStringLiteral("model"), QStringLiteral("fast.gguf")},
                              {QStringLiteral("fallbackModel"), QStringLiteral("fallback.gguf")}}));
    QCOMPARE(registry.resolveModel(QStringLiteral("fast_agent")), QStringLiteral("fast.gguf"));
    QVERIFY(registry.setRole(QStringLiteral("fast_agent"),
                             {{QStringLiteral("model"), QString()},
                              {QStringLiteral("fallbackModel"), QStringLiteral("fallback.gguf")}}));
    QCOMPARE(registry.resolveModel(QStringLiteral("fast_agent")), QStringLiteral("fallback.gguf"));
    QVERIFY(registry.setRole(QStringLiteral("fast_agent"),
                             {{QStringLiteral("enabled"), false}}));
    QVERIFY(registry.schedulingHint(QStringLiteral("fast_agent")).isEmpty());
    QVERIFY(registry.setRole(QStringLiteral("fast_agent"),
                             {{QStringLiteral("enabled"), true}}));
}

void ModelRoleRegistryTests::persistsAllowedOverrides()
{
    ModelRoleRegistry first;
    QVERIFY(first.setRole(QStringLiteral("verifier"),
                          {{QStringLiteral("priority"), 77},
                           {QStringLiteral("maxConcurrency"), 2}}));
    ModelRoleRegistry second;
    QCOMPARE(second.role(QStringLiteral("verifier")).value(QStringLiteral("priority")).toInt(), 77);
    QCOMPARE(second.role(QStringLiteral("verifier")).value(QStringLiteral("maxConcurrency")).toInt(), 2);
    QVERIFY(first.resetRole(QStringLiteral("verifier")));
}

QTEST_MAIN(ModelRoleRegistryTests)
#include "test_model_role_registry.moc"
