#include "instance.h"
#include "mainwindow.h"

#include <QApplication>
#include <QFont>
#include <QFontInfo>
#include <QStyle>
#include <QStyleFactory>
#include <QTextStream>
#include <QTimer>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Omacontacts"));
  QApplication::setOrganizationName(QStringLiteral("omelia"));
  QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
  QApplication::setDesktopFileName(QStringLiteral("omacontacts"));
  if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) app.setStyle(fusion);
  QFont font(QStringLiteral("JetBrainsMono Nerd Font"));
  font.setPixelSize(12);
  if (!QFontInfo(font).family().isEmpty()) app.setFont(font);

  const QStringList args = QCoreApplication::arguments();
  if (args.contains(QStringLiteral("--version"))) {
    QTextStream(stdout) << "omacontacts 0.1.0\n";
    return 0;
  }

  omacontacts::MainWindow window;
  const bool selfTest = args.contains(QStringLiteral("--self-test"));
  omacontacts::InstanceLock instance(omacontacts::instanceSocketName());
  if (!selfTest && !instance.claim([&window] { window.present(); })) return 0;
  if (selfTest) {
    window.resize(980, 680);
    window.show();
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    return app.exec();
  }
  window.show();
  return app.exec();
}
