#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

namespace omacontacts {

struct HttpResponse {
  int status = 0;
  QByteArray body;
  QString error;
  QString url;
};

class HttpClient {
 public:
  virtual ~HttpClient() = default;
  virtual HttpResponse request(const QString& method, const QString& url,
                               const QList<QPair<QString, QString>>& headers, const QByteArray& body) = 0;
};

}  // namespace omacontacts
