QT += core gui qml quick quickcontrols2

CONFIG += c++17 release
TARGET = veikk
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/config.h \
    src/engine.h \
    src/evdev.h \
    src/filters.h \
    src/keynames.h \
    src/tabletmath.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/config.cpp \
    src/engine.cpp \
    src/evdev.cpp \
    src/filters.cpp \
    src/keynames.cpp \
    src/tabletmath.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
