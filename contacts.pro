QT += widgets network
CONFIG += c++20 warn_on
TARGET = contacts
TEMPLATE = app
INCLUDEPATH += src
CONFIG += link_pkgconfig
PKGCONFIG += libsecret-1
QMAKE_CXXFLAGS += -Wall -Wextra

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/dialogs.cpp \
    src/blockingnam.cpp \
    src/secrets.cpp \
    src/theme.cpp \
    src/contact.cpp \
    src/search.cpp \
    src/oauth.cpp \
    src/google.cpp \
    src/store.cpp \
    src/shortcuts.cpp \
    src/mail.cpp \
    src/photos.cpp \
    src/instance.cpp

HEADERS += \
    src/mainwindow.h \
    src/dialogs.h \
    src/blockingnam.h \
    src/secrets.h \
    src/theme.h \
    src/contact.h \
    src/search.h \
    src/oauth.h \
    src/google.h \
    src/store.h \
    src/shortcuts.h \
    src/mail.h \
    src/photos.h \
    src/http.h \
    src/net.h \
    src/worker.h \
    src/instance.h

PREFIX = $$(HOME)/.local
target.path = $$PREFIX/bin
desktop.files = dist/contacts.desktop
desktop.path = $$PREFIX/share/applications
icon.files = dist/contacts.svg
icon.path = $$PREFIX/share/icons/hicolor/scalable/apps
INSTALLS += target desktop icon
