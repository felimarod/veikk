#pragma once

// What the daemon does to the events it reads, with no devices in sight: feed
// in what the tablet sent, get out what the virtual devices should send.

#include <linux/input.h>

#include <QList>
#include <set>
#include <vector>

#include "config.h"
#include "tabletmath.h"

class PenFilter {
public:
    // The ranges of the physical pen, which the virtual tablet copies.
    void setRanges(int xMax, int yMax, int pressureMax);
    void configure(const Config &config, const TabletMath::Layout &layout);

    void feed(const input_event &event, std::vector<input_event> &out);

    // Forget half-read frames, e.g. after the pen was unplugged.
    void reset() { m_rawX = m_rawY = 0; m_haveX = m_haveY = m_dirty = false; }

private:
    TabletMath::Area m_area;     // the part of the pad in use
    TabletMath::Area m_region;   // where it lands, as a share of the layout
    TabletMath::Curve m_curve;
    int m_rotation = 0;
    QString m_barrel = "stylus";
    int m_xMax = 32767, m_yMax = 32767, m_pressureMax = 8191;
    int m_rawX = 0, m_rawY = 0;
    bool m_haveX = false, m_haveY = false, m_dirty = false;
};

class KeyFilter {
public:
    void setRules(const QList<ButtonRule> &rules) { m_rules = rules; }

    void feed(const input_event &event, std::vector<input_event> &out);

    // The newest chord the pad produced, before any remapping; `seq` counts
    // them so a caller can tell a repeat of the same chord from no news.
    QList<int> lastChord() const { return m_lastChord; }
    int sequence() const { return m_sequence; }
    // The buttons held right now, before any remapping.
    QList<int> pressed() const { return QList<int>(m_pressed.begin(), m_pressed.end()); }

    // Forget held keys, e.g. after the pad was unplugged. The newest chord and
    // its counter stay, so the window does not see a reset as news.
    void reset() { m_pressed.clear(); m_sent.clear(); m_previousChord.clear(); }

private:
    QList<ButtonRule> m_rules;
    std::set<int> m_pressed, m_sent;
    QList<int> m_lastChord, m_previousChord;
    int m_sequence = 0;
};
