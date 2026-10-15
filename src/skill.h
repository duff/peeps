#pragma once

#include <QString>
#include <QStringList>

namespace contacts {

// The agent skill installs with Peeps, in share/peeps/skills/peeps beside the
// binary's bin directory. Agents find skills through links in their own
// folders, the way Omarchy links its skills, so the same installed skill
// serves Claude Code, Codex, pi, and any agent that reads ~/.agents/skills.

// The installed skill for a peeps binary in `binDir`, or empty when there is
// none, as when running from the build directory.
QString installedSkillDir(const QString& binDir);

// The skills folders of the agents set up in `home`. CLAUDE_CONFIG_DIR and
// CODEX_HOME move those agents' folders, and an empty value means the default.
QStringList agentSkillFolders(const QString& home, const QString& claudeConfigDir = {}, const QString& codexHome = {});

struct SkillLinks {
  // Links made or taken away by this call.
  QStringList changed;
  // Links that already pointed at the skill.
  QStringList unchanged;
  QStringList skipped;
  QString error;
};

// Links `skillDir` as peeps in each skills folder. A link to somewhere else is
// replaced. Anything else named peeps is left alone and reported.
SkillLinks linkSkill(const QString& skillDir, const QStringList& folders);

// Removes the peeps links that point at a Peeps skill. Anything else named
// peeps is left alone.
SkillLinks unlinkSkill(const QStringList& folders);

}  // namespace contacts
