#include "search.h"

#include <QRegularExpression>

#include <algorithm>

namespace omacontacts {
namespace {

int scoreField(const QString& field, const QString& token, int prefix, int word, int substr) {
  if (token.isEmpty() || field.size() < token.size()) return 0;
  if (field.startsWith(token)) return prefix;
  int best = 0;
  int from = 0;
  while ((from = field.indexOf(token, from)) >= 0) {
    const bool boundary = from == 0 || field.at(from - 1) == QLatin1Char(' ') || field.at(from - 1) == QLatin1Char('@') ||
                          field.at(from - 1) == QLatin1Char('.');
    best = std::max(best, boundary ? word : substr);
    ++from;
  }
  return best;
}

QStringList tokensOf(const QString& text) {
  return text.toCaseFolded().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
}

}  // namespace

ContactIndex::Entry ContactIndex::makeEntry(Contact contact) {
  Entry entry;
  entry.sortKey = contact.displayName().toCaseFolded();
  entry.name = (contact.givenName + QLatin1Char(' ') + contact.familyName + QLatin1Char(' ') + contact.displayName())
                   .toCaseFolded();
  QStringList emails;
  for (const auto& email : contact.emails) emails.append(email.value.toCaseFolded());
  entry.emails = emails.join(QLatin1Char(' '));
  entry.other = contact.searchText().toCaseFolded();
  for (const auto& phone : contact.phones) entry.digits += digitsOnly(phone.value);
  entry.contact = std::move(contact);
  return entry;
}

void ContactIndex::replaceAll(QList<Contact> contacts) {
  entries_.clear();
  entries_.reserve(contacts.size());
  for (auto& contact : contacts) entries_.append(makeEntry(std::move(contact)));
  std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.sortKey < b.sortKey; });
}

void ContactIndex::upsert(Contact contact) {
  const QString id = contact.resourceName;
  Entry entry = makeEntry(std::move(contact));
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&](const Entry& existing) { return existing.contact.resourceName == id; }),
                 entries_.end());
  const auto place = std::lower_bound(entries_.begin(), entries_.end(), entry,
                                       [](const Entry& a, const Entry& b) { return a.sortKey < b.sortKey; });
  entries_.insert(place, std::move(entry));
}

bool ContactIndex::remove(const QString& resourceName) {
  const auto before = entries_.size();
  entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                [&](const Entry& entry) { return entry.contact.resourceName == resourceName; }),
                 entries_.end());
  return entries_.size() != before;
}

const Contact* ContactIndex::find(const QString& resourceName) const {
  const auto it = std::find_if(entries_.cbegin(), entries_.cend(),
                               [&](const Entry& entry) { return entry.contact.resourceName == resourceName; });
  return it == entries_.cend() ? nullptr : &it->contact;
}

int ContactIndex::size() const { return entries_.size(); }

const Contact& ContactIndex::at(int index) const { return entries_.at(index).contact; }

QList<Contact> ContactIndex::all() const {
  QList<Contact> contacts;
  contacts.reserve(entries_.size());
  for (const auto& entry : entries_) contacts.append(entry.contact);
  return contacts;
}

int ContactIndex::scoreEntry(const Entry& entry, const QStringList& tokens) {
  int total = 0;
  for (const QString& token : tokens) {
    int best = scoreField(entry.name, token, 1000, 800, 400);
    best = std::max(best, scoreField(entry.emails, token, 700, 600, 300));
    best = std::max(best, scoreField(entry.other, token, 250, 200, 100));
    const QString digits = digitsOnly(token);
    const bool numeric = !digits.isEmpty() && digits.size() == token.size();
    if (numeric && !entry.digits.isEmpty() && entry.digits.contains(digits)) {
      best = std::max(best, entry.digits.startsWith(digits) ? 650 : 250);
    }
    if (!numeric && digits.size() >= 3 && entry.digits.contains(digits)) best = std::max(best, 250);
    if (best == 0) return 0;
    total += best;
  }
  return total;
}

QList<SearchHit> ContactIndex::query(const QString& text) const {
  QList<SearchHit> hits;
  const QStringList tokens = tokensOf(text);
  if (tokens.isEmpty()) {
    hits.reserve(entries_.size());
    for (int i = 0; i < entries_.size(); ++i) hits.append(SearchHit{i, 1, &entries_.at(i).contact});
    return hits;
  }
  for (int i = 0; i < entries_.size(); ++i) {
    const int score = scoreEntry(entries_.at(i), tokens);
    if (score > 0) hits.append(SearchHit{i, score, &entries_.at(i).contact});
  }
  std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
    if (a.score != b.score) return a.score > b.score;
    return a.contact->displayName() < b.contact->displayName();
  });
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

}  // namespace omacontacts
