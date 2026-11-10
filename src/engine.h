#pragma once

// The daemon: takes the Veikk pen and pad keys away from the compositor,
// reshapes what they send, and hands the result on through virtual devices
// that Hyprland does recognise as a tablet and a keyboard.

#include <QFileSystemWatcher>
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <memory>

#include "config.h"
#include "evdev.h"
#include "filters.h"
#include "layoutwatcher.h"

class Engine : public QObject {
    Q_OBJECT

public:
    // A problem is kept as its English format and arguments, so the window can
    // translate it; the daemon itself carries no translations.
    struct Problem {
        QString format;
        QStringList args;
        bool isEmpty() const { return format.isEmpty(); }
        QString text() const;
        void clear() { format.clear(); args.clear(); }
    };

    // `hyprctl` is a parameter so tests can point it at a fake.
    explicit Engine(const QString &configPath, const QString &stateDirectory, QObject *parent = nullptr,
                    const QString &hyprctl = {});
    ~Engine() override;

    // False when another daemon already runs against the same state directory.
    bool ownsState() const { return m_lockFd >= 0; }

private:
    struct Source {
        int fd = -1;
        std::unique_ptr<QSocketNotifier> notifier;
        void close();
    };

    void rescan();
    void openPen(const Evdev::Device &device);
    void openKeypad(const Evdev::Device &device);
    void readPen();
    void readKeypad();
    void loseSource(Source &source, int &sink);
    void reloadConfig();
    void applyConfig();
    void writeState();
    Problem problem() const;

    static int createTablet(const Evdev::AbsRange &x, const Evdev::AbsRange &y, int pressureMax, Problem *error);
    static int createKeypad(Problem *error);
    static void destroySink(int &fd);
    static void send(int fd, const std::vector<input_event> &events);

    QString m_configPath, m_stateDirectory;
    Config m_config;
    std::unique_ptr<LayoutWatcher> m_layoutWatcher;
    PenFilter m_pen;
    KeyFilter m_keys;

    Source m_penSource, m_keypadSource;
    int m_tablet = -1, m_keypad = -1; // uinput file descriptors
    Problem m_penProblem, m_keypadProblem;
    int m_lockFd = -1; // held while this daemon owns the state directory

    QFileSystemWatcher m_watcher;
    QTimer m_reloadDebounce, m_rescanTimer;
    QFileSystemWatcher m_deviceWatcher; // /dev/input: nodes come and go on hotplug
};
