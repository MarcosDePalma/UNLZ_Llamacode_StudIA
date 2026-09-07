// Unit tests de MermaidRenderer (sin sidecar mmdc):
//   - segments(): split de texto / bloques mermaid; fences sin cerrar = texto.
//   - sourceHash: estable y distinto por contenido.
//   - cachedPath: "" cuando no se rindió.
//   - requestRender: emite renderFailed si mmdc no está (entorno de test).
// El render real (mmdc → PNG) necesita mermaid-cli instalado → no se cubre acá.

#include <QtTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "core/MermaidRenderer.h"

class MermaidTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void segments_plainTextOnly();
    void segments_singleBlock();
    void segments_textBeforeAndAfter();
    void segments_unclosedFenceStaysText();
    void segments_caseInsensitiveLang();
    void segments_svgBlock();
    void segments_mixedMermaidAndSvg();
    void hash_stableAndDistinct();
    void cachedPath_emptyWhenNotRendered();
    void requestRender_emitsResult();
    void renderSvg_validProducesPng();
    void renderSvg_invalidReturnsEmpty();
    void exportSource_writesVerbatim();
    void exportPng_svgProducesFile();
    void exportPng_uncachedReturnsFalse();
    void export_emptyDestReturnsFalse();
};

static QString segType(const QVariantList &l, int i)
{ return l.at(i).toMap().value("type").toString(); }
static QString segText(const QVariantList &l, int i)
{ return l.at(i).toMap().value("text").toString(); }

void MermaidTests::segments_plainTextOnly()
{
    MermaidRenderer m;
    const QVariantList s = m.segments("hola mundo, sin diagramas");
    QCOMPARE(s.size(), 1);
    QCOMPARE(segType(s, 0), QString("text"));
}

void MermaidTests::segments_singleBlock()
{
    MermaidRenderer m;
    const QVariantList s = m.segments("```mermaid\ngraph TD;A-->B;\n```");
    QCOMPARE(s.size(), 1);
    QCOMPARE(segType(s, 0), QString("mermaid"));
    QCOMPARE(segText(s, 0), QString("graph TD;A-->B;"));
}

void MermaidTests::segments_textBeforeAndAfter()
{
    MermaidRenderer m;
    const QString in = "antes\n```mermaid\ngraph TD;A-->B;\n```\ndespues";
    const QVariantList s = m.segments(in);
    QCOMPARE(s.size(), 3);
    QCOMPARE(segType(s, 0), QString("text"));
    QCOMPARE(segType(s, 1), QString("mermaid"));
    QCOMPARE(segType(s, 2), QString("text"));
    QVERIFY(segText(s, 0).contains("antes"));
    QVERIFY(segText(s, 2).contains("despues"));
}

void MermaidTests::segments_unclosedFenceStaysText()
{
    MermaidRenderer m;
    // Streaming: bloque sin cerrar → no se rinde, queda como texto.
    const QVariantList s = m.segments("texto\n```mermaid\ngraph TD;A-->B;");
    QCOMPARE(s.size(), 1);
    QCOMPARE(segType(s, 0), QString("text"));
}

void MermaidTests::segments_caseInsensitiveLang()
{
    MermaidRenderer m;
    const QVariantList s = m.segments("```Mermaid\nsequenceDiagram\n```");
    QCOMPARE(s.size(), 1);
    QCOMPARE(segType(s, 0), QString("mermaid"));
}

void MermaidTests::segments_svgBlock()
{
    MermaidRenderer m;
    const QVariantList s = m.segments("```svg\n<svg/>\n```");
    QCOMPARE(s.size(), 1);
    QCOMPARE(segType(s, 0), QString("svg"));
    QCOMPARE(segText(s, 0), QString("<svg/>"));
}

void MermaidTests::segments_mixedMermaidAndSvg()
{
    MermaidRenderer m;
    const QString in = "```mermaid\ngraph TD;A-->B;\n```\nmedio\n```svg\n<svg/>\n```";
    const QVariantList s = m.segments(in);
    QCOMPARE(s.size(), 3);
    QCOMPARE(segType(s, 0), QString("mermaid"));
    QCOMPARE(segType(s, 1), QString("text"));
    QCOMPARE(segType(s, 2), QString("svg"));
}

void MermaidTests::hash_stableAndDistinct()
{
    MermaidRenderer m;
    const QString a1 = m.sourceHash("graph TD;A-->B;");
    const QString a2 = m.sourceHash("graph TD;A-->B;");
    const QString b  = m.sourceHash("graph TD;A-->C;");
    QCOMPARE(a1, a2);
    QVERIFY(a1 != b);
    QVERIFY(!a1.isEmpty());
}

void MermaidTests::cachedPath_emptyWhenNotRendered()
{
    MermaidRenderer m;
    QVERIFY(m.cachedPath("diagrama inexistente xyz").isEmpty());
}

void MermaidTests::requestRender_emitsResult()
{
    // requestRender siempre termina en renderReady o renderFailed.
    //  - Sin mmdc: renderFailed sincrónico.
    //  - Con mmdc: render async (PNG) → esperar el spy.
    MermaidRenderer m;
    QSignalSpy failed(&m, &MermaidRenderer::renderFailed);
    QSignalSpy ready(&m, &MermaidRenderer::renderReady);
    m.requestRender("graph TD;A-->B; %% test render");
    if (failed.count() + ready.count() == 0) {
        // async (sidecar presente): esperar hasta 60s a que mmdc resuelva.
        QTRY_VERIFY_WITH_TIMEOUT(failed.count() + ready.count() >= 1, 60000);
    }
    QVERIFY(failed.count() + ready.count() >= 1);
}

void MermaidTests::renderSvg_validProducesPng()
{
    // SVG válido → rasteriza a PNG (sincrónico, sin sidecar). Idempotente.
    MermaidRenderer m;
    const QString svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='40' height='30'>"
        "<rect width='40' height='30' fill='red'/></svg>";
    const QString p = m.renderSvg(svg);
    QVERIFY(!p.isEmpty());
    QVERIFY(QFile::exists(p));
    QVERIFY(p.endsWith(".svg.png"));
    QCOMPARE(m.renderSvg(svg), p);  // cache hit
}

void MermaidTests::renderSvg_invalidReturnsEmpty()
{
    MermaidRenderer m;
    QVERIFY(m.renderSvg("no es svg en absoluto <<<").isEmpty());
}

void MermaidTests::exportSource_writesVerbatim()
{
    // exportSource escribe el source crudo byte a byte.
    MermaidRenderer m;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dest = dir.filePath("out.mmd");
    const QString src = "graph TD;A-->B;\nstyle A fill:#f00";
    QVERIFY(m.exportSource(src, dest));
    QFile f(dest);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(f.readAll()), src);
}

void MermaidTests::exportPng_svgProducesFile()
{
    // svg: exportPng rasteriza (si hace falta) y copia el PNG al destino.
    MermaidRenderer m;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dest = dir.filePath("out.png");
    const QString svg =
        "<svg xmlns='http://www.w3.org/2000/svg' width='40' height='30'>"
        "<rect width='40' height='30' fill='blue'/></svg>";
    QVERIFY(m.exportPng(svg, dest));
    QVERIFY(QFile::exists(dest));
    QVERIFY(QFileInfo(dest).size() > 0);
    // Idempotente: pisa un destino existente sin fallar.
    QVERIFY(m.exportPng(svg, dest));
}

void MermaidTests::exportPng_uncachedReturnsFalse()
{
    // mermaid sin render previo (y sin cache) → no hay PNG que copiar.
    MermaidRenderer m;
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(!m.exportPng("graph TD;Z-->Q; %% nunca rendido xyz", dir.filePath("o.png")));
}

void MermaidTests::export_emptyDestReturnsFalse()
{
    MermaidRenderer m;
    QVERIFY(!m.exportSource("x", ""));
    QVERIFY(!m.exportPng("<svg/>", ""));
}

QTEST_MAIN(MermaidTests)
#include "test_mermaid.moc"
