#include "contact.h"

namespace omacontacts {
namespace {

QString trimmed(const QString& text) { return text.trimmed(); }

bool validEmail(const QString& value) {
  const int at = value.indexOf(QLatin1Char('@'));
  if (at <= 0 || at != value.lastIndexOf(QLatin1Char('@')) || at + 1 >= value.size()) return false;
  return !value.contains(QLatin1Char(' ')) && !value.contains(QLatin1Char('\n')) && !value.contains(QLatin1Char('\r')) &&
         !value.contains(QLatin1Char('\t'));
}

}  // namespace

QString digitsOnly(const QString& text) {
  QString out;
  out.reserve(text.size());
  for (const QChar c : text) {
    if (c.isDigit()) out.append(c);
  }
  return out;
}

QString Contact::displayName() const {
  QString joined = trimmed(givenName);
  const QString family = trimmed(familyName);
  if (!joined.isEmpty() && !family.isEmpty()) joined.append(QLatin1Char(' '));
  joined.append(family);
  if (!joined.isEmpty()) return joined;
  if (!trimmed(organization).isEmpty()) return trimmed(organization);
  if (!emails.isEmpty() && !emails.first().value.isEmpty()) return emails.first().value;
  return QStringLiteral("Unnamed");
}

QString Contact::initials() const {
  const auto take = [](QString& letters, const QString& word) {
    if (word.isEmpty() || letters.size() >= 2) return;
    letters.append(word.at(0));
  };
  QString letters;
  if (!trimmed(givenName).isEmpty() || !trimmed(familyName).isEmpty()) {
    take(letters, trimmed(givenName));
    take(letters, trimmed(familyName));
  } else {
    const QString name = displayName();
    const QStringList parts = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (!parts.isEmpty()) take(letters, parts.at(0));
    if (parts.size() > 1) take(letters, parts.at(1));
  }
  if (letters.isEmpty()) return QStringLiteral("?");
  return letters.toUpper();
}

QString Contact::primaryEmail() const { return emails.isEmpty() ? QString() : emails.first().value; }

QString Contact::primaryPhone() const { return phones.isEmpty() ? QString() : phones.first().value; }

QString Contact::subtitle() const {
  if (!organization.isEmpty() && !title.isEmpty()) return title + QStringLiteral(" · ") + organization;
  if (!organization.isEmpty()) return organization;
  if (!title.isEmpty()) return title;
  return primaryEmail();
}

Draft Draft::fromContact(const Contact& contact) {
  Draft draft;
  draft.givenName = contact.givenName;
  draft.familyName = contact.familyName;
  draft.organization = contact.organization;
  draft.title = contact.title;
  draft.notes = contact.notes;
  draft.emails = contact.emails;
  draft.phones = contact.phones;
  return draft;
}

QString Draft::validate() const {
  if (trimmed(givenName).isEmpty() && trimmed(familyName).isEmpty() && trimmed(organization).isEmpty()) {
    return QStringLiteral("Enter a name or an organization.");
  }
  for (const auto& email : emails) {
    const QString value = trimmed(email.value);
    if (value.isEmpty()) continue;
    if (!validEmail(value)) return QStringLiteral("Check the email address “%1”.").arg(value);
  }
  return {};
}

Contact Draft::toContact(const Contact& base) const {
  Contact contact = base;
  contact.givenName = trimmed(givenName);
  contact.familyName = trimmed(familyName);
  contact.organization = trimmed(organization);
  contact.title = trimmed(title);
  contact.notes = notes;
  while (!contact.notes.isEmpty() && (contact.notes.back() == QLatin1Char('\n') || contact.notes.back() == QLatin1Char('\r'))) {
    contact.notes.chop(1);
  }
  contact.emails.clear();
  for (const auto& email : emails) {
    const QString value = trimmed(email.value);
    if (value.isEmpty()) continue;
    contact.emails.append(LabeledValue{value, trimmed(email.label)});
  }
  contact.phones.clear();
  for (const auto& phone : phones) {
    const QString value = trimmed(phone.value);
    if (value.isEmpty()) continue;
    contact.phones.append(LabeledValue{value, trimmed(phone.label)});
  }
  return contact;
}

}  // namespace omacontacts
