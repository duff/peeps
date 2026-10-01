#pragma once

#include "contact.h"
#include "store.h"

#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
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
// Fields sit in titled sections: who the person is, how to reach them, where
// they are, and notes. Rarer fields wait behind More fields until a contact
// has one.
class ContactEditor : public QWidget {
 public:
  ContactEditor(const Contact& base, EditorFocus focus, QWidget* parent = nullptr);
  ~ContactEditor() override;

  void setOnSave(std::function<void()> save);
  void setOnCancel(std::function<void()> cancel);
  std::optional<Contact> validatedContact();
  // True when saving would change the contact or its photo.
  bool hasChanges() const;
  QByteArray photoBytes() const;
  bool removePhoto() const;
  void setPreview(const QPixmap& pixmap);
  void focusField();

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  struct Form;
  std::unique_ptr<Form> form_;
};

bool confirmDialog(QWidget* parent, const QString& title, const QString& body, const QString& action,
                   const QString& cancel = QStringLiteral("Cancel"));

void showHelpDialog(QWidget* parent);

enum class AccountAction { Save, Connect, Disconnect };

struct AccountResult {
  AccountAction action = AccountAction::Save;
  AppConfig config;
};

std::optional<AccountResult> accountDialog(QWidget* parent, const AppConfig& config, bool connected);

}  // namespace contacts
