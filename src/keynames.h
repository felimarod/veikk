#pragma once

// Names for Linux key codes, so a chord reads "ctrl+shift+z" in the window and
// in the config file instead of as numbers.

#include <QList>
#include <QString>

namespace KeyNames {

QString name(int code);
// 0 when the name is unknown.
int code(const QString &name);

// "ctrl+shift+z": modifiers first, then the rest in code order.
QString chord(const QList<int> &codes);
// Empty and ok == false when any part is unknown; "none" is a valid, empty chord.
QList<int> parseChord(const QString &text, bool *ok = nullptr);

} // namespace KeyNames
