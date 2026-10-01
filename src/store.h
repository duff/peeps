#pragma once

#include "contact.h"
#include "oauth.h"

#include <QString>
#include <optional>

namespace contacts {

struct AppPaths {
  QString configFile;
  QString cacheFile;
  QString photoDir;

  static AppPaths forEnv(const QString& home, const QString& xdgConfigHome, const QString& xdgDataHome,
                         const QString& xdgCacheHome = {});
};

struct AppConfig {
  QString clientId;
  QString clientSecret;
};

// A packaged build can carry a published OAuth client so people only sign in.
// Build with CONTACTS_GOOGLE_CLIENT_ID (and CONTACTS_GOOGLE_CLIENT_SECRET) set.
// Desktop client ids are not secret: Google says an installed app cannot keep one.
AppConfig builtInClient();
// The client id saved in Account, or the built-in one when none is saved.
AppConfig effectiveClient(const AppConfig& saved);

struct CacheSnapshot {
  QList<Contact> contacts;
  QString syncToken;
  QString error;
  bool ok() const { return error.isEmpty(); }
};

QByteArray writeConfigJson(const AppConfig& config);
std::optional<AppConfig> readConfigJson(const QByteArray& json);
QByteArray writeCacheJson(const QList<Contact>& contacts, const QString& syncToken);
CacheSnapshot readCacheJson(const QByteArray& json);
QByteArray writeTokensJson(const OAuthTokens& tokens);
std::optional<OAuthTokens> readTokensJson(const QByteArray& json);

bool writePrivateFile(const QString& path, const QByteArray& contents, QString& error);
std::optional<QByteArray> readFile(const QString& path);

}  // namespace contacts
