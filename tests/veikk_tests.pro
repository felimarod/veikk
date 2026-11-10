QT += core gui quick quickcontrols2 testlib
CONFIG += c++17 testcase
TARGET = veikk_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/config.h ../src/engine.h ../src/evdev.h ../src/filters.h \
    ../src/keynames.h ../src/tabletmath.h ../src/theme.h ../src/translator.h
SOURCES += veikk_tests.cpp ../src/backend.cpp ../src/config.cpp ../src/engine.cpp ../src/evdev.cpp \
    ../src/filters.cpp ../src/keynames.cpp ../src/tabletmath.cpp ../src/theme.cpp ../src/translator.cpp
