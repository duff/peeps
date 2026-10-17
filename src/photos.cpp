#include "photos.h"

#include "net.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace contacts {

QImage decodePhoto(const QByteArray& bytes) {
  QBuffer buffer;
  buffer.setData(bytes);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer);
  reader.setAutoTransform(true);
  return reader.read();
}

QByteArray photoUploadBytes(const QByteArray& original) {
  QBuffer buffer;
  buffer.setData(original);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer);
  // A JPEG that relies on its orientation tag would be re-encoded without it
  // and show up sideways, so only an upright one goes as it is.
  const bool uprightJpeg = reader.format() == "jpeg" && reader.transformation() == QImageIOHandler::TransformationNone;
  reader.setAutoTransform(true);
  const QImage image = reader.read();
  if (image.isNull()) return {};
  if (uprightJpeg) return original;
  QImage flat(image.size(), QImage::Format_RGB32);
  flat.fill(Qt::white);
  QPainter painter(&flat);
  painter.drawImage(0, 0, image);
  painter.end();
  QByteArray bytes;
  QBuffer out(&bytes);
  out.open(QIODevice::WriteOnly);
  if (!flat.save(&out, "JPEG", 100)) return {};
  return bytes;
}

std::optional<QByteArray> choosePhotoBytes(QWidget* parent) {
  const QString path = QFileDialog::getOpenFileName(
      parent, QStringLiteral("Choose a photo"), QString(),
      QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp)"));
  if (path.isEmpty()) return std::nullopt;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return QByteArray();
  return photoUploadBytes(file.readAll());
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

void PhotoStore::setAccessToken(const QString& token) {
  if (token == token_) return;
  token_ = token;
  // A photo refused under the old token may load under this one.
  const QHash<QString, QString> retry = std::exchange(failed_, {});
  for (auto it = retry.cbegin(); it != retry.cend(); ++it) enqueue(it.key(), it.value());
}

QString PhotoStore::filePath(const QString& resourceName) const {
  return directory_ + QLatin1Char('/') + safeName(resourceName);
}

QString PhotoStore::sizedPath(const QString& resourceName, int edge) const {
  return filePath(resourceName) + QStringLiteral("@%1.png").arg(edge);
}

bool PhotoStore::current(const QString& resourceName, const QString& url) const {
  QFile marker(filePath(resourceName) + QStringLiteral(".fetch"));
  return marker.open(QIODevice::ReadOnly) && QString::fromUtf8(marker.readAll()) == photoFetchUrl(url);
}

QPixmap PhotoStore::imageFor(const QString& resourceName, const QString& url, int logicalSize, qreal devicePixelRatio) {
  if (resourceName.isEmpty() || url.isEmpty() || logicalSize <= 0) return {};
  if (devicePixelRatio < 1.0) devicePixelRatio = 1.0;
  const QString key = cacheKey(resourceName, logicalSize, devicePixelRatio);
  const auto cached = memory_.constFind(key);
  if (cached != memory_.cend() && !cached->isNull()) return *cached;

  if (!current(resourceName, url)) {
    enqueue(resourceName, url);
    return {};
  }
  const int requested = qRound(logicalSize * devicePixelRatio);
  // A lossless copy at exactly this size, scaled from the original once. It
  // draws the same pixels as scaling the original, without decoding a photo
  // that can be 3200 pixels across each time the contact opens.
  const QString sized = sizedPath(resourceName, requested);
  QImage image(sized);
  if (image.isNull()) {
    QFile file(filePath(resourceName));
    const QImage full = file.open(QIODevice::ReadOnly) ? decodePhoto(file.readAll()) : QImage();
    if (full.isNull()) {
      // The download passed the header check but does not decode.
      discard(resourceName);
      failed_.insert(resourceName, url);
      return {};
    }
    const int longest = std::max(full.width(), full.height());
    const int edge = std::min(requested, std::max(1, longest));
    image = full.scaled(edge, edge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // PNG is lossless at any level, and a light one writes several times faster.
    QSaveFile copy(sized);
    if (copy.open(QIODevice::WriteOnly) && image.save(&copy, "PNG", 80)) copy.commit();
  }
  QPixmap pixmap = QPixmap::fromImage(image);
  pixmap.setDevicePixelRatio(devicePixelRatio);
  memory_.insert(key, pixmap);
  return pixmap;
}

QByteArray PhotoStore::originalBytes(const QString& resourceName, const QString& url) const {
  if (!current(resourceName, url)) return {};
  QFile file(filePath(resourceName));
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void PhotoStore::remember(const QString& resourceName, const QString& url, const QByteArray& bytes) {
  if (resourceName.isEmpty() || url.isEmpty() || bytes.isEmpty()) return;
  failed_.remove(resourceName);
  queuedUrls_.remove(resourceName);
  queue_.removeAll(resourceName);
  if (store(resourceName, url, bytes)) emit updated(resourceName);
}

void PhotoStore::forget(const QString& resourceName) {
  if (resourceName.isEmpty()) return;
  failed_.remove(resourceName);
  queuedUrls_.remove(resourceName);
  queue_.removeAll(resourceName);
  discard(resourceName);
  emit updated(resourceName);
}

// The original and its marker, written so that no copy of the photo it
// replaces is left to be drawn.
bool PhotoStore::store(const QString& resourceName, const QString& url, const QByteArray& bytes) {
  const QString path = filePath(resourceName);
  QDir().mkpath(directory_);
  dropSized(resourceName);
  QSaveFile file(path);
  if (!(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())) return false;
  writeFetchMarker(path, url);
  dropMemory(resourceName);
  return true;
}

void PhotoStore::discard(const QString& resourceName) {
  QFile::remove(filePath(resourceName));
  QFile::remove(filePath(resourceName) + QStringLiteral(".fetch"));
  QFile::remove(filePath(resourceName) + QStringLiteral(".url"));
  dropSized(resourceName);
  dropMemory(resourceName);
}

void PhotoStore::enqueue(const QString& resourceName, const QString& url) {
  if (failed_.contains(resourceName) || queuedUrls_.contains(resourceName)) return;
  if (current(resourceName, url)) return;
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

void PhotoStore::dropSized(const QString& resourceName) const {
  QDir dir(directory_);
  for (const QString& name : dir.entryList({safeName(resourceName) + QStringLiteral("@*.png")}, QDir::Files)) {
    dir.remove(name);
  }
}

void PhotoStore::finished(QNetworkReply* reply, const QString& resourceName, const QString& url) {
  reply->deleteLater();
  --active_;
  queuedUrls_.remove(resourceName);
  const QByteArray bytes = reply->readAll();
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  // Only the header is read here. The one full decode happens when the photo
  // is drawn, and a file that fails it is thrown away there.
  QBuffer buffer;
  buffer.setData(bytes);
  buffer.open(QIODevice::ReadOnly);
  const bool ok = reply->error() == QNetworkReply::NoError && status < 400 && QImageReader(&buffer).canRead();
  if (ok && store(resourceName, url, bytes)) emit updated(resourceName);
  else failed_.insert(resourceName, url);
  pump();
}

}  // namespace contacts
