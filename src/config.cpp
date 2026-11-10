#include "config.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

#include "keynames.h"

bool Config::operator==(const Config &o) const {
    return monitor == o.monitor && rotation == o.rotation && area == o.area && target == o.target
        && keepProportions == o.keepProportions && tabletAspect == o.tabletAspect && realSize == o.realSize
        && tabletWidthMm == o.tabletWidthMm && curve == o.curve
        && barrel == o.barrel && buttons == o.buttons && padSlots == o.padSlots;
}

QJsonObject Config::toJson() const {
    QJsonArray padChords;
    for (int i = 0; i < kPadSlots; ++i)
        padChords.append(KeyNames::chord(i < padSlots.size() ? padSlots[i] : QList<int>()));
    QJsonArray rules;
    for (const auto &rule : buttons)
        rules.append(QJsonObject{{"from", KeyNames::chord(rule.from)}, {"to", KeyNames::chord(rule.to)}});
    return {
        {"monitor", monitor},
        {"rotation", rotation},
        {"area", QJsonObject{{"x", area.x}, {"y", area.y}, {"w", area.w}, {"h", area.h}}},
        {"target", QJsonObject{{"x", target.x}, {"y", target.y}, {"w", target.w}, {"h", target.h}}},
        {"keepProportions", keepProportions},
        {"tabletAspect", tabletAspect},
        {"realSize", realSize},
        {"tabletWidthMm", tabletWidthMm},
        {"curve", QJsonObject{{"x1", curve.x1}, {"y1", curve.y1}, {"x2", curve.x2}, {"y2", curve.y2}}},
        {"barrel", barrel},
        {"buttons", rules},
        {"padSlots", padChords},
    };
}

Config Config::fromJson(const QJsonObject &o) {
    Config c;
    auto unit = [](const QJsonObject &obj, const char *key, double fallback) {
        return std::clamp(obj.value(key).toDouble(fallback), 0.0, 1.0);
    };
    c.monitor = o.value("monitor").toString();
    const int rotation = o.value("rotation").toInt(0);
    c.rotation = (rotation == 90 || rotation == 180 || rotation == 270) ? rotation : 0;

    const auto area = o.value("area").toObject();
    c.area.w = std::max(kMinArea, unit(area, "w", 1));
    c.area.h = std::max(kMinArea, unit(area, "h", 1));
    c.area.x = std::min(unit(area, "x", 0), 1 - c.area.w);
    c.area.y = std::min(unit(area, "y", 0), 1 - c.area.h);

    const auto target = o.value("target").toObject();
    c.target.w = std::max(kMinArea, unit(target, "w", 1));
    c.target.h = std::max(kMinArea, unit(target, "h", 1));
    c.target.x = std::min(unit(target, "x", 0), 1 - c.target.w);
    c.target.y = std::min(unit(target, "y", 0), 1 - c.target.h);

    c.keepProportions = o.value("keepProportions").toBool(true);
    const double aspect = o.value("tabletAspect").toDouble(1.5);
    c.tabletAspect = aspect >= 0.2 && aspect <= 5 ? aspect : 1.5;
    c.realSize = o.value("realSize").toBool(false);
    const double width = o.value("tabletWidthMm").toDouble(152.4);
    c.tabletWidthMm = width >= 20 && width <= 1000 ? width : 152.4;

    const auto curve = o.value("curve").toObject();
    c.curve = {unit(curve, "x1", .25), unit(curve, "y1", .25), unit(curve, "x2", .75), unit(curve, "y2", .75)};

    const QString barrel = o.value("barrel").toString("stylus");
    c.barrel = (barrel == "stylus2" || barrel == "none") ? barrel : "stylus";

    for (const auto &value : o.value("buttons").toArray()) {
        const auto rule = value.toObject();
        bool fromOk = false, toOk = false;
        const auto from = KeyNames::parseChord(rule.value("from").toString(), &fromOk);
        const auto to = KeyNames::parseChord(rule.value("to").toString(), &toOk);
        // A rule that matches no button would swallow nothing and mean nothing.
        if (fromOk && toOk && !from.isEmpty())
            c.buttons.append({from, to});
    }
    const auto stored = o.value("padSlots").toArray();
    for (int i = 0; i < kPadSlots; ++i) {
        bool ok = false;
        const auto chord = i < stored.size() ? KeyNames::parseChord(stored[i].toString(), &ok) : QList<int>();
        c.padSlots.append(ok ? chord : QList<int>());
    }
    // All slots empty is the same as not learned.
    if (std::all_of(c.padSlots.begin(), c.padSlots.end(), [](const QList<int> &x) { return x.isEmpty(); }))
        c.padSlots.clear();
    return c;
}

QString Config::defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/veikk/config.json";
}

Config Config::load(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return fromJson(QJsonDocument::fromJson(file.readAll()).object());
}

bool Config::save(const Config &config, const QString &path, QString *error) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(config.toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}
