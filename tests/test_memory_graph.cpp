// Unit tests de la memoria estructurada del agente:
//   - MemoryStore: save / recall (ranking por keywords + filtro scope) / forget
//     (stale vs delete) sobre el JSONL en <cwd>/.llamacode/.
//   - GraphStore: addEntity / link / query (depth 1 y 2) + normalización de nombres.
// Cada test usa un cwd temporal aislado.

#include <QtTest>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include "core/agent/AgentEventLog.h"
#include "core/agent/MemoryStore.h"
#include "core/agent/GraphStore.h"
#include "core/agent/KnowledgePacket.h"

class MemoryGraphTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void memory_saveThenRecall();
    void memory_recallFiltersByScope();
    void memory_personalScopeIsGlobalAndIsolated();
    void memory_recallRanksByQuery();
    void memory_forgetStale();
    void memory_forgetDelete();
    void memory_pruneBudget();
    void memory_pruneRedundant();
    void memory_pruneDryRun();
    void memory_decay_marksOnlyOldLowValueFacts();
    void memory_metadataAffectsRanking();
    void memory_newFieldsArePersisted();
    void memory_skillTypeIsPersistedAndRecalled();
    void memory_supersedesHidesOldFact();
    void memory_verifyClaimsReturnsEvidenceLevels();

    void graph_addEntityAndQuery();
    void graph_linkRelation();
    void graph_typedEdgesAndProvenance();
    void graph_verifyAndDropEdge();
    void graph_queryDepth2();
    void graph_normalizesNames();
    void graph_decideKeepsRejected();
    void graph_decisionsFiltersByTopic();
    void graph_sourceEvidencePacketAndDoctor();
    void graph_infersToolTouchesAndConsolidationLinks();
    void knowledge_packetMergesMemoryAndGraph();

    void eventLog_appendTypedEvent();
};

void MemoryGraphTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QFile::remove(MemoryStore::personalJsonlPath());
    QFile::remove(MemoryStore::personalJsonlPath() + QStringLiteral(".lock"));
}

void MemoryGraphTests::cleanupTestCase()
{
    QFile::remove(MemoryStore::personalJsonlPath());
    QFile::remove(MemoryStore::personalJsonlPath() + QStringLiteral(".lock"));
}

void MemoryGraphTests::memory_saveThenRecall()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "el build usa cmake con BUILD_TESTS",
                      "project", "fact", 0.9, "user");
    const QString out = MemoryStore::recall(dir.path(), "", "", 10);
    QVERIFY(out.contains("cmake"));
    QVERIFY(QFile::exists(MemoryStore::jsonlPath(dir.path())));
}

void MemoryGraphTests::memory_recallFiltersByScope()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "regla del proyecto X", "project", "decision", 0.8, "");
    MemoryStore::save(dir.path(), "preferencia personal Y", "personal", "preference", 0.8, "");
    const QString proj = MemoryStore::recall(dir.path(), "", "project", 10);
    QVERIFY(proj.contains("proyecto X"));
    QVERIFY(!proj.contains("personal Y"));
    MemoryStore::forget(dir.path(), "personal Y", "personal", "delete");
}

void MemoryGraphTests::memory_personalScopeIsGlobalAndIsolated()
{
    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());
    const QString token = QStringLiteral("__lc_global_personal_%1")
        .arg(QDateTime::currentMSecsSinceEpoch());
    MemoryStore::save(first.path(), token, "personal", "preference", 1.0, "user");

    const QString fromOtherProject = MemoryStore::recall(
        second.path(), token, "personal", 5);
    QVERIFY(fromOtherProject.contains(token));
    QVERIFY(QFile::exists(MemoryStore::personalJsonlPath()));
    const QString projectOnly = MemoryStore::recall(second.path(), token, "project", 5);
    // La respuesta de "sin hechos" repite el query; inspeccionar la fila de
    // memoria evita confundir ese eco con una fuga entre scopes.
    QVERIFY(!projectOnly.contains(QStringLiteral("[personal/preference]")));

    const QString forgotten = MemoryStore::forget(second.path(), token, "personal", "delete");
    QVERIFY(forgotten.contains(QStringLiteral("1 hecho")));
    QVERIFY(!MemoryStore::recall(second.path(), token, "personal", 5)
                 .contains(QStringLiteral("[personal/preference]")));
}

void MemoryGraphTests::memory_recallRanksByQuery()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "tema sobre vulkan backend", "project", "fact", 0.5, "");
    MemoryStore::save(dir.path(), "tema sobre cuda backend", "project", "fact", 0.5, "");
    const QString out = MemoryStore::recall(dir.path(), "vulkan", "", 1);
    QVERIFY(out.contains("vulkan"));
    QVERIFY(!out.contains("cuda"));
}

void MemoryGraphTests::memory_verifyClaimsReturnsEvidenceLevels()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/src/graph.cpp");
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("GraphStore validates source hashes and keeps citations.\n");
    source.close();

    const QVector<MemoryStore::ClaimEvidence> evidence = MemoryStore::verifyClaims(
        dir.path(), {QStringLiteral("GraphStore validates source hashes"),
                     QStringLiteral("unicorn subsystem always compiles")});
    QCOMPARE(evidence.size(), 2);
    QCOMPARE(evidence[0].status, QStringLiteral("accredited"));
    QVERIFY(evidence[0].coverage >= 0.8);
    QCOMPARE(evidence[1].status, QStringLiteral("unaccredited"));
    QCOMPARE(evidence[1].coverage, 0.0);
}

void MemoryGraphTests::memory_forgetStale()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "dato obsoleto deprecado", "project", "fact", 0.5, "");
    MemoryStore::forget(dir.path(), "obsoleto", "", "stale");
    const QString out = MemoryStore::recall(dir.path(), "", "", 10);
    QVERIFY(!out.contains("dato obsoleto"));  // ya no se recupera
}

void MemoryGraphTests::memory_forgetDelete()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "borrar esta linea entera", "project", "fact", 0.5, "");
    MemoryStore::save(dir.path(), "conservar esta", "project", "fact", 0.5, "");
    MemoryStore::forget(dir.path(), "borrar", "", "delete");
    QFile f(MemoryStore::jsonlPath(dir.path()));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString content = QString::fromUtf8(f.readAll());
    QVERIFY(!content.contains("borrar esta linea"));
    QVERIFY(content.contains("conservar esta"));
}

void MemoryGraphTests::memory_pruneBudget()
{
    QTemporaryDir dir;
    // 5 hechos sin solapamiento de keywords; presupuesto 3 → evicta 2.
    const char *topics[] = {"vulkan backend rendering", "perfiles guardado disco",
                            "catalogo modelos descarga", "voz transcripcion whisper",
                            "memoria grafo entidades"};
    for (const char *t : topics)
        MemoryStore::save(dir.path(), QString::fromLatin1(t), "project", "fact", 0.5, "");
    const QString rep = MemoryStore::prune(dir.path(), "", 3, "stale", false);
    QVERIFY(rep.contains("3 conservado"));
    // recall ya no devuelve los evictos (quedaron stale).
    const QString out = MemoryStore::recall(dir.path(), "", "project", 30);
    QCOMPARE(out.count(QStringLiteral("[project/")), 3);
}

void MemoryGraphTests::memory_pruneRedundant()
{
    QTemporaryDir dir;
    // dos casi-duplicados: el de menor confianza debe caer aunque haya presupuesto.
    MemoryStore::save(dir.path(), "el backend de red usa raw chat completions sse",
                      "project", "fact", 0.9, "");
    MemoryStore::save(dir.path(), "el backend de red usa raw chat completions sse",
                      "project", "fact", 0.3, "");
    MemoryStore::save(dir.path(), "tema totalmente aparte sobre vulkan", "project", "fact", 0.8, "");
    const QString rep = MemoryStore::prune(dir.path(), "", 50, "stale", false);
    QVERIFY(rep.contains("redundante"));
    QVERIFY(rep.contains("1 evicto"));
}

void MemoryGraphTests::memory_pruneDryRun()
{
    QTemporaryDir dir;
    for (int i = 0; i < 4; ++i)
        MemoryStore::save(dir.path(), QStringLiteral("dato %1 unico").arg(i),
                          "project", "fact", 0.5, "");
    const QString rep = MemoryStore::prune(dir.path(), "", 2, "stale", true);
    QVERIFY(rep.contains("dry-run"));
    // dry_run NO toca nada: recall sigue devolviendo los 4.
    const QString out = MemoryStore::recall(dir.path(), "", "", 30);
    QCOMPARE(out.count(QStringLiteral("unico")), 4);
}

void MemoryGraphTests::memory_decay_marksOnlyOldLowValueFacts()
{
    QTemporaryDir dir;
    const QString path = MemoryStore::jsonlPath(dir.path());
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    const QString oldTs = QDateTime::currentDateTime().addDays(-180).toString(Qt::ISODate);
    const QString recentTs = QDateTime::currentDateTime().toString(Qt::ISODate);
    const QJsonObject oldFact{
        {QStringLiteral("id"), QStringLiteral("old-fact")},
        {QStringLiteral("content"), QStringLiteral("old low value observation")},
        {QStringLiteral("scope"), QStringLiteral("project")},
        {QStringLiteral("type"), QStringLiteral("fact")},
        {QStringLiteral("confidence"), 0.1}, {QStringLiteral("importance"), 0.0},
        {QStringLiteral("surprise"), 0.0}, {QStringLiteral("verification"), QStringLiteral("inferred")},
        {QStringLiteral("useCount"), 0}, {QStringLiteral("ts"), oldTs}};
    const QJsonObject recentFact{
        {QStringLiteral("id"), QStringLiteral("recent-fact")},
        {QStringLiteral("content"), QStringLiteral("recent low value observation")},
        {QStringLiteral("scope"), QStringLiteral("project")},
        {QStringLiteral("type"), QStringLiteral("fact")},
        {QStringLiteral("confidence"), 0.1}, {QStringLiteral("importance"), 0.0},
        {QStringLiteral("surprise"), 0.0}, {QStringLiteral("verification"), QStringLiteral("inferred")},
        {QStringLiteral("useCount"), 0}, {QStringLiteral("ts"), recentTs}};
    const QJsonObject protectedDecision{
        {QStringLiteral("id"), QStringLiteral("protected-decision")},
        {QStringLiteral("content"), QStringLiteral("old verified project decision")},
        {QStringLiteral("scope"), QStringLiteral("project")},
        {QStringLiteral("type"), QStringLiteral("decision")},
        {QStringLiteral("confidence"), 0.1}, {QStringLiteral("importance"), 0.0},
        {QStringLiteral("surprise"), 0.0}, {QStringLiteral("verification"), QStringLiteral("user")},
        {QStringLiteral("useCount"), 0}, {QStringLiteral("ts"), oldTs}};
    QFile out(path);
    QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Text));
    for (const QJsonObject &row : {oldFact, recentFact, protectedDecision}) {
        out.write(QJsonDocument(row).toJson(QJsonDocument::Compact));
        out.write("\n");
    }
    out.close();

    const QString report = MemoryStore::decay(dir.path(), "project", 90, 0.28, false);
    QVERIFY(report.contains(QStringLiteral("1 candidato")));
    QVERIFY(out.open(QIODevice::ReadOnly | QIODevice::Text));
    QHash<QString, QJsonObject> rows;
    while (!out.atEnd()) {
        const QJsonObject row = QJsonDocument::fromJson(out.readLine()).object();
        rows.insert(row.value(QStringLiteral("id")).toString(), row);
    }
    QVERIFY(rows.value(QStringLiteral("old-fact")).value(QStringLiteral("stale")).toBool());
    QVERIFY(!rows.value(QStringLiteral("recent-fact")).value(QStringLiteral("stale")).toBool());
    QVERIFY(!rows.value(QStringLiteral("protected-decision")).value(QStringLiteral("stale")).toBool());
}

void MemoryGraphTests::memory_metadataAffectsRanking()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "regla vulkan rutinaria", "project", "fact", 0.8, "agent");
    MemoryStore::save(dir.path(), "regla vulkan corregida", "project", "decision", 0.8, "user",
                      1.0, 1.0, "user");
    const QString out = MemoryStore::recall(dir.path(), "regla vulkan", "project", 1);
    QVERIFY(out.contains("corregida"));
}

void MemoryGraphTests::memory_newFieldsArePersisted()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "usar siempre Release", "project", "decision", 1.0, "user",
                      0.95, 0.9, "user", "old-id");
    QFile f(MemoryStore::jsonlPath(dir.path()));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
    QCOMPARE(o.value("importance").toDouble(), 0.95);
    QCOMPARE(o.value("surprise").toDouble(), 0.9);
    QCOMPARE(o.value("verification").toString(), QString("user"));
    QCOMPARE(o.value("supersedes").toString(), QString("old-id"));
}

void MemoryGraphTests::memory_skillTypeIsPersistedAndRecalled()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(),
                      "si UI Automation no expone el control, usar OCR y verificar después",
                      "project", "skill", 0.9, "recovery_learning",
                      0.9, 0.8, "tool");
    QFile f(MemoryStore::jsonlPath(dir.path()));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject row = QJsonDocument::fromJson(f.readLine()).object();
    QCOMPARE(row.value("type").toString(), QString("skill"));
    QCOMPARE(row.value("source").toString(), QString("recovery_learning"));
    QVERIFY(MemoryStore::recall(dir.path(), "OCR control", "project", 5)
                .contains(QStringLiteral("[project/skill")));
}

void MemoryGraphTests::memory_supersedesHidesOldFact()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), "usar siempre Debug", "project", "decision", 1.0, "user");
    QFile f(MemoryStore::jsonlPath(dir.path()));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString oldId = QJsonDocument::fromJson(f.readLine()).object().value("id").toString();
    f.close();
    MemoryStore::save(dir.path(), "usar siempre Release", "project", "decision", 1.0, "user",
                      1.0, 1.0, "user", oldId);
    const QString out = MemoryStore::recall(dir.path(), "usar siempre", "project", 10);
    QVERIFY(out.contains("Release"));
    QVERIFY(!out.contains("Debug"));
}

void MemoryGraphTests::graph_addEntityAndQuery()
{
    QTemporaryDir dir;
    GraphStore::addEntity(dir.path(), "AppController", "module");
    const QString out = GraphStore::query(dir.path(), "AppController", 1);
    QVERIFY(out.contains("AppController"));
    QVERIFY(QFile::exists(GraphStore::jsonlPath(dir.path())));
}

void MemoryGraphTests::graph_linkRelation()
{
    QTemporaryDir dir;
    GraphStore::link(dir.path(), "AppController", "usa", "ProfileManager");
    const QString out = GraphStore::query(dir.path(), "AppController", 1);
    QVERIFY(out.contains("ProfileManager"));
    QVERIFY(out.contains("REQUIRES"));                  // 'usa' → tipo REQUIRES
    QVERIFY(out.contains("unreviewed"));                // edge del agente: sin revisar por defecto
}

// Typed edges + provenance + confianza (patrón CKG): el edge inferido por el LLM
// entra unreviewed y se distingue del determinista; edge_type fuerza taxonomía;
// query ordena verificados antes que unreviewed; relaciones viejas sin los campos
// se leen igual (back-compat) infiriendo el tipo del pred.
void MemoryGraphTests::graph_typedEdgesAndProvenance()
{
    QTemporaryDir dir;
    // edge_type explícito + confianza → verificado.
    GraphStore::link(dir.path(), "A", "rel", "B", "IMPLEMENTS", 0.9,
                     QStringLiteral("user"));
    // edge del agente sin conf → unreviewed.
    GraphStore::link(dir.path(), "A", "toca", "C");
    const QString out = GraphStore::query(dir.path(), "A", 1);
    QVERIFY(out.contains("IMPLEMENTS"));                // taxonomía forzada gana
    QVERIFY(out.contains("conf=0.9") && out.contains("user"));
    QVERIFY(out.contains("unreviewed"));               // el segundo edge
    // Orden: el verificado (B) aparece antes que el unreviewed (C).
    QVERIFY(out.indexOf('B') < out.indexOf('C'));

    // Back-compat: relación vieja SIN etype/conf/prov se lee y tipa por el pred.
    // Mismo id de entidad que usa el store (sha1 del nombre normalizado, hex[0:8]).
    auto eid = [](const QString &name) {
        const QByteArray h = QCryptographicHash::hash(
            name.trimmed().toLower().toUtf8(), QCryptographicHash::Sha1);
        return QStringLiteral("e_") + QString::fromLatin1(h.toHex().left(8));
    };
    const QString path = GraphStore::jsonlPath(dir.path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::Append | QIODevice::Text));
    const QString legId = eid("legacy.cpp"), depId = eid("dep.h");
    f.write(QStringLiteral("{\"kind\":\"entity\",\"id\":\"%1\",\"name\":\"legacy.cpp\","
            "\"etype\":\"file\",\"ts\":\"2025-01-01\"}\n").arg(legId).toUtf8());
    f.write(QStringLiteral("{\"kind\":\"entity\",\"id\":\"%1\",\"name\":\"dep.h\","
            "\"etype\":\"file\",\"ts\":\"2025-01-01\"}\n").arg(depId).toUtf8());
    f.write(QStringLiteral("{\"kind\":\"relation\",\"id\":\"r_old1\",\"subj\":\"%1\","
            "\"pred\":\"imports\",\"obj\":\"%2\",\"ts\":\"2025-01-01\"}\n")
            .arg(legId, depId).toUtf8());
    f.close();
    const QString leg = GraphStore::query(dir.path(), "legacy.cpp", 1);
    QVERIFY(leg.contains("IMPORTS"));                  // etype inferido del pred viejo
    QVERIFY(leg.contains("unreviewed"));              // conf ausente → unreviewed
}

void MemoryGraphTests::graph_verifyAndDropEdge()
{
    QTemporaryDir dir;
    // Edge del agente entra unreviewed.
    GraphStore::link(dir.path(), "X", "toca", "Y");
    QVERIFY(GraphStore::query(dir.path(), "X", 1).contains("unreviewed"));

    // verify sube conf + marca prov=user → deja de ser unreviewed.
    const QString v = GraphStore::reviewRelation(dir.path(), "X", "toca", "Y", 0.8);
    QVERIFY(v.contains("revisado"));
    const QString q = GraphStore::query(dir.path(), "X", 1);
    QVERIFY(!q.contains("unreviewed"));
    QVERIFY(q.contains("conf=0.8") && q.contains("user"));

    // Edge inexistente → error, no crash.
    QVERIFY(GraphStore::reviewRelation(dir.path(), "X", "nada", "Z", 1.0)
                .contains("no existe"));

    // drop tacha el edge puntual (Y desaparece del vecindario).
    GraphStore::link(dir.path(), "X", "usa", "W");
    const QString d = GraphStore::reviewRelation(dir.path(), "X", "toca", "Y", 0, "user", true);
    QVERIFY(d.contains("tachado"));
    const QString q2 = GraphStore::query(dir.path(), "X", 1);
    QVERIFY(!q2.contains("Y"));    // edge tachado
    QVERIFY(q2.contains("W"));     // el otro edge intacto
}

void MemoryGraphTests::graph_queryDepth2()
{
    QTemporaryDir dir;
    GraphStore::link(dir.path(), "A", "usa", "B");
    GraphStore::link(dir.path(), "B", "usa", "C");
    const QString d1 = GraphStore::query(dir.path(), "A", 1);
    const QString d2 = GraphStore::query(dir.path(), "A", 2);
    QVERIFY(d1.contains("B"));
    QVERIFY(d2.contains("C"));  // vecino de vecino visible con depth=2
}

void MemoryGraphTests::graph_normalizesNames()
{
    QTemporaryDir dir;
    GraphStore::link(dir.path(), "  MyModule  ", "rel", "Other");
    // Misma entidad normalizada (trim/lowercase) → query la encuentra y ve la relación.
    const QString out = GraphStore::query(dir.path(), "mymodule", 1);
    QVERIFY(!out.isEmpty());
    QVERIFY(out.contains("Other"));
}

void MemoryGraphTests::graph_decideKeepsRejected()
{
    QTemporaryDir dir;
    GraphStore::Rejected rej{{"polling cada 5s", "quema CPU"},
                             {"websockets", "complejidad innecesaria"}};
    GraphStore::decide(dir.path(), "refresco de VRAM", "signal en cambio",
                       rej, "más simple y reactivo");
    const QString out = GraphStore::decisions(dir.path(), "");
    QVERIFY(out.contains("refresco de VRAM"));
    QVERIFY(out.contains("signal en cambio"));
    // las alternativas rechazadas se conservan con su motivo (audit trail).
    QVERIFY(out.contains("polling cada 5s"));
    QVERIFY(out.contains("quema CPU"));
    QVERIFY(out.contains("websockets"));
}

void MemoryGraphTests::graph_decisionsFiltersByTopic()
{
    QTemporaryDir dir;
    GraphStore::decide(dir.path(), "backend de red", "RawChat", {}, "");
    GraphStore::decide(dir.path(), "formato de storage", "JSONL", {}, "append-only");
    const QString out = GraphStore::decisions(dir.path(), "storage");
    QVERIFY(out.contains("JSONL"));
    QVERIFY(!out.contains("RawChat"));  // filtrado por substring del tema
}

void MemoryGraphTests::graph_sourceEvidencePacketAndDoctor()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/src/store.cpp");
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray bytes("class Store {};\n");
    source.write(bytes);
    source.close();

    GraphStore::SourceRef ref;
    ref.path = QStringLiteral("src/store.cpp");
    ref.startLine = 1;
    ref.endLine = 1;
    ref.sha256 = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    ref.kind = QStringLiteral("code");
    GraphStore::link(dir.path(), "src/store.cpp", "defines", "Store",
                     QStringLiteral("DEFINES"), 1.0, QStringLiteral("indexer"),
                     GraphStore::SourceRefs{ref});

    const QJsonObject packet = GraphStore::queryPacket(dir.path(), "src/store.cpp", 1);
    QVERIFY(packet.value(QStringLiteral("ok")).toBool());
    QVERIFY(packet.value(QStringLiteral("sources")).toArray().size() == 1);
    QVERIFY(packet.value(QStringLiteral("edges")).toArray().first().toObject()
                .value(QStringLiteral("citations")).toArray().contains("src/store.cpp:1"));
    QVERIFY(GraphStore::query(dir.path(), "src/store.cpp", 1).contains("src/store.cpp:1"));

    const QJsonObject healthy = GraphStore::doctor(dir.path());
    QVERIFY(healthy.value(QStringLiteral("healthy")).toBool());
    QCOMPARE(healthy.value(QStringLiteral("sourceRefs")).toInt(), 1);

    QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
    source.write("class Store {};\n// changed\n");
    source.close();
    const QJsonObject stale = GraphStore::doctor(dir.path());
    QCOMPARE(stale.value(QStringLiteral("staleSources")).toInt(), 1);
    QVERIFY(!stale.value(QStringLiteral("healthy")).toBool());
}

void MemoryGraphTests::graph_infersToolTouchesAndConsolidationLinks()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/src/widget.cpp");
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile source(path);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("class Widget {};\n");
    source.close();

    const QString touch = GraphStore::inferToolTouch(
        dir.path(), QStringLiteral("write_file"), QStringLiteral("src/widget.cpp"),
        QStringLiteral("session-1"), QStringLiteral("corr-1"));
    QVERIFY(touch.contains(QStringLiteral("graph infer")));
    const QJsonObject packet = GraphStore::queryPacket(dir.path(), QStringLiteral("module:src"), 1);
    QVERIFY(packet.value(QStringLiteral("ok")).toBool());
    const QJsonObject edge = packet.value(QStringLiteral("edges")).toArray().first().toObject();
    QCOMPARE(edge.value(QStringLiteral("pred")).toString(), QStringLiteral("touches"));
    QCOMPARE(edge.value(QStringLiteral("status")).toString(), QStringLiteral("unreviewed"));
    QVERIFY(edge.value(QStringLiteral("sources")).toArray().first().toObject()
                .value(QStringLiteral("sessionId")).toString() == QStringLiteral("session-1"));

    const int links = GraphStore::inferConsolidationLinks(
        dir.path(), {{QStringLiteral("decision"), QStringLiteral("Usar GraphStore para el backend")},
                     {QStringLiteral("bug"), QStringLiteral("GraphStore falla al abrir el backend")}},
        QStringLiteral("session-1"), QStringLiteral("corr-1"));
    QCOMPARE(links, 1);
    QFile graph(GraphStore::jsonlPath(dir.path()));
    QVERIFY(graph.open(QIODevice::ReadOnly));
    const QString graphText = QString::fromUtf8(graph.readAll());
    QVERIFY(graphText.contains(QStringLiteral("relates_to")));
    QVERIFY(graphText.contains(QStringLiteral("session-1")));
}

void MemoryGraphTests::knowledge_packetMergesMemoryAndGraph()
{
    QTemporaryDir dir;
    MemoryStore::save(dir.path(), QStringLiteral("Store usa configuración durable"),
                      QStringLiteral("project"), QStringLiteral("decision"), 1.0,
                      QStringLiteral("user"));
    GraphStore::link(dir.path(), QStringLiteral("Store"), QStringLiteral("requires"),
                     QStringLiteral("Config"), QStringLiteral("REQUIRES"), 0.9,
                     QStringLiteral("user"));

    const QJsonObject packet = KnowledgePacket::build(
        dir.path(), QStringLiteral("Store configuración"), 4, 4);
    QVERIFY(packet.value(QStringLiteral("factsText")).toString().contains(QStringLiteral("Store")));
    QVERIFY(packet.value(QStringLiteral("edges")).toArray().size() >= 1);
    QVERIFY(packet.value(QStringLiteral("receipt")).toObject()
                .value(QStringLiteral("edgeCount")).toInt() >= 1);
    const QString formatted = KnowledgePacket::format(packet, 4000);
    QVERIFY(formatted.contains(QStringLiteral("knowledge-receipt")));
    QVERIFY(formatted.contains(QStringLiteral("REQUIRES")));
    QVERIFY(packet.value(QStringLiteral("decisions")).toArray().size() >= 1);
    QVERIFY(formatted.contains(QStringLiteral("Decisiones vigentes")));
    QVERIFY(formatted.contains(QStringLiteral("fuente de verdad")));
}

void MemoryGraphTests::eventLog_appendTypedEvent()
{
    QTemporaryDir dir;
    const bool ok = AgentEventLog::append(
        dir.path(), "s1", "tool_call",
        QJsonObject{{"tool", "read_file"}, {"kind", "read"}, {"detail", "README.md"}});
    QVERIFY(ok);
    QFile f(AgentEventLog::jsonlPath(dir.path()));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
    QCOMPARE(o.value("kind").toString(), QString("tool_call"));
    QCOMPARE(o.value("sessionId").toString(), QString("s1"));
    QCOMPARE(o.value("tool").toString(), QString("read_file"));
    QVERIFY(!o.value("id").toString().isEmpty());
    QVERIFY(!o.value("ts").toString().isEmpty());
}

QTEST_MAIN(MemoryGraphTests)
#include "test_memory_graph.moc"
