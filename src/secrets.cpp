#include "secrets.h"

#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")

namespace contacts {
namespace {

const SecretSchema* schemaNamed(const char* name) {
  return secret_schema_new(name, SECRET_SCHEMA_NONE, "account", SECRET_SCHEMA_ATTRIBUTE_STRING, nullptr);
}

const SecretSchema* tokenSchema() {
  static const SecretSchema* schema = schemaNamed("org.omelia.contacts");
  return schema;
}

// Tokens saved before the schema rename.
const SecretSchema* legacyTokenSchema() {
  static const SecretSchema* schema = schemaNamed("org.omelia.omacontacts");
  return schema;
}

std::optional<QString> lookupPassword(const SecretSchema* schema, QString& error) {
  GError* gerror = nullptr;
  gchar* secret = secret_password_lookup_sync(schema, nullptr, &gerror, "account", "default", nullptr);
  if (gerror != nullptr) {
    error = QString::fromUtf8(gerror->message);
    g_error_free(gerror);
    return std::nullopt;
  }
  if (secret == nullptr) return std::nullopt;
  const QString value = QString::fromUtf8(secret);
  secret_password_free(secret);
  return value;
}

bool clearSchema(const SecretSchema* schema, QString& error) {
  GError* gerror = nullptr;
  secret_password_clear_sync(schema, nullptr, &gerror, "account", "default", nullptr);
  if (gerror != nullptr) {
    error = QString::fromUtf8(gerror->message);
    g_error_free(gerror);
    return false;
  }
  return true;
}

class LibsecretStore final : public SecretStore {
 public:
  std::optional<QString> load(QString& error) override {
    if (const auto current = lookupPassword(tokenSchema(), error)) return current;
    if (!error.isEmpty()) return std::nullopt;
    const auto legacy = lookupPassword(legacyTokenSchema(), error);
    if (!legacy) return std::nullopt;
    QString moveError;
    if (save(*legacy, moveError)) {
      QString clearError;
      clearSchema(legacyTokenSchema(), clearError);
    }
    return legacy;
  }

  bool save(const QString& secret, QString& error) override {
    GError* gerror = nullptr;
    const QByteArray copy = secret.toUtf8();
    const gboolean stored =
        secret_password_store_sync(tokenSchema(), SECRET_COLLECTION_DEFAULT, "Contacts Google", copy.constData(),
                                   nullptr, &gerror, "account", "default", nullptr);
    if (gerror != nullptr) {
      error = QString::fromUtf8(gerror->message);
      g_error_free(gerror);
      return false;
    }
    if (!stored) error = QStringLiteral("The keyring did not store the Google sign-in.");
    return stored;
  }

  bool clear(QString& error) override {
    const bool current = clearSchema(tokenSchema(), error);
    QString legacyError;
    const bool legacy = clearSchema(legacyTokenSchema(), legacyError);
    if (current && legacy) return true;
    if (error.isEmpty()) error = legacyError;
    return false;
  }
};

}  // namespace

std::unique_ptr<SecretStore> makeSecretStore() { return std::make_unique<LibsecretStore>(); }

}  // namespace contacts
