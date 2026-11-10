#pragma once

// Starts and stops the daemon as a systemd user service.

#include <QObject>
#include <QProcess>
#include <QTimer>

class ServiceController : public QObject {
    Q_OBJECT

public:
    // `systemctl` is a parameter so tests can point it at a fake.
    explicit ServiceController(const QString &systemctl = "systemctl", QObject *parent = nullptr);
    ~ServiceController() override;

    bool busy() const { return m_process.state() != QProcess::NotRunning; }
    // Enables (and starts) or disables (and stops) veikk.service.
    void setEnabled(bool enabled);

signals:
    // Empty on success, otherwise what went wrong.
    void finished(const QString &error);

private:
    QString m_systemctl;
    QProcess m_process;
    QTimer m_timeout;
};
