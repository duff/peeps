#pragma once

#include "contact.h"
#include "http.h"
#include "oauth.h"

#include <QDateTime>
#include <QString>

#include <functional>
#include <optional>

namespace contacts {

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
QString photoFetchUrl(const QString& url);

QList<Contact> applyFullSync(const QList<PersonChange>& people);
QList<Contact> applyIncrementalSync(QList<Contact> current, const QList<PersonChange>& changes);

HttpResponse authorizedRequest(HttpClient& http, QString& accessToken, const RefreshFn& refresh, const QString& method,
                               const QString& url, const QByteArray& body);

PeopleResult fetchConnections(HttpClient& http, QString& accessToken, const QString& syncToken, const RefreshFn& refresh);
WriteResult createContact(HttpClient& http, QString& accessToken, const Contact& draft, const RefreshFn& refresh);
WriteResult updateContact(HttpClient& http, QString& accessToken, const Contact& contact, const RefreshFn& refresh);
WriteResult updateContactPhoto(HttpClient& http, QString& accessToken, const QString& resourceName, const QByteArray& jpeg,
                               const RefreshFn& refresh);
WriteResult deleteContactPhoto(HttpClient& http, QString& accessToken, const QString& resourceName, const RefreshFn& refresh);
QString deleteContact(HttpClient& http, QString& accessToken, const QString& resourceName, const RefreshFn& refresh);

// One set of tokens for a run of Google calls. prepare() refreshes an expiring
// access token, and call() hands each request the token plus a refresh that
// the request uses once after a 401.
class GoogleSession {
 public:
  GoogleSession(HttpClient& http, OAuthClient client, OAuthTokens tokens)
      : http_(http), client_(std::move(client)), tokens_(std::move(tokens)) {}

  QString prepare(qint64 nowUnix);

  template <typename Fn>
  auto call(Fn&& fn) {
    QString access = tokens_.accessToken;
    const RefreshFn retry = [this, &access]() -> QString {
      const QString error = refreshTokens(QDateTime::currentSecsSinceEpoch());
      if (error.isEmpty()) access = tokens_.accessToken;
      return error;
    };
    auto result = fn(http_, access, retry);
    tokens_.accessToken = access;
    return result;
  }

  const OAuthTokens& tokens() const { return tokens_; }

 private:
  QString refreshTokens(qint64 nowUnix);

  HttpClient& http_;
  OAuthClient client_;
  OAuthTokens tokens_;
};

}  // namespace contacts
