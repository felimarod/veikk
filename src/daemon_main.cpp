// veikkd — the background service. Takes the pen and pad keys away from the
// compositor and hands them back through virtual devices. Core Qt only.

#include <signal.h>

#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

#include "config.h"
#include "engine.h"
#include "state.h"

#ifndef VEIKK_VERSION
#define VEIKK_VERSION "0.0.0"
#endif

static volatile sig_atomic_t g_stop = 0;

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("veikk");
    QCoreApplication::setApplicationVersion(VEIKK_VERSION);
    if (app.arguments().contains("--version")) {
        std::printf("veikkd %s\n", VEIKK_VERSION);
        return 0;
    }

    struct sigaction action{};
    action.sa_handler = [](int) { g_stop = 1; };
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);
    // A signal handler may only set a flag, so a timer turns it into a clean
    // quit that destroys the virtual devices and removes the state file.
    QTimer stop;
    QObject::connect(&stop, &QTimer::timeout, &app, [] { if (g_stop) QCoreApplication::quit(); });
    stop.start(200);

    Engine engine(Config::defaultPath(), State::directory());
    if (!engine.ownsState()) {
        std::fprintf(stderr, "veikkd: another daemon is already running\n");
        return 2; // veikk.service does not restart on this
    }
    return app.exec();
}
