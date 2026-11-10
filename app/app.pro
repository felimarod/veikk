TEMPLATE = app
TARGET = veikk
CONFIG += release
include($$PWD/../ui.pri)
SOURCES += $$PWD/../src/main.cpp
RESOURCES += $$PWD/../src/resources.qrc
DESTDIR = $$OUT_PWD/..

isEmpty(PREFIX): PREFIX = /usr
target.path = $$PREFIX/bin
INSTALLS += target
