#include "photos.h"

#include "net.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>

namespace contacts {

QByteArray photoUploadBytes(const QImage& image) {
  if (image.isNull()) return {};
  QImage working = image;
  const int longest = std::max(working.width(), working.height());
  if (longest > 1024) working = working.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  QImage flat(working.size(), QImage::Format_RGB32);
  flat.fill(Qt::white);
  QPainter painter(&flat);
  painter.drawImage(0, 0, working);
  painter.end();
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  if (!flat.save(&buffer, "JPEG", 85)) return {};
  return bytes;
}

std::optional<QByteArray> choosePhotoBytes(QWidget* parent) {
  const QString path = QFileDialog::getOpenFileName(
      parent, QStringLiteral("Choose a photo"), QString(),
      QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp)"));
  if (path.isEmpty()) return std::nullopt;
  const QByteArray bytes = photoUploadBytes(QImage(path));
  if (bytes.isEmpty()) return QByteArray();
  return bytes;
}

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

// Callers emit updated() as soon as this returns, and that handler reads the
// marker. QFile keeps the write in a buffer, so close it here. Otherwise the
// handler sees an empty file, treats the photo as missing, and downloads again.
void writeFetchMarker(const QString& imagePath, const QString& url) {
  QFile marker(imagePath + QStringLiteral(".fetch"));
  if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
  marker.write(photoFetchUrl(url).toUtf8());
  marker.close();
}

}  // namespace

PhotoStore::PhotoStore(QString directory, QObject* parent) : QObject(parent), directory_(std::move(directory)) {
  network_ = new QNetworkAccessManager(this);
  network_->setTransferTimeout(30000);
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

QImage PhotoStore::originalImage(const QString& resourceName, const QString& url) const {
  return loadFull(resourceName, url);
}

void PhotoStore::remember(const QString& resourceName, const QString& url, const QByteArray& bytes) {
  if (resourceName.isEmpty() || url.isEmpty() || bytes.isEmpty()) return;
  failed_.remove(resourceName);
  queuedUrls_.remove(resourceName);
  queue_.removeAll(resourceName);
  const QString path = filePath(resourceName);
  QDir().mkpath(directory_);
  QSaveFile file(path);
  if (!(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())) return;
  writeFetchMarker(path, url);
  dropMemory(resourceName);
  emit updated(resourceName);
}

void PhotoStore::forget(const QString& resourceName) {
  if (resourceName.isEmpty()) return;
  failed_.remove(resourceName);
  queuedUrls_.remove(resourceName);
  queue_.removeAll(resourceName);
  QFile::remove(filePath(resourceName));
  QFile::remove(filePath(resourceName) + QStringLiteral(".fetch"));
  QFile::remove(filePath(resourceName) + QStringLiteral(".url"));
  dropMemory(resourceName);
  emit updated(resourceName);
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
    prepareNetworkRequest(request);
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
      writeFetchMarker(path, url);
      dropMemory(resourceName);
      emit updated(resourceName);
    } else {
      failed_.insert(resourceName);
    }
  }
  pump();
}

}  // namespace contacts
