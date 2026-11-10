#include "backend.h"

#include <linux/input.h>
#include <unistd.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>
#include <cerrno>
#include <cstring>

#include "evdev.h"
#include "keynames.h"
#include "state.h"

Backend::Backend(const QString &configPath, const QString &stateDirectory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_configPath(configPath.isEmpty() ? Config::defaultPath() : configPath),
      m_stateDirectory(stateDirectory.isEmpty() ? State::directory() : stateDirectory) {
    m_config = Config::load(m_configPath);
    m_lastEdit.start();

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(150);
    connect(&m_saveTimer, &QTimer::timeout, this, &Backend::save);

    m_layoutWatcher = std::make_unique<LayoutWatcher>(hyprctl);
    connect(m_layoutWatcher.get(), &LayoutWatcher::changed, this, &Backend::layoutChanged);
    // The computed area of real size follows the monitors.
    connect(m_layoutWatcher.get(), &LayoutWatcher::changed, this, &Backend::configChanged);
    connect(&m_service, &ServiceController::finished, this, [this](const QString &error) {
        m_serviceProblem = error;
        updateProblem();
        refreshDevices();
    });

    // The daemon rewrites its state file whenever a pad button is pressed;
    // the poll below catches a daemon that starts or dies without a write.
    connect(&m_stateWatcher, &QFileSystemWatcher::fileChanged, this, [this] {
        readState();
        watchStateFile();
    });
    // The daemon creates and removes its state file and virtual devices as it
    // starts and stops, and the tablet's nodes come and go with the cable, so
    // watch those; the timer is only a net for what a watch cannot see.
    QDir().mkpath(m_stateDirectory);
    m_stateWatcher.addPath(m_stateDirectory);
    connect(&m_stateWatcher, &QFileSystemWatcher::directoryChanged, this, [this] { refreshDevices(); });
    connect(&m_deviceWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        QTimer::singleShot(150, this, &Backend::refreshDevices);
    });
    m_deviceWatcher.addPath("/dev/input");
    connect(&m_pollTimer, &QTimer::timeout, this, [this] {
        refreshDevices();
        watchStateFile();
    });
    m_pollTimer.start(5000);

    refreshDevices();
}

Backend::~Backend() {
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        save(); // never lose an edit made just before quitting
    }
    closeSources();
}

QStringList Backend::monitors() const {
    QStringList names;
    for (const auto &m : layout().monitors)
        names << m.name;
    return names;
}

QVariantList Backend::buttons() const {
    QVariantList list;
    for (const auto &rule : m_config.buttons)
        list.append(QVariantMap{{"from", KeyNames::chord(rule.from)}, {"to", KeyNames::chord(rule.to)}});
    return list;
}

// ---- settings --------------------------------------------------------------

TabletMath::RealSize Backend::real() const {
    if (!m_config.realSize)
        return {};
    return TabletMath::realSize(m_config.tabletWidthMm, m_config.tabletWidthMm / m_config.tabletAspect, layout(),
                                m_config.monitor, m_config.rotation);
}

TabletMath::Area Backend::effectiveArea() const {
    const auto r = real();
    return r.valid ? r.area : m_config.area;
}

QString Backend::displayMonitor() const {
    return m_config.realSize ? layout().effective(m_config.monitor).name : m_config.monitor;
}

void Backend::fitArea(Config &config) const {
    if (config.keepProportions && !config.realSize)
        config.area = TabletMath::fitProportions(config.area, config.tabletAspect, layout().aspect(config.monitor),
                                                 config.rotation);
}

void Backend::commit(Config next, const char *kind) {
    fitArea(next);
    if (next == m_config)
        return;
    // Dragging fires hundreds of edits; one undo step per gesture, not per pixel.
    if (m_lastKind != kind || m_lastEdit.elapsed() > 1000) {
        m_undo.append(m_config);
        if (m_undo.size() > 100)
            m_undo.removeFirst();
    }
    m_redo.clear();
    m_lastKind = kind;
    m_lastEdit.restart();
    m_config = next;
    emit configChanged();
    m_saveTimer.start();
}

void Backend::applyHistory(Config config) {
    m_config = config;
    m_lastKind.clear();
    emit configChanged();
    m_saveTimer.start();
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    m_redo.append(m_config);
    applyHistory(m_undo.takeLast());
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    m_undo.append(m_config);
    applyHistory(m_redo.takeLast());
}

void Backend::save() {
    QString error;
    const bool ok = Config::save(m_config, m_configPath, &error);
    const QString text = ok ? QString() : tr("Cannot save the settings: %1").arg(error);
    if (text != m_saveError) {
        m_saveError = text;
        emit configChanged();
    }
}

void Backend::setMonitor(const QString &name) {
    Config next = m_config;
    next.monitor = name;
    commit(next, "monitor");
}

void Backend::setRotation(int degrees) {
    Config next = m_config;
    next.rotation = (degrees == 90 || degrees == 180 || degrees == 270) ? degrees : 0;
    commit(next, "rotation");
}

void Backend::setArea(double x, double y, double w, double h) {
    Config next = m_config;
    next.area.w = std::clamp(w, Config::kMinArea, 1.0);
    next.area.h = std::clamp(h, Config::kMinArea, 1.0);
    next.area.x = std::clamp(x, 0.0, 1 - next.area.w);
    next.area.y = std::clamp(y, 0.0, 1 - next.area.h);
    commit(next, "area");
}

void Backend::resetArea() {
    Config next = m_config;
    next.area = {};
    commit(next, "reset-area");
}

void Backend::setRealSize(bool on) {
    Config next = m_config;
    next.realSize = on;
    commit(next, "real-size");
}

void Backend::setTabletWidthMm(double mm) {
    if (mm < 20 || mm > 1000)
        return;
    Config next = m_config;
    next.tabletWidthMm = mm;
    commit(next, "pad-width");
}

void Backend::setKeepProportions(bool keep) {
    Config next = m_config;
    next.keepProportions = keep;
    commit(next, "proportions");
}

void Backend::setTabletAspect(double aspect) {
    if (aspect < 0.2 || aspect > 5)
        return;
    Config next = m_config;
    next.tabletAspect = aspect;
    commit(next, "aspect");
}

void Backend::setCurve(double x1, double y1, double x2, double y2) {
    Config next = m_config;
    auto unit = [](double v) { return std::clamp(v, 0.0, 1.0); };
    next.curve = {unit(x1), unit(y1), unit(x2), unit(y2)};
    commit(next, "curve");
}

void Backend::resetCurve() {
    Config next = m_config;
    next.curve = {};
    commit(next, "reset-curve");
}

double Backend::curveAt(double pressure) const {
    return TabletMath::applyCurve(m_config.curve, pressure);
}

void Backend::setBarrel(const QString &mode) {
    if (mode != "stylus" && mode != "stylus2" && mode != "none")
        return;
    Config next = m_config;
    next.barrel = mode;
    commit(next, "barrel");
}

QString Backend::addButton(const QString &from, const QString &to) {
    bool fromOk = false, toOk = false;
    const auto fromKeys = KeyNames::parseChord(from, &fromOk);
    const auto toKeys = KeyNames::parseChord(to, &toOk);
    if (!fromOk || fromKeys.isEmpty())
        return tr("Press a pad button, or type its keys like ctrl+z.");
    if (!toOk)
        return tr("Unknown key in \"%1\". Use names like ctrl+shift+z, f5 or none.").arg(to);
    Config next = m_config;
    QList<int> sortedFrom = fromKeys;
    std::sort(sortedFrom.begin(), sortedFrom.end());
    for (int i = next.buttons.size() - 1; i >= 0; --i) {
        QList<int> existing = next.buttons[i].from;
        std::sort(existing.begin(), existing.end());
        if (existing == sortedFrom)
            next.buttons.removeAt(i); // one rule per button: the new one wins
    }
    next.buttons.append({fromKeys, toKeys});
    commit(next, "button");
    return {};
}

void Backend::removeButton(int index) {
    if (index < 0 || index >= m_config.buttons.size())
        return;
    Config next = m_config;
    next.buttons.removeAt(index);
    commit(next, "button");
}

void Backend::setDaemonEnabled(bool enabled) {
    m_service.setEnabled(enabled);
}

// ---- devices ---------------------------------------------------------------

void Backend::updateProblem() {
    const QString text = !m_serviceProblem.isEmpty() ? m_serviceProblem
                       : !m_daemonProblem.isEmpty() ? m_daemonProblem : m_openProblem;
    if (text == m_problem)
        return;
    m_problem = text;
    emit deviceChanged();
}

void Backend::watchStateFile() {
    const QString path = m_stateDirectory + "/state.json";
    // The daemon replaces the file on every write, which drops it from the watch.
    if (QFileInfo::exists(path) && !m_stateWatcher.files().contains(path))
        m_stateWatcher.addPath(path);
}

void Backend::readState() {
    const bool running = State::daemonAlive(m_stateDirectory);
    QString problem;
    if (running) {
        const auto state = State::read(m_stateDirectory);
        // The daemon sends the English format and its arguments; translate here.
        const QString format = state.value("problemFormat").toString();
        if (!format.isEmpty()) {
            problem = QCoreApplication::translate("Engine", format.toUtf8().constData());
            for (const auto &arg : state.value("problemArgs").toArray())
                problem = problem.arg(arg.toString());
        } else {
            problem = state.value("problem").toString();
        }
        const int sequence = state.value("sequence").toInt();
        if (sequence != m_stateSequence) {
            m_stateSequence = sequence;
            const QString chord = state.value("chord").toString();
            if (sequence > 0 && chord != "none") {
                m_lastChord = chord;
                emit chordChanged();
            }
        }
    }
    if (running != m_daemonRunning) {
        m_daemonRunning = running;
        emit deviceChanged();
    }
    m_daemonProblem = problem;
    updateProblem();
}

void Backend::closeSources() {
    m_penNotifier.reset();
    m_keypadNotifier.reset();
    if (m_penFd >= 0)
        ::close(m_penFd);
    if (m_keypadFd >= 0)
        ::close(m_keypadFd);
    m_penFd = m_keypadFd = -1;
    m_penPath.clear();
    m_keypadPath.clear();
}

void Backend::refreshDevices() {
    readState();

    // With the daemon running the physical pen is grabbed, so read what the
    // daemon makes of it instead: the same stream the desktop sees.
    QString penPath, keypadPath, name;
    bool processed = false;
    for (const auto &device : Evdev::scan()) {
        if (device.pen && device.virtualDevice == m_daemonRunning && penPath.isEmpty()) {
            penPath = device.path;
            name = device.name;
            processed = device.virtualDevice;
        }
        if (device.keypad && !device.virtualDevice && !m_daemonRunning && keypadPath.isEmpty())
            keypadPath = device.path;
    }

    bool changed = false;
    if (penPath != m_penPath || processed != m_penProcessed || (penPath.isEmpty() != (m_penFd < 0) && penPath != m_failedPenPath)) {
        if (m_penNotifier)
            m_penNotifier.release()->deleteLater(); // may be running right now
        m_failedPenPath.clear();
        if (m_penFd >= 0)
            ::close(m_penFd);
        m_penFd = -1;
        m_penPath = penPath;
        m_penProcessed = processed;
        m_inRange = m_down = m_barrelPressed = false;
        m_pressure = 0;
        if (!penPath.isEmpty()) {
            m_penFd = Evdev::open(penPath);
            if (m_penFd < 0) {
                m_openProblem = tr("Cannot open %1: %2").arg(penPath, QString::fromLocal8Bit(strerror(errno)));
                m_failedPenPath = penPath; // do not retry every scan
            } else {
                m_openProblem.clear();
                m_xMax = std::max(1, Evdev::absRange(m_penFd, ABS_X).max);
                m_yMax = std::max(1, Evdev::absRange(m_penFd, ABS_Y).max);
                m_pressureMax = std::max(1, Evdev::absRange(m_penFd, ABS_PRESSURE).max);
                m_penNotifier = std::make_unique<QSocketNotifier>(m_penFd, QSocketNotifier::Read);
                connect(m_penNotifier.get(), &QSocketNotifier::activated, this, &Backend::readPen);
            }
        }
        changed = true;
        emit penChanged();
    }
    if (keypadPath != m_keypadPath) {
        if (m_keypadNotifier)
            m_keypadNotifier.release()->deleteLater();
        if (m_keypadFd >= 0)
            ::close(m_keypadFd);
        m_keypadFd = -1;
        m_keypadPath = keypadPath;
        m_keysDown.clear();
        if (!keypadPath.isEmpty()) {
            m_keypadFd = Evdev::open(keypadPath);
            if (m_keypadFd >= 0) {
                m_keypadNotifier = std::make_unique<QSocketNotifier>(m_keypadFd, QSocketNotifier::Read);
                connect(m_keypadNotifier.get(), &QSocketNotifier::activated, this, &Backend::readKeypad);
            }
        }
    }
    if (penPath.isEmpty())
        m_openProblem.clear();
    updateProblem();
    if (name != m_deviceName) {
        m_deviceName = name;
        changed = true;
    }
    if (changed)
        emit deviceChanged();
}

void Backend::readPen() {
    input_event buffer[64];
    for (;;) {
        const ssize_t n = ::read(m_penFd, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            if (n == 0 || errno != EAGAIN) {
                // Unplugged: stop polling the dead descriptor; the next scan reopens.
                if (m_penNotifier)
                    m_penNotifier->setEnabled(false);
            }
            return;
        }
        for (ssize_t i = 0; i < n / ssize_t(sizeof(input_event)); ++i) {
            const input_event &e = buffer[i];
            if (e.type == EV_ABS) {
                switch (e.code) {
                case ABS_X: m_rawX = e.value; break;
                case ABS_Y: m_rawY = e.value; break;
                case ABS_PRESSURE: m_rawPressure = e.value; break;
                case ABS_TILT_X: m_rawTiltX = e.value; break;
                case ABS_TILT_Y: m_rawTiltY = e.value; break;
                }
            } else if (e.type == EV_KEY) {
                if (e.code == BTN_TOOL_PEN || e.code == BTN_TOOL_RUBBER)
                    m_inRange = e.value != 0;
                else if (e.code == BTN_TOUCH)
                    m_down = e.value != 0;
                else if (e.code == BTN_STYLUS || e.code == BTN_STYLUS2)
                    m_barrelPressed = e.value != 0;
            } else if (e.type == EV_SYN && e.code == SYN_REPORT) {
                double x = double(m_rawX) / m_xMax, y = double(m_rawY) / m_yMax;
                if (m_penProcessed) {
                    // The daemon spreads the pen over the whole layout; show it
                    // relative to the monitor the pad is mapped to.
                    const auto r = layout().region(displayMonitor());
                    x = std::clamp((x - r.x) / r.w, 0.0, 1.0);
                    y = std::clamp((y - r.y) / r.h, 0.0, 1.0);
                }
                m_penX = x;
                m_penY = y;
                m_pressure = std::clamp(double(m_rawPressure) / m_pressureMax, 0.0, 1.0);
                m_tiltX = m_rawTiltX / 127.0;
                m_tiltY = m_rawTiltY / 127.0;
                emit penChanged();
                if (m_inRange)
                    emit stroke(m_penX, m_penY, m_pressure, m_down);
            }
        }
    }
}

void Backend::readKeypad() {
    input_event buffer[32];
    for (;;) {
        const ssize_t n = ::read(m_keypadFd, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            if ((n == 0 || errno != EAGAIN) && m_keypadNotifier)
                m_keypadNotifier->setEnabled(false);
            return;
        }
        for (ssize_t i = 0; i < n / ssize_t(sizeof(input_event)); ++i) {
            const input_event &e = buffer[i];
            if (e.type == EV_KEY) {
                if (e.value == 1)
                    m_keysDown.insert(e.code);
                else if (e.value == 0)
                    m_keysDown.erase(e.code);
            } else if (e.type == EV_SYN && e.code == SYN_REPORT && !m_keysDown.empty()) {
                const QString chord = KeyNames::chord(QList<int>(m_keysDown.begin(), m_keysDown.end()));
                if (chord != m_lastChord) {
                    m_lastChord = chord;
                    emit chordChanged();
                }
            }
        }
    }
}
