#pragma once

// The pure geometry and curves behind the daemon: no devices, no Qt Quick, so
// tests can pin every number down.

#include <QByteArray>
#include <QList>
#include <QString>

namespace TabletMath {

// A rectangle of the tablet, as fractions of its width and height.
struct Area {
    double x = 0, y = 0, w = 1, h = 1;
    bool operator==(const Area &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

struct Point {
    double x = 0, y = 0;
};

// A cubic Bezier from (0,0) to (1,1) with two control points, like CSS
// cubic-bezier(). The default is the straight line, so pressure is untouched.
struct Curve {
    double x1 = .25, y1 = .25, x2 = .75, y2 = .75;
    bool operator==(const Curve &o) const { return x1 == o.x1 && y1 == o.y1 && x2 == o.x2 && y2 == o.y2; }
};

double applyCurve(const Curve &curve, double pressure);

// Quarter turns of the unit square, clockwise.
Point rotate(Point p, int degrees);

// Raw tablet position (0..1) to a position on the target (0..1): clamp to the
// area, scale it to fill the target, then rotate.
Point map(Point raw, const Area &area, int rotation);

// Resizes the area, keeping its centre, so that a physical distance on the pad
// is the same on the target in both directions. tabletAspect is the pad's
// width over height; targetAspect is the monitor's.
Area fitProportions(Area area, double tabletAspect, double targetAspect, int rotation);

struct Monitor {
    QString name;
    double x = 0, y = 0, w = 0, h = 0; // logical pixels in the layout
};

// The monitors Hyprland lays out, from `hyprctl monitors -j`.
struct Layout {
    QList<Monitor> monitors;

    static Layout fromHyprctl(const QByteArray &json);
    Monitor bounds() const;
    // The chosen monitor as a fraction of the whole layout; the whole layout
    // when the name is empty or unknown.
    Area region(const QString &monitor) const;
    double aspect(const QString &monitor) const;
};

} // namespace TabletMath
