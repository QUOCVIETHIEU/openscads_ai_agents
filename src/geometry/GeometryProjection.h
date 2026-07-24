#pragma once

#include <memory>

#include "geometry/Geometry.h"
#include "geometry/Polygon2d.h"
#include "geometry/linalg.h"

struct OrthographicDrawingViews {
  std::shared_ptr<const Polygon2d> top;       // XY silhouette
  std::shared_ptr<const Polygon2d> front;     // XZ silhouette
  std::shared_ptr<const Polygon2d> side;      // YZ silhouette
  // Material cuts (hatched on PDF) — cut near a rim so hole grids don't shred the section
  std::shared_ptr<const Polygon2d> sectionAA;  // cut || YZ (look along +X)
  std::shared_ptr<const Polygon2d> sectionBB;  // cut || XZ (look along +Y)
};

namespace GeometryProjection {

/*! Orthographic silhouette projection of 3D geometry onto the XY plane (non-cut). */
std::shared_ptr<const Polygon2d> projectGeometryToPolygon2d(
  const std::shared_ptr<const Geometry>& geom);

/*! Apply transform then project to XY silhouette. */
std::shared_ptr<const Polygon2d> projectGeometryToPolygon2d(
  const std::shared_ptr<const Geometry>& geom, const Transform3d& transform);

/*! Slice after translating `planePoint` to origin and applying `alignCutToXY` (Z=0 = cut plane). */
std::shared_ptr<const Polygon2d> sliceGeometryAtPlane(
  const std::shared_ptr<const Geometry>& geom, const Vector3d& planePoint,
  const Transform3d& alignCutToXY);

/*! Build Top / Front / Side silhouettes plus Section A-A / B-B rim cuts.
    If geom is already 2D, only `top` is filled. */
OrthographicDrawingViews projectOrthographicViews(
  const std::shared_ptr<const Geometry>& geom);

}  // namespace GeometryProjection
