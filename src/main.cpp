#include "cli.h"
#include "instance.h"
#include "mainwindow.h"
#include "worker.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFont>
#include <QFontInfo>
#include <QStyle>
#include <QStyleFactory>
#include <QTextStream>
#include <QTimer>

#include <unistd.h>

int main(int argc, char** argv) {
  QStringList args;
  args.reserve(argc > 0 ? argc - 1 : 0);
  for (int i = 1; i < argc; ++i) args.append(QString::fromLocal8Bit(argv[i]));
  if (contacts::isCliInvocation(args)) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Contacts"));
    QCoreApplication::setOrganizationName(QStringLiteral("omelia"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    contacts::CliLaunch launch;
    launch.paths = contacts::AppPaths::forEnv(QDir::homePath(), qEnvironmentVariable("XDG_CONFIG_HOME"),
                                              qEnvironmentVariable("XDG_DATA_HOME"), qEnvironmentVariable("XDG_CACHE_HOME"));
    launch.stdoutIsTty = isatty(STDOUT_FILENO) == 1;
    const contacts::CliResult result = contacts::runContactsCommand(args, launch);
    QTextStream out(stdout);
    QTextStream err(stderr);
    out << result.out;
    err << result.err;
    return result.code;
  }

  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Contacts"));
  QApplication::setOrganizationName(QStringLiteral("omelia"));
  QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
  QApplication::setDesktopFileName(QStringLiteral("contacts"));
  if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) app.setStyle(fusion);
  QFont font(QStringLiteral("JetBrainsMono Nerd Font"));
  font.setPixelSize(12);
  if (!QFontInfo(font).family().isEmpty()) app.setFont(font);

  const bool selfTest = QCoreApplication::arguments().contains(QStringLiteral("--self-test"));
  // The window schedules a sync on the next event-loop turn, and claim()
  // runs an event loop while it asks an already-open copy to come forward.
  // Building the window first starts that sync, then this process exits and
  // tears down Qt's TLS plugin under the sync thread.
  contacts::MainWindow* window = nullptr;
  contacts::InstanceLock instance(contacts::instanceSocketName());
  if (!selfTest && !instance.claim([&window] {
        if (window != nullptr) window->present();
      })) {
    contacts::finishBackground(0);
    return 0;
  }
  contacts::MainWindow owned;
  window = &owned;
  if (selfTest) {
    owned.resize(980, 680);
    owned.show();
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    const int code = app.exec();
    contacts::finishBackground(code);
    return code;
  }
  owned.show();
  const int code = app.exec();
  contacts::finishBackground(code);
  return code;
}
