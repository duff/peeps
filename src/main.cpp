#include "instance.h"
#include "mainwindow.h"
#include "worker.h"

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

  const bool selfTest = args.contains(QStringLiteral("--self-test"));
  // The window schedules a sync on the next event-loop turn, and claim()
  // runs an event loop while it asks an already-open copy to come forward.
  // Building the window first starts that sync, then this process exits and
  // tears down Qt's TLS plugin under the sync thread.
  omacontacts::MainWindow* window = nullptr;
  omacontacts::InstanceLock instance(omacontacts::instanceSocketName());
  if (!selfTest && !instance.claim([&window] {
        if (window != nullptr) window->present();
      })) {
    omacontacts::finishBackground(0);
    return 0;
  }
  omacontacts::MainWindow owned;
  window = &owned;
  if (selfTest) {
    owned.resize(980, 680);
    owned.show();
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    const int code = app.exec();
    omacontacts::finishBackground(code);
    return code;
  }
  owned.show();
  const int code = app.exec();
  omacontacts::finishBackground(code);
  return code;
}
