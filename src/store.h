#pragma once

#include "contact.h"
#include "oauth.h"

#include <QString>
#include <optional>

namespace contacts {

struct AppPaths {
  QString settingsFile;
  QString cacheFile;
  QString photoDir;

  static AppPaths forEnv(const QString& home, const QString& xdgConfigHome, const QString& xdgDataHome,
                         const QString& xdgCacheHome = {});
};

// The Google client Peeps signs in with, built in from PEEPS_GOOGLE_CLIENT_ID (and
// PEEPS_GOOGLE_CLIENT_SECRET). Empty when the build has none, and then nobody can connect.
// Desktop client ids are not secret: Google says an installed app cannot keep one.
OAuthClient builtInClient();

// The selected field names its shortcut until it has been used this often.
inline constexpr int kHintLearnedAfter = 10;

// Choices that belong to Peeps rather than to Omarchy.
struct AppSettings {
  // Added to Omarchy's text size. Ctrl+= and Ctrl+- change it.
  int textSizeOffset = 0;
  // Copies and emails done from the keyboard, counted up to kHintLearnedAfter.
  int keyboardCopies = 0;
  int keyboardEmails = 0;

  bool copyLearned() const { return keyboardCopies >= kHintLearnedAfter; }
  bool emailLearned() const { return keyboardEmails >= kHintLearnedAfter; }
};

struct CacheSnapshot {
  QList<Contact> contacts;
  QString syncToken;
  // When the cache last matched Google, in Unix seconds. Zero when unknown.
  qint64 syncedAt = 0;
  QString error;
  bool ok() const { return error.isEmpty(); }
};

QByteArray writeSettingsJson(const AppSettings& settings);
AppSettings readSettingsJson(const QByteArray& json);
QByteArray writeCacheJson(const QList<Contact>& contacts, const QString& syncToken, qint64 syncedAt = 0);

// "Synced 3 hours ago", "Synced yesterday", and so on, for the footer.
QString syncedAgo(qint64 syncedAt, qint64 nowUnix);
CacheSnapshot readCacheJson(const QByteArray& json);
QByteArray writeTokensJson(const OAuthTokens& tokens);
std::optional<OAuthTokens> readTokensJson(const QByteArray& json);

bool writePrivateFile(const QString& path, const QByteArray& contents, QString& error);
std::optional<QByteArray> readFile(const QString& path);

}  // namespace contacts
