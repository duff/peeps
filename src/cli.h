#pragma once

#include "contact.h"
#include "google.h"
#include "http.h"
#include "secrets.h"
#include "store.h"

#include <QStringList>

#include <functional>
#include <optional>

namespace contacts {

constexpr int kCliOk = 0;
constexpr int kCliFailed = 1;
constexpr int kCliUsage = 2;
constexpr int kCliSignedOut = 3;
constexpr int kCliNotFound = 4;

struct CliResult {
  int code = kCliOk;
  QString out;
  QString err;
};

// In-memory contacts plus the hooks a command needs. Reads never call them.
// Tests supply the hooks; the real binary fills them from the keyring and Google.
struct CliSession {
  QList<Contact> contacts;
  QString syncToken;
  bool signedIn = false;
  QString authError;
  std::function<WriteResult(const Contact&)> create;
  std::function<WriteResult(const Contact&)> update;
  std::function<QString(const QString& id)> remove;
  std::function<PeopleResult(bool full)> syncPeople;
  std::function<WriteResult(const QString& id, const QByteArray& jpeg, bool removePhoto)> photo;
  std::function<bool(const QList<Contact>& contacts, const QString& syncToken, QString& error)> save;
  std::optional<QByteArray> stdinBytes;
};

struct CliLaunch {
  AppPaths paths;
  OAuthClient client = builtInClient();
  SecretStore* secrets = nullptr;
  HttpClient* http = nullptr;
  bool stdoutIsTty = false;
  std::optional<QByteArray> stdinBytes;
};

// True when argv should stay in the terminal instead of opening the window.
bool isCliInvocation(const QStringList& args);

// args does not include the program name. session.contacts is the cache.
CliResult runCli(const QStringList& args, CliSession& session, bool stdoutIsTty);

// Loads the cache, and for writes the Google account, then runs the command.
// Prints nothing. The caller writes out and err.
CliResult runContactsCommand(const QStringList& args, const CliLaunch& launch);

}  // namespace contacts
