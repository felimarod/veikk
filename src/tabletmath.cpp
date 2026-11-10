#include "tabletmath.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace TabletMath {

static double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

double applyCurve(const Curve &c, double p) {
    p = clamp01(p);
    // x(t) is monotonic while both control x values sit in 0..1, so bisection
    // finds the t whose x is the input pressure.
    const double x1 = clamp01(c.x1), x2 = clamp01(c.x2);
    auto bezier = [](double a, double b, double t) {
        const double u = 1 - t;
        return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t;
    };
    double lo = 0, hi = 1;
    for (int i = 0; i < 40; ++i) {
        const double mid = (lo + hi) / 2;
        (bezier(x1, x2, mid) < p ? lo : hi) = mid;
    }
    return clamp01(bezier(clamp01(c.y1), clamp01(c.y2), (lo + hi) / 2));
}

Point rotate(Point p, int degrees) {
    switch (((degrees % 360) + 360) % 360) {
    case 90: return {1 - p.y, p.x};
    case 180: return {1 - p.x, 1 - p.y};
    case 270: return {p.y, 1 - p.x};
    default: return p;
    }
}

Point map(Point raw, const Area &a, int rotation) {
    const double w = a.w > 0 ? a.w : 1, h = a.h > 0 ? a.h : 1;
    return rotate({clamp01((raw.x - a.x) / w), clamp01((raw.y - a.y) / h)}, rotation);
}

Area fitProportions(Area a, double tabletAspect, double targetAspect, int rotation) {
    if (tabletAspect <= 0 || targetAspect <= 0)
        return a;
    // A quarter turn swaps which pad edge runs along the monitor's width.
    const double wanted = (rotation % 180 == 0) ? targetAspect : 1 / targetAspect;
    const double cx = a.x + a.w / 2, cy = a.y + a.h / 2;
    double w = a.w, h = w * tabletAspect / wanted;
    if (h > 1) {
        h = 1;
        w = wanted * h / tabletAspect;
    }
    if (w > 1) {
        w = 1;
        h = w * tabletAspect / wanted;
    }
    Area out;
    out.w = w;
    out.h = h;
    out.x = std::clamp(cx - w / 2, 0.0, 1 - w);
    out.y = std::clamp(cy - h / 2, 0.0, 1 - h);
    return out;
}

Layout Layout::fromHyprctl(const QByteArray &json) {
    Layout layout;
    for (const auto &value : QJsonDocument::fromJson(json).array()) {
        const auto o = value.toObject();
        const double scale = o.value("scale").toDouble(1) > 0 ? o.value("scale").toDouble(1) : 1;
        double w = o.value("width").toDouble() / scale, h = o.value("height").toDouble() / scale;
        // Odd transforms are the sideways ones, which swap the logical size.
        if (o.value("transform").toInt() % 2 == 1)
            std::swap(w, h);
        if (w > 0 && h > 0)
            layout.monitors.append({o.value("name").toString(), o.value("x").toDouble(), o.value("y").toDouble(), w, h});
    }
    return layout;
}

Monitor Layout::bounds() const {
    if (monitors.isEmpty())
        return {{}, 0, 0, 1, 1};
    double x0 = monitors[0].x, y0 = monitors[0].y, x1 = x0 + monitors[0].w, y1 = y0 + monitors[0].h;
    for (const auto &m : monitors) {
        x0 = std::min(x0, m.x);
        y0 = std::min(y0, m.y);
        x1 = std::max(x1, m.x + m.w);
        y1 = std::max(y1, m.y + m.h);
    }
    return {{}, x0, y0, x1 - x0, y1 - y0};
}

Area Layout::region(const QString &monitor) const {
    const Monitor box = bounds();
    for (const auto &m : monitors)
        if (m.name == monitor)
            return {(m.x - box.x) / box.w, (m.y - box.y) / box.h, m.w / box.w, m.h / box.h};
    return {0, 0, 1, 1};
}

double Layout::aspect(const QString &monitor) const {
    for (const auto &m : monitors)
        if (m.name == monitor)
            return m.w / m.h;
    const Monitor box = bounds();
    return box.w / box.h;
}

} // namespace TabletMath
