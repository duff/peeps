#include "oauth.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QUrl>

namespace contacts {
namespace {

QString base64Url(const QByteArray& data) {
  return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

TokenResult postToken(HttpClient& http, const QString& body, qint64 nowUnix, const QString& previousRefresh) {
  const HttpResponse response = http.request(
      QStringLiteral("POST"), QStringLiteral("https://oauth2.googleapis.com/token"),
      {{QStringLiteral("Content-Type"), QStringLiteral("application/x-www-form-urlencoded")},
       {QStringLiteral("Accept"), QStringLiteral("application/json")}},
      body.toUtf8());
  if (response.status == 0) {
    TokenResult result;
    result.error = response.error.isEmpty() ? QStringLiteral("Could not reach Google.") : response.error;
    return result;
  }
  auto parsed = parseTokenResponse(response.body, nowUnix, previousRefresh);
  if (response.status >= 400 && parsed.ok()) {
    parsed.error = QStringLiteral("Google refused the token request (%1).").arg(response.status);
    parsed.tokens = {};
  }
  if (!parsed.ok() && parsed.error.isEmpty()) parsed.error = QStringLiteral("Google refused the token request.");
  return parsed;
}

}  // namespace

QString randomUrlToken(int bytes) {
  QByteArray buffer(bytes, 0);
  auto* generator = QRandomGenerator::system();
  for (int i = 0; i < bytes; ++i) buffer[i] = static_cast<char>(generator->generate() & 0xff);
  return base64Url(buffer);
}

QString pkceChallengeS256(const QString& verifier) {
  const QByteArray digest = QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);
  return base64Url(digest);
}

QString urlEncode(const QString& value) { return QString::fromUtf8(QUrl::toPercentEncoding(value)); }

QString buildAuthUrl(const OAuthClient& client, const QString& redirectUri, const QString& state,
                     const QString& codeChallenge) {
  return QStringLiteral(
             "https://accounts.google.com/o/oauth2/v2/auth?client_id=%1&redirect_uri=%2&response_type=code"
             "&scope=%3&code_challenge=%4&code_challenge_method=S256&access_type=offline&prompt=consent&state=%5")
      .arg(urlEncode(client.clientId), urlEncode(redirectUri),
           urlEncode(QStringLiteral("https://www.googleapis.com/auth/contacts")), urlEncode(codeChallenge),
           urlEncode(state));
}

RedirectQuery parseOauthRedirect(const QByteArray& httpRequest) {
  RedirectQuery query;
  const int lineEnd = httpRequest.indexOf("\r\n");
  const QByteArray first = lineEnd < 0 ? httpRequest : httpRequest.left(lineEnd);
  const int qpos = first.indexOf('?');
  if (qpos < 0) {
    query.error = QStringLiteral("The browser returned no authorization code.");
    return query;
  }
  const int space = first.indexOf(' ', qpos);
  const QByteArray raw = space < 0 ? first.mid(qpos + 1) : first.mid(qpos + 1, space - qpos - 1);
  for (const QByteArray& part : raw.split('&')) {
    const int eq = part.indexOf('=');
    const QString key = QString::fromUtf8(eq < 0 ? part : part.left(eq));
    const QString value = QString::fromUtf8(eq < 0 ? QByteArray() : part.mid(eq + 1));
    if (key == QLatin1String("code")) query.code = value;
    else if (key == QLatin1String("state")) query.state = value;
    else if (key == QLatin1String("error")) query.error = value;
  }
  if (query.error.isEmpty() && query.code.isEmpty()) {
    query.error = QStringLiteral("The browser returned no authorization code.");
  }
  return query;
}

QString authorizationCodeBody(const OAuthClient& client, const QString& code, const QString& redirectUri,
                              const QString& verifier) {
  QString body = QStringLiteral("grant_type=authorization_code&code=%1&client_id=%2&redirect_uri=%3&code_verifier=%4")
                     .arg(urlEncode(code), urlEncode(client.clientId), urlEncode(redirectUri), urlEncode(verifier));
  if (!client.clientSecret.isEmpty()) body += QStringLiteral("&client_secret=") + urlEncode(client.clientSecret);
  return body;
}

QString refreshBody(const OAuthClient& client, const QString& refreshToken) {
  QString body = QStringLiteral("grant_type=refresh_token&refresh_token=%1&client_id=%2")
                     .arg(urlEncode(refreshToken), urlEncode(client.clientId));
  if (!client.clientSecret.isEmpty()) body += QStringLiteral("&client_secret=") + urlEncode(client.clientSecret);
  return body;
}

TokenResult parseTokenResponse(const QByteArray& json, qint64 nowUnix, const QString& previousRefreshToken) {
  TokenResult result;
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    result.error = parseError.error == QJsonParseError::NoError ? QStringLiteral("The token response was empty.")
                                                                : parseError.errorString();
    return result;
  }
  const QJsonObject object = document.object();
  if (object.contains(QLatin1String("error"))) {
    result.error = object.value(QLatin1String("error_description")).toString();
    if (result.error.isEmpty()) result.error = object.value(QLatin1String("error")).toString();
    if (result.error.isEmpty()) result.error = QStringLiteral("Google refused the token request.");
    return result;
  }
  result.tokens.accessToken = object.value(QLatin1String("access_token")).toString();
  result.tokens.refreshToken = object.value(QLatin1String("refresh_token")).toString();
  if (result.tokens.refreshToken.isEmpty()) result.tokens.refreshToken = previousRefreshToken;
  const int expiresIn = object.value(QLatin1String("expires_in")).toInt(3600);
  result.tokens.expiryUnix = nowUnix + expiresIn;
  if (result.tokens.accessToken.isEmpty()) result.error = QStringLiteral("Google did not return an access token.");
  return result;
}

bool accessExpiring(const OAuthTokens& tokens, qint64 nowUnix) {
  return tokens.accessToken.isEmpty() || tokens.expiryUnix <= nowUnix + 60;
}

TokenResult exchangeCode(HttpClient& http, const OAuthClient& client, const QString& code, const QString& redirectUri,
                         const QString& verifier, qint64 nowUnix) {
  return postToken(http, authorizationCodeBody(client, code, redirectUri, verifier), nowUnix, {});
}

TokenResult refreshAccess(HttpClient& http, const OAuthClient& client, const OAuthTokens& current, qint64 nowUnix) {
  if (current.refreshToken.isEmpty()) {
    TokenResult result;
    result.error = QStringLiteral("Sign in to Google Contacts again.");
    return result;
  }
  return postToken(http, refreshBody(client, current.refreshToken), nowUnix, current.refreshToken);
}

TokenResult ensureAccess(HttpClient& http, const OAuthClient& client, OAuthTokens& tokens, qint64 nowUnix) {
  if (!accessExpiring(tokens, nowUnix)) {
    TokenResult result;
    result.tokens = tokens;
    return result;
  }
  auto refreshed = refreshAccess(http, client, tokens, nowUnix);
  if (refreshed.ok()) tokens = refreshed.tokens;
  return refreshed;
}

BrowserCommand browserCommand(const QString& url, bool haveOmarchy) {
  BrowserCommand command;
  if (haveOmarchy) command.argv = {QStringLiteral("omarchy"), QStringLiteral("launch"), QStringLiteral("browser"), url};
  else command.argv = {QStringLiteral("xdg-open"), url};
  return command;
}

}  // namespace contacts
