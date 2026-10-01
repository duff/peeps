#pragma once

#include "contact.h"

#include <QByteArray>

namespace contacts {

// A vCard 3.0 card, the version Google, iOS, and Android all import. `jpeg`
// becomes an inline PHOTO when it is not empty. Lines end in CRLF and fold at
// 75 bytes, as RFC 2426 asks.
QByteArray contactVCard(const Contact& contact, const QByteArray& jpeg = {});

}  // namespace contacts
