#include "mainwindow.h"

#include "blockingnam.h"
#include "dialogs.h"
#include "google.h"
#include "mail.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QResizeEvent>
#include <QShowEvent>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QPlainTextEdit>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace omacontacts {

class ContactListModel : public QAbstractListModel {
 public:
  struct Row {
    QString id;
    QString name;
    QString subtitle;
  };

  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : rows_.size(); }

  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
    const Row& row = rows_.at(index.row());
    if (role == Qt::DisplayRole) return row.name;
    if (role == Qt::UserRole) return row.subtitle;
    if (role == Qt::AccessibleTextRole) return row.subtitle.isEmpty() ? row.name : row.name + QStringLiteral(", ") + row.subtitle;
    return {};
  }

  void setRows(QList<Row> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
  }

  QString idAt(int row) const { return row >= 0 && row < rows_.size() ? rows_.at(row).id : QString(); }

 private:
  QList<Row> rows_;
};

class ContactDelegate : public QStyledItemDelegate {
 public:
  Palette palette;

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    if (option.state & QStyle::State_Selected) {
      painter->fillRect(option.rect, QColor(palette.foreground.red(), palette.foreground.green(), palette.foreground.blue(), 46));
    } else if (option.state & QStyle::State_MouseOver) {
      painter->fillRect(option.rect, QColor(palette.foreground.red(), palette.foreground.green(), palette.foreground.blue(), 20));
    }
    const QString name = index.data(Qt::DisplayRole).toString();
    const QString subtitle = index.data(Qt::UserRole).toString();
    QFont nameFont = option.font;
    nameFont.setPixelSize(12);
    painter->setFont(nameFont);
    const QFontMetrics metrics(nameFont);
    const int padding = 8;
    const QRect rect = option.rect.adjusted(padding, 0, -padding, 0);
    int subtitleWidth = 0;
    if (!subtitle.isEmpty()) subtitleWidth = std::min(rect.width() / 2, metrics.horizontalAdvance(subtitle) + 12);
    painter->setPen(palette.foreground);
    painter->drawText(rect.adjusted(0, 0, -subtitleWidth, 0), Qt::AlignVCenter | Qt::AlignLeft,
                      metrics.elidedText(name, Qt::ElideRight, std::max(0, rect.width() - subtitleWidth)));
    if (!subtitle.isEmpty()) {
      painter->setPen(palette.secondary);
      painter->drawText(rect.adjusted(rect.width() - subtitleWidth, 0, 0, 0), Qt::AlignVCenter | Qt::AlignRight,
                        metrics.elidedText(subtitle, Qt::ElideRight, subtitleWidth));
    }
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return QSize(0, 28); }
};

namespace {

QPushButton* kindButton(const QString& text, const QString& kind) {
  auto* button = new QPushButton(text);
  button->setProperty("kind", kind);
  button->setAccessibleName(text);
  button->setCursor(Qt::PointingHandCursor);
  return button;
}

Key mapKey(int key) {
  switch (key) {
    case Qt::Key_Up:
      return Key::Up;
    case Qt::Key_Down:
      return Key::Down;
    case Qt::Key_Home:
      return Key::Home;
    case Qt::Key_End:
      return Key::End;
    case Qt::Key_PageUp:
      return Key::PageUp;
    case Qt::Key_PageDown:
      return Key::PageDown;
    case Qt::Key_Escape:
      return Key::Escape;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      return Key::Return;
    case Qt::Key_Delete:
      return Key::Delete;
    case Qt::Key_Backspace:
      return Key::Backspace;
    case Qt::Key_Slash:
      return Key::Slash;
    case Qt::Key_Question:
      return Key::Question;
    case Qt::Key_F1:
      return Key::F1;
    case Qt::Key_J:
      return Key::J;
    case Qt::Key_K:
      return Key::K;
    case Qt::Key_N:
      return Key::N;
    case Qt::Key_E:
      return Key::E;
    case Qt::Key_M:
      return Key::M;
    case Qt::Key_Y:
      return Key::Y;
    case Qt::Key_P:
      return Key::P;
    case Qt::Key_R:
      return Key::R;
    case Qt::Key_S:
      return Key::S;
    case Qt::Key_Q:
      return Key::Q;
    case Qt::Key_Comma:
      return Key::Comma;
    default:
      return Key::Other;
  }
}

QString oauthPage(const QString& heading, const QString& detail) {
  return QStringLiteral("<!doctype html><meta charset=\"utf-8\"><title>Omacontacts</title>"
                        "<body style=\"margin:48px;font-family:monospace;background:#1a1b26;color:#c0caf5\">"
                        "<p>%1</p><p style=\"color:#a9b1d6\">%2</p>")
      .arg(heading.toHtmlEscaped(), detail.toHtmlEscaped());
}

void clearLayout(QLayout* layout) {
  while (QLayoutItem* item = layout->takeAt(0)) {
    if (QWidget* widget = item->widget()) widget->deleteLater();
    if (QLayout* child = item->layout()) clearLayout(child);
    delete item;
  }
}

qint64 nowUnix() { return QDateTime::currentSecsSinceEpoch(); }

}  // namespace

MainWindow::MainWindow() : secrets_(makeSecretStore()) {
  setWindowTitle(QStringLiteral("Omacontacts"));
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  resize(980, 680);
  themePaths_ = ThemePaths::forHome(QDir::homePath());
  paths_ = AppPaths::forEnv(QDir::homePath(), qEnvironmentVariable("XDG_CONFIG_HOME"), qEnvironmentVariable("XDG_DATA_HOME"));
  buildUi();
  loadLocal();
  reloadTheme();
  refilter();
  qApp->installEventFilter(this);
  if (!tokens_.refreshToken.isEmpty()) QTimer::singleShot(0, this, [this] { runSync(false); });
}

MainWindow::~MainWindow() {
  if (qApp) qApp->removeEventFilter(this);
}

void MainWindow::buildUi() {
  auto* central = new QWidget;
  auto* root = new QVBoxLayout(central);
  root->setContentsMargins(18, 18, 18, 18);
  root->setSpacing(14);

  auto* header = new QHBoxLayout;
  header->setSpacing(8);
  auto* title = new QLabel(QStringLiteral("Omacontacts"));
  title->setObjectName(QStringLiteral("title"));
  themeLabel_ = new QLabel;
  themeLabel_->setObjectName(QStringLiteral("themeName"));
  header->addWidget(title);
  header->addWidget(themeLabel_);
  header->addStretch(1);
  accountButton_ = kindButton(QStringLiteral("Account"), QStringLiteral("outline"));
  syncButton_ = kindButton(QStringLiteral("Sync"), QStringLiteral("outline"));
  newButton_ = kindButton(QStringLiteral("New"), QStringLiteral("primary"));
  auto* help = kindButton(QStringLiteral("Help"), QStringLiteral("outline"));
  header->addWidget(accountButton_);
  header->addWidget(syncButton_);
  header->addWidget(newButton_);
  header->addWidget(help);
  root->addLayout(header);

  search_ = new QLineEdit;
  search_->setPlaceholderText(QStringLiteral("Find a contact"));
  search_->setClearButtonEnabled(true);
  search_->setAccessibleName(QStringLiteral("Find a contact"));
  root->addWidget(search_);

  auto* split = new QHBoxLayout;
  split->setSpacing(14);
  listPane_ = new QWidget;
  auto* listLayout = new QVBoxLayout(listPane_);
  listLayout->setContentsMargins(0, 0, 0, 0);
  listLayout->setSpacing(8);
  model_ = new ContactListModel;
  delegate_ = new ContactDelegate;
  list_ = new QListView;
  list_->setModel(model_);
  list_->setItemDelegate(delegate_);
  list_->setUniformItemSizes(true);
  list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  list_->setMouseTracking(true);
  list_->viewport()->setMouseTracking(true);
  list_->setMinimumWidth(300);
  listLayout->addWidget(list_, 1);
  emptyLabel_ = new QLabel;
  emptyLabel_->setObjectName(QStringLiteral("secondary"));
  emptyLabel_->setWordWrap(true);
  listLayout->addWidget(emptyLabel_);

  auto* rule = new QFrame;
  rule->setObjectName(QStringLiteral("vhairline"));
  rule->setFixedWidth(1);

  detailPane_ = new QWidget;
  auto* detailOuter = new QVBoxLayout(detailPane_);
  detailOuter->setContentsMargins(0, 0, 0, 0);
  auto* scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto* detailBody = new QWidget;
  detailLayout_ = new QVBoxLayout(detailBody);
  detailLayout_->setContentsMargins(0, 0, 0, 0);
  detailLayout_->setSpacing(14);
  detailLayout_->addStretch(1);
  scroll->setWidget(detailBody);
  detailOuter->addWidget(scroll);

  split->addWidget(listPane_);
  split->addWidget(rule);
  split->addWidget(detailPane_, 1);
  root->addLayout(split, 1);

  auto* footer = new QHBoxLayout;
  countLabel_ = new QLabel;
  countLabel_->setObjectName(QStringLiteral("status"));
  statusLabel_ = new QLabel;
  statusLabel_->setObjectName(QStringLiteral("status"));
  footer->addWidget(countLabel_);
  footer->addStretch(1);
  footer->addWidget(statusLabel_);
  root->addLayout(footer);

  toastFrame_ = new QFrame;
  toastFrame_->setObjectName(QStringLiteral("toast"));
  auto* toastLayout = new QHBoxLayout(toastFrame_);
  toastLayout->setContentsMargins(14, 14, 14, 14);
  toastLabel_ = new QLabel;
  toastLabel_->setWordWrap(true);
  toastLayout->addWidget(toastLabel_);
  toastFrame_->hide();
  root->addWidget(toastFrame_);

  setCentralWidget(central);

  connect(search_, &QLineEdit::textChanged, this, [this] { refilter(); });
  connect(list_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { renderDetail(); });
  connect(list_, &QListView::activated, this, [this] { handleCommand(Command::PrimaryAction); });
  connect(accountButton_, &QPushButton::clicked, this, [this] { openAccount(); });
  connect(syncButton_, &QPushButton::clicked, this, [this] { runSync(false); });
  connect(newButton_, &QPushButton::clicked, this, [this] { newContact(); });
  connect(help, &QPushButton::clicked, this, [this] { showHelpDialog(this); });
  toastFrame_->installEventFilter(this);

  watcher_ = new QFileSystemWatcher(this);
  themeTimer_ = new QTimer(this);
  themeTimer_->setSingleShot(true);
  themeTimer_->setInterval(200);
  connect(themeTimer_, &QTimer::timeout, this, [this] { reloadTheme(); });
  connect(watcher_, &QFileSystemWatcher::fileChanged, this, [this] { scheduleThemeReload(); });
  connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { scheduleThemeReload(); });
  toastTimer_ = new QTimer(this);
  toastTimer_->setSingleShot(true);
  connect(toastTimer_, &QTimer::timeout, this, [this] { toastFrame_->hide(); });
}

void MainWindow::loadLocal() {
  if (auto bytes = readFile(paths_.configFile)) {
    if (auto config = readConfigJson(*bytes)) config_ = *config;
  }
  QString error;
  if (auto secret = secrets_->load(error)) {
    if (auto tokens = readTokensJson(secret->toUtf8())) tokens_ = *tokens;
  }
  if (auto bytes = readFile(paths_.cacheFile)) {
    const auto snapshot = readCacheJson(*bytes);
    if (snapshot.ok()) {
      syncToken_ = snapshot.syncToken;
      index_.replaceAll(snapshot.contacts);
    }
  }
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  if (firstShow_) {
    firstShow_ = false;
    search_->setFocus();
    applyWidth(width());
  }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  applyWidth(width());
}

void MainWindow::closeEvent(QCloseEvent* event) {
  closing_ = true;
  if (oauthServer_) oauthServer_->close();
  QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == toastFrame_ && event->type() == QEvent::MouseButtonRelease) {
    toastFrame_->hide();
    return true;
  }
  if (event->type() != QEvent::KeyPress) return QMainWindow::eventFilter(watched, event);
  if (QApplication::activeModalWidget()) return QMainWindow::eventFilter(watched, event);
  auto* widget = qobject_cast<QWidget*>(watched);
  if (!widget || (widget != this && !isAncestorOf(widget))) return QMainWindow::eventFilter(watched, event);
  auto* key = static_cast<QKeyEvent*>(event);
  if (qobject_cast<QPushButton*>(QApplication::focusWidget()) &&
      (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Space)) {
    return QMainWindow::eventFilter(watched, event);
  }
  KeyEvent mapped;
  mapped.key = mapKey(key->key());
  mapped.ctrl = key->modifiers() & Qt::ControlModifier;
  mapped.shift = key->modifiers() & Qt::ShiftModifier;
  mapped.alt = key->modifiers() & Qt::AltModifier;
  auto* focus = QApplication::focusWidget();
  mapped.textInput = qobject_cast<QLineEdit*>(focus) || qobject_cast<QTextEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus);
  mapped.searchEmpty = search_->text().isEmpty();
  mapped.narrowDetail = narrow_ && narrowDetail_;
  mapped.surface = Surface::Browser;
  const Command command = routeKey(mapped);
  if (command == Command::Ignore) return QMainWindow::eventFilter(watched, event);
  handleCommand(command);
  return true;
}

void MainWindow::applyPalette(const Palette& palette) {
  palette_ = palette;
  havePalette_ = true;
  if (delegate_) delegate_->palette = palette;
  qApp->setStyleSheet(stylesheetFor(palette));
  const auto buttons = findChildren<QPushButton*>();
  for (auto* button : buttons) {
    button->style()->unpolish(button);
    button->style()->polish(button);
  }
  if (list_) list_->viewport()->update();
  if (themeLabel_) themeLabel_->setText(palette.name);
}

void MainWindow::scheduleThemeReload() {
  const QString colors = themePaths_.stateColors;
  if (QFile::exists(colors) && !watcher_->files().contains(colors)) watcher_->addPath(colors);
  themeTimer_->start();
}

void MainWindow::reloadTheme() {
  if (QDir(themePaths_.stateDir).exists() && !watcher_->directories().contains(themePaths_.stateDir)) {
    watcher_->addPath(themePaths_.stateDir);
  }
  const QString themeDir = QFileInfo(themePaths_.stateColors).absolutePath();
  if (QDir(themeDir).exists() && !watcher_->directories().contains(themeDir)) watcher_->addPath(themeDir);
  if (QFile::exists(themePaths_.stateColors) && !watcher_->files().contains(themePaths_.stateColors)) {
    watcher_->addPath(themePaths_.stateColors);
  }
  const ThemeLoad load = loadTheme(themePaths_);
  if (load.origin == ThemeOrigin::Builtin && havePalette_) {
    const QFileInfo info(themePaths_.stateColors);
    if (info.exists() && info.size() < 40) return;
  }
  if (havePalette_ && palette_.sameColors(load.palette) && palette_.name == load.palette.name) return;
  applyPalette(load.palette);
}

void MainWindow::refilter() {
  const QString keep = selected() ? selected()->resourceName : QString();
  const auto hits = index_.query(search_->text());
  QList<ContactListModel::Row> rows;
  rows.reserve(hits.size());
  int select = 0;
  bool found = false;
  for (const auto& hit : hits) {
    if (hit.contact->resourceName == keep) {
      select = rows.size();
      found = true;
    }
    rows.append({hit.contact->resourceName, hit.contact->displayName(), hit.contact->subtitle()});
  }
  model_->setRows(std::move(rows));
  if (model_->rowCount() > 0) list_->setCurrentIndex(model_->index(found ? select : 0, 0));
  else renderDetail();
  updateChrome();
}

void MainWindow::renderDetail() {
  clearLayout(detailLayout_);
  const Contact* contact = selected();
  if (contact == nullptr) {
    auto* title = new QLabel(search_->text().isEmpty() ? QStringLiteral("No contact selected") : QStringLiteral("No contacts match"));
    title->setObjectName(QStringLiteral("panelTitle"));
    auto* body = new QLabel;
    body->setObjectName(QStringLiteral("secondary"));
    body->setWordWrap(true);
    if (!search_->text().isEmpty()) body->setText(QStringLiteral("Try a shorter name, email, or phone number."));
    else if (tokens_.refreshToken.isEmpty()) {
      body->setText(QStringLiteral("Connect Google Contacts from Account. Create a Desktop OAuth client, enable the People API, "
                                   "and paste the client id."));
    } else {
      body->setText(QStringLiteral("Press n to add a contact."));
    }
    detailLayout_->addWidget(title);
    detailLayout_->addWidget(body);
    detailLayout_->addStretch(1);
    return;
  }
  const Contact shown = *contact;
  auto* identity = new QHBoxLayout;
  backButton_ = kindButton(QStringLiteral("Back"), QStringLiteral("outline"));
  backButton_->setVisible(narrow_ && narrowDetail_);
  connect(backButton_, &QPushButton::clicked, this, [this] { handleCommand(Command::Back); });
  auto* avatar = new QLabel(shown.initials());
  avatar->setObjectName(QStringLiteral("avatar"));
  avatar->setAlignment(Qt::AlignCenter);
  auto* names = new QVBoxLayout;
  auto* name = new QLabel(shown.displayName());
  name->setObjectName(QStringLiteral("title"));
  name->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto* sub = new QLabel(shown.subtitle());
  sub->setObjectName(QStringLiteral("secondary"));
  sub->setTextInteractionFlags(Qt::TextSelectableByMouse);
  names->addWidget(name);
  names->addWidget(sub);
  identity->addWidget(backButton_);
  identity->addWidget(avatar);
  identity->addLayout(names, 1);
  detailLayout_->addLayout(identity);

  auto* actions = new QHBoxLayout;
  actions->setSpacing(6);
  auto* edit = kindButton(QStringLiteral("Edit"), QStringLiteral("primary"));
  auto* remove = kindButton(QStringLiteral("Delete"), QStringLiteral("danger"));
  connect(edit, &QPushButton::clicked, this, [this] { editSelected(); });
  connect(remove, &QPushButton::clicked, this, [this] { deleteSelected(); });
  actions->addWidget(edit);
  actions->addWidget(remove);
  actions->addStretch(1);
  detailLayout_->addLayout(actions);

  if (shown.emails.isEmpty()) {
    auto* none = new QLabel(QStringLiteral("No email address"));
    none->setObjectName(QStringLiteral("secondary"));
    detailLayout_->addWidget(none);
  } else {
    auto* heading = new QLabel(QStringLiteral("Email"));
    heading->setObjectName(QStringLiteral("panelTitle"));
    detailLayout_->addWidget(heading);
    for (const auto& email : shown.emails) {
      auto* row = new QHBoxLayout;
      auto* value = new QLabel(email.value);
      value->setTextInteractionFlags(Qt::TextSelectableByMouse);
      auto* label = new QLabel(email.label);
      label->setObjectName(QStringLiteral("secondary"));
      auto* send = kindButton(QStringLiteral("Email"), QStringLiteral("outline"));
      const QString address = email.value;
      connect(send, &QPushButton::clicked, this, [this, address] {
        Contact one;
        one.givenName = selected() ? selected()->displayName() : address;
        one.emails.append({address, {}});
        const auto command = mailCommand(one);
        if (!command.ok()) {
          toast(command.error, true);
          return;
        }
        QProcess::startDetached(command.argv.first(), command.argv.mid(1));
        toast(QStringLiteral("Opening email…"), false);
      });
      row->addWidget(value, 1);
      row->addWidget(label);
      row->addWidget(send);
      detailLayout_->addLayout(row);
    }
  }

  if (!shown.phones.isEmpty()) {
    auto* heading = new QLabel(QStringLiteral("Phone"));
    heading->setObjectName(QStringLiteral("panelTitle"));
    detailLayout_->addWidget(heading);
    for (const auto& phone : shown.phones) {
      auto* row = new QHBoxLayout;
      auto* value = new QLabel(phone.value);
      value->setTextInteractionFlags(Qt::TextSelectableByMouse);
      auto* label = new QLabel(phone.label);
      label->setObjectName(QStringLiteral("secondary"));
      auto* copy = kindButton(QStringLiteral("Copy"), QStringLiteral("outline"));
      const QString number = phone.value;
      connect(copy, &QPushButton::clicked, this, [this, number] {
        QGuiApplication::clipboard()->setText(number);
        toast(QStringLiteral("Phone copied"), false);
      });
      row->addWidget(value, 1);
      row->addWidget(label);
      row->addWidget(copy);
      detailLayout_->addLayout(row);
    }
  }

  if (!shown.notes.isEmpty()) {
    auto* heading = new QLabel(QStringLiteral("Notes"));
    heading->setObjectName(QStringLiteral("panelTitle"));
    auto* notes = new QLabel(shown.notes);
    notes->setWordWrap(true);
    notes->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout_->addWidget(heading);
    detailLayout_->addWidget(notes);
  }
  detailLayout_->addStretch(1);
}

void MainWindow::updateChrome() {
  const int shown = model_ ? model_->rowCount() : 0;
  const int total = index_.size();
  if (search_ && !search_->text().isEmpty()) countLabel_->setText(QStringLiteral("%1 of %2").arg(shown).arg(total));
  else countLabel_->setText(QStringLiteral("%1 contacts").arg(total));
  if (busy_) statusLabel_->setText(QStringLiteral("Syncing…"));
  else if (!tokens_.refreshToken.isEmpty()) statusLabel_->setText(QStringLiteral("Google"));
  else statusLabel_->setText(QStringLiteral("Not signed in"));
  if (syncButton_) syncButton_->setEnabled(!busy_);
  if (newButton_) newButton_->setEnabled(!busy_);
  if (emptyLabel_) {
    emptyLabel_->setVisible(shown == 0);
    if (shown == 0) {
      emptyLabel_->setText(!search_->text().isEmpty() ? QStringLiteral("No contacts match.")
                           : tokens_.refreshToken.isEmpty() ? QStringLiteral("Connect a Google account to begin.")
                                                            : QStringLiteral("No contacts yet."));
    }
  }
}

void MainWindow::toast(const QString& text, bool error) {
  toastLabel_->setText(text);
  toastFrame_->setProperty("error", error);
  toastFrame_->style()->unpolish(toastFrame_);
  toastFrame_->style()->polish(toastFrame_);
  toastFrame_->show();
  if (error) toastTimer_->stop();
  else toastTimer_->start(6000);
}

void MainWindow::handleCommand(Command command) {
  switch (command) {
    case Command::MoveUp:
      moveSelection(-1, true);
      break;
    case Command::MoveDown:
      moveSelection(1, true);
      break;
    case Command::PageUp:
      moveSelection(-10, false);
      break;
    case Command::PageDown:
      moveSelection(10, false);
      break;
    case Command::MoveHome:
      if (model_->rowCount() > 0) list_->setCurrentIndex(model_->index(0, 0));
      break;
    case Command::MoveEnd:
      if (model_->rowCount() > 0) list_->setCurrentIndex(model_->index(model_->rowCount() - 1, 0));
      break;
    case Command::FocusSearch:
      search_->setFocus();
      search_->selectAll();
      break;
    case Command::ClearSearch:
      search_->clear();
      search_->setFocus();
      break;
    case Command::Back:
      narrowDetail_ = false;
      applyWidth(width());
      search_->setFocus();
      break;
    case Command::NewContact:
      newContact();
      break;
    case Command::EditContact:
      editSelected();
      break;
    case Command::DeleteContact:
      deleteSelected();
      break;
    case Command::PrimaryAction:
      if (narrow_ && !narrowDetail_) {
        narrowDetail_ = true;
        applyWidth(width());
        renderDetail();
        break;
      }
      if (selected() && !selected()->primaryEmail().isEmpty()) sendMail();
      else editSelected();
      break;
    case Command::CopyEmail:
      copyEmail();
      break;
    case Command::CopyPhone:
      copyPhone();
      break;
    case Command::Sync:
      runSync(false);
      break;
    case Command::Help:
      showHelpDialog(this);
      break;
    case Command::Account:
      openAccount();
      break;
    case Command::Quit:
      close();
      break;
    case Command::Ignore:
    case Command::Save:
    case Command::Cancel:
      break;
  }
}

void MainWindow::moveSelection(int delta, bool wrap) {
  const int count = model_->rowCount();
  if (count <= 0) return;
  const int current = list_->currentIndex().isValid() ? list_->currentIndex().row() : 0;
  const int next = moveIndex(current, count, delta, wrap);
  const QModelIndex index = model_->index(next, 0);
  list_->setCurrentIndex(index);
  list_->scrollTo(index);
}

void MainWindow::selectId(const QString& id) {
  for (int row = 0; row < model_->rowCount(); ++row) {
    if (model_->idAt(row) == id) {
      const QModelIndex index = model_->index(row, 0);
      list_->setCurrentIndex(index);
      list_->scrollTo(index);
      return;
    }
  }
}

const Contact* MainWindow::selected() const {
  if (!list_ || !list_->currentIndex().isValid()) return nullptr;
  return index_.find(model_->idAt(list_->currentIndex().row()));
}

void MainWindow::editSelected() {
  const Contact* contact = selected();
  if (contact == nullptr || busy_) return;
  auto edited = editContactDialog(this, *contact);
  if (!edited) return;
  runSave(*edited, false);
}

void MainWindow::newContact() {
  if (busy_) return;
  auto created = editContactDialog(this, {});
  if (!created) return;
  runSave(*created, true);
}

void MainWindow::deleteSelected() {
  const Contact* contact = selected();
  if (contact == nullptr || busy_) return;
  const QString id = contact->resourceName;
  const QString name = contact->displayName();
  if (!confirmDialog(this, QStringLiteral("Delete %1?").arg(name),
                     QStringLiteral("This removes the contact from Google Contacts."), QStringLiteral("Delete"))) {
    return;
  }
  runDelete(id);
}

void MainWindow::copyEmail() {
  const Contact* contact = selected();
  if (contact == nullptr || contact->primaryEmail().isEmpty()) {
    toast(QStringLiteral("This contact has no email address."), true);
    return;
  }
  QGuiApplication::clipboard()->setText(contact->primaryEmail());
  toast(QStringLiteral("Email copied"), false);
}

void MainWindow::copyPhone() {
  const Contact* contact = selected();
  if (contact == nullptr || contact->primaryPhone().isEmpty()) {
    toast(QStringLiteral("This contact has no phone number."), true);
    return;
  }
  QGuiApplication::clipboard()->setText(contact->primaryPhone());
  toast(QStringLiteral("Phone copied"), false);
}

void MainWindow::sendMail() {
  const Contact* contact = selected();
  if (contact == nullptr) return;
  const auto command = mailCommand(*contact);
  if (!command.ok()) {
    toast(command.error, true);
    return;
  }
  QProcess::startDetached(command.argv.first(), command.argv.mid(1));
  toast(QStringLiteral("Opening email…"), false);
}

void MainWindow::openAccount() {
  auto result = accountDialog(this, config_, !tokens_.refreshToken.isEmpty());
  if (!result) return;
  config_ = result->config;
  saveConfig();
  if (result->action == AccountAction::Connect) startOAuth();
  else if (result->action == AccountAction::Disconnect) {
    if (!confirmDialog(this, QStringLiteral("Disconnect Google?"),
                       QStringLiteral("Omacontacts forgets the local copy. Contacts stay in the Google account."),
                       QStringLiteral("Disconnect"))) {
      return;
    }
    QString error;
    secrets_->clear(error);
    tokens_ = {};
    syncToken_.clear();
    index_.replaceAll({});
    saveCache();
    refilter();
    toast(QStringLiteral("Disconnected"), false);
  } else {
    toast(QStringLiteral("Account saved"), false);
  }
}

void MainWindow::setBusy(bool busy) {
  busy_ = busy;
  updateChrome();
}

void MainWindow::saveCache() {
  QString error;
  if (!writePrivateFile(paths_.cacheFile, writeCacheJson(index_.all(), syncToken_), error)) toast(error, true);
}

void MainWindow::saveConfig() {
  QString error;
  if (!writePrivateFile(paths_.configFile, writeConfigJson(config_), error)) toast(error, true);
}

void MainWindow::saveTokens(const OAuthTokens& tokens) {
  tokens_ = tokens;
  QString error;
  if (!secrets_->save(QString::fromUtf8(writeTokensJson(tokens_)), error)) toast(error, true);
}

void MainWindow::applyWidth(int width) {
  narrow_ = width < 760;
  if (!narrow_) narrowDetail_ = false;
  if (listPane_) listPane_->setVisible(!narrow_ || !narrowDetail_);
  if (detailPane_) detailPane_->setVisible(!narrow_ || narrowDetail_);
  if (backButton_) backButton_->setVisible(narrow_ && narrowDetail_);
}

void MainWindow::runSync(bool forceFull) {
  if (busy_) return;
  if (tokens_.refreshToken.isEmpty() || config_.clientId.isEmpty()) {
    toast(QStringLiteral("Connect a Google account first."), true);
    return;
  }
  setBusy(true);
  const auto client = OAuthClient{config_.clientId, config_.clientSecret};
  auto tokens = tokens_;
  const QString syncToken = forceFull ? QString() : syncToken_;
  const QList<Contact> current = index_.all();
  QThreadPool::globalInstance()->start([this, client, tokens, syncToken, current]() mutable {
    BlockingNam http;
    QString error;
    auto ensured = ensureAccess(http, client, tokens, nowUnix());
    if (!ensured.ok()) error = ensured.error;
    QList<Contact> contacts = current;
    QString nextToken = syncToken;
    if (error.isEmpty()) {
      QString access = tokens.accessToken;
      auto refresh = [&]() -> QString {
        auto refreshed = refreshAccess(http, client, tokens, nowUnix());
        if (!refreshed.ok()) return refreshed.error;
        tokens = refreshed.tokens;
        access = tokens.accessToken;
        return {};
      };
      auto fetched = fetchConnections(http, access, syncToken, refresh);
      tokens.accessToken = access;
      if (!fetched.ok()) error = fetched.error;
      else {
        contacts = syncToken.isEmpty() ? applyFullSync(fetched.people) : applyIncrementalSync(current, fetched.people);
        if (!fetched.syncToken.isEmpty()) nextToken = fetched.syncToken;
      }
    }
    QMetaObject::invokeMethod(
        this,
        [this, error, contacts = std::move(contacts), nextToken, tokens]() mutable {
          if (closing_) return;
          setBusy(false);
          saveTokens(tokens);
          if (error == QLatin1String("sync-token-expired") && !resyncing_) {
            resyncing_ = true;
            syncToken_.clear();
            runSync(true);
            return;
          }
          resyncing_ = false;
          if (!error.isEmpty()) {
            toast(error, true);
            return;
          }
          index_.replaceAll(std::move(contacts));
          syncToken_ = nextToken;
          saveCache();
          refilter();
          toast(QStringLiteral("Synced %1 contacts").arg(index_.size()), false);
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::runSave(const Contact& contact, bool isNew) {
  if (busy_) return;
  if (tokens_.refreshToken.isEmpty()) {
    toast(QStringLiteral("Connect a Google account before saving."), true);
    return;
  }
  setBusy(true);
  const auto client = OAuthClient{config_.clientId, config_.clientSecret};
  auto tokens = tokens_;
  QThreadPool::globalInstance()->start([this, client, tokens, contact, isNew]() mutable {
    BlockingNam http;
    auto ensured = ensureAccess(http, client, tokens, nowUnix());
    QString error = ensured.ok() ? QString() : ensured.error;
    Contact saved = contact;
    if (error.isEmpty()) {
      QString access = tokens.accessToken;
      auto refresh = [&]() -> QString {
        auto refreshed = refreshAccess(http, client, tokens, nowUnix());
        if (!refreshed.ok()) return refreshed.error;
        tokens = refreshed.tokens;
        access = tokens.accessToken;
        return {};
      };
      const auto written = isNew ? createContact(http, access, contact, refresh) : updateContact(http, access, contact, refresh);
      tokens.accessToken = access;
      if (!written.ok()) error = written.error;
      else saved = written.contact;
    }
    QMetaObject::invokeMethod(
        this,
        [this, error, saved, tokens, isNew]() {
          if (closing_) return;
          setBusy(false);
          saveTokens(tokens);
          if (!error.isEmpty()) {
            toast(error, true);
            return;
          }
          index_.upsert(saved);
          saveCache();
          refilter();
          selectId(saved.resourceName);
          toast(isNew ? QStringLiteral("Contact added") : QStringLiteral("Contact saved"), false);
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::runDelete(const QString& id) {
  if (busy_) return;
  setBusy(true);
  const auto client = OAuthClient{config_.clientId, config_.clientSecret};
  auto tokens = tokens_;
  QThreadPool::globalInstance()->start([this, client, tokens, id]() mutable {
    BlockingNam http;
    auto ensured = ensureAccess(http, client, tokens, nowUnix());
    QString error = ensured.ok() ? QString() : ensured.error;
    if (error.isEmpty()) {
      QString access = tokens.accessToken;
      auto refresh = [&]() -> QString {
        auto refreshed = refreshAccess(http, client, tokens, nowUnix());
        if (!refreshed.ok()) return refreshed.error;
        tokens = refreshed.tokens;
        access = tokens.accessToken;
        return {};
      };
      error = deleteContact(http, access, id, refresh);
      tokens.accessToken = access;
    }
    QMetaObject::invokeMethod(
        this,
        [this, error, id, tokens]() {
          if (closing_) return;
          setBusy(false);
          saveTokens(tokens);
          if (!error.isEmpty()) {
            toast(error, true);
            return;
          }
          index_.remove(id);
          saveCache();
          refilter();
          toast(QStringLiteral("Contact deleted"), false);
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::startOAuth() {
  if (config_.clientId.isEmpty()) {
    toast(QStringLiteral("Enter a Google client id first."), true);
    return;
  }
  if (oauthServer_) {
    oauthServer_->close();
    oauthServer_->deleteLater();
  }
  oauthServer_ = new QTcpServer(this);
  if (!oauthServer_->listen(QHostAddress::LocalHost, 0)) {
    toast(QStringLiteral("Could not listen for the Google sign-in redirect."), true);
    return;
  }
  oauthVerifier_ = randomUrlToken(32);
  oauthState_ = randomUrlToken(16);
  oauthRedirect_ = QStringLiteral("http://127.0.0.1:%1").arg(oauthServer_->serverPort());
  oauthBuffer_.clear();
  const QString url = buildAuthUrl({config_.clientId, config_.clientSecret}, oauthRedirect_, oauthState_,
                                   pkceChallengeS256(oauthVerifier_));
  connect(oauthServer_, &QTcpServer::newConnection, this, [this] {
    QTcpSocket* socket = oauthServer_->nextPendingConnection();
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
      oauthBuffer_.append(socket->readAll());
      if (!oauthBuffer_.contains("\r\n\r\n") && oauthBuffer_.size() < 8192) return;
      const RedirectQuery query = parseOauthRedirect(oauthBuffer_);
      QString heading = QStringLiteral("Omacontacts is signed in.");
      QString detail = QStringLiteral("You can close this tab.");
      QString failure;
      if (!query.ok()) failure = query.error;
      else if (query.state != oauthState_) failure = QStringLiteral("The sign-in response did not match this window.");
      if (!failure.isEmpty()) {
        heading = QStringLiteral("Omacontacts could not sign in.");
        detail = failure;
      }
      const QByteArray html = oauthPage(heading, detail).toUtf8();
      const QByteArray response = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ") +
                                  QByteArray::number(html.size()) + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + html;
      socket->write(response);
      socket->disconnectFromHost();
      oauthServer_->close();
      if (!failure.isEmpty()) {
        toast(failure, true);
        return;
      }
      if (oauthTimer_) oauthTimer_->stop();
      setBusy(true);
      const auto client = OAuthClient{config_.clientId, config_.clientSecret};
      const QString code = query.code;
      const QString redirect = oauthRedirect_;
      const QString verifier = oauthVerifier_;
      QThreadPool::globalInstance()->start([this, client, code, redirect, verifier] {
        BlockingNam http;
        auto exchanged = exchangeCode(http, client, code, redirect, verifier, nowUnix());
        QList<Contact> contacts;
        QString syncToken;
        QString error = exchanged.ok() ? QString() : exchanged.error;
        auto tokens = exchanged.tokens;
        if (error.isEmpty()) {
          QString access = tokens.accessToken;
          auto refresh = [&]() -> QString {
            auto refreshed = refreshAccess(http, client, tokens, nowUnix());
            if (!refreshed.ok()) return refreshed.error;
            tokens = refreshed.tokens;
            access = tokens.accessToken;
            return {};
          };
          auto fetched = fetchConnections(http, access, QString(), refresh);
          tokens.accessToken = access;
          if (!fetched.ok()) error = fetched.error;
          else {
            contacts = applyFullSync(fetched.people);
            syncToken = fetched.syncToken;
          }
        }
        QMetaObject::invokeMethod(
            this,
            [this, error, contacts = std::move(contacts), syncToken, tokens]() mutable {
              if (closing_) return;
              setBusy(false);
              if (!error.isEmpty()) {
                toast(error, true);
                return;
              }
              saveTokens(tokens);
              index_.replaceAll(std::move(contacts));
              syncToken_ = syncToken;
              saveCache();
              refilter();
              toast(QStringLiteral("Connected to Google Contacts"), false);
            },
            Qt::QueuedConnection);
      });
    });
  });
  if (!oauthTimer_) {
    oauthTimer_ = new QTimer(this);
    oauthTimer_->setSingleShot(true);
    connect(oauthTimer_, &QTimer::timeout, this, [this] {
      if (oauthServer_) oauthServer_->close();
      toast(QStringLiteral("Timed out waiting for Google in the browser."), true);
    });
  }
  oauthTimer_->start(180000);
  const bool haveOmarchy = !QStandardPaths::findExecutable(QStringLiteral("omarchy")).isEmpty();
  const auto command = browserCommand(url, haveOmarchy);
  QProcess::startDetached(command.argv.first(), command.argv.mid(1));
  toast(QStringLiteral("Waiting for the browser…"), false);
}

}  // namespace omacontacts
