#include "secrets.h"

#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")

namespace omacontacts {
namespace {

const SecretSchema* tokenSchema() {
  static SecretSchema* schema = secret_schema_new("org.omelia.omacontacts", SECRET_SCHEMA_NONE, "account",
                                                  SECRET_SCHEMA_ATTRIBUTE_STRING, nullptr);
  return schema;
}

class LibsecretStore final : public SecretStore {
 public:
  std::optional<QString> load(QString& error) override {
    GError* gerror = nullptr;
    gchar* secret = secret_password_lookup_sync(tokenSchema(), nullptr, &gerror, "account", "default", nullptr);
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
    GError* gerror = nullptr;
    secret_password_clear_sync(tokenSchema(), nullptr, &gerror, "account", "default", nullptr);
    if (gerror != nullptr) {
      error = QString::fromUtf8(gerror->message);
      g_error_free(gerror);
      return false;
    }
    return true;
  }
};

}  // namespace

std::unique_ptr<SecretStore> makeSecretStore() { return std::make_unique<LibsecretStore>(); }

}  // namespace omacontacts
