#pragma once

#include "contact.h"
#include "dialogs.h"
#include "google.h"
#include "oauth.h"
#include "search.h"
#include "secrets.h"
#include "shortcuts.h"
#include "store.h"
#include "theme.h"

#include <QElapsedTimer>
#include <QMainWindow>

#include <functional>
#include <memory>

class QFileSystemWatcher;
class QFrame;
class QAction;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QPushButton;
class QStandardItemModel;
class QTcpServer;
class QTimer;
class QVBoxLayout;
class QHBoxLayout;

namespace contacts {

class ContactDelegate;
class ContactListModel;
class FieldDelegate;
class PhotoStore;

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow();
  ~MainWindow() override;

  void setContacts(QList<Contact> contacts);
  // Brings the window forward for a second launch. `activationToken` is the
  // XDG activation token that launch was started with, when it had one.
  void present(const QString& activationToken = {});

  // For tests.
  int detailPaneHeight() const;
  bool detailShows(const QString& text) const;
  bool detailIsOnScreen() const;
  QString detailPlainText() const;

 protected:
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  using GoogleWork = std::function<QString(GoogleSession& google)>;
  using GoogleDone = std::function<void(const QString& error)>;

  void buildUi();
  void loadLocal();
  void applyStyle();
  void scheduleThemeReload();
  void reloadTheme();
  void watchThemeFiles();
  void refilter();
  void renderDetail();
  void updateChrome();
  void toast(const QString& text, bool error = false);
  void placeToast();
  void handleCommand(Command command);
  void moveSelection(int delta, bool wrap);
  void moveField(int delta, bool wrap);
  void selectId(const QString& id);
  // These return true when something was copied or an email was started.
  bool activateField();
  bool copyCurrentField();
  void copyPhoto(const QByteArray& original);
  bool sendSelectedEmail();
  void countShortcut(int& uses);
  void saveSettings();
  void linkAgentSkill();
  const Contact* selected() const;
  void editSelected();
  void newContact();
  void showEditor(const Contact& contact, EditorFocus focus, bool isNew);
  void requestCloseEditor();
  void closeEditor();
  void commitEditor();
  void copyWholeContact();
  void shareSelected();
  QString writeCard(const Contact& contact);
  void deleteSelected();
  void openAccount();
  void openSelected();
  void closeDetail();
  bool signedIn() const;
  void runGoogle(GoogleWork work, GoogleDone done);
  void runSync(bool forceFull, bool announce = true);
  void runSave(const Contact& contact, bool isNew, const QByteArray& photoBytes = {}, bool removePhoto = false);
  void runDelete(const QString& id);
  void startOAuth();
  void finishOAuth(const QString& code);
  void saveCache();
  void saveTokens(const OAuthTokens& tokens);
  void setTextSizeOffset(int offset);
  void applyView();
  void setBusy(bool busy);

  Palette palette_;
  ShellMetrics metrics_;
  QString fontFamily_;
  bool styled_ = false;
  ThemePaths themePaths_;
  AppPaths paths_;
  OAuthClient client_ = builtInClient();
  AppSettings settings_;
  QElapsedTimer lastRowClick_;
  OAuthTokens tokens_;
  QString syncToken_;
  QString syncError_;
  ContactIndex index_;
  std::unique_ptr<SecretStore> secrets_;
  bool busy_ = false;
  // The running sync started by itself, so the footer does not announce it.
  bool quietSync_ = false;
  bool closing_ = false;
  // A photo copy still being prepared gives way to a later copy of anything.
  quint64 photoCopies_ = 0;
  quint64 clipboardChanges_ = 0;
  bool detailOpen_ = false;
  bool editing_ = false;
  bool editingNew_ = false;
  bool editorRestoreDetail_ = false;
  bool firstShow_ = true;
  bool resyncing_ = false;
  bool renderingDetail_ = false;
  bool saving_ = false;
  bool windowActive_ = false;
  qint64 lastSyncAt_ = 0;
  // When the cache last matched Google, for "Synced 3 hours ago".
  qint64 syncedAt_ = 0;
  // The same, as last written to the cache on disk.
  qint64 cacheSyncedAt_ = 0;
  // The search the list shows, so a redraw for the same one keeps its scroll.
  QString listedQuery_;
  // Google revoked or expired the sign-in, so only Reconnect helps.
  bool signInExpired_ = false;

  QVBoxLayout* rootLayout_ = nullptr;
  QHBoxLayout* headerLayout_ = nullptr;
  QVBoxLayout* listLayout_ = nullptr;
  QVBoxLayout* detailLayout_ = nullptr;
  QLineEdit* search_ = nullptr;
  QListView* list_ = nullptr;
  QListView* fieldList_ = nullptr;
  ContactEditor* editor_ = nullptr;
  ContactListModel* model_ = nullptr;
  ContactDelegate* delegate_ = nullptr;
  FieldDelegate* fieldDelegate_ = nullptr;
  QStandardItemModel* fieldModel_ = nullptr;
  PhotoStore* photos_ = nullptr;
  QWidget* listPane_ = nullptr;
  QWidget* detailPane_ = nullptr;
  QLabel* emptyLabel_ = nullptr;
  QLabel* countLabel_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QPushButton* reconnectButton_ = nullptr;
  QAction* clearSearchAction_ = nullptr;
  QLabel* toastLabel_ = nullptr;
  QFrame* toastFrame_ = nullptr;
  QPushButton* titleButton_ = nullptr;
  QPushButton* menuButton_ = nullptr;
  QMenu* menu_ = nullptr;
  QAction* newAction_ = nullptr;
  QAction* syncAction_ = nullptr;
  QFileSystemWatcher* watcher_ = nullptr;
  QTimer* themeTimer_ = nullptr;
  QTimer* toastTimer_ = nullptr;
  QTimer* focusSyncTimer_ = nullptr;
  QTimer* syncedTimer_ = nullptr;
  QTcpServer* oauthServer_ = nullptr;
  QTimer* oauthTimer_ = nullptr;
  QString oauthVerifier_;
  QString oauthState_;
  QString oauthRedirect_;
};

}  // namespace contacts
