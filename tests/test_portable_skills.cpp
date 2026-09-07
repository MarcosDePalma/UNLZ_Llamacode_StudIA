#include <QtTest>

#include "core/agent/PortableSkillStore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>

class PortableSkillsTests : public QObject
{
    Q_OBJECT
private slots:
    void discoversAndLoadsProjectSkill();
    void harnessPolicyFiltersAndBlocksSkills();
    void rejectsInvalidManifest();
    void bundledScientificPackIsValid();
};

static void writeSkill(const QString &workspace, const QString &slug, const QString &text)
{
    const QString dir = QDir(workspace).filePath(QStringLiteral(".llamacode/skills/") + slug);
    QVERIFY(QDir().mkpath(dir));
    QFile file(QDir(dir).filePath(QStringLiteral("SKILL.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(text.toUtf8()), text.toUtf8().size());
}

void PortableSkillsTests::discoversAndLoadsProjectSkill()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    writeSkill(temp.path(), QStringLiteral("review-paper"),
               QStringLiteral("---\n"
                              "name: review-paper\n"
                              "description: Revisa evidencia científica con trazabilidad.\n"
                              "version: 1.0.0\n"
                              "---\n"
                              "# Procedimiento\n\nVerificá cada afirmación contra la fuente.\n"));

    const QVariantList listed = PortableSkillStore::list(temp.path());
    auto it = std::find_if(listed.cbegin(), listed.cend(), [](const QVariant &v) {
        return v.toMap().value(QStringLiteral("name")).toString()
            == QLatin1String("review-paper");
    });
    QVERIFY(it != listed.cend());
    const QVariantMap metadata = it->toMap();
    QCOMPARE(metadata.value(QStringLiteral("scope")).toString(), QStringLiteral("project"));
    QVERIFY(!metadata.contains(QStringLiteral("instructions"))); // carga progresiva

    const QVariantMap loaded = PortableSkillStore::load(QStringLiteral("review-paper"), temp.path());
    QVERIFY(loaded.value(QStringLiteral("ok")).toBool());
    QVERIFY(loaded.value(QStringLiteral("instructions")).toString().contains(
        QStringLiteral("Verificá cada afirmación")));
}

void PortableSkillsTests::rejectsInvalidManifest()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    writeSkill(temp.path(), QStringLiteral("safe-folder"),
               QStringLiteral("---\n"
                              "name: otro-nombre\n"
                              "description: No debe aceptarse.\n"
                              "---\n"
                              "Instrucciones\n"));

    const QVariantMap loaded = PortableSkillStore::load(QStringLiteral("safe-folder"), temp.path());
    QVERIFY(!loaded.value(QStringLiteral("ok")).toBool());
    QVERIFY(loaded.value(QStringLiteral("error")).toString().contains(
        QStringLiteral("no encontrada"), Qt::CaseInsensitive));
}

void PortableSkillsTests::harnessPolicyFiltersAndBlocksSkills()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    writeSkill(temp.path(), QStringLiteral("review-paper"),
               QStringLiteral("---\n"
                              "name: review-paper\n"
                              "description: Test\n"
                              "---\n"
                              "Contenido\n"));
    writeSkill(temp.path(), QStringLiteral("other-skill"),
               QStringLiteral("---\n"
                              "name: other-skill\n"
                              "description: Test\n"
                              "---\n"
                              "Contenido\n"));

    HarnessSkillsModule policy;
    policy.set = true;
    policy.include = {QStringLiteral("review-paper"), QStringLiteral("other-skill")};
    policy.exclude = {QStringLiteral("review-paper")};
    const QVariantList listed = PortableSkillStore::list(temp.path(), policy);
    QCOMPARE(listed.size(), 1);
    QCOMPARE(listed.first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("other-skill"));
    const QVariantMap blocked = PortableSkillStore::load(QStringLiteral("review-paper"),
                                                          temp.path(), policy);
    QVERIFY(!blocked.value(QStringLiteral("ok")).toBool());
    QVERIFY(blocked.value(QStringLiteral("error")).toString().contains(
        QStringLiteral("desactivada"), Qt::CaseInsensitive));

    policy.include = {QStringLiteral("review-paper")};
    policy.exclude.clear();
    QVERIFY(PortableSkillStore::load(QStringLiteral("review-paper"), temp.path(), policy)
                .value(QStringLiteral("ok")).toBool());
}

void PortableSkillsTests::bundledScientificPackIsValid()
{
    const QVariantList skills = PortableSkillStore::list();
    const QStringList expected{
        QStringLiteral("autoprompt-coding"),
        QStringLiteral("literature-review"),
        QStringLiteral("critical-paper-reading"),
        QStringLiteral("experimental-design"),
        QStringLiteral("citation-verification"),
        QStringLiteral("peer-review"),
        QStringLiteral("reproducible-data-analysis")
    };
    for (const QString &name : expected) {
        auto it = std::find_if(skills.cbegin(), skills.cend(), [&name](const QVariant &v) {
            return v.toMap().value(QStringLiteral("name")).toString() == name;
        });
        QVERIFY2(it != skills.cend(), qPrintable(QStringLiteral("Falta skill bundled: ") + name));
        QCOMPARE(it->toMap().value(QStringLiteral("scope")).toString(),
                 QStringLiteral("bundled"));
        QVERIFY(PortableSkillStore::load(name).value(QStringLiteral("ok")).toBool());
    }

    const QVariantMap autoprompt = PortableSkillStore::load(QStringLiteral("autoprompt-coding"));
    QVERIFY(autoprompt.value(QStringLiteral("instructions")).toString().contains(
        QStringLiteral("LC_GATE: PASS")));
}

QTEST_GUILESS_MAIN(PortableSkillsTests)
#include "test_portable_skills.moc"
