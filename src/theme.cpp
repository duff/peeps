#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontInfo>
#include <QHash>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace contacts {
namespace {

int hexValue(QChar c) {
  const char latin = c.toLatin1();
  if (latin >= '0' && latin <= '9') return latin - '0';
  if (latin >= 'a' && latin <= 'f') return latin - 'a' + 10;
  if (latin >= 'A' && latin <= 'F') return latin - 'A' + 10;
  return -1;
}

std::optional<QColor> parseHex(const QString& text) {
  if (text.size() != 7 || text.at(0) != QLatin1Char('#')) return std::nullopt;
  int channels[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    const int hi = hexValue(text.at(1 + i * 2));
    const int lo = hexValue(text.at(2 + i * 2));
    if (hi < 0 || lo < 0) return std::nullopt;
    channels[i] = hi * 16 + lo;
  }
  return QColor(channels[0], channels[1], channels[2]);
}

double channel(double value) {
  const double u = value / 255.0;
  if (u <= 0.04045) return u / 12.92;
  return std::pow((u + 0.055) / 1.055, 2.4);
}

double luminance(const QColor& color) {
  return 0.2126 * channel(color.red()) + 0.7152 * channel(color.green()) + 0.0722 * channel(color.blue());
}

double contrastRatio(const QColor& a, const QColor& b) {
  double lighter = luminance(a);
  double darker = luminance(b);
  if (lighter < darker) std::swap(lighter, darker);
  return (lighter + 0.05) / (darker + 0.05);
}

QColor mixToward(const QColor& from, const QColor& to, double amount) {
  const double t = std::clamp(amount, 0.0, 1.0);
  auto mix = [t](int a, int b) { return static_cast<int>(std::lround(a + (b - a) * t)); };
  return QColor(mix(from.red(), to.red()), mix(from.green(), to.green()), mix(from.blue(), to.blue()));
}

QColor bestOnColor(const QColor& background) {
  const QColor black(0, 0, 0);
  const QColor white(255, 255, 255);
  return contrastRatio(black, background) >= contrastRatio(white, background) ? black : white;
}

QColor ensureTextContrast(const QColor& ink, const QColor& canvas) {
  if (contrastRatio(ink, canvas) >= 4.5) return ink;
  const QColor black(0, 0, 0);
  for (int step = 1; step <= 20; ++step) {
    const QColor darkened = mixToward(ink, black, step / 20.0);
    if (contrastRatio(darkened, canvas) >= 4.5) return darkened;
  }
  return black;
}

QString readText(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QString::fromUtf8(file.readAll());
}

bool fileExists(const QString& path) { return QFile::exists(path); }

std::optional<QColor> requiredColor(const QHash<QString, QString>& values, const QString& key) {
  const auto it = values.constFind(key);
  if (it == values.constEnd()) return std::nullopt;
  return parseHex(it.value());
}

std::optional<QColor> firstColor(const QHash<QString, QString>& values, std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    const auto it = values.constFind(QLatin1String(key));
    if (it == values.constEnd()) continue;
    return parseHex(it.value());
  }
  return std::nullopt;
}

}  // namespace

QString rgba(const QColor& color, double alpha) {
  return QStringLiteral("rgba(%1,%2,%3,%4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(alpha, 0, 'f', 3);
}

bool Palette::sameColors(const Palette& other) const {
  auto hex = [](const QColor& color) { return color.name(QColor::HexRgb); };
  return appearance == other.appearance && hex(background) == hex(other.background) &&
         hex(foreground) == hex(other.foreground) && hex(accent) == hex(other.accent) &&
         hex(surface) == hex(other.surface) && hex(inset) == hex(other.inset) && hex(bright) == hex(other.bright) &&
         hex(secondary) == hex(other.secondary) && hex(selection) == hex(other.selection) &&
         hex(border) == hex(other.border) && hex(onAccent) == hex(other.onAccent) && hex(danger) == hex(other.danger) &&
         hex(warning) == hex(other.warning) && hex(success) == hex(other.success);
}

ThemePaths ThemePaths::forHome(const QString& home) {
  ThemePaths paths;
  paths.stateDir = home + QStringLiteral("/.local/state/omarchy/current");
  paths.stateColors = paths.stateDir + QStringLiteral("/theme/colors.toml");
  paths.stateName = paths.stateDir + QStringLiteral("/theme.name");
  paths.stateShell = paths.stateDir + QStringLiteral("/theme/shell.toml");
  paths.legacyDir = home + QStringLiteral("/.config/omarchy/current");
  paths.legacyColors = paths.legacyDir + QStringLiteral("/theme/colors.toml");
  paths.legacyName = paths.legacyDir + QStringLiteral("/theme.name");
  paths.legacyShell = paths.legacyDir + QStringLiteral("/theme/shell.toml");
  paths.userShell = home + QStringLiteral("/.config/omarchy/shell.toml");
  return paths;
}

int ShellMetrics::text(int px) const { return std::max(1, static_cast<int>(std::lround(px * basePx / 12.0))); }

int ShellMetrics::space(int px) const { return std::max(0, static_cast<int>(std::lround(px * spacingScale))); }

std::optional<QString> tomlValue(const QString& text, const QString& section, const QString& key) {
  QString current;
  for (QString line : text.split(QLatin1Char('\n'))) {
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
    if (line.startsWith(QLatin1Char('['))) {
      const int close = line.indexOf(QLatin1Char(']'));
      current = close < 0 ? QString() : line.mid(1, close - 1).trimmed();
      continue;
    }
    if (current != section) continue;
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq < 0 || line.left(eq).trimmed() != key) continue;
    QString value = line.mid(eq + 1).trimmed();
    if (value.startsWith(QLatin1Char('"'))) {
      const int close = value.indexOf(QLatin1Char('"'), 1);
      return close < 0 ? value.mid(1) : value.mid(1, close - 1);
    }
    const int comment = value.indexOf(QLatin1Char('#'));
    if (comment >= 0) value.truncate(comment);
    return value.trimmed();
  }
  return std::nullopt;
}

ShellMetrics parseShellMetrics(const QString& themeToml, const QString& userToml, int textSizeOffset) {
  const auto value = [&](const char* section, const char* key) {
    if (auto user = tomlValue(userToml, QLatin1String(section), QLatin1String(key))) return user;
    return tomlValue(themeToml, QLatin1String(section), QLatin1String(key));
  };
  const auto number = [&](const char* section, const char* key, double fallback) {
    bool ok = false;
    const double parsed = value(section, key).value_or(QString()).toDouble(&ok);
    return ok && parsed > 0 ? parsed : fallback;
  };
  ShellMetrics metrics;
  // The shell floors base-size at 1px. Below 9px the window stops being usable.
  const int omarchy = static_cast<int>(std::lround(number("font", "base-size", 12)));
  metrics.basePx = std::clamp(omarchy + std::clamp(textSizeOffset, kMinTextSizeOffset, kMaxTextSizeOffset), 9, 32);
  const bool followFont = value("spacing", "scale-with-font").value_or(QStringLiteral("true")) != QLatin1String("false");
  metrics.spacingScale = std::clamp(number("spacing", "scale", 1.0), 0.5, 3.0) * (followFont ? metrics.basePx / 12.0 : 1.0);
  return metrics;
}

ShellMetrics loadShellMetrics(const ThemePaths& paths, int textSizeOffset) {
  const QString theme = QDir(paths.stateDir).exists() ? paths.stateShell : paths.legacyShell;
  return parseShellMetrics(readText(theme), readText(paths.userShell), textSizeOffset);
}

namespace {
ShellMetrics& metricsSlot() {
  static ShellMetrics metrics;
  return metrics;
}
}  // namespace

const ShellMetrics& activeMetrics() { return metricsSlot(); }

void setActiveMetrics(const ShellMetrics& metrics) { metricsSlot() = metrics; }

QString monospaceFamily() {
  const QString family = QFontInfo(QFont(QStringLiteral("monospace"))).family();
  return family.isEmpty() ? QStringLiteral("monospace") : family;
}

Palette tokyoNight() {
  auto color = [](const char* hex) { return *parseHex(QLatin1String(hex)); };
  Palette palette;
  palette.name = QStringLiteral("Tokyo Night");
  palette.appearance = Appearance::Dark;
  palette.background = color("#1a1b26");
  palette.foreground = color("#a9b1d6");
  palette.accent = color("#7aa2f7");
  palette.surface = color("#24283b");
  palette.inset = color("#13141c");
  palette.bright = color("#c0caf5");
  palette.secondary = mixToward(palette.background, palette.foreground, 0.75);
  palette.selection = color("#292e42");
  palette.border = color("#414868");
  palette.onAccent = bestOnColor(palette.accent);
  palette.danger = color("#f7768e");
  palette.warning = color("#e0af68");
  palette.success = color("#9ece6a");
  return palette;
}

std::optional<Palette> parseColorsToml(const QString& text, const QString& name) {
  QHash<QString, QString> values;
  QString mode;
  const QStringList lines = text.split(QLatin1Char('\n'));
  for (QString line : lines) {
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
    const int comment = line.indexOf(QStringLiteral(" #"));
    if (comment >= 0) line.truncate(comment);
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq < 0) return std::nullopt;
    const QString key = line.left(eq).trimmed();
    QString value = line.mid(eq + 1).trimmed();
    if (value.size() >= 2 && value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"')) {
      value = value.mid(1, value.size() - 2);
    }
    if (key.isEmpty()) return std::nullopt;
    if (key == QLatin1String("mode")) {
      mode = value;
      continue;
    }
    if (!parseHex(value)) return std::nullopt;
    values.insert(key, value);
  }

  const auto background = requiredColor(values, QStringLiteral("background"));
  const auto foreground = requiredColor(values, QStringLiteral("foreground"));
  const auto accent = requiredColor(values, QStringLiteral("accent"));
  const auto danger = firstColor(values, {"red", "color1"});
  const auto success = firstColor(values, {"green", "color2"});
  const auto warning = firstColor(values, {"yellow", "color3"});
  if (!background || !foreground || !accent || !danger || !success || !warning) return std::nullopt;

  Palette palette;
  palette.name = name.trimmed().isEmpty() ? QStringLiteral("Omarchy") : name.trimmed();
  palette.background = *background;
  palette.foreground = *foreground;
  palette.accent = *accent;
  palette.danger = *danger;
  palette.success = *success;
  palette.warning = *warning;

  if (mode == QLatin1String("light")) {
    palette.appearance = Appearance::Light;
  } else if (mode == QLatin1String("dark")) {
    palette.appearance = Appearance::Dark;
  } else if (!mode.isEmpty()) {
    return std::nullopt;
  } else {
    palette.appearance =
        luminance(palette.background) > luminance(palette.foreground) ? Appearance::Light : Appearance::Dark;
  }

  if (auto surface = firstColor(values, {"lighter_background"})) palette.surface = *surface;
  else palette.surface = mixToward(palette.background, palette.foreground, 0.05);
  if (auto inset = firstColor(values, {"dark_background"})) palette.inset = *inset;
  else palette.inset = mixToward(palette.background, palette.foreground, 0.08);
  if (auto bright = firstColor(values, {"bright_foreground", "selection_foreground", "cursor"})) palette.bright = *bright;
  else palette.bright = palette.foreground;
  palette.secondary = mixToward(palette.background, palette.foreground, 0.75);
  if (auto selection = firstColor(values, {"selection", "selection_background"})) palette.selection = *selection;
  else palette.selection = mixToward(palette.background, palette.accent, 0.20);
  if (auto border = firstColor(values, {"muted", "color8"})) palette.border = *border;
  else palette.border = mixToward(palette.background, palette.foreground, 0.25);
  palette.onAccent = bestOnColor(palette.accent);
  if (palette.appearance == Appearance::Light) {
    palette.danger = ensureTextContrast(palette.danger, palette.background);
    palette.warning = ensureTextContrast(palette.warning, palette.background);
    palette.success = ensureTextContrast(palette.success, palette.background);
  }
  return palette;
}

ThemeLoad loadTheme(const ThemePaths& paths) {
  auto fromFiles = [](const QString& colorsPath, const QString& namePath, ThemeOrigin origin) -> std::optional<ThemeLoad> {
    if (!fileExists(colorsPath)) return std::nullopt;
    QString name = QStringLiteral("Omarchy");
    const QString rawName = readText(namePath).trimmed();
    if (!rawName.isEmpty()) name = rawName;
    auto palette = parseColorsToml(readText(colorsPath), name);
    if (!palette) return std::nullopt;
    ThemeLoad load;
    load.palette = *palette;
    load.origin = origin;
    load.path = colorsPath;
    return load;
  };

  // The state directory wins even when its theme file is broken. A stale
  // legacy palette must not come back after an upgrade.
  if (QDir(paths.stateDir).exists()) {
    if (auto load = fromFiles(paths.stateColors, paths.stateName, ThemeOrigin::State)) return *load;
    ThemeLoad fallback;
    fallback.palette = tokyoNight();
    fallback.origin = ThemeOrigin::Builtin;
    return fallback;
  }
  if (auto load = fromFiles(paths.legacyColors, paths.legacyName, ThemeOrigin::Legacy)) return *load;
  ThemeLoad fallback;
  fallback.palette = tokyoNight();
  fallback.origin = ThemeOrigin::Builtin;
  return fallback;
}

QString stylesheetFor(const Palette& palette, const ShellMetrics& m, const QString& family) {
  const auto a = [&](double alpha) { return rgba(palette.foreground, alpha); };
  const auto px = [](int value) { return QString::number(value) + QStringLiteral("px"); };
  QString css = QStringLiteral(R"(
QWidget { background: @@BG@@; color: @@FG@@; font-family: "@@FAMILY@@", monospace; font-size: @@BODY@@; }
QMainWindow, QDialog { background: @@BG@@; color: @@FG@@; }
QLabel { background: transparent; color: @@FG@@; font-size: @@BODY@@; }
QLabel#title { color: @@BRIGHT@@; font-size: @@HEADING@@; font-weight: 700; }
QLabel#section { color: @@BRIGHT@@; font-weight: 700; }
QLabel#secondary, QLabel#status, QLabel#hint { color: @@SECONDARY@@; }
QLabel#caption { color: @@SECONDARY@@; font-size: @@SMALL@@; }
QLabel#danger { color: @@DANGER@@; }
QLabel#accent { color: @@ACCENT@@; }
QLabel#photo { background: @@INSET@@; color: @@SECONDARY@@; border: 1px solid @@EDGE@@; font-size: @@HEADING@@; font-weight: 700; }
QPushButton { background: transparent; color: @@FG@@; border: 1px solid transparent; border-radius: 0; padding: @@BTNY@@ @@BTNX@@; font-size: @@BODY@@; font-weight: 400; }
QPushButton[kind="outline"] { border-color: @@EDGE@@; }
QPushButton[kind="primary"] { color: @@ACCENT@@; border-color: @@ACCENT@@; }
QPushButton[kind="danger"] { color: @@DANGER@@; border-color: @@DANGER@@; }
QPushButton[kind="link"] { color: @@ACCENT@@; padding: @@LINKY@@ @@LINKX@@; text-align: left; }
QPushButton[kind="icon"] { color: @@SECONDARY@@; padding: @@BTNY@@ @@ICONX@@; }
QPushButton[kind="icon"]:hover, QPushButton[kind="icon"]:focus { color: @@DANGER@@; }
QPushButton:hover { background: @@HOVER@@; }
QPushButton:focus { border-color: @@FOCUS@@; }
QPushButton[kind="primary"]:focus { border-color: @@ACCENT@@; background: @@HOVER@@; }
QPushButton:pressed { background: @@PRESSED@@; }
QPushButton:disabled { color: @@DISABLED@@; }
QLineEdit, QTextEdit, QPlainTextEdit, QComboBox { background: transparent; color: @@FG@@; border: 1px solid @@EDGE@@; border-radius: 0; padding: @@INPUTY@@ @@INPUTX@@; selection-background-color: @@SELECTION@@; selection-color: @@FG@@; font-size: @@BODY@@; }
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus { border: 1px solid @@ACCENT@@; }
QComboBox::drop-down { border: none; width: @@DROPDOWN@@; }
QComboBox QAbstractItemView { background: @@BG@@; color: @@FG@@; border: 1px solid @@EDGE@@; selection-background-color: @@SELECTION@@; selection-color: @@FG@@; outline: none; }
QMenu { background: @@BG@@; color: @@FG@@; border: 1px solid @@EDGE@@; padding: @@MENUPAD@@ 0; }
QMenu::item { padding: @@MENUY@@ @@MENUX@@; background: transparent; }
QMenu::item:selected { background: @@HOVER@@; color: @@ACCENT@@; }
QMenu::item:disabled { color: @@DISABLED@@; }
QMenu::separator { height: 1px; background: @@DIVIDER@@; margin: @@MENUPAD@@ @@MENUSEP@@; }
QTextEdit#notes, QPlainTextEdit#notes { background: @@INSET@@; }
QListView { background: transparent; border: none; outline: none; }
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: @@SCROLLTRACK@@; width: @@SCROLLW@@; margin: 0; border: none; }
QScrollBar::handle:vertical { background: @@SCROLL@@; min-height: @@SCROLLMIN@@; border: none; }
QScrollBar::handle:vertical:hover { background: @@SCROLLHOVER@@; }
QScrollBar::handle:vertical:pressed { background: @@SCROLLPRESS@@; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; height: 0; width: 0; }
QScrollBar:horizontal { height: 0; }
QFrame#hairline { background: @@DIVIDER@@; max-height: 1px; min-height: 1px; border: none; }
QFrame#toast { background: @@BG@@; border: 1px solid @@EDGE@@; }
QFrame#toast[error="true"] { border: 1px solid @@DANGER@@; }
)");
  const QList<QPair<QString, QString>> tokens = {
      {QStringLiteral("@@FAMILY@@"), family},
      {QStringLiteral("@@BODY@@"), px(m.text(12))},
      {QStringLiteral("@@SMALL@@"), px(m.text(11))},
      {QStringLiteral("@@HEADING@@"), px(m.text(16))},
      {QStringLiteral("@@BTNY@@"), px(m.space(6))},
      {QStringLiteral("@@BTNX@@"), px(m.space(10))},
      {QStringLiteral("@@ICONX@@"), px(m.space(8))},
      {QStringLiteral("@@LINKY@@"), px(m.space(4))},
      {QStringLiteral("@@LINKX@@"), px(m.space(2))},
      {QStringLiteral("@@INPUTY@@"), px(m.space(7))},
      {QStringLiteral("@@INPUTX@@"), px(m.space(10))},
      {QStringLiteral("@@DROPDOWN@@"), px(m.space(16))},
      {QStringLiteral("@@MENUPAD@@"), px(m.space(4))},
      {QStringLiteral("@@MENUY@@"), px(m.space(8))},
      {QStringLiteral("@@MENUX@@"), px(m.space(18))},
      {QStringLiteral("@@MENUSEP@@"), px(m.space(8))},
      {QStringLiteral("@@SCROLLW@@"), px(std::max(6, m.space(8)))},
      {QStringLiteral("@@SCROLLMIN@@"), px(m.space(24))},
      {QStringLiteral("@@BG@@"), palette.background.name(QColor::HexRgb)},
      {QStringLiteral("@@FG@@"), palette.foreground.name(QColor::HexRgb)},
      {QStringLiteral("@@BRIGHT@@"), palette.bright.name(QColor::HexRgb)},
      {QStringLiteral("@@SECONDARY@@"), palette.secondary.name(QColor::HexRgb)},
      {QStringLiteral("@@DANGER@@"), palette.danger.name(QColor::HexRgb)},
      {QStringLiteral("@@ACCENT@@"), palette.accent.name(QColor::HexRgb)},
      {QStringLiteral("@@EDGE@@"), a(0.40)},
      {QStringLiteral("@@FOCUS@@"), a(0.70)},
      {QStringLiteral("@@HOVER@@"), a(0.08)},
      {QStringLiteral("@@PRESSED@@"), a(0.22)},
      {QStringLiteral("@@DISABLED@@"), a(0.45)},
      {QStringLiteral("@@SELECTION@@"), palette.selection.name(QColor::HexRgb)},
      {QStringLiteral("@@INSET@@"), palette.inset.name(QColor::HexRgb)},
      {QStringLiteral("@@SCROLLTRACK@@"), a(0.04)},
      {QStringLiteral("@@SCROLL@@"), a(0.35)},
      {QStringLiteral("@@SCROLLHOVER@@"), a(0.55)},
      {QStringLiteral("@@SCROLLPRESS@@"), a(0.70)},
      {QStringLiteral("@@DIVIDER@@"), a(0.12)},
  };
  for (const auto& token : tokens) css.replace(token.first, token.second);
  return css;
}

}  // namespace contacts
