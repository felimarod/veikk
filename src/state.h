#pragma once

// What the daemon and the window share: where the state lives, who owns it,
// and what the daemon last reported.

#include <QJsonObject>
#include <QString>

namespace State {

// $XDG_RUNTIME_DIR/veikk; without a runtime directory, a private per-user
// folder under the temp directory.
QString directory();

// Creates the directory (mode 0700) and takes the daemon lock. Returns the
// lock's file descriptor, or -1 when another daemon already holds it.
int acquireLock(const QString &directory);

// True while some process holds the daemon lock. Unlike a pid in a file this
// cannot be fooled by a recycled pid.
bool daemonAlive(const QString &directory);

QJsonObject read(const QString &directory);

} // namespace State
