#include "gui/project/ProjectExplorer.h"
#include "gui/project/ProjectFileIconProvider.h"
#include "gui/project/ProjectManager.h"
#include "gui/qtgettext.h"
#include "openscad_gui.h"

#include <QApplication>
#include <QColor>
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
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTreeView>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDesktopServices>
#include <QIcon>
#include <QUrl>
#include <QStyleFactory>
#include <QTimer>
#include <functional>

namespace {

// Cursor-style: every folder keeps an expand chevron, even when empty.
// QFileSystemModel marks empty dirs with Qt::ItemNeverHasChildren after the
// first fetch, which hides the branch arrow even if hasChildren() is true.
class ProjectFileSystemModel : public QFileSystemModel
{
public:
  using QFileSystemModel::QFileSystemModel;

  bool hasChildren(const QModelIndex& parent = QModelIndex()) const override
  {
    if (parent.isValid() && isDir(parent)) return true;
    return QFileSystemModel::hasChildren(parent);
  }

  Qt::ItemFlags flags(const QModelIndex& index) const override
  {
    Qt::ItemFlags f = QFileSystemModel::flags(index);
    if (index.isValid() && isDir(index)) {
      f &= ~Qt::ItemNeverHasChildren;
    }
    return f;
  }
};

// Tight icon→label gap; paints hover/selection ourselves so macOS does not
// force blue highlight + white text. Chevrons are drawn in-row (no branch
// column), so child indent is only setIndentation() — not a second gutter.
class ProjectTreeDelegate : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void setChrome(const QColor& text, const QColor& muted, const QColor& hover, const QColor& selected)
  {
    text_ = text;
    muted_ = muted;
    hover_ = hover;
    selected_ = selected;
  }

  void paint(QPainter *painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override
  {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    const bool selected = opt.state & QStyle::State_Selected;
    const bool hovered = opt.state & QStyle::State_MouseOver;
    if (selected) {
      painter->fillRect(opt.rect, selected_);
    } else if (hovered) {
      painter->fillRect(opt.rect, hover_);
    }

    auto *tree = qobject_cast<const QTreeView *>(opt.widget);
    auto *fs = tree ? qobject_cast<const QFileSystemModel *>(tree->model()) : nullptr;
    const bool isDir = fs && fs->isDir(index);

    constexpr int kIcon = 14;
    constexpr int kChevron = 10;
    constexpr int kGap = 3;

    QRect r = opt.rect;
    int x = r.left();

    if (isDir) {
      const bool open = tree && tree->isExpanded(index);
      painter->save();
      painter->setRenderHint(QPainter::Antialiasing, true);
      QPen pen(muted_, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
      painter->setPen(pen);
      const qreal cx = x + kChevron * 0.5;
      const qreal cy = r.center().y();
      if (open) {
        // v
        painter->drawLine(QPointF(cx - 3.2, cy - 1.2), QPointF(cx, cy + 2.0));
        painter->drawLine(QPointF(cx, cy + 2.0), QPointF(cx + 3.2, cy - 1.2));
      } else {
        // >
        painter->drawLine(QPointF(cx - 1.2, cy - 3.2), QPointF(cx + 2.0, cy));
        painter->drawLine(QPointF(cx + 2.0, cy), QPointF(cx - 1.2, cy + 3.2));
      }
      painter->restore();
      x += kChevron + kGap;
    } else if (!opt.icon.isNull()) {
      const QPixmap pm = opt.icon.pixmap(
        QSize(kIcon, kIcon), (opt.state & QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled);
      const int y = r.top() + (r.height() - kIcon) / 2;
      painter->drawPixmap(x, y, pm);
      x += kIcon + kGap;
    }

    // Keep label readable on gray selection (never system HighlightedText/white).
    painter->setPen(text_);
    painter->setFont(opt.font);
    painter->drawText(QRect(x, r.top(), r.right() - x, r.height()),
                      Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, opt.text);
  }

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
  {
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    s.setHeight(22);
    return s;
  }

private:
  QColor text_{QStringLiteral("#333333")};
  QColor muted_{QStringLiteral("#6e6e6e")};
  QColor hover_{QStringLiteral("#e8e8e8")};
  QColor selected_{QStringLiteral("#e8e8e8")};
};

// Own folder expand/collapse on press so QTreeView's branch handler cannot
// toggle again (that expand-then-collapse race felt like a frozen click).
class ProjectTreeView : public QTreeView
{
public:
  using QTreeView::QTreeView;

  std::function<void(const QString& path, bool expanding)> folderToggleHook;

protected:
  void mousePressEvent(QMouseEvent *event) override
  {
    if (event->button() == Qt::LeftButton) {
      const QModelIndex index = indexAt(event->pos());
      auto *fs = qobject_cast<QFileSystemModel *>(model());
      if (index.isValid() && fs && fs->isDir(index)) {
        if (selectionModel()) {
          selectionModel()->setCurrentIndex(
            index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        const bool expanding = !isExpanded(index);
        if (folderToggleHook) {
          folderToggleHook(QDir::cleanPath(fs->filePath(index)), expanding);
        }
        setExpanded(index, expanding);
        if (expanding && fs->canFetchMore(index)) {
          fs->fetchMore(index);
        }
        event->accept();
        return;
      }
    }
    QTreeView::mousePressEvent(event);
  }
};

QString inputDialogStyle(bool dark)
{
  if (dark) {
    return QStringLiteral(R"(
      QInputDialog, QMessageBox {
        background: #252526;
      }
      QLabel {
        color: #cccccc;
        font-size: 12px;
        padding: 0 0 4px 0;
      }
      QLineEdit {
        min-height: 28px;
        padding: 4px 8px;
        font-size: 12px;
        color: #cccccc;
        background: #1e1e1e;
        border: 1px solid #3c3c3c;
        border-radius: 4px;
        selection-background-color: #094771;
      }
      QLineEdit:focus {
        border: 1px solid #0078d4;
      }
      QPushButton {
        min-width: 72px;
        min-height: 26px;
        padding: 4px 14px;
        font-size: 12px;
        border-radius: 4px;
        border: 1px solid #3c3c3c;
        background: #2d2d2d;
        color: #cccccc;
      }
      QPushButton:hover {
        background: #3a3a3a;
      }
      QPushButton:default {
        background: #0e639c;
        border: 1px solid #0e639c;
        color: #ffffff;
      }
      QPushButton:default:hover {
        background: #1177bb;
      }
    )");
  }
  return QStringLiteral(R"(
    QInputDialog, QMessageBox {
      background: #f8f8f8;
    }
    QLabel {
      color: #333333;
      font-size: 12px;
      padding: 0 0 4px 0;
    }
    QLineEdit {
      min-height: 28px;
      padding: 4px 8px;
      font-size: 12px;
      color: #333333;
      background: #ffffff;
      border: 1px solid #d0d0d0;
      border-radius: 4px;
      selection-background-color: #cce0ff;
    }
    QLineEdit:focus {
      border: 1px solid #0078d4;
    }
    QPushButton {
      min-width: 72px;
      min-height: 26px;
      padding: 4px 14px;
      font-size: 12px;
      border-radius: 4px;
      border: 1px solid #d0d0d0;
      background: #f3f3f3;
      color: #333333;
    }
    QPushButton:hover {
      background: #e8e8e8;
    }
    QPushButton:default {
      background: #0078d4;
      border: 1px solid #0078d4;
      color: #ffffff;
    }
    QPushButton:default:hover {
      background: #106ebe;
    }
  )");
}

QString promptText(QWidget *parent, const QString& title, const QString& label, const QString& text,
                   bool *ok)
{
  QInputDialog dlg(parent);
  dlg.setWindowTitle(title);
  dlg.setLabelText(label);
  dlg.setTextValue(text);
  dlg.setInputMode(QInputDialog::TextInput);
  dlg.setOkButtonText(_("OK"));
  dlg.setCancelButtonText(_("Cancel"));
  dlg.setStyleSheet(inputDialogStyle(isDarkMode()));
  dlg.setMinimumSize(340, 140);
  dlg.resize(380, 150);
  const int result = dlg.exec();
  if (ok) *ok = (result == QDialog::Accepted);
  return dlg.textValue();
}

int askYesNo(QWidget *parent, const QString& title, const QString& text)
{
  QMessageBox box(parent);
  box.setWindowTitle(title);
  box.setText(text);
  box.setIcon(QMessageBox::Question);
  box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  box.setDefaultButton(QMessageBox::No);
  box.setStyleSheet(inputDialogStyle(isDarkMode()));
  box.setMinimumWidth(320);
  return box.exec();
}

QString explorerContextMenuStyle(bool dark)
{
  if (dark) {
    return QStringLiteral(R"(
      QMenu#projectExplorerMenu {
        background: #252526;
        border: 1px solid #3a3a3a;
        border-radius: 6px;
        padding: 4px;
        min-width: 180px;
      }
      QMenu#projectExplorerMenu::item {
        padding: 5px 14px 5px 8px;
        margin: 0px 1px;
        border-radius: 4px;
        color: #cccccc;
        font-size: 12px;
        min-height: 20px;
      }
      QMenu#projectExplorerMenu::item:selected {
        background: #094771;
        color: #ffffff;
      }
      QMenu#projectExplorerMenu::item:disabled {
        color: #6a6a6a;
        background: transparent;
      }
      QMenu#projectExplorerMenu::icon {
        padding-left: 2px;
        width: 14px;
        height: 14px;
      }
      QMenu#projectExplorerMenu::separator {
        height: 1px;
        background: #333333;
        margin: 4px 8px;
      }
    )");
  }
  return QStringLiteral(R"(
    QMenu#projectExplorerMenu {
      background: #ffffff;
      border: 1px solid #e6e6e6;
      border-radius: 6px;
      padding: 4px;
      min-width: 180px;
    }
    QMenu#projectExplorerMenu::item {
      padding: 5px 14px 5px 8px;
      margin: 0px 1px;
      border-radius: 4px;
      color: #333333;
      font-size: 12px;
      min-height: 20px;
    }
    QMenu#projectExplorerMenu::item:selected {
      background: #eef5ff;
      color: #1a1a1a;
    }
    QMenu#projectExplorerMenu::item:disabled {
      color: #a0a0a0;
      background: transparent;
    }
    QMenu#projectExplorerMenu::icon {
      padding-left: 2px;
      width: 14px;
      height: 14px;
    }
    QMenu#projectExplorerMenu::separator {
      height: 1px;
      background: #ededed;
      margin: 4px 8px;
    }
  )");
}

QAction *addMenuAction(QMenu *menu, const QString& text, const QString& iconName)
{
  auto *action = menu->addAction(text);
  if (!iconName.isEmpty()) {
    action->setIcon(QIcon::fromTheme(iconName));
  }
  return action;
}

}  // namespace

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

  model_ = new ProjectFileSystemModel(this);
  model_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
  model_->setReadOnly(false);
  iconProvider_ = new ProjectFileIconProvider();
  iconProvider_->setDarkMode(isDarkMode());
  model_->setIconProvider(iconProvider_);

  tree_ = new ProjectTreeView(treePage_);
  tree_->setObjectName(QStringLiteral("projectExplorerTree"));
  tree_->setModel(model_);
  tree_->setItemDelegate(new ProjectTreeDelegate(tree_));
  tree_->setHeaderHidden(true);
  tree_->setAnimated(false);
  // Nesting only — chevrons are painted in the delegate, not a branch column.
  tree_->setIndentation(8);
  tree_->setIconSize(QSize(14, 14));
  tree_->setUniformRowHeights(true);
  tree_->setRootIsDecorated(false);
  tree_->setItemsExpandable(true);
  tree_->setExpandsOnDoubleClick(false);
  tree_->setContextMenuPolicy(Qt::CustomContextMenu);
  // Rename only via the context-menu dialog, never inline on the tree.
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  // Hide size/type/date columns
  for (int c = 1; c < model_->columnCount(); ++c) {
    tree_->hideColumn(c);
  }

  auto *projectTree = static_cast<ProjectTreeView *>(tree_);
  projectTree->folderToggleHook = [this](const QString& path, bool expanding) {
    if (expanding) {
      pendingExpandPaths_.insert(path);
    } else {
      pendingExpandPaths_.remove(path);
    }
  };

  connect(tree_, &QTreeView::expanded, this, &ProjectExplorer::onTreeExpanded);
  connect(tree_, &QTreeView::collapsed, this, &ProjectExplorer::onTreeCollapsed);
  connect(model_, &QFileSystemModel::directoryLoaded, this, &ProjectExplorer::onDirectoryLoaded);
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

  const QString text = dark ? QStringLiteral("#cccccc") : QStringLiteral("#333333");
  const QString muted = dark ? QStringLiteral("#969696") : QStringLiteral("#6e6e6e");
  const QString hover = dark ? QStringLiteral("#2a2d2e") : QStringLiteral("#e8e8e8");
  // Same as hover — gray selection, never blue / white-on-blue.
  const QString selected = hover;
  if (auto *delegate = dynamic_cast<ProjectTreeDelegate *>(tree_->itemDelegate())) {
    delegate->setChrome(QColor(text), QColor(muted), QColor(hover), QColor(selected));
  }

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
      show-decoration-selected: 0;
    }
    QTreeView#projectExplorerTree::item {
      min-height: 22px;
      height: 22px;
      padding: 0px;
    }
    QTreeView#projectExplorerTree::branch {
      background: transparent;
      border-image: none;
      image: none;
    }
    QTreeView#projectExplorerTree::item:hover,
    QTreeView#projectExplorerTree::item:selected,
    QTreeView#projectExplorerTree::item:selected:active,
    QTreeView#projectExplorerTree::item:selected:!active {
      background: %6;
      color: %4;
    }
  )")
                  .arg(dark ? QStringLiteral("#1e1e1e") : QStringLiteral("#f8f8f8"),
                       dark ? QStringLiteral("#2b2b2b") : QStringLiteral("#e5e5e5"),
                       dark ? QStringLiteral("#252526") : QStringLiteral("#f3f3f3"), text, muted,
                       hover));
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
    pendingExpandPaths_.clear();
    return;
  }
  titleLabel_->setText(pm.projectName().toUpper());
  pendingExpandPaths_.clear();
  const QString root = pm.rootPath();
  const QModelIndex rootIndex = model_->setRootPath(root);
  tree_->setRootIndex(rootIndex);
  // Prefetch top-level folder contents so the first expand is synchronous-feeling.
  if (model_->canFetchMore(rootIndex)) {
    model_->fetchMore(rootIndex);
  }
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

void ProjectExplorer::onTreeExpanded(const QModelIndex& index)
{
  if (!index.isValid()) return;
  const QString path = QDir::cleanPath(model_->filePath(index));
  pendingExpandPaths_.insert(path);
  if (model_->canFetchMore(index)) {
    model_->fetchMore(index);
  }
  // Re-assert after the model finishes its layout churn on the next tick.
  QTimer::singleShot(0, this, [this, path]() {
    if (!pendingExpandPaths_.contains(path)) return;
    const QModelIndex idx = model_->index(path);
    if (idx.isValid() && !tree_->isExpanded(idx)) {
      tree_->setExpanded(idx, true);
    }
  });
}

void ProjectExplorer::onTreeCollapsed(const QModelIndex& index)
{
  if (!index.isValid()) return;
  pendingExpandPaths_.remove(QDir::cleanPath(model_->filePath(index)));
}

void ProjectExplorer::onDirectoryLoaded(const QString& path)
{
  const QString clean = QDir::cleanPath(path);
  const QModelIndex idx = model_->index(clean);
  if (!idx.isValid()) return;

  // Prefetch children of directories so expand does not wait on the first click.
  for (int i = 0; i < model_->rowCount(idx); ++i) {
    const QModelIndex child = model_->index(i, 0, idx);
    if (model_->isDir(child) && model_->canFetchMore(child)) {
      model_->fetchMore(child);
    }
  }

  if (!pendingExpandPaths_.contains(clean)) return;
  if (!tree_->isExpanded(idx)) {
    tree_->setExpanded(idx, true);
  }
}

void ProjectExplorer::onDoubleClicked(const QModelIndex& index)
{
  if (!index.isValid()) return;
  const QString path = absolutePathForIndex(index);
  if (QFileInfo(path).isDir()) return;
  emit openFileRequested(path);
}

void ProjectExplorer::onCustomContextMenu(const QPoint& pos)
{
  auto& pm = ProjectManager::instance();
  if (!pm.hasProject()) return;

  const QModelIndex index = tree_->indexAt(pos);
  QString targetDir = pm.rootPath();
  QString targetFile;
  bool isScadFile = false;
  if (index.isValid()) {
    const QString path = absolutePathForIndex(index);
    QFileInfo info(path);
    if (info.isDir()) {
      targetDir = path;
    } else {
      targetFile = path;
      targetDir = info.absolutePath();
      isScadFile = info.suffix().compare(QLatin1String("scad"), Qt::CaseInsensitive) == 0;
    }
  }

  QMenu menu(this);
  menu.setObjectName(QStringLiteral("projectExplorerMenu"));
  // Fusion avoids macOS native menu chrome stacking a second thick border
  // on top of the stylesheet edge.
  if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
    menu.setStyle(fusion);
  }
  menu.setWindowFlags(menu.windowFlags() | Qt::NoDropShadowWindowHint);
  menu.setAttribute(Qt::WA_TranslucentBackground, false);
  menu.setStyleSheet(explorerContextMenuStyle(isDarkMode()));

  QAction *render = nullptr;
  if (isScadFile) {
    render = addMenuAction(&menu, _("Render"), QStringLiteral("chokusen-render"));
    menu.addSeparator();
  }

  QAction *newFile =
    addMenuAction(&menu, _("New File…"), QStringLiteral("chokusen-new"));
  QAction *newFolder =
    addMenuAction(&menu, _("New Folder…"), QStringLiteral("chokusen-folder"));
  menu.addSeparator();
  QAction *rename =
    addMenuAction(&menu, _("Rename…"), QStringLiteral("chokusen-file"));
  QAction *remove =
    addMenuAction(&menu, _("Delete…"), QStringLiteral("chokusen-recycle"));
  menu.addSeparator();
  QAction *reveal =
    addMenuAction(&menu, _("Reveal in Finder"), QStringLiteral("chokusen-open"));

  const bool protectedItem = index.isValid() && isProtectedPath(absolutePathForIndex(index));
  rename->setEnabled(index.isValid() && !protectedItem);
  remove->setEnabled(index.isValid() && !protectedItem);

  QAction *chosen = menu.exec(tree_->viewport()->mapToGlobal(pos));
  if (!chosen) return;

  if (render && chosen == render) {
    emit renderFileRequested(targetFile);
  } else if (chosen == newFile) {
    bool ok = false;
    const QString name =
      promptText(this, _("New File"), _("File name:"), QStringLiteral("untitled.scad"), &ok);
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
    const QString name =
      promptText(this, _("New Folder"), _("Folder name:"), QStringLiteral("folder"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    if (name.contains(QLatin1String(".."))) return;
    QDir(targetDir).mkpath(name.trimmed());
  } else if (chosen == rename && index.isValid()) {
    const QString path = absolutePathForIndex(index);
    QFileInfo info(path);
    bool ok = false;
    const QString name = promptText(this, _("Rename"), _("New name:"), info.fileName(), &ok);
    if (!ok || name.trimmed().isEmpty() || name.contains(QLatin1String(".."))) return;
    const QString dest = info.dir().filePath(name.trimmed());
    QFile::rename(path, dest);
  } else if (chosen == remove && index.isValid()) {
    const QString path = absolutePathForIndex(index);
    const auto answer = askYesNo(this, _("Delete"),
                                 tr("Delete \"%1\"?").arg(QFileInfo(path).fileName()));
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
