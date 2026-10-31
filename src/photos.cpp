#include "photos.h"

#include "net.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
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

bool isImageFormat(const QString& format) { return format.startsWith(QLatin1String("image/")); }

}  // namespace

bool mayHoldPhoto(const QMimeData* data) {
  if (data == nullptr) return false;
  if (data->hasUrls()) return true;
  const QStringList formats = data->formats();
  return std::any_of(formats.begin(), formats.end(), isImageFormat);
}

DroppedPhoto droppedPhoto(const QMimeData* data) {
  const QString unreadable = QStringLiteral("Could not read that image.");
  if (data == nullptr) return {{}, unreadable};
  bool remote = false;
  // An app that readies the file once the drag starts may not have it yet.
  // Qt stops waiting after a moment and reads nothing.
  bool unready = false;
  if (data->hasUrls()) {
    const QList<QUrl> urls = data->urls();
    unready = urls.isEmpty();
    for (const QUrl& url : urls) {
      if (!url.isLocalFile()) {
        remote = true;
        continue;
      }
      // The first file is the photo, as the editor holds one.
      QFile file(url.toLocalFile());
      const QByteArray bytes = file.open(QIODevice::ReadOnly) ? photoUploadBytes(file.readAll()) : QByteArray();
      if (bytes.isEmpty()) return {{}, unreadable};
      return {bytes, {}};
    }
  }
  // An app can hand over the picture itself. A JPEG goes first, as one that
  // is upright goes to Google byte for byte.
  QStringList formats = data->formats();
  formats.removeIf([](const QString& format) { return !isImageFormat(format); });
  std::stable_partition(formats.begin(), formats.end(),
                        [](const QString& format) { return format == QLatin1String("image/jpeg"); });
  for (const QString& format : formats) {
    const QByteArray bytes = photoUploadBytes(data->data(format));
    if (!bytes.isEmpty()) return {bytes, {}};
  }
  if (unready) return {{}, QStringLiteral("That photo wasn’t ready. Try again in a moment.")};
  if (remote) return {{}, QStringLiteral("Peeps takes photos from files on this computer. Save the image, then drop the file.")};
  return {{}, unreadable};
}

namespace {

constexpr int kMaxActive = 4;

QString safeName(const QString& resourceName) {
  QString name = resourceName;
  name.replace(QLatin1Char('/'), QLatin1Char('_'));
  name.replace(QLatin1Char('\\'), QLatin1Char('_'));
  return name;
}

// Photos are as private as the contacts, so only this user may open the folder,
// including one an older Peeps made open to everyone.
void makePrivateDir(const QString& path) {
  QDir().mkpath(path);
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
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

// Each contact's original is named for its resource name, people_c123, with
// its marker and sized copies beside it as people_c123.fetch and
// people_c123@240.png.
QString photoFile(const QString& directory, const QString& resourceName) {
  return directory + QLatin1Char('/') + safeName(resourceName);
}

bool photoCurrent(const QString& directory, const QString& resourceName, const QString& url) {
  QFile marker(photoFile(directory, resourceName) + QStringLiteral(".fetch"));
  return marker.open(QIODevice::ReadOnly) && QString::fromUtf8(marker.readAll()) == photoFetchUrl(url);
}

void removeSized(const QString& directory, const QString& resourceName) {
  QDir dir(directory);
  for (const QString& name : dir.entryList({safeName(resourceName) + QStringLiteral("@*.png")}, QDir::Files)) {
    dir.remove(name);
  }
}

// The original and its marker, written so that no copy of the photo it
// replaces is left to be drawn.
bool writePhoto(const QString& directory, const QString& resourceName, const QString& url, const QByteArray& bytes) {
  const QString path = photoFile(directory, resourceName);
  makePrivateDir(directory);
  removeSized(directory, resourceName);
  QSaveFile file(path);
  if (!(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit())) return false;
  writeFetchMarker(path, url);
  return true;
}

// Only the header is read here. The one full decode happens when the photo
// is drawn, and a file that fails it is thrown away there.
bool looksLikeImage(const QByteArray& bytes) {
  QBuffer buffer;
  buffer.setData(bytes);
  buffer.open(QIODevice::ReadOnly);
  return QImageReader(&buffer).canRead();
}

// Deletes every file of the contacts whose safe names are not in `keep`.
void prunePhotos(const QString& directory, const QSet<QString>& keep) {
  static const QRegularExpression suffix(QStringLiteral("[.@].*$"));
  QDir dir(directory);
  for (const QString& name : dir.entryList({QStringLiteral("people_*")}, QDir::Files)) {
    if (!keep.contains(QString(name).remove(suffix))) dir.remove(name);
  }
}

QSet<QString> photoNames(const QList<Contact>& contacts) {
  QSet<QString> names;
  for (const Contact& contact : contacts) {
    if (!contact.resourceName.isEmpty() && !contact.photoUrl.isEmpty()) names.insert(safeName(contact.resourceName));
  }
  return names;
}

}  // namespace

PhotoSync syncPhotos(HttpClient& http, const QString& accessToken, const QString& directory,
                     const QList<Contact>& contacts) {
  PhotoSync result;
  makePrivateDir(directory);
  QList<QPair<QString, QString>> headers;
  if (!accessToken.isEmpty()) headers.append({QStringLiteral("Authorization"), QStringLiteral("Bearer ") + accessToken});
  bool unreachable = false;
  for (const Contact& contact : contacts) {
    if (contact.resourceName.isEmpty() || contact.photoUrl.isEmpty()) continue;
    if (photoCurrent(directory, contact.resourceName, contact.photoUrl)) continue;
    // Once Google is out of reach, each photo left would wait out a timeout.
    if (unreachable) {
      ++result.failed;
      continue;
    }
    const HttpResponse response = http.request(QStringLiteral("GET"), photoFetchUrl(contact.photoUrl), headers, {});
    unreachable = response.status == 0;
    const bool ok = response.status > 0 && response.status < 400 && looksLikeImage(response.body) &&
                    writePhoto(directory, contact.resourceName, contact.photoUrl, response.body);
    if (ok) ++result.downloaded;
    else ++result.failed;
  }
  prunePhotos(directory, photoNames(contacts));
  return result;
}

PhotoStore::PhotoStore(QString directory, QObject* parent) : QObject(parent), directory_(std::move(directory)) {
  network_ = new QNetworkAccessManager(this);
  network_->setTransferTimeout(30000);
  makePrivateDir(directory_);
}

void PhotoStore::setAccessToken(const QString& token) {
  if (token == token_) return;
  token_ = token;
  // A photo refused under the old token may load under this one.
  const QHash<QString, QString> retry = std::exchange(failed_, {});
  for (auto it = retry.cbegin(); it != retry.cend(); ++it) enqueue(it.key(), it.value(), false);
}

QString PhotoStore::filePath(const QString& resourceName) const { return photoFile(directory_, resourceName); }

QString PhotoStore::sizedPath(const QString& resourceName, int edge) const {
  return filePath(resourceName) + QStringLiteral("@%1.png").arg(edge);
}

bool PhotoStore::current(const QString& resourceName, const QString& url) const {
  return photoCurrent(directory_, resourceName, url);
}

QPixmap PhotoStore::imageFor(const QString& resourceName, const QString& url, int logicalSize, qreal devicePixelRatio) {
  if (resourceName.isEmpty() || url.isEmpty() || logicalSize <= 0) return {};
  if (devicePixelRatio < 1.0) devicePixelRatio = 1.0;
  const QString key = cacheKey(resourceName, logicalSize, devicePixelRatio);
  const auto cached = memory_.constFind(key);
  if (cached != memory_.cend() && !cached->isNull()) return *cached;

  if (!current(resourceName, url)) {
    enqueue(resourceName, url, true);
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

void PhotoStore::sync(const QList<Contact>& contacts) {
  for (const Contact& contact : contacts) {
    if (!contact.resourceName.isEmpty() && !contact.photoUrl.isEmpty()) enqueue(contact.resourceName, contact.photoUrl, false);
  }
  const QSet<QString> keep = photoNames(contacts);
  const auto gone = [&keep](const QString& resourceName) { return !keep.contains(safeName(resourceName)); };
  for (auto it = queuedUrls_.begin(); it != queuedUrls_.end();) {
    if (!gone(it.key())) {
      ++it;
      continue;
    }
    queue_.removeAll(it.key());
    it = queuedUrls_.erase(it);
  }
  failed_.removeIf([&gone](QHash<QString, QString>::iterator it) { return gone(it.key()); });
  memory_.removeIf([&gone](QHash<QString, QPixmap>::iterator it) { return gone(it.key().section(QLatin1Char('@'), 0, 0)); });
  prunePhotos(directory_, keep);
}

bool PhotoStore::store(const QString& resourceName, const QString& url, const QByteArray& bytes) {
  if (!writePhoto(directory_, resourceName, url, bytes)) return false;
  dropMemory(resourceName);
  return true;
}

void PhotoStore::discard(const QString& resourceName) {
  QFile::remove(filePath(resourceName));
  QFile::remove(filePath(resourceName) + QStringLiteral(".fetch"));
  QFile::remove(filePath(resourceName) + QStringLiteral(".url"));
  removeSized(directory_, resourceName);
  dropMemory(resourceName);
}

void PhotoStore::enqueue(const QString& resourceName, const QString& url, bool urgent) {
  // A photo that failed waits for a new token, unless Google has a new one.
  if (failed_.value(resourceName) == url) return;
  if (queuedUrls_.contains(resourceName)) {
    // The photo someone opened goes ahead of the rest of the book.
    if (urgent && queue_.removeOne(resourceName)) queue_.prepend(resourceName);
    return;
  }
  if (current(resourceName, url)) return;
  queuedUrls_.insert(resourceName, url);
  if (urgent) queue_.prepend(resourceName);
  else queue_.append(resourceName);
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
  // A photo saved, removed, or synced away meanwhile wins over this one.
  const auto queued = queuedUrls_.constFind(resourceName);
  if (queued == queuedUrls_.cend() || *queued != url) {
    pump();
    return;
  }
  queuedUrls_.erase(queued);
  const QByteArray bytes = reply->readAll();
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const bool ok = reply->error() == QNetworkReply::NoError && status < 400 && looksLikeImage(bytes);
  if (ok && store(resourceName, url, bytes)) emit updated(resourceName);
  else failed_.insert(resourceName, url);
  pump();
}

}  // namespace contacts
