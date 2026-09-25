#pragma once

#include "http.h"

namespace contacts {

class BlockingNam final : public HttpClient {
 public:
  HttpResponse request(const QString& method, const QString& url, const QList<QPair<QString, QString>>& headers,
                       const QByteArray& body) override;
};

}  // namespace contacts
