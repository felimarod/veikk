#include "layoutwatcher.h"

#include <QStandardPaths>

LayoutWatcher::LayoutWatcher(const QString &hyprctl, QObject *parent)
    : QObject(parent), m_hyprctl(hyprctl.isEmpty() ? QStandardPaths::findExecutable("hyprctl") : hyprctl) {
    connect(&m_process, &QProcess::finished, this, [this] {
        const auto layout = TabletMath::Layout::fromHyprctl(m_process.readAllStandardOutput());
        if (layout.monitors.isEmpty())
            return; // keep the last good layout rather than falling back to "everything"
        if (!sameLayout(layout, m_layout)) {
            m_layout = layout;
            emit changed();
        }
    });
    if (m_hyprctl.isEmpty())
        return;
    m_events = std::make_unique<HyprEvents>();
    connect(m_events.get(), &HyprEvents::monitorsMayHaveChanged, this, &LayoutWatcher::refresh);
    // Only a net under the events: without a socket this is the whole story.
    connect(&m_safetyNet, &QTimer::timeout, this, &LayoutWatcher::refresh);
    m_safetyNet.start(m_events->connected() ? 60000 : 3000);
    refresh();
}

LayoutWatcher::~LayoutWatcher() {
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(500);
    }
}

void LayoutWatcher::refresh() {
    if (m_hyprctl.isEmpty() || m_process.state() != QProcess::NotRunning)
        return;
    m_process.start(m_hyprctl, {"-j", "monitors"});
}

bool LayoutWatcher::sameLayout(const TabletMath::Layout &a, const TabletMath::Layout &b) {
    if (a.monitors.size() != b.monitors.size())
        return false;
    for (int i = 0; i < a.monitors.size(); ++i) {
        const auto &x = a.monitors[i], &y = b.monitors[i];
        if (x.name != y.name || x.x != y.x || x.y != y.y || x.w != y.w || x.h != y.h)
            return false;
    }
    return true;
}
