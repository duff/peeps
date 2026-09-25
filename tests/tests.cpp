#include "tests.h"

#include "contact.h"
#include "instance.h"
#include "mainwindow.h"
#include "net.h"
#include "google.h"
#include "mail.h"
#include "oauth.h"
#include "search.h"
#include "shortcuts.h"
#include "store.h"
#include "theme.h"

#include <QDir>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QNetworkRequest>
#include <QUrl>
#include <QSslConfiguration>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QTest>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <QtTest>

namespace {

class ScriptedHttp : public omacontacts::HttpClient {
 public:
  QList<omacontacts::HttpResponse> script;
  QStringList urls;
  QList<QByteArray> bodies;
  omacontacts::HttpResponse request(const QString&, const QString& url, const QList<QPair<QString, QString>>&,
                                    const QByteArray& body) override {
    urls.append(url);
    bodies.append(body);
    if (script.isEmpty()) return {500, {}, QStringLiteral("no script"), url};
    auto response = script.takeFirst();
    response.url = url;
    return response;
  }
};

omacontacts::Contact person(QString id, QString given, QString family, QString email = {}, QString phone = {},
                            QString org = {}) {
  omacontacts::Contact contact;
  contact.resourceName = std::move(id);
  contact.givenName = std::move(given);
  contact.familyName = std::move(family);
  contact.organization = std::move(org);
  if (!email.isEmpty()) contact.emails.append({std::move(email), QStringLiteral("Work"), QStringLiteral("work")});
  if (!phone.isEmpty()) contact.phones.append({std::move(phone), QStringLiteral("Mobile"), QStringLiteral("mobile")});
  return contact;
}

void writeFile(const QString& path, const QString& text) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return;
  file.write(text.toUtf8());
}

const char* kSemantic = R"(
mode = "dark"
accent = "#e68e0d"
selection = "#2a2a2a"
muted = "#333333"
background = "#121212"
dark_background = "#0d0d0d"
lighter_background = "#1e1e1e"
foreground = "#bebebe"
bright_foreground = "#ffffff"
red = "#D35F5F"
yellow = "#FFC107"
green = "#9ece6a"
)";

omacontacts::KeyEvent browser(omacontacts::Key key, bool text = false) {
  omacontacts::KeyEvent event;
  event.key = key;
  event.textInput = text;
  return event;
}

}  // namespace

void ThemeTests::parsesOnlyFullHex() {
  const auto color = omacontacts::parseColorsToml(QStringLiteral("background = \"#1a1B26\"\nforeground = \"#ffffff\"\n"
                                                                 "accent = \"#7aa2f7\"\nred = \"#ff0000\"\n"
                                                                 "green = \"#00ff00\"\nyellow = \"#ffff00\"\n"),
                                                  QStringLiteral("X"));
  QVERIFY(color.has_value());
  QCOMPARE(color->background.name(QColor::HexRgb), QStringLiteral("#1a1b26"));
  QVERIFY(!omacontacts::parseColorsToml(QStringLiteral("accent = \"#fff\"\n"), QStringLiteral("x")));
  QVERIFY(!omacontacts::parseColorsToml(QStringLiteral("accent = \"#1a1b26ff\"\n"), QStringLiteral("x")));
}

void ThemeTests::mixesToward() {
  const auto palette = omacontacts::parseColorsToml(QStringLiteral("background = \"#000000\"\nforeground = \"#ffffff\"\n"
                                                                   "accent = \"#7aa2f7\"\nred = \"#ff0000\"\n"
                                                                   "green = \"#00ff00\"\nyellow = \"#ffff00\"\n"),
                                                    QString());
  QVERIFY(palette.has_value());
  QCOMPARE(palette->surface.name(QColor::HexRgb), QStringLiteral("#0d0d0d"));
}

void ThemeTests::onAccentFollowsContrast() {
  const auto pale = omacontacts::parseColorsToml(QStringLiteral("mode = \"dark\"\nbackground = \"#000000\"\n"
                                                                "foreground = \"#ffffff\"\naccent = \"#F0E68C\"\n"
                                                                "red = \"#ff0000\"\ngreen = \"#00ff00\"\n"
                                                                "yellow = \"#ffff00\"\n"),
                                                 QStringLiteral("pale"));
  QVERIFY(pale.has_value());
  QCOMPARE(pale->onAccent.name(QColor::HexRgb), QStringLiteral("#000000"));
  const auto dark = omacontacts::parseColorsToml(QStringLiteral("mode = \"dark\"\nbackground = \"#000000\"\n"
                                                                "foreground = \"#ffffff\"\naccent = \"#102040\"\n"
                                                                "red = \"#ff0000\"\ngreen = \"#00ff00\"\n"
                                                                "yellow = \"#ffff00\"\n"),
                                                 QStringLiteral("dark"));
  QVERIFY(dark.has_value());
  QCOMPARE(dark->onAccent.name(QColor::HexRgb), QStringLiteral("#ffffff"));
}

void ThemeTests::semanticPaletteUsesDeclaredRoles() {
  auto palette = omacontacts::parseColorsToml(QLatin1String(kSemantic), QStringLiteral("Matte Black"));
  QVERIFY(palette.has_value());
  QCOMPARE(palette->name, QStringLiteral("Matte Black"));
  QCOMPARE(palette->appearance, omacontacts::Appearance::Dark);
  QCOMPARE(palette->accent.name(QColor::HexRgb), QStringLiteral("#e68e0d"));
  QCOMPARE(palette->surface.name(QColor::HexRgb), QStringLiteral("#1e1e1e"));
  QCOMPARE(palette->inset.name(QColor::HexRgb), QStringLiteral("#0d0d0d"));
  QCOMPARE(palette->bright.name(QColor::HexRgb), QStringLiteral("#ffffff"));
  QCOMPARE(palette->selection.name(QColor::HexRgb), QStringLiteral("#2a2a2a"));
  QCOMPARE(palette->border.name(QColor::HexRgb), QStringLiteral("#333333"));
  QCOMPARE(palette->danger.name(QColor::HexRgb), QStringLiteral("#d35f5f"));
}

void ThemeTests::derivesMissingRoles() {
  auto palette = omacontacts::parseColorsToml(QStringLiteral("background = \"#000000\"\nforeground = \"#ffffff\"\n"
                                                             "accent = \"#7aa2f7\"\nred = \"#ff0000\"\n"
                                                             "green = \"#00ff00\"\nyellow = \"#ffff00\"\n"),
                                              QString());
  QVERIFY(palette.has_value());
  QCOMPARE(palette->name, QStringLiteral("Omarchy"));
  QCOMPARE(palette->surface.name(QColor::HexRgb), QStringLiteral("#0d0d0d"));
  QVERIFY(palette->secondary.name(QColor::HexRgb) != palette->background.name(QColor::HexRgb));
  QCOMPARE(palette->bright.name(QColor::HexRgb), QStringLiteral("#ffffff"));
}

void ThemeTests::ansiAliases() {
  auto palette = omacontacts::parseColorsToml(QStringLiteral("background = \"#111111\"\nforeground = \"#eeeeee\"\n"
                                                             "accent = \"#88c0d0\"\ncolor1 = \"#bf616a\"\n"
                                                             "color2 = \"#a3be8c\"\ncolor3 = \"#ebcb8b\"\n"
                                                             "color8 = \"#4c566a\"\n"),
                                              QStringLiteral("Ansi"));
  QVERIFY(palette.has_value());
  QCOMPARE(palette->danger.name(QColor::HexRgb), QStringLiteral("#bf616a"));
  QCOMPARE(palette->success.name(QColor::HexRgb), QStringLiteral("#a3be8c"));
  QCOMPARE(palette->warning.name(QColor::HexRgb), QStringLiteral("#ebcb8b"));
  QCOMPARE(palette->border.name(QColor::HexRgb), QStringLiteral("#4c566a"));
}

void ThemeTests::rejectsIncompleteDocuments() {
  QVERIFY(!omacontacts::parseColorsToml(QStringLiteral("background = \"#000000\"\n"), QStringLiteral("x")));
  QVERIFY(!omacontacts::parseColorsToml(QString(QLatin1String(kSemantic)) + QStringLiteral("accent = \"nope\"\n"),
                                        QStringLiteral("x")));
  QVERIFY(!omacontacts::parseColorsToml(QStringLiteral("background = \"#000000\"\nforeground = \"#ffffff\"\n"
                                                       "color1 = \"#ff0000\"\ncolor2 = \"#00ff00\"\n"
                                                       "color3 = \"#ffff00\"\n"),
                                        QStringLiteral("x")));
  QVERIFY(!omacontacts::parseColorsToml(QStringLiteral("this is not toml"), QStringLiteral("x")));
}

void ThemeTests::lightModeDarkensStatusInk() {
  auto palette = omacontacts::parseColorsToml(QStringLiteral("mode = \"light\"\nbackground = \"#FFFCF0\"\n"
                                                             "foreground = \"#100F0F\"\naccent = \"#205EA6\"\n"
                                                             "red = \"#ffb4a8\"\nyellow = \"#ffe08a\"\n"
                                                             "green = \"#d5e8a0\"\n"),
                                              QStringLiteral("Light"));
  QVERIFY(palette.has_value());
  QCOMPARE(palette->appearance, omacontacts::Appearance::Light);
  QCOMPARE_NE(palette->danger.name(QColor::HexRgb), QStringLiteral("#ffb4a8"));
  QVERIFY(palette->danger.lightness() < QColor(QStringLiteral("#ffb4a8")).lightness());
}

void ThemeTests::infersLightAppearance() {
  auto palette = omacontacts::parseColorsToml(QStringLiteral("background = \"#ffffff\"\nforeground = \"#111111\"\n"
                                                             "accent = \"#205EA6\"\nred = \"#800000\"\n"
                                                             "green = \"#006400\"\nyellow = \"#806000\"\n"),
                                              QStringLiteral("Inferred"));
  QVERIFY(palette.has_value());
  QCOMPARE(palette->appearance, omacontacts::Appearance::Light);
}

void ThemeTests::brokenStateDoesNotReviveLegacy() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto paths = omacontacts::ThemePaths::forHome(dir.path());
  writeFile(paths.legacyColors, QLatin1String(kSemantic));
  writeFile(paths.legacyName, QStringLiteral("Legacy\n"));
  QDir().mkpath(paths.stateDir);
  writeFile(paths.stateColors, QStringLiteral("accent = \"nope\"\n"));
  const auto load = omacontacts::loadTheme(paths);
  QCOMPARE(load.origin, omacontacts::ThemeOrigin::Builtin);
  QCOMPARE(load.palette.name, QStringLiteral("Tokyo Night"));
  QCOMPARE(load.palette.accent.name(QColor::HexRgb), QStringLiteral("#7aa2f7"));
}

void ThemeTests::legacyOnlyWhenStateIsAbsent() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto paths = omacontacts::ThemePaths::forHome(dir.path());
  writeFile(paths.legacyColors, QLatin1String(kSemantic));
  writeFile(paths.legacyName, QStringLiteral("\n"));
  const auto load = omacontacts::loadTheme(paths);
  QCOMPARE(load.origin, omacontacts::ThemeOrigin::Legacy);
  QCOMPARE(load.palette.name, QStringLiteral("Omarchy"));
  QCOMPARE(load.palette.accent.name(QColor::HexRgb), QStringLiteral("#e68e0d"));
}

void ThemeTests::missingHomeFallsBackWhole() {
  const auto load = omacontacts::loadTheme(omacontacts::ThemePaths::forHome(QStringLiteral("/no/such/home")));
  QCOMPARE(load.origin, omacontacts::ThemeOrigin::Builtin);
  QCOMPARE(load.palette.background.name(QColor::HexRgb), QStringLiteral("#1a1b26"));
  QCOMPARE(load.palette.foreground.name(QColor::HexRgb), QStringLiteral("#a9b1d6"));
}

void ThemeTests::stylesheetUsesTheGivenPalette() {
  const auto palette = omacontacts::parseColorsToml(QLatin1String(kSemantic), QStringLiteral("Matte Black"));
  QVERIFY(palette.has_value());
  const QString css = omacontacts::stylesheetFor(*palette);
  QVERIFY(css.contains(QStringLiteral("#e68e0d")));
  QVERIFY(css.contains(QStringLiteral("border-radius: 0")));
  QVERIFY(css.contains(QStringLiteral("font-size: 12px")));
  QVERIFY(!css.contains(QStringLiteral("#7aa2f7")));
}

void SearchTests::plainTextSkipsThePhoto() {
  omacontacts::Contact contact = person(QStringLiteral("people/c1"), QStringLiteral("Ada"), QStringLiteral("Lovelace"),
                                        QStringLiteral("ada@example.com"), QStringLiteral("+1 555"), QStringLiteral("Analytical"));
  contact.title = QStringLiteral("Engineer");
  contact.notes = QStringLiteral("First programmer");
  contact.photoUrl = QStringLiteral("https://example.test/ada.jpg");
  omacontacts::PostalAddress home;
  home.street = QStringLiteral("12 St James's Square");
  home.city = QStringLiteral("London");
  home.postalCode = QStringLiteral("SW1Y 4LB");
  home.country = QStringLiteral("UK");
  contact.addresses.append(home);
  const QString text = omacontacts::contactPlainText(contact);
  QCOMPARE(text, QStringLiteral("Ada Lovelace\nada@example.com\n+1 555\n12 St James's Square\nLondon SW1Y 4LB\nUK"));
  QVERIFY(!text.contains(QStringLiteral("example.test")));
  QVERIFY(!text.contains(QStringLiteral("Photo")));
  QVERIFY(!text.contains(QStringLiteral("Engineer")));
  QVERIFY(!text.contains(QStringLiteral("programmer")));
}

void SearchTests::postalAddressBreaksAfterTheStreet() {
  omacontacts::PostalAddress home;
  home.street = QStringLiteral("1 Main St");
  home.city = QStringLiteral("Austin");
  home.region = QStringLiteral("TX");
  home.postalCode = QStringLiteral("78701");
  QCOMPARE(home.display(), QStringLiteral("1 Main St\nAustin, TX 78701"));

  omacontacts::PostalAddress office;
  office.poBox = QStringLiteral("12");
  office.street = QStringLiteral("12 St James's Square");
  office.extended = QStringLiteral("Floor 2");
  office.city = QStringLiteral("London");
  office.region = QStringLiteral("Greater London");
  office.postalCode = QStringLiteral("SW1Y 4LB");
  office.country = QStringLiteral("UK");
  QCOMPARE(office.display(),
           QStringLiteral("PO Box 12\n12 St James's Square\nFloor 2\nLondon, Greater London SW1Y 4LB\nUK"));

  omacontacts::PostalAddress formatted;
  formatted.formatted = QStringLiteral("Somewhere\nElse");
  QCOMPARE(formatted.display(), QStringLiteral("Somewhere\nElse"));
}

void SearchTests::displayNameAndInitials() {
  const auto ada = person(QStringLiteral("people/ada"), QStringLiteral("Ada"), QStringLiteral("Lovelace"),
                          QStringLiteral("ada@example.com"));
  QCOMPARE(ada.displayName(), QStringLiteral("Ada Lovelace"));
  QCOMPARE(ada.initials(), QStringLiteral("AL"));
  omacontacts::Contact org;
  org.organization = QStringLiteral("Analytical Engines");
  QCOMPARE(org.displayName(), QStringLiteral("Analytical Engines"));
  QCOMPARE(org.initials(), QStringLiteral("AE"));
}

void SearchTests::draftValidation() {
  omacontacts::Draft draft;
  QVERIFY(!draft.validate().isEmpty());
  draft.organization = QStringLiteral("Acme");
  draft.emails.append({QStringLiteral("not-an-email"), QStringLiteral("Work"), QStringLiteral("work")});
  QVERIFY(!draft.validate().isEmpty());
  draft.emails[0].value = QStringLiteral("a@acme.test");
  QVERIFY(draft.validate().isEmpty());
  omacontacts::Contact base;
  base.resourceName = QStringLiteral("people/c1");
  base.etag = QStringLiteral("etag");
  const auto saved = draft.toContact(base);
  QCOMPARE(saved.resourceName, QStringLiteral("people/c1"));
  QCOMPARE(saved.etag, QStringLiteral("etag"));
  QCOMPARE(saved.emails.size(), 1);
}

void SearchTests::emptyQueryIsAlphabetical() {
  omacontacts::ContactIndex index;
  index.replaceAll({person(QStringLiteral("people/b"), QStringLiteral("Bea"), QStringLiteral("Ng")),
                    person(QStringLiteral("people/a"), QStringLiteral("Ada"), QStringLiteral("Lovelace"))});
  const auto hits = index.query(QString());
  QCOMPARE(hits.size(), 2);
  QCOMPARE(hits.at(0).contact->displayName(), QStringLiteral("Ada Lovelace"));
  QCOMPARE(hits.at(1).contact->displayName(), QStringLiteral("Bea Ng"));
}

void SearchTests::namePrefixOutranksEmail() {
  omacontacts::ContactIndex index;
  index.replaceAll({person(QStringLiteral("people/mail"), QStringLiteral("Sam"), QStringLiteral("River"),
                           QStringLiteral("ada@example.com")),
                    person(QStringLiteral("people/name"), QStringLiteral("Ada"), QStringLiteral("Lovelace"),
                           QStringLiteral("hello@example.com"))});
  const auto hits = index.query(QStringLiteral("ada"));
  QCOMPARE(hits.size(), 2);
  QCOMPARE(hits.at(0).contact->resourceName, QStringLiteral("people/name"));
  QVERIFY(hits.at(0).score > hits.at(1).score);
}

void SearchTests::tokensAreAnded() {
  omacontacts::ContactIndex index;
  auto ada = person(QStringLiteral("people/ada"), QStringLiteral("Ada"), QStringLiteral("Lovelace"),
                    QStringLiteral("ada@analytical.example"), QStringLiteral("+1 (555) 010-1000"),
                    QStringLiteral("Engines"));
  ada.notes = QStringLiteral("Difference engine");
  index.replaceAll({ada, person(QStringLiteral("people/alan"), QStringLiteral("Alan"), QStringLiteral("Turing"),
                                QStringLiteral("alan@example.com"))});
  QCOMPARE(index.query(QStringLiteral("ADA lovelace")).size(), 1);
  QVERIFY(index.query(QStringLiteral("ada turing")).isEmpty());
  const auto engines = index.query(QStringLiteral("engines"));
  QVERIFY(!engines.isEmpty());
  QCOMPARE(engines.first().contact->resourceName, QStringLiteral("people/ada"));
  const auto phone = index.query(QStringLiteral("555010"));
  QVERIFY(!phone.isEmpty());
  QCOMPARE(phone.first().contact->resourceName, QStringLiteral("people/ada"));
  const auto punctuated = index.query(QStringLiteral("555-010"));
  QVERIFY(!punctuated.isEmpty());
  QCOMPARE(punctuated.first().contact->resourceName, QStringLiteral("people/ada"));
}

void SearchTests::upsertAndRemove() {
  omacontacts::ContactIndex index;
  index.replaceAll({person(QStringLiteral("people/a"), QStringLiteral("Ada"), QStringLiteral("Lovelace"))});
  index.upsert(person(QStringLiteral("people/a"), QStringLiteral("Augusta"), QStringLiteral("King"),
                      QStringLiteral("ada@example.com")));
  QCOMPARE(index.size(), 1);
  QVERIFY(index.find(QStringLiteral("people/a")) != nullptr);
  QCOMPARE(index.find(QStringLiteral("people/a"))->givenName, QStringLiteral("Augusta"));
  QVERIFY(index.remove(QStringLiteral("people/a")));
  QCOMPARE(index.size(), 0);
  QVERIFY(!index.remove(QStringLiteral("people/missing")));
}

void SearchTests::tenThousandContactsStayFast() {
  QList<omacontacts::Contact> contacts;
  contacts.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    contacts.append(person(QStringLiteral("people/c%1").arg(i), QStringLiteral("Person"), QString::number(i),
                           QStringLiteral("p%1@example.com").arg(i)));
  }
  omacontacts::ContactIndex index;
  index.replaceAll(std::move(contacts));
  QElapsedTimer timer;
  timer.start();
  for (int i = 0; i < 40; ++i) {
    const auto hits = index.query(QStringLiteral("person 42"));
    QVERIFY(!hits.isEmpty());
  }
  QVERIFY2(timer.elapsed() < 1500, qPrintable(QString::number(timer.elapsed())));
}

void SearchTests::moveWrapsAndClamps() {
  QCOMPARE(omacontacts::moveIndex(0, 3, -1, true), 2);
  QCOMPARE(omacontacts::moveIndex(2, 3, 1, true), 0);
  QCOMPARE(omacontacts::moveIndex(0, 3, -1, false), 0);
  QCOMPARE(omacontacts::moveIndex(2, 3, 5, false), 2);
  QCOMPARE(omacontacts::moveIndex(0, 0, 1, true), 0);
}

void GoogleTests::pkceMatchesRfc7636() {
  const QString verifier = QStringLiteral("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
  QCOMPARE(omacontacts::pkceChallengeS256(verifier), QStringLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
}

void GoogleTests::authUrl() {
  const auto url = omacontacts::buildAuthUrl({QStringLiteral("client"), QStringLiteral("secret")},
                                             QStringLiteral("http://127.0.0.1:9000"), QStringLiteral("state-1"),
                                             QStringLiteral("challenge"));
  QVERIFY(url.contains(QStringLiteral("code_challenge_method=S256")));
  QVERIFY(url.contains(QStringLiteral("redirect_uri=http%3A%2F%2F127.0.0.1%3A9000")));
  QVERIFY(url.contains(QStringLiteral("scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcontacts")));
  QVERIFY(url.contains(QStringLiteral("state=state-1")));
  QVERIFY(url.contains(QStringLiteral("access_type=offline")));
}

void GoogleTests::parsesBrowserRedirect() {
  const auto ok = omacontacts::parseOauthRedirect("GET /?code=abc&state=state-1 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
  QVERIFY(ok.ok());
  QCOMPARE(ok.code, QStringLiteral("abc"));
  QCOMPARE(ok.state, QStringLiteral("state-1"));
  const auto denied = omacontacts::parseOauthRedirect("GET /?error=access_denied HTTP/1.1\r\n\r\n");
  QVERIFY(!denied.ok());
  QCOMPARE(denied.error, QStringLiteral("access_denied"));
}

void GoogleTests::keepsRefreshToken() {
  const auto parsed = omacontacts::parseTokenResponse(
      R"({"access_token":"new","expires_in":100,"token_type":"Bearer"})", 1000, QStringLiteral("refresh-1"));
  QVERIFY(parsed.ok());
  QCOMPARE(parsed.tokens.accessToken, QStringLiteral("new"));
  QCOMPARE(parsed.tokens.refreshToken, QStringLiteral("refresh-1"));
  QCOMPARE(parsed.tokens.expiryUnix, 1100);
  QVERIFY(!omacontacts::accessExpiring(parsed.tokens, 1000));
  QVERIFY(omacontacts::accessExpiring(parsed.tokens, 1040));
}

void GoogleTests::refreshPostsToken() {
  ScriptedHttp http;
  http.script.append({200, R"({"access_token":"fresh","expires_in":50})", {}, {}});
  omacontacts::OAuthTokens current;
  current.refreshToken = QStringLiteral("refresh-1");
  current.expiryUnix = 10;
  const auto result = omacontacts::refreshAccess(http, {QStringLiteral("id"), QStringLiteral("secret")}, current, 100);
  QVERIFY(result.ok());
  QCOMPARE(result.tokens.accessToken, QStringLiteral("fresh"));
  QCOMPARE(result.tokens.refreshToken, QStringLiteral("refresh-1"));
  QVERIFY(http.bodies.first().contains("refresh_token=refresh-1"));
  QVERIFY(http.bodies.first().contains("client_secret=secret"));
}

void GoogleTests::parsesPrimaryFields() {
  const char* json = R"({
    "connections": [
      {
        "resourceName": "people/c1",
        "etag": "e1",
        "names": [
          {"givenName": "Old", "familyName": "Name"},
          {"givenName": "Ada", "familyName": "Lovelace", "metadata": {"primary": true}}
        ],
        "emailAddresses": [
          {"value": "other@example.com"},
          {"value": "ada@example.com", "type": "work", "metadata": {"primary": true}}
        ],
        "phoneNumbers": [{"value": "+1 555", "formattedType": "Mobile"}],
        "organizations": [{"name": "Engines", "title": "Mathematician", "metadata": {"primary": true}}],
        "biographies": [{"value": "Notes", "metadata": {"primary": true}}],
        "photos": [
          {"url": "https://example.test/default", "default": true, "metadata": {"primary": true}},
          {"url": "https://example.test/ada.jpg", "default": false}
        ]
      },
      {"resourceName": "people/c2", "metadata": {"deleted": true}}
    ],
    "nextSyncToken": "sync-9"
  })";
  const auto page = omacontacts::parseConnectionsPage(json);
  QVERIFY(page.ok());
  QCOMPARE(page.people.size(), 2);
  QCOMPARE(page.people.at(0).contact.givenName, QStringLiteral("Ada"));
  QCOMPARE(page.people.at(0).contact.primaryEmail(), QStringLiteral("ada@example.com"));
  QCOMPARE(page.people.at(0).contact.emails.at(0).label, QStringLiteral("Work"));
  QCOMPARE(page.people.at(0).contact.emails.at(0).type, QStringLiteral("work"));
  QCOMPARE(page.people.at(0).contact.phones.at(0).label, QStringLiteral("Mobile"));
  QCOMPARE(page.people.at(0).contact.phones.at(0).type, QStringLiteral("mobile"));
  QCOMPARE(page.people.at(0).contact.organization, QStringLiteral("Engines"));
  QCOMPARE(page.people.at(0).contact.notes, QStringLiteral("Notes"));
  QCOMPARE(page.people.at(0).contact.photoUrl, QStringLiteral("https://example.test/ada.jpg"));
  QVERIFY(page.people.at(1).deleted);
  QCOMPARE(page.nextSyncToken, QStringLiteral("sync-9"));
}

void GoogleTests::prefersRealPhotoOverDefault() {
  const char* onlyDefault = R"({"resourceName":"people/c1","names":[{"givenName":"Ada"}],
    "photos":[{"url":"https://example.test/silhouette","default":true,"metadata":{"primary":true}}]})";
  const auto plain = omacontacts::parsePersonJson(onlyDefault);
  QVERIFY(plain.has_value());
  QVERIFY(plain->photoUrl.isEmpty());
  const char* primaryReal = R"({"resourceName":"people/c1","names":[{"givenName":"Ada"}],
    "photos":[
      {"url":"https://example.test/other.jpg","default":false},
      {"url":"https://example.test/primary.jpg","default":false,"metadata":{"primary":true}}
    ]})";
  const auto chosen = omacontacts::parsePersonJson(primaryReal);
  QVERIFY(chosen.has_value());
  QCOMPARE(chosen->photoUrl, QStringLiteral("https://example.test/primary.jpg"));
}

void GoogleTests::sizesPhotoUrl() {
  QCOMPARE(omacontacts::photoFetchUrl(QStringLiteral("https://example.test/a.jpg")),
           QStringLiteral("https://example.test/a.jpg?sz=2048"));
  QCOMPARE(omacontacts::photoFetchUrl(QStringLiteral("https://example.test/a.jpg?x=1&sz=50")),
           QStringLiteral("https://example.test/a.jpg?x=1&sz=2048"));
  QCOMPARE(omacontacts::photoFetchUrl(QStringLiteral("https://lh3.googleusercontent.com/a=s64")),
           QStringLiteral("https://lh3.googleusercontent.com/a=s0"));
  QCOMPARE(omacontacts::photoFetchUrl(QStringLiteral("https://lh3.googleusercontent.com/a=s100-c")),
           QStringLiteral("https://lh3.googleusercontent.com/a=s0"));
}

void GoogleTests::oldCacheRequestsFullSync() {
  const auto fresh = omacontacts::readCacheJson(R"({"version":1,"sync_token":"old","contacts":[]})");
  QVERIFY(fresh.ok());
  QVERIFY(fresh.syncToken.isEmpty());
}

void GoogleTests::roundTripsPerson() {
  omacontacts::Contact contact;
  contact.resourceName = QStringLiteral("people/c1");
  contact.etag = QStringLiteral("etag");
  contact.givenName = QStringLiteral("Ada");
  contact.familyName = QStringLiteral("Lovelace");
  contact.organization = QStringLiteral("Engines");
  contact.title = QStringLiteral("Mathematician");
  contact.department = QStringLiteral("Calculus");
  contact.nickname = QStringLiteral("Ada");
  contact.birthday = {1815, 12, 10, {}};
  omacontacts::PostalAddress address;
  address.type = QStringLiteral("home");
  address.street = QStringLiteral("1 Engine Lane");
  address.city = QStringLiteral("London");
  contact.addresses.append(address);
  contact.customs.append({QStringLiteral("blue"), QStringLiteral("Favorite color"), {}});
  contact.emails.append({QStringLiteral("ada@example.com"), QStringLiteral("School"), QStringLiteral("School")});
  contact.phones.append({QStringLiteral("+1555"), QStringLiteral("Mobile"), QStringLiteral("mobile")});
  contact.notes = QStringLiteral("Line \"one\"");
  const QByteArray json = omacontacts::personJson(contact);
  QVERIFY(json.contains("\"type\":\"School\""));
  QVERIFY(json.contains("\"type\":\"mobile\""));
  QVERIFY(!json.contains("formattedType"));
  const auto parsed = omacontacts::parsePersonJson(json);
  QVERIFY(parsed.has_value());
  QCOMPARE(parsed->givenName, QStringLiteral("Ada"));
  QCOMPARE(parsed->emails.at(0).label, QStringLiteral("School"));
  QCOMPARE(parsed->emails.at(0).type, QStringLiteral("School"));
  QCOMPARE(parsed->phones.at(0).type, QStringLiteral("mobile"));
  QCOMPARE(parsed->phones.at(0).label, QStringLiteral("Mobile"));
  QCOMPARE(parsed->notes, QStringLiteral("Line \"one\""));
  QCOMPARE(parsed->title, QStringLiteral("Mathematician"));
  QCOMPARE(parsed->department, QStringLiteral("Calculus"));
  QCOMPARE(parsed->nickname, QStringLiteral("Ada"));
  QCOMPARE(parsed->birthday.year, 1815);
  QCOMPARE(parsed->birthday.month, 12);
  QCOMPARE(parsed->birthday.day, 10);
  QCOMPARE(parsed->addresses.at(0).city, QStringLiteral("London"));
  QCOMPARE(parsed->customs.at(0).label, QStringLiteral("Favorite color"));
  QCOMPARE(parsed->customs.at(0).value, QStringLiteral("blue"));
}

void GoogleTests::updateRetriesWithFreshEtag() {
  ScriptedHttp http;
  http.script.append({400, QByteArray(R"({"error":{"message":"Request person.etag is different than the current person.etag."}})"),
                      {}, {}});
  http.script.append({200, QByteArray(R"({"resourceName":"people/c1","etag":"fresh"})"), {}, {}});
  http.script.append(
      {200, QByteArray(R"({"resourceName":"people/c1","etag":"saved","names":[{"givenName":"Ada"}]})"), {}, {}});
  omacontacts::Contact contact;
  contact.resourceName = QStringLiteral("people/c1");
  contact.etag = QStringLiteral("stale");
  contact.givenName = QStringLiteral("Ada");
  QString token = QStringLiteral("token");
  const auto result = omacontacts::updateContact(http, token, contact, [] { return QString(); });
  QVERIFY(result.ok());
  QCOMPARE(result.contact.etag, QStringLiteral("saved"));
  QCOMPARE(http.bodies.size(), 3);
  QVERIFY(http.bodies.at(0).contains("\"etag\":\"stale\""));
  QVERIFY(http.bodies.at(2).contains("\"etag\":\"fresh\""));
  QVERIFY(http.urls.at(1).contains(QStringLiteral("personFields=metadata")));
}

void GoogleTests::updatesContactPhoto() {
  ScriptedHttp http;
  http.script.append({200, QByteArray(R"({"person":{"resourceName":"people/c1","etag":"e2","photos":[{"url":"https://lh3.googleusercontent.com/a=s100","metadata":{"primary":true}}]}})"),
                      {}, {}});
  QString token = QStringLiteral("access");
  const auto result = omacontacts::updateContactPhoto(http, token, QStringLiteral("people/c1"), QByteArray("jpeg-bytes"), {});
  QVERIFY(result.ok());
  QCOMPARE(result.contact.resourceName, QStringLiteral("people/c1"));
  QCOMPARE(result.contact.photoUrl, QStringLiteral("https://lh3.googleusercontent.com/a=s100"));
  QCOMPARE(http.urls.size(), 1);
  QVERIFY(http.urls.first().contains(QStringLiteral("people/c1:updateContactPhoto")));
  QVERIFY(http.bodies.first().contains("photoBytes"));
  QVERIFY(http.bodies.first().contains(QByteArray("jpeg-bytes").toBase64()));

  http.script.append({200, QByteArray(R"({"person":{"resourceName":"people/c1","etag":"e3","photos":[{"url":"https://lh3.googleusercontent.com/silhouette","default":true}]}})"),
                      {}, {}});
  const auto removed = omacontacts::deleteContactPhoto(http, token, QStringLiteral("people/c1"), {});
  QVERIFY(removed.ok());
  QVERIFY(removed.contact.photoUrl.isEmpty());
  QVERIFY(http.urls.last().contains(QStringLiteral(":deleteContactPhoto")));
}

void GoogleTests::labelTypesRoundTrip() {
  const auto home = omacontacts::resolveLabel(omacontacts::LabelField::Email, QStringLiteral("Home"));
  QCOMPARE(home.type, QStringLiteral("home"));
  QCOMPARE(home.display, QStringLiteral("Home"));
  const auto other = omacontacts::resolveLabel(omacontacts::LabelField::Email, QStringLiteral("other"));
  QCOMPARE(other.type, QStringLiteral("other"));
  const auto school = omacontacts::resolveLabel(omacontacts::LabelField::Email, QStringLiteral("School"));
  QCOMPARE(school.type, QStringLiteral("School"));
  QCOMPARE(school.display, QStringLiteral("School"));
  const auto voice = omacontacts::resolveLabel(omacontacts::LabelField::Phone, QStringLiteral("Google Voice"));
  QCOMPARE(voice.type, QStringLiteral("googleVoice"));
  QCOMPARE(voice.display, QStringLiteral("Google Voice"));
  const auto typedHome = omacontacts::resolveLabel(omacontacts::LabelField::Phone, QStringLiteral("home"));
  QCOMPARE(typedHome.type, QStringLiteral("home"));
  const auto custom = omacontacts::resolveLabel(omacontacts::LabelField::Phone, QStringLiteral("Front desk"));
  QCOMPARE(custom.type, QStringLiteral("Front desk"));
  const auto blankEmail = omacontacts::resolveLabel(omacontacts::LabelField::Email, QString());
  QCOMPARE(blankEmail.type, QString());
  QCOMPARE(blankEmail.display, QString());
  const auto blankPhone = omacontacts::resolveLabel(omacontacts::LabelField::Phone, QString());
  QCOMPARE(blankPhone.type, QString());
  const auto addressHome = omacontacts::resolveLabel(omacontacts::LabelField::Address, QStringLiteral("Home"));
  QCOMPARE(addressHome.type, QStringLiteral("home"));
  QCOMPARE(addressHome.display, QStringLiteral("Home"));
  const auto addressOther = omacontacts::resolveLabel(omacontacts::LabelField::Address, QStringLiteral("Other"));
  QCOMPARE(addressOther.type, QStringLiteral("other"));
  QCOMPARE(addressOther.display, QStringLiteral("Other"));
  const auto parents = omacontacts::resolveLabel(omacontacts::LabelField::Address, QStringLiteral("Parents"));
  QCOMPARE(parents.type, QStringLiteral("Parents"));
  QCOMPARE(parents.display, QStringLiteral("Parents"));

  omacontacts::Contact contact;
  contact.givenName = QStringLiteral("Ada");
  contact.emails.append({QStringLiteral("ada@example.com"), QStringLiteral("Other"), QStringLiteral("other")});
  contact.phones.append({QStringLiteral("+1555"), QStringLiteral("Google Voice"), QStringLiteral("googleVoice")});
  const QByteArray json = omacontacts::personJson(contact);
  QVERIFY(json.contains("\"type\":\"other\""));
  QVERIFY(json.contains("\"type\":\"googleVoice\""));
  QVERIFY(!json.contains("formattedType"));
  const auto parsed = omacontacts::parsePersonJson(json);
  QVERIFY(parsed.has_value());
  QCOMPARE(parsed->emails.at(0).type, QStringLiteral("other"));
  QCOMPARE(parsed->emails.at(0).label, QStringLiteral("Other"));
  QCOMPARE(parsed->phones.at(0).type, QStringLiteral("googleVoice"));
  QCOMPARE(parsed->phones.at(0).label, QStringLiteral("Google Voice"));

  omacontacts::Contact unlabeled;
  unlabeled.emails.append({QStringLiteral("ada@example.com"), QString(), QString()});
  unlabeled.phones.append({QStringLiteral("+1555"), QString(), QString()});
  omacontacts::PostalAddress bare;
  bare.street = QStringLiteral("1 Main");
  unlabeled.addresses.append(bare);
  omacontacts::PostalAddress otherAddress;
  otherAddress.type = QStringLiteral("other");
  otherAddress.street = QStringLiteral("2 Main");
  unlabeled.addresses.append(otherAddress);
  omacontacts::PostalAddress customAddress;
  customAddress.type = QStringLiteral("Parents");
  customAddress.street = QStringLiteral("3 Main");
  unlabeled.addresses.append(customAddress);
  const auto unlabeledParsed = omacontacts::parsePersonJson(omacontacts::personJson(unlabeled));
  QVERIFY(unlabeledParsed.has_value());
  QCOMPARE(unlabeledParsed->emails.at(0).type, QString());
  QCOMPARE(unlabeledParsed->phones.at(0).type, QString());
  QCOMPARE(unlabeledParsed->addresses.at(0).type, QString());
  QCOMPARE(unlabeledParsed->addresses.at(1).type, QStringLiteral("other"));
  QCOMPARE(unlabeledParsed->addresses.at(1).label, QStringLiteral("Other"));
  QCOMPARE(unlabeledParsed->addresses.at(2).type, QStringLiteral("Parents"));
  QCOMPARE(unlabeledParsed->addresses.at(2).label, QStringLiteral("Parents"));

  const auto legacy = omacontacts::readCacheJson(
      R"({"version":2,"sync_token":"s","contacts":[{"resource_name":"people/c1","given_name":"Ada","emails":[{"value":"ada@example.com","label":"work"}],"phones":[{"value":"+1","label":"School"}]}]})");
  QVERIFY(legacy.ok());
  QCOMPARE(legacy.contacts.at(0).emails.at(0).type, QStringLiteral("work"));
  QCOMPARE(legacy.contacts.at(0).phones.at(0).type, QStringLiteral("School"));
  QCOMPARE(legacy.contacts.at(0).phones.at(0).label, QStringLiteral("School"));
}

void GoogleTests::fullAndIncrementalSync() {
  omacontacts::Contact ada;
  ada.resourceName = QStringLiteral("people/ada");
  ada.givenName = QStringLiteral("Ada");
  omacontacts::Contact alan;
  alan.resourceName = QStringLiteral("people/alan");
  alan.givenName = QStringLiteral("Alan");
  const auto full = omacontacts::applyFullSync({{ada, false}});
  QCOMPARE(full.size(), 1);
  omacontacts::Contact ada2 = ada;
  ada2.familyName = QStringLiteral("Lovelace");
  const auto merged = omacontacts::applyIncrementalSync({ada, alan}, {{ada2, false}, {alan, true}});
  QCOMPARE(merged.size(), 1);
  QCOMPARE(merged.at(0).familyName, QStringLiteral("Lovelace"));
}

void GoogleTests::retriesOnceAfterUnauthorized() {
  ScriptedHttp http;
  http.script.append({401, R"({"error":{"message":"expired"}})", {}, {}});
  http.script.append({200, R"({"connections":[],"nextSyncToken":"s"})", {}, {}});
  QString token = QStringLiteral("old");
  int refreshes = 0;
  const auto result = omacontacts::fetchConnections(http, token, QString(), [&] {
    ++refreshes;
    token = QStringLiteral("new");
    return QString();
  });
  QVERIFY(result.ok());
  QCOMPARE(refreshes, 1);
  QCOMPARE(result.syncToken, QStringLiteral("s"));
  QCOMPARE(http.urls.size(), 2);
}

void GoogleTests::createReadsResource() {
  ScriptedHttp http;
  http.script.append(
      {200, R"({"resourceName":"people/c9","etag":"e","names":[{"givenName":"Ada","familyName":"Lovelace"}]})", {}, {}});
  omacontacts::Contact draft;
  draft.givenName = QStringLiteral("Ada");
  draft.familyName = QStringLiteral("Lovelace");
  QString token = QStringLiteral("token");
  const auto result = omacontacts::createContact(http, token, draft, {});
  QVERIFY2(result.ok(), qPrintable(result.error));
  QCOMPARE(result.contact.resourceName, QStringLiteral("people/c9"));
  QVERIFY(http.urls.first().contains(QStringLiteral("people:createContact")));
}

void GoogleTests::mailUsesDesktopHandler() {
  omacontacts::Contact contact;
  contact.givenName = QStringLiteral("Ada");
  contact.familyName = QStringLiteral("Lovelace");
  contact.emails.append({QStringLiteral("ada@example.com"), QStringLiteral("Work"), QStringLiteral("work")});
  const auto command = omacontacts::mailCommand(contact);
  QVERIFY(command.ok());
  QCOMPARE(command.argv.size(), 4);
  QCOMPARE(command.argv.at(0), QStringLiteral("xdg-email"));
  QCOMPARE(command.argv.at(3), QStringLiteral("ada@example.com"));
  omacontacts::Contact nameless;
  QVERIFY(!omacontacts::mailCommand(nameless).ok());
}

void GoogleTests::browserPrefersOmarchy() {
  const auto command = omacontacts::browserCommand(QStringLiteral("https://example.com"), true);
  QCOMPARE(command.argv.size(), 4);
  QCOMPARE(command.argv.at(0), QStringLiteral("omarchy"));
  QCOMPARE(command.argv.at(2), QStringLiteral("browser"));
  const auto fallback = omacontacts::browserCommand(QStringLiteral("https://example.com"), false);
  QCOMPARE(fallback.argv.at(0), QStringLiteral("xdg-open"));
}

void GoogleTests::storeRoundTrip() {
  omacontacts::Contact contact;
  contact.resourceName = QStringLiteral("people/c1");
  contact.givenName = QStringLiteral("Ada");
  contact.notes = QStringLiteral("a \"quote\"");
  contact.photoUrl = QStringLiteral("https://example.test/ada.jpg");
  contact.emails.append({QStringLiteral("ada@example.com"), QStringLiteral("Work"), QStringLiteral("work")});
  const auto cache = omacontacts::readCacheJson(omacontacts::writeCacheJson({contact}, QStringLiteral("sync")));
  QVERIFY(cache.ok());
  QCOMPARE(cache.syncToken, QStringLiteral("sync"));
  QCOMPARE(cache.contacts.size(), 1);
  QCOMPARE(cache.contacts.at(0), contact);
  QVERIFY(!omacontacts::readCacheJson("{").ok());

  const auto config =
      omacontacts::readConfigJson(omacontacts::writeConfigJson({QStringLiteral("id"), QStringLiteral("secret")}));
  QVERIFY(config.has_value());
  QCOMPARE(config->clientSecret, QStringLiteral("secret"));

  const auto stored =
      omacontacts::readTokensJson(omacontacts::writeTokensJson({QStringLiteral("access"), QStringLiteral("refresh"), 50}));
  QVERIFY(stored.has_value());
  QCOMPARE(stored->refreshToken, QStringLiteral("refresh"));
  QCOMPARE(stored->expiryUnix, 50);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString file = dir.filePath(QStringLiteral("nested/config.json"));
  QString error;
  QVERIFY2(omacontacts::writePrivateFile(file, "{\"client_id\":\"abc\"}", error), qPrintable(error));
  QFileInfo info(file);
  QCOMPARE(info.permissions() & (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup |
                                 QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther),
           QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  const auto paths = omacontacts::AppPaths::forEnv(QStringLiteral("/home/duff"), QStringLiteral("/xdg/config"), QString());
  QCOMPARE(paths.configFile, QStringLiteral("/xdg/config/omacontacts/config.json"));
  QCOMPARE(paths.cacheFile, QStringLiteral("/home/duff/.local/share/omacontacts/contacts.json"));
  QCOMPARE(paths.photoDir, QStringLiteral("/home/duff/.cache/omacontacts/photos"));
}

omacontacts::Contact sampleContact(QString id, QString given, QString family, QString email, QString phone,
                                    QString notes) {
  omacontacts::Contact contact;
  contact.resourceName = std::move(id);
  contact.givenName = std::move(given);
  contact.familyName = std::move(family);
  contact.notes = std::move(notes);
  if (!email.isEmpty()) contact.emails.append({std::move(email), QStringLiteral("Work"), QStringLiteral("work")});
  if (!phone.isEmpty()) contact.phones.append({std::move(phone), QStringLiteral("Mobile"), QStringLiteral("mobile")});
  return contact;
}

void LayoutTests::enterReplacesTheListWithThatContact() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QByteArray oldData = qgetenv("XDG_DATA_HOME");
  const QByteArray oldCache = qgetenv("XDG_CACHE_HOME");
  const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
  qputenv("XDG_DATA_HOME", dir.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", dir.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("XDG_CONFIG_HOME", dir.filePath(QStringLiteral("config")).toUtf8());
  qputenv("OMACONTACTS_NO_SYNC", "1");

  const QString caraOneNotes =
      QStringLiteral("Notes for Cara One that must stay on screen. The whole note is visible without scrolling the detail.");
  const QString caraTwoNotes =
      QStringLiteral("Notes for Cara Two that must stay on screen. The whole note is visible without scrolling the detail.");
  omacontacts::MainWindow window;
  window.resize(1000, 700);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QList<omacontacts::Contact> contacts = {
      sampleContact(QStringLiteral("people/cara1"), QStringLiteral("Cara"), QStringLiteral("One"),
                    QStringLiteral("cara.one@example.com"), QStringLiteral("+1 555 010 1001"), caraOneNotes),
      sampleContact(QStringLiteral("people/cara2"), QStringLiteral("Cara"), QStringLiteral("Two"),
                    QStringLiteral("cara.two@example.com"), QStringLiteral("+1 555 010 1002"), caraTwoNotes),
  };
  for (int i = 0; i < 400; ++i) {
    contacts.append(sampleContact(QStringLiteral("people/other%1").arg(i), QStringLiteral("Other"),
                                  QString::number(i), QStringLiteral("other%1@example.com").arg(i), {}, {}));
  }
  window.setContacts(std::move(contacts));
  QApplication::processEvents();
  const auto describe = [&] {
    return QStringLiteral("detail height %1 window %2 on screen %3 text [%4]")
        .arg(window.detailPaneHeight())
        .arg(window.height())
        .arg(window.detailIsOnScreen())
        .arg(window.detailPlainText());
  };
  auto* list = window.findChild<QListView*>();
  QVERIFY(list != nullptr);
  QVERIFY2(list->isVisible() && list->height() >= 200 && !window.detailShows(QStringLiteral("cara.one@example.com")),
           qPrintable(describe() + QStringLiteral(" list %1 visible %2").arg(list->height()).arg(list->isVisible())));
  QTest::keyClick(&window, Qt::Key_Down);
  QApplication::processEvents();
  QVERIFY2(!window.detailShows(QStringLiteral("cara.two@example.com")) && list->isVisible(),
           qPrintable(describe() + QStringLiteral(" list %1").arg(list->height())));
  QTest::keyClick(&window, Qt::Key_Return);
  QApplication::processEvents();
  QVERIFY2(window.detailShows(QStringLiteral("cara.two@example.com")) && window.detailShows(caraTwoNotes) &&
               window.detailShows(QStringLiteral("Full name")) && window.detailShows(QStringLiteral("First name")) &&
               window.detailShows(QStringLiteral("Last name")) && !window.detailShows(QStringLiteral("Given name")) &&
               !window.detailShows(QStringLiteral("cara.one@example.com")) && !list->isVisible(),
           qPrintable(describe() + QStringLiteral(" list %1 visible %2").arg(list->height()).arg(list->isVisible())));
  const auto countText = [&] {
    QString text;
    const auto labels = window.findChildren<QLabel*>(QStringLiteral("status"));
    for (auto* label : labels) {
      if (label->text().contains(QStringLiteral("contact")) || label->text().contains(QStringLiteral(" of ")))
        text = label->text();
    }
    return text;
  };
  QVERIFY2(countText().isEmpty(), qPrintable(countText()));
  QTest::keyClick(&window, Qt::Key_Down);
  QApplication::processEvents();
  QVERIFY2(window.detailShows(QStringLiteral("cara.two@example.com")) && !window.detailShows(QStringLiteral("cara.one@example.com")),
           qPrintable(describe()));
  window.resize(529, 632);
  QApplication::processEvents();
  QVERIFY2(window.detailShows(QStringLiteral("cara.two@example.com")) && window.detailShows(caraTwoNotes) && !list->isVisible(),
           qPrintable(describe() + QStringLiteral(" list visible %1").arg(list->isVisible())));
  QTest::keyClick(&window, Qt::Key_Escape);
  QApplication::processEvents();
  QVERIFY2(list->isVisible() && list->height() >= 120 && !window.detailShows(QStringLiteral("cara.two@example.com")),
           qPrintable(describe() + QStringLiteral(" list %1 visible %2").arg(list->height()).arg(list->isVisible())));
  QVERIFY2(countText().contains(QStringLiteral("contacts")), qPrintable(countText()));

  if (oldData.isNull()) qunsetenv("XDG_DATA_HOME");
  else qputenv("XDG_DATA_HOME", oldData);
  if (oldCache.isNull()) qunsetenv("XDG_CACHE_HOME");
  else qputenv("XDG_CACHE_HOME", oldCache);
  if (oldConfig.isNull()) qunsetenv("XDG_CONFIG_HOME");
  else qputenv("XDG_CONFIG_HOME", oldConfig);
}

void LayoutTests::editOpensOnTheSelectedField() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QByteArray oldData = qgetenv("XDG_DATA_HOME");
  const QByteArray oldCache = qgetenv("XDG_CACHE_HOME");
  const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
  qputenv("XDG_DATA_HOME", dir.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", dir.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("XDG_CONFIG_HOME", dir.filePath(QStringLiteral("config")).toUtf8());
  qputenv("OMACONTACTS_NO_SYNC", "1");

  const QString notes = QStringLiteral("Keep this note.");
  auto cara = sampleContact(QStringLiteral("people/cara"), QStringLiteral("Cara"), QStringLiteral("Two"), {},
                            QStringLiteral("+1 555 010 1002"), notes);
  cara.emails = {
      {QStringLiteral("home@example.com"), QStringLiteral("Home"), QStringLiteral("home")},
      {QStringLiteral("work@example.com"), QStringLiteral("Work"), QStringLiteral("work")},
  };
  omacontacts::MainWindow window;
  window.resize(1000, 700);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  window.setContacts({cara});
  QApplication::processEvents();

  QSize editorSize;
  bool editorInside = false;
  const auto editAndReadFocus = [&](QString* lineText, bool* notesFocused) {
    lineText->clear();
    *notesFocused = false;
    QTest::keyClick(&window, Qt::Key_E, Qt::ControlModifier);
    QApplication::processEvents();
    auto* editor = window.findChild<QWidget*>(QStringLiteral("editor"));
    editorInside = editor != nullptr && editor->isVisible() && editor->width() <= window.width() && editor->height() < window.height() &&
                   QApplication::activeModalWidget() == nullptr;
    if (editor != nullptr) editorSize = editor->size();
    QWidget* focus = window.focusWidget();
    if (auto* line = qobject_cast<QLineEdit*>(focus)) *lineText = line->text();
    *notesFocused = qobject_cast<QPlainTextEdit*>(focus) != nullptr;
    QTest::keyClick(&window, Qt::Key_Escape);
    QApplication::processEvents();
  };

  QString lineText;
  bool notesFocused = false;
  editAndReadFocus(&lineText, &notesFocused);
  QCOMPARE(lineText, QStringLiteral("Cara"));
  QVERIFY2(editorInside, qPrintable(QStringLiteral("editor %1x%2 window %3x%4")
                                        .arg(editorSize.width())
                                        .arg(editorSize.height())
                                        .arg(window.width())
                                        .arg(window.height())));

  QTest::keyClick(&window, Qt::Key_Return);
  QApplication::processEvents();
  QListView* fields = nullptr;
  for (auto* view : window.findChildren<QListView*>()) {
    if (view->isVisible()) fields = view;
  }
  QVERIFY(fields != nullptr);
  const auto selectValue = [&](const QString& value) {
    for (int i = 0; i < fields->model()->rowCount(); ++i) {
      if (fields->model()->index(i, 0).data(Qt::DisplayRole).toString() == value) {
        fields->setCurrentIndex(fields->model()->index(i, 0));
        fields->setFocus();
        return true;
      }
    }
    return false;
  };
  QVERIFY(selectValue(QStringLiteral("Two")));
  editAndReadFocus(&lineText, &notesFocused);
  QCOMPARE(lineText, QStringLiteral("Two"));
  QVERIFY(selectValue(QStringLiteral("work@example.com")));
  editAndReadFocus(&lineText, &notesFocused);
  QCOMPARE(lineText, QStringLiteral("work@example.com"));
  QVERIFY(selectValue(notes));
  editAndReadFocus(&lineText, &notesFocused);
  QVERIFY(notesFocused);

  if (oldData.isNull()) qunsetenv("XDG_DATA_HOME");
  else qputenv("XDG_DATA_HOME", oldData);
  if (oldCache.isNull()) qunsetenv("XDG_CACHE_HOME");
  else qputenv("XDG_CACHE_HOME", oldCache);
  if (oldConfig.isNull()) qunsetenv("XDG_CONFIG_HOME");
  else qputenv("XDG_CONFIG_HOME", oldConfig);
}

void LayoutTests::companyWithoutAPersonalName() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QByteArray oldData = qgetenv("XDG_DATA_HOME");
  const QByteArray oldCache = qgetenv("XDG_CACHE_HOME");
  const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
  qputenv("XDG_DATA_HOME", dir.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", dir.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("XDG_CONFIG_HOME", dir.filePath(QStringLiteral("config")).toUtf8());
  qputenv("OMACONTACTS_NO_SYNC", "1");

  omacontacts::Contact contact;
  contact.resourceName = QStringLiteral("people/download");
  contact.organization = QStringLiteral("download.com");
  contact.fileAs = QStringLiteral("Download");
  contact.phoneticGiven = QStringLiteral("down");
  contact.phoneticMiddle = QStringLiteral("load");
  contact.phoneticFamily = QStringLiteral("com");
  omacontacts::ImClient im;
  im.username = QStringLiteral("dl");
  im.protocol = QStringLiteral("skype");
  contact.ims.append(im);
  contact.sips.append({QStringLiteral("sip:dl@example.com"), QStringLiteral("Work"), QStringLiteral("work")});
  omacontacts::PostalAddress office;
  office.label = QStringLiteral("Work");
  office.type = QStringLiteral("work");
  office.street = QStringLiteral("1 Main St");
  office.city = QStringLiteral("Austin");
  office.region = QStringLiteral("TX");
  office.postalCode = QStringLiteral("78701");
  contact.addresses.append(office);

  omacontacts::MainWindow window;
  window.resize(1000, 700);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  window.setContacts({contact});
  QApplication::processEvents();
  QTest::keyClick(&window, Qt::Key_Return);
  QApplication::processEvents();

  const QString detail = window.detailPlainText();
  QVERIFY2(!detail.contains(QStringLiteral("Full name")), qPrintable(detail));
  QVERIFY2(detail.contains(QStringLiteral("Company")) && detail.contains(QStringLiteral("download.com")), qPrintable(detail));
  QVERIFY2(detail.contains(QStringLiteral("1 Main St\nAustin, TX 78701")), qPrintable(detail));
  QVERIFY2(detail.contains(QStringLiteral("File as")) && detail.contains(QStringLiteral("Phonetic first name")) &&
               detail.contains(QStringLiteral("Instant message")) && detail.contains(QStringLiteral("SIP")),
           qPrintable(detail));

  QListView* fields = nullptr;
  for (auto* view : window.findChildren<QListView*>()) {
    if (view->isVisible()) fields = view;
  }
  QVERIFY(fields != nullptr);
  bool addressTaller = false;
  for (int i = 0; i < fields->model()->rowCount(); ++i) {
    const QModelIndex index = fields->model()->index(i, 0);
    if (!index.data(Qt::DisplayRole).toString().contains(QLatin1Char('\n'))) continue;
    fields->scrollTo(index);
    QApplication::processEvents();
    addressTaller = fields->visualRect(index).height() > 28;
  }
  QVERIFY(addressTaller);

  const auto selectValue = [&](const QString& value) {
    for (int i = 0; i < fields->model()->rowCount(); ++i) {
      if (fields->model()->index(i, 0).data(Qt::DisplayRole).toString() == value) {
        fields->setCurrentIndex(fields->model()->index(i, 0));
        fields->setFocus();
        return true;
      }
    }
    return false;
  };
  QVERIFY(selectValue(QStringLiteral("1 Main St\nAustin, TX 78701")));
  QTest::keyClick(&window, Qt::Key_E, Qt::ControlModifier);
  QApplication::processEvents();

  auto* editor = window.findChild<QWidget*>(QStringLiteral("editor"));
  QVERIFY(editor != nullptr && editor->isVisible());
  QWidget* focus = window.focusWidget();
  auto* line = qobject_cast<QLineEdit*>(focus);
  QVERIFY(line != nullptr);
  QCOMPARE(line->text(), QStringLiteral("1 Main St"));

  const auto button = [&](const QString& text) -> QPushButton* {
    for (auto* candidate : editor->findChildren<QPushButton*>()) {
      if (candidate->text() == text) return candidate;
    }
    return nullptr;
  };
  auto* addPhone = button(QStringLiteral("Add phone"));
  auto* addAddress = button(QStringLiteral("Add address"));
  auto* more = button(QStringLiteral("More"));
  QVERIFY(addPhone != nullptr && addPhone->isVisible());
  QVERIFY(addAddress != nullptr && addAddress->isVisible());
  QVERIFY(more != nullptr && more->isVisible());
  QVERIFY(addPhone->mapTo(editor, QPoint(0, 0)).y() < addAddress->mapTo(editor, QPoint(0, 0)).y());
  QVERIFY(addAddress->mapTo(editor, QPoint(0, 0)).y() < more->mapTo(editor, QPoint(0, 0)).y());
  QVERIFY(button(QStringLiteral("Add instant message")) == nullptr);
  QVERIFY(button(QStringLiteral("Add SIP address")) == nullptr);
  QTest::mouseClick(more, Qt::LeftButton);
  QApplication::processEvents();
  const auto labelNamed = [&](const QString& text) -> QLabel* {
    for (auto* label : editor->findChildren<QLabel*>()) {
      if (label->text() == text) return label;
    }
    return nullptr;
  };
  for (const QString& heading : {QStringLiteral("Website"), QStringLiteral("Related person"), QStringLiteral("Another company"),
                                 QStringLiteral("Significant date"), QStringLiteral("Custom field")}) {
    QLabel* headingLabel = labelNamed(heading);
    QVERIFY2(headingLabel != nullptr && !headingLabel->isVisible(), qPrintable(heading));
  }
  QVERIFY(button(QStringLiteral("Add website")) != nullptr && button(QStringLiteral("Add website"))->isVisible());
  QVERIFY(button(QStringLiteral("Add related person")) != nullptr && button(QStringLiteral("Add related person"))->isVisible());
  for (auto* field : editor->findChildren<QLineEdit*>()) {
    QVERIFY(field->placeholderText() != QStringLiteral("PO box"));
  }
  for (auto* label : editor->findChildren<QLabel*>()) {
    const QString text = label->text();
    QVERIFY(text != QStringLiteral("File as"));
    QVERIFY(text != QStringLiteral("Phonetic first name"));
    QVERIFY(text != QStringLiteral("Phonetic middle name"));
    QVERIFY(text != QStringLiteral("Phonetic last name"));
    QVERIFY(text != QStringLiteral("Instant message"));
    QVERIFY(text != QStringLiteral("SIP address"));
  }

  contact.addresses[0].poBox = QStringLiteral("12");
  omacontacts::ContactEditor direct(contact, {}, nullptr);
  const auto saved = direct.validatedContact();
  QVERIFY(saved.has_value());
  QCOMPARE(saved->organization, QStringLiteral("download.com"));
  QCOMPARE(saved->givenName, QString());
  QCOMPARE(saved->fileAs, QStringLiteral("Download"));
  QCOMPARE(saved->phoneticGiven, QStringLiteral("down"));
  QCOMPARE(saved->phoneticMiddle, QStringLiteral("load"));
  QCOMPARE(saved->phoneticFamily, QStringLiteral("com"));
  QCOMPARE(saved->ims.size(), 1);
  QCOMPARE(saved->ims.at(0).username, QStringLiteral("dl"));
  QCOMPARE(saved->sips.size(), 1);
  QCOMPARE(saved->sips.at(0).value, QStringLiteral("sip:dl@example.com"));
  QCOMPARE(saved->addresses.size(), 1);
  QCOMPARE(saved->addresses.at(0).street, QStringLiteral("1 Main St"));
  QCOMPARE(saved->addresses.at(0).city, QStringLiteral("Austin"));
  QCOMPARE(saved->addresses.at(0).region, QStringLiteral("TX"));
  QCOMPARE(saved->addresses.at(0).postalCode, QStringLiteral("78701"));
  QCOMPARE(saved->addresses.at(0).poBox, QStringLiteral("12"));

  if (oldData.isNull()) qunsetenv("XDG_DATA_HOME");
  else qputenv("XDG_DATA_HOME", oldData);
  if (oldCache.isNull()) qunsetenv("XDG_CACHE_HOME");
  else qputenv("XDG_CACHE_HOME", oldCache);
  if (oldConfig.isNull()) qunsetenv("XDG_CONFIG_HOME");
  else qputenv("XDG_CONFIG_HOME", oldConfig);
}

void LayoutTests::notesWrapInsteadOfEllipsis() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QByteArray oldData = qgetenv("XDG_DATA_HOME");
  const QByteArray oldCache = qgetenv("XDG_CACHE_HOME");
  const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
  qputenv("XDG_DATA_HOME", dir.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", dir.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("XDG_CONFIG_HOME", dir.filePath(QStringLiteral("config")).toUtf8());
  qputenv("OMACONTACTS_NO_SYNC", "1");

  omacontacts::Contact contact;
  contact.resourceName = QStringLiteral("people/notes");
  contact.givenName = QStringLiteral("Dan");
  contact.familyName = QStringLiteral("Williams");
  contact.notes = QStringLiteral("alpha ").repeated(80).trimmed();

  omacontacts::MainWindow window;
  window.resize(1000, 700);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  window.setContacts({contact});
  QApplication::processEvents();
  QTest::keyClick(&window, Qt::Key_Return);
  QApplication::processEvents();

  QListView* fields = nullptr;
  for (auto* view : window.findChildren<QListView*>()) {
    if (view->isVisible()) fields = view;
  }
  QVERIFY(fields != nullptr);
  bool wrapped = false;
  for (int i = 0; i < fields->model()->rowCount(); ++i) {
    const QModelIndex index = fields->model()->index(i, 0);
    if (index.data(Qt::UserRole).toString() != QStringLiteral("Notes")) continue;
    QCOMPARE(index.data(Qt::DisplayRole).toString(), contact.notes);
    QVERIFY(!index.data(Qt::DisplayRole).toString().contains(QStringLiteral("…")));
    fields->scrollTo(index);
    QApplication::processEvents();
    wrapped = fields->visualRect(index).height() > 28;
  }
  QVERIFY(wrapped);

  if (oldData.isNull()) qunsetenv("XDG_DATA_HOME");
  else qputenv("XDG_DATA_HOME", oldData);
  if (oldCache.isNull()) qunsetenv("XDG_CACHE_HOME");
  else qputenv("XDG_CACHE_HOME", oldCache);
  if (oldConfig.isNull()) qunsetenv("XDG_CONFIG_HOME");
  else qputenv("XDG_CONFIG_HOME", oldConfig);
}

void LayoutTests::blankPhoneTypeStaysBlank() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QByteArray oldData = qgetenv("XDG_DATA_HOME");
  const QByteArray oldCache = qgetenv("XDG_CACHE_HOME");
  const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
  qputenv("XDG_DATA_HOME", dir.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", dir.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("XDG_CONFIG_HOME", dir.filePath(QStringLiteral("config")).toUtf8());
  qputenv("OMACONTACTS_NO_SYNC", "1");

  omacontacts::Contact contact;
  contact.resourceName = QStringLiteral("people/phone");
  contact.givenName = QStringLiteral("Dan");
  contact.familyName = QStringLiteral("Williams");
  contact.phones.append({QStringLiteral("+15551212"), QString(), QString()});

  omacontacts::MainWindow window;
  window.resize(1000, 700);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  window.setContacts({contact});
  QApplication::processEvents();
  QTest::keyClick(&window, Qt::Key_Return);
  QApplication::processEvents();

  QVERIFY(!window.detailShows(QStringLiteral("Phone")));
  QVERIFY(!window.detailShows(QStringLiteral("Mobile")));
  QVERIFY(window.detailShows(QStringLiteral("+15551212")));

  QTest::keyClick(&window, Qt::Key_E, Qt::ControlModifier);
  QApplication::processEvents();
  auto* editor = window.findChild<QWidget*>(QStringLiteral("editor"));
  QVERIFY(editor != nullptr);
  QComboBox* label = nullptr;
  for (auto* field : editor->findChildren<QLineEdit*>()) {
    if (field->text() != QStringLiteral("+15551212")) continue;
    label = field->parentWidget() == nullptr ? nullptr : field->parentWidget()->findChild<QComboBox*>();
  }
  QVERIFY(label != nullptr);
  QCOMPARE(label->currentText(), QString());
  const auto saved = [&] {
    omacontacts::ContactEditor direct(contact, {}, nullptr);
    return direct.validatedContact();
  }();
  QVERIFY(saved.has_value());
  QCOMPARE(saved->phones.at(0).type, QString());
  QCOMPARE(saved->phones.at(0).label, QString());

  if (oldData.isNull()) qunsetenv("XDG_DATA_HOME");
  else qputenv("XDG_DATA_HOME", oldData);
  if (oldCache.isNull()) qunsetenv("XDG_CACHE_HOME");
  else qputenv("XDG_CACHE_HOME", oldCache);
  if (oldConfig.isNull()) qunsetenv("XDG_CONFIG_HOME");
  else qputenv("XDG_CONFIG_HOME", oldConfig);
}

void GuardTests::requestsStayOnHttp1() {
  QNetworkRequest request{QUrl(QStringLiteral("https://people.googleapis.com/v1/people/me/connections"))};
  omacontacts::prepareNetworkRequest(request);
  QCOMPARE(request.attribute(QNetworkRequest::Http2AllowedAttribute).toBool(), false);
  QCOMPARE(request.attribute(QNetworkRequest::Http2DirectAttribute).toBool(), false);
  QCOMPARE(request.attribute(QNetworkRequest::Http2CleartextAllowedAttribute).toBool(), false);
  QCOMPARE(request.transferTimeout(), 30000);
  const QList<QByteArray> protocols = request.sslConfiguration().allowedNextProtocols();
  QVERIFY(protocols.contains(QByteArray(QSslConfiguration::NextProtocolHttp1_1)));
  QVERIFY(!protocols.contains(QByteArray(QSslConfiguration::ALPNProtocolHTTP2)));
}

void GuardTests::secondLaunchRaisesTheFirst() {
  const QString name = QDir::tempPath() + QStringLiteral("/omacontacts-test-%1.sock").arg(QCoreApplication::applicationPid());
  omacontacts::InstanceLock first(name);
  int raises = 0;
  QVERIFY(first.claim([&raises] { ++raises; }));
  omacontacts::InstanceLock second(name);
  QVERIFY(!second.claim([] {}));
  QCOMPARE(first.raises(), 1);
  QCOMPARE(raises, 1);
}

void ShortcutTests::typingSuspendsSingleKeys() {
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::J, true)), omacontacts::Command::Ignore);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::Down, true)), omacontacts::Command::MoveDown);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::N, true)), omacontacts::Command::Ignore);
  auto created = browser(omacontacts::Key::N, true);
  created.ctrl = true;
  QCOMPARE(omacontacts::routeKey(created), omacontacts::Command::NewContact);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::F1, true)), omacontacts::Command::Help);
}

void ShortcutTests::escapeClearsBeforeBack() {
  auto event = browser(omacontacts::Key::Escape, true);
  event.searchEmpty = false;
  event.detailOpen = true;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::Back);
  event.detailOpen = false;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::ClearSearch);
  event.searchEmpty = true;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::Ignore);
  event.detailOpen = true;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::Back);
  auto down = browser(omacontacts::Key::Down, true);
  down.detailOpen = true;
  QCOMPARE(omacontacts::routeKey(down), omacontacts::Command::MoveDown);
  auto act = browser(omacontacts::Key::Return, true);
  act.detailOpen = true;
  QCOMPARE(omacontacts::routeKey(act), omacontacts::Command::ActivateField);
  auto copy = browser(omacontacts::Key::C);
  copy.ctrl = true;
  copy.detailOpen = true;
  QCOMPARE(omacontacts::routeKey(copy), omacontacts::Command::CopyField);
  copy.textInput = true;
  QCOMPARE(omacontacts::routeKey(copy), omacontacts::Command::Ignore);
}

void ShortcutTests::listKeys() {
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::J)), omacontacts::Command::Ignore);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::E)), omacontacts::Command::Ignore);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::R)), omacontacts::Command::Ignore);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::PageDown)), omacontacts::Command::Ignore);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::Down)), omacontacts::Command::MoveDown);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::Slash)), omacontacts::Command::FocusSearch);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::Slash, true)), omacontacts::Command::Ignore);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::Delete)), omacontacts::Command::Ignore);
  auto mail = browser(omacontacts::Key::M);
  mail.ctrl = true;
  mail.detailOpen = true;
  QCOMPARE(omacontacts::routeKey(mail), omacontacts::Command::SendEmail);
  auto backspace = browser(omacontacts::Key::Backspace);
  backspace.detailOpen = true;
  QCOMPARE(omacontacts::routeKey(backspace), omacontacts::Command::Back);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::Return)), omacontacts::Command::OpenContact);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::Return, true)), omacontacts::Command::OpenContact);
  auto edit = browser(omacontacts::Key::Return);
  edit.ctrl = true;
  QCOMPARE(omacontacts::routeKey(edit), omacontacts::Command::EditContact);
  auto ctrlE = browser(omacontacts::Key::E);
  ctrlE.ctrl = true;
  QCOMPARE(omacontacts::routeKey(ctrlE), omacontacts::Command::EditContact);
  QCOMPARE(omacontacts::routeKey(browser(omacontacts::Key::Question)), omacontacts::Command::Help);
  auto quit = browser(omacontacts::Key::Q);
  quit.ctrl = true;
  QCOMPARE(omacontacts::routeKey(quit), omacontacts::Command::Quit);
}

void ShortcutTests::editorKeepsKeys() {
  omacontacts::KeyEvent event;
  event.surface = omacontacts::Surface::Editor;
  event.textInput = true;
  event.key = omacontacts::Key::N;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::Ignore);
  event.key = omacontacts::Key::Escape;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::Cancel);
  event.key = omacontacts::Key::Return;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::Save);
  event.multiline = true;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::Ignore);
  event.ctrl = true;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::Save);
}

void ShortcutTests::accountKeepsQuestionMarks() {
  omacontacts::KeyEvent event;
  event.surface = omacontacts::Surface::Account;
  event.textInput = true;
  event.key = omacontacts::Key::Question;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::Ignore);
  event.key = omacontacts::Key::Escape;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::Cancel);
}

void ShortcutTests::deleteConfirmIgnoresEnter() {
  omacontacts::KeyEvent event;
  event.surface = omacontacts::Surface::ConfirmDelete;
  event.key = omacontacts::Key::Return;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::Ignore);
  event.key = omacontacts::Key::Escape;
  QCOMPARE(omacontacts::routeKey(event), omacontacts::Command::Cancel);
}

void ShortcutTests::helpListsEmail() {
  const auto rows = omacontacts::bindingHelp();
  QVERIFY(rows.size() >= 8);
  bool sawOpen = false;
  bool sawKeybindings = false;
  bool sawSync = false;
  bool sawBareLetter = false;
  for (const auto& row : rows) {
    QVERIFY(!row.keys.isEmpty());
    QVERIFY(!row.action.isEmpty());
    if (row.keys == QLatin1String("Enter") && row.action.contains(QStringLiteral("Open"))) sawOpen = true;
    if (row.action.contains(QStringLiteral("keybindings"))) sawKeybindings = true;
    if (row.action.contains(QStringLiteral("Sync")) || row.keys.contains(QStringLiteral("PgUp"))) sawSync = true;
    if (row.keys == QLatin1String("e") || row.keys == QLatin1String("n") || row.keys == QLatin1String("r") ||
        row.keys == QLatin1String("m")) {
      sawBareLetter = true;
    }
  }
  QVERIFY(sawOpen);
  QVERIFY(sawKeybindings);
  QVERIFY(!sawSync);
  QVERIFY(!sawBareLetter);
}
