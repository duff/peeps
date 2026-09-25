#include "tests.h"

#include <QApplication>

#include <QtTest>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
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
    LayoutTests tests;
    status |= QTest::qExec(&tests, argc, argv);
  }
  {
    GuardTests tests;
    status |= QTest::qExec(&tests, argc, argv);
  }
  {
    ShortcutTests tests;
    status |= QTest::qExec(&tests, argc, argv);
  }
  {
    CliTests tests;
    status |= QTest::qExec(&tests, argc, argv);
  }
  return status;
}
