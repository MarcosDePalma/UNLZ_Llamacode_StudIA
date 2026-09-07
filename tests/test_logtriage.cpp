// Unit tests de LogTriage: detección de líneas de error, normalización de firmas
// (timestamps/punteros/rutas/números colapsan), agrupación con conteo y resumen
// acotado. Todo PURO, sin disco.

#include <QtTest>
#include "core/diag/LogTriage.h"

class LogTriageTests : public QObject
{
    Q_OBJECT
private slots:
    void isErrorLine_detectsErrorsIgnoresInfo();
    void normalize_collapsesVariableParts();
    void group_dedupsAndCounts();
    void group_ordersByCountDesc();
    void summarize_capsGroupsAndReportsRest();
    void summarize_emptyWhenNoErrors();
    void performanceWarnings_detectsCpuFallbacks();
    void performanceWarnings_ignoresHealthyLog();
};

void LogTriageTests::isErrorLine_detectsErrorsIgnoresInfo()
{
    QVERIFY(LogTriage::isErrorLine(QStringLiteral("[server/stderr] CUDA error: out of memory")));
    QVERIFY(LogTriage::isErrorLine(QStringLiteral("Segfault at 0x1234")));
    QVERIFY(LogTriage::isErrorLine(QStringLiteral("failed to load model")));
    QVERIFY(!LogTriage::isErrorLine(QStringLiteral("[server/stdout] loading model, 42 layers")));
    QVERIFY(!LogTriage::isErrorLine(QStringLiteral("health check ok")));
}

void LogTriageTests::normalize_collapsesVariableParts()
{
    const QString a = LogTriage::normalizeSignature(
        QStringLiteral("[2026-06-19 10:11:12.345] error at 0xDEADBEEF in C:\\x\\y.cpp line 42"));
    const QString b = LogTriage::normalizeSignature(
        QStringLiteral("[2026-06-19 11:00:00.001] error at 0xCAFEBABE in C:\\a\\b.cpp line 7"));
    QCOMPARE(a, b);   // misma firma: timestamp, puntero, ruta y número colapsan
    QVERIFY(!a.contains(QStringLiteral("0xDEAD")));
    QVERIFY(a.contains(QStringLiteral("0xADDR")));
}

void LogTriageTests::group_dedupsAndCounts()
{
    const QString log =
        QStringLiteral("[10:00:00] error: cudaMalloc failed size 100\n"
                       "info: all good\n"
                       "[10:00:01] error: cudaMalloc failed size 200\n"
                       "[10:00:02] error: cudaMalloc failed size 300\n");
    const auto g = LogTriage::group(log);
    QCOMPARE(g.size(), 1);
    QCOMPARE(g.first().count, 3);
    QVERIFY(g.first().sample.contains(QStringLiteral("cudaMalloc failed")));
}

void LogTriageTests::group_ordersByCountDesc()
{
    const QString log =
        QStringLiteral("error: bind address already in use\n"
                       "error: cudaMalloc failed 1\n"
                       "error: cudaMalloc failed 2\n");
    const auto g = LogTriage::group(log);
    QCOMPARE(g.size(), 2);
    QVERIFY(g.first().sample.contains(QStringLiteral("cudaMalloc")));   // count 2 primero
    QCOMPARE(g.first().count, 2);
    QCOMPARE(g.last().count, 1);
}

void LogTriageTests::summarize_capsGroupsAndReportsRest()
{
    const QString log =
        QStringLiteral("error: alpha falla distinta\n"
                       "error: bravo otro problema\n"
                       "error: charlie tercer fallo\n"
                       "error: delta cuarto issue\n"
                       "error: echo quinto crash\n");
    const QString summary = LogTriage::summarize(log, 2);
    QVERIFY(summary.contains(QStringLiteral("1x")));
    QVERIFY(summary.contains(QStringLiteral("3 firma")));   // 5 firmas - 2 mostradas = 3 restantes
}

void LogTriageTests::summarize_emptyWhenNoErrors()
{
    QVERIFY(LogTriage::summarize(QStringLiteral("info: todo bien\nhealth ok")).isEmpty());
    QVERIFY(LogTriage::summarize(QString()).isEmpty());
}

void LogTriageTests::performanceWarnings_detectsCpuFallbacks()
{
    const QString log = QStringLiteral(
        "llama_params_fit is not implemented for SPLIT_MODE_TENSOR\n"
        "backend sampling not supported with SPLIT_MODE_TENSOR, using CPU sampler\n"
        "layer 0 is assigned to device CPU but fused op is assigned to device CUDA0\n");
    const QString warnings = LogTriage::performanceWarnings(log);
    QVERIFY(warnings.contains(QStringLiteral("sampler CPU")));
    QVERIFY(warnings.contains(QStringLiteral("cayó en CPU")));
    QCOMPARE(warnings.count(QStringLiteral("Rendimiento:")), 2);
}

void LogTriageTests::performanceWarnings_ignoresHealthyLog()
{
    QVERIFY(LogTriage::performanceWarnings(
        QStringLiteral("CUDA0: all layers assigned\nCUDA1: all layers assigned\n"))
        .isEmpty());
}

QTEST_MAIN(LogTriageTests)
#include "test_logtriage.moc"
