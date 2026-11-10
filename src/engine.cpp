#include "engine.h"

#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <cerrno>
#include <cstring>

#include "keynames.h"
#include "state.h"

void Engine::Source::close() {
    notifier.reset();
    if (fd >= 0)
        ::close(fd); // closing also hands the device back to the compositor
    fd = -1;
}

QString Engine::Problem::text() const {
    QString out = format;
    for (const QString &arg : args)
        out = out.arg(arg);
    return out;
}

static QString errnoText() {
    return QString::fromLocal8Bit(strerror(errno));
}

Engine::Engine(const QString &configPath, const QString &stateDirectory, QObject *parent, const QString &hyprctl)
    : QObject(parent), m_configPath(configPath), m_stateDirectory(stateDirectory) {
    // A second daemon would only fight over the grab and then delete the
    // first one's state file on its way out.
    m_lockFd = State::acquireLock(m_stateDirectory);
    if (m_lockFd < 0)
        return;

    m_reloadDebounce.setSingleShot(true);
    m_reloadDebounce.setInterval(80);
    connect(&m_reloadDebounce, &QTimer::timeout, this, &Engine::reloadConfig);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_reloadDebounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_reloadDebounce.start(); });

    // Plugging the tablet in adds nodes under /dev/input, which wakes the scan
    // at once; the timer only covers what the watch cannot see.
    connect(&m_deviceWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        QTimer::singleShot(150, this, &Engine::rescan);
    });
    m_deviceWatcher.addPath("/dev/input");
    connect(&m_rescanTimer, &QTimer::timeout, this, &Engine::rescan);
    m_rescanTimer.start(10000);

    m_layoutWatcher = std::make_unique<LayoutWatcher>(hyprctl);
    connect(m_layoutWatcher.get(), &LayoutWatcher::changed, this, &Engine::applyConfig);

    reloadConfig();
    rescan();
    writeState();
}

Engine::~Engine() {
    if (m_lockFd < 0)
        return; // not ours: leave the running daemon's devices and state alone
    m_penSource.close();
    m_keypadSource.close();
    destroySink(m_tablet);
    destroySink(m_keypad);
    QFile::remove(m_stateDirectory + "/state.json");
    ::close(m_lockFd); // releases the lock
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
    m_pen.configure(m_config, m_layoutWatcher->layout());
    m_keys.setRules(m_config.buttons);
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
        m_penProblem = {"Cannot open %1: %2", {device.path, errnoText()}};
        writeState();
        return;
    }
    const auto x = Evdev::absRange(fd, ABS_X), y = Evdev::absRange(fd, ABS_Y),
               pressure = Evdev::absRange(fd, ABS_PRESSURE);
    Problem error;
    const int sink = createTablet(x, y, pressure.max, &error);
    if (sink < 0) {
        ::close(fd);
        m_penProblem = error;
        writeState();
        return;
    }
    m_tablet = sink;
    m_pen.setRanges(x.max, y.max, pressure.max);
    applyConfig();
    m_penSource.fd = fd;
    m_penSource.notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
    connect(m_penSource.notifier.get(), &QSocketNotifier::activated, this, &Engine::readPen);
    m_penProblem.clear();
    writeState();
}

void Engine::openKeypad(const Evdev::Device &device) {
    const int fd = Evdev::open(device.path, true);
    if (fd < 0) {
        m_keypadProblem = {"Cannot open %1: %2", {device.path, errnoText()}};
        writeState();
        return;
    }
    Problem error;
    const int sink = createKeypad(&error);
    if (sink < 0) {
        ::close(fd);
        m_keypadProblem = error;
        writeState();
        return;
    }
    m_keypad = sink;
    m_keypadSource.fd = fd;
    m_keypadSource.notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
    connect(m_keypadSource.notifier.get(), &QSocketNotifier::activated, this, &Engine::readKeypad);
    m_keypadProblem.clear();
    writeState();
}

void Engine::loseSource(Source &source, int &sink) {
    if (&source == &m_penSource)
        m_pen.reset();
    else
        m_keys.reset();
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
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN)
                loseSource(m_penSource, m_tablet); // unplugged
            break;
        }
        if (n == 0) { // end of file: the device is gone
            loseSource(m_penSource, m_tablet);
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
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN)
                loseSource(m_keypadSource, m_keypad);
            break;
        }
        if (n == 0) {
            loseSource(m_keypadSource, m_keypad);
            break;
        }
        for (ssize_t i = 0; i < n / ssize_t(sizeof(input_event)); ++i)
            m_keys.feed(buffer[i], out);
    }
    send(m_keypad, out);
    // The window shows the newest chord while learning a button, and the
    // buttons held on the test page.
    const QList<int> held = m_keys.pressed();
    if (m_keys.sequence() != before || held != m_lastHeld) {
        m_lastHeld = held;
        writeState();
    }
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

static int openUinput(Engine::Problem *error) {
    const int fd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0 && error)
        *error = {"Cannot open /dev/uinput: %1", {errnoText()}};
    return fd;
}

static bool setup(Engine::Problem *error, bool ok) {
    if (!ok && error && error->isEmpty())
        *error = {"Cannot set up the virtual device: %1", {errnoText()}};
    return ok;
}

static bool finishUinput(int fd, const char *name, __u16 product, Engine::Problem *error) {
    uinput_setup config{};
    config.id = {BUS_VIRTUAL, Evdev::kVeikkVendor, product, 1};
    std::strncpy(config.name, name, UINPUT_MAX_NAME_SIZE - 1);
    if (ioctl(fd, UI_DEV_SETUP, &config) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        if (error)
            *error = {"Cannot create the virtual device: %1", {errnoText()}};
        return false;
    }
    return true;
}

int Engine::createTablet(const Evdev::AbsRange &xRange, const Evdev::AbsRange &yRange, int pressureMax,
                         Problem *error) {
    const int fd = openUinput(error);
    if (fd < 0)
        return -1;
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) >= 0;
    for (int key : {BTN_TOOL_PEN, BTN_TOOL_RUBBER, BTN_TOUCH, BTN_STYLUS, BTN_STYLUS2})
        ok = ok && ioctl(fd, UI_SET_KEYBIT, key) >= 0;
    ok = ok && ioctl(fd, UI_SET_EVBIT, EV_ABS) >= 0;
    // A pad tablet, not a screen: the pen moves a cursor over the target.
    ok = ok && ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_POINTER) >= 0;
    // Units per millimetre as the pen reports them; 200 when it does not say.
    const int xResolution = xRange.resolution > 0 ? xRange.resolution : 200;
    const int yResolution = yRange.resolution > 0 ? yRange.resolution : 200;
    struct Axis { int code, min, max, resolution; };
    for (const Axis &a : {Axis{ABS_X, 0, xRange.max, xResolution}, Axis{ABS_Y, 0, yRange.max, yResolution},
                          Axis{ABS_PRESSURE, 0, pressureMax, 0}, Axis{ABS_TILT_X, -127, 127, 0},
                          Axis{ABS_TILT_Y, -127, 127, 0}}) {
        uinput_abs_setup axis{};
        axis.code = a.code;
        axis.absinfo.minimum = a.min;
        axis.absinfo.maximum = a.max;
        axis.absinfo.resolution = a.resolution;
        ok = ok && ioctl(fd, UI_SET_ABSBIT, a.code) >= 0 && ioctl(fd, UI_ABS_SETUP, &axis) >= 0;
    }
    if (!setup(error, ok) || !finishUinput(fd, Evdev::kVirtualTablet, 0xfe01, error)) {
        ::close(fd);
        return -1;
    }
    return fd;
}

int Engine::createKeypad(Problem *error) {
    const int fd = openUinput(error);
    if (fd < 0)
        return -1;
    bool ok = ioctl(fd, UI_SET_EVBIT, EV_KEY) >= 0;
    // Exactly the codes KeyNames accepts, so no rule can name a key the
    // virtual keyboard would drop.
    for (int key = 1; ok && key <= KEY_MAX; ++key)
        if (KeyNames::emittable(key))
            ok = ioctl(fd, UI_SET_KEYBIT, key) >= 0;
    if (!setup(error, ok) || !finishUinput(fd, Evdev::kVirtualKeypad, 0xfe02, error)) {
        ::close(fd);
        return -1;
    }
    return fd;
}

Engine::Problem Engine::problem() const {
    return m_penProblem.isEmpty() ? m_keypadProblem : m_penProblem;
}

void Engine::writeState() {
    QJsonArray pressed;
    for (int code : m_keys.pressed())
        pressed.append(code);
    QSaveFile file(m_stateDirectory + "/state.json");
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{
        {"pid", qint64(getpid())},
        {"pen", m_penSource.fd >= 0},
        {"keypad", m_keypadSource.fd >= 0},
        {"problem", problem().text()},
        {"problemFormat", problem().format},
        {"problemArgs", QJsonArray::fromStringList(problem().args)},
        {"chord", KeyNames::chord(m_keys.lastChord())},
        {"sequence", m_keys.sequence()},
        {"pressed", pressed},
    }).toJson(QJsonDocument::Compact));
    file.commit();
}
