#pragma once

#include "http.h"

#include <memory>

class QNetworkAccessManager;

namespace contacts {

// Runs requests one at a time on the calling thread. One instance keeps its
// connection open across requests, so a multi-page sync or a save followed by
// a photo upload pays for one TLS handshake. Use it on the thread that made it.
class BlockingNam final : public HttpClient {
 public:
  BlockingNam();
  ~BlockingNam() override;

  HttpResponse request(const QString& method, const QString& url, const QList<QPair<QString, QString>>& headers,
                       const QByteArray& body) override;

 private:
  std::unique_ptr<QNetworkAccessManager> network_;
};

}  // namespace contacts
