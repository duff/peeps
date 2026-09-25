#pragma once

#include "contact.h"
#include "http.h"

#include <QString>
#include <functional>
#include <optional>

namespace omacontacts {

struct PersonChange {
  Contact contact;
  bool deleted = false;
};

struct ListPage {
  QList<PersonChange> people;
  QString nextPageToken;
  QString nextSyncToken;
  QString error;
  bool ok() const { return error.isEmpty(); }
};

struct PeopleResult {
  QList<PersonChange> people;
  QString syncToken;
  QString error;
  bool ok() const { return error.isEmpty(); }
};

struct WriteResult {
  Contact contact;
  QString error;
  bool ok() const { return error.isEmpty(); }
};

// Refresh replaces the access token and returns an empty string, or an error.
using RefreshFn = std::function<QString()>;

ListPage parseConnectionsPage(const QByteArray& json);
std::optional<Contact> parsePersonJson(const QByteArray& json);
QByteArray personJson(const Contact& contact);
QString googleErrorMessage(const QByteArray& body, int status);
QString connectionsUrl(const QString& pageToken, const QString& syncToken);

QList<Contact> applyFullSync(const QList<PersonChange>& people);
QList<Contact> applyIncrementalSync(QList<Contact> current, const QList<PersonChange>& changes);

HttpResponse authorizedRequest(HttpClient& http, QString& accessToken, const RefreshFn& refresh, const QString& method,
                               const QString& url, const QByteArray& body);

PeopleResult fetchConnections(HttpClient& http, QString& accessToken, const QString& syncToken, const RefreshFn& refresh);
WriteResult createContact(HttpClient& http, QString& accessToken, const Contact& draft, const RefreshFn& refresh);
WriteResult updateContact(HttpClient& http, QString& accessToken, const Contact& contact, const RefreshFn& refresh);
QString deleteContact(HttpClient& http, QString& accessToken, const QString& resourceName, const RefreshFn& refresh);

}  // namespace omacontacts
