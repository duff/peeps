#include "mainwindow.h"

#include "blockingnam.h"
#include "dialogs.h"
#include "google.h"
#include "mail.h"
#include "photos.h"
#include "worker.h"

#include <QAction>
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
#include <QMenu>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QPlainTextEdit>
#include <QStyledItemDelegate>
#include <QTextEdit>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace omacontacts {

class ContactListModel : public QAbstractListModel {
 public:
  struct Row {
    QString id;
    QString name;
    QString subtitle;
    QString photoUrl;
    QString initials;
  };

  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : rows_.size(); }

  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
    const Row& row = rows_.at(index.row());
    if (role == Qt::DisplayRole) return row.name;
    if (role == Qt::UserRole) return row.subtitle;
    if (role == Qt::UserRole + 1) return row.photoUrl;
    if (role == Qt::UserRole + 2) return row.initials;
    if (role == Qt::UserRole + 3) return row.id;
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
  PhotoStore* photos = nullptr;
  bool suppressHover = false;

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    if (option.state & QStyle::State_Selected) {
      painter->fillRect(option.rect, QColor(palette.foreground.red(), palette.foreground.green(), palette.foreground.blue(), 46));
    } else if (!suppressHover && (option.state & QStyle::State_MouseOver)) {
      painter->fillRect(option.rect, QColor(palette.foreground.red(), palette.foreground.green(), palette.foreground.blue(), 20));
    }
    const QString name = index.data(Qt::DisplayRole).toString();
    const QString subtitle = index.data(Qt::UserRole).toString();
    QFont nameFont = option.font;
    nameFont.setPixelSize(12);
    nameFont.setBold(false);
    painter->setFont(nameFont);
    const QFontMetrics metrics(nameFont);
    const QRect rect = option.rect.adjusted(8, 0, -8, 0);
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

enum class FieldAction { Copy, CopyPhoto, Edit, DeleteContact, CopyContact, ChangePhoto, RemovePhoto };

class FieldDelegate : public QStyledItemDelegate {
 public:
  Palette palette;
  bool controlHeld = false;

  static bool wraps(const QModelIndex& index) {
    return index.data(Qt::UserRole + 4).toInt() == static_cast<int>(EditorField::Notes);
  }

  static int rowWidth(const QStyleOptionViewItem& option) {
    if (const auto* view = qobject_cast<const QAbstractItemView*>(option.widget)) {
      const int width = view->viewport()->width();
      if (width > 0) return width;
    }
    return std::max(option.rect.width(), 0);
  }

  static int valueColumnWidth(const QFontMetrics& metrics, int width, const QString& label, const QString& action) {
    const int inner = std::max(0, width - 16);
    const int actionWidth = action.isEmpty() ? 0 : metrics.horizontalAdvance(action) + 16;
    const int labelWidth =
        label.isEmpty() ? 0 : std::min(inner / 3, metrics.horizontalAdvance(QStringLiteral("Phonetic first name")) + 16);
    return std::max(1, inner - labelWidth - actionWidth);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    const auto actionKind = static_cast<FieldAction>(index.data(Qt::UserRole + 2).toInt());
    const bool photoRow = actionKind == FieldAction::CopyPhoto;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    if (!photoRow && selected) {
      painter->fillRect(option.rect, QColor(palette.foreground.red(), palette.foreground.green(), palette.foreground.blue(), 46));
    } else if (!photoRow && hover) {
      painter->fillRect(option.rect, QColor(palette.foreground.red(), palette.foreground.green(), palette.foreground.blue(), 20));
    }
    const QString label = index.data(Qt::UserRole).toString();
    const QString value = index.data(Qt::DisplayRole).toString();
    const QString action = index.data(Qt::UserRole + 1).toString();
    const bool danger = index.data(Qt::UserRole + 2).toInt() == static_cast<int>(FieldAction::DeleteContact);
    const QPixmap photo = index.data(Qt::DecorationRole).value<QPixmap>();
    const QRect rect = option.rect.adjusted(8, 0, -8, 0);
    if (photoRow) {
      if (!photo.isNull()) {
        const int maxEdge = std::min(160, rect.width());
        QSize logical = photo.deviceIndependentSize().toSize();
        logical.scale(maxEdge, maxEdge, Qt::KeepAspectRatio);
        const QRect target(rect.center().x() - logical.width() / 2, rect.center().y() - logical.height() / 2, logical.width(),
                           logical.height());
        painter->drawPixmap(target, photo);
        if (selected || hover) {
          QPen pen(palette.foreground);
          pen.setWidth(selected ? 2 : 1);
          painter->setPen(pen);
          painter->setBrush(Qt::NoBrush);
          painter->drawRect(target.adjusted(-4, -4, 3, 3));
        }
      }
      painter->restore();
      return;
    }
    QFont font = option.font;
    font.setPixelSize(12);
    painter->setFont(font);
    const QFontMetrics metrics(font);
    const bool wrap = wraps(index);
    const QStringList lines = value.split(QLatin1Char('\n'));
    const int lineHeight = std::max(1, metrics.lineSpacing());
    const int pad = std::max(0, (28 - lineHeight) / 2);
    const bool several = wrap || lines.size() > 1;
    const int textTop = several ? rect.top() + pad : rect.top();
    const int textHeight = several ? lineHeight : rect.height();
    const bool rowCue = actionKind == FieldAction::Copy || actionKind == FieldAction::CopyContact ||
                        actionKind == FieldAction::Edit || actionKind == FieldAction::DeleteContact;
    const bool showAction = !action.isEmpty() && (!rowCue || selected || hover);
    const bool emailCopy = showAction && actionKind == FieldAction::Copy && index.data(Qt::UserRole + 3).toBool();
    const QString shownCue = emailCopy && controlHeld ? QStringLiteral("Send email") : action;
    const int actionWidth = showAction || (actionKind == FieldAction::Copy && wrap)
                                ? (shownCue.isEmpty() ? 0 : metrics.horizontalAdvance(shownCue) + 16)
                                : 0;
    const bool keepLabel = index.data(Qt::UserRole + 6).toBool();
    const int labelWidth = label.isEmpty() && !keepLabel
                               ? 0
                               : std::min(rect.width() / 3, metrics.horizontalAdvance(QStringLiteral("Phonetic first name")) + 16);
    painter->setPen(palette.secondary);
    if (!label.isEmpty()) {
      painter->drawText(QRect(rect.left(), textTop, labelWidth, textHeight), Qt::AlignVCenter | Qt::AlignLeft,
                        metrics.elidedText(label, Qt::ElideRight, labelWidth));
    }
    int valueLeft = rect.left() + labelWidth;
    int valueWidth = std::max(0, rect.width() - labelWidth - actionWidth);
    painter->setPen(danger ? palette.danger : palette.foreground);
    if (wrap) {
      painter->drawText(QRect(valueLeft, textTop, valueWidth, std::max(lineHeight, rect.bottom() - textTop - pad + 1)),
                        Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, value);
    } else {
      for (int i = 0; i < lines.size(); ++i) {
        painter->drawText(QRect(valueLeft, textTop + i * (several ? lineHeight : 0), valueWidth, textHeight),
                          Qt::AlignVCenter | Qt::AlignLeft, metrics.elidedText(lines.at(i), Qt::ElideRight, valueWidth));
      }
    }
    if (showAction) {
      painter->setPen(danger ? palette.danger : palette.secondary);
      painter->drawText(QRect(rect.right() - actionWidth + 16, textTop, actionWidth - 16, textHeight),
                        Qt::AlignVCenter | Qt::AlignRight, shownCue);
    }
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    if (static_cast<FieldAction>(index.data(Qt::UserRole + 2).toInt()) == FieldAction::CopyPhoto) return QSize(0, 176);
    QFont font = option.font;
    font.setPixelSize(12);
    const QFontMetrics metrics(font);
    const int lineHeight = std::max(1, metrics.lineSpacing());
    const int pad = std::max(0, (28 - lineHeight) / 2);
    const QString value = index.data(Qt::DisplayRole).toString();
    if (wraps(index)) {
      const int width = valueColumnWidth(metrics, std::max(1, rowWidth(option)), index.data(Qt::UserRole).toString(),
                                         index.data(Qt::UserRole + 1).toString());
      const QRect bounds = metrics.boundingRect(QRect(0, 0, width, 100000), Qt::AlignLeft | Qt::TextWordWrap, value);
      if (bounds.height() <= lineHeight) return QSize(0, 28);
      return QSize(0, pad + bounds.height() + pad);
    }
    const int lines = std::max(1, static_cast<int>(value.count(QLatin1Char('\n'))) + 1);
    if (lines <= 1) return QSize(0, 28);
    return QSize(0, pad + lines * lineHeight + pad);
  }
};

namespace {

QPushButton* kindButton(const QString& text, const QString& kind) {
  auto* button = new QPushButton(text);
  button->setProperty("kind", kind);
  button->setAccessibleName(text);
  button->setCursor(Qt::PointingHandCursor);
  button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  return button;
}

class QuietListView : public QListView {
 public:
  using QListView::QListView;

 protected:
  void keyboardSearch(const QString&) override {}
};

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
    case Qt::Key_C:
      return Key::C;
    case Qt::Key_Comma:
      return Key::Comma;
    default:
      return Key::Other;
  }
}

QString oauthPage(const QString& heading, const QString& detail) {
  return QStringLiteral("<!doctype html><meta charset=\"utf-8\"><title>Contacts</title>"
                        "<body style=\"margin:48px;font-family:monospace;background:#1a1b26;color:#c0caf5\">"
                        "<p>%1</p><p style=\"color:#a9b1d6\">%2</p>")
      .arg(heading.toHtmlEscaped(), detail.toHtmlEscaped());
}

void clearLayout(QLayout* layout) {
  while (QLayoutItem* item = layout->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      widget->hide();
      widget->deleteLater();
    }
    if (QLayout* child = item->layout()) clearLayout(child);
    delete item;
  }
}

qint64 nowUnix() { return QDateTime::currentSecsSinceEpoch(); }

}  // namespace

MainWindow::MainWindow() : secrets_(makeSecretStore()) {
  setWindowTitle(QStringLiteral("Contacts"));
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  resize(980, 680);
  setMinimumSize(480, 560);
  themePaths_ = ThemePaths::forHome(QDir::homePath());
  paths_ = AppPaths::forEnv(QDir::homePath(), qEnvironmentVariable("XDG_CONFIG_HOME"), qEnvironmentVariable("XDG_DATA_HOME"),
                            qEnvironmentVariable("XDG_CACHE_HOME"));
  migrateLegacyPaths(paths_);
  photos_ = new PhotoStore(paths_.photoDir, this);
  connect(photos_, &PhotoStore::updated, this, [this](const QString& id) {
    if (list_) list_->viewport()->update();
    if (detailOpen_ && !editing_ && selected() && selected()->resourceName == id) renderDetail();
    if (editing_ && editor_ != nullptr && selected() && selected()->resourceName == id && photos_ != nullptr) {
      editor_->setPreview(photos_->imageFor(id, selected()->photoUrl, 160, devicePixelRatioF()));
    }
  });
  buildUi();
  loadLocal();
  reloadTheme();
  refilter();
  qApp->installEventFilter(this);
  if (!qEnvironmentVariableIsSet("OMACONTACTS_NO_SYNC") && !tokens_.refreshToken.isEmpty()) {
    QTimer::singleShot(0, this, [this] { runSync(false); });
  }
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
  auto* title = new QLabel(QStringLiteral("Contacts"));
  title->setObjectName(QStringLiteral("title"));
  header->addWidget(title);
  header->addStretch(1);
  menuButton_ = kindButton(QStringLiteral("Menu"), QStringLiteral("outline"));
  menu_ = new QMenu(menuButton_);
  newAction_ = menu_->addAction(QStringLiteral("New contact"));
  syncAction_ = menu_->addAction(QStringLiteral("Sync"));
  menu_->addSeparator();
  QAction* keybindingsAction = menu_->addAction(QStringLiteral("Keybindings"));
  QAction* accountAction = menu_->addAction(QStringLiteral("Account"));
  header->addWidget(menuButton_);
  root->addLayout(header);

  search_ = new QLineEdit;
  search_->setPlaceholderText(QStringLiteral("Find a contact"));
  search_->setClearButtonEnabled(true);
  search_->setAccessibleName(QStringLiteral("Find a contact"));
  root->addWidget(search_);

  splitLayout_ = new QVBoxLayout;
  splitLayout_->setSpacing(14);
  listPane_ = new QWidget;
  auto* listLayout = new QVBoxLayout(listPane_);
  listLayout->setContentsMargins(0, 0, 0, 0);
  listLayout->setSpacing(8);
  model_ = new ContactListModel;
  delegate_ = new ContactDelegate;
  delegate_->photos = photos_;
  list_ = new QuietListView;
  list_->setModel(model_);
  list_->setItemDelegate(delegate_);
  list_->setUniformItemSizes(true);
  list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  list_->setMouseTracking(true);
  list_->viewport()->setMouseTracking(true);
  list_->setMinimumWidth(0);
  list_->setMinimumHeight(0);
  list_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  listLayout->addWidget(list_, 1);
  emptyLabel_ = new QLabel;
  emptyLabel_->setObjectName(QStringLiteral("secondary"));
  emptyLabel_->setWordWrap(true);
  listLayout->addWidget(emptyLabel_);

  rule_ = new QFrame;
  rule_->setObjectName(QStringLiteral("hairline"));
  rule_->setFixedHeight(1);
  rule_->hide();

  detailPane_ = new QWidget;
  detailPane_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  detailLayout_ = new QVBoxLayout(detailPane_);
  detailLayout_->setContentsMargins(0, 0, 0, 0);
  detailLayout_->setSpacing(8);
  headerPane_ = new QWidget;
  headerPane_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  headerLayout_ = new QVBoxLayout(headerPane_);
  headerLayout_->setContentsMargins(0, 0, 0, 0);
  headerLayout_->setSpacing(8);
  fieldModel_ = new QStandardItemModel(this);
  fieldDelegate_ = new FieldDelegate;
  fieldList_ = new QuietListView;
  fieldList_->setModel(fieldModel_);
  fieldList_->setItemDelegate(fieldDelegate_);
  fieldList_->setUniformItemSizes(false);
  fieldList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  fieldList_->setSelectionMode(QAbstractItemView::SingleSelection);
  fieldList_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  fieldList_->setMouseTracking(true);
  fieldList_->viewport()->setMouseTracking(true);
  fieldList_->setMinimumWidth(0);
  fieldList_->setMinimumHeight(0);
  fieldList_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  detailLayout_->addWidget(headerPane_);
  detailLayout_->addWidget(fieldList_, 1);
  listPane_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  listPane_->setMinimumHeight(0);
  detailPane_->hide();

  splitLayout_->addWidget(listPane_, 1);
  splitLayout_->addWidget(detailPane_, 1);
  root->addLayout(splitLayout_, 1);

  auto* footer = new QHBoxLayout;
  countLabel_ = new QLabel;
  countLabel_->setObjectName(QStringLiteral("status"));
  statusLabel_ = new QLabel;
  statusLabel_->setObjectName(QStringLiteral("status"));
  footer->addWidget(countLabel_);
  footer->addSpacing(16);
  footer->addWidget(statusLabel_);
  footer->addStretch(1);
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

  focusSyncTimer_ = new QTimer(this);
  focusSyncTimer_->setInterval(5 * 60 * 1000);
  connect(focusSyncTimer_, &QTimer::timeout, this, [this] {
    if (isActiveWindow()) runSync(false, false);
  });

  connect(search_, &QLineEdit::textChanged, this, [this] {
    if (detailOpen_) closeDetail();
    refilter();
  });
  connect(list_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] {
    if (detailOpen_) renderDetail();
  });
  connect(list_, &QListView::activated, this, [this] { openSelected(); });
  connect(fieldList_, &QListView::activated, this, [this] { activateField(); });
  connect(menuButton_, &QPushButton::clicked, this, [this] {
    const QSize menuSize = menu_->sizeHint();
    const QPoint pos = menuButton_->mapToGlobal(QPoint(menuButton_->width() - menuSize.width(), menuButton_->height()));
    menu_->popup(pos);
  });
  connect(newAction_, &QAction::triggered, this, [this] { newContact(); });
  connect(syncAction_, &QAction::triggered, this, [this] { runSync(false); });
  connect(keybindingsAction, &QAction::triggered, this, [this] { showHelpDialog(this); });
  connect(accountAction, &QAction::triggered, this, [this] { openAccount(); });
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
  if (photos_) photos_->setAccessToken(tokens_.accessToken);
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
    applyView();
    search_->setFocus();
  }
}

void MainWindow::resizeEvent(QResizeEvent* event) { QMainWindow::resizeEvent(event); }

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
  if (event->type() == QEvent::KeyPress && delegate_ != nullptr && list_ != nullptr && list_->isVisible()) {
    delegate_->suppressHover = true;
    list_->viewport()->update();
  }
  if (event->type() == QEvent::MouseMove && list_ != nullptr && watched == list_->viewport() && delegate_ != nullptr &&
      delegate_->suppressHover) {
    delegate_->suppressHover = false;
    list_->viewport()->update();
  }
  if (watched == search_ && event->type() == QEvent::FocusIn && detailOpen_ && !editing_) {
    closeDetail();
    return false;
  }
  if (event->type() == QEvent::WindowDeactivate && fieldDelegate_ != nullptr && fieldDelegate_->controlHeld) {
    fieldDelegate_->controlHeld = false;
    if (fieldList_ != nullptr) fieldList_->viewport()->update();
  }
  if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) && fieldDelegate_ != nullptr &&
      fieldList_ != nullptr && fieldList_->isVisible()) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Control && !key->isAutoRepeat()) {
      QTimer::singleShot(0, this, [this] {
        if (fieldDelegate_ == nullptr || fieldList_ == nullptr) return;
        const bool held = QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ControlModifier);
        if (fieldDelegate_->controlHeld == held) return;
        fieldDelegate_->controlHeld = held;
        fieldList_->viewport()->update();
      });
    }
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
  mapped.multiline = qobject_cast<QPlainTextEdit*>(focus) != nullptr || qobject_cast<QTextEdit*>(focus) != nullptr;
  mapped.searchEmpty = search_->text().isEmpty();
  mapped.detailOpen = detailOpen_;
  mapped.surface = editing_ ? Surface::Editor : Surface::Browser;
  const Command command = routeKey(mapped);
  if (command == Command::Ignore) return QMainWindow::eventFilter(watched, event);
  if (editing_ && command == Command::Cancel) {
    if (QWidget* popup = QApplication::activePopupWidget()) {
      popup->close();
      return true;
    }
    closeEditor();
    return true;
  }
  if (editing_ && command == Command::Save) {
    if (QApplication::activePopupWidget() != nullptr) return QMainWindow::eventFilter(watched, event);
    commitEditor();
    return true;
  }
  handleCommand(command);
  return true;
}

void MainWindow::applyPalette(const Palette& palette) {
  palette_ = palette;
  havePalette_ = true;
  if (delegate_) delegate_->palette = palette;
  if (fieldDelegate_) fieldDelegate_->palette = palette;
  qApp->setStyleSheet(stylesheetFor(palette));
  const auto buttons = findChildren<QPushButton*>();
  for (auto* button : buttons) {
    button->style()->unpolish(button);
    button->style()->polish(button);
  }
  if (list_) list_->viewport()->update();
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
    rows.append({hit.contact->resourceName, hit.contact->displayName(), hit.contact->subtitle(), hit.contact->photoUrl,
                 hit.contact->initials()});
  }
  model_->setRows(std::move(rows));
  if (model_->rowCount() > 0) list_->setCurrentIndex(model_->index(found ? select : 0, 0));
  else if (!editing_) detailOpen_ = false;
  if (!editing_ && detailOpen_ && selected() == nullptr) detailOpen_ = false;
  applyView();
  updateChrome();
}

void MainWindow::renderDetail() {
  if (renderingDetail_ || headerLayout_ == nullptr || fieldModel_ == nullptr) return;
  renderingDetail_ = true;
  const int keepRow = fieldList_ != nullptr && fieldList_->currentIndex().isValid() ? fieldList_->currentIndex().row() : 0;
  fieldModel_->clear();
  if (headerPane_ != nullptr) headerPane_->hide();
  const Contact* contact = selected();
  if (contact == nullptr) {
    renderingDetail_ = false;
    return;
  }
  const Contact shown = *contact;

  auto addField = [this](const QString& label, const QString& value, FieldAction action, bool email = false,
                         std::optional<EditorFocus> focus = std::nullopt, bool keepLabelColumn = false) {
    const bool textRow = action == FieldAction::Copy;
    if (textRow && value.trimmed().isEmpty()) return;
    auto* item = new QStandardItem(value);
    item->setEditable(false);
    item->setData(label, Qt::UserRole);
    item->setData(keepLabelColumn, Qt::UserRole + 6);
    const QString actionText = action == FieldAction::Edit             ? QStringLiteral("Edit")
                               : action == FieldAction::DeleteContact   ? QStringLiteral("Delete")
                               : action == FieldAction::Copy             ? QStringLiteral("Copy")
                               : action == FieldAction::CopyContact      ? QStringLiteral("Copy")
                                                                         : QString();
    item->setData(actionText, Qt::UserRole + 1);
    item->setData(static_cast<int>(action), Qt::UserRole + 2);
    item->setData(email, Qt::UserRole + 3);
    if (focus) {
      item->setData(static_cast<int>(focus->field), Qt::UserRole + 4);
      item->setData(focus->index, Qt::UserRole + 5);
    }
    fieldModel_->appendRow(item);
  };
  if (!shown.photoUrl.isEmpty()) {
    const QPixmap thumb = photos_ != nullptr ? photos_->imageFor(shown.resourceName, shown.photoUrl, 160, devicePixelRatioF()) : QPixmap();
    if (!thumb.isNull()) {
      auto* item = new QStandardItem;
      item->setEditable(false);
      item->setData(QString(), Qt::UserRole);
      item->setData(QString(), Qt::UserRole + 1);
      item->setData(static_cast<int>(FieldAction::CopyPhoto), Qt::UserRole + 2);
      item->setData(false, Qt::UserRole + 3);
      item->setData(thumb, Qt::DecorationRole);
      fieldModel_->appendRow(item);
    }
  }
  const bool hasPersonalName = !shown.givenName.trimmed().isEmpty() || !shown.familyName.trimmed().isEmpty();
  if (hasPersonalName) {
    addField(QStringLiteral("Full name"), shown.displayName(), FieldAction::Copy, false, EditorFocus{EditorField::Given, 0});
  }
  addField(QStringLiteral("Prefix"), shown.prefix, FieldAction::Copy, false, EditorFocus{EditorField::Prefix, 0});
  addField(QStringLiteral("First name"), shown.givenName, FieldAction::Copy, false, EditorFocus{EditorField::Given, 0});
  addField(QStringLiteral("Middle name"), shown.middleName, FieldAction::Copy, false, EditorFocus{EditorField::Middle, 0});
  addField(QStringLiteral("Last name"), shown.familyName, FieldAction::Copy, false, EditorFocus{EditorField::Family, 0});
  addField(QStringLiteral("Suffix"), shown.suffix, FieldAction::Copy, false, EditorFocus{EditorField::Suffix, 0});
  addField(QStringLiteral("Nickname"), shown.nickname, FieldAction::Copy, false, EditorFocus{EditorField::Nickname, 0});
  addField(QStringLiteral("File as"), shown.fileAs, FieldAction::Copy);
  addField(QStringLiteral("Phonetic first name"), shown.phoneticGiven, FieldAction::Copy);
  addField(QStringLiteral("Phonetic middle name"), shown.phoneticMiddle, FieldAction::Copy);
  addField(QStringLiteral("Phonetic last name"), shown.phoneticFamily, FieldAction::Copy);
  addField(QStringLiteral("Company"), shown.organization, FieldAction::Copy, false, EditorFocus{EditorField::Organization, 0});
  addField(QStringLiteral("Job title"), shown.title, FieldAction::Copy, false, EditorFocus{EditorField::Title, 0});
  addField(QStringLiteral("Department"), shown.department, FieldAction::Copy, false, EditorFocus{EditorField::Department, 0});
  for (int i = 0; i < shown.otherOrganizations.size(); ++i) {
    const auto& org = shown.otherOrganizations.at(i);
    addField(QStringLiteral("Company"), org.name, FieldAction::Copy, false, EditorFocus{EditorField::OtherOrganization, i});
    addField(QStringLiteral("Job title"), org.title, FieldAction::Copy, false, EditorFocus{EditorField::OtherOrganization, i});
    addField(QStringLiteral("Department"), org.department, FieldAction::Copy, false, EditorFocus{EditorField::OtherOrganization, i});
  }
  addField(QStringLiteral("Occupation"), shown.occupation, FieldAction::Copy, false, EditorFocus{EditorField::Occupation, 0});
  for (int i = 0; i < shown.emails.size(); ++i) {
    const auto& email = shown.emails.at(i);
    const QString emailLabel = labelDisplay(LabelField::Email, email);
    addField(emailLabel, email.value, FieldAction::Copy, true, EditorFocus{EditorField::Email, i}, emailLabel.isEmpty());
  }
  for (int i = 0; i < shown.phones.size(); ++i) {
    const auto& phone = shown.phones.at(i);
    const QString phoneLabel = labelDisplay(LabelField::Phone, phone);
    addField(phoneLabel, phone.value, FieldAction::Copy, false, EditorFocus{EditorField::Phone, i}, phoneLabel.isEmpty());
  }
  for (int i = 0; i < shown.addresses.size(); ++i) {
    const auto& address = shown.addresses.at(i);
    LabeledValue addressKind;
    addressKind.type = address.type;
    addressKind.label = address.label;
    const QString addressLabel = labelDisplay(LabelField::Address, addressKind);
    const QString addressHeading = addressLabel.isEmpty() ? QStringLiteral("Address") : addressLabel;
    addField(addressHeading, address.display(), FieldAction::Copy, false, EditorFocus{EditorField::Address, i});
  }
  addField(QStringLiteral("Birthday"), shown.birthday.display(), FieldAction::Copy, false, EditorFocus{EditorField::Birthday, 0});
  for (int i = 0; i < shown.urls.size(); ++i) {
    const auto& url = shown.urls.at(i);
    const QString urlLabel = labelDisplay(LabelField::Other, url);
    addField(urlLabel.isEmpty() ? QStringLiteral("Website") : urlLabel, url.value, FieldAction::Copy, false, EditorFocus{EditorField::Url, i});
  }
  for (int i = 0; i < shown.relations.size(); ++i) {
    const auto& relation = shown.relations.at(i);
    const QString relationLabel = labelDisplay(LabelField::Other, relation);
    addField(relationLabel.isEmpty() ? QStringLiteral("Related") : relationLabel, relation.value, FieldAction::Copy, false,
             EditorFocus{EditorField::Relation, i});
  }
  for (int i = 0; i < shown.events.size(); ++i) {
    const auto& event = shown.events.at(i);
    const QString eventLabel = event.label.trimmed().isEmpty() ? QStringLiteral("Significant date") : event.label;
    addField(eventLabel, event.date.display(), FieldAction::Copy, false, EditorFocus{EditorField::Event, i});
  }
  for (int i = 0; i < shown.ims.size(); ++i) {
    addField(QStringLiteral("Instant message"), shown.ims.at(i).display(), FieldAction::Copy);
  }
  for (int i = 0; i < shown.sips.size(); ++i) {
    addField(QStringLiteral("SIP"), shown.sips.at(i).value, FieldAction::Copy);
  }
  for (int i = 0; i < shown.customs.size(); ++i) {
    const auto& custom = shown.customs.at(i);
    const QString customLabel = custom.label.trimmed().isEmpty() ? QStringLiteral("Custom") : custom.label;
    addField(customLabel, custom.value, FieldAction::Copy, false, EditorFocus{EditorField::Custom, i});
  }
  addField(QStringLiteral("Notes"), shown.notes, FieldAction::Copy, false, EditorFocus{EditorField::Notes, 0});
  addField({}, QStringLiteral("Copy this contact"), FieldAction::CopyContact);
  addField({}, QStringLiteral("Edit this contact"), FieldAction::Edit);
  addField({}, QStringLiteral("Delete this contact"), FieldAction::DeleteContact);

  if (fieldList_ != nullptr && fieldModel_->rowCount() > 0) {
    const int row = std::clamp(keepRow, 0, fieldModel_->rowCount() - 1);
    const QModelIndex index = fieldModel_->index(row, 0);
    fieldList_->setCurrentIndex(index);
    fieldList_->scrollTo(index);
  }
  renderingDetail_ = false;
}

void MainWindow::updateChrome() {
  const int shown = model_ ? model_->rowCount() : 0;
  const int total = index_.size();
  if (detailOpen_) countLabel_->clear();
  else if (search_ && !search_->text().isEmpty()) countLabel_->setText(QStringLiteral("%1 of %2").arg(shown).arg(total));
  else countLabel_->setText(QStringLiteral("%1 contacts").arg(total));
  if (busy_) statusLabel_->setText(QStringLiteral("Syncing…"));
  else if (saving_) statusLabel_->setText(QStringLiteral("Saving to Google"));
  else if (tokens_.refreshToken.isEmpty()) statusLabel_->setText(QStringLiteral("Not signed in"));
  else statusLabel_->clear();
  if (syncAction_) syncAction_->setEnabled(!busy_);
  if (newAction_) newAction_->setEnabled(!busy_);
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
      if (detailOpen_) moveField(-100000, false);
      else if (model_->rowCount() > 0) list_->setCurrentIndex(model_->index(0, 0));
      break;
    case Command::MoveEnd:
      if (detailOpen_) moveField(100000, false);
      else if (model_->rowCount() > 0) list_->setCurrentIndex(model_->index(model_->rowCount() - 1, 0));
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
      closeDetail();
      break;
    case Command::OpenContact:
      openSelected();
      break;
    case Command::ActivateField:
      activateField();
      break;
    case Command::CopyField:
      copyCurrentField();
      break;
    case Command::CopyContact:
      copyWholeContact();
      break;
    case Command::SendEmail:
      sendSelectedEmail();
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

void MainWindow::moveField(int delta, bool wrap) {
  if (fieldList_ == nullptr || fieldModel_ == nullptr) return;
  const int count = fieldModel_->rowCount();
  if (count <= 0) return;
  const int current = fieldList_->currentIndex().isValid() ? fieldList_->currentIndex().row() : 0;
  const int next = moveIndex(current, count, delta, wrap);
  const QModelIndex index = fieldModel_->index(next, 0);
  fieldList_->setCurrentIndex(index);
  fieldList_->scrollTo(index);
}

void MainWindow::activateField() {
  if (fieldModel_ == nullptr || fieldList_ == nullptr || !fieldList_->currentIndex().isValid()) return;
  const QModelIndex index = fieldList_->currentIndex();
  const auto action = static_cast<FieldAction>(index.data(Qt::UserRole + 2).toInt());
  if (action == FieldAction::DeleteContact) deleteSelected();
  else if (action == FieldAction::Edit) editSelected();
  else if (action == FieldAction::CopyContact) copyWholeContact();
  else if (action == FieldAction::ChangePhoto) changeSelectedPhoto(false);
  else if (action == FieldAction::RemovePhoto) changeSelectedPhoto(true);
  else copyCurrentField();
}

void MainWindow::sendSelectedEmail() {
  if (fieldList_ == nullptr || !fieldList_->currentIndex().isValid()) {
    toast(QStringLiteral("Select an email address first."), true);
    return;
  }
  const QModelIndex index = fieldList_->currentIndex();
  if (!index.data(Qt::UserRole + 3).toBool()) {
    toast(QStringLiteral("Select an email address first."), true);
    return;
  }
  sendMailTo(index.data(Qt::DisplayRole).toString());
}

void MainWindow::copyCurrentField() {
  if (fieldModel_ == nullptr || fieldList_ == nullptr || !fieldList_->currentIndex().isValid()) return;
  const QModelIndex index = fieldList_->currentIndex();
  const auto action = static_cast<FieldAction>(index.data(Qt::UserRole + 2).toInt());
  if (action != FieldAction::Copy && action != FieldAction::CopyPhoto) return;
  if (action == FieldAction::CopyPhoto) {
    const Contact* contact = selected();
    const QImage image = contact == nullptr || photos_ == nullptr ? QImage() : photos_->originalImage(contact->resourceName, contact->photoUrl);
    if (image.isNull()) {
      toast(QStringLiteral("Photo isn’t available yet."), true);
      return;
    }
    QGuiApplication::clipboard()->setImage(image);
    toast(QStringLiteral("Copied photo"), false);
    return;
  }
  const QString value = index.data(Qt::DisplayRole).toString().trimmed();
  if (value.isEmpty()) return;
  QGuiApplication::clipboard()->setText(value);
  toast(QStringLiteral("Copied"), false);
}

void MainWindow::sendMailTo(const QString& address) {
  Contact one;
  const Contact* contact = selected();
  one.givenName = contact != nullptr ? contact->displayName() : address;
  LabeledValue email;
  email.value = address;
  one.emails.append(email);
  const auto command = mailCommand(one);
  if (!command.ok()) {
    toast(command.error, true);
    return;
  }
  QProcess::startDetached(command.argv.first(), command.argv.mid(1));
  toast(QStringLiteral("Opening email…"), false);
}

void MainWindow::moveSelection(int delta, bool wrap) {
  if (detailOpen_) {
    moveField(delta, wrap);
    return;
  }
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
  if (contact == nullptr || busy_ || editing_) return;
  EditorFocus focus;
  if (detailOpen_ && fieldList_ != nullptr && fieldList_->currentIndex().isValid()) {
    const QModelIndex index = fieldList_->currentIndex();
    const QVariant kind = index.data(Qt::UserRole + 4);
    if (kind.isValid()) {
      focus.field = static_cast<EditorField>(kind.toInt());
      focus.index = index.data(Qt::UserRole + 5).toInt();
    }
  }
  showEditor(*contact, focus, false);
}

void MainWindow::newContact() {
  if (busy_ || editing_) return;
  showEditor(Contact{}, {}, true);
}

void MainWindow::showEditor(const Contact& contact, EditorFocus focus, bool isNew) {
  editorRestoreDetail_ = detailOpen_ && !isNew;
  editing_ = true;
  editingNew_ = isNew;
  if (editor_ != nullptr) {
    detailLayout_->removeWidget(editor_);
    editor_->deleteLater();
    editor_ = nullptr;
  }
  editor_ = new ContactEditor(contact, focus, detailPane_);
  editor_->setOnSave([this] { commitEditor(); });
  editor_->setOnCancel([this] { closeEditor(); });
  detailLayout_->addWidget(editor_, 1);
  if (!contact.photoUrl.isEmpty() && photos_ != nullptr) {
    editor_->setPreview(photos_->imageFor(contact.resourceName, contact.photoUrl, 160, devicePixelRatioF()));
  }
  detailOpen_ = true;
  applyView();
  editor_->focusField();
  QTimer::singleShot(0, editor_, [editor = editor_] {
    if (editor != nullptr) editor->focusField();
  });
}

void MainWindow::closeEditor() {
  const bool restoreDetail = editorRestoreDetail_;
  editing_ = false;
  editingNew_ = false;
  editorRestoreDetail_ = false;
  if (editor_ != nullptr) {
    detailLayout_->removeWidget(editor_);
    editor_->deleteLater();
    editor_ = nullptr;
  }
  if (restoreDetail && selected() != nullptr) {
    detailOpen_ = true;
    applyView();
    renderDetail();
    if (fieldList_ != nullptr) fieldList_->setFocus();
    return;
  }
  closeDetail();
}

void MainWindow::commitEditor() {
  if (editor_ == nullptr) return;
  const auto contact = editor_->validatedContact();
  if (!contact) return;
  const QByteArray photo = editor_->photoBytes();
  const bool remove = editor_->removePhoto();
  const bool isNew = editingNew_;
  closeEditor();
  runSave(*contact, isNew, photo, remove);
}

void MainWindow::copyWholeContact() {
  const Contact* contact = selected();
  if (contact == nullptr) return;
  const QString text = contactPlainText(*contact);
  if (text.isEmpty()) return;
  QGuiApplication::clipboard()->setText(text);
  toast(QStringLiteral("Copied name, addresses and phones."), false);
}

void MainWindow::changeSelectedPhoto(bool remove) {
  const Contact* contact = selected();
  if (contact == nullptr || contact->resourceName.isEmpty() || busy_ || saving_ || editing_) return;
  QByteArray bytes;
  if (!remove) {
    const std::optional<QByteArray> chosen = choosePhotoBytes(this);
    if (!chosen) return;
    if (chosen->isEmpty()) {
      toast(QStringLiteral("Could not read that image."), true);
      return;
    }
    bytes = *chosen;
  }
  runPhoto(*contact, bytes, remove);
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
                       QStringLiteral("Contacts forgets the local copy. People stay in the Google account."),
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
  if (photos_) photos_->setAccessToken(tokens_.accessToken);
  QString error;
  if (!secrets_->save(QString::fromUtf8(writeTokensJson(tokens_)), error)) toast(error, true);
}

void MainWindow::setContacts(QList<Contact> contacts) {
  index_.replaceAll(std::move(contacts));
  refilter();
}

void MainWindow::present() {
  show();
  raise();
  activateWindow();
}

int MainWindow::detailPaneHeight() const { return detailPane_ == nullptr ? 0 : detailPane_->height(); }

bool MainWindow::detailShows(const QString& text) const {
  if (!detailOpen_ || fieldList_ == nullptr || !fieldList_->isVisible() || fieldModel_ == nullptr) return false;
  for (int i = 0; i < fieldModel_->rowCount(); ++i) {
    const QModelIndex index = fieldModel_->index(i, 0);
    const QString value = index.data(Qt::DisplayRole).toString();
    const QString label = index.data(Qt::UserRole).toString();
    if (!value.contains(text) && !label.contains(text)) continue;
    QRect row = fieldList_->visualRect(index);
    if (row.height() < 1) {
      const int scrolled = fieldList_->verticalScrollBar() != nullptr ? fieldList_->verticalScrollBar()->value() : 0;
      row = QRect(0, i * 28 - scrolled, std::max(1, fieldList_->viewport()->width()), 28);
    }
    const int viewport = std::max(fieldList_->viewport()->height(), fieldList_->height());
    if (viewport < 1) return true;
    if (row.bottom() < 0 || row.top() >= viewport) continue;
    return true;
  }
  return false;
}

bool MainWindow::detailIsOnScreen() const {
  if (detailPane_ == nullptr || !detailPane_->isVisible() || detailPane_->height() < 120) return false;
  const QPoint topLeft = detailPane_->mapTo(this, QPoint(0, 0));
  return topLeft.y() >= 0 && topLeft.y() + detailPane_->height() <= height();
}

QString MainWindow::detailPlainText() const {
  QStringList parts;
  if (detailName_ != nullptr) parts.append(detailName_->text());
  if (fieldModel_ == nullptr) return parts.join(QStringLiteral(" | "));
  for (int i = 0; i < fieldModel_->rowCount(); ++i) {
    const QModelIndex index = fieldModel_->index(i, 0);
    parts.append(index.data(Qt::UserRole).toString());
    parts.append(index.data(Qt::DisplayRole).toString());
  }
  return parts.join(QStringLiteral(" | "));
}

void MainWindow::openSelected() {
  if (selected() == nullptr) return;
  detailOpen_ = true;
  applyView();
  renderDetail();
  if (fieldList_ != nullptr) fieldList_->setFocus();
}

void MainWindow::closeDetail() {
  detailOpen_ = false;
  applyView();
  if (search_ != nullptr) search_->setFocus();
}

void MainWindow::applyView() {
  if (listPane_ == nullptr || detailPane_ == nullptr) return;
  if (detailOpen_ && selected() == nullptr) detailOpen_ = false;
  listPane_->setMinimumWidth(0);
  listPane_->setMaximumWidth(QWIDGETSIZE_MAX);
  listPane_->setMinimumHeight(0);
  listPane_->setMaximumHeight(QWIDGETSIZE_MAX);
  listPane_->setVisible(!detailOpen_);
  detailPane_->setVisible(detailOpen_);
  if (fieldList_ != nullptr) fieldList_->setVisible(detailOpen_ && !editing_);
  if (editor_ != nullptr) editor_->setVisible(editing_);
  if (search_ != nullptr) search_->setVisible(!detailOpen_);
  if (rule_ != nullptr) rule_->setVisible(false);
  updateChrome();
}

void MainWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  if (event->type() != QEvent::ActivationChange) return;
  const bool active = isActiveWindow();
  if (active == windowActive_) return;
  windowActive_ = active;
  if (!active) {
    if (focusSyncTimer_ != nullptr) focusSyncTimer_->stop();
    return;
  }
  if (qEnvironmentVariableIsSet("OMACONTACTS_NO_SYNC")) return;
  if (lastSyncAt_ == 0 || nowUnix() - lastSyncAt_ >= 60) runSync(false, false);
  if (focusSyncTimer_ != nullptr) focusSyncTimer_->start();
}

void MainWindow::runSync(bool forceFull, bool announce) {
  if (!announce && qEnvironmentVariableIsSet("OMACONTACTS_NO_SYNC")) return;
  if (busy_) return;
  if (tokens_.refreshToken.isEmpty() || config_.clientId.isEmpty()) {
    if (announce) toast(QStringLiteral("Connect a Google account first."), true);
    return;
  }
  lastSyncAt_ = nowUnix();
  setBusy(true);
  const auto client = OAuthClient{config_.clientId, config_.clientSecret};
  auto tokens = tokens_;
  const QString syncToken = forceFull ? QString() : syncToken_;
  const QList<Contact> current = index_.all();
  runInBackground([this, client, tokens, syncToken, current, announce]() mutable {
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
        [this, error, contacts = std::move(contacts), nextToken, tokens, announce]() mutable {
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
          if (saving_) {
            setBusy(false);
            return;
          }
          if (!error.isEmpty()) {
            toast(error, true);
            return;
          }
          index_.replaceAll(std::move(contacts));
          syncToken_ = nextToken;
          saveCache();
          refilter();
          if (announce) toast(QStringLiteral("Synced %1 contacts").arg(index_.size()), false);
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::runSave(const Contact& contact, bool isNew, const QByteArray& photoBytes, bool removePhoto) {
  if (saving_) return;
  if (tokens_.refreshToken.isEmpty()) {
    toast(QStringLiteral("Connect a Google account before saving."), true);
    return;
  }
  Contact previous;
  bool hadPrevious = false;
  if (!isNew && !contact.resourceName.isEmpty()) {
    if (const Contact* existing = index_.find(contact.resourceName)) {
      previous = *existing;
      hadPrevious = true;
    }
    index_.upsert(contact);
    saveCache();
    refilter();
    selectId(contact.resourceName);
  }
  saving_ = true;
  updateChrome();
  const auto client = OAuthClient{config_.clientId, config_.clientSecret};
  auto tokens = tokens_;
  runInBackground([this, client, tokens, contact, previous, hadPrevious, isNew, photoBytes, removePhoto]() mutable {
    BlockingNam http;
    auto ensured = ensureAccess(http, client, tokens, nowUnix());
    QString error = ensured.ok() ? QString() : ensured.error;
    QString photoError;
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
      else {
        saved = written.contact;
        if (!photoBytes.isEmpty()) {
          auto pictured = updateContactPhoto(http, access, saved.resourceName, photoBytes, refresh);
          tokens.accessToken = access;
          if (!pictured.ok()) photoError = pictured.error;
          else {
            if (!pictured.contact.photoUrl.isEmpty()) saved.photoUrl = pictured.contact.photoUrl;
            if (!pictured.contact.etag.isEmpty()) saved.etag = pictured.contact.etag;
          }
        } else if (removePhoto && !saved.resourceName.isEmpty()) {
          auto cleared = deleteContactPhoto(http, access, saved.resourceName, refresh);
          tokens.accessToken = access;
          if (!cleared.ok()) photoError = cleared.error;
          else {
            saved.photoUrl.clear();
            if (!cleared.contact.etag.isEmpty()) saved.etag = cleared.contact.etag;
          }
        }
      }
    }
    QMetaObject::invokeMethod(
        this,
        [this, error, photoError, saved, tokens, contact, previous, hadPrevious, isNew, photoBytes, removePhoto]() {
          if (closing_) return;
          saving_ = false;
          saveTokens(tokens);
          updateChrome();
          if (!error.isEmpty()) {
            if (hadPrevious) {
              index_.upsert(previous);
              saveCache();
              refilter();
              selectId(previous.resourceName);
            }
            toast(error, true);
            return;
          }
          Contact merged = contact;
          if (!saved.resourceName.isEmpty()) merged.resourceName = saved.resourceName;
          if (!saved.etag.isEmpty()) merged.etag = saved.etag;
          if (merged.photoUrl.isEmpty() && !saved.photoUrl.isEmpty()) merged.photoUrl = saved.photoUrl;
          if (!isNew) {
            if (const Contact* current = index_.find(contact.resourceName)) {
              merged = *current;
              if (!saved.etag.isEmpty()) merged.etag = saved.etag;
              if (!saved.resourceName.isEmpty()) merged.resourceName = saved.resourceName;
            }
          }
          if (removePhoto) merged.photoUrl.clear();
          else if (!photoBytes.isEmpty() && !saved.photoUrl.isEmpty()) merged.photoUrl = saved.photoUrl;
          index_.upsert(merged);
          if (photos_ != nullptr) {
            if (removePhoto) photos_->forget(merged.resourceName);
            else if (!photoBytes.isEmpty() && !merged.photoUrl.isEmpty()) photos_->remember(merged.resourceName, merged.photoUrl, photoBytes);
          }
          saveCache();
          refilter();
          selectId(merged.resourceName);
          if (!photoError.isEmpty()) toast(photoError, true);
          else toast(isNew ? QStringLiteral("Contact added") : QStringLiteral("Contact saved"), false);
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::runPhoto(const Contact& contact, const QByteArray& photoBytes, bool removePhoto) {
  if (saving_ || contact.resourceName.isEmpty()) return;
  if (!removePhoto && photoBytes.isEmpty()) return;
  saving_ = true;
  updateChrome();
  const auto client = OAuthClient{config_.clientId, config_.clientSecret};
  auto tokens = tokens_;
  const QString id = contact.resourceName;
  runInBackground([this, client, tokens, id, photoBytes, removePhoto]() mutable {
    BlockingNam http;
    auto ensured = ensureAccess(http, client, tokens, nowUnix());
    QString error = ensured.ok() ? QString() : ensured.error;
    Contact pictured;
    if (error.isEmpty()) {
      QString access = tokens.accessToken;
      auto refresh = [&]() -> QString {
        auto refreshed = refreshAccess(http, client, tokens, nowUnix());
        if (!refreshed.ok()) return refreshed.error;
        tokens = refreshed.tokens;
        access = tokens.accessToken;
        return {};
      };
      const auto written = removePhoto ? deleteContactPhoto(http, access, id, refresh)
                                       : updateContactPhoto(http, access, id, photoBytes, refresh);
      tokens.accessToken = access;
      if (!written.ok()) error = written.error;
      else pictured = written.contact;
    }
    QMetaObject::invokeMethod(
        this,
        [this, error, pictured, tokens, id, photoBytes, removePhoto]() {
          if (closing_) return;
          saving_ = false;
          saveTokens(tokens);
          updateChrome();
          if (!error.isEmpty()) {
            toast(error, true);
            return;
          }
          if (const Contact* current = index_.find(id)) {
            Contact merged = *current;
            if (removePhoto) merged.photoUrl.clear();
            else if (!pictured.photoUrl.isEmpty()) merged.photoUrl = pictured.photoUrl;
            if (!pictured.etag.isEmpty()) merged.etag = pictured.etag;
            index_.upsert(merged);
            saveCache();
            if (photos_ != nullptr) {
              if (removePhoto) photos_->forget(id);
              else if (!photoBytes.isEmpty() && !merged.photoUrl.isEmpty()) photos_->remember(id, merged.photoUrl, photoBytes);
            }
            refilter();
            selectId(id);
            if (detailOpen_ && !editing_) renderDetail();
          }
          toast(removePhoto ? QStringLiteral("Photo removed") : QStringLiteral("Photo saved"), false);
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::runDelete(const QString& id) {
  if (busy_) return;
  setBusy(true);
  const auto client = OAuthClient{config_.clientId, config_.clientSecret};
  auto tokens = tokens_;
  runInBackground([this, client, tokens, id]() mutable {
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
          detailOpen_ = false;
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
      QString heading = QStringLiteral("Contacts is signed in.");
      QString detail = QStringLiteral("You can close this tab.");
      QString failure;
      if (!query.ok()) failure = query.error;
      else if (query.state != oauthState_) failure = QStringLiteral("The sign-in response did not match this window.");
      if (!failure.isEmpty()) {
        heading = QStringLiteral("Contacts could not sign in.");
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
      runInBackground([this, client, code, redirect, verifier] {
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
