#include "vcard.h"

#include <QStringList>

namespace contacts {
namespace {

QString escaped(QString text) {
  text.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
  text.replace(QLatin1Char(','), QStringLiteral("\\,"));
  text.replace(QLatin1Char(';'), QStringLiteral("\\;"));
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\\n"));
  text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
  text.remove(QLatin1Char('\r'));
  return text;
}

QString components(const QStringList& parts) {
  QStringList out;
  out.reserve(parts.size());
  for (const QString& part : parts) out.append(escaped(part.trimmed()));
  return out.join(QLatin1Char(';'));
}

// Folds after 75 bytes without splitting a UTF-8 sequence.
void appendFolded(QByteArray& card, const QByteArray& line) {
  int start = 0;
  int limit = 75;
  while (line.size() - start > limit) {
    int cut = start + limit;
    while (cut > start && (static_cast<unsigned char>(line.at(cut)) & 0xC0) == 0x80) --cut;
    card += line.mid(start, cut - start) + "\r\n ";
    start = cut;
    limit = 74;
  }
  card += line.mid(start) + "\r\n";
}

// vCard writes a date without a year as --MM-DD.
QString dateText(const DateValue& date) {
  if (date.month < 1 || date.month > 12 || date.day < 1 || date.day > 31) return {};
  return date.year > 0 ? date.isoText() : QStringLiteral("--") + date.isoText();
}

QString emailTypes(const LabeledValue& email) {
  if (email.type == QLatin1String("home")) return QStringLiteral("INTERNET,HOME");
  if (email.type == QLatin1String("work")) return QStringLiteral("INTERNET,WORK");
  return QStringLiteral("INTERNET");
}

QString phoneTypes(const LabeledValue& phone) {
  static const QList<QPair<QLatin1String, QLatin1String>> kinds = {
      {QLatin1String("mobile"), QLatin1String("CELL")},         {QLatin1String("home"), QLatin1String("HOME,VOICE")},
      {QLatin1String("work"), QLatin1String("WORK,VOICE")},     {QLatin1String("main"), QLatin1String("VOICE,PREF")},
      {QLatin1String("homeFax"), QLatin1String("HOME,FAX")},    {QLatin1String("workFax"), QLatin1String("WORK,FAX")},
      {QLatin1String("otherFax"), QLatin1String("FAX")},        {QLatin1String("pager"), QLatin1String("PAGER")},
      {QLatin1String("workMobile"), QLatin1String("WORK,CELL")}, {QLatin1String("workPager"), QLatin1String("WORK,PAGER")},
  };
  for (const auto& kind : kinds) {
    if (phone.type == kind.first) return kind.second;
  }
  return QStringLiteral("VOICE");
}

}  // namespace

QByteArray contactVCard(const Contact& contact, const QByteArray& jpeg) {
  QByteArray card;
  const auto line = [&card](const QString& text) { appendFolded(card, text.toUtf8()); };
  const auto property = [&line](const QString& name, const QString& value) {
    if (!value.trimmed().isEmpty()) line(name + QLatin1Char(':') + value);
  };
  line(QStringLiteral("BEGIN:VCARD"));
  line(QStringLiteral("VERSION:3.0"));
  line(QStringLiteral("N:") + components({contact.familyName, contact.givenName, contact.middleName, contact.prefix, contact.suffix}));
  line(QStringLiteral("FN:") + escaped(contact.displayName()));
  property(QStringLiteral("NICKNAME"), escaped(contact.nickname.trimmed()));
  if (!contact.organization.trimmed().isEmpty() || !contact.department.trimmed().isEmpty()) {
    line(QStringLiteral("ORG:") + components({contact.organization, contact.department}));
  }
  property(QStringLiteral("TITLE"), escaped(contact.title.trimmed()));
  property(QStringLiteral("ROLE"), escaped(contact.occupation.trimmed()));
  for (const auto& email : contact.emails) {
    property(QStringLiteral("EMAIL;TYPE=") + emailTypes(email), escaped(email.value.trimmed()));
  }
  for (const auto& phone : contact.phones) {
    property(QStringLiteral("TEL;TYPE=") + phoneTypes(phone), escaped(phone.value.trimmed()));
  }
  for (const auto& address : contact.addresses) {
    const QString type = address.type == QLatin1String("home")   ? QStringLiteral(";TYPE=HOME")
                         : address.type == QLatin1String("work") ? QStringLiteral(";TYPE=WORK")
                                                                 : QString();
    const bool parts = !(address.street + address.extended + address.poBox + address.city + address.region +
                         address.postalCode + address.country)
                            .trimmed()
                            .isEmpty();
    line(QStringLiteral("ADR") + type + QLatin1Char(':') +
         (parts ? components({address.poBox, address.extended, address.street, address.city, address.region,
                              address.postalCode, address.country})
                : components({QString(), QString(), address.formatted, QString(), QString(), QString(), QString()})));
  }
  property(QStringLiteral("BDAY"), dateText(contact.birthday));
  // A URL is a URI value, not text, so its commas stay as they are.
  for (const auto& url : contact.urls) property(QStringLiteral("URL"), url.value.simplified());
  property(QStringLiteral("NOTE"), escaped(contact.notes.trimmed()));
  if (!jpeg.isEmpty()) line(QStringLiteral("PHOTO;ENCODING=b;TYPE=JPEG:") + QString::fromLatin1(jpeg.toBase64()));
  line(QStringLiteral("END:VCARD"));
  return card;
}

}  // namespace contacts
