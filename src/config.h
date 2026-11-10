#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include "tabletmath.h"

// A pad button that sends one chord, remapped to another. An empty `to`
// swallows the button.
struct ButtonRule {
    QList<int> from, to;
    bool operator==(const ButtonRule &o) const { return from == o.from && to == o.to; }
};

// Everything the daemon applies, kept in ~/.config/veikk/config.json. The
// window writes it and the daemon watches it, so a change is live at once.
struct Config {
    static constexpr double kMinArea = 0.05; // smallest share of the pad in use
    static constexpr int kPadSlots = 6;      // the VK640 has six buttons, in a column

    QString monitor;           // empty: the whole layout
    int rotation = 0;          // 0, 90, 180 or 270
    TabletMath::Area area;     // the part of the pad in use
    TabletMath::Area target;   // where it lands, as shares of the monitor
    bool keepProportions = true;
    double tabletAspect = 1.5; // the VK640 pad is 6 x 4 inches
    bool realSize = false;     // pad at its physical size, centred on the monitor
    double tabletWidthMm = 152.4; // 6 inches; the pad reports no size of its own
    TabletMath::Curve curve;
    QString barrel = "stylus"; // "stylus", "stylus2" or "none"
    QList<ButtonRule> buttons;
    // What each of the pad's physical buttons types, top to bottom, learned on
    // the Test page so it can draw which one is held. Empty: not learned yet.
    QList<QList<int>> padSlots;

    bool operator==(const Config &o) const;
    QJsonObject toJson() const;
    // Anything missing or out of range falls back to the default.
    static Config fromJson(const QJsonObject &object);

    static QString defaultPath();
    // A missing or unreadable file is the default config.
    static Config load(const QString &path);
    // Written beside the target and renamed, so the daemon never reads half.
    static bool save(const Config &config, const QString &path, QString *error = nullptr);
};
