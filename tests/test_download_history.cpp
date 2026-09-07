// Unit tests de DownloadHistoryStore: (de)serialización JSON roundtrip, append +
// persistencia con aislamiento de disco (QStandardPaths test mode), orden
// más-nuevo-primero, cap a kMax, y clear.

#include <QtTest>
#include "core/downloads/DownloadHistoryStore.h"

class DownloadHistoryTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void jsonRoundTrip();
    void append_fillsIdAndFinishedAt();
    void append_persistsAndOrdersNewestFirst();
    void append_capsToMax();
    void clear_removesHistory();
};

void DownloadHistoryTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void DownloadHistoryTests::init()
{
    DownloadHistoryStore s;
    s.clear();
}

void DownloadHistoryTests::jsonRoundTrip()
{
    const QVariantMap in{
        {"id", "abc123"}, {"kind", "model"}, {"name", "Qwen-Q4.gguf"},
        {"repo", "org/repo"}, {"path", "C:/models/Qwen-Q4.gguf"},
        {"state", "done"}, {"detail", "Modelo descargado"}, {"sizeMb", 1234.5},
        {"finishedAt", "2026-06-22T10:00:00"}
    };
    const QVariantMap out = DownloadHistoryStore::fromJson(DownloadHistoryStore::toJson(in));
    QCOMPARE(out.value("id").toString(), QStringLiteral("abc123"));
    QCOMPARE(out.value("kind").toString(), QStringLiteral("model"));
    QCOMPARE(out.value("name").toString(), QStringLiteral("Qwen-Q4.gguf"));
    QCOMPARE(out.value("state").toString(), QStringLiteral("done"));
    QCOMPARE(out.value("sizeMb").toDouble(), 1234.5);
}

void DownloadHistoryTests::append_fillsIdAndFinishedAt()
{
    DownloadHistoryStore s;
    s.append({{"kind", "model"}, {"name", "a.gguf"}, {"state", "done"}});
    const QVariantList h = s.history();
    QCOMPARE(h.size(), 1);
    const QVariantMap r = h.first().toMap();
    QVERIFY(!r.value("id").toString().isEmpty());
    QVERIFY(!r.value("finishedAt").toString().isEmpty());
}

void DownloadHistoryTests::append_persistsAndOrdersNewestFirst()
{
    { DownloadHistoryStore s; s.append({{"name", "first.gguf"}, {"state", "done"}}); }
    { DownloadHistoryStore s; s.append({{"name", "second.gguf"}, {"state", "error"}}); }
    // Instancia nueva: lee de disco.
    DownloadHistoryStore s2;
    const QVariantList h = s2.history();
    QCOMPARE(h.size(), 2);
    QCOMPARE(h.first().toMap().value("name").toString(), QStringLiteral("second.gguf"));
    QCOMPARE(h.last().toMap().value("name").toString(), QStringLiteral("first.gguf"));
}

void DownloadHistoryTests::append_capsToMax()
{
    DownloadHistoryStore s;
    for (int i = 0; i < DownloadHistoryStore::kMax + 25; ++i)
        s.append({{"name", QStringLiteral("m%1.gguf").arg(i)}, {"state", "done"}});
    const QVariantList h = s.history();
    QCOMPARE(h.size(), DownloadHistoryStore::kMax);
    // El más nuevo es el último encolado.
    QCOMPARE(h.first().toMap().value("name").toString(),
             QStringLiteral("m%1.gguf").arg(DownloadHistoryStore::kMax + 24));
}

void DownloadHistoryTests::clear_removesHistory()
{
    DownloadHistoryStore s;
    s.append({{"name", "x.gguf"}, {"state", "done"}});
    QVERIFY(!s.history().isEmpty());
    s.clear();
    QVERIFY(s.history().isEmpty());
}

QTEST_MAIN(DownloadHistoryTests)
#include "test_download_history.moc"
