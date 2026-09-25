#pragma once

#include "http.h"

#include <QString>

namespace omacontacts {

struct OAuthTokens {
  QString accessToken;
  QString refreshToken;
  qint64 expiryUnix = 0;
};

struct OAuthClient {
  QString clientId;
  QString clientSecret;
};

struct RedirectQuery {
  QString code;
  QString state;
  QString error;
  bool ok() const { return error.isEmpty() && !code.isEmpty(); }
};

struct TokenResult {
  OAuthTokens tokens;
  QString error;
  bool ok() const { return error.isEmpty(); }
};

QString randomUrlToken(int bytes);
QString pkceChallengeS256(const QString& verifier);
QString urlEncode(const QString& value);

QString buildAuthUrl(const OAuthClient& client, const QString& redirectUri, const QString& state,
                     const QString& codeChallenge);
RedirectQuery parseOauthRedirect(const QByteArray& httpRequest);
QString authorizationCodeBody(const OAuthClient& client, const QString& code, const QString& redirectUri,
                              const QString& verifier);
QString refreshBody(const OAuthClient& client, const QString& refreshToken);
TokenResult parseTokenResponse(const QByteArray& json, qint64 nowUnix, const QString& previousRefreshToken);
bool accessExpiring(const OAuthTokens& tokens, qint64 nowUnix);

TokenResult exchangeCode(HttpClient& http, const OAuthClient& client, const QString& code, const QString& redirectUri,
                         const QString& verifier, qint64 nowUnix);
TokenResult refreshAccess(HttpClient& http, const OAuthClient& client, const OAuthTokens& current, qint64 nowUnix);
TokenResult ensureAccess(HttpClient& http, const OAuthClient& client, OAuthTokens& tokens, qint64 nowUnix);

struct BrowserCommand {
  QStringList argv;
};

BrowserCommand browserCommand(const QString& url, bool haveOmarchy);

}  // namespace omacontacts
