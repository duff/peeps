#include "tests.h"

#include "cli.h"
#include "skill.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <QtTest>

namespace {

class MemorySecrets : public contacts::SecretStore {
 public:
  QString secret;
  int loads = 0;

  std::optional<QString> load(QString&) override {
    ++loads;
    if (secret.isEmpty()) return std::nullopt;
    return secret;
  }
  bool save(const QString& value, QString&) override {
    secret = value;
    return true;
  }
  bool clear(QString&) override {
    secret.clear();
    return true;
  }
};

class RecordingHttp : public contacts::HttpClient {
 public:
  int status = 200;
  QByteArray body;
  QString method;
  QByteArray sent;
  int calls = 0;

  contacts::HttpResponse request(const QString& method, const QString&, const QList<QPair<QString, QString>>&,
                                 const QByteArray& body) override {
    ++calls;
    this->method = method;
    sent = body;
    contacts::HttpResponse response;
    response.status = status;
    response.body = this->body;
    return response;
  }
};

contacts::Contact sample(QString id, QString given, QString family, QString email = {}, QString company = {}) {
  contacts::Contact contact;
  contact.resourceName = std::move(id);
  contact.etag = QStringLiteral("etag");
  contact.givenName = std::move(given);
  contact.familyName = std::move(family);
  contact.organization = std::move(company);
  if (!email.isEmpty()) contact.emails.append({std::move(email), QStringLiteral("Work"), QStringLiteral("work")});
  return contact;
}

contacts::CliResult run(const QStringList& args, contacts::CliSession& session, bool tty = false) {
  return contacts::runCli(args, session, tty);
}

QJsonObject objectOf(const contacts::CliResult& result) {
  return QJsonDocument::fromJson(result.out.toUtf8()).object();
}

}  // namespace

void CliTests::invocationLeavesTheWindowAlone() {
  QVERIFY(!contacts::isCliInvocation({}));
  QVERIFY(!contacts::isCliInvocation({QStringLiteral("--self-test")}));
  QVERIFY(!contacts::isCliInvocation({QStringLiteral("-platform"), QStringLiteral("offscreen")}));
  QVERIFY(contacts::isCliInvocation({QStringLiteral("list")}));
  QVERIFY(contacts::isCliInvocation({QStringLiteral("--help")}));
  QVERIFY(contacts::isCliInvocation({QStringLiteral("-h")}));
  QVERIFY(contacts::isCliInvocation({QStringLiteral("--json"), QStringLiteral("search"), QStringLiteral("ada")}));
  QVERIFY(contacts::isCliInvocation({QStringLiteral("no-such-command")}));
}

void CliTests::helpAndCommands() {
  contacts::CliSession session;
  const auto help = run({QStringLiteral("help")}, session, true);
  QCOMPARE(help.code, contacts::kCliOk);
  QVERIFY(help.out.contains(QStringLiteral("peeps search lovelace")));
  QVERIFY(help.out.contains(QStringLiteral("0  success")));
  QVERIFY(help.err.isEmpty());

  const auto update = run({QStringLiteral("update"), QStringLiteral("--help")}, session, true);
  QCOMPARE(update.code, contacts::kCliOk);
  QVERIFY(update.out.contains(QStringLiteral("--notes")));
  QVERIFY(update.out.contains(QStringLiteral("Fields you leave out")));

  const auto commands = run({QStringLiteral("commands"), QStringLiteral("--json")}, session, true);
  QCOMPARE(commands.code, contacts::kCliOk);
  const QJsonArray listed = objectOf(commands).value(QStringLiteral("commands")).toArray();
  QCOMPARE(listed.size(), 9);
  QCOMPARE(listed.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("list"));
  QCOMPARE(listed.at(3).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("export"));
  QCOMPARE(listed.at(7).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("sync"));
  QCOMPARE(listed.at(8).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("skill"));
}

void CliTests::usageAndVersion() {
  contacts::CliSession session;
  const auto version = run({QStringLiteral("--version")}, session, true);
  QCOMPARE(version.code, contacts::kCliOk);
  QCOMPARE(version.out, QStringLiteral("peeps 0.1.0\n"));

  const auto missing = run({QStringLiteral("--json")}, session, true);
  QCOMPARE(missing.code, contacts::kCliUsage);
  QVERIFY(missing.err.contains(QStringLiteral("Pass a command")));

  const auto unknown = run({QStringLiteral("explode")}, session, true);
  QCOMPARE(unknown.code, contacts::kCliUsage);
  QVERIFY(unknown.err.contains(QStringLiteral("Unknown command")));

  const auto both = run({QStringLiteral("list"), QStringLiteral("--json"), QStringLiteral("--text")}, session, true);
  QCOMPARE(both.code, contacts::kCliUsage);

  int writes = 0;
  session.remove = [&](const QString&) {
    ++writes;
    return QString();
  };
  const auto refuse = run({QStringLiteral("delete"), QStringLiteral("people/c1")}, session, true);
  QCOMPARE(refuse.code, contacts::kCliNotFound);
  QCOMPARE(writes, 0);
}

void CliTests::listAndSearchStayOnTheCache() {
  contacts::CliSession session;
  session.signedIn = false;
  session.contacts = {sample(QStringLiteral("people/c2"), QStringLiteral("Alan"), QStringLiteral("Turing"),
                             QStringLiteral("alan@example.com")),
                      sample(QStringLiteral("people/c1"), QStringLiteral("Ada"), QStringLiteral("Lovelace"),
                             QStringLiteral("ada@example.com"), QStringLiteral("Engines"))};
  int writes = 0;
  session.create = [&](const contacts::Contact&) {
    ++writes;
    return contacts::WriteResult{};
  };
  session.syncPeople = [&](bool) {
    ++writes;
    return contacts::PeopleResult{};
  };

  const auto text = run({QStringLiteral("list")}, session, true);
  QCOMPARE(text.code, contacts::kCliOk);
  QCOMPARE(writes, 0);
  QVERIFY(text.out.startsWith(QStringLiteral("2 contacts")));
  const int ada = text.out.indexOf(QStringLiteral("Ada Lovelace"));
  const int alan = text.out.indexOf(QStringLiteral("Alan Turing"));
  QVERIFY(ada >= 0 && alan > ada);
  QVERIFY(text.out.contains(QStringLiteral("ada@example.com  ·  Engines")));

  const auto json = run({QStringLiteral("list"), QStringLiteral("--limit"), QStringLiteral("1")}, session, false);
  QCOMPARE(json.code, contacts::kCliOk);
  QVERIFY(json.out.startsWith(QLatin1Char('{')));
  const QJsonObject listed = objectOf(json);
  QCOMPARE(listed.value(QStringLiteral("total")).toInt(), 2);
  QCOMPARE(listed.value(QStringLiteral("count")).toInt(), 1);
  QCOMPARE(listed.value(QStringLiteral("contacts")).toArray().at(0).toObject().value(QStringLiteral("id")).toString(),
           QStringLiteral("people/c1"));

  const auto tty = run({QStringLiteral("search"), QStringLiteral("lovelace")}, session, true);
  QVERIFY(!tty.out.startsWith(QLatin1Char('{')));
  QVERIFY(tty.out.contains(QStringLiteral("Ada Lovelace")));
  QVERIFY(!tty.out.contains(QStringLiteral("Alan Turing")));

  const auto found = run({QStringLiteral("search"), QStringLiteral("ada"), QStringLiteral("lovelace")}, session, false);
  QCOMPARE(found.code, contacts::kCliOk);
  QCOMPARE(objectOf(found).value(QStringLiteral("query")).toString(), QStringLiteral("ada lovelace"));
  QCOMPARE(objectOf(found).value(QStringLiteral("total")).toInt(), 1);
  QVERIFY(objectOf(found).value(QStringLiteral("contacts")).toArray().at(0).toObject().contains(QStringLiteral("score")));
  QCOMPARE(writes, 0);

  const auto none = run({QStringLiteral("search"), QStringLiteral("zzz")}, session, true);
  QCOMPARE(none.out, QStringLiteral("No matches.\n"));
}

void CliTests::showResolvesIdNameAndEmail() {
  contacts::CliSession session;
  session.contacts = {sample(QStringLiteral("people/c1"), QStringLiteral("Ada"), QStringLiteral("Lovelace"),
                             QStringLiteral("ada@example.com"), QStringLiteral("Engines")),
                      sample(QStringLiteral("people/c2"), QStringLiteral("Ada"), QStringLiteral("Lovelace"),
                             QStringLiteral("other@example.com"))};
  session.contacts[0].notes = QStringLiteral("First line\nSecond line");
  session.contacts[0].phones.append({QStringLiteral("+1 555"), QStringLiteral("Mobile"), QStringLiteral("mobile")});

  const auto byEmail = run({QStringLiteral("show"), QStringLiteral("ada@example.com"), QStringLiteral("--json")}, session, true);
  QCOMPARE(byEmail.code, contacts::kCliOk);
  QCOMPARE(objectOf(byEmail).value(QStringLiteral("id")).toString(), QStringLiteral("people/c1"));
  QCOMPARE(objectOf(byEmail).value(QStringLiteral("given_name")).toString(), QStringLiteral("Ada"));
  QCOMPARE(objectOf(byEmail).value(QStringLiteral("notes")).toString(), QStringLiteral("First line\nSecond line"));

  const auto bySuffix = run({QStringLiteral("show"), QStringLiteral("c1"), QStringLiteral("--text")}, session, false);
  QCOMPARE(bySuffix.code, contacts::kCliOk);
  QVERIFY(bySuffix.out.contains(QStringLiteral("people/c1")));
  QVERIFY(bySuffix.out.contains(QStringLiteral("Second line")));
  QVERIFY(bySuffix.out.contains(QStringLiteral("+1 555")));

  const auto ambiguous = run({QStringLiteral("show"), QStringLiteral("Ada Lovelace")}, session, true);
  QCOMPARE(ambiguous.code, contacts::kCliFailed);
  QVERIFY(ambiguous.err.contains(QStringLiteral("matches 2 contacts")));
  QVERIFY(ambiguous.err.contains(QStringLiteral("people/c1")));

  const auto missing = run({QStringLiteral("show"), QStringLiteral("nobody")}, session, true);
  QCOMPARE(missing.code, contacts::kCliNotFound);
}

void CliTests::addUpdateAndDelete() {
  contacts::CliSession session;
  session.signedIn = true;
  session.contacts = {sample(QStringLiteral("people/c1"), QStringLiteral("Ada"), QStringLiteral("Lovelace"),
                             QStringLiteral("ada@example.com"), QStringLiteral("Engines"))};
  session.contacts[0].notes = QStringLiteral("Keep me");
  session.contacts[0].phones.append({QStringLiteral("+1 555"), QStringLiteral("Mobile"), QStringLiteral("mobile")});
  QList<contacts::Contact> created;
  QList<contacts::Contact> updated;
  QStringList removed;
  session.create = [&](const contacts::Contact& contact) {
    created.append(contact);
    contacts::WriteResult result;
    result.contact = contact;
    result.contact.resourceName = QStringLiteral("people/new");
    result.contact.etag = QStringLiteral("e-new");
    return result;
  };
  session.update = [&](const contacts::Contact& contact) {
    updated.append(contact);
    contacts::WriteResult result;
    result.contact = contact;
    result.contact.etag = QStringLiteral("e-next");
    return result;
  };
  session.remove = [&](const QString& id) {
    removed.append(id);
    return QString();
  };

  const auto bad = run({QStringLiteral("add"), QStringLiteral("--email"), QStringLiteral("not-an-email")}, session, true);
  QCOMPARE(bad.code, contacts::kCliUsage);
  QVERIFY(created.isEmpty());

  const auto added = run({QStringLiteral("add"), QStringLiteral("--given"), QStringLiteral("Grace"), QStringLiteral("--family"),
                          QStringLiteral("Hopper"), QStringLiteral("--email"), QStringLiteral("home=grace@example.com"),
                          QStringLiteral("--json")},
                         session, true);
  QCOMPARE(added.code, contacts::kCliOk);
  QCOMPARE(created.size(), 1);
  QCOMPARE(created.first().givenName, QStringLiteral("Grace"));
  QCOMPARE(created.first().emails.first().type, QStringLiteral("home"));
  QCOMPARE(objectOf(added).value(QStringLiteral("id")).toString(), QStringLiteral("people/new"));
  QCOMPARE(session.contacts.size(), 2);

  const auto same = run({QStringLiteral("update"), QStringLiteral("people/c1"), QStringLiteral("--notes"), QStringLiteral("Keep me")},
                        session, true);
  QCOMPARE(same.code, contacts::kCliUsage);
  QVERIFY(same.err.contains(QStringLiteral("Nothing to change")));
  QVERIFY(updated.isEmpty());

  const auto changed = run({QStringLiteral("update"), QStringLiteral("ada@example.com"), QStringLiteral("--notes"),
                            QStringLiteral("Met in 1843"), QStringLiteral("--email=")},
                           session, true);
  QCOMPARE(changed.code, contacts::kCliOk);
  QCOMPARE(updated.size(), 1);
  QCOMPARE(updated.first().resourceName, QStringLiteral("people/c1"));
  QCOMPARE(updated.first().etag, QStringLiteral("etag"));
  QCOMPARE(updated.first().notes, QStringLiteral("Met in 1843"));
  QVERIFY(updated.first().emails.isEmpty());
  QCOMPARE(updated.first().phones.size(), 1);
  QCOMPARE(updated.first().organization, QStringLiteral("Engines"));
  QVERIFY(changed.out.contains(QStringLiteral("Updated Ada Lovelace.")));
  QCOMPARE(session.contacts.first().etag, QStringLiteral("e-next"));

  const auto refused = run({QStringLiteral("delete"), QStringLiteral("people/c1")}, session, true);
  QCOMPARE(refused.code, contacts::kCliUsage);
  QVERIFY(refused.err.contains(QStringLiteral("without --yes")));
  QVERIFY(removed.isEmpty());

  const auto deleted = run({QStringLiteral("delete"), QStringLiteral("c1"), QStringLiteral("--yes"), QStringLiteral("--json")}, session, true);
  QCOMPARE(deleted.code, contacts::kCliOk);
  QCOMPARE(removed, QStringList{QStringLiteral("people/c1")});
  QCOMPARE(objectOf(deleted).value(QStringLiteral("deleted")).toString(), QStringLiteral("people/c1"));
  QCOMPARE(session.contacts.size(), 1);

  session.signedIn = false;
  const auto signedOut = run({QStringLiteral("add"), QStringLiteral("--given"), QStringLiteral("Ada")}, session, true);
  QCOMPARE(signedOut.code, contacts::kCliSignedOut);
  QCOMPARE(created.size(), 1);
}

void CliTests::jsonDataRoundTrip() {
  contacts::CliSession session;
  session.signedIn = true;
  contacts::Contact contact = sample(QStringLiteral("people/c1"), QStringLiteral("Ada"), QStringLiteral("Lovelace"),
                                     QStringLiteral("ada@example.com"), QStringLiteral("Engines"));
  contacts::PostalAddress address;
  address.label = QStringLiteral("Home");
  address.type = QStringLiteral("home");
  address.street = QStringLiteral("1 Main");
  address.extended = QStringLiteral("Flat 2");
  address.poBox = QStringLiteral("12");
  address.city = QStringLiteral("London");
  contact.addresses.append(address);
  contact.customs.append({QStringLiteral("Ann"), QStringLiteral("Spouse"), {}});
  session.contacts = {contact};
  contacts::Contact sent;
  session.update = [&](const contacts::Contact& next) {
    sent = next;
    contacts::WriteResult result;
    result.contact = next;
    return result;
  };

  const auto shown = run({QStringLiteral("show"), QStringLiteral("people/c1"), QStringLiteral("--json")}, session, true);
  QCOMPARE(shown.code, contacts::kCliOk);
  QJsonObject patch = objectOf(shown);
  patch.insert(QStringLiteral("notes"), QStringLiteral("From JSON"));
  const QByteArray body = QJsonDocument(patch).toJson(QJsonDocument::Compact);
  session.stdinBytes = body;
  const auto updated = run({QStringLiteral("update"), QStringLiteral("people/c1"), QStringLiteral("--data"), QStringLiteral("-"),
                            QStringLiteral("--title"), QStringLiteral("Mathematician")},
                           session, false);
  QCOMPARE(updated.code, contacts::kCliOk);
  QCOMPARE(sent.notes, QStringLiteral("From JSON"));
  QCOMPARE(sent.title, QStringLiteral("Mathematician"));
  QCOMPARE(sent.addresses.size(), 1);
  QCOMPARE(sent.addresses.first().extended, QStringLiteral("Flat 2"));
  QCOMPARE(sent.addresses.first().poBox, QStringLiteral("12"));
  QCOMPARE(sent.emails.first().value, QStringLiteral("ada@example.com"));
  QCOMPARE(sent.customs.first().label, QStringLiteral("Spouse"));
  QCOMPARE(sent.customs.first().value, QStringLiteral("Ann"));

  session.stdinBytes = QByteArrayLiteral("{\"nope\":1}");
  const auto badField = run({QStringLiteral("update"), QStringLiteral("people/c1"), QStringLiteral("--data"), QStringLiteral("-")},
                            session, true);
  QCOMPARE(badField.code, contacts::kCliUsage);
  QVERIFY(badField.err.contains(QStringLiteral("Unknown field")));
}

void CliTests::addFlagsAppend() {
  contacts::CliSession session;
  session.signedIn = true;
  auto ada = sample(QStringLiteral("people/c1"), QStringLiteral("Ada"), QStringLiteral("Lovelace"), QStringLiteral("ada@example.com"));
  ada.phones.append({QStringLiteral("+1 312-555-0100"), QStringLiteral("Mobile"), QStringLiteral("mobile")});
  session.contacts = {ada};
  contacts::Contact sent;
  session.update = [&](const contacts::Contact& next) {
    sent = next;
    contacts::WriteResult result;
    result.contact = next;
    return result;
  };
  const auto added = run({QStringLiteral("update"), QStringLiteral("people/c1"), QStringLiteral("--add-email"),
                          QStringLiteral("home=ada@home.example"), QStringLiteral("--add-email"), QStringLiteral("ADA@example.com"),
                          QStringLiteral("--add-phone"), QStringLiteral("work=(312) 555-0100"), QStringLiteral("--add-address"),
                          QStringLiteral("home|12 St James's Square|London||SW1Y 4LB|United Kingdom")},
                         session);
  QCOMPARE(added.code, contacts::kCliOk);
  // The first email stays. One already there, in any case, is not added twice.
  QCOMPARE(sent.emails.size(), 2);
  QCOMPARE(sent.emails.at(0).value, QStringLiteral("ada@example.com"));
  QCOMPARE(sent.emails.at(1).value, QStringLiteral("ada@home.example"));
  // The same number written another way is the same phone.
  QCOMPARE(sent.phones.size(), 1);
  QCOMPARE(sent.addresses.size(), 1);
  QCOMPARE(sent.addresses.first().city, QStringLiteral("London"));

  // --email still replaces the whole list.
  session.contacts = {sent};
  const auto replaced = run({QStringLiteral("update"), QStringLiteral("people/c1"), QStringLiteral("--email"),
                             QStringLiteral("work=ada@engine.example")},
                            session);
  QCOMPARE(replaced.code, contacts::kCliOk);
  QCOMPARE(sent.emails.size(), 1);
  QCOMPARE(sent.emails.first().value, QStringLiteral("ada@engine.example"));
}

void CliTests::partialNamesListCandidates() {
  contacts::CliSession session;
  session.contacts = {sample(QStringLiteral("people/c1"), QStringLiteral("Maya"), QStringLiteral("Okafor")),
                      sample(QStringLiteral("people/c2"), QStringLiteral("Maya"), QStringLiteral("Lindqvist"))};
  // A first name is not an id. Peeps names who it could mean instead of guessing.
  const auto both = run({QStringLiteral("show"), QStringLiteral("Maya")}, session);
  QCOMPARE(both.code, contacts::kCliFailed);
  QVERIFY(both.err.contains(QStringLiteral("These contacts match it")));
  QVERIFY(both.err.contains(QStringLiteral("people/c1  Maya Okafor")));
  QVERIFY(both.err.contains(QStringLiteral("people/c2  Maya Lindqvist")));
  // Even one possible match is not acted on, since a write could hit the wrong person.
  const auto one = run({QStringLiteral("delete"), QStringLiteral("okafor"), QStringLiteral("--yes")}, session);
  QCOMPARE(one.code, contacts::kCliFailed);
  QVERIFY(one.err.contains(QStringLiteral("This contact matches it")));
  QCOMPARE(run({QStringLiteral("show"), QStringLiteral("Maya Okafor")}, session).code, contacts::kCliOk);
  QCOMPARE(run({QStringLiteral("show"), QStringLiteral("nobody")}, session).code, contacts::kCliNotFound);
}

void CliTests::skillLinks() {
  QTemporaryDir root;
  QVERIFY(root.isValid());
  const QString home = root.path() + QStringLiteral("/home");
  const QString prefix = root.path() + QStringLiteral("/usr");
  QVERIFY(QDir().mkpath(prefix + QStringLiteral("/bin")));
  QVERIFY(contacts::installedSkillDir(prefix + QStringLiteral("/bin")).isEmpty());
  const QString skill = prefix + QStringLiteral("/share/peeps/skills/peeps");
  QVERIFY(QDir().mkpath(skill));
  QFile file(skill + QStringLiteral("/SKILL.md"));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.close();
  QCOMPARE(contacts::installedSkillDir(prefix + QStringLiteral("/bin")), skill);

  // Only agents that are set up get a link.
  QVERIFY(QDir().mkpath(home + QStringLiteral("/.claude")));
  QVERIFY(QDir().mkpath(home + QStringLiteral("/.agents/skills/peeps")));
  QVERIFY(QDir().mkpath(home + QStringLiteral("/.pi/agent/skills")));
  QVERIFY(QFile::link(QStringLiteral("/old/share/peeps/skills/peeps"), home + QStringLiteral("/.pi/agent/skills/peeps")));
  const QStringList folders = contacts::agentSkillFolders(home);
  QCOMPARE(folders, QStringList({home + QStringLiteral("/.claude/skills"), home + QStringLiteral("/.pi/agent/skills"),
                                 home + QStringLiteral("/.agents/skills")}));
  QCOMPARE(contacts::agentSkillFolders(home, root.path(), {}).first(), root.path() + QStringLiteral("/skills"));

  // A stale link is replaced. A real folder of the same name is left alone.
  const auto linked = contacts::linkSkill(skill, folders);
  QVERIFY(linked.error.isEmpty());
  QCOMPARE(linked.changed.size(), 2);
  QCOMPARE(linked.skipped, QStringList({home + QStringLiteral("/.agents/skills/peeps")}));
  QCOMPARE(QFileInfo(home + QStringLiteral("/.claude/skills/peeps")).symLinkTarget(), skill);
  QCOMPARE(QFileInfo(home + QStringLiteral("/.pi/agent/skills/peeps")).symLinkTarget(), skill);
  QVERIFY(contacts::linkSkill(skill, folders).error.isEmpty());

  const auto removed = contacts::unlinkSkill(folders);
  QCOMPARE(removed.changed.size(), 2);
  QVERIFY(!QFileInfo(home + QStringLiteral("/.claude/skills/peeps")).isSymLink());
  QVERIFY(QFileInfo(home + QStringLiteral("/.agents/skills/peeps")).isDir());
}

void CliTests::skillCoversEveryCommand() {
  QFile file(QStringLiteral(PEEPS_SOURCE_DIR "/skills/peeps/SKILL.md"));
  QVERIFY(file.open(QIODevice::ReadOnly));
  const QString skill = QString::fromUtf8(file.readAll());
  QVERIFY(skill.startsWith(QStringLiteral("---\nname: peeps\n")));
  contacts::CliSession session;
  const auto catalog = run({QStringLiteral("commands"), QStringLiteral("--json")}, session);
  for (const QJsonValue& row : objectOf(catalog).value(QStringLiteral("commands")).toArray()) {
    const QString name = row.toObject().value(QStringLiteral("name")).toString();
    if (name == QLatin1String("skill")) continue;
    QVERIFY2(skill.contains(QStringLiteral("peeps ") + name) || skill.contains(QLatin1Char('`') + name + QLatin1Char('`')),
             qPrintable(name + QStringLiteral(" is missing from the skill")));
  }
  for (const char* flag : {"--add-email", "--add-phone", "--add-url", "--add-address", "--data", "--yes"}) {
    QVERIFY2(skill.contains(QLatin1String(flag)), flag);
  }
}

void CliTests::syncRefreshesTheCache() {
  contacts::CliSession session;
  session.signedIn = true;
  session.syncToken = QStringLiteral("old");
  session.contacts = {sample(QStringLiteral("people/c1"), QStringLiteral("Ada"), QStringLiteral("Lovelace"))};
  QList<bool> calls;
  session.syncPeople = [&](bool full) {
    calls.append(full);
    contacts::PeopleResult result;
    if (!full) {
      result.error = QStringLiteral("sync-token-expired");
      return result;
    }
    contacts::PersonChange change;
    change.contact = sample(QStringLiteral("people/c9"), QStringLiteral("Grace"), QStringLiteral("Hopper"));
    result.people = {change};
    result.syncToken = QStringLiteral("next");
    return result;
  };
  const auto synced = run({QStringLiteral("sync"), QStringLiteral("--json")}, session, true);
  QCOMPARE(synced.code, contacts::kCliOk);
  QCOMPARE(calls, QList<bool>({false, true}));
  QCOMPARE(session.syncToken, QStringLiteral("next"));
  QVERIFY(session.syncedAt > 0);
  QCOMPARE(session.contacts.size(), 1);
  QCOMPARE(session.contacts.first().givenName, QStringLiteral("Grace"));
  QVERIFY(objectOf(synced).value(QStringLiteral("full")).toBool());

  calls.clear();
  session.syncPeople = [&](bool full) {
    calls.append(full);
    contacts::PeopleResult result;
    contacts::PersonChange change;
    change.contact = sample(QStringLiteral("people/c2"), QStringLiteral("Alan"), QStringLiteral("Turing"));
    result.people = {change};
    result.syncToken = QStringLiteral("full-token");
    return result;
  };
  const auto full = run({QStringLiteral("sync"), QStringLiteral("--full")}, session, true);
  QCOMPARE(full.code, contacts::kCliOk);
  QCOMPARE(calls, QList<bool>({true}));
  QVERIFY(full.out.contains(QStringLiteral("Synced 1 contact.")));
}

void CliTests::liveCacheAndGoogle() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString root = dir.path();
  contacts::CliLaunch launch;
  launch.paths = contacts::AppPaths::forEnv(root, root + QStringLiteral("/config"), root + QStringLiteral("/data"),
                                            root + QStringLiteral("/cache"));
  launch.client = {};
  launch.stdoutIsTty = false;
  MemorySecrets secrets;
  launch.secrets = &secrets;
  RecordingHttp http;
  launch.http = &http;

  const auto listed = contacts::runContactsCommand({QStringLiteral("list"), QStringLiteral("--json")}, launch);
  QCOMPARE(listed.code, contacts::kCliOk);
  QCOMPARE(objectOf(listed).value(QStringLiteral("total")).toInt(), 0);
  QCOMPARE(secrets.loads, 0);
  QCOMPARE(http.calls, 0);

  launch.client = {QStringLiteral("id"), QStringLiteral("secret")};
  secrets.secret = QString::fromUtf8(
      contacts::writeTokensJson({QStringLiteral("access"), QStringLiteral("refresh"), QDateTime::currentSecsSinceEpoch() + 3600}));
  http.body = QByteArrayLiteral(
      "{\"resourceName\":\"people/c9\",\"etag\":\"e9\",\"names\":[{\"givenName\":\"Ada\",\"familyName\":\"Lovelace\"}],"
      "\"emailAddresses\":[{\"value\":\"ada@example.com\",\"type\":\"work\"}]}");
  const auto added = contacts::runContactsCommand({QStringLiteral("add"), QStringLiteral("--given"), QStringLiteral("Ada"),
                                                   QStringLiteral("--family"), QStringLiteral("Lovelace"), QStringLiteral("--email"),
                                                   QStringLiteral("ada@example.com"), QStringLiteral("--json")},
                                                  launch);
  QCOMPARE(added.code, contacts::kCliOk);
  QCOMPARE(http.method, QStringLiteral("POST"));
  QVERIFY(http.sent.contains("Ada"));
  QCOMPARE(objectOf(added).value(QStringLiteral("id")).toString(), QStringLiteral("people/c9"));
  const int loadsAfterAdd = secrets.loads;

  const auto again = contacts::runContactsCommand({QStringLiteral("search"), QStringLiteral("ada"), QStringLiteral("--json")}, launch);
  QCOMPARE(again.code, contacts::kCliOk);
  QCOMPARE(objectOf(again).value(QStringLiteral("total")).toInt(), 1);
  QCOMPARE(secrets.loads, loadsAfterAdd);
  QCOMPARE(http.calls, 1);

  http.body = QByteArrayLiteral(
      "{\"connections\":[{\"resourceName\":\"people/c3\",\"etag\":\"e3\",\"names\":[{\"givenName\":\"Grace\",\"familyName\":\"Hopper\"}]}],"
      "\"nextSyncToken\":\"sync-1\"}");
  const auto synced = contacts::runContactsCommand({QStringLiteral("sync"), QStringLiteral("--json")}, launch);
  QCOMPARE(synced.code, contacts::kCliOk);
  QCOMPARE(http.method, QStringLiteral("GET"));
  QCOMPARE(objectOf(synced).value(QStringLiteral("count")).toInt(), 1);
  const auto after = contacts::runContactsCommand({QStringLiteral("show"), QStringLiteral("people/c3"), QStringLiteral("--json")}, launch);
  QCOMPARE(after.code, contacts::kCliOk);
  QCOMPARE(objectOf(after).value(QStringLiteral("given_name")).toString(), QStringLiteral("Grace"));

  QImage image(2, 2, QImage::Format_RGB32);
  image.fill(Qt::red);
  const QString photo = dir.filePath(QStringLiteral("face.png"));
  QVERIFY(image.save(photo, "PNG"));
  http.body = QByteArrayLiteral(
      "{\"person\":{\"resourceName\":\"people/c3\",\"etag\":\"e4\",\"photos\":[{\"url\":\"https://example.test/g.jpg\",\"default\":false}]}}");
  int callsBeforePhoto = http.calls;
  const auto pictured = contacts::runContactsCommand(
      {QStringLiteral("update"), QStringLiteral("people/c3"), QStringLiteral("--photo"), photo, QStringLiteral("--json")}, launch);
  QCOMPARE(pictured.code, contacts::kCliOk);
  QVERIFY(http.calls > callsBeforePhoto);
  QVERIFY(http.sent.contains("photoBytes"));
  QCOMPARE(objectOf(pictured).value(QStringLiteral("photo_url")).toString(), QStringLiteral("https://example.test/g.jpg"));
}

void CliTests::exportPrintsVCards() {
  contacts::CliSession session;
  session.contacts = {sample(QStringLiteral("people/b"), QStringLiteral("Bea"), QStringLiteral("Ng"), QStringLiteral("bea@example.com")),
                      sample(QStringLiteral("people/a"), QStringLiteral("Ada"), QStringLiteral("Lovelace"), QStringLiteral("ada@example.com"))};
  const auto all = run({QStringLiteral("export")}, session);
  QCOMPARE(all.code, contacts::kCliOk);
  QCOMPARE(all.out.count(QStringLiteral("BEGIN:VCARD")), 2);
  QVERIFY(all.out.indexOf(QStringLiteral("FN:Ada Lovelace")) < all.out.indexOf(QStringLiteral("FN:Bea Ng")));

  const auto one = run({QStringLiteral("export"), QStringLiteral("bea@example.com")}, session, true);
  QCOMPARE(one.code, contacts::kCliOk);
  QCOMPARE(one.out.count(QStringLiteral("BEGIN:VCARD")), 1);
  QVERIFY(one.out.contains(QStringLiteral("EMAIL;TYPE=INTERNET,WORK:bea@example.com\r\n")));

  QCOMPARE(run({QStringLiteral("export"), QStringLiteral("nobody")}, session).code, contacts::kCliNotFound);
  QCOMPARE(run({QStringLiteral("export"), QStringLiteral("--json")}, session).code, contacts::kCliUsage);
  QVERIFY(run({QStringLiteral("help"), QStringLiteral("export")}, session).out.contains(QStringLiteral("vCard")));
}
