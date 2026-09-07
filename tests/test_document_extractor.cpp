// Unit tests de DocumentExtractor (sin sidecar Python):
//   - isImage por extensión.
//   - extract de texto plano / código → lectura directa.
//   - imágenes → "" (las maneja visión).
//   - budget: documentos enormes se truncan con nota.
// Los formatos ricos (pdf/office) requieren markitdown → no se cubren acá.

#include <QtTest>
#include <QTemporaryDir>
#include <QStandardPaths>
#include "core/DocumentExtractor.h"

class DocExtractorTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void isImage_byExtension();
    void isRich_byExtension();
    void extract_plainText();
    void extract_codeFile();
    void extract_imageReturnsEmpty();
    void extract_truncatesHugeDoc();

private:
    QString write(const QString &name, const QByteArray &data);
    QTemporaryDir m_dir;
};

QString DocExtractorTests::write(const QString &name, const QByteArray &data)
{
    const QString p = m_dir.filePath(name);
    QFile f(p);
    if (f.open(QIODevice::WriteOnly)) { f.write(data); f.close(); }
    return p;
}

void DocExtractorTests::isImage_byExtension()
{
    QVERIFY(DocumentExtractor::isImage("foto.PNG"));
    QVERIFY(DocumentExtractor::isImage("x.jpeg"));
    QVERIFY(!DocumentExtractor::isImage("notas.txt"));
}

// isRich decide a qué archivos el indexador del RAG híbrido le paga la extracción
// (include_docs). Código y texto plano NO son ricos: se leen directo.
void DocExtractorTests::isRich_byExtension()
{
    QVERIFY(DocumentExtractor::isRich("manual.PDF"));
    QVERIFY(DocumentExtractor::isRich("spec.docx"));
    QVERIFY(DocumentExtractor::isRich("hoja.xlsx"));
    QVERIFY(DocumentExtractor::isRich("pagina.html"));
    QVERIFY(!DocumentExtractor::isRich("main.cpp"));
    QVERIFY(!DocumentExtractor::isRich("notas.md"));
    QVERIFY(!DocumentExtractor::isRich("foto.png"));
}

void DocExtractorTests::extract_plainText()
{
    const QString p = write("notas.txt", "hola mundo\nsegunda linea");
    QString err;
    const QString out = DocumentExtractor::extract(p, &err);
    QCOMPARE(out, QStringLiteral("hola mundo\nsegunda linea"));
    QVERIFY(err.isEmpty());
}

void DocExtractorTests::extract_codeFile()
{
    const QString p = write("main.py", "def f():\n    return 42\n");
    const QString out = DocumentExtractor::extract(p);
    QVERIFY(out.contains("return 42"));
}

void DocExtractorTests::extract_imageReturnsEmpty()
{
    const QString p = write("img.png", QByteArray("\x89PNG\r\n", 6));
    QString err;
    const QString out = DocumentExtractor::extract(p, &err);
    QVERIFY(out.isEmpty());
}

void DocExtractorTests::extract_truncatesHugeDoc()
{
    const QString p = write("big.txt", QByteArray(300000, 'a'));  // > 240k cap
    const QString out = DocumentExtractor::extract(p);
    QVERIFY(out.size() < 300000);
    QVERIFY(out.contains("truncado"));
}

QTEST_MAIN(DocExtractorTests)
#include "test_document_extractor.moc"
