// Unit tests del indexador determinista repo→grafo:
//   - GraphStore::addBatch: inserción masiva + dedupe (entidades y relaciones).
//   - CodeGraphIndexer::build: extrae símbolos (clases/funciones) e imports/
//     includes de un repo fixture y los vuelca como file-[defines]->símbolo y
//     file-[imports]->file; idempotente; respeta el filtro de lenguajes.
// Cada test usa un cwd temporal aislado.

#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include "core/agent/GraphStore.h"
#include "core/agent/CodeGraphIndexer.h"

class CodeGraphTests : public QObject
{
    Q_OBJECT
private slots:
    void addBatch_insertsAndDedupes();
    void index_extractsSymbols();
    void index_extractsImports();
    void index_edgesCarrySourceCitations();
    void index_idempotent();
    void index_langFilter();
    void graphStore_removeRelationsBySubject();
    void graphStore_entityNames();
    void reindex_dropsStaleEdges();
    void incremental_freshFallsBackToFull();
    void incremental_mtimeReindexesChanged();
    void incremental_gitSubdirUsesMtime();

private:
    // Escribe un archivo bajo dir.path() (creando subcarpetas) con 'content'.
    static void writeFile(const QString &root, const QString &rel, const QString &content)
    {
        const QString abs = QDir(root).absoluteFilePath(rel);
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile f(abs);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(content.toUtf8());
        f.close();
    }
};

void CodeGraphTests::addBatch_insertsAndDedupes()
{
    QTemporaryDir dir;
    QVector<QPair<QString, QString>> ents{{"Foo", "file"}, {"Bar", "concept"}};
    QVector<GraphStore::Triple> rels{{"Foo", "defines", "Bar"}};
    int e1 = 0, r1 = 0;
    GraphStore::addBatch(dir.path(), ents, rels, &e1, &r1);
    QCOMPARE(e1, 2);
    QCOMPARE(r1, 1);

    // Segundo batch idéntico: nada nuevo (dedupe contra lo ya escrito).
    int e2 = 0, r2 = 0;
    GraphStore::addBatch(dir.path(), ents, rels, &e2, &r2);
    QCOMPARE(e2, 0);
    QCOMPARE(r2, 0);

    const QString out = GraphStore::query(dir.path(), "Foo", 1);
    QVERIFY(out.contains("Bar"));
    QVERIFY(out.contains("DEFINES"));                    // edge tipado (pred 'defines' → DEFINES)
    QVERIFY(out.contains("conf=1") && out.contains("indexer"));  // indexador determinista: verificado
}

void CodeGraphTests::index_extractsSymbols()
{
    QTemporaryDir dir;
    writeFile(dir.path(), "src/widget.cpp",
              "#include \"widget.h\"\n"
              "class Widget {\n};\n"
              "void doStuff(int a) {\n  return;\n}\n"
              "int main() {\n  return 0;\n}\n");

    QString report;
    const CodeGraphIndexer::Stats st = CodeGraphIndexer::build(dir.path(), {}, &report);
    QCOMPARE(st.files, 1);
    QVERIFY(st.symbols >= 2);   // Widget, doStuff, main (al menos 2)

    const QString out = GraphStore::query(dir.path(), "src/widget.cpp", 1);
    QVERIFY(out.contains("Widget"));
    QVERIFY(out.contains("doStuff"));
    QVERIFY(out.contains("DEFINES"));
    QVERIFY(report.contains("code_graph"));
}

void CodeGraphTests::index_extractsImports()
{
    QTemporaryDir dir;
    // Nombres de >=2 chars: extractImportRefs descarta basenames de 1 char.
    writeFile(dir.path(), "src/alpha.cpp", "#include \"beta.h\"\nvoid fa() {}\n");
    writeFile(dir.path(), "src/beta.h", "class Beta {};\n");

    CodeGraphIndexer::build(dir.path(), {}, nullptr);

    // alpha.cpp importa beta (resuelto por basename → src/beta.h).
    const QString out = GraphStore::query(dir.path(), "src/alpha.cpp", 1);
    QVERIFY(out.contains("IMPORTS"));
    QVERIFY(out.contains("beta.h"));
}

void CodeGraphTests::index_edgesCarrySourceCitations()
{
    QTemporaryDir dir;
    writeFile(dir.path(), "src/widget.cpp",
              "#include \"widget.h\"\n"
              "class Widget {}\n"
              "void render() {}\n");
    writeFile(dir.path(), "src/widget.h", "class Widget;\n");

    CodeGraphIndexer::build(dir.path(), {}, nullptr);
    const QJsonObject packet = GraphStore::queryPacket(
        dir.path(), QStringLiteral("src/widget.cpp"), 1);
    QVERIFY(packet.value(QStringLiteral("ok")).toBool());
    const QJsonArray edges = packet.value(QStringLiteral("edges")).toArray();
    QVERIFY(!edges.isEmpty());
    bool sawLine = false;
    for (const QJsonValue &v : edges) {
        const QJsonObject edge = v.toObject();
        if (edge.value(QStringLiteral("etype")).toString() == QLatin1String("DEFINES")
            && !edge.value(QStringLiteral("citations")).toArray().isEmpty()) {
            sawLine = sawLine || edge.value(QStringLiteral("citations")).toArray().first()
                                      .toString().contains(QStringLiteral("src/widget.cpp:2"));
        }
    }
    QVERIFY(sawLine);
    const QJsonObject report = GraphStore::doctor(dir.path());
    QVERIFY(report.value(QStringLiteral("sourceRefs")).toInt() >= 2);
    QVERIFY(report.value(QStringLiteral("staleSources")).toInt() == 0);
}

void CodeGraphTests::index_idempotent()
{
    QTemporaryDir dir;
    writeFile(dir.path(), "m.py", "def foo():\n    pass\nclass Bar:\n    pass\n");

    const CodeGraphIndexer::Stats s1 = CodeGraphIndexer::build(dir.path(), {}, nullptr);
    QVERIFY(s1.edges > 0);
    // Segunda corrida: mismo contenido → 0 relaciones nuevas (dedupe).
    const CodeGraphIndexer::Stats s2 = CodeGraphIndexer::build(dir.path(), {}, nullptr);
    QCOMPARE(s2.edges, 0);
}

void CodeGraphTests::index_langFilter()
{
    QTemporaryDir dir;
    writeFile(dir.path(), "keep.cpp", "void cppFn() {}\n");
    writeFile(dir.path(), "skip.py", "def pyFn():\n    pass\n");

    // Sólo cpp: el símbolo de python NO debe entrar al grafo.
    const CodeGraphIndexer::Stats st = CodeGraphIndexer::build(
        dir.path(), QStringList{QStringLiteral("cpp")}, nullptr);
    QCOMPARE(st.files, 1);

    QFile f(GraphStore::jsonlPath(dir.path()));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(f.readAll());
    QVERIFY(content.contains("cppFn"));
    QVERIFY(!content.contains("pyFn"));
}

void CodeGraphTests::graphStore_removeRelationsBySubject()
{
    QTemporaryDir dir;
    GraphStore::link(dir.path(), "fileA", "defines", "symX");
    GraphStore::link(dir.path(), "fileA", "imports", "fileB");
    GraphStore::link(dir.path(), "fileB", "defines", "symY");

    const int removed = GraphStore::removeRelationsBySubject(dir.path(), "fileA");
    QCOMPARE(removed, 2);   // las dos de fileA

    const QString a = GraphStore::query(dir.path(), "fileA", 1);
    QVERIFY(a.contains("sin relaciones"));   // fileA quedó sin edges
    const QString b = GraphStore::query(dir.path(), "fileB", 1);
    QVERIFY(b.contains("symY"));             // fileB intacto
}

void CodeGraphTests::graphStore_entityNames()
{
    QTemporaryDir dir;
    GraphStore::addEntity(dir.path(), "src/foo.cpp", "file");
    GraphStore::addEntity(dir.path(), "Foo", "concept");
    const QStringList files = GraphStore::entityNames(dir.path(), "file");
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.first(), QString("src/foo.cpp"));
}

void CodeGraphTests::reindex_dropsStaleEdges()
{
    QTemporaryDir dir;
    writeFile(dir.path(), "mod.cpp", "void alphaFn() {}\n");
    CodeGraphIndexer::build(dir.path(), {}, nullptr);
    QVERIFY(GraphStore::query(dir.path(), "mod.cpp", 1).contains("alphaFn"));

    // Reescribir: alphaFn desaparece, aparece betaFn.
    writeFile(dir.path(), "mod.cpp", "void betaFn() {}\n");
    CodeGraphIndexer::reindexFiles(dir.path(), QStringList{QStringLiteral("mod.cpp")},
                                   {}, nullptr);
    const QString out = GraphStore::query(dir.path(), "mod.cpp", 1);
    QVERIFY(out.contains("betaFn"));
    QVERIFY(!out.contains("alphaFn"));   // edge viejo borrado
}

void CodeGraphTests::incremental_freshFallsBackToFull()
{
    QTemporaryDir dir;
    writeFile(dir.path(), "a.cpp", "void aFn() {}\n");
    writeFile(dir.path(), "b.cpp", "void bFn() {}\n");
    // Sin estado previo → build completo.
    const CodeGraphIndexer::Stats st = CodeGraphIndexer::buildIncremental(dir.path(), {}, nullptr);
    QCOMPARE(st.files, 2);
    QVERIFY(GraphStore::query(dir.path(), "a.cpp", 1).contains("aFn"));
}

void CodeGraphTests::incremental_mtimeReindexesChanged()
{
    QTemporaryDir dir;   // no es repo git → camino mtime
    writeFile(dir.path(), "x.cpp", "void oldFn() {}\n");
    writeFile(dir.path(), "y.cpp", "void yFn() {}\n");
    CodeGraphIndexer::build(dir.path(), {}, nullptr);   // deja estado (ts=ahora)

    // Cruzar el límite de segundo del marcador y modificar sólo x.cpp.
    QTest::qWait(1100);
    writeFile(dir.path(), "x.cpp", "void newFn() {}\n");

    QString report;
    CodeGraphIndexer::buildIncremental(dir.path(), {}, &report);
    QVERIFY(report.contains("incremental"));
    const QString out = GraphStore::query(dir.path(), "x.cpp", 1);
    QVERIFY(out.contains("newFn"));
    QVERIFY(!out.contains("oldFn"));   // re-extraído, edge viejo borrado
}

void CodeGraphTests::incremental_gitSubdirUsesMtime()
{
    // Repro del bug: rootCwd es un SUBDIR de un repo git más grande. `git
    // diff/status` devuelven rutas relativas al toplevel del repo (no a
    // rootCwd), que no matchean el grafo (indexado relativo a rootCwd). El
    // indexador debe caer al camino mtime y reindexar igual.
    QTemporaryDir repo;
    {
        QProcess p;
        p.setWorkingDirectory(repo.path());
        p.start(QStringLiteral("git"), {QStringLiteral("init")});
        if (!p.waitForStarted(3000) || !p.waitForFinished(8000)
            || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
            QSKIP("git no disponible");
    }
    const QString sub = QDir(repo.path()).absoluteFilePath(QStringLiteral("proj"));
    writeFile(sub, "x.cpp", "void oldFn() {}\n");
    CodeGraphIndexer::build(sub, {}, nullptr);

    QTest::qWait(1100);
    writeFile(sub, "x.cpp", "void newFn() {}\n");

    QString report;
    CodeGraphIndexer::buildIncremental(sub, {}, &report);
    QVERIFY2(report.contains("mtime"), qPrintable(report));   // no camino git
    const QString out = GraphStore::query(sub, "x.cpp", 1);
    QVERIFY(out.contains("newFn"));
    QVERIFY(!out.contains("oldFn"));
}

QTEST_MAIN(CodeGraphTests)
#include "test_code_graph.moc"
