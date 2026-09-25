#include "photos.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>

namespace omacontacts {
namespace {

constexpr int kMaxActive = 4;

QString safeName(const QString& resourceName) {
  QString name = resourceName;
  name.replace(QLatin1Char('/'), QLatin1Char('_'));
  name.replace(QLatin1Char('\\'), QLatin1Char('_'));
  return name;
}

QString cacheKey(const QString& resourceName, int logicalSize, qreal devicePixelRatio) {
  return resourceName + QStringLiteral("@%1@%2").arg(logicalSize).arg(devicePixelRatio, 0, 'f', 2);
}

}  // namespace

PhotoStore::PhotoStore(QString directory, QObject* parent) : QObject(parent), directory_(std::move(directory)) {
  network_ = new QNetworkAccessManager(this);
  QDir().mkpath(directory_);
}

void PhotoStore::setAccessToken(const QString& token) { token_ = token; }

QString PhotoStore::filePath(const QString& resourceName) const {
  return directory_ + QLatin1Char('/') + safeName(resourceName);
}

QImage PhotoStore::loadFull(const QString& resourceName, const QString& url) const {
  const QString path = filePath(resourceName);
  QFile marker(path + QStringLiteral(".fetch"));
  if (!marker.open(QIODevice::ReadOnly)) return {};
  if (QString::fromUtf8(marker.readAll()) != photoFetchUrl(url)) return {};
  return QImage(path);
}

QPixmap PhotoStore::imageFor(const QString& resourceName, const QString& url, int logicalSize, qreal devicePixelRatio) {
  if (resourceName.isEmpty() || url.isEmpty() || logicalSize <= 0) return {};
  if (devicePixelRatio < 1.0) devicePixelRatio = 1.0;
  const QString key = cacheKey(resourceName, logicalSize, devicePixelRatio);
  const auto cached = memory_.constFind(key);
  if (cached != memory_.cend() && !cached->isNull()) return *cached;

  const QImage full = loadFull(resourceName, url);
  if (full.isNull()) {
    enqueue(resourceName, url);
    return {};
  }
  const int requested = qRound(logicalSize * devicePixelRatio);
  const int longest = std::max(full.width(), full.height());
  const int edge = std::min(requested, std::max(1, longest));
  const QImage scaled = full.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QPixmap pixmap = QPixmap::fromImage(scaled);
  pixmap.setDevicePixelRatio(devicePixelRatio);
  memory_.insert(key, pixmap);
  return pixmap;
}

void PhotoStore::enqueue(const QString& resourceName, const QString& url) {
  if (failed_.contains(resourceName) || queuedUrls_.contains(resourceName)) return;
  if (!loadFull(resourceName, url).isNull()) return;
  queuedUrls_.insert(resourceName, url);
  queue_.append(resourceName);
  pump();
}

void PhotoStore::pump() {
  while (active_ < kMaxActive && !queue_.isEmpty()) {
    const QString resourceName = queue_.takeFirst();
    const QString url = queuedUrls_.value(resourceName);
    const QUrl fetchUrl(photoFetchUrl(url));
    if (!fetchUrl.isValid()) {
      queuedUrls_.remove(resourceName);
      continue;
    }
    QNetworkRequest request(fetchUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Omacontacts/0.1"));
    if (!token_.isEmpty()) request.setRawHeader("Authorization", QByteArray("Bearer ") + token_.toUtf8());
    QNetworkReply* reply = network_->get(request);
    ++active_;
    connect(reply, &QNetworkReply::finished, this, [this, reply, resourceName, url] { finished(reply, resourceName, url); });
  }
}

void PhotoStore::dropMemory(const QString& resourceName) {
  const QString prefix = resourceName + QLatin1Char('@');
  for (auto it = memory_.begin(); it != memory_.end();) {
    if (it.key().startsWith(prefix)) it = memory_.erase(it);
    else ++it;
  }
}

void PhotoStore::finished(QNetworkReply* reply, const QString& resourceName, const QString& url) {
  reply->deleteLater();
  --active_;
  queuedUrls_.remove(resourceName);
  const QByteArray bytes = reply->readAll();
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  QImage image;
  const bool ok = reply->error() == QNetworkReply::NoError && status < 400 && image.loadFromData(bytes) && !image.isNull();
  if (!ok) failed_.insert(resourceName);
  if (ok) {
    const QString path = filePath(resourceName);
    QDir().mkpath(directory_);
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) {
      QFile marker(path + QStringLiteral(".fetch"));
      if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) marker.write(photoFetchUrl(url).toUtf8());
      dropMemory(resourceName);
      emit updated(resourceName);
    } else {
      failed_.insert(resourceName);
    }
  }
  pump();
}

}  // namespace omacontacts
