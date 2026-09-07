#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QHash>

// Detección y metadata de CLIs "maestro" (claude code / codex) usados como
// supervisor del agente local. La detección también expone capacidades; la
// ejecución pasa por ManagedAgentRunStore desde AgentToolRunner (tool ask_teacher).
class MasterCli : public QObject
{
    Q_OBJECT
public:
    explicit MasterCli(QObject *parent = nullptr) : QObject(parent) {}

    // CLIs soportados: "claude" | "codex". Lista para poblar la UI.
    Q_INVOKABLE static QStringList supported() { return {QStringLiteral("claude"),
                                                         QStringLiteral("codex")}; }

    // Estado de un CLI: {name, installed(bool), version, path, label, installCommand}.
    // Cachea el resultado por nombre; pasar force=true reejecuta la detección.
    Q_INVOKABLE QVariantMap status(const QString &name, bool force = false);

    // Comando de instalación sugerido (npm global). No instala: la UI lo muestra.
    Q_INVOKABLE static QString installCommand(const QString &name);

    // Nombre legible para la UI.
    Q_INVOKABLE static QString label(const QString &name);

    // Binario invocable resuelto en PATH ("" si no está). Reusa cache de status.
    QString resolvePath(const QString &name);

    // Limpia la cache (tras instalar, por ej.).
    Q_INVOKABLE void invalidate() { m_cache.clear(); }

private:
    QHash<QString, QVariantMap> m_cache;
};
