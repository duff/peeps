#pragma once

#include <QColor>
#include <QString>
#include <optional>

namespace omacontacts {

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
  QString legacyDir;
  QString legacyColors;
  QString legacyName;

  static ThemePaths forHome(const QString& home);
};

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
QString stylesheetFor(const Palette& palette);

}  // namespace omacontacts
