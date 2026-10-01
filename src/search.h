#pragma once

#include "contact.h"

#include <QHash>
#include <QList>
#include <QString>

namespace contacts {

struct SearchHit {
  int index = 0;
  int score = 0;
  const Contact* contact = nullptr;
  // Shared copies of the index's cached strings. Building a row costs nothing.
  QString name;
  QString subtitle;
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
    QString display;
    QString subtitle;
    QString sortKey;
    QString name;
    QString emails;
    QString other;
    QString digits;
  };
  struct Token {
    QString text;
    QString digits;
    bool numeric = false;
  };

  QList<Entry> entries_;
  QHash<QString, int> positions_;

  void reindex();
  static Entry makeEntry(Contact contact);
  static int scoreEntry(const Entry& entry, const QList<Token>& tokens);
};

int moveIndex(int current, int count, int delta, bool wrap);

}  // namespace contacts
