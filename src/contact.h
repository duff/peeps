#pragma once

#include <QList>
#include <QString>

namespace omacontacts {

struct LabeledValue {
  QString value;
  QString label;

  bool operator==(const LabeledValue& other) const = default;
};

struct Contact {
  QString resourceName;
  QString etag;
  QString givenName;
  QString familyName;
  QString organization;
  QString title;
  QString notes;
  QString photoUrl;
  QList<LabeledValue> emails;
  QList<LabeledValue> phones;

  QString displayName() const;
  QString initials() const;
  QString primaryEmail() const;
  QString primaryPhone() const;
  QString subtitle() const;

  bool operator==(const Contact& other) const = default;
};

struct Draft {
  QString givenName;
  QString familyName;
  QString organization;
  QString title;
  QString notes;
  QList<LabeledValue> emails;
  QList<LabeledValue> phones;

  static Draft fromContact(const Contact& contact);
  QString validate() const;
  Contact toContact(const Contact& base) const;
};

QString digitsOnly(const QString& text);

}  // namespace omacontacts
