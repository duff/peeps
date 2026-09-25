#pragma once

#include "worker.h"

#include <QNetworkRequest>
#include <QSslConfiguration>

namespace omacontacts {

// Qt 6 negotiates HTTP/2 by default. That helper thread can enter a userspace
// spin on a Google connection and never read its event queue again, so one
// core stays busy and closing the window waits forever to join the thread.
// HTTP/1.1 does not use that code.
inline void prepareNetworkRequest(QNetworkRequest& request) {
  if (backgroundStopped()) return;
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Contacts/0.1"));
  request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
  request.setAttribute(QNetworkRequest::Http2DirectAttribute, false);
  request.setAttribute(QNetworkRequest::Http2CleartextAllowedAttribute, false);
  request.setTransferTimeout(30000);
  QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
  ssl.setAllowedNextProtocols({QByteArray(QSslConfiguration::NextProtocolHttp1_1)});
  request.setSslConfiguration(ssl);
}

}  // namespace omacontacts
