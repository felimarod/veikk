// veikk — set up a Veikk drawing tablet: area, pressure curve and pad buttons.
//
//   veikk            open the window
//   veikk daemon     run the background service the window configures
//   veikk status     say whether the tablet and the service are running

#include <unistd.h>

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
#include <QUrl>
#include <cerrno>
#include <cstdio>

#include "backend.h"
#include "config.h"
#include "evdev.h"
#include "theme.h"
#include "state.h"
#include "translator.h"

#ifndef VEIKK_VERSION
#define VEIKK_VERSION "0.0.0"
#endif

// `veikk daemon` is kept for old habits and unit files: it becomes veikkd,
// which does not carry Qt Quick along.
static int runDaemon(char *argv[]) {
    const QString next = QCoreApplication::applicationDirPath() + "/veikkd";
    execl(qPrintable(next), "veikkd", static_cast<char *>(nullptr));
    execlp("veikkd", "veikkd", static_cast<char *>(nullptr));
    std::fprintf(stderr, "%s: cannot run veikkd\n", argv[0]);
    return 1;
}

static int printStatus(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    bool json = false;
    for (const auto &arg : app.arguments())
        json = json || arg == "--json";

    const bool running = State::daemonAlive(State::directory());
    const QJsonObject state = running ? State::read(State::directory()) : QJsonObject();

    QJsonArray devices;
    for (const auto &device : Evdev::scan())
        devices.append(QJsonObject{{"name", device.name}, {"path", device.path}, {"pen", device.pen},
                                   {"keypad", device.keypad}, {"virtual", device.virtualDevice}});
    const QJsonObject out{{"daemonRunning", running}, {"problem", state.value("problem")},
                          {"devices", devices}};
    if (json) {
        std::puts(QJsonDocument(out).toJson(QJsonDocument::Compact).constData());
    } else {
        std::printf("daemon: %s\n", running ? "running" : "not running");
        for (const auto &d : devices)
            std::printf("device: %s (%s)\n", d.toObject().value("name").toString().toLocal8Bit().constData(),
                        d.toObject().value("path").toString().toLocal8Bit().constData());
    }
    return running ? 0 : 3; // 3 is what systemctl uses for "not running"
}

int main(int argc, char *argv[]) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (command == "--version" || command == "-v") {
        std::printf("veikk %s\n", VEIKK_VERSION);
        return 0;
    }
    if (command == "--help" || command == "-h") {
        std::printf("usage: veikk [open | daemon | status [--json]]\n");
        return 0;
    }
    if (command == "daemon")
        return runDaemon(argv);
    if (command == "status")
        return printStatus(argc, argv);
    if (!command.isEmpty() && command != "open") {
        std::fprintf(stderr, "usage: veikk [open | daemon | status [--json]]\n");
        return 1;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("veikk");
    app.setApplicationVersion(VEIKK_VERSION);

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
