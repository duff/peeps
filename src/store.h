#pragma once

#include "contact.h"
#include "oauth.h"

#include <QString>
#include <optional>

namespace contacts {

struct AppPaths {
  QString cacheFile;
  QString photoDir;

  static AppPaths forEnv(const QString& home, const QString& xdgDataHome, const QString& xdgCacheHome = {});
};

// The Google client Peeps signs in with, built in from PEEPS_GOOGLE_CLIENT_ID (and
// PEEPS_GOOGLE_CLIENT_SECRET). Empty when the build has none, and then nobody can connect.
// Desktop client ids are not secret: Google says an installed app cannot keep one.
OAuthClient builtInClient();

struct CacheSnapshot {
  QList<Contact> contacts;
  QString syncToken;
  QString error;
  bool ok() const { return error.isEmpty(); }
};

QByteArray writeCacheJson(const QList<Contact>& contacts, const QString& syncToken);
CacheSnapshot readCacheJson(const QByteArray& json);
QByteArray writeTokensJson(const OAuthTokens& tokens);
std::optional<OAuthTokens> readTokensJson(const QByteArray& json);

bool writePrivateFile(const QString& path, const QByteArray& contents, QString& error);
std::optional<QByteArray> readFile(const QString& path);

}  // namespace contacts
