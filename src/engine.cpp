#include "engine.h"

#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <cerrno>
#include <cstring>

#include "keynames.h"

void Engine::Source::close() {
    notifier.reset();
    if (fd >= 0)
        ::close(fd); // closing also hands the device back to the compositor
    fd = -1;
}

Engine::Engine(const QString &configPath, const QString &stateDirectory, QObject *parent, const QString &hyprctl)
    : QObject(parent), m_configPath(configPath), m_stateDirectory(stateDirectory),
      m_hyprctl(hyprctl.isEmpty() ? QStandardPaths::findExecutable("hyprctl") : hyprctl) {
    m_reloadDebounce.setSingleShot(true);
    m_reloadDebounce.setInterval(80);
    connect(&m_reloadDebounce, &QTimer::timeout, this, &Engine::reloadConfig);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_reloadDebounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_reloadDebounce.start(); });

    connect(&m_rescanTimer, &QTimer::timeout, this, &Engine::rescan);
    m_rescanTimer.start(2000);

    connect(&m_layoutProcess, &QProcess::finished, this, [this] {
        const auto layout = TabletMath::Layout::fromHyprctl(m_layoutProcess.readAllStandardOutput());
        if (layout.monitors.isEmpty())
            return; // keep the last good layout rather than falling back to "everything"
        bool same = layout.monitors.size() == m_layout.monitors.size();
        for (int i = 0; same && i < layout.monitors.size(); ++i) {
            const auto &a = layout.monitors[i], &b = m_layout.monitors[i];
            same = a.name == b.name && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }
        if (!same) {
            m_layout = layout;
            applyConfig();
        }
    });
    connect(&m_layoutTimer, &QTimer::timeout, this, &Engine::refreshLayout);
    m_layoutTimer.start(3000);

    QDir().mkpath(m_stateDirectory);
    reloadConfig();
    refreshLayout();
    rescan();
    writeState();
}

Engine::~Engine() {
    m_penSource.close();
    m_keypadSource.close();
    destroySink(m_tablet);
    destroySink(m_keypad);
    QFile::remove(m_stateDirectory + "/state.json");
}

void Engine::reloadConfig() {
    m_config = Config::load(m_configPath);
    // The file is replaced on every save, which drops it from the watch.
    const QString directory = QFileInfo(m_configPath).absolutePath();
    QDir().mkpath(directory);
    if (!m_watcher.directories().contains(directory))
        m_watcher.addPath(directory);
    if (QFileInfo::exists(m_configPath) && !m_watcher.files().contains(m_configPath))
        m_watcher.addPath(m_configPath);
    applyConfig();
}

void Engine::applyConfig() {
    m_pen.configure(m_config, m_layout);
    m_keys.setRules(m_config.buttons);
}

void Engine::refreshLayout() {
    if (m_hyprctl.isEmpty() || m_layoutProcess.state() != QProcess::NotRunning)
        return;
    m_layoutProcess.start(m_hyprctl, {"-j", "monitors"});
}

void Engine::rescan() {
    if (m_penSource.fd >= 0 && m_keypadSource.fd >= 0)
        return;
    for (const auto &device : Evdev::scan()) {
        if (device.virtualDevice)
            continue;
        if (device.pen && m_penSource.fd < 0)
            openPen(device);
        else if (device.keypad && m_keypadSource.fd < 0)
            openKeypad(device);
    }
}

void Engine::openPen(const Evdev::Device &device) {
    const int fd = Evdev::open(device.path, true);
    if (fd < 0) {
        m_problem = tr("Cannot open %1: %2").arg(device.path, QString::fromLocal8Bit(strerror(errno)));
        writeState();
        return;
    }
    const auto x = Evdev::absRange(fd, ABS_X), y = Evdev::absRange(fd, ABS_Y),
               pressure = Evdev::absRange(fd, ABS_PRESSURE);
    QString error;
    const int sink = createTablet(x.max, y.max, pressure.max, &error);
    if (sink < 0) {
        ::close(fd);
        m_problem = error;
        writeState();
        return;
    }
    m_tablet = sink;
    m_pen.setRanges(x.max, y.max, pressure.max);
    applyConfig();
    m_penSource.fd = fd;
    m_penSource.notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
    connect(m_penSource.notifier.get(), &QSocketNotifier::activated, this, &Engine::readPen);
    m_problem.clear();
    writeState();
}

void Engine::openKeypad(const Evdev::Device &device) {
    const int fd = Evdev::open(device.path, true);
    if (fd < 0) {
        m_problem = tr("Cannot open %1: %2").arg(device.path, QString::fromLocal8Bit(strerror(errno)));
        writeState();
        return;
    }
    QString error;
    const int sink = createKeypad(&error);
    if (sink < 0) {
        ::close(fd);
        m_problem = error;
        writeState();
        return;
    }
    m_keypad = sink;
    m_keypadSource.fd = fd;
    m_keypadSource.notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
    connect(m_keypadSource.notifier.get(), &QSocketNotifier::activated, this, &Engine::readKeypad);
    m_problem.clear();
    writeState();
}

void Engine::loseSource(Source &source, int &sink) {
    source.close();
    destroySink(sink);
    writeState();
}

void Engine::readPen() {
    input_event buffer[64];
    std::vector<input_event> out;
    for (;;) {
        const ssize_t n = ::read(m_penSource.fd, buffer, sizeof buffer);
        if (n < 0) {
            if (errno != EAGAIN)
                loseSource(m_penSource, m_tablet); // unplugged
            break;
        }
        for (ssize_t i = 0; i < n / ssize_t(sizeof(input_event)); ++i)
            m_pen.feed(buffer[i], out);
    }
    send(m_tablet, out);
}

void Engine::readKeypad() {
    input_event buffer[64];
    std::vector<input_event> out;
    const int before = m_keys.sequence();
    for (;;) {
        const ssize_t n = ::read(m_keypadSource.fd, buffer, sizeof buffer);
        if (n < 0) {
            if (errno != EAGAIN)
                loseSource(m_keypadSource, m_keypad);
            break;
        }
        for (ssize_t i = 0; i < n / ssize_t(sizeof(input_event)); ++i)
            m_keys.feed(buffer[i], out);
    }
    send(m_keypad, out);
    if (m_keys.sequence() != before)
        writeState(); // the window shows the newest chord while learning a button
}

void Engine::send(int fd, const std::vector<input_event> &events) {
    if (fd < 0 || events.empty())
        return;
    // Best effort: a full queue drops the frame rather than stalling the pen.
    [[maybe_unused]] const ssize_t written = ::write(fd, events.data(), events.size() * sizeof(input_event));
}

void Engine::destroySink(int &fd) {
    if (fd >= 0) {
        ioctl(fd, UI_DEV_DESTROY);
        ::close(fd);
    }
    fd = -1;
}

static int openUinput(QString *error) {
    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0 && error)
        *error = QObject::tr("Cannot open /dev/uinput: %1").arg(QString::fromLocal8Bit(strerror(errno)));
    return fd;
}

static bool finishUinput(int fd, const char *name, __u16 product, QString *error) {
    uinput_setup setup{};
    setup.id = {BUS_VIRTUAL, Evdev::kVeikkVendor, product, 1};
    std::strncpy(setup.name, name, UINPUT_MAX_NAME_SIZE - 1);
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        if (error)
            *error = QObject::tr("Cannot create the virtual device: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    return true;
}

int Engine::createTablet(int xMax, int yMax, int pressureMax, QString *error) {
    const int fd = openUinput(error);
    if (fd < 0)
        return -1;
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    for (int key : {BTN_TOOL_PEN, BTN_TOOL_RUBBER, BTN_TOUCH, BTN_STYLUS, BTN_STYLUS2})
        ioctl(fd, UI_SET_KEYBIT, key);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    // A pad tablet, not a screen: the pen moves a cursor over the target.
    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
    struct Axis { int code, min, max, resolution; };
    for (const Axis &a : {Axis{ABS_X, 0, xMax, 200}, Axis{ABS_Y, 0, yMax, 200},
                          Axis{ABS_PRESSURE, 0, pressureMax, 0}, Axis{ABS_TILT_X, -127, 127, 0},
                          Axis{ABS_TILT_Y, -127, 127, 0}}) {
        ioctl(fd, UI_SET_ABSBIT, a.code);
        uinput_abs_setup setup{};
        setup.code = a.code;
        setup.absinfo.minimum = a.min;
        setup.absinfo.maximum = a.max;
        setup.absinfo.resolution = a.resolution;
        ioctl(fd, UI_ABS_SETUP, &setup);
    }
    if (!finishUinput(fd, Evdev::kVirtualTablet, 0xfe01, error)) {
        ::close(fd);
        return -1;
    }
    return fd;
}

int Engine::createKeypad(QString *error) {
    const int fd = openUinput(error);
    if (fd < 0)
        return -1;
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    // Real keys only; the 0x100 range is mouse and tablet buttons.
    for (int key = 1; key < 0x100; ++key)
        ioctl(fd, UI_SET_KEYBIT, key);
    for (int key = 0x160; key < 0x2ff; ++key)
        ioctl(fd, UI_SET_KEYBIT, key);
    if (!finishUinput(fd, Evdev::kVirtualKeypad, 0xfe02, error)) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void Engine::writeState() {
    QSaveFile file(m_stateDirectory + "/state.json");
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{
        {"pid", qint64(getpid())},
        {"pen", m_penSource.fd >= 0},
        {"keypad", m_keypadSource.fd >= 0},
        {"problem", m_problem},
        {"chord", KeyNames::chord(m_keys.lastChord())},
        {"sequence", m_keys.sequence()},
    }).toJson(QJsonDocument::Compact));
    file.commit();
}
