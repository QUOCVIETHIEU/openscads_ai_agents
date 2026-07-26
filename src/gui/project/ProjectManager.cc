#include "gui/project/ProjectManager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QTextStream>
#include <sstream>

namespace {

constexpr const char *kRecentKey = "project/recent";
constexpr int kMaxRecent = 10;
constexpr const char *kProjectJsonRel = "system/project.json";
constexpr const char *kLegacyProjectJsonRel = "project.json";

QStringList fixedFolderNames()
{
  return {QStringLiteral("design"), QStringLiteral("assets"), QStringLiteral("skills"),
          QStringLiteral("rules"), QStringLiteral("exports"), QStringLiteral("system")};
}

bool looksLikeProjectEntry(const QString& entry)
{
  return ProjectManager::fixedFolders().contains(entry) ||
         entry == QLatin1String(kLegacyProjectJsonRel);
}

}  // namespace

ProjectManager& ProjectManager::instance()
{
  static ProjectManager mgr;
  return mgr;
}

ProjectManager::ProjectManager(QObject *parent) : QObject(parent) {}

const QStringList& ProjectManager::fixedFolders()
{
  static const QStringList folders = fixedFolderNames();
  return folders;
}

bool ProjectManager::isFixedFolderName(const QString& name)
{
  return fixedFolders().contains(name);
}

QString ProjectManager::designDir() const
{
  return rootPath_.isEmpty() ? QString() : QDir(rootPath_).filePath(QStringLiteral("design"));
}
QString ProjectManager::assetsDir() const
{
  return rootPath_.isEmpty() ? QString() : QDir(rootPath_).filePath(QStringLiteral("assets"));
}
QString ProjectManager::skillsDir() const
{
  return rootPath_.isEmpty() ? QString() : QDir(rootPath_).filePath(QStringLiteral("skills"));
}
QString ProjectManager::rulesDir() const
{
  return rootPath_.isEmpty() ? QString() : QDir(rootPath_).filePath(QStringLiteral("rules"));
}
QString ProjectManager::exportsDir() const
{
  return rootPath_.isEmpty() ? QString() : QDir(rootPath_).filePath(QStringLiteral("exports"));
}
QString ProjectManager::systemDir() const
{
  return rootPath_.isEmpty() ? QString() : QDir(rootPath_).filePath(QStringLiteral("system"));
}

QString ProjectManager::projectJsonPath() const
{
  return rootPath_.isEmpty() ? QString() : QDir(rootPath_).filePath(QString::fromUtf8(kProjectJsonRel));
}

QString ProjectManager::legacyProjectJsonPath() const
{
  return rootPath_.isEmpty() ? QString()
                             : QDir(rootPath_).filePath(QString::fromUtf8(kLegacyProjectJsonRel));
}

bool ProjectManager::ensureScaffold(const QString& root, QString *errorOut)
{
  QDir dir(root);
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
    if (errorOut) *errorOut = QObject::tr("Cannot create project folder: %1").arg(root);
    return false;
  }
  for (const QString& name : fixedFolders()) {
    if (!dir.mkpath(name)) {
      if (errorOut) *errorOut = QObject::tr("Cannot create folder: %1").arg(name);
      return false;
    }
  }

  // Seed design/main.scad if missing
  const QString mainScad = dir.filePath(QStringLiteral("design/main.scad"));
  if (!QFileInfo::exists(mainScad)) {
    QFile f(mainScad);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream out(&f);
      out << "// OpenSCAD AI project — main design\n"
             "// Describe changes in Chat; the agent writes here by default.\n"
             "\n"
             "cube([20, 20, 20], center = true);\n";
    }
  }

  // Seed rules/README.md if missing
  const QString rulesReadme = dir.filePath(QStringLiteral("rules/README.md"));
  if (!QFileInfo::exists(rulesReadme)) {
    QFile f(rulesReadme);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream out(&f);
      out << "# Project rules\n\n"
             "Put markdown files here. They are always injected into the Cad Agent "
             "system prompt for this project.\n";
    }
  }

  // Seed skills placeholder readme
  const QString skillsReadme = dir.filePath(QStringLiteral("skills/README.md"));
  if (!QFileInfo::exists(skillsReadme)) {
    QFile f(skillsReadme);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream out(&f);
      out << "# Project skills\n\n"
             "Create a subfolder with `SKILL.md` (and optional `SKILL.compact.md`). "
             "The agent loads them via `get_skill`.\n";
    }
  }

  // Seed system folder readme (project.json lives here)
  const QString systemReadme = dir.filePath(QStringLiteral("system/README.md"));
  if (!QFileInfo::exists(systemReadme)) {
    QFile f(systemReadme);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream out(&f);
      out << "# System\n\n"
             "App metadata for this project (`project.json`, chat history, …). "
             "Do not delete this folder.\n";
    }
  }

  return true;
}

bool ProjectManager::createProject(const QString& rootPath, const QString& name, QString *errorOut)
{
  const QString root = QDir::cleanPath(rootPath);
  QDir dir(root);
  if (dir.exists()) {
    const QStringList entries =
      dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
    const bool hasMeta = QFileInfo::exists(dir.filePath(QString::fromUtf8(kProjectJsonRel))) ||
                         QFileInfo::exists(dir.filePath(QString::fromUtf8(kLegacyProjectJsonRel)));
    if (!entries.isEmpty() && !hasMeta) {
      // Allow creating into a folder that only has empty structure; otherwise refuse if non-empty
      // without project.json to avoid clobbering user data.
      bool onlyOurs = true;
      for (const QString& e : entries) {
        if (!looksLikeProjectEntry(e)) {
          onlyOurs = false;
          break;
        }
      }
      if (!onlyOurs) {
        if (errorOut)
          *errorOut = QObject::tr("Folder is not empty. Choose an empty folder or Open Project.");
        return false;
      }
    }
  }

  if (!ensureScaffold(root, errorOut)) return false;

  meta_ = QJsonObject();
  meta_.insert(QStringLiteral("name"), name.isEmpty() ? QFileInfo(root).fileName() : name);
  meta_.insert(QStringLiteral("createdAt"), QDateTime::currentMSecsSinceEpoch());
  meta_.insert(QStringLiteral("updatedAt"), QDateTime::currentMSecsSinceEpoch());
  meta_.insert(QStringLiteral("activeFile"), QStringLiteral("design/main.scad"));
  meta_.insert(QStringLiteral("chats"), QJsonArray());

  rootPath_ = root;
  projectName_ = meta_.value(QStringLiteral("name")).toString();
  activeFile_ = QDir(root).filePath(QStringLiteral("design/main.scad"));

  if (!saveProjectJson()) {
    if (errorOut) *errorOut = QObject::tr("Failed to write system/project.json");
    rootPath_.clear();
    return false;
  }

  rememberRecent(root);
  emit projectChanged();
  emit activeFileChanged(activeFile_);
  return true;
}

bool ProjectManager::openProject(const QString& rootPath, QString *errorOut)
{
  const QString root = QDir::cleanPath(rootPath);
  if (!QDir(root).exists()) {
    if (errorOut) *errorOut = QObject::tr("Folder does not exist: %1").arg(root);
    return false;
  }
  if (!ensureScaffold(root, errorOut)) return false;

  rootPath_ = root;
  if (!loadProjectJson()) {
    // Brand-new attachment: synthesize project.json
    meta_ = QJsonObject();
    meta_.insert(QStringLiteral("name"), QFileInfo(root).fileName());
    meta_.insert(QStringLiteral("createdAt"), QDateTime::currentMSecsSinceEpoch());
    meta_.insert(QStringLiteral("updatedAt"), QDateTime::currentMSecsSinceEpoch());
    meta_.insert(QStringLiteral("activeFile"), QStringLiteral("design/main.scad"));
    meta_.insert(QStringLiteral("chats"), QJsonArray());
    saveProjectJson();
  }

  projectName_ = meta_.value(QStringLiteral("name")).toString(QFileInfo(root).fileName());
  const QString rel = meta_.value(QStringLiteral("activeFile")).toString(QStringLiteral("design/main.scad"));
  activeFile_ = QDir(root).filePath(rel);
  if (!QFileInfo::exists(activeFile_)) {
    activeFile_ = QDir(root).filePath(QStringLiteral("design/main.scad"));
  }

  rememberRecent(root);
  emit projectChanged();
  emit activeFileChanged(activeFile_);
  return true;
}

void ProjectManager::closeProject()
{
  if (rootPath_.isEmpty()) return;
  saveProjectJson();
  rootPath_.clear();
  projectName_.clear();
  activeFile_.clear();
  meta_ = QJsonObject();
  emit projectChanged();
  emit activeFileChanged(QString());
}

bool ProjectManager::loadProjectJson()
{
  QString path = projectJsonPath();
  if (!QFileInfo::exists(path)) {
    // Migrate from legacy root-level project.json
    const QString legacy = legacyProjectJsonPath();
    if (QFileInfo::exists(legacy)) path = legacy;
    else return false;
  }
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return false;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  if (!doc.isObject()) return false;
  meta_ = doc.object();
  // Persist into system/ if we loaded the legacy location
  if (path == legacyProjectJsonPath() && path != projectJsonPath()) {
    if (projectName_.isEmpty()) {
      projectName_ = meta_.value(QStringLiteral("name")).toString();
    }
    saveProjectJson();
    QFile::remove(legacyProjectJsonPath());
  }
  return true;
}

bool ProjectManager::saveProjectJson() const
{
  if (rootPath_.isEmpty()) return false;
  QDir(rootPath_).mkpath(QStringLiteral("system"));
  QJsonObject obj = meta_;
  obj.insert(QStringLiteral("name"), projectName_);
  obj.insert(QStringLiteral("updatedAt"), QDateTime::currentMSecsSinceEpoch());
  if (!activeFile_.isEmpty()) {
    QDir root(rootPath_);
    obj.insert(QStringLiteral("activeFile"), root.relativeFilePath(activeFile_));
  }
  // Preserve chats from meta_ (already in obj via copy)
  QFile f(projectJsonPath());
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
  return true;
}

void ProjectManager::setActiveFile(const QString& absolutePath)
{
  if (rootPath_.isEmpty()) return;
  const QString clean = QDir::cleanPath(absolutePath);
  if (!clean.startsWith(rootPath_)) return;
  if (activeFile_ == clean) return;
  activeFile_ = clean;
  saveProjectJson();
  emit activeFileChanged(activeFile_);
}

QString ProjectManager::defaultTempScadPath() const
{
  return designDir().isEmpty() ? QString()
                               : QDir(designDir()).filePath(QStringLiteral("temp.scad"));
}

QString ProjectManager::ensureTempScad()
{
  const QString path = defaultTempScadPath();
  if (path.isEmpty()) return {};
  if (!QFileInfo::exists(path)) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QTextStream out(&f);
      out << "// Temporary AI target — overwritten when no file is selected\n";
    }
  }
  return path;
}

QString ProjectManager::aiTargetFile()
{
  if (rootPath_.isEmpty()) return {};
  if (!activeFile_.isEmpty() && activeFile_.endsWith(QStringLiteral(".scad"), Qt::CaseInsensitive) &&
      QFileInfo::exists(activeFile_)) {
    return activeFile_;
  }
  return ensureTempScad();
}

QString ProjectManager::rulesText() const
{
  if (rootPath_.isEmpty()) return {};
  QDir dir(rulesDir());
  const QStringList files =
    dir.entryList({QStringLiteral("*.md"), QStringLiteral("*.txt")}, QDir::Files, QDir::Name);
  QString out;
  for (const QString& name : files) {
    if (name.compare(QStringLiteral("README.md"), Qt::CaseInsensitive) == 0) continue;
    QFile f(dir.filePath(name));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
    out += QStringLiteral("\n--- rules/%1 ---\n").arg(name);
    out += QString::fromUtf8(f.readAll());
    out += QLatin1Char('\n');
  }
  // If only README exists, still include it lightly
  if (out.isEmpty()) {
    QFile f(dir.filePath(QStringLiteral("README.md")));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
      out = QString::fromUtf8(f.readAll());
    }
  }
  return out.trimmed();
}

QStringList ProjectManager::listSkillNames() const
{
  QStringList names;
  if (rootPath_.isEmpty()) return names;
  QDir dir(skillsDir());
  for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    if (QFileInfo::exists(dir.filePath(name + QStringLiteral("/SKILL.md")))) {
      names.append(name);
    }
  }
  return names;
}

QString ProjectManager::skillText(const QString& name, bool compact) const
{
  if (rootPath_.isEmpty() || name.isEmpty() || name.contains(QLatin1String("..")) ||
      name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
    return {};
  }
  const QString file = QDir(skillsDir()).filePath(
    name + (compact ? QStringLiteral("/SKILL.compact.md") : QStringLiteral("/SKILL.md")));
  QFile f(file);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (compact) {
      // Fall back to full skill
      return skillText(name, false);
    }
    return {};
  }
  return QString::fromUtf8(f.readAll());
}

QStringList ProjectManager::listDesignFiles() const
{
  QStringList out;
  if (rootPath_.isEmpty()) return out;
  QDir dir(designDir());
  for (const QString& name : dir.entryList({QStringLiteral("*.scad")}, QDir::Files, QDir::Name)) {
    out.append(QStringLiteral("design/") + name);
  }
  return out;
}

QString ProjectManager::listProjectFilesText(int maxDepth, int maxEntries) const
{
  if (rootPath_.isEmpty()) return "Error: no project open.";
  QStringList lines;
  std::function<void(const QDir&, int)> walk = [&](const QDir& dir, int depth) {
    if (depth > maxDepth || lines.size() >= maxEntries) return;
    const QFileInfoList entries =
      dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
    for (const QFileInfo& info : entries) {
      if (lines.size() >= maxEntries) break;
      const QString rel = QDir(rootPath_).relativeFilePath(info.absoluteFilePath());
      if (info.isDir()) {
        lines.append(rel + QLatin1Char('/'));
        walk(QDir(info.absoluteFilePath()), depth + 1);
      } else {
        lines.append(rel);
      }
    }
  };
  walk(QDir(rootPath_), 0);
  return lines.join(QLatin1Char('\n'));
}

QString ProjectManager::resolveProjectPath(const QString& relativeOrAbsolute, QString *errorOut) const
{
  if (rootPath_.isEmpty()) {
    if (errorOut) *errorOut = QObject::tr("No project open.");
    return {};
  }
  if (relativeOrAbsolute.contains(QLatin1String(".."))) {
    if (errorOut) *errorOut = QObject::tr("Path must not contain '..'.");
    return {};
  }
  QString abs;
  if (QDir::isAbsolutePath(relativeOrAbsolute)) {
    abs = QDir::cleanPath(relativeOrAbsolute);
  } else {
    abs = QDir::cleanPath(QDir(rootPath_).filePath(relativeOrAbsolute));
  }
  if (!abs.startsWith(rootPath_)) {
    if (errorOut) *errorOut = QObject::tr("Path is outside the project.");
    return {};
  }
  return abs;
}

QString ProjectManager::readProjectFile(const QString& relativePath, QString *errorOut) const
{
  const QString abs = resolveProjectPath(relativePath, errorOut);
  if (abs.isEmpty()) return {};
  QFile f(abs);
  if (!f.open(QIODevice::ReadOnly)) {
    if (errorOut) *errorOut = QObject::tr("Cannot read file: %1").arg(relativePath);
    return {};
  }
  // Binary images handled by caller via extension; here return bytes as UTF-8 text
  // for text formats. For images return empty and let ChatWidget encode.
  return QString::fromUtf8(f.readAll());
}

QJsonArray ProjectManager::loadChats() const
{
  return meta_.value(QStringLiteral("chats")).toArray();
}

void ProjectManager::storeChats(const QJsonArray& chats)
{
  if (rootPath_.isEmpty()) return;
  meta_.insert(QStringLiteral("chats"), chats);
  saveProjectJson();
}

void ProjectManager::rememberRecent(const QString& root)
{
  QSettings settings;
  QStringList recent = settings.value(kRecentKey).toStringList();
  recent.removeAll(root);
  recent.prepend(root);
  while (recent.size() > kMaxRecent) recent.removeLast();
  settings.setValue(kRecentKey, recent);
}

QStringList ProjectManager::recentProjects() const
{
  QSettings settings;
  QStringList recent = settings.value(kRecentKey).toStringList();
  QStringList valid;
  for (const QString& p : recent) {
    if (QDir(p).exists()) valid.append(p);
  }
  if (valid.size() != recent.size()) {
    settings.setValue(kRecentKey, valid);
  }
  return valid;
}

void ProjectManager::clearRecentProjects()
{
  QSettings settings;
  settings.remove(kRecentKey);
}

std::string ProjectManager::buildContextPromptBlock() const
{
  if (rootPath_.isEmpty()) return {};
  std::ostringstream os;
  os << "### PROJECT CONTEXT\n";
  os << "Project name: " << projectName_.toStdString() << "\n";
  os << "Project root: " << rootPath_.toStdString() << "\n";
  const QString target = const_cast<ProjectManager *>(this)->aiTargetFile();
  os << "AI target file (write OpenSCAD here via set_editor_code): "
     << QDir(rootPath_).relativeFilePath(target).toStdString() << "\n";
  if (!activeFile_.isEmpty()) {
    os << "Active editor file: " << QDir(rootPath_).relativeFilePath(activeFile_).toStdString()
       << "\n";
  }
  os << "Design files:\n";
  for (const QString& f : listDesignFiles()) {
    os << "- " << f.toStdString() << "\n";
  }
  const QStringList skills = listSkillNames();
  if (!skills.isEmpty()) {
    os << "Project skills (call get_skill with these names):\n";
    for (const QString& s : skills) os << "- " << s.toStdString() << "\n";
  }
  const QString rules = rulesText();
  if (!rules.isEmpty()) {
    os << "\n### PROJECT RULES (always follow)\n";
    os << rules.toStdString() << "\n";
  }
  os << "\nUse list_project_files / read_project_file / get_project_rules to inspect assets and "
        "rules. Prefer writing complete scripts to the AI target file.\n";
  return os.str();
}
