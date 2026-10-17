#include "mainwindow.h"

#include "blockingnam.h"
#include "desktop.h"
#include "dialogs.h"
#include "google.h"
#include "photos.h"
#include "skill.h"
#include "vcard.h"
#include "worker.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QIcon>
#include <QImageReader>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>

namespace contacts {

class ContactListModel : public QAbstractListModel {
 public:
  enum Role { SubtitleRole = Qt::UserRole, IdRole };

  struct Row {
    QString id;
    QString name;
    QString subtitle;
  };

  using QAbstractListModel::QAbstractListModel;

  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }

  QVariant data(const QModelIndex& index, int role) const override {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
    const Row& row = rows_.at(index.row());
    switch (role) {
      case Qt::DisplayRole:
        return row.name;
      case SubtitleRole:
        return row.subtitle;
      case IdRole:
        return row.id;
      case Qt::AccessibleTextRole:
        return row.subtitle.isEmpty() ? row.name : row.name + QStringLiteral(", ") + row.subtitle;
      default:
        return {};
    }
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

namespace {

QColor tint(const QColor& color, int alpha) { return QColor(color.red(), color.green(), color.blue(), alpha); }

// One text row at the shell's row height, never shorter than the font needs.
int rowHeight(const ShellMetrics& metrics, const QFontMetrics& font) {
  return std::max(metrics.space(28), font.lineSpacing() + metrics.space(10));
}

}  // namespace

class ContactDelegate : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  Palette palette;
  ShellMetrics metrics;
  bool suppressHover = false;

  QFont font(const QStyleOptionViewItem& option) const {
    QFont font = option.font;
    font.setPixelSize(metrics.text(12));
    font.setBold(false);
    return font;
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    if (option.state & QStyle::State_Selected) painter->fillRect(option.rect, tint(palette.foreground, 46));
    else if (!suppressHover && (option.state & QStyle::State_MouseOver)) painter->fillRect(option.rect, tint(palette.foreground, 20));
    const QString name = index.data(Qt::DisplayRole).toString();
    const QString subtitle = index.data(ContactListModel::SubtitleRole).toString();
    const QFont nameFont = font(option);
    painter->setFont(nameFont);
    const QFontMetrics fm(nameFont);
    const int pad = metrics.space(8);
    const QRect rect = option.rect.adjusted(pad, 0, -pad, 0);
    const int subtitleWidth = subtitle.isEmpty() ? 0 : std::min(rect.width() / 2, fm.horizontalAdvance(subtitle) + metrics.space(12));
    painter->setPen(palette.foreground);
    painter->drawText(rect.adjusted(0, 0, -subtitleWidth, 0), Qt::AlignVCenter | Qt::AlignLeft,
                      fm.elidedText(name, Qt::ElideRight, std::max(0, rect.width() - subtitleWidth)));
    if (!subtitle.isEmpty()) {
      painter->setPen(palette.secondary);
      painter->drawText(rect.adjusted(rect.width() - subtitleWidth, 0, 0, 0), Qt::AlignVCenter | Qt::AlignRight,
                        fm.elidedText(subtitle, Qt::ElideRight, subtitleWidth));
    }
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
    return QSize(0, rowHeight(metrics, QFontMetrics(font(option))));
  }
};

enum class FieldAction { Copy, CopyPhoto, Edit, DeleteContact, ShareContact };

enum FieldRole {
  LabelRole = Qt::UserRole,
  CueRole,
  ActionRole,
  EmailRole,
  EditorFieldRole,
  EditorIndexRole,
  KeepLabelRole,
};

class FieldDelegate : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;

  Palette palette;
  ShellMetrics metrics;
  bool controlHeld = false;
  bool copyLearned = false;
  bool emailLearned = false;

  int photoEdge() const { return metrics.space(160); }

  QFont font(const QStyleOptionViewItem& option) const {
    QFont font = option.font;
    font.setPixelSize(metrics.text(12));
    return font;
  }

  static bool wraps(const QModelIndex& index) {
    return index.data(EditorFieldRole).toInt() == static_cast<int>(EditorField::Notes) && index.data(EditorFieldRole).isValid();
  }

  static FieldAction action(const QModelIndex& index) { return static_cast<FieldAction>(index.data(ActionRole).toInt()); }

  // One piece of a cue. A key, when there is one, is drawn in the accent
  // color in front of what it does, the way the keybindings list shows it.
  struct CuePart {
    QString key;
    QString text;
  };

  // The cue at the end of a row. The selected row also names its keys until
  // they have been used a few times, so a newcomer learns them in place.
  QList<CuePart> cueFor(const QModelIndex& index, bool selected) const {
    const QString cue = index.data(CueRole).toString();
    if (cue.isEmpty()) return {};
    const bool email = index.data(EmailRole).toBool();
    // Holding Control turns Copy into Send email on an address.
    if (email && controlHeld) return {{QString(), QStringLiteral("Send email")}};
    if (!selected) return {{QString(), cue}};
    QList<CuePart> parts{copyLearned ? CuePart{QString(), cue} : CuePart{QStringLiteral("Enter"), QStringLiteral("to copy")}};
    if (email && !emailLearned) parts.append({QStringLiteral("Ctrl+Enter"), QStringLiteral("to email")});
    return parts;
  }

  int cuePartGap() const { return metrics.space(20); }

  int cueWidth(const QFontMetrics& fm, const QList<CuePart>& parts) const {
    int width = 0;
    for (const CuePart& part : parts) {
      if (width > 0) width += cuePartGap();
      width += fm.horizontalAdvance(part.key.isEmpty() ? part.text : part.key + QLatin1Char(' ') + part.text);
    }
    return width == 0 ? 0 : width + metrics.space(16);
  }

  int labelWidth(const QFontMetrics& fm, int width) const {
    return std::min(width / 3, fm.horizontalAdvance(QStringLiteral("Phonetic first name")) + metrics.space(16));
  }

  int valueColumnWidth(const QFontMetrics& fm, int width, const QModelIndex& index) const {
    const int inner = std::max(0, width - 2 * metrics.space(8));
    const int cue = cueWidth(fm, cueFor(index, true));
    const int label = index.data(LabelRole).toString().isEmpty() ? 0 : labelWidth(fm, inner);
    return std::max(1, inner - label - cue);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    painter->save();
    const FieldAction kind = action(index);
    const bool photoRow = kind == FieldAction::CopyPhoto;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    if (!photoRow && selected) painter->fillRect(option.rect, tint(palette.foreground, 46));
    else if (!photoRow && hover) painter->fillRect(option.rect, tint(palette.foreground, 20));
    const int pad = metrics.space(8);
    const QRect rect = option.rect.adjusted(pad, 0, -pad, 0);
    if (photoRow) {
      const QPixmap photo = index.data(Qt::DecorationRole).value<QPixmap>();
      if (!photo.isNull()) {
        QSize logical = photo.deviceIndependentSize().toSize();
        logical.scale(std::min(photoEdge(), rect.width()), photoEdge(), Qt::KeepAspectRatio);
        const QRect target(rect.left(), rect.center().y() - logical.height() / 2, logical.width(), logical.height());
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
    const QFont textFont = font(option);
    painter->setFont(textFont);
    const QFontMetrics fm(textFont);
    const QString label = index.data(LabelRole).toString();
    const QString value = index.data(Qt::DisplayRole).toString();
    const QString cue = index.data(CueRole).toString();
    const bool danger = kind == FieldAction::DeleteContact;
    const bool wrap = wraps(index);
    const QStringList lines = value.split(QLatin1Char('\n'));
    const int lineHeight = std::max(1, fm.lineSpacing());
    const int single = rowHeight(metrics, fm);
    const int top = std::max(0, (single - lineHeight) / 2);
    const bool several = wrap || lines.size() > 1;
    const int textTop = several ? rect.top() + top : rect.top();
    const int textHeight = several ? lineHeight : rect.height();
    // The cue shows on the selected or hovered row only.
    const bool showCue = !cue.isEmpty() && (selected || hover);
    const QList<CuePart> shownCue = cueFor(index, selected);
    // Wrapped notes keep room for the widest cue, so selecting them does not rewrap.
    const int cueSpace = kind == FieldAction::Copy && wrap ? cueWidth(fm, cueFor(index, true))
                         : showCue                      ? cueWidth(fm, shownCue)
                                                        : 0;
    const int labelColumn = label.isEmpty() && !index.data(KeepLabelRole).toBool() ? 0 : labelWidth(fm, rect.width());
    painter->setPen(palette.secondary);
    if (!label.isEmpty()) {
      painter->drawText(QRect(rect.left(), textTop, labelColumn, textHeight), Qt::AlignVCenter | Qt::AlignLeft,
                        fm.elidedText(label, Qt::ElideRight, labelColumn));
    }
    const int valueLeft = rect.left() + labelColumn;
    const int valueWidth = std::max(0, rect.width() - labelColumn - cueSpace);
    painter->setPen(danger ? palette.danger : palette.foreground);
    if (wrap) {
      painter->drawText(QRect(valueLeft, textTop, valueWidth, std::max(lineHeight, rect.bottom() - textTop - top + 1)),
                        Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, value);
    } else {
      for (int i = 0; i < lines.size(); ++i) {
        painter->drawText(QRect(valueLeft, textTop + i * (several ? lineHeight : 0), valueWidth, textHeight),
                          Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(lines.at(i), Qt::ElideRight, valueWidth));
      }
    }
    if (showCue) {
      // Right-aligned: the last part ends at the row's edge.
      int x = rect.right() + 1 - (cueWidth(fm, shownCue) - metrics.space(16));
      const auto draw = [&](const QString& text, const QColor& color) {
        const int width = fm.horizontalAdvance(text);
        painter->setPen(color);
        painter->drawText(QRect(x, textTop, width, textHeight), Qt::AlignVCenter | Qt::AlignLeft, text);
        x += width;
      };
      for (int i = 0; i < shownCue.size(); ++i) {
        if (i > 0) x += cuePartGap();
        const CuePart& part = shownCue.at(i);
        if (!part.key.isEmpty()) draw(part.key + QLatin1Char(' '), palette.accent);
        draw(part.text, danger ? palette.danger : palette.secondary);
      }
    }
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    if (action(index) == FieldAction::CopyPhoto) return QSize(0, photoEdge() + metrics.space(16));
    const QFontMetrics fm(font(option));
    const int lineHeight = std::max(1, fm.lineSpacing());
    const int single = rowHeight(metrics, fm);
    const int pad = std::max(0, (single - lineHeight) / 2);
    const QString value = index.data(Qt::DisplayRole).toString();
    if (wraps(index)) {
      int width = std::max(option.rect.width(), 0);
      if (const auto* view = qobject_cast<const QAbstractItemView*>(option.widget)) {
        if (view->viewport()->width() > 0) width = view->viewport()->width();
      }
      const QRect bounds = fm.boundingRect(QRect(0, 0, valueColumnWidth(fm, std::max(1, width), index), 100000),
                                           Qt::AlignLeft | Qt::TextWordWrap, value);
      return QSize(0, bounds.height() <= lineHeight ? single : pad + bounds.height() + pad);
    }
    const int lines = static_cast<int>(value.count(QLatin1Char('\n'))) + 1;
    return QSize(0, lines <= 1 ? single : pad + lines * lineHeight + pad);
  }
};

namespace {

QPushButton* kindButton(const QString& text, const QString& kind) {
  auto* button = new QPushButton(text);
  button->setProperty("kind", kind);
  button->setAccessibleName(text);
  button->setCursor(Qt::PointingHandCursor);
  button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  button->setAutoDefault(true);
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
    case Qt::Key_N:
      return Key::N;
    case Qt::Key_E:
      return Key::E;
    case Qt::Key_S:
      return Key::S;
    case Qt::Key_C:
      return Key::C;
    case Qt::Key_J:
      return Key::J;
    case Qt::Key_K:
      return Key::K;
    case Qt::Key_G:
      return Key::G;
    case Qt::Key_Plus:
      return Key::Plus;
    case Qt::Key_Equal:
      return Key::Equal;
    case Qt::Key_Minus:
      return Key::Minus;
    case Qt::Key_0:
      return Key::Zero;
    default:
      return Key::Other;
  }
}

bool printable(const QKeyEvent* key) {
  const QString text = key->text();
  return text.size() == 1 && text.at(0).isPrint() && !text.at(0).isSpace();
}

// The page the browser shows after Google sends it back, in the window's theme.
QString oauthPage(const Palette& palette, const QString& family, const QString& heading, const QString& detail) {
  return QStringLiteral("<!doctype html><meta charset=\"utf-8\"><title>Peeps</title>"
                        "<body style=\"margin:48px;font-family:'%1',monospace;background:%2;color:%3\">"
                        "<p style=\"color:%4;font-weight:700\">%5</p><p>%6</p>")
      .arg(family.toHtmlEscaped(), palette.background.name(), palette.foreground.name(), palette.bright.name(),
           heading.toHtmlEscaped(), detail.toHtmlEscaped());
}

QString copiedNotice(const QModelIndex& index) {
  QString label = index.data(LabelRole).toString().trimmed();
  QString kind;
  if (index.data(EmailRole).toBool()) {
    kind = QStringLiteral("email");
  } else if (index.data(EditorFieldRole).isValid()) {
    switch (static_cast<EditorField>(index.data(EditorFieldRole).toInt())) {
      case EditorField::Phone:
        kind = QStringLiteral("phone");
        break;
      case EditorField::Address:
        kind = QStringLiteral("address");
        break;
      case EditorField::Url:
        kind = QStringLiteral("website");
        break;
      default:
        break;
    }
  }
  // "SIP" stays capitalized. "Work" becomes "work".
  const bool acronym = std::any_of(label.cbegin(), label.cend(), [](QChar c) { return c.isLetter(); }) &&
                       std::none_of(label.cbegin(), label.cend(), [](QChar c) { return c.isLower(); });
  if (!acronym) label = label.toLower();
  if (label.isEmpty()) return kind.isEmpty() ? QStringLiteral("Copied") : QStringLiteral("Copied %1").arg(kind);
  if (!kind.isEmpty() && label.compare(kind, Qt::CaseInsensitive) != 0) return QStringLiteral("Copied %1 %2").arg(label, kind);
  return QStringLiteral("Copied %1").arg(label);
}

qint64 nowUnix() { return QDateTime::currentSecsSinceEpoch(); }

// The footer names the last sync once it is this many seconds old.
constexpr qint64 kStaleAfter = 60 * 60;

// Coming back to the window syncs, unless the last sync was this recent, so
// flipping between windows does not send a stream of requests.
constexpr qint64 kFocusSyncAfter = 10;

// An x in the theme's quiet text color, brighter while pressed.
QIcon clearIcon(const Palette& palette, int size, qreal ratio) {
  QIcon icon;
  const std::pair<QIcon::Mode, QColor> modes[] = {{QIcon::Normal, palette.secondary}, {QIcon::Active, palette.foreground}};
  for (const auto& [mode, color] : modes) {
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, std::max(1.0, size / 10.0));
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    const qreal inset = size * 0.3;
    painter.drawLine(QPointF(inset, inset), QPointF(size - inset, size - inset));
    painter.drawLine(QPointF(size - inset, inset), QPointF(inset, size - inset));
    icon.addPixmap(pixmap, mode);
  }
  return icon;
}

}  // namespace

MainWindow::MainWindow() : secrets_(makeSecretStore()) {
  setWindowTitle(QStringLiteral("Peeps"));
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  resize(980, 680);
  setMinimumSize(480, 560);
  themePaths_ = ThemePaths::forHome(QDir::homePath());
  paths_ = AppPaths::forEnv(QDir::homePath(), qEnvironmentVariable("XDG_CONFIG_HOME"), qEnvironmentVariable("XDG_DATA_HOME"),
                            qEnvironmentVariable("XDG_CACHE_HOME"));
  fontFamily_ = monospaceFamily();
  photos_ = new PhotoStore(paths_.photoDir, this);
  connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this] { ++clipboardChanges_; });
  connect(photos_, &PhotoStore::updated, this, [this](const QString& id) {
    const Contact* contact = selected();
    if (contact == nullptr || contact->resourceName != id) return;
    if (detailOpen_ && !editing_) renderDetail();
    if (editing_ && !editingNew_ && editor_ != nullptr) {
      editor_->setPreview(photos_->imageFor(id, contact->photoUrl, metrics_.space(96), devicePixelRatioF()));
    }
  });
  buildUi();
  loadLocal();
  reloadTheme();
  refilter();
  qApp->installEventFilter(this);
  if (!qEnvironmentVariableIsSet("PEEPS_NO_SYNC") && signedIn()) {
    QTimer::singleShot(0, this, [this] { runSync(false, false); });
  }
}

MainWindow::~MainWindow() {
  if (qApp) qApp->removeEventFilter(this);
}

void MainWindow::buildUi() {
  auto* central = new QWidget;
  rootLayout_ = new QVBoxLayout(central);

  headerLayout_ = new QHBoxLayout;
  // The title doubles as the way back to the list once a contact is open.
  titleButton_ = new QPushButton(QStringLiteral("Peeps"));
  titleButton_->setObjectName(QStringLiteral("title"));
  titleButton_->setFocusPolicy(Qt::NoFocus);
  titleButton_->setAccessibleName(QStringLiteral("Back to the list"));
  connect(titleButton_, &QPushButton::clicked, this, [this] { closeDetail(); });
  headerLayout_->addWidget(titleButton_);
  headerLayout_->addStretch(1);
  menuButton_ = kindButton(QStringLiteral("Menu"), QStringLiteral("outline"));
  menu_ = new QMenu(menuButton_);
  // Text after a tab sits in the menu's shortcut column. The keys themselves
  // are handled in eventFilter().
  newAction_ = menu_->addAction(QStringLiteral("New contact\tCtrl+N"));
  syncAction_ = menu_->addAction(QStringLiteral("Sync now"));
  menu_->addSeparator();
  QAction* keybindingsAction = menu_->addAction(QStringLiteral("Keybindings\t?"));
  QAction* accountAction = menu_->addAction(QStringLiteral("Google account"));
  headerLayout_->addWidget(menuButton_);
  rootLayout_->addLayout(headerLayout_);

  search_ = new QLineEdit;
  search_->setPlaceholderText(QStringLiteral("Find a contact"));
  search_->setAccessibleName(QStringLiteral("Find a contact"));
  // Qt's own clear button has a fixed gray icon. This one is drawn in the theme.
  clearSearchAction_ = search_->addAction(QIcon(), QLineEdit::TrailingPosition);
  clearSearchAction_->setToolTip(QStringLiteral("Clear the search"));
  clearSearchAction_->setVisible(false);
  connect(clearSearchAction_, &QAction::triggered, this, [this] {
    search_->clear();
    search_->setFocus();
  });
  rootLayout_->addWidget(search_);

  listPane_ = new QWidget;
  listLayout_ = new QVBoxLayout(listPane_);
  listLayout_->setContentsMargins(0, 0, 0, 0);
  model_ = new ContactListModel(this);
  delegate_ = new ContactDelegate(this);
  list_ = new QuietListView;
  list_->setModel(model_);
  list_->setItemDelegate(delegate_);
  list_->setUniformItemSizes(true);
  fieldModel_ = new QStandardItemModel(this);
  fieldDelegate_ = new FieldDelegate(this);
  fieldList_ = new QuietListView;
  fieldList_->setModel(fieldModel_);
  fieldList_->setItemDelegate(fieldDelegate_);
  for (QListView* view : {list_, fieldList_}) {
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->setMouseTracking(true);
    view->viewport()->setMouseTracking(true);
    view->setMinimumSize(0, 0);
    view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }
  listLayout_->addWidget(list_, 1);
  emptyLabel_ = new QLabel;
  emptyLabel_->setObjectName(QStringLiteral("secondary"));
  emptyLabel_->setWordWrap(true);
  listLayout_->addWidget(emptyLabel_);

  detailPane_ = new QWidget;
  detailPane_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  detailLayout_ = new QVBoxLayout(detailPane_);
  detailLayout_->setContentsMargins(0, 0, 0, 0);
  detailLayout_->addWidget(fieldList_, 1);
  detailPane_->hide();
  rootLayout_->addWidget(listPane_, 1);
  rootLayout_->addWidget(detailPane_, 1);

  auto* footer = new QHBoxLayout;
  countLabel_ = new QLabel;
  countLabel_->setObjectName(QStringLiteral("status"));
  statusLabel_ = new QLabel;
  statusLabel_->setObjectName(QStringLiteral("status"));
  // Status text is clipped rather than widening the window.
  statusLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  reconnectButton_ = kindButton(QStringLiteral("Reconnect"), QStringLiteral("link"));
  reconnectButton_->setAutoDefault(false);
  reconnectButton_->hide();
  connect(reconnectButton_, &QPushButton::clicked, this, &MainWindow::startOAuth);
  footer->addWidget(countLabel_);
  footer->addSpacing(16);
  footer->addWidget(statusLabel_, 1);
  footer->addWidget(reconnectButton_);
  rootLayout_->addLayout(footer);
  // A copy that turns stale while the window sits open says so.
  syncedTimer_ = new QTimer(this);
  syncedTimer_->setInterval(30'000);
  connect(syncedTimer_, &QTimer::timeout, this, &MainWindow::updateChrome);
  syncedTimer_->start();

  // The toast floats over the bottom of the window, so a notice never pushes
  // the list around.
  toastFrame_ = new QFrame(central);
  toastFrame_->setObjectName(QStringLiteral("toast"));
  auto* toastLayout = new QHBoxLayout(toastFrame_);
  toastLabel_ = new QLabel;
  toastLabel_->setWordWrap(true);
  toastLayout->addWidget(toastLabel_);
  toastFrame_->hide();
  toastFrame_->installEventFilter(this);

  setCentralWidget(central);

  focusSyncTimer_ = new QTimer(this);
  focusSyncTimer_->setInterval(5 * 60 * 1000);
  connect(focusSyncTimer_, &QTimer::timeout, this, [this] {
    if (isActiveWindow()) runSync(false, false);
  });

  connect(search_, &QLineEdit::textChanged, this, [this] {
    clearSearchAction_->setVisible(!search_->text().isEmpty());
    if (detailOpen_) closeDetail();
    refilter();
  });
  connect(list_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] {
    if (detailOpen_) renderDetail();
  });
  // A contact opens, and a field does what its cue says, on one click. The
  // second click of a double click is ignored, so it neither repeats the
  // action nor lands on a field of the contact that just opened.
  const auto firstClick = [this] {
    if (lastRowClick_.isValid() && lastRowClick_.elapsed() < QApplication::doubleClickInterval()) return false;
    lastRowClick_.start();
    return true;
  };
  connect(list_, &QListView::clicked, this, [this, firstClick] {
    if (firstClick()) openSelected();
  });
  connect(fieldList_, &QListView::clicked, this, [this, firstClick] {
    if (firstClick()) activateField();
  });
  connect(menuButton_, &QPushButton::clicked, this, [this] {
    const QSize size = menu_->sizeHint();
    menu_->popup(menuButton_->mapToGlobal(QPoint(menuButton_->width() - size.width(), menuButton_->height())));
  });
  connect(newAction_, &QAction::triggered, this, [this] { newContact(); });
  connect(syncAction_, &QAction::triggered, this, [this] { runSync(false); });
  connect(keybindingsAction, &QAction::triggered, this, [this] { showHelpDialog(this); });
  connect(accountAction, &QAction::triggered, this, [this] { openAccount(); });

  watcher_ = new QFileSystemWatcher(this);
  themeTimer_ = new QTimer(this);
  themeTimer_->setSingleShot(true);
  themeTimer_->setInterval(200);
  connect(themeTimer_, &QTimer::timeout, this, [this] { reloadTheme(); });
  connect(watcher_, &QFileSystemWatcher::fileChanged, this, [this] { scheduleThemeReload(); });
  connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { scheduleThemeReload(); });
  toastTimer_ = new QTimer(this);
  toastTimer_->setSingleShot(true);
  connect(toastTimer_, &QTimer::timeout, toastFrame_, &QWidget::hide);
}

void MainWindow::loadLocal() {
  if (auto bytes = readFile(paths_.settingsFile)) settings_ = readSettingsJson(*bytes);
  fieldDelegate_->copyLearned = settings_.copyLearned();
  fieldDelegate_->emailLearned = settings_.emailLearned();
  QString error;
  if (auto secret = secrets_->load(error)) {
    if (auto tokens = readTokensJson(secret->toUtf8())) tokens_ = *tokens;
  }
  photos_->setAccessToken(tokens_.accessToken);
  if (auto bytes = readFile(paths_.cacheFile)) {
    const auto snapshot = readCacheJson(*bytes);
    if (snapshot.ok()) {
      syncToken_ = snapshot.syncToken;
      syncedAt_ = snapshot.syncedAt;
      cacheSyncedAt_ = snapshot.syncedAt;
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
    linkAgentSkill();
  }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  if (toastFrame_->isVisible()) placeToast();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  closing_ = true;
  if (oauthServer_) oauthServer_->close();
  QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  const QEvent::Type type = event->type();
  if (watched == toastFrame_ && type == QEvent::MouseButtonRelease) {
    toastFrame_->hide();
    return true;
  }
  if (type == QEvent::KeyPress && list_->isVisible() && !delegate_->suppressHover) {
    delegate_->suppressHover = true;
    list_->viewport()->update();
  }
  if (type == QEvent::MouseMove && watched == list_->viewport() && delegate_->suppressHover) {
    delegate_->suppressHover = false;
    list_->viewport()->update();
  }
  if (watched == search_ && type == QEvent::FocusIn && detailOpen_ && !editing_) {
    closeDetail();
    return false;
  }
  // A mouse's back button leaves the contact, as it would in a browser.
  if (type == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button() == Qt::BackButton && detailOpen_ &&
      !editing_ && !QApplication::activeModalWidget()) {
    if (auto* widget = qobject_cast<QWidget*>(watched); widget != nullptr && widget->window() == this) {
      closeDetail();
      return true;
    }
  }
  if (type == QEvent::WindowDeactivate && fieldDelegate_->controlHeld) {
    fieldDelegate_->controlHeld = false;
    fieldList_->viewport()->update();
  }
  if ((type == QEvent::KeyPress || type == QEvent::KeyRelease) && fieldList_->isVisible()) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Control && !key->isAutoRepeat()) {
      // Holding Control turns Copy into Send email on an address.
      QTimer::singleShot(0, this, [this] {
        const bool held = QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ControlModifier);
        if (fieldDelegate_->controlHeld == held) return;
        fieldDelegate_->controlHeld = held;
        fieldList_->viewport()->update();
      });
    }
  }
  if (type != QEvent::KeyPress || QApplication::activeModalWidget()) return QMainWindow::eventFilter(watched, event);
  auto* widget = qobject_cast<QWidget*>(watched);
  if (!widget || (widget != this && !isAncestorOf(widget))) return QMainWindow::eventFilter(watched, event);
  auto* key = static_cast<QKeyEvent*>(event);
  QWidget* focus = QApplication::focusWidget();
  if (qobject_cast<QPushButton*>(focus) &&
      (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Space)) {
    return QMainWindow::eventFilter(watched, event);
  }
  KeyEvent mapped;
  mapped.key = mapKey(key->key());
  mapped.ctrl = key->modifiers() & Qt::ControlModifier;
  mapped.shift = key->modifiers() & Qt::ShiftModifier;
  mapped.alt = key->modifiers() & (Qt::AltModifier | Qt::MetaModifier);
  mapped.printable = printable(key);
  mapped.textInput = qobject_cast<QLineEdit*>(focus) || qobject_cast<QTextEdit*>(focus) || qobject_cast<QPlainTextEdit*>(focus);
  mapped.multiline = qobject_cast<QPlainTextEdit*>(focus) != nullptr || qobject_cast<QTextEdit*>(focus) != nullptr;
  mapped.searchEmpty = search_->text().isEmpty();
  mapped.detailOpen = detailOpen_;
  mapped.surface = editing_ ? Surface::Editor : Surface::Browser;
  const Command command = routeKey(mapped);
  if (command == Command::Ignore) return QMainWindow::eventFilter(watched, event);
  if (QWidget* popup = QApplication::activePopupWidget()) {
    // A label list is open in the editor. Esc closes it, and Enter picks.
    if (command == Command::Cancel) popup->close();
    return command == Command::Cancel;
  }
  if (command == Command::Cancel) requestCloseEditor();
  else if (command == Command::Save) commitEditor();
  else if (command == Command::TypeToSearch) {
    search_->setFocus();
    search_->setText(key->text());
  } else {
    handleCommand(command);
  }
  return true;
}

void MainWindow::applyStyle() {
  setActiveMetrics(metrics_);
  QFont font(fontFamily_);
  font.setPixelSize(metrics_.text(12));
  QApplication::setFont(font);
  qApp->setStyleSheet(stylesheetFor(palette_, metrics_, fontFamily_));
  delegate_->palette = palette_;
  delegate_->metrics = metrics_;
  fieldDelegate_->palette = palette_;
  fieldDelegate_->metrics = metrics_;
  clearSearchAction_->setIcon(clearIcon(palette_, metrics_.text(16), devicePixelRatioF()));
  const int pad = metrics_.space(18);
  rootLayout_->setContentsMargins(pad, pad, pad, pad);
  rootLayout_->setSpacing(metrics_.space(14));
  headerLayout_->setSpacing(metrics_.space(8));
  listLayout_->setSpacing(metrics_.space(8));
  detailLayout_->setSpacing(metrics_.space(8));
  const int toastPad = metrics_.space(12);
  toastFrame_->layout()->setContentsMargins(toastPad, toastPad, toastPad, toastPad);
  styled_ = true;
  // Row heights follow the text size, so the views lay out again.
  refilter();
  if (detailOpen_ && !editing_) renderDetail();
}

void MainWindow::watchThemeFiles() {
  QStringList directories = {themePaths_.stateDir, QFileInfo(themePaths_.stateColors).absolutePath(),
                             QFileInfo(themePaths_.userShell).absolutePath()};
  QStringList files = {themePaths_.stateColors, themePaths_.stateShell, themePaths_.userShell};
  // Omarchy replaces these files instead of writing into them, so a watch on
  // the file alone goes quiet after the first change.
  for (const QString& dir : directories) {
    if (QDir(dir).exists() && !watcher_->directories().contains(dir)) watcher_->addPath(dir);
  }
  for (const QString& file : files) {
    if (QFile::exists(file) && !watcher_->files().contains(file)) watcher_->addPath(file);
  }
}

void MainWindow::scheduleThemeReload() {
  watchThemeFiles();
  themeTimer_->start();
}

void MainWindow::reloadTheme() {
  watchThemeFiles();
  const ThemeLoad load = loadTheme(themePaths_);
  Palette palette = load.palette;
  if (load.origin == ThemeOrigin::Builtin && styled_) {
    // A theme switch can catch colors.toml half written. Keep the old colors.
    const QFileInfo info(themePaths_.stateColors);
    if (info.exists() && info.size() < 40) palette = palette_;
  }
  const ShellMetrics metrics = loadShellMetrics(themePaths_, settings_.textSizeOffset);
  if (styled_ && palette_.sameColors(palette) && palette_.name == palette.name && metrics_ == metrics) return;
  palette_ = palette;
  metrics_ = metrics;
  applyStyle();
}

void MainWindow::refilter() {
  const QString keep = selected() ? selected()->resourceName : QString();
  // A sync that changes contacts redraws the list under someone scrolling it.
  // For the same search, the row at the top stays at the top, and a selection
  // that was on screen stays on screen.
  const QString query = search_->text();
  const QString top = query == listedQuery_ ? model_->idAt(list_->indexAt(QPoint(0, 0)).row()) : QString();
  const bool selectionShown = list_->viewport()->rect().intersects(list_->visualRect(list_->currentIndex()));
  listedQuery_ = query;
  const auto hits = index_.query(query);
  QList<ContactListModel::Row> rows;
  rows.reserve(hits.size());
  int select = 0;
  int topRow = -1;
  for (const auto& hit : hits) {
    if (!keep.isEmpty() && hit.contact->resourceName == keep) select = static_cast<int>(rows.size());
    if (!top.isEmpty() && hit.contact->resourceName == top) topRow = static_cast<int>(rows.size());
    rows.append({hit.contact->resourceName, hit.name, hit.subtitle});
  }
  model_->setRows(std::move(rows));
  if (model_->rowCount() > 0) list_->setCurrentIndex(model_->index(select, 0));
  if (topRow >= 0) {
    list_->scrollTo(model_->index(topRow, 0), QAbstractItemView::PositionAtTop);
    if (selectionShown) list_->scrollTo(list_->currentIndex());
  }
  if (!editing_ && selected() == nullptr) detailOpen_ = false;
  applyView();
}

void MainWindow::renderDetail() {
  if (renderingDetail_) return;
  renderingDetail_ = true;
  const int keepRow = fieldList_->currentIndex().isValid() ? fieldList_->currentIndex().row() : 0;
  fieldModel_->clear();
  const Contact* contact = selected();
  if (contact == nullptr) {
    renderingDetail_ = false;
    return;
  }
  const Contact shown = *contact;

  auto addField = [this](const QString& label, const QString& value, FieldAction action, bool email = false,
                         std::optional<EditorFocus> focus = std::nullopt, bool keepLabelColumn = false) {
    if (action == FieldAction::Copy && value.trimmed().isEmpty()) return;
    auto* item = new QStandardItem(value);
    item->setEditable(false);
    item->setData(label, LabelRole);
    item->setData(keepLabelColumn, KeepLabelRole);
    // Share, Edit, and Delete name their action in the row itself, so only a
    // field gets a cue.
    item->setData(action == FieldAction::Copy ? QStringLiteral("Copy") : QString(), CueRole);
    item->setData(static_cast<int>(action), ActionRole);
    item->setData(email, EmailRole);
    if (focus) {
      item->setData(static_cast<int>(focus->field), EditorFieldRole);
      item->setData(focus->index, EditorIndexRole);
    }
    fieldModel_->appendRow(item);
  };
  if (!shown.photoUrl.isEmpty()) {
    const QPixmap thumb = photos_->imageFor(shown.resourceName, shown.photoUrl, fieldDelegate_->photoEdge(), devicePixelRatioF());
    if (!thumb.isNull()) {
      auto* item = new QStandardItem;
      item->setEditable(false);
      item->setData(static_cast<int>(FieldAction::CopyPhoto), ActionRole);
      item->setData(thumb, Qt::DecorationRole);
      item->setAccessibleText(QStringLiteral("Photo"));
      fieldModel_->appendRow(item);
    }
  }
  if (!shown.givenName.trimmed().isEmpty() || !shown.familyName.trimmed().isEmpty()) {
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
    const EditorFocus focus{EditorField::OtherOrganization, i};
    addField(QStringLiteral("Company"), org.name, FieldAction::Copy, false, focus);
    addField(QStringLiteral("Job title"), org.title, FieldAction::Copy, false, focus);
    addField(QStringLiteral("Department"), org.department, FieldAction::Copy, false, focus);
  }
  addField(QStringLiteral("Occupation"), shown.occupation, FieldAction::Copy, false, EditorFocus{EditorField::Occupation, 0});
  for (int i = 0; i < shown.emails.size(); ++i) {
    const QString label = labelDisplay(LabelField::Email, shown.emails.at(i));
    addField(label, shown.emails.at(i).value, FieldAction::Copy, true, EditorFocus{EditorField::Email, i}, label.isEmpty());
  }
  for (int i = 0; i < shown.phones.size(); ++i) {
    const QString label = labelDisplay(LabelField::Phone, shown.phones.at(i));
    addField(label, shown.phones.at(i).value, FieldAction::Copy, false, EditorFocus{EditorField::Phone, i}, label.isEmpty());
  }
  for (int i = 0; i < shown.addresses.size(); ++i) {
    const auto& address = shown.addresses.at(i);
    const QString label = labelDisplay(LabelField::Address, LabeledValue{QString(), address.label, address.type});
    addField(label.isEmpty() ? QStringLiteral("Address") : label, address.display(), FieldAction::Copy, false,
             EditorFocus{EditorField::Address, i});
  }
  addField(QStringLiteral("Birthday"), shown.birthday.display(), FieldAction::Copy, false, EditorFocus{EditorField::Birthday, 0});
  for (int i = 0; i < shown.urls.size(); ++i) {
    const QString label = labelDisplay(LabelField::Other, shown.urls.at(i));
    addField(label.isEmpty() ? QStringLiteral("Website") : label, shown.urls.at(i).value, FieldAction::Copy, false,
             EditorFocus{EditorField::Url, i});
  }
  for (int i = 0; i < shown.relations.size(); ++i) {
    const QString label = labelDisplay(LabelField::Other, shown.relations.at(i));
    addField(label.isEmpty() ? QStringLiteral("Related") : label, shown.relations.at(i).value, FieldAction::Copy, false,
             EditorFocus{EditorField::Relation, i});
  }
  for (int i = 0; i < shown.events.size(); ++i) {
    const auto& event = shown.events.at(i);
    addField(event.label.trimmed().isEmpty() ? QStringLiteral("Significant date") : event.label, event.date.display(),
             FieldAction::Copy, false, EditorFocus{EditorField::Event, i});
  }
  for (const auto& im : shown.ims) addField(QStringLiteral("Instant message"), im.display(), FieldAction::Copy);
  for (const auto& sip : shown.sips) addField(QStringLiteral("SIP"), sip.value, FieldAction::Copy);
  for (int i = 0; i < shown.customs.size(); ++i) {
    const auto& custom = shown.customs.at(i);
    addField(custom.label.trimmed().isEmpty() ? QStringLiteral("Custom") : custom.label, custom.value, FieldAction::Copy, false,
             EditorFocus{EditorField::Custom, i});
  }
  addField(QStringLiteral("Notes"), shown.notes, FieldAction::Copy, false, EditorFocus{EditorField::Notes, 0});
  addField({}, QStringLiteral("Share this contact"), FieldAction::ShareContact);
  addField({}, QStringLiteral("Edit this contact"), FieldAction::Edit);
  addField({}, QStringLiteral("Delete this contact"), FieldAction::DeleteContact);

  if (fieldModel_->rowCount() > 0) {
    const QModelIndex index = fieldModel_->index(std::clamp(keepRow, 0, fieldModel_->rowCount() - 1), 0);
    fieldList_->setCurrentIndex(index);
    fieldList_->scrollTo(index);
  }
  renderingDetail_ = false;
}

void MainWindow::updateChrome() {
  const int shown = model_->rowCount();
  const int total = index_.size();
  if (detailOpen_) countLabel_->clear();
  else if (!search_->text().isEmpty()) countLabel_->setText(QStringLiteral("%1 of %2").arg(shown).arg(total));
  else countLabel_->setText(total == 1 ? QStringLiteral("1 contact") : QStringLiteral("%1 contacts").arg(total));
  // A background sync stays quiet, unless there is nothing to show until it lands.
  if (busy_ && (!quietSync_ || index_.size() == 0)) statusLabel_->setText(QStringLiteral("Syncing…"));
  else if (saving_) statusLabel_->setText(QStringLiteral("Saving to Google"));
  else if (!signedIn()) statusLabel_->setText(QStringLiteral("Not signed in"));
  else if (signInExpired_) statusLabel_->setText(QStringLiteral("Google sign-in expired"));
  else if (!syncError_.isEmpty()) statusLabel_->setText(QStringLiteral("Sync failed. Menu → Sync now tries again."));
  // Only a stale copy is worth a word. Peeps syncs whenever it is focused.
  else if (syncedAt_ > 0 && nowUnix() - syncedAt_ >= kStaleAfter) statusLabel_->setText(syncedAgo(syncedAt_, nowUnix()));
  else statusLabel_->clear();
  statusLabel_->setToolTip(syncError_);
  reconnectButton_->setVisible(signInExpired_ && signedIn() && !busy_);
  syncAction_->setEnabled(!busy_ && signedIn());
  newAction_->setEnabled(!busy_);
  emptyLabel_->setVisible(shown == 0);
  if (shown == 0) {
    emptyLabel_->setText(!search_->text().isEmpty() ? QStringLiteral("No contacts match.")
                         : !signedIn()              ? QStringLiteral("Open Menu → Google account to connect your contacts.")
                                                    : QStringLiteral("No contacts yet. Ctrl+N starts one."));
  }
}

void MainWindow::toast(const QString& text, bool error) {
  toastLabel_->setText(text);
  toastFrame_->setProperty("error", error);
  toastFrame_->style()->unpolish(toastFrame_);
  toastFrame_->style()->polish(toastFrame_);
  placeToast();
  toastFrame_->show();
  toastFrame_->raise();
  // Errors stay until clicked, so they are read. Notices get out of the way.
  if (error) toastTimer_->stop();
  else toastTimer_->start(3000);
}

void MainWindow::placeToast() {
  QWidget* host = centralWidget();
  const int margin = metrics_.space(18);
  const int width = std::min(host->width() - 2 * margin, metrics_.space(460));
  toastFrame_->setFixedWidth(std::max(width, 1));
  const int height = toastFrame_->layout()->hasHeightForWidth() ? toastFrame_->layout()->totalHeightForWidth(width)
                                                                 : toastFrame_->sizeHint().height();
  toastFrame_->setFixedHeight(height);
  toastFrame_->move(host->width() - margin - width, host->height() - margin - height);
}

void MainWindow::handleCommand(Command command) {
  switch (command) {
    case Command::MoveUp:
      moveSelection(-1, true);
      break;
    case Command::MoveDown:
      moveSelection(1, true);
      break;
    case Command::MoveHome:
      if (detailOpen_) moveField(-1'000'000, false);
      else moveSelection(-1'000'000, false);
      break;
    case Command::MoveEnd:
      if (detailOpen_) moveField(1'000'000, false);
      else moveSelection(1'000'000, false);
      break;
    case Command::FocusSearch:
      if (detailOpen_) closeDetail();
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
      if (activateField()) countShortcut(settings_.keyboardCopies);
      break;
    case Command::CopyField:
      if (copyCurrentField()) countShortcut(settings_.keyboardCopies);
      break;
    case Command::SendEmail:
      if (sendSelectedEmail()) countShortcut(settings_.keyboardEmails);
      break;
    case Command::NewContact:
      newContact();
      break;
    case Command::EditContact:
      editSelected();
      break;
    case Command::Help:
      showHelpDialog(this);
      break;
    case Command::TextLarger:
      setTextSizeOffset(settings_.textSizeOffset + 1);
      break;
    case Command::TextSmaller:
      setTextSizeOffset(settings_.textSizeOffset - 1);
      break;
    case Command::TextReset:
      setTextSizeOffset(0);
      break;
    case Command::TypeToSearch:
    case Command::Ignore:
    case Command::Save:
    case Command::Cancel:
      break;
  }
}

void MainWindow::moveField(int delta, bool wrap) {
  const int count = fieldModel_->rowCount();
  if (count <= 0) return;
  const int current = fieldList_->currentIndex().isValid() ? fieldList_->currentIndex().row() : 0;
  const QModelIndex index = fieldModel_->index(moveIndex(current, count, delta, wrap), 0);
  fieldList_->setCurrentIndex(index);
  fieldList_->scrollTo(index);
}

void MainWindow::moveSelection(int delta, bool wrap) {
  if (detailOpen_) {
    moveField(delta, wrap);
    return;
  }
  const int count = model_->rowCount();
  if (count <= 0) return;
  const int current = list_->currentIndex().isValid() ? list_->currentIndex().row() : 0;
  const QModelIndex index = model_->index(moveIndex(current, count, delta, wrap), 0);
  list_->setCurrentIndex(index);
  list_->scrollTo(index);
}

bool MainWindow::activateField() {
  const QModelIndex index = fieldList_->currentIndex();
  if (!index.isValid()) return false;
  switch (FieldDelegate::action(index)) {
    case FieldAction::DeleteContact:
      deleteSelected();
      break;
    case FieldAction::Edit:
      editSelected();
      break;
    case FieldAction::ShareContact:
      shareSelected();
      break;
    case FieldAction::Copy:
    case FieldAction::CopyPhoto:
      return copyCurrentField();
  }
  return false;
}

bool MainWindow::sendSelectedEmail() {
  const QModelIndex index = fieldList_->currentIndex();
  if (!index.isValid() || !index.data(EmailRole).toBool()) {
    toast(QStringLiteral("Select an email address first."), true);
    return false;
  }
  const DesktopCommand command = mailCommand(index.data(Qt::DisplayRole).toString());
  if (!command.ok()) {
    toast(command.error, true);
    return false;
  }
  runDetached(command);
  toast(QStringLiteral("Opening email…"));
  return true;
}

bool MainWindow::copyCurrentField() {
  const QModelIndex index = fieldList_->currentIndex();
  if (!index.isValid()) return false;
  const FieldAction action = FieldDelegate::action(index);
  if (action == FieldAction::CopyPhoto) {
    const Contact* contact = selected();
    const QByteArray original = contact == nullptr ? QByteArray() : photos_->originalBytes(contact->resourceName, contact->photoUrl);
    if (original.isEmpty()) {
      toast(QStringLiteral("The photo isn’t downloaded yet."), true);
      return false;
    }
    copyPhoto(original);
    return true;
  }
  if (action != FieldAction::Copy) return false;
  const QString value = index.data(Qt::DisplayRole).toString().trimmed();
  if (value.isEmpty()) return false;
  QGuiApplication::clipboard()->setText(value);
  toast(copiedNotice(index));
  return true;
}

// The clipboard gets the photo at full size: the original file as it is, and
// a lossless PNG for apps that only paste PNG. Omarchy's clipboard history asks
// for the PNG the moment it is offered, and an offered QImage is encoded on
// this thread for each request, which froze the window for seconds on a large
// photo. So the PNG is made once, on a worker, before the copy is offered.
void MainWindow::copyPhoto(const QByteArray& original) {
  const quint64 copy = ++photoCopies_;
  const quint64 clipboardBefore = clipboardChanges_;
  runInBackground([this, original, copy, clipboardBefore] {
    QBuffer source;
    source.setData(original);
    source.open(QIODevice::ReadOnly);
    QImageReader reader(&source);
    const QString type = QStringLiteral("image/") + QString::fromLatin1(reader.format());
    const QImage image = decodePhoto(original);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    // PNG is lossless at any level, and a light one encodes a 3200 pixel
    // photo in half a second instead of more than two.
    if (image.isNull() || !image.save(&buffer, "PNG", 80)) png.clear();
    QMetaObject::invokeMethod(
        this,
        [this, original, png, type, copy, clipboardBefore] {
          if (closing_ || copy != photoCopies_ || clipboardChanges_ != clipboardBefore) return;
          if (png.isEmpty()) {
            toast(QStringLiteral("Could not copy that photo."), true);
            return;
          }
          auto* data = new QMimeData;
          data->setData(QStringLiteral("image/png"), png);
          if (type != QLatin1String("image/png")) data->setData(type, original);
          QGuiApplication::clipboard()->setMimeData(data);
          toast(QStringLiteral("Copied photo"));
        },
        Qt::QueuedConnection);
  });
}

// A shortcut used from the keyboard counts toward retiring its hint. The
// mouse does not, since clicking teaches nothing about the keys.
void MainWindow::countShortcut(int& uses) {
  if (uses >= kHintLearnedAfter) return;
  ++uses;
  saveSettings();
  fieldDelegate_->copyLearned = settings_.copyLearned();
  fieldDelegate_->emailLearned = settings_.emailLearned();
  // A shorter cue gives wrapped notes more room.
  fieldList_->doItemsLayout();
  fieldList_->viewport()->update();
}

void MainWindow::saveSettings() {
  // peeps skill remove may have written this while the window was open.
  if (auto bytes = readFile(paths_.settingsFile)) settings_.skillDeclined = readSettingsJson(*bytes).skillDeclined;
  QString error;
  if (!writePrivateFile(paths_.settingsFile, writeSettingsJson(settings_), error)) toast(error, true);
}

// Each person gets the agent skill without a step of their own: a package
// cannot write into home folders, so the window links it when it opens. After
// peeps skill remove it stays away.
void MainWindow::linkAgentSkill() {
  if (settings_.skillDeclined) return;
  const QString skill = installedSkillDir(QCoreApplication::applicationDirPath());
  if (skill.isEmpty()) return;
  const SkillLinks links = linkSkill(
      skill, agentSkillFolders(QDir::homePath(), qEnvironmentVariable("CLAUDE_CONFIG_DIR"), qEnvironmentVariable("CODEX_HOME")));
  if (!links.changed.isEmpty() && links.error.isEmpty()) toast(QStringLiteral("Linked the Peeps skill for your AI agents"));
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
  if (!list_->currentIndex().isValid()) return nullptr;
  return index_.find(model_->idAt(list_->currentIndex().row()));
}

void MainWindow::editSelected() {
  const Contact* contact = selected();
  if (contact == nullptr || busy_ || editing_) return;
  EditorFocus focus;
  const QModelIndex index = fieldList_->currentIndex();
  if (detailOpen_ && index.isValid() && index.data(EditorFieldRole).isValid()) {
    focus.field = static_cast<EditorField>(index.data(EditorFieldRole).toInt());
    focus.index = index.data(EditorIndexRole).toInt();
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
  }
  editor_ = new ContactEditor(contact, focus, detailPane_);
  editor_->setOnSave([this] { commitEditor(); });
  editor_->setOnCancel([this] { requestCloseEditor(); });
  detailLayout_->addWidget(editor_, 1);
  if (!contact.photoUrl.isEmpty()) {
    editor_->setPreview(photos_->imageFor(contact.resourceName, contact.photoUrl, metrics_.space(96), devicePixelRatioF()));
  }
  detailOpen_ = true;
  applyView();
  editor_->focusField();
  QTimer::singleShot(0, editor_, [editor = editor_] { editor->focusField(); });
}

void MainWindow::requestCloseEditor() {
  if (editor_ != nullptr && editor_->hasChanges()) {
    const Contact* contact = editingNew_ ? nullptr : selected();
    const QString body = contact == nullptr ? QStringLiteral("This contact has not been saved.")
                                            : QStringLiteral("%1 keeps what was last saved.").arg(contact->displayName());
    if (!confirmDialog(this, QStringLiteral("Discard your changes?"), body, QStringLiteral("Discard"),
                       QStringLiteral("Keep editing"))) {
      return;
    }
  }
  closeEditor();
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
    fieldList_->setFocus();
    return;
  }
  closeDetail();
}

void MainWindow::commitEditor() {
  if (editor_ == nullptr) return;
  if (saving_) {
    // Only one save goes to Google at a time. The editor stays open with the edits.
    toast(QStringLiteral("Still saving the last change. Save again in a moment."));
    return;
  }
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
  toast(QStringLiteral("Copied contact"));
}

QString MainWindow::writeCard(const Contact& contact) {
  // The runtime directory is private to this login and clears at logout.
  QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  dir = dir.isEmpty() ? QFileInfo(paths_.photoDir).absolutePath() + QStringLiteral("/cards") : dir + QStringLiteral("/peeps");
  const QString path = dir + QLatin1Char('/') + cardFileName(contact.displayName());
  const QByteArray jpeg = photoUploadBytes(photos_->originalBytes(contact.resourceName, contact.photoUrl));
  QString error;
  if (!writePrivateFile(path, contactVCard(contact, jpeg), error)) {
    toast(error, true);
    return {};
  }
  return path;
}

void MainWindow::shareSelected() {
  const Contact* selectedContact = selected();
  if (selectedContact == nullptr) return;
  const Contact contact = *selectedContact;
  QMenu menu(this);
  QAction* send = menu.addAction(QStringLiteral("Send with LocalSend"));
  QAction* email = menu.addAction(QStringLiteral("Email as a vCard"));
  QAction* copy = menu.addAction(QStringLiteral("Copy as text"));
  QAction* save = menu.addAction(QStringLiteral("Save vCard…"));
  const QRect row = fieldList_->visualRect(fieldList_->currentIndex());
  const QPoint anchor = row.isValid() ? fieldList_->viewport()->mapToGlobal(row.bottomLeft() + QPoint(metrics_.space(8), 0))
                                      : mapToGlobal(rect().center());
  QAction* chosen = menu.exec(anchor, send);
  if (chosen == nullptr) return;
  if (chosen == copy) {
    copyWholeContact();
    return;
  }
  if (chosen == save) {
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save vCard"),
                                                      downloads + QLatin1Char('/') + cardFileName(contact.displayName()),
                                                      QStringLiteral("vCard (*.vcf)"));
    if (path.isEmpty()) return;
    QString error;
    const QByteArray jpeg = photoUploadBytes(photos_->originalBytes(contact.resourceName, contact.photoUrl));
    if (!writePrivateFile(path, contactVCard(contact, jpeg), error)) toast(error, true);
    else toast(QStringLiteral("Saved %1").arg(QFileInfo(path).fileName()));
    return;
  }
  const QString path = writeCard(contact);
  if (path.isEmpty()) return;
  if (chosen == send) {
    const DesktopCommand command =
        shareCommand(path, haveProgram(QStringLiteral("omarchy")), haveProgram(QStringLiteral("localsend")));
    if (!command.ok()) toast(command.error, true);
    else if (!runDetached(command)) toast(QStringLiteral("Could not start LocalSend."), true);
    else toast(QStringLiteral("Sending %1 with LocalSend").arg(QFileInfo(path).fileName()));
    return;
  }
  if (chosen == email) {
    if (mailHandlerTakesAttachments(mailHandler())) {
      runDetached(attachCommand(path));
      toast(QStringLiteral("Opening email…"));
      return;
    }
    // Webmail such as HEY cannot take an attachment from a mailto link. The
    // card goes on the clipboard as a file, so a paste in the new message
    // attaches it. Somewhere that only takes text gets the card as text.
    QGuiApplication::clipboard()->setMimeData(fileClipboard(path, contactPlainText(contact)));
    runDetached(composeCommand());
    toast(QStringLiteral("Paste into the new message to attach %1").arg(QFileInfo(path).fileName()));
  }
}

void MainWindow::deleteSelected() {
  const Contact* contact = selected();
  if (contact == nullptr || busy_) return;
  const QString id = contact->resourceName;
  if (!confirmDialog(this, QStringLiteral("Delete %1?").arg(contact->displayName()),
                     QStringLiteral("This removes the contact from Google Contacts on every device."), QStringLiteral("Delete"))) {
    return;
  }
  runDelete(id);
}

void MainWindow::openAccount() {
  const auto action = accountDialog(this, !client_.clientId.isEmpty(), signedIn());
  if (!action) return;
  if (*action == AccountAction::Connect) {
    startOAuth();
    return;
  }
  if (!confirmDialog(this, QStringLiteral("Disconnect Google?"),
                     QStringLiteral("Peeps forgets the local copy. People stay in the Google account."),
                     QStringLiteral("Disconnect"))) {
    return;
  }
  QString error;
  if (!secrets_->clear(error)) toast(error, true);
  tokens_ = {};
  syncToken_.clear();
  syncError_.clear();
  syncedAt_ = 0;
  signInExpired_ = false;
  index_.replaceAll({});
  saveCache();
  refilter();
  toast(QStringLiteral("Disconnected"));
}

void MainWindow::setBusy(bool busy) {
  busy_ = busy;
  if (!busy) quietSync_ = false;
  updateChrome();
}

void MainWindow::saveCache() {
  QString error;
  if (!writePrivateFile(paths_.cacheFile, writeCacheJson(index_.all(), syncToken_, syncedAt_), error)) toast(error, true);
  else cacheSyncedAt_ = syncedAt_;
}

void MainWindow::setTextSizeOffset(int offset) {
  offset = std::clamp(offset, kMinTextSizeOffset, kMaxTextSizeOffset);
  if (offset != settings_.textSizeOffset) {
    settings_.textSizeOffset = offset;
    saveSettings();
    reloadTheme();
  }
  toast(QStringLiteral("Text size %1 px").arg(metrics_.basePx));
}

void MainWindow::saveTokens(const OAuthTokens& tokens) {
  tokens_ = tokens;
  photos_->setAccessToken(tokens_.accessToken);
  QString error;
  if (!secrets_->save(QString::fromUtf8(writeTokensJson(tokens_)), error)) toast(error, true);
}

void MainWindow::setContacts(QList<Contact> contacts) {
  index_.replaceAll(std::move(contacts));
  refilter();
}

void MainWindow::present(const QString& activationToken) {
  // Qt hands this token to the compositor with the activate request.
  if (!activationToken.isEmpty()) qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
  if (isMinimized()) showNormal();
  else show();
  raise();
  activateWindow();
  focusWindowOnHyprland(QCoreApplication::applicationPid());
  if (!editing_ && !detailOpen_) {
    search_->setFocus();
    search_->selectAll();
  }
}

int MainWindow::detailPaneHeight() const { return detailPane_->height(); }

bool MainWindow::detailShows(const QString& text) const {
  if (!detailOpen_ || !fieldList_->isVisible()) return false;
  for (int i = 0; i < fieldModel_->rowCount(); ++i) {
    const QModelIndex index = fieldModel_->index(i, 0);
    if (!index.data(Qt::DisplayRole).toString().contains(text) && !index.data(LabelRole).toString().contains(text)) continue;
    const QRect row = fieldList_->visualRect(index);
    const int viewport = std::max(fieldList_->viewport()->height(), fieldList_->height());
    if (viewport < 1 || row.height() < 1) return true;
    if (row.bottom() < 0 || row.top() >= viewport) continue;
    return true;
  }
  return false;
}

bool MainWindow::detailIsOnScreen() const {
  if (!detailPane_->isVisible() || detailPane_->height() < 120) return false;
  const QPoint topLeft = detailPane_->mapTo(this, QPoint(0, 0));
  return topLeft.y() >= 0 && topLeft.y() + detailPane_->height() <= height();
}

QString MainWindow::detailPlainText() const {
  QStringList parts;
  for (int i = 0; i < fieldModel_->rowCount(); ++i) {
    const QModelIndex index = fieldModel_->index(i, 0);
    parts.append(index.data(LabelRole).toString());
    parts.append(index.data(Qt::DisplayRole).toString());
  }
  return parts.join(QStringLiteral(" | "));
}

void MainWindow::openSelected() {
  if (selected() == nullptr) return;
  detailOpen_ = true;
  applyView();
  renderDetail();
  fieldList_->setFocus();
}

void MainWindow::closeDetail() {
  detailOpen_ = false;
  applyView();
  search_->setFocus();
}

void MainWindow::applyView() {
  if (detailOpen_ && !editing_ && selected() == nullptr) detailOpen_ = false;
  listPane_->setVisible(!detailOpen_);
  detailPane_->setVisible(detailOpen_);
  fieldList_->setVisible(detailOpen_ && !editing_);
  if (editor_ != nullptr) editor_->setVisible(editing_);
  search_->setVisible(!detailOpen_);
  // The editor has its own Cancel, so the title leads back only from a contact.
  const bool canGoBack = detailOpen_ && !editing_;
  titleButton_->setText(canGoBack ? QStringLiteral("← Peeps") : QStringLiteral("Peeps"));
  titleButton_->setEnabled(canGoBack);
  titleButton_->setCursor(canGoBack ? Qt::PointingHandCursor : Qt::ArrowCursor);
  updateChrome();
}

void MainWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  if (event->type() != QEvent::ActivationChange) return;
  const bool active = isActiveWindow();
  if (active == windowActive_) return;
  windowActive_ = active;
  if (!active) {
    focusSyncTimer_->stop();
    return;
  }
  if (qEnvironmentVariableIsSet("PEEPS_NO_SYNC")) return;
  if (lastSyncAt_ == 0 || nowUnix() - lastSyncAt_ >= kFocusSyncAfter) runSync(false, false);
  focusSyncTimer_->start();
}

bool MainWindow::signedIn() const { return !tokens_.refreshToken.isEmpty() && !client_.clientId.isEmpty(); }

void MainWindow::runGoogle(GoogleWork work, GoogleDone done) {
  const OAuthClient oauth = client_;
  const OAuthTokens tokens = tokens_;
  runInBackground([this, oauth, tokens, work = std::move(work), done = std::move(done)] {
    BlockingNam http;
    GoogleSession google(http, oauth, tokens);
    QString error = google.prepare(nowUnix());
    if (error.isEmpty()) error = work(google);
    QMetaObject::invokeMethod(
        this,
        [this, error, before = tokens, after = google.tokens(), done] {
          if (closing_) return;
          // A refreshed access token is worth keeping, unless the account was
          // disconnected while the request ran.
          if (after != before && tokens_.refreshToken == before.refreshToken && !tokens_.refreshToken.isEmpty()) saveTokens(after);
          if (error == signInExpiredError() && tokens_.refreshToken == before.refreshToken) {
            signInExpired_ = true;
            updateChrome();
          }
          done(error);
        },
        Qt::QueuedConnection);
  });
}

void MainWindow::runSync(bool forceFull, bool announce) {
  if (!announce && qEnvironmentVariableIsSet("PEEPS_NO_SYNC")) return;
  if (busy_) return;
  if (!signedIn()) {
    if (announce) toast(QStringLiteral("Connect a Google account first."), true);
    return;
  }
  // Each try would fail the same way until Reconnect.
  if (signInExpired_ && !announce) return;
  lastSyncAt_ = nowUnix();
  quietSync_ = !announce;
  setBusy(true);
  const QString syncToken = forceFull ? QString() : syncToken_;
  auto fetched = std::make_shared<PeopleResult>();
  runGoogle(
      [syncToken, fetched](GoogleSession& google) {
        *fetched = google.call([&](HttpClient& http, QString& access, const RefreshFn& refresh) {
          return fetchConnections(http, access, syncToken, refresh);
        });
        return fetched->error;
      },
      [this, syncToken, fetched, announce](const QString& error) {
        setBusy(false);
        if (error == QLatin1String("sync-token-expired") && !resyncing_) {
          resyncing_ = true;
          syncToken_.clear();
          runSync(true, announce);
          return;
        }
        resyncing_ = false;
        // A save is on its way to Google. The next sync picks up both.
        if (saving_) return;
        if (!error.isEmpty()) {
          // A quiet background sync reports in the status line instead of
          // putting an error in front of someone who did not ask for it.
          syncError_ = error;
          if (announce) toast(error, true);
          updateChrome();
          return;
        }
        syncError_.clear();
        if (!fetched->syncToken.isEmpty()) syncToken_ = fetched->syncToken;
        syncedAt_ = nowUnix();
        // Most syncs bring nothing new. Then the index, the list, and the open
        // contact stay as they are, and the cache on disk catches up once an
        // hour. Its older token is safe: Google replays the changes since that
        // token, and there were none.
        if (!syncToken.isEmpty() && fetched->people.isEmpty()) {
          if (syncedAt_ - cacheSyncedAt_ >= kStaleAfter) saveCache();
          updateChrome();
          if (announce) toast(QStringLiteral("Synced %1 contacts").arg(index_.size()));
          return;
        }
        index_.replaceAll(syncToken.isEmpty() ? applyFullSync(fetched->people)
                                              : applyIncrementalSync(index_.all(), fetched->people));
        saveCache();
        // Selecting the row again redraws an open contact.
        refilter();
        if (announce) toast(QStringLiteral("Synced %1 contacts").arg(index_.size()));
      });
}

void MainWindow::runSave(const Contact& contact, bool isNew, const QByteArray& photoBytes, bool removePhoto) {
  if (saving_) return;
  if (!signedIn()) {
    toast(QStringLiteral("Connect a Google account before saving."), true);
    return;
  }
  // An edit shows at once and rolls back if Google refuses it.
  std::optional<Contact> previous;
  if (!isNew && !contact.resourceName.isEmpty()) {
    if (const Contact* existing = index_.find(contact.resourceName)) previous = *existing;
    index_.upsert(contact);
    saveCache();
    refilter();
    selectId(contact.resourceName);
  }
  saving_ = true;
  updateChrome();
  struct Outcome {
    Contact saved;
    QString photoError;
  };
  auto outcome = std::make_shared<Outcome>();
  runGoogle(
      [contact, isNew, photoBytes, removePhoto, outcome](GoogleSession& google) {
        const WriteResult written = google.call([&](HttpClient& http, QString& access, const RefreshFn& refresh) {
          return isNew ? createContact(http, access, contact, refresh) : updateContact(http, access, contact, refresh);
        });
        if (!written.ok()) return written.error;
        outcome->saved = written.contact;
        if (photoBytes.isEmpty() && !removePhoto) return QString();
        const WriteResult pictured = google.call([&](HttpClient& http, QString& access, const RefreshFn& refresh) {
          return removePhoto ? deleteContactPhoto(http, access, outcome->saved.resourceName, refresh)
                             : updateContactPhoto(http, access, outcome->saved.resourceName, photoBytes, refresh);
        });
        if (!pictured.ok()) {
          outcome->photoError = pictured.error;
        } else {
          outcome->saved.photoUrl = removePhoto ? QString() : pictured.contact.photoUrl;
          if (!pictured.contact.etag.isEmpty()) outcome->saved.etag = pictured.contact.etag;
        }
        return QString();
      },
      [this, contact, previous, isNew, photoBytes, removePhoto, outcome](const QString& error) {
        saving_ = false;
        updateChrome();
        if (!error.isEmpty()) {
          if (previous) {
            index_.upsert(*previous);
            saveCache();
            refilter();
            selectId(previous->resourceName);
          }
          toast(error, true);
          return;
        }
        const Contact& saved = outcome->saved;
        Contact merged = contact;
        if (const Contact* current = isNew ? nullptr : index_.find(contact.resourceName)) merged = *current;
        if (!saved.resourceName.isEmpty()) merged.resourceName = saved.resourceName;
        if (!saved.etag.isEmpty()) merged.etag = saved.etag;
        // Keep the cached photo's URL unless the photo itself changed, so an
        // edit does not download the same picture again.
        if (removePhoto && outcome->photoError.isEmpty()) merged.photoUrl.clear();
        else if ((!photoBytes.isEmpty() || merged.photoUrl.isEmpty()) && !saved.photoUrl.isEmpty()) merged.photoUrl = saved.photoUrl;
        index_.upsert(merged);
        if (outcome->photoError.isEmpty()) {
          if (removePhoto) photos_->forget(merged.resourceName);
          else if (!photoBytes.isEmpty() && !merged.photoUrl.isEmpty()) photos_->remember(merged.resourceName, merged.photoUrl, photoBytes);
        }
        saveCache();
        refilter();
        selectId(merged.resourceName);
        if (!outcome->photoError.isEmpty()) toast(QStringLiteral("Saved, but the photo failed. %1").arg(outcome->photoError), true);
        else toast(isNew ? QStringLiteral("Contact added") : QStringLiteral("Contact saved"));
      });
}

void MainWindow::runDelete(const QString& id) {
  if (busy_) return;
  setBusy(true);
  runGoogle(
      [id](GoogleSession& google) {
        return google.call([&](HttpClient& http, QString& access, const RefreshFn& refresh) {
          return deleteContact(http, access, id, refresh);
        });
      },
      [this, id](const QString& error) {
        setBusy(false);
        if (!error.isEmpty()) {
          toast(error, true);
          return;
        }
        index_.remove(id);
        saveCache();
        detailOpen_ = false;
        refilter();
        search_->setFocus();
        toast(QStringLiteral("Contact deleted"));
      });
}

void MainWindow::startOAuth() {
  if (client_.clientId.isEmpty()) {
    toast(QStringLiteral("This build of Peeps has no Google client."), true);
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
  const QString url = buildAuthUrl(client_, oauthRedirect_, oauthState_,
                                   pkceChallengeS256(oauthVerifier_));
  connect(oauthServer_, &QTcpServer::newConnection, this, [this, server = oauthServer_] {
    while (QTcpSocket* socket = server->nextPendingConnection()) {
      connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      // Browsers open spare connections. Each socket keeps its own request.
      connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
        QByteArray request = socket->property("request").toByteArray() + socket->readAll();
        socket->setProperty("request", request);
        if (!request.contains("\r\n\r\n") && request.size() < 8192) return;
        if (oauthState_.isEmpty()) {
          // The redirect was already handled. This is a spare connection,
          // such as the browser asking for a favicon.
          socket->write("HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n");
          socket->disconnectFromHost();
          return;
        }
        const RedirectQuery query = parseOauthRedirect(request);
        QString failure;
        if (!query.ok()) failure = query.error;
        else if (query.state != oauthState_) failure = QStringLiteral("The sign-in response did not match this window.");
        const QByteArray html = oauthPage(palette_, fontFamily_,
                                          failure.isEmpty() ? QStringLiteral("Peeps is signed in.")
                                                            : QStringLiteral("Peeps could not sign in."),
                                          failure.isEmpty() ? QStringLiteral("You can close this tab.") : failure)
                                    .toUtf8();
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                      QByteArray::number(html.size()) + "\r\nConnection: close\r\n\r\n" + html);
        socket->disconnectFromHost();
        oauthState_.clear();
        if (oauthServer_ != nullptr) oauthServer_->close();
        if (oauthTimer_ != nullptr) oauthTimer_->stop();
        if (!failure.isEmpty()) {
          toast(failure, true);
          return;
        }
        finishOAuth(query.code);
      });
    }
  });
  if (oauthTimer_ == nullptr) {
    oauthTimer_ = new QTimer(this);
    oauthTimer_->setSingleShot(true);
    connect(oauthTimer_, &QTimer::timeout, this, [this] {
      if (oauthServer_) oauthServer_->close();
      toast(QStringLiteral("Timed out waiting for Google in the browser."), true);
    });
  }
  oauthTimer_->start(180000);
  runDetached(browserCommand(url, haveProgram(QStringLiteral("omarchy"))));
  toast(QStringLiteral("Finish signing in to Google in the browser…"));
}

void MainWindow::finishOAuth(const QString& code) {
  setBusy(true);
  const OAuthClient client = client_;
  const QString redirect = oauthRedirect_;
  const QString verifier = oauthVerifier_;
  runInBackground([this, client, code, redirect, verifier] {
    BlockingNam http;
    const TokenResult exchanged = exchangeCode(http, client, code, redirect, verifier, nowUnix());
    QString error = exchanged.error;
    PeopleResult fetched;
    OAuthTokens tokens = exchanged.tokens;
    if (error.isEmpty()) {
      GoogleSession google(http, client, tokens);
      fetched = google.call([](HttpClient& http, QString& access, const RefreshFn& refresh) {
        return fetchConnections(http, access, QString(), refresh);
      });
      error = fetched.error;
      tokens = google.tokens();
    }
    QMetaObject::invokeMethod(
        this,
        [this, error, fetched, tokens] {
          if (closing_) return;
          setBusy(false);
          if (!tokens.refreshToken.isEmpty()) saveTokens(tokens);
          if (!error.isEmpty()) {
            toast(error, true);
            return;
          }
          syncError_.clear();
          signInExpired_ = false;
          index_.replaceAll(applyFullSync(fetched.people));
          syncToken_ = fetched.syncToken;
          lastSyncAt_ = nowUnix();
          syncedAt_ = lastSyncAt_;
          saveCache();
          refilter();
          activateWindow();
          focusWindowOnHyprland(QCoreApplication::applicationPid());
          toast(QStringLiteral("Connected to Google Contacts"));
        },
        Qt::QueuedConnection);
  });
}

}  // namespace contacts
