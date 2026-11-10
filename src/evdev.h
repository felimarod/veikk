#pragma once

// Finding the tablet's input devices and talking to them through evdev.

#include <QByteArray>
#include <QList>
#include <QString>

namespace Evdev {

constexpr quint16 kVeikkVendor = 0x2feb;
inline const char *kVirtualTablet = "Veikk Virtual Tablet";
inline const char *kVirtualKeypad = "Veikk Virtual Keypad";

struct Device {
    QString name;
    QString path;       // /dev/input/eventN
    bool pen = false;   // reports pen position and pressure
    bool keypad = false; // the pad's keys, which show up as a keyboard
    bool virtualDevice = false; // one of ours, made by the daemon
};

// Parses /proc/bus/input/devices, keeping the Veikk hardware's pen and keys
// and the daemon's own virtual devices; its mouse interface is left alone.
QList<Device> parse(const QByteArray &procText);
QList<Device> scan();

bool bitSet(const QString &mask, int bit);

// Opens read-only and non-blocking; -1 on failure. With `grab`, the kernel
// stops delivering the device to everyone else until the fd closes.
int open(const QString &path, bool grab = false);

struct AbsRange {
    int min = 0, max = 0, resolution = 0;
    bool valid() const { return max > min; }
};
AbsRange absRange(int fd, int code);

} // namespace Evdev
