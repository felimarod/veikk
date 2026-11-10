#pragma once

// Hyprland announces monitor changes on its event socket (.socket2.sock), so
// nobody has to run hyprctl on a timer to notice them.

#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <memory>

class HyprEvents : public QObject {
    Q_OBJECT

public:
    // `socketPath` is a parameter for tests; empty finds Hyprland's.
    explicit HyprEvents(const QString &socketPath = {}, QObject *parent = nullptr);
    ~HyprEvents() override;

    bool connected() const { return m_fd >= 0; }
    static QString defaultSocketPath();

signals:
    void monitorsMayHaveChanged();

private:
    void connectSocket();
    void readEvents();
    void disconnectSocket();

    QString m_path;
    int m_fd = -1;
    QByteArray m_pending;
    std::unique_ptr<QSocketNotifier> m_notifier;
    QTimer m_retry;
};
