// veikk — set up a Veikk drawing tablet: area, pressure curve and pad buttons.
//
//   veikk            open the window
//   veikk daemon     run the background service the window configures
//   veikk status     say whether the tablet and the service are running

#include <signal.h>

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>
#include <cerrno>
#include <cstdio>

#include "backend.h"
#include "config.h"
#include "engine.h"
#include "evdev.h"
#include "theme.h"
#include "translator.h"

static QString stateDirectory() {
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    return (runtime.isEmpty() ? QDir::tempPath() : runtime) + "/veikk";
}

static volatile sig_atomic_t g_stop = 0;

static int runDaemon(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("veikk");

    struct sigaction action{};
    action.sa_handler = [](int) { g_stop = 1; };
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
    // A signal handler may only set a flag, so a timer turns it into a clean
    // quit that destroys the virtual devices and removes the state file.
    QTimer stop;
    QObject::connect(&stop, &QTimer::timeout, &app, [] { if (g_stop) QCoreApplication::quit(); });
    stop.start(200);

    Engine engine(Config::defaultPath(), stateDirectory());
    return app.exec();
}

static int printStatus(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    bool json = false;
    for (const auto &arg : app.arguments())
        json = json || arg == "--json";

    QJsonObject state;
    QFile file(stateDirectory() + "/state.json");
    if (file.open(QIODevice::ReadOnly))
        state = QJsonDocument::fromJson(file.readAll()).object();
    const qint64 pid = state.value("pid").toInteger();
    const bool running = pid > 0 && (kill(pid_t(pid), 0) == 0 || errno == EPERM);

    QJsonArray devices;
    for (const auto &device : Evdev::scan())
        devices.append(QJsonObject{{"name", device.name}, {"path", device.path}, {"pen", device.pen},
                                   {"keypad", device.keypad}, {"virtual", device.virtualDevice}});
    const QJsonObject out{{"daemonRunning", running}, {"problem", running ? state.value("problem") : QJsonValue("")},
                          {"devices", devices}};
    if (json) {
        std::puts(QJsonDocument(out).toJson(QJsonDocument::Compact).constData());
    } else {
        std::printf("daemon: %s\n", running ? "running" : "not running");
        for (const auto &d : devices)
            std::printf("device: %s (%s)\n", d.toObject().value("name").toString().toLocal8Bit().constData(),
                        d.toObject().value("path").toString().toLocal8Bit().constData());
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (command == "daemon")
        return runDaemon(argc, argv);
    if (command == "status")
        return printStatus(argc, argv);
    if (!command.isEmpty() && command != "open") {
        std::fprintf(stderr, "usage: veikk [open | daemon | status [--json]]\n");
        return 1;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("veikk");
    app.setApplicationVersion("0.1.0");

    // Matches the window to veikk.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("veikk");
    app.setWindowIcon(QIcon::fromTheme("veikk"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
