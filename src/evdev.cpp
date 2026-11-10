#include "evdev.h"

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <QFile>
#include <QRegularExpression>

namespace Evdev {

bool bitSet(const QString &mask, int bit) {
    const QStringList words = mask.split(' ', Qt::SkipEmptyParts);
    const int index = bit / 64;
    if (index >= words.size())
        return false;
    // The kernel prints the highest word first.
    const quint64 word = words[words.size() - 1 - index].toULongLong(nullptr, 16);
    return (word >> (bit % 64)) & 1;
}

QList<Device> parse(const QByteArray &text) {
    QList<Device> found;
    const QRegularExpression ids(R"(Vendor=([0-9a-f]+))");
    for (const QString &block : QString::fromUtf8(text).split("\n\n", Qt::SkipEmptyParts)) {
        quint16 vendor = 0;
        QString name, handlers, key, abs;
        for (const QString &line : block.split('\n')) {
            if (line.startsWith("I:")) {
                const auto m = ids.match(line);
                vendor = m.hasMatch() ? m.captured(1).toUShort(nullptr, 16) : 0;
            } else if (line.startsWith("N: Name=")) {
                name = line.mid(8).remove('"');
            } else if (line.startsWith("H: Handlers=")) {
                handlers = line.mid(12);
            } else if (line.startsWith("B: KEY=")) {
                key = line.mid(7);
            } else if (line.startsWith("B: ABS=")) {
                abs = line.mid(7);
            }
        }
        const bool ours = name == kVirtualTablet || name == kVirtualKeypad;
        if (vendor != kVeikkVendor && !ours)
            continue;
        const auto event = QRegularExpression(R"(\b(event\d+)\b)").match(handlers);
        if (!event.hasMatch())
            continue;

        Device d;
        d.name = name;
        d.path = "/dev/input/" + event.captured(1);
        d.virtualDevice = ours;
        d.pen = bitSet(abs, ABS_X) && bitSet(key, BTN_TOOL_PEN);
        d.keypad = !d.pen && bitSet(key, KEY_A) && bitSet(key, KEY_LEFTCTRL);
        if (d.pen || d.keypad)
            found.append(d);
    }
    return found;
}

QList<Device> scan() {
    QFile file("/proc/bus/input/devices");
    return file.open(QIODevice::ReadOnly) ? parse(file.readAll()) : QList<Device>();
}

int open(const QString &path, bool grab) {
    const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return -1;
    if (grab && ioctl(fd, EVIOCGRAB, 1) < 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

AbsRange absRange(int fd, int code) {
    input_absinfo info{};
    if (ioctl(fd, EVIOCGABS(code), &info) < 0)
        return {};
    return {info.minimum, info.maximum, info.resolution};
}

} // namespace Evdev
