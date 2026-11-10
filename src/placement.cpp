#include "placement.h"

#include <algorithm>

using TabletMath::Area;

namespace {

// Moves a rectangle, never resizing it, so it stays inside the unit square.
Area inside(Area a) {
    a.x = std::clamp(a.x, 0.0, 1.0 - a.w);
    a.y = std::clamp(a.y, 0.0, 1.0 - a.h);
    return a;
}

} // namespace

Area fitTarget(Area target, const Area &area, double tabletAspect, double monitorAspect, int rotation) {
    if (target.w <= 0 || target.h <= 0 || area.w <= 0 || area.h <= 0 || monitorAspect <= 0)
        return target;
    // Width over height of the pad part, as it sits on the pad.
    double wanted = area.w * tabletAspect / area.h;
    if (rotation == 90 || rotation == 270)
        wanted = 1 / wanted;
    const double cx = target.x + target.w / 2, cy = target.y + target.h / 2;
    // Keep the width; the height that gives the wanted shape on this monitor.
    double w = target.w, h = w * monitorAspect / wanted;
    if (h > 1) {
        h = 1;
        w = h * wanted / monitorAspect;
    }
    target.w = std::clamp(w, Config::kMinArea * 0.1, 1.0);
    target.h = std::clamp(h, Config::kMinArea * 0.1, 1.0);
    target.x = cx - target.w / 2;
    target.y = cy - target.h / 2;
    return inside(target);
}

Placement placement(const Config &config, const TabletMath::Layout &layout) {
    Placement out;
    const QString monitor = layout.effective(config.monitor).name;
    const Area whole = config.realSize ? layout.region(monitor) : layout.region(config.monitor);
    const double monitorAspect = layout.aspect(config.realSize ? monitor : config.monitor);

    const auto real = config.realSize
        ? TabletMath::realSize(config.tabletWidthMm, config.tabletWidthMm / config.tabletAspect, layout,
                               config.monitor, config.rotation)
        : TabletMath::RealSize();
    if (real.valid) {
        out.real = true;
        out.area = real.area;
        Area target{0, 0, real.region.w / whole.w, real.region.h / whole.h};
        target.x = config.target.x + config.target.w / 2 - target.w / 2;
        target.y = config.target.y + config.target.h / 2 - target.h / 2;
        out.target = inside(target);
    } else {
        out.area = config.area;
        out.target = config.keepProportions
            ? fitTarget(config.target, config.area, config.tabletAspect, monitorAspect, config.rotation)
            : config.target;
    }
    out.region = {whole.x + whole.w * out.target.x, whole.y + whole.h * out.target.y,
                  whole.w * out.target.w, whole.h * out.target.h};
    return out;
}
