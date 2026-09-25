#pragma once

#include "contact.h"
#include "store.h"

#include <QString>
#include <QWidget>

#include <functional>
#include <optional>

class QPixmap;

namespace contacts {

enum class EditorField {
  Given,
  Family,
  Organization,
  Title,
  Email,
  Phone,
  Notes,
  Prefix,
  Middle,
  Suffix,
  Nickname,
  FileAs,
  PhoneticGiven,
  PhoneticMiddle,
  PhoneticFamily,
  Department,
  Occupation,
  Birthday,
  Address,
  Url,
  Relation,
  Event,
  Im,
  Sip,
  Custom,
  OtherOrganization
};

struct EditorFocus {
  EditorField field = EditorField::Given;
  int index = 0;
};

// The contact editor fills the detail area. It is not a separate window.
class ContactEditor : public QWidget {
 public:
  ContactEditor(const Contact& base, EditorFocus focus, QWidget* parent = nullptr);
  ~ContactEditor() override;

  void setOnSave(std::function<void()> save);
  void setOnCancel(std::function<void()> cancel);
  std::optional<Contact> validatedContact();
  QByteArray photoBytes() const;
  bool removePhoto() const;
  void setPreview(const QPixmap& pixmap);
  void focusField();

 private:
  struct Form;
  Form* form_ = nullptr;
};

bool confirmDialog(QWidget* parent, const QString& title, const QString& body, const QString& action);

void showHelpDialog(QWidget* parent);

enum class AccountAction { Save, Connect, Disconnect };

struct AccountResult {
  AccountAction action = AccountAction::Save;
  AppConfig config;
};

std::optional<AccountResult> accountDialog(QWidget* parent, const AppConfig& config, bool connected);

}  // namespace contacts
