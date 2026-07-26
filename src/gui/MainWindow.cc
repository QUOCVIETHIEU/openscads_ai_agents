/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "gui/MainWindow.h"

#include <sys/stat.h>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDropEvent>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QList>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QMutexLocker>
#include <QPalette>
#include <QPoint>
#include <QProcess>
#include <QProgressDialog>
#include <QScreen>
#include <QSettings>  //Include QSettings for direct operations on settings arrays
#include <QSignalMapper>
#include <QSoundEffect>
#include <QSplitter>
#include <QStackedWidget>
#include <QAbstractItemModel>
#include <QStatusBar>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QStyle>
#include <QStyleFactory>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSizePolicy>
#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QString>
#include <algorithm>
#include <boost/range/adaptor/reversed.hpp>
#include <boost/version.hpp>
#include <cassert>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "openscad_gui.h"
#include "core/AST.h"
#include "core/BuiltinContext.h"
#include "core/Builtins.h"
#include "core/CSGNode.h"
#include "core/Context.h"
#include "core/EvaluationSession.h"
#include "core/Expression.h"
#include "core/RenderVariables.h"
#include "core/ScopeContext.h"
#include "core/Settings.h"
#include "core/SourceFileCache.h"
#include "core/customizer/CommentParser.h"
#include "core/node.h"
#include "core/parsersettings.h"
#include "core/progress.h"
#include "geometry/Geometry.h"
#include "geometry/GeometryCache.h"
#include "geometry/GeometryEvaluator.h"
#include "glview/PolySetRenderer.h"
#include "glview/RenderSettings.h"
#if not defined(USE_POLYSET_FOR_CGAL)
#include "glview/cgal/CGALRenderer.h"
#endif
#include "glview/preview/CSGTreeNormalizer.h"
#include "glview/preview/ThrownTogetherRenderer.h"
#include "gui/AboutDialog.h"
#include "gui/CGALWorker.h"
#include "gui/ColorList.h"
#include "gui/Dock.h"
#include "gui/BottomPanelHeader.h"
#include "gui/Console.h"
#include "gui/ai/AIDock.h"
#include "gui/ai/ChatWidget.h"
#include "gui/project/ProjectManager.h"
#include "gui/project/ProjectExplorer.h"
#include "gui/Editor.h"
#include "gui/Export3mfDialog.h"
#include "gui/ExportPdfDialog.h"
#include "gui/ExportSvgDialog.h"
#include "gui/ExternalToolInterface.h"
#include "gui/ImportUtils.h"
#include "gui/LibraryInfoDialog.h"
#include "gui/Measurement.h"
#include "gui/OpenSCADApp.h"
#include "gui/Preferences.h"
#include "Feature.h"
#include "gui/PrintInitDialog.h"
#include "gui/ProgressWidget.h"
#include "gui/QGLView.h"
#include "gui/QSettingsCached.h"
#include "gui/QWordSearchField.h"
#include "gui/ScintillaEditor.h"
#include "gui/SettingsWriter.h"
#include "gui/TabManager.h"
#include "gui/UIUtils.h"
#include "gui/input/InputDriverEvent.h"
#include "gui/input/InputDriverManager.h"
#include "io/dxfdim.h"
#include "io/export.h"
#include "io/fileutils.h"
#include "openscad.h"
#include "platform/PlatformUtils.h"
#include "utils/exceptions.h"
#include "utils/printutils.h"
#include "version.h"

#ifdef ENABLE_CGAL
#include "geometry/cgal/CGALCache.h"
#include "geometry/cgal/CGALNefGeometry.h"
#include "geometry/cgal/cgal.h"
#endif  // ENABLE_CGAL
#ifdef ENABLE_MANIFOLD
#include "geometry/manifold/ManifoldGeometry.h"
#include "geometry/manifold/manifoldutils.h"
#endif  // ENABLE_MANIFOLD
#ifdef ENABLE_OPENCSG
#include <opencsg.h>

#include "core/CSGTreeEvaluator.h"
#include "glview/preview/OpenCSGRenderer.h"
#endif
#ifdef OPENSCAD_UPDATER
#include "gui/AutoUpdater.h"
#endif

#ifdef ENABLE_PYTHON
#include "nettle/base64.h"
#include "nettle/sha2.h"
#include "python/python_public.h"

std::string SHA256HashString(std::string aString)
{
  uint8_t digest[SHA256_DIGEST_SIZE];
  sha256_ctx sha256_ctx;

  sha256_init(&sha256_ctx);
  sha256_update(&sha256_ctx, aString.length(), (uint8_t *)aString.c_str());
  sha256_digest(&sha256_ctx, SHA256_DIGEST_SIZE, digest);

  base64_encode_ctx base64_ctx;
  char digest_base64[BASE64_ENCODE_LENGTH(SHA256_DIGEST_SIZE) + 1];
  memset(digest_base64, 0, sizeof(digest_base64));

  base64_encode_init(&base64_ctx);
  base64_encode_update(&base64_ctx, digest_base64, SHA256_DIGEST_SIZE, digest);
  base64_encode_final(&base64_ctx, digest_base64);
  return digest_base64;
}

#endif  // ifdef ENABLE_PYTHON

#include "gui/PrintService.h"
#include "input/MouseConfigWidget.h"

// Global application state
unsigned int GuiLocker::guiLocked = 0;

bool MainWindow::undockMode = false;
bool MainWindow::reorderMode = false;
const int MainWindow::tabStopWidth = 15;
QElapsedTimer *MainWindow::progressThrottle = new QElapsedTimer();

namespace {

const int autoReloadPollingPeriodMS = 200;

struct DockFocus {
  Dock *widget;
  std::function<void(MainWindow *)> focus;
};

QAction *findAction(const QList<QAction *>& actions, const std::string& name)
{
  for (const auto action : actions) {
    if (action->objectName().toStdString() == name) {
      return action;
    }
    if (action->menu()) {
      auto foundAction = findAction(action->menu()->actions(), name);
      if (foundAction) return foundAction;
    }
  }
  return nullptr;
}

void fileExportedMessage(const QString& format, const QString& filename)
{
  LOG("%1$s export finished: %2$s", format.toUtf8().constData(), filename.toUtf8().constData());
}

void removeExportActions(QToolBar *toolbar, QAction *action)
{
  int idx = toolbar->actions().indexOf(action);
  while (idx > 0) {
    QAction *a = toolbar->actions().at(idx - 1);
    if (a->objectName().isEmpty())  // separator
      break;
    toolbar->removeAction(a);
    idx--;
  }
}

constexpr int kEditorActivityBarW = 32;
constexpr int kWorkbenchHeaderH = 32;

QIcon projectActivityIcon()
{
  return QIcon::fromTheme(QStringLiteral("ico_folder"));
}

QIcon editorActivityIcon()
{
  return QIcon::fromTheme(QStringLiteral("ico_editor"));
}

std::unique_ptr<ExternalToolInterface> createExternalToolService(print_service_t serviceType,
                                                                 const QString& serviceName,
                                                                 FileFormat fileFormat)
{
  switch (serviceType) {
  case print_service_t::NONE:
    // TODO: Print warning
    return nullptr;
    break;
  case print_service_t::PRINT_SERVICE: {
    if (const auto printService = PrintService::getPrintService(serviceName.toStdString())) {
      return createExternalPrintService(printService, fileFormat);
    }
    LOG("Unknown print service \"%1$s\"", serviceName.toStdString());
    return nullptr;
    break;
  }
  case print_service_t::OCTOPRINT:         return createOctoPrintService(fileFormat); break;
  case print_service_t::LOCAL_APPLICATION: return createLocalProgramService(fileFormat); break;
  }
  return {};
}

}  // namespace

MainWindow::MainWindow(const QStringList& filenames) : rubberBandManager(this)
{
  // Main UI setup
  setupWindow();
  setupMenusAndActions();

  // Workers, timers etc.
  // Set up early as some reactions to GUI state changes may trigger
  setupCoreSubsystems();

  // Docks
  setupConsole();
  auto guard = scopedSetCurrentOutput();

  setupStatusBar();
  setupAnimate();
  setupEditor(filenames);
  setupCustomizer();
  setupErrorLog();
  setupFontList();
  setupColorList();
  setupAIDock();
  setupDocks();

  setup3DView();
  setupViewportControl();
  setupInput();
  setupPreferences();

  restoreWindowState();

  this->hideFind();
  show();
  openRemainingFiles(filenames);

  // Restore the last AI project when launching without explicit files.
  if (filenames.isEmpty() || (filenames.size() == 1 && filenames.first().isEmpty())) {
    const QStringList recent = ProjectManager::instance().recentProjects();
    if (!recent.isEmpty()) {
      QString err;
      if (ProjectManager::instance().openProject(recent.first(), &err)) {
        const QString target = ProjectManager::instance().aiTargetFile();
        if (!target.isEmpty() && tabManager) tabManager->open(target);
        updateRecentProjectActions();
      }
    }
  }
}

void MainWindow::setAllMouseViewActions()
{
  // Set the mouse actions to those held in the settings.
  this->qglview->setMouseActions(MouseConfig::MouseAction::LEFT_CLICK,
                                 MouseConfig::viewActionArrays.at(static_cast<MouseConfig::ViewAction>(
                                   Settings::Settings::inputMouseLeftClick.value())));
  this->qglview->setMouseActions(MouseConfig::MouseAction::MIDDLE_CLICK,
                                 MouseConfig::viewActionArrays.at(static_cast<MouseConfig::ViewAction>(
                                   Settings::Settings::inputMouseMiddleClick.value())));
  this->qglview->setMouseActions(MouseConfig::MouseAction::RIGHT_CLICK,
                                 MouseConfig::viewActionArrays.at(static_cast<MouseConfig::ViewAction>(
                                   Settings::Settings::inputMouseRightClick.value())));
  this->qglview->setMouseActions(MouseConfig::MouseAction::SHIFT_LEFT_CLICK,
                                 MouseConfig::viewActionArrays.at(static_cast<MouseConfig::ViewAction>(
                                   Settings::Settings::inputMouseShiftLeftClick.value())));
  this->qglview->setMouseActions(MouseConfig::MouseAction::SHIFT_MIDDLE_CLICK,
                                 MouseConfig::viewActionArrays.at(static_cast<MouseConfig::ViewAction>(
                                   Settings::Settings::inputMouseShiftMiddleClick.value())));
  this->qglview->setMouseActions(MouseConfig::MouseAction::SHIFT_RIGHT_CLICK,
                                 MouseConfig::viewActionArrays.at(static_cast<MouseConfig::ViewAction>(
                                   Settings::Settings::inputMouseShiftRightClick.value())));
  this->qglview->setMouseActions(MouseConfig::MouseAction::CTRL_LEFT_CLICK,
                                 MouseConfig::viewActionArrays.at(static_cast<MouseConfig::ViewAction>(
                                   Settings::Settings::inputMouseCtrlLeftClick.value())));
  this->qglview->setMouseActions(MouseConfig::MouseAction::CTRL_MIDDLE_CLICK,
                                 MouseConfig::viewActionArrays.at(static_cast<MouseConfig::ViewAction>(
                                   Settings::Settings::inputMouseCtrlMiddleClick.value())));
  this->qglview->setMouseActions(MouseConfig::MouseAction::CTRL_RIGHT_CLICK,
                                 MouseConfig::viewActionArrays.at(static_cast<MouseConfig::ViewAction>(
                                   Settings::Settings::inputMouseCtrlRightClick.value())));
  this->qglview->setMouseActions(MouseConfig::MouseAction::CTRL_SHIFT_LEFT_CLICK,
                                 MouseConfig::viewActionArrays.at(static_cast<MouseConfig::ViewAction>(
                                   Settings::Settings::inputMouseCtrlShiftLeftClick.value())));
  this->qglview->setMouseActions(MouseConfig::MouseAction::CTRL_SHIFT_MIDDLE_CLICK,
                                 MouseConfig::viewActionArrays.at(static_cast<MouseConfig::ViewAction>(
                                   Settings::Settings::inputMouseCtrlShiftMiddleClick.value())));
  this->qglview->setMouseActions(MouseConfig::MouseAction::CTRL_SHIFT_RIGHT_CLICK,
                                 MouseConfig::viewActionArrays.at(static_cast<MouseConfig::ViewAction>(
                                   Settings::Settings::inputMouseCtrlShiftRightClick.value())));
}

void MainWindow::onNavigationOpenContextMenu()
{
  navigationMenu->exec(QCursor::pos());
}

void MainWindow::onNavigationCloseContextMenu()
{
  rubberBandManager.hide();
}

void MainWindow::onNavigationTriggerContextMenuEntry()
{
  auto *action = qobject_cast<QAction *>(sender());
  if (!action || !action->property("id").isValid()) return;

  Dock *dock = action->property("id").value<Dock *>();
  assert(dock != nullptr);

  dock->show();
  dock->raise();
  dock->setFocus();

  // Forward the focus on the content of the tabmanager
  if (dock == editorDock) {
    tabManager->setFocus();
  }
}

void MainWindow::onNavigationHoveredContextMenuEntry()
{
  auto *action = qobject_cast<QAction *>(sender());
  if (!action || !action->property("id").isValid()) return;

  Dock *dock = action->property("id").value<Dock *>();
  assert(dock != nullptr);

  // Hover signal is emitted at each mouse move, to avoid excessive
  // load we only raise/emphasize if it is not yet done.
  if (rubberBandManager.isEmphasized(dock)) return;

  dock->raise();
  rubberBandManager.emphasize(dock);
}

void MainWindow::addExportActions(QToolBar *toolbar, QAction *action) const
{
  for (const std::string& identifier :
       {Settings::Settings::toolbarExport3D.value(), Settings::Settings::toolbarExport2D.value()}) {
    QAction *exportAction = formatIdentifierToAction(identifier);
    if (exportAction) {
      toolbar->insertAction(action, exportAction);
    }
  }
  // Always offer quick 2D drawing PDF next to the configured export formats
  if (this->fileActionExportDrawingPDF) {
    toolbar->insertAction(action, this->fileActionExportDrawingPDF);
  }
}

void MainWindow::updateExportActions()
{
  removeExportActions(editortoolbar, this->designAction3DPrint);
  addExportActions(editortoolbar, this->designAction3DPrint);

  // handle the hide/show of export action in view toolbar according to the visibility of editor dock
  removeExportActions(viewerToolBar, this->viewActionViewAll);
  if (!editorDock->isVisible()) {
    addExportActions(viewerToolBar, this->viewActionViewAll);
  }
}

void MainWindow::openFileFromPath(const QString& path, int line)
{
  if (editorDock->isVisible()) {
    auto guard = scopedSetCurrentOutput();
    activeEditor->setFocus();
    if (!path.isEmpty()) tabManager->open(path);
    activeEditor->setFocus();
    activeEditor->setCursorPosition(line, 0);
  }
}

void MainWindow::addKeyboardShortCut(const QList<QAction *>& actions)
{
  for (auto& action : actions) {
    // prevent adding shortcut twice if action is added to multiple toolbars
    if (action->toolTip().contains("&nbsp;")) {
      continue;
    }

    const QString shortCut(action->shortcut().toString(QKeySequence::NativeText));
    if (shortCut.isEmpty()) {
      continue;
    }

    const QString toolTip(
      "%1 &nbsp;<span style=\"color: gray; font-size: small; font-style: italic\">%2</span>");
    action->setToolTip(toolTip.arg(action->toolTip(), shortCut));
  }
}

void MainWindow::onAxisChanged(InputEventAxisChanged *)
{
}

void MainWindow::onButtonChanged(InputEventButtonChanged *)
{
}

void MainWindow::onTranslateEvent(InputEventTranslate *event)
{
  const double zoomFactor = 0.001 * qglview->cam.zoomValue();

  if (event->viewPortRelative) {
    qglview->translate(event->x, event->y, event->z, event->relative, true);
  } else {
    qglview->translate(zoomFactor * event->x, event->y, zoomFactor * event->z, event->relative, false);
  }
}

void MainWindow::onRotateEvent(InputEventRotate *event)
{
  qglview->rotate(event->x, event->y, event->z, event->relative);
}

void MainWindow::onRotate2Event(InputEventRotate2 *event)
{
  qglview->rotate2(event->x, event->y, event->z);
}

void MainWindow::onActionEvent(InputEventAction *event)
{
  const std::string actionName = event->action;
  if (actionName.find("::") == std::string::npos) {
    QAction *action = findAction(this->menuBar()->actions(), actionName);
    if (action) {
      action->trigger();
    } else if ("viewActionTogglePerspective" == actionName) {
      viewTogglePerspective();
    }
  } else {
    const std::string target = actionName.substr(0, actionName.find("::"));
    if (target == "animate") {
      this->animateWidget->onActionEvent(event);
    } else {
      std::cout << "unknown onActionEvent target: " << actionName << std::endl;
    }
  }
}

void MainWindow::onZoomEvent(InputEventZoom *event)
{
  qglview->zoom(event->zoom, event->relative);
}

void MainWindow::loadViewSettings()
{
  const QSettingsCached settings;

  if (settings.value("view/showEdges").toBool()) {
    viewActionShowEdges->setChecked(true);
  }
  if (settings.value("view/showAxes", true).toBool()) {
    viewActionShowAxes->setChecked(true);
  }
  if (settings.value("view/showFloor", true).toBool()) {
    viewActionShowFloor->setChecked(true);
  }
  if (settings.value("view/showCrosshairs").toBool()) {
    viewActionShowCrosshairs->setChecked(true);
  }
  if (settings.value("view/showScaleProportional", true).toBool()) {
    viewActionShowScaleProportional->setChecked(true);
  }
  viewTogglePerspective();

  // Editor toolbar (New/Open/Save/Undo/…) is intentionally removed from the UI —
  // those actions stay available via the File / Edit / Design menus.
  if (this->editortoolbar) {
    this->editortoolbar->hide();
  }
  if (this->viewActionHideEditorToolBar) {
    this->viewActionHideEditorToolBar->setVisible(false);
  }

  updateUndockMode(GlobalPreferences::inst()->getValue("advanced/undockableWindows").toBool());
  updateReorderMode(GlobalPreferences::inst()->getValue("advanced/reorderWindows").toBool());
}

void MainWindow::loadDesignSettings()
{
  const QSettingsCached settings;
  if (settings.value("design/autoReload", false).toBool()) {
    designActionAutoReload->setChecked(true);
  }
  auto polySetCacheSizeMB = GlobalPreferences::inst()->getValue("advanced/polysetCacheSizeMB").toUInt();
  GeometryCache::instance()->setMaxSizeMB(polySetCacheSizeMB);
  auto cgalCacheSizeMB = GlobalPreferences::inst()->getValue("advanced/cgalCacheSizeMB").toUInt();
  CGALCache::instance()->setMaxSizeMB(cgalCacheSizeMB);
  auto backend3D =
    GlobalPreferences::inst()->getValue("advanced/renderBackend3D").toString().toStdString();
  RenderSettings::inst()->backend3D =
    renderBackend3DFromString(backend3D).value_or(DEFAULT_RENDERING_BACKEND_3D);
}

void MainWindow::updateUndockMode(bool undockMode)
{
  MainWindow::undockMode = undockMode;
  if (undockMode) {
    editorDock->setFeatures(editorDock->features() | QDockWidget::DockWidgetFloatable);
    consoleDock->setFeatures(consoleDock->features() | QDockWidget::DockWidgetFloatable);
    parameterDock->setFeatures(parameterDock->features() | QDockWidget::DockWidgetFloatable);
    errorLogDock->setFeatures(errorLogDock->features() | QDockWidget::DockWidgetFloatable);
    animateDock->setFeatures(animateDock->features() | QDockWidget::DockWidgetFloatable);
    fontListDock->setFeatures(fontListDock->features() | QDockWidget::DockWidgetFloatable);
    colorListDock->setFeatures(colorListDock->features() | QDockWidget::DockWidgetFloatable);
    viewportControlDock->setFeatures(viewportControlDock->features() | QDockWidget::DockWidgetFloatable);
    aiDock->setFeatures(aiDock->features() | QDockWidget::DockWidgetFloatable);
  } else {
    if (editorDock->isFloating()) {
      editorDock->setFloating(false);
    }
    editorDock->setFeatures(editorDock->features() & ~QDockWidget::DockWidgetFloatable);

    if (consoleDock->isFloating()) {
      consoleDock->setFloating(false);
    }
    consoleDock->setFeatures(consoleDock->features() & ~QDockWidget::DockWidgetFloatable);

    if (parameterDock->isFloating()) {
      parameterDock->setFloating(false);
    }
    parameterDock->setFeatures(parameterDock->features() & ~QDockWidget::DockWidgetFloatable);

    if (errorLogDock->isFloating()) {
      errorLogDock->setFloating(false);
    }
    errorLogDock->setFeatures(errorLogDock->features() & ~QDockWidget::DockWidgetFloatable);

    if (animateDock->isFloating()) {
      animateDock->setFloating(false);
    }
    animateDock->setFeatures(animateDock->features() & ~QDockWidget::DockWidgetFloatable);

    if (fontListDock->isFloating()) {
      fontListDock->setFloating(false);
    }
    fontListDock->setFeatures(fontListDock->features() & ~QDockWidget::DockWidgetFloatable);

    if (colorListDock->isFloating()) {
      colorListDock->setFloating(false);
    }
    colorListDock->setFeatures(colorListDock->features() & ~QDockWidget::DockWidgetFloatable);

    if (viewportControlDock->isFloating()) {
      viewportControlDock->setFloating(false);
    }
    viewportControlDock->setFeatures(viewportControlDock->features() &
                                     ~QDockWidget::DockWidgetFloatable);

    if (aiDock->isFloating()) {
      aiDock->setFloating(false);
    }
    aiDock->setFeatures(aiDock->features() & ~QDockWidget::DockWidgetFloatable);
  }

  // AI-first layout: editor panel must stay open (no close button)
  editorDock->setFeatures(editorDock->features() & ~QDockWidget::DockWidgetClosable);
}

void MainWindow::updateReorderMode(bool reorderMode)
{
  MainWindow::reorderMode = reorderMode;
  for (auto& [dock, name] : docks) {
    // AI-first layout: hide Editor + AI Chat + Console dock title bars so chrome
    // matches VS Code (Console uses BottomPanelHeader instead).
    if (dock == editorDock || dock == aiDock || dock == consoleDock) {
      dock->setTitleBarVisibility(false);
      continue;
    }
    dock->setTitleBarVisibility(reorderMode);
  }
}

MainWindow::~MainWindow()
{
  delete this->cgalworker;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
  if (!tabManager->shouldClose()) {
    event->ignore();
    return;
  }
  event->accept();

  // Only save when this is the last MainWindow.
  if (scadApp->windowManager.getWindows().size() == 1) {
    saveWindowState();
  }

  isClosing = true;
  progress_report_fin();

  if (this->tempFile) {
    delete this->tempFile;
    this->tempFile = nullptr;
  }

  // Log to stdout from now on
  clearCurrentOutput();
  // Disable invokeMethod calls for consoleOutput during shutdown,
  // otherwise will segfault if echos are in progress.
  hideCurrentOutput();

  // Make sure all the floating docks are closed too as those
  // would stick around otherwise if the application keeps
  // running (after closing just a single window, or when
  // aborting the close process because of user cancellation).
  for (auto& [dock, title] : docks) {
    if (dock->isFloating()) {
      dock->close();
    }
  }

  scadApp->windowManager.remove(this);
  if (scadApp->windowManager.getWindows().empty()) {
    // Quit application even in case some other windows like
    // Preferences are still open.
    QApplication::quit();
  }
}

void MainWindow::saveWindowState()
{
  QSettingsCached settings;
  settings.setValue("window/geometry", saveGeometry());
  auto windowState = saveState();
  UIUtils::dumpSaveState(windowState);
  settings.setValue("window/state", windowState);
}

void MainWindow::showProgress()
{
  updateStatusBar(qobject_cast<ProgressWidget *>(sender()));
}

void MainWindow::report_func(const std::shared_ptr<const AbstractNode>&, void *vp, int mark)
{
  // limit to progress bar update calls to 5 per second
  static const qint64 MIN_TIMEOUT = 200;
  if (progressThrottle->hasExpired(MIN_TIMEOUT)) {
    progressThrottle->start();

    auto thisp = static_cast<MainWindow *>(vp);
    auto v = static_cast<int>((mark * 1000.0) / progress_report_count);
    auto permille = v < 1000 ? v : 999;
    if (permille > thisp->progresswidget->value()) {
      QMetaObject::invokeMethod(thisp->progresswidget, "setValue", Qt::QueuedConnection,
                                Q_ARG(int, permille));
      QApplication::processEvents();
    }

    // FIXME: Check if cancel was requested by e.g. Application quit
    if (thisp->progresswidget->wasCanceled()) throw ProgressCancelException();
  }
}

bool MainWindow::network_progress_func(const double permille)
{
  QMetaObject::invokeMethod(this->progresswidget, "setValue", Qt::QueuedConnection,
                            Q_ARG(int, (int)permille));
  return (progresswidget && progresswidget->wasCanceled());
}

void MainWindow::updateRecentFiles(const QString& FileSavedOrOpened)
{
  // Check that the canonical file path exists - only update recent files
  // if it does. Should prevent empty list items on initial open etc.
  QSettingsCached settings;  // already set up properly via main.cpp
  auto files = settings.value("recentFileList").toStringList();
  files.removeAll(FileSavedOrOpened);
  files.prepend(FileSavedOrOpened);
  while (files.size() > UIUtils::maxRecentFiles) files.removeLast();
  settings.setValue("recentFileList", files);

  for (auto& widget : QApplication::topLevelWidgets()) {
    auto mainWin = qobject_cast<MainWindow *>(widget);
    if (mainWin) {
      mainWin->updateRecentFileActions();
    }
  }
}

/*!
   compiles the design. Calls compileDone() if anything was compiled
 */
void MainWindow::compile(bool reload, bool forcedone)
{
  OpenSCAD::hardwarnings = GlobalPreferences::inst()->getValue("advanced/enableHardwarnings").toBool();
  OpenSCAD::traceDepth = GlobalPreferences::inst()->getValue("advanced/traceDepth").toUInt();
  OpenSCAD::traceUsermoduleParameters =
    GlobalPreferences::inst()->getValue("advanced/enableTraceUsermoduleParameters").toBool();
  OpenSCAD::parameterCheck =
    GlobalPreferences::inst()->getValue("advanced/enableParameterCheck").toBool();
  OpenSCAD::rangeCheck =
    GlobalPreferences::inst()->getValue("advanced/enableParameterRangeCheck").toBool();

  try {
    bool shouldcompiletoplevel = false;
    bool didcompile = false;

    compileErrors = 0;
    compileWarnings = 0;

    this->renderStatistic.start();

    // Reload checks the timestamp of the toplevel file and refreshes if necessary,
    if (reload) {
      // Refresh files if it has changed on disk
      if (fileChangedOnDisk() && checkEditorModified()) {
        shouldcompiletoplevel =
          tabManager->refreshDocument();  // don't compile if we couldn't open the file
        if (shouldcompiletoplevel &&
            GlobalPreferences::inst()->getValue("advanced/autoReloadRaise").toBool()) {
          // reloading the 'same' document brings the 'old' one to front.
          this->raise();
        }
      }
      // If the file has some content and there is no currently compiled content,
      // then we force the top level compilation.
      else {
        auto current_doc = activeEditor->toPlainText();
        if (current_doc.size() && lastCompiledDoc.size() == 0) {
          shouldcompiletoplevel = true;
        }
      }
    } else {
      shouldcompiletoplevel = true;
    }

    if (this->parsedFile) {
      auto mtime = this->parsedFile->includesChanged();
      if (mtime > this->includesMTime) {
        this->includesMTime = mtime;
        shouldcompiletoplevel = true;
      }
    }

    // Parsing and dependency handling must run to completion even with stop on errors to prevent auto
    // reload picking up where it left off, thwarting the stop, so we turn off exceptions in PRINT.
    no_exceptions_for_warnings();
    if (shouldcompiletoplevel) {
      initialize_rng();
      this->errorLogWidget->clearModel();
      if (GlobalPreferences::inst()->getValue("advanced/consoleAutoClear").toBool()) {
        this->console->clear();
      }
      if (activeEditor->isContentModified()) saveBackup();
      parseTopLevelDocument();
      didcompile = true;
    }

    if (didcompile && parser_error_pos != lastParserErrorPos) {
      if (lastParserErrorPos >= 0) emit unhighlightLastError();
      if (parser_error_pos >= 0) emit highlightError(parser_error_pos);
      lastParserErrorPos = parser_error_pos;
    }

    if (this->rootFile) {
      auto mtime = this->rootFile->handleDependencies();
      if (mtime > this->depsMTime) {
        this->depsMTime = mtime;
        LOG("Used file cache size: %1$d files", SourceFileCache::instance()->size());
        didcompile = true;
      }
    }

    // Had any errors in the parse that would have caused exceptions via PRINT.
    if (would_have_thrown()) throw HardWarningException("");
    // If we're auto-reloading, listen for a cascade of changes by starting a timer
    // if something changed _and_ there are any external dependencies
    if (reload && didcompile && this->rootFile) {
      if (this->rootFile->hasIncludes() || this->rootFile->usesLibraries()) {
        this->waitAfterReloadTimer->start();
        this->procevents = false;
        return;
      }
    }

    compileDone(didcompile | forcedone);
  } catch (const HardWarningException&) {
    exceptionCleanup();
  } catch (const std::exception& ex) {
    UnknownExceptionCleanup(ex.what());
  } catch (...) {
    UnknownExceptionCleanup();
  }
}

void MainWindow::waitAfterReload()
{
  no_exceptions_for_warnings();
  auto mtime = this->rootFile->handleDependencies();
  auto stop = would_have_thrown();
  if (mtime > this->depsMTime) this->depsMTime = mtime;
  else if (!stop) {
    compile(true, true);  // In case file itself or top-level includes changed during dependency updates
    return;
  }
  this->waitAfterReloadTimer->start();
}

void MainWindow::on_toolButtonCompileResultClose_clicked()
{
  frameCompileResult->hide();
}

void MainWindow::updateCompileResult()
{
  if ((compileErrors == 0) && (compileWarnings == 0)) {
    frameCompileResult->hide();
    return;
  }

  if (!Settings::Settings::showWarningsIn3dView.value()) {
    return;
  }

  QString msg;
  if (compileErrors > 0) {
    if (activeEditor->filepath.isEmpty()) {
      msg = QString(_("Compile error."));
    } else {
      const QFileInfo fileInfo(activeEditor->filepath);
      msg = QString(_("Error while compiling '%1'.")).arg(fileInfo.fileName());
    }
    toolButtonCompileResultIcon->setIcon(
      QIcon(QString::fromUtf8(":/icons/information-icons-error.png")));
  } else {
    const char *fmt = ngettext("Compilation generated %1 warning.", "Compilation generated %1 warnings.",
                               compileWarnings);
    msg = QString(fmt).arg(compileWarnings);
    toolButtonCompileResultIcon->setIcon(
      QIcon(QString::fromUtf8(":/icons/information-icons-warning.png")));
  }
  const QFontMetrics fm(labelCompileResultMessage->font());
  const int sizeIcon = std::max(12, std::min(32, fm.height()));
  const int sizeClose = std::max(10, std::min(32, fm.height()) - 4);
  toolButtonCompileResultIcon->setIconSize(QSize(sizeIcon, sizeIcon));
  toolButtonCompileResultClose->setIconSize(QSize(sizeClose, sizeClose));

  msg += _(
    R"( For details see the <a href="#errorlog">error log</a> and <a href="#console">console window</a>.)");
  labelCompileResultMessage->setText(msg);
  frameCompileResult->show();
}

void MainWindow::compileDone(bool didchange)
{
  OpenSCAD::hardwarnings = GlobalPreferences::inst()->getValue("advanced/enableHardwarnings").toBool();
  try {
    const char *callslot;
    if (didchange) {
      instantiateRoot();
      updateCompileResult();
      callslot = afterCompileSlot;
    } else {
      callslot = "compileEnded";
    }

    this->procevents = false;
    QMetaObject::invokeMethod(this, callslot);
  } catch (const HardWarningException&) {
    exceptionCleanup();
  }

  if (didchange) {
    const bool flagAutoCompleteIncludeVariables =
      GlobalPreferences::inst()->getValue("editor/autoCompleteIncludeVariables").toBool();
    const bool flagAutoCompleteIncludeModules =
      GlobalPreferences::inst()->getValue("editor/autoCompleteIncludeModules").toBool();
    const bool flagAutoCompleteIncludeFunctions =
      GlobalPreferences::inst()->getValue("editor/autoCompleteIncludeFunctions").toBool();

    const auto completionMode = Settings::SettingsAutoCompletion::autocompleteMode.value();

    auto *scintillaEditor = dynamic_cast<ScintillaEditor *>(this->activeEditor);

    if (scintillaEditor) {
      if (completionMode == "ParsedFileMode") {
        scintillaEditor->correctUserVarNamesForCompletionFromSourceFile(
          parsedFile.get(), flagAutoCompleteIncludeVariables, flagAutoCompleteIncludeModules,
          flagAutoCompleteIncludeFunctions);

      } else if (completionMode == "RegexInputTextMode") {
        scintillaEditor->correctUserVarNamesForCompletionFromInputText(flagAutoCompleteIncludeVariables,
                                                                       flagAutoCompleteIncludeModules,
                                                                       flagAutoCompleteIncludeFunctions);
      }
    }
  }
}

void MainWindow::compileEnded()
{
  clearCurrentOutput();
  GuiLocker::unlock();
  if (designActionAutoReload->isChecked()) autoReloadTimer->start();
#ifdef ENABLE_GUI_TESTS
  emit compilationDone(this->rootFile.get());
#endif
  if (aiRenderCompleteCallback) {
    const AIRenderResult result = collectAIRenderResult();
    aiRenderCapturing = false;
    aiRenderMessages.clear();
    auto cb = std::move(aiRenderCompleteCallback);
    aiRenderCompleteCallback = nullptr;
    // Defer so unlock/status updates settle before the AI turn finishes its UI.
    QTimer::singleShot(0, this, [cb = std::move(cb), result]() { cb(result); });
  }
}

AIRenderResult MainWindow::collectAIRenderResult()
{
  AIRenderResult r;
  r.errorCount = this->compileErrors;
  r.warningCount = this->compileWarnings;
  r.isPreview = this->isPreview;

  const auto geom = this->rootGeom;
  if (geom && !geom->isEmpty()) {
    r.empty = false;
    r.success = (this->compileErrors == 0);
    r.dimension = geom->getDimension();
    r.facets = geom->numFacets();
    const BoundingBox bb = geom->getBoundingBox();
    r.hasBoundingBox = true;
    r.bboxSize[0] = bb.sizes().x();
    r.bboxSize[1] = bb.sizes().y();
    r.bboxSize[2] = bb.sizes().z();
  } else if (this->qglview && this->qglview->getRenderer()) {
    // Preview (F5) has CSG products but usually no rootGeom mesh yet.
    const BoundingBox bb = this->qglview->getRenderer()->getBoundingBox();
    if (!bb.isEmpty()) {
      r.empty = false;
      r.success = (this->compileErrors == 0);
      r.dimension = 3;
      r.hasBoundingBox = true;
      r.bboxSize[0] = bb.sizes().x();
      r.bboxSize[1] = bb.sizes().y();
      r.bboxSize[2] = bb.sizes().z();
    } else {
      r.empty = true;
      r.success = false;
    }
  } else {
    r.empty = true;
    r.success = false;
  }

  std::string log;
  for (const auto& m : this->aiRenderMessages) {
    if (!log.empty()) log += "\n";
    log += m;
    if (log.size() > 2000) {
      log.resize(2000);
      log += "…";
      break;
    }
  }
  r.log = log;
  return r;
}

void MainWindow::startAIPreview(std::function<void(const AIRenderResult&)> onComplete)
{
  cancelAIFullRenderCallback();
  aiRenderCompleteCallback = std::move(onComplete);
  aiRenderCapturing = true;
  aiRenderMessages.clear();

  auto startPreview = [this]() {
    if (GuiLocker::isLocked()) {
      if (aiRenderCompleteCallback) {
        AIRenderResult result;
        result.isPreview = true;
        result.log = "Preview skipped: another compile was already in progress.";
        aiRenderCapturing = false;
        aiRenderMessages.clear();
        auto cb = std::move(aiRenderCompleteCallback);
        aiRenderCompleteCallback = nullptr;
        QTimer::singleShot(0, this, [cb = std::move(cb), result]() { cb(result); });
      }
      return;
    }
    // F5 — fast CSG preview for iteration / visual checks.
    on_designActionPreview_triggered();
  };

  QTimer::singleShot(0, this, startPreview);
}

void MainWindow::startAIFullRender(std::function<void(const AIRenderResult&)> onComplete)
{
  cancelAIFullRenderCallback();
  aiRenderCompleteCallback = std::move(onComplete);
  aiRenderCapturing = true;
  aiRenderMessages.clear();

  auto startRender = [this]() {
    if (GuiLocker::isLocked()) {
      // Another compile is in progress; finish the AI turn without hanging.
      if (aiRenderCompleteCallback) {
        AIRenderResult result;
        result.log = "Render skipped: another compile was already in progress.";
        aiRenderCapturing = false;
        aiRenderMessages.clear();
        auto cb = std::move(aiRenderCompleteCallback);
        aiRenderCompleteCallback = nullptr;
        QTimer::singleShot(0, this, [cb = std::move(cb), result]() { cb(result); });
      }
      return;
    }
    on_designActionRender_triggered();
  };

  // Let the editor apply text + layout settle before locking the GUI for F6.
  QTimer::singleShot(0, this, startRender);
}

void MainWindow::cancelAIFullRenderCallback()
{
  aiRenderCompleteCallback = nullptr;
  aiRenderCapturing = false;
  aiRenderMessages.clear();
}

#ifdef ENABLE_GUI_TESTS
std::shared_ptr<AbstractNode> MainWindow::instantiateRootFromSource(SourceFile *file)
{
  EvaluationSession session{file->getFullpath()};
  ContextHandle<BuiltinContext> builtin_context{Context::create<BuiltinContext>(&session)};
  setRenderVariables(builtin_context);

  std::shared_ptr<const FileContext> file_context;
  std::shared_ptr<AbstractNode> node = this->rootFile->instantiate(*builtin_context, &file_context);

  return node;
}
#endif  // ifdef ENABLE_GUI_TESTS

void MainWindow::instantiateRoot()
{
  // Go on and instantiate root_node, then call the continuation slot

  // Invalidate renderers before we kill the CSG tree
  this->qglview->setRenderer(nullptr);
#ifdef ENABLE_OPENCSG
  this->previewRenderer = nullptr;
#endif
  this->thrownTogetherRenderer = nullptr;

  // Remove previous CSG tree
  this->absoluteRootNode.reset();

  this->csgRoot.reset();
  this->normalizedRoot.reset();
  this->rootProduct.reset();

  this->rootNode.reset();
  this->tree.setRoot(nullptr);

  const std::filesystem::path doc(activeEditor->filepath.toStdString());
  this->tree.setDocumentPath(doc.parent_path().string());

  renderedEditor = activeEditor;

  if (this->rootFile) {
    // Evaluate CSG tree
    LOG("Compiling design (CSG Tree generation)...");
    this->processEvents();

    AbstractNode::resetIndexCounter();

    EvaluationSession session{doc.parent_path().string()};
    ContextHandle<BuiltinContext> builtin_context{Context::create<BuiltinContext>(&session)};
    setRenderVariables(builtin_context);

    std::shared_ptr<const FileContext> file_context;
#ifdef ENABLE_PYTHON
    if (python_result_node != NULL && this->python_active) this->absoluteRootNode = python_result_node;
    else
#endif
      this->absoluteRootNode = this->rootFile->instantiate(*builtin_context, &file_context);
    if (file_context) {
      this->qglview->cam.updateView(file_context, false);
      viewportControlWidget->cameraChanged();
    }

    if (this->absoluteRootNode) {
      // Do we have an explicit root node (! modifier)?
      const Location *nextLocation = nullptr;
      if (!(this->rootNode = find_root_tag(this->absoluteRootNode, &nextLocation))) {
        this->rootNode = this->absoluteRootNode;
      }
      if (nextLocation) {
        LOG(message_group::NONE, *nextLocation, builtin_context->documentRoot(),
            "More than one Root Modifier (!)");
      }

      // FIXME: Consider giving away ownership of root_node to the Tree, or use reference counted
      // pointers
      this->tree.setRoot(this->rootNode);
    }
  }

  if (!this->rootNode) {
    if (parser_error_pos < 0) {
      LOG(message_group::Error, "Compilation failed! (no top level object found)");
    } else {
      LOG(message_group::Error, "Compilation failed!");
    }
    LOG(" ");
    this->processEvents();
  }
}

/*!
   Generates CSG tree for OpenCSG evaluation.
   Assumes that the design has been parsed and evaluated (this->root_node is set)
 */
void MainWindow::compileCSG()
{
  OpenSCAD::hardwarnings = GlobalPreferences::inst()->getValue("advanced/enableHardwarnings").toBool();
  try {
    assert(this->rootNode);
    LOG("Compiling design (CSG Products generation)...");
    this->processEvents();

    // Main CSG evaluation
    this->progresswidget = new ProgressWidget(this);
    connect(this->progresswidget, &ProgressWidget::requestShow, this, &MainWindow::showProgress);

    GeometryEvaluator geomevaluator(this->tree);
#ifdef ENABLE_OPENCSG
    CSGTreeEvaluator csgrenderer(this->tree, &geomevaluator);
#endif

    if (!isClosing) progress_report_prep(this->rootNode, report_func, this);
    else return;
    try {
#ifdef ENABLE_OPENCSG
      this->processEvents();
      this->csgRoot = csgrenderer.buildCSGTree(*rootNode);
#endif
      renderStatistic.printCacheStatistic();
      this->processEvents();
    } catch (const ProgressCancelException&) {
      LOG("CSG generation cancelled.");
    } catch (const HardWarningException&) {
      LOG("CSG generation cancelled due to hardwarning being enabled.");
    }
    progress_report_fin();
    updateStatusBar(nullptr);

    LOG("Compiling design (CSG Products normalization)...");
    this->processEvents();

    const size_t normalizelimit =
      2ul * GlobalPreferences::inst()->getValue("advanced/openCSGLimit").toUInt();
    CSGTreeNormalizer normalizer(normalizelimit);

    if (this->csgRoot) {
      this->normalizedRoot = normalizer.normalize(this->csgRoot);
      if (this->normalizedRoot) {
        this->rootProduct = std::make_shared<CSGProducts>();
        this->rootProduct->import(this->normalizedRoot);
      } else {
        this->rootProduct.reset();
        LOG(message_group::Warning, "CSG normalization resulted in an empty tree");
        this->processEvents();
      }
    }

    const std::vector<std::shared_ptr<CSGNode>>& highlight_terms = csgrenderer.getHighlightNodes();
    if (highlight_terms.size() > 0) {
      LOG("Compiling highlights (%1$d CSG Trees)...", highlight_terms.size());
      this->processEvents();

      this->highlightsProducts = std::make_shared<CSGProducts>();
      for (const auto& highlight_term : highlight_terms) {
        auto nterm = normalizer.normalize(highlight_term);
        if (nterm) {
          this->highlightsProducts->import(nterm);
        }
      }
    } else {
      this->highlightsProducts.reset();
    }

    const auto& background_terms = csgrenderer.getBackgroundNodes();
    if (background_terms.size() > 0) {
      LOG("Compiling background (%1$d CSG Trees)...", background_terms.size());
      this->processEvents();

      this->backgroundProducts = std::make_shared<CSGProducts>();
      for (const auto& background_term : background_terms) {
        auto nterm = normalizer.normalize(background_term);
        if (nterm) {
          this->backgroundProducts->import(nterm);
        }
      }
    } else {
      this->backgroundProducts.reset();
    }

    if (this->rootProduct && (this->rootProduct->size() >
                              GlobalPreferences::inst()->getValue("advanced/openCSGLimit").toUInt())) {
      LOG(message_group::UI_Warning, "Normalized tree has %1$d elements!", this->rootProduct->size());
      LOG(message_group::UI_Warning, "OpenCSG rendering has been disabled.");
    }
#ifdef ENABLE_OPENCSG
    else {
      LOG("Normalized tree has %1$d elements!", (this->rootProduct ? this->rootProduct->size() : 0));
      this->previewRenderer = std::make_shared<OpenCSGRenderer>(
        this->rootProduct, this->highlightsProducts, this->backgroundProducts);
    }
#endif  // ifdef ENABLE_OPENCSG
    this->thrownTogetherRenderer = std::make_shared<ThrownTogetherRenderer>(
      this->rootProduct, this->highlightsProducts, this->backgroundProducts);
    LOG("Compile and preview finished.");
    renderStatistic.printRenderingTime();
    this->processEvents();
  } catch (const HardWarningException&) {
    exceptionCleanup();
  }
}

void MainWindow::on_fileActionOpen_triggered()
{
  auto guard = scopedSetCurrentOutput();
  auto fileInfoList = UIUtils::openFiles(this);
  for (auto& i : fileInfoList) {
    if (!i.exists()) {
      return;
    }
    tabManager->open(i.filePath());
  }
}

void MainWindow::on_fileActionNewWindow_triggered()
{
  new MainWindow(QStringList());
}

void MainWindow::on_fileActionOpenWindow_triggered()
{
  auto fileInfoList = UIUtils::openFiles(this);
  for (auto& i : fileInfoList) {
    if (!i.exists()) {
      return;
    }
    new MainWindow(QStringList(i.filePath()));
  }
}

void MainWindow::actionOpenRecent()
{
  auto guard = scopedSetCurrentOutput();
  auto action = qobject_cast<QAction *>(sender());
  tabManager->open(action->data().toString());
}

void MainWindow::on_fileActionClearRecent_triggered()
{
  QSettingsCached settings;  // already set up properly via main.cpp
  const QStringList files;
  settings.setValue("recentFileList", files);

  updateRecentFileActions();
}

void MainWindow::on_fileActionNewProject_triggered()
{
  const QString parentDir = QFileDialog::getExistingDirectory(
    this, _("Choose parent folder for the new project"), QDir::homePath());
  if (parentDir.isEmpty()) return;

  QDialog dlg(this);
  dlg.setWindowTitle(_("New Project"));
  dlg.setModal(true);
  dlg.setMinimumWidth(360);
  auto *root = new QVBoxLayout(&dlg);
  root->setContentsMargins(20, 18, 20, 16);
  root->setSpacing(12);

  auto *nameLabel = new QLabel(_("Project name"), &dlg);
  auto *nameEdit = new QLineEdit(QStringLiteral("MyDesign"), &dlg);
  nameEdit->setMinimumHeight(28);
  nameEdit->selectAll();
  root->addWidget(nameLabel);
  root->addWidget(nameEdit);

  auto *hint = new QLabel(
    tr("Folder: %1").arg(QDir::toNativeSeparators(parentDir)), &dlg);
  hint->setWordWrap(true);
  hint->setStyleSheet(QStringLiteral("color: #6e6e6e; font-size: 11px;"));
  root->addWidget(hint);

  root->addSpacing(4);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  buttons->button(QDialogButtonBox::Ok)->setText(_("OK"));
  buttons->button(QDialogButtonBox::Cancel)->setText(_("Cancel"));
  root->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  connect(nameEdit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);
  nameEdit->setFocus();

  if (dlg.exec() != QDialog::Accepted) return;
  const QString safeName = nameEdit->text().trimmed();
  if (safeName.isEmpty()) return;
  const QString projectRoot = QDir(parentDir).filePath(safeName);

  QString err;
  if (!ProjectManager::instance().createProject(projectRoot, safeName, &err)) {
    QMessageBox::warning(this, _("New Project"), err.isEmpty() ? _("Failed to create project.") : err);
    return;
  }
  const QString mainScad = ProjectManager::instance().aiTargetFile();
  if (!mainScad.isEmpty()) tabManager->open(mainScad);
  updateRecentProjectActions();
}

void MainWindow::on_fileActionOpenProject_triggered()
{
  const QString root =
    QFileDialog::getExistingDirectory(this, _("Open Project Folder"), QDir::homePath());
  if (root.isEmpty()) return;
  QString err;
  if (!ProjectManager::instance().openProject(root, &err)) {
    QMessageBox::warning(this, _("Open Project"), err.isEmpty() ? _("Failed to open project.") : err);
    return;
  }
  const QString target = ProjectManager::instance().aiTargetFile();
  if (!target.isEmpty()) tabManager->open(target);
  updateRecentProjectActions();
}

void MainWindow::on_fileActionClearRecentProjects_triggered()
{
  ProjectManager::instance().clearRecentProjects();
  updateRecentProjectActions();
}

void MainWindow::actionOpenRecentProject()
{
  const auto *action = qobject_cast<QAction *>(sender());
  if (!action) return;
  const QString root = action->data().toString();
  if (root.isEmpty()) return;
  QString err;
  if (!ProjectManager::instance().openProject(root, &err)) {
    QMessageBox::warning(this, _("Open Project"), err.isEmpty() ? _("Failed to open project.") : err);
    updateRecentProjectActions();
    return;
  }
  const QString target = ProjectManager::instance().aiTargetFile();
  if (!target.isEmpty()) tabManager->open(target);
  updateRecentProjectActions();
}

void MainWindow::updateRecentProjectActions()
{
  if (!this->menuRecentProjects) return;
  this->menuRecentProjects->clear();
  const QStringList recent = ProjectManager::instance().recentProjects();
  for (const QString& path : recent) {
    auto *action = new QAction(QFileInfo(path).fileName().replace("&", "&&"), this);
    action->setData(path);
    action->setToolTip(path);
    connect(action, &QAction::triggered, this, &MainWindow::actionOpenRecentProject);
    this->menuRecentProjects->addAction(action);
  }
  if (!recent.isEmpty()) this->menuRecentProjects->addSeparator();
  this->menuRecentProjects->addAction(this->fileActionClearRecentProjects);
}

void MainWindow::onProjectExplorerOpenFile(const QString& path)
{
  if (path.isEmpty()) return;
  QFileInfo info(path);
  if (!info.exists() || info.isDir()) return;

  const QString suffix = info.suffix().toLower();
  if (suffix == QLatin1String("png") || suffix == QLatin1String("jpg") ||
      suffix == QLatin1String("jpeg") || suffix == QLatin1String("webp") ||
      suffix == QLatin1String("gif") || suffix == QLatin1String("bmp")) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    return;
  }
  tabManager->open(path);
  ProjectManager::instance().setActiveFile(path);
  showEditorView();
}

void MainWindow::showProjectView()
{
  if (!this->editorAreaStack) return;
  this->editorAreaStack->setCurrentIndex(0);
  setEditorAreaContentVisible(true);
  refreshEditorActivityBar();
  refreshWindowTitle();
}

void MainWindow::showEditorView()
{
  if (!this->editorAreaStack) return;
  this->editorAreaStack->setCurrentIndex(1);
  setEditorAreaContentVisible(true);
  refreshEditorActivityBar();
  refreshWindowTitle();
}

void MainWindow::setEditorAreaContentVisible(bool visible)
{
  this->editorAreaContentVisible = visible;
  if (this->editorAreaStack) {
    this->editorAreaStack->setVisible(visible);
  }
  if (auto *divider = this->editorActivityBar
                        ? this->editorActivityBar->parentWidget()
                              ? this->editorActivityBar->parentWidget()->findChild<QFrame *>(
                                  QStringLiteral("editorActivityDivider"))
                              : nullptr
                        : nullptr) {
    divider->setVisible(visible);
  }

  // Collapse the whole editor dock to the activity bar width (VS Code hide sidebar).
  // Hiding the stack alone leaves an empty gray strip because the dock keeps its width.
  constexpr int kActivityBarW = kEditorActivityBarW;
  if (this->editorDock) {
    if (!visible) {
      const int cur = this->editorDock->width();
      if (cur > kActivityBarW + 16) this->editorAreaExpandedWidth = cur;
      this->editorDock->setMinimumWidth(kActivityBarW);
      this->editorDock->setMaximumWidth(kActivityBarW);
      resizeDocks({this->editorDock}, {kActivityBarW}, Qt::Horizontal);
    } else {
      this->editorDock->setMaximumWidth(QWIDGETSIZE_MAX);
      this->editorDock->setMinimumWidth(0);
      const int restore =
        this->editorAreaExpandedWidth > kActivityBarW ? this->editorAreaExpandedWidth : 400;
      resizeDocks({this->editorDock}, {restore}, Qt::Horizontal);
    }
  }
  refreshEditorActivityBar();
}

void MainWindow::refreshEditorActivityBar()
{
  const bool showing = this->editorAreaContentVisible && this->editorAreaStack;
  const int idx = showing ? this->editorAreaStack->currentIndex() : -1;
  if (this->projectViewTabBtn) this->projectViewTabBtn->setChecked(idx == 0);
  if (this->editorViewTabBtn) this->editorViewTabBtn->setChecked(idx == 1);
}

void MainWindow::refreshWindowTitle()
{
  // Explorer activity: show the project name. Editor activity: show the file.
  if (this->editorAreaStack && this->editorAreaStack->currentIndex() == 0 &&
      ProjectManager::instance().hasProject()) {
    const QString project = ProjectManager::instance().projectName();
    if (!project.isEmpty()) {
      setWindowTitle(project);
      return;
    }
  }
  setWindowTitle(getCurrentFileName());
}

void MainWindow::onProjectActivityClicked()
{
  // VS Code: click active activity again → hide the side panel
  if (this->editorAreaContentVisible && this->editorAreaStack &&
      this->editorAreaStack->currentIndex() == 0) {
    setEditorAreaContentVisible(false);
    return;
  }
  showProjectView();
}

void MainWindow::onEditorActivityClicked()
{
  if (this->editorAreaContentVisible && this->editorAreaStack &&
      this->editorAreaStack->currentIndex() == 1) {
    setEditorAreaContentVisible(false);
    return;
  }
  showEditorView();
}

void MainWindow::onProjectManagerChanged()
{
  if (this->projectExplorer) this->projectExplorer->refreshTheme();
  if (this->aiDock && this->aiDock->chatWidget()) {
    this->aiDock->chatWidget()->onProjectChanged();
  }
  // Opening a project should land on the Project activity so the tree is visible.
  if (ProjectManager::instance().hasProject()) {
    showProjectView();
  }
}

// Updates the content of the recent files menu entries
// by iterating over the recently opened files.
void MainWindow::updateRecentFileActions()
{
  auto files = UIUtils::recentFiles();

  for (int i = 0; i < files.size(); ++i) {
    QAction *recent;
    if (i < this->fileActionRecentFiles.size()) {
      recent = this->fileActionRecentFiles[i];
    } else {
      recent = new QAction(this);
      connect(recent, &QAction::triggered, this, &MainWindow::actionOpenRecent);
      this->fileActionRecentFiles.push_back(recent);
    }
    this->menuOpenRecent->addAction(recent);
    recent->setText(QFileInfo(files[i]).fileName().replace("&", "&&"));
    recent->setData(files[i]);
    recent->setVisible(true);
  }
  this->menuOpenRecent->addSeparator();
  this->menuOpenRecent->addAction(this->fileActionClearRecent);
}

void MainWindow::show_examples()
{
  bool found_example = false;

  for (const auto& cat : UIUtils::exampleCategories()) {
    auto examples = UIUtils::exampleFiles(cat.name);
    auto menu = this->menuExamples->addMenu(gettext(cat.name.toStdString().c_str()));
    if (!cat.tooltip.trimmed().isEmpty()) {
      menu->setToolTip(gettext(cat.tooltip.toStdString().c_str()));
      menu->setToolTipsVisible(true);
    }

    for (const auto& ex : examples) {
      auto openAct = new QAction(ex.fileName().replace("&", "&&"), this);
      connect(openAct, &QAction::triggered, this, &MainWindow::actionOpenExample);
      menu->addAction(openAct);
      openAct->setData(ex.canonicalFilePath());
      found_example = true;
    }
  }

  if (!found_example) {
    delete this->menuExamples;
    this->menuExamples = nullptr;
  }
}

void MainWindow::actionOpenExample()
{
  auto guard = scopedSetCurrentOutput();
  const auto action = qobject_cast<QAction *>(sender());
  if (action) {
    const auto& path = action->data().toString();
    tabManager->open(path);
  }
}

void MainWindow::writeBackup(QFile *file)
{
  // see MainWindow::saveBackup()
  file->resize(0);
  QTextStream writer(file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  writer.setCodec("UTF-8");
#endif
  writer << activeEditor->toPlainText();
  this->activeEditor->parameterWidget->saveBackupFile(file->fileName());

  LOG("Saved backup file: %1$s", file->fileName().toUtf8().constData());
}

void MainWindow::saveBackup()
{
  auto path = PlatformUtils::backupPath();
  if ((!fs::exists(path)) && (!PlatformUtils::createBackupPath())) {
    LOG(message_group::UI_Warning, "Cannot create backup path: %1$s", path);
    return;
  }

  auto backupPath = QString::fromLocal8Bit(path.c_str());
  if (!backupPath.endsWith("/")) backupPath.append("/");

  QString basename = "unsaved";
  if (!activeEditor->filepath.isEmpty()) {
    auto fileInfo = QFileInfo(activeEditor->filepath);
    basename = fileInfo.baseName();
  }

  if (!this->tempFile) {
#ifdef ENABLE_PYTHON
    const QString suffix = this->python_active ? "py" : "scad";
#else
    const QString suffix = "scad";
#endif
    this->tempFile = new QTemporaryFile(backupPath.append(basename + "-backup-XXXXXXXX." + suffix));
  }

  if ((!this->tempFile->isOpen()) && (!this->tempFile->open())) {
    LOG(message_group::UI_Warning, "Failed to create backup file");
    return;
  }
  return writeBackup(this->tempFile);
}

void MainWindow::on_fileActionSave_triggered()
{
  tabManager->save(activeEditor);
}

void MainWindow::on_fileActionSaveAs_triggered()
{
  tabManager->saveAs(activeEditor);
}

void MainWindow::on_fileActionPythonRevoke_triggered()
{
  QSettingsCached settings;
#ifdef ENABLE_PYTHON
  python_trusted = false;
  this->trusted_edit_document_name = "";
#endif
  settings.remove("python_hash");
  QMessageBox::information(this, _("Trusted Files"), "All trusted python files revoked",
                           QMessageBox::Ok);
}

void MainWindow::on_fileActionPythonCreateVenv_triggered()
{
#ifdef ENABLE_PYTHON
  const QString selectedDir = QFileDialog::getExistingDirectory(this, "Create Virtual Environment");
  if (selectedDir.isEmpty()) {
    return;
  }

  const QDir venvDir{selectedDir};
  if (!venvDir.exists()) {
    // Should not happen, but just in case double check...
    QMessageBox::critical(this, _("Create Virtual Environment"),
                          "Directory does not exist. Can't create virtual environment.",
                          QMessageBox::Ok);
    return;
  }

  if (!venvDir.isEmpty()) {
    QMessageBox::critical(this, _("Create Virtual Environment"),
                          "Directory is not empty. Can't create virtual environment.", QMessageBox::Ok);
    return;
  }

  const auto& path = venvDir.absolutePath().toStdString();
  LOG("Creating Python virtual environment in '%1$s'...", path);
  int result = pythonCreateVenv(path);

  if (result == 0) {
    Settings::SettingsPython::pythonVirtualEnv.setValue(path);
    Settings::Settings::visit(SettingsWriter());
    LOG("Python virtual environment creation successfull.");
    QMessageBox::information(this, _("Create Virtual Environment"),
                             "Virtual environment created, please restart OpenSCAD to activate.",
                             QMessageBox::Ok);
  } else {
    LOG("Python virtual environment creation failed.");
    QMessageBox::critical(this, _("Create Virtual Environment"), "Virtual environment creation failed.",
                          QMessageBox::Ok);
  }
#endif  // ifdef ENABLE_PYTHON
}

void MainWindow::on_fileActionPythonSelectVenv_triggered()
{
#ifdef ENABLE_PYTHON
  const QString venvDir = QFileDialog::getExistingDirectory(this, "Select Virtual Environment");
  if (venvDir.isEmpty()) {
    return;
  }
  const QFileInfo fileInfo{QDir{venvDir}, "pyvenv.cfg"};
  if (fileInfo.exists()) {
    Settings::SettingsPython::pythonVirtualEnv.setValue(venvDir.toStdString());
    Settings::Settings::visit(SettingsWriter());
    QMessageBox::information(this, _("Select Virtual Environment"),
                             "Virtual environment selected, please restart OpenSCAD to activate.",
                             QMessageBox::Ok);
  }
#endif  // ifdef ENABLE_PYTHON
}

void MainWindow::on_fileActionSaveACopy_triggered()
{
  tabManager->saveACopy(activeEditor);
}

void MainWindow::on_fileShowLibraryFolder_triggered()
{
  auto guard = scopedSetCurrentOutput();
  auto path = PlatformUtils::userLibraryPath();
  if (!fs::exists(path)) {
    LOG(message_group::UI_Warning, "Library path %1$s doesn't exist. Creating", path);
    if (!PlatformUtils::createUserLibraryPath()) {
      LOG(message_group::UI_Error, "Cannot create library path: %1$s", path);
    }
  }
  auto url = QString::fromStdString(path);
  LOG("Opening file browser for %1$s", url.toStdString());
  QDesktopServices::openUrl(QUrl::fromLocalFile(url));
}

void MainWindow::on_fileActionReload_triggered()
{
  auto guard = scopedSetCurrentOutput();
  if (checkEditorModified()) {
    fileChangedOnDisk();                  // force cached autoReloadId to update
    (void)tabManager->refreshDocument();  // ignore errors opening the file
  }
}

void MainWindow::on_editActionCopyVPT_triggered()
{
  const auto vpt = qglview->cam.getVpt();
  const QString txt =
    QString("[ %1, %2, %3 ]").arg(vpt.x(), 0, 'f', 2).arg(vpt.y(), 0, 'f', 2).arg(vpt.z(), 0, 'f', 2);
  QApplication::clipboard()->setText(txt);
}

void MainWindow::on_editActionCopyVPR_triggered()
{
  const auto vpr = qglview->cam.getVpr();
  const QString txt =
    QString("[ %1, %2, %3 ]").arg(vpr.x(), 0, 'f', 2).arg(vpr.y(), 0, 'f', 2).arg(vpr.z(), 0, 'f', 2);
  QApplication::clipboard()->setText(txt);
}

void MainWindow::on_editActionCopyVPD_triggered()
{
  const QString txt = QString::number(qglview->cam.zoomValue(), 'f', 2);
  QApplication::clipboard()->setText(txt);
}

void MainWindow::on_editActionCopyVPF_triggered()
{
  const QString txt = QString::number(qglview->cam.fovValue(), 'f', 2);
  QApplication::clipboard()->setText(txt);
}

QList<double> MainWindow::getTranslation() const
{
  QList<double> ret;
  ret.append(qglview->cam.object_trans.x());
  ret.append(qglview->cam.object_trans.y());
  ret.append(qglview->cam.object_trans.z());
  return ret;
}

QList<double> MainWindow::getRotation() const
{
  QList<double> ret;
  ret.append(qglview->cam.object_rot.x());
  ret.append(qglview->cam.object_rot.y());
  ret.append(qglview->cam.object_rot.z());
  return ret;
}

void MainWindow::hideFind()
{
  find_panel->hide();
  if (activeEditor) {
    activeEditor->findState = TabManager::FIND_HIDDEN;
  }
  editActionFindNext->setEnabled(false);
  editActionFindPrevious->setEnabled(false);
  const int findCount =
    (activeEditor) ? activeEditor->updateFindIndicators(this->findInputField->text(), false) : 0;
  this->findInputField->setFindCount(findCount);
  this->processEvents();
}

// Prepare the UI for the find (and replace if requested)
// Among other thing it makes the text field and replacement field visible and well as it configures the
// activeEditor to appropriate search mode.
void MainWindow::showFind(bool doFindAndReplace)
{
  findInputField->setFindCount(activeEditor->updateFindIndicators(findInputField->text()));
  processEvents();

  if (doFindAndReplace) {
    findTypeComboBox->setCurrentIndex(1);
    replaceInputField->show();
    replaceButton->show();
    replaceAllButton->show();
    activeEditor->findState = TabManager::FIND_REPLACE_VISIBLE;
  } else {
    findTypeComboBox->setCurrentIndex(0);
    replaceInputField->hide();
    replaceButton->hide();
    replaceAllButton->hide();
    activeEditor->findState = TabManager::FIND_VISIBLE;
  }

  find_panel->show();
  editActionFindNext->setEnabled(true);
  editActionFindPrevious->setEnabled(true);
  if (!activeEditor->selectedText().isEmpty()) {
    findInputField->setText(activeEditor->selectedText());
  }
  findInputField->setFocus();
  findInputField->selectAll();
}

void MainWindow::on_editActionFind_triggered()
{
  showFind(false);
}

void MainWindow::findString(const QString& textToFind)
{
  if (!activeEditor) return;

  this->findInputField->setFindCount(activeEditor->updateFindIndicators(textToFind));
  this->processEvents();
  activeEditor->find(textToFind);
}

void MainWindow::on_editActionFindAndReplace_triggered()
{
  showFind(true);
}

void MainWindow::actionSelectFind(int type)
{
  // If type is one, then we shows the find and replace UI component
  showFind(type == 1);
}

void MainWindow::replace()
{
  activeEditor->replaceSelectedText(this->replaceInputField->text());
  activeEditor->find(this->findInputField->text());
}

void MainWindow::replaceAll()
{
  activeEditor->replaceAll(this->findInputField->text(), this->replaceInputField->text());
}

void MainWindow::on_editActionConvertTabsToSpaces_triggered()
{
  const auto text = activeEditor->toPlainText();

  QString converted;

  int cnt = 4;
  for (auto c : text) {
    if (c == '\t') {
      for (; cnt > 0; cnt--) {
        converted.append(' ');
      }
    } else {
      converted.append(c);
    }
    if (cnt <= 0 || c == '\n') {
      cnt = 5;
    }
    cnt--;
  }
  activeEditor->setText(converted);
}

void MainWindow::on_editActionFindNext_triggered()
{
  activeEditor->find(this->findInputField->text(), true);
}

void MainWindow::on_editActionFindPrevious_triggered()
{
  activeEditor->find(this->findInputField->text(), true, true);
}

void MainWindow::on_editActionUseSelectionForFind_triggered()
{
  findInputField->setText(activeEditor->selectedText());
}

void MainWindow::updateFindBuffer(const QString& s)
{
  QApplication::clipboard()->setText(s, QClipboard::FindBuffer);
}

void MainWindow::findBufferChanged()
{
  auto t = QApplication::clipboard()->text(QClipboard::FindBuffer);
  // The convention seems to be to not update the search field if the findbuffer is empty
  if (!t.isEmpty()) {
    findInputField->setText(t);
  }
}

bool MainWindow::event(QEvent *event)
{
  if (event->type() == InputEvent::eventType) {
    auto *inputEvent = dynamic_cast<InputEvent *>(event);
    if (inputEvent) {
      inputEvent->deliver(this);
    }
    event->accept();
    return true;
  }
  return QMainWindow::event(event);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
  if (rubberBandManager.isVisible()) {
    if (event->type() == QEvent::KeyRelease) {
      auto keyEvent = static_cast<QKeyEvent *>(event);
      if (keyEvent->key() == Qt::Key_Control) {
        rubberBandManager.hide();
      }
    }
  }

  if (obj == find_panel) {
    if (event->type() == QEvent::KeyPress) {
      auto keyEvent = static_cast<QKeyEvent *>(event);
      if (keyEvent->key() == Qt::Key_Escape) {
        this->hideFind();
        return true;
      }
    }
    return false;
  }

  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::setRenderVariables(ContextHandle<BuiltinContext>& context)
{
  const RenderVariables r = {
    .preview = this->isPreview,
    .time = this->animateWidget->getAnimTval(),
    .camera = qglview->cam,
  };
  r.applyToContext(context);
}

/*!
   Returns true if the current document is a file on disk and that file has new content.
   Returns false if a file on disk has disappeared or if we haven't yet saved.
 */
bool MainWindow::fileChangedOnDisk()
{
  if (!activeEditor->filepath.isEmpty()) {
    struct stat st;
    memset(&st, 0, sizeof(struct stat));
    const bool valid = (stat(activeEditor->filepath.toLocal8Bit(), &st) == 0);
    // If file isn't there, just return and use current editor text
    if (!valid) return false;

    auto newid = str(boost::format("%x.%x") % st.st_mtime % st.st_size);
    if (newid != activeEditor->autoReloadId) {
      activeEditor->autoReloadId = newid;
      return true;
    }
  }
  return false;
}

/*!
   Returns true if anything was compiled.
 */

#ifdef ENABLE_PYTHON
bool MainWindow::trust_python_file(const std::string& file, const std::string& content)
{
  QSettingsCached settings;
  char setting_key[256];
  if (python_trusted) return true;

  std::string act_hash, ref_hash;
  snprintf(setting_key, sizeof(setting_key) - 1, "python_hash/%s", file.c_str());
  act_hash = SHA256HashString(content);

  if (file == this->untrusted_edit_document_name) return false;

  if (file == this->trusted_edit_document_name) {
    settings.setValue(setting_key, act_hash.c_str());
    return true;
  }

  if (content.size() <= 1) {  // 1st character already typed
    this->trusted_edit_document_name = file;
    return true;
  }
  if (content.rfind("from openscad import", 0) == 0) {  // 1st character already typed
    this->trusted_edit_document_name = file;
    return true;
  }

  if (settings.contains(setting_key)) {
    ref_hash = settings.value(setting_key).toString().toStdString();
  }

  if (act_hash == ref_hash) {
    this->trusted_edit_document_name = file;
    return true;
  }

  auto ret = QMessageBox::warning(this, "Application",
                                  _("Python files can potentially contain harmful stuff.\n"
                                    "Do you trust this file ?\n"),
                                  QMessageBox::Yes | QMessageBox::YesAll | QMessageBox::No);
  if (ret == QMessageBox::YesAll) {
    python_trusted = true;
    return true;
  }
  if (ret == QMessageBox::Yes) {
    this->trusted_edit_document_name = file;
    settings.setValue(setting_key, act_hash.c_str());
    return true;
  }

  if (ret == QMessageBox::No) {
    this->untrusted_edit_document_name = file;
    return false;
  }
  return false;
}
#endif  // ifdef ENABLE_PYTHON

std::shared_ptr<SourceFile> MainWindow::parseDocument(EditorInterface *editor)
{
  resetSuppressedMessages();

  auto document = editor->toPlainText();
  auto fulltext = std::string(document.toUtf8().constData()) + "\n\x03\n" + commandline_commands;

  const std::string fname = editor->filepath.isEmpty() ? "" : editor->filepath.toStdString();
#ifdef ENABLE_PYTHON
  this->python_active = false;
  if (boost::algorithm::ends_with(fname, ".py")) {
    std::string content = std::string(this->lastCompiledDoc.toUtf8().constData());
    if (Feature::ExperimentalPythonEngine.is_enabled() && trust_python_file(fname, content))
      this->python_active = true;
    else LOG(message_group::Warning, Location::NONE, "", "Python is not enabled");
  }

  if (this->python_active) {
    auto fulltext_py = std::string(this->lastCompiledDoc.toUtf8().constData());

    const auto& venv = venvBinDirFromSettings();
    initPython(venv, this->animateWidget->getAnimTval());

    if (venv.empty()) {
      LOG("Running %1$s without venv.", python_version());
    } else {
      const auto& v = Settings::SettingsPython::pythonVirtualEnv.value();
      LOG("Running %1$s in venv '%2$s'.", python_version(), v);
    }
    auto error = evaluatePython(fulltext_py, false);
    if (error.size() > 0) LOG(message_group::Error, Location::NONE, "", error.c_str());
    fulltext = "\n";
  }
#endif  // ifdef ENABLE_PYTHON

  SourceFile *sourceFile;
  sourceFile = parse(sourceFile, fulltext, fname, fname, false) ? sourceFile : nullptr;

  editor->resetHighlighting();
  if (sourceFile) {
    // add parameters as annotation in AST
    CommentParser::collectParameters(fulltext, sourceFile);
    editor->parameterWidget->setParameters(sourceFile, fulltext);
    editor->parameterWidget->applyParameters(sourceFile);
    editor->parameterWidget->setEnabled(true);
    editor->setIndicator(sourceFile->indicatorData);
  } else {
    editor->parameterWidget->setEnabled(false);
  }

  return std::shared_ptr<SourceFile>(sourceFile);
}

void MainWindow::parseTopLevelDocument()
{
  resetSuppressedMessages();

  this->lastCompiledDoc = activeEditor->toPlainText();

  activeEditor->resetHighlighting();
  this->rootFile = parseDocument(activeEditor);
  this->parsedFile = this->rootFile;
}

void MainWindow::checkAutoReload()
{
  if (!activeEditor->filepath.isEmpty()) {
    actionReloadRenderPreview();
  }
}

void MainWindow::on_designActionAutoReload_toggled(bool on)
{
  QSettingsCached settings;
  settings.setValue("design/autoReload", designActionAutoReload->isChecked());
  if (on) {
    autoReloadTimer->start(autoReloadPollingPeriodMS);
  } else {
    autoReloadTimer->stop();
  }
}

bool MainWindow::checkEditorModified()
{
  if (activeEditor->isContentModified()) {
    auto ret = QMessageBox::warning(this, _("Application"),
                                    _("The document has been modified.\n"
                                      "Do you really want to reload the file?"),
                                    QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) {
      return false;
    }
  }
  return true;
}

void MainWindow::on_designActionReloadAndPreview_triggered()
{
  requestFitViewAfterRender();
  actionReloadRenderPreview();
}

void MainWindow::actionReloadRenderPreview()
{
  if (GuiLocker::isLocked()) return;
  GuiLocker::lock();
  autoReloadTimer->stop();
  setCurrentOutput();

  // Opening/reloading a file should frame the model in the preview by default.
  requestFitViewAfterRender();
  this->afterCompileSlot = "csgReloadRender";
  this->procevents = true;
  this->isPreview = true;
  compile(true);
}

void MainWindow::csgReloadRender()
{
  if (this->rootNode) compileCSG();

  // Go to non-CGAL view mode
  if (viewActionThrownTogether->isChecked()) {
    viewModeThrownTogether();
  } else {
#ifdef ENABLE_OPENCSG
    viewModePreview();
#else
    viewModeThrownTogether();
#endif
  }
  applyPendingFitView();
  compileEnded();
}

void MainWindow::prepareCompile(const char *afterCompileSlot, bool procevents, bool preview)
{
  setCurrentOutput();
  autoReloadTimer->stop();
  LOG(" ");
  LOG("Parsing design (AST generation)...");
  this->processEvents();
  this->afterCompileSlot = afterCompileSlot;
  this->procevents = procevents;
  this->isPreview = preview;
}

void MainWindow::on_designActionPreview_triggered()
{
  requestFitViewAfterRender();
  actionRenderPreview();
}

void MainWindow::actionRenderPreview()
{
  static bool preview_requested;
  preview_requested = true;

  if (GuiLocker::isLocked()) return;

  GuiLocker::lock();
  preview_requested = false;

  // Default framing after every preview (F5, open-file auto-preview, AI tools).
  requestFitViewAfterRender();
  resetMeasurementsState(false, "Render (not preview) to enable measurements");

  prepareCompile("csgRender", !animateDock->isVisible(), true);
  compile(false, false);

  if (preview_requested) {
    // if the action was called when the gui was locked, we must request it one more time
    // however, it's not possible to call it directly NOR make the loop
    // it must be called from the mainloop
    QTimer::singleShot(0, this, &MainWindow::actionRenderPreview);
    return;
  }
}

void MainWindow::csgRender()
{
  if (isEmpty() || !this->rootNode) {
    clearViewportGeometry();
    if (isEmpty()) {
      LOG("Editor is empty — viewport cleared.");
    } else {
      LOG(message_group::UI_Warning, "No top level geometry to preview");
    }
    if (activeEditor) {
      activeEditor->contentsRendered = true;
      renderedEditor = activeEditor;
    }
    pendingFitViewAfterRender = false;
    compileEnded();
    return;
  }

  compileCSG();

  // Go to non-CGAL view mode
  if (viewActionThrownTogether->isChecked()) {
    viewModeThrownTogether();
  } else {
#ifdef ENABLE_OPENCSG
    viewModePreview();
#else
    viewModeThrownTogether();
#endif
  }

  if (animateWidget->dumpPictures()) {
    const int steps = animateWidget->nextFrame();
    const QImage img = this->qglview->grabFrame();
    const QString filename = QString("frame%1.png").arg(steps, 5, 10, QChar('0'));
    img.save(filename, "PNG");
  }

  applyPendingFitView();
  compileEnded();
}

void MainWindow::sendToExternalTool(ExternalToolInterface& externalToolService)
{
  const QFileInfo activeFile(activeEditor->filepath);
  QString activeFileName = activeFile.fileName();
  if (activeFileName.isEmpty()) activeFileName = "Untitled.scad";
  // TODO: Replace suffix to match exported file format?

  activeFileName = activeFileName +
                   QString::fromStdString("." + fileformat::toSuffix(externalToolService.fileFormat()));

  const bool export_status =
    externalToolService.exportTemporaryFile(rootGeom, activeFileName, &qglview->cam);
  if (!export_status) {
    return;
  }

  this->progresswidget = new ProgressWidget(this);
  connect(this->progresswidget, &ProgressWidget::requestShow, this, &MainWindow::showProgress);

  const bool process_status = externalToolService.process(
    activeFileName.toStdString(), [this](double permille) { return network_progress_func(permille); });
  updateStatusBar(nullptr);
  if (!process_status) {
    return;
  }

  const auto url = externalToolService.getURL();
  if (!url.empty()) {
    QDesktopServices::openUrl(QUrl{QString::fromStdString(url)});
  }
}

void MainWindow::on_designAction3DPrint_triggered()
{
  if (GuiLocker::isLocked()) return;
  const GuiLocker lock;

  // Make sure we can export:
  const unsigned int dim = 3;
  if (!canExport(dim)) return;

  PrintInitDialog printInitDialog;
  const auto status = printInitDialog.exec();

  if (status == QDialog::Accepted) {
    auto guard = scopedSetCurrentOutput();
    const print_service_t serviceType = printInitDialog.getServiceType();
    const QString serviceName = printInitDialog.getServiceName();
    const FileFormat fileFormat = printInitDialog.getFileFormat();

    LOG("Selected File format: %1$s", fileformat::info(fileFormat).description);

    GlobalPreferences::inst()->updateGUI();
    const auto externalToolService = createExternalToolService(serviceType, serviceName, fileFormat);
    if (!externalToolService) {
      LOG("Error: Unable to create service: %1$d %2$s %3$d", static_cast<int>(serviceType),
          serviceName.toStdString(), static_cast<int>(fileFormat));
      return;
    }
    sendToExternalTool(*externalToolService);
  }
}

void MainWindow::on_designActionRender_triggered()
{
  if (GuiLocker::isLocked()) return;
  GuiLocker::lock();

  requestFitViewAfterRender();
  prepareCompile("cgalRender", true, false);
  compile(false);
}

void MainWindow::cgalRender()
{
  if (isEmpty() || !this->rootFile || !this->rootNode) {
    clearViewportGeometry();
    if (isEmpty()) {
      LOG("Editor is empty — viewport cleared.");
    } else {
      LOG(message_group::UI_Warning, "No top level geometry to render");
    }
    if (activeEditor) {
      activeEditor->contentsRendered = true;
      renderedEditor = activeEditor;
    }
    pendingFitViewAfterRender = false;
    compileEnded();
    return;
  }

  this->qglview->setRenderer(nullptr);
  this->geomRenderer = nullptr;
  rootGeom.reset();

  LOG("Rendering Polygon Mesh using %1$s...",
      renderBackend3DToString(RenderSettings::inst()->backend3D).c_str());

  this->progresswidget = new ProgressWidget(this);
  connect(this->progresswidget, &ProgressWidget::requestShow, this, &MainWindow::showProgress);

  if (!isClosing) progress_report_prep(this->rootNode, report_func, this);
  else return;

  this->cgalworker->start(this->tree);
}

void MainWindow::clearViewportGeometry()
{
  if (this->qglview) {
    this->qglview->setRenderer(nullptr);
  }
#ifdef ENABLE_OPENCSG
  this->previewRenderer = nullptr;
#endif
  this->thrownTogetherRenderer = nullptr;
  this->geomRenderer = nullptr;
  this->rootGeom.reset();
  this->absoluteRootNode.reset();
  this->csgRoot.reset();
  this->normalizedRoot.reset();
  this->rootProduct.reset();
  this->highlightsProducts.reset();
  this->backgroundProducts.reset();
  this->rootNode.reset();
  this->tree.setRoot(nullptr);
  resetMeasurementsState(false, "No top level geometry; render something to enable measurements");
  if (this->qglview) {
    this->qglview->update();
  }
}

void MainWindow::actionRenderDone(const std::shared_ptr<const Geometry>& root_geom)
{
#ifdef ENABLE_PYTHON
  python_lock();
#endif
  progress_report_fin();
  if (root_geom) {
    std::vector<std::string> options;
    if (Settings::Settings::summaryCamera.value()) {
      options.emplace_back(RenderStatistic::CAMERA);
    }
    if (Settings::Settings::summaryArea.value()) {
      options.emplace_back(RenderStatistic::AREA);
    }
    if (Settings::Settings::summaryBoundingBox.value()) {
      options.emplace_back(RenderStatistic::BOUNDING_BOX);
    }
    renderStatistic.printAll(root_geom, qglview->cam, options);
    LOG("Rendering finished.");

    this->rootGeom = root_geom;
#if defined(USE_POLYSET_FOR_CGAL)
    this->geomRenderer = std::make_shared<PolySetRenderer>(this->rootGeom);
#else
    // Choose PolySetRenderer for PolySet and Polygon2d, and for Manifold since we
    // know that all geometries are convertible to PolySet.
    if (RenderSettings::inst()->backend3D == RenderBackend3D::ManifoldBackend ||
        std::dynamic_pointer_cast<const PolySet>(this->rootGeom) ||
        std::dynamic_pointer_cast<const Polygon2d>(this->rootGeom)) {
      this->geomRenderer = std::make_shared<PolySetRenderer>(this->rootGeom);
    } else {
      this->geomRenderer = std::make_shared<CGALRenderer>(this->rootGeom);
    }
#endif

    // Go to CGAL view mode
    viewModeRender();
    resetMeasurementsState(true, "Click to start measuring");
    applyPendingFitView();
  } else {
    resetMeasurementsState(false, "No top level geometry; render something to enable measurements");
    LOG(message_group::UI_Warning, "No top level geometry to render");
    pendingFitViewAfterRender = false;
  }

  updateStatusBar(nullptr);

  const bool renderSoundEnabled =
    GlobalPreferences::inst()->getValue("advanced/enableSoundNotification").toBool();
  const uint soundThreshold =
    GlobalPreferences::inst()->getValue("advanced/timeThresholdOnRenderCompleteSound").toUInt();
  if (renderSoundEnabled && soundThreshold <= renderStatistic.ms().count() / 1000) {
    renderCompleteSoundEffect->play();
  }

  renderedEditor = activeEditor;
  activeEditor->contentsRendered = true;
  compileEnded();
}

void MainWindow::handleMeasurementClicked(QAction *clickedAction)
{
  // If we're unchecking, just stop.
  if (activeMeasurement == clickedAction) {
    resetMeasurementsState(true, "Click to start measuring");
    return;
  }

  resetMeasurementsState(true, "Click to start measuring");
  // Measure tools turn off pan mode.
  if (viewActionPan && viewActionPan->isChecked()) {
    viewActionPan->setChecked(false);
  }
  clickedAction->setToolTip("Click to cancel measurement");
  clickedAction->setChecked(true);
  activeMeasurement = clickedAction;

  if (clickedAction == designActionMeasureDist) {
    meas.startMeasureDist();
  } else if (clickedAction == designActionMeasureAngle) {
    meas.startMeasureAngle();
  }
}

void MainWindow::leftClick(QPoint mouse)
{
  auto state = meas.statemachine(mouse);
  if (state.status != Measurement::Result::Status::NoChange) {
    this->qglview->measure_state = Measurement::MEASURE_DIRTY;
    QMenu resultmenu(this);
    // Ensures we clean the display regardless of how menu gets closed.
    connect(&resultmenu, &QMenu::aboutToHide, this, &MainWindow::measureFinished);

    // Create the context menu and write successful measurements to the console.
    // boost adaptor can eventually be replaced with C++20 std::views::reverse
    bool first = true;
    for (const auto& msg : boost::adaptors::reverse(state.messages)) {
      auto str = msg.display_text;
      if (state.status == Measurement::Result::Status::Success) {
        if (auto m = make_message_obj(first ? "%1$s" : "  %1$s", str.toStdString())) {
          this->consoleOutput(*m);
        }
      }
      auto action = resultmenu.addAction(str);
      auto clipboard = msg.clipboard_text ? *msg.clipboard_text : str;
      connect(action, &QAction::triggered, this,
              [clipboard]() { QApplication::clipboard()->setText(clipboard); });
      first = false;
    }
    resultmenu.addAction("Click any above to copy its data to the clipboard");
    resultmenu.exec(qglview->mapToGlobal(mouse));
    resetMeasurementsState(true, "Click to start measuring");
  }
}

/**
 * Call the mouseselection to determine the id of the clicked-on object.
 * Use the generated ID and try to find it within the list of products
 * And finally move the cursor to the beginning of the selected object in the editor
 */
void MainWindow::rightClick(QPoint position)
{
  // selecting without a renderer?!
  if (!this->qglview->renderer) {
    return;
  }
  // Nothing to select
  if (!this->rootProduct) {
    return;
  }

  // Select the object at mouse coordinates
  const int index = this->qglview->pickObject(position);
  std::deque<std::shared_ptr<const AbstractNode>> path;
  const std::shared_ptr<const AbstractNode> result = this->rootNode->getNodeByID(index, path);

  if (result) {
    // Create context menu with the backtrace
    QMenu tracemenu(this);
    std::stringstream ss;
    for (auto& step : path) {
      // Skip certain node types
      if (step->name() == "root") {
        continue;
      }
      const bool hasSourceRef = step->modinst && !step->modinst->location().isNone();
      if (!hasSourceRef) {
        // Show an entry so the backtrace stays complete; no jump/highlight (no "id", no hover)
        std::string name;
        if (step->modinst) {
          const std::string vname = step->verbose_name();
          const int first_position = (vname.find("module") == std::string::npos) ? 0 : 7;
          name = vname.empty() ? step->modinst->name() : vname.substr(first_position);
        } else {
          const std::string vname = step->verbose_name();
          const int first_position = (vname.find("module") == std::string::npos) ? 0 : 7;
          name = vname.empty() ? "?" : vname.substr(first_position);
        }
        ss.str("");
        ss << name << " (no source reference)";
        tracemenu.addAction(QString::fromStdString(ss.str()));
        continue;
      }
      auto location = step->modinst->location();
      ss.str("");

      // Remove the "module" prefix if any as it induce confusion between the module declaration and
      // instanciation
      const int first_position = (step->verbose_name().find("module") == std::string::npos) ? 0 : 7;
      std::string name = step->verbose_name().substr(first_position);

      // It happens that the verbose_name is empty (eg: in for loops), when this happens instead of
      // letting empty entry in the menu we prefer using the name in the modinstanciation.
      if (step->verbose_name().empty()) name = step->modinst->name();

      // Check if the path is contained in a library (using parsersettings.h)
      const fs::path libpath = get_library_for_path(location.filePath());
      if (!libpath.empty()) {
        // Display the library (without making the window too wide!)
        ss << name << " (library " << location.fileName().substr(libpath.string().length() + 1) << ":"
           << location.firstLine() << ")";
      } else if (renderedEditor->filepath.toStdString() == location.fileName()) {
        // removes the "module" prefix if any as it makes it not clear if it is module declaration or
        // call.
        ss << name << " (" << location.filePath().filename().string() << ":" << location.firstLine()
           << ")";
      } else {
        auto relative_filename =
          fs_uncomplete(location.filePath(),
                        fs::path(renderedEditor->filepath.toStdString()).parent_path())
            .generic_string();

        // Set the displayed name relative to the active editor window
        ss << name << " (" << relative_filename << ":" << location.firstLine() << ")";
      }
      // Prepare the action to be sent
      auto action = tracemenu.addAction(QString::fromStdString(ss.str()));
      if (editorDock->isVisible()) {
        action->setProperty("id", step->idx);
        connect(action, &QAction::hovered, this, &MainWindow::onHoveredObjectInSelectionMenu);
      }
    }

    // Before starting we need to lock the GUI to avoid interferance with reload/update
    // triggered by other part of the application (eg: changing the renderedEditor)
    GuiLocker::lock();

    // Execute this lambda function when the selection menu is closing.
    connect(&tracemenu, &QMenu::aboutToHide, [this]() {
      // remove the visual hints in the editor
      renderedEditor->clearAllSelectionIndicators();
      // unlock the GUI so the other part of the interface can now be updated.
      // (eg: changing the renderedEditor)
      GuiLocker::unlock();
    });
    tracemenu.exec(this->qglview->mapToGlobal(position));
  } else {
    clearAllSelectionIndicators();
  }
}

void MainWindow::measureFinished()
{
  auto didSomething = meas.stopMeasure();
  if (didSomething) resetMeasurementsState(true, "Click to start measuring");
}

void MainWindow::clearAllSelectionIndicators()
{
  this->activeEditor->clearAllSelectionIndicators();
}

void MainWindow::setSelectionIndicatorStatus(EditorInterface *editor, int nodeIndex,
                                             EditorSelectionIndicatorStatus status)
{
  std::deque<std::shared_ptr<const AbstractNode>> stack;
  this->rootNode->getNodeByID(nodeIndex, stack);

  int level = 1;

  // first we flags all the nodes in the stack of the provided index
  // ends at size - 1 because we are not doing anything for the root node.
  // starts at 1 because we will process this one after later
  for (size_t i = 1; i < stack.size() - 1; i++) {
    const auto& node = stack[i];
    if (!node->modinst || node->modinst->location().isNone()) {
      level++;
      continue;
    }
    auto& location = node->modinst->location();
    if (location.filePath().compare(editor->filepath.toStdString()) != 0) {
      level++;
      continue;
    }

    if (node->verbose_name().rfind("module", 0) == 0 || node->modinst->name() == "children") {
      editor->setSelectionIndicatorStatus(status, level, location.firstLine() - 1,
                                          location.firstColumn() - 1, location.lastLine() - 1,
                                          location.lastColumn() - 1);
      level++;
    }
  }

  auto& node = stack[0];
  if (!node->modinst || node->modinst->location().isNone()) {
    return;
  }
  auto location = node->modinst->location();
  auto line = location.firstLine();
  auto column = location.firstColumn();
  auto lastLine = location.lastLine();
  auto lastColumn = location.lastColumn();

  // Update the location returned by location to cover the whole section.
  node->getCodeLocation(0, 0, &line, &column, &lastLine, &lastColumn, 0);

  editor->setSelectionIndicatorStatus(status, 0, line - 1, column - 1, lastLine - 1, lastColumn - 1);
}

void MainWindow::setSelection(int index)
{
  assert(renderedEditor != nullptr);
  if (currentlySelectedObject == index) return;

  std::deque<std::shared_ptr<const AbstractNode>> path;
  const std::shared_ptr<const AbstractNode> selected_node = rootNode->getNodeByID(index, path);

  if (!selected_node) return;
  if (!selected_node->modinst || selected_node->modinst->location().isNone()) {
    return;
  }

  currentlySelectedObject = index;

  auto location = selected_node->modinst->location();
  auto file = location.fileName();
  auto line = location.firstLine();
  auto column = location.firstColumn();

  // Unsaved files do have the pwd as current path, therefore we will not open a new
  // tab on click
  if (!fs::is_directory(fs::path(file))) {
    tabManager->open(QString::fromStdString(file));
  }

  // removes all previsly configure selection indicators.
  renderedEditor->clearAllSelectionIndicators();
  renderedEditor->show();

  std::vector<std::shared_ptr<const AbstractNode>> nodesSameModule{};
  rootNode->findNodesWithSameMod(selected_node, nodesSameModule);

  // highlight in the text editor all the text fragment of the hierarchy of object with same mode.
  for (const auto& element : nodesSameModule) {
    if (element->index() != currentlySelectedObject) {
      setSelectionIndicatorStatus(renderedEditor, element->index(),
                                  EditorSelectionIndicatorStatus::IMPACTED);
    }
  }

  // highlight in the text editor only the fragment correponding to the selected stack.
  // this step must be done after all the impacted element have been marked.
  setSelectionIndicatorStatus(renderedEditor, currentlySelectedObject,
                              EditorSelectionIndicatorStatus::SELECTED);

  renderedEditor->setCursorPosition(line - 1, column - 1);
}

/**
 * Expects the sender to have properties "id" defined
 */
void MainWindow::onHoveredObjectInSelectionMenu()
{
  assert(renderedEditor != nullptr);
  auto *action = qobject_cast<QAction *>(sender());
  if (!action || !action->property("id").isValid()) {
    return;
  }

  setSelection(action->property("id").toInt());
}

void MainWindow::setLastFocus(QWidget *widget)
{
  this->lastFocus = widget;
}

/**
 * Switch version label and progress widget. When switching to the progress
 * widget, the new instance is passed by the caller.
 * In case of resetting back to the version label, nullptr will be passed and
 * multiple calls can happen. So this method must guard against adding the
 * version label multiple times.
 *
 * @param progressWidget a pointer to the progress widget to show or nullptr in
 * case the display should switch back to the version label.
 */
void MainWindow::updateStatusBar(ProgressWidget *progressWidget)
{
  auto sb = this->statusBar();
  if (progressWidget == nullptr) {
    if (this->progresswidget != nullptr) {
      sb->removeWidget(this->progresswidget);
      delete this->progresswidget;
      this->progresswidget = nullptr;
    }
    if (versionLabel == nullptr) {
      versionLabel =
        new QLabel("OpenSCAD " + QString::fromStdString(std::string(openscad_displayversionnumber)));
      QFont statusFont = versionLabel->font();
      statusFont.setPointSize(11);
      statusFont.setWeight(QFont::Normal);
      versionLabel->setFont(statusFont);
      sb->addPermanentWidget(this->versionLabel);
    }
  } else {
    if (this->versionLabel != nullptr) {
      sb->removeWidget(this->versionLabel);
      delete this->versionLabel;
      this->versionLabel = nullptr;
    }
    sb->addPermanentWidget(progressWidget);
  }
}

void MainWindow::exceptionCleanup()
{
  LOG("Execution aborted");
  LOG(" ");
  GuiLocker::unlock();
  if (designActionAutoReload->isChecked()) autoReloadTimer->start();
}

void MainWindow::UnknownExceptionCleanup(std::string msg)
{
  auto guard = scopedSetCurrentOutput();  // we need to show this error
  if (msg.size() == 0) {
    LOG(message_group::Error, "Compilation aborted by unknown exception");
  } else {
    LOG(message_group::Error, "Compilation aborted by exception: %1$s", msg);
  }
  LOG(" ");
  GuiLocker::unlock();
  if (designActionAutoReload->isChecked()) autoReloadTimer->start();
}

void MainWindow::showTextInWindow(const QString& type, const QString& content)
{
  auto e = new QTextEdit(this);
  e->setAttribute(Qt::WA_DeleteOnClose);
  e->setWindowFlags(Qt::Window);
  e->setTabStopDistance(tabStopWidth);
  e->setWindowTitle(type + " Dump");
  if (content.isEmpty()) e->setPlainText("No " + type + " to dump. Please try compiling first...");
  else e->setPlainText(content);

  e->setReadOnly(true);
  e->resize(600, 400);
  e->show();
}

void MainWindow::on_designActionDisplayAST_triggered()
{
  auto guard = scopedSetCurrentOutput();
  QString text = (rootFile) ? QString::fromStdString(rootFile->dump("")) : "";
  showTextInWindow("AST", text);
}

void MainWindow::on_designActionDisplayCSGTree_triggered()
{
  auto guard = scopedSetCurrentOutput();
  QString text = (rootNode) ? QString::fromStdString(tree.getString(*rootNode, "  ")) : "";
  showTextInWindow("CSG", text);
}

void MainWindow::on_designActionDisplayCSGProducts_triggered()
{
  auto guard = scopedSetCurrentOutput();
  // a small lambda to avoid code duplication
  auto constexpr dump = [](auto node) { return QString::fromStdString(node ? node->dump() : "N/A"); };
  auto text =
    QString(
      "\nCSG before normalization:\n%1\n\n\nCSG after normalization:\n%2\n\n\nCSG rendering "
      "chain:\n%3\n\n\nHighlights CSG rendering chain:\n%4\n\n\nBackground CSG rendering chain:\n%5\n")
      .arg(dump(csgRoot), dump(normalizedRoot), dump(rootProduct), dump(highlightsProducts),
           dump(backgroundProducts));
  showTextInWindow("CSG Products Dump", text);
}

void MainWindow::on_designCheckValidity_triggered()
{
  if (GuiLocker::isLocked()) return;
  const GuiLocker lock;
  auto guard = scopedSetCurrentOutput();

  if (!rootGeom) {
    LOG("Nothing to validate! Try building first (press F6).");
    return;
  }

  if (rootGeom->getDimension() != 3) {
    LOG("Current top level object is not a 3D object.");
    return;
  }

  bool valid = true;
#ifdef ENABLE_CGAL
  if (auto N = std::dynamic_pointer_cast<const CGALNefGeometry>(rootGeom)) {
    valid = N->p3 ? const_cast<CGAL_Nef_polyhedron3&>(*N->p3).is_valid() : false;
  } else
#endif
#ifdef ENABLE_MANIFOLD
    if (auto mani = std::dynamic_pointer_cast<const ManifoldGeometry>(rootGeom)) {
    valid = mani->isValid();
  }
#endif
  LOG("Valid:      %1$6s", (valid ? "yes" : "no"));
}

// Returns if we can export (true) or not(false) (bool)
// Separated into it's own function for re-use.
bool MainWindow::canExport(unsigned int dim)
{
  auto guard = scopedSetCurrentOutput();
  if (!rootGeom) {
    LOG(message_group::Error, "Nothing to export! Try rendering first (press F6)");
    return false;
  }

  // editor has changed since last render
  if (!activeEditor->contentsRendered) {
    auto ret = QMessageBox::warning(this, "Application",
                                    "The current tab has been modified since its last render (F6).\n"
                                    "Do you really want to export the previous content?",
                                    QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) {
      return false;
    }
  }

  // other tab contents most recently rendered
  if (renderedEditor != activeEditor) {
    auto ret = QMessageBox::warning(this, "Application",
                                    "The rendered data is of different tab.\n"
                                    "Do you really want to export the another tab's content?",
                                    QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) {
      return false;
    }
  }

  if (rootGeom->getDimension() != dim) {
    LOG(message_group::UI_Error, "Current top level object is not a %1$dD object.", dim);
    return false;
  }

  if (rootGeom->isEmpty()) {
    LOG(message_group::UI_Error, "Current top level object is empty.");
    return false;
  }

#ifdef ENABLE_CGAL
  auto N = dynamic_cast<const CGALNefGeometry *>(rootGeom.get());
  if (N && !N->p3->is_simple()) {
    LOG(message_group::UI_Warning,
        "Object may not be a valid 2-manifold and may need repair! See "
        "https://en.wikibooks.org/wiki/OpenSCAD_User_Manual/STL_Import_and_Export");
  }
#endif
#ifdef ENABLE_MANIFOLD
  auto manifold = dynamic_cast<const ManifoldGeometry *>(rootGeom.get());
  if (manifold && !manifold->isValid()) {
    LOG(message_group::UI_Warning,
        "Object may not be a valid manifold and may need repair! "
        "Error message: %1$s. See "
        "https://en.wikibooks.org/wiki/OpenSCAD_User_Manual/STL_Import_and_Export",
        ManifoldUtils::statusToString(manifold->getManifold().Status()));
  }
#endif

  return true;
}

void MainWindow::actionExport(unsigned int dim, ExportInfo& exportInfo)
{
  const auto type_name = QString::fromStdString(exportInfo.info.description);
  const auto suffix = QString::fromStdString(exportInfo.info.suffix);

  // Setting filename skips the file selection dialog and uses the path provided instead.
  if (GuiLocker::isLocked()) return;
  const GuiLocker lock;

  auto guard = scopedSetCurrentOutput();

  // Return if something is wrong and we can't export.
  if (!canExport(dim)) return;

  auto title = QString(_("Export %1 File")).arg(type_name);
  auto filter = QString(_("%1 Files (*%2)")).arg(type_name, suffix);
  auto exportFilename = QFileDialog::getSaveFileName(this, title, exportPath(suffix), filter);
  auto guard2 = scopedSetCurrentOutput();
  if (exportFilename.isEmpty()) {
    return;
  }
  this->exportPaths[suffix] = exportFilename;

  const bool exportResult = exportFileByName(rootGeom, exportFilename.toStdString(), exportInfo);

  if (exportResult) fileExportedMessage(type_name, exportFilename);
}

void MainWindow::actionExportFileFormat(int fmt)
{
  const auto format = static_cast<FileFormat>(fmt);
  const FileFormatInfo& info = fileformat::info(format);

  ExportInfo exportInfo =
    createExportInfo(format, info, activeEditor->filepath.toStdString(), &qglview->cam, {});

  switch (format) {
  case FileFormat::PDF: {
    ExportPdfDialog exportPdfDialog;
    if (exportPdfDialog.exec() == QDialog::Rejected) {
      return;
    }

    exportInfo.optionsPdf = exportPdfDialog.getOptions();
    actionExport(2, exportInfo);
  } break;
  case FileFormat::_3MF: {
    Export3mfDialog export3mfDialog;
    if (export3mfDialog.exec() == QDialog::Rejected) {
      return;
    }

    exportInfo.options3mf = export3mfDialog.getOptions();
    actionExport(3, exportInfo);
  } break;
  case FileFormat::CSG: {
    auto guard = scopedSetCurrentOutput();

    if (!this->rootNode) {
      LOG(message_group::Error, "Nothing to export. Please try compiling first.");
      return;
    }
    const QString suffix = "csg";
    auto csg_filename = QFileDialog::getSaveFileName(this, _("Export CSG File"), exportPath(suffix),
                                                     _("CSG Files (*.csg)"));

    if (csg_filename.isEmpty()) {
      return;
    }

    auto guard2 = scopedSetCurrentOutput();
    std::ofstream fstream(std::filesystem::u8path(csg_filename.toStdString()));
    if (!fstream.is_open()) {
      LOG("Can't open file \"%1$s\" for export", csg_filename.toStdString());
    } else {
      fstream << this->tree.getString(*this->rootNode, "\t") << "\n";
      fstream.close();
      fileExportedMessage("CSG", csg_filename);
      this->exportPaths[suffix] = csg_filename;
    }

  } break;
  case FileFormat::PNG: {
    // Grab first to make sure dialog box isn't part of the grabbed image
    qglview->grabFrame();
    const QString suffix = "png";
    auto img_filename =
      QFileDialog::getSaveFileName(this, _("Export Image"), exportPath(suffix), _("PNG Files (*.png)"));
    if (!img_filename.isEmpty()) {
      const bool saveResult = qglview->save(img_filename.toStdString().c_str());
      if (saveResult) {
        this->exportPaths[suffix] = img_filename;
        auto guard = scopedSetCurrentOutput();
        fileExportedMessage("PNG", img_filename);
      } else {
        LOG("Can't open file \"%1$s\" for export image", img_filename.toStdString());
      }
    }
  } break;
  case FileFormat::SVG: {
    ExportSvgDialog exportSvgDialog;
    if (exportSvgDialog.exec() == QDialog::Rejected) {
      return;
    }
    exportInfo.optionsSvg = std::make_shared<ExportSvgOptions>(exportSvgDialog.getOptions());
    actionExport(2, exportInfo);
  } break;
  default: actionExport(fileformat::is3D(format) ? 3 : fileformat::is2D(format) ? 2 : 0, exportInfo);
  }
}

void MainWindow::on_fileActionExportDrawingPDF_triggered()
{
  if (GuiLocker::isLocked()) return;
  const GuiLocker lock;

  auto guard = scopedSetCurrentOutput();

  if (!rootGeom) {
    LOG(message_group::Error, "Nothing to export! Try rendering first (press F6)");
    return;
  }

  if (!activeEditor->contentsRendered) {
    auto ret = QMessageBox::warning(this, "Application",
                                    "The current tab has been modified since its last render (F6).\n"
                                    "Do you really want to export the previous content?",
                                    QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) {
      return;
    }
  }

  if (renderedEditor != activeEditor) {
    auto ret = QMessageBox::warning(this, "Application",
                                    "The rendered data is of different tab.\n"
                                    "Do you really want to export the another tab's content?",
                                    QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) {
      return;
    }
  }

  const unsigned int dim = rootGeom->getDimension();
  if (dim != 2 && dim != 3) {
    LOG(message_group::UI_Error, "Current top level object is not a 2D or 3D object.");
    return;
  }

  if (rootGeom->isEmpty()) {
    LOG(message_group::UI_Error, "Current top level object is empty.");
    return;
  }

  const QString suffix = "pdf";
  auto exportFilename =
    QFileDialog::getSaveFileName(this, _("Export 2D Drawing PDF"), exportPath(suffix),
                                 _("PDF Files (*.pdf)"));
  auto guard2 = scopedSetCurrentOutput();
  if (exportFilename.isEmpty()) {
    return;
  }
  this->exportPaths[suffix] = exportFilename;

  OrthographicDrawingViews views = GeometryProjection::projectOrthographicViews(rootGeom);
  if (!views.top || views.top->isEmpty()) {
    LOG(message_group::Error, "Failed to create 2D projection for drawing export.");
    return;
  }

  const FileFormatInfo& info = fileformat::info(FileFormat::PDF);
  ExportInfo exportInfo =
    createExportInfo(FileFormat::PDF, info, activeEditor->filepath.toStdString(), &qglview->cam, {});
  exportInfo.title = QFileInfo(exportFilename).completeBaseName().toStdString();

  std::ios::openmode mode = std::ios::out | std::ios::trunc | std::ios::binary;
  std::ofstream fstream(std::filesystem::u8path(exportFilename.toStdString()), mode);
  if (!fstream.is_open()) {
    LOG(_("Can't open file \"%1$s\" for export"), exportFilename.toStdString());
    return;
  }

  bool onerror = false;
  fstream.exceptions(std::ios::badbit | std::ios::failbit);
  try {
    export_drawing_pdf(views, fstream, exportInfo);
  } catch (std::ios::failure&) {
    onerror = true;
  }
  try {
    fstream.close();
  } catch (std::ios::failure&) {
    onerror = true;
  }

  if (onerror) {
    LOG(message_group::Error, _("\"%1$s\" write error. (Disk full?)"), exportFilename.toStdString());
    return;
  }

  fileExportedMessage(_("2D Drawing PDF"), exportFilename);
}

void MainWindow::on_editActionCopy_triggered()
{
  if (tryFocusedTextEditClipboard(QStringLiteral("copy"))) {
    return;
  }
  auto *c = dynamic_cast<Console *>(lastFocus);
  if (c) {
    c->copy();
  } else {
    tabManager->copy();
  }
}

void MainWindow::on_editActionCut_triggered()
{
  if (tryFocusedTextEditClipboard(QStringLiteral("cut"))) {
    return;
  }
  tabManager->cut();
}

void MainWindow::on_editActionPaste_triggered()
{
  if (tryFocusedTextEditClipboard(QStringLiteral("paste"))) {
    return;
  }
  tabManager->paste();
}

bool MainWindow::tryFocusedTextEditClipboard(const QString& op)
{
  QWidget *focus = QApplication::focusWidget();
  if (!focus) return false;

  // Keep code-editor shortcuts on the Scintilla tab manager.
  if (editorDock && (focus == editorDock || editorDock->isAncestorOf(focus))) {
    return false;
  }

  if (auto *plain = qobject_cast<QPlainTextEdit *>(focus)) {
    if (op == QLatin1String("cut")) plain->cut();
    else if (op == QLatin1String("copy")) plain->copy();
    else if (op == QLatin1String("paste")) plain->paste();
    else return false;
    return true;
  }
  if (auto *line = qobject_cast<QLineEdit *>(focus)) {
    if (op == QLatin1String("cut")) line->cut();
    else if (op == QLatin1String("copy")) line->copy();
    else if (op == QLatin1String("paste")) line->paste();
    else return false;
    return true;
  }
  if (auto *text = qobject_cast<QTextEdit *>(focus)) {
    if (op == QLatin1String("cut")) text->cut();
    else if (op == QLatin1String("copy")) text->copy();
    else if (op == QLatin1String("paste")) text->paste();
    else return false;
    return true;
  }
  if (op == QLatin1String("copy")) {
    if (auto *label = qobject_cast<QLabel *>(focus)) {
      if (label->hasSelectedText()) {
        QApplication::clipboard()->setText(label->selectedText());
        return true;
      }
    }
  }
  return false;
}

void MainWindow::on_editActionCopyViewport_triggered()
{
  const auto& image = qglview->grabFrame();
  auto clipboard = QApplication::clipboard();
  clipboard->setImage(image);
}

void MainWindow::on_designActionFlushCaches_triggered()
{
  auto guard = scopedSetCurrentOutput();
  GeometryCache::instance()->clear();
  CGALCache::instance()->clear();
  dxf_dim_cache.clear();
  dxf_cross_cache.clear();
  SourceFileCache::instance()->clear();

  LOG("Caches Flushed");
}

void MainWindow::viewModeActionsUncheck()
{
  previewModeGroup->setEnabled(false);
}

#ifdef ENABLE_OPENCSG

void MainWindow::viewModeRender()
{
  previewModeGroup->setEnabled(false);
  this->qglview->setRenderer(this->geomRenderer);
  this->qglview->updateColorScheme();
  this->qglview->update();
}

/*!
   Go to the OpenCSG view mode.
   Falls back to thrown together mode if OpenCSG is not available
 */
void MainWindow::on_viewActionPreview_triggered()
{
  viewModePreview();
}

void MainWindow::viewModePreview()
{
  previewModeGroup->setEnabled(true);
  if (this->qglview->hasOpenCSGSupport()) {
    viewActionPreview->setChecked(true);
    this->qglview->setRenderer(this->previewRenderer ? this->previewRenderer
                                                     : this->thrownTogetherRenderer);
    this->qglview->updateColorScheme();
    this->qglview->update();
  } else {
    viewModeThrownTogether();
  }
}

#endif /* ENABLE_OPENCSG */

void MainWindow::updateViewModeAfterGLInit()
{
#ifdef ENABLE_OPENCSG
  viewActionPreview->setEnabled(this->qglview->hasOpenCSGSupport());
  if (this->qglview->hasOpenCSGSupport()) {
    viewModePreview();
  }
#endif
}

void MainWindow::on_viewActionThrownTogether_triggered()
{
  viewModeThrownTogether();
}

void MainWindow::viewModeThrownTogether()
{
  previewModeGroup->setEnabled(true);
  viewActionThrownTogether->setChecked(true);
  this->qglview->setRenderer(this->thrownTogetherRenderer);
  this->qglview->updateColorScheme();
  this->qglview->update();
}

void MainWindow::on_viewActionShowEdges_toggled(bool checked)
{
  QSettingsCached settings;
  settings.setValue("view/showEdges", checked);
  this->qglview->setShowEdges(checked);
  this->qglview->update();
}

void MainWindow::on_viewActionShowAxes_toggled(bool checked)
{
  QSettingsCached settings;
  settings.setValue("view/showAxes", checked);
  this->viewActionShowScaleProportional->setEnabled(checked);
  this->qglview->setShowAxes(checked);
  this->qglview->update();
}

void MainWindow::on_viewActionShowFloor_toggled(bool checked)
{
  QSettingsCached settings;
  settings.setValue("view/showFloor", checked);
  this->qglview->setShowFloor(checked);
  this->qglview->update();
}

void MainWindow::on_viewActionShowCrosshairs_toggled(bool checked)
{
  QSettingsCached settings;
  settings.setValue("view/showCrosshairs", checked);
  this->qglview->setShowCrosshairs(checked);
  this->qglview->update();
}

void MainWindow::on_viewActionShowScaleProportional_toggled(bool checked)
{
  QSettingsCached settings;
  settings.setValue("view/showScaleProportional", checked);
  this->qglview->setShowScaleProportional(checked);
  this->qglview->update();
}

bool MainWindow::isEmpty()
{
  return activeEditor->toPlainText().isEmpty();
}

void MainWindow::editorContentChanged()
{
  // this slot is called when the content of the active editor changed.
  // it rely on the activeEditor member to pick the new data.

  auto current_doc = activeEditor->toPlainText();
  if (current_doc != lastCompiledDoc) {
    animateWidget->editorContentChanged();

    // removes the live selection feedbacks in both the 3d view and editor.
    clearAllSelectionIndicators();
  }
}

void MainWindow::requestFitViewAfterRender()
{
  pendingFitViewAfterRender = true;
}

void MainWindow::fitViewToModel()
{
  pendingFitViewAfterRender = false;
  if (!this->qglview || !this->qglview->getRenderer()) return;
  this->qglview->viewAll();
  this->qglview->update();
  updateZoomPercentLabel();
}

void MainWindow::applyPendingFitView()
{
  if (!pendingFitViewAfterRender) return;
  fitViewToModel();
}

void MainWindow::on_viewActionTop_triggered()
{
  qglview->cam.object_rot << 90, 0, 0;
  this->qglview->viewAll();
  this->qglview->update();
}

void MainWindow::on_viewActionBottom_triggered()
{
  qglview->cam.object_rot << 270, 0, 0;
  this->qglview->viewAll();
  this->qglview->update();
}

void MainWindow::on_viewActionLeft_triggered()
{
  qglview->cam.object_rot << 0, 0, 90;
  this->qglview->viewAll();
  this->qglview->update();
}

void MainWindow::on_viewActionRight_triggered()
{
  qglview->cam.object_rot << 0, 0, 270;
  this->qglview->viewAll();
  this->qglview->update();
}

void MainWindow::on_viewActionFront_triggered()
{
  qglview->cam.object_rot << 0, 0, 0;
  this->qglview->viewAll();
  this->qglview->update();
}

void MainWindow::on_viewActionBack_triggered()
{
  qglview->cam.object_rot << 0, 0, 180;
  this->qglview->viewAll();
  this->qglview->update();
}

void MainWindow::on_viewActionDiagonal_triggered()
{
  qglview->cam.object_rot << 35, 0, -25;
  this->qglview->viewAll();
  this->qglview->update();
}

void MainWindow::on_viewActionCenter_triggered()
{
  qglview->cam.object_trans << 0, 0, 0;
  this->qglview->viewAll();
  this->qglview->update();
}

void MainWindow::setProjectionType(ProjectionType mode)
{
  const bool isOrthogonal = ProjectionType::ORTHOGONAL == mode;
  QSettingsCached settings;
  settings.setValue("view/orthogonalProjection", isOrthogonal);
  qglview->setOrthoMode(isOrthogonal);
  qglview->update();
}

void MainWindow::on_viewActionPerspective_toggled(bool checked)
{
  if (checked) {
    setProjectionType(ProjectionType::PERSPECTIVE);
  }
}

void MainWindow::on_viewActionOrthogonal_toggled(bool checked)
{
  if (checked) {
    setProjectionType(ProjectionType::ORTHOGONAL);
  }
}

void MainWindow::viewTogglePerspective()
{
  const QSettingsCached settings;
  const bool isOrthogonal = settings.value("view/orthogonalProjection").toBool();
  if (isOrthogonal) {
    viewActionOrthogonal->setChecked(true);
  } else {
    viewActionPerspective->setChecked(true);
  }
}

void MainWindow::on_viewActionResetView_triggered()
{
  this->qglview->resetView();
  this->qglview->update();
}

void MainWindow::on_viewActionViewAll_triggered()
{
  this->qglview->viewAll();
  this->qglview->update();
  updateZoomPercentLabel();
}

void MainWindow::on_viewActionPan_toggled(bool checked)
{
  if (!this->qglview) return;
  if (checked) {
    // Pan tool and measure tools are mutually exclusive.
    resetMeasurementsState(true, "Click to start measuring");
  }
  this->qglview->setPanToolActive(checked);
}

void MainWindow::on_viewActionHideEditorToolBar_toggled(bool checked)
{
  Q_UNUSED(checked);
  // Editor toolbar is permanently hidden; File/Edit/Design menus replace it.
  if (this->editortoolbar) this->editortoolbar->hide();
}

void MainWindow::on_viewActionHide3DViewToolBar_toggled(bool checked)
{
  QSettingsCached settings;
  settings.setValue("view/hide3DViewToolbar", checked);

  if (checked) {
    viewerToolBar->hide();
  } else {
    viewerToolBar->show();
  }
}

void MainWindow::showLink(const QString& link)
{
  if (link == "#console") {
    showBottomPanelTab(BottomPanelHeader::ConsoleTab);
  } else if (link == "#errorlog") {
    showBottomPanelTab(BottomPanelHeader::ErrorLogTab);
  } else if (link == "#colorlist") {
    colorListDock->show();
  }
}

void MainWindow::onEditorDockVisibilityChanged(bool isVisible)
{
  // AI-first layout: never allow closing the editor dock
  if (!isVisible) {
    QTimer::singleShot(0, this, [this]() {
      if (this->editorDock) {
        this->editorDock->show();
      }
    });
    return;
  }

  auto e = (ScintillaEditor *)this->activeEditor;
  e->qsci->setReadOnly(false);
  e->setupAutoComplete(false);
  editorDock->raise();
  tabManager->setFocus();
  updateExportActions();
}

void MainWindow::onConsoleDockVisibilityChanged(bool isVisible)
{
  if (isVisible) {
    frameCompileResult->hide();
    consoleDock->raise();
    if (bottomPanelHeader && bottomPanelHeader->activeTab() == BottomPanelHeader::ErrorLogTab &&
        errorLogWidget) {
      errorLogWidget->setFocus();
    } else if (console) {
      console->setFocus();
    }
  }
}

void MainWindow::onErrorLogDockVisibilityChanged(bool isVisible)
{
  // Error Log lives inside the bottom panel now; redirect legacy dock show requests.
  if (isVisible) {
    errorLogDock->hide();
    showBottomPanelTab(BottomPanelHeader::ErrorLogTab);
  }
}

void MainWindow::onAnimateDockVisibilityChanged(bool isVisible)
{
  if (isVisible) {
    animateDock->raise();
    animateWidget->setFocus();
  }
}

void MainWindow::onFontListDockVisibilityChanged(bool isVisible)
{
  if (isVisible) {
    fontListWidget->update_font_list();
    fontListWidget->setFocus();
    fontListDock->raise();
  }
}

void MainWindow::onColorListDockVisibilityChanged(bool isVisible)
{
  if (isVisible) {
    colorListWidget->setFocus();
    colorListDock->raise();
  }
}

void MainWindow::onViewportControlDockVisibilityChanged(bool isVisible)
{
  if (isVisible) {
    viewportControlDock->raise();
    viewportControlWidget->setFocus();
  }
}

void MainWindow::onParametersDockVisibilityChanged(bool isVisible)
{
  if (isVisible) {
    parameterDock->raise();
    activeEditor->parameterWidget->scrollArea->setFocus();
  }
}

void MainWindow::onAIDockVisibilityChanged(bool isVisible)
{
  updateAIChatRevealButton(!isVisible);
}

void MainWindow::onExperimentalChanged()
{
  bool aiEnabled = Feature::ExperimentalAiFeatures.is_enabled();
  if (this->aiDock) {
    this->aiDock->toggleViewAction()->setVisible(aiEnabled);
    if (aiEnabled) {
      this->aiDock->show();
      this->aiDock->raise();
    } else {
      this->aiDock->hide();
    }
    if (this->navigationMenu) {
      for (auto *action : this->navigationMenu->actions()) {
        if (action->text() == _("&AI Chat")) {
          action->setVisible(aiEnabled);
        }
      }
    }
  }
  updateAIChatRevealButton(aiEnabled && this->aiDock && !this->aiDock->isVisible());
}

void MainWindow::onColorListColorSelected(const QString& selectedColor)
{
  activeEditor->insertOrReplaceText(selectedColor);
}

// Use the sender's to detect if we are moving forward/backward in docks
// and search for the next dock to "activate" or "emphasize"
// If no dock can be found, returns the first one.
Dock *MainWindow::getNextDockFromSender(QObject *sender)
{
  int direction = 0;

  auto *action = qobject_cast<QAction *>(sender);
  if (action != nullptr) {
    direction = (action == windowActionNextWindow) ? 1 : -1;
  } else {
    auto *shortcut = qobject_cast<QShortcut *>(sender);
    direction = (shortcut == shortcutNextWindow) ? 1 : -1;
  }

  return findVisibleDockToActivate(direction);
}

void MainWindow::onWindowActionNextPrevHovered()
{
  auto dock = getNextDockFromSender(sender());

  // This can happens if there is no visible dock at all
  if (dock == nullptr) return;

  // Hover signal is emitted at each mouse move, to avoid excessive
  // load we only raise/emphasize if it is not yet done.
  if (rubberBandManager.isEmphasized(dock)) return;

  dock->raise();
  rubberBandManager.emphasize(dock);
}

void MainWindow::onWindowActionNextPrevTriggered()
{
  auto dock = getNextDockFromSender(sender());

  // This can happens if there is no visible dock at all
  if (dock == nullptr) return;

  activateDock(dock);
}

void MainWindow::onWindowShortcutNextPrevActivated()
{
  auto dock = getNextDockFromSender(sender());

  // This can happens if there is no visible dock at all
  if (dock == nullptr) return;

  activateDock(dock);
  rubberBandManager.emphasize(dock);
}

QAction *MainWindow::formatIdentifierToAction(const std::string& identifier) const
{
  FileFormat format;
  if (fileformat::fromIdentifier(identifier, format)) {
    const auto it = exportMap.find(format);
    if (it != exportMap.end()) {
      return it->second;
    }
  }
  return nullptr;
}

void MainWindow::onWindowShortcutExport3DActivated()
{
  QAction *action = formatIdentifierToAction(Settings::Settings::toolbarExport3D.value());
  if (action) {
    action->trigger();
  }
}

void MainWindow::on_editActionInsertTemplate_triggered()
{
  activeEditor->displayTemplates();
}

void MainWindow::on_editActionFoldAll_triggered()
{
  activeEditor->foldUnfold();
}

QString MainWindow::getCurrentFileName() const
{
  if (activeEditor == nullptr) return {};

  const QFileInfo fileInfo(activeEditor->filepath);
  QString fname = _("Untitled.scad");
  if (!fileInfo.fileName().isEmpty()) fname = fileInfo.fileName();
  return fname.replace("&", "&&");
}

/**
 * Convert a dock title to a base name for action naming.
 * Removes mnemonic markers (&) and hyphens, creating a camelCase name.
 * Examples: "&Editor" -> "Editor", "Error-&Log" -> "ErrorLog"
 */
QString MainWindow::getDockBaseName(const QString& title) const
{
  QString baseName = title;
  // Remove mnemonic marker
  baseName.remove('&');
  // Remove hyphens
  baseName.remove('-');
  // Remove spaces
  baseName.remove(' ');
  return baseName;
}

void MainWindow::onTabManagerAboutToCloseEditor(EditorInterface *closingEditor)
{
  // This slots is in charge of closing properly the preview when the
  // associated editor is about to close.
  if (closingEditor == renderedEditor) {
    renderedEditor = nullptr;

    // Invalidate renderers before we kill the CSG tree
    this->qglview->setRenderer(nullptr);
#ifdef ENABLE_OPENCSG
    this->previewRenderer = nullptr;
#endif
    this->thrownTogetherRenderer = nullptr;

    // Remove previous CSG tree
    this->absoluteRootNode.reset();

    this->csgRoot.reset();
    this->normalizedRoot.reset();
    this->rootProduct.reset();

    this->rootNode.reset();
    this->tree.setRoot(nullptr);
    this->qglview->update();
  }
}

void MainWindow::onTabManagerEditorContentReloaded(EditorInterface *reloadedEditor)
{
  try {
    // when a new editor is created, it is important to compile the initial geometry
    // so the customizer panels are ok.
    parseDocument(reloadedEditor);
  } catch (const HardWarningException&) {
    exceptionCleanup();
  } catch (const std::exception& ex) {
    UnknownExceptionCleanup(ex.what());
  } catch (...) {
    UnknownExceptionCleanup();
  }

  // updates the content of the Recents Files menu to integrate the one possibly
  // associated with the created editor. The reason is that an editor can be created
  // or updated without a file associated with it.
  updateRecentFileActions();
}

void MainWindow::onTabManagerEditorChanged(EditorInterface *newEditor)
{
  activeEditor = newEditor;

  if (newEditor == nullptr) return;

  parameterDock->setWidget(newEditor->parameterWidget);
  editActionUndo->setEnabled(newEditor->canUndo());

  const QString name = getCurrentFileName();
  refreshWindowTitle();

  consoleDock->setNameSuffix(name);
  errorLogDock->setNameSuffix(name);
  animateDock->setNameSuffix(name);
  fontListDock->setNameSuffix(name);
  colorListDock->setNameSuffix(name);
  viewportControlDock->setNameSuffix(name);

  // Keep AI project target in sync with the visible editor tab.
  if (!newEditor->filepath.isEmpty()) {
    ProjectManager::instance().setActiveFile(newEditor->filepath);
  }

  // If there is no renderedEditor we request for a new preview if the
  // auto-reload is enabled.
  if (renderedEditor == nullptr && designActionAutoReload->isChecked() && !MainWindow::isEmpty()) {
    actionRenderPreview();
  }
}

Dock *MainWindow::findVisibleDockToActivate(int offset) const
{
  const auto dockCount = docks.size();
  int focusedDockIndex = 0;

  // search among the docks the one that is having the focus. This is done by
  // traversing the widget hierarchy from the focused widget up to the docks that
  // contains it.
  const auto focusWidget = QApplication::focusWidget();
  for (auto widget = focusWidget; widget != nullptr; widget = widget->parentWidget()) {
    for (unsigned int index = 0; index < dockCount; ++index) {
      if (docks[index].first == focusWidget) {
        focusedDockIndex = index;
      }
    }
  }

  for (size_t o = 1; o < dockCount; ++o) {
    // starting from dockCount + focusedDockIndex move left or right (o*offset)
    // to find the first visible one. dockCount is there so there is no situation in which
    // (-1) % dockCount
    const int target = (dockCount + focusedDockIndex + o * offset) % dockCount;
    const auto& dock = docks.at(target).first;

    if (dock->isVisible()) {
      return dock;
    }
  }
  return nullptr;
}

void MainWindow::activateDock(Dock *dock)
{
  if (dock == nullptr) return;

  // We always need to activate the window.
  if (dock->isFloating()) dock->activateWindow();
  else QMainWindow::activateWindow();

  dock->raise();
  dock->setFocus();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
  if (event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
  }
}

void MainWindow::dropEvent(QDropEvent *event)
{
  auto guard = scopedSetCurrentOutput();
  const QList<QUrl> urls = event->mimeData()->urls();
  for (const auto& url : urls) {
    handleFileDrop(url);
  }
}

void MainWindow::handleFileDrop(const QUrl& url)
{
  if (url.scheme() != "file") return;
  const auto fileName = url.toLocalFile();
  const auto fileInfo = QFileInfo{fileName};
  const auto suffix = fileInfo.suffix().toLower();
  const auto cmd = Importer::knownFileExtensions[suffix];
  if (cmd.isEmpty()) {
    tabManager->open(fileName);
  } else {
    activeEditor->insert(cmd.arg(fileName));
  }
}

void MainWindow::on_helpActionAbout_triggered()
{
  qApp->setWindowIcon(QApplication::windowIcon());
  auto dialog = new AboutDialog(this);
  dialog->exec();
  dialog->deleteLater();
}

void MainWindow::on_helpActionHomepage_triggered()
{
  UIUtils::openHomepageURL();
}

void MainWindow::on_helpActionManual_triggered()
{
  UIUtils::openUserManualURL();
}

void MainWindow::on_helpActionOfflineManual_triggered()
{
  UIUtils::openOfflineUserManual();
}

void MainWindow::on_helpActionCheatSheet_triggered()
{
  UIUtils::openCheatSheetURL();
}

void MainWindow::on_helpActionOfflineCheatSheet_triggered()
{
  UIUtils::openOfflineCheatSheet();
}

void MainWindow::on_helpActionLibraryInfo_triggered()
{
  if (!this->libraryInfoDialog) {
    const QString rendererInfo(qglview->getRendererInfo().c_str());
    auto dialog = new LibraryInfoDialog(rendererInfo);
    this->libraryInfoDialog = dialog;
  }
  this->libraryInfoDialog->show();
}

void MainWindow::on_editActionPreferences_triggered()
{
  GlobalPreferences::inst()->update();
  GlobalPreferences::inst()->show();
  GlobalPreferences::inst()->activateWindow();
  GlobalPreferences::inst()->raise();
}

void MainWindow::setColorScheme(const QString& scheme)
{
  RenderSettings::inst()->colorscheme = scheme.toStdString();
  this->qglview->setColorScheme(scheme.toStdString());
  this->qglview->update();
}

void MainWindow::setFont(const QString& family, uint size)
{
  QFont font;
  if (!family.isEmpty()) font.setFamily(family);
  else font.setFixedPitch(true);
  if (size > 0) font.setPointSize(size);
  font.setStyleHint(QFont::TypeWriter);
  activeEditor->setFont(font);
}

void MainWindow::consoleOutput(const Message& msgObj, void *userdata)
{
  // Invoke the method in the main thread in case the output
  // originates in a worker thread.
  auto thisp = static_cast<MainWindow *>(userdata);
  QMetaObject::invokeMethod(thisp, "consoleOutput", Q_ARG(Message, msgObj));
}

void MainWindow::consoleOutput(const Message& msgObj)
{
  this->console->addMessage(msgObj);
  if (msgObj.group == message_group::Warning || msgObj.group == message_group::Deprecated) {
    ++this->compileWarnings;
  } else if (msgObj.group == message_group::Error) {
    ++this->compileErrors;
  }
  // While an AI render runs, keep error/warning text so the agent can self-correct.
  if (aiRenderCapturing && aiRenderMessages.size() < 20 &&
      (msgObj.group == message_group::Error || msgObj.group == message_group::Warning ||
       msgObj.group == message_group::UI_Error || msgObj.group == message_group::UI_Warning)) {
    aiRenderMessages.push_back(msgObj.msg);
  }
  // FIXME: scad parsing/evaluation should be done on separate thread so as not to block the gui.
  // Then processEvents should no longer be needed here.
  this->processEvents();
  if (consoleUpdater && !consoleUpdater->isActive()) {
    consoleUpdater->start(50);  // Limit console updates to 20 FPS
  }
}

void MainWindow::consoleOutputRaw(const QString& html)
{
  this->console->addHtml(html);
  this->processEvents();
}

void MainWindow::errorLogOutput(const Message& log_msg, void *userdata)
{
  auto thisp = static_cast<MainWindow *>(userdata);
  QMetaObject::invokeMethod(thisp, "errorLogOutput", Q_ARG(Message, log_msg));
}

void MainWindow::errorLogOutput(const Message& log_msg)
{
  this->errorLogWidget->toErrorLog(log_msg);
}

void MainWindow::setCurrentOutput()
{
  set_output_handler(&MainWindow::consoleOutput, &MainWindow::errorLogOutput, this);
}

void MainWindow::hideCurrentOutput()
{
  set_output_handler(&MainWindow::noOutputConsole, &MainWindow::noOutputErrorLog, this);
}

void MainWindow::clearCurrentOutput()
{
  set_output_handler(nullptr, nullptr, nullptr);
}

void MainWindow::openCSGSettingsChanged()
{
#ifdef ENABLE_OPENCSG
  OpenCSG::setOption(OpenCSG::AlgorithmSetting,
                     GlobalPreferences::inst()->getValue("advanced/forceGoldfeather").toBool()
                       ? OpenCSG::Goldfeather
                       : OpenCSG::Automatic);
#endif
}

void MainWindow::processEvents()
{
  if (this->procevents) QApplication::processEvents();
}

QString MainWindow::exportPath(const QString& suffix)
{
  const auto path_it = this->exportPaths.find(suffix);
  const auto basename =
    activeEditor->filepath.isEmpty() ? "Untitled" : QFileInfo(activeEditor->filepath).completeBaseName();
  QString dir;
  if (path_it != exportPaths.end()) {
    dir = QFileInfo(path_it->second).absolutePath();
  } else if (activeEditor->filepath.isEmpty()) {
    dir = QString::fromStdString(PlatformUtils::userDocumentsPath());
  } else {
    dir = QFileInfo(activeEditor->filepath).absolutePath();
  }
  return QString("%1/%2.%3").arg(dir, basename, suffix);
}

void MainWindow::jumpToLine(int line, int col)
{
  this->activeEditor->setCursorPosition(line, col);
}

void MainWindow::resetMeasurementsState(bool enable, const QString& tooltipMessage)
{
  if (RenderSettings::inst()->backend3D != RenderBackend3D::ManifoldBackend) {
    enable = false;
    static const auto noCGALMessage =
      "Measurements only work with Manifold backend; Preferences->Advanced->3D Rendering->Backend";
    this->designActionMeasureDist->setToolTip(noCGALMessage);
    this->designActionMeasureAngle->setToolTip(noCGALMessage);
  } else {
    this->designActionMeasureDist->setToolTip(tooltipMessage);
    this->designActionMeasureAngle->setToolTip(tooltipMessage);
  }

  this->designActionMeasureDist->setEnabled(enable);
  this->designActionMeasureDist->setChecked(false);
  this->designActionMeasureAngle->setEnabled(enable);
  this->designActionMeasureAngle->setChecked(false);

  (void)meas.stopMeasure();
  activeMeasurement = nullptr;
}

/**
  Initialize the GUI from the .ui design file and other top-level GUI setup.
 */
void MainWindow::setupWindow()
{
  installEventFilter(this);
  setupUi(this);
  this->setAttribute(Qt::WA_DeleteOnClose);
  scadApp->windowManager.add(this);
  setAcceptDrops(true);

  // Compact editor + preview toolbars (VS Code-style header density)
  const QSize compactIconSize(16, 16);
  if (this->editortoolbar) {
    this->editortoolbar->setIconSize(compactIconSize);
  }
  if (this->viewerToolBar) {
    this->viewerToolBar->setIconSize(compactIconSize);
  }
}

/**
  Set up non-GUI elements like timers, workers, sounds, etc.
 */
void MainWindow::setupCoreSubsystems()
{
  renderCompleteSoundEffect = new QSoundEffect(this);
  renderCompleteSoundEffect->setSource(QUrl("qrc:/sounds/complete.wav"));

  this->cgalworker = new CGALWorker();
  connect(this->cgalworker, &CGALWorker::done, this, &MainWindow::actionRenderDone);

  autoReloadTimer = new QTimer(this);
  autoReloadTimer->setSingleShot(false);
  autoReloadTimer->setInterval(autoReloadPollingPeriodMS);
  connect(autoReloadTimer, &QTimer::timeout, this, &MainWindow::checkAutoReload);

  waitAfterReloadTimer = new QTimer(this);
  waitAfterReloadTimer->setSingleShot(true);
  waitAfterReloadTimer->setInterval(autoReloadPollingPeriodMS);
  connect(waitAfterReloadTimer, &QTimer::timeout, this, &MainWindow::waitAfterReload);

  consoleUpdater = new QTimer(this);
  consoleUpdater->setSingleShot(true);
  connect(consoleUpdater, &QTimer::timeout, this->console, &Console::update);
  this->consoleUpdater->start(0);  // Show initial messages immediately

  progressThrottle->start();
}

/**
  Initialize preferences and set up connections to respond to preference changes.
 */
void MainWindow::setupPreferences()
{
  connect(GlobalPreferences::inst(), &Preferences::updateReorderMode, this,
          &MainWindow::updateReorderMode);
  connect(GlobalPreferences::inst(), &Preferences::updateUndockMode, this,
          &MainWindow::updateUndockMode);
  connect(GlobalPreferences::inst(), &Preferences::openCSGSettingsChanged, this,
          &MainWindow::openCSGSettingsChanged);
  connect(GlobalPreferences::inst(), &Preferences::colorSchemeChanged, this,
          &MainWindow::setColorScheme);
  connect(GlobalPreferences::inst(), &Preferences::uiColorModeChanged, this, [this]() {
    applyFlatWorkbenchChrome();
    if (this->tabManager) this->tabManager->applyTheme();
    if (this->aiDock && this->aiDock->chatWidget()) {
      this->aiDock->chatWidget()->applyVSCodeChrome();
    }
  });
  connect(GlobalPreferences::inst(), &Preferences::toolbarExportChanged, this,
          &MainWindow::updateExportActions);

  connect(GlobalPreferences::inst(), &Preferences::requestRedraw, this->qglview,
          QOverload<>::of(&QGLView::update));
  connect(GlobalPreferences::inst(), &Preferences::updateMouseCentricZoom, this->qglview,
          &QGLView::setMouseCentricZoom);
  connect(GlobalPreferences::inst()->MouseConfig, &MouseConfigWidget::updateMouseActions, this,
          &MainWindow::setAllMouseViewActions);

  GlobalPreferences::inst()->apply_win();
  GlobalPreferences::inst()->ButtonConfig->init();
  GlobalPreferences::inst()->MouseConfig->init();

  connect(GlobalPreferences::inst()->ButtonConfig, &ButtonConfigWidget::inputMappingChanged,
          InputDriverManager::instance(), &InputDriverManager::onInputMappingUpdated,
          Qt::UniqueConnection);
  connect(GlobalPreferences::inst()->AxisConfig, &AxisConfigWidget::inputMappingChanged,
          InputDriverManager::instance(), &InputDriverManager::onInputMappingUpdated,
          Qt::UniqueConnection);
  connect(GlobalPreferences::inst()->AxisConfig, &AxisConfigWidget::inputCalibrationChanged,
          InputDriverManager::instance(), &InputDriverManager::onInputCalibrationUpdated,
          Qt::UniqueConnection);
  connect(GlobalPreferences::inst()->AxisConfig, &AxisConfigWidget::inputGainChanged,
          InputDriverManager::instance(), &InputDriverManager::onInputGainUpdated, Qt::UniqueConnection);

  connect(GlobalPreferences::inst(), &Preferences::ExperimentalChanged, this,
          &MainWindow::onExperimentalChanged);
  onExperimentalChanged();
}

/**
  Set up resources related to the Status Bar
 */
void MainWindow::setupStatusBar()
{
  this->versionLabel = nullptr;  // must be initialized before calling updateStatusBar()
  updateStatusBar(nullptr);
}

/**
  Set up resources related to the Console dock widget
 */
void MainWindow::setupConsole()
{
  connect(this->console, &Console::openWindowRequested, this, &MainWindow::showLink);
  connect(this->console, &Console::openFile, this, &MainWindow::openFileFromPath);

  QObject::connect(consoleDock, &Dock::visibilityChanged, this,
                   &MainWindow::onConsoleDockVisibilityChanged);

  // Prefer Explorer-matching console type (12px UI sans). One-shot migrate off terminal mono.
  {
    QSettingsCached settings;
    uint consoleSize = GlobalPreferences::inst()->getValue("advanced/consoleFontSize").toUInt();
    QString consoleFamily =
      GlobalPreferences::inst()->getValue("advanced/consoleFontFamily").toString();
    if (!settings.value("advanced/consoleFontExplorerUi", false).toBool()) {
      if (consoleSize == 0 || consoleSize == 11) {
        consoleSize = 12;
        settings.setValue("advanced/consoleFontSize", consoleSize);
      }
      // Drop leftover mono faces so Console matches Explorer.
      const QFontInfo info{QFont(consoleFamily)};
      if (consoleFamily.isEmpty() || info.fixedPitch()) {
        consoleFamily = QFontInfo{QApplication::font()}.family();
        settings.setValue("advanced/consoleFontFamily", consoleFamily);
      }
      settings.setValue("advanced/consoleFontExplorerUi", true);
    }
    if (consoleSize == 0) {
      consoleSize = 12;
    }
    this->console->setConsoleFont(consoleFamily, consoleSize);
  }
}

/**
  Set up resources related to the Error Log dock widget
 */
void MainWindow::setupErrorLog()
{
  connect(this->errorLogWidget, &ErrorLog::openFile, this, &MainWindow::openFileFromPath);

  QObject::connect(errorLogDock, &Dock::visibilityChanged, this,
                   &MainWindow::onErrorLogDockVisibilityChanged);

  // VS Code-style bottom panel needs both Console + Error Log widgets
  setupBottomPanel();
}

/**
  Build a VS Code-style bottom panel: custom tab header + stacked Console/Error Log.
 */
void MainWindow::setupBottomPanel()
{
  if (!consoleDockContents || !console || !errorLogWidget || bottomPanelHeader) {
    return;
  }

  consoleDock->setTitleBarVisibility(false);

  auto *rootLayout = qobject_cast<QVBoxLayout *>(consoleDockContents->layout());
  if (!rootLayout) {
    return;
  }

  // Detach console from its layout slot
  rootLayout->removeWidget(console);

  // Detach error log from its own dock and embed it here
  if (auto *errLayout = qobject_cast<QVBoxLayout *>(errorLogDockContents->layout())) {
    errLayout->removeWidget(errorLogWidget);
  }

  bottomPanelStack = new QStackedWidget(consoleDockContents);
  bottomPanelStack->setObjectName(QStringLiteral("bottomPanelStack"));
  bottomPanelStack->addWidget(console);
  bottomPanelStack->addWidget(errorLogWidget);
  bottomPanelStack->setCurrentIndex(0);

  bottomPanelHeader = new BottomPanelHeader(consoleDockContents);
  rootLayout->addWidget(bottomPanelHeader);
  rootLayout->addWidget(bottomPanelStack, 1);

  // Hide the standalone Error Log dock; tabs live in the bottom panel header
  removeDockWidget(errorLogDock);
  errorLogDock->hide();

  connect(bottomPanelHeader, &BottomPanelHeader::tabChanged, this, [this](int tab) {
    if (bottomPanelStack) {
      bottomPanelStack->setCurrentIndex(tab);
    }
    if (tab == BottomPanelHeader::ConsoleTab && console) {
      console->setFocus();
    } else if (tab == BottomPanelHeader::ErrorLogTab && errorLogWidget) {
      errorLogWidget->setFocus();
    }
  });

  connect(bottomPanelHeader, &BottomPanelHeader::clearClicked, this, [this]() {
    if (!bottomPanelHeader || !bottomPanelStack) return;
    if (bottomPanelHeader->activeTab() == BottomPanelHeader::ConsoleTab) {
      if (console) console->clear();
    } else if (errorLogWidget) {
      errorLogWidget->clearModel();
      updateBottomPanelErrorBadge();
    }
  });

  connect(bottomPanelHeader, &BottomPanelHeader::moreClearConsole, this, [this]() {
    if (console) console->clear();
  });
  connect(bottomPanelHeader, &BottomPanelHeader::moreSaveConsole, this, [this]() {
    if (console) console->on_actionSaveAs_triggered();
  });
  connect(bottomPanelHeader, &BottomPanelHeader::closeClicked, this, [this]() {
    if (consoleDock) consoleDock->hide();
  });
  connect(bottomPanelHeader, &BottomPanelHeader::maximizeClicked, this,
          &MainWindow::toggleBottomPanelMaximize);

  if (errorLogWidget && errorLogWidget->errorLogModel) {
    auto *model = errorLogWidget->errorLogModel;
    const auto refreshBadge = [this]() { updateBottomPanelErrorBadge(); };
    connect(model, &QAbstractItemModel::rowsInserted, this, refreshBadge);
    connect(model, &QAbstractItemModel::rowsRemoved, this, refreshBadge);
    connect(model, &QAbstractItemModel::modelReset, this, refreshBadge);
  }
  updateBottomPanelErrorBadge();
}

void MainWindow::updateBottomPanelErrorBadge()
{
  if (!bottomPanelHeader || !errorLogWidget || !errorLogWidget->errorLogModel) return;
  bottomPanelHeader->setErrorCount(errorLogWidget->errorLogModel->rowCount());
}

void MainWindow::toggleBottomPanelMaximize()
{
  if (!consoleDock || !consoleDock->isVisible()) return;
  if (!bottomPanelMaximized) {
    bottomPanelNormalHeight = std::max(120, consoleDock->height());
    const int target = std::max(240, static_cast<int>(height() * 0.55));
    resizeDocks({consoleDock}, {target}, Qt::Vertical);
    bottomPanelMaximized = true;
  } else {
    resizeDocks({consoleDock}, {bottomPanelNormalHeight}, Qt::Vertical);
    bottomPanelMaximized = false;
  }
  if (bottomPanelHeader) {
    bottomPanelHeader->setMaximized(bottomPanelMaximized);
  }
}

void MainWindow::showBottomPanelTab(int tab)
{
  if (consoleDock) {
    consoleDock->show();
    consoleDock->raise();
  }
  if (bottomPanelHeader) {
    bottomPanelHeader->setActiveTab(tab);
  } else if (bottomPanelStack) {
    bottomPanelStack->setCurrentIndex(tab);
  }
}

/**
  Set up resources related to the Editor dock widget
 */
void MainWindow::setupEditor(const QStringList& filenames)
{
  tabManager = new TabManager(this, filenames.isEmpty() ? QString() : filenames[0]);
  activeEditor = tabManager->editor;

  // VS Code activity bar (vertical) + show/hide content panel
  auto *editorArea = new QWidget(editorDockContents);
  auto *editorAreaLayout = new QHBoxLayout(editorArea);
  editorAreaLayout->setContentsMargins(0, 0, 0, 0);
  editorAreaLayout->setSpacing(0);

  this->editorActivityBar = new QWidget(editorArea);
  this->editorActivityBar->setObjectName(QStringLiteral("editorActivityBar"));
  this->editorActivityBar->setFixedWidth(kEditorActivityBarW);
  auto *activityLayout = new QVBoxLayout(this->editorActivityBar);
  activityLayout->setContentsMargins(0, 0, 0, 0);
  activityLayout->setSpacing(0);

  auto makeActivityBtn = [this](const QString& tooltip, const QIcon& icon) {
    auto *btn = new QToolButton(this->editorActivityBar);
    btn->setToolTip(tooltip);
    btn->setCheckable(true);
    btn->setAutoRaise(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    btn->setIcon(icon);
    btn->setIconSize(QSize(16, 16));
    btn->setFixedSize(kEditorActivityBarW, kEditorActivityBarW);
    return btn;
  };
  this->projectViewTabBtn = makeActivityBtn(_("Project"), projectActivityIcon());
  this->editorViewTabBtn = makeActivityBtn(_("Editor"), editorActivityIcon());
  activityLayout->addWidget(this->projectViewTabBtn, 0, Qt::AlignHCenter);
  activityLayout->addWidget(this->editorViewTabBtn, 0, Qt::AlignHCenter);
  activityLayout->addStretch(1);

  this->projectExplorer = new ProjectExplorer(editorArea);

  this->editorAreaStack = new QStackedWidget(editorArea);
  this->editorAreaStack->setObjectName(QStringLiteral("editorAreaStack"));
  this->editorAreaStack->setFrameShape(QFrame::NoFrame);
  this->editorAreaStack->addWidget(this->projectExplorer);           // index 0: Project
  this->editorAreaStack->addWidget(tabManager->getTabContent());     // index 1: Editor

  // Single 1px flat divider (avoids double border / white bevel next to the line).
  auto *activityDivider = new QFrame(editorArea);
  activityDivider->setObjectName(QStringLiteral("editorActivityDivider"));
  activityDivider->setFrameShape(QFrame::NoFrame);
  activityDivider->setFixedWidth(1);
  activityDivider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

  editorAreaLayout->addWidget(this->editorActivityBar);
  editorAreaLayout->addWidget(activityDivider);
  editorAreaLayout->addWidget(this->editorAreaStack, 1);

  editorDockContents->layout()->addWidget(editorArea);

  connect(this->projectViewTabBtn, &QToolButton::clicked, this, &MainWindow::onProjectActivityClicked);
  connect(this->editorViewTabBtn, &QToolButton::clicked, this, &MainWindow::onEditorActivityClicked);
  showEditorView();

  connect(this->projectExplorer, &ProjectExplorer::requestNewProject, this,
          &MainWindow::on_fileActionNewProject_triggered);
  connect(this->projectExplorer, &ProjectExplorer::requestOpenProject, this,
          &MainWindow::on_fileActionOpenProject_triggered);
  connect(this->projectExplorer, &ProjectExplorer::openFileRequested, this,
          &MainWindow::onProjectExplorerOpenFile);
  connect(&ProjectManager::instance(), &ProjectManager::projectChanged, this,
          &MainWindow::onProjectManagerChanged);

  connect(this->fileActionNew, &QAction::triggered, tabManager, &TabManager::actionNew);
  connect(this->fileActionClose, &QAction::triggered, tabManager, &TabManager::closeCurrentTab);
  connect(this->fileActionSaveAll, &QAction::triggered, tabManager, &TabManager::saveAll);

  connect(this, &MainWindow::highlightError, tabManager, &TabManager::highlightError);
  connect(this, &MainWindow::unhighlightLastError, tabManager, &TabManager::unhighlightLastError);

  connect(this->editActionUndo, &QAction::triggered, tabManager, &TabManager::undo);
  connect(this->editActionRedo, &QAction::triggered, tabManager, &TabManager::redo);
  connect(this->editActionRedo_2, &QAction::triggered, tabManager, &TabManager::redo);
  // Cut/Copy/Paste are handled by MainWindow slots so chat/settings fields can use keyboard shortcuts.

  connect(this->editActionIndent, &QAction::triggered, tabManager, &TabManager::indentSelection);
  connect(this->editActionUnindent, &QAction::triggered, tabManager, &TabManager::unindentSelection);
  connect(this->editActionComment, &QAction::triggered, tabManager, &TabManager::commentSelection);
  connect(this->editActionUncomment, &QAction::triggered, tabManager, &TabManager::uncommentSelection);

  connect(this->editActionToggleBookmark, &QAction::triggered, tabManager, &TabManager::toggleBookmark);
  connect(this->editActionNextBookmark, &QAction::triggered, tabManager, &TabManager::nextBookmark);
  connect(this->editActionPrevBookmark, &QAction::triggered, tabManager, &TabManager::prevBookmark);
  connect(this->editActionJumpToNextError, &QAction::triggered, tabManager,
          &TabManager::jumpToNextError);

  connect(tabManager, &TabManager::editorAboutToClose, this,
          &MainWindow::onTabManagerAboutToCloseEditor);
  connect(tabManager, &TabManager::currentEditorChanged, this, &MainWindow::onTabManagerEditorChanged);
  connect(tabManager, &TabManager::editorContentReloaded, this,
          &MainWindow::onTabManagerEditorContentReloaded);

  connect(this->editActionNextTab, &QAction::triggered, tabManager, &TabManager::nextTab);
  connect(this->editActionPrevTab, &QAction::triggered, tabManager, &TabManager::prevTab);

  onTabManagerEditorChanged(activeEditor);
  QObject::connect(editorDock, &Dock::visibilityChanged, this,
                   &MainWindow::onEditorDockVisibilityChanged);

  updateRecentProjectActions();
}

/**
  Set up resources related to the Customizer dock widget
 */
void MainWindow::setupCustomizer()
{
  QObject::connect(parameterDock, &Dock::visibilityChanged, this,
                   &MainWindow::onParametersDockVisibilityChanged);
}

/**
  Set up resources related to the Animate dock widget
 */
void MainWindow::setupAnimate()
{
  this->animateWidget->setMainWindow(this);
  QObject::connect(animateDock, &Dock::visibilityChanged, this,
                   &MainWindow::onAnimateDockVisibilityChanged);
}

/**
  Set up resources related to the Font List  dock widget
 */
void MainWindow::setupFontList()
{
  QObject::connect(fontListDock, &Dock::visibilityChanged, this,
                   &MainWindow::onFontListDockVisibilityChanged);
}

/**
  Set up resources related to the Color List  dock widget
 */
void MainWindow::setupColorList()
{
  QObject::connect(colorListDock, &Dock::visibilityChanged, this,
                   &MainWindow::onColorListDockVisibilityChanged);
  QObject::connect(colorListWidget, &ColorList::colorSelected, this,
                   &MainWindow::onColorListColorSelected);
}

/**
  Set up resources related to the Viewport Control dock widget
 */
void MainWindow::setupViewportControl()
{
  this->viewportControlWidget->setMainWindow(this);
  QObject::connect(viewportControlDock, &Dock::visibilityChanged, this,
                   &MainWindow::onViewportControlDockVisibilityChanged);
}

/**
  Setup AIDock
 */
void MainWindow::setupAIDock()
{
  this->aiDock = new AIDock(this);
  addDockWidget(Qt::RightDockWidgetArea, this->aiDock);

  if (Feature::ExperimentalAiFeatures.is_enabled()) {
    this->aiDock->show();
  } else {
    this->aiDock->hide();
  }
  this->aiDock->setTitleBarVisibility(false);
  this->aiDock->setFeatures(this->aiDock->features() | QDockWidget::DockWidgetClosable);

  QObject::connect(this->aiDock, &Dock::visibilityChanged, this, &MainWindow::onAIDockVisibilityChanged);
  if (ChatWidget *chat = this->aiDock->chatWidget()) {
    connect(chat, &ChatWidget::collapsedChanged, this, [this](bool collapsed) {
      updateAIChatRevealButton(collapsed);
    });
  }

  setupAIChatRevealButton();
}

void MainWindow::setupAIChatRevealButton()
{
  if (!this->viewerToolBar || this->aiChatRevealButton) return;

  QWidget *barParent = this->viewerToolBar->parentWidget();
  auto *vlayout = barParent ? qobject_cast<QVBoxLayout *>(barParent->layout()) : nullptr;
  if (!vlayout) {
    // Fallback: keep button on toolbar (may overflow into "...")
    this->aiChatRevealButton = new QToolButton(this->viewerToolBar);
  } else {
    const int idx = vlayout->indexOf(this->viewerToolBar);
    vlayout->removeWidget(this->viewerToolBar);

    this->previewHeaderRow = new QWidget(barParent);
    this->previewHeaderRow->setObjectName(QStringLiteral("previewHeaderRow"));
    this->previewHeaderRow->setAttribute(Qt::WA_StyledBackground, true);
    this->previewHeaderRow->setFixedHeight(kWorkbenchHeaderH);
    this->previewHeaderRow->setMinimumHeight(kWorkbenchHeaderH);
    this->previewHeaderRow->setMaximumHeight(kWorkbenchHeaderH);
    this->previewHeaderRow->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *row = new QHBoxLayout(this->previewHeaderRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);

    this->viewerToolBar->setParent(this->previewHeaderRow);
    row->addWidget(this->viewerToolBar, 1);

    this->aiChatRevealButton = new QToolButton(this->previewHeaderRow);
    row->addWidget(this->aiChatRevealButton, 0, Qt::AlignVCenter);

    if (idx >= 0) {
      vlayout->insertWidget(idx, this->previewHeaderRow);
    } else {
      vlayout->insertWidget(0, this->previewHeaderRow);
    }
  }

  QPixmap pm(32, 32);
  pm.fill(Qt::transparent);
  {
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#555555"), 2.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(5, 6, 22, 20), 2.5, 2.5);
    p.drawLine(QPointF(20, 6), QPointF(20, 26));
  }

  this->aiChatRevealButton->setObjectName(QStringLiteral("aiChatRevealButton"));
  this->aiChatRevealButton->setIcon(QIcon(pm));
  this->aiChatRevealButton->setIconSize(QSize(16, 16));
  this->aiChatRevealButton->setFixedSize(28, 28);
  this->aiChatRevealButton->setAutoRaise(true);
  this->aiChatRevealButton->setToolTip(_("Show AI chat"));
  this->aiChatRevealButton->setFocusPolicy(Qt::NoFocus);
  this->aiChatRevealButton->setCursor(Qt::PointingHandCursor);
  this->aiChatRevealButton->setVisible(false);
  this->aiChatRevealButton->setStyleSheet(QStringLiteral(
    "QToolButton#aiChatRevealButton { border: none; border-radius: 4px; padding: 0px; }"
    "QToolButton#aiChatRevealButton:hover { background: rgba(0,0,0,0.08); }"));

  connect(this->aiChatRevealButton, &QToolButton::clicked, this, [this]() {
    if (!this->aiDock) return;
    if (ChatWidget *chat = this->aiDock->chatWidget()) {
      // Always request expand — setCollapsed also recovers when the flag
      // is already false but the dock is still hidden.
      chat->setCollapsed(false);
    } else {
      addDockWidget(Qt::RightDockWidgetArea, this->aiDock);
      this->aiDock->setMinimumWidth(220);
      this->aiDock->show();
      this->aiDock->raise();
      resizeDocks({this->aiDock}, {320}, Qt::Horizontal);
    }
  });

  // If we fell back to toolbar parenting, add the widget there
  if (!this->previewHeaderRow && this->viewerToolBar) {
    this->viewerToolBar->addWidget(this->aiChatRevealButton);
  }
}

void MainWindow::updateAIChatRevealButton(bool chatHidden)
{
  const bool show =
    chatHidden && Feature::ExperimentalAiFeatures.is_enabled() && this->aiChatRevealButton;
  if (this->aiChatRevealButton) {
    this->aiChatRevealButton->setVisible(show);
    if (show) {
      this->aiChatRevealButton->raise();
    }
  }
}

void MainWindow::setupZoomControls()
{
  if (!this->viewerToolBar || this->zoomControlGroup) return;

  this->viewerToolBar->removeAction(this->viewActionZoomIn);
  this->viewerToolBar->removeAction(this->viewActionZoomOut);

  this->zoomControlGroup = new QWidget(this->viewerToolBar);
  this->zoomControlGroup->setObjectName(QStringLiteral("zoomControlGroup"));
  this->zoomControlGroup->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  auto *layout = new QHBoxLayout(this->zoomControlGroup);
  // Keep hover fills inset from the rounded outer border. Qt stylesheets do
  // not clip child backgrounds to a parent's border radius.
  layout->setContentsMargins(4, 2, 4, 2);
  layout->setSpacing(0);

  auto makeZoomButton = [this](const QString& text, const QString& objectName,
                               const QString& tip) {
    auto *btn = new QToolButton(this->zoomControlGroup);
    btn->setObjectName(objectName);
    btn->setText(text);
    btn->setToolTip(tip);
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(20);
    return btn;
  };

  auto *zoomOutBtn =
    makeZoomButton(QStringLiteral("−"), QStringLiteral("zoomOutButton"), _("Zoom Out"));
  zoomOutBtn->setFixedWidth(24);
  connect(zoomOutBtn, &QToolButton::clicked, this->viewActionZoomOut, &QAction::trigger);

  this->zoomPercentEdit = new QLineEdit(QStringLiteral("100%"), this->zoomControlGroup);
  this->zoomPercentEdit->setObjectName(QStringLiteral("zoomPercentEdit"));
  this->zoomPercentEdit->setAlignment(Qt::AlignCenter);
  this->zoomPercentEdit->setFixedWidth(48);
  this->zoomPercentEdit->setFixedHeight(20);
  this->zoomPercentEdit->setFrame(false);
  this->zoomPercentEdit->setToolTip(_("Type a zoom percent and press Enter"));
  this->zoomPercentEdit->setValidator(new QRegularExpressionValidator(
    QRegularExpression(QStringLiteral(R"(^\s*\d{1,4}\s*%?\s*$)")), this->zoomPercentEdit));
  connect(this->zoomPercentEdit, &QLineEdit::editingFinished, this,
          &MainWindow::applyZoomPercentFromEdit);
  connect(this->zoomPercentEdit, &QLineEdit::returnPressed, this, [this]() {
    applyZoomPercentFromEdit();
    if (this->zoomPercentEdit) this->zoomPercentEdit->clearFocus();
  });

  auto *zoomInBtn =
    makeZoomButton(QStringLiteral("+"), QStringLiteral("zoomInButton"), _("Zoom In"));
  zoomInBtn->setFixedWidth(24);
  connect(zoomInBtn, &QToolButton::clicked, this->viewActionZoomIn, &QAction::trigger);

  auto *sep = new QFrame(this->zoomControlGroup);
  sep->setObjectName(QStringLiteral("zoomGroupDivider"));
  sep->setFrameShape(QFrame::VLine);
  sep->setFrameShadow(QFrame::Plain);
  sep->setFixedWidth(1);
  sep->setFixedHeight(14);

  auto *zoomFitBtn =
    makeZoomButton(QString(), QStringLiteral("zoom100Button"),
                   _("Fit model to preview"));
  zoomFitBtn->setIcon(QIcon::fromTheme(QStringLiteral("chokusen-zoom-100")));
  zoomFitBtn->setIconSize(QSize(14, 14));
  zoomFitBtn->setFixedWidth(24);
  zoomFitBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);

  layout->addWidget(zoomOutBtn, 0, Qt::AlignVCenter);
  layout->addWidget(this->zoomPercentEdit, 0, Qt::AlignVCenter);
  layout->addWidget(zoomInBtn, 0, Qt::AlignVCenter);
  layout->addWidget(sep, 0, Qt::AlignVCenter);
  layout->addWidget(zoomFitBtn, 0, Qt::AlignVCenter);

  this->viewerToolBar->insertWidget(this->viewActionResetView, this->zoomControlGroup);

  connect(zoomFitBtn, &QToolButton::clicked, this->viewActionViewAll, &QAction::trigger);

  styleZoomControls();
  updateZoomPercentLabel();
}

void MainWindow::styleZoomControls()
{
  if (!this->zoomControlGroup) return;

  const bool dark = isDarkMode();
  const QString border = dark ? QStringLiteral("#3a3a3c") : QStringLiteral("#d8d8da");
  const QString bg = dark ? QStringLiteral("#2b2b2d") : QStringLiteral("#f0f0f2");
  const QString hover = dark ? QStringLiteral("#414144") : QStringLiteral("#dedee1");
  const QString pressed = dark ? QStringLiteral("#4a4a4d") : QStringLiteral("#d2d2d5");
  const QString text = dark ? QStringLiteral("#e0e0e0") : QStringLiteral("#333333");
  const QString divider = dark ? QStringLiteral("#404040") : QStringLiteral("#e0e0e0");

  this->zoomControlGroup->setStyleSheet(QStringLiteral(R"(
    QWidget#zoomControlGroup {
      background: %1;
      border: 1px solid %2;
      border-radius: 7px;
      margin: 0px 2px;
    }
    QWidget#zoomControlGroup QToolButton {
      background: transparent;
      border: none;
      border-radius: 4px;
      color: %3;
      font-size: 13px;
      font-weight: 600;
      padding: 0px 4px;
      margin: 0px;
    }
    QWidget#zoomControlGroup QToolButton#zoom100Button {
      padding: 0px;
    }
    QWidget#zoomControlGroup QToolButton:hover {
      background: %4;
    }
    QWidget#zoomControlGroup QToolButton:pressed {
      background: %5;
    }
    QLineEdit#zoomPercentEdit {
      background: transparent;
      border: none;
      color: %3;
      font-size: 11px;
      font-weight: 600;
      padding: 0px 1px;
      margin: 0px;
      selection-background-color: %4;
    }
    QFrame#zoomGroupDivider {
      background: %6;
      border: none;
      max-width: 1px;
      margin: 0px 2px;
    }
  )")
                                          .arg(bg, border, text, hover, pressed, divider));
}

void MainWindow::applyZoomPercentFromEdit()
{
  if (!this->zoomPercentEdit || !this->qglview) return;

  QString raw = this->zoomPercentEdit->text().trimmed();
  raw.remove(QLatin1Char('%'));
  raw = raw.trimmed();
  bool ok = false;
  const int percent = raw.toInt(&ok);
  if (!ok || percent < 1 || percent > 9999) {
    updateZoomPercentLabel();
    return;
  }

  const double distance = Camera::DEFAULT_VIEWER_DISTANCE * 100.0 / static_cast<double>(percent);
  this->qglview->zoom(qMax(0.01, distance), false);
  updateZoomPercentLabel();
}

void MainWindow::updateZoomPercentLabel()
{
  if (!this->zoomPercentEdit || !this->qglview) return;
  // Don't fight the user while they are typing a new value.
  if (this->zoomPercentEdit->hasFocus()) return;

  const double distance = this->qglview->cam.zoomValue();
  if (distance <= 0.0) {
    this->zoomPercentEdit->setText(QStringLiteral("—"));
    return;
  }

  const int percent =
    qRound(100.0 * Camera::DEFAULT_VIEWER_DISTANCE / distance);
  const int clamped = qBound(percent, 1, 9999);
  this->zoomPercentEdit->setText(QStringLiteral("%1%").arg(clamped));
  this->zoomPercentEdit->setToolTip(
    QString(_("Current zoom: %1%% — type a value and press Enter (distance %2)"))
      .arg(clamped)
      .arg(QString::number(distance, 'f', 2)));
}

void MainWindow::applyFlatWorkbenchChrome()
{
  // Match editor|preview divider to the thin flat AI chat separator (no 3D bevel).
  // Editor panel background matches AI chat (#f8f8f8 / #1e1e1e).
  const bool dark = isDarkMode();
  const QString sep = dark ? QStringLiteral("#2b2b2b") : QStringLiteral("#e5e5e5");
  const QString sepHover = dark ? QStringLiteral("#3c3c3c") : QStringLiteral("#c8c8c8");
  const QString panelBg = dark ? QStringLiteral("#1e1e1e") : QStringLiteral("#f8f8f8");
  const QString headerBg = dark ? QStringLiteral("#252526") : QStringLiteral("#f3f3f3");

  setStyleSheet(QStringLiteral(R"(
    QMainWindow::separator {
      background: %1;
      width: 1px;
      height: 1px;
      margin: 0px;
      padding: 0px;
      border: none;
    }
    QMainWindow::separator:hover {
      background: %2;
    }
    QDockWidget {
      border: none;
      margin: 0px;
      padding: 0px;
      background: %3;
    }
    QDockWidget::title {
      text-align: left;
      background: %4;
      color: %6;
      border: none;
      border-bottom: 1px solid %1;
      padding: 0px 10px;
      min-height: 32px;
      max-height: 32px;
      font-size: 12px;
      font-weight: 500;
    }
    QDockWidget > QWidget {
      background: %3;
      border: none;
      margin: 0px;
      padding: 0px;
    }
    QWidget#editorDockContents, QWidget#centralwidget, QWidget#mainWidget,
    QWidget#consoleDockContents, QWidget#errorLogDockContents {
      border: none;
      margin: 0px;
      padding: 0px;
      background: %3;
    }
    QFrame#editorActivityDivider {
      background: %1;
      border: none;
      margin: 0px;
      padding: 0px;
      max-width: 1px;
      min-width: 1px;
    }
    QWidget#consoleDockContents Console,
    QWidget#consoleDockContents QPlainTextEdit {
      background: %3;
      color: %6;
      border: none;
    }
    QWidget#errorLogDockContents QTableView {
      background: %3;
      color: %6;
      border: none;
      gridline-color: %1;
      outline: 0;
    }
    QWidget#errorLogDockContents QHeaderView::section {
      background: %4;
      color: %6;
      border: none;
      border-bottom: 1px solid %1;
      border-right: 1px solid %1;
      padding: 3px 6px;
      font-size: 11px;
    }
    QToolBar#editortoolbar {
      background: %4;
      border: none;
      spacing: 2px;
    }
    QWidget#previewHeaderRow {
      background: %4;
      border: none;
      border-bottom: 1px solid %1;
      min-height: 32px;
      max-height: 32px;
      padding: 0px;
    }
    QToolBar#viewerToolBar {
      background: transparent;
      border: none;
      spacing: 1px;
      padding: 0px 4px;
      margin: 0px;
      min-height: 31px;
      max-height: 31px;
    }
    QToolBar#viewerToolBar::separator {
      background: %1;
      width: 1px;
      margin: 6px 4px;
    }
    QToolBar#viewerToolBar QToolButton {
      background: transparent;
      border: none;
      border-radius: 4px;
      padding: 3px;
      margin: 0px;
    }
    QToolBar#viewerToolBar QToolButton:hover {
      background: %8;
    }
    QToolBar#viewerToolBar QToolButton:pressed,
    QToolBar#viewerToolBar QToolButton:checked {
      background: %7;
    }
    QStatusBar {
      background: %3;
      border: none;
      border-top: 1px solid %1;
      min-height: 22px;
      max-height: 24px;
      padding: 0px 14px;
      font-size: 11px;
      font-weight: normal;
      color: %6;
    }
    QStatusBar QLabel {
      font-size: 11px;
      font-weight: normal;
      color: %6;
      padding: 0px 6px;
      margin: 0px;
    }
    QStatusBar::item {
      border: none;
      margin: 0px 2px;
    }
    QFrame#find_panel {
      background: %4;
      border: none;
      border-bottom: 1px solid %1;
    }
    QFrame#find_panel QComboBox {
      min-height: 22px;
      max-height: 22px;
      padding: 0px 4px 0px 6px;
      border: 1px solid %1;
      border-radius: 4px;
      background: %3;
      color: %6;
      font-size: 12px;
    }
    QFrame#find_panel QComboBox#findTypeComboBox {
      min-width: 72px;
      max-width: 88px;
    }
    QFrame#find_panel QComboBox::drop-down {
      border: none;
      width: 14px;
    }
    QFrame#find_panel QComboBox QAbstractItemView {
      background: %3;
      color: %6;
      border: 1px solid %1;
      outline: 0;
      selection-background-color: %7;
      selection-color: %6;
      padding: 2px;
    }
    QFrame#find_panel QLineEdit {
      min-height: 22px;
      max-height: 22px;
      padding: 1px 6px;
      border: 1px solid %1;
      border-radius: 4px;
      background: %3;
      color: %6;
      selection-background-color: %5;
      font-size: 12px;
    }
    QFrame#find_panel QToolButton,
    QFrame#find_panel QPushButton {
      min-height: 22px;
      max-height: 22px;
      padding: 0px 8px;
      border: 1px solid %1;
      border-radius: 4px;
      background: %3;
      color: %6;
      font-size: 12px;
    }
    QFrame#find_panel QToolButton#findPrevButton,
    QFrame#find_panel QToolButton#findNextButton {
      min-width: 22px;
      max-width: 22px;
      min-height: 22px;
      max-height: 22px;
      padding: 0px;
      border-radius: 4px;
    }
    QFrame#find_panel QToolButton:hover,
    QFrame#find_panel QPushButton:hover {
      background: %2;
    }
    QFrame#find_panel QToolButton:pressed,
    QFrame#find_panel QPushButton:pressed {
      background: %1;
    }
  )")
                  .arg(sep, sepHover, panelBg, headerBg,
                       dark ? QStringLiteral("#264f78") : QStringLiteral("#add6ff"),
                       dark ? QStringLiteral("#cccccc") : QStringLiteral("#333333"),
                       dark ? QStringLiteral("#094771") : QStringLiteral("#e8e8e8"),
                       dark ? QStringLiteral("#2a2d2e") : QStringLiteral("#e8e8e8")));

  // Kill macOS Aqua bevel on the preview toolbar so it matches the flat chat header.
  if (this->viewerToolBar) {
    this->viewerToolBar->setAttribute(Qt::WA_StyledBackground, true);
    this->viewerToolBar->setMovable(false);
    this->viewerToolBar->setFloatable(false);
    static QStyle *fusionToolbarStyle = QStyleFactory::create(QStringLiteral("Fusion"));
    if (fusionToolbarStyle) {
      this->viewerToolBar->setStyle(fusionToolbarStyle);
    }
  }
  if (this->previewHeaderRow) {
    this->previewHeaderRow->setAttribute(Qt::WA_StyledBackground, true);
    this->previewHeaderRow->setFixedHeight(kWorkbenchHeaderH);
    this->previewHeaderRow->setMinimumHeight(kWorkbenchHeaderH);
    this->previewHeaderRow->setMaximumHeight(kWorkbenchHeaderH);
  }
  if (auto *grid = find_panel ? qobject_cast<QGridLayout *>(find_panel->layout()) : nullptr) {
    grid->setContentsMargins(6, 4, 6, 4);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(4);
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 0);
    grid->setColumnStretch(2, 0);
    grid->setColumnStretch(3, 0);
    grid->setColumnStretch(4, 0);
    grid->setColumnStretch(5, 1);  // leftover space goes right of Done
  }
  if (findTypeComboBox) {
    findTypeComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    findTypeComboBox->setMaximumWidth(88);
    findTypeComboBox->setMinimumWidth(72);
    auto *view = findTypeComboBox->view();
    if (view) {
      QPalette pal = view->palette();
      const QColor text = dark ? QColor("#cccccc") : QColor("#333333");
      const QColor bg = dark ? QColor("#1e1e1e") : QColor("#ffffff");
      const QColor sel = dark ? QColor("#094771") : QColor("#e8e8e8");
      pal.setColor(QPalette::Base, bg);
      pal.setColor(QPalette::Text, text);
      pal.setColor(QPalette::WindowText, text);
      pal.setColor(QPalette::ButtonText, text);
      pal.setColor(QPalette::HighlightedText, text);
      pal.setColor(QPalette::Highlight, sel);
      view->setPalette(pal);
    }
  }
  if (findInputField) {
    findInputField->setMaximumWidth(240);
    findInputField->setMinimumWidth(160);
    findInputField->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  }
  if (replaceInputField) {
    replaceInputField->setMaximumWidth(240);
    replaceInputField->setMinimumWidth(160);
    replaceInputField->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  }

  // Keep Console flat / theme-matched when chrome or OS theme changes
  if (this->console) {
    this->console->setFrameShape(QFrame::NoFrame);
    this->console->setConsoleFont(
      GlobalPreferences::inst()->getValue("advanced/consoleFontFamily").toString(),
      GlobalPreferences::inst()->getValue("advanced/consoleFontSize").toUInt());
  }
  if (this->bottomPanelHeader) {
    this->bottomPanelHeader->applyTheme();
  }
  if (this->errorLogWidget) {
    this->errorLogWidget->applyTheme();
  }
  if (this->projectExplorer) {
    this->projectExplorer->refreshTheme();
  }
  if (this->editorActivityBar) {
    this->editorActivityBar->setAttribute(Qt::WA_StyledBackground, true);
    this->editorActivityBar->setFixedWidth(kEditorActivityBarW);
    const QString muted = dark ? QStringLiteral("#858585") : QStringLiteral("#616161");
    const QString hover = dark ? QStringLiteral("#2a2d2e") : QStringLiteral("#e8e8e8");
    const QString accent = dark ? QStringLiteral("#007acc") : QStringLiteral("#005fb8");
    const QString barBg = dark ? QStringLiteral("#333333") : QStringLiteral("#f3f3f3");
    this->editorActivityBar->setStyleSheet(
      QStringLiteral(R"(
        QWidget#editorActivityBar {
          background: %1;
          border: none;
        }
        QWidget#editorActivityBar QToolButton {
          background: transparent;
          border: none;
          border-radius: 0px;
          border-left: 2px solid transparent;
          margin: 0px;
          padding: 0px;
        }
        QWidget#editorActivityBar QToolButton:hover {
          background: %2;
        }
        QWidget#editorActivityBar QToolButton:checked {
          background: %2;
          border-left: 2px solid %3;
        }
      )")
        .arg(barBg, hover, accent));
    if (this->projectViewTabBtn) {
      this->projectViewTabBtn->setIcon(projectActivityIcon());
      this->projectViewTabBtn->setIconSize(QSize(16, 16));
      this->projectViewTabBtn->setFixedSize(kEditorActivityBarW, kEditorActivityBarW);
    }
    if (this->editorViewTabBtn) {
      this->editorViewTabBtn->setIcon(editorActivityIcon());
      this->editorViewTabBtn->setIconSize(QSize(16, 16));
      this->editorViewTabBtn->setFixedSize(kEditorActivityBarW, kEditorActivityBarW);
    }
  }
  styleZoomControls();

  // Keep the built-in editor schemes in sync with the Appearance preference so
  // the code paper matches the chrome (#1e1e1e). Leave custom schemes alone.
  if (GlobalPreferences::inst()) {
    const QString lightScheme = QStringLiteral("For Light Background");
    const QString darkScheme = QStringLiteral("For Dark Background");
    const QString current =
      GlobalPreferences::inst()->getValue("editor/syntaxhighlight").toString();
    QString desired;
    if (dark && (current.isEmpty() || current == lightScheme)) {
      desired = darkScheme;
    } else if (!dark && current == darkScheme) {
      desired = lightScheme;
    }
    if (!desired.isEmpty() && desired != current) {
      QSettingsCached settings;
      settings.setValue("editor/syntaxhighlight", desired);
      emit GlobalPreferences::inst()->syntaxHighlightChanged(desired);
    }
  }
}

/**
  Set up resources related to the 3d View
 */
void MainWindow::setup3DView()
{
  this->qglview->statusLabel = new QLabel(this);
  this->qglview->statusLabel->setMinimumWidth(100);
  {
    QFont statusFont = this->qglview->statusLabel->font();
    statusFont.setPointSize(11);
    statusFont.setWeight(QFont::Normal);
    this->qglview->statusLabel->setFont(statusFont);
  }
  statusBar()->addWidget(this->qglview->statusLabel);

  const QSettingsCached settings;
  this->qglview->setMouseCentricZoom(Settings::Settings::mouseCentricZoom.value());
  this->setAllMouseViewActions();
  this->meas.setView(qglview);
  resetMeasurementsState(false, "Render (not preview) to enable measurements");

  // Initial Color Scheme
  const QString cs = GlobalPreferences::inst()->getValue("3dview/colorscheme").toString();
  this->setColorScheme(cs);

  // Initialize View Mode
  // Default to ThrownTogether as OpenCSG support is not known until initializeGL()
  // runs (after show()). The initialized() signal will trigger an update to
  // Preview mode if supported.
  viewModeThrownTogether();

  loadViewSettings();
  loadDesignSettings();

  connect(this->qglview, &QGLView::cameraChanged, animateWidget, &Animate::cameraChanged);
  connect(this->qglview, &QGLView::cameraChanged, viewportControlWidget,
          &ViewportControl::cameraChanged);
  connect(this->qglview, &QGLView::cameraChanged, this, &MainWindow::updateZoomPercentLabel);
  connect(this->qglview, &QGLView::resized, viewportControlWidget, &ViewportControl::viewResized);
  connect(this->qglview, &QGLView::doRightClick, this, &MainWindow::rightClick);
  connect(this->qglview, &QGLView::doLeftClick, this, &MainWindow::leftClick);
  connect(this->qglview, &QGLView::initialized, this, &MainWindow::updateViewModeAfterGLInit);

  setupZoomControls();
}

/**
FIXME(kintel): Is this the right place for this?
 */
void MainWindow::setupInput()
{
  InputDriverManager::instance()->registerActions(this->menuBar()->actions(), "", "");
  InputDriverManager::instance()->registerActions(this->animateWidget->actions(), "animation",
                                                  "animate");
}

/**
 Set up glocal Dock widget handling.
 */
void MainWindow::setupDocks()
{
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
  // VS Code layout: left (editor) + right (AI) run full height; bottom Console
  // sits only in the center column between them — not full window width.
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

  // clang-format off
  docks = {
    {editorDock, _("&Editor")},
    {consoleDock, _("&Console")},
    {parameterDock, _("C&ustomizer")},
    {animateDock, _("&Animate")},
    {fontListDock, _("&Font List")},
    {colorListDock, _("C&olor List")},
    {viewportControlDock, _("&Viewport Control")},
    {aiDock,_("&AI Chat")}
  };
  // clang-format off

  // Connect the menu "Windows/Navigation" to slot that process it by opening in a pop menu
  // the navigationMenu.
  connect(windowActionJumpTo, &QAction::triggered, this, &MainWindow::onNavigationOpenContextMenu);

  // Create the popup menu to navigate between the docks by keyboard.
  navigationMenu = new QMenu();

  // Create the docks, connect corresponding action and install menu entries
  for (auto& [dock, title] : docks) {
    dock->setName(title);
    dock->setFocusPolicy(Qt::FocusPolicy::StrongFocus);

    // It is neede to have the event filter installed in each dock so that the events are
    // correctly processed when the dock are floating (is in a different window that the mainwindow)
    dock->installEventFilter(this);

    // Get the toggle action from Qt and set an objectName for DBus accessibility
    QAction *toggleAction = dock->toggleViewAction();
    QString baseName = getDockBaseName(title);
    toggleAction->setObjectName("windowActionToggle" + baseName);
    menuWindow->addAction(toggleAction);

    auto dockAction = navigationMenu->addAction(title);
    dockAction->setProperty("id", QVariant::fromValue(dock));
    connect(dockAction, &QAction::triggered, this, &MainWindow::onNavigationTriggerContextMenuEntry);
    connect(dockAction, &QAction::hovered, this, &MainWindow::onNavigationHoveredContextMenuEntry);
  }

  // Error Log is a tab inside the bottom panel (VS Code-style), not a separate dock.
  {
    auto *errorLogAction = menuWindow->addAction(_("Error &Log"));
    errorLogAction->setObjectName(QStringLiteral("windowActionToggleErrorLog"));
    errorLogAction->setCheckable(true);
    connect(errorLogAction, &QAction::triggered, this, [this, errorLogAction](bool checked) {
      if (checked) {
        showBottomPanelTab(BottomPanelHeader::ErrorLogTab);
      } else if (consoleDock && consoleDock->isVisible() && bottomPanelHeader &&
                 bottomPanelHeader->activeTab() == BottomPanelHeader::ErrorLogTab) {
        consoleDock->hide();
      }
      errorLogAction->setChecked(consoleDock && consoleDock->isVisible() && bottomPanelHeader &&
                                 bottomPanelHeader->activeTab() == BottomPanelHeader::ErrorLogTab);
    });
    auto *navError = navigationMenu->addAction(_("Error &Log"));
    connect(navError, &QAction::triggered, this,
            [this]() { showBottomPanelTab(BottomPanelHeader::ErrorLogTab); });
  }

  connect(navigationMenu, &QMenu::aboutToHide, this, &MainWindow::onNavigationCloseContextMenu);
  connect(menuWindow, &QMenu::aboutToHide, this, &MainWindow::onNavigationCloseContextMenu);
  windowActionJumpTo->setMenu(navigationMenu);

  // AI-first layout: editor cannot be closed via title button or Window menu
  editorDock->setFeatures(editorDock->features() & ~QDockWidget::DockWidgetClosable);
  editorDock->setTitleBarVisibility(false);
  if (QAction *editorToggle = editorDock->toggleViewAction()) {
    editorToggle->setVisible(false);
    editorToggle->setEnabled(false);
  }

  applyFlatWorkbenchChrome();
}

/**
  Connect menus and other actions.
 */
void MainWindow::setupMenusAndActions()
{
  this->exportFormatMapper = new QSignalMapper(this);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  connect(this->exportFormatMapper, &QSignalMapper::mappedInt, this,
          &MainWindow::actionExportFileFormat);
#else
  connect(this->exportFormatMapper, static_cast<void (QSignalMapper::*)(int)>(&QSignalMapper::mapped),
          this, &MainWindow::actionExportFileFormat);
#endif

  frameCompileResult->hide();
  this->labelCompileResultMessage->setOpenExternalLinks(false);
  connect(this->labelCompileResultMessage, &QLabel::linkActivated, this, &MainWindow::showLink);

  // actions not included in menu
  this->addAction(editActionInsertTemplate);
  this->addAction(editActionFoldAll);

  //
  // File menu
  //


  // Recent files
  updateRecentFileActions();

  show_examples();
#ifndef __APPLE__
  auto shortcuts = this->fileActionReload->shortcuts();
  shortcuts.push_back(QKeySequence(Qt::Key_F3));
  this->fileActionReload->setShortcuts(shortcuts);
#endif


#ifndef __APPLE__
  shortcuts = this->fileActionSave->shortcuts();
  this->fileActionSave->setShortcuts(shortcuts);
#endif


  connect(this->fileActionQuit, &QAction::triggered, scadApp, &OpenSCADApp::closeApp, Qt::QueuedConnection);

#ifdef ENABLE_PYTHON
#else
  this->menuPython->menuAction()->setVisible(false);
#endif

  //
  // Edit menu
  //
  // Edit->Find
#ifdef Q_OS_WIN
  this->editActionFindAndReplace->setShortcut(QKeySequence("Ctrl+Shift+F"));
#endif

  //
  // Design menu
  //
  measurementGroup = new QActionGroup(this);
  measurementGroup->addAction(designActionMeasureDist);
  measurementGroup->addAction(designActionMeasureAngle);
  connect(this->measurementGroup, &QActionGroup::triggered, this, &MainWindow::handleMeasurementClicked);

  exportMap[FileFormat::BINARY_STL] = this->fileActionExportBinarySTL;
  exportMap[FileFormat::ASCII_STL] = this->fileActionExportAsciiSTL;
  exportMap[FileFormat::_3MF] = this->fileActionExport3MF;
  exportMap[FileFormat::OBJ] = this->fileActionExportOBJ;
  exportMap[FileFormat::OFF] = this->fileActionExportOFF;
  exportMap[FileFormat::WRL] = this->fileActionExportWRL;
  exportMap[FileFormat::POV] = this->fileActionExportPOV;
  exportMap[FileFormat::AMF] = this->fileActionExportAMF;
  exportMap[FileFormat::DXF] = this->fileActionExportDXF;
  exportMap[FileFormat::SVG] = this->fileActionExportSVG;
  exportMap[FileFormat::PDF] = this->fileActionExportPDF;
  exportMap[FileFormat::CSG] = this->fileActionExportCSG;
  exportMap[FileFormat::PNG] = this->fileActionExportImage;

  for (auto& [format, action] : exportMap) {
    connect(action, &QAction::triggered, this->exportFormatMapper, QOverload<>::of(&QSignalMapper::map));
    this->exportFormatMapper->setMapping(action, int(format));
  }

#ifndef ENABLE_LIB3MF
  this->fileActionExport3MF->setVisible(false);
#endif

  //
  // View menu
  //
  previewModeGroup = new QActionGroup(this);
  previewModeGroup->setExclusive(true);
  previewModeGroup->addAction(this->viewActionPreview);
  previewModeGroup->addAction(this->viewActionThrownTogether);
  if (this->qglview->hasOpenCSGSupport()) {
    this->viewActionPreview->setChecked(true);
  } else {
    this->viewActionThrownTogether->setChecked(true);
  }

  viewActionProjectionGroup = new QActionGroup(this);
  viewActionProjectionGroup->setExclusive(true);
  viewActionProjectionGroup->addAction(this->viewActionPerspective);
  viewActionProjectionGroup->addAction(this->viewActionOrthogonal);


  connect(this->viewActionZoomIn, &QAction::triggered, qglview, &QGLView::ZoomIn);
  connect(this->viewActionZoomOut, &QAction::triggered, qglview, &QGLView::ZoomOut);

  //
  // Help menu
  //

  // Checks if the Documentation has been downloaded and hides the Action otherwise
  if (!UIUtils::hasOfflineUserManual()) {
    this->helpActionOfflineManual->setVisible(false);
  }
  if (!UIUtils::hasOfflineCheatSheet()) {
    this->helpActionOfflineCheatSheet->setVisible(false);
  }
#ifdef OPENSCAD_UPDATER
  this->menuBar()->addMenu(AutoUpdater::updater()->updateMenu);
#endif

  connect(this->findTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::actionSelectFind);
  connect(this->findInputField, &QWordSearchField::textChanged, this, &MainWindow::findString);
  connect(this->findInputField, &QWordSearchField::returnPressed, this->findNextButton,
          [this] { this->findNextButton->animateClick(); });
  find_panel->installEventFilter(this);
  if (QApplication::clipboard()->supportsFindBuffer()) {
    connect(this->findInputField, &QWordSearchField::textChanged, this, &MainWindow::updateFindBuffer);
    connect(QApplication::clipboard(), &QClipboard::findBufferChanged, this,
            &MainWindow::findBufferChanged);
    // With Qt 4.8.6, there seems to be a bug that often gives an incorrect findbuffer content when
    // the app receives focus for the first time
    this->findInputField->setText(QApplication::clipboard()->text(QClipboard::FindBuffer));
  }

  // Compact find strip — theme chevron icons for prev/next
  auto wireFindNavButton = [](QToolButton *btn, QAction *action, const QIcon& icon) {
    btn->setText(QString());
    btn->setIcon(icon);
    btn->setIconSize(QSize(14, 14));
    btn->setFixedSize(24, 24);
    btn->setToolTip(action->toolTip().isEmpty() ? action->text() : action->toolTip());
    btn->setAutoRaise(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    QObject::connect(btn, &QToolButton::clicked, action, &QAction::trigger);
  };
  wireFindNavButton(this->findPrevButton, this->editActionFindPrevious,
                    QIcon::fromTheme(QStringLiteral("chokusen-prev")));
  wireFindNavButton(this->findNextButton, this->editActionFindNext,
                    QIcon::fromTheme(QStringLiteral("chokusen-next")));
  this->findDoneButton->setText(_("Done"));
  this->findDoneButton->setAutoRaise(true);
  this->findDoneButton->setCursor(Qt::PointingHandCursor);
  this->findDoneButton->setFocusPolicy(Qt::NoFocus);
  this->findDoneButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  connect(this->findDoneButton, &QToolButton::clicked, this, &MainWindow::hideFind);
  connect(this->replaceButton, &QPushButton::clicked, this, &MainWindow::replace);
  connect(this->replaceAllButton, &QPushButton::clicked, this, &MainWindow::replaceAll);
  connect(this->replaceInputField, &QLineEdit::returnPressed, this->replaceButton,
          [this] { this->replaceButton->animateClick(); });
  this->replaceButton->setCursor(Qt::PointingHandCursor);
  this->replaceAllButton->setCursor(Qt::PointingHandCursor);
  this->replaceButton->setFlat(true);
  this->replaceAllButton->setFlat(true);

  addKeyboardShortCut(this->viewerToolBar->actions());
  addKeyboardShortCut(this->editortoolbar->actions());

  // connect the signal of next/prev windowAction and the dedicated slot
  // hovering is connected to rubberband activation while triggering is for actual
  // activation of the corresponding dock.
  const std::vector<QAction *> actions = {windowActionNextWindow, windowActionPreviousWindow};
  for (auto& action : actions) {
    connect(action, &QAction::hovered, this, &MainWindow::onWindowActionNextPrevHovered);
    connect(action, &QAction::triggered, this, &MainWindow::onWindowActionNextPrevTriggered);
  }

  // Adds shortcut for the prev/next window switching
  shortcutNextWindow = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
  QObject::connect(shortcutNextWindow, &QShortcut::activated, this,
                   &MainWindow::onWindowShortcutNextPrevActivated);
  shortcutPreviousWindow = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_H), this);
  QObject::connect(shortcutPreviousWindow, &QShortcut::activated, this,
                   &MainWindow::onWindowShortcutNextPrevActivated);

  auto shortcutExport3D = new QShortcut(QKeySequence("F7"), this);
  QObject::connect(shortcutExport3D, &QShortcut::activated, this,
                   &MainWindow::onWindowShortcutExport3DActivated);

  updateExportActions();
}

/**
  Restore GUI state from settings.
 */
void MainWindow::restoreWindowState()
{
  QSettingsCached settings;
  // v2: AI-first side chat. v3: Console default visible.
  // v4: VS Code bottom panel header. v5: center-only console (L/R full height).
  constexpr int kAiFirstLayoutVersion = 5;
  const int layoutVersion = settings.value("window/layoutVersion", 0).toInt();
  const bool forceAiFirstLayout = layoutVersion < kAiFirstLayoutVersion;

  auto windowState = settings.value("window/state", QByteArray()).toByteArray();
  if (forceAiFirstLayout) {
    windowState.clear();
  }

  clearCurrentOutput();
  UIUtils::dumpSaveState(windowState);
  setCurrentOutput();
  restoreGeometry(settings.value("window/geometry", QByteArray()).toByteArray());
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
  // Workaround for a Qt bug (possible QTBUG-46620, but it's still there in Qt-6.5.3)
  // Blindly restoring a maximized window to a different screen resolution causes a crash
  // on the next move/resize operation on macOS:
  // https://github.com/openscad/openscad/issues/5486
  if (isMaximized()) {
    setGeometry(screen()->availableGeometry());
  }
#endif
  if (!windowState.isEmpty()) {
    restoreState(windowState);
  }

  if (windowState.size() == 0) {
    /*
     * Default AI-first / VS Code workbench:
     *   Left  = Editor (full height)
     *   Right = AI chat (full height)
     *   Center top    = 3D preview (central widget)
     *   Center bottom = Console/log (only between L/R, not full width)
     */
    addDockWidget(Qt::LeftDockWidgetArea, editorDock);
    if (this->aiDock) {
      addDockWidget(Qt::RightDockWidgetArea, this->aiDock);
    }
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock);

    // Utility docks stay available via Window menu (not in the main 3-column layout)
    tabifyDockWidget(fontListDock, colorListDock);
    tabifyDockWidget(colorListDock, animateDock);
    tabifyDockWidget(animateDock, viewportControlDock);
    parameterDock->hide();
    fontListDock->hide();
    colorListDock->hide();
    animateDock->hide();
    viewportControlDock->hide();

    editorDock->show();
    editorDock->raise();
    consoleDock->show();
    consoleDock->raise();
    if (bottomPanelHeader) {
      bottomPanelHeader->setActiveTab(BottomPanelHeader::ConsoleTab);
    }

    const int winW = std::max(1000, this->width());
    const int editorW = std::max(320, (5 * winW) / 11);
    const int aiW = std::max(280, winW / 4);
    activeEditor->setInitialSizeHint(QSize(editorW, 100));
    resizeDocks({editorDock}, {editorW}, Qt::Horizontal);
    if (this->aiDock) {
      resizeDocks({this->aiDock}, {aiW}, Qt::Horizontal);
    }
    resizeDocks({consoleDock}, {160}, Qt::Vertical);
    // Dock sizes are more reliable after the window is shown
    QTimer::singleShot(0, this, [this, editorW, aiW]() {
      if (editorDock && editorDock->isVisible()) {
        resizeDocks({editorDock}, {editorW}, Qt::Horizontal);
      }
      if (aiDock && aiDock->isVisible()) {
        resizeDocks({aiDock}, {aiW}, Qt::Horizontal);
      }
      if (consoleDock && consoleDock->isVisible()) {
        resizeDocks({consoleDock}, {160}, Qt::Vertical);
      }
    });

    // AI-first migration: enable AI features and show the chat dock
    if (forceAiFirstLayout) {
      settings.setValue("feature/ai-features", true);
      Feature::enable_feature("ai-features", true);
    }

    if (Feature::ExperimentalAiFeatures.is_enabled() && this->aiDock) {
      this->aiDock->show();
      this->aiDock->raise();
      onExperimentalChanged();
    } else if (this->aiDock) {
      this->aiDock->hide();
    }

    settings.setValue("window/layoutVersion", kAiFirstLayoutVersion);
  } else {
#ifdef Q_OS_WIN
    // Try moving the main window into the display range, this
    // can occur when closing OpenSCAD on a second monitor which
    // is not available at the time the application is started
    // again.
    // On Windows that causes the main window to open in a not
    // easily reachable place.
    auto primaryScreen = QApplication::primaryScreen();
    auto desktopRect = primaryScreen->availableGeometry().adjusted(250, 150, -250, -150).normalized();
    auto windowRect = frameGeometry();
    if (!desktopRect.intersects(windowRect)) {
      windowRect.moveCenter(desktopRect.center());
      windowRect = windowRect.intersected(desktopRect);
      move(windowRect.topLeft());
      resize(windowRect.size());
    }
#endif  // ifdef Q_OS_WIN
  }

  // Compact toolbar icons to match the AI-first VS Code-style layout
  if (this->viewerToolBar) {
    this->viewerToolBar->setIconSize(QSize(16, 16));
  }
  if (this->editortoolbar) {
    this->editortoolbar->setIconSize(QSize(16, 16));
  }

  // Ensure reveal control matches dock visibility after restoreState()
  updateAIChatRevealButton(this->aiDock && !this->aiDock->isVisible());
}

void MainWindow::openRemainingFiles(const QStringList& filenames)
{
  for (int i = 1; i < filenames.size(); ++i) tabManager->createTab(filenames[i]);

  activeEditor->setFocus();
}

void MainWindow::changeEvent(QEvent *event)
{
  if (event->type() == QEvent::ThemeChange) {
    // Only follow OS theme when Appearance is System; Light/Dark stay fixed.
    setGlobalTheme();
    applyFlatWorkbenchChrome();
    if (this->tabManager) this->tabManager->applyTheme();
    if (this->aiDock && this->aiDock->chatWidget()) {
      this->aiDock->chatWidget()->applyVSCodeChrome();
    }
  }
  QMainWindow::changeEvent(event);
}
