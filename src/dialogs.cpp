#include "dialogs.h"

#include "desktop.h"
#include "photos.h"
#include "shortcuts.h"
#include "theme.h"

#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <functional>

namespace contacts {
namespace {

constexpr char kSetupGuide[] = "https://github.com/duff/peeps#google";

const ShellMetrics& metrics() { return activeMetrics(); }

QPushButton* kindButton(const QString& text, const QString& kind) {
  auto* button = new QPushButton(text);
  button->setProperty("kind", kind);
  button->setAccessibleName(text);
  button->setCursor(Qt::PointingHandCursor);
  button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  // Enter presses a focused button, in the editor as well as in a dialog.
  button->setAutoDefault(true);
  return button;
}

// "+ Add email" reads as an action without a box around it.
QPushButton* addButton(const QString& what) {
  auto* button = kindButton(QStringLiteral("+ Add ") + what, QStringLiteral("link"));
  button->setAccessibleName(QStringLiteral("Add ") + what);
  return button;
}

QPushButton* removeButton(const QString& what) {
  auto* button = kindButton(QStringLiteral("×"), QStringLiteral("icon"));
  button->setAccessibleName(QStringLiteral("Remove ") + what);
  button->setToolTip(QStringLiteral("Remove ") + what);
  return button;
}

QLabel* labelNamed(const QString& text, const QString& name) {
  auto* label = new QLabel(text);
  label->setObjectName(name);
  return label;
}

QWidget* sectionHeading(const QString& text) {
  auto* row = new QWidget;
  auto* box = new QHBoxLayout(row);
  box->setContentsMargins(0, 0, 0, 0);
  box->setSpacing(metrics().space(10));
  auto* rule = new QFrame;
  rule->setObjectName(QStringLiteral("hairline"));
  box->addWidget(labelNamed(text, QStringLiteral("section")));
  box->addWidget(rule, 1, Qt::AlignVCenter);
  return row;
}

// A titled group. Sections sit apart and their fields sit close, so the eye
// reads the form as a few groups instead of one long column.
QVBoxLayout* addSection(QVBoxLayout* parent, const QString& title) {
  auto* body = new QVBoxLayout;
  body->setContentsMargins(0, 0, 0, 0);
  body->setSpacing(metrics().space(8));
  body->addWidget(sectionHeading(title));
  parent->addLayout(body);
  return body;
}

QVBoxLayout* rowsLayout(QVBoxLayout* section, int spacing) {
  auto* rows = new QVBoxLayout;
  rows->setContentsMargins(0, 0, 0, 0);
  rows->setSpacing(metrics().space(spacing));
  section->addLayout(rows);
  return rows;
}

// A caption over its field, for the two-column grids.
QWidget* labeled(const QString& text, QWidget* field) {
  auto* cell = new QWidget;
  auto* box = new QVBoxLayout(cell);
  box->setContentsMargins(0, 0, 0, 0);
  box->setSpacing(metrics().space(4));
  box->addWidget(labelNamed(text, QStringLiteral("caption")));
  box->addWidget(field);
  field->setAccessibleName(text);
  return cell;
}

QGridLayout* pairGrid(QVBoxLayout* section) {
  auto* grid = new QGridLayout;
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(metrics().space(10));
  grid->setVerticalSpacing(metrics().space(10));
  grid->setColumnStretch(0, 1);
  grid->setColumnStretch(1, 1);
  section->addLayout(grid);
  return grid;
}

QLineEdit* lineEdit(const QString& text, const QString& placeholder = {}, const QString& name = {}) {
  auto* field = new QLineEdit(text);
  field->setPlaceholderText(placeholder);
  if (!name.isEmpty()) field->setObjectName(name);
  return field;
}

DateValue dateFromEditor(const QString& text, const DateValue& original) {
  static const QRegularExpression iso(QStringLiteral("^(\\d{4})-(\\d{1,2})-(\\d{1,2})$"));
  static const QRegularExpression monthDay(QStringLiteral("^(\\d{1,2})-(\\d{1,2})$"));
  const QString trimmed = text.trimmed();
  if (trimmed == original.isoText()) return original;
  DateValue date;
  if (const auto full = iso.match(trimmed); full.hasMatch()) {
    date.year = full.captured(1).toInt();
    date.month = full.captured(2).toInt();
    date.day = full.captured(3).toInt();
    return date;
  }
  if (const auto partial = monthDay.match(trimmed); partial.hasMatch()) {
    date.month = partial.captured(1).toInt();
    date.day = partial.captured(2).toInt();
    return date;
  }
  date.text = trimmed;
  return date;
}

// The wheel scrolls the form. A label box changes only once it has focus.
// The stylesheet leaves no arrow, so the box draws its own in the text color,
// which says it is a choice and follows the theme.
class LabelBox : public QComboBox {
 public:
  using QComboBox::QComboBox;

 protected:
  void wheelEvent(QWheelEvent* event) override {
    if (hasFocus()) QComboBox::wheelEvent(event);
    else event->ignore();
  }

  void paintEvent(QPaintEvent* event) override {
    QComboBox::paintEvent(event);
    QStyleOptionComboBox option;
    initStyleOption(&option);
    const QRect area = style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxArrow, this);
    const qreal half = metrics().space(4);
    const QPointF center = QRectF(area).center() - QPointF(metrics().space(4), 0);
    QColor color = palette().color(QPalette::Text);
    if (!hasFocus()) color.setAlphaF(0.6);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, std::max(1.0, metrics().space(1) * 1.5));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    const QPointF chevron[] = {center + QPointF(-half, -half / 2), center + QPointF(0, half / 2), center + QPointF(half, -half / 2)};
    painter.drawPolyline(chevron, 3);
  }
};

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

QComboBox* labelBox(const QStringList& presets, const QString& accessibleName) {
  auto* box = new LabelBox;
  box->addItems(presets);
  box->setEditable(true);
  box->setInsertPolicy(QComboBox::NoInsert);
  box->setFixedWidth(metrics().space(136));
  box->setFocusPolicy(Qt::StrongFocus);
  box->setAccessibleName(accessibleName);
  if (QCompleter* completer = box->completer()) {
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchStartsWith);
    completer->setCompletionMode(QCompleter::PopupCompletion);
  }
  if (box->lineEdit() != nullptr) {
    box->lineEdit()->setPlaceholderText(QStringLiteral("Label"));
    box->lineEdit()->installEventFilter(new OpenListOnClick(box));
  }
  return box;
}

QString shownLabel(LabelField field, const QString& type, const QString& label) {
  if (type.trimmed().isEmpty() && label.trimmed().isEmpty()) return {};
  const QString shown = labelDisplay(field, LabeledValue{QString(), label, type});
  return shown.isEmpty() ? type : shown;
}

KeyEvent eventFrom(QKeyEvent* event, Surface surface) {
  KeyEvent mapped;
  mapped.key = event->key() == Qt::Key_Escape     ? Key::Escape
               : event->key() == Qt::Key_Question ? Key::Question
               : event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ? Key::Return
                                                                                 : Key::Other;
  mapped.ctrl = event->modifiers() & Qt::ControlModifier;
  mapped.alt = event->modifiers() & Qt::AltModifier;
  mapped.textInput = true;
  mapped.surface = surface;
  return mapped;
}

// Frameless, square, and padded like the shell's own panels. Hyprland floats
// it over the window because it has a parent.
class Panel : public QDialog {
 public:
  Panel(QWidget* parent, Surface surface) : QDialog(parent), surface_(surface) {
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setMinimumWidth(metrics().space(440));
    root_ = new QVBoxLayout(this);
    const int pad = metrics().space(18);
    root_->setContentsMargins(pad, pad, pad, pad);
    root_->setSpacing(metrics().space(14));
  }

 protected:
  // A wrapped label needs the height for the width the panel ends up with.
  // Wayland sizes the window from the size hint alone, which cuts off a
  // second line, so the panel fixes its own size before it is mapped.
  void showEvent(QShowEvent* event) override {
    const int width = std::max(minimumWidth(), sizeHint().width());
    setFixedSize(width, layout()->totalHeightForWidth(width));
    QDialog::showEvent(event);
  }

  void keyPressEvent(QKeyEvent* event) override {
    if (routeKey(eventFrom(event, surface_)) == Command::Cancel) {
      reject();
      return;
    }
    QDialog::keyPressEvent(event);
  }

  QVBoxLayout* root_ = nullptr;

 private:
  Surface surface_;
};

class ConfirmBox : public Panel {
 public:
  ConfirmBox(QWidget* parent, const QString& titleText, const QString& body, const QString& action, const QString& cancelText)
      : Panel(parent, Surface::ConfirmDelete) {
    auto* title = labelNamed(titleText, QStringLiteral("title"));
    title->setWordWrap(true);
    auto* description = labelNamed(body, QStringLiteral("secondary"));
    description->setWordWrap(true);
    root_->addWidget(title);
    root_->addWidget(description);
    auto* footer = new QHBoxLayout;
    footer->addStretch(1);
    auto* cancel = kindButton(cancelText, QStringLiteral("outline"));
    auto* confirm = kindButton(action, QStringLiteral("danger"));
    // Enter keeps what is there. Changing anything takes Tab first.
    cancel->setDefault(true);
    footer->addWidget(cancel);
    footer->addWidget(confirm);
    root_->addLayout(footer);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, this, &QDialog::accept);
    cancel->setFocus();
  }
};

class HelpBox : public Panel {
 public:
  explicit HelpBox(QWidget* parent) : Panel(parent, Surface::Help) {
    root_->setSpacing(metrics().space(8));
    root_->addWidget(labelNamed(QStringLiteral("Keybindings"), QStringLiteral("title")));
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(metrics().space(18));
    grid->setVerticalSpacing(metrics().space(6));
    grid->setColumnStretch(1, 1);
    QString group;
    int row = 0;
    for (const auto& binding : bindingHelp()) {
      if (binding.group != group) {
        group = binding.group;
        if (row > 0) grid->setRowMinimumHeight(row++, metrics().space(8));
        grid->addWidget(sectionHeading(group), row++, 0, 1, 2);
      }
      // One line per binding, so the list reads as a table.
      grid->addWidget(labelNamed(binding.keys, QStringLiteral("accent")), row, 0);
      grid->addWidget(labelNamed(binding.action, QStringLiteral("secondary")), row++, 1);
    }
    root_->addLayout(grid);
    auto* close = kindButton(QStringLiteral("Close"), QStringLiteral("outline"));
    close->setDefault(true);
    root_->addSpacing(metrics().space(8));
    root_->addWidget(close, 0, Qt::AlignRight);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
  }
};

class AccountBox : public Panel {
 public:
  AccountBox(QWidget* parent, bool hasClient, bool connected) : Panel(parent, Surface::Account) {
    root_->addWidget(labelNamed(QStringLiteral("Google account"), QStringLiteral("title")));
    const QString state = connected   ? QStringLiteral("Connected. Peeps syncs with this Google account.")
                          : hasClient ? QStringLiteral("Not connected.")
                                      : QStringLiteral("This build of Peeps has no Google client, so it cannot connect. "
                                                       "The setup guide shows how to build it with one.");
    auto* status = labelNamed(state, QStringLiteral("secondary"));
    status->setWordWrap(true);
    root_->addWidget(status);
    if (!hasClient) {
      auto* guide = kindButton(QStringLiteral("Open the setup guide"), QStringLiteral("link"));
      root_->addWidget(guide, 0, Qt::AlignLeft);
      connect(guide, &QPushButton::clicked, this, [] {
        runDetached(browserCommand(QString::fromLatin1(kSetupGuide), haveProgram(QStringLiteral("omarchy"))));
      });
    }

    auto* footer = new QHBoxLayout;
    if (connected) {
      auto* disconnect = kindButton(QStringLiteral("Disconnect"), QStringLiteral("danger"));
      footer->addWidget(disconnect);
      connect(disconnect, &QPushButton::clicked, this, [this] { finish(AccountAction::Disconnect); });
    }
    footer->addStretch(1);
    auto* cancel = kindButton(hasClient ? QStringLiteral("Cancel") : QStringLiteral("Close"), QStringLiteral("outline"));
    footer->addWidget(cancel);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    QPushButton* focus = cancel;
    if (hasClient) {
      auto* connectButton = kindButton(connected ? QStringLiteral("Reconnect") : QStringLiteral("Connect"), QStringLiteral("primary"));
      footer->addWidget(connectButton);
      connect(connectButton, &QPushButton::clicked, this, [this] { finish(AccountAction::Connect); });
      focus = connectButton;
    }
    focus->setDefault(true);
    root_->addLayout(footer);
    focus->setFocus();
  }

  AccountAction action() const { return action_; }

 private:
  void finish(AccountAction action) {
    action_ = action;
    accept();
  }

  AccountAction action_ = AccountAction::Connect;
};

struct ValueRow {
  QWidget* row = nullptr;
  LabelField field = LabelField::Email;
  QComboBox* preset = nullptr;
  QLineEdit* editor = nullptr;
  LabeledValue original;
  QString shown;
};

struct AddressRow {
  QWidget* row = nullptr;
  QComboBox* type = nullptr;
  QLineEdit* street = nullptr;
  QLineEdit* extended = nullptr;
  QLineEdit* city = nullptr;
  QLineEdit* region = nullptr;
  QLineEdit* postal = nullptr;
  QLineEdit* country = nullptr;
  PostalAddress original;
  QString shown;
  // Google sent only formatted text, so it sits in the street field.
  bool streetFromFormatted = false;
};

struct OrgRow {
  QWidget* row = nullptr;
  QLineEdit* name = nullptr;
  QLineEdit* title = nullptr;
  QLineEdit* department = nullptr;
};

struct EventRow {
  QWidget* row = nullptr;
  QLineEdit* type = nullptr;
  QLineEdit* date = nullptr;
  DatedEvent original;
  QString shown;
};

bool fieldIsMore(EditorField field) {
  switch (field) {
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

template <typename Row>
void dropRow(QList<Row>& rows, QWidget* row) {
  for (int i = 0; i < rows.size(); ++i) {
    if (rows.at(i).row == row) {
      rows.removeAt(i);
      break;
    }
  }
  row->deleteLater();
}

}  // namespace

struct ContactEditor::Form {
  ContactEditor* host = nullptr;
  Contact base;
  EditorFocus focus;
  bool hadPhoto = false;
  bool removePhotoFlag = false;
  QByteArray photoBytes;
  std::function<void()> onSave;
  std::function<void()> onCancel;

  QScrollArea* scroll = nullptr;
  QWidget* page = nullptr;
  QLabel* photoLabel = nullptr;
  QPushButton* photoButton = nullptr;
  QPushButton* removePhotoButton = nullptr;
  QPushButton* moreButton = nullptr;
  QLabel* moreSummary = nullptr;
  QWidget* morePane = nullptr;
  QPushButton* cancelButton = nullptr;
  QPushButton* saveButton = nullptr;
  QLabel* error = nullptr;
  bool moreOpen = false;

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
  QPlainTextEdit* notes = nullptr;
  // The text box turns CRLF into LF and a no-break space into a space. Until
  // someone edits the notes, the contact keeps Google's exact text.
  QString notesShown;

  QVBoxLayout* emailRows = nullptr;
  QVBoxLayout* phoneRows = nullptr;
  QVBoxLayout* addressRows = nullptr;
  QVBoxLayout* orgRows = nullptr;
  QVBoxLayout* eventRows = nullptr;
  QVBoxLayout* urlRows = nullptr;
  QVBoxLayout* relationRows = nullptr;
  QVBoxLayout* customRows = nullptr;
  QPushButton* addEmailButton = nullptr;
  QPushButton* addPhoneButton = nullptr;
  QPushButton* addAddressButton = nullptr;
  QPushButton* addOrgButton = nullptr;
  QPushButton* addEventButton = nullptr;
  QPushButton* addUrlButton = nullptr;
  QPushButton* addRelationButton = nullptr;
  QPushButton* addCustomButton = nullptr;

  QList<ValueRow> emails;
  QList<ValueRow> phones;
  QList<ValueRow> urls;
  QList<ValueRow> relations;
  QList<ValueRow> customs;
  QList<AddressRow> addresses;
  QList<EventRow> events;
  QList<OrgRow> orgs;

  void showError(const QString& text) {
    error->setText(text);
    error->setVisible(!text.isEmpty());
  }

  // ---- photo ----

  void showPixmap(const QPixmap& pixmap) {
    const int edge = photoLabel->width();
    if (pixmap.isNull()) {
      photoLabel->setPixmap({});
      photoLabel->setText(base.resourceName.isEmpty() ? QString() : base.initials());
      return;
    }
    // Fill the square and crop the overflow so every face sits the same way.
    const qreal ratio = std::max<qreal>(1.0, photoLabel->devicePixelRatioF());
    const int device = qRound(edge * ratio);
    QPixmap filled = pixmap.scaled(device, device, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    filled = filled.copy((filled.width() - device) / 2, (filled.height() - device) / 2, device, device);
    filled.setDevicePixelRatio(ratio);
    photoLabel->setText({});
    photoLabel->setPixmap(filled);
  }

  void refreshPhotoButtons() {
    const bool has = !photoBytes.isEmpty() || (hadPhoto && !removePhotoFlag);
    photoButton->setText(has ? QStringLiteral("Change photo") : QStringLiteral("Add photo"));
    photoButton->setAccessibleName(photoButton->text());
    removePhotoButton->setVisible(has);
  }

  void choosePhoto() {
    const std::optional<QByteArray> chosen = choosePhotoBytes(host);
    if (!chosen) return;
    if (chosen->isEmpty()) {
      showError(QStringLiteral("Could not read that image."));
      return;
    }
    QImage image;
    image.loadFromData(*chosen);
    photoBytes = *chosen;
    removePhotoFlag = false;
    showPixmap(QPixmap::fromImage(image));
    showError({});
    refreshPhotoButtons();
  }

  void clearPhoto() {
    photoBytes.clear();
    removePhotoFlag = true;
    showPixmap({});
    refreshPhotoButtons();
    photoButton->setFocus();
  }

  // ---- rows ----

  // Tab follows the page from top to bottom and left to right, including rows
  // added after the editor opened.
  void retab() {
    if (page == nullptr || page->layout() == nullptr) return;
    page->layout()->activate();
    QList<QWidget*> chain;
    for (QWidget* widget : page->findChildren<QWidget*>()) {
      if (!widget->isVisibleTo(page) || !(widget->focusPolicy() & Qt::TabFocus)) continue;
      if (qobject_cast<QComboBox*>(widget->parentWidget()) != nullptr) continue;
      chain.append(widget);
    }
    std::stable_sort(chain.begin(), chain.end(), [this](QWidget* a, QWidget* b) {
      const QPoint pa = a->mapTo(page, QPoint(0, 0));
      const QPoint pb = b->mapTo(page, QPoint(0, 0));
      return pa.y() != pb.y() ? pa.y() < pb.y() : pa.x() < pb.x();
    });
    chain.append(cancelButton);
    chain.append(saveButton);
    for (int i = 1; i < chain.size(); ++i) QWidget::setTabOrder(chain.at(i - 1), chain.at(i));
  }

  void scheduleRetab() {
    QTimer::singleShot(0, host, [this] { retab(); });
  }

  void reveal(QWidget* widget) {
    if (widget == nullptr) return;
    QTimer::singleShot(0, widget, [this, widget] {
      scroll->ensureWidgetVisible(widget, 0, metrics().space(24));
      widget->setFocus(Qt::OtherFocusReason);
    });
  }

  ValueRow& addValueRow(QVBoxLayout* layout, QList<ValueRow>& rows, LabelField field, const QStringList& presets,
                        const LabeledValue& value, const QString& placeholder, const QString& what, QPushButton* after) {
    auto* row = new QWidget;
    auto* box = new QHBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(metrics().space(6));
    ValueRow stored;
    stored.row = row;
    stored.field = field;
    stored.original = value;
    stored.preset = labelBox(presets, what + QStringLiteral(" label"));
    stored.shown = field == LabelField::Other && value.label.trimmed().isEmpty() && value.type.trimmed().isEmpty()
                       ? QString()
                       : shownLabel(field, value.type, value.label);
    stored.preset->setEditText(stored.shown);
    stored.editor = lineEdit(value.value, placeholder);
    stored.editor->setAccessibleName(what);
    auto* remove = removeButton(what);
    box->addWidget(stored.preset);
    box->addWidget(stored.editor, 1);
    box->addWidget(remove);
    layout->addWidget(row);
    rows.append(stored);
    QObject::connect(remove, &QPushButton::clicked, row, [this, &rows, row, after] {
      dropRow(rows, row);
      after->setFocus();
      scheduleRetab();
    });
    scheduleRetab();
    return rows.last();
  }

  LabeledValue valueFrom(const ValueRow& row) const {
    LabeledValue result;
    result.value = row.editor->text();
    const QString text = row.preset->currentText().trimmed();
    if (text == row.shown) {
      result.type = row.original.type;
      result.label = row.original.label;
      return result;
    }
    const ResolvedLabel resolved = resolveLabel(row.field, text);
    result.type = resolved.type;
    result.label = resolved.display;
    return result;
  }

  QLineEdit* addEmail(const LabeledValue& value) {
    return addValueRow(emailRows, emails, LabelField::Email, {QStringLiteral("Home"), QStringLiteral("Work")}, value,
                       QStringLiteral("name@example.com"), QStringLiteral("email"), addEmailButton)
        .editor;
  }

  QLineEdit* addPhone(const LabeledValue& value) {
    return addValueRow(phoneRows, phones, LabelField::Phone, {QStringLiteral("Mobile"), QStringLiteral("Work"), QStringLiteral("Home")},
                       value, QStringLiteral("+1 555 010 1000"), QStringLiteral("phone"), addPhoneButton)
        .editor;
  }

  QLineEdit* addUrl(const LabeledValue& value) {
    return addValueRow(urlRows, urls, LabelField::Other,
                       {QStringLiteral("Home"), QStringLiteral("Work"), QStringLiteral("Blog"), QStringLiteral("Profile")}, value,
                       QStringLiteral("https://"), QStringLiteral("website"), addUrlButton)
        .editor;
  }

  QLineEdit* addRelation(const LabeledValue& value) {
    return addValueRow(relationRows, relations, LabelField::Other,
                       {QStringLiteral("Spouse"), QStringLiteral("Partner"), QStringLiteral("Child"), QStringLiteral("Parent"),
                        QStringLiteral("Friend"), QStringLiteral("Assistant"), QStringLiteral("Manager")},
                       value, QStringLiteral("Name"), QStringLiteral("related person"), addRelationButton)
        .editor;
  }

  QLineEdit* addCustom(const LabeledValue& value) {
    return addValueRow(customRows, customs, LabelField::Other, {}, value, QStringLiteral("Value"), QStringLiteral("custom field"),
                       addCustomButton)
        .editor;
  }

  QLineEdit* addAddress(const PostalAddress& address) {
    auto* row = new QWidget;
    auto* box = new QVBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(metrics().space(6));
    AddressRow stored;
    stored.row = row;
    stored.original = address;
    stored.type = labelBox({QStringLiteral("Home"), QStringLiteral("Work")}, QStringLiteral("Address label"));
    stored.shown = shownLabel(LabelField::Address, address.type, address.label);
    stored.type->setEditText(stored.shown);
    const bool partsEmpty = (address.street + address.extended + address.poBox + address.city + address.region +
                             address.postalCode + address.country)
                                .trimmed()
                                .isEmpty();
    stored.streetFromFormatted = partsEmpty && !address.formatted.trimmed().isEmpty();
    const QString street = stored.streetFromFormatted ? address.formatted.simplified() : address.street;
    stored.street = lineEdit(street, QStringLiteral("Street"));
    stored.extended = lineEdit(address.extended, QStringLiteral("Apartment, suite, or floor"));
    stored.city = lineEdit(address.city, QStringLiteral("City"));
    stored.region = lineEdit(address.region, QStringLiteral("State or region"));
    stored.postal = lineEdit(address.postalCode, QStringLiteral("Postal code"));
    stored.country = lineEdit(address.country, QStringLiteral("Country"));
    auto* remove = removeButton(QStringLiteral("address"));
    auto* top = new QHBoxLayout;
    top->setSpacing(metrics().space(6));
    top->addWidget(stored.type);
    top->addStretch(1);
    top->addWidget(remove);
    auto* place = new QHBoxLayout;
    place->setSpacing(metrics().space(6));
    place->addWidget(stored.city, 3);
    place->addWidget(stored.region, 2);
    place->addWidget(stored.postal, 2);
    box->addLayout(top);
    box->addWidget(stored.street);
    box->addWidget(stored.extended);
    box->addLayout(place);
    box->addWidget(stored.country);
    addressRows->addWidget(row);
    addresses.append(stored);
    QObject::connect(remove, &QPushButton::clicked, row, [this, row] {
      dropRow(addresses, row);
      addAddressButton->setFocus();
      scheduleRetab();
    });
    scheduleRetab();
    return stored.street;
  }

  PostalAddress addressFrom(const AddressRow& row) const {
    PostalAddress address = row.original;
    const QString text = row.type->currentText().trimmed();
    if (text != row.shown) {
      const ResolvedLabel resolved = resolveLabel(LabelField::Address, text);
      address.type = resolved.type;
      address.label = resolved.display;
    }
    const bool untouchedFormatted = row.streetFromFormatted && row.street->text() == row.original.formatted.simplified() &&
                                    (row.extended->text() + row.city->text() + row.region->text() + row.postal->text() +
                                     row.country->text())
                                        .trimmed()
                                        .isEmpty();
    if (untouchedFormatted) return address;
    const PostalAddress& original = row.original;
    const QString street = row.streetFromFormatted ? original.formatted.simplified() : original.street;
    const bool edited = row.street->text() != street || row.extended->text() != original.extended ||
                        row.city->text() != original.city || row.region->text() != original.region ||
                        row.postal->text() != original.postalCode || row.country->text() != original.country;
    if (!edited) return address;
    // Google's formatted text describes the old parts. Without it, an address
    // whose parts are all cleared is empty and goes away.
    address.formatted.clear();
    address.street = row.street->text();
    address.extended = row.extended->text();
    address.city = row.city->text();
    address.region = row.region->text();
    address.postalCode = row.postal->text();
    address.country = row.country->text();
    return address;
  }

  QLineEdit* addOrganization(const Organization& org) {
    auto* row = new QWidget;
    auto* box = new QHBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(metrics().space(6));
    OrgRow stored{row, lineEdit(org.name, QStringLiteral("Company")), lineEdit(org.title, QStringLiteral("Job title")),
                  lineEdit(org.department, QStringLiteral("Department"))};
    stored.name->setAccessibleName(QStringLiteral("Other company"));
    auto* remove = removeButton(QStringLiteral("company"));
    box->addWidget(stored.name, 1);
    box->addWidget(stored.title, 1);
    box->addWidget(stored.department, 1);
    box->addWidget(remove);
    orgRows->addWidget(row);
    orgs.append(stored);
    QObject::connect(remove, &QPushButton::clicked, row, [this, row] {
      dropRow(orgs, row);
      addOrgButton->setFocus();
      scheduleRetab();
    });
    scheduleRetab();
    return stored.name;
  }

  QLineEdit* addEvent(const DatedEvent& event) {
    auto* row = new QWidget;
    auto* box = new QHBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(metrics().space(6));
    EventRow stored;
    stored.row = row;
    stored.original = event;
    stored.shown = event.label.isEmpty() ? event.type : event.label;
    stored.type = lineEdit(stored.shown, QStringLiteral("Anniversary"));
    stored.type->setFixedWidth(metrics().space(136));
    stored.type->setAccessibleName(QStringLiteral("Date label"));
    stored.date = lineEdit(event.date.isoText(), QStringLiteral("YYYY-MM-DD"));
    stored.date->setAccessibleName(QStringLiteral("Date"));
    auto* remove = removeButton(QStringLiteral("date"));
    box->addWidget(stored.type);
    box->addWidget(stored.date, 1);
    box->addWidget(remove);
    eventRows->addWidget(row);
    events.append(stored);
    QObject::connect(remove, &QPushButton::clicked, row, [this, row] {
      dropRow(events, row);
      addEventButton->setFocus();
      scheduleRetab();
    });
    scheduleRetab();
    return stored.date;
  }

  DatedEvent eventFrom(const EventRow& row) const {
    DatedEvent event = row.original;
    const QString text = row.type->text().trimmed();
    if (text != row.shown) {
      // Google keeps "anniversary" as a type and any other word as a label.
      event.type = text.isEmpty() ? QStringLiteral("other")
                   : text.compare(QLatin1String("anniversary"), Qt::CaseInsensitive) == 0 ? QStringLiteral("anniversary")
                                                                                          : text;
      event.label = text.isEmpty() ? QStringLiteral("other") : text;
    }
    event.date = dateFromEditor(row.date->text(), row.original.date);
    return event;
  }

  // ---- focus ----

  QWidget* focusTarget() const {
    const int i = focus.index;
    switch (focus.field) {
      case EditorField::Given:
        return given;
      case EditorField::Family:
        return family;
      case EditorField::Organization:
        return organization;
      case EditorField::Title:
        return titleEdit;
      case EditorField::Email:
        return i >= 0 && i < emails.size() ? emails.at(i).editor : nullptr;
      case EditorField::Phone:
        return i >= 0 && i < phones.size() ? phones.at(i).editor : nullptr;
      case EditorField::Notes:
        return notes;
      case EditorField::Prefix:
        return prefix;
      case EditorField::Middle:
        return middle;
      case EditorField::Suffix:
        return suffix;
      case EditorField::Nickname:
        return nickname;
      case EditorField::Department:
        return department;
      case EditorField::Occupation:
        return occupation;
      case EditorField::Birthday:
        return birthday;
      case EditorField::Address:
        return i >= 0 && i < addresses.size() ? addresses.at(i).street : nullptr;
      case EditorField::Url:
        return i >= 0 && i < urls.size() ? urls.at(i).editor : nullptr;
      case EditorField::Relation:
        return i >= 0 && i < relations.size() ? relations.at(i).editor : nullptr;
      case EditorField::Event:
        return i >= 0 && i < events.size() ? events.at(i).date : nullptr;
      case EditorField::Custom:
        return i >= 0 && i < customs.size() ? customs.at(i).editor : nullptr;
      case EditorField::OtherOrganization:
        return i >= 0 && i < orgs.size() ? orgs.at(i).name : nullptr;
      case EditorField::FileAs:
      case EditorField::PhoneticGiven:
      case EditorField::PhoneticMiddle:
      case EditorField::PhoneticFamily:
      case EditorField::Im:
      case EditorField::Sip:
        return nullptr;
    }
    return nullptr;
  }

  void revealFocus() {
    if (fieldIsMore(focus.field) && !moreOpen) setMoreOpen(true);
    QWidget* target = focusTarget();
    if (target == nullptr) target = given;
    scroll->ensureWidgetVisible(target, 0, metrics().space(24));
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
    morePane->setVisible(open);
    moreSummary->setVisible(!open);
    moreButton->setText(open ? QStringLiteral("Fewer fields") : QStringLiteral("More fields"));
    moreButton->setAccessibleName(moreButton->text());
    scheduleRetab();
  }

  // ---- reading the form ----

  Draft draft() const {
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
    draft.birthday = dateFromEditor(birthday->text(), base.birthday);
    const QString typed = notes->toPlainText();
    draft.notes = typed == notesShown ? base.notes : typed;
    draft.emails.clear();
    draft.phones.clear();
    draft.urls.clear();
    draft.relations.clear();
    draft.customs.clear();
    draft.addresses.clear();
    draft.events.clear();
    draft.otherOrganizations.clear();
    for (const auto& row : emails) draft.emails.append(valueFrom(row));
    for (const auto& row : phones) draft.phones.append(valueFrom(row));
    for (const auto& row : urls) draft.urls.append(valueFrom(row));
    for (const auto& row : relations) draft.relations.append(valueFrom(row));
    for (const auto& row : customs) draft.customs.append(valueFrom(row));
    for (const auto& row : addresses) draft.addresses.append(addressFrom(row));
    for (const auto& row : events) draft.events.append(eventFrom(row));
    for (const auto& row : orgs) draft.otherOrganizations.append({row.name->text(), row.title->text(), row.department->text()});
    return draft;
  }

  std::optional<Contact> validated() {
    const Draft current = draft();
    const QString problem = current.validate();
    showError(problem);
    if (!problem.isEmpty()) return std::nullopt;
    return current.toContact(base);
  }

  bool hasChanges() const {
    if (!photoBytes.isEmpty() || (removePhotoFlag && hadPhoto)) return true;
    return draft().toContact(base) != Draft::fromContact(base).toContact(base);
  }

  // ---- layout ----

  void buildIdentity(QVBoxLayout* pageLayout) {
    auto* identity = new QHBoxLayout;
    identity->setSpacing(metrics().space(18));
    auto* photoColumn = new QVBoxLayout;
    photoColumn->setSpacing(metrics().space(4));
    photoLabel = labelNamed(QString(), QStringLiteral("photo"));
    photoLabel->setAlignment(Qt::AlignCenter);
    photoLabel->setFixedSize(metrics().space(96), metrics().space(96));
    photoLabel->setAccessibleName(QStringLiteral("Photo"));
    photoButton = kindButton(QStringLiteral("Add photo"), QStringLiteral("link"));
    removePhotoButton = kindButton(QStringLiteral("Remove photo"), QStringLiteral("link"));
    photoColumn->addWidget(photoLabel);
    photoColumn->addSpacing(metrics().space(4));
    photoColumn->addWidget(photoButton);
    photoColumn->addWidget(removePhotoButton);
    photoColumn->addStretch(1);
    QObject::connect(photoButton, &QPushButton::clicked, host, [this] { choosePhoto(); });
    QObject::connect(removePhotoButton, &QPushButton::clicked, host, [this] { clearPhoto(); });

    auto* names = new QGridLayout;
    names->setContentsMargins(0, 0, 0, 0);
    names->setHorizontalSpacing(metrics().space(10));
    names->setVerticalSpacing(metrics().space(10));
    names->setColumnStretch(0, 1);
    names->setColumnStretch(1, 1);
    names->addWidget(labeled(QStringLiteral("First name"), given), 0, 0);
    names->addWidget(labeled(QStringLiteral("Last name"), family), 0, 1);
    names->addWidget(labeled(QStringLiteral("Company"), organization), 1, 0);
    names->addWidget(labeled(QStringLiteral("Job title"), titleEdit), 1, 1);
    names->setRowStretch(2, 1);
    identity->addLayout(photoColumn);
    identity->addLayout(names, 1);
    pageLayout->addLayout(identity);
    showPixmap({});
    refreshPhotoButtons();
  }

  void buildMore(QVBoxLayout* pageLayout) {
    auto* toggle = new QHBoxLayout;
    toggle->setSpacing(metrics().space(10));
    moreButton = kindButton(QStringLiteral("More fields"), QStringLiteral("link"));
    moreSummary = labelNamed(QStringLiteral("Nickname, birthday, websites, people, and custom fields"), QStringLiteral("caption"));
    toggle->addWidget(moreButton);
    toggle->addWidget(moreSummary);
    toggle->addStretch(1);
    pageLayout->addLayout(toggle);
    QObject::connect(moreButton, &QPushButton::clicked, host, [this] {
      setMoreOpen(!moreOpen);
      if (moreOpen) reveal(prefix);
    });

    morePane = new QWidget;
    auto* more = new QVBoxLayout(morePane);
    more->setContentsMargins(0, 0, 0, 0);
    more->setSpacing(metrics().space(26));

    auto* names = pairGrid(addSection(more, QStringLiteral("Name details")));
    names->addWidget(labeled(QStringLiteral("Prefix"), prefix), 0, 0);
    names->addWidget(labeled(QStringLiteral("Middle name"), middle), 0, 1);
    names->addWidget(labeled(QStringLiteral("Suffix"), suffix), 1, 0);
    names->addWidget(labeled(QStringLiteral("Nickname"), nickname), 1, 1);

    QVBoxLayout* work = addSection(more, QStringLiteral("Work details"));
    auto* workGrid = pairGrid(work);
    workGrid->addWidget(labeled(QStringLiteral("Department"), department), 0, 0);
    workGrid->addWidget(labeled(QStringLiteral("Occupation"), occupation), 0, 1);
    orgRows = rowsLayout(work, 6);
    addOrgButton = addButton(QStringLiteral("company"));
    work->addWidget(addOrgButton, 0, Qt::AlignLeft);
    for (const auto& org : base.otherOrganizations) addOrganization(org);
    QObject::connect(addOrgButton, &QPushButton::clicked, host, [this] { reveal(addOrganization({})); });

    QVBoxLayout* dates = addSection(more, QStringLiteral("Dates"));
    auto* birthdayRow = new QHBoxLayout;
    birthdayRow->setSpacing(metrics().space(6));
    auto* birthdayLabel = labelNamed(QStringLiteral("Birthday"), QStringLiteral("caption"));
    birthdayLabel->setFixedWidth(metrics().space(136));
    birthdayRow->addWidget(birthdayLabel);
    birthdayRow->addWidget(birthday, 1);
    birthday->setAccessibleName(QStringLiteral("Birthday"));
    dates->addLayout(birthdayRow);
    eventRows = rowsLayout(dates, 6);
    addEventButton = addButton(QStringLiteral("date"));
    dates->addWidget(addEventButton, 0, Qt::AlignLeft);
    for (const auto& event : base.events) addEvent(event);
    QObject::connect(addEventButton, &QPushButton::clicked, host, [this] { reveal(addEvent({})); });

    QVBoxLayout* web = addSection(more, QStringLiteral("Websites"));
    urlRows = rowsLayout(web, 6);
    addUrlButton = addButton(QStringLiteral("website"));
    web->addWidget(addUrlButton, 0, Qt::AlignLeft);
    for (const auto& url : base.urls) addUrl(url);
    QObject::connect(addUrlButton, &QPushButton::clicked, host, [this] { reveal(addUrl({})); });

    QVBoxLayout* people = addSection(more, QStringLiteral("Related people"));
    relationRows = rowsLayout(people, 6);
    addRelationButton = addButton(QStringLiteral("person"));
    people->addWidget(addRelationButton, 0, Qt::AlignLeft);
    for (const auto& relation : base.relations) addRelation(relation);
    QObject::connect(addRelationButton, &QPushButton::clicked, host, [this] { reveal(addRelation({})); });

    QVBoxLayout* custom = addSection(more, QStringLiteral("Custom fields"));
    customRows = rowsLayout(custom, 6);
    addCustomButton = addButton(QStringLiteral("custom field"));
    custom->addWidget(addCustomButton, 0, Qt::AlignLeft);
    for (const auto& field : base.customs) addCustom(field);
    QObject::connect(addCustomButton, &QPushButton::clicked, host, [this] { reveal(addCustom({})); });

    pageLayout->addWidget(morePane);
    setMoreOpen(base.hasLessCommonFields() || fieldIsMore(focus.field));
  }

  void build() {
    hadPhoto = !base.photoUrl.isEmpty();
    // Fields are made up front because focus and the More pane refer to them.
    prefix = lineEdit(base.prefix, {}, QStringLiteral("prefix"));
    given = lineEdit(base.givenName, {}, QStringLiteral("given"));
    middle = lineEdit(base.middleName, {}, QStringLiteral("middle"));
    family = lineEdit(base.familyName, {}, QStringLiteral("family"));
    suffix = lineEdit(base.suffix, {}, QStringLiteral("suffix"));
    nickname = lineEdit(base.nickname);
    organization = lineEdit(base.organization, {}, QStringLiteral("organization"));
    titleEdit = lineEdit(base.title, {}, QStringLiteral("title-field"));
    department = lineEdit(base.department);
    occupation = lineEdit(base.occupation);
    birthday = lineEdit(base.birthday.isoText(), QStringLiteral("YYYY-MM-DD, or MM-DD without a year"), QStringLiteral("birthday"));

    auto* root = new QVBoxLayout(host);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(metrics().space(14));
    root->addWidget(labelNamed(base.resourceName.isEmpty() ? QStringLiteral("New contact") : QStringLiteral("Edit contact"),
                               QStringLiteral("title")));

    page = new QWidget;
    auto* pageLayout = new QVBoxLayout(page);
    // Room on the right keeps focus borders clear of the scroll bar.
    pageLayout->setContentsMargins(0, metrics().space(4), metrics().space(10), metrics().space(4));
    pageLayout->setSpacing(metrics().space(26));

    buildIdentity(pageLayout);

    QVBoxLayout* email = addSection(pageLayout, QStringLiteral("Email"));
    emailRows = rowsLayout(email, 6);
    addEmailButton = addButton(QStringLiteral("email"));
    email->addWidget(addEmailButton, 0, Qt::AlignLeft);
    for (const auto& value : base.emails) addEmail(value);
    if (base.emails.isEmpty()) addEmail({QString(), QStringLiteral("Home"), QStringLiteral("home")});
    QObject::connect(addEmailButton, &QPushButton::clicked, host,
                     [this] { reveal(addEmail({QString(), QStringLiteral("Home"), QStringLiteral("home")})); });

    QVBoxLayout* phone = addSection(pageLayout, QStringLiteral("Phone"));
    phoneRows = rowsLayout(phone, 6);
    addPhoneButton = addButton(QStringLiteral("phone"));
    phone->addWidget(addPhoneButton, 0, Qt::AlignLeft);
    for (const auto& value : base.phones) addPhone(value);
    if (base.phones.isEmpty()) addPhone({QString(), QStringLiteral("Mobile"), QStringLiteral("mobile")});
    QObject::connect(addPhoneButton, &QPushButton::clicked, host,
                     [this] { reveal(addPhone({QString(), QStringLiteral("Mobile"), QStringLiteral("mobile")})); });

    QVBoxLayout* address = addSection(pageLayout, QStringLiteral("Address"));
    addressRows = rowsLayout(address, 18);
    addAddressButton = addButton(QStringLiteral("address"));
    address->addWidget(addAddressButton, 0, Qt::AlignLeft);
    for (const auto& value : base.addresses) addAddress(value);
    QObject::connect(addAddressButton, &QPushButton::clicked, host, [this] {
      PostalAddress fresh;
      fresh.type = QStringLiteral("home");
      fresh.label = QStringLiteral("Home");
      reveal(addAddress(fresh));
    });

    QVBoxLayout* notesSection = addSection(pageLayout, QStringLiteral("Notes"));
    notes = new QPlainTextEdit(base.notes);
    notesShown = notes->toPlainText();
    notes->setObjectName(QStringLiteral("notes"));
    notes->setAccessibleName(QStringLiteral("Notes"));
    notes->setTabChangesFocus(true);
    notes->setMinimumHeight(metrics().space(112));
    notesSection->addWidget(notes);

    buildMore(pageLayout);
    pageLayout->addStretch(1);

    scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    root->addWidget(scroll, 1);

    error = labelNamed(QString(), QStringLiteral("danger"));
    error->setWordWrap(true);
    error->hide();
    root->addWidget(error);

    auto* footer = new QHBoxLayout;
    footer->setSpacing(metrics().space(8));
    footer->addWidget(labelNamed(QStringLiteral("Ctrl+S saves · Esc cancels"), QStringLiteral("hint")));
    footer->addStretch(1);
    cancelButton = kindButton(QStringLiteral("Cancel"), QStringLiteral("outline"));
    saveButton = kindButton(QStringLiteral("Save"), QStringLiteral("primary"));
    footer->addWidget(cancelButton);
    footer->addWidget(saveButton);
    root->addLayout(footer);
    QObject::connect(cancelButton, &QPushButton::clicked, host, [this] {
      if (onCancel) onCancel();
    });
    QObject::connect(saveButton, &QPushButton::clicked, host, [this] {
      if (onSave) onSave();
    });
  }
};

ContactEditor::ContactEditor(const Contact& base, EditorFocus focus, QWidget* parent)
    : QWidget(parent), form_(std::make_unique<Form>()) {
  setObjectName(QStringLiteral("editor"));
  form_->host = this;
  form_->base = base;
  form_->focus = focus;
  form_->build();
}

ContactEditor::~ContactEditor() = default;

void ContactEditor::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  form_->scheduleRetab();
}

void ContactEditor::setOnSave(std::function<void()> save) { form_->onSave = std::move(save); }

void ContactEditor::setOnCancel(std::function<void()> cancel) { form_->onCancel = std::move(cancel); }

std::optional<Contact> ContactEditor::validatedContact() { return form_->validated(); }

bool ContactEditor::hasChanges() const { return form_->hasChanges(); }

QByteArray ContactEditor::photoBytes() const { return form_->photoBytes; }

bool ContactEditor::removePhoto() const { return form_->hadPhoto && form_->removePhotoFlag && form_->photoBytes.isEmpty(); }

void ContactEditor::setPreview(const QPixmap& pixmap) {
  if (form_->removePhotoFlag || !form_->photoBytes.isEmpty()) return;
  form_->showPixmap(pixmap);
}

void ContactEditor::focusField() { form_->revealFocus(); }

bool confirmDialog(QWidget* parent, const QString& title, const QString& body, const QString& action, const QString& cancel) {
  ConfirmBox dialog(parent, title, body, action, cancel);
  return dialog.exec() == QDialog::Accepted;
}

void showHelpDialog(QWidget* parent) {
  HelpBox dialog(parent);
  dialog.exec();
}

std::optional<AccountAction> accountDialog(QWidget* parent, bool hasClient, bool connected) {
  AccountBox dialog(parent, hasClient, connected);
  if (dialog.exec() != QDialog::Accepted) return std::nullopt;
  return dialog.action();
}

}  // namespace contacts
