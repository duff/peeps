#pragma once

#include "contact.h"
#include "oauth.h"
#include "search.h"
#include "secrets.h"
#include "shortcuts.h"
#include "store.h"
#include "theme.h"

#include <QMainWindow>

#include <memory>

class QFileSystemWatcher;
class QFrame;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QTcpServer;
class QTimer;
class QVBoxLayout;

namespace omacontacts {

class ContactDelegate;
class ContactListModel;

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow();
  ~MainWindow() override;

 protected:
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
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
  void toast(const QString& text, bool error);
  void handleCommand(Command command);
  void moveSelection(int delta, bool wrap);
  void selectId(const QString& id);
  const Contact* selected() const;
  void editSelected();
  void newContact();
  void deleteSelected();
  void copyEmail();
  void copyPhone();
  void sendMail();
  void openAccount();
  void runSync(bool forceFull);
  void runSave(const Contact& contact, bool isNew);
  void runDelete(const QString& id);
  void startOAuth();
  void saveCache();
  void saveConfig();
  void saveTokens(const OAuthTokens& tokens);
  void applyWidth(int width);
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
  bool narrow_ = false;
  bool narrowDetail_ = false;
  bool firstShow_ = true;
  bool resyncing_ = false;

  QLineEdit* search_ = nullptr;
  QListView* list_ = nullptr;
  ContactListModel* model_ = nullptr;
  ContactDelegate* delegate_ = nullptr;
  QWidget* listPane_ = nullptr;
  QWidget* detailPane_ = nullptr;
  QVBoxLayout* detailLayout_ = nullptr;
  QLabel* emptyLabel_ = nullptr;
  QLabel* countLabel_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QLabel* themeLabel_ = nullptr;
  QLabel* toastLabel_ = nullptr;
  QFrame* toastFrame_ = nullptr;
  QPushButton* backButton_ = nullptr;
  QPushButton* syncButton_ = nullptr;
  QPushButton* newButton_ = nullptr;
  QPushButton* accountButton_ = nullptr;
  QFileSystemWatcher* watcher_ = nullptr;
  QTimer* themeTimer_ = nullptr;
  QTimer* toastTimer_ = nullptr;
  QTcpServer* oauthServer_ = nullptr;
  QTimer* oauthTimer_ = nullptr;
  QByteArray oauthBuffer_;
  QString oauthVerifier_;
  QString oauthState_;
  QString oauthRedirect_;
};

}  // namespace omacontacts
