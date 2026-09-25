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
  command.argv = {QStringLiteral("xdg-email"), QStringLiteral("--subject"),
                  QStringLiteral("Hello ") + contact.displayName(), email};
  return command;
}

}  // namespace contacts
