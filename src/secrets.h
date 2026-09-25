#pragma once

#include <QString>

#include <memory>
#include <optional>

namespace omacontacts {

class SecretStore {
 public:
  virtual ~SecretStore() = default;
  virtual std::optional<QString> load(QString& error) = 0;
  virtual bool save(const QString& secret, QString& error) = 0;
  virtual bool clear(QString& error) = 0;
};

std::unique_ptr<SecretStore> makeSecretStore();

}  // namespace omacontacts
