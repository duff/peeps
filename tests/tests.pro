QT += core gui testlib
CONFIG += console c++20 warn_on testcase
CONFIG -= app_bundle
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
    ../src/mail.cpp
HEADERS += tests.h
QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic
