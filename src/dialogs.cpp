#include "dialogs.h"

#include "shortcuts.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace omacontacts {
namespace {

QPushButton* kindButton(const QString& text, const QString& kind) {
  auto* button = new QPushButton(text);
  button->setProperty("kind", kind);
  button->setAccessibleName(text);
  button->setCursor(Qt::PointingHandCursor);
  return button;
}

QLabel* caption(const QString& text) {
  auto* label = new QLabel(text);
  label->setObjectName(QStringLiteral("secondary"));
  return label;
}

Key mapKey(int key) {
  switch (key) {
    case Qt::Key_Up:
      return Key::Up;
    case Qt::Key_Down:
      return Key::Down;
    case Qt::Key_Escape:
      return Key::Escape;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      return Key::Return;
    case Qt::Key_Question:
      return Key::Question;
    case Qt::Key_F1:
      return Key::F1;
    case Qt::Key_S:
      return Key::S;
    default:
      return Key::Other;
  }
}

KeyEvent eventFrom(QKeyEvent* event, Surface surface) {
  KeyEvent mapped;
  mapped.key = mapKey(event->key());
  mapped.ctrl = event->modifiers() & Qt::ControlModifier;
  mapped.shift = event->modifiers() & Qt::ShiftModifier;
  mapped.alt = event->modifiers() & Qt::AltModifier;
  mapped.textInput = true;
  mapped.surface = surface;
  return mapped;
}

class EditDialog : public QDialog {
 public:
  EditDialog(QWidget* parent, const Contact& base) : QDialog(parent), base_(base) {
    setWindowTitle(base.resourceName.isEmpty() ? QStringLiteral("New contact") : QStringLiteral("Edit contact"));
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumWidth(420);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(14);
    auto* title = new QLabel(windowTitle());
    title->setObjectName(QStringLiteral("title"));
    root->addWidget(title);

    given_ = new QLineEdit(base.givenName);
    family_ = new QLineEdit(base.familyName);
    organization_ = new QLineEdit(base.organization);
    titleEdit_ = new QLineEdit(base.title);
    root->addWidget(caption(QStringLiteral("Given name")));
    root->addWidget(given_);
    root->addWidget(caption(QStringLiteral("Family name")));
    root->addWidget(family_);
    root->addWidget(caption(QStringLiteral("Organization")));
    root->addWidget(organization_);
    root->addWidget(caption(QStringLiteral("Title")));
    root->addWidget(titleEdit_);

    root->addWidget(caption(QStringLiteral("Email")));
    emailLayout_ = new QVBoxLayout;
    emailLayout_->setSpacing(6);
    root->addLayout(emailLayout_);
    for (const auto& email : base.emails) addEmail(email);
    if (base.emails.isEmpty()) addEmail({});
    auto* addEmailButton = kindButton(QStringLiteral("Add email"), QStringLiteral("outline"));
    root->addWidget(addEmailButton, 0, Qt::AlignLeft);
    connect(addEmailButton, &QPushButton::clicked, this, [this] { addEmail({}); });

    root->addWidget(caption(QStringLiteral("Phone")));
    phoneLayout_ = new QVBoxLayout;
    phoneLayout_->setSpacing(6);
    root->addLayout(phoneLayout_);
    for (const auto& phone : base.phones) addPhone(phone);
    auto* addPhoneButton = kindButton(QStringLiteral("Add phone"), QStringLiteral("outline"));
    root->addWidget(addPhoneButton, 0, Qt::AlignLeft);
    connect(addPhoneButton, &QPushButton::clicked, this, [this] { addPhone({}); });

    root->addWidget(caption(QStringLiteral("Notes")));
    notes_ = new QPlainTextEdit(base.notes);
    notes_->setObjectName(QStringLiteral("notes"));
    notes_->setMinimumHeight(96);
    root->addWidget(notes_);

    error_ = new QLabel;
    error_->setObjectName(QStringLiteral("danger"));
    error_->setWordWrap(true);
    root->addWidget(error_);

    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    auto* cancel = kindButton(QStringLiteral("Cancel"), QStringLiteral("outline"));
    auto* save = kindButton(QStringLiteral("Save"), QStringLiteral("primary"));
    save->setDefault(false);
    cancel->setDefault(true);
    cancel->setAutoDefault(true);
    footer->addWidget(cancel);
    footer->addWidget(save);
    root->addLayout(footer);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this] { trySave(); });
    given_->setFocus();
  }

  Contact result() const { return result_; }

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    const Command command = routeKey(eventFrom(event, Surface::Editor));
    if (command == Command::Cancel) {
      reject();
      return;
    }
    if (command == Command::Save) {
      trySave();
      return;
    }
    QDialog::keyPressEvent(event);
  }

 private:
  void addEmail(const LabeledValue& value) { addRow(emailLayout_, emails_, value, QStringLiteral("name@example.com")); }
  void addPhone(const LabeledValue& value) { addRow(phoneLayout_, phones_, value, QStringLiteral("+1 555 010 1000")); }

  void addRow(QVBoxLayout* layout, QList<QPair<QLineEdit*, QLineEdit*>>& rows, const LabeledValue& value,
              const QString& placeholder) {
    auto* row = new QWidget;
    auto* box = new QHBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(6);
    auto* edit = new QLineEdit(value.value);
    edit->setPlaceholderText(placeholder);
    auto* label = new QLineEdit(value.label);
    label->setPlaceholderText(QStringLiteral("home"));
    label->setFixedWidth(90);
    auto* remove = kindButton(QStringLiteral("Remove"), QStringLiteral("outline"));
    box->addWidget(edit, 1);
    box->addWidget(label);
    box->addWidget(remove);
    layout->addWidget(row);
    rows.append(QPair<QLineEdit*, QLineEdit*>(edit, label));
    connect(remove, &QPushButton::clicked, this, [this, edit, &rows, row] {
      for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).first == edit) {
          rows.removeAt(i);
          break;
        }
      }
      row->deleteLater();
    });
  }

  void trySave() {
    Draft draft;
    draft.givenName = given_->text();
    draft.familyName = family_->text();
    draft.organization = organization_->text();
    draft.title = titleEdit_->text();
    draft.notes = notes_->toPlainText();
    for (const auto& row : emails_) draft.emails.append({row.first->text(), row.second->text()});
    for (const auto& row : phones_) draft.phones.append({row.first->text(), row.second->text()});
    const QString error = draft.validate();
    if (!error.isEmpty()) {
      error_->setText(error);
      return;
    }
    result_ = draft.toContact(base_);
    accept();
  }

  Contact base_;
  Contact result_;
  QLineEdit* given_ = nullptr;
  QLineEdit* family_ = nullptr;
  QLineEdit* organization_ = nullptr;
  QLineEdit* titleEdit_ = nullptr;
  QPlainTextEdit* notes_ = nullptr;
  QVBoxLayout* emailLayout_ = nullptr;
  QVBoxLayout* phoneLayout_ = nullptr;
  QList<QPair<QLineEdit*, QLineEdit*>> emails_;
  QList<QPair<QLineEdit*, QLineEdit*>> phones_;
  QLabel* error_ = nullptr;
};

class ConfirmBox : public QDialog {
 public:
  ConfirmBox(QWidget* parent, const QString& titleText, const QString& body, const QString& action) : QDialog(parent) {
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumWidth(420);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(14);
    auto* title = new QLabel(titleText);
    title->setObjectName(QStringLiteral("title"));
    title->setWordWrap(true);
    auto* description = new QLabel(body);
    description->setObjectName(QStringLiteral("secondary"));
    description->setWordWrap(true);
    root->addWidget(title);
    root->addWidget(description);
    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    auto* cancel = kindButton(QStringLiteral("Cancel"), QStringLiteral("outline"));
    auto* confirm = kindButton(action, QStringLiteral("danger"));
    cancel->setDefault(true);
    cancel->setAutoDefault(true);
    footer->addWidget(cancel);
    footer->addWidget(confirm);
    root->addLayout(footer);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, this, &QDialog::accept);
    cancel->setFocus();
  }

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (routeKey(eventFrom(event, Surface::ConfirmDelete)) == Command::Cancel) {
      reject();
      return;
    }
    QDialog::keyPressEvent(event);
  }
};

class HelpBox : public QDialog {
 public:
  explicit HelpBox(QWidget* parent) : QDialog(parent) {
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumWidth(420);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(8);
    auto* title = new QLabel(QStringLiteral("Shortcuts"));
    title->setObjectName(QStringLiteral("title"));
    root->addWidget(title);
    for (const auto& row : bindingHelp()) {
      auto* line = new QHBoxLayout;
      auto* keys = new QLabel(row.keys);
      keys->setObjectName(QStringLiteral("accent"));
      keys->setMinimumWidth(140);
      auto* action = new QLabel(row.action);
      action->setObjectName(QStringLiteral("secondary"));
      line->addWidget(keys);
      line->addWidget(action, 1);
      root->addLayout(line);
    }
    auto* close = kindButton(QStringLiteral("Close"), QStringLiteral("outline"));
    close->setDefault(true);
    root->addSpacing(8);
    root->addWidget(close, 0, Qt::AlignRight);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
  }

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (routeKey(eventFrom(event, Surface::Help)) == Command::Cancel) {
      accept();
      return;
    }
    QDialog::keyPressEvent(event);
  }
};

class AccountBox : public QDialog {
 public:
  AccountBox(QWidget* parent, const AppConfig& config, bool connected) : QDialog(parent), result_{AccountAction::Save, config} {
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumWidth(420);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("Google account"));
    title->setObjectName(QStringLiteral("title"));
    auto* help = new QLabel(QStringLiteral(
        "In Google Cloud, enable the People API and create an OAuth client of type Desktop. "
        "While the consent screen is in testing, add your Google account as a test user. "
        "Paste the client id here. The client secret is optional for a desktop client."));
    help->setWordWrap(true);
    help->setObjectName(QStringLiteral("secondary"));
    root->addWidget(title);
    root->addWidget(help);
    root->addWidget(caption(QStringLiteral("Client id")));
    clientId_ = new QLineEdit(config.clientId);
    root->addWidget(clientId_);
    root->addWidget(caption(QStringLiteral("Client secret")));
    secret_ = new QLineEdit(config.clientSecret);
    secret_->setEchoMode(QLineEdit::Password);
    root->addWidget(secret_);
    error_ = new QLabel;
    error_->setObjectName(QStringLiteral("danger"));
    error_->setWordWrap(true);
    root->addWidget(error_);
    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    auto* cancel = kindButton(QStringLiteral("Cancel"), QStringLiteral("outline"));
    cancel->setDefault(true);
    footer->addWidget(cancel);
    if (connected) {
      auto* disconnect = kindButton(QStringLiteral("Disconnect"), QStringLiteral("danger"));
      footer->addWidget(disconnect);
      connect(disconnect, &QPushButton::clicked, this, [this] {
        readFields();
        result_.action = AccountAction::Disconnect;
        accept();
      });
    }
    auto* save = kindButton(QStringLiteral("Save"), QStringLiteral("outline"));
    auto* connectButton = kindButton(QStringLiteral("Connect"), QStringLiteral("primary"));
    footer->addWidget(save);
    footer->addWidget(connectButton);
    root->addLayout(footer);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this] {
      if (!readFields()) return;
      result_.action = AccountAction::Save;
      accept();
    });
    connect(connectButton, &QPushButton::clicked, this, [this] {
      if (!readFields()) return;
      result_.action = AccountAction::Connect;
      accept();
    });
    clientId_->setFocus();
  }

  AccountResult result() const { return result_; }

 protected:
  void keyPressEvent(QKeyEvent* event) override {
    if (routeKey(eventFrom(event, Surface::Account)) == Command::Cancel) {
      reject();
      return;
    }
    QDialog::keyPressEvent(event);
  }

 private:
  bool readFields() {
    result_.config.clientId = clientId_->text().trimmed();
    result_.config.clientSecret = secret_->text().trimmed();
    if (result_.config.clientId.isEmpty()) {
      error_->setText(QStringLiteral("Enter the Google client id."));
      return false;
    }
    error_->clear();
    return true;
  }

  AccountResult result_;
  QLineEdit* clientId_ = nullptr;
  QLineEdit* secret_ = nullptr;
  QLabel* error_ = nullptr;
};

}  // namespace

std::optional<Contact> editContactDialog(QWidget* parent, const Contact& base) {
  EditDialog dialog(parent, base);
  if (dialog.exec() != QDialog::Accepted) return std::nullopt;
  return dialog.result();
}

bool confirmDialog(QWidget* parent, const QString& title, const QString& body, const QString& action) {
  ConfirmBox dialog(parent, title, body, action);
  return dialog.exec() == QDialog::Accepted;
}

void showHelpDialog(QWidget* parent) {
  HelpBox dialog(parent);
  dialog.exec();
}

std::optional<AccountResult> accountDialog(QWidget* parent, const AppConfig& config, bool connected) {
  AccountBox dialog(parent, config, connected);
  if (dialog.exec() != QDialog::Accepted) return std::nullopt;
  return dialog.result();
}

}  // namespace omacontacts
