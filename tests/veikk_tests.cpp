#include <linux/input-event-codes.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "config.h"
#include "evdev.h"
#include "filters.h"
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
