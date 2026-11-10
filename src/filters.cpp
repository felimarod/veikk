#include "filters.h"

#include <algorithm>
#include <cmath>

namespace {

input_event make(const input_event &like, __u16 type, __u16 code, __s32 value) {
    input_event e = like;
    e.type = type;
    e.code = code;
    e.value = value;
    return e;
}

} // namespace

void PenFilter::setRanges(int xMax, int yMax, int pressureMax) {
    m_xMax = std::max(1, xMax);
    m_yMax = std::max(1, yMax);
    m_pressureMax = std::max(1, pressureMax);
}

void PenFilter::configure(const Config &config, const TabletMath::Layout &layout) {
    m_rotation = config.rotation;
    m_curve = config.curve;
    m_barrel = config.barrel;
    m_region = layout.region(config.monitor);
    m_area = config.keepProportions
        ? TabletMath::fitProportions(config.area, config.tabletAspect, layout.aspect(config.monitor), config.rotation)
        : config.area;
}

void PenFilter::feed(const input_event &e, std::vector<input_event> &out) {
    switch (e.type) {
    case EV_ABS:
        if (e.code == ABS_X) {
            m_rawX = e.value;
            m_haveX = m_dirty = true;
        } else if (e.code == ABS_Y) {
            m_rawY = e.value;
            m_haveY = m_dirty = true;
        } else if (e.code == ABS_PRESSURE) {
            const double shaped = TabletMath::applyCurve(m_curve, double(e.value) / m_pressureMax);
            out.push_back(make(e, EV_ABS, ABS_PRESSURE, int(std::lround(shaped * m_pressureMax))));
        } else {
            out.push_back(e); // tilt
        }
        break;
    case EV_KEY:
        if (e.code == BTN_STYLUS) {
            if (m_barrel == "stylus2")
                out.push_back(make(e, EV_KEY, BTN_STYLUS2, e.value));
            else if (m_barrel != "none")
                out.push_back(e);
        } else if (e.code == BTN_STYLUS2) {
            // The second barrel button is only ever reported by some firmware;
            // keep it unless the user turned the barrel off.
            if (m_barrel != "none")
                out.push_back(e);
        } else {
            out.push_back(e); // tool in range, touching
        }
        break;
    case EV_SYN:
        if (e.code != SYN_REPORT)
            break;
        if (m_dirty && m_haveX && m_haveY) {
            const auto p = TabletMath::map({double(m_rawX) / m_xMax, double(m_rawY) / m_yMax}, m_area, m_rotation);
            const double x = m_region.x + p.x * m_region.w, y = m_region.y + p.y * m_region.h;
            out.push_back(make(e, EV_ABS, ABS_X, int(std::lround(std::clamp(x, 0.0, 1.0) * m_xMax))));
            out.push_back(make(e, EV_ABS, ABS_Y, int(std::lround(std::clamp(y, 0.0, 1.0) * m_yMax))));
        }
        m_dirty = false;
        out.push_back(e);
        break;
    default:
        break; // MSC scancodes mean nothing to the virtual tablet
    }
}

void KeyFilter::feed(const input_event &e, std::vector<input_event> &out) {
    if (e.type == EV_KEY) {
        if (e.value == 1)
            m_pressed.insert(e.code);
        else if (e.value == 0)
            m_pressed.erase(e.code);
        return;
    }
    if (e.type != EV_SYN)
        return;
    if (e.code == SYN_DROPPED) {
        m_pressed.clear();
        return;
    }
    if (e.code != SYN_REPORT)
        return;

    std::set<int> wanted = m_pressed;
    QList<int> chord(m_pressed.begin(), m_pressed.end());
    std::sort(chord.begin(), chord.end());
    for (const auto &rule : m_rules) {
        QList<int> from = rule.from;
        std::sort(from.begin(), from.end());
        if (from == chord) {
            wanted = std::set<int>(rule.to.begin(), rule.to.end());
            break;
        }
    }

    // A chord is news only when keys went down, not when they let go.
    if (!chord.isEmpty() && chord != m_previousChord) {
        m_lastChord = chord;
        ++m_sequence;
    }
    m_previousChord = chord;

    bool changed = false;
    for (int code : m_sent)
        if (!wanted.count(code)) {
            out.push_back(make(e, EV_KEY, code, 0));
            changed = true;
        }
    for (int code : wanted)
        if (!m_sent.count(code)) {
            out.push_back(make(e, EV_KEY, code, 1));
            changed = true;
        }
    m_sent = wanted;
    if (changed)
        out.push_back(e);
}
