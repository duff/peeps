#include "skill.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace contacts {
namespace {

const QString kName = QStringLiteral("peeps");

// A link this install made, or an older install's, both end in the same path.
bool isPeepsSkillLink(const QFileInfo& entry) {
  return entry.isSymLink() && entry.symLinkTarget().endsWith(QStringLiteral("/peeps/skills/peeps"));
}

}  // namespace

QString installedSkillDir(const QString& binDir) {
  const QString dir = QDir::cleanPath(binDir + QStringLiteral("/../share/peeps/skills/peeps"));
  return QFileInfo::exists(dir + QStringLiteral("/SKILL.md")) ? dir : QString();
}

QStringList agentSkillFolders(const QString& home, const QString& claudeConfigDir, const QString& codexHome) {
  const QString claude = claudeConfigDir.isEmpty() ? home + QStringLiteral("/.claude") : claudeConfigDir;
  const QString codex = codexHome.isEmpty() ? home + QStringLiteral("/.codex") : codexHome;
  const QStringList agents = {claude, codex, home + QStringLiteral("/.pi/agent"), home + QStringLiteral("/.agents")};
  QStringList folders;
  for (const QString& agent : agents) {
    if (QFileInfo(agent).isDir()) folders.append(agent + QStringLiteral("/skills"));
  }
  return folders;
}

SkillLinks linkSkill(const QString& skillDir, const QStringList& folders) {
  SkillLinks result;
  for (const QString& folder : folders) {
    const QString link = folder + QLatin1Char('/') + kName;
    const QFileInfo entry(link);
    if (entry.isSymLink()) {
      if (entry.symLinkTarget() == skillDir) {
        result.unchanged.append(link);
        continue;
      }
      QFile::remove(link);
    } else if (entry.exists()) {
      result.skipped.append(link);
      continue;
    }
    if (!QDir().mkpath(folder) || !QFile::link(skillDir, link)) {
      result.error = QStringLiteral("Could not link %1.").arg(link);
      return result;
    }
    result.changed.append(link);
  }
  return result;
}

SkillLinks unlinkSkill(const QStringList& folders) {
  SkillLinks result;
  for (const QString& folder : folders) {
    const QString link = folder + QLatin1Char('/') + kName;
    const QFileInfo entry(link);
    if (isPeepsSkillLink(entry)) {
      if (!QFile::remove(link)) {
        result.error = QStringLiteral("Could not remove %1.").arg(link);
        return result;
      }
      result.changed.append(link);
    } else if (entry.exists() || entry.isSymLink()) {
      result.skipped.append(link);
    }
  }
  return result;
}

}  // namespace contacts
