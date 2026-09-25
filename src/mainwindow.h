#pragma once

#include "contact.h"
#include "dialogs.h"
#include "oauth.h"
#include "search.h"
#include "secrets.h"
#include "shortcuts.h"
#include "store.h"
#include "theme.h"

#include <QMainWindow>

#include <memory>

class QBoxLayout;
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
  void present();
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
  void buildUi();
  void loadLocal();
  void applyPalette(const Palette& palette);
  void scheduleThemeReload();
  void reloadTheme();
  void refilter();
  void renderDetail();
  void updateChrome();
  void toast(const QString& text, bool error = false, int milliseconds = 6000);
  void handleCommand(Command command);
  void moveSelection(int delta, bool wrap);
  void moveField(int delta, bool wrap);
  void selectId(const QString& id);
  void activateField();
  void copyCurrentField();
  void sendSelectedEmail();
  void sendMailTo(const QString& address);
  const Contact* selected() const;
  void editSelected();
  void newContact();
  void showEditor(const Contact& contact, EditorFocus focus, bool isNew);
  void closeEditor();
  void commitEditor();
  void copyWholeContact();
  void changeSelectedPhoto(bool remove);
  void deleteSelected();
  void copyEmail();
  void copyPhone();
  void sendMail();
  void openAccount();
  void openSelected();
  void closeDetail();
  void runSync(bool forceFull, bool announce = true);
  void runSave(const Contact& contact, bool isNew, const QByteArray& photoBytes = {}, bool removePhoto = false);
  void runPhoto(const Contact& contact, const QByteArray& photoBytes, bool removePhoto);
  void runDelete(const QString& id);
  void startOAuth();
  void saveCache();
  void saveConfig();
  void saveTokens(const OAuthTokens& tokens);
  void applyView();
  void setBusy(bool busy);

  Palette palette_;
  bool havePalette_ = false;
  ThemePaths themePaths_;
  AppPaths paths_;
  AppConfig config_;
  OAuthTokens tokens_;
  QString syncToken_;
  ContactIndex index_;
  std::unique_ptr<SecretStore> secrets_;
  bool busy_ = false;
  bool closing_ = false;
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

  QLineEdit* search_ = nullptr;
  QListView* list_ = nullptr;
  QListView* fieldList_ = nullptr;
  ContactEditor* editor_ = nullptr;
  ContactListModel* model_ = nullptr;
  ContactDelegate* delegate_ = nullptr;
  FieldDelegate* fieldDelegate_ = nullptr;
  QStandardItemModel* fieldModel_ = nullptr;
  PhotoStore* photos_ = nullptr;
  QLabel* portrait_ = nullptr;
  QLabel* detailName_ = nullptr;
  QWidget* listPane_ = nullptr;
  QWidget* headerPane_ = nullptr;
  QWidget* detailPane_ = nullptr;
  QBoxLayout* splitLayout_ = nullptr;
  QFrame* rule_ = nullptr;
  QVBoxLayout* detailLayout_ = nullptr;
  QVBoxLayout* headerLayout_ = nullptr;
  QLabel* emptyLabel_ = nullptr;
  QLabel* countLabel_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QLabel* toastLabel_ = nullptr;
  QFrame* toastFrame_ = nullptr;
  QPushButton* backButton_ = nullptr;
  QPushButton* menuButton_ = nullptr;
  QMenu* menu_ = nullptr;
  QAction* newAction_ = nullptr;
  QAction* syncAction_ = nullptr;
  QFileSystemWatcher* watcher_ = nullptr;
  QTimer* themeTimer_ = nullptr;
  QTimer* toastTimer_ = nullptr;
  QTimer* focusSyncTimer_ = nullptr;
  QTcpServer* oauthServer_ = nullptr;
  QTimer* oauthTimer_ = nullptr;
  QByteArray oauthBuffer_;
  QString oauthVerifier_;
  QString oauthState_;
  QString oauthRedirect_;
};

}  // namespace contacts
