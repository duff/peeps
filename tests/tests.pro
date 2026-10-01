QT += core gui widgets network testlib
CONFIG += console c++20 warn_on testcase link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += libsecret-1
TARGET = contacts-tests
TEMPLATE = app
INCLUDEPATH += ../src
QMAKE_CXXFLAGS += -Wall -Wextra -Wpedantic

include(../contacts.pri)

SOURCES += main.cpp tests.cpp cli_tests.cpp
HEADERS += tests.h
