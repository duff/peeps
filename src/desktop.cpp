#include "desktop.h"

#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace contacts {

DesktopCommand mailCommand(const QString& address) {
  DesktopCommand command;
  const QString email = address.trimmed();
  if (email.isEmpty()) {
    command.error = QStringLiteral("This contact has no email address.");
    return command;
  }
  if (email.contains(QLatin1Char('\n')) || email.contains(QLatin1Char('\r')) || !email.contains(QLatin1Char('@'))) {
    command.error = QStringLiteral("The email address is not usable.");
    return command;
  }
  // A subject becomes mailto:addr?subject=... Omarchy's HEY handler treats
  // everything after mailto: as the address, so the To field stays empty.
  command.argv = {QStringLiteral("xdg-email"), email};
  return command;
}

DesktopCommand composeCommand() { return {{QStringLiteral("xdg-email")}, {}}; }

bool mailHandlerTakesAttachments(const QString& desktopId) {
  const QString id = desktopId.toLower();
  for (const char* client : {"thunderbird", "betterbird", "evolution", "kmail", "claws-mail"}) {
    if (id.contains(QLatin1String(client))) return true;
  }
  return false;
}

DesktopCommand attachCommand(const QString& path) { return {{QStringLiteral("xdg-email"), QStringLiteral("--attach"), path}, {}}; }

DesktopCommand browserCommand(const QString& url, bool haveOmarchy) {
  if (haveOmarchy) return {{QStringLiteral("omarchy"), QStringLiteral("launch"), QStringLiteral("browser"), url}, {}};
  return {{QStringLiteral("xdg-open"), url}, {}};
}

DesktopCommand shareCommand(const QString& path, bool haveOmarchy, bool haveLocalSend) {
  if (haveOmarchy) return {{QStringLiteral("omarchy"), QStringLiteral("share"), QStringLiteral("file"), path}, {}};
  if (haveLocalSend) {
    return {{QStringLiteral("localsend"), QStringLiteral("--headless"), QStringLiteral("send"), path}, {}};
  }
  return {{}, QStringLiteral("Install LocalSend to share contacts.")};
}

QStringList hyprlandFocusArgs(qint64 pid, bool lua) {
  const QString target = QStringLiteral("pid:%1").arg(pid);
  if (lua) return {QStringLiteral("dispatch"), QStringLiteral("hl.dsp.focus({ window = \"%1\" })").arg(target)};
  return {QStringLiteral("dispatch"), QStringLiteral("focuswindow"), target};
}

bool haveProgram(const QString& name) { return !QStandardPaths::findExecutable(name).isEmpty(); }

bool runDetached(const DesktopCommand& command) {
  return command.ok() && QProcess::startDetached(command.argv.first(), command.argv.mid(1));
}

QString mailHandler() {
  QProcess query;
  query.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), QStringLiteral("x-scheme-handler/mailto")});
  if (!query.waitForFinished(2000)) return {};
  return QString::fromUtf8(query.readAllStandardOutput()).trimmed();
}

void focusWindowOnHyprland(qint64 pid) {
  if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE") || !haveProgram(QStringLiteral("hyprctl"))) return;
  auto* lua = new QProcess;
  QObject::connect(lua, &QProcess::finished, lua, [lua, pid] {
    if (QString::fromUtf8(lua->readAllStandardOutput()).trimmed() != QLatin1String("ok")) {
      QProcess::startDetached(QStringLiteral("hyprctl"), hyprlandFocusArgs(pid, false));
    }
    lua->deleteLater();
  });
  QObject::connect(lua, &QProcess::errorOccurred, lua, &QObject::deleteLater);
  lua->start(QStringLiteral("hyprctl"), hyprlandFocusArgs(pid, true));
}

QMimeData* fileClipboard(const QString& path, const QString& text) {
  auto* data = new QMimeData;
  const QUrl url = QUrl::fromLocalFile(path);
  data->setUrls({url});
  // Nautilus and GTK apps read this one. Chromium reads text/uri-list.
  data->setData(QStringLiteral("x-special/gnome-copied-files"), "copy\n" + url.toEncoded());
  data->setText(text);
  return data;
}

QString cardFileName(const QString& displayName) {
  QString name;
  name.reserve(displayName.size());
  for (const QChar c : displayName.trimmed()) {
    if (c == QLatin1Char('/') || c == QLatin1Char('\\') || c.category() == QChar::Other_Control) continue;
    name.append(c);
  }
  while (name.startsWith(QLatin1Char('.'))) name.remove(0, 1);
  if (name.isEmpty()) name = QStringLiteral("Contact");
  return name + QStringLiteral(".vcf");
}

}  // namespace contacts
