# The window's code, on top of the core.
QT += gui qml quick quickcontrols2
include($$PWD/core.pri)

UI_HEADERS = backend servicecontroller theme translator
for(name, UI_HEADERS) {
    HEADERS += $$PWD/src/$${name}.h
    SOURCES += $$PWD/src/$${name}.cpp
}
