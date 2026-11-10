# The daemon's code: devices, filters, geometry and config. Core Qt only, so
# the background service never loads Qt Quick.
QT += core
CONFIG += c++17
DEFINES += QT_DEPRECATED_WARNINGS
isEmpty(VERSION): VERSION = 0.1.0
DEFINES += VEIKK_VERSION=\\\"$$VERSION\\\"

INCLUDEPATH += $$PWD/src

CORE_HEADERS = config engine evdev filters hyprevents keynames layoutwatcher placement state tabletmath
for(name, CORE_HEADERS) {
    HEADERS += $$PWD/src/$${name}.h
    SOURCES += $$PWD/src/$${name}.cpp
}
