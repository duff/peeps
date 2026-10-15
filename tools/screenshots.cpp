// Renders the README's screenshots from a made-up address book.
//
// Peeps runs against a throwaway home directory, so nothing here reads or
// writes a real address book, the keyring, or Google. Every contact, email,
// and phone number is fictional: emails use reserved example domains and
// phones use the 555-01xx range set aside for fiction. The photo is a
// generated face, not a real person.
//
//   peeps-screenshots <output dir> <photo.jpg>

#include "mainwindow.h"
#include "photos.h"
#include "store.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QPixmap>
#include <QScreen>
#include <QRandomGenerator>
#include <QSet>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

using namespace contacts;

namespace {

const char* const kFirst[] = {
    "Amara",  "Ben",    "Carmen", "Dmitri", "Elena",  "Farid",   "Greta",  "Hiro",   "Ines",   "Jonah",
    "Keiko",  "Liam",   "Marisol", "Nikhil", "Olga",  "Pablo",   "Quinn",  "Rosa",   "Sanjay", "Tamsin",
    "Umar",   "Vera",   "Wes",    "Ximena", "Yusuf",  "Zara",    "Aiden",  "Bea",    "Cyrus",  "Daphne",
    "Emeka",  "Freya",  "Gus",    "Hana",   "Isaac",  "Jade",    "Kofi",   "Lucia",  "Mateo",  "Nora",
    "Owen",   "Priya",  "Rafael", "Sofia",  "Theo",   "Uma",     "Victor", "Wren",   "Yara",   "Zoe",
    "Anders", "Chloe",  "Diego",  "Esme",   "Felix",  "Ingrid",  "Jamal",  "Leila",  "Malik",  "Nadia",
};

const char* const kLast[] = {
    "Abara",    "Bergstrom", "Castillo", "Dubois",   "Eriksen",  "Fontaine", "Gallagher", "Haddad",
    "Ivanova",  "Jensen",    "Kowalski", "Lindgren", "Moreau",   "Nakamura", "Okonkwo",   "Petrov",
    "Quintero", "Rinaldi",   "Sato",     "Torres",   "Ueda",     "Varga",    "Whitfield", "Xu",
    "Yilmaz",   "Zimmerman", "Achebe",   "Brennan",  "Costa",    "Delgado",  "Ellison",   "Ferreira",
    "Grant",    "Holm",      "Iyer",     "Joshi",    "Kaur",     "Larsen",   "Mendez",    "Novak",
    "Oduya",    "Park",      "Quill",    "Reyes",    "Sorensen", "Tanaka",   "Underwood", "Vance",
};

struct Company {
  const char* name;
  const char* domain;
  const char* titles[3];
};

const Company kCompanies[] = {
    {"Northwind Studio", "northwind.example", {"Product designer", "Illustrator", "Studio manager"}},
    {"Bluebird Bakery", "bluebird.example", {"Head baker", "Pastry chef", "Owner"}},
    {"Harbor Lane Clinic", "harborlane.example", {"Nurse", "Physiotherapist", "Family doctor"}},
    {"Cedar & Pine Architects", "cedarpine.example", {"Architect", "Partner", "Project lead"}},
    {"Lumen Robotics", "lumen.example", {"Engineer", "Data scientist", "Product manager"}},
    {"Fieldstone Schools", "fieldstone.example", {"Teacher", "Principal", "Counselor"}},
    {"Atlas Freight", "atlasfreight.example", {"Dispatcher", "Driver", "Operations lead"}},
    {"Orchard Credit Union", "orchardcu.example", {"Accountant", "Loan officer", "Branch manager"}},
    {"Kestrel Labs", "kestrel.example", {"Researcher", "Lab manager", "Founder"}},
    {"Riverbend Library", "riverbend.example", {"Librarian", "Archivist", "Children's librarian"}},
    {"Saltmarsh Brewing", "saltmarsh.example", {"Brewer", "Taproom manager", "Founder"}},
    {"Tidewater Legal", "tidewater.example", {"Counsel", "Paralegal", "Office manager"}},
};

const char* const kStreets[] = {"Maple Avenue", "Harbor Road", "Linden Street", "Quarry Lane", "Willow Court", "Birch Way"};
const char* const kCities[][3] = {
    {"Toronto", "ON", "Canada"},      {"Portland", "OR", "United States"}, {"Chicago", "IL", "United States"},
    {"Halifax", "NS", "Canada"},      {"Austin", "TX", "United States"},   {"Madison", "WI", "United States"},
};
const char* const kAreaCodes[] = {"312", "416", "503", "512", "608", "902"};

QString phone(int area, int line) {
  return QStringLiteral("+1 %1-555-01%2").arg(QLatin1String(kAreaCodes[area % 6])).arg(line % 100, 2, 10, QLatin1Char('0'));
}

QString handle(const QString& name) { return name.toLower().remove(QLatin1Char(' ')); }

// About two hundred people, the same every run, with the mix of detail a real
// address book has: some have a company, a second number, or an address.
QList<Contact> addressBook() {
  QRandomGenerator random(1815);
  const auto pick = [&random](const auto& list) { return list[random.bounded(int(std::size(list)))]; };
  QList<Contact> people;
  QSet<QString> names;
  while (people.size() < 210) {
    Contact c;
    c.givenName = QString::fromLatin1(pick(kFirst));
    c.familyName = QString::fromLatin1(pick(kLast));
    if (names.contains(c.displayName())) continue;
    names.insert(c.displayName());
    const int i = people.size();
    c.resourceName = QStringLiteral("people/c%1").arg(1000 + i);
    const QString user = handle(c.givenName) + QLatin1Char('.') + handle(c.familyName);
    if (random.bounded(3) != 0) {
      const Company& company = pick(kCompanies);
      c.organization = QString::fromLatin1(company.name);
      if (random.bounded(2) == 0) c.title = QString::fromLatin1(pick(company.titles));
      c.emails.append({user + QLatin1Char('@') + QLatin1String(company.domain), {}, QStringLiteral("work")});
    }
    if (c.emails.isEmpty() || random.bounded(4) == 0) {
      const char* const personal[] = {"example.com", "example.org", "example.net"};
      c.emails.append({user + QLatin1Char('@') + QLatin1String(pick(personal)), {}, QStringLiteral("home")});
    }
    c.phones.append({phone(random.bounded(6), random.bounded(100)), {}, QStringLiteral("mobile")});
    if (random.bounded(5) == 0) c.phones.append({phone(random.bounded(6), random.bounded(100)), {}, QStringLiteral("work")});
    if (random.bounded(4) == 0) {
      const auto& city = pick(kCities);
      PostalAddress address;
      address.type = QStringLiteral("home");
      address.street = QStringLiteral("%1 %2").arg(10 + random.bounded(990)).arg(QLatin1String(pick(kStreets)));
      address.city = QString::fromLatin1(city[0]);
      address.region = QString::fromLatin1(city[1]);
      address.country = QString::fromLatin1(city[2]);
      c.addresses.append(address);
    }
    if (random.bounded(6) == 0) c.birthday = {1960 + random.bounded(45), 1 + random.bounded(12), 1 + random.bounded(28), {}};
    people.append(c);
  }

  // The contact the second screenshot opens.
  Contact linus;
  linus.resourceName = QStringLiteral("people/c0001");
  linus.givenName = QStringLiteral("Linus");
  linus.familyName = QStringLiteral("Halvorsen");
  linus.organization = QStringLiteral("Lumen Robotics");
  linus.title = QStringLiteral("Systems engineer");
  linus.photoUrl = QStringLiteral("https://photos.example/linus.jpg");
  linus.emails = {{QStringLiteral("linus@lumen.example"), {}, QStringLiteral("work")},
                  {QStringLiteral("linus.halvorsen@example.com"), {}, QStringLiteral("home")}};
  linus.phones = {{QStringLiteral("+1 416-555-0142"), {}, QStringLiteral("mobile")},
                  {QStringLiteral("+1 416-555-0187"), {}, QStringLiteral("work")}};
  PostalAddress home;
  home.type = QStringLiteral("home");
  home.street = QStringLiteral("48 Linden Street");
  home.city = QStringLiteral("Toronto");
  home.region = QStringLiteral("ON");
  home.postalCode = QStringLiteral("M4M 1A1");
  home.country = QStringLiteral("Canada");
  linus.addresses = {home};
  linus.birthday = {1984, 9, 17, {}};
  linus.notes = QStringLiteral("Met at the Omarchy meetup. We both really enjoyed DHH's 2026 keynote at Rails World.");
  people.append(linus);
  return people;
}

void key(int k, const QString& text = {}, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QWidget* target = QApplication::focusWidget();
  QKeyEvent press(QEvent::KeyPress, k, modifiers, text);
  QApplication::sendEvent(target, &press);
  QKeyEvent release(QEvent::KeyRelease, k, modifiers, text);
  QApplication::sendEvent(target, &release);
}

void type(const QString& text) {
  for (QChar c : text) key(c.isLetter() ? Qt::Key_A + (c.toLower().unicode() - 'a') : Qt::Key_Space, QString(c));
}

// The list or the open contact's fields, whichever is showing.
QListView* visibleList(QWidget& window, bool fields) {
  for (auto* view : window.findChildren<QListView*>()) {
    if (!view->isVisible() || view->model()->rowCount() == 0) continue;
    if (view->model()->index(0, 0).data(Qt::UserRole + 2).isValid() == fields) return view;
  }
  return nullptr;
}

bool writeText(const QString& path, const QByteArray& text) {
  QDir().mkpath(QFileInfo(path).path());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: peeps-screenshots <output dir> <photo.jpg>\n");
    return 2;
  }
  const QString out = QDir(QString::fromLocal8Bit(argv[1])).absolutePath();
  const QString photoPath = QString::fromLocal8Bit(argv[2]);

  // A home of its own: Omarchy's theme is missing there, so Peeps paints Tokyo
  // Night, and the monospace font is Omarchy's default.
  QTemporaryDir home;
  if (!home.isValid()) return 1;
  const QString root = home.path();
  qputenv("HOME", root.toUtf8());
  qputenv("XDG_CONFIG_HOME", (root + QStringLiteral("/.config")).toUtf8());
  qputenv("XDG_DATA_HOME", (root + QStringLiteral("/.local/share")).toUtf8());
  qputenv("XDG_CACHE_HOME", (root + QStringLiteral("/.cache")).toUtf8());
  qputenv("PEEPS_NO_SYNC", "1");
  // No session bus, so the keyring is never asked for a Google sign-in.
  qputenv("DBUS_SESSION_BUS_ADDRESS", "disabled:");
  // Offscreen, so the window keeps its size instead of being tiled.
  qputenv("QT_QPA_PLATFORM", "offscreen");
  writeText(root + QStringLiteral("/.config/fontconfig/fonts.conf"),
            "<?xml version=\"1.0\"?>\n<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n<fontconfig>\n"
            "  <alias><family>monospace</family><prefer><family>JetBrainsMono Nerd Font</family></prefer></alias>\n"
            "</fontconfig>\n");

  QApplication app(argc, argv);
  app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

  const AppPaths paths = AppPaths::forEnv(root, qEnvironmentVariable("XDG_CONFIG_HOME"), qEnvironmentVariable("XDG_DATA_HOME"),
                                          qEnvironmentVariable("XDG_CACHE_HOME"));
  const QList<Contact> people = addressBook();
  QString error;
  QDir().mkpath(QFileInfo(paths.cacheFile).path());
  if (!writePrivateFile(paths.cacheFile, writeCacheJson(people, {}), error)) {
    fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
  }
  QFile photo(photoPath);
  if (!photo.open(QIODevice::ReadOnly)) {
    fprintf(stderr, "cannot read %s\n", qPrintable(photoPath));
    return 1;
  }
  PhotoStore(paths.photoDir).remember(people.last().resourceName, people.last().photoUrl, photo.readAll());

  QDir().mkpath(out);
  MainWindow window;
  window.show();
  window.activateWindow();
  window.resize(900, 700);
  // Signed out on purpose, so the footer would say so. A screenshot has no use for that.
  for (auto* label : window.findChildren<QLabel*>(QStringLiteral("status"))) {
    if (label->text() == QStringLiteral("Not signed in")) label->hide();
  }

  int failures = 0;
  const auto save = [&](const QString& name) {
    const QString path = out + QLatin1Char('/') + name;
    // Twice the window's size, so text stays sharp on a high-density screen.
    QPixmap picture(window.size() * 2);
    picture.setDevicePixelRatio(2);
    window.render(&picture);
    if (picture.save(path)) fprintf(stderr, "wrote %s\n", qPrintable(path));
    else ++failures;
  };

  // The whole book, then Linus open.
  QTimer::singleShot(500, &window, [&] {
    save(QStringLiteral("list.png"));
    type(QStringLiteral("linus halvorsen"));
    QTimer::singleShot(300, &window, [&] {
      key(Qt::Key_Return);
      // Select the work email, where both of its keys are named.
      if (QListView* fields = visibleList(window, true)) {
        for (int row = 0; row < fields->model()->rowCount(); ++row) {
          if (fields->model()->index(row, 0).data().toString().startsWith(QStringLiteral("linus@"))) {
            fields->setCurrentIndex(fields->model()->index(row, 0));
            fields->scrollToTop();
            break;
          }
        }
      } else {
        ++failures;
      }
      QTimer::singleShot(300, &window, [&] {
        save(QStringLiteral("contact.png"));
        app.exit(failures == 0 ? 0 : 1);
      });
    });
  });
  return app.exec();
}
