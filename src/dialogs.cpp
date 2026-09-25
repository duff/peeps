#include "dialogs.h"

#include "photos.h"
#include "shortcuts.h"

#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace contacts {
namespace {

QPushButton* kindButton(const QString& text, const QString& kind) {
  auto* button = new QPushButton(text);
  button->setProperty("kind", kind);
  button->setAccessibleName(text);
  button->setCursor(Qt::PointingHandCursor);
  button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  return button;
}

struct ValueRow {
  LabelField field = LabelField::Email;
  QComboBox* preset = nullptr;
  QLineEdit* editor = nullptr;
  QString originalType;
  QString originalDisplay;
};

QStringList emphasizedLabels(LabelField field) {
  if (field == LabelField::Phone) return {QStringLiteral("Mobile"), QStringLiteral("Work")};
  if (field == LabelField::Email || field == LabelField::Address) return {QStringLiteral("Home"), QStringLiteral("Work")};
  return {QStringLiteral("Home"), QStringLiteral("Work"), QStringLiteral("Other")};
}

bool fieldIsMore(EditorField field) {
  switch (field) {
    case EditorField::Title:
    case EditorField::Prefix:
    case EditorField::Middle:
    case EditorField::Suffix:
    case EditorField::Nickname:
    case EditorField::Department:
    case EditorField::Occupation:
    case EditorField::Birthday:
    case EditorField::Url:
    case EditorField::Relation:
    case EditorField::Event:
    case EditorField::Custom:
    case EditorField::OtherOrganization:
      return true;
    default:
      return false;
  }
}

DateValue dateFromEditor(const QString& text, const DateValue& original) {
  const QString trimmed = text.trimmed();
  if (trimmed == original.display()) return original;
  DateValue date;
  const QRegularExpression iso(QStringLiteral("^(\\d{4})-(\\d{1,2})-(\\d{1,2})$"));
  const QRegularExpression monthDay(QStringLiteral("^(\\d{1,2})-(\\d{1,2})$"));
  const auto full = iso.match(trimmed);
  if (full.hasMatch()) {
    date.year = full.captured(1).toInt();
    date.month = full.captured(2).toInt();
    date.day = full.captured(3).toInt();
    return date;
  }
  const auto partial = monthDay.match(trimmed);
  if (partial.hasMatch()) {
    date.month = partial.captured(1).toInt();
    date.day = partial.captured(2).toInt();
    return date;
  }
  date.text = trimmed;
  return date;
}

LabeledValue labelFrom(const ValueRow& row) {
  LabeledValue result;
  result.value = row.editor->text();
  const QString text = row.preset->currentText().trimmed();
  if (!row.originalType.isEmpty() && text == row.originalDisplay) {
    result.type = row.originalType;
    result.label = row.originalDisplay;
    return result;
  }
  const ResolvedLabel resolved = resolveLabel(row.field, text);
  result.type = resolved.type;
  result.label = resolved.display;
  return result;
}

class OpenListOnClick : public QObject {
 public:
  explicit OpenListOnClick(QComboBox* combo) : QObject(combo), combo_(combo) {}

 protected:
  bool eventFilter(QObject*, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonPress && combo_->completer() != nullptr) {
      QTimer::singleShot(0, combo_, [combo = combo_] {
        if (combo->lineEdit() != nullptr) combo->lineEdit()->selectAll();
        combo->completer()->setCompletionPrefix(QString());
        combo->completer()->complete();
      });
    }
    return false;
  }

 private:
  QComboBox* combo_ = nullptr;
};

QString labelBoxName(LabelField field) {
  if (field == LabelField::Email) return QStringLiteral("Email label");
  if (field == LabelField::Phone) return QStringLiteral("Phone label");
  if (field == LabelField::Address) return QStringLiteral("Address label");
  return QStringLiteral("Label");
}

void prepareLabelBox(QComboBox* preset, LabelField field) {
  preset->addItems(emphasizedLabels(field));
  preset->setEditable(true);
  preset->setInsertPolicy(QComboBox::NoInsert);
  preset->setFixedWidth(160);
  preset->setAccessibleName(labelBoxName(field));
  if (QCompleter* completer = preset->completer()) {
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchStartsWith);
    completer->setCompletionMode(QCompleter::PopupCompletion);
  }
  if (preset->lineEdit() != nullptr) preset->lineEdit()->installEventFilter(new OpenListOnClick(preset));
}

void showLabelBox(QComboBox* preset, LabelField field, const QString& type, const QString& label) {
  LabeledValue value;
  value.type = type;
  value.label = label;
  if (type.trimmed().isEmpty() && label.trimmed().isEmpty()) {
    preset->setEditText(QString());
    return;
  }
  const QString shown = labelDisplay(field, value);
  preset->setEditText(shown.isEmpty() ? type : shown);
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
    auto* title = new QLabel(QStringLiteral("Keybindings"));
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

struct AddressRow {
  QComboBox* type = nullptr;
  QLineEdit* street = nullptr;
  QLineEdit* extended = nullptr;
  QString poBox;
  QLineEdit* city = nullptr;
  QLineEdit* region = nullptr;
  QLineEdit* postal = nullptr;
  QLineEdit* country = nullptr;
  QString originalType;
  QString originalDisplay;
};

struct OrgRow {
  QLineEdit* name = nullptr;
  QLineEdit* title = nullptr;
  QLineEdit* department = nullptr;
};

struct EventRow {
  QLineEdit* type = nullptr;
  QLineEdit* date = nullptr;
  DateValue original;
};

class ContactEditor::Form {
 public:
  Contact base;
  EditorFocus focus;
  bool hadPhoto = false;
  bool removePhotoFlag = false;
  QByteArray photoBytes;
  std::function<void()> onSave;
  std::function<void()> onCancel;
  QScrollArea* scroll = nullptr;
  QLabel* photoLabel = nullptr;
  QPushButton* photoButton = nullptr;
  QPushButton* removePhotoButton = nullptr;
  QPushButton* moreButton = nullptr;
  QWidget* morePane = nullptr;
  QLineEdit* prefix = nullptr;
  QLineEdit* given = nullptr;
  QLineEdit* middle = nullptr;
  QLineEdit* family = nullptr;
  QLineEdit* suffix = nullptr;
  QLineEdit* nickname = nullptr;
  QLineEdit* organization = nullptr;
  QLineEdit* titleEdit = nullptr;
  QLineEdit* department = nullptr;
  QLineEdit* occupation = nullptr;
  QLineEdit* birthday = nullptr;
  DateValue originalBirthday;
  QPlainTextEdit* notes = nullptr;
  QVBoxLayout* emailLayout = nullptr;
  QVBoxLayout* phoneLayout = nullptr;
  QVBoxLayout* addressLayout = nullptr;
  QVBoxLayout* urlLayout = nullptr;
  QVBoxLayout* relationLayout = nullptr;
  QVBoxLayout* eventLayout = nullptr;
  QVBoxLayout* customLayout = nullptr;
  QVBoxLayout* orgLayout = nullptr;
  QLabel* orgCaption = nullptr;
  QLabel* urlCaption = nullptr;
  QLabel* relationCaption = nullptr;
  QLabel* eventCaption = nullptr;
  QLabel* customCaption = nullptr;
  QList<ValueRow> emails;
  QList<ValueRow> phones;
  QList<ValueRow> urls;
  QList<ValueRow> relations;
  QList<ValueRow> customs;
  QList<AddressRow> addresses;
  QList<EventRow> events;
  QList<OrgRow> orgs;
  QLabel* error = nullptr;
  bool revealed = false;
  bool moreOpen = false;

  void showPixmap(const QPixmap& pixmap) {
    if (photoLabel == nullptr) return;
    if (pixmap.isNull()) {
      photoLabel->setPixmap({});
      photoLabel->setText(QStringLiteral("No photo"));
      return;
    }
    const qreal ratio = std::max(1.0, photoLabel->devicePixelRatioF());
    QPixmap shown = pixmap.scaled(QSize(qRound(160 * ratio), qRound(160 * ratio)), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    shown.setDevicePixelRatio(ratio);
    photoLabel->setText({});
    photoLabel->setPixmap(shown);
  }

  void refreshPhotoButtons() {
    const bool has = !photoBytes.isEmpty() || (hadPhoto && !removePhotoFlag);
    if (photoButton != nullptr) photoButton->setText(has ? QStringLiteral("Change photo") : QStringLiteral("Add photo"));
    if (removePhotoButton != nullptr) removePhotoButton->setVisible(has);
  }

  void choosePhoto(QWidget* host) {
    const std::optional<QByteArray> chosen = choosePhotoBytes(host);
    if (!chosen) return;
    if (chosen->isEmpty()) {
      error->setText(QStringLiteral("Could not read that image."));
      return;
    }
    QImage image;
    image.loadFromData(*chosen);
    photoBytes = *chosen;
    removePhotoFlag = false;
    showPixmap(QPixmap::fromImage(image));
    error->setText({});
    refreshPhotoButtons();
  }

  void clearPhoto() {
    photoBytes.clear();
    removePhotoFlag = true;
    showPixmap({});
    refreshPhotoButtons();
  }

  void addEmail(LabeledValue value) { addRow(emailLayout, emails, LabelField::Email, value, QStringLiteral("name@example.com")); }

  void addPhone(LabeledValue value) { addRow(phoneLayout, phones, LabelField::Phone, value, QStringLiteral("+1 555 010 1000")); }

  void addSuggestedEmail() {
    LabeledValue value;
    value.type = QStringLiteral("home");
    value.label = QStringLiteral("Home");
    addEmail(value);
  }

  void addSuggestedPhone() {
    LabeledValue value;
    value.type = QStringLiteral("mobile");
    value.label = QStringLiteral("Mobile");
    addPhone(value);
  }

  void addRow(QVBoxLayout* layout, QList<ValueRow>& rows, LabelField field, const LabeledValue& value, const QString& placeholder,
              const std::function<void()>& onChange = {}) {
    auto* row = new QWidget;
    auto* box = new QHBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(6);
    auto* preset = new QComboBox;
    prepareLabelBox(preset, field);
    showLabelBox(preset, field, value.type, value.label);
    auto* edit = new QLineEdit(value.value);
    edit->setPlaceholderText(placeholder);
    auto* remove = kindButton(QStringLiteral("Remove"), QStringLiteral("outline"));
    box->addWidget(preset);
    box->addWidget(edit, 1);
    box->addWidget(remove);
    layout->addWidget(row);
    ValueRow stored;
    stored.field = field;
    stored.preset = preset;
    stored.editor = edit;
    stored.originalDisplay = preset->currentText();
    stored.originalType = value.type.isEmpty() ? resolveLabel(field, value.label).type : value.type;
    rows.append(stored);
    if (onChange) onChange();
    connect(remove, &QPushButton::clicked, row, [edit, &rows, row, onChange] {
      for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).editor == edit) {
          rows.removeAt(i);
          break;
        }
      }
      if (onChange) onChange();
      row->deleteLater();
    });
  }

  QWidget* indexed(const QList<ValueRow>& rows) const {
    if (focus.index >= 0 && focus.index < rows.size()) return rows.at(focus.index).editor;
    return nullptr;
  }

  QWidget* focusTarget() const {
    QWidget* target = given;
    switch (focus.field) {
      case EditorField::Given:
        target = given;
        break;
      case EditorField::Family:
        target = family;
        break;
      case EditorField::Organization:
        target = organization;
        break;
      case EditorField::Title:
        target = titleEdit;
        break;
      case EditorField::Email:
        target = indexed(emails);
        break;
      case EditorField::Phone:
        target = indexed(phones);
        break;
      case EditorField::Notes:
        target = notes;
        break;
      case EditorField::Prefix:
        target = prefix;
        break;
      case EditorField::Middle:
        target = middle;
        break;
      case EditorField::Suffix:
        target = suffix;
        break;
      case EditorField::Nickname:
        target = nickname;
        break;
      case EditorField::FileAs:
      case EditorField::PhoneticGiven:
      case EditorField::PhoneticMiddle:
      case EditorField::PhoneticFamily:
        break;
      case EditorField::Department:
        target = department;
        break;
      case EditorField::Occupation:
        target = occupation;
        break;
      case EditorField::Birthday:
        target = birthday;
        break;
      case EditorField::Address:
        if (focus.index >= 0 && focus.index < addresses.size()) target = addresses.at(focus.index).street;
        break;
      case EditorField::Url:
        target = indexed(urls);
        break;
      case EditorField::Relation:
        target = indexed(relations);
        break;
      case EditorField::Event:
        if (focus.index >= 0 && focus.index < events.size()) target = events.at(focus.index).date;
        break;
      case EditorField::Im:
      case EditorField::Sip:
        break;
      case EditorField::Custom:
        target = indexed(customs);
        break;
      case EditorField::OtherOrganization:
        if (focus.index >= 0 && focus.index < orgs.size()) target = orgs.at(focus.index).name;
        break;
    }
    return target == nullptr ? given : target;
  }

  void revealFocus() {
    if (fieldIsMore(focus.field) && morePane != nullptr && !morePane->isVisible()) setMoreOpen(true);
    QWidget* target = focusTarget();
    if (target == nullptr) return;
    if (scroll != nullptr) scroll->ensureWidgetVisible(target, 0, 24);
    target->setFocus(Qt::OtherFocusReason);
    if (auto* line = qobject_cast<QLineEdit*>(target)) line->selectAll();
    if (auto* note = qobject_cast<QPlainTextEdit*>(target)) {
      QTextCursor cursor = note->textCursor();
      cursor.movePosition(QTextCursor::End);
      note->setTextCursor(cursor);
    }
  }

  void setMoreOpen(bool open) {
    moreOpen = open;
    if (morePane != nullptr) morePane->setVisible(open);
    if (moreButton != nullptr) moreButton->setText(open ? QStringLiteral("Less") : QStringLiteral("More"));
  }

  PostalAddress addressFromRow(const AddressRow& row) const {
    PostalAddress address;
    const QString text = row.type == nullptr ? QString() : row.type->currentText().trimmed();
    if (!row.originalType.isEmpty() && text == row.originalDisplay) {
      address.type = row.originalType;
      address.label = row.originalDisplay;
    } else {
      const ResolvedLabel resolved = resolveLabel(LabelField::Address, text);
      address.type = resolved.type;
      address.label = resolved.display;
    }
    address.street = row.street->text();
    address.extended = row.extended->text();
    address.poBox = row.poBox;
    address.city = row.city->text();
    address.region = row.region->text();
    address.postalCode = row.postal->text();
    address.country = row.country->text();
    return address;
  }

  std::optional<Contact> validated() {
    Draft draft = Draft::fromContact(base);
    draft.prefix = prefix->text();
    draft.givenName = given->text();
    draft.middleName = middle->text();
    draft.familyName = family->text();
    draft.suffix = suffix->text();
    draft.nickname = nickname->text();
    draft.organization = organization->text();
    draft.title = titleEdit->text();
    draft.department = department->text();
    draft.occupation = occupation->text();
    draft.birthday = dateFromEditor(birthday->text(), originalBirthday);
    draft.notes = notes->toPlainText();
    draft.emails.clear();
    draft.phones.clear();
    draft.urls.clear();
    draft.relations.clear();
    draft.customs.clear();
    draft.addresses.clear();
    draft.events.clear();
    draft.otherOrganizations.clear();
    for (const auto& row : emails) draft.emails.append(labelFrom(row));
    for (const auto& row : phones) draft.phones.append(labelFrom(row));
    for (const auto& row : urls) draft.urls.append(labelFrom(row));
    for (const auto& row : relations) draft.relations.append(labelFrom(row));
    for (const auto& row : customs) {
      LabeledValue custom;
      custom.label = row.preset->currentText().trimmed();
      custom.value = row.editor->text().trimmed();
      draft.customs.append(custom);
    }
    for (const auto& row : addresses) draft.addresses.append(addressFromRow(row));
    for (const auto& row : events) {
      DatedEvent event;
      event.type = row.type->text().trimmed().isEmpty() ? QStringLiteral("other") : row.type->text().trimmed();
      event.label = event.type;
      event.date = dateFromEditor(row.date->text(), row.original);
      draft.events.append(event);
    }
    for (const auto& row : orgs) {
      Organization org;
      org.name = row.name->text();
      org.title = row.title->text();
      org.department = row.department->text();
      draft.otherOrganizations.append(org);
    }
    const QString problem = draft.validate();
    if (!problem.isEmpty()) {
      error->setText(problem);
      return std::nullopt;
    }
    error->setText({});
    return draft.toContact(base);
  }

  QLineEdit* pairedLine(const QString& text, const QString& placeholder) {
    auto* field = new QLineEdit(text);
    field->setPlaceholderText(placeholder);
    return field;
  }

  void removeWidgetRow(QWidget* row, const std::function<void()>& erase) {
    erase();
    row->deleteLater();
  }

  void showRowsCaption(QLabel* label, int count) {
    if (label != nullptr) label->setVisible(count > 0);
  }

  void addOrganization(Organization org) {
    auto* row = new QWidget;
    auto* box = new QVBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(6);
    auto* name = pairedLine(org.name, QStringLiteral("Company"));
    auto* title = pairedLine(org.title, QStringLiteral("Job title"));
    auto* departmentEdit = pairedLine(org.department, QStringLiteral("Department"));
    auto* remove = kindButton(QStringLiteral("Remove company"), QStringLiteral("outline"));
    box->addWidget(name);
    box->addWidget(title);
    box->addWidget(departmentEdit);
    box->addWidget(remove, 0, Qt::AlignLeft);
    orgLayout->addWidget(row);
    orgs.append(OrgRow{name, title, departmentEdit});
    showRowsCaption(orgCaption, orgs.size());
    QObject::connect(remove, &QPushButton::clicked, row, [this, name, row] {
      removeWidgetRow(row, [this, name] {
        for (int i = 0; i < orgs.size(); ++i) {
          if (orgs.at(i).name == name) {
            orgs.removeAt(i);
            break;
          }
        }
        showRowsCaption(orgCaption, orgs.size());
      });
    });
  }

  void addAddress(PostalAddress address) {
    auto* row = new QWidget;
    auto* box = new QVBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(6);
    auto* type = new QComboBox;
    prepareLabelBox(type, LabelField::Address);
    showLabelBox(type, LabelField::Address, address.type, address.label);
    auto* street = pairedLine(address.street, QStringLiteral("Street"));
    auto* extended = pairedLine(address.extended, QStringLiteral("Line 2"));
    auto* city = pairedLine(address.city, QStringLiteral("City"));
    auto* region = pairedLine(address.region, QStringLiteral("State"));
    auto* postal = pairedLine(address.postalCode, QStringLiteral("Postal code"));
    auto* country = pairedLine(address.country, QStringLiteral("Country"));
    auto* remove = kindButton(QStringLiteral("Remove address"), QStringLiteral("outline"));
    box->addWidget(type);
    box->addWidget(street);
    box->addWidget(extended);
    box->addWidget(city);
    box->addWidget(region);
    box->addWidget(postal);
    box->addWidget(country);
    box->addWidget(remove, 0, Qt::AlignLeft);
    addressLayout->addWidget(row);
    const QString keptType = address.type.isEmpty() ? resolveLabel(LabelField::Address, address.label).type : address.type;
    addresses.append(AddressRow{type, street, extended, address.poBox, city, region, postal, country, keptType, type->currentText()});
    QObject::connect(remove, &QPushButton::clicked, row, [this, street, row] {
      removeWidgetRow(row, [this, street] {
        for (int i = 0; i < addresses.size(); ++i) {
          if (addresses.at(i).street == street) {
            addresses.removeAt(i);
            return;
          }
        }
      });
    });
  }

  void addUrl(LabeledValue value) {
    addRow(urlLayout, urls, LabelField::Other, value, QStringLiteral("https://"),
           [this] { showRowsCaption(urlCaption, urls.size()); });
  }

  void addRelation(LabeledValue value) {
    if (value.label.isEmpty() && !value.type.isEmpty()) value.label = value.type;
    addRow(relationLayout, relations, LabelField::Other, value, QStringLiteral("Name"),
           [this] { showRowsCaption(relationCaption, relations.size()); });
  }

  void addCustom(LabeledValue value) {
    if (value.type.isEmpty()) value.type = value.label;
    addRow(customLayout, customs, LabelField::Other, value, QStringLiteral("Value"),
           [this] { showRowsCaption(customCaption, customs.size()); });
  }

  void addEvent(DatedEvent event) {
    auto* row = new QWidget;
    auto* box = new QHBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(6);
    auto* type = pairedLine(event.label.isEmpty() ? event.type : event.label, QStringLiteral("Anniversary"));
    auto* date = pairedLine(event.date.display(), QStringLiteral("YYYY-MM-DD"));
    auto* remove = kindButton(QStringLiteral("Remove"), QStringLiteral("outline"));
    box->addWidget(type);
    box->addWidget(date, 1);
    box->addWidget(remove);
    eventLayout->addWidget(row);
    events.append(EventRow{type, date, event.date});
    showRowsCaption(eventCaption, events.size());
    QObject::connect(remove, &QPushButton::clicked, row, [this, date, row] {
      removeWidgetRow(row, [this, date] {
        for (int i = 0; i < events.size(); ++i) {
          if (events.at(i).date == date) {
            events.removeAt(i);
            break;
          }
        }
        showRowsCaption(eventCaption, events.size());
      });
    });
  }

  void build(QWidget* host) {
    hadPhoto = !base.photoUrl.isEmpty();
    auto* root = new QVBoxLayout(host);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(14);
    auto* title = new QLabel(base.resourceName.isEmpty() ? QStringLiteral("New contact") : QStringLiteral("Edit contact"));
    title->setObjectName(QStringLiteral("title"));
    root->addWidget(title);

    auto* form = new QWidget;
    auto* formLayout = new QVBoxLayout(form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(14);

    photoLabel = new QLabel(QStringLiteral("No photo"));
    photoLabel->setObjectName(QStringLiteral("photo"));
    photoLabel->setAlignment(Qt::AlignCenter);
    photoLabel->setMinimumHeight(160);
    photoLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    formLayout->addWidget(photoLabel, 0, Qt::AlignHCenter);
    auto* photoButtons = new QHBoxLayout;
    photoButtons->setAlignment(Qt::AlignHCenter);
    photoButton = kindButton(QStringLiteral("Add photo"), QStringLiteral("outline"));
    removePhotoButton = kindButton(QStringLiteral("Remove photo"), QStringLiteral("outline"));
    photoButtons->addWidget(photoButton);
    photoButtons->addWidget(removePhotoButton);
    formLayout->addLayout(photoButtons);
    QObject::connect(photoButton, &QPushButton::clicked, host, [this, host] { choosePhoto(host); });
    QObject::connect(removePhotoButton, &QPushButton::clicked, host, [this] { clearPhoto(); });
    refreshPhotoButtons();

    auto edit = [](const QString& text, const QString& name) {
      auto* field = new QLineEdit(text);
      if (!name.isEmpty()) field->setObjectName(name);
      return field;
    };
    prefix = edit(base.prefix, QStringLiteral("prefix"));
    given = edit(base.givenName, QStringLiteral("given"));
    middle = edit(base.middleName, QStringLiteral("middle"));
    family = edit(base.familyName, QStringLiteral("family"));
    suffix = edit(base.suffix, QStringLiteral("suffix"));
    nickname = edit(base.nickname, {});
    organization = edit(base.organization, QStringLiteral("organization"));
    titleEdit = edit(base.title, QStringLiteral("title-field"));
    department = edit(base.department, {});
    occupation = edit(base.occupation, {});
    originalBirthday = base.birthday;
    birthday = edit(base.birthday.display(), QStringLiteral("birthday"));
    formLayout->addWidget(caption(QStringLiteral("First name")));
    formLayout->addWidget(given);
    formLayout->addWidget(caption(QStringLiteral("Last name")));
    formLayout->addWidget(family);
    formLayout->addWidget(caption(QStringLiteral("Company")));
    formLayout->addWidget(organization);

    formLayout->addWidget(caption(QStringLiteral("Email")));
    emailLayout = new QVBoxLayout;
    emailLayout->setSpacing(6);
    formLayout->addLayout(emailLayout);
    for (const auto& email : base.emails) addEmail(email);
    if (base.emails.isEmpty()) addSuggestedEmail();
    auto* addEmailButton = kindButton(QStringLiteral("Add email"), QStringLiteral("outline"));
    formLayout->addWidget(addEmailButton, 0, Qt::AlignLeft);
    QObject::connect(addEmailButton, &QPushButton::clicked, host, [this] { addSuggestedEmail(); });

    formLayout->addWidget(caption(QStringLiteral("Phone")));
    phoneLayout = new QVBoxLayout;
    phoneLayout->setSpacing(6);
    formLayout->addLayout(phoneLayout);
    for (const auto& phone : base.phones) addPhone(phone);
    auto* addPhoneButton = kindButton(QStringLiteral("Add phone"), QStringLiteral("outline"));
    formLayout->addWidget(addPhoneButton, 0, Qt::AlignLeft);
    QObject::connect(addPhoneButton, &QPushButton::clicked, host, [this] { addSuggestedPhone(); });

    formLayout->addWidget(caption(QStringLiteral("Address")));
    addressLayout = new QVBoxLayout;
    addressLayout->setSpacing(10);
    formLayout->addLayout(addressLayout);
    for (const auto& address : base.addresses) addAddress(address);
    auto* addAddressButton = kindButton(QStringLiteral("Add address"), QStringLiteral("outline"));
    formLayout->addWidget(addAddressButton, 0, Qt::AlignLeft);
    QObject::connect(addAddressButton, &QPushButton::clicked, host, [this] {
      PostalAddress address;
      address.type = QStringLiteral("home");
      address.label = QStringLiteral("Home");
      addAddress(address);
    });

    formLayout->addWidget(caption(QStringLiteral("Notes")));
    notes = new QPlainTextEdit(base.notes);
    notes->setObjectName(QStringLiteral("notes"));
    notes->setMinimumHeight(96);
    notes->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    formLayout->addWidget(notes, 1);

    moreOpen = base.hasLessCommonFields() || fieldIsMore(focus.field);
    moreButton = kindButton(moreOpen ? QStringLiteral("Less") : QStringLiteral("More"), QStringLiteral("outline"));
    formLayout->addWidget(moreButton, 0, Qt::AlignLeft);
    morePane = new QWidget;
    auto* moreLayout = new QVBoxLayout(morePane);
    moreLayout->setContentsMargins(0, 0, 0, 0);
    moreLayout->setSpacing(14);
    auto addPair = [&](QVBoxLayout* parent, const QString& title) {
      parent->addWidget(caption(title));
    };
    addPair(moreLayout, QStringLiteral("Prefix"));
    moreLayout->addWidget(prefix);
    addPair(moreLayout, QStringLiteral("Middle name"));
    moreLayout->addWidget(middle);
    addPair(moreLayout, QStringLiteral("Suffix"));
    moreLayout->addWidget(suffix);
    addPair(moreLayout, QStringLiteral("Nickname"));
    moreLayout->addWidget(nickname);
    addPair(moreLayout, QStringLiteral("Job title"));
    moreLayout->addWidget(titleEdit);
    addPair(moreLayout, QStringLiteral("Department"));
    moreLayout->addWidget(department);
    addPair(moreLayout, QStringLiteral("Occupation"));
    moreLayout->addWidget(occupation);
    addPair(moreLayout, QStringLiteral("Birthday"));
    birthday->setPlaceholderText(QStringLiteral("YYYY-MM-DD"));
    moreLayout->addWidget(birthday);

    orgCaption = caption(QStringLiteral("Another company"));
    orgCaption->setVisible(false);
    moreLayout->addWidget(orgCaption);
    orgLayout = new QVBoxLayout;
    orgLayout->setSpacing(6);
    moreLayout->addLayout(orgLayout);
    for (const auto& org : base.otherOrganizations) addOrganization(org);
    auto* addOrgButton = kindButton(QStringLiteral("Add company"), QStringLiteral("outline"));
    moreLayout->addWidget(addOrgButton, 0, Qt::AlignLeft);
    QObject::connect(addOrgButton, &QPushButton::clicked, host, [this] { addOrganization({}); });

    urlCaption = caption(QStringLiteral("Website"));
    urlCaption->setVisible(false);
    moreLayout->addWidget(urlCaption);
    urlLayout = new QVBoxLayout;
    urlLayout->setSpacing(6);
    moreLayout->addLayout(urlLayout);
    for (const auto& url : base.urls) addUrl(url);
    auto* addUrlButton = kindButton(QStringLiteral("Add website"), QStringLiteral("outline"));
    moreLayout->addWidget(addUrlButton, 0, Qt::AlignLeft);
    QObject::connect(addUrlButton, &QPushButton::clicked, host, [this] { addUrl({}); });

    relationCaption = caption(QStringLiteral("Related person"));
    relationCaption->setVisible(false);
    moreLayout->addWidget(relationCaption);
    relationLayout = new QVBoxLayout;
    relationLayout->setSpacing(6);
    moreLayout->addLayout(relationLayout);
    for (const auto& relation : base.relations) addRelation(relation);
    auto* addRelationButton = kindButton(QStringLiteral("Add related person"), QStringLiteral("outline"));
    moreLayout->addWidget(addRelationButton, 0, Qt::AlignLeft);
    QObject::connect(addRelationButton, &QPushButton::clicked, host, [this] { addRelation({}); });

    eventCaption = caption(QStringLiteral("Significant date"));
    eventCaption->setVisible(false);
    moreLayout->addWidget(eventCaption);
    eventLayout = new QVBoxLayout;
    eventLayout->setSpacing(6);
    moreLayout->addLayout(eventLayout);
    for (const auto& event : base.events) addEvent(event);
    auto* addEventButton = kindButton(QStringLiteral("Add significant date"), QStringLiteral("outline"));
    moreLayout->addWidget(addEventButton, 0, Qt::AlignLeft);
    QObject::connect(addEventButton, &QPushButton::clicked, host, [this] { addEvent({}); });

    customCaption = caption(QStringLiteral("Custom field"));
    customCaption->setVisible(false);
    moreLayout->addWidget(customCaption);
    customLayout = new QVBoxLayout;
    customLayout->setSpacing(6);
    moreLayout->addLayout(customLayout);
    for (const auto& custom : base.customs) addCustom(custom);
    auto* addCustomButton = kindButton(QStringLiteral("Add custom field"), QStringLiteral("outline"));
    moreLayout->addWidget(addCustomButton, 0, Qt::AlignLeft);
    QObject::connect(addCustomButton, &QPushButton::clicked, host, [this] { addCustom({}); });

    morePane->setVisible(moreOpen);
    formLayout->addWidget(morePane);
    QObject::connect(moreButton, &QPushButton::clicked, host, [this] { setMoreOpen(!moreOpen); });

    scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(form);
    root->addWidget(scroll, 1);

    error = new QLabel;
    error->setObjectName(QStringLiteral("danger"));
    error->setWordWrap(true);
    root->addWidget(error);

    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    auto* cancel = kindButton(QStringLiteral("Cancel"), QStringLiteral("outline"));
    auto* save = kindButton(QStringLiteral("Save"), QStringLiteral("primary"));
    footer->addWidget(cancel);
    footer->addWidget(save);
    root->addLayout(footer);
    QObject::connect(cancel, &QPushButton::clicked, host, [this] {
      if (onCancel) onCancel();
    });
    QObject::connect(save, &QPushButton::clicked, host, [this] {
      if (onSave) onSave();
    });
  }
};

ContactEditor::ContactEditor(const Contact& base, EditorFocus focus, QWidget* parent) : QWidget(parent), form_(new Form) {
  setObjectName(QStringLiteral("editor"));
  form_->base = base;
  form_->focus = focus;
  form_->build(this);
}

ContactEditor::~ContactEditor() { delete form_; }

void ContactEditor::setOnSave(std::function<void()> save) { form_->onSave = std::move(save); }

void ContactEditor::setOnCancel(std::function<void()> cancel) { form_->onCancel = std::move(cancel); }

std::optional<Contact> ContactEditor::validatedContact() { return form_->validated(); }

QByteArray ContactEditor::photoBytes() const { return form_->photoBytes; }

bool ContactEditor::removePhoto() const { return form_->removePhotoFlag && form_->photoBytes.isEmpty(); }

void ContactEditor::setPreview(const QPixmap& pixmap) {
  if (form_->removePhotoFlag || !form_->photoBytes.isEmpty()) return;
  form_->showPixmap(pixmap);
}

void ContactEditor::focusField() { form_->revealFocus(); }

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

}  // namespace contacts
