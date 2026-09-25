#include "tests.h"

#include <QGuiApplication>

#include <QtTest>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  int status = 0;
  {
    ThemeTests tests;
    status |= QTest::qExec(&tests, argc, argv);
  }
  {
    SearchTests tests;
    status |= QTest::qExec(&tests, argc, argv);
  }
  {
    GoogleTests tests;
    status |= QTest::qExec(&tests, argc, argv);
  }
  {
    ShortcutTests tests;
    status |= QTest::qExec(&tests, argc, argv);
  }
  return status;
}
