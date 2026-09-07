#pragma once

#include <QJsonObject>
#include <QHash>
#include <QSet>
#include <QString>

// Presupuesto elástico del loop ReAct. No limita el tiempo legítimo de una tool:
// decide sólo al terminar acciones, premiando evidencia nueva y detectando
// familias semánticas repetidas aunque cambien argumentos superficiales.
class AgentProgressGovernor
{
public:
    struct Policy {
        int initialCredits = 8;
        int maxCredits = 16;
        int replanAfter = 3;
        int stopAfterReplan = 5;
        // Techo duro de archivos DISTINTOS escritos en un turno. El chequeo de
        // contenido duplicado no alcanza cuando el modelo cambia una línea en cada
        // vuelta (prime_checker.py, prime_checker_v2.py, …_v10.py): ahí cada
        // escritura es "nueva" y el presupuesto elástico nunca cierra. Ningún
        // objetivo legítimo necesita tantos archivos nuevos en un solo turno.
        int maxDistinctWrites = 24;
    };
    enum Action { Continue, Replan, Stop };
    struct Decision {
        Action action = Continue;
        bool progress = false;
        bool objectiveSatisfied = false;
        bool repeatedFailure = false;
        int credits = 0;
        int stagnant = 0;
        QString semanticKey;
        QString reason;
    };

    explicit AgentProgressGovernor(const Policy &policy = {});
    void setPolicy(const Policy &policy);
    void reset(const QString &objective = QString());
    Decision record(const QString &tool, const QString &arguments,
                    bool ok, const QString &result, bool isWrite);

    static QString semanticKey(const QString &tool, const QString &arguments);

private:
    Policy m_policy;
    int m_credits = 0;
    int m_stagnant = 0;
    int m_replans = 0;
    QSet<QString> m_evidence;
    QSet<QString> m_semanticSuccess;
    QSet<QString> m_exactWriteSuccess;
    // contenido ya escrito -> primer path donde se escribio. Re-escribir lo mismo
    // con otro nombre no es progreso: es el bucle de "prime_checker_v10.py".
    QHash<QString, QString> m_contentFirstPath;
    // Último hash escrito por ruta. Reescribir el mismo archivo con el mismo
    // contenido no es progreso; cambiarlo sí lo es.
    QHash<QString, QString> m_lastWriteContentHash;
    // Firmas (clave semántica + resultado) de acciones que ya fallaron: repetirlas
    // es el bucle clasico (list_dir ".." una y otra vez) y cuesta doble.
    QSet<QString> m_failedSignatures;
    bool m_multiLanguageRequested = false;
    QSet<QString> m_expectedArtifacts;
    QSet<QString> m_completedArtifacts;
};
