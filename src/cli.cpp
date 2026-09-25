#include "cli.h"

#include "blockingnam.h"
#include "oauth.h"
#include "search.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

namespace contacts {
namespace {

enum class Take { No, Yes, Error };

const char* kCommands[] = {"list", "search", "show", "add", "update", "delete", "sync", "help", "commands", "version"};

bool isCommand(const QString& text) {
  for (const char* command : kCommands) {
    if (text == QLatin1String(command)) return true;
  }
  return false;
}

bool isWriteCommand(const QString& command) {
  return command == QLatin1String("add") || command == QLatin1String("update") || command == QLatin1String("delete") ||
         command == QLatin1String("sync");
}

CliResult finish(int code, QString out = {}, QString err = {}) {
  if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n'))) out.append(QLatin1Char('\n'));
  if (!err.isEmpty() && !err.endsWith(QLatin1Char('\n'))) err.append(QLatin1Char('\n'));
  return CliResult{code, std::move(out), std::move(err)};
}

QString overview() {
  return QStringLiteral(
      "contacts — look up and change Google contacts\n"
      "\n"
      "List, search, and show read the local cache, so they stay fast.\n"
      "Add, update, delete, and sync talk to Google, then refresh that cache.\n"
      "\n"
      "  contacts list [--limit N]\n"
      "  contacts search QUERY [--limit N]\n"
      "  contacts show ID\n"
      "  contacts add FLAGS\n"
      "  contacts update ID FLAGS\n"
      "  contacts delete ID --yes\n"
      "  contacts sync [--full]\n"
      "  contacts commands\n"
      "  contacts help [COMMAND]\n"
      "\n"
      "ID is a resource name (people/c123), a unique name, or a unique email.\n"
      "A terminal prints text. A pipe prints JSON. --json and --text choose.\n"
      "\n"
      "  0  success\n"
      "  1  the command failed\n"
      "  2  usage\n"
      "  3  not signed in\n"
      "  4  no contact matched\n"
      "\n"
      "  contacts search lovelace\n"
      "  contacts show ada@example.com --json\n"
      "  contacts add --given Ada --family Lovelace --email home=ada@example.com\n"
      "  contacts update people/c1 --notes \"Met in 1843\"\n"
      "  contacts delete people/c1 --yes\n");
}

QString fieldHelp() {
  return QStringLiteral(
      "Fields you leave out stay as they are. Repeatable flags replace that\n"
      "whole list, and an empty value clears it. --data is applied first.\n"
      "\n"
      "  --given TEXT\n"
      "  --family TEXT\n"
      "  --middle TEXT\n"
      "  --prefix TEXT\n"
      "  --suffix TEXT\n"
      "  --nickname TEXT\n"
      "  --file-as TEXT\n"
      "  --phonetic-given TEXT\n"
      "  --phonetic-middle TEXT\n"
      "  --phonetic-family TEXT\n"
      "  --organization TEXT\n"
      "  --company TEXT\n"
      "  --title TEXT\n"
      "  --department TEXT\n"
      "  --occupation TEXT\n"
      "  --notes TEXT\n"
      "  --birthday YYYY-MM-DD\n"
      "  --email LABEL=ADDRESS       Address alone leaves the label blank\n"
      "  --phone LABEL=NUMBER\n"
      "  --url LABEL=URL\n"
      "  --relation LABEL=NAME\n"
      "  --sip LABEL=URI\n"
      "  --custom LABEL=VALUE\n"
      "  --address LABEL|STREET|CITY|REGION|POSTAL|COUNTRY\n"
      "  --data FILE                 JSON from show. FILE - reads stdin\n"
      "  --photo FILE\n"
      "  --remove-photo\n");
}

QString commandHelp(const QString& command) {
  if (command == QLatin1String("list")) {
    return QStringLiteral(
        "List contacts from the local cache, in name order.\n"
        "\n"
        "  contacts list [--limit N] [--json]\n"
        "\n"
        "JSON is {\"total\", \"count\", \"contacts\":[{\"id\", \"name\", \"email\",\n"
        "\"phone\", \"organization\"}]}.\n");
  }
  if (command == QLatin1String("search")) {
    return QStringLiteral(
        "Find contacts in the local cache. Every word must match.\n"
        "A name match ranks above email, phone, and notes.\n"
        "\n"
        "  contacts search QUERY [--limit N] [--json]\n");
  }
  if (command == QLatin1String("show")) {
    return QStringLiteral(
        "Print one contact from the local cache.\n"
        "\n"
        "  contacts show ID [--json]\n"
        "\n"
        "JSON is the object --data accepts, plus id, name, etag, and photo_url.\n");
  }
  if (command == QLatin1String("add")) {
    return QStringLiteral("Create a contact on Google, then store it in the local cache.\n"
                          "A name or a company is required.\n"
                          "\n"
                          "  contacts add FLAGS\n"
                          "\n") +
           fieldHelp();
  }
  if (command == QLatin1String("update")) {
    return QStringLiteral("Change a contact on Google, then store it in the local cache.\n"
                          "\n"
                          "  contacts update ID FLAGS\n"
                          "\n") +
           fieldHelp();
  }
  if (command == QLatin1String("delete")) {
    return QStringLiteral(
        "Delete a contact on Google, then remove it from the local cache.\n"
        "This is permanent. --yes is required.\n"
        "\n"
        "  contacts delete ID --yes\n");
  }
  if (command == QLatin1String("sync")) {
    return QStringLiteral(
        "Refresh the local cache from Google.\n"
        "\n"
        "  contacts sync [--full]\n"
        "\n"
        "Without --full, only changes since the last sync are downloaded.\n");
  }
  if (command == QLatin1String("commands")) {
    return QStringLiteral("List the commands.\n\n  contacts commands [--json]\n");
  }
  if (command == QLatin1String("version")) return QStringLiteral("Print the version.\n\n  contacts --version\n");
  return overview();
}

struct CommandRow {
  const char* name;
  const char* summary;
};

const CommandRow kCatalog[] = {
    {"list", "List contacts from the local cache"},
    {"search", "Find contacts in the local cache"},
    {"show", "Print one contact"},
    {"add", "Create a contact on Google"},
    {"update", "Change a contact on Google"},
    {"delete", "Delete a contact on Google"},
    {"sync", "Refresh the local cache from Google"},
};

QString catalogText(bool json) {
  if (!json) {
    QString out = QStringLiteral("Commands\n\n");
    for (const CommandRow& row : kCatalog) {
      out += QStringLiteral("  %1  %2\n").arg(QString::fromLatin1(row.name).leftJustified(8), QString::fromLatin1(row.summary));
    }
    return out;
  }
  QJsonArray array;
  for (const CommandRow& row : kCatalog) {
    QJsonObject object;
    object.insert(QStringLiteral("name"), QString::fromLatin1(row.name));
    object.insert(QStringLiteral("summary"), QString::fromLatin1(row.summary));
    array.append(object);
  }
  QJsonObject root;
  root.insert(QStringLiteral("commands"), array);
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

struct Patch {
  bool any = false;
  std::optional<QString> prefix;
  std::optional<QString> givenName;
  std::optional<QString> middleName;
  std::optional<QString> familyName;
  std::optional<QString> suffix;
  std::optional<QString> phoneticGiven;
  std::optional<QString> phoneticMiddle;
  std::optional<QString> phoneticFamily;
  std::optional<QString> nickname;
  std::optional<QString> fileAs;
  std::optional<QString> organization;
  std::optional<QString> title;
  std::optional<QString> department;
  std::optional<QString> occupation;
  std::optional<QString> notes;
  std::optional<DateValue> birthday;
  QStringList emails;
  QStringList phones;
  QStringList urls;
  QStringList relations;
  QStringList sips;
  QStringList customs;
  QStringList addresses;
  QList<PostalAddress> addressValues;
  bool emailsSet = false;
  bool phonesSet = false;
  bool urlsSet = false;
  bool relationsSet = false;
  bool sipsSet = false;
  bool customsSet = false;
  bool addressesSet = false;
  bool addressValuesSet = false;
  bool otherOrganizationsSet = false;
  bool eventsSet = false;
  bool imsSet = false;
  QList<Organization> otherOrganizations;
  QList<DatedEvent> events;
  QList<ImClient> ims;
  QString dataPath;
  QString photoPath;
  bool removePhoto = false;
  QString dataId;
};

struct Parsed {
  bool stop = false;
  CliResult early;
  QString command;
  QStringList positionals;
  bool help = false;
  bool json = false;
  bool text = false;
  bool yes = false;
  bool full = false;
  int limit = -1;
  Patch patch;
  bool needsAccount = false;
};

Take takeValue(const QStringList& args, int& index, const QString& flag, QString& value, QString& error) {
  const QString& arg = args.at(index);
  const QString prefix = flag + QLatin1Char('=');
  if (arg.startsWith(prefix)) {
    value = arg.mid(prefix.size());
    return Take::Yes;
  }
  if (arg != flag) return Take::No;
  if (index + 1 >= args.size() || args.at(index + 1).startsWith(QLatin1String("--"))) {
    error = flag + QStringLiteral(" needs a value.");
    return Take::Error;
  }
  value = args.at(++index);
  return Take::Yes;
}

QString dateText(const DateValue& date) {
  if (date.year > 0 && date.month > 0 && date.day > 0) {
    return QStringLiteral("%1-%2-%3")
        .arg(date.year, 4, 10, QLatin1Char('0'))
        .arg(date.month, 2, 10, QLatin1Char('0'))
        .arg(date.day, 2, 10, QLatin1Char('0'));
  }
  if (date.year <= 0 && date.month > 0 && date.day > 0) {
    return QStringLiteral("%1-%2").arg(date.month, 2, 10, QLatin1Char('0')).arg(date.day, 2, 10, QLatin1Char('0'));
  }
  return date.text;
}

std::optional<DateValue> parseDate(const QString& text, QString& error) {
  const QString trimmed = text.trimmed();
  DateValue date;
  if (trimmed.isEmpty()) return date;
  const QRegularExpression iso(QStringLiteral("^(\\d{4})-(\\d{1,2})-(\\d{1,2})$"));
  const QRegularExpression monthDay(QStringLiteral("^(\\d{1,2})-(\\d{1,2})$"));
  const auto full = iso.match(trimmed);
  const auto partial = monthDay.match(trimmed);
  int month = 0;
  int day = 0;
  if (full.hasMatch()) {
    date.year = full.captured(1).toInt();
    month = full.captured(2).toInt();
    day = full.captured(3).toInt();
  } else if (partial.hasMatch()) {
    month = partial.captured(1).toInt();
    day = partial.captured(2).toInt();
  } else {
    date.text = trimmed;
    return date;
  }
  if (month < 1 || month > 12 || day < 1 || day > 31) {
    error = QStringLiteral("Check the date \"%1\".").arg(trimmed);
    return std::nullopt;
  }
  date.month = month;
  date.day = day;
  return date;
}

LabeledValue parseLabeled(LabelField field, const QString& spec) {
  LabeledValue value;
  const int eq = spec.indexOf(QLatin1Char('='));
  QString label;
  if (eq >= 0) {
    label = spec.left(eq).trimmed();
    value.value = spec.mid(eq + 1).trimmed();
  } else {
    value.value = spec.trimmed();
  }
  if (!label.isEmpty()) {
    const ResolvedLabel resolved = resolveLabel(field, label);
    value.type = resolved.type;
    value.label = resolved.display.isEmpty() ? label : resolved.display;
  }
  return value;
}

QList<LabeledValue> labeledList(LabelField field, const QStringList& specs) {
  QList<LabeledValue> values;
  for (const QString& spec : specs) {
    if (spec.trimmed().isEmpty()) continue;
    const LabeledValue value = parseLabeled(field, spec);
    if (!value.value.isEmpty()) values.append(value);
  }
  return values;
}

std::optional<PostalAddress> parseAddress(const QString& spec, QString& error) {
  if (spec.trimmed().isEmpty()) return PostalAddress{};
  PostalAddress address;
  const QStringList parts = spec.split(QLatin1Char('|'));
  const auto at = [&](int index) { return index < parts.size() ? parts.at(index).trimmed() : QString(); };
  if (!spec.contains(QLatin1Char('|'))) {
    address.street = spec.trimmed();
  } else {
    address.label = at(0);
    address.street = at(1);
    address.city = at(2);
    address.region = at(3);
    address.postalCode = at(4);
    address.country = at(5);
    if (parts.size() > 6) {
      error = QStringLiteral("An address is LABEL|STREET|CITY|REGION|POSTAL|COUNTRY.");
      return std::nullopt;
    }
    if (!address.label.isEmpty()) {
      const ResolvedLabel resolved = resolveLabel(LabelField::Address, address.label);
      address.type = resolved.type;
      if (!resolved.display.isEmpty()) address.label = resolved.display;
    }
  }
  return address;
}

bool takeString(const QJsonObject& object, const char* key, std::optional<QString>& dest, QString& error) {
  if (!object.contains(QLatin1String(key))) return true;
  const QJsonValue value = object.value(QLatin1String(key));
  if (value.isNull()) {
    dest = QString();
    return true;
  }
  if (!value.isString()) {
    error = QStringLiteral("%1 must be a string.").arg(QLatin1String(key));
    return false;
  }
  dest = value.toString();
  return true;
}

QList<LabeledValue> labeledFromJson(const QJsonArray& array, LabelField field, QString& error, bool& ok) {
  QList<LabeledValue> values;
  for (const QJsonValue& value : array) {
    if (!value.isObject()) {
      error = QStringLiteral("Each entry needs value and label.");
      ok = false;
      return {};
    }
    const QJsonObject object = value.toObject();
    if (object.contains(QLatin1String("value")) && !object.value(QLatin1String("value")).isString()) {
      error = QStringLiteral("value must be a string.");
      ok = false;
      return {};
    }
    LabeledValue labeled;
    labeled.value = object.value(QLatin1String("value")).toString().trimmed();
    labeled.label = object.value(QLatin1String("label")).toString().trimmed();
    labeled.type = object.value(QLatin1String("type")).toString().trimmed();
    if (labeled.type.isEmpty() && !labeled.label.isEmpty()) {
      const ResolvedLabel resolved = resolveLabel(field, labeled.label);
      labeled.type = resolved.type;
      if (!resolved.display.isEmpty()) labeled.label = resolved.display;
    }
    if (!labeled.value.isEmpty()) values.append(labeled);
  }
  ok = true;
  return values;
}

bool applyJson(const QJsonObject& object, Patch& patch, QString& error) {
  if (object.contains(QLatin1String("id")) && object.value(QLatin1String("id")).isString()) {
    patch.dataId = object.value(QLatin1String("id")).toString();
  }
  const char* scalars[][2] = {
      {"prefix", "prefix"},
      {"given_name", "given"},
      {"middle_name", "middle"},
      {"family_name", "family"},
      {"suffix", "suffix"},
      {"phonetic_given", "phonetic_given"},
      {"phonetic_middle", "phonetic_middle"},
      {"phonetic_family", "phonetic_family"},
      {"nickname", "nickname"},
      {"file_as", "file_as"},
      {"title", "title"},
      {"department", "department"},
      {"occupation", "occupation"},
      {"notes", "notes"},
  };
  std::optional<QString>* dests[] = {&patch.prefix,     &patch.givenName,      &patch.middleName,    &patch.familyName,
                                     &patch.suffix,     &patch.phoneticGiven,  &patch.phoneticMiddle, &patch.phoneticFamily,
                                     &patch.nickname,   &patch.fileAs,         &patch.title,         &patch.department,
                                     &patch.occupation, &patch.notes};
  for (int i = 0; i < 14; ++i) {
    if (!takeString(object, scalars[i][0], *dests[i], error)) return false;
    if (dests[i]->has_value()) patch.any = true;
  }
  const bool hasOrganization = object.contains(QLatin1String("organization"));
  const bool hasCompany = object.contains(QLatin1String("company"));
  if (hasOrganization && hasCompany &&
      object.value(QLatin1String("organization")).toString() != object.value(QLatin1String("company")).toString()) {
    error = QStringLiteral("organization and company disagree.");
    return false;
  }
  if (hasOrganization) {
    if (!takeString(object, "organization", patch.organization, error)) return false;
  } else if (hasCompany) {
    if (!takeString(object, "company", patch.organization, error)) return false;
  }
  if (patch.organization.has_value()) patch.any = true;
  if (object.contains(QLatin1String("birthday"))) {
    const QJsonValue value = object.value(QLatin1String("birthday"));
    if (!value.isString() && !value.isNull()) {
      error = QStringLiteral("birthday must be a string.");
      return false;
    }
    const auto date = parseDate(value.toString(), error);
    if (!date) return false;
    patch.birthday = *date;
    patch.any = true;
  }

  auto takeList = [&](const char* key, LabelField field, QStringList& specs, bool& set) {
    if (!object.contains(QLatin1String(key))) return true;
    if (!object.value(QLatin1String(key)).isArray()) {
      error = QStringLiteral("%1 must be a list.").arg(QLatin1String(key));
      return false;
    }
    bool ok = false;
    const QList<LabeledValue> values = labeledFromJson(object.value(QLatin1String(key)).toArray(), field, error, ok);
    if (!ok) return false;
    specs.clear();
    for (const LabeledValue& value : values) {
      specs.append(value.label.isEmpty() ? value.value : value.label + QLatin1Char('=') + value.value);
    }
    set = true;
    patch.any = true;
    return true;
  };
  if (!takeList("emails", LabelField::Email, patch.emails, patch.emailsSet)) return false;
  if (!takeList("phones", LabelField::Phone, patch.phones, patch.phonesSet)) return false;
  if (!takeList("urls", LabelField::Other, patch.urls, patch.urlsSet)) return false;
  if (!takeList("relations", LabelField::Other, patch.relations, patch.relationsSet)) return false;
  if (!takeList("sips", LabelField::Other, patch.sips, patch.sipsSet)) return false;
  if (!takeList("customs", LabelField::Other, patch.customs, patch.customsSet)) return false;

  if (object.contains(QLatin1String("addresses"))) {
    if (!object.value(QLatin1String("addresses")).isArray()) {
      error = QStringLiteral("addresses must be a list.");
      return false;
    }
    patch.addressValues.clear();
    for (const QJsonValue& value : object.value(QLatin1String("addresses")).toArray()) {
      if (!value.isObject()) {
        error = QStringLiteral("Each address must be an object.");
        return false;
      }
      const QJsonObject item = value.toObject();
      PostalAddress address;
      address.label = item.value(QLatin1String("label")).toString().trimmed();
      address.type = item.value(QLatin1String("type")).toString().trimmed();
      address.street = item.value(QLatin1String("street")).toString();
      address.extended = item.value(QLatin1String("extended")).toString();
      address.poBox = item.value(QLatin1String("po_box")).toString();
      address.city = item.value(QLatin1String("city")).toString();
      address.region = item.value(QLatin1String("region")).toString();
      address.postalCode = item.value(QLatin1String("postal_code")).toString();
      address.country = item.value(QLatin1String("country")).toString();
      address.formatted = item.value(QLatin1String("formatted")).toString();
      if (address.type.isEmpty() && !address.label.isEmpty()) {
        const ResolvedLabel resolved = resolveLabel(LabelField::Address, address.label);
        address.type = resolved.type;
        if (!resolved.display.isEmpty()) address.label = resolved.display;
      }
      if (!address.isEmpty()) patch.addressValues.append(address);
    }
    patch.addressValuesSet = true;
    patch.any = true;
  }
  if (object.contains(QLatin1String("other_organizations"))) {
    if (!object.value(QLatin1String("other_organizations")).isArray()) {
      error = QStringLiteral("other_organizations must be a list.");
      return false;
    }
    patch.otherOrganizations.clear();
    for (const QJsonValue& value : object.value(QLatin1String("other_organizations")).toArray()) {
      if (!value.isObject()) {
        error = QStringLiteral("Each company must be an object.");
        return false;
      }
      const QJsonObject item = value.toObject();
      Organization organization;
      organization.name = item.value(QLatin1String("name")).toString();
      organization.title = item.value(QLatin1String("title")).toString();
      organization.department = item.value(QLatin1String("department")).toString();
      if (!organization.isEmpty()) patch.otherOrganizations.append(organization);
    }
    patch.otherOrganizationsSet = true;
    patch.any = true;
  }
  if (object.contains(QLatin1String("events"))) {
    if (!object.value(QLatin1String("events")).isArray()) {
      error = QStringLiteral("events must be a list.");
      return false;
    }
    patch.events.clear();
    for (const QJsonValue& value : object.value(QLatin1String("events")).toArray()) {
      if (!value.isObject()) {
        error = QStringLiteral("Each date must be an object.");
        return false;
      }
      const QJsonObject item = value.toObject();
      if (item.contains(QLatin1String("date")) && !item.value(QLatin1String("date")).isString() &&
          !item.value(QLatin1String("date")).isNull()) {
        error = QStringLiteral("date must be a string.");
        return false;
      }
      const auto date = parseDate(item.value(QLatin1String("date")).toString(), error);
      if (!date) return false;
      DatedEvent event;
      event.label = item.value(QLatin1String("label")).toString();
      event.type = item.value(QLatin1String("type")).toString();
      event.date = *date;
      if (!event.isEmpty()) patch.events.append(event);
    }
    patch.eventsSet = true;
    patch.any = true;
  }
  if (object.contains(QLatin1String("ims"))) {
    if (!object.value(QLatin1String("ims")).isArray()) {
      error = QStringLiteral("ims must be a list.");
      return false;
    }
    patch.ims.clear();
    for (const QJsonValue& value : object.value(QLatin1String("ims")).toArray()) {
      if (!value.isObject()) {
        error = QStringLiteral("Each instant message must be an object.");
        return false;
      }
      const QJsonObject item = value.toObject();
      ImClient im;
      im.username = item.value(QLatin1String("username")).toString();
      im.protocol = item.value(QLatin1String("protocol")).toString();
      im.label = item.value(QLatin1String("label")).toString();
      im.type = item.value(QLatin1String("type")).toString();
      if (!im.isEmpty()) patch.ims.append(im);
    }
    patch.imsSet = true;
    patch.any = true;
  }

  const QStringList known = {QStringLiteral("id"),
                             QStringLiteral("etag"),
                             QStringLiteral("name"),
                             QStringLiteral("photo_url"),
                             QStringLiteral("prefix"),
                             QStringLiteral("given_name"),
                             QStringLiteral("middle_name"),
                             QStringLiteral("family_name"),
                             QStringLiteral("suffix"),
                             QStringLiteral("phonetic_given"),
                             QStringLiteral("phonetic_middle"),
                             QStringLiteral("phonetic_family"),
                             QStringLiteral("nickname"),
                             QStringLiteral("file_as"),
                             QStringLiteral("organization"),
                             QStringLiteral("company"),
                             QStringLiteral("title"),
                             QStringLiteral("department"),
                             QStringLiteral("occupation"),
                             QStringLiteral("notes"),
                             QStringLiteral("birthday"),
                             QStringLiteral("emails"),
                             QStringLiteral("phones"),
                             QStringLiteral("addresses"),
                             QStringLiteral("urls"),
                             QStringLiteral("relations"),
                             QStringLiteral("events"),
                             QStringLiteral("ims"),
                             QStringLiteral("sips"),
                             QStringLiteral("customs"),
                             QStringLiteral("other_organizations")};
  for (const QString& key : object.keys()) {
    if (!known.contains(key)) {
      error = QStringLiteral("Unknown field \"%1\".").arg(key);
      return false;
    }
  }
  return true;
}

QJsonObject labeledJson(const LabeledValue& value) {
  QJsonObject object;
  object.insert(QStringLiteral("value"), value.value);
  object.insert(QStringLiteral("label"), value.label);
  if (!value.type.isEmpty()) object.insert(QStringLiteral("type"), value.type);
  return object;
}

QJsonArray labeledArray(const QList<LabeledValue>& values) {
  QJsonArray array;
  for (const LabeledValue& value : values) array.append(labeledJson(value));
  return array;
}

QJsonObject contactJson(const Contact& contact) {
  QJsonObject object;
  object.insert(QStringLiteral("id"), contact.resourceName);
  object.insert(QStringLiteral("etag"), contact.etag);
  object.insert(QStringLiteral("name"), contact.displayName());
  object.insert(QStringLiteral("prefix"), contact.prefix);
  object.insert(QStringLiteral("given_name"), contact.givenName);
  object.insert(QStringLiteral("middle_name"), contact.middleName);
  object.insert(QStringLiteral("family_name"), contact.familyName);
  object.insert(QStringLiteral("suffix"), contact.suffix);
  object.insert(QStringLiteral("phonetic_given"), contact.phoneticGiven);
  object.insert(QStringLiteral("phonetic_middle"), contact.phoneticMiddle);
  object.insert(QStringLiteral("phonetic_family"), contact.phoneticFamily);
  object.insert(QStringLiteral("nickname"), contact.nickname);
  object.insert(QStringLiteral("file_as"), contact.fileAs);
  object.insert(QStringLiteral("organization"), contact.organization);
  object.insert(QStringLiteral("title"), contact.title);
  object.insert(QStringLiteral("department"), contact.department);
  object.insert(QStringLiteral("occupation"), contact.occupation);
  QJsonArray organizations;
  for (const Organization& organization : contact.otherOrganizations) {
    QJsonObject item;
    item.insert(QStringLiteral("name"), organization.name);
    item.insert(QStringLiteral("title"), organization.title);
    item.insert(QStringLiteral("department"), organization.department);
    organizations.append(item);
  }
  object.insert(QStringLiteral("other_organizations"), organizations);
  object.insert(QStringLiteral("notes"), contact.notes);
  object.insert(QStringLiteral("birthday"), dateText(contact.birthday));
  object.insert(QStringLiteral("photo_url"), contact.photoUrl);
  object.insert(QStringLiteral("emails"), labeledArray(contact.emails));
  object.insert(QStringLiteral("phones"), labeledArray(contact.phones));
  QJsonArray addresses;
  for (const PostalAddress& address : contact.addresses) {
    QJsonObject item;
    item.insert(QStringLiteral("label"), address.label);
    if (!address.type.isEmpty()) item.insert(QStringLiteral("type"), address.type);
    item.insert(QStringLiteral("street"), address.street);
    item.insert(QStringLiteral("extended"), address.extended);
    item.insert(QStringLiteral("po_box"), address.poBox);
    item.insert(QStringLiteral("city"), address.city);
    item.insert(QStringLiteral("region"), address.region);
    item.insert(QStringLiteral("postal_code"), address.postalCode);
    item.insert(QStringLiteral("country"), address.country);
    if (!address.formatted.isEmpty()) item.insert(QStringLiteral("formatted"), address.formatted);
    addresses.append(item);
  }
  object.insert(QStringLiteral("addresses"), addresses);
  object.insert(QStringLiteral("urls"), labeledArray(contact.urls));
  object.insert(QStringLiteral("relations"), labeledArray(contact.relations));
  QJsonArray events;
  for (const DatedEvent& event : contact.events) {
    QJsonObject item;
    item.insert(QStringLiteral("label"), event.label);
    if (!event.type.isEmpty()) item.insert(QStringLiteral("type"), event.type);
    item.insert(QStringLiteral("date"), dateText(event.date));
    events.append(item);
  }
  object.insert(QStringLiteral("events"), events);
  QJsonArray ims;
  for (const ImClient& im : contact.ims) {
    QJsonObject item;
    item.insert(QStringLiteral("username"), im.username);
    item.insert(QStringLiteral("protocol"), im.protocol);
    item.insert(QStringLiteral("label"), im.label);
    if (!im.type.isEmpty()) item.insert(QStringLiteral("type"), im.type);
    ims.append(item);
  }
  object.insert(QStringLiteral("ims"), ims);
  object.insert(QStringLiteral("sips"), labeledArray(contact.sips));
  object.insert(QStringLiteral("customs"), labeledArray(contact.customs));
  return object;
}

QString jsonText(const QJsonObject& object) { return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)); }

void appendField(QStringList& lines, const QString& label, const QString& value) {
  if (value.trimmed().isEmpty()) return;
  const QStringList parts = value.split(QLatin1Char('\n'));
  lines.append(label.leftJustified(16) + parts.first());
  for (int i = 1; i < parts.size(); ++i) lines.append(QString(16, QLatin1Char(' ')) + parts.at(i));
}

QString contactText(const Contact& contact) {
  QStringList lines;
  lines.append(contact.displayName());
  if (!contact.resourceName.isEmpty()) lines.append(contact.resourceName);
  lines.append(QString());
  appendField(lines, QStringLiteral("Prefix"), contact.prefix);
  appendField(lines, QStringLiteral("First name"), contact.givenName);
  appendField(lines, QStringLiteral("Middle name"), contact.middleName);
  appendField(lines, QStringLiteral("Last name"), contact.familyName);
  appendField(lines, QStringLiteral("Suffix"), contact.suffix);
  appendField(lines, QStringLiteral("Nickname"), contact.nickname);
  appendField(lines, QStringLiteral("File as"), contact.fileAs);
  appendField(lines, QStringLiteral("Phonetic given"), contact.phoneticGiven);
  appendField(lines, QStringLiteral("Phonetic middle"), contact.phoneticMiddle);
  appendField(lines, QStringLiteral("Phonetic family"), contact.phoneticFamily);
  appendField(lines, QStringLiteral("Company"), contact.organization);
  appendField(lines, QStringLiteral("Job title"), contact.title);
  appendField(lines, QStringLiteral("Department"), contact.department);
  appendField(lines, QStringLiteral("Occupation"), contact.occupation);
  for (const Organization& organization : contact.otherOrganizations) {
    QStringList bits;
    if (!organization.name.isEmpty()) bits.append(organization.name);
    if (!organization.title.isEmpty()) bits.append(organization.title);
    if (!organization.department.isEmpty()) bits.append(organization.department);
    appendField(lines, QStringLiteral("Company"), bits.join(QStringLiteral(" · ")));
  }
  for (const LabeledValue& email : contact.emails) {
    appendField(lines, QStringLiteral("Email"), email.label.isEmpty() ? email.value : email.value + QStringLiteral("  ") + email.label);
  }
  for (const LabeledValue& phone : contact.phones) {
    appendField(lines, QStringLiteral("Phone"), phone.label.isEmpty() ? phone.value : phone.value + QStringLiteral("  ") + phone.label);
  }
  for (const PostalAddress& address : contact.addresses) {
    QString shown = address.display();
    if (!address.label.isEmpty()) shown = address.label + QStringLiteral("\n") + shown;
    appendField(lines, QStringLiteral("Address"), shown);
  }
  appendField(lines, QStringLiteral("Birthday"), contact.birthday.display());
  for (const LabeledValue& url : contact.urls) {
    appendField(lines, QStringLiteral("Website"), url.label.isEmpty() ? url.value : url.value + QStringLiteral("  ") + url.label);
  }
  for (const LabeledValue& relation : contact.relations) {
    appendField(lines, QStringLiteral("Related"),
                relation.label.isEmpty() ? relation.value : relation.value + QStringLiteral("  ") + relation.label);
  }
  for (const DatedEvent& event : contact.events) {
    const QString label = event.label.isEmpty() ? QStringLiteral("Date") : event.label;
    appendField(lines, label == QLatin1String("Date") ? label : QStringLiteral("Date"),
                label == QLatin1String("Date") ? event.date.display() : event.date.display() + QStringLiteral("  ") + label);
  }
  for (const ImClient& im : contact.ims) appendField(lines, QStringLiteral("Message"), im.display());
  for (const LabeledValue& sip : contact.sips) appendField(lines, QStringLiteral("SIP"), sip.value);
  for (const LabeledValue& custom : contact.customs) {
    appendField(lines, custom.label.isEmpty() ? QStringLiteral("Custom") : custom.label, custom.value);
  }
  appendField(lines, QStringLiteral("Notes"), contact.notes);
  while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
  return lines.join(QLatin1Char('\n'));
}

QString renderContact(const Contact& contact, bool json) { return json ? jsonText(contactJson(contact)) : contactText(contact); }

QString listTrail(const Contact& contact) {
  const QString email = contact.primaryEmail();
  const QString phone = contact.primaryPhone();
  const QString company = contact.organization;
  if (!email.isEmpty() && !company.isEmpty()) return email + QStringLiteral("  ·  ") + company;
  if (!email.isEmpty()) return email;
  if (!phone.isEmpty() && !company.isEmpty()) return phone + QStringLiteral("  ·  ") + company;
  if (!phone.isEmpty()) return phone;
  return company;
}

QString renderHits(const QList<SearchHit>& hits, int limit, bool json, const QString& query) {
  const int total = static_cast<int>(hits.size());
  const int shown = limit < 0 ? total : std::min(limit, total);
  if (json) {
    QJsonArray array;
    for (int i = 0; i < shown; ++i) {
      const Contact& contact = *hits.at(i).contact;
      QJsonObject object;
      object.insert(QStringLiteral("id"), contact.resourceName);
      object.insert(QStringLiteral("name"), contact.displayName());
      object.insert(QStringLiteral("email"), contact.primaryEmail());
      object.insert(QStringLiteral("phone"), contact.primaryPhone());
      object.insert(QStringLiteral("organization"), contact.organization);
      if (!query.isEmpty()) object.insert(QStringLiteral("score"), hits.at(i).score);
      array.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("total"), total);
    root.insert(QStringLiteral("count"), shown);
    if (!query.isEmpty()) root.insert(QStringLiteral("query"), query);
    root.insert(QStringLiteral("contacts"), array);
    return jsonText(root);
  }
  if (total == 0) return query.isEmpty() ? QStringLiteral("No contacts.") : QStringLiteral("No matches.");
  const QString noun = total == 1 ? QStringLiteral("contact") : QStringLiteral("contacts");
  QString header = shown == total ? QStringLiteral("%1 %2").arg(total).arg(noun)
                                  : QStringLiteral("%1 of %2 %3").arg(shown).arg(total).arg(noun);
  int width = 0;
  for (int i = 0; i < shown; ++i) width = std::max(width, static_cast<int>(hits.at(i).contact->displayName().size()));
  width = std::min(width, 32);
  QStringList lines;
  lines.append(header);
  lines.append(QString());
  for (int i = 0; i < shown; ++i) {
    const Contact& contact = *hits.at(i).contact;
    const QString name = contact.displayName();
    const QString trail = listTrail(contact);
    QString line = name.size() >= width ? name : name.leftJustified(width);
    if (!trail.isEmpty()) line += QStringLiteral("  ") + trail;
    lines.append(line);
  }
  return lines.join(QLatin1Char('\n'));
}

struct Found {
  Contact contact;
  QString error;
  int code = kCliOk;
};

Found resolveContact(const QList<Contact>& contacts, const QString& query) {
  const QString needle = query.trimmed();
  Found found;
  if (needle.isEmpty()) {
    found.code = kCliUsage;
    found.error = QStringLiteral("Pass a contact id, name, or email.");
    return found;
  }
  QList<const Contact*> matches;
  const auto consider = [&](const Contact& contact, bool hit) {
    if (hit) matches.append(&contact);
  };
  for (const Contact& contact : contacts) consider(contact, contact.resourceName == needle);
  if (matches.size() == 1) {
    found.contact = *matches.first();
    return found;
  }
  matches.clear();
  const QString prefixed = needle.startsWith(QLatin1String("people/")) ? needle : QStringLiteral("people/") + needle;
  if (prefixed != needle) {
    for (const Contact& contact : contacts) consider(contact, contact.resourceName == prefixed);
    if (matches.size() == 1) {
      found.contact = *matches.first();
      return found;
    }
    if (matches.size() > 1) {
      found.code = kCliFailed;
      found.error = QStringLiteral("%1 matches %2 contacts. Pass an id.").arg(needle).arg(matches.size());
      return found;
    }
    matches.clear();
  }
  for (const Contact& contact : contacts) {
    bool email = false;
    for (const LabeledValue& value : contact.emails) {
      if (value.value.compare(needle, Qt::CaseInsensitive) == 0) email = true;
    }
    consider(contact, email);
  }
  if (matches.size() == 1) {
    found.contact = *matches.first();
    return found;
  }
  if (matches.size() > 1) {
    found.code = kCliFailed;
    found.error = QStringLiteral("%1 matches %2 contacts. Pass an id.").arg(needle).arg(matches.size());
    return found;
  }
  matches.clear();
  for (const Contact& contact : contacts) consider(contact, contact.displayName().compare(needle, Qt::CaseInsensitive) == 0);
  if (matches.size() == 1) {
    found.contact = *matches.first();
    return found;
  }
  if (matches.size() > 1) {
    found.code = kCliFailed;
    QStringList lines;
    lines.append(QStringLiteral("%1 matches %2 contacts. Pass an id.").arg(needle).arg(matches.size()));
    const int count = std::min(5, static_cast<int>(matches.size()));
    for (int i = 0; i < count; ++i) {
      lines.append(QStringLiteral("  %1  %2").arg(matches.at(i)->resourceName, matches.at(i)->displayName()));
    }
    found.error = lines.join(QLatin1Char('\n'));
    return found;
  }
  found.code = kCliNotFound;
  found.error = QStringLiteral("No contact matches \"%1\".").arg(needle);
  return found;
}

std::optional<QByteArray> readInput(const CliSession& session, const QString& path, QString& error) {
  if (path == QLatin1String("-")) {
    if (session.stdinBytes) return *session.stdinBytes;
    QFile in;
    if (!in.open(stdin, QIODevice::ReadOnly)) {
      error = QStringLiteral("Could not read stdin.");
      return std::nullopt;
    }
    return in.readAll();
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = QStringLiteral("Could not read %1.").arg(path);
    return std::nullopt;
  }
  return file.readAll();
}

QByteArray jpegBytes(const QString& path, QString& error) {
  QImage image;
  if (!image.load(path) || image.isNull()) {
    error = QStringLiteral("Could not read that image.");
    return {};
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  if (!image.save(&buffer, "JPEG", 90) || bytes.isEmpty()) {
    error = QStringLiteral("Could not read that image.");
    return {};
  }
  return bytes;
}

Contact editedContact(const Contact& base, const Patch& patch, QString& error) {
  Draft draft = Draft::fromContact(base);
  auto set = [](QString& dest, const std::optional<QString>& value) {
    if (value) dest = *value;
  };
  set(draft.prefix, patch.prefix);
  set(draft.givenName, patch.givenName);
  set(draft.middleName, patch.middleName);
  set(draft.familyName, patch.familyName);
  set(draft.suffix, patch.suffix);
  set(draft.phoneticGiven, patch.phoneticGiven);
  set(draft.phoneticMiddle, patch.phoneticMiddle);
  set(draft.phoneticFamily, patch.phoneticFamily);
  set(draft.nickname, patch.nickname);
  set(draft.fileAs, patch.fileAs);
  set(draft.organization, patch.organization);
  set(draft.title, patch.title);
  set(draft.department, patch.department);
  set(draft.occupation, patch.occupation);
  set(draft.notes, patch.notes);
  if (patch.birthday) draft.birthday = *patch.birthday;
  if (patch.emailsSet) draft.emails = labeledList(LabelField::Email, patch.emails);
  if (patch.phonesSet) draft.phones = labeledList(LabelField::Phone, patch.phones);
  if (patch.urlsSet) draft.urls = labeledList(LabelField::Other, patch.urls);
  if (patch.relationsSet) draft.relations = labeledList(LabelField::Other, patch.relations);
  if (patch.sipsSet) draft.sips = labeledList(LabelField::Other, patch.sips);
  if (patch.customsSet) {
    draft.customs.clear();
    for (const QString& spec : patch.customs) {
      if (spec.trimmed().isEmpty()) continue;
      const int eq = spec.indexOf(QLatin1Char('='));
      if (eq <= 0) {
        error = QStringLiteral("Write custom fields as Label=value.");
        return {};
      }
      LabeledValue custom;
      custom.label = spec.left(eq).trimmed();
      custom.value = spec.mid(eq + 1).trimmed();
      if (!custom.value.isEmpty() || !custom.label.isEmpty()) draft.customs.append(custom);
    }
  }
  if (patch.addressesSet) {
    draft.addresses.clear();
    for (const QString& spec : patch.addresses) {
      const auto address = parseAddress(spec, error);
      if (!address) return {};
      if (!address->isEmpty()) draft.addresses.append(*address);
    }
  } else if (patch.addressValuesSet) {
    draft.addresses = patch.addressValues;
  }
  if (patch.otherOrganizationsSet) draft.otherOrganizations = patch.otherOrganizations;
  if (patch.eventsSet) draft.events = patch.events;
  if (patch.imsSet) draft.ims = patch.ims;
  if (!error.isEmpty()) return {};
  return draft.toContact(base);
}

bool sameFields(const Contact& left, const Contact& right) {
  Contact a = left;
  Contact b = right;
  a.resourceName = b.resourceName;
  a.etag = b.etag;
  a.photoUrl = b.photoUrl;
  return a == b;
}

void remember(CliSession& session, const Contact& contact) {
  for (Contact& existing : session.contacts) {
    if (existing.resourceName == contact.resourceName) {
      existing = contact;
      return;
    }
  }
  session.contacts.append(contact);
}

bool persist(CliSession& session, QString& error) {
  if (!session.save) return true;
  return session.save(session.contacts, session.syncToken, error);
}

CliResult signedOut(const CliSession& session) {
  if (!session.authError.isEmpty()) return finish(kCliFailed, {}, session.authError);
  return finish(kCliSignedOut, {}, QStringLiteral("Not signed in. Open Contacts and connect a Google account."));
}

QString cacheMiss(const QString& detail) { return QStringLiteral("Google saved the change, but the local cache could not be written. %1 Run contacts sync.").arg(detail); }

Contact mergedWrite(const Contact& submitted, const Contact& saved) {
  Contact merged = submitted;
  if (!saved.resourceName.isEmpty()) merged.resourceName = saved.resourceName;
  if (!saved.etag.isEmpty()) merged.etag = saved.etag;
  if (merged.photoUrl.isEmpty() && !saved.photoUrl.isEmpty()) merged.photoUrl = saved.photoUrl;
  return merged;
}

void overlayFlags(Patch& base, const Patch& flags) {
  auto take = [](std::optional<QString>& dest, const std::optional<QString>& src) {
    if (src) dest = *src;
  };
  take(base.prefix, flags.prefix);
  take(base.givenName, flags.givenName);
  take(base.middleName, flags.middleName);
  take(base.familyName, flags.familyName);
  take(base.suffix, flags.suffix);
  take(base.phoneticGiven, flags.phoneticGiven);
  take(base.phoneticMiddle, flags.phoneticMiddle);
  take(base.phoneticFamily, flags.phoneticFamily);
  take(base.nickname, flags.nickname);
  take(base.fileAs, flags.fileAs);
  take(base.organization, flags.organization);
  take(base.title, flags.title);
  take(base.department, flags.department);
  take(base.occupation, flags.occupation);
  take(base.notes, flags.notes);
  if (flags.birthday) base.birthday = flags.birthday;
  if (flags.emailsSet) {
    base.emails = flags.emails;
    base.emailsSet = true;
  }
  if (flags.phonesSet) {
    base.phones = flags.phones;
    base.phonesSet = true;
  }
  if (flags.urlsSet) {
    base.urls = flags.urls;
    base.urlsSet = true;
  }
  if (flags.relationsSet) {
    base.relations = flags.relations;
    base.relationsSet = true;
  }
  if (flags.sipsSet) {
    base.sips = flags.sips;
    base.sipsSet = true;
  }
  if (flags.customsSet) {
    base.customs = flags.customs;
    base.customsSet = true;
  }
  if (flags.addressesSet) {
    base.addresses = flags.addresses;
    base.addressesSet = true;
    base.addressValuesSet = false;
  }
  if (flags.otherOrganizationsSet) {
    base.otherOrganizations = flags.otherOrganizations;
    base.otherOrganizationsSet = true;
  }
  if (flags.eventsSet) {
    base.events = flags.events;
    base.eventsSet = true;
  }
  if (flags.imsSet) {
    base.ims = flags.ims;
    base.imsSet = true;
  }
  if (!flags.photoPath.isEmpty()) base.photoPath = flags.photoPath;
  if (flags.removePhoto) base.removePhoto = true;
  base.dataPath = flags.dataPath;
  if (flags.any) base.any = true;
}

CliResult loadData(const CliSession& session, Patch& patch) {
  if (patch.dataPath.isEmpty()) return finish(kCliOk);
  QString error;
  const auto bytes = readInput(session, patch.dataPath, error);
  if (!bytes) return finish(kCliUsage, {}, error);
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(*bytes, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    const QString message = parseError.error == QJsonParseError::NoError ? QStringLiteral("Contact JSON must be an object.")
                                                                        : QStringLiteral("Could not read that JSON. %1").arg(parseError.errorString());
    return finish(kCliUsage, {}, message);
  }
  if (!applyJson(document.object(), patch, error)) return finish(kCliUsage, {}, error);
  return finish(kCliOk);
}

Parsed parseArgs(const QStringList& args) {
  Parsed parsed;
  for (const QString& arg : args) {
    if (arg == QLatin1String("--version")) {
      parsed.stop = true;
      parsed.early = finish(kCliOk, QStringLiteral("contacts 0.1.0"));
      return parsed;
    }
  }
  QString error;
  bool failed = false;
  for (int i = 0; i < args.size(); ++i) {
    const QString arg = args.at(i);
    if (arg == QLatin1String("--help") || arg == QLatin1String("-h")) {
      parsed.help = true;
      continue;
    }
    if (arg == QLatin1String("--json")) {
      parsed.json = true;
      continue;
    }
    if (arg == QLatin1String("--text")) {
      parsed.text = true;
      continue;
    }
    if (arg == QLatin1String("--yes")) {
      parsed.yes = true;
      continue;
    }
    if (arg == QLatin1String("--full")) {
      parsed.full = true;
      continue;
    }
    if (arg == QLatin1String("--remove-photo")) {
      parsed.patch.removePhoto = true;
      parsed.patch.any = true;
      continue;
    }
    if (arg.startsWith(QLatin1Char('-')) && !arg.startsWith(QLatin1String("--"))) {
      parsed.stop = true;
      parsed.early = finish(kCliUsage, {}, QStringLiteral("Unknown flag %1.").arg(arg));
      return parsed;
    }
    if (!arg.startsWith(QLatin1String("--"))) {
      if (parsed.command.isEmpty()) parsed.command = arg;
      else parsed.positionals.append(arg);
      continue;
    }
    QString value;
    auto match = [&](const char* name) {
      const Take taken = takeValue(args, i, QLatin1String(name), value, error);
      if (taken == Take::Error) failed = true;
      return taken == Take::Yes;
    };
    auto scalar = [&](const char* name, std::optional<QString>& dest) {
      if (!match(name)) return false;
      dest = value;
      parsed.patch.any = true;
      return true;
    };
    if (failed) break;
    if (scalar("--given", parsed.patch.givenName) || failed) continue;
    if (scalar("--family", parsed.patch.familyName) || failed) continue;
    if (scalar("--middle", parsed.patch.middleName) || failed) continue;
    if (scalar("--prefix", parsed.patch.prefix) || failed) continue;
    if (scalar("--suffix", parsed.patch.suffix) || failed) continue;
    if (scalar("--nickname", parsed.patch.nickname) || failed) continue;
    if (scalar("--file-as", parsed.patch.fileAs) || failed) continue;
    if (scalar("--phonetic-given", parsed.patch.phoneticGiven) || failed) continue;
    if (scalar("--phonetic-middle", parsed.patch.phoneticMiddle) || failed) continue;
    if (scalar("--phonetic-family", parsed.patch.phoneticFamily) || failed) continue;
    if (scalar("--organization", parsed.patch.organization) || failed) continue;
    if (scalar("--company", parsed.patch.organization) || failed) continue;
    if (scalar("--title", parsed.patch.title) || failed) continue;
    if (scalar("--department", parsed.patch.department) || failed) continue;
    if (scalar("--occupation", parsed.patch.occupation) || failed) continue;
    if (scalar("--notes", parsed.patch.notes) || failed) continue;
    if (match("--birthday") || failed) {
      if (!failed) {
        const auto date = parseDate(value, error);
        if (!date) failed = true;
        else {
          parsed.patch.birthday = *date;
          parsed.patch.any = true;
        }
      }
      continue;
    }
    auto repeat = [&](const char* name, QStringList& specs, bool& set) {
      if (!match(name)) return false;
      specs.append(value);
      set = true;
      parsed.patch.any = true;
      return true;
    };
    if (repeat("--email", parsed.patch.emails, parsed.patch.emailsSet) || failed) continue;
    if (repeat("--phone", parsed.patch.phones, parsed.patch.phonesSet) || failed) continue;
    if (repeat("--url", parsed.patch.urls, parsed.patch.urlsSet) || failed) continue;
    if (repeat("--relation", parsed.patch.relations, parsed.patch.relationsSet) || failed) continue;
    if (repeat("--sip", parsed.patch.sips, parsed.patch.sipsSet) || failed) continue;
    if (repeat("--custom", parsed.patch.customs, parsed.patch.customsSet) || failed) continue;
    if (repeat("--address", parsed.patch.addresses, parsed.patch.addressesSet) || failed) continue;
    if (match("--data") || failed) {
      if (!failed) parsed.patch.dataPath = value;
      continue;
    }
    if (match("--photo") || failed) {
      if (!failed) {
        parsed.patch.photoPath = value;
        parsed.patch.any = true;
      }
      continue;
    }
    if (match("--limit") || failed) {
      if (!failed) {
        bool ok = false;
        const int limit = value.toInt(&ok);
        if (!ok || limit < 1) {
          error = QStringLiteral("--limit needs a positive number.");
          failed = true;
        } else {
          parsed.limit = limit;
        }
      }
      continue;
    }
    if (arg.startsWith(QLatin1String("--yes=")) || arg.startsWith(QLatin1String("--full=")) ||
        arg.startsWith(QLatin1String("--json=")) || arg.startsWith(QLatin1String("--text=")) ||
        arg.startsWith(QLatin1String("--help=")) || arg.startsWith(QLatin1String("--remove-photo="))) {
      error = arg.left(arg.indexOf(QLatin1Char('='))) + QStringLiteral(" does not take a value.");
      failed = true;
      break;
    }
    error = QStringLiteral("Unknown flag %1.").arg(arg);
    failed = true;
    break;
  }
  if (failed || !error.isEmpty()) {
    parsed.stop = true;
    parsed.early = finish(kCliUsage, {}, error.isEmpty() ? QStringLiteral("Could not read those flags.") : error);
    return parsed;
  }
  if (parsed.json && parsed.text) {
    parsed.stop = true;
    parsed.early = finish(kCliUsage, {}, QStringLiteral("Pass --json or --text, not both."));
    return parsed;
  }
  if (parsed.command == QLatin1String("version")) {
    parsed.stop = true;
    parsed.early = finish(kCliOk, QStringLiteral("contacts 0.1.0"));
    return parsed;
  }
  if (!parsed.command.isEmpty() && !isCommand(parsed.command)) {
    parsed.stop = true;
    parsed.early = finish(kCliUsage, {}, QStringLiteral("Unknown command \"%1\". Run contacts help.").arg(parsed.command));
    return parsed;
  }
  if (parsed.command.isEmpty() && parsed.help) {
    parsed.stop = true;
    parsed.early = finish(kCliOk, overview());
    return parsed;
  }
  if (parsed.command.isEmpty()) {
    parsed.stop = true;
    parsed.early = finish(kCliUsage, {}, QStringLiteral("Pass a command. Run contacts help."));
    return parsed;
  }
  if (parsed.help || parsed.command == QLatin1String("help")) {
    const QString topic = parsed.command == QLatin1String("help") ? (parsed.positionals.isEmpty() ? QString() : parsed.positionals.first())
                                                                  : parsed.command;
    if (parsed.command == QLatin1String("help") && parsed.positionals.size() > 1) {
      parsed.stop = true;
      parsed.early = finish(kCliUsage, {}, QStringLiteral("help takes one command."));
      return parsed;
    }
    if (!topic.isEmpty() && !isCommand(topic)) {
      parsed.stop = true;
      parsed.early = finish(kCliUsage, {}, QStringLiteral("Unknown command \"%1\". Run contacts help.").arg(topic));
      return parsed;
    }
    parsed.stop = true;
    parsed.early = finish(kCliOk, topic.isEmpty() ? overview() : commandHelp(topic));
    return parsed;
  }
  parsed.needsAccount = isWriteCommand(parsed.command);
  return parsed;
}

QString oneId(const Parsed& parsed, QString& error) {
  if (parsed.positionals.size() != 1) {
    error = QStringLiteral("%1 takes one contact.").arg(parsed.command);
    return {};
  }
  return parsed.positionals.first();
}

CliResult execute(Parsed parsed, CliSession& session, bool stdoutIsTty) {
  const bool json = parsed.json || (!parsed.text && !stdoutIsTty);
  const QString command = parsed.command;
  auto usage = [](const QString& message) { return finish(kCliUsage, {}, message); };

  if (parsed.yes && command != QLatin1String("delete")) return usage(QStringLiteral("--yes is only for delete."));
  if (parsed.full && command != QLatin1String("sync")) return usage(QStringLiteral("--full is only for sync."));
  if (parsed.limit >= 0 && command != QLatin1String("list") && command != QLatin1String("search")) {
    return usage(QStringLiteral("--limit is only for list and search."));
  }
  if ((!parsed.patch.dataPath.isEmpty() || parsed.patch.removePhoto || !parsed.patch.photoPath.isEmpty() || parsed.patch.any) &&
      command != QLatin1String("add") && command != QLatin1String("update")) {
    return usage(QStringLiteral("Those flags belong on add or update."));
  }
  if (parsed.patch.removePhoto && !parsed.patch.photoPath.isEmpty()) {
    return usage(QStringLiteral("Pass a photo or --remove-photo, not both."));
  }
  if (command == QLatin1String("add") && parsed.patch.removePhoto) {
    return usage(QStringLiteral("--remove-photo is only for update."));
  }

  if (command == QLatin1String("commands")) {
    if (!parsed.positionals.isEmpty()) return usage(QStringLiteral("commands takes no arguments."));
    return finish(kCliOk, catalogText(json));
  }

  ContactIndex index;
  auto hitsFor = [&](const QString& query) {
    index.replaceAll(session.contacts);
    return index.query(query);
  };

  if (command == QLatin1String("list")) {
    if (!parsed.positionals.isEmpty()) return usage(QStringLiteral("list takes no query. Use contacts search."));
    return finish(kCliOk, renderHits(hitsFor(QString()), parsed.limit, json, QString()));
  }
  if (command == QLatin1String("search")) {
    if (parsed.positionals.isEmpty()) return usage(QStringLiteral("search needs a query."));
    const QString query = parsed.positionals.join(QLatin1Char(' '));
    return finish(kCliOk, renderHits(hitsFor(query), parsed.limit, json, query));
  }
  if (command == QLatin1String("show")) {
    QString error;
    const QString id = oneId(parsed, error);
    if (id.isEmpty()) return usage(error);
    const Found found = resolveContact(session.contacts, id);
    if (found.code != kCliOk) return finish(found.code, {}, found.error);
    return finish(kCliOk, renderContact(found.contact, json));
  }

  if (command == QLatin1String("sync")) {
    if (!parsed.positionals.isEmpty()) return usage(QStringLiteral("sync takes no arguments."));
    if (!session.signedIn) return signedOut(session);
    if (!session.syncPeople) return finish(kCliFailed, {}, QStringLiteral("Contacts has no Google connection."));
    bool full = parsed.full || session.syncToken.isEmpty();
    PeopleResult fetched = session.syncPeople(full);
    if (fetched.error == QLatin1String("sync-token-expired") && !full) {
      full = true;
      fetched = session.syncPeople(true);
    }
    if (!fetched.ok()) return finish(kCliFailed, {}, fetched.error);
    session.contacts = full ? applyFullSync(fetched.people) : applyIncrementalSync(session.contacts, fetched.people);
    if (!fetched.syncToken.isEmpty()) session.syncToken = fetched.syncToken;
    QString error;
    if (!persist(session, error)) return finish(kCliFailed, {}, error);
    if (json) {
      QJsonObject object;
      object.insert(QStringLiteral("count"), session.contacts.size());
      object.insert(QStringLiteral("full"), full);
      return finish(kCliOk, jsonText(object));
    }
    const QString noun = session.contacts.size() == 1 ? QStringLiteral("contact") : QStringLiteral("contacts");
    return finish(kCliOk, QStringLiteral("Synced %1 %2.").arg(session.contacts.size()).arg(noun));
  }

  if (command == QLatin1String("delete")) {
    QString error;
    const QString id = oneId(parsed, error);
    if (id.isEmpty()) return usage(error);
    const Found found = resolveContact(session.contacts, id);
    if (found.code != kCliOk) return finish(found.code, {}, found.error);
    if (!parsed.yes) {
      return usage(QStringLiteral("Refusing to delete %1 (%2) without --yes.")
                       .arg(found.contact.displayName(), found.contact.resourceName));
    }
    if (!session.signedIn) return signedOut(session);
    if (!session.remove) return finish(kCliFailed, {}, QStringLiteral("Contacts has no Google connection."));
    const QString removed = session.remove(found.contact.resourceName);
    if (!removed.isEmpty()) return finish(kCliFailed, {}, removed);
    session.contacts.erase(std::remove_if(session.contacts.begin(), session.contacts.end(),
                                          [&](const Contact& contact) { return contact.resourceName == found.contact.resourceName; }),
                           session.contacts.end());
    if (!persist(session, error)) return finish(kCliFailed, {}, cacheMiss(error));
    if (json) {
      QJsonObject object;
      object.insert(QStringLiteral("deleted"), found.contact.resourceName);
      object.insert(QStringLiteral("name"), found.contact.displayName());
      return finish(kCliOk, jsonText(object));
    }
    return finish(kCliOk, QStringLiteral("Deleted %1.").arg(found.contact.displayName()));
  }

  Contact base;
  if (command == QLatin1String("update")) {
    QString error;
    const QString id = oneId(parsed, error);
    if (id.isEmpty()) return usage(error);
    const Found found = resolveContact(session.contacts, id);
    if (found.code != kCliOk) return finish(found.code, {}, found.error);
    base = found.contact;
  } else if (command == QLatin1String("add")) {
    if (!parsed.positionals.isEmpty()) return usage(QStringLiteral("add takes flags, not a name. Use --given and --family."));
  } else {
    return usage(QStringLiteral("Unknown command \"%1\". Run contacts help.").arg(command));
  }

  if (!parsed.patch.dataPath.isEmpty()) {
    const Patch flags = parsed.patch;
    Patch fromJson;
    fromJson.dataPath = flags.dataPath;
    const CliResult data = loadData(session, fromJson);
    if (data.code != kCliOk) return data;
    overlayFlags(fromJson, flags);
    parsed.patch = fromJson;
  }
  if (command == QLatin1String("update") && !parsed.patch.dataId.isEmpty() && parsed.patch.dataId != base.resourceName) {
    return usage(QStringLiteral("The JSON id is %1, not %2.").arg(parsed.patch.dataId, base.resourceName));
  }
  if (!parsed.patch.dataPath.isEmpty()) parsed.patch.any = true;
  if (!parsed.patch.any && parsed.patch.photoPath.isEmpty() && !parsed.patch.removePhoto) {
    return usage(QStringLiteral("Nothing to change."));
  }

  QString error;
  Contact edited = editedContact(base, parsed.patch, error);
  if (!error.isEmpty()) return usage(error);
  const QString invalid = Draft::fromContact(edited).validate();
  if (!invalid.isEmpty()) return usage(invalid);
  const Contact normalized = Draft::fromContact(base).toContact(base);
  const bool photoChange = !parsed.patch.photoPath.isEmpty() || (parsed.patch.removePhoto && !base.photoUrl.isEmpty());
  if (command == QLatin1String("update") && sameFields(edited, normalized) && !photoChange) {
    return usage(QStringLiteral("Nothing to change."));
  }
  if (!session.signedIn) return signedOut(session);

  QByteArray photo;
  if (!parsed.patch.photoPath.isEmpty()) {
    photo = jpegBytes(parsed.patch.photoPath, error);
    if (photo.isEmpty()) return usage(error);
  }

  Contact saved = edited;
  if (command == QLatin1String("add") || !sameFields(edited, normalized)) {
    if (command == QLatin1String("add")) {
      if (!session.create) return finish(kCliFailed, {}, QStringLiteral("Contacts has no Google connection."));
      const WriteResult result = session.create(edited);
      if (!result.ok()) return finish(kCliFailed, {}, result.error);
      saved = mergedWrite(edited, result.contact);
    } else {
      if (!session.update) return finish(kCliFailed, {}, QStringLiteral("Contacts has no Google connection."));
      const WriteResult result = session.update(edited);
      if (!result.ok()) return finish(kCliFailed, {}, result.error);
      saved = mergedWrite(edited, result.contact);
    }
  }
  QString photoError;
  if (!photo.isEmpty() || parsed.patch.removePhoto) {
    if (!session.photo) return finish(kCliFailed, {}, QStringLiteral("Contacts has no Google connection."));
    const WriteResult pictured = session.photo(saved.resourceName, photo, parsed.patch.removePhoto);
    if (!pictured.ok()) photoError = pictured.error;
    else {
      if (parsed.patch.removePhoto) saved.photoUrl.clear();
      else if (!pictured.contact.photoUrl.isEmpty()) saved.photoUrl = pictured.contact.photoUrl;
      if (!pictured.contact.etag.isEmpty()) saved.etag = pictured.contact.etag;
    }
  }
  remember(session, saved);
  if (!persist(session, error)) return finish(kCliFailed, {}, cacheMiss(error));
  const QString card = renderContact(saved, json);
  if (!photoError.isEmpty()) {
    return finish(kCliFailed, card, QStringLiteral("Saved %1, but the photo failed. %2").arg(saved.resourceName, photoError));
  }
  if (json) return finish(kCliOk, card);
  const QString verb = command == QLatin1String("add") ? QStringLiteral("Added") : QStringLiteral("Updated");
  return finish(kCliOk, verb + QLatin1Char(' ') + saved.displayName() + QStringLiteral(".\n\n") + card);
}

struct GoogleSession {
  HttpClient* http = nullptr;
  OAuthClient client;
  OAuthTokens tokens;
  SecretStore* secrets = nullptr;
  bool dirty = false;

  QString prepare() {
    const QString before = tokens.accessToken;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const TokenResult ensured = ensureAccess(*http, client, tokens, now);
    if (!ensured.ok()) return ensured.error;
    if (tokens.accessToken != before) dirty = true;
    return {};
  }

  template <typename Fn>
  auto call(Fn&& fn) {
    QString access = tokens.accessToken;
    auto refresh = [&]() -> QString {
      const TokenResult refreshed = refreshAccess(*http, client, tokens, QDateTime::currentSecsSinceEpoch());
      if (!refreshed.ok()) return refreshed.error;
      tokens = refreshed.tokens;
      access = tokens.accessToken;
      dirty = true;
      return {};
    };
    auto result = fn(access, refresh);
    tokens.accessToken = access;
    return result;
  }

  void saveSecrets() {
    if (!dirty || secrets == nullptr) return;
    QString error;
    secrets->save(QString::fromUtf8(writeTokensJson(tokens)), error);
  }
};

}  // namespace

bool isCliInvocation(const QStringList& args) {
  if (args.isEmpty()) return false;
  const QString& first = args.first();
  if (first == QLatin1String("--self-test")) return false;
  if (first.startsWith(QLatin1Char('-')) && !first.startsWith(QLatin1String("--")) && first != QLatin1String("-h")) return false;
  return true;
}

CliResult runCli(const QStringList& args, CliSession& session, bool stdoutIsTty) {
  const Parsed parsed = parseArgs(args);
  if (parsed.stop) return parsed.early;
  return execute(parsed, session, stdoutIsTty);
}

CliResult runContactsCommand(const QStringList& args, const CliLaunch& launch) {
  const Parsed parsed = parseArgs(args);
  if (parsed.stop) return parsed.early;

  migrateLegacyPaths(launch.paths);
  CliSession session;
  session.stdinBytes = launch.stdinBytes;
  if (const auto bytes = readFile(launch.paths.cacheFile)) {
    const CacheSnapshot snapshot = readCacheJson(*bytes);
    if (!snapshot.ok()) return finish(kCliFailed, {}, snapshot.error);
    session.contacts = snapshot.contacts;
    session.syncToken = snapshot.syncToken;
  }
  session.save = [&](const QList<Contact>& contacts, const QString& syncToken, QString& error) {
    return writePrivateFile(launch.paths.cacheFile, writeCacheJson(contacts, syncToken), error);
  };

  std::unique_ptr<SecretStore> ownedSecrets;
  std::unique_ptr<BlockingNam> ownedHttp;
  SecretStore* secrets = launch.secrets;
  HttpClient* http = launch.http;
  GoogleSession google;
  if (parsed.needsAccount) {
    if (secrets == nullptr) {
      ownedSecrets = makeSecretStore();
      secrets = ownedSecrets.get();
    }
    if (http == nullptr) {
      ownedHttp = std::make_unique<BlockingNam>();
      http = ownedHttp.get();
    }
    AppConfig config;
    if (const auto bytes = readFile(launch.paths.configFile)) {
      if (const auto read = readConfigJson(*bytes)) config = *read;
    }
    QString secretError;
    const auto secret = secrets->load(secretError);
    OAuthTokens tokens;
    if (secret) {
      if (const auto read = readTokensJson(secret->toUtf8())) tokens = *read;
    }
    if (!secretError.isEmpty() && !secret) {
      session.authError = secretError;
    } else if (config.clientId.isEmpty() || tokens.refreshToken.isEmpty()) {
      session.signedIn = false;
    } else {
      google.http = http;
      google.client = OAuthClient{config.clientId, config.clientSecret};
      google.tokens = tokens;
      google.secrets = secrets;
      const QString ready = google.prepare();
      if (!ready.isEmpty()) {
        session.signedIn = true;
        session.authError = ready;
        session.signedIn = false;
      } else {
        session.signedIn = true;
        session.create = [&](const Contact& contact) {
          return google.call([&](QString& access, const RefreshFn& refresh) { return createContact(*http, access, contact, refresh); });
        };
        session.update = [&](const Contact& contact) {
          return google.call([&](QString& access, const RefreshFn& refresh) { return updateContact(*http, access, contact, refresh); });
        };
        session.remove = [&](const QString& id) {
          return google.call([&](QString& access, const RefreshFn& refresh) { return deleteContact(*http, access, id, refresh); });
        };
        session.photo = [&](const QString& id, const QByteArray& jpeg, bool removePhoto) {
          return google.call([&](QString& access, const RefreshFn& refresh) {
            return removePhoto ? deleteContactPhoto(*http, access, id, refresh) : updateContactPhoto(*http, access, id, jpeg, refresh);
          });
        };
        session.syncPeople = [&](bool full) {
          return google.call([&](QString& access, const RefreshFn& refresh) {
            return fetchConnections(*http, access, full ? QString() : session.syncToken, refresh);
          });
        };
      }
    }
  }

  const CliResult result = execute(parsed, session, launch.stdoutIsTty);
  google.saveSecrets();
  return result;
}

}  // namespace contacts
