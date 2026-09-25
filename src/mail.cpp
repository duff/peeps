#include "mail.h"

namespace contacts {

MailCommand mailCommand(const Contact& contact) {
  MailCommand command;
  const QString email = contact.primaryEmail();
  if (email.isEmpty()) {
    command.error = QStringLiteral("This contact has no email address.");
    return command;
  }
  if (email.contains(QLatin1Char('\n')) || email.contains(QLatin1Char('\r'))) {
    command.error = QStringLiteral("The email address is not usable.");
    return command;
  }
  // A subject becomes mailto:addr?subject=... Omarchy's HEY handler treats
  // everything after mailto: as the address, so the To field stays empty.
  command.argv = {QStringLiteral("xdg-email"), email};
  return command;
}

}  // namespace contacts
