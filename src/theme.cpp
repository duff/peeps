#include "theme.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace omacontacts {
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
  paths.legacyDir = home + QStringLiteral("/.config/omarchy/current");
  paths.legacyColors = paths.legacyDir + QStringLiteral("/theme/colors.toml");
  paths.legacyName = paths.legacyDir + QStringLiteral("/theme.name");
  return paths;
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

QString stylesheetFor(const Palette& palette) {
  const auto a = [&](double alpha) { return rgba(palette.foreground, alpha); };
  QString css = QStringLiteral(R"(
QWidget { background: @@BG@@; color: @@FG@@; font-family: "JetBrainsMono Nerd Font", monospace; font-size: 12px; }
QMainWindow, QDialog { background: @@BG@@; color: @@FG@@; }
QLabel { background: transparent; color: @@FG@@; font-size: 12px; }
QLabel#title { color: @@BRIGHT@@; font-size: 16px; font-weight: 700; }
QLabel#panelTitle { color: @@BRIGHT@@; font-size: 14px; font-weight: 700; }
QLabel#secondary, QLabel#themeName, QLabel#status { color: @@SECONDARY@@; }
QLabel#danger { color: @@DANGER@@; }
QLabel#accent { color: @@ACCENT@@; }
QLabel#avatar { background: @@SURFACE@@; color: @@FG@@; border: 1px solid @@EDGE@@; min-width: 32px; max-width: 32px; min-height: 32px; max-height: 32px; font-weight: 700; qproperty-alignment: AlignCenter; }
QPushButton { background: transparent; color: @@FG@@; border: 1px solid transparent; border-radius: 0; padding: 6px 10px; font-size: 12px; font-weight: 400; }
QPushButton[kind="outline"] { border-color: @@EDGE@@; }
QPushButton[kind="primary"] { color: @@ACCENT@@; border-color: @@ACCENT@@; }
QPushButton[kind="danger"] { color: @@DANGER@@; border-color: @@DANGER@@; }
QPushButton:hover { background: @@HOVER@@; }
QPushButton:pressed { background: @@PRESSED@@; }
QPushButton:disabled { color: @@DISABLED@@; }
QLineEdit, QTextEdit, QPlainTextEdit { background: transparent; color: @@FG@@; border: 1px solid @@EDGE@@; border-radius: 0; padding: 7px 10px; selection-background-color: @@SELECTION@@; selection-color: @@FG@@; font-size: 12px; }
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus { border: 1px solid @@ACCENT@@; }
QTextEdit#notes, QPlainTextEdit#notes { background: @@INSET@@; }
QListView { background: transparent; border: none; outline: none; }
QScrollBar:vertical { background: @@SCROLLTRACK@@; width: 8px; margin: 0; border: none; }
QScrollBar::handle:vertical { background: @@SCROLL@@; min-height: 24px; border: none; }
QScrollBar::handle:vertical:hover { background: @@SCROLLHOVER@@; }
QScrollBar::handle:vertical:pressed { background: @@SCROLLPRESS@@; }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; height: 0; width: 0; }
QScrollBar:horizontal { height: 0; }
QFrame#hairline { background: @@DIVIDER@@; max-height: 1px; border: none; }
QFrame#vhairline { background: @@DIVIDER@@; max-width: 1px; border: none; }
QFrame#toast { background: @@BG@@; border: 1px solid @@EDGE@@; }
QFrame#toast[error="true"] { border: 1px solid @@DANGER@@; }
)");
  const QList<QPair<QString, QString>> tokens = {
      {QStringLiteral("@@BG@@"), palette.background.name(QColor::HexRgb)},
      {QStringLiteral("@@FG@@"), palette.foreground.name(QColor::HexRgb)},
      {QStringLiteral("@@BRIGHT@@"), palette.bright.name(QColor::HexRgb)},
      {QStringLiteral("@@SECONDARY@@"), palette.secondary.name(QColor::HexRgb)},
      {QStringLiteral("@@DANGER@@"), palette.danger.name(QColor::HexRgb)},
      {QStringLiteral("@@ACCENT@@"), palette.accent.name(QColor::HexRgb)},
      {QStringLiteral("@@SURFACE@@"), palette.surface.name(QColor::HexRgb)},
      {QStringLiteral("@@EDGE@@"), a(0.40)},
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

}  // namespace omacontacts
