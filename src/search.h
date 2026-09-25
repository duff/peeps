#pragma once

#include "contact.h"

#include <QList>
#include <QString>

namespace omacontacts {

struct SearchHit {
  int index = 0;
  int score = 0;
  const Contact* contact = nullptr;
};

// Precomputed casefolded fields. A query is one pass over memory.
class ContactIndex {
 public:
  void replaceAll(QList<Contact> contacts);
  void upsert(Contact contact);
  bool remove(const QString& resourceName);
  const Contact* find(const QString& resourceName) const;
  int size() const;
  const Contact& at(int index) const;
  QList<Contact> all() const;

  // Empty text returns every contact in display-name order.
  // Tokens are ANDed. A name hit outranks email, phone, and notes.
  QList<SearchHit> query(const QString& text) const;

 private:
  struct Entry {
    Contact contact;
    QString name;
    QString emails;
    QString other;
    QString digits;
    QString sortKey;
  };

  QList<Entry> entries_;

  static Entry makeEntry(Contact contact);
  static int scoreEntry(const Entry& entry, const QStringList& tokens);
};

int moveIndex(int current, int count, int delta, bool wrap);

}  // namespace omacontacts
