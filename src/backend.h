#pragma once

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <QVariantList>
#include <memory>
#include <set>

#include "config.h"
#include "layoutwatcher.h"
#include "placement.h"
#include "servicecontroller.h"
#include "tabletmath.h"

// What the window shows and changes: the settings the daemon applies, and a
// live view of the pen and the pad's keys.
class Backend : public QObject {
    Q_OBJECT
    // Device and daemon.
    Q_PROPERTY(bool penFound READ penFound NOTIFY deviceChanged)
    Q_PROPERTY(bool daemonRunning READ daemonRunning NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY deviceChanged)
    // Settings.
    Q_PROPERTY(QString monitor READ monitor NOTIFY configChanged)
    Q_PROPERTY(QStringList monitors READ monitors NOTIFY layoutChanged)
    Q_PROPERTY(double monitorAspect READ monitorAspect NOTIFY layoutChanged)
    Q_PROPERTY(int rotation READ rotation NOTIFY configChanged)
    Q_PROPERTY(bool realSize READ realSize NOTIFY configChanged)
    // Real size is on and both physical sizes are known, so the area is computed.
    Q_PROPERTY(bool realActive READ realActive NOTIFY configChanged)
    Q_PROPERTY(double tabletWidthMm READ tabletWidthMm NOTIFY configChanged)
    Q_PROPERTY(double tabletHeightMm READ tabletHeightMm NOTIFY configChanged)
    Q_PROPERTY(double monitorWidthMm READ monitorWidthMm NOTIFY configChanged)
    Q_PROPERTY(double monitorHeightMm READ monitorHeightMm NOTIFY configChanged)
    // Where on the monitor the pad acts, as shares of it.
    Q_PROPERTY(double targetX READ targetX NOTIFY configChanged)
    Q_PROPERTY(double targetY READ targetY NOTIFY configChanged)
    Q_PROPERTY(double targetW READ targetW NOTIFY configChanged)
    Q_PROPERTY(double targetH READ targetH NOTIFY configChanged)
    Q_PROPERTY(double areaX READ areaX NOTIFY configChanged)
    Q_PROPERTY(double areaY READ areaY NOTIFY configChanged)
    Q_PROPERTY(double areaW READ areaW NOTIFY configChanged)
    Q_PROPERTY(double areaH READ areaH NOTIFY configChanged)
    Q_PROPERTY(bool keepProportions READ keepProportions NOTIFY configChanged)
    Q_PROPERTY(double tabletAspect READ tabletAspect NOTIFY configChanged)
    Q_PROPERTY(double curveX1 READ curveX1 NOTIFY configChanged)
    Q_PROPERTY(double curveY1 READ curveY1 NOTIFY configChanged)
    Q_PROPERTY(double curveX2 READ curveX2 NOTIFY configChanged)
    Q_PROPERTY(double curveY2 READ curveY2 NOTIFY configChanged)
    Q_PROPERTY(QString barrel READ barrel NOTIFY configChanged)
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY configChanged)
    Q_PROPERTY(QString saveError READ saveError NOTIFY configChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY configChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY configChanged)
    // Live.
    Q_PROPERTY(double penX READ penX NOTIFY penChanged)
    Q_PROPERTY(double penY READ penY NOTIFY penChanged)
    Q_PROPERTY(double pressure READ pressure NOTIFY penChanged)
    Q_PROPERTY(double tiltX READ tiltX NOTIFY penChanged)
    Q_PROPERTY(double tiltY READ tiltY NOTIFY penChanged)
    Q_PROPERTY(bool penInRange READ penInRange NOTIFY penChanged)
    Q_PROPERTY(bool penDown READ penDown NOTIFY penChanged)
    Q_PROPERTY(bool barrelPressed READ barrelPressed NOTIFY penChanged)
    Q_PROPERTY(bool barrel1 READ barrel1 NOTIFY penChanged)
    Q_PROPERTY(bool barrel2 READ barrel2 NOTIFY penChanged)
    Q_PROPERTY(bool eraser READ eraser NOTIFY penChanged)
    // Names of the pad buttons held right now, modifiers first.
    Q_PROPERTY(QStringList padKeys READ padKeys NOTIFY keysChanged)
    // The chord held on the pad ("" when none), what it makes the desktop type,
    // and which of the six learned buttons it is (-1 when unknown).
    Q_PROPERTY(QString padChord READ padChord NOTIFY keysChanged)
    Q_PROPERTY(QString padSends READ padSends NOTIFY keysChanged)
    Q_PROPERTY(int padSlot READ padSlot NOTIFY keysChanged)
    // The chord of each button, top to bottom; "" where not learned.
    Q_PROPERTY(QStringList padSlots READ padSlots NOTIFY configChanged)
    Q_PROPERTY(QString lastChord READ lastChord NOTIFY chordChanged)

public:
    explicit Backend(const QString &configPath = {}, const QString &stateDirectory = {}, QObject *parent = nullptr,
                     const QString &hyprctl = {});
    ~Backend() override;

    bool penFound() const { return m_penFd >= 0; }
    bool daemonRunning() const { return m_daemonRunning; }
    QString deviceName() const { return m_deviceName; }
    QString problem() const { return m_problem; }

    QString monitor() const { return m_config.monitor; }
    QStringList monitors() const;
    double monitorAspect() const { return layout().aspect(displayMonitor()); }
    bool realSize() const { return m_config.realSize; }
    double targetX() const { return placed().target.x; }
    double targetY() const { return placed().target.y; }
    double targetW() const { return placed().target.w; }
    double targetH() const { return placed().target.h; }
    bool realActive() const { return placed().real; }
    double tabletWidthMm() const { return m_config.tabletWidthMm; }
    double tabletHeightMm() const { return m_config.tabletWidthMm / m_config.tabletAspect; }
    double monitorWidthMm() const { return layout().effective(m_config.monitor).mmW; }
    double monitorHeightMm() const { return layout().effective(m_config.monitor).mmH; }
    int rotation() const { return m_config.rotation; }
    double areaX() const { return placed().area.x; }
    double areaY() const { return placed().area.y; }
    double areaW() const { return placed().area.w; }
    double areaH() const { return placed().area.h; }
    bool keepProportions() const { return m_config.keepProportions; }
    double tabletAspect() const { return m_config.tabletAspect; }
    double curveX1() const { return m_config.curve.x1; }
    double curveY1() const { return m_config.curve.y1; }
    double curveX2() const { return m_config.curve.x2; }
    double curveY2() const { return m_config.curve.y2; }
    QString barrel() const { return m_config.barrel; }
    QVariantList buttons() const;
    QString saveError() const { return m_saveError; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }

    double penX() const { return m_penX; }
    double penY() const { return m_penY; }
    double pressure() const { return m_pressure; }
    double tiltX() const { return m_tiltX; }
    double tiltY() const { return m_tiltY; }
    bool penInRange() const { return m_inRange; }
    bool penDown() const { return m_down; }
    bool barrelPressed() const { return m_barrel1 || m_barrel2; }
    bool barrel1() const { return m_barrel1; }
    bool barrel2() const { return m_barrel2; }
    bool eraser() const { return m_toolRubber; }
    QStringList padKeys() const { return m_padKeys; }
    QString padChord() const;
    QString padSends() const;
    int padSlot() const;
    QStringList padSlots() const;
    QString lastChord() const { return m_lastChord; }

    Q_INVOKABLE void setMonitor(const QString &name);
    Q_INVOKABLE void setRotation(int degrees);
    Q_INVOKABLE void setArea(double x, double y, double w, double h);
    Q_INVOKABLE void setTarget(double x, double y, double w, double h);
    Q_INVOKABLE void resetTarget();
    Q_INVOKABLE void resetArea();
    Q_INVOKABLE void setRealSize(bool on);
    Q_INVOKABLE void setTabletWidthMm(double mm);
    Q_INVOKABLE void setKeepProportions(bool keep);
    Q_INVOKABLE void setTabletAspect(double aspect);
    Q_INVOKABLE void setCurve(double x1, double y1, double x2, double y2);
    Q_INVOKABLE void resetCurve();
    Q_INVOKABLE double curveAt(double pressure) const;
    Q_INVOKABLE void setBarrel(const QString &mode);
    // Empty on success, otherwise what is wrong with the chords typed.
    Q_INVOKABLE QString addButton(const QString &from, const QString &to);
    Q_INVOKABLE void removeButton(int index);
    // Teaches which chord is the pad's button `index`; empty on success, else why not.
    Q_INVOKABLE QString setPadSlot(int index, const QString &chord);
    Q_INVOKABLE void clearPadSlots();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Starts or stops the daemon as a user service, enabled at login.
    Q_INVOKABLE void setDaemonEnabled(bool enabled);

signals:
    void deviceChanged();
    void configChanged();
    void layoutChanged();
    void penChanged();
    void chordChanged();
    void keysChanged();
    // One pen sample, for the test canvas to draw.
    void stroke(double x, double y, double pressure, bool down);

private:
    void commit(Config next, const char *kind);
    Placement placed() const { return placement(m_config, layout()); }
    // The monitor the pad drives: the chosen one, or in real size the first.
    QString displayMonitor() const;
    void applyHistory(Config config);
    void save();
    const TabletMath::Layout &layout() const { return m_layoutWatcher->layout(); }
    void refreshDevices();
    void readState();
    void watchStateFile();
    void readPen();
    void readKeypad();
    void setPadKeys(const QList<int> &codes);
    void closeSources();
    void updateProblem();

    QString m_configPath, m_stateDirectory;
    Config m_config;
    QList<Config> m_undo, m_redo;
    QString m_lastKind;
    QElapsedTimer m_lastEdit;
    QString m_saveError;
    QTimer m_saveTimer, m_pollTimer;
    std::unique_ptr<LayoutWatcher> m_layoutWatcher;
    ServiceController m_service;
    QFileSystemWatcher m_deviceWatcher;

    int m_penFd = -1, m_keypadFd = -1;
    std::unique_ptr<QSocketNotifier> m_penNotifier, m_keypadNotifier;
    int m_openRetries = 0;
    QString m_failedPenPath, m_penPath, m_keypadPath, m_deviceName, m_problem;
    // Three sources, shown in this order: what systemctl said, what the
    // daemon reported, and why the window itself could not open the pen.
    QString m_serviceProblem, m_daemonProblem, m_openProblem;
    bool m_penProcessed = false;
    bool m_daemonRunning = false;
    int m_xMax = 1, m_yMax = 1, m_pressureMax = 1;

    int m_rawX = 0, m_rawY = 0, m_rawPressure = 0, m_rawTiltX = 0, m_rawTiltY = 0;
    double m_penX = 0, m_penY = 0, m_pressure = 0, m_tiltX = 0, m_tiltY = 0;
    bool m_inRange = false, m_down = false, m_barrel1 = false, m_barrel2 = false;
    bool m_toolPen = false, m_toolRubber = false;
    QStringList m_padKeys;
    QList<int> m_padCodes;
    std::set<int> m_keysDown;
    QString m_lastChord;
    int m_stateSequence = -1;
    QFileSystemWatcher m_stateWatcher;
};
