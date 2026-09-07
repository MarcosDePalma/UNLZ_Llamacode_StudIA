// Unit tests de FuzzyMatch: normalización (acelerador '&', tildes, puntuación),
// las tres métricas (ratio / partial / token-sort), la combinación ponderada y la
// selección del mejor candidato con cutoff. Todo PURO, sin disco ni UIA.

#include <QtTest>
#include "core/automation/FuzzyMatch.h"

class FuzzyMatchTests : public QObject
{
    Q_OBJECT
private slots:
    void normalize_stripsAcceleratorAccentsAndPunctuation();
    void ratio_isSymmetricAndBounded();
    void partialRatio_findsSubstring();
    void tokenSortRatio_ignoresWordOrder();
    void weightedRatio_matchesRealControlNameVariants();
    void weightedRatio_penalizesShortNeedleInLongName();
    void extractOne_picksBestCandidate();
    void extractOne_respectsCutoff();
    void extractOne_isDeterministicOnTies();
    void extractOne_rejectsEmptyNeedle();
};

void FuzzyMatchTests::normalize_stripsAcceleratorAccentsAndPunctuation()
{
    using namespace FuzzyMatch;
    // El caso que motiva todo: el modelo escribe el label "limpio", UIA lo expone
    // con acelerador y elipsis.
    QCOMPARE(normalize(QStringLiteral("&Guardar como...")), QStringLiteral("guardar como"));
    QCOMPARE(normalize(QStringLiteral("Guardar como")), QStringLiteral("guardar como"));
    // Tildes: el modelo suele perderlas al repetir un nombre.
    QCOMPARE(normalize(QStringLiteral("Añadir")), QStringLiteral("anadir"));
    QCOMPARE(normalize(QStringLiteral("Configuración")), QStringLiteral("configuracion"));
    // Espacios colapsados, bordes recortados.
    QCOMPARE(normalize(QStringLiteral("  Abrir   archivo  ")), QStringLiteral("abrir archivo"));
    QCOMPARE(normalize(QString()), QString());
    QCOMPARE(normalize(QStringLiteral("...")), QString());
}

void FuzzyMatchTests::ratio_isSymmetricAndBounded()
{
    using namespace FuzzyMatch;
    QCOMPARE(ratio(QStringLiteral("Guardar"), QStringLiteral("Guardar")), 100);
    QCOMPARE(ratio(QString(), QString()), 100);
    QCOMPARE(ratio(QStringLiteral("Guardar"), QString()), 0);
    // Simétrica y dentro de rango.
    const int ab = ratio(QStringLiteral("Guardar"), QStringLiteral("Guardor"));
    QCOMPARE(ab, ratio(QStringLiteral("Guardor"), QStringLiteral("Guardar")));
    QVERIFY(ab > 0 && ab < 100);
    // Nada en común → bajo.
    QVERIFY(ratio(QStringLiteral("Guardar"), QStringLiteral("xyz")) < 40);
}

void FuzzyMatchTests::partialRatio_findsSubstring()
{
    using namespace FuzzyMatch;
    // El corto entra entero en el largo → 100 (sin penalizar; penaliza weightedRatio).
    QCOMPARE(partialRatio(QStringLiteral("Guardar"), QStringLiteral("Guardar como...")), 100);
    QCOMPARE(partialRatio(QStringLiteral("Guardar como..."), QStringLiteral("Guardar")), 100);
    QVERIFY(partialRatio(QStringLiteral("Eliminar"), QStringLiteral("Guardar como")) < 60);
}

void FuzzyMatchTests::tokenSortRatio_ignoresWordOrder()
{
    using namespace FuzzyMatch;
    QCOMPARE(tokenSortRatio(QStringLiteral("como guardar"), QStringLiteral("Guardar como")), 100);
    // Sin reordenar, el ratio directo sí cae.
    QVERIFY(ratio(QStringLiteral("como guardar"), QStringLiteral("Guardar como")) < 100);
}

void FuzzyMatchTests::weightedRatio_matchesRealControlNameVariants()
{
    using namespace FuzzyMatch;
    // Variantes que en la práctica son el MISMO control.
    QCOMPARE(weightedRatio(QStringLiteral("Guardar como"), QStringLiteral("&Guardar como...")), 100);
    QCOMPARE(weightedRatio(QStringLiteral("Anadir"), QStringLiteral("Añadir")), 100);
    // Typo de una letra: sigue arriba del cutoff por defecto.
    QVERIFY(weightedRatio(QStringLiteral("Configuracion"),
                          QStringLiteral("Configuración")) >= 75);
    // Controles distintos: por debajo del cutoff → no se confunden.
    QVERIFY(weightedRatio(QStringLiteral("Eliminar"), QStringLiteral("Guardar")) < 75);
    QVERIFY(weightedRatio(QStringLiteral("Aceptar"), QStringLiteral("Cancelar")) < 75);
}

void FuzzyMatchTests::weightedRatio_penalizesShortNeedleInLongName()
{
    using namespace FuzzyMatch;
    // "Guardar" está entero adentro de "Guardar como", pero no son el mismo control:
    // alto, y aun así por debajo de la igualdad.
    const int s = weightedRatio(QStringLiteral("Guardar"), QStringLiteral("Guardar como"));
    QVERIFY(s >= 75);
    QVERIFY(s < 100);
    // Diferencia de largo extrema: la penalización fuerte evita que una palabra
    // suelta matchee un nombre largo cualquiera que la contenga.
    QVERIFY(weightedRatio(QStringLiteral("de"),
                          QStringLiteral("Restablecer todas las opciones de la aplicación "
                                         "a los valores de fábrica")) < 75);
}

void FuzzyMatchTests::extractOne_picksBestCandidate()
{
    using namespace FuzzyMatch;
    const QStringList controls{QStringLiteral("Eliminar"), QStringLiteral("&Guardar como..."),
                               QStringLiteral("Cancelar")};
    const Match m = extractOne(QStringLiteral("Guardar como"), controls);
    QVERIFY(m.ok());
    QCOMPARE(m.index, 1);
    QCOMPARE(m.score, 100);
}

void FuzzyMatchTests::extractOne_respectsCutoff()
{
    using namespace FuzzyMatch;
    const QStringList controls{QStringLiteral("Guardar"), QStringLiteral("Cancelar")};
    // Nada parecido → sin match (no elige "el menos malo").
    QVERIFY(!extractOne(QStringLiteral("Imprimir en PDF"), controls).ok());
    QVERIFY(extractOne(QStringLiteral("Imprimir en PDF"), controls).index < 0);
    // Cutoff bajo sí admite un match flojo: el umbral manda, no la función.
    QVERIFY(extractOne(QStringLiteral("Guardando"), controls, 60).ok());
    QVERIFY(!extractOne(QStringLiteral("Guardando"), controls, 99).ok());
}

void FuzzyMatchTests::extractOne_isDeterministicOnTies()
{
    using namespace FuzzyMatch;
    // Dos controles con el mismo nombre (pasa: dos botones "Abrir" en paneles
    // distintos). Ante empate gana el primero, siempre.
    const QStringList controls{QStringLiteral("Abrir"), QStringLiteral("Abrir")};
    QCOMPARE(extractOne(QStringLiteral("Abrir"), controls).index, 0);
    QCOMPARE(extractOne(QStringLiteral("Abrir"), controls).index, 0);
}

void FuzzyMatchTests::extractOne_rejectsEmptyNeedle()
{
    using namespace FuzzyMatch;
    const QStringList controls{QStringLiteral("Guardar"), QString()};
    QVERIFY(!extractOne(QString(), controls).ok());
    // Un needle que normaliza a vacío tampoco puede matchear el control sin nombre.
    QVERIFY(!extractOne(QStringLiteral("..."), controls).ok());
}

QTEST_MAIN(FuzzyMatchTests)
#include "test_fuzzy_match.moc"
