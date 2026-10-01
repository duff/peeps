#pragma once

#include <QString>
#include <QStringList>

class QMimeData;

namespace contacts {

// A program to hand work to: the browser, the mail app, LocalSend, Hyprland.
struct DesktopCommand {
  QStringList argv;
  QString error;
  bool ok() const { return error.isEmpty() && !argv.isEmpty(); }
};

// xdg-email follows the desktop mailto handler. Pass only the address.
// Omarchy's HEY handler cannot separate a subject from that address.
DesktopCommand mailCommand(const QString& address);
// A blank message, for when the contact card goes on the clipboard instead.
DesktopCommand composeCommand();
// Thunderbird and its kin take --attach. Webmail handlers such as HEY read
// everything after mailto: as the address.
bool mailHandlerTakesAttachments(const QString& desktopId);
DesktopCommand attachCommand(const QString& path);
DesktopCommand browserCommand(const QString& url, bool haveOmarchy);
// Omarchy shares files with LocalSend, the same as Super+Ctrl+S.
DesktopCommand shareCommand(const QString& path, bool haveOmarchy, bool haveLocalSend);
// Hyprland focuses a window by pid. Lua configs (Hyprland 0.55 and later) take
// hl.dsp.focus. Older configs take focuswindow.
QStringList hyprlandFocusArgs(qint64 pid, bool lua);

bool haveProgram(const QString& name);
bool runDetached(const DesktopCommand& command);
QString mailHandler();
// Asks Hyprland to focus this process's window. A second launch has no input
// serial to activate the first with, so Qt's own request can be ignored.
void focusWindowOnHyprland(qint64 pid);

// A file on the clipboard, as a file manager would put it there, with `text`
// for targets that only paste text.
QMimeData* fileClipboard(const QString& path, const QString& text);
// A file name for a contact card: the person's name without path characters.
QString cardFileName(const QString& displayName);

}  // namespace contacts
