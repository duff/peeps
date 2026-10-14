#include "blockingnam.h"

#include "net.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>

namespace contacts {

BlockingNam::BlockingNam() = default;

BlockingNam::~BlockingNam() = default;

HttpResponse BlockingNam::request(const QString& method, const QString& url,
                                  const QList<QPair<QString, QString>>& headers, const QByteArray& body) {
  HttpResponse response;
  response.url = url;
  if (backgroundStopped()) {
    response.error = QStringLiteral("Peeps is closing.");
    return response;
  }
  if (!network_) {
    network_ = std::make_unique<QNetworkAccessManager>();
    network_->setTransferTimeout(30000);
  }
  QNetworkRequest request{QUrl(url)};
  prepareNetworkRequest(request);
  for (const auto& header : headers) {
    request.setRawHeader(header.first.toUtf8(), header.second.toUtf8());
  }
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QNetworkReply* reply = network_->sendCustomRequest(request, method.toUtf8(), body);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(60000);
  loop.exec();
  if (!reply->isFinished()) {
    reply->abort();
    response.error = QStringLiteral("The request to Google timed out.");
    reply->deleteLater();
    return response;
  }
  if (reply->error() != QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isNull()) {
    response.error = reply->errorString();
    reply->deleteLater();
    return response;
  }
  response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  response.body = reply->readAll();
  reply->deleteLater();
  return response;
}

}  // namespace contacts
