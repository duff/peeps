#include "blockingnam.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace omacontacts {

HttpResponse BlockingNam::request(const QString& method, const QString& url,
                                  const QList<QPair<QString, QString>>& headers, const QByteArray& body) {
  HttpResponse response;
  response.url = url;
  QNetworkAccessManager network;
  QNetworkRequest request{QUrl(url)};
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Omacontacts/0.1"));
  for (const auto& header : headers) {
    request.setRawHeader(header.first.toUtf8(), header.second.toUtf8());
  }
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QNetworkReply* reply = network.sendCustomRequest(request, method.toUtf8(), body);
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

}  // namespace omacontacts
