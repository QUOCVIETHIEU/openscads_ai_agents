#pragma once

#include "glview/system-gl.h"
#include "core/Selection.h"
#include "gui/MouseSelector.h"

#include <memory>
#include <array>
#include <QImage>
#include <QMouseEvent>
#include <QPoint>
#include <QWheelEvent>
#include <QWidget>
#include <QtGlobal>
#include <QOpenGLWidget>
#include <QLabel>
#include <string>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include "glview/GLView.h"
#include "../core/MouseConfig.h"

class QGLView : public QOpenGLWidget, public GLView
{
  Q_OBJECT
  Q_PROPERTY(bool showEdges READ showEdges WRITE setShowEdges);
  Q_PROPERTY(bool showAxes READ showAxes WRITE setShowAxes);
  Q_PROPERTY(bool showFloor READ showFloor WRITE setShowFloor);
  Q_PROPERTY(bool showCrosshairs READ showCrosshairs WRITE setShowCrosshairs);
  Q_PROPERTY(bool orthoMode READ orthoMode WRITE setOrthoMode);
  Q_PROPERTY(double showScaleProportional READ showScaleProportional WRITE setShowScaleProportional);

public:
  QGLView(QWidget *parent = nullptr);
  ~QGLView() override;
#ifdef ENABLE_OPENCSG
  bool hasOpenCSGSupport() { return this->is_opencsg_capable; }
#endif
  // Properties
  bool orthoMode() const { return (this->cam.projection == Camera::ProjectionType::ORTHOGONAL); }
  void setOrthoMode(bool enabled);
  bool showScaleProportional() const { return this->showscale; }
  void setShowScaleProportional(bool enabled) { this->showscale = enabled; }
  std::string getRendererInfo() const override;
  float getDPI() override { return this->devicePixelRatio(); }

  const QImage& grabFrame();
  bool save(const char *filename) const override;
  void resetView();
  void viewAll();
  void selectPoint(int x, int y);
  std::vector<SelectedObject> findObject(int x, int y);
  int measure_state;

  int pickObject(QPoint position);

public slots:
  void ZoomIn();
  void ZoomOut();
  void setMouseCentricZoom(bool var) { this->mouseCentricZoom = var; }
  void setPanToolActive(bool active);
  bool isPanToolActive() const { return panToolActive; }
  void setMouseActions(int mouseAction, std::array<float, MouseConfig::ACTION_DIMENSION> var)
  {
    // Load an array defining the behaviour for a single mouse action.
    for (int i = 0; i < MouseConfig::ACTION_DIMENSION; i++) {
      this->mouseActions[MouseConfig::ACTION_DIMENSION * mouseAction + i] = var[i];
    }
  }

public:
  QLabel *statusLabel;

  void zoom(double v, bool relative);
  void zoomFov(double v);
  void zoomCursor(int x, int y, int zoom);
  void rotate(double x, double y, double z, bool relative);
  void rotate2(double x, double y, double z);
  /*! View-relative orbit that keeps pivot fixed in world space (click-point orbit). */
  void rotate2AboutPivot(double x, double y, double z, const Eigen::Vector3d& pivot);
  void translate(double x, double y, double z, bool relative, bool viewPortRelative = true);
  /*! Grab-hand pan: move the model with the cursor by dx/dy widget pixels (Y+ = down). */
  void panGrabByPixels(double dx, double dy);

private:
  void init();
  [[nodiscard]] Eigen::Matrix3d objectRotationMatrix() const;
  /*! Pick world-space orbit pivot under the cursor; falls back to current look-at. */
  Eigen::Vector3d pickOrbitPivot(const QPoint& pos);

  bool mouse_drag_active;
  bool mouse_drag_moved = true;
  bool mouseCentricZoom = true;
  bool panToolActive = false;
  bool hasOrbitPivot = false;
  Eigen::Vector3d orbitPivot{0, 0, 0};
  // Information held for each mouse action is a 3x2 rotation matrix, a 3x2 translation matrix, and a
  // zoom 2-vector.
  float mouseActions[MouseConfig::MouseAction::NUM_MOUSE_ACTIONS * MouseConfig::ACTION_DIMENSION];
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  QPointF last_mouse;
#else
  QPoint last_mouse;
#endif
  QImage frame;  // Used by grabFrame() and save()

  void wheelEvent(QWheelEvent *event) override;
  bool event(QEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;

  void initializeGL() override;
  void resizeGL(int w, int h) override;

  void paintGL() override;
  void normalizeAngle(GLdouble& angle);

#ifdef ENABLE_OPENCSG
  void display_opencsg_warning() override;
  std::unique_ptr<MouseSelector> selector;
private slots:
  void display_opencsg_warning_dialog();
#endif

signals:
  void cameraChanged();
  void resized();
  void doRightClick(QPoint screen_coordinate);
  void doLeftClick(QPoint screen_coordinate);
  void initialized();
};

/* These are defined in QLGView2.cc.  See the commentary there. */
// Can't include <QOpenGLContext>, as it will clash with glew. Forward declare.
class QOpenGLContext;
QOpenGLContext *getGLContext();
void setGLContext(QOpenGLContext *);
