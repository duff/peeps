#include "google.h"

#include "oauth.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace contacts {
namespace {

const char* kFields =
    "addresses,biographies,birthdays,emailAddresses,events,fileAses,imClients,names,nicknames,occupations,"
    "organizations,phoneNumbers,photos,relations,sipAddresses,urls,userDefined,metadata";
const char* kUpdateFields =
    "addresses,biographies,birthdays,emailAddresses,events,fileAses,imClients,names,nicknames,occupations,"
    "organizations,phoneNumbers,relations,sipAddresses,urls,userDefined";

QString jsonString(const QJsonObject& object, const char* key) { return object.value(QLatin1String(key)).toString(); }

bool isPrimary(const QJsonObject& object) {
  return object.value(QLatin1String("metadata")).toObject().value(QLatin1String("primary")).toBool();
}

QJsonObject primaryObject(const QJsonArray& array) {
  QJsonObject first;
  bool haveFirst = false;
  for (const QJsonValue& value : array) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    if (!haveFirst) {
      first = object;
      haveFirst = true;
    }
    if (isPrimary(object)) return object;
  }
  return first;
}

QList<LabeledValue> readLabeled(const QJsonObject& person, const char* key, LabelField field) {
  QList<LabeledValue> primary;
  QList<LabeledValue> rest;
  const QJsonArray array = person.value(QLatin1String(key)).toArray();
  for (const QJsonValue& value : array) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    LabeledValue labeled;
    labeled.value = jsonString(object, "value");
    labeled.type = jsonString(object, "type");
    labeled.label = jsonString(object, "formattedType");
    if (labeled.type == QLatin1String("custom") && !labeled.label.isEmpty()) {
      const ResolvedLabel resolved = resolveLabel(field, labeled.label);
      labeled.type = resolved.type;
    } else if (labeled.type.isEmpty() && !labeled.label.isEmpty()) {
      labeled.type = resolveLabel(field, labeled.label).type;
    } else if (!labeled.type.isEmpty()) {
      labeled.type = resolveLabel(field, labeled.type).type;
    }
    if (labeled.type.isEmpty() && field == LabelField::Other) labeled.type = QStringLiteral("other");
    if (labeled.label.isEmpty()) labeled.label = labelDisplay(field, labeled);
    if (labeled.value.isEmpty()) continue;
    if (isPrimary(object)) primary.append(labeled);
    else rest.append(labeled);
  }
  primary.append(rest);
  return primary;
}

QString choosePhotoUrl(const QJsonArray& photos);

DateValue readDate(const QJsonObject& object) {
  DateValue date;
  const QJsonObject parts = object.value(QLatin1String("date")).toObject();
  date.year = parts.value(QLatin1String("year")).toInt();
  date.month = parts.value(QLatin1String("month")).toInt();
  date.day = parts.value(QLatin1String("day")).toInt();
  date.text = jsonString(object, "text");
  return date;
}

QJsonObject writeDate(const DateValue& date) {
  QJsonObject object;
  if (date.year > 0 || date.month > 0 || date.day > 0) {
    QJsonObject parts;
    if (date.year > 0) parts.insert(QStringLiteral("year"), date.year);
    if (date.month > 0) parts.insert(QStringLiteral("month"), date.month);
    if (date.day > 0) parts.insert(QStringLiteral("day"), date.day);
    object.insert(QStringLiteral("date"), parts);
  }
  if (!date.text.trimmed().isEmpty() && (date.year <= 0 && date.month <= 0 && date.day <= 0)) {
    object.insert(QStringLiteral("text"), date.text.trimmed());
  }
  return object;
}

QList<PostalAddress> readAddresses(const QJsonObject& person) {
  QList<PostalAddress> primary;
  QList<PostalAddress> rest;
  for (const QJsonValue& value : person.value(QLatin1String("addresses")).toArray()) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    PostalAddress address;
    address.street = jsonString(object, "streetAddress");
    address.extended = jsonString(object, "extendedAddress");
    address.poBox = jsonString(object, "poBox");
    address.city = jsonString(object, "city");
    address.region = jsonString(object, "region");
    address.postalCode = jsonString(object, "postalCode");
    address.country = jsonString(object, "country");
    address.formatted = jsonString(object, "formattedValue");
    address.type = jsonString(object, "type");
    address.label = jsonString(object, "formattedType");
    if (address.type == QLatin1String("custom") && !address.label.isEmpty()) address.type = address.label;
    if (!address.type.isEmpty()) {
      const ResolvedLabel resolved = resolveLabel(LabelField::Address, address.type);
      address.type = resolved.type;
      if (address.label.isEmpty()) address.label = resolved.display;
    } else if (!address.label.isEmpty()) {
      const ResolvedLabel resolved = resolveLabel(LabelField::Address, address.label);
      address.type = resolved.type;
      address.label = resolved.display;
    }
    if (address.isEmpty()) continue;
    if (isPrimary(object)) primary.append(address);
    else rest.append(address);
  }
  primary.append(rest);
  return primary;
}

QList<DatedEvent> readEvents(const QJsonObject& person) {
  QList<DatedEvent> events;
  for (const QJsonValue& value : person.value(QLatin1String("events")).toArray()) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    DatedEvent event;
    event.date = readDate(object);
    event.type = jsonString(object, "type");
    event.label = jsonString(object, "formattedType");
    if (event.type.isEmpty()) event.type = QStringLiteral("other");
    if (event.label.isEmpty()) event.label = event.type;
    if (!event.isEmpty()) events.append(event);
  }
  return events;
}

QList<ImClient> readIms(const QJsonObject& person) {
  QList<ImClient> ims;
  for (const QJsonValue& value : person.value(QLatin1String("imClients")).toArray()) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    ImClient im;
    im.username = jsonString(object, "username");
    im.protocol = jsonString(object, "protocol");
    im.type = jsonString(object, "type");
    im.label = jsonString(object, "formattedProtocol");
    if (im.label.isEmpty()) im.label = jsonString(object, "formattedType");
    if (!im.isEmpty()) ims.append(im);
  }
  return ims;
}

QList<LabeledValue> readRelations(const QJsonObject& person) {
  QList<LabeledValue> relations;
  for (const QJsonValue& value : person.value(QLatin1String("relations")).toArray()) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    LabeledValue relation;
    relation.value = jsonString(object, "person");
    relation.type = jsonString(object, "type");
    relation.label = jsonString(object, "formattedType");
    if (relation.type.isEmpty()) relation.type = QStringLiteral("other");
    if (relation.label.isEmpty()) relation.label = relation.type;
    if (!relation.value.isEmpty()) relations.append(relation);
  }
  return relations;
}

QList<LabeledValue> readCustoms(const QJsonObject& person) {
  QList<LabeledValue> customs;
  for (const QJsonValue& value : person.value(QLatin1String("userDefined")).toArray()) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    LabeledValue custom;
    custom.label = jsonString(object, "key");
    custom.value = jsonString(object, "value");
    if (custom.label.isEmpty() && custom.value.isEmpty()) continue;
    customs.append(custom);
  }
  return customs;
}

void readOrganizations(Contact& contact, const QJsonObject& person) {
  QList<Organization> primary;
  QList<Organization> rest;
  for (const QJsonValue& value : person.value(QLatin1String("organizations")).toArray()) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    Organization org;
    org.name = jsonString(object, "name");
    org.title = jsonString(object, "title");
    org.department = jsonString(object, "department");
    if (org.isEmpty()) continue;
    if (isPrimary(object)) primary.append(org);
    else rest.append(org);
  }
  primary.append(rest);
  if (primary.isEmpty()) return;
  contact.organization = primary.first().name;
  contact.title = primary.first().title;
  contact.department = primary.first().department;
  primary.removeFirst();
  contact.otherOrganizations = primary;
}

Contact personFromObject(const QJsonObject& object) {
  Contact contact;
  contact.resourceName = jsonString(object, "resourceName");
  contact.etag = jsonString(object, "etag");
  const QJsonObject name = primaryObject(object.value(QLatin1String("names")).toArray());
  contact.prefix = jsonString(name, "honorificPrefix");
  contact.givenName = jsonString(name, "givenName");
  contact.middleName = jsonString(name, "middleName");
  contact.familyName = jsonString(name, "familyName");
  contact.suffix = jsonString(name, "honorificSuffix");
  contact.phoneticGiven = jsonString(name, "phoneticGivenName");
  contact.phoneticMiddle = jsonString(name, "phoneticMiddleName");
  contact.phoneticFamily = jsonString(name, "phoneticFamilyName");
  const QJsonObject nickname = primaryObject(object.value(QLatin1String("nicknames")).toArray());
  contact.nickname = jsonString(nickname, "value");
  const QJsonObject fileAs = primaryObject(object.value(QLatin1String("fileAses")).toArray());
  contact.fileAs = jsonString(fileAs, "value");
  const QJsonObject occupation = primaryObject(object.value(QLatin1String("occupations")).toArray());
  contact.occupation = jsonString(occupation, "value");
  contact.emails = readLabeled(object, "emailAddresses", LabelField::Email);
  contact.phones = readLabeled(object, "phoneNumbers", LabelField::Phone);
  contact.urls = readLabeled(object, "urls", LabelField::Other);
  contact.sips = readLabeled(object, "sipAddresses", LabelField::Other);
  readOrganizations(contact, object);
  const QJsonObject bio = primaryObject(object.value(QLatin1String("biographies")).toArray());
  contact.notes = jsonString(bio, "value");
  const QJsonObject birthday = primaryObject(object.value(QLatin1String("birthdays")).toArray());
  contact.birthday = readDate(birthday);
  contact.addresses = readAddresses(object);
  contact.relations = readRelations(object);
  contact.events = readEvents(object);
  contact.ims = readIms(object);
  contact.customs = readCustoms(object);
  contact.photoUrl = choosePhotoUrl(object.value(QLatin1String("photos")).toArray());
  return contact;
}

QString choosePhotoUrl(const QJsonArray& photos) {
  QString fallback;
  for (const QJsonValue& value : photos) {
    if (!value.isObject()) continue;
    const QJsonObject photo = value.toObject();
    if (photo.value(QLatin1String("default")).toBool()) continue;
    const QString url = jsonString(photo, "url");
    if (url.isEmpty()) continue;
    if (isPrimary(photo)) return url;
    if (fallback.isEmpty()) fallback = url;
  }
  return fallback;
}

bool personDeleted(const QJsonObject& object) {
  return object.value(QLatin1String("metadata")).toObject().value(QLatin1String("deleted")).toBool();
}

QJsonArray labeledArray(const QList<LabeledValue>& values, LabelField field) {
  QJsonArray array;
  for (const auto& value : values) {
    QJsonObject object;
    object.insert(QStringLiteral("value"), value.value);
    const QString type = value.type.isEmpty() ? resolveLabel(field, value.label).type : resolveLabel(field, value.type).type;
    if (!type.isEmpty()) object.insert(QStringLiteral("type"), type);
    array.append(object);
  }
  return array;
}

QString bearer(const QString& token) { return QStringLiteral("Bearer ") + token; }

}  // namespace

QString googleErrorMessage(const QByteArray& body, int status) {
  const QJsonDocument document = QJsonDocument::fromJson(body);
  if (document.isObject()) {
    const QJsonObject object = document.object();
    const QString nested = object.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString();
    if (!nested.isEmpty()) return nested;
    const QString description = object.value(QLatin1String("error_description")).toString();
    if (!description.isEmpty()) return description;
    const QString message = object.value(QLatin1String("message")).toString();
    if (!message.isEmpty()) return message;
  }
  return QStringLiteral("Google Contacts returned HTTP %1.").arg(status);
}

ListPage parseConnectionsPage(const QByteArray& json) {
  ListPage page;
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    page.error = parseError.error == QJsonParseError::NoError ? QStringLiteral("Google returned an empty contact list.")
                                                              : parseError.errorString();
    return page;
  }
  const QJsonObject object = document.object();
  if (object.contains(QLatin1String("error"))) {
    page.error = googleErrorMessage(json, 400);
    return page;
  }
  page.nextPageToken = jsonString(object, "nextPageToken");
  page.nextSyncToken = jsonString(object, "nextSyncToken");
  const QJsonArray connections = object.value(QLatin1String("connections")).toArray();
  for (const QJsonValue& value : connections) {
    if (!value.isObject()) continue;
    const QJsonObject person = value.toObject();
    PersonChange change;
    change.deleted = personDeleted(person);
    change.contact = personFromObject(person);
    if (!change.contact.resourceName.isEmpty()) page.people.append(change);
  }
  return page;
}

std::optional<Contact> parsePersonJson(const QByteArray& json) {
  const QJsonDocument document = QJsonDocument::fromJson(json);
  if (!document.isObject()) return std::nullopt;
  Contact contact = personFromObject(document.object());
  if (contact.resourceName.isEmpty() && contact.givenName.isEmpty() && contact.familyName.isEmpty() &&
      contact.organization.isEmpty()) {
    return std::nullopt;
  }
  return contact;
}

void insertIfSet(QJsonObject& object, const char* key, const QString& value) {
  if (!value.isEmpty()) object.insert(QLatin1String(key), value);
}

QJsonArray organizationArray(const Contact& contact) {
  QJsonArray orgs;
  auto append = [&](const QString& name, const QString& title, const QString& department) {
    if (name.isEmpty() && title.isEmpty() && department.isEmpty()) return;
    QJsonObject org;
    org.insert(QStringLiteral("name"), name);
    org.insert(QStringLiteral("title"), title);
    org.insert(QStringLiteral("department"), department);
    orgs.append(org);
  };
  append(contact.organization, contact.title, contact.department);
  for (const auto& org : contact.otherOrganizations) append(org.name, org.title, org.department);
  return orgs;
}

QJsonArray addressArray(const QList<PostalAddress>& addresses) {
  QJsonArray array;
  for (const auto& address : addresses) {
    if (address.isEmpty()) continue;
    QJsonObject object;
    object.insert(QStringLiteral("streetAddress"), address.street);
    object.insert(QStringLiteral("extendedAddress"), address.extended);
    object.insert(QStringLiteral("poBox"), address.poBox);
    object.insert(QStringLiteral("city"), address.city);
    object.insert(QStringLiteral("region"), address.region);
    object.insert(QStringLiteral("postalCode"), address.postalCode);
    object.insert(QStringLiteral("country"), address.country);
    // An address typed as one line has no parts. Without formattedValue the
    // update would leave it empty on Google.
    const bool parts = !(address.street + address.extended + address.poBox + address.city + address.region +
                         address.postalCode + address.country)
                            .trimmed()
                            .isEmpty();
    if (!parts) object.insert(QStringLiteral("formattedValue"), address.formatted);
    if (!address.type.isEmpty()) {
      const ResolvedLabel resolved = resolveLabel(LabelField::Address, address.type);
      if (!resolved.type.isEmpty()) object.insert(QStringLiteral("type"), resolved.type);
    }
    array.append(object);
  }
  return array;
}

QJsonArray eventArray(const QList<DatedEvent>& events) {
  QJsonArray array;
  for (const auto& event : events) {
    if (event.isEmpty()) continue;
    QJsonObject object = writeDate(event.date);
    object.insert(QStringLiteral("type"), event.type.isEmpty() ? QStringLiteral("other") : event.type);
    array.append(object);
  }
  return array;
}

QJsonArray relationArray(const QList<LabeledValue>& relations) {
  QJsonArray array;
  for (const auto& relation : relations) {
    if (relation.value.isEmpty()) continue;
    QJsonObject object;
    object.insert(QStringLiteral("person"), relation.value);
    object.insert(QStringLiteral("type"), relation.type.isEmpty() ? QStringLiteral("other") : relation.type);
    array.append(object);
  }
  return array;
}

QJsonArray imArray(const QList<ImClient>& ims) {
  QJsonArray array;
  for (const auto& im : ims) {
    if (im.isEmpty()) continue;
    QJsonObject object;
    object.insert(QStringLiteral("username"), im.username);
    object.insert(QStringLiteral("protocol"), im.protocol.isEmpty() ? QStringLiteral("other") : im.protocol);
    if (!im.type.isEmpty()) object.insert(QStringLiteral("type"), im.type);
    array.append(object);
  }
  return array;
}

QJsonArray customArray(const QList<LabeledValue>& customs) {
  QJsonArray array;
  for (const auto& custom : customs) {
    if (custom.label.isEmpty() && custom.value.isEmpty()) continue;
    QJsonObject object;
    object.insert(QStringLiteral("key"), custom.label);
    object.insert(QStringLiteral("value"), custom.value);
    array.append(object);
  }
  return array;
}

QByteArray personJson(const Contact& contact) {
  QJsonObject root;
  if (!contact.etag.isEmpty()) root.insert(QStringLiteral("etag"), contact.etag);
  QString given = contact.givenName;
  QString family = contact.familyName;
  if (given.isEmpty() && family.isEmpty() && contact.prefix.isEmpty() && contact.middleName.isEmpty() && contact.suffix.isEmpty()) {
    given = contact.displayName();
  }
  QJsonObject name;
  name.insert(QStringLiteral("givenName"), given);
  name.insert(QStringLiteral("familyName"), family);
  insertIfSet(name, "honorificPrefix", contact.prefix);
  insertIfSet(name, "middleName", contact.middleName);
  insertIfSet(name, "honorificSuffix", contact.suffix);
  insertIfSet(name, "phoneticGivenName", contact.phoneticGiven);
  insertIfSet(name, "phoneticMiddleName", contact.phoneticMiddle);
  insertIfSet(name, "phoneticFamilyName", contact.phoneticFamily);
  name.insert(QStringLiteral("displayName"), contact.displayName());
  root.insert(QStringLiteral("names"), QJsonArray{name});
  QJsonArray nicknames;
  if (!contact.nickname.isEmpty()) nicknames.append(QJsonObject{{QStringLiteral("value"), contact.nickname}});
  root.insert(QStringLiteral("nicknames"), nicknames);
  QJsonArray fileAses;
  if (!contact.fileAs.isEmpty()) fileAses.append(QJsonObject{{QStringLiteral("value"), contact.fileAs}});
  root.insert(QStringLiteral("fileAses"), fileAses);
  QJsonArray occupations;
  if (!contact.occupation.isEmpty()) occupations.append(QJsonObject{{QStringLiteral("value"), contact.occupation}});
  root.insert(QStringLiteral("occupations"), occupations);
  root.insert(QStringLiteral("emailAddresses"), labeledArray(contact.emails, LabelField::Email));
  root.insert(QStringLiteral("phoneNumbers"), labeledArray(contact.phones, LabelField::Phone));
  root.insert(QStringLiteral("urls"), labeledArray(contact.urls, LabelField::Other));
  root.insert(QStringLiteral("sipAddresses"), labeledArray(contact.sips, LabelField::Other));
  root.insert(QStringLiteral("organizations"), organizationArray(contact));
  root.insert(QStringLiteral("addresses"), addressArray(contact.addresses));
  root.insert(QStringLiteral("relations"), relationArray(contact.relations));
  root.insert(QStringLiteral("events"), eventArray(contact.events));
  root.insert(QStringLiteral("imClients"), imArray(contact.ims));
  root.insert(QStringLiteral("userDefined"), customArray(contact.customs));
  QJsonArray birthdays;
  if (!contact.birthday.isEmpty()) birthdays.append(writeDate(contact.birthday));
  root.insert(QStringLiteral("birthdays"), birthdays);
  QJsonArray bios;
  if (!contact.notes.isEmpty()) {
    QJsonObject bio;
    bio.insert(QStringLiteral("value"), contact.notes);
    bio.insert(QStringLiteral("contentType"), QStringLiteral("TEXT_PLAIN"));
    bios.append(bio);
  }
  root.insert(QStringLiteral("biographies"), bios);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QString photoFetchUrl(const QString& url) {
  if (url.isEmpty()) return {};
  static const QRegularExpression sizeParam(QStringLiteral("([?&])sz=\\d+"));
  // Google's image servers use "=s100" (or "=s100-c") at the end of the URL.
  // "=s0" asks for the original file instead of that thumbnail.
  static const QRegularExpression googleSize(QStringLiteral("=s\\d+(-[A-Za-z0-9]+)?$"));
  static const QRegularExpression googleBox(QStringLiteral("=[wh]\\d+-[wh]\\d+(-[A-Za-z0-9]+)?$"));
  QString result = url;
  result.remove(sizeParam);
  if (result.endsWith(QLatin1Char('?')) || result.endsWith(QLatin1Char('&'))) result.chop(1);
  if (googleSize.match(result).hasMatch()) return result.replace(googleSize, QStringLiteral("=s0"));
  if (googleBox.match(result).hasMatch()) return result.replace(googleBox, QStringLiteral("=s0"));
  return result + (result.contains(QLatin1Char('?')) ? QStringLiteral("&sz=2048") : QStringLiteral("?sz=2048"));
}

QString connectionsUrl(const QString& pageToken, const QString& syncToken) {
  QString url = QStringLiteral("https://people.googleapis.com/v1/people/me/connections?personFields=%1"
                               "&pageSize=1000&requestSyncToken=true&sources=READ_SOURCE_TYPE_CONTACT")
                    .arg(QLatin1String(kFields));
  if (!pageToken.isEmpty()) url += QStringLiteral("&pageToken=") + urlEncode(pageToken);
  if (!syncToken.isEmpty()) url += QStringLiteral("&syncToken=") + urlEncode(syncToken);
  return url;
}

QList<Contact> applyFullSync(const QList<PersonChange>& people) {
  QList<Contact> contacts;
  contacts.reserve(people.size());
  for (const auto& change : people) {
    if (!change.deleted) contacts.append(change.contact);
  }
  return contacts;
}

QList<Contact> applyIncrementalSync(QList<Contact> current, const QList<PersonChange>& changes) {
  // The last change for a person wins, whatever order the pages arrive in.
  QHash<QString, const PersonChange*> latest;
  latest.reserve(changes.size());
  for (const auto& change : changes) latest.insert(change.contact.resourceName, &change);
  current.removeIf([&](const Contact& contact) { return latest.contains(contact.resourceName); });
  for (const auto& change : changes) {
    if (!change.deleted && latest.value(change.contact.resourceName) == &change) current.append(change.contact);
  }
  return current;
}

HttpResponse authorizedRequest(HttpClient& http, QString& accessToken, const RefreshFn& refresh, const QString& method,
                               const QString& url, const QByteArray& body) {
  auto once = [&](const QString& token) {
    QList<QPair<QString, QString>> headers = {{QStringLiteral("Authorization"), bearer(token)},
                                              {QStringLiteral("Accept"), QStringLiteral("application/json")}};
    if (!body.isEmpty()) headers.append({QStringLiteral("Content-Type"), QStringLiteral("application/json")});
    return http.request(method, url, headers, body);
  };
  HttpResponse response = once(accessToken);
  if (response.status != 401) return response;
  const QString refreshError = refresh ? refresh() : QStringLiteral("Sign in to Google Contacts again.");
  if (!refreshError.isEmpty()) {
    response.error = refreshError;
    return response;
  }
  return once(accessToken);
}

PeopleResult fetchConnections(HttpClient& http, QString& accessToken, const QString& syncToken, const RefreshFn& refresh) {
  PeopleResult result;
  QString pageToken;
  bool first = true;
  do {
    const QString url = connectionsUrl(pageToken, first ? syncToken : QString());
    first = false;
    const HttpResponse response = authorizedRequest(http, accessToken, refresh, QStringLiteral("GET"), url, {});
    if (response.status == 0) {
      result.error = response.error.isEmpty() ? QStringLiteral("Could not reach Google Contacts.") : response.error;
      return result;
    }
    if (response.status == 410) {
      result.error = QStringLiteral("sync-token-expired");
      return result;
    }
    if (response.status >= 400) {
      result.error = googleErrorMessage(response.body, response.status);
      return result;
    }
    const ListPage page = parseConnectionsPage(response.body);
    if (!page.ok()) {
      result.error = page.error;
      return result;
    }
    result.people.append(page.people);
    if (!page.nextSyncToken.isEmpty()) result.syncToken = page.nextSyncToken;
    pageToken = page.nextPageToken;
  } while (!pageToken.isEmpty());
  return result;
}

WriteResult createContact(HttpClient& http, QString& accessToken, const Contact& draft, const RefreshFn& refresh) {
  WriteResult result;
  const HttpResponse response =
      authorizedRequest(http, accessToken, refresh, QStringLiteral("POST"),
                        QStringLiteral("https://people.googleapis.com/v1/people:createContact"), personJson(draft));
  if (response.status == 0) {
    result.error = response.error.isEmpty() ? QStringLiteral("Could not reach Google Contacts.") : response.error;
    return result;
  }
  if (response.status >= 400) {
    result.error = googleErrorMessage(response.body, response.status);
    return result;
  }
  auto contact = parsePersonJson(response.body);
  if (!contact || contact->resourceName.isEmpty()) {
    result.error = QStringLiteral("Google did not return the new contact.");
    return result;
  }
  result.contact = *contact;
  return result;
}

WriteResult updateContact(HttpClient& http, QString& accessToken, const Contact& contact, const RefreshFn& refresh) {
  if (contact.resourceName.isEmpty()) {
    WriteResult result;
    result.error = QStringLiteral("This contact has no Google id.");
    return result;
  }
  auto send = [&](const Contact& attempt) {
    WriteResult result;
    const QString url = QStringLiteral("https://people.googleapis.com/v1/%1:updateContact?updatePersonFields=%2")
                            .arg(attempt.resourceName, QLatin1String(kUpdateFields));
    const HttpResponse response = authorizedRequest(http, accessToken, refresh, QStringLiteral("PATCH"), url, personJson(attempt));
    if (response.status == 0) {
      result.error = response.error.isEmpty() ? QStringLiteral("Could not reach Google Contacts.") : response.error;
      return result;
    }
    if (response.status >= 400) {
      result.error = googleErrorMessage(response.body, response.status);
      return result;
    }
    auto parsed = parsePersonJson(response.body);
    if (!parsed) {
      result.error = QStringLiteral("Google did not return the updated contact.");
      return result;
    }
    result.contact = *parsed;
    return result;
  };
  WriteResult result = send(contact);
  if (result.ok() || !result.error.contains(QStringLiteral("etag"), Qt::CaseInsensitive)) return result;
  const QString lookup = QStringLiteral("https://people.googleapis.com/v1/%1?personFields=metadata").arg(contact.resourceName);
  const HttpResponse latest = authorizedRequest(http, accessToken, refresh, QStringLiteral("GET"), lookup, {});
  if (latest.status < 200 || latest.status >= 300) return result;
  const QJsonDocument document = QJsonDocument::fromJson(latest.body);
  const QString etag = document.isObject() ? jsonString(document.object(), "etag") : QString();
  if (etag.isEmpty() || etag == contact.etag) return result;
  Contact retry = contact;
  retry.etag = etag;
  return send(retry);
}

WriteResult photoResult(const HttpResponse& response) {
  WriteResult result;
  if (response.status == 0) {
    result.error = response.error.isEmpty() ? QStringLiteral("Could not reach Google Contacts.") : response.error;
    return result;
  }
  if (response.status >= 400) {
    result.error = googleErrorMessage(response.body, response.status);
    return result;
  }
  const QJsonDocument document = QJsonDocument::fromJson(response.body);
  if (!document.isObject()) {
    result.error = QStringLiteral("Google did not return the contact photo.");
    return result;
  }
  const QJsonObject person = document.object().value(QLatin1String("person")).toObject();
  if (person.isEmpty()) {
    result.error = QStringLiteral("Google did not return the contact photo.");
    return result;
  }
  result.contact = personFromObject(person);
  if (result.contact.resourceName.isEmpty()) result.error = QStringLiteral("Google did not return the contact photo.");
  return result;
}

WriteResult updateContactPhoto(HttpClient& http, QString& accessToken, const QString& resourceName, const QByteArray& jpeg,
                               const RefreshFn& refresh) {
  WriteResult result;
  if (resourceName.isEmpty()) {
    result.error = QStringLiteral("This contact has no Google id.");
    return result;
  }
  if (jpeg.isEmpty()) {
    result.error = QStringLiteral("Choose a photo first.");
    return result;
  }
  QJsonObject body;
  body.insert(QStringLiteral("photoBytes"), QString::fromLatin1(jpeg.toBase64()));
  body.insert(QStringLiteral("personFields"), QStringLiteral("photos,metadata"));
  const QString url = QStringLiteral("https://people.googleapis.com/v1/%1:updateContactPhoto").arg(resourceName);
  return photoResult(authorizedRequest(http, accessToken, refresh, QStringLiteral("PATCH"), url,
                                       QJsonDocument(body).toJson(QJsonDocument::Compact)));
}

WriteResult deleteContactPhoto(HttpClient& http, QString& accessToken, const QString& resourceName, const RefreshFn& refresh) {
  if (resourceName.isEmpty()) {
    WriteResult result;
    result.error = QStringLiteral("This contact has no Google id.");
    return result;
  }
  const QString url = QStringLiteral("https://people.googleapis.com/v1/%1:deleteContactPhoto?personFields=photos,metadata")
                          .arg(resourceName);
  WriteResult result = photoResult(authorizedRequest(http, accessToken, refresh, QStringLiteral("DELETE"), url, {}));
  if (result.ok()) result.contact.photoUrl.clear();
  return result;
}

QString GoogleSession::prepare(qint64 nowUnix) {
  const TokenResult ensured = ensureAccess(http_, client_, tokens_, nowUnix);
  return ensured.ok() ? QString() : ensured.error;
}

QString GoogleSession::refreshTokens(qint64 nowUnix) {
  const TokenResult refreshed = refreshAccess(http_, client_, tokens_, nowUnix);
  if (!refreshed.ok()) return refreshed.error;
  tokens_ = refreshed.tokens;
  return {};
}

QString deleteContact(HttpClient& http, QString& accessToken, const QString& resourceName, const RefreshFn& refresh) {
  const QString url = QStringLiteral("https://people.googleapis.com/v1/%1:deleteContact").arg(resourceName);
  const HttpResponse response = authorizedRequest(http, accessToken, refresh, QStringLiteral("DELETE"), url, {});
  if (response.status == 0) return response.error.isEmpty() ? QStringLiteral("Could not reach Google Contacts.") : response.error;
  if (response.status >= 400) return googleErrorMessage(response.body, response.status);
  return {};
}

}  // namespace contacts
