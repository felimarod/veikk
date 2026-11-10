#include "keynames.h"

#include <linux/input-event-codes.h>

#include <QHash>
#include <algorithm>

namespace {

struct Entry {
    const char *name;
    int code;
};

const Entry kEntries[] = {
    {"ctrl", KEY_LEFTCTRL}, {"shift", KEY_LEFTSHIFT}, {"alt", KEY_LEFTALT}, {"super", KEY_LEFTMETA},
    {"rctrl", KEY_RIGHTCTRL}, {"rshift", KEY_RIGHTSHIFT}, {"ralt", KEY_RIGHTALT}, {"rsuper", KEY_RIGHTMETA},
    {"a", KEY_A}, {"b", KEY_B}, {"c", KEY_C}, {"d", KEY_D}, {"e", KEY_E}, {"f", KEY_F}, {"g", KEY_G},
    {"h", KEY_H}, {"i", KEY_I}, {"j", KEY_J}, {"k", KEY_K}, {"l", KEY_L}, {"m", KEY_M}, {"n", KEY_N},
    {"o", KEY_O}, {"p", KEY_P}, {"q", KEY_Q}, {"r", KEY_R}, {"s", KEY_S}, {"t", KEY_T}, {"u", KEY_U},
    {"v", KEY_V}, {"w", KEY_W}, {"x", KEY_X}, {"y", KEY_Y}, {"z", KEY_Z},
    {"1", KEY_1}, {"2", KEY_2}, {"3", KEY_3}, {"4", KEY_4}, {"5", KEY_5}, {"6", KEY_6}, {"7", KEY_7},
    {"8", KEY_8}, {"9", KEY_9}, {"0", KEY_0},
    {"f1", KEY_F1}, {"f2", KEY_F2}, {"f3", KEY_F3}, {"f4", KEY_F4}, {"f5", KEY_F5}, {"f6", KEY_F6},
    {"f7", KEY_F7}, {"f8", KEY_F8}, {"f9", KEY_F9}, {"f10", KEY_F10}, {"f11", KEY_F11}, {"f12", KEY_F12},
    {"f13", KEY_F13}, {"f14", KEY_F14}, {"f15", KEY_F15}, {"f16", KEY_F16}, {"f17", KEY_F17},
    {"f18", KEY_F18}, {"f19", KEY_F19}, {"f20", KEY_F20}, {"f21", KEY_F21}, {"f22", KEY_F22},
    {"f23", KEY_F23}, {"f24", KEY_F24},
    {"esc", KEY_ESC}, {"enter", KEY_ENTER}, {"space", KEY_SPACE}, {"tab", KEY_TAB},
    {"backspace", KEY_BACKSPACE}, {"delete", KEY_DELETE}, {"insert", KEY_INSERT},
    {"home", KEY_HOME}, {"end", KEY_END}, {"pageup", KEY_PAGEUP}, {"pagedown", KEY_PAGEDOWN},
    {"up", KEY_UP}, {"down", KEY_DOWN}, {"left", KEY_LEFT}, {"right", KEY_RIGHT},
    {"minus", KEY_MINUS}, {"equal", KEY_EQUAL}, {"comma", KEY_COMMA}, {"dot", KEY_DOT},
    {"slash", KEY_SLASH}, {"backslash", KEY_BACKSLASH}, {"semicolon", KEY_SEMICOLON},
    {"apostrophe", KEY_APOSTROPHE}, {"grave", KEY_GRAVE}, {"leftbrace", KEY_LEFTBRACE},
    {"rightbrace", KEY_RIGHTBRACE}, {"capslock", KEY_CAPSLOCK}, {"print", KEY_SYSRQ},
    {"volumeup", KEY_VOLUMEUP}, {"volumedown", KEY_VOLUMEDOWN}, {"mute", KEY_MUTE},
    {"playpause", KEY_PLAYPAUSE}, {"next", KEY_NEXTSONG}, {"previous", KEY_PREVIOUSSONG},
};

const QHash<QString, int> &byName() {
    static const QHash<QString, int> table = [] {
        QHash<QString, int> t;
        for (const auto &e : kEntries)
            t.insert(e.name, e.code);
        return t;
    }();
    return table;
}

const QHash<int, QString> &byCode() {
    static const QHash<int, QString> table = [] {
        QHash<int, QString> t;
        for (const auto &e : kEntries)
            t.insert(e.code, e.name);
        return t;
    }();
    return table;
}

bool isModifier(int c) {
    return c == KEY_LEFTCTRL || c == KEY_RIGHTCTRL || c == KEY_LEFTSHIFT || c == KEY_RIGHTSHIFT
        || c == KEY_LEFTALT || c == KEY_RIGHTALT || c == KEY_LEFTMETA || c == KEY_RIGHTMETA;
}

} // namespace

namespace KeyNames {

QString name(int c) {
    // A code with no friendly name still round-trips as key<number>.
    return byCode().value(c, QStringLiteral("key%1").arg(c));
}

int code(const QString &n) {
    const QString lower = n.trimmed().toLower();
    if (byName().contains(lower))
        return byName().value(lower);
    if (lower.startsWith("key")) {
        bool ok = false;
        const int c = lower.mid(3).toInt(&ok);
        if (ok && c > 0 && c < KEY_MAX)
            return c;
    }
    return 0;
}

QString chord(const QList<int> &codes) {
    if (codes.isEmpty())
        return QStringLiteral("none");
    QList<int> sorted = codes;
    std::sort(sorted.begin(), sorted.end(), [](int a, int b) {
        return isModifier(a) != isModifier(b) ? isModifier(a) : a < b;
    });
    QStringList parts;
    for (int c : sorted)
        parts << name(c);
    return parts.join('+');
}

QList<int> parseChord(const QString &text, bool *ok) {
    if (ok)
        *ok = false;
    const QString trimmed = text.trimmed().toLower();
    if (trimmed == "none" || trimmed.isEmpty()) {
        if (ok)
            *ok = true;
        return {};
    }
    QList<int> codes;
    for (const QString &part : trimmed.split('+', Qt::SkipEmptyParts)) {
        const int c = code(part);
        if (c == 0)
            return {};
        if (!codes.contains(c))
            codes << c;
    }
    if (ok)
        *ok = !codes.isEmpty();
    return codes;
}

} // namespace KeyNames
