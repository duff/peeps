QT += core gui widgets network testlib
CONFIG += console c++20 warn_on testcase link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += libsecret-1
TARGET = omacontacts-tests
TEMPLATE = app
INCLUDEPATH += ../src
SOURCES += \
    main.cpp \
    tests.cpp \
    ../src/theme.cpp \
    ../src/contact.cpp \
    ../src/search.cpp \
    ../src/oauth.cpp \
    ../src/google.cpp \
    ../src/store.cpp \
    ../src/shortcuts.cpp \
    ../src/mail.cpp \
    ../src/mainwindow.cpp \
    ../src/dialogs.cpp \
    ../src/photos.cpp \
    ../src/blockingnam.cpp \
    ../src/secrets.cpp
HEADERS += tests.h ../src/mainwindow.h ../src/photos.h
QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
