# veikk: the window (veikk) and the background service (veikkd).
#   qmake6 veikk.pro [VERSION=x.y.z] [PREFIX=/usr] && make && make install
TEMPLATE = subdirs
SUBDIRS = daemon app

isEmpty(PREFIX): PREFIX = /usr

license.files = LICENSE
license.path = $$PREFIX/share/licenses/veikk
icon.files = pkgbuild/veikk.svg
icon.path = $$PREFIX/share/icons/hicolor/scalable/apps
desktop.files = pkgbuild/veikk.desktop
desktop.path = $$PREFIX/share/applications
service.files = pkgbuild/veikk.service
service.path = $$PREFIX/lib/systemd/user
rules.files = pkgbuild/70-veikk.rules
rules.path = $$PREFIX/lib/udev/rules.d
INSTALLS += license icon desktop service rules
