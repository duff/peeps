#pragma once

#include "contact.h"
#include "oauth.h"

#include <QString>
#include <optional>

namespace omacontacts {

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

}  // namespace omacontacts
