#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
#include <string>

/*! Owns the currently open AI CAD project (folder on disk).
 *
 * Layout:
 *   <root>/
 *     design/   — .scad sources (main.scad + temp.scad)
 *     assets/   — reference images / drawings
 *     skills/   — personal SKILL.md trees
 *     rules/    — always injected into the AI system prompt
 *     exports/  — STL / PNG outputs
 *     system/   — project metadata (project.json, chats, …)
 */
class ProjectManager : public QObject
{
  Q_OBJECT

public:
  static ProjectManager& instance();

  static const QStringList& fixedFolders();
  static bool isFixedFolderName(const QString& name);

  bool hasProject() const { return !rootPath_.isEmpty(); }
  QString rootPath() const { return rootPath_; }
  QString projectName() const { return projectName_; }

  /*! Create a new project at `rootPath` (must be empty or non-existent). */
  bool createProject(const QString& rootPath, const QString& name, QString *errorOut = nullptr);
  /*! Open an existing project folder (scaffolds missing fixed folders). */
  bool openProject(const QString& rootPath, QString *errorOut = nullptr);
  void closeProject();

  QString activeFilePath() const { return activeFile_; }
  void setActiveFile(const QString& absolutePath);

  /*! Preferred project .scad for AI context (active file if it exists). Empty if none —
   *  the UI creates an unsaved Untitled tab instead of writing design/temp.scad. */
  QString aiTargetFile();

  QString designDir() const;
  QString assetsDir() const;
  QString skillsDir() const;
  QString rulesDir() const;
  QString exportsDir() const;
  QString systemDir() const;

  QString rulesText() const;
  QStringList listSkillNames() const;
  QString skillText(const QString& name, bool compact = false) const;
  QStringList listDesignFiles() const;

  /*! Relative path listing for AI (depth-limited). */
  QString listProjectFilesText(int maxDepth = 4, int maxEntries = 200) const;
  /*! Read a project-relative path; rejects `..`. */
  QString readProjectFile(const QString& relativePath, QString *errorOut = nullptr) const;
  /*! Absolute path if `relativeOrAbsolute` resolves inside the project. */
  QString resolveProjectPath(const QString& relativeOrAbsolute, QString *errorOut = nullptr) const;

  QJsonArray loadChats() const;
  void storeChats(const QJsonArray& chats);

  QStringList recentProjects() const;
  void clearRecentProjects();

  /*! Compact block for system-prompt injection. */
  std::string buildContextPromptBlock() const;

signals:
  void projectChanged();
  void activeFileChanged(const QString& absolutePath);

private:
  explicit ProjectManager(QObject *parent = nullptr);

  bool ensureScaffold(const QString& root, QString *errorOut);
  bool loadProjectJson();
  bool saveProjectJson() const;
  void rememberRecent(const QString& root);
  QString defaultTempScadPath() const;
  QString ensureTempScad();
  QString projectJsonPath() const;
  QString legacyProjectJsonPath() const;

  QString rootPath_;
  QString projectName_;
  QString activeFile_;
  QJsonObject meta_;  // full project.json object in memory
};
