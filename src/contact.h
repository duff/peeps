#pragma once

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

namespace contacts {

struct LabeledValue {
  QString value;
  QString label;
  QString type;

  bool operator==(const LabeledValue& other) const = default;
};

// A calendar date from Google. Year, month, or day may be zero when Google
// stored only part of it. `text` is the free-form fallback.
struct DateValue {
  int year = 0;
  int month = 0;
  int day = 0;
  QString text;

  bool operator==(const DateValue& other) const = default;
  bool isEmpty() const;
  // "December 10, 1815" for reading.
  QString display() const;
  // "1815-12-10", or "12-10" without a year, for typing and for JSON.
  QString isoText() const;
  // Whole years from this date to `today`. Nothing without a full date, or
  // for a date still to come.
  std::optional<int> yearsOn(const QDate& today) const;
  // The age of someone born on this date, for reading: "42", or "5 months"
  // in their first year. Empty when yearsOn() is.
  QString ageOn(const QDate& today) const;
};

struct PostalAddress {
  QString type;
  QString label;
  QString street;
  QString extended;
  QString poBox;
  QString city;
  QString region;
  QString postalCode;
  QString country;
  QString formatted;

  bool operator==(const PostalAddress& other) const = default;
  bool isEmpty() const;
  QString display() const;
};

struct Organization {
  QString name;
  QString title;
  QString department;

  bool operator==(const Organization& other) const = default;
  bool isEmpty() const;
};

struct DatedEvent {
  QString type;
  QString label;
  DateValue date;

  bool operator==(const DatedEvent& other) const = default;
  bool isEmpty() const;
};

struct ImClient {
  QString username;
  QString protocol;
  QString type;
  QString label;

  bool operator==(const ImClient& other) const = default;
  bool isEmpty() const;
  QString display() const;
};

enum class LabelField { Email, Phone, Address, Other };

struct ResolvedLabel {
  QString type;
  QString display;
};

// Map typed or stored text onto Google's type token. Fixed types use Google's
// token. Anything else is kept as the trimmed custom word.
ResolvedLabel resolveLabel(LabelField field, const QString& text);
QString labelDisplay(LabelField field, const LabeledValue& value);

struct Contact {
  QString resourceName;
  QString etag;
  QString prefix;
  QString givenName;
  QString middleName;
  QString familyName;
  QString suffix;
  QString phoneticGiven;
  QString phoneticMiddle;
  QString phoneticFamily;
  QString nickname;
  QString fileAs;
  // Primary company. Further companies live in otherOrganizations.
  QString organization;
  QString title;
  QString department;
  QList<Organization> otherOrganizations;
  QString occupation;
  QString notes;
  QString photoUrl;
  DateValue birthday;
  QList<LabeledValue> emails;
  QList<LabeledValue> phones;
  QList<PostalAddress> addresses;
  QList<LabeledValue> urls;
  QList<LabeledValue> relations;
  QList<DatedEvent> events;
  QList<ImClient> ims;
  QList<LabeledValue> sips;
  QList<LabeledValue> customs;

  QString displayName() const;
  QString initials() const;
  QString primaryEmail() const;
  QString primaryPhone() const;
  QString subtitle() const;
  QString searchText() const;
  // Fields the editor keeps behind More fields until a contact has one.
  bool hasLessCommonFields() const;

  bool operator==(const Contact& other) const = default;
};

struct Draft {
  QString prefix;
  QString givenName;
  QString middleName;
  QString familyName;
  QString suffix;
  QString phoneticGiven;
  QString phoneticMiddle;
  QString phoneticFamily;
  QString nickname;
  QString fileAs;
  QString organization;
  QString title;
  QString department;
  QList<Organization> otherOrganizations;
  QString occupation;
  QString notes;
  DateValue birthday;
  QList<LabeledValue> emails;
  QList<LabeledValue> phones;
  QList<PostalAddress> addresses;
  QList<LabeledValue> urls;
  QList<LabeledValue> relations;
  QList<DatedEvent> events;
  QList<ImClient> ims;
  QList<LabeledValue> sips;
  QList<LabeledValue> customs;

  static Draft fromContact(const Contact& contact);
  QString validate() const;
  Contact toContact(const Contact& base) const;
};

QString digitsOnly(const QString& text);
// Writes a North American number as (312) 555-0100 x12, or +1 (312) 555-0100
// when typed with the country code. Without one, only someone in the US or
// Canada can tell a number is North American, so anywhere else it stays as
// typed. So does anything else that isn't clearly such a number.
QString formatPhone(const QString& text);
QString contactPlainText(const Contact& contact);

}  // namespace contacts
