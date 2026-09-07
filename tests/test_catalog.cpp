// Integration tests de ModelCatalog (persistencia SQLite, aislada vía
// QStandardPaths test mode) + CatalogModel::sizeLabel.
//   - addOrUpdate / addBatch / get / getAt / findById / allForRoot
//   - filtros rootId / family / visionOnly
//   - markRootUnavailable / removeByRootId / reload

#include <QtTest>
#include <QStandardPaths>
#include "core/CatalogModel.h"
#include "core/ModelCatalog.h"

static CatalogModel mk(const QString &id, const QString &root, const QString &family,
                       bool vision = false, qint64 size = 1024)
{
    CatalogModel m;
    m.id = id; m.rootId = root; m.absolutePath = "C:/models/" + id + ".gguf";
    m.fileName = id + ".gguf"; m.familyHint = family;
    m.isVisionCandidate = vision; m.sizeBytes = size; m.isAvailable = true;
    m.mtime = QDateTime::currentDateTime();  // mtime es NOT NULL en la DB
    return m;
}

class CatalogTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        // Borrar la DB de corridas previas (la ubicación de test es estable) para
        // que los tests partan de un catálogo limpio y determinista.
        QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + "/model_catalog.db");
    }
    void init();  // catálogo limpio por test

    void sizeLabel();
    void addOrUpdate_andGet();
    void addBatch_andFindById();
    void allForRoot();
    void filterByFamily();
    void filterVisionOnly();
    void manualCompatibility_survivesRescanAndReload();
    void removeByRootId();
    void persistsAcrossReload();
    void reconcileRoot_marksMissingUnavailable();
    void reconcileRoot_emptyScanKeepsCatalog();
    void stableId_assignedAndPersisted();
    void stableId_survivesMovingTheFile();
    void stableId_distinctFilesGetDistinctAnchors();

private:
    void clearCatalog(ModelCatalog &c);
};

void CatalogTests::clearCatalog(ModelCatalog &c)
{
    c.removeByRootId("rootA");
    c.removeByRootId("rootB");
}

void CatalogTests::init() { ModelCatalog c; clearCatalog(c); }

void CatalogTests::sizeLabel()
{
    CatalogModel m; m.sizeBytes = 1536LL * 1024 * 1024;  // ~1.5 GB
    QVERIFY(m.sizeLabel().contains("GB") || m.sizeLabel().contains("GiB"));
}

void CatalogTests::addOrUpdate_andGet()
{
    ModelCatalog c;
    c.addOrUpdate(mk("a1", "rootA", "qwen"));
    QCOMPARE(c.get("a1").value("family").toString(), QStringLiteral("qwen"));
    // update mismo id
    c.addOrUpdate(mk("a1", "rootA", "llama"));
    QCOMPARE(c.get("a1").value("family").toString(), QStringLiteral("llama"));
}

void CatalogTests::addBatch_andFindById()
{
    ModelCatalog c;
    c.addBatch({mk("a1", "rootA", "qwen"), mk("a2", "rootA", "phi")});
    QCOMPARE(c.findById("a2").familyHint, QStringLiteral("phi"));
    QVERIFY(c.findById("nope").id.isEmpty());
}

void CatalogTests::allForRoot()
{
    ModelCatalog c;
    c.addBatch({mk("a1", "rootA", "qwen"), mk("b1", "rootB", "phi")});
    QCOMPARE(c.allForRoot("rootA").size(), 1);
    QCOMPARE(c.allForRoot("rootA").first().id, QStringLiteral("a1"));
}

void CatalogTests::filterByFamily()
{
    ModelCatalog c;
    c.addBatch({mk("a1", "rootA", "qwen"), mk("a2", "rootA", "phi")});
    c.setFilterFamily("qwen");
    QCOMPARE(c.count(), 1);
    c.setFilterFamily("");  // reset
    QVERIFY(c.count() >= 2);
}

void CatalogTests::filterVisionOnly()
{
    ModelCatalog c;
    c.addBatch({mk("a1", "rootA", "qwen", false), mk("a2", "rootA", "qwen", true)});
    c.setFilterVisionOnly(true);
    QCOMPARE(c.count(), 1);
    c.setFilterVisionOnly(false);
}

void CatalogTests::manualCompatibility_survivesRescanAndReload()
{
    {
        ModelCatalog c;
        c.addOrUpdate(mk("manual", "rootA", "qwen", false));
        QVERIFY(c.setManualCompatibility("manual", true, true));
        QVERIFY(c.get("manual").value("isVision").toBool());
        c.addOrUpdate(mk("manual", "rootA", "qwen", false));
        QVERIFY(c.findById("manual").isVisionCandidate);
    }
    ModelCatalog c2;
    QVERIFY(c2.findById("manual").isVisionCandidate);
    QVERIFY(c2.findById("manual").isDraftCandidate);
}

void CatalogTests::removeByRootId()
{
    ModelCatalog c;
    c.addBatch({mk("a1", "rootA", "qwen"), mk("b1", "rootB", "phi")});
    c.removeByRootId("rootA");
    QVERIFY(c.findById("a1").id.isEmpty());
    QVERIFY(!c.findById("b1").id.isEmpty());
}

void CatalogTests::persistsAcrossReload()
{
    { ModelCatalog c; c.addOrUpdate(mk("a1", "rootA", "gemma")); }
    ModelCatalog c2;  // reabre la misma DB
    QCOMPARE(c2.findById("a1").familyHint, QStringLiteral("gemma"));
}

// Tras un scan, lo que ya no está en disco debe dejar de estar disponible. Sin
// esto el catálogo sólo crecía: un root con modelos borrados seguía ofreciéndolos
// y un perfil que apuntaba a uno fallaba recién al arrancar el server.
void CatalogTests::reconcileRoot_marksMissingUnavailable()
{
    ModelCatalog c;
    c.addBatch({mk("a1", "rootA", "qwen"), mk("a2", "rootA", "qwen"),
                mk("b1", "rootB", "phi")});

    // El scan de rootA sólo encontró a1: a2 se borró del disco.
    c.reconcileRoot("rootA", QSet<QString>{QStringLiteral("a1")});

    QVERIFY(c.findById("a1").isAvailable);
    QVERIFY(!c.findById("a2").isAvailable);
    // Otros roots no se tocan.
    QVERIFY(c.findById("b1").isAvailable);

    // Vuelve a aparecer en un scan posterior → disponible otra vez.
    c.reconcileRoot("rootA", QSet<QString>{QStringLiteral("a1"), QStringLiteral("a2")});
    QVERIFY(c.findById("a2").isAvailable);
}

// Un scan que no devuelve nada es un root offline o un fallo, no un root vacío:
// invalidar todo ahí borraría el catálogo del usuario ante un disco desconectado.
void CatalogTests::reconcileRoot_emptyScanKeepsCatalog()
{
    ModelCatalog c;
    c.addBatch({mk("a1", "rootA", "qwen"), mk("a2", "rootA", "qwen")});
    c.reconcileRoot("rootA", QSet<QString>{});
    QVERIFY(c.findById("a1").isAvailable);
    QVERIFY(c.findById("a2").isAvailable);
}

// Toda fila recibe ancla > 0, y el ancla sobrevive a reabrir la DB.
void CatalogTests::stableId_assignedAndPersisted()
{
    qint64 anchor = 0;
    {
        ModelCatalog c;
        c.addBatch({mk("a1", "rootA", "qwen")});
        anchor = c.findById("a1").stableId;
        QVERIFY(anchor > 0);
    }
    ModelCatalog c2;   // reabre la misma DB
    QCOMPARE(c2.findById("a1").stableId, anchor);
    QCOMPARE(c2.findByStableId(anchor).id, QStringLiteral("a1"));
}

// EL caso que rompía los perfiles: el id textual sale de la ruta, así que mover el
// gguf de carpeta lo cambia. El ancla no cambia, y el perfil lo vuelve a encontrar.
void CatalogTests::stableId_survivesMovingTheFile()
{
    ModelCatalog c;
    CatalogModel before = mk("id-en-carpeta-vieja", "rootA", "qwen");
    before.fileName = QStringLiteral("modelo.gguf");
    before.absolutePath = QStringLiteral("D:/vieja/modelo.gguf");
    before.sizeBytes = 4242;
    c.addBatch({before});
    const qint64 anchor = c.findById("id-en-carpeta-vieja").stableId;
    QVERIFY(anchor > 0);

    // El scan posterior ve el MISMO archivo en otra carpeta: id nuevo, ancla igual.
    CatalogModel after = before;
    after.id = QStringLiteral("id-en-carpeta-nueva");
    after.absolutePath = QStringLiteral("D:/nueva/modelo.gguf");
    after.stableId = 0;                      // el scanner no conoce el ancla
    after.mtime = before.mtime.addSecs(60);  // el scan posterior la ve más nueva
    c.addBatch({after});

    QCOMPARE(c.findById("id-en-carpeta-nueva").stableId, anchor);
    // Y desde el ancla se llega a la fila vigente, que es lo que hace el perfil.
    QCOMPARE(c.findByStableId(anchor).absolutePath, QStringLiteral("D:/nueva/modelo.gguf"));
}

// Dos archivos distintos nunca comparten ancla: si la compartieran, un perfil
// terminaría cargando el modelo equivocado, que es peor que fallar.
void CatalogTests::stableId_distinctFilesGetDistinctAnchors()
{
    ModelCatalog c;
    CatalogModel a = mk("a1", "rootA", "qwen");
    a.fileName = QStringLiteral("uno.gguf"); a.sizeBytes = 100;
    CatalogModel b = mk("b1", "rootA", "qwen");
    b.fileName = QStringLiteral("dos.gguf"); b.sizeBytes = 100;   // mismo tamaño
    CatalogModel d = mk("c1", "rootA", "qwen");
    d.fileName = QStringLiteral("uno.gguf"); d.sizeBytes = 200;   // mismo nombre
    c.addBatch({a, b, d});

    const qint64 x = c.findById("a1").stableId;
    const qint64 y = c.findById("b1").stableId;
    const qint64 z = c.findById("c1").stableId;
    QVERIFY(x > 0 && y > 0 && z > 0);
    QVERIFY(x != y);
    QVERIFY(x != z);
    QVERIFY(y != z);
}

QTEST_MAIN(CatalogTests)
#include "test_catalog.moc"
