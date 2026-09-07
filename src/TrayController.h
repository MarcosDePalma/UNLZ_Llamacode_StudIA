#pragma once

#include <QIcon>
#include <QObject>

class QMenu;
class QAction;
class QSystemTrayIcon;

class TrayController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibleChanged)
public:
    explicit TrayController(const QIcon &icon, QObject *parent = nullptr);
    ~TrayController() override;

    bool isVisible() const;
    void setVisible(bool visible);

    Q_INVOKABLE void setTeachState(const QString &state);
    Q_INVOKABLE void setMenuTexts(const QString &open, const QString &pause,
                                  const QString &resume, const QString &finish,
                                  const QString &cancel, const QString &quit);

signals:
    void visibleChanged();
    void openRequested();
    void quitRequested();
    void pauseTeachRequested(bool paused);
    void finishTeachRequested();
    void cancelTeachRequested();

private:
    void rebuildMenu();

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QAction *m_finishAction = nullptr;
    QAction *m_cancelAction = nullptr;
    QAction *m_quitAction = nullptr;
    QString m_teachState;
    QString m_openText = QStringLiteral("Abrir LlamaCode");
    QString m_pauseText = QStringLiteral("Pausar Teach");
    QString m_resumeText = QStringLiteral("Continuar Teach");
    QString m_finishText = QStringLiteral("Finalizar Teach");
    QString m_cancelText = QStringLiteral("Cancelar Teach");
    QString m_quitText = QStringLiteral("Salir");
};
