#include "GraphStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <algorithm>

namespace {

QString norm(const QString &s) { return s.trimmed().toLower(); }

QString normType(const QString &t)
{
    const QString v = norm(t);
    static const QSet<QString> ok{
        QStringLiteral("file"), QStringLiteral("module"), QStringLiteral("decision"),
        QStringLiteral("bug"), QStringLiteral("person"), QStringLiteral("concept"),
        QStringLiteral("other")};
    return ok.contains(v) ? v : QStringLiteral("concept");
}

// Taxonomía CERRADA de tipos de arista. El 'pred' libre (verbo) se conserva como
// label humano, pero cada relación se clasifica en uno de estos tipos para que el
// agente NO trate una relación blanda ("relates_to") como una dependencia dura
// ("requires"). Idea del CKG (typed edges): separar dependencia de asociación.
QString normEdge(const QString &edgeType, const QString &pred)
{
    static const QSet<QString> ok{
        QStringLiteral("REQUIRES"), QStringLiteral("ENABLES"),
        QStringLiteral("IMPLEMENTS"), QStringLiteral("DEFINES"),
        QStringLiteral("CALLS"), QStringLiteral("IMPORTS"),
        QStringLiteral("RELATES_TO")};
    const QString explicitT = edgeType.trimmed().toUpper();
    if (ok.contains(explicitT)) return explicitT;

    // Inferir del verbo libre.
    const QString p = norm(pred);
    if (p.contains(QLatin1String("requir")) || p.contains(QLatin1String("depend"))
        || p.contains(QLatin1String("needs")) || p == QLatin1String("necesita")
        || p == QLatin1String("usa") || p == QLatin1String("uses"))
        return QStringLiteral("REQUIRES");
    if (p.contains(QLatin1String("enable")) || p.contains(QLatin1String("provide"))
        || p.contains(QLatin1String("habilita")))
        return QStringLiteral("ENABLES");
    if (p.contains(QLatin1String("implement")))
        return QStringLiteral("IMPLEMENTS");
    if (p.contains(QLatin1String("defin")))
        return QStringLiteral("DEFINES");
    if (p.contains(QLatin1String("call")) || p.contains(QLatin1String("invoke")))
        return QStringLiteral("CALLS");
    if (p.contains(QLatin1String("import")))
        return QStringLiteral("IMPORTS");
    return QStringLiteral("RELATES_TO");
}

// conf<0 → JSON null (unreviewed, NO = wrong). Si no, número acotado a [0,1].
QJsonValue confVal(double conf)
{
    if (conf < 0.0) return QJsonValue(QJsonValue::Null);
    return QJsonValue(qBound(0.0, conf, 1.0));
}

// id estable de entidad: hash del nombre normalizado (mismo nombre → mismo id).
QString entId(const QString &name)
{
    const QByteArray h = QCryptographicHash::hash(
        norm(name).toUtf8(), QCryptographicHash::Sha1);
    return QStringLiteral("e_") + QString::fromLatin1(h.toHex().left(8));
}

QString relId(const QString &subj, const QString &pred, const QString &obj)
{
    const QByteArray h = QCryptographicHash::hash(
        (subj + QLatin1Char('|') + norm(pred) + QLatin1Char('|') + obj).toUtf8(),
        QCryptographicHash::Sha1);
    return QStringLiteral("r_") + QString::fromLatin1(h.toHex().left(8));
}

void appendObj(const QString &path, const QJsonObject &o)
{
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.write("\n");
    f.close();
}

QJsonArray sourceArray(const GraphStore::SourceRefs &sources)
{
    QJsonArray out;
    for (const GraphStore::SourceRef &source : sources) {
        const QJsonObject o = source.toJson();
        if (!o.isEmpty()) out.append(o);
    }
    return out;
}

GraphStore::SourceRefs readSources(const QJsonObject &o)
{
    GraphStore::SourceRefs out;
    for (const QJsonValue &v : o.value(QStringLiteral("sources")).toArray()) {
        if (v.isObject()) out.append(GraphStore::SourceRef::fromJson(v.toObject()));
    }
    return out;
}

QString sourceKey(const GraphStore::SourceRef &source)
{
    return QString::fromUtf8(QJsonDocument(source.toJson()).toJson(QJsonDocument::Compact));
}

}  // namespace

namespace GraphStore {

QJsonObject SourceRef::toJson() const
{
    QJsonObject o;
    if (!path.trimmed().isEmpty()) o[QStringLiteral("path")] = path.trimmed();
    if (startLine > 0) o[QStringLiteral("startLine")] = startLine;
    if (endLine > 0) o[QStringLiteral("endLine")] = endLine;
    if (!sha256.trimmed().isEmpty()) o[QStringLiteral("sha256")] = sha256.trimmed();
    if (!kind.trimmed().isEmpty()) o[QStringLiteral("kind")] = kind.trimmed();
    if (!sessionId.trimmed().isEmpty()) o[QStringLiteral("sessionId")] = sessionId.trimmed();
    if (!correlationId.trimmed().isEmpty())
        o[QStringLiteral("correlationId")] = correlationId.trimmed();
    if (!commit.trimmed().isEmpty()) o[QStringLiteral("commit")] = commit.trimmed();
    return o;
}

SourceRef SourceRef::fromJson(const QJsonObject &o)
{
    SourceRef out;
    out.path = o.value(QStringLiteral("path")).toString();
    out.startLine = qMax(0, o.value(QStringLiteral("startLine")).toInt());
    out.endLine = qMax(0, o.value(QStringLiteral("endLine")).toInt());
    out.sha256 = o.value(QStringLiteral("sha256")).toString();
    out.kind = o.value(QStringLiteral("kind")).toString();
    out.sessionId = o.value(QStringLiteral("sessionId")).toString();
    out.correlationId = o.value(QStringLiteral("correlationId")).toString();
    out.commit = o.value(QStringLiteral("commit")).toString();
    return out;
}

QString jsonlPath(const QString &cwd)
{
    return QDir::cleanPath(cwd + QStringLiteral("/.llamacode/graph.jsonl"));
}

QString addEntity(const QString &cwd, const QString &name, const QString &etype)
{
    const QString nm = name.trimmed();
    if (nm.isEmpty()) return QStringLiteral("[graph: 'name' vacío]");

    const QString path = jsonlPath(cwd);
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString id = entId(nm);

    // Dedupe: si ya existe esa entidad, no la re-agregamos.
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            if (l.isEmpty()) continue;
            const QJsonObject o = QJsonDocument::fromJson(l).object();
            if (o.value(QStringLiteral("kind")).toString() == QLatin1String("entity")
                && o.value(QStringLiteral("id")).toString() == id) {
                f.close();
                return QStringLiteral("[entidad ya existe · id=%1 '%2']").arg(id, nm);
            }
        }
        f.close();
    }

    appendObj(path, QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("entity")},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), nm},
        {QStringLiteral("etype"), normType(etype)},
        {QStringLiteral("ts"), QDateTime::currentDateTime().toString(Qt::ISODate)}});
    return QStringLiteral("[entidad creada · id=%1 etype=%2 '%3']")
        .arg(id, normType(etype), nm);
}

QString link(const QString &cwd, const QString &subj, const QString &pred,
             const QString &obj, const QString &edgeType, double conf,
             const QString &prov, const SourceRefs &sources)
{
    const QString s = subj.trimmed(), p = pred.trimmed(), o = obj.trimmed();
    if (s.isEmpty() || p.isEmpty() || o.isEmpty())
        return QStringLiteral("[graph link: subj/pred/obj requeridos]");

    addEntity(cwd, s, QString());      // auto-crea (dedupe interno)
    addEntity(cwd, o, QString());

    const QString path = jsonlPath(cwd);
    const QString sid = entId(s), oid = entId(o), rid = relId(sid, p, oid);

    // Dedupe de la relación.
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QByteArray l = f.readLine().trimmed();
            if (l.isEmpty()) continue;
            const QJsonObject ro = QJsonDocument::fromJson(l).object();
            if (ro.value(QStringLiteral("kind")).toString() == QLatin1String("relation")
                && ro.value(QStringLiteral("id")).toString() == rid) {
                f.close();
                return QStringLiteral("[relación ya existe · %1 -[%2]-> %3]").arg(s, p, o);
            }
        }
        f.close();
    }

    const QString et = normEdge(edgeType, p);
    QJsonObject relation{
        {QStringLiteral("kind"), QStringLiteral("relation")},
        {QStringLiteral("id"), rid},
        {QStringLiteral("subj"), sid},
        {QStringLiteral("pred"), norm(p)},
        {QStringLiteral("obj"), oid},
        {QStringLiteral("etype"), et},
        {QStringLiteral("conf"), confVal(conf)},
        {QStringLiteral("prov"), prov.trimmed().isEmpty() ? QStringLiteral("llm")
                                                          : prov.trimmed()},
        {QStringLiteral("ts"), QDateTime::currentDateTime().toString(Qt::ISODate)}};
    const QJsonArray evidence = sourceArray(sources);
    if (!evidence.isEmpty()) relation.insert(QStringLiteral("sources"), evidence);
    appendObj(path, relation);
    return QStringLiteral("[relación creada · %1 -[%2]-> %3]").arg(s, et, o);
}

QString inferToolTouch(const QString &cwd, const QString &tool,
                       const QString &path, const QString &sessionId,
                       const QString &correlationId)
{
    QString rel = QDir::fromNativeSeparators(path.trimmed());
    if (rel.isEmpty()) return QStringLiteral("[graph infer: path vacío]");
    if (QDir::isAbsolutePath(rel))
        rel = QDir::fromNativeSeparators(QDir(cwd).relativeFilePath(rel));
    rel = QDir::cleanPath(rel);
    if (rel.isEmpty() || rel == QLatin1String(".") || rel == QLatin1String("..")
        || rel.startsWith(QLatin1String("../")))
        return QStringLiteral("[graph infer: path fuera del proyecto]");

    const int slash = rel.lastIndexOf(QLatin1Char('/'));
    const QString dir = slash > 0 ? rel.left(slash) : QStringLiteral("project");
    const QString module = QStringLiteral("module:%1").arg(dir);
    addEntity(cwd, module, QStringLiteral("module"));
    addEntity(cwd, rel, QStringLiteral("file"));

    SourceRef source;
    source.path = rel;
    source.kind = QStringLiteral("session");
    source.sessionId = sessionId;
    source.correlationId = correlationId;
    QFile file(QDir(cwd).absoluteFilePath(rel));
    if (file.open(QIODevice::ReadOnly)) {
        source.sha256 = QString::fromLatin1(
            QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    }
    const QString result = link(cwd, module, QStringLiteral("touches"), rel,
                                QStringLiteral("RELATES_TO"), -1.0,
                                tool.trimmed().isEmpty() ? QStringLiteral("tool")
                                                          : QStringLiteral("tool"),
                                SourceRefs{source});
    return QStringLiteral("[graph infer tool=%1] %2").arg(
        tool.trimmed().isEmpty() ? QStringLiteral("unknown") : tool.trimmed(), result);
}

int inferConsolidationLinks(const QString &cwd,
                            const QVector<QPair<QString, QString>> &facts,
                            const QString &sessionId,
                            const QString &correlationId)
{
    struct Fact { QString type; QString content; QString entity; QSet<QString> terms; };
    QVector<Fact> decisions;
    QVector<Fact> bugs;
    for (const auto &raw : facts) {
        const QString type = norm(raw.first);
        if (type != QLatin1String("decision") && type != QLatin1String("bug")) continue;
        const QString content = raw.second.trimmed();
        if (content.isEmpty()) continue;
        const QByteArray hash = QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha1);
        Fact fact{type, content,
                  QStringLiteral("%1:%2 · %3").arg(type, QString::fromLatin1(hash.toHex().left(10)),
                                                      content.left(120)), {}};
        for (const QString &term : content.toLower().split(
                 QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_]+")), Qt::SkipEmptyParts)) {
            if (term.size() >= 4) fact.terms.insert(term);
        }
        addEntity(cwd, fact.entity, type);
        if (type == QLatin1String("decision")) decisions.append(fact);
        else bugs.append(fact);
    }

    int added = 0;
    for (const Fact &decision : decisions) {
        for (const Fact &bug : bugs) {
            QSet<QString> shared = decision.terms;
            shared.intersect(bug.terms);
            if (shared.isEmpty()) continue;
            SourceRef source;
            source.kind = QStringLiteral("session");
            source.sessionId = sessionId;
            source.correlationId = correlationId;
            const QString result = link(cwd, decision.entity, QStringLiteral("relates_to"),
                                        bug.entity, QStringLiteral("RELATES_TO"), -1.0,
                                        QStringLiteral("consolidation"), SourceRefs{source});
            if (!result.contains(QStringLiteral("ya existe"))
                && !result.startsWith(QStringLiteral("[graph")))
                ++added;
        }
    }
    return added;
}

QString reviewRelation(const QString &cwd, const QString &subj, const QString &pred,
                       const QString &obj, double conf, const QString &prov, bool drop)
{
    const QString s = subj.trimmed(), p = pred.trimmed(), o = obj.trimmed();
    if (s.isEmpty() || p.isEmpty() || o.isEmpty())
        return QStringLiteral("[graph verify: subj/pred/obj requeridos]");

    const QString path = jsonlPath(cwd);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("[grafo vacío]");

    const QString rid = relId(entId(s), p, entId(o));
    const QString provTag = prov.trimmed().isEmpty() ? QStringLiteral("user")
                                                     : prov.trimmed();
    QByteArray out;
    bool hit = false;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.isEmpty()) continue;
        QJsonObject ro = QJsonDocument::fromJson(l).object();
        if (ro.value(QStringLiteral("kind")).toString() == QLatin1String("relation")
            && ro.value(QStringLiteral("id")).toString() == rid) {
            hit = true;
            if (drop) continue;   // tachar: no lo re-escribimos
            ro.insert(QStringLiteral("conf"), confVal(conf));
            ro.insert(QStringLiteral("prov"), provTag);
            out += QJsonDocument(ro).toJson(QJsonDocument::Compact);
            out += '\n';
            continue;
        }
        out += l;
        out += '\n';
    }
    f.close();
    if (!hit)
        return QStringLiteral("[graph verify: no existe el edge %1 -[%2]-> %3]").arg(s, p, o);

    QFile w(path);
    if (!w.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return QStringLiteral("[graph verify: no se pudo reescribir %1]").arg(path);
    w.write(out);
    w.close();
    if (drop)
        return QStringLiteral("[edge tachado · %1 -[%2]-> %3]").arg(s, p, o);
    return QStringLiteral("[edge revisado · %1 -[%2]-> %3 · conf=%4 prov=%5]")
        .arg(s, p, o).arg(conf < 0 ? QStringLiteral("null") : QString::number(conf, 'g', 2),
                          provTag);
}

QString addBatch(const QString &cwd,
                 const QVector<QPair<QString, QString>> &entities,
                 const QVector<Triple> &relations,
                 int *addedEnt, int *addedRel, const QString &prov, double conf)
{
    const QString path = jsonlPath(cwd);
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString provTag = prov.trimmed().isEmpty() ? QStringLiteral("indexer")
                                                     : prov.trimmed();

    // 1. Una sola lectura: junta los ids de entidades/relaciones ya presentes.
    QSet<QString> haveEnt, haveRel;
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!f.atEnd()) {
                const QByteArray l = f.readLine().trimmed();
                if (l.isEmpty()) continue;
                const QJsonObject o = QJsonDocument::fromJson(l).object();
                const QString k = o.value(QStringLiteral("kind")).toString();
                if (k == QLatin1String("entity"))
                    haveEnt.insert(o.value(QStringLiteral("id")).toString());
                else if (k == QLatin1String("relation"))
                    haveRel.insert(o.value(QStringLiteral("id")).toString());
            }
            f.close();
        }
    }

    // 2. Un solo Append con todo lo nuevo (dedupe contra lo existente y dentro
    //    del propio lote vía los sets, que vamos engordando a medida que escribimos).
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return QStringLiteral("[graph batch: no se pudo abrir %1]").arg(path);

    const QString ts = QDateTime::currentDateTime().toString(Qt::ISODate);
    int nE = 0, nR = 0;
    auto writeEnt = [&](const QString &name, const QString &etype) {
        const QString nm = name.trimmed();
        if (nm.isEmpty()) return;
        const QString id = entId(nm);
        if (haveEnt.contains(id)) return;
        haveEnt.insert(id);
        const QJsonObject o{
            {QStringLiteral("kind"), QStringLiteral("entity")},
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), nm},
            {QStringLiteral("etype"), normType(etype)},
            {QStringLiteral("ts"), ts}};
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.write("\n");
        ++nE;
    };
    for (const auto &e : entities) writeEnt(e.first, e.second);
    for (const Triple &t : relations) {
        const QString s = t.subj.trimmed(), p = t.pred.trimmed(), o = t.obj.trimmed();
        if (s.isEmpty() || p.isEmpty() || o.isEmpty()) continue;
        writeEnt(s, QString());   // auto-crea las entidades referidas (dedupe interno)
        writeEnt(o, QString());
        const QString sid = entId(s), oid = entId(o), rid = relId(sid, p, oid);
        if (haveRel.contains(rid)) continue;
        haveRel.insert(rid);
        QJsonObject ro{
            {QStringLiteral("kind"), QStringLiteral("relation")},
            {QStringLiteral("id"), rid},
            {QStringLiteral("subj"), sid},
            {QStringLiteral("pred"), norm(p)},
            {QStringLiteral("obj"), oid},
            {QStringLiteral("etype"), normEdge(QString(), p)},
            {QStringLiteral("conf"), confVal(conf)},
            {QStringLiteral("prov"), provTag},
            {QStringLiteral("ts"), ts}};
        const QJsonArray evidence = sourceArray(t.sources);
        if (!evidence.isEmpty()) ro.insert(QStringLiteral("sources"), evidence);
        f.write(QJsonDocument(ro).toJson(QJsonDocument::Compact));
        f.write("\n");
        ++nR;
    }
    f.close();

    if (addedEnt) *addedEnt = nE;
    if (addedRel) *addedRel = nR;
    return QStringLiteral("[graph batch: +%1 entidades, +%2 relaciones]").arg(nE).arg(nR);
}

int removeRelationsBySubject(const QString &cwd, const QString &subjName)
{
    const QString nm = subjName.trimmed();
    if (nm.isEmpty()) return 0;
    const QString path = jsonlPath(cwd);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;

    const QString sid = entId(nm);
    QByteArray kept;
    int removed = 0;
    while (!f.atEnd()) {
        const QByteArray raw = f.readLine();
        const QByteArray l = raw.trimmed();
        if (l.isEmpty()) continue;
        const QJsonObject o = QJsonDocument::fromJson(l).object();
        if (o.value(QStringLiteral("kind")).toString() == QLatin1String("relation")
            && o.value(QStringLiteral("subj")).toString() == sid) {
            ++removed;
            continue;   // dropear
        }
        kept += l;
        kept += '\n';
    }
    f.close();
    if (removed == 0) return 0;   // nada que reescribir

    QFile w(path);
    if (!w.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return 0;
    w.write(kept);
    w.close();
    return removed;
}

QStringList entityNames(const QString &cwd, const QString &etype)
{
    QStringList out;
    QFile f(jsonlPath(cwd));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    const QString want = norm(etype);
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.isEmpty()) continue;
        const QJsonObject o = QJsonDocument::fromJson(l).object();
        if (o.value(QStringLiteral("kind")).toString() != QLatin1String("entity"))
            continue;
        if (!want.isEmpty() && o.value(QStringLiteral("etype")).toString() != want)
            continue;
        out << o.value(QStringLiteral("name")).toString();
    }
    f.close();
    return out;
}

QString query(const QString &cwd, const QString &name, int depth)
{
    const QJsonObject packet = queryPacket(cwd, name, depth);
    if (!packet.value(QStringLiteral("ok")).toBool())
        return QStringLiteral("[graph query: %1]")
            .arg(packet.value(QStringLiteral("error")).toString());

    const QJsonArray edgeArray = packet.value(QStringLiteral("edges")).toArray();
    if (edgeArray.isEmpty())
        return QStringLiteral("[entidad '%1' sin relaciones]").arg(name.trimmed());

    QStringList lines;
    for (const QJsonValue &v : edgeArray) {
        const QJsonObject e = v.toObject();
        QString line = QStringLiteral("- %1 -[%2]-> %3%4")
            .arg(e.value(QStringLiteral("subj")).toString(),
                 e.value(QStringLiteral("etype")).toString(),
                 e.value(QStringLiteral("obj")).toString(),
                 e.value(QStringLiteral("incoming")).toBool()
                     ? QStringLiteral(" (entrante)") : QString());
        if (e.value(QStringLiteral("status")).toString() == QLatin1String("unreviewed"))
            line += QStringLiteral(" [unreviewed·%1]")
                .arg(e.value(QStringLiteral("prov")).toString(QStringLiteral("llm")));
        else
            line += QStringLiteral(" [conf=%1·%2]")
                .arg(e.value(QStringLiteral("conf")).toDouble(), 0, 'g', 2)
                .arg(e.value(QStringLiteral("prov")).toString(QStringLiteral("indexer")));
        const QStringList citations = [&]() {
            QStringList out;
            for (const QJsonValue &c : e.value(QStringLiteral("citations")).toArray())
                out << c.toString();
            return out;
        }();
        if (!citations.isEmpty()) line += QStringLiteral(" · fuente: ") + citations.join(", ");
        lines << line;
    }
    return QStringLiteral("Vecindario de '%1' (depth=%2):\n").arg(name.trimmed())
           .arg(packet.value(QStringLiteral("depth")).toInt())
           + lines.join(QLatin1Char('\n'));
}

QJsonObject queryPacket(const QString &cwd, const QString &name, int depth)
{
    if (depth <= 0) depth = 1;
    depth = qBound(1, depth, 3);
    const QString nm = name.trimmed();
    if (nm.isEmpty()) return {{QStringLiteral("ok"), false},
                              {QStringLiteral("error"), QStringLiteral("'name' vacío")}};

    QFile f(jsonlPath(cwd));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("grafo vacío")}};

    struct Rel {
        QString id, subj, pred, obj, etype, prov;
        double conf = -1.0;
        SourceRefs sources;
    };
    QHash<QString, QString> idToName;
    QHash<QString, QString> idToType;
    QVector<Rel> rels;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.isEmpty()) continue;
        const QJsonObject o = QJsonDocument::fromJson(l).object();
        const QString kind = o.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("entity")) {
            const QString id = o.value(QStringLiteral("id")).toString();
            idToName.insert(id, o.value(QStringLiteral("name")).toString());
            idToType.insert(id, o.value(QStringLiteral("etype")).toString());
        } else if (kind == QLatin1String("relation")) {
            const QString pred = o.value(QStringLiteral("pred")).toString();
            QString etype = o.value(QStringLiteral("etype")).toString();
            if (etype.isEmpty()) etype = normEdge(QString(), pred);
            const QJsonValue cv = o.value(QStringLiteral("conf"));
            rels.append({o.value(QStringLiteral("id")).toString(),
                         o.value(QStringLiteral("subj")).toString(), pred,
                         o.value(QStringLiteral("obj")).toString(), etype,
                         o.value(QStringLiteral("prov")).toString(),
                         cv.isDouble() ? cv.toDouble() : -1.0, readSources(o)});
        }
    }
    f.close();

    const QString startId = entId(nm);
    if (!idToName.contains(startId))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("no existe la entidad '%1'").arg(nm)}};

    QSet<QString> frontier{startId}, visited{startId}, seenEdges;
    QVector<QPair<double, QJsonObject>> selected;
    QSet<QString> sourceKeys;
    QJsonArray sourceArrayOut;
    QJsonArray nodes;
    auto addNode = [&](const QString &id) {
        if (!idToName.contains(id)) return;
        nodes.append(QJsonObject{{QStringLiteral("id"), id},
                                 {QStringLiteral("name"), idToName.value(id)},
                                 {QStringLiteral("etype"), idToType.value(id)}});
    };
    addNode(startId);

    for (int d = 0; d < depth; ++d) {
        QSet<QString> next;
        for (const Rel &r : rels) {
            QString other;
            bool outgoing = false;
            if (frontier.contains(r.subj)) { other = r.obj; outgoing = true; }
            else if (frontier.contains(r.obj)) { other = r.subj; }
            else continue;

            if (!seenEdges.contains(r.id.isEmpty() ? r.subj + r.pred + r.obj : r.id)) {
                const QString edgeId = r.id.isEmpty() ? r.subj + r.pred + r.obj : r.id;
                seenEdges.insert(edgeId);
                QJsonObject edge{
                    {QStringLiteral("id"), edgeId},
                    {QStringLiteral("subj"), idToName.value(r.subj, r.subj)},
                    {QStringLiteral("obj"), idToName.value(r.obj, r.obj)},
                    {QStringLiteral("pred"), r.pred},
                    {QStringLiteral("etype"), r.etype},
                    {QStringLiteral("prov"), r.prov.isEmpty() ? QStringLiteral("llm") : r.prov},
                    {QStringLiteral("conf"), r.conf},
                    {QStringLiteral("status"), r.conf < 0.0 ? QStringLiteral("unreviewed")
                                                               : QStringLiteral("verified")},
                    {QStringLiteral("incoming"), !outgoing}};
                QJsonArray refs;
                QJsonArray citations;
                for (const SourceRef &source : r.sources) {
                    const QJsonObject sourceJson = source.toJson();
                    if (sourceJson.isEmpty()) continue;
                    refs.append(sourceJson);
                    const QString key = sourceKey(source);
                    if (!sourceKeys.contains(key)) {
                        sourceKeys.insert(key);
                        sourceArrayOut.append(sourceJson);
                    }
                    if (!source.path.trimmed().isEmpty()) {
                        QString citation = source.path.trimmed();
                        if (source.startLine > 0) {
                            citation += QStringLiteral(":%1").arg(source.startLine);
                            if (source.endLine > source.startLine)
                                citation += QStringLiteral("-%1").arg(source.endLine);
                        }
                        if (!citations.contains(citation)) citations.append(citation);
                    }
                }
                if (!refs.isEmpty()) edge.insert(QStringLiteral("sources"), refs);
                if (!citations.isEmpty()) edge.insert(QStringLiteral("citations"), citations);
                selected.append({r.conf, edge});
            }
            if (!visited.contains(other)) {
                next.insert(other);
                visited.insert(other);
                addNode(other);
            }
        }
        frontier = next;
        if (frontier.isEmpty()) break;
    }

    std::stable_sort(selected.begin(), selected.end(), [](const auto &a, const auto &b) {
        const double ca = a.first < 0.0 ? -1.0 : a.first;
        const double cb = b.first < 0.0 ? -1.0 : b.first;
        return ca > cb;
    });
    QJsonArray edges;
    for (const auto &entry : selected) edges.append(entry.second);
    const int unreviewedEdges = std::count_if(
        selected.cbegin(), selected.cend(), [](const auto &e) { return e.first < 0.0; });
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("name"), nm},
            {QStringLiteral("depth"), depth},
            {QStringLiteral("nodes"), nodes},
            {QStringLiteral("edges"), edges},
            {QStringLiteral("sources"), sourceArrayOut},
            {QStringLiteral("receipt"), QJsonObject{
                {QStringLiteral("schemaVersion"), 1},
                {QStringLiteral("edgeCount"), edges.size()},
                {QStringLiteral("sourceCount"), sourceArrayOut.size()},
             {QStringLiteral("unreviewedEdges"), unreviewedEdges}}}};
}

QJsonObject doctor(const QString &cwd)
{
    QFile f(jsonlPath(cwd));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {{QStringLiteral("ok"), true}, {QStringLiteral("exists"), false},
                {QStringLiteral("entities"), 0}, {QStringLiteral("relations"), 0},
                {QStringLiteral("decisions"), 0}, {QStringLiteral("issues"), QJsonArray{}}};

    QSet<QString> entities;
    QVector<QJsonObject> relations;
    int invalidLines = 0, decisions = 0, sourceRefs = 0, unreviewed = 0;
    QJsonArray issues;
    while (!f.atEnd()) {
        const QByteArray raw = f.readLine().trimmed();
        if (raw.isEmpty()) continue;
        QJsonParseError error;
        const QJsonObject o = QJsonDocument::fromJson(raw, &error).object();
        if (error.error != QJsonParseError::NoError || o.isEmpty()) {
            ++invalidLines;
            continue;
        }
        const QString kind = o.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("entity")) entities.insert(o.value(QStringLiteral("id")).toString());
        else if (kind == QLatin1String("relation")) {
            relations.append(o);
            if (!o.value(QStringLiteral("conf")).isDouble()) ++unreviewed;
            sourceRefs += o.value(QStringLiteral("sources")).toArray().size();
        } else if (kind == QLatin1String("decision")) {
            ++decisions;
        }
    }
    f.close();

    int orphanEdges = 0, staleSources = 0;
    QSet<QString> seenSourceKeys;
    for (const QJsonObject &relation : relations) {
        const QString subj = relation.value(QStringLiteral("subj")).toString();
        const QString obj = relation.value(QStringLiteral("obj")).toString();
        if (!entities.contains(subj) || !entities.contains(obj)) {
            ++orphanEdges;
            if (issues.size() < 20)
                issues.append(QStringLiteral("orphan relation %1")
                             .arg(relation.value(QStringLiteral("id")).toString()));
        }
        for (const QJsonValue &v : relation.value(QStringLiteral("sources")).toArray()) {
            const SourceRef source = SourceRef::fromJson(v.toObject());
            const QString key = sourceKey(source);
            if (seenSourceKeys.contains(key)) continue;
            seenSourceKeys.insert(key);
            if (source.path.isEmpty() || source.sha256.isEmpty()) continue;
            const QString abs = QDir(cwd).absoluteFilePath(source.path);
            QFile sourceFile(abs);
            // CodeGraphIndexer hashea los bytes que leyó del archivo; conservar
            // modo binario acá hace que el SHA sea estable también en Windows,
            // donde Text puede convertir CRLF y producir un falso stale.
            if (!sourceFile.open(QIODevice::ReadOnly)) continue;
            const QString actual = QString::fromLatin1(
                QCryptographicHash::hash(sourceFile.readAll(), QCryptographicHash::Sha256).toHex());
            if (actual != source.sha256) {
                ++staleSources;
                if (issues.size() < 20)
                    issues.append(QStringLiteral("stale source %1").arg(source.path));
            }
        }
    }
    if (invalidLines > 0 && issues.size() < 20)
        issues.append(QStringLiteral("invalid JSONL lines: %1").arg(invalidLines));

    return {{QStringLiteral("ok"), true}, {QStringLiteral("exists"), true},
            {QStringLiteral("entities"), entities.size()},
            {QStringLiteral("relations"), relations.size()},
            {QStringLiteral("decisions"), decisions},
            {QStringLiteral("unreviewedEdges"), unreviewed},
            {QStringLiteral("sourceRefs"), sourceRefs},
            {QStringLiteral("orphanEdges"), orphanEdges},
            {QStringLiteral("staleSources"), staleSources},
            {QStringLiteral("invalidLines"), invalidLines},
            {QStringLiteral("healthy"), invalidLines == 0 && orphanEdges == 0 && staleSources == 0},
            {QStringLiteral("issues"), issues}};
}

QString decide(const QString &cwd, const QString &topic, const QString &chosen,
               const Rejected &rejected, const QString &reason)
{
    const QString tp = topic.trimmed(), ch = chosen.trimmed();
    if (tp.isEmpty() || ch.isEmpty())
        return QStringLiteral("[graph decide: 'topic' y 'chosen' requeridos]");

    const QString path = jsonlPath(cwd);
    QDir().mkpath(QFileInfo(path).absolutePath());

    // id estable por tema: re-decidir el mismo tema crea una entrada nueva
    // (el log es append-only/inmutable), pero comparten prefijo para agrupar.
    const QByteArray h = QCryptographicHash::hash(
        (norm(tp) + QLatin1Char('|') + QDateTime::currentDateTime().toString(Qt::ISODate)).toUtf8(),
        QCryptographicHash::Sha1);
    const QString id = QStringLiteral("d_") + QString::fromLatin1(h.toHex().left(8));

    QJsonArray rej;
    for (const auto &r : rejected) {
        if (r.first.trimmed().isEmpty()) continue;
        rej.append(QJsonObject{
            {QStringLiteral("alt"), r.first.trimmed()},
            {QStringLiteral("reason"), r.second.trimmed()}});
    }

    appendObj(path, QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("decision")},
        {QStringLiteral("id"), id},
        {QStringLiteral("topic"), tp},
        {QStringLiteral("chosen"), ch},
        {QStringLiteral("reason"), reason.trimmed()},
        {QStringLiteral("rejected"), rej},
        {QStringLiteral("ts"), QDateTime::currentDateTime().toString(Qt::ISODate)}});
    return QStringLiteral("[decisión registrada · id=%1 '%2' → %3 (%4 rechazada/s)]")
        .arg(id, tp, ch).arg(rej.size());
}

QString decisions(const QString &cwd, const QString &topic)
{
    QFile f(jsonlPath(cwd));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("[sin decisiones registradas]");

    const QString filt = norm(topic);
    QStringList blocks;
    while (!f.atEnd()) {
        const QByteArray l = f.readLine().trimmed();
        if (l.isEmpty()) continue;
        const QJsonObject o = QJsonDocument::fromJson(l).object();
        if (o.value(QStringLiteral("kind")).toString() != QLatin1String("decision"))
            continue;
        const QString tp = o.value(QStringLiteral("topic")).toString();
        if (!filt.isEmpty() && !norm(tp).contains(filt)) continue;

        QString b = QStringLiteral("### %1\n- elegido: %2")
            .arg(tp, o.value(QStringLiteral("chosen")).toString());
        const QString rs = o.value(QStringLiteral("reason")).toString();
        if (!rs.isEmpty()) b += QStringLiteral("\n- motivo: %1").arg(rs);
        const QJsonArray rej = o.value(QStringLiteral("rejected")).toArray();
        for (const QJsonValue &v : rej) {
            const QJsonObject ro = v.toObject();
            const QString rr = ro.value(QStringLiteral("reason")).toString();
            b += QStringLiteral("\n- ✗ rechazado: %1%2")
                .arg(ro.value(QStringLiteral("alt")).toString(),
                     rr.isEmpty() ? QString() : QStringLiteral(" — ") + rr);
        }
        b += QStringLiteral("\n- ts: %1").arg(o.value(QStringLiteral("ts")).toString());
        blocks << b;
    }
    f.close();

    if (blocks.isEmpty())
        return filt.isEmpty() ? QStringLiteral("[sin decisiones registradas]")
                              : QStringLiteral("[sin decisiones para '%1']").arg(topic.trimmed());
    return QStringLiteral("Decisiones (%1):\n").arg(blocks.size()) + blocks.join(QStringLiteral("\n\n"));
}

}  // namespace GraphStore
