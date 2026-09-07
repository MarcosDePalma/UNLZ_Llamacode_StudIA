// Probe E2E opt-in del matcher visual. Abre una ventana Qt propia, captura una
// plantilla real al DPI del monitor elegido y la busca en foreground. Sólo mueve
// el mouse/clickea con --execute-click; nunca se registra en ctest.
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>

#include "core/automation/DesktopAutomationBackend.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("execute-click"), QStringLiteral("Ejecuta el clic real sobre la ventana del probe.")});
    parser.addOption({QStringLiteral("screen"), QStringLiteral("Índice de monitor para QA DPI/multimonitor."),
                      QStringLiteral("index"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("matrix"),
                      QStringLiteral("Ejecuta temas claro/oscuro y dos objetivos en todos los monitores.")});
    parser.process(app);

    QWidget window;
    window.setWindowTitle(QStringLiteral("LlamaCode Visual QA Probe"));
    window.resize(640, 380);
    auto *layout = new QVBoxLayout(&window);
    auto *primary = new QPushButton(QStringLiteral("◆ OBJETIVO VISUAL 7429 ◆"), &window);
    auto *secondary = new QPushButton(QStringLiteral("◉ ACCIÓN GRÁFICA 5183 ◉"), &window);
    primary->setFixedSize(260, 72);
    secondary->setFixedSize(230, 58);
    layout->addStretch();
    layout->addWidget(primary, 0, Qt::AlignCenter);
    layout->addWidget(secondary, 0, Qt::AlignCenter);
    layout->addStretch();
    const QList<QScreen *> screens = QGuiApplication::screens();
    const int screenIndex = screens.isEmpty() ? 0
        : qBound(0, parser.value(QStringLiteral("screen")).toInt(), screens.size() - 1);
    if (!screens.isEmpty()) {
        const QRect area = screens.at(screenIndex)->availableGeometry();
        window.move(area.center() - QPoint(window.width() / 2, window.height() / 2));
    }
    int primaryClicks = 0, secondaryClicks = 0;
    QObject::connect(primary, &QPushButton::clicked, [&]() { ++primaryClicks; });
    QObject::connect(secondary, &QPushButton::clicked, [&]() { ++secondaryClicks; });
    window.show();

    QTemporaryDir temp;
    struct ProbeCase { int screen = 0; bool dark = true; QPushButton *target = nullptr; QString name; };
    QList<ProbeCase> cases;
    const bool matrix = parser.isSet(QStringLiteral("matrix"));
    const int firstScreen = screens.isEmpty() ? 0 : screenIndex;
    const int screenCount = matrix ? qMax(1, screens.size()) : 1;
    for (int s = 0; s < screenCount; ++s) {
        const int actual = matrix ? s : firstScreen;
        cases << ProbeCase{actual, true, primary, QStringLiteral("dark-primary")};
        if (matrix) {
            cases << ProbeCase{actual, true, secondary, QStringLiteral("dark-secondary")}
                  << ProbeCase{actual, false, primary, QStringLiteral("light-primary")}
                  << ProbeCase{actual, false, secondary, QStringLiteral("light-secondary")};
        }
    }

    auto index = std::make_shared<int>(0);
    auto failures = std::make_shared<int>(0);
    auto runNext = std::make_shared<std::function<void()>>();
    *runNext = [&, index, failures, runNext]() {
        if (*index >= cases.size()) {
            QTextStream(stdout) << "summary cases=" << cases.size()
                                << " failures=" << *failures << Qt::endl;
            return app.exit(*failures == 0 ? 0 : 2);
        }
        const ProbeCase current = cases.at((*index)++);
        const QString primaryStyle = current.dark
            ? QStringLiteral("QPushButton { color:white; background:#3157c8; border:5px solid #f2b84b; border-radius:13px; font:700 14px 'Segoe UI'; }")
            : QStringLiteral("QPushButton { color:#182034; background:#dce8ff; border:5px solid #3157c8; border-radius:13px; font:700 14px 'Segoe UI'; }");
        const QString secondaryStyle = current.dark
            ? QStringLiteral("QPushButton { color:#fff; background:#7b2f91; border:4px dashed #62dfc7; border-radius:5px; font:700 13px 'Segoe UI'; }")
            : QStringLiteral("QPushButton { color:#3d1748; background:#f2d8fa; border:4px dashed #267c6d; border-radius:5px; font:700 13px 'Segoe UI'; }");
        primary->setStyleSheet(primaryStyle);
        secondary->setStyleSheet(secondaryStyle);
        if (!screens.isEmpty()) {
            const QRect area = screens.at(current.screen)->availableGeometry();
            window.move(area.center() - QPoint(window.width() / 2, window.height() / 2));
        }
        window.raise();
        QTimer::singleShot(350, [&, current, failures, runNext]() {
        QString targetId;
        for (const QVariant &v : DesktopAutomationBackend::windows()) {
            const QVariantMap row = v.toMap();
            if (row.value(QStringLiteral("label")).toString().contains(window.windowTitle())) {
                targetId = row.value(QStringLiteral("id")).toString(); break;
            }
        }
        QString error;
        const QString needlePath = temp.filePath(current.name + QStringLiteral("-%1.png").arg(current.screen));
        const QImage full = DesktopAutomationBackend::capture(
            QStringLiteral("window"), targetId, &error);
        const double dpr = screens.isEmpty() ? 1.0 : screens.at(current.screen)->devicePixelRatio();
        const QPoint relative = current.target->mapToGlobal(QPoint()) - window.frameGeometry().topLeft();
        // Igual que Teach, conservar contexto alrededor del objetivo. Recortar el
        // interior exacto de un control plano produce plantillas deliberadamente
        // ambiguas (varias franjas internas son visualmente idénticas).
        const int padding = qRound(36 * dpr);
        const QRect buttonRect(qRound(relative.x() * dpr) - padding,
                               qRound(relative.y() * dpr) - padding,
                               qRound(current.target->width() * dpr) + 2 * padding,
                               qRound(current.target->height() * dpr) + 2 * padding);
        full.copy(buttonRect.intersected(full.rect())).save(needlePath, "PNG");
        const QVariantMap match = DesktopAutomationBackend::findImage(
            QStringLiteral("window"), targetId, needlePath, 0.86, 1.0, 1.0, true, &error);
        QTextStream out(stdout);
        out << "case=" << current.name << " screen=" << current.screen
            << " dpr=" << dpr
            << " target=" << targetId << " match="
            << QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(match)).toJson(QJsonDocument::Compact))
            << " error=" << error << Qt::endl;
        if (!match.value(QStringLiteral("found")).toBool() || match.value(QStringLiteral("ambiguous")).toBool()) {
            ++*failures;
            return QTimer::singleShot(0, *runNext);
        }
        if (!parser.isSet(QStringLiteral("execute-click")))
            return QTimer::singleShot(0, *runNext);
        const int clicksBefore = current.target == primary ? primaryClicks : secondaryClicks;
        QVariantMap trace;
        if (!DesktopAutomationBackend::clickImage(QStringLiteral("window"), targetId,
                needlePath, 0.86, 1.0, 1.0, QStringLiteral("left"), &error, &trace)) {
            qWarning().noquote() << error;
            ++*failures;
            return QTimer::singleShot(0, *runNext);
        }
        QTimer::singleShot(250, [&, current, clicksBefore, failures, runNext]() {
            const int clicksAfter = current.target == primary ? primaryClicks : secondaryClicks;
            if (clicksAfter != clicksBefore + 1) ++*failures;
            (*runNext)();
        });
        });
    };
    QTimer::singleShot(800, *runNext);
    return app.exec();
}
