#include "geometry/GeometryProjection.h"

#include <memory>
#include <utility>
#include <vector>

#include "geometry/ClipperUtils.h"
#include "geometry/PolySet.h"
#include "geometry/PolySetUtils.h"
#include "glview/RenderSettings.h"
#include "utils/degree_trig.h"

#ifdef ENABLE_MANIFOLD
#include "geometry/manifold/ManifoldGeometry.h"
#include "geometry/manifold/manifoldutils.h"
#endif
#ifdef ENABLE_CGAL
#include "geometry/cgal/cgalutils.h"
#endif

namespace GeometryProjection {

namespace {

std::shared_ptr<const Polygon2d> emptyPolygon()
{
  return std::make_shared<Polygon2d>();
}

std::shared_ptr<const Geometry> withTransform(const std::shared_ptr<const Geometry>& geom,
                                              const Transform3d& transform)
{
  if (transform.matrix().isIdentity()) {
    return geom;
  }
  std::shared_ptr<Geometry> copy(geom->copy());
  copy->transform(transform);
  return copy;
}

std::shared_ptr<const Polygon2d> projectPolySetPath(const std::shared_ptr<const Geometry>& geom)
{
  auto ps = PolySetUtils::getGeometryAsPolySet(geom);
  if (!ps || ps->isEmpty()) {
    return emptyPolygon();
  }
  auto poly = PolySetUtils::project(*ps);
  if (!poly) {
    return emptyPolygon();
  }
  std::vector<std::shared_ptr<const Polygon2d>> tmp_geom;
  tmp_geom.push_back(std::shared_ptr<const Polygon2d>(std::move(poly)));
  auto projected = ClipperUtils::applyProjection(tmp_geom);
  if (!projected) {
    return emptyPolygon();
  }
  return std::shared_ptr<const Polygon2d>(std::move(projected));
}

Vector3d bboxCenter(const std::shared_ptr<const Geometry>& geom)
{
  const BoundingBox bb = geom->getBoundingBox();
  return 0.5 * (bb.min() + bb.max());
}

}  // namespace

std::shared_ptr<const Polygon2d> projectGeometryToPolygon2d(
  const std::shared_ptr<const Geometry>& geom, const Transform3d& transform)
{
  if (!geom || geom->isEmpty()) {
    return emptyPolygon();
  }

  if (geom->getDimension() == 2) {
    if (auto poly = std::dynamic_pointer_cast<const Polygon2d>(geom)) {
      if (transform.matrix().isIdentity()) {
        return poly;
      }
      return poly;
    }
    return emptyPolygon();
  }

  auto transformed = withTransform(geom, transform);

#ifdef ENABLE_MANIFOLD
  if (RenderSettings::inst()->backend3D == RenderBackend3D::ManifoldBackend) {
    auto manifold = ManifoldUtils::createManifoldFromGeometry(transformed);
    if (manifold != nullptr && !manifold->isEmpty()) {
      auto poly2d = manifold->project();
      return std::shared_ptr<const Polygon2d>(ClipperUtils::sanitize(poly2d));
    }
  }
#endif

  return projectPolySetPath(transformed);
}

std::shared_ptr<const Polygon2d> projectGeometryToPolygon2d(
  const std::shared_ptr<const Geometry>& geom)
{
  return projectGeometryToPolygon2d(geom, Transform3d::Identity());
}

std::shared_ptr<const Polygon2d> sliceGeometryAtPlane(
  const std::shared_ptr<const Geometry>& geom, const Vector3d& planePoint,
  const Transform3d& alignCutToXY)
{
  if (!geom || geom->isEmpty() || geom->getDimension() != 3) {
    return emptyPolygon();
  }

  Transform3d toPlane = Transform3d::Identity();
  toPlane.translate(-planePoint);
  const Transform3d full = alignCutToXY * toPlane;
  auto transformed = withTransform(geom, full);

#ifdef ENABLE_MANIFOLD
  if (RenderSettings::inst()->backend3D == RenderBackend3D::ManifoldBackend) {
    auto manifold = ManifoldUtils::createManifoldFromGeometry(transformed);
    if (manifold != nullptr && !manifold->isEmpty()) {
      auto poly2d = manifold->slice();
      auto sanitized = ClipperUtils::sanitize(poly2d);
      if (sanitized) {
        return std::shared_ptr<const Polygon2d>(std::move(sanitized));
      }
    }
  }
#endif

#ifdef ENABLE_CGAL
  auto Nptr = CGALUtils::getNefPolyhedronFromGeometry(transformed);
  if (Nptr && !Nptr->isEmpty()) {
    auto poly = CGALUtils::project(*Nptr, true);
    if (poly) {
      return std::shared_ptr<const Polygon2d>(std::move(poly));
    }
  }
#endif

  return projectGeometryToPolygon2d(transformed, Transform3d::Identity());
}

OrthographicDrawingViews projectOrthographicViews(
  const std::shared_ptr<const Geometry>& geom)
{
  OrthographicDrawingViews views;
  if (!geom || geom->isEmpty()) {
    views.top = emptyPolygon();
    return views;
  }

  if (geom->getDimension() == 2) {
    if (auto poly = std::dynamic_pointer_cast<const Polygon2d>(geom)) {
      views.top = poly;
    } else {
      views.top = emptyPolygon();
    }
    return views;
  }

  Transform3d front = Transform3d::Identity();
  front.rotate(angle_axis_degrees(90, Vector3d::UnitX()));

  Transform3d side = Transform3d::Identity();
  side.rotate(angle_axis_degrees(90, Vector3d::UnitY()));

  views.top = projectGeometryToPolygon2d(geom, Transform3d::Identity());
  views.front = projectGeometryToPolygon2d(geom, front);
  views.side = projectGeometryToPolygon2d(geom, side);

  const BoundingBox bb = geom->getBoundingBox();
  const Vector3d c = bboxCenter(geom);
  const double sx = std::max(1e-6, bb.max().x() - bb.min().x());
  const double sy = std::max(1e-6, bb.max().y() - bb.min().y());

  // Cut near a rim (not geometric mid) so dense hole grids don't shred the section profile.
  // 12% inset from the min face typically lands in the outer wall / flange.
  const double cutX = bb.min().x() + 0.12 * sx;
  const double cutY = bb.min().y() + 0.12 * sy;

  // Section A-A: plane X = cutX  (|| YZ). Rotate Y+90 so X→−Z', slice Z'=0.
  Transform3d alignAA = Transform3d::Identity();
  alignAA.rotate(angle_axis_degrees(90, Vector3d::UnitY()));
  views.sectionAA = sliceGeometryAtPlane(geom, Vector3d(cutX, c.y(), c.z()), alignAA);

  // Section B-B: plane Y = cutY  (|| XZ). Rotate X−90 so Y→Z', slice Z'=0.
  Transform3d alignBB = Transform3d::Identity();
  alignBB.rotate(angle_axis_degrees(-90, Vector3d::UnitX()));
  views.sectionBB = sliceGeometryAtPlane(geom, Vector3d(c.x(), cutY, c.z()), alignBB);

  return views;
}

}  // namespace GeometryProjection
