#include "hyprevents.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <QFileInfo>
#include <cerrno>
#include <cstring>

QString HyprEvents::defaultSocketPath() {
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (signature.isEmpty() || runtime.isEmpty())
        return {};
    return runtime + "/hypr/" + signature + "/.socket2.sock";
}

HyprEvents::HyprEvents(const QString &socketPath, QObject *parent)
    : QObject(parent), m_path(socketPath.isEmpty() ? defaultSocketPath() : socketPath) {
    if (m_path.isEmpty())
        return; // not under Hyprland
    connectSocket();
    // Hyprland restarts take the socket with them.
    connect(&m_retry, &QTimer::timeout, this, [this] { if (m_fd < 0) connectSocket(); });
    m_retry.start(5000);
}

HyprEvents::~HyprEvents() {
    disconnectSocket();
}

void HyprEvents::connectSocket() {
    if (!QFileInfo::exists(m_path))
        return;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const QByteArray path = m_path.toLocal8Bit();
    if (path.size() >= int(sizeof address.sun_path)) {
        ::close(fd);
        return;
    }
    std::memcpy(address.sun_path, path.constData(), size_t(path.size()));
    if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) < 0) {
        ::close(fd);
        return;
    }
    m_fd = fd;
    m_notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
    connect(m_notifier.get(), &QSocketNotifier::activated, this, &HyprEvents::readEvents);
    emit monitorsMayHaveChanged(); // whatever happened while we were away
}

void HyprEvents::disconnectSocket() {
    if (m_notifier)
        m_notifier.release()->deleteLater(); // possibly called from its own slot
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
    m_pending.clear();
}

void HyprEvents::readEvents() {
    char buffer[4096];
    bool relevant = false;
    for (;;) {
        const ssize_t n = ::read(m_fd, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR)
            continue;
        if (n == 0 || (n < 0 && errno != EAGAIN)) {
            disconnectSocket();
            break;
        }
        if (n < 0)
            break;
        m_pending.append(buffer, int(n));
    }
    // Lines look like "monitoraddedv2>>1,DP-1,...".
    int newline;
    while ((newline = m_pending.indexOf('\n')) >= 0) {
        const QByteArray line = m_pending.left(newline);
        m_pending.remove(0, newline + 1);
        const QByteArray name = line.left(line.indexOf(">>"));
        if (name.startsWith("monitor") || name == "configreloaded")
            relevant = true;
    }
    if (relevant)
        emit monitorsMayHaveChanged();
}
