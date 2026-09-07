#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QSet>

// HotspotAnalyzer: detecta archivos "riesgosos" combinando señales de git con
// cobertura de tests. Inspirado en el hallazgo empírico (repowise/CodeScene):
// los mejores predictores de bugs NO son métricas de complejidad sino los
// "untested hotspots" (mucho churn sin test) y la "developer congestion"
// (muchos autores tocando el mismo archivo).
//
// El núcleo es PURO y unit-testeable: parseGitLog() arma el churn desde la
// salida de `git log`, y rank() puntúa una lista de entradas. analyzeRepo() es
// el driver con I/O (corre git, lee los tests) y arma el reporte.
class HotspotAnalyzer
{
public:
    // Churn por archivo: cuántos commits lo tocaron y qué autores distintos.
    struct Churn {
        QString path;
        int commits = 0;
        QSet<QString> authors;
    };

    // Resultado puntuado para un archivo.
    struct Hotspot {
        QString path;
        int commits = 0;
        int authors = 0;
        bool hasTest = false;
        int score = 0;            // 1..10 (10 = más riesgoso)
        QStringList reasons;      // motivos legibles del score
    };

    struct Options {
        int minCommits = 2;       // ignora archivos con menos commits (ruido)
        int topN = 20;            // cuántos hotspots devolver (0 = todos)
        int sinceDays = 0;        // ventana de git log (0 = todo el historial)
    };

    // --- Núcleo puro (testeable sin git ni disco) ---

    // Parsea la salida de `git log --no-merges --pretty=format:"@@%an" --name-only`.
    // Líneas que empiezan con "@@" fijan el autor del commit en curso; las demás
    // líneas no vacías son rutas de archivos tocados por ese commit. Devuelve el
    // churn agregado por archivo (1 commit contado por archivo por commit).
    static QList<Churn> parseGitLog(const QString &gitLogOutput);

    // Puntúa y rankea. `testedPaths` = rutas (relativas, '/') que SÍ tienen test.
    // Score: combina churn + congestión de autores (normalizados al máximo del
    // set); estar SIN test eleva el piso de riesgo. Filtra por minCommits y corta
    // en topN. Orden: score desc, luego commits desc, luego path.
    static QList<Hotspot> rank(const QList<Churn> &churn,
                               const QSet<QString> &testedPaths,
                               const Options &opts = {});

    // Heurística: ¿la ruta es un archivo de test? (carpeta tests/__tests__/spec,
    // o nombre con prefijo/sufijo test_/_test/.spec/.test). Usado para excluir los
    // tests del ranking y para emparejar cobertura.
    static bool isTestPath(const QString &path);

    // ¿`sourcePath` parece de código? (extensión típica). Filtra docs/assets.
    static bool isSourcePath(const QString &path);

    // Stem de un archivo: nombre sin carpeta ni extensión. "src/a/Foo.cpp"→"Foo".
    static QString stemOf(const QString &path);

    // --- Driver con I/O ---

    // Corre git en `repoPath`, deduce cobertura leyendo los archivos de test
    // (un source está "tested" si algún test menciona su stem) y devuelve el
    // ranking. Si no es un repo git o git falla, deja `error` y devuelve vacío.
    static QList<Hotspot> analyzeRepo(const QString &repoPath, const Options &opts,
                                      QString *error = nullptr);

    // Reporte de texto compacto para mostrar/loguear o devolver como tool result.
    static QString formatReport(const QList<Hotspot> &hotspots);
};
