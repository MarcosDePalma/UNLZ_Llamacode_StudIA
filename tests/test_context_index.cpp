#include <QtTest>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/agent/ContextIndex.h"
#include "core/agent/ProjectBrain.h"

class ContextIndexTests : public QObject
{
    Q_OBJECT
private slots:
    void refreshPersistsFilesChunksAndEdges();
    void scoutReturnsBudgetReceiptAndHandle();
    void fetchRejectsStaleHandle();
    void projectBrainDiffDetectsIndirectMutation();
};

static void writeText(const QString &root, const QString &rel, const QString &text)
{
    const QString path = QDir(root).absoluteFilePath(rel);
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Text), qPrintable(f.fileName()));
    QVERIFY(f.write(text.toUtf8()) == text.toUtf8().size());
}

void ContextIndexTests::refreshPersistsFilesChunksAndEdges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeText(dir.path(), QStringLiteral("util.h"), QStringLiteral("int helper();\n"));
    writeText(dir.path(), QStringLiteral("main.cpp"),
              QStringLiteral("#include \"util.h\"\nint main(){return helper();}\n"));
    writeText(dir.path(), QStringLiteral("src/widget.cpp"),
              QStringLiteral("int widget() { return 1; }\n"));
    writeText(dir.path(), QStringLiteral("tests/test_widget.cpp"),
              QStringLiteral("#include \"widget.cpp\"\n"));
    writeText(dir.path(), QStringLiteral("CMakeLists.txt"),
              QStringLiteral("add_library(widget src/widget.cpp)\n"));

    const QVariantMap state = ContextIndex::refresh(dir.path());
    QVERIFY(state.value(QStringLiteral("ok")).toBool());
    QVERIFY(state.value(QStringLiteral("files")).toInt() >= 2);
    QVERIFY(state.value(QStringLiteral("chunks")).toInt() >= 2);
    QVERIFY(state.value(QStringLiteral("edges")).toInt() >= 3);

    const QVariantMap second = ContextIndex::refresh(dir.path());
    QCOMPARE(second.value(QStringLiteral("reused")).toInt(),
             state.value(QStringLiteral("files")).toInt());
}

void ContextIndexTests::scoutReturnsBudgetReceiptAndHandle()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeText(dir.path(), QStringLiteral("auth.cpp"),
              QStringLiteral("// AUTH_CONTEXT_MARKER\nvoid authenticate_user() {}\n"));

    const QVariantMap result = ContextIndex::scout(
        dir.path(), QStringLiteral("authentication AUTH_CONTEXT_MARKER"), 200, 4, true);
    QVERIFY(result.value(QStringLiteral("ok")).toBool());
    const QVariantMap receipt = result.value(QStringLiteral("receipt")).toMap();
    QVERIFY(!receipt.value(QStringLiteral("returned")).toList().isEmpty());
    QVERIFY(receipt.contains(QStringLiteral("usedTokensEst")));
    const QVariantMap hit = receipt.value(QStringLiteral("returned")).toList().first().toMap();
    QVERIFY(hit.value(QStringLiteral("handle")).toString().startsWith(QStringLiteral("ctx:")));
    const QString formatted = ContextIndex::formatScout(result);
    QVERIFY(formatted.contains(QStringLiteral("context-receipt")));
    QVERIFY(QJsonDocument::fromJson(
                QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Compact)).isObject());
}

void ContextIndexTests::fetchRejectsStaleHandle()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeText(dir.path(), QStringLiteral("file.cpp"), QStringLiteral("// OLD_MARKER\nint value=1;\n"));
    const QVariantMap result = ContextIndex::scout(dir.path(), QStringLiteral("OLD_MARKER"), 300);
    const QString handle = result.value(QStringLiteral("receipt")).toMap()
                               .value(QStringLiteral("returned")).toList().first().toMap()
                               .value(QStringLiteral("handle")).toString();
    QVERIFY(!handle.isEmpty());
    writeText(dir.path(), QStringLiteral("file.cpp"), QStringLiteral("// NEW_MARKER\nint value=2;\n"));
    const QString fetched = ContextIndex::fetch(dir.path(), handle);
    QVERIFY2(fetched.contains(QStringLiteral("handle obsoleto")),
             qPrintable(fetched));
}

void ContextIndexTests::projectBrainDiffDetectsIndirectMutation()
{
    const QVariantMap before{{QStringLiteral("files"), QVariantList{
        QVariantMap{{QStringLiteral("path"), QStringLiteral("same.cpp")},
                    {QStringLiteral("sha256"), QStringLiteral("old")}},
        QVariantMap{{QStringLiteral("path"), QStringLiteral("removed.cpp")},
                    {QStringLiteral("sha256"), QStringLiteral("gone")}}}}};
    const QVariantMap after{{QStringLiteral("files"), QVariantList{
        QVariantMap{{QStringLiteral("path"), QStringLiteral("same.cpp")},
                    {QStringLiteral("sha256"), QStringLiteral("new")}},
        QVariantMap{{QStringLiteral("path"), QStringLiteral("added.cpp")},
                    {QStringLiteral("sha256"), QStringLiteral("fresh")}}}}};

    const QStringList changed = ProjectBrain::changedPaths(before, after);
    QCOMPARE(changed.size(), 3);
    QVERIFY(changed.contains(QStringLiteral("same.cpp")));
    QVERIFY(changed.contains(QStringLiteral("removed.cpp")));
    QVERIFY(changed.contains(QStringLiteral("added.cpp")));
    QVERIFY(ProjectBrain::changedPaths(after, after).isEmpty());
}

QTEST_MAIN(ContextIndexTests)
#include "test_context_index.moc"
