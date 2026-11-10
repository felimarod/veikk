#include "servicecontroller.h"

ServiceController::ServiceController(const QString &systemctl, QObject *parent)
    : QObject(parent), m_systemctl(systemctl) {
    // systemctl can hang on a broken user bus; the window must not hang with it.
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(15000);
    connect(&m_timeout, &QTimer::timeout, &m_process, &QProcess::kill);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_timeout.stop();
            emit finished(tr("systemctl failed: %1").arg(m_process.errorString()));
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_timeout.stop();
        if (code == 0 && status == QProcess::NormalExit)
            emit finished({});
        else
            emit finished(tr("systemctl failed: %1")
                              .arg(QString::fromUtf8(m_process.readAllStandardError()).trimmed()));
    });
}

ServiceController::~ServiceController() {
    if (busy()) {
        m_process.kill();
        m_process.waitForFinished(500);
    }
}

void ServiceController::setEnabled(bool enabled) {
    if (busy())
        return;
    m_timeout.start();
    m_process.start(m_systemctl, {"--user", enabled ? "enable" : "disable", "--now", "veikk.service"});
}
