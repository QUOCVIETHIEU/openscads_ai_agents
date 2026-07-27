#pragma once

#include <QWidget>
#include <QString>
#include <QSet>

class QTreeView;
class QFileSystemModel;
class QLabel;
class QPushButton;
class QStackedWidget;
class QModelIndex;
class ProjectFileIconProvider;

class ProjectExplorer : public QWidget
{
  Q_OBJECT

public:
  explicit ProjectExplorer(QWidget *parent = nullptr);

  void refreshTheme();
  void setCollapsed(bool collapsed);
  bool isCollapsed() const { return collapsed_; }

signals:
  void requestNewProject();
  void requestOpenProject();
  void openFileRequested(const QString& absolutePath);
  void renderFileRequested(const QString& absolutePath);
  void collapsedChanged(bool collapsed);

public slots:
  void onProjectChanged();

private slots:
  void onTreeExpanded(const QModelIndex& index);
  void onTreeCollapsed(const QModelIndex& index);
  void onDirectoryLoaded(const QString& path);
  void onDoubleClicked(const QModelIndex& index);
  void onCustomContextMenu(const QPoint& pos);
  void onCollapsePressed();

private:
  void rebuildEmptyState();
  bool isProtectedPath(const QString& absolutePath) const;
  QString absolutePathForIndex(const QModelIndex& index) const;

  QStackedWidget *stack_ = nullptr;
  QWidget *emptyPage_ = nullptr;
  QWidget *treePage_ = nullptr;
  QLabel *titleLabel_ = nullptr;
  QTreeView *tree_ = nullptr;
  QFileSystemModel *model_ = nullptr;
  ProjectFileIconProvider *iconProvider_ = nullptr;
  QPushButton *collapseBtn_ = nullptr;
  QSet<QString> pendingExpandPaths_;
  bool collapsed_ = false;
  int expandedWidth_ = 220;
};
