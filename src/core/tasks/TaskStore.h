#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>

// TaskStore — catálogo de "Tasks": macros configurables que NO son tontas. Cada
// Task guarda un objetivo en lenguaje natural (la intención) + una secuencia de
// pasos de referencia. En la ejecución el agente IA re-deriva las acciones con
// sus tools (browser MCP, shell, mail, etc.) y se adapta si cambió un botón o un
// archivo de lugar. La grabación literal queda como referencia, no como guion
// rígido.
//
// Persistencia: <AppLocalData>/tasks/tasks.json (respeta
// QStandardPaths::setTestModeEnabled para aislamiento en tests).
//
// Las funciones de (de)serialización y composición de prompt son PURAS (estáticas,
// sin disco) → unit test directo. El modelo expone CRUD a QML como BinaryRegistry.
//
// Fase 1: CRUD + ejecución manual. El campo `schedule` (cron) se persiste pero su
// disparo automático es Fase 2.
class TaskStore : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        ProfileIdRole,
        StepsRole,          // QVariantList de pasos {kind,intent,ref}
        StepCountRole,
        ScheduleEnabledRole,
        ScheduleCronRole,
        ScheduleSpecRole,   // QVariantMap del builder amigable (ver TaskSchedule)
        PermScopeRole,      // "project" | "folder" | "full"
        PermFoldersRole,    // QVariantList de rutas absolutas (scope "folder")
        CreatedAtRole,
        UpdatedAtRole,
        LastRunAtRole,
        LastRunStatusRole,  // "" | "ok" | "error" | "running"
        LastRunSummaryRole,
        PrePromptRole,
        PostPromptRole,
        SilentUnlessErrorRole,
        ExecutionModeRole,
        ApprovalPolicyRole,
        SafetyProfileRole,
        TeachArtifactIdRole,
        TeachFormatVersionRole,
        TrainedAtRole,
        ScopeKindRole,
        ScopeTargetIdRole,
        ScopeLabelRole,
        ScopeWidthRole,
        ScopeHeightRole,
        ScopeDpiRole,
        TimeoutSecRole,
        MaxActionsRole,
        MaxRetriesRole,
        AutomationStatusRole,
        LoopEnabledRole,        // bool: correr en bucle hasta cumplir el objetivo
        LoopGoalRole,           // condición de éxito en lenguaje natural
        LoopMaxIterationsRole,  // techo de iteraciones (corta el bucle sí o sí)
        LoopMaxSecondsRole,     // techo operativo de tiempo; 0 = sin límite
        VerifyProfileIdRole,    // perfil opcional para la fase de verificación/goal-check
        AutoDifficultyRoutingRole // usar el verificador sólo ante dificultad alta
    };

    // Routing multi-modelo (fase verify): perfil a usar para la verificación
    // (postprompt / goal-check del bucle). PURA. Devuelve el verifyProfileId si
    // está seteado y difiere del de ejecución; "" si no hay que cambiar de modelo.
    static QString verifyProfileFor(const QVariantMap &task, const QString &execProfileId);

    // Decisión PURA de si el bucle debe correr otra vez. Sin disco ni estado.
    // `iteration` = nº de corridas ya completadas (1-based). `lastStatus` es el
    // status final de la última corrida ("ok"/"error"/...). `lastSummary` es la
    // salida del chequeo de objetivo (puede contener el marcador GOAL_MET).
    struct LoopDecision { bool repeat; QString reason; };
    static LoopDecision decideLoop(const QVariantMap &task, int iteration,
                                   const QString &lastStatus, const QString &lastSummary,
                                   qint64 elapsedSeconds = -1);

    // Marcadores que el agente debe emitir al evaluar el objetivo del bucle.
    static const QString kGoalMetMarker;      // "GOAL_MET"
    static const QString kGoalNotMetMarker;   // "GOAL_NOT_MET"

    // Prompt PURO que pide al agente verificar si el objetivo del bucle se
    // cumplió y responder con el marcador correspondiente. Vacío si no hay loop.
    static QString composeLoopGoalPrompt(const QVariantMap &task);

    // Memoria de progreso (checkpoint) entre iteraciones del bucle. Cada iteración
    // corre en sesión LIMPIA → el agente pierde lo aprendido. Esta función toma el
    // veredicto de la verificación previa (empieza con GOAL_NOT_MET + evidencia/qué
    // ajustar), le saca el marcador y arma un preámbulo para que la próxima corrida
    // RESUMA desde ese estado en vez de re-arrancar de cero. `completedIterations` =
    // nº de corridas del cuerpo ya hechas. Vacío si no hay nota útil. PURA.
    static QString composeLoopProgress(const QString &priorVerdict, int completedIterations);

    explicit TaskStore(QObject *parent = nullptr);

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }

    // CRUD. `def` usa las claves de roleNames (name, description, profileId,
    // steps[], scheduleEnabled, scheduleCron). Devuelve el id (nuevo o existente).
    Q_INVOKABLE QString save(const QString &id, const QVariantMap &def);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE QVariantMap get(const QString &id) const;
    // Todas las Tasks como filas (para el scheduler).
    QVariantList all() const;
    Q_INVOKABLE QString duplicate(const QString &id);
    Q_INVOKABLE void refresh();
    // Marca el resultado de una corrida (actualiza lastRunAt/lastRunStatus).
    Q_INVOKABLE void markRun(const QString &id, const QString &status, const QString &summary = QString());
    void markWorkflowState(const QString &id, const QVariantMap &state);

    // Nombre → slug seguro / id. minúsculas [a-z0-9_-].
    static QString sanitize(const QString &name);

    // Compone el prompt-objetivo que recibe el agente. PURA y testeable: arma el
    // objetivo + los pasos de referencia + la consigna de adaptación.
    static QString composePrompt(const QVariantMap &task);
    static QString composePostPrompt(const QVariantMap &task);

    // (de)serialización pura (sin disco).
    static QJsonObject toJson(const QVariantMap &task);
    static QVariantMap fromJson(const QJsonObject &obj);

signals:
    void countChanged();
    void changed();

private:
    void load();
    void save() const;
    QString storagePath() const;
    int indexOfId(const QString &id) const;

    QList<QVariantMap> m_items;
};
