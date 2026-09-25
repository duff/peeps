#include "store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace omacontacts {
namespace {

QJsonArray labeledJson(const QList<LabeledValue>& values) {
  QJsonArray array;
  for (const auto& value : values) {
    QJsonObject object;
    object.insert(QStringLiteral("value"), value.value);
    object.insert(QStringLiteral("label"), value.label);
    array.append(object);
  }
  return array;
}

QList<LabeledValue> labeledFrom(const QJsonArray& array) {
  QList<LabeledValue> values;
  for (const QJsonValue& value : array) {
    if (!value.isObject()) continue;
    LabeledValue labeled;
    labeled.value = value.toObject().value(QLatin1String("value")).toString();
    labeled.label = value.toObject().value(QLatin1String("label")).toString();
    if (!labeled.value.isEmpty()) values.append(labeled);
  }
  return values;
}

Contact contactFromObject(const QJsonObject& object) {
  Contact contact;
  contact.resourceName = object.value(QLatin1String("resource_name")).toString();
  contact.etag = object.value(QLatin1String("etag")).toString();
  contact.givenName = object.value(QLatin1String("given_name")).toString();
  contact.familyName = object.value(QLatin1String("family_name")).toString();
  contact.organization = object.value(QLatin1String("organization")).toString();
  contact.title = object.value(QLatin1String("title")).toString();
  contact.notes = object.value(QLatin1String("notes")).toString();
  contact.photoUrl = object.value(QLatin1String("photo_url")).toString();
  contact.emails = labeledFrom(object.value(QLatin1String("emails")).toArray());
  contact.phones = labeledFrom(object.value(QLatin1String("phones")).toArray());
  return contact;
}

}  // namespace

AppPaths AppPaths::forEnv(const QString& home, const QString& xdgConfigHome, const QString& xdgDataHome,
                           const QString& xdgCacheHome) {
  AppPaths paths;
  const QString configRoot = xdgConfigHome.isEmpty() ? home + QStringLiteral("/.config") : xdgConfigHome;
  const QString dataRoot = xdgDataHome.isEmpty() ? home + QStringLiteral("/.local/share") : xdgDataHome;
  const QString cacheRoot = xdgCacheHome.isEmpty() ? home + QStringLiteral("/.cache") : xdgCacheHome;
  paths.configFile = configRoot + QStringLiteral("/omacontacts/config.json");
  paths.cacheFile = dataRoot + QStringLiteral("/omacontacts/contacts.json");
  paths.photoDir = cacheRoot + QStringLiteral("/omacontacts/photos");
  return paths;
}

QByteArray writeConfigJson(const AppConfig& config) {
  QJsonObject object;
  object.insert(QStringLiteral("client_id"), config.clientId);
  object.insert(QStringLiteral("client_secret"), config.clientSecret);
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<AppConfig> readConfigJson(const QByteArray& json) {
  const QJsonDocument document = QJsonDocument::fromJson(json);
  if (!document.isObject()) return std::nullopt;
  AppConfig config;
  config.clientId = document.object().value(QLatin1String("client_id")).toString();
  config.clientSecret = document.object().value(QLatin1String("client_secret")).toString();
  return config;
}

QByteArray writeCacheJson(const QList<Contact>& contacts, const QString& syncToken) {
  QJsonArray array;
  for (const auto& contact : contacts) {
    QJsonObject object;
    object.insert(QStringLiteral("resource_name"), contact.resourceName);
    object.insert(QStringLiteral("etag"), contact.etag);
    object.insert(QStringLiteral("given_name"), contact.givenName);
    object.insert(QStringLiteral("family_name"), contact.familyName);
    object.insert(QStringLiteral("organization"), contact.organization);
    object.insert(QStringLiteral("title"), contact.title);
    object.insert(QStringLiteral("notes"), contact.notes);
    object.insert(QStringLiteral("photo_url"), contact.photoUrl);
    object.insert(QStringLiteral("emails"), labeledJson(contact.emails));
    object.insert(QStringLiteral("phones"), labeledJson(contact.phones));
    array.append(object);
  }
  QJsonObject root;
  root.insert(QStringLiteral("version"), 2);
  root.insert(QStringLiteral("sync_token"), syncToken);
  root.insert(QStringLiteral("contacts"), array);
  return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

CacheSnapshot readCacheJson(const QByteArray& json) {
  CacheSnapshot snapshot;
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    snapshot.error = parseError.error == QJsonParseError::NoError ? QStringLiteral("Could not read the contact cache.")
                                                                  : parseError.errorString();
    return snapshot;
  }
  const int version = document.object().value(QLatin1String("version")).toInt();
  if (version >= 2) snapshot.syncToken = document.object().value(QLatin1String("sync_token")).toString();
  const QJsonArray array = document.object().value(QLatin1String("contacts")).toArray();
  snapshot.contacts.reserve(array.size());
  for (const QJsonValue& value : array) {
    if (value.isObject()) snapshot.contacts.append(contactFromObject(value.toObject()));
  }
  return snapshot;
}

QByteArray writeTokensJson(const OAuthTokens& tokens) {
  QJsonObject object;
  object.insert(QStringLiteral("access_token"), tokens.accessToken);
  object.insert(QStringLiteral("refresh_token"), tokens.refreshToken);
  object.insert(QStringLiteral("expiry_unix"), static_cast<double>(tokens.expiryUnix));
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<OAuthTokens> readTokensJson(const QByteArray& json) {
  const QJsonDocument document = QJsonDocument::fromJson(json);
  if (!document.isObject()) return std::nullopt;
  OAuthTokens tokens;
  tokens.accessToken = document.object().value(QLatin1String("access_token")).toString();
  tokens.refreshToken = document.object().value(QLatin1String("refresh_token")).toString();
  tokens.expiryUnix = static_cast<qint64>(document.object().value(QLatin1String("expiry_unix")).toDouble());
  if (tokens.accessToken.isEmpty() && tokens.refreshToken.isEmpty()) return std::nullopt;
  return tokens;
}

bool writePrivateFile(const QString& path, const QByteArray& contents, QString& error) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    error = QStringLiteral("Could not write %1.").arg(path);
    return false;
  }
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  if (file.write(contents) != contents.size() || !file.commit()) {
    error = QStringLiteral("Could not write %1.").arg(path);
    return false;
  }
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return true;
}

std::optional<QByteArray> readFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
  return file.readAll();
}

}  // namespace omacontacts
