#pragma once

// Where the pad lands: which part of the pad is used, and which rectangle of
// the monitor it is spread over. The daemon applies it and the window draws it.

#include "config.h"
#include "tabletmath.h"

struct Placement {
    TabletMath::Area area;   // the part of the pad in use, as shares of the pad
    TabletMath::Area target; // where it lands, as shares of the chosen monitor
    TabletMath::Area region; // the same place, as shares of the whole layout
    bool real = false;       // real size is on and both physical sizes are known
};

// `target` in the config is where the user put the rectangle. Keeping
// proportions reshapes it around its centre so a circle stays a circle; real
// size replaces its size with the physical one, keeping the centre.
Placement placement(const Config &config, const TabletMath::Layout &layout);

// Resizes `target`, keeping its centre, until the shape it covers on a monitor
// of `monitorAspect` has the same proportions as the pad part `area` laid on
// a pad of `tabletAspect`.
TabletMath::Area fitTarget(TabletMath::Area target, const TabletMath::Area &area, double tabletAspect,
                           double monitorAspect, int rotation);
