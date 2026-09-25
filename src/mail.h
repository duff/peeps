#pragma once

#include "contact.h"

#include <QStringList>

namespace contacts {

struct MailCommand {
  QStringList argv;
  QString error;
  bool ok() const { return error.isEmpty(); }
};

// xdg-email follows the desktop mailto handler. Pass only the address.
// Omarchy's HEY handler cannot separate a subject from that address.
MailCommand mailCommand(const Contact& contact);

}  // namespace contacts
