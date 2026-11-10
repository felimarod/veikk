#include "state.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>

namespace State {

QString directory() {
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    return runtime.isEmpty() ? QDir::tempPath() + "/veikk-" + QString::number(getuid()) : runtime + "/veikk";
}

int acquireLock(const QString &directory) {
    QDir().mkpath(directory);
    QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const int fd = ::open(qPrintable(directory + "/daemon.lock"), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool daemonAlive(const QString &directory) {
    const int fd = ::open(qPrintable(directory + "/daemon.lock"), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool held = flock(fd, LOCK_SH | LOCK_NB) < 0;
    ::close(fd); // also drops the shared lock if we got it
    return held;
}

QJsonObject read(const QString &directory) {
    QFile file(directory + "/state.json");
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace State
