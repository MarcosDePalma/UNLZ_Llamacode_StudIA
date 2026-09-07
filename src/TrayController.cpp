#include "TrayController.h"

#include <QAction>
#include <QMenu>
#include <QSystemTrayIcon>

TrayController::TrayController(const QIcon &icon, QObject *parent)
    : QObject(parent), m_tray(new QSystemTrayIcon(icon, this)), m_menu(new QMenu)
{
    m_tray->setToolTip(QStringLiteral("UNLZ_Llamacode"));
    m_openAction = m_menu->addAction(m_openText);
    m_pauseAction = m_menu->addAction(m_pauseText);
    m_finishAction = m_menu->addAction(m_finishText);
    m_cancelAction = m_menu->addAction(m_cancelText);
    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(m_quitText);
    m_tray->setContextMenu(m_menu);

    connect(m_openAction, &QAction::triggered, this, &TrayController::openRequested);
    connect(m_quitAction, &QAction::triggered, this, &TrayController::quitRequested);
    connect(m_pauseAction, &QAction::triggered, this, [this]() {
        emit pauseTeachRequested(m_teachState != QLatin1String("paused"));
    });
    connect(m_finishAction, &QAction::triggered, this, &TrayController::finishTeachRequested);
    connect(m_cancelAction, &QAction::triggered, this, &TrayController::cancelTeachRequested);
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger
            || reason == QSystemTrayIcon::DoubleClick)
            emit openRequested();
    });
    rebuildMenu();
}

TrayController::~TrayController()
{
    m_tray->hide();
    delete m_menu;
}

bool TrayController::isVisible() const
{
    return m_tray->isVisible();
}

void TrayController::setVisible(bool visible)
{
    if (visible == isVisible()) return;
    if (visible) m_tray->show();
    else m_tray->hide();
    emit visibleChanged();
}

void TrayController::setTeachState(const QString &state)
{
    if (m_teachState == state) return;
    m_teachState = state;
    rebuildMenu();
}

void TrayController::setMenuTexts(const QString &open, const QString &pause,
                                  const QString &resume, const QString &finish,
                                  const QString &cancel, const QString &quit)
{
    m_openText = open;
    m_pauseText = pause;
    m_resumeText = resume;
    m_finishText = finish;
    m_cancelText = cancel;
    m_quitText = quit;
    rebuildMenu();
}

void TrayController::rebuildMenu()
{
    if (!m_openAction) return;
    const bool teachActive = m_teachState == QLatin1String("recording")
                             || m_teachState == QLatin1String("paused");
    m_openAction->setText(m_openText);
    m_pauseAction->setText(m_teachState == QLatin1String("paused")
                               ? m_resumeText : m_pauseText);
    m_finishAction->setText(m_finishText);
    m_cancelAction->setText(m_cancelText);
    m_quitAction->setText(m_quitText);
    m_pauseAction->setVisible(teachActive);
    m_finishAction->setVisible(teachActive);
    m_cancelAction->setVisible(teachActive);
}
