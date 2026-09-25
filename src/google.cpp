#include "google.h"

#include "oauth.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace omacontacts {
namespace {

const char* kFields = "names,emailAddresses,phoneNumbers,organizations,biographies,metadata";

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

QList<LabeledValue> readLabeled(const QJsonObject& person, const char* key) {
  QList<LabeledValue> primary;
  QList<LabeledValue> rest;
  const QJsonArray array = person.value(QLatin1String(key)).toArray();
  for (const QJsonValue& value : array) {
    if (!value.isObject()) continue;
    const QJsonObject object = value.toObject();
    LabeledValue labeled;
    labeled.value = jsonString(object, "value");
    labeled.label = jsonString(object, "formattedType");
    if (labeled.label.isEmpty()) labeled.label = jsonString(object, "type");
    if (labeled.value.isEmpty()) continue;
    if (isPrimary(object)) primary.append(labeled);
    else rest.append(labeled);
  }
  primary.append(rest);
  return primary;
}

Contact personFromObject(const QJsonObject& object) {
  Contact contact;
  contact.resourceName = jsonString(object, "resourceName");
  contact.etag = jsonString(object, "etag");
  const QJsonObject name = primaryObject(object.value(QLatin1String("names")).toArray());
  contact.givenName = jsonString(name, "givenName");
  contact.familyName = jsonString(name, "familyName");
  contact.emails = readLabeled(object, "emailAddresses");
  contact.phones = readLabeled(object, "phoneNumbers");
  const QJsonObject org = primaryObject(object.value(QLatin1String("organizations")).toArray());
  contact.organization = jsonString(org, "name");
  contact.title = jsonString(org, "title");
  const QJsonObject bio = primaryObject(object.value(QLatin1String("biographies")).toArray());
  contact.notes = jsonString(bio, "value");
  return contact;
}

bool personDeleted(const QJsonObject& object) {
  return object.value(QLatin1String("metadata")).toObject().value(QLatin1String("deleted")).toBool();
}

QJsonArray labeledArray(const QList<LabeledValue>& values) {
  QJsonArray array;
  for (const auto& value : values) {
    QJsonObject object;
    object.insert(QStringLiteral("value"), value.value);
    const QString label = value.label.isEmpty() ? QStringLiteral("other") : value.label;
    const bool standard = label == QLatin1String("home") || label == QLatin1String("work") ||
                          label == QLatin1String("other") || label == QLatin1String("mobile") ||
                          label == QLatin1String("main");
    object.insert(QStringLiteral("type"), standard ? label : QStringLiteral("custom"));
    if (!standard) object.insert(QStringLiteral("formattedType"), label);
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

QByteArray personJson(const Contact& contact) {
  QJsonObject root;
  if (!contact.etag.isEmpty()) root.insert(QStringLiteral("etag"), contact.etag);
  QString given = contact.givenName;
  QString family = contact.familyName;
  if (given.isEmpty() && family.isEmpty()) given = contact.displayName();
  QJsonObject name;
  name.insert(QStringLiteral("givenName"), given);
  name.insert(QStringLiteral("familyName"), family);
  name.insert(QStringLiteral("displayName"), contact.displayName());
  root.insert(QStringLiteral("names"), QJsonArray{name});
  root.insert(QStringLiteral("emailAddresses"), labeledArray(contact.emails));
  root.insert(QStringLiteral("phoneNumbers"), labeledArray(contact.phones));
  QJsonArray orgs;
  if (!contact.organization.isEmpty() || !contact.title.isEmpty()) {
    QJsonObject org;
    org.insert(QStringLiteral("name"), contact.organization);
    org.insert(QStringLiteral("title"), contact.title);
    orgs.append(org);
  }
  root.insert(QStringLiteral("organizations"), orgs);
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
  for (const auto& change : changes) {
    current.erase(std::remove_if(current.begin(), current.end(),
                                 [&](const Contact& contact) { return contact.resourceName == change.contact.resourceName; }),
                  current.end());
    if (!change.deleted) current.append(change.contact);
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
  WriteResult result;
  if (contact.resourceName.isEmpty()) {
    result.error = QStringLiteral("This contact has no Google id.");
    return result;
  }
  const QString url = QStringLiteral("https://people.googleapis.com/v1/%1:updateContact?updatePersonFields=names,"
                                     "emailAddresses,phoneNumbers,organizations,biographies")
                          .arg(contact.resourceName);
  const HttpResponse response = authorizedRequest(http, accessToken, refresh, QStringLiteral("PATCH"), url, personJson(contact));
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
}

QString deleteContact(HttpClient& http, QString& accessToken, const QString& resourceName, const RefreshFn& refresh) {
  const QString url = QStringLiteral("https://people.googleapis.com/v1/%1:deleteContact").arg(resourceName);
  const HttpResponse response = authorizedRequest(http, accessToken, refresh, QStringLiteral("DELETE"), url, {});
  if (response.status == 0) return response.error.isEmpty() ? QStringLiteral("Could not reach Google Contacts.") : response.error;
  if (response.status >= 400) return googleErrorMessage(response.body, response.status);
  return {};
}

}  // namespace omacontacts
