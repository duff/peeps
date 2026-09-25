#include "contact.h"

#include <QStringList>

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

struct FixedLabel {
  const char* id;
  const char* display;
  bool email;
  bool phone;
  bool address;
};

const FixedLabel kFixedLabels[] = {
    {"home", "Home", true, true, true},
    {"work", "Work", true, true, true},
    {"other", "Other", true, true, true},
    {"mobile", "Mobile", false, true, false},
    {"main", "Main", false, true, false},
    {"googleVoice", "Google Voice", false, true, false},
    {"homeFax", "Home Fax", false, true, false},
    {"workFax", "Work Fax", false, true, false},
    {"otherFax", "Other Fax", false, true, false},
    {"pager", "Pager", false, true, false},
    {"workMobile", "Work Mobile", false, true, false},
    {"workPager", "Work Pager", false, true, false},
};

struct LabelAlias {
  const char* folded;
  const char* id;
  bool email;
  bool phone;
};

const LabelAlias kLabelAliases[] = {
    {"personal", "home", true, false},
    {"cell", "mobile", false, true},
};

QString foldLabel(QString text) {
  text = text.trimmed().toLower();
  QString folded;
  folded.reserve(text.size());
  for (const QChar c : text) {
    if (c.isSpace() || c == QLatin1Char('-') || c == QLatin1Char('_')) continue;
    folded.append(c);
  }
  return folded;
}

bool labelApplies(LabelField field, bool email, bool phone, bool address) {
  switch (field) {
    case LabelField::Email:
      return email;
    case LabelField::Phone:
      return phone;
    case LabelField::Address:
      return address;
    case LabelField::Other:
      return false;
  }
  return false;
}

const FixedLabel* fixedLabel(LabelField field, const QString& id) {
  for (const FixedLabel& label : kFixedLabels) {
    if (labelApplies(field, label.email, label.phone, label.address) && id == QLatin1String(label.id)) return &label;
  }
  return nullptr;
}

QString digitsOnly(const QString& text) {
  QString out;
  out.reserve(text.size());
  for (const QChar c : text) {
    if (c.isDigit()) out.append(c);
  }
  return out;
}

bool DateValue::isEmpty() const { return year <= 0 && month <= 0 && day <= 0 && text.trimmed().isEmpty(); }

QString DateValue::display() const {
  static const char* kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                  "July",    "August",   "September", "October", "November", "December"};
  if (month >= 1 && month <= 12 && day >= 1 && day <= 31) {
    QString shown = QStringLiteral("%1 %2").arg(QLatin1String(kMonths[month - 1])).arg(day);
    if (year > 0) shown += QStringLiteral(", %1").arg(year);
    return shown;
  }
  return text.trimmed();
}

bool PostalAddress::isEmpty() const {
  return street.trimmed().isEmpty() && extended.trimmed().isEmpty() && poBox.trimmed().isEmpty() && city.trimmed().isEmpty() &&
         region.trimmed().isEmpty() && postalCode.trimmed().isEmpty() && country.trimmed().isEmpty() && formatted.trimmed().isEmpty();
}

QString PostalAddress::display() const {
  QStringList lines;
  auto add = [&](const QString& line) {
    const QString value = line.trimmed();
    if (!value.isEmpty()) lines.append(value);
  };
  add(poBox.isEmpty() ? QString() : QStringLiteral("PO Box %1").arg(poBox.trimmed()));
  add(street);
  add(extended);
  QString cityLine = city.trimmed();
  const QString regionText = region.trimmed();
  const QString postal = postalCode.trimmed();
  if (!regionText.isEmpty()) {
    if (!cityLine.isEmpty()) cityLine += QStringLiteral(", ");
    cityLine += regionText;
  }
  if (!postal.isEmpty()) {
    if (!cityLine.isEmpty()) cityLine += QLatin1Char(' ');
    cityLine += postal;
  }
  add(cityLine);
  add(country);
  if (!lines.isEmpty()) return lines.join(QLatin1Char('\n'));
  return formatted.trimmed();
}

bool Organization::isEmpty() const { return name.trimmed().isEmpty() && title.trimmed().isEmpty() && department.trimmed().isEmpty(); }

bool DatedEvent::isEmpty() const { return date.isEmpty(); }

bool ImClient::isEmpty() const { return username.trimmed().isEmpty(); }

QString ImClient::display() const {
  const QString who = username.trimmed();
  const QString via = !label.trimmed().isEmpty() ? label.trimmed() : protocol.trimmed();
  if (via.isEmpty()) return who;
  if (who.isEmpty()) return via;
  return who + QStringLiteral(" · ") + via;
}

ResolvedLabel resolveLabel(LabelField field, const QString& text) {
  const QString trimmedText = text.trimmed();
  if (field == LabelField::Other) {
    if (trimmedText.isEmpty()) return {QStringLiteral("other"), QStringLiteral("Other")};
    return {trimmedText, trimmedText};
  }
  // Google leaves email, phone, and address type unset when the label is blank.
  if (trimmedText.isEmpty()) return {};
  const QString folded = foldLabel(trimmedText);
  if (field != LabelField::Address) {
    for (const LabelAlias& alias : kLabelAliases) {
      if ((field == LabelField::Email ? alias.email : alias.phone) && folded == QLatin1String(alias.folded)) {
        const FixedLabel* fixed = fixedLabel(field, QString::fromLatin1(alias.id));
        if (fixed != nullptr) return {QString::fromLatin1(fixed->id), QString::fromLatin1(fixed->display)};
      }
    }
  }
  for (const FixedLabel& label : kFixedLabels) {
    if (!labelApplies(field, label.email, label.phone, label.address)) continue;
    if (folded == foldLabel(QString::fromLatin1(label.id)) || folded == foldLabel(QString::fromLatin1(label.display))) {
      return {QString::fromLatin1(label.id), QString::fromLatin1(label.display)};
    }
  }
  return {trimmedText, trimmedText};
}

QString labelDisplay(LabelField field, const LabeledValue& value) {
  if (field == LabelField::Other) {
    if (!value.label.trimmed().isEmpty()) return value.label.trimmed();
    return value.type.trimmed();
  }
  const QString token = value.type.isEmpty() ? value.label : value.type;
  if (token.trimmed().isEmpty()) return {};
  const ResolvedLabel resolved = resolveLabel(field, token);
  if (resolved.type.isEmpty()) return {};
  const FixedLabel* fixed = fixedLabel(field, resolved.type);
  if (fixed == nullptr) return !value.label.isEmpty() ? value.label : value.type;
  if (!value.label.isEmpty() && foldLabel(value.label) != foldLabel(QString::fromLatin1(fixed->id)) &&
      foldLabel(value.label) != foldLabel(QString::fromLatin1(fixed->display))) {
    return value.label;
  }
  return QString::fromLatin1(fixed->display);
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

QString Contact::searchText() const {
  QStringList parts = {organization, title, department, occupation, notes, nickname, fileAs, middleName, prefix, suffix,
                       phoneticGiven, phoneticMiddle, phoneticFamily, birthday.display()};
  for (const auto& org : otherOrganizations) parts << org.name << org.title << org.department;
  for (const auto& address : addresses) {
    QString shown = address.display();
    shown.replace(QLatin1Char('\n'), QLatin1Char(' '));
    parts << shown;
  }
  for (const auto& url : urls) parts << url.value;
  for (const auto& relation : relations) parts << relation.value << relation.label;
  for (const auto& event : events) parts << event.date.display() << event.label;
  for (const auto& im : ims) parts << im.display();
  for (const auto& sip : sips) parts << sip.value;
  for (const auto& custom : customs) parts << custom.label << custom.value;
  return parts.join(QLatin1Char(' '));
}

bool Contact::hasLessCommonFields() const {
  if (!prefix.trimmed().isEmpty() || !middleName.trimmed().isEmpty() || !suffix.trimmed().isEmpty() ||
      !nickname.trimmed().isEmpty() || !title.trimmed().isEmpty() || !department.trimmed().isEmpty() ||
      !occupation.trimmed().isEmpty() || !birthday.isEmpty()) {
    return true;
  }
  if (!otherOrganizations.isEmpty() || !urls.isEmpty() || !relations.isEmpty() || !events.isEmpty() || !customs.isEmpty()) {
    return true;
  }
  return false;
}

Draft Draft::fromContact(const Contact& contact) {
  Draft draft;
  draft.prefix = contact.prefix;
  draft.givenName = contact.givenName;
  draft.middleName = contact.middleName;
  draft.familyName = contact.familyName;
  draft.suffix = contact.suffix;
  draft.phoneticGiven = contact.phoneticGiven;
  draft.phoneticMiddle = contact.phoneticMiddle;
  draft.phoneticFamily = contact.phoneticFamily;
  draft.nickname = contact.nickname;
  draft.fileAs = contact.fileAs;
  draft.organization = contact.organization;
  draft.title = contact.title;
  draft.department = contact.department;
  draft.otherOrganizations = contact.otherOrganizations;
  draft.occupation = contact.occupation;
  draft.notes = contact.notes;
  draft.birthday = contact.birthday;
  draft.emails = contact.emails;
  draft.phones = contact.phones;
  draft.addresses = contact.addresses;
  draft.urls = contact.urls;
  draft.relations = contact.relations;
  draft.events = contact.events;
  draft.ims = contact.ims;
  draft.sips = contact.sips;
  draft.customs = contact.customs;
  return draft;
}

QString Draft::validate() const {
  if (trimmed(givenName).isEmpty() && trimmed(familyName).isEmpty() && trimmed(organization).isEmpty()) {
    return QStringLiteral("Enter a name or a company.");
  }
  for (const auto& email : emails) {
    const QString value = trimmed(email.value);
    if (value.isEmpty()) continue;
    if (!validEmail(value)) return QStringLiteral("Check the email address “%1”.").arg(value);
  }
  return {};
}

QString contactPlainText(const Contact& contact) {
  QStringList lines;
  const QString full = contact.displayName();
  if (full != QLatin1String("Unnamed")) lines.append(full);
  for (const auto& email : contact.emails) {
    const QString value = email.value.trimmed();
    if (!value.isEmpty()) lines.append(value);
  }
  for (const auto& phone : contact.phones) {
    const QString value = phone.value.trimmed();
    if (!value.isEmpty()) lines.append(value);
  }
  for (const auto& address : contact.addresses) {
    const QString value = address.display().trimmed();
    if (!value.isEmpty()) lines.append(value);
  }
  return lines.join(QLatin1Char('\n'));
}

Contact Draft::toContact(const Contact& base) const {
  Contact contact = base;
  contact.prefix = trimmed(prefix);
  contact.givenName = trimmed(givenName);
  contact.middleName = trimmed(middleName);
  contact.familyName = trimmed(familyName);
  contact.suffix = trimmed(suffix);
  contact.phoneticGiven = trimmed(phoneticGiven);
  contact.phoneticMiddle = trimmed(phoneticMiddle);
  contact.phoneticFamily = trimmed(phoneticFamily);
  contact.nickname = trimmed(nickname);
  contact.fileAs = trimmed(fileAs);
  contact.organization = trimmed(organization);
  contact.title = trimmed(title);
  contact.department = trimmed(department);
  contact.otherOrganizations.clear();
  for (const auto& org : otherOrganizations) {
    if (!org.isEmpty()) {
      Organization kept;
      kept.name = trimmed(org.name);
      kept.title = trimmed(org.title);
      kept.department = trimmed(org.department);
      contact.otherOrganizations.append(kept);
    }
  }
  contact.occupation = trimmed(occupation);
  contact.birthday = birthday;
  contact.notes = notes;
  while (!contact.notes.isEmpty() && (contact.notes.back() == QLatin1Char('\n') || contact.notes.back() == QLatin1Char('\r'))) {
    contact.notes.chop(1);
  }
  contact.emails.clear();
  for (const auto& email : emails) {
    const QString value = trimmed(email.value);
    if (value.isEmpty()) continue;
    contact.emails.append(LabeledValue{value, trimmed(email.label), trimmed(email.type)});
  }
  contact.phones.clear();
  for (const auto& phone : phones) {
    const QString value = trimmed(phone.value);
    if (value.isEmpty()) continue;
    contact.phones.append(LabeledValue{value, trimmed(phone.label), trimmed(phone.type)});
  }
  contact.addresses.clear();
  for (const auto& address : addresses) {
    if (!address.isEmpty()) contact.addresses.append(address);
  }
  auto keepLabeled = [](QList<LabeledValue>& dest, const QList<LabeledValue>& source) {
    dest.clear();
    for (const auto& value : source) {
      if (trimmed(value.value).isEmpty() && trimmed(value.label).isEmpty()) continue;
      dest.append(LabeledValue{trimmed(value.value), trimmed(value.label), trimmed(value.type)});
    }
  };
  keepLabeled(contact.urls, urls);
  keepLabeled(contact.relations, relations);
  keepLabeled(contact.sips, sips);
  keepLabeled(contact.customs, customs);
  contact.events.clear();
  for (const auto& event : events) {
    if (!event.isEmpty()) contact.events.append(event);
  }
  contact.ims.clear();
  for (const auto& im : ims) {
    if (!im.isEmpty()) contact.ims.append(im);
  }
  return contact;
}

}  // namespace omacontacts
