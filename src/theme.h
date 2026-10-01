#pragma once

#include <QColor>
#include <QString>
#include <optional>

namespace contacts {

enum class Appearance { Dark, Light };
enum class ThemeOrigin { State, Legacy, Builtin };

struct Palette {
  Appearance appearance = Appearance::Dark;
  QString name = QStringLiteral("Tokyo Night");
  QColor background;
  QColor foreground;
  QColor accent;
  QColor surface;
  QColor inset;
  QColor bright;
  QColor secondary;
  QColor selection;
  QColor border;
  QColor onAccent;
  QColor danger;
  QColor warning;
  QColor success;

  bool sameColors(const Palette& other) const;
};

struct ThemePaths {
  QString stateDir;
  QString stateColors;
  QString stateName;
  QString stateShell;
  QString legacyDir;
  QString legacyColors;
  QString legacyName;
  QString legacyShell;
  // `omarchy display text size` writes [font] base-size here, over the theme.
  QString userShell;

  static ThemePaths forHome(const QString& home);
};

// Sizes from Omarchy's shell.toml. Every size in the window is written for the
// shell's 12px base and goes through text() or space(), so the window follows
// the text size chosen in the Omarchy menu.
struct ShellMetrics {
  int basePx = 12;
  double spacingScale = 1.0;

  bool operator==(const ShellMetrics& other) const = default;
  int text(int px) const;
  int space(int px) const;
};

// A value under [section] in a small TOML file, without quotes or comments.
std::optional<QString> tomlValue(const QString& text, const QString& section, const QString& key);
// The user's shell.toml wins key by key over the theme's.
ShellMetrics parseShellMetrics(const QString& themeToml, const QString& userToml);
ShellMetrics loadShellMetrics(const ThemePaths& paths);

// The window publishes its metrics here so dialogs size themselves the same way.
const ShellMetrics& activeMetrics();
void setActiveMetrics(const ShellMetrics& metrics);

// The family fontconfig gives `monospace`, which `omarchy font set` changes.
QString monospaceFamily();

struct ThemeLoad {
  Palette palette;
  ThemeOrigin origin = ThemeOrigin::Builtin;
  QString path;
};

QString rgba(const QColor& color, double alpha);
Palette tokyoNight();

// Null when a required role is missing or any color is not #RRGGBB.
// `name` is kept when parsing succeeds; a blank name becomes "Omarchy".
std::optional<Palette> parseColorsToml(const QString& text, const QString& name);
ThemeLoad loadTheme(const ThemePaths& paths);

// Square, quiet widget sheet. Fills are for interaction state only.
QString stylesheetFor(const Palette& palette, const ShellMetrics& metrics = {},
                      const QString& family = QStringLiteral("monospace"));

}  // namespace contacts
