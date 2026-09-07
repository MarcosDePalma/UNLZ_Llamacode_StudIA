#pragma once
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVector>

// Memoria PERSISTENTE por capas para el agente. Hechos atómicos con metadata,
// guardados como JSONL en <cwd>/.llamacode/memory.jsonl (un objeto por línea).
// Convive con el viejo memory.md (append-only) que sigue manejando la tool
// 'memory' para back-compat; este store es la capa estructurada (scope/type/
// confidence + recall por relevancia).
//
// Capas (scope):
//   - "session"  : contexto de la sesión actual (volátil semánticamente).
//   - "project"  : reglas, arquitectura, decisiones, paths, bugs del repo.
//   - "personal" : preferencias/estilo del usuario (transversal a proyectos).
// type: preference | decision | fact | bug | skill | other. `skill` representa
// un procedimiento reutilizable aprendido tras resolver una dificultad real.
namespace MemoryStore {

// Resultado compartido por la tool `verify_claims` y el gate de consolidación.
// coverage es la proporción de términos de la afirmación encontrados en un
// único fragmento del repo o de la memoria estructurada.
struct ClaimEvidence {
    QString claim;
    QString status;       // accredited | partial | unaccredited
    QString where;
    double coverage = 0.0;
};

// Ruta del JSONL estructurado para un cwd dado.
QString jsonlPath(const QString &cwd);
// Ruta global para hechos `personal`, compartida entre proyectos del usuario.
QString personalJsonlPath();

// Guarda un hecho atómico. 'source' = PROVENANCE (de dónde salió el hecho:
// nombre de sesión/tarea, archivo, "user", etc.); opcional. Cada hecho recibe
// un 'id' corto estable. Devuelve un mensaje de estado para la tool.
QString save(const QString &cwd, const QString &content, const QString &scope,
             const QString &type, double confidence, const QString &source,
             double importance = 0.0, double surprise = 0.0,
             const QString &verification = QString(),
             const QString &supersedes = QString());

// Igual que recall(), pero conserva la metadata estructurada y el score de
// selección para que los ensambladores puedan separar decisiones vigentes de
// hechos de apoyo sin volver a parsear Markdown.
QJsonArray recallFacts(const QString &cwd, const QString &query,
                       const QString &scope, int k);

// Recupera hechos NO obsoletos. Si query != "", rankea por solapamiento de
// keywords + sesgo por confianza y recencia; si scope != "", filtra por capa.
// Devuelve top-k formateado (markdown) con id y provenance.
QString recall(const QString &cwd, const QString &query, const QString &scope, int k);

// Verifica afirmaciones contra el repo y la memoria. root puede limitar la
// búsqueda a un subdirectorio del proyecto; vacío usa cwd.
QVector<ClaimEvidence> verifyClaims(const QString &cwd, const QStringList &claims,
                                    const QString &root = QString(), int maxFiles = 8000);

// OLVIDO: marca como obsoletos (o borra) los hechos que matchean 'query'
// (keywords sobre content) y/o 'scope'. mode='stale' (default, conserva
// historial) o 'delete' (reescribe el JSONL sin ellos). Devuelve estado.
QString forget(const QString &cwd, const QString &query, const QString &scope,
               const QString &mode);

// PODA anti-bloat (penaliza el peso, no sólo premia guardar). Inspirado en el
// gate MDL de "Self-Revising Discovery Systems": un hecho se conserva sólo si su
// VALOR (confianza · recencia · tipo) compensa su COSTO (largo en chars) y no es
// redundante con otro mejor. Marca stale (default) o borra (mode='delete') los
// hechos de menor valor por encima de 'maxKeep' y los casi-duplicados. Con
// dryRun=true sólo reporta sin tocar nada. scope opcional acota la capa.
QString prune(const QString &cwd, const QString &scope, int maxKeep,
              const QString &mode, bool dryRun);

// DECAIMIENTO conservador: marca stale hechos viejos, de bajo valor y no
// protegidos por verificación/importancia/uso. dryRun sólo informa candidatos.
QString decay(const QString &cwd, const QString &scope, int maxAgeDays = 90,
              double minValue = 0.28, bool dryRun = false);

// Mantenimiento automático acotado por tiempo. Se ejecuta como máximo una vez
// por intervalo para que el recall no convierta cada consulta en una reescritura.
QString maintain(const QString &cwd, const QString &scope = QString(),
                 int intervalHours = 24);

}  // namespace MemoryStore
