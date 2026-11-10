#pragma once

// The daemon: takes the Veikk pen and pad keys away from the compositor,
// reshapes what they send, and hands the result on through virtual devices
// that Hyprland does recognise as a tablet and a keyboard.

#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QSocketNotifier>
#include <QTimer>
#include <memory>

#include "config.h"
#include "evdev.h"
#include "filters.h"

class Engine : public QObject {
    Q_OBJECT

public:
    // `hyprctl` is a parameter so tests can point it at a fake.
    explicit Engine(const QString &configPath, const QString &stateDirectory, QObject *parent = nullptr,
                    const QString &hyprctl = {});
    ~Engine() override;

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
    void refreshLayout();
    void applyConfig();
    void writeState();

    static int createTablet(int xMax, int yMax, int pressureMax, QString *error);
    static int createKeypad(QString *error);
    static void destroySink(int &fd);
    static void send(int fd, const std::vector<input_event> &events);

    QString m_configPath, m_stateDirectory, m_hyprctl;
    Config m_config;
    TabletMath::Layout m_layout;
    PenFilter m_pen;
    KeyFilter m_keys;

    Source m_penSource, m_keypadSource;
    int m_tablet = -1, m_keypad = -1; // uinput file descriptors
    QString m_problem;

    QFileSystemWatcher m_watcher;
    QTimer m_reloadDebounce, m_rescanTimer, m_layoutTimer;
    QProcess m_layoutProcess;
};
