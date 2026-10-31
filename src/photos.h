#pragma once

#include "google.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QString>

#include <optional>

class QMimeData;
class QNetworkAccessManager;
class QNetworkReply;
class QWidget;

namespace contacts {

// Decodes with the photo's orientation applied, so a phone picture is upright.
QImage decodePhoto(const QByteArray& bytes);
// The bytes Peeps uploads and puts in a vCard, at full quality. An upright
// JPEG goes as it is, byte for byte. Anything else is turned upright, laid on
// white, and encoded as a quality 100 JPEG at its full size.
QByteArray photoUploadBytes(const QByteArray& original);
std::optional<QByteArray> choosePhotoBytes(QWidget* parent);

// True when a drag may carry a photo: files, or an image itself. Only the
// formats are looked at, as reading another app's data waits on that app,
// which may still be readying the photo.
bool mayHoldPhoto(const QMimeData* data);

// The photo a drop carries, as photoUploadBytes() makes it, or why there is
// none, in words for the window.
struct DroppedPhoto {
  QByteArray bytes;
  QString error;
};
DroppedPhoto droppedPhoto(const QMimeData* data);

struct PhotoSync {
  int downloaded = 0;
  int failed = 0;
};

// Makes `directory` hold every contact's photo as Google has it: downloads
// each one that is missing or has changed, one at a time, and deletes those
// of contacts that are gone or no longer have a photo. Other programs read
// the folder, so it follows the whole book, not just the photos on screen.
PhotoSync syncPhotos(HttpClient& http, const QString& accessToken, const QString& directory,
                     const QList<Contact>& contacts);

// Downloads the original contact photo and keeps those bytes on disk.
// `logicalSize` is the on-screen size in pixels; the returned pixmap is
// rendered at the screen's device pixel ratio so it stays sharp.
class PhotoStore : public QObject {
  Q_OBJECT
 public:
  explicit PhotoStore(QString directory, QObject* parent = nullptr);

  void setAccessToken(const QString& token);
  QPixmap imageFor(const QString& resourceName, const QString& url, int logicalSize, qreal devicePixelRatio);
  // The original file as Google sent it. Empty until it has downloaded.
  QByteArray originalBytes(const QString& resourceName, const QString& url) const;
  void remember(const QString& resourceName, const QString& url, const QByteArray& bytes);
  void forget(const QString& resourceName);
  // What syncPhotos() does, in the background. A photo on screen goes first.
  void sync(const QList<Contact>& contacts);

 signals:
  void updated(const QString& resourceName);

 private:
  void enqueue(const QString& resourceName, const QString& url, bool urgent);
  void pump();
  void finished(QNetworkReply* reply, const QString& resourceName, const QString& url);
  bool store(const QString& resourceName, const QString& url, const QByteArray& bytes);
  void discard(const QString& resourceName);
  void dropMemory(const QString& resourceName);
  QString filePath(const QString& resourceName) const;
  QString sizedPath(const QString& resourceName, int edge) const;
  bool current(const QString& resourceName, const QString& url) const;

  QString directory_;
  QString token_;
  QNetworkAccessManager* network_ = nullptr;
  QHash<QString, QPixmap> memory_;
  QStringList queue_;
  // Photos waiting or downloading. A download whose entry is gone or has
  // another URL by the time it lands is thrown away.
  QHash<QString, QString> queuedUrls_;
  // Photos that did not download this session, with their URLs for a retry.
  QHash<QString, QString> failed_;
  int active_ = 0;
};

}  // namespace contacts
