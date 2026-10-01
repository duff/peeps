#include "search.h"

#include <QRegularExpression>

#include <algorithm>

namespace contacts {
namespace {

int scoreField(const QString& field, const QString& token, int prefix, int word, int substr) {
  if (token.isEmpty() || field.size() < token.size()) return 0;
  if (field.startsWith(token)) return prefix;
  int best = 0;
  int from = 0;
  while ((from = field.indexOf(token, from)) >= 0) {
    const QChar before = field.at(from - 1);
    if (before == QLatin1Char(' ') || before == QLatin1Char('@') || before == QLatin1Char('.')) return word;
    best = substr;
    ++from;
  }
  return best;
}

bool bySortKey(const QString& a, const QString& b) { return a < b; }

}  // namespace

ContactIndex::Entry ContactIndex::makeEntry(Contact contact) {
  Entry entry;
  entry.display = contact.displayName();
  entry.subtitle = contact.subtitle();
  entry.sortKey = entry.display.toCaseFolded();
  entry.name = (contact.givenName + QLatin1Char(' ') + contact.familyName + QLatin1Char(' ') + entry.display).toCaseFolded();
  QStringList emails;
  emails.reserve(contact.emails.size());
  for (const auto& email : contact.emails) emails.append(email.value.toCaseFolded());
  entry.emails = emails.join(QLatin1Char(' '));
  entry.other = contact.searchText().toCaseFolded();
  for (const auto& phone : contact.phones) entry.digits += digitsOnly(phone.value);
  entry.contact = std::move(contact);
  return entry;
}

void ContactIndex::reindex() {
  positions_.clear();
  positions_.reserve(entries_.size());
  for (int i = 0; i < entries_.size(); ++i) positions_.insert(entries_.at(i).contact.resourceName, i);
}

void ContactIndex::replaceAll(QList<Contact> contacts) {
  entries_.clear();
  entries_.reserve(contacts.size());
  for (auto& contact : contacts) entries_.append(makeEntry(std::move(contact)));
  std::stable_sort(entries_.begin(), entries_.end(),
                   [](const Entry& a, const Entry& b) { return bySortKey(a.sortKey, b.sortKey); });
  reindex();
}

void ContactIndex::upsert(Contact contact) {
  const auto existing = positions_.constFind(contact.resourceName);
  if (existing != positions_.cend()) entries_.removeAt(existing.value());
  Entry entry = makeEntry(std::move(contact));
  const auto place = std::upper_bound(entries_.begin(), entries_.end(), entry,
                                      [](const Entry& a, const Entry& b) { return bySortKey(a.sortKey, b.sortKey); });
  entries_.insert(place, std::move(entry));
  reindex();
}

bool ContactIndex::remove(const QString& resourceName) {
  const auto existing = positions_.constFind(resourceName);
  if (existing == positions_.cend()) return false;
  entries_.removeAt(existing.value());
  reindex();
  return true;
}

const Contact* ContactIndex::find(const QString& resourceName) const {
  const auto it = positions_.constFind(resourceName);
  return it == positions_.cend() ? nullptr : &entries_.at(it.value()).contact;
}

int ContactIndex::size() const { return static_cast<int>(entries_.size()); }

const Contact& ContactIndex::at(int index) const { return entries_.at(index).contact; }

QList<Contact> ContactIndex::all() const {
  QList<Contact> contacts;
  contacts.reserve(entries_.size());
  for (const auto& entry : entries_) contacts.append(entry.contact);
  return contacts;
}

int ContactIndex::scoreEntry(const Entry& entry, const QList<Token>& tokens) {
  int total = 0;
  for (const Token& token : tokens) {
    int best = scoreField(entry.name, token.text, 1000, 800, 400);
    if (best < 1000) best = std::max(best, scoreField(entry.emails, token.text, 700, 600, 300));
    if (best < 250) best = std::max(best, scoreField(entry.other, token.text, 250, 200, 100));
    if (!entry.digits.isEmpty() && token.digits.size() >= (token.numeric ? 1 : 3)) {
      const int at = entry.digits.indexOf(token.digits);
      if (at >= 0) best = std::max(best, token.numeric && at == 0 ? 650 : 250);
    }
    if (best == 0) return 0;
    total += best;
  }
  return total;
}

QList<SearchHit> ContactIndex::query(const QString& text) const {
  static const QRegularExpression whitespace(QStringLiteral("\\s+"));
  QList<Token> tokens;
  for (const QString& part : text.toCaseFolded().split(whitespace, Qt::SkipEmptyParts)) {
    Token token{part, digitsOnly(part)};
    token.numeric = !token.digits.isEmpty() && token.digits.size() == part.size();
    tokens.append(token);
  }
  QList<SearchHit> hits;
  if (tokens.isEmpty()) hits.reserve(entries_.size());
  for (int i = 0; i < entries_.size(); ++i) {
    const Entry& entry = entries_.at(i);
    const int score = tokens.isEmpty() ? 1 : scoreEntry(entry, tokens);
    if (score > 0) hits.append(SearchHit{i, score, &entry.contact, entry.display, entry.subtitle});
  }
  // Entries are already in name order, so a stable sort keeps ties alphabetical.
  if (!tokens.isEmpty()) {
    std::stable_sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) { return a.score > b.score; });
  }
  return hits;
}

int moveIndex(int current, int count, int delta, bool wrap) {
  if (count <= 0) return 0;
  if (current < 0 || current >= count) current = 0;
  if (delta == 0) return current;
  int next = current + delta;
  if (wrap) {
    next %= count;
    if (next < 0) next += count;
    return next;
  }
  return std::clamp(next, 0, count - 1);
}

}  // namespace contacts
