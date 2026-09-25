#pragma once

#include "google.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace omacontacts {

// Downloads the original contact photo and keeps those bytes on disk.
// `logicalSize` is the on-screen size in pixels; the returned pixmap is
// rendered at the screen's device pixel ratio so it stays sharp.
class PhotoStore : public QObject {
  Q_OBJECT
 public:
  explicit PhotoStore(QString directory, QObject* parent = nullptr);

  void setAccessToken(const QString& token);
  QPixmap imageFor(const QString& resourceName, const QString& url, int logicalSize, qreal devicePixelRatio);

 signals:
  void updated(const QString& resourceName);

 private:
  void enqueue(const QString& resourceName, const QString& url);
  void pump();
  void finished(QNetworkReply* reply, const QString& resourceName, const QString& url);
  void dropMemory(const QString& resourceName);
  QString filePath(const QString& resourceName) const;
  QImage loadFull(const QString& resourceName, const QString& url) const;

  QString directory_;
  QString token_;
  QNetworkAccessManager* network_ = nullptr;
  QHash<QString, QPixmap> memory_;
  QStringList queue_;
  QHash<QString, QString> queuedUrls_;
  QSet<QString> failed_;
  int active_ = 0;
};

}  // namespace omacontacts
