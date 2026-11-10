TEMPLATE = app
TARGET = veikkd
CONFIG += release
QT -= gui
include($$PWD/../core.pri)
SOURCES += $$PWD/../src/daemon_main.cpp
DESTDIR = $$OUT_PWD/..

isEmpty(PREFIX): PREFIX = /usr
target.path = $$PREFIX/bin
INSTALLS += target
