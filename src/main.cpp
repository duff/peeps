#include "cli.h"
#include "instance.h"
#include "mainwindow.h"
#include "version.h"
#include "worker.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QStyleFactory>
#include <QTextStream>
#include <QTimer>

#include <unistd.h>

namespace {

void describeApp() {
  QCoreApplication::setApplicationName(QStringLiteral("Contacts"));
  QCoreApplication::setOrganizationName(QStringLiteral("omelia"));
  QCoreApplication::setApplicationVersion(QString::fromLatin1(contacts::kVersion));
}

}  // namespace

int main(int argc, char** argv) {
  QStringList args;
  args.reserve(argc > 0 ? argc - 1 : 0);
  for (int i = 1; i < argc; ++i) args.append(QString::fromLocal8Bit(argv[i]));
  if (contacts::isCliInvocation(args)) {
    QCoreApplication app(argc, argv);
    describeApp();
    contacts::CliLaunch launch;
    launch.paths = contacts::AppPaths::forEnv(QDir::homePath(), qEnvironmentVariable("XDG_CONFIG_HOME"),
                                              qEnvironmentVariable("XDG_DATA_HOME"), qEnvironmentVariable("XDG_CACHE_HOME"));
    launch.stdoutIsTty = isatty(STDOUT_FILENO) == 1;
    const contacts::CliResult result = contacts::runContactsCommand(args, launch);
    QTextStream(stdout) << result.out;
    QTextStream(stderr) << result.err;
    return result.code;
  }

  QApplication app(argc, argv);
  describeApp();
  // The Wayland app id, which Hyprland shows as the window class. It matches
  // the desktop file, so the launcher finds the window's name and icon.
  QApplication::setDesktopFileName(QStringLiteral("org.omelia.Contacts"));
  if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) app.setStyle(fusion);

  const bool selfTest = QCoreApplication::arguments().contains(QStringLiteral("--self-test"));
  // A second launch hands its activation token to the open window and exits
  // before it builds a window, so it never starts a sync of its own.
  contacts::MainWindow* window = nullptr;
  contacts::InstanceLock instance(contacts::instanceSocketName());
  const auto raise = [&window](const QString& token) {
    if (window != nullptr) window->present(token);
  };
  if (!selfTest && !instance.claim(raise, qEnvironmentVariable("XDG_ACTIVATION_TOKEN"))) {
    contacts::finishBackground(0);
    return 0;
  }
  contacts::MainWindow owned;
  window = &owned;
  owned.show();
  if (selfTest) QTimer::singleShot(0, &app, &QCoreApplication::quit);
  const int code = app.exec();
  contacts::finishBackground(code);
  return code;
}
