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

#include "gui/QGLView.h"
#include <cmath>
#include <memory>
#include <QtCore/qpoint.h>

#include "core/Selection.h"
#include "geometry/linalg.h"
#include "gui/qtgettext.h"
#include "gui/Preferences.h"
#include "glview/Renderer.h"
#include "utils/degree_trig.h"
#include "utils/scope_guard.hpp"
#if defined(USE_GLEW) || defined(OPENCSG_GLEW)
#include "glview/glew-utils.h"
#endif

#include <QImage>
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QWidget>
#include <iostream>
#include <QApplication>
#include <QWheelEvent>
#include <QEvent>
#include <QNativeGestureEvent>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QErrorMessage>
#ifdef USE_GLAD
#include <QOpenGLContext>
#endif
#include "gui/OpenCSGWarningDialog.h"

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#ifdef ENABLE_OPENCSG
#include <opencsg.h>
#endif

#include "gui/qt-obsolete.h"
#include "gui/Measurement.h"

namespace {

QSurfaceFormat compatibleWidgetFormat()
{
  auto format = QSurfaceFormat::defaultFormat();
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setProfile(QSurfaceFormat::CompatibilityProfile);
  if (format.depthBufferSize() < 24) format.setDepthBufferSize(24);
  if (format.stencilBufferSize() < 8) format.setStencilBufferSize(8);
  return format;
}

}  // namespace

QGLView::QGLView(QWidget *parent) : QOpenGLWidget(parent)
{
  setFormat(compatibleWidgetFormat());
  init();
}

QGLView::~QGLView()
{
  // Just to make sure we can call GL functions in the supertype destructor
  makeCurrent();
}

void QGLView::init()
{
  resetView();

  this->mouse_drag_active = false;
  this->statusLabel = nullptr;

  setMouseTracking(true);
}

void QGLView::resetView()
{
  cam.resetView();
}

void QGLView::viewAll()
{
  if (auto renderer = this->getRenderer()) {
    auto bbox = renderer->getBoundingBox();
    cam.autocenter = true;
    cam.viewAll(renderer->getBoundingBox());
  }
}

void QGLView::initializeGL()
{
#if defined(USE_GLEW) || defined(OPENCSG_GLEW)
  // Since OpenCSG requires glew, we need to initialize it.
  // ..in a separate compilation unit to avoid duplicate symbols with x.
  initializeGlew();
#endif
#ifdef USE_GLAD
  // We could ask for gladLoadGLES2UserPtr() here if we want to use GLES2+
  const auto version = gladLoadGLUserPtr(
    [](void *ctx, const char *name) -> GLADapiproc {
      return reinterpret_cast<QOpenGLContext *>(ctx)->getProcAddress(name);
    },
    this->context());
  if (version == 0) {
    std::cerr << "Unable to init GLAD" << std::endl;
    return;
  }
  PRINTDB("GLAD: Loaded OpenGL %d.%d", GLAD_VERSION_MAJOR(version) % GLAD_VERSION_MINOR(version));
#endif  // ifdef USE_GLAD

  PRINTD(gl_dump());

  GLView::initializeGL();

  this->selector = std::make_unique<MouseSelector>(this);

  emit initialized();
}

std::string QGLView::getRendererInfo() const
{
  std::ostringstream info;
  info << gl_dump();
  // Don't translate as translated text in the Library Info dialog is not wanted
  info << "\nQt graphics widget: QOpenGLWidget";
  auto qsf = this->format();
  auto rbits = qsf.redBufferSize();
  auto gbits = qsf.greenBufferSize();
  auto bbits = qsf.blueBufferSize();
  auto abits = qsf.alphaBufferSize();
  auto dbits = qsf.depthBufferSize();
  auto sbits = qsf.stencilBufferSize();
  info << boost::format("\nQSurfaceFormat: RGBA(%d%d%d%d), depth(%d), stencil(%d)\n\n") % rbits % gbits %
            bbits % abits % dbits % sbits;
  info << gl_extensions_dump();
  return info.str();
}

#ifdef ENABLE_OPENCSG
void QGLView::display_opencsg_warning()
{
  if (GlobalPreferences::inst()->getValue("advanced/opencsg_show_warning").toBool()) {
    QTimer::singleShot(0, this, &QGLView::display_opencsg_warning_dialog);
  }
}

void QGLView::display_opencsg_warning_dialog()
{
  auto dialog = new OpenCSGWarningDialog(this);

  QString message =
    _("Warning: Missing OpenGL capabilities for OpenCSG - OpenCSG has been disabled.\n\n");
  message +=
    _("It is highly recommended to use OpenSCAD on a system with "
      "OpenGL 2.0 or later.\n"
      "Your renderer information is as follows:\n");
#if defined(USE_GLEW) || defined(OPENCSG_GLEW)
  QString rendererinfo(_("GLEW version %1\n%2 (%3)\nOpenGL version %4\n"));
  message +=
    rendererinfo.arg((const char *)glewGetString(GLEW_VERSION), (const char *)glGetString(GL_RENDERER),
                     (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_VERSION));
#endif
#ifdef USE_GLAD
  QString rendererinfo(_("GLAD version %1\n%2 (%3)\nOpenGL version %4\n"));
  message +=
    rendererinfo.arg(GLAD_GENERATOR_VERSION, (const char *)glGetString(GL_RENDERER),
                     (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_VERSION));
#endif
  dialog->setText(message);
  dialog->exec();
}
#endif  // ifdef ENABLE_OPENCSG

void QGLView::resizeGL(int w, int h)
{
  GLView::resizeGL(w, h);
  emit resized();
}

void QGLView::paintGL()
{
  GLView::paintGL();

  if (statusLabel) {
    auto status = QString("%1 (%2x%3)")
                    .arg(QString::fromStdString(cam.statusText()))
                    .arg(size().rwidth())
                    .arg(size().rheight());
    statusLabel->setText(status);
  }
}

void QGLView::mousePressEvent(QMouseEvent *event)
{
  if (!mouse_drag_active) {
    mouse_drag_moved = false;
  }

  mouse_drag_active = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  last_mouse = event->globalPosition();
#else
  last_mouse = event->globalPos();
#endif

  // Middle-button drag, or Pan tool + left-drag — grabbing-hand cursor.
  if (event->button() == Qt::MiddleButton ||
      (panToolActive && event->button() == Qt::LeftButton)) {
    setCursor(Qt::ClosedHandCursor);
  }

  // Capture orbit pivot under the cursor so left-drag rotates around that point
  // instead of the world origin / default look-at.
  orbitPivot = pickOrbitPivot(event->pos());
  hasOrbitPivot = true;
}

/*
 * Voodoo warning...
 *
 * This function selects the widget's OpenGL context (via this->makeCurrent()).
 * Because it's changing the OpenGL context, it seems polite to save and restore it.
 * That resolution seems correct, independent of the mysteries below.
 *
 * Let's call the widget's context W, and the alternate context that we are called with A.
 *
 * It's important that A is selected when we return (as it is when we enter), because
 * if it isn't then sometimes the subsequent mouseReleaseEvent is called with W, when it
 * is normally called with A.  When that happens, the object-selection magic in selectObject
 * messes up W, and rendering is forever after broken in that window.
 *
 * However, as hygienic as saving-and-restoring seems, the picture is still unsatisfying.
 *
 * Open questions:
 * - Why are these mouse event functions called with A, rather than being called with W?
 *   It's unsurprising that the selection magic needs its own GL context, but it seems like
 *   it should be the one that needs to explicitly select it, not this function.
 * - Where did A come from?
 * - Why does a subsequent mouseReleaseEvent call get called with W?
 * - Why does it only sometimes get called with W, and sometimes (correctly) with A?
 * - Why do later mouseReleaseEvent calls revert to being (correctly) called with A?
 * - Why does this only happen with right clicks?  With left clicks, this function
 *   changes the context, but it's OK again on the following mouseReleaseEvent.
 * - Why does this only happen when you click on empty space, and not when you click
 *   on the model?  Double clicks on the model are not detected as double clicks.
 *   Perhaps this is because the first click pops a menu and the second click is
 *   on the menu, not this widget.
 *
 * getGLContext() and setGLContext() are in a separate file, QGLView2.cc, so that this
 * file doesn't need a full declaration of QOpenGLContext.  <QOpenGLContext> is
 * incompatible with GLEW and causes compilation warnings.
 *
 * For future attention:
 * - This function should probably only react to left double clicks.  Right double clicks
 *   should probably be ignored.
 */
void QGLView::mouseDoubleClickEvent(QMouseEvent *event)
{
  QOpenGLContext *oldContext = getGLContext();
  this->makeCurrent();
  auto guard = sg::make_scope_guard([this, oldContext] {
    this->doneCurrent();
    setGLContext(oldContext);
  });

  setupCamera();

  int viewport[4];
  GLdouble modelview[16];
  GLdouble projection[16];

  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
  glGetDoublev(GL_PROJECTION_MATRIX, projection);

  const double dpi = this->getDPI();
  const double x = event->pos().x() * dpi;
  const double y = viewport[3] - event->pos().y() * dpi;
  GLfloat z = 0;

  glGetError();  // clear error state so we don't pick up previous errors
  glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &z);
  if (const auto glError = glGetError(); glError != GL_NO_ERROR) {
    if (statusLabel) {
      auto status = QString("Center View: OpenGL Error reading Pixel: %s")
                      .arg(QString::fromLocal8Bit((const char *)gluErrorString(glError)));
      statusLabel->setText(status);
    }
    return;
  }

  if (z == 1) {
    return;  // outside object
  }

  GLdouble px, py, pz;

  auto success = gluUnProject(x, y, z, modelview, projection, viewport, &px, &py, &pz);

  if (success == GL_TRUE) {
    cam.object_trans -= Vector3d(px, py, pz);
    update();
    emit cameraChanged();
  }
}

void QGLView::normalizeAngle(GLdouble& angle)
{
  while (angle < 0) angle += 360;
  while (angle > 360) angle -= 360;
}

void QGLView::mouseMoveEvent(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  auto this_mouse = event->globalPosition();
#else
  auto this_mouse = event->globalPos();
#endif
  if (measure_state != Measurement::MEASURE_IDLE) {
    QPoint pt = event->pos();
    this->shown_obj = findObject(pt.x(), pt.y());
    update();
  }
  double raw_dx = this_mouse.x() - last_mouse.x();
  double raw_dy = this_mouse.y() - last_mouse.y();
  // Soften rotation / zoom-from-drag; pan uses raw pixels for 1:1 cursor tracking.
  double dx = raw_dx * 0.7;
  double dy = raw_dy * 0.7;
  if (mouse_drag_active) {
    mouse_drag_moved = true;

    bool multipleButtonsPressed = false;
    int buttonIndex = -1;
    if (event->buttons() & Qt::LeftButton) {
      buttonIndex = 0;
    }
    if (event->buttons() & Qt::MiddleButton) {
      if (buttonIndex != -1) {
        multipleButtonsPressed = true;
      } else {
        buttonIndex = 1;
      }
    }
    if (event->buttons() & Qt::RightButton) {
      if (buttonIndex != -1) {
        multipleButtonsPressed = true;
      } else {
        buttonIndex = 2;
      }
    }
    int modifierIndex = 0;
    if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
      modifierIndex = 1;
    }
    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
      if (modifierIndex == 1) {
        modifierIndex = 3;  // Ctrl + Shift
      } else {
        modifierIndex = 2;
      }
    }

    if (buttonIndex != -1 && !multipleButtonsPressed) {
      float *selectedMouseActions =
        &this->mouseActions[MouseConfig::ACTION_DIMENSION * (buttonIndex + modifierIndex * 3)];

      // Grabbing-hand middle-drag always pans in the view plane (left/right & up/down),
      // matching ClosedHandCursor — even if an older setting still maps middle to fore/back.
      // Pan toolbar tool also forces left-drag to pan.
      if ((buttonIndex == 1 && modifierIndex == 0) || (panToolActive && buttonIndex == 0 && modifierIndex == 0)) {
        selectedMouseActions =
          MouseConfig::viewActionArrays.at(MouseConfig::PAN_LR_UD).data();
      }

      // Rotation angles from mouse movement
      // First 6 elements to selectedMouseActions are interpreted as a row-major 3x2 matrix, which is
      // right-multiplied by (dx, dy)^T to produce the rotation angle increments.
      double rx = selectedMouseActions[0] * dx + selectedMouseActions[1] * dy;
      double ry = selectedMouseActions[2] * dx + selectedMouseActions[3] * dy;
      double rz = selectedMouseActions[4] * dx + selectedMouseActions[5] * dy;
      if (!(rx == 0.0 && ry == 0.0 && rz == 0.0)) {
        // View-relative orbit (same path as 3D-mouse) feels much more controllable than
        // accumulating world Euler angles, which gimbal-lock and twist unpredictably.
        // Soften gain so small hand movements don't overshoot.
        constexpr double kRotateGain = 0.55;
        const double ax = rx * kRotateGain;
        const double ay = ry * kRotateGain;
        const double az = rz * kRotateGain;
        if (hasOrbitPivot) {
          rotate2AboutPivot(ax, ay, az, orbitPivot);
        } else {
          rotate2(ax, ay, az);
        }
      }

      // Panning: keep the model under the cursor (grab-hand 1:1) by unprojecting
      // the screen delta onto the look-at plane. The older Euler+frustum path drifted
      // badly once the view was rotated.
      const bool wantsPan = selectedMouseActions[6] != 0.0f || selectedMouseActions[7] != 0.0f ||
                            selectedMouseActions[8] != 0.0f || selectedMouseActions[9] != 0.0f ||
                            selectedMouseActions[10] != 0.0f || selectedMouseActions[11] != 0.0f;
      if (wantsPan && (raw_dx != 0.0 || raw_dy != 0.0)) {
        const bool screenPlanePan =
          selectedMouseActions[6] != 0.0f || selectedMouseActions[11] != 0.0f ||
          selectedMouseActions[7] != 0.0f || selectedMouseActions[10] != 0.0f;
        const bool depthPan = selectedMouseActions[8] != 0.0f || selectedMouseActions[9] != 0.0f;

        if (screenPlanePan) {
          // Match PAN_LR_UD signs: +screen X → +view X, +screen Y → −view Z (down).
          const double pan_dx =
            selectedMouseActions[6] * raw_dx + selectedMouseActions[7] * raw_dy;
          const double pan_dy =
            -(selectedMouseActions[10] * raw_dx + selectedMouseActions[11] * raw_dy);
          panGrabByPixels(pan_dx, pan_dy);
        }
        if (depthPan) {
          // Fore/back pan still uses the classic zoom-scaled path (into the scene).
          const int vp_w = std::max(1, QWidget::width());
          const int vp_h = std::max(1, QWidget::height());
          double my = selectedMouseActions[6 + 2] * (raw_dx / vp_w) +
                      selectedMouseActions[6 + 3] * (raw_dy / vp_h);
          my *= 3.0 * cam.zoomValue();
          if (my != 0.0) translate(0.0, my, 0.0, true);
        }
      }

      // Zoom from mouse movement
      // Final 2 elements of selectedMouseActions are interpreted as a 2-dimensional vector. The inner
      // product of this is taken with (dx, dy)^T to produce the zoom increment.
      double dZoom = selectedMouseActions[12] * dx + selectedMouseActions[13] * dy;
      if (dZoom != 0.0) {
        dZoom *= 12.0;
        zoom(dZoom, true);
      }
    }
  }
  last_mouse = this_mouse;
}

void QGLView::mouseReleaseEvent(QMouseEvent *event)
{
  mouse_drag_active = false;
  hasOrbitPivot = false;
  releaseMouse();

  if (panToolActive) {
    setCursor(Qt::OpenHandCursor);
  } else if (!(event->buttons() & Qt::MiddleButton)) {
    unsetCursor();
  }

  if (!mouse_drag_moved) {
    if (event->button() == Qt::RightButton) {
      QPoint point = event->pos();
      emit doRightClick(point);
    }
    if (event->button() == Qt::LeftButton) {
      QPoint point = event->pos();
      emit doLeftClick(point);
    }
  }
  mouse_drag_moved = false;
}

const QImage& QGLView::grabFrame()
{
  // Force reading from front buffer. Some configurations will read from the back buffer here.
  glReadBuffer(GL_FRONT);
  this->frame = grabFramebuffer();
  return this->frame;
}

bool QGLView::save(const char *filename) const
{
  return this->frame.save(filename, "PNG");
}

void QGLView::wheelEvent(QWheelEvent *event)
{
  const auto pos = Q_WHEEL_EVENT_POSITION(event);

  // Mouse wheels usually report angleDelta (120 per notch).
  // macOS trackpads often report pixelDelta with angleDelta == 0.
  int v = event->angleDelta().y();
  if (v == 0) v = event->angleDelta().x();
  if (v == 0) {
    const QPoint pixels = event->pixelDelta();
    if (!pixels.isNull()) {
      const int px = pixels.y() != 0 ? pixels.y() : pixels.x();
      // Scale pixel scroll into wheel-notch units used by Camera::zoom()
      v = px * 4;
    }
  }
  if (v == 0) {
    event->ignore();
    return;
  }

  if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
    zoomFov(v);
  } else if (this->mouseCentricZoom) {
    zoomCursor(pos.x(), pos.y(), v);
  } else {
    zoom(v, true);
  }
  event->accept();
}

bool QGLView::event(QEvent *event)
{
  // Trackpad pinch-to-zoom (macOS / some other platforms)
  if (event->type() == QEvent::NativeGesture) {
    auto *gesture = static_cast<QNativeGestureEvent *>(event);
    if (gesture->gestureType() == Qt::ZoomNativeGesture) {
      // gesture->value() is a small relative scale delta (e.g. 0.01)
      const int v = qRound(gesture->value() * 600.0);
      if (v != 0) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPointF local = gesture->position();
#else
        const QPointF local = gesture->pos();
#endif
        if (this->mouseCentricZoom) {
          zoomCursor(int(local.x()), int(local.y()), v);
        } else {
          zoom(v, true);
        }
      }
      event->accept();
      return true;
    }
  }
  return QOpenGLWidget::event(event);
}

void QGLView::ZoomIn()
{
  zoom(120, true);
}

void QGLView::ZoomOut()
{
  zoom(-120, true);
}

void QGLView::setPanToolActive(bool active)
{
  panToolActive = active;
  if (active) {
    setCursor(Qt::OpenHandCursor);
  } else if (!mouse_drag_active) {
    unsetCursor();
  }
}

void QGLView::zoom(double v, bool relative)
{
  this->cam.zoom(v, relative);
  update();
  emit cameraChanged();
}

void QGLView::zoomFov(double v)
{
  this->cam.setVpf(this->cam.fovValue() * pow(0.9, v / 120.0));
  update();
  emit cameraChanged();
}

void QGLView::zoomCursor(int x, int y, int zoom)
{
  const auto old_dist = cam.zoomValue();
  this->cam.zoom(zoom, true);
  const auto dist = cam.zoomValue();
  const auto ratio = old_dist / dist - 1.0;
  // screen coordinates from -1 to 1 — use logical size so Retina DPR matches mouse coords
  const double dpr = std::max(1.0e-6, static_cast<double>(devicePixelRatioF()));
  const double logical_w = std::max(1.0, cam.pixel_width / dpr);
  const double logical_h = std::max(1.0, cam.pixel_height / dpr);
  const auto screen_x = 2.0 * (x + 0.5) / logical_w - 1.0;
  const auto screen_y = 1.0 - 2.0 * (y + 0.5) / logical_h;
  const auto height = dist * tan_degrees(cam.fov / 2);
  const auto mx = ratio * screen_x * (aspectratio * height);
  const auto mz = ratio * screen_y * height;
  translate(-mx, 0, -mz, true);
}

void QGLView::panGrabByPixels(double dx, double dy)
{
  if (dx == 0.0 && dy == 0.0) return;

  makeCurrent();

  // Rebuild the same matrices used for drawing, including object_trans, so unproject
  // returns deltas in the space we add to object_trans.
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  const auto dist = cam.zoomValue();
  switch (cam.projection) {
  case Camera::ProjectionType::PERSPECTIVE:
    gluPerspective(cam.fov, aspectratio, 0.1 * dist, 100 * dist);
    break;
  default:
  case Camera::ProjectionType::ORTHOGONAL: {
    const auto height = dist * tan_degrees(cam.fov / 2);
    glOrtho(-height * aspectratio, height * aspectratio, -height, height, -100 * dist, +100 * dist);
    break;
  }
  }
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  gluLookAt(0.0, -dist, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0);
  glRotated(cam.object_rot.x(), 1.0, 0.0, 0.0);
  glRotated(cam.object_rot.y(), 0.0, 1.0, 0.0);
  glRotated(cam.object_rot.z(), 0.0, 0.0, 1.0);
  glTranslated(cam.object_trans.x(), cam.object_trans.y(), cam.object_trans.z());

  GLdouble mv[16];
  GLdouble pr[16];
  int vp[4];
  glGetDoublev(GL_MODELVIEW_MATRIX, mv);
  glGetDoublev(GL_PROJECTION_MATRIX, pr);
  glGetIntegerv(GL_VIEWPORT, vp);

  // Depth of the look-at point (model origin after current transforms).
  GLdouble ox, oy, oz;
  if (gluProject(0.0, 0.0, 0.0, mv, pr, vp, &ox, &oy, &oz) != GL_TRUE) return;

  const double dpr = std::max(1.0e-6, static_cast<double>(devicePixelRatioF()));
  GLdouble x0, y0, z0, x1, y1, z1;
  if (gluUnProject(ox, oy, oz, mv, pr, vp, &x0, &y0, &z0) != GL_TRUE) return;
  // GL window Y grows upward; widget/mouse Y grows downward.
  if (gluUnProject(ox + dx * dpr, oy - dy * dpr, oz, mv, pr, vp, &x1, &y1, &z1) != GL_TRUE) return;

  cam.object_trans.x() += (x1 - x0);
  cam.object_trans.y() += (y1 - y0);
  cam.object_trans.z() += (z1 - z0);
  update();
  emit cameraChanged();
}

void QGLView::setOrthoMode(bool enabled)
{
  if (enabled) this->cam.setProjection(Camera::ProjectionType::ORTHOGONAL);
  else this->cam.setProjection(Camera::ProjectionType::PERSPECTIVE);
}

void QGLView::translate(double x, double y, double z, bool relative, bool viewPortRelative)
{
  Matrix3d aax, aay, aaz;
  aax = angle_axis_degrees(-cam.object_rot.x(), Vector3d::UnitX());
  aay = angle_axis_degrees(-cam.object_rot.y(), Vector3d::UnitY());
  aaz = angle_axis_degrees(-cam.object_rot.z(), Vector3d::UnitZ());
  Matrix3d tm3 = aaz * aay * aax;

  Matrix4d tm = Matrix4d::Identity();
  if (viewPortRelative) {
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) {
        tm(j, i) = tm3(j, i);
      }
    }
  }

  Matrix4d vec;
  // clang-format off
  vec << 0, 0, 0, x,
         0, 0, 0, y,
         0, 0, 0, z,
         0, 0, 0, 1;
  // clang-format on
  tm = tm * vec;
  double f = relative ? 1 : 0;
  cam.object_trans.x() = f * cam.object_trans.x() + tm(0, 3);
  cam.object_trans.y() = f * cam.object_trans.y() + tm(1, 3);
  cam.object_trans.z() = f * cam.object_trans.z() + tm(2, 3);
  update();
  emit cameraChanged();
}

void QGLView::rotate(double x, double y, double z, bool relative)
{
  double f = relative ? 1 : 0;
  cam.object_rot.x() = f * cam.object_rot.x() + x;
  cam.object_rot.y() = f * cam.object_rot.y() + y;
  cam.object_rot.z() = f * cam.object_rot.z() + z;
  normalizeAngle(cam.object_rot.x());
  normalizeAngle(cam.object_rot.y());
  normalizeAngle(cam.object_rot.z());
  update();
  emit cameraChanged();
}

void QGLView::rotate2(double x, double y, double z)
{
  // This vector describes the rotation.
  // The direction of the vector is the angle around which to rotate, and
  // the length of the vector is the angle by which to rotate
  Vector3d rot = Vector3d(-x, -y, -z);

  // get current rotation matrix
  Matrix3d aax, aay, aaz, rmx;
  aax = angle_axis_degrees(-cam.object_rot.x(), Vector3d::UnitX());
  aay = angle_axis_degrees(-cam.object_rot.y(), Vector3d::UnitY());
  aaz = angle_axis_degrees(-cam.object_rot.z(), Vector3d::UnitZ());
  rmx = aaz * (aay * aax);

  // rotate
  rmx = rmx * angle_axis_degrees(rot.norm(), rot.normalized());

  // back to euler
  // see: http://staff.city.ac.uk/~sbbh653/publications/euler.pdf
  double theta, psi, phi;
  if (abs(rmx(2, 0)) != 1) {
    theta = -asin_degrees(rmx(2, 0));
    psi = atan2_degrees(rmx(2, 1) / cos_degrees(theta), rmx(2, 2) / cos_degrees(theta));
    phi = atan2_degrees(rmx(1, 0) / cos_degrees(theta), rmx(0, 0) / cos_degrees(theta));
  } else {
    phi = 0;
    if (rmx(2, 0) == -1) {
      theta = 90;
      psi = phi + atan2_degrees(rmx(0, 1), rmx(0, 2));
    } else {
      theta = -90;
      psi = -phi + atan2_degrees(-rmx(0, 1), -rmx(0, 2));
    }
  }

  cam.object_rot.x() = -psi;
  cam.object_rot.y() = -theta;
  cam.object_rot.z() = -phi;

  normalizeAngle(cam.object_rot.x());
  normalizeAngle(cam.object_rot.y());
  normalizeAngle(cam.object_rot.z());

  update();
  emit cameraChanged();
}

Eigen::Matrix3d QGLView::objectRotationMatrix() const
{
  // Matches glRotated(x)*glRotated(y)*glRotated(z) order used in setupCamera().
  const Matrix3d rx = angle_axis_degrees(cam.object_rot.x(), Vector3d::UnitX());
  const Matrix3d ry = angle_axis_degrees(cam.object_rot.y(), Vector3d::UnitY());
  const Matrix3d rz = angle_axis_degrees(cam.object_rot.z(), Vector3d::UnitZ());
  return rx * ry * rz;
}

void QGLView::rotate2AboutPivot(double x, double y, double z, const Eigen::Vector3d& pivot)
{
  // eye = LookAt * R * (geom + object_trans). Keep `pivot` fixed while changing R:
  //   R_new * (pivot + t_new) = R_old * (pivot + t_old)
  const Matrix3d R_old = objectRotationMatrix();
  const Vector3d v = R_old * (pivot + cam.object_trans);
  rotate2(x, y, z);
  const Matrix3d R_new = objectRotationMatrix();
  cam.object_trans = R_new.inverse() * v - pivot;
  update();
  emit cameraChanged();
}

Eigen::Vector3d QGLView::pickOrbitPivot(const QPoint& pos)
{
  // Default: current look-at (same as classic OpenSCAD orbit center).
  Vector3d fallback = -cam.object_trans;

  if (!isValid()) return fallback;

  makeCurrent();
  auto guard = sg::make_scope_guard([this]() { this->doneCurrent(); });

  // Use LookAt*R without object_trans — same trick as mouseDoubleClickEvent:
  // unproject recovers (geom + object_trans), then subtract t to get geom.
  setupCamera();

  int viewport[4];
  GLdouble modelview[16];
  GLdouble projection[16];
  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
  glGetDoublev(GL_PROJECTION_MATRIX, projection);

  const double dpi = this->getDPI();
  const double win_x = pos.x() * dpi;
  const double win_y = viewport[3] - pos.y() * dpi;

  GLfloat z = 1.0f;
  glGetError();
  glReadPixels(static_cast<GLint>(win_x), static_cast<GLint>(win_y), 1, 1, GL_DEPTH_COMPONENT,
               GL_FLOAT, &z);
  if (glGetError() != GL_NO_ERROR) return fallback;

  if (z >= 1.0f - 1e-6f) {
    // Empty space: orbit around the point on the look-at plane under the cursor.
    GLdouble lx, ly, lz;
    if (gluProject(0.0, 0.0, 0.0, modelview, projection, viewport, &lx, &ly, &lz) != GL_TRUE) {
      return fallback;
    }
    z = static_cast<GLfloat>(lz);
  }

  GLdouble px, py, pz;
  if (gluUnProject(win_x, win_y, z, modelview, projection, viewport, &px, &py, &pz) != GL_TRUE) {
    return fallback;
  }

  // unprojected p = geom + object_trans  →  geom pivot
  return Vector3d(px, py, pz) - cam.object_trans;
}

std::vector<SelectedObject> QGLView::findObject(int mouse_x, int mouse_y)
{
  int viewport[4] = {0, 0, 0, 0};
  double posXF, posYF, posZF;
  double posXN, posYN, posZN;
  viewport[2] = size().rwidth();
  viewport[3] = size().rheight();

  GLdouble winX = mouse_x;
  GLdouble winY = viewport[3] - mouse_y;

  gluUnProject(winX, winY, 1, this->modelview, this->projection, viewport, &posXF, &posYF, &posZF);
  gluUnProject(winX, winY, -1, this->modelview, this->projection, viewport, &posXN, &posYN, &posZN);
  Vector3d far_pt(posXF, posYF, posZF);
  Vector3d near_pt(posXN, posYN, posZN);

  Vector3d testpt(0, 0, 0);
  std::vector<SelectedObject> result;
  auto renderer = this->getRenderer();
  if (renderer == nullptr) return result;
  result = renderer->findModelObject(near_pt, far_pt, mouse_x, mouse_y, cam.zoomValue() / 300);
  return result;
}

void QGLView::selectPoint(int mouse_x, int mouse_y)
{
  std::vector<SelectedObject> obj = findObject(mouse_x, mouse_y);
  if (obj.size() == 1) {
    this->selected_obj.push_back(obj[0]);
    update();
  }
}

int QGLView::pickObject(QPoint position)
{
  if (!isValid()) return -1;

  if (this->getRenderer()) {
    this->makeCurrent();
    auto guard = sg::make_scope_guard([this]() { this->doneCurrent(); });

    // Update the selector with the right image size
    this->selector->reset(this);

    return this->selector->select(this->getRenderer(), position.x(), position.y());
  }
  return -1;
}
