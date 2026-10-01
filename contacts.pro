QT += widgets network
CONFIG += c++20 warn_on link_pkgconfig
TARGET = contacts
TEMPLATE = app
INCLUDEPATH += src
PKGCONFIG += libsecret-1
QMAKE_CXXFLAGS += -Wall -Wextra

include(contacts.pri)

SOURCES += src/main.cpp

isEmpty(PREFIX): PREFIX = $$(HOME)/.local
target.path = $$PREFIX/bin
desktop.files = dist/org.omelia.Contacts.desktop
desktop.path = $$PREFIX/share/applications
icon.files = dist/org.omelia.Contacts.svg
icon.path = $$PREFIX/share/icons/hicolor/scalable/apps
INSTALLS += target desktop icon
