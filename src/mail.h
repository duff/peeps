#pragma once

#include "contact.h"

#include <QStringList>

namespace contacts {

struct MailCommand {
  QStringList argv;
  QString error;
  bool ok() const { return error.isEmpty(); }
};

// xdg-email follows the desktop mailto handler. On stock Omarchy that is HEY.
// On this machine it is whatever x-scheme-handler/mailto points at.
MailCommand mailCommand(const Contact& contact);

}  // namespace contacts
