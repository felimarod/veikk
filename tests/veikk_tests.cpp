#include <linux/input-event-codes.h>
#include <unistd.h>

#include <QDir>
#include <QSaveFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "config.h"
#include "evdev.h"
#include "filters.h"
#include "hyprevents.h"
#include "layoutwatcher.h"
#include "placement.h"
#include "state.h"
#include "engine.h"
#include "keynames.h"
#include "tabletmath.h"
#include "theme.h"
#include "translator.h"

using namespace TabletMath;

namespace {

input_event ev(int type, int code, int value) {
    input_event e{};
    e.type = type;
    e.code = code;
    e.value = value;
    return e;
}

std::vector<input_event> frame(PenFilter &f, std::initializer_list<input_event> events) {
    std::vector<input_event> out;
    for (const auto &e : events)
        f.feed(e, out);
    f.feed(ev(EV_SYN, SYN_REPORT, 0), out);
    return out;
}

int valueOf(const std::vector<input_event> &events, int type, int code) {
    for (const auto &e : events)
        if (e.type == type && e.code == code)
            return e.value;
    return -1;
}

void write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

} // namespace

class VeikkTests : public QObject {
    Q_OBJECT

private slots:
    // ---- geometry ---------------------------------------------------------

    void straightCurveChangesNothing() {
        for (double p : {0.0, 0.1, 0.5, 0.9, 1.0})
            QVERIFY(qAbs(applyCurve(Curve{}, p) - p) < 1e-6);
    }

    void softCurveNeedsMorePressure() {
        const Curve soft{.7, .0, 1., .3};
        QVERIFY(applyCurve(soft, 0.5) < 0.5);
        QCOMPARE(applyCurve(soft, 0.0), 0.0);
        QVERIFY(qAbs(applyCurve(soft, 1.0) - 1.0) < 1e-6);
    }

    void rotationTurnsClockwise() {
        const Point p{0.2, 0.0};
        QCOMPARE(rotate(p, 0).x, 0.2);
        QVERIFY(qAbs(rotate(p, 90).x - 1.0) < 1e-9 && qAbs(rotate(p, 90).y - 0.2) < 1e-9);
        QVERIFY(qAbs(rotate(p, 180).x - 0.8) < 1e-9 && qAbs(rotate(p, 180).y - 1.0) < 1e-9);
        QVERIFY(qAbs(rotate(p, 270).x - 0.0) < 1e-9 && qAbs(rotate(p, 270).y - 0.8) < 1e-9);
        // Four quarter turns come home.
        Point q{0.3, 0.7};
        for (int i = 0; i < 4; ++i)
            q = rotate(q, 90);
        QVERIFY(qAbs(q.x - 0.3) < 1e-9 && qAbs(q.y - 0.7) < 1e-9);
    }

    void areaFillsTheTargetAndClamps() {
        const Area a{0.25, 0.25, 0.5, 0.5};
        const Point mid = map({0.5, 0.5}, a, 0);
        QVERIFY(qAbs(mid.x - 0.5) < 1e-9 && qAbs(mid.y - 0.5) < 1e-9);
        const Point corner = map({0.25, 0.75}, a, 0);
        QVERIFY(qAbs(corner.x) < 1e-9 && qAbs(corner.y - 1) < 1e-9);
        const Point outside = map({0.0, 1.0}, a, 0);
        QCOMPARE(outside.x, 0.0);
        QCOMPARE(outside.y, 1.0);
    }

    void proportionsMatchThePhysicalPad() {
        // A 3:2 pad driving a 16:9 monitor: the full-width area is shortened.
        const Area fitted = fitProportions({0, 0, 1, 1}, 1.5, 16.0 / 9.0, 0);
        QCOMPARE(fitted.w, 1.0);
        QVERIFY(qAbs(fitted.h - (1.5 / (16.0 / 9.0))) < 1e-9);
        QVERIFY(qAbs(fitted.y - (1 - fitted.h) / 2) < 1e-9); // stays centred
        // A tall monitor squeezes the width instead.
        const Area tall = fitProportions({0, 0, 1, 1}, 1.5, 0.5, 0);
        QCOMPARE(tall.h, 1.0);
        QVERIFY(tall.w < 1.0);
    }

    void layoutPicksTheMonitor() {
        const auto layout = Layout::fromHyprctl(
            R"([{"name":"DP-1","x":0,"y":0,"width":2560,"height":1440,"scale":1,"transform":0},
                {"name":"eDP-1","x":2560,"y":0,"width":3840,"height":2400,"scale":2,"transform":0}])");
        QCOMPARE(layout.monitors.size(), 2);
        QCOMPARE(layout.bounds().w, 4480.0);
        const Area dp = layout.region("DP-1");
        QVERIFY(qAbs(dp.w - 2560.0 / 4480) < 1e-9 && dp.x == 0);
        const Area edp = layout.region("eDP-1"); // logical 1920x1200
        QVERIFY(qAbs(edp.x - 2560.0 / 4480) < 1e-9 && qAbs(edp.w - 1920.0 / 4480) < 1e-9);
        const Area all = layout.region("");
        QVERIFY(all.x == 0 && all.w == 1 && all.h == 1);
        QVERIFY(qAbs(layout.aspect("DP-1") - 16.0 / 9.0) < 1e-9);
    }

    // ---- pen filter -------------------------------------------------------

    void penLandsOnTheChosenMonitor() {
        Config config;
        config.monitor = "B";
        config.keepProportions = false;
        const auto layout = Layout::fromHyprctl(
            R"([{"name":"A","x":0,"y":0,"width":1000,"height":1000,"scale":1,"transform":0},
                {"name":"B","x":1000,"y":0,"width":1000,"height":1000,"scale":1,"transform":0}])");
        PenFilter pen;
        pen.setRanges(1000, 1000, 100);
        pen.configure(config, layout);

        auto out = frame(pen, {ev(EV_ABS, ABS_X, 0), ev(EV_ABS, ABS_Y, 0)});
        QCOMPARE(valueOf(out, EV_ABS, ABS_X), 500); // left edge of B is mid-layout
        out = frame(pen, {ev(EV_ABS, ABS_X, 1000), ev(EV_ABS, ABS_Y, 1000)});
        QCOMPARE(valueOf(out, EV_ABS, ABS_X), 1000);
        QCOMPARE(valueOf(out, EV_ABS, ABS_Y), 1000);
    }

    void penWaitsForBothAxes() {
        PenFilter pen;
        pen.setRanges(1000, 1000, 100);
        pen.configure(Config{}, Layout{});
        const auto out = frame(pen, {ev(EV_ABS, ABS_X, 400)});
        QCOMPARE(valueOf(out, EV_ABS, ABS_X), -1);
    }

    void penPressureFollowsTheCurve() {
        Config config;
        config.curve = {.7, .0, 1., .3};
        PenFilter pen;
        pen.setRanges(1000, 1000, 1000);
        pen.configure(config, Layout{});
        const auto out = frame(pen, {ev(EV_ABS, ABS_PRESSURE, 500)});
        const int shaped = valueOf(out, EV_ABS, ABS_PRESSURE);
        QVERIFY(shaped > 0 && shaped < 500);
    }

    void barrelButtonCanBeMovedOrSwitchedOff() {
        PenFilter pen;
        pen.setRanges(1000, 1000, 100);
        Config config;
        pen.configure(config, Layout{});
        QCOMPARE(valueOf(frame(pen, {ev(EV_KEY, BTN_STYLUS, 1)}), EV_KEY, BTN_STYLUS), 1);
        config.barrel = "stylus2";
        pen.configure(config, Layout{});
        auto out = frame(pen, {ev(EV_KEY, BTN_STYLUS, 1)});
        QCOMPARE(valueOf(out, EV_KEY, BTN_STYLUS2), 1);
        QCOMPARE(valueOf(out, EV_KEY, BTN_STYLUS), -1);
        config.barrel = "none";
        pen.configure(config, Layout{});
        out = frame(pen, {ev(EV_KEY, BTN_STYLUS, 1)});
        QCOMPARE(valueOf(out, EV_KEY, BTN_STYLUS), -1);
        QCOMPARE(valueOf(out, EV_KEY, BTN_STYLUS2), -1);
    }

    void penKeepsTouchAndProximity() {
        PenFilter pen;
        pen.setRanges(1000, 1000, 100);
        pen.configure(Config{}, Layout{});
        const auto out = frame(pen, {ev(EV_KEY, BTN_TOOL_PEN, 1), ev(EV_KEY, BTN_TOUCH, 1)});
        QCOMPARE(valueOf(out, EV_KEY, BTN_TOOL_PEN), 1);
        QCOMPARE(valueOf(out, EV_KEY, BTN_TOUCH), 1);
    }

    // ---- pad keys ---------------------------------------------------------

    void unmappedKeysPassThrough() {
        KeyFilter keys;
        std::vector<input_event> out;
        keys.feed(ev(EV_KEY, KEY_A, 1), out);
        keys.feed(ev(EV_SYN, SYN_REPORT, 0), out);
        QCOMPARE(valueOf(out, EV_KEY, KEY_A), 1);
        out.clear();
        keys.feed(ev(EV_KEY, KEY_A, 0), out);
        keys.feed(ev(EV_SYN, SYN_REPORT, 0), out);
        QCOMPARE(valueOf(out, EV_KEY, KEY_A), 0);
    }

    void aChordIsRewrittenAsOne() {
        KeyFilter keys;
        keys.setRules({{{KEY_LEFTCTRL, KEY_Z}, {KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_Z}}});
        std::vector<input_event> out;
        keys.feed(ev(EV_KEY, KEY_LEFTCTRL, 1), out);
        keys.feed(ev(EV_KEY, KEY_Z, 1), out);
        keys.feed(ev(EV_SYN, SYN_REPORT, 0), out);
        QCOMPARE(valueOf(out, EV_KEY, KEY_LEFTSHIFT), 1);
        QCOMPARE(valueOf(out, EV_KEY, KEY_Z), 1);
        // Letting go releases what was sent, not what was pressed.
        out.clear();
        keys.feed(ev(EV_KEY, KEY_LEFTCTRL, 0), out);
        keys.feed(ev(EV_KEY, KEY_Z, 0), out);
        keys.feed(ev(EV_SYN, SYN_REPORT, 0), out);
        QCOMPARE(valueOf(out, EV_KEY, KEY_LEFTSHIFT), 0);
        QCOMPARE(valueOf(out, EV_KEY, KEY_Z), 0);
    }

    void modifiersGoDownFirstAndUpLast() {
        KeyFilter keys;
        keys.setRules({{{KEY_F5}, {KEY_LEFTMETA, KEY_A}}});
        std::vector<input_event> out;
        keys.feed(ev(EV_KEY, KEY_F5, 1), out);
        keys.feed(ev(EV_SYN, SYN_REPORT, 0), out);
        QCOMPARE(int(out[0].code), int(KEY_LEFTMETA)); // KEY_A (30) is numerically lower
        QCOMPARE(int(out[1].code), int(KEY_A));
        out.clear();
        keys.feed(ev(EV_KEY, KEY_F5, 0), out);
        keys.feed(ev(EV_SYN, SYN_REPORT, 0), out);
        QCOMPARE(int(out[0].code), int(KEY_A));
        QCOMPARE(int(out[1].code), int(KEY_LEFTMETA));
    }

    void filtersForgetHeldKeysOnReset() {
        KeyFilter keys;
        std::vector<input_event> out;
        keys.feed(ev(EV_KEY, KEY_A, 1), out);
        keys.feed(ev(EV_SYN, SYN_REPORT, 0), out);
        keys.reset();
        out.clear();
        keys.feed(ev(EV_SYN, SYN_REPORT, 0), out);
        QVERIFY(out.empty()); // no stray release of a key the new device never pressed
    }

    void aButtonCanBeSwallowed() {
        KeyFilter keys;
        keys.setRules({{{KEY_F5}, {}}});
        std::vector<input_event> out;
        keys.feed(ev(EV_KEY, KEY_F5, 1), out);
        keys.feed(ev(EV_SYN, SYN_REPORT, 0), out);
        QVERIFY(out.empty());
    }

    void theNewestChordIsRememberedBeforeRemapping() {
        KeyFilter keys;
        keys.setRules({{{KEY_F5}, {KEY_F6}}});
        std::vector<input_event> out;
        for (int i = 0; i < 2; ++i) { // the same button twice still counts twice
            keys.feed(ev(EV_KEY, KEY_F5, 1), out);
            keys.feed(ev(EV_SYN, SYN_REPORT, 0), out);
            keys.feed(ev(EV_KEY, KEY_F5, 0), out);
            keys.feed(ev(EV_SYN, SYN_REPORT, 0), out);
        }
        QCOMPARE(keys.sequence(), 2);
        QCOMPARE(keys.lastChord(), QList<int>{KEY_F5});
    }

    // ---- names and config -------------------------------------------------

    void chordsRoundTrip() {
        bool ok = false;
        const auto codes = KeyNames::parseChord("Shift+Ctrl+Z", &ok);
        QVERIFY(ok);
        QCOMPARE(KeyNames::chord(codes), QString("ctrl+shift+z"));
        QCOMPARE(KeyNames::chord({}), QString("none"));
        KeyNames::parseChord("ctrl+banana", &ok);
        QVERIFY(!ok);
        QVERIFY(KeyNames::parseChord("none", &ok).isEmpty() && ok);
        QCOMPARE(KeyNames::code("key190"), 190); // unnamed keys survive as numbers
    }

    void configRoundTripsAndRepairsBadValues() {
        QTemporaryDir dir;
        Config c;
        c.monitor = "DP-1";
        c.rotation = 90;
        c.area = {0.1, 0.2, 0.5, 0.4};
        c.keepProportions = false;
        c.curve = {.3, .1, .9, .8};
        c.barrel = "none";
        c.buttons = {{{KEY_LEFTCTRL, KEY_Z}, {KEY_F5}}};
        const QString path = dir.filePath("nested/config.json");
        QVERIFY(Config::save(c, path));
        QVERIFY(Config::load(path) == c);

        write(dir.filePath("bad.json"),
              R"({"rotation": 45, "area": {"x": 3, "w": -1}, "barrel": "laser", "tabletAspect": 0,
                  "buttons": [{"from": "ctrl+nope", "to": "a"}, {"from": "none", "to": "a"}]})");
        const Config repaired = Config::load(dir.filePath("bad.json"));
        QCOMPARE(repaired.rotation, 0);
        QVERIFY(repaired.area.x + repaired.area.w <= 1.0 + 1e-9 && repaired.area.w > 0);
        QCOMPARE(repaired.barrel, QString("stylus"));
        QCOMPARE(repaired.tabletAspect, 1.5);
        QVERIFY(repaired.buttons.isEmpty());
        QVERIFY(Config::load(dir.filePath("missing.json")) == Config{});
    }

    // ---- devices ----------------------------------------------------------

    void procParsingFindsPenAndPadButOnlyVeikk() {
        const QByteArray proc =
            "I: Bus=0003 Vendor=2feb Product=0005 Version=0100\n"
            "N: Name=\"VEIKK.INC VK640 Mouse\"\nH: Handlers=event23 mouse5 \nB: EV=1f\nB: KEY=400 1f0000 0 0 0 0\nB: REL=1943\n\n"
            "I: Bus=0003 Vendor=2feb Product=0005 Version=0100\n"
            "N: Name=\"VEIKK.INC VK640\"\nH: Handlers=event24 mouse6 \nB: EV=1b\nB: KEY=c03 0 0 0 0 0\nB: ABS=d000003\n\n"
            "I: Bus=0003 Vendor=2feb Product=0005 Version=0100\n"
            "N: Name=\"VEIKK.INC VK640 Keyboard\"\nH: Handlers=sysrq kbd event25 \n"
            "B: KEY=1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe\n\n"
            "I: Bus=0003 Vendor=046d Product=c548 Version=0111\n"
            "N: Name=\"Logitech\"\nH: Handlers=sysrq kbd event3 \n"
            "B: KEY=1000000000007 ff9f207ac14057ff febeffdfffefffff fffffffffffffffe\n";
        const auto devices = Evdev::parse(proc);
        QCOMPARE(devices.size(), 2);
        QVERIFY(devices[0].pen && devices[0].path == "/dev/input/event24");
        QVERIFY(devices[1].keypad && devices[1].path == "/dev/input/event25");
    }

    // ---- window backend ---------------------------------------------------

    void backendEditsSaveAndUndo() {
        QTemporaryDir dir;
        const QString path = dir.filePath("config.json");
        {
            Backend backend(path, dir.filePath("state"), nullptr, "/nonexistent");
            backend.setKeepProportions(false);
            backend.setRotation(90);
            QCOMPARE(backend.rotation(), 90);
            QVERIFY(backend.canUndo());
            backend.undo();
            QCOMPARE(backend.rotation(), 0);
            QVERIFY(backend.canRedo());
            backend.redo();
            QCOMPARE(backend.rotation(), 90);
            QVERIFY(backend.addButton("ctrl+z", "none").isEmpty());
            QVERIFY(!backend.addButton("ctrl+z", "wat").isEmpty());
            QVERIFY(!backend.addButton("", "a").isEmpty());
            QCOMPARE(backend.buttons().size(), 1);
            backend.addButton("ctrl+z", "f5"); // the same button replaces its rule
            QCOMPARE(backend.buttons().size(), 1);
        }
        const Config saved = Config::load(path); // saved on the way out
        QCOMPARE(saved.rotation, 90);
        QCOMPARE(saved.buttons.size(), 1);
    }

    void dragsAreOneUndoStep() {
        QTemporaryDir dir;
        Backend backend(dir.filePath("config.json"), dir.filePath("state"), nullptr, "/nonexistent");
        backend.setKeepProportions(false);
        backend.setArea(0.0, 0.0, 0.9, 0.9);
        backend.setArea(0.1, 0.1, 0.8, 0.8);
        backend.setArea(0.2, 0.2, 0.7, 0.7);
        backend.undo();
        QCOMPARE(backend.areaW(), 1.0); // straight back to before the drag
    }

    // ---- theme and language -----------------------------------------------

    // ---- state and daemon lock ---------------------------------------------

    void onlyOneDaemonHoldsTheLock() {
        QTemporaryDir dir;
        QVERIFY(!State::daemonAlive(dir.path()));
        const int first = State::acquireLock(dir.path());
        QVERIFY(first >= 0);
        QVERIFY(State::daemonAlive(dir.path()));
        QCOMPARE(State::acquireLock(dir.path()), -1);
        ::close(first);
        QVERIFY(!State::daemonAlive(dir.path()));
        const int again = State::acquireLock(dir.path());
        QVERIFY(again >= 0);
        ::close(again);
    }

    void aSecondEngineLeavesTheFirstAlone() {
        QTemporaryDir dir;
        const int held = State::acquireLock(dir.path());
        QVERIFY(held >= 0);
        write(dir.path() + "/state.json", R"({"pid":1})");
        {
            Engine second(dir.path() + "/config.json", dir.path());
            QVERIFY(!second.ownsState());
        } // its destructor must not remove the running daemon's state
        QVERIFY(QFile::exists(dir.path() + "/state.json"));
        ::close(held);
    }

    // ---- hyprland ------------------------------------------------------------

    void layoutWatcherReportsOnlyRealChanges() {
        QTemporaryDir dir;
        const QString script = dir.filePath("hyprctl");
        write(script, "#!/bin/sh\ncat " + dir.filePath("monitors.json").toLocal8Bit() + "\n");
        QFile::setPermissions(script, QFile::permissions(script) | QFile::ExeOwner);
        write(dir.filePath("monitors.json"),
              R"([{"name":"A","x":0,"y":0,"width":100,"height":50,"scale":1,"transform":0}])");
        LayoutWatcher watcher(script);
        QSignalSpy changed(&watcher, &LayoutWatcher::changed);
        QTRY_COMPARE(changed.count(), 1);
        QCOMPARE(watcher.layout().monitors.size(), 1);
        watcher.refresh(); // same answer: no news
        QTest::qWait(300);
        QCOMPARE(changed.count(), 1);
        write(dir.filePath("monitors.json"),
              R"([{"name":"A","x":0,"y":0,"width":100,"height":50,"scale":1,"transform":0},
                  {"name":"B","x":100,"y":0,"width":100,"height":50,"scale":1,"transform":0}])");
        watcher.refresh();
        QTRY_COMPARE(changed.count(), 2);
        QCOMPARE(watcher.layout().monitors.size(), 2);
    }

    void hyprlandMonitorEventsWakeTheWatcher() {
        QTemporaryDir dir;
        const QString path = dir.filePath("sock");
        QLocalServer server;
        QVERIFY(server.listen(path));
        HyprEvents events(path);
        QSignalSpy spy(&events, &HyprEvents::monitorsMayHaveChanged);
        QVERIFY(events.connected());
        QVERIFY(server.waitForNewConnection(1000));
        QLocalSocket *client = server.nextPendingConnection();
        client->write("workspace>>1\nfocusedmon>>A,1\n");
        client->flush();
        QTest::qWait(150);
        QCOMPARE(spy.count(), 0); // not about monitors
        client->write("monitoraddedv2>>1,DP-2,x\n");
        client->flush();
        QTRY_COMPARE(spy.count(), 1);
    }

    // ---- keys and config -------------------------------------------------------

    void onlyEmittableKeyNamesAreAccepted() {
        QCOMPARE(KeyNames::code("key30"), 30);
        QCOMPARE(KeyNames::code("key0"), 0);
        QCOMPARE(KeyNames::code("key272"), 0); // BTN_LEFT: a mouse button, not a key
        QVERIFY(KeyNames::emittable(KEY_A));
        QVERIFY(!KeyNames::emittable(BTN_LEFT));
    }

    void badConfigFilesFallBackToDefaults() {
        QTemporaryDir dir;
        write(dir.filePath("empty.json"), "");
        QVERIFY(Config::load(dir.filePath("empty.json")) == Config());
        write(dir.filePath("garbage.json"), "{not json");
        QVERIFY(Config::load(dir.filePath("garbage.json")) == Config());
        const Config wrongTypes = Config::fromJson(QJsonDocument::fromJson(
            R"({"rotation":"left","area":{"w":"big","x":[]},"curve":5,"barrel":7,"buttons":"none"})").object());
        QVERIFY(wrongTypes == Config());
    }

    void areaNeverShrinksBelowTheMinimum() {
        const Config config = Config::fromJson(QJsonDocument::fromJson(R"({"area":{"w":0.001,"h":0}})").object());
        QCOMPARE(config.area.w, Config::kMinArea);
        QCOMPARE(config.area.h, Config::kMinArea);
    }

    void saveReportsAnUnwritablePlace() {
        QString error;
        QVERIFY(!Config::save(Config(), "/proc/veikk/config.json", &error));
        QVERIFY(!error.isEmpty());
    }

    // ---- translation -----------------------------------------------------------

    void pluralCategoriesFollowCldr() {
        QCOMPARE(Translator::pluralCategory("en", 1), QString("one"));
        QCOMPARE(Translator::pluralCategory("en", 2), QString("other"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
        QCOMPARE(Translator::pluralCategory("ru", 21), QString("one"));
        QCOMPARE(Translator::pluralCategory("ru", 3), QString("few"));
        QCOMPARE(Translator::pluralCategory("ru", 5), QString("many"));
    }

    void spanishCatalogCoversEverySourceString() {
        // Every tr()/qsTr() literal in src/ must have an entry, or the window
        // silently shows English for it.
        const QDir sources(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath() + "/../src");
        QFile catalogFile(sources.filePath("i18n/es-MX.json"));
        QVERIFY(catalogFile.open(QIODevice::ReadOnly));
        const QJsonObject catalog = QJsonDocument::fromJson(catalogFile.readAll()).object();
        static const QRegularExpression call("(?<![A-Za-z_])(?:qsTr|tr)\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
        QStringList missing;
        for (const QString &name : sources.entryList({"*.cpp", "*.qml"})) {
            QFile file(sources.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly));
            auto matches = call.globalMatch(QString::fromUtf8(file.readAll()));
            while (matches.hasNext()) {
                QString text = matches.next().captured(1);
                text.replace("\\\"", "\"");
                if (!catalog.contains(text))
                    missing << name + ": " + text;
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join("\n")));
        // The daemon's formats are translated by the window.
        for (const char *format : {"Cannot open %1: %2", "Cannot open /dev/uinput: %1",
                                   "Cannot create the virtual device: %1", "Cannot set up the virtual device: %1"})
            QVERIFY2(catalog.contains(QString::fromLatin1(format)), format);
    }

    // ---- real size -------------------------------------------------------------

    void realSizeCentresThePadAtItsPhysicalSize() {
        // A 330 x 210 mm screen of 1920 x 1200 px, and a 150 x 100 mm pad.
        const auto layout = Layout::fromHyprctl(
            R"([{"name":"S","x":0,"y":0,"width":1920,"height":1200,"scale":1,"transform":0,
                 "physicalWidth":330,"physicalHeight":210}])");
        const auto r = realSize(150, 100, layout, "S", 0);
        QVERIFY(r.valid);
        QCOMPARE(r.area.w, 1.0);
        QCOMPARE(r.area.h, 1.0);
        QVERIFY(qAbs(r.region.w - 150.0 / 330) < 1e-9 && qAbs(r.region.h - 100.0 / 210) < 1e-9);
        QVERIFY(qAbs(r.region.x - (1 - 150.0 / 330) / 2) < 1e-9); // centred both ways
        QVERIFY(qAbs(r.region.y - (1 - 100.0 / 210) / 2) < 1e-9);
        // The same distance on the pad and on the screen, in millimetres.
        QVERIFY(qAbs(r.region.w * 330 - 150) < 1e-9 && qAbs(r.region.h * 210 - 100) < 1e-9);
    }

    void realSizeUsesOnlyWhatFitsOnASmallScreen() {
        const auto layout = Layout::fromHyprctl(
            R"([{"name":"S","x":0,"y":0,"width":800,"height":600,"scale":1,"transform":0,
                 "physicalWidth":100,"physicalHeight":150}])");
        const auto r = realSize(200, 100, layout, "S", 0);
        QVERIFY(r.valid);
        QVERIFY(qAbs(r.area.w - 0.5) < 1e-9 && r.area.h == 1.0); // half the pad's width fits
        QVERIFY(qAbs(r.area.x - 0.25) < 1e-9);                   // the middle half
        QCOMPARE(r.region.w, 1.0);
        QVERIFY(qAbs(r.region.h - 100.0 / 150) < 1e-9);
    }

    void realSizeFollowsRotation() {
        const auto layout = Layout::fromHyprctl(
            R"([{"name":"S","x":0,"y":0,"width":1000,"height":1000,"scale":1,"transform":0,
                 "physicalWidth":200,"physicalHeight":200}])");
        const auto r = realSize(150, 100, layout, "S", 90); // pad turned: 100 wide, 150 tall on screen
        QVERIFY(qAbs(r.region.w - 0.5) < 1e-9 && qAbs(r.region.h - 0.75) < 1e-9);
    }

    void realSizeNeedsPhysicalSizes() {
        const auto layout = Layout::fromHyprctl(
            R"([{"name":"S","x":0,"y":0,"width":1000,"height":1000,"scale":1,"transform":0}])");
        QVERIFY(!realSize(150, 100, layout, "S", 0).valid);
        QVERIFY(!realSize(150, 100, Layout(), "", 0).valid);
    }

    void penLandsInTheCentredRegionInRealSize() {
        Config config;
        config.realSize = true;
        config.tabletWidthMm = 100;
        config.tabletAspect = 1.0;
        const auto layout = Layout::fromHyprctl(
            R"([{"name":"S","x":0,"y":0,"width":1000,"height":1000,"scale":1,"transform":0,
                 "physicalWidth":200,"physicalHeight":200}])");
        PenFilter pen;
        pen.setRanges(1000, 1000, 100);
        pen.configure(config, layout);
        auto out = frame(pen, {ev(EV_ABS, ABS_X, 0), ev(EV_ABS, ABS_Y, 0)});
        QCOMPARE(valueOf(out, EV_ABS, ABS_X), 250); // a quarter in: the pad is half the screen
        out = frame(pen, {ev(EV_ABS, ABS_X, 1000), ev(EV_ABS, ABS_Y, 1000)});
        QCOMPARE(valueOf(out, EV_ABS, ABS_X), 750);
        QCOMPARE(valueOf(out, EV_ABS, ABS_Y), 750);
    }

    void realSizeSettingsRoundTrip() {
        Config config;
        config.realSize = true;
        config.tabletWidthMm = 200;
        QVERIFY(Config::fromJson(config.toJson()) == config);
        QCOMPARE(Config::fromJson(QJsonDocument::fromJson(R"({"tabletWidthMm":5})").object()).tabletWidthMm, 152.4);
    }

    // ---- placement on the screen -----------------------------------------------

    void theTargetMovesWhereThePadActs() {
        Config config;
        config.keepProportions = false;
        config.target = {0.5, 0.25, 0.5, 0.5}; // the bottom-right-ish quarter
        const auto layout = Layout::fromHyprctl(
            R"([{"name":"S","x":0,"y":0,"width":1000,"height":1000,"scale":1,"transform":0}])");
        PenFilter pen;
        pen.setRanges(1000, 1000, 100);
        pen.configure(config, layout);
        auto out = frame(pen, {ev(EV_ABS, ABS_X, 0), ev(EV_ABS, ABS_Y, 0)});
        QCOMPARE(valueOf(out, EV_ABS, ABS_X), 500);
        QCOMPARE(valueOf(out, EV_ABS, ABS_Y), 250);
        out = frame(pen, {ev(EV_ABS, ABS_X, 1000), ev(EV_ABS, ABS_Y, 1000)});
        QCOMPARE(valueOf(out, EV_ABS, ABS_X), 1000);
        QCOMPARE(valueOf(out, EV_ABS, ABS_Y), 750);
    }

    void keepingProportionsReshapesTheTarget() {
        // A 3:2 pad on a 2:1 screen: the target keeps its width and gets the
        // height that makes a circle on the pad a circle on the screen.
        const Area fitted = fitTarget({0.25, 0.25, 0.5, 0.5}, {0, 0, 1, 1}, 1.5, 2.0, 0);
        QVERIFY(qAbs(fitted.w - 0.5) < 1e-9);
        QVERIFY(qAbs(fitted.h - 0.5 * 2.0 / 1.5) < 1e-9);
        QVERIFY(qAbs((fitted.x + fitted.w / 2) - 0.5) < 1e-9); // same centre
        QVERIFY(qAbs((fitted.y + fitted.h / 2) - 0.5) < 1e-9);
        // Too tall to fit: the height caps at the screen and the width follows.
        const Area capped = fitTarget({0, 0, 1, 1}, {0, 0, 1, 1}, 1.5, 3.0, 0);
        QCOMPARE(capped.h, 1.0);
        QVERIFY(qAbs(capped.w - 1.5 / 3.0) < 1e-9);
    }

    void realSizeKeepsTheChosenCentre() {
        Config config;
        config.realSize = true;
        config.tabletWidthMm = 100;
        config.tabletAspect = 1.0;
        config.target = {0.7, 0.7, 0.2, 0.2}; // centred at (0.8, 0.8)
        const auto layout = Layout::fromHyprctl(
            R"([{"name":"S","x":0,"y":0,"width":1000,"height":1000,"scale":1,"transform":0,
                 "physicalWidth":200,"physicalHeight":200}])");
        const Placement where = placement(config, layout);
        QVERIFY(where.real);
        QVERIFY(qAbs(where.target.w - 0.5) < 1e-9 && qAbs(where.target.h - 0.5) < 1e-9);
        QVERIFY(qAbs(where.target.x - 0.5) < 1e-9); // clamped to stay on the screen
        QCOMPARE(where.area.w, 1.0);
    }

    void theWindowShowsTheButtonsTheDaemonReports() {
        QTemporaryDir dir;
        const int held = State::acquireLock(dir.path()); // stands in for a running daemon
        QVERIFY(held >= 0);
        write(dir.filePath("state.json"), R"({"pid":1,"pressed":[44,29],"sequence":0,"chord":"none"})");
        Backend backend(dir.filePath("config.json"), dir.path(), nullptr, "/nonexistent");
        QCOMPARE(backend.padKeys(), QStringList({"ctrl", "z"})); // modifiers first
        // The daemon replaces the file with a rename, as QSaveFile does.
        QSaveFile next(dir.filePath("state.json"));
        QVERIFY(next.open(QIODevice::WriteOnly));
        next.write(R"({"pid":1,"pressed":[],"sequence":1,"chord":"z"})");
        QVERIFY(next.commit());
        QTRY_VERIFY(backend.padKeys().isEmpty());
        ::close(held);
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void translatorFollowsTheLanguageFile() {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }
};

QTEST_MAIN(VeikkTests)
#include "veikk_tests.moc"
