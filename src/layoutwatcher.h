#pragma once

// Keeps the monitor layout current: asks hyprctl when Hyprland says monitors
// changed, with a slow timer as a safety net, and reports only real changes.

#include <QObject>
#include <QProcess>
#include <QTimer>

#include "hyprevents.h"
#include "tabletmath.h"

class LayoutWatcher : public QObject {
    Q_OBJECT

public:
    // `hyprctl` is a parameter so tests can point it at a fake or at nothing.
    explicit LayoutWatcher(const QString &hyprctl = {}, QObject *parent = nullptr);
    ~LayoutWatcher() override;

    const TabletMath::Layout &layout() const { return m_layout; }
    void refresh();

    static bool sameLayout(const TabletMath::Layout &a, const TabletMath::Layout &b);

signals:
    void changed();

private:
    QString m_hyprctl;
    TabletMath::Layout m_layout;
    QProcess m_process;
    QTimer m_safetyNet;
    std::unique_ptr<HyprEvents> m_events;
};
