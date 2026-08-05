#include "AppController.h"
#include "core/ControlApi.h"
#include "core/MermaidRenderer.h"
#include "core/studia/StudiaController.h"
#include "ThemeProvider.h"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QtMessageHandler>
#include <QPixmap>
#include <QPainter>
#include <QWindow>
#include <QWidget>
#include <QLabel>
#include <QScreen>
#include <QGuiApplication>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <shobjidl.h>
#endif

static QFile s_logFile;
static QTextStream s_logStream;

static void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const char *level = "DEBUG";
    if      (type == QtWarningMsg)  level = "WARN ";
    else if (type == QtCriticalMsg) level = "ERROR";
    else if (type == QtFatalMsg)    level = "FATAL";

    QString line = QStringLiteral("[%1] [%2] %3")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss.zzz"))
        .arg(QLatin1String(level))
        .arg(msg);

    if (s_logFile.isOpen()) {
        s_logStream << line << "\n";
        s_logStream.flush();
    }

    // Also forward to default output (visible when run from cmd)
    fprintf(type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg ? stderr : stdout,
            "%s\n", qPrintable(line));

    if (type == QtFatalMsg)
        abort();
}

int main(int argc, char *argv[])
{
    // Log file: %APPDATA%\LlamaCode\llamacode.log
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(logDir);
    s_logFile.setFileName(logDir + "/llamacode.log");
    if (s_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        s_logStream.setDevice(&s_logFile);

    qInstallMessageHandler(messageHandler);
    qDebug() << "=== LlamaCode starting ===" << QDateTime::currentDateTime().toString();

    QApplication app(argc, argv);

#ifdef Q_OS_WIN
    // Identidad de taskbar explícita: sin esto Windows no asocia el icono a la
    // ventana frameless y muestra el icono genérico (splash y app).
    SetCurrentProcessExplicitAppUserModelID(L"LlamaCode.Desktop.App");
#endif

    // Icono según build: Debug = rojo (debug_icon), Release = normal. Coincide
    // con el icono embebido en el .exe (app_icon.rc).
#ifdef LC_DEBUG_ICON
    const QString appIconSource = QStringLiteral("qrc:/assets/debug_icon.ico");
    const QIcon appIcon(QStringLiteral(":/assets/debug_icon.ico"));
#else
    const QString appIconSource = QStringLiteral("qrc:/assets/app_icon.ico");
    const QIcon appIcon(QStringLiteral(":/assets/app_icon.ico"));
#endif
    app.setWindowIcon(appIcon);
    app.setApplicationName("LlamaCode");
    app.setOrganizationName("LlamaCode");
    app.setApplicationVersion("0.1.1");

    qDebug() << "QApplication ready";

    // Splash nativo: se muestra ANTES de cargar QML y cubre el escaneo pesado de
    // arranque (runStartupScan). Se cierra cuando la ventana principal aparece.
    QPixmap splashPix(360, 160);
    splashPix.fill(QColor(0x1e, 0x1e, 0x22));
    {
        QPainter p(&splashPix);
        const QPixmap icon = appIcon.pixmap(64, 64);
        if (!icon.isNull())
            p.drawPixmap((360 - 64) / 2, 28, icon);
        p.setPen(QColor(0xe0, 0xe0, 0xe0));
        QFont f = p.font(); f.setPointSize(11); p.setFont(f);
        p.drawText(QRect(0, 104, 360, 24), Qt::AlignCenter, "UNLZ_Llamacode");
        p.setPen(QColor(0x9a, 0x9a, 0x9a));
        f.setPointSize(9); p.setFont(f);
        p.drawText(QRect(0, 128, 360, 20), Qt::AlignCenter, "Cargando…");
    }
    // Splash como QWidget frameless NORMAL (no QSplashScreen): el flag
    // Qt::SplashScreen no aplica el icono al botón de taskbar (queda genérico).
    // Una ventana frameless común sí lo usa, igual que la ventana principal.
    QWidget splash;
    splash.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    splash.setWindowIcon(appIcon);
    splash.setFixedSize(360, 160);
    splash.setAttribute(Qt::WA_DeleteOnClose, false);
    {
        QLabel *lbl = new QLabel(&splash);
        lbl->setPixmap(splashPix);
        lbl->setGeometry(0, 0, 360, 160);
    }
    if (QScreen *scr = QGuiApplication::primaryScreen()) {
        const QRect g = scr->availableGeometry();
        splash.move(g.center() - QPoint(180, 80));
    }
    splash.show();
    if (QWindow *sw = splash.windowHandle())
        sw->setIcon(appIcon);
    app.processEvents();

    AppController controller;
    ThemeProvider theme;
    MermaidRenderer mermaid;
    // StudIA: modulo propio (indice documental + asistente de estudio). No
    // depende de AppController; la URL del server se la pasa QML.
    StudiaController studia;

    qDebug() << "Controllers ready";

    // Escaneo pesado (binaries/roots/hardware/catálogo) con el splash visible.
    controller.runStartupScan();
    qDebug() << "Startup scan done";

    QQmlApplicationEngine engine;

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() {
                         qCritical() << "QML object creation failed — aborting";
                         QCoreApplication::exit(-1);
                     }, Qt::QueuedConnection);

    engine.rootContext()->setContextProperty("App", &controller);
    engine.rootContext()->setContextProperty("Theme", &theme);
    engine.rootContext()->setContextProperty("Mermaid", &mermaid);
    engine.rootContext()->setContextProperty("Studia", &studia);
    engine.rootContext()->setContextProperty("AppIconSource", appIconSource);

    // Control API headless (espejo de AppController) para tests sin GUI.
    // Puerto: env LLAMACODE_CONTROL_PORT (default 8765). 0 = desactivado. Localhost.
    {
        const QByteArray pEnv = qgetenv("LLAMACODE_CONTROL_PORT");
        const quint16 port = pEnv.isEmpty() ? 8765 : static_cast<quint16>(pEnv.toUInt());
        auto *ctl = new ControlApi(&controller, &controller);
        ctl->start(port);
    }
    engine.addImportPath(QStringLiteral("qrc:/"));

    qDebug() << "Loading Main.qml";
    engine.loadFromModule("LlamaCode", "Main");

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "No root objects — QML load failed";
        return -1;
    }

    // Cerrar el splash cuando la ventana principal se haga visible (la geometría
    // se restaura en Main.qml Component.onCompleted → visible=true / showMaximized).
    if (auto *win = qobject_cast<QWindow *>(engine.rootObjects().constFirst())) {
        // Setear el icono DIRECTO en el QWindow raíz: con ventana frameless el
        // setWindowIcon de la app no siempre llega al botón de taskbar.
        win->setIcon(appIcon);
        if (win->isVisible()) {
            win->setIcon(appIcon);
            splash.close();
        } else {
            QObject::connect(win, &QWindow::visibleChanged, &splash, [win, appIcon, &splash](bool v) {
                if (!v)
                    return;
                win->setIcon(appIcon);
                splash.close();
            });
        }
    } else {
        splash.close();
    }

    qDebug() << "QML loaded OK — entering event loop";
    int ret = app.exec();
    qDebug() << "Event loop exited with code" << ret;
    return ret;
}
