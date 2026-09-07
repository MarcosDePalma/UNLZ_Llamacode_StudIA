#pragma once
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QPair>

// KNOWLEDGE GRAPH liviano para el agente. Entidades + relaciones tipadas,
// persistidas como JSONL en <cwd>/.llamacode/graph.jsonl (un objeto por línea).
// Complementa la memoria atómica de [[MemoryStore]]: ahí guardamos hechos sueltos,
// acá guardamos CÓMO se conectan (entidad -[predicado]-> entidad).
//
// Cada línea es uno de tres tipos:
//   {"kind":"entity","id":..,"name":..,"etype":..,"ts":..}
//   {"kind":"relation","id":..,"subj":<entId>,"pred":..,"obj":<entId>,"ts":..}
//   {"kind":"decision","id":..,"topic":..,"chosen":..,"reason":..,
//        "rejected":[{"alt":..,"reason":..}],"ts":..}
// Entidades se identifican por nombre NORMALIZADO (lowercase/trim) → id estable.
// etype: file|module|decision|bug|person|concept|other.
//
// El log de DECISIONES (decide/decisions) preserva las alternativas RECHAZADAS
// como artefactos de primera clase (con su motivo), no las descarta: deja un
// audit trail de qué se consideró y por qué se descartó. Idea de
// "Self-Revising Discovery Systems" (rejected alternatives como artefactos).
namespace GraphStore {

// Evidencia concreta que respalda una relación. Todos los campos salvo path
// son opcionales para conservar compatibilidad con grafos viejos y con
// relaciones inferidas que todavía no tienen una fuente exacta.
struct SourceRef {
    QString path;                 // ruta relativa al cwd
    int startLine = 0;            // 1-based; 0 = desconocido
    int endLine = 0;              // 1-based; 0 = desconocido
    QString sha256;               // hash del archivo o fragmento
    QString kind;                 // code|doc|test|decision|session|other
    QString sessionId;
    QString correlationId;
    QString commit;

    QJsonObject toJson() const;
    static SourceRef fromJson(const QJsonObject &o);
};

using SourceRefs = QVector<SourceRef>;

// Una alternativa rechazada: (texto de la alternativa, motivo del descarte).
using Rejected = QVector<QPair<QString, QString>>;

QString jsonlPath(const QString &cwd);

// Crea/asegura una entidad por nombre. Devuelve estado (incluye su id).
QString addEntity(const QString &cwd, const QString &name, const QString &etype);

// Crea una relación subj -[pred]-> obj (auto-crea las entidades por nombre).
// Cada relación se clasifica en un TIPO cerrado (REQUIRES/ENABLES/IMPLEMENTS/
// DEFINES/CALLS/IMPORTS/RELATES_TO) para no confundir dependencia dura con
// asociación blanda. 'edgeType' vacío = inferir del verbo 'pred'.
// PROVENANCE + CONFIANZA: 'prov' = origen del edge (llm|indexer|user); 'conf' =
// [0,1] o <0 → null (unreviewed, NO significa incorrecto). Un edge inferido por
// el LLM entra unreviewed; el indexador determinista entra conf=1 prov=indexer.
QString link(const QString &cwd, const QString &subj, const QString &pred,
             const QString &obj, const QString &edgeType = QString(),
             double conf = -1.0, const QString &prov = QStringLiteral("llm"),
             const SourceRefs &sources = {});

// Registra una relación inferida por una tool real: módulo/directorio tocado
// -> archivo. La relación queda unreviewed y con provenance="tool"; no se
// presenta como una decisión confirmada del usuario.
QString inferToolTouch(const QString &cwd, const QString &tool,
                       const QString &path, const QString &sessionId = QString(),
                       const QString &correlationId = QString());

// Conecta decisiones y bugs consolidados cuando comparten vocabulario de
// proyecto. Devuelve cuántos edges inferidos agregó.
int inferConsolidationLinks(const QString &cwd,
                            const QVector<QPair<QString, QString>> &facts,
                            const QString &sessionId = QString(),
                            const QString &correlationId = QString());

// Una relación tipada para inserción masiva.
struct Triple {
    QString subj, pred, obj;
    SourceRefs sources;
};

// INSERCIÓN MASIVA: vuelca muchas entidades+relaciones en UNA pasada (lee el
// grafo existente una sola vez para deduplicar, después appende todo). Pensado
// para el indexador determinista [[CodeGraphIndexer]]: evita el O(N²) de llamar
// addEntity/link uno por uno (cada uno reabre y reescanea el archivo).
// 'entities' = (name, etype); las relaciones auto-crean sus entidades por nombre.
// Llena *addedEnt/*addedRel (nuevos, sin contar los ya existentes) si != nullptr.
// 'prov'/'conf' se aplican a TODAS las relaciones del lote (el indexador
// determinista pasa prov="indexer" conf=1.0: edges verificados, no inferidos).
QString addBatch(const QString &cwd,
                 const QVector<QPair<QString, QString>> &entities,
                 const QVector<Triple> &relations,
                 int *addedEnt = nullptr, int *addedRel = nullptr,
                 const QString &prov = QStringLiteral("indexer"),
                 double conf = 1.0);

// Borra (reescribe el JSONL) TODAS las relaciones cuyo SUBJ sea 'subjName'
// (entidad por nombre normalizado). Para el reindexado incremental de
// [[CodeGraphIndexer]]: limpia los edges viejos de un archivo antes de
// re-extraerlos, así un símbolo/import eliminado DESAPARECE del grafo (el resto
// es append-only). Entidades y decisiones quedan intactas. Devuelve cuántas borró.
int removeRelationsBySubject(const QString &cwd, const QString &subjName);

// Nombres de las entidades de un 'etype' dado (ej. "file"). Para que el
// indexador incremental detecte archivos borrados (entidad cuyo archivo ya no
// existe) sin re-parsear el JSONL afuera.
QStringList entityNames(const QString &cwd, const QString &etype);

// REVISIÓN de un edge existente (colaboración/auditoría): sube su 'conf' y marca
// su 'prov' (default "user") — convierte un edge unreviewed del LLM en verificado.
// Con drop=true en cambio TACHA ese edge puntual (edge equivocado). Ubica la
// relación por (subj,pred,obj) normalizados; reescribe el JSONL. Devuelve estado.
QString reviewRelation(const QString &cwd, const QString &subj, const QString &pred,
                       const QString &obj, double conf,
                       const QString &prov = QStringLiteral("user"),
                       bool drop = false);

// Consulta el vecindario de una entidad por nombre. depth=1 (default) o 2
// (graph expansion: incluye vecinos de vecinos). Devuelve markdown.
QString query(const QString &cwd, const QString &name, int depth);

// Consulta estructurada para el harness: conserva nodos, edges, fuentes y un
// recibo compacto. El formato Markdown de query() sigue siendo el camino
// compatible para modelos y sesiones existentes.
QJsonObject queryPacket(const QString &cwd, const QString &name, int depth = 1);

// Diagnóstico read-only del grafo. Detecta edges huérfanos, relaciones sin
// evidencia y fuentes cuyo hash ya no coincide con el archivo actual.
QJsonObject doctor(const QString &cwd);

// Registra una decisión: tema, opción elegida, motivo y las alternativas
// rechazadas (cada una con su propio motivo). Se conservan TODAS: el valor está
// en saber qué se descartó y por qué, para no re-evaluarlo más tarde.
QString decide(const QString &cwd, const QString &topic, const QString &chosen,
               const Rejected &rejected, const QString &reason);

// Devuelve el log de decisiones en markdown. topic vacío = todas; si no,
// filtra por substring del tema (case-insensitive). Incluye las rechazadas.
QString decisions(const QString &cwd, const QString &topic);

}  // namespace GraphStore
