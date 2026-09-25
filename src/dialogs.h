#pragma once

#include "contact.h"
#include "store.h"

#include <QString>

#include <optional>

class QWidget;

namespace omacontacts {

std::optional<Contact> editContactDialog(QWidget* parent, const Contact& base);

bool confirmDialog(QWidget* parent, const QString& title, const QString& body, const QString& action);

void showHelpDialog(QWidget* parent);

enum class AccountAction { Save, Connect, Disconnect };

struct AccountResult {
  AccountAction action = AccountAction::Save;
  AppConfig config;
};

std::optional<AccountResult> accountDialog(QWidget* parent, const AppConfig& config, bool connected);

}  // namespace omacontacts
