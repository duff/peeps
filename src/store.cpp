#include "store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace contacts {
namespace {

QJsonArray labeledJson(const QList<LabeledValue>& values) {
  QJsonArray array;
  for (const auto& value : values) {
    QJsonObject object;
    object.insert(QStringLiteral("value"), value.value);
    object.insert(QStringLiteral("label"), value.label);
    if (!value.type.isEmpty()) object.insert(QStringLiteral("type"), value.type);
    array.append(object);
  }
  return array;
}

QList<LabeledValue> labeledFrom(const QJsonArray& array, LabelField field) {
  QList<LabeledValue> values;
  for (const QJsonValue& value : array) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    LabeledValue labeled;
    labeled.value = object.value(QLatin1String("value")).toString();
    labeled.label = object.value(QLatin1String("label")).toString();
    labeled.type = object.value(QLatin1String("type")).toString();
    if (labeled.type.isEmpty() && !labeled.label.isEmpty()) labeled.type = resolveLabel(field, labeled.label).type;
    if (labeled.label.isEmpty()) labeled.label = labelDisplay(field, labeled);
    if (!labeled.value.isEmpty()) values.append(labeled);
  }
  return values;
}

QJsonObject dateJson(const DateValue& date) {
  QJsonObject object;
  if (date.year > 0) object.insert(QStringLiteral("year"), date.year);
  if (date.month > 0) object.insert(QStringLiteral("month"), date.month);
  if (date.day > 0) object.insert(QStringLiteral("day"), date.day);
  if (!date.text.isEmpty()) object.insert(QStringLiteral("text"), date.text);
  return object;
}

DateValue dateFrom(const QJsonObject& object) {
  DateValue date;
  date.year = object.value(QLatin1String("year")).toInt();
  date.month = object.value(QLatin1String("month")).toInt();
  date.day = object.value(QLatin1String("day")).toInt();
  date.text = object.value(QLatin1String("text")).toString();
  return date;
}

QJsonArray addressJson(const QList<PostalAddress>& addresses) {
  QJsonArray array;
  for (const auto& address : addresses) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), address.type);
    object.insert(QStringLiteral("label"), address.label);
    object.insert(QStringLiteral("street"), address.street);
    object.insert(QStringLiteral("extended"), address.extended);
    object.insert(QStringLiteral("po_box"), address.poBox);
    object.insert(QStringLiteral("city"), address.city);
    object.insert(QStringLiteral("region"), address.region);
    object.insert(QStringLiteral("postal_code"), address.postalCode);
    object.insert(QStringLiteral("country"), address.country);
    object.insert(QStringLiteral("formatted"), address.formatted);
    array.append(object);
  }
  return array;
}

QList<PostalAddress> addressFrom(const QJsonArray& array) {
  QList<PostalAddress> addresses;
  for (const QJsonValue& value : array) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    PostalAddress address;
    address.type = object.value(QLatin1String("type")).toString();
    address.label = object.value(QLatin1String("label")).toString();
    address.street = object.value(QLatin1String("street")).toString();
    address.extended = object.value(QLatin1String("extended")).toString();
    address.poBox = object.value(QLatin1String("po_box")).toString();
    address.city = object.value(QLatin1String("city")).toString();
    address.region = object.value(QLatin1String("region")).toString();
    address.postalCode = object.value(QLatin1String("postal_code")).toString();
    address.country = object.value(QLatin1String("country")).toString();
    address.formatted = object.value(QLatin1String("formatted")).toString();
    if (!address.isEmpty()) addresses.append(address);
  }
  return addresses;
}

QJsonArray orgJson(const QList<Organization>& orgs) {
  QJsonArray array;
  for (const auto& org : orgs) {
    QJsonObject object;
    object.insert(QStringLiteral("name"), org.name);
    object.insert(QStringLiteral("title"), org.title);
    object.insert(QStringLiteral("department"), org.department);
    array.append(object);
  }
  return array;
}

QList<Organization> orgFrom(const QJsonArray& array) {
  QList<Organization> orgs;
  for (const QJsonValue& value : array) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    Organization org;
    org.name = object.value(QLatin1String("name")).toString();
    org.title = object.value(QLatin1String("title")).toString();
    org.department = object.value(QLatin1String("department")).toString();
    if (!org.isEmpty()) orgs.append(org);
  }
  return orgs;
}

QJsonArray eventJson(const QList<DatedEvent>& events) {
  QJsonArray array;
  for (const auto& event : events) {
    QJsonObject object;
    object.insert(QStringLiteral("type"), event.type);
    object.insert(QStringLiteral("label"), event.label);
    object.insert(QStringLiteral("date"), dateJson(event.date));
    array.append(object);
  }
  return array;
}

QList<DatedEvent> eventFrom(const QJsonArray& array) {
  QList<DatedEvent> events;
  for (const QJsonValue& value : array) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    DatedEvent event;
    event.type = object.value(QLatin1String("type")).toString();
    event.label = object.value(QLatin1String("label")).toString();
    event.date = dateFrom(object.value(QLatin1String("date")).toObject());
    if (!event.isEmpty()) events.append(event);
  }
  return events;
}

QJsonArray imJson(const QList<ImClient>& ims) {
  QJsonArray array;
  for (const auto& im : ims) {
    QJsonObject object;
    object.insert(QStringLiteral("username"), im.username);
    object.insert(QStringLiteral("protocol"), im.protocol);
    object.insert(QStringLiteral("type"), im.type);
    object.insert(QStringLiteral("label"), im.label);
    array.append(object);
  }
  return array;
}

QList<ImClient> imFrom(const QJsonArray& array) {
  QList<ImClient> ims;
  for (const QJsonValue& value : array) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    ImClient im;
    im.username = object.value(QLatin1String("username")).toString();
    im.protocol = object.value(QLatin1String("protocol")).toString();
    im.type = object.value(QLatin1String("type")).toString();
    im.label = object.value(QLatin1String("label")).toString();
    if (!im.isEmpty()) ims.append(im);
  }
  return ims;
}

Contact contactFromObject(const QJsonObject& object) {
  Contact contact;
  contact.resourceName = object.value(QLatin1String("resource_name")).toString();
  contact.etag = object.value(QLatin1String("etag")).toString();
  contact.prefix = object.value(QLatin1String("prefix")).toString();
  contact.givenName = object.value(QLatin1String("given_name")).toString();
  contact.middleName = object.value(QLatin1String("middle_name")).toString();
  contact.familyName = object.value(QLatin1String("family_name")).toString();
  contact.suffix = object.value(QLatin1String("suffix")).toString();
  contact.phoneticGiven = object.value(QLatin1String("phonetic_given")).toString();
  contact.phoneticMiddle = object.value(QLatin1String("phonetic_middle")).toString();
  contact.phoneticFamily = object.value(QLatin1String("phonetic_family")).toString();
  contact.nickname = object.value(QLatin1String("nickname")).toString();
  contact.fileAs = object.value(QLatin1String("file_as")).toString();
  contact.organization = object.value(QLatin1String("organization")).toString();
  contact.title = object.value(QLatin1String("title")).toString();
  contact.department = object.value(QLatin1String("department")).toString();
  contact.otherOrganizations = orgFrom(object.value(QLatin1String("other_organizations")).toArray());
  contact.occupation = object.value(QLatin1String("occupation")).toString();
  contact.notes = object.value(QLatin1String("notes")).toString();
  contact.photoUrl = object.value(QLatin1String("photo_url")).toString();
  contact.birthday = dateFrom(object.value(QLatin1String("birthday")).toObject());
  contact.emails = labeledFrom(object.value(QLatin1String("emails")).toArray(), LabelField::Email);
  contact.phones = labeledFrom(object.value(QLatin1String("phones")).toArray(), LabelField::Phone);
  contact.addresses = addressFrom(object.value(QLatin1String("addresses")).toArray());
  contact.urls = labeledFrom(object.value(QLatin1String("urls")).toArray(), LabelField::Other);
  contact.relations = labeledFrom(object.value(QLatin1String("relations")).toArray(), LabelField::Other);
  contact.sips = labeledFrom(object.value(QLatin1String("sips")).toArray(), LabelField::Other);
  contact.customs = labeledFrom(object.value(QLatin1String("customs")).toArray(), LabelField::Other);
  contact.events = eventFrom(object.value(QLatin1String("events")).toArray());
  contact.ims = imFrom(object.value(QLatin1String("ims")).toArray());
  return contact;
}

}  // namespace

AppPaths AppPaths::forEnv(const QString& home, const QString& xdgDataHome, const QString& xdgCacheHome) {
  AppPaths paths;
  const QString dataRoot = xdgDataHome.isEmpty() ? home + QStringLiteral("/.local/share") : xdgDataHome;
  const QString cacheRoot = xdgCacheHome.isEmpty() ? home + QStringLiteral("/.cache") : xdgCacheHome;
  paths.cacheFile = dataRoot + QStringLiteral("/peeps/contacts.json");
  paths.photoDir = cacheRoot + QStringLiteral("/peeps/photos");
  return paths;
}

OAuthClient builtInClient() {
#ifdef PEEPS_GOOGLE_CLIENT_ID
#ifndef PEEPS_GOOGLE_CLIENT_SECRET
#define PEEPS_GOOGLE_CLIENT_SECRET ""
#endif
  return {QStringLiteral(PEEPS_GOOGLE_CLIENT_ID), QStringLiteral(PEEPS_GOOGLE_CLIENT_SECRET)};
#else
  return {};
#endif
}

QByteArray writeCacheJson(const QList<Contact>& contacts, const QString& syncToken) {
  QJsonArray array;
  for (const auto& contact : contacts) {
    QJsonObject object;
    object.insert(QStringLiteral("resource_name"), contact.resourceName);
    object.insert(QStringLiteral("etag"), contact.etag);
    object.insert(QStringLiteral("prefix"), contact.prefix);
    object.insert(QStringLiteral("given_name"), contact.givenName);
    object.insert(QStringLiteral("middle_name"), contact.middleName);
    object.insert(QStringLiteral("family_name"), contact.familyName);
    object.insert(QStringLiteral("suffix"), contact.suffix);
    object.insert(QStringLiteral("phonetic_given"), contact.phoneticGiven);
    object.insert(QStringLiteral("phonetic_middle"), contact.phoneticMiddle);
    object.insert(QStringLiteral("phonetic_family"), contact.phoneticFamily);
    object.insert(QStringLiteral("nickname"), contact.nickname);
    object.insert(QStringLiteral("file_as"), contact.fileAs);
    object.insert(QStringLiteral("organization"), contact.organization);
    object.insert(QStringLiteral("title"), contact.title);
    object.insert(QStringLiteral("department"), contact.department);
    object.insert(QStringLiteral("other_organizations"), orgJson(contact.otherOrganizations));
    object.insert(QStringLiteral("occupation"), contact.occupation);
    object.insert(QStringLiteral("notes"), contact.notes);
    object.insert(QStringLiteral("photo_url"), contact.photoUrl);
    object.insert(QStringLiteral("birthday"), dateJson(contact.birthday));
    object.insert(QStringLiteral("emails"), labeledJson(contact.emails));
    object.insert(QStringLiteral("phones"), labeledJson(contact.phones));
    object.insert(QStringLiteral("addresses"), addressJson(contact.addresses));
    object.insert(QStringLiteral("urls"), labeledJson(contact.urls));
    object.insert(QStringLiteral("relations"), labeledJson(contact.relations));
    object.insert(QStringLiteral("events"), eventJson(contact.events));
    object.insert(QStringLiteral("ims"), imJson(contact.ims));
    object.insert(QStringLiteral("sips"), labeledJson(contact.sips));
    object.insert(QStringLiteral("customs"), labeledJson(contact.customs));
    array.append(object);
  }
  QJsonObject root;
  root.insert(QStringLiteral("version"), 3);
  root.insert(QStringLiteral("sync_token"), syncToken);
  root.insert(QStringLiteral("contacts"), array);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

CacheSnapshot readCacheJson(const QByteArray& json) {
  CacheSnapshot snapshot;
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    snapshot.error = parseError.error == QJsonParseError::NoError ? QStringLiteral("Could not read the contact cache.")
                                                                  : parseError.errorString();
    return snapshot;
  }
  const int version = document.object().value(QLatin1String("version")).toInt();
  // Version 2 did not store every Google field. Drop its sync token so the next
  // sync is a full read and those fields are not wiped by a later edit.
  if (version >= 3) snapshot.syncToken = document.object().value(QLatin1String("sync_token")).toString();
  const QJsonArray array = document.object().value(QLatin1String("contacts")).toArray();
  snapshot.contacts.reserve(array.size());
  for (const QJsonValue& value : array) {
    if (value.isObject()) snapshot.contacts.append(contactFromObject(value.toObject()));
  }
  return snapshot;
}

QByteArray writeTokensJson(const OAuthTokens& tokens) {
  QJsonObject object;
  object.insert(QStringLiteral("access_token"), tokens.accessToken);
  object.insert(QStringLiteral("refresh_token"), tokens.refreshToken);
  object.insert(QStringLiteral("expiry_unix"), static_cast<double>(tokens.expiryUnix));
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<OAuthTokens> readTokensJson(const QByteArray& json) {
  const QJsonDocument document = QJsonDocument::fromJson(json);
  if (!document.isObject()) return std::nullopt;
  OAuthTokens tokens;
  tokens.accessToken = document.object().value(QLatin1String("access_token")).toString();
  tokens.refreshToken = document.object().value(QLatin1String("refresh_token")).toString();
  tokens.expiryUnix = static_cast<qint64>(document.object().value(QLatin1String("expiry_unix")).toDouble());
  if (tokens.accessToken.isEmpty() && tokens.refreshToken.isEmpty()) return std::nullopt;
  return tokens;
}

bool writePrivateFile(const QString& path, const QByteArray& contents, QString& error) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    error = QStringLiteral("Could not write %1.").arg(path);
    return false;
  }
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  if (file.write(contents) != contents.size() || !file.commit()) {
    error = QStringLiteral("Could not write %1.").arg(path);
    return false;
  }
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return true;
}

std::optional<QByteArray> readFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
  return file.readAll();
}

}  // namespace contacts
