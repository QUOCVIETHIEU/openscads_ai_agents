#include "gui/project/ProjectExplorer.h"
#include "gui/project/ProjectFileIconProvider.h"
#include "gui/project/ProjectManager.h"
#include "gui/qtgettext.h"
#include "openscad_gui.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeView>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDesktopServices>
#include <QUrl>

ProjectExplorer::ProjectExplorer(QWidget *parent) : QWidget(parent)
{
  setObjectName(QStringLiteral("projectExplorer"));

  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  auto *header = new QWidget(this);
  header->setObjectName(QStringLiteral("projectExplorerHeader"));
  header->setFixedHeight(32);
  header->setMinimumHeight(32);
  header->setMaximumHeight(32);
  header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  header->setAttribute(Qt::WA_StyledBackground, true);
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(12, 0, 8, 0);
  headerLayout->setSpacing(4);

  titleLabel_ = new QLabel(_("EXPLORER"), header);
  titleLabel_->setObjectName(QStringLiteral("projectExplorerTitle"));
  titleLabel_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
  titleLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  headerLayout->addWidget(titleLabel_, 1);

  collapseBtn_ = new QPushButton(header);
  collapseBtn_->setObjectName(QStringLiteral("projectExplorerCollapse"));
  collapseBtn_->setFlat(true);
  collapseBtn_->setFixedSize(24, 24);
  collapseBtn_->setText(QStringLiteral("‹"));
  collapseBtn_->setToolTip(_("Hide explorer"));
  collapseBtn_->setCursor(Qt::PointingHandCursor);
  collapseBtn_->setFocusPolicy(Qt::NoFocus);
  connect(collapseBtn_, &QPushButton::clicked, this, &ProjectExplorer::onCollapsePressed);
  headerLayout->addWidget(collapseBtn_);
  // View switching is handled by Project/Editor tabs — hide legacy collapse.
  collapseBtn_->hide();

  outer->addWidget(header);

  stack_ = new QStackedWidget(this);
  outer->addWidget(stack_, 1);

  // Empty / no-project page
  emptyPage_ = new QWidget(stack_);
  auto *emptyLayout = new QVBoxLayout(emptyPage_);
  emptyLayout->setContentsMargins(12, 16, 12, 12);
  emptyLayout->setSpacing(6);
  auto *hint = new QLabel(
    _("Open or create a project folder to organize design files, assets, skills, and rules."),
    emptyPage_);
  hint->setWordWrap(true);
  hint->setObjectName(QStringLiteral("projectExplorerHint"));
  emptyLayout->addWidget(hint);
  auto *newBtn = new QPushButton(_("New Project…"), emptyPage_);
  newBtn->setObjectName(QStringLiteral("projectExplorerActionBtn"));
  newBtn->setCursor(Qt::PointingHandCursor);
  connect(newBtn, &QPushButton::clicked, this, &ProjectExplorer::requestNewProject);
  emptyLayout->addWidget(newBtn);
  auto *openBtn = new QPushButton(_("Open Project…"), emptyPage_);
  openBtn->setObjectName(QStringLiteral("projectExplorerActionBtn"));
  openBtn->setCursor(Qt::PointingHandCursor);
  connect(openBtn, &QPushButton::clicked, this, &ProjectExplorer::requestOpenProject);
  emptyLayout->addWidget(openBtn);
  emptyLayout->addStretch(1);
  stack_->addWidget(emptyPage_);

  // Tree page
  treePage_ = new QWidget(stack_);
  auto *treeLayout = new QVBoxLayout(treePage_);
  treeLayout->setContentsMargins(0, 0, 0, 0);
  treeLayout->setSpacing(0);

  model_ = new QFileSystemModel(this);
  model_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
  model_->setReadOnly(false);
  iconProvider_ = new ProjectFileIconProvider();
  iconProvider_->setDarkMode(isDarkMode());
  model_->setIconProvider(iconProvider_);

  tree_ = new QTreeView(treePage_);
  tree_->setObjectName(QStringLiteral("projectExplorerTree"));
  tree_->setModel(model_);
  tree_->setHeaderHidden(true);
  tree_->setAnimated(false);
  tree_->setIndentation(12);
  tree_->setIconSize(QSize(16, 16));
  tree_->setUniformRowHeights(true);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  tree_->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
  // Hide size/type/date columns
  for (int c = 1; c < model_->columnCount(); ++c) {
    tree_->hideColumn(c);
  }
  connect(tree_, &QTreeView::doubleClicked, this, &ProjectExplorer::onDoubleClicked);
  connect(tree_, &QTreeView::customContextMenuRequested, this, &ProjectExplorer::onCustomContextMenu);
  treeLayout->addWidget(tree_);
  stack_->addWidget(treePage_);

  stack_->setCurrentWidget(emptyPage_);

  connect(&ProjectManager::instance(), &ProjectManager::projectChanged, this,
          &ProjectExplorer::onProjectChanged);

  refreshTheme();
  onProjectChanged();
}

void ProjectExplorer::refreshTheme()
{
  const bool dark = isDarkMode();
  if (iconProvider_) iconProvider_->setDarkMode(dark);
  // Force the tree to re-query icons after theme change.
  if (tree_ && model_ && ProjectManager::instance().hasProject()) {
    const QString root = ProjectManager::instance().rootPath();
    const QModelIndex rootIndex = model_->setRootPath(root);
    tree_->setRootIndex(rootIndex);
  }
  const QString iconRoot =
    dark ? QStringLiteral(":/icons/chokusen-dark/svg/") : QStringLiteral(":/icons/chokusen/svg/");
  setStyleSheet(QStringLiteral(R"(
    QWidget#projectExplorer {
      background: %1;
      border: none;
    }
    QWidget#projectExplorerHeader {
      background: %3;
      border: none;
      border-bottom: 1px solid %2;
      min-height: 32px;
      max-height: 32px;
      padding: 0px;
    }
    QLabel#projectExplorerTitle {
      color: %4;
      font-size: 11px;
      font-weight: 700;
      letter-spacing: 0.6px;
      padding: 0px;
      margin: 0px;
    }
    QLabel#projectExplorerHint {
      color: %5;
      font-size: 12px;
    }
    QPushButton#projectExplorerActionBtn {
      background: %3;
      color: %4;
      border: 1px solid %2;
      border-radius: 4px;
      padding: 5px 12px;
      font-size: 12px;
      text-align: center;
    }
    QPushButton#projectExplorerActionBtn:hover {
      background: %6;
      border-color: %5;
    }
    QPushButton#projectExplorerActionBtn:pressed {
      background: %2;
    }
    QPushButton#projectExplorerCollapse {
      background: transparent;
      border: none;
      border-radius: 4px;
      color: %5;
      font-size: 16px;
    }
    QPushButton#projectExplorerCollapse:hover {
      background: %6;
      color: %4;
    }
    QTreeView#projectExplorerTree {
      background: %1;
      border: none;
      color: %4;
      font-size: 12px;
      outline: 0;
    }
    QTreeView#projectExplorerTree::item {
      min-height: 22px;
      height: 22px;
      padding: 0px 4px;
    }
    QTreeView#projectExplorerTree::branch {
      background: transparent;
      border-image: none;
      image: none;
    }
    QTreeView#projectExplorerTree::branch:has-children:!has-siblings:closed,
    QTreeView#projectExplorerTree::branch:closed:has-children:has-siblings {
      border-image: none;
      image: url(%8);
    }
    QTreeView#projectExplorerTree::branch:open:has-children:!has-siblings,
    QTreeView#projectExplorerTree::branch:open:has-children:has-siblings {
      border-image: none;
      image: url(%9);
    }
    QTreeView#projectExplorerTree::item:hover {
      background: %6;
    }
    QTreeView#projectExplorerTree::item:selected {
      background: %7;
      color: %4;
    }
  )")
                  .arg(dark ? QStringLiteral("#1e1e1e") : QStringLiteral("#f8f8f8"),
                       dark ? QStringLiteral("#2b2b2b") : QStringLiteral("#e5e5e5"),
                       dark ? QStringLiteral("#252526") : QStringLiteral("#f3f3f3"),
                       dark ? QStringLiteral("#cccccc") : QStringLiteral("#333333"),
                       dark ? QStringLiteral("#969696") : QStringLiteral("#6e6e6e"),
                       dark ? QStringLiteral("#2a2d2e") : QStringLiteral("#e8e8e8"),
                       dark ? QStringLiteral("#094771") : QStringLiteral("#e8f1ff"),
                       iconRoot + QStringLiteral("explorer-branch-right.svg"),
                       iconRoot + QStringLiteral("explorer-branch-down.svg")));
}

void ProjectExplorer::setCollapsed(bool collapsed)
{
  if (collapsed_ == collapsed) return;
  if (collapsed) {
    if (width() > 40) expandedWidth_ = width();
    hide();
  } else {
    show();
    resize(expandedWidth_, height());
  }
  collapsed_ = collapsed;
  emit collapsedChanged(collapsed_);
}

void ProjectExplorer::onCollapsePressed()
{
  setCollapsed(true);
}

void ProjectExplorer::onProjectChanged()
{
  auto& pm = ProjectManager::instance();
  if (!pm.hasProject()) {
    titleLabel_->setText(_("EXPLORER"));
    stack_->setCurrentWidget(emptyPage_);
    return;
  }
  titleLabel_->setText(pm.projectName().toUpper());
  const QString root = pm.rootPath();
  const QModelIndex rootIndex = model_->setRootPath(root);
  tree_->setRootIndex(rootIndex);
  tree_->expandToDepth(0);
  stack_->setCurrentWidget(treePage_);
}

QString ProjectExplorer::absolutePathForIndex(const QModelIndex& index) const
{
  return model_->filePath(index);
}

bool ProjectExplorer::isProtectedPath(const QString& absolutePath) const
{
  auto& pm = ProjectManager::instance();
  if (!pm.hasProject()) return false;
  const QString rel = QDir(pm.rootPath()).relativeFilePath(absolutePath);
  if (rel == QLatin1String("project.json") || rel == QLatin1String("system/project.json")) return true;
  const QString top = rel.section(QLatin1Char('/'), 0, 0);
  // Protect fixed folders themselves (not their contents)
  if (ProjectManager::isFixedFolderName(top) && !rel.contains(QLatin1Char('/'))) return true;
  return false;
}

void ProjectExplorer::onDoubleClicked(const QModelIndex& index)
{
  if (!index.isValid()) return;
  const QString path = absolutePathForIndex(index);
  QFileInfo info(path);
  if (info.isDir()) {
    tree_->setExpanded(index, !tree_->isExpanded(index));
    return;
  }
  emit openFileRequested(path);
}

void ProjectExplorer::onCustomContextMenu(const QPoint& pos)
{
  auto& pm = ProjectManager::instance();
  if (!pm.hasProject()) return;

  const QModelIndex index = tree_->indexAt(pos);
  QString targetDir = pm.rootPath();
  QString targetFile;
  if (index.isValid()) {
    const QString path = absolutePathForIndex(index);
    QFileInfo info(path);
    if (info.isDir()) {
      targetDir = path;
    } else {
      targetFile = path;
      targetDir = info.absolutePath();
    }
  }

  QMenu menu(this);
  QAction *newFile = menu.addAction(_("New File…"));
  QAction *newFolder = menu.addAction(_("New Folder…"));
  menu.addSeparator();
  QAction *rename = menu.addAction(_("Rename…"));
  QAction *remove = menu.addAction(_("Delete…"));
  menu.addSeparator();
  QAction *reveal = menu.addAction(_("Reveal in Finder"));

  const bool protectedItem = index.isValid() && isProtectedPath(absolutePathForIndex(index));
  rename->setEnabled(index.isValid() && !protectedItem);
  remove->setEnabled(index.isValid() && !protectedItem);

  QAction *chosen = menu.exec(tree_->viewport()->mapToGlobal(pos));
  if (!chosen) return;

  if (chosen == newFile) {
    bool ok = false;
    const QString name = QInputDialog::getText(this, _("New File"), _("File name:"), QLineEdit::Normal,
                                               QStringLiteral("untitled.scad"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    if (name.contains(QLatin1String(".."))) return;
    const QString path = QDir(targetDir).filePath(name.trimmed());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
      f.close();
      emit openFileRequested(path);
    }
  } else if (chosen == newFolder) {
    bool ok = false;
    const QString name = QInputDialog::getText(this, _("New Folder"), _("Folder name:"),
                                               QLineEdit::Normal, QStringLiteral("folder"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    if (name.contains(QLatin1String(".."))) return;
    QDir(targetDir).mkpath(name.trimmed());
  } else if (chosen == rename && index.isValid()) {
    const QString path = absolutePathForIndex(index);
    QFileInfo info(path);
    bool ok = false;
    const QString name =
      QInputDialog::getText(this, _("Rename"), _("New name:"), QLineEdit::Normal, info.fileName(), &ok);
    if (!ok || name.trimmed().isEmpty() || name.contains(QLatin1String(".."))) return;
    const QString dest = info.dir().filePath(name.trimmed());
    QFile::rename(path, dest);
  } else if (chosen == remove && index.isValid()) {
    const QString path = absolutePathForIndex(index);
    const auto answer = QMessageBox::question(
      this, _("Delete"),
      tr("Delete \"%1\"?").arg(QFileInfo(path).fileName()),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    QFileInfo info(path);
    if (info.isDir()) {
      QDir(path).removeRecursively();
    } else {
      QFile::remove(path);
    }
  } else if (chosen == reveal) {
    const QString path = index.isValid() ? absolutePathForIndex(index) : pm.rootPath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).isDir() ? path
                                                                         : QFileInfo(path).absolutePath()));
  }
}
