#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "geometry/GeometryProjection.h"
#include "geometry/Polygon2d.h"
#include "geometry/linalg.h"
#include "io/export.h"
#include "utils/printutils.h"
#include "utils/version_helper.h"

#ifdef ENABLE_CAIRO

#include <cairo-pdf.h>
#include <cairo.h>

namespace {

constexpr inline auto FONT = "Liberation Sans";
constexpr double PTS_IN_MM = 2.834645656693;
constexpr double PAGE_MARGIN = 28.0;
constexpr double CELL_PAD = 14.0;
constexpr double LABEL_HEIGHT = 14.0;
constexpr double STROKE_WIDTH_MM = 0.25;

constexpr double DIM_OFFSET = 10.0;
constexpr double DIM_ROW_STEP = 16.0;
constexpr double DIM_TICK = 3.5;
constexpr double DIM_FONT = 7.0;
constexpr double COORD_MERGE_MM = 0.35;   // merge near-duplicate ordinates
constexpr double EDGE_ALIGN_MM = 0.25;    // axis-alignment tolerance
constexpr double MIN_SEGMENT_MM = 0.8;    // ignore tiny chain segments
constexpr double CIRCLE_FIT_TOL = 0.08;   // relative radius variation
constexpr size_t MAX_CHAIN_COORDS = 12;   // avoid clutter
constexpr size_t MAX_DIAMETER_CALLOUTS = 6;
constexpr size_t MAX_FILLET_CALLOUTS = 6;
constexpr double MIN_FILLET_R_MM = 0.4;
constexpr double FILLET_R_MERGE = 0.15;  // merge similar radii (mm)

constexpr double PAGE_W = 842.0;
constexpr double PAGE_H = 595.0;

double mm_to_points(double mm)
{
  return mm * PTS_IN_MM;
}

cairo_status_t export_pdf_write(void *closure, const unsigned char *data, unsigned int length)
{
  auto *stream = static_cast<std::ostream *>(closure);
  stream->write(reinterpret_cast<const char *>(data), length);
  return !(*stream) ? CAIRO_STATUS_WRITE_ERROR : CAIRO_STATUS_SUCCESS;
}

void draw_text(cairo_t *cr, const char *text, double x, double y, double fontSize)
{
  cairo_select_font_face(cr, FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, fontSize);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, text);
}

std::string format_mm(double mm)
{
  const double absMm = std::fabs(mm);
  char buf[64];
  if (absMm < 100.0) {
    std::snprintf(buf, sizeof(buf), "%.2f", mm);
  } else {
    std::snprintf(buf, sizeof(buf), "%.1f", mm);
  }
  std::string s(buf);
  if (s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  return s;
}

void draw_polygon(cairo_t *cr, const Polygon2d& poly)
{
  for (const auto& o : poly.outlines()) {
    if (o.vertices.empty()) continue;
    const Eigen::Vector2d& p0 = o.vertices[0];
    cairo_move_to(cr, mm_to_points(p0.x()), mm_to_points(-p0.y()));
    for (unsigned int idx = 1; idx < o.vertices.size(); idx++) {
      const Eigen::Vector2d& p = o.vertices[idx];
      cairo_line_to(cr, mm_to_points(p.x()), mm_to_points(-p.y()));
    }
    cairo_line_to(cr, mm_to_points(p0.x()), mm_to_points(-p0.y()));
  }
}

void fill_polygon_hatched(cairo_t *cr, const Polygon2d& poly, double invScale)
{
  // Solid fill first (light), then hatch lines clipped to the fill path
  draw_polygon(cr, poly);
  cairo_set_source_rgba(cr, 0.82, 0.82, 0.82, 1.0);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
  cairo_set_line_width(cr, mm_to_points(STROKE_WIDTH_MM) * invScale);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_stroke_preserve(cr);

  cairo_save(cr);
  cairo_clip(cr);
  const BoundingBox bb = poly.getBoundingBox();
  const double x0 = mm_to_points(bb.min().x()) - 8.0;
  const double x1 = mm_to_points(bb.max().x()) + 8.0;
  const double y0 = mm_to_points(-bb.max().y()) - 8.0;
  const double y1 = mm_to_points(-bb.min().y()) + 8.0;
  cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 0.9);
  cairo_set_line_width(cr, 0.55 * invScale);
  const double step = std::max(2.5, 3.5 * invScale);
  for (double t = x0 - (y1 - y0); t < x1 + (y1 - y0); t += step) {
    cairo_move_to(cr, t, y0);
    cairo_line_to(cr, t + (y1 - y0), y1);
  }
  cairo_stroke(cr);
  cairo_restore(cr);
}

struct ViewCell {
  const Polygon2d *poly{nullptr};
  const char *label{nullptr};
  const char *subtitle{nullptr};  // section depth key / thickness hints
  double x{0};
  double y{0};
  double w{0};
  double h{0};
  bool isSection{false};
};

/*! Infer likely wall/floor thicknesses from axis-aligned section edges. */
std::string sectionThicknessHint(const Polygon2d& poly)
{
  if (poly.isEmpty()) return {};

  const BoundingBox bb = poly.getBoundingBox();
  const double overallX = std::max(1e-6, bb.max().x() - bb.min().x());
  const double overallY = std::max(1e-6, bb.max().y() - bb.min().y());

  std::map<int, int> horizHist;
  std::map<int, int> vertHist;
  for (const auto& outline : poly.outlines()) {
    const auto& verts = outline.vertices;
    if (verts.size() < 2) continue;
    for (size_t i = 0; i < verts.size(); ++i) {
      const Eigen::Vector2d& a = verts[i];
      const Eigen::Vector2d& b = verts[(i + 1) % verts.size()];
      const double dx = std::fabs(b.x() - a.x());
      const double dy = std::fabs(b.y() - a.y());
      if (dy < EDGE_ALIGN_MM && dx >= MIN_SEGMENT_MM) {
        horizHist[static_cast<int>(std::lround(dx * 100.0))]++;
      } else if (dx < EDGE_ALIGN_MM && dy >= MIN_SEGMENT_MM) {
        vertHist[static_cast<int>(std::lround(dy * 100.0))]++;
      }
    }
  }

  auto pickThin = [](const std::map<int, int>& hist, double overallSpan) -> double {
    double best = 0.0;
    int bestCount = 0;
    for (const auto& [key, count] : hist) {
      const double len = key / 100.0;
      // Prefer short material bands (walls/floors), not the outer envelope
      if (len < 0.5 || len > overallSpan * 0.45) continue;
      if (count > bestCount || (count == bestCount && (best <= 0.0 || len < best))) {
        best = len;
        bestCount = count;
      }
    }
    return best;
  };

  // Section A-A: X≈world Z (heights/floors), Y≈world Y (wall thickness)
  // Section B-B: similar — thin vertical ≈ wall, thin horizontal ≈ floor
  const double wallT = pickThin(vertHist, overallY);
  const double floorT = pickThin(horizHist, overallX);

  std::string out = "hatched=SOLID  empty=POCKET  NOT a solid block";
  if (wallT > 0.0 || floorT > 0.0) {
    out += "  |";
    if (wallT > 0.0) out += " wall~" + format_mm(wallT) + "mm";
    if (floorT > 0.0) out += " floor~" + format_mm(floorT) + "mm";
  }
  out += "  → prefer difference(outer, cavity)";
  return out;
}

struct CircleFeature {
  double cx{0};
  double cy{0};
  double diameter{0};
  bool isHole{true};
};

struct FilletFeature {
  double cx{0};      // arc center (mm)
  double cy{0};
  double radius{0};  // mm
  double midX{0};    // midpoint on arc (mm) — label anchor
  double midY{0};
  double angleSpan{0};
  int repeatCount{1};
};

struct ViewFeatures {
  std::vector<double> xs;  // significant X ordinates (mm), sorted
  std::vector<double> ys;  // significant Y ordinates (mm), sorted
  std::vector<CircleFeature> circles;
  std::vector<FilletFeature> fillets;
  double widthMm{0};
  double heightMm{0};
};

double outlineAreaAbs(const Outline2d& o)
{
  // Shoelace
  double a = 0;
  const size_t n = o.vertices.size();
  if (n < 3) return 0;
  for (size_t i = 0; i < n; ++i) {
    const auto& p0 = o.vertices[i];
    const auto& p1 = o.vertices[(i + 1) % n];
    a += p0.x() * p1.y() - p1.x() * p0.y();
  }
  return std::fabs(a) * 0.5;
}

const Outline2d *largestOutline(const Polygon2d& poly, bool preferPositive)
{
  const Outline2d *best = nullptr;
  double bestArea = -1;
  for (const auto& o : poly.outlines()) {
    if (preferPositive && !o.positive) continue;
    const double a = outlineAreaAbs(o);
    if (a > bestArea) {
      bestArea = a;
      best = &o;
    }
  }
  if (!best) {
    for (const auto& o : poly.outlines()) {
      const double a = outlineAreaAbs(o);
      if (a > bestArea) {
        bestArea = a;
        best = &o;
      }
    }
  }
  return best;
}

std::vector<double> mergeCoords(std::vector<double> vals, double tol)
{
  if (vals.empty()) return vals;
  std::sort(vals.begin(), vals.end());
  std::vector<double> out;
  double sum = vals.front();
  int count = 1;
  double clusterStart = vals.front();
  for (size_t i = 1; i < vals.size(); ++i) {
    if (vals[i] - clusterStart <= tol) {
      sum += vals[i];
      ++count;
    } else {
      out.push_back(sum / count);
      clusterStart = vals[i];
      sum = vals[i];
      count = 1;
    }
  }
  out.push_back(sum / count);
  return out;
}

void addLocalExtrema(const Outline2d& outline, std::vector<double>& xs, std::vector<double>& ys)
{
  const auto& v = outline.vertices;
  const size_t n = v.size();
  if (n < 3) return;

  for (size_t i = 0; i < n; ++i) {
    const auto& prev = v[(i + n - 1) % n];
    const auto& cur = v[i];
    const auto& next = v[(i + 1) % n];

    const bool xExt =
      (cur.x() >= prev.x() - 1e-9 && cur.x() >= next.x() - 1e-9) ||
      (cur.x() <= prev.x() + 1e-9 && cur.x() <= next.x() + 1e-9);
    const bool yExt =
      (cur.y() >= prev.y() - 1e-9 && cur.y() >= next.y() - 1e-9) ||
      (cur.y() <= prev.y() + 1e-9 && cur.y() <= next.y() + 1e-9);

    // Strong corner / turning point
    const Vector2d a = (cur - prev);
    const Vector2d b = (next - cur);
    const double la = a.norm();
    const double lb = b.norm();
    if (la < 1e-9 || lb < 1e-9) continue;
    const double cross = a.x() * b.y() - a.y() * b.x();
    const double turn = std::fabs(cross) / (la * lb);
    const bool sharp = turn > 0.15;  // ~sin of turn angle

    if (xExt && (sharp || std::fabs(a.y()) < EDGE_ALIGN_MM || std::fabs(b.y()) < EDGE_ALIGN_MM)) {
      xs.push_back(cur.x());
    }
    if (yExt && (sharp || std::fabs(a.x()) < EDGE_ALIGN_MM || std::fabs(b.x()) < EDGE_ALIGN_MM)) {
      ys.push_back(cur.y());
    }
  }

  // Axis-aligned edge endpoints (feature steps on silhouette)
  for (size_t i = 0; i < n; ++i) {
    const auto& p0 = v[i];
    const auto& p1 = v[(i + 1) % n];
    const double dx = std::fabs(p1.x() - p0.x());
    const double dy = std::fabs(p1.y() - p0.y());
    if (dy <= EDGE_ALIGN_MM && dx >= MIN_SEGMENT_MM) {
      xs.push_back(p0.x());
      xs.push_back(p1.x());
    } else if (dx <= EDGE_ALIGN_MM && dy >= MIN_SEGMENT_MM) {
      ys.push_back(p0.y());
      ys.push_back(p1.y());
    }
  }
}

bool fitCircle(const Outline2d& o, CircleFeature& out)
{
  if (o.vertices.size() < 8) return false;
  Vector2d c(0, 0);
  for (const auto& p : o.vertices) c += p;
  c /= static_cast<double>(o.vertices.size());

  double rSum = 0;
  double rMin = 1e99;
  double rMax = 0;
  for (const auto& p : o.vertices) {
    const double r = (p - c).norm();
    rSum += r;
    rMin = std::min(rMin, r);
    rMax = std::max(rMax, r);
  }
  const double rMean = rSum / static_cast<double>(o.vertices.size());
  if (rMean < 0.15) return false;
  if ((rMax - rMin) / rMean > CIRCLE_FIT_TOL * 2.5) return false;

  // Angular coverage: prefer full loops
  double angSpan = 0;
  for (size_t i = 0; i + 1 < o.vertices.size(); ++i) {
    const Vector2d a = o.vertices[i] - c;
    const Vector2d b = o.vertices[i + 1] - c;
    if (a.norm() < 1e-9 || b.norm() < 1e-9) continue;
    double d = std::atan2(a.x() * b.y() - a.y() * b.x(), a.dot(b));
    angSpan += d;
  }
  if (std::fabs(angSpan) < M_PI * 1.2) return false;  // need mostly closed circle

  out.cx = c.x();
  out.cy = c.y();
  out.diameter = 2.0 * rMean;
  out.isHole = !o.positive;
  return true;
}

/*! Circumcircle of three non-colinear points. */
bool circumcircle3(const Vector2d& a, const Vector2d& b, const Vector2d& c, Vector2d& center,
                   double& radius)
{
  const double d = 2.0 * (a.x() * (b.y() - c.y()) + b.x() * (c.y() - a.y()) + c.x() * (a.y() - b.y()));
  if (std::fabs(d) < 1e-12) return false;
  const double a2 = a.squaredNorm();
  const double b2 = b.squaredNorm();
  const double c2 = c.squaredNorm();
  center.x() = (a2 * (b.y() - c.y()) + b2 * (c.y() - a.y()) + c2 * (a.y() - b.y())) / d;
  center.y() = (a2 * (c.x() - b.x()) + b2 * (a.x() - c.x()) + c2 * (b.x() - a.x())) / d;
  radius = (center - a).norm();
  return radius > 1e-9 && std::isfinite(radius);
}

double signedTurn(const Vector2d& prev, const Vector2d& cur, const Vector2d& next)
{
  const Vector2d a = cur - prev;
  const Vector2d b = next - cur;
  return a.x() * b.y() - a.y() * b.x();
}

double localRadiusEstimate(const Vector2d& prev, const Vector2d& cur, const Vector2d& next)
{
  Vector2d c;
  double r;
  if (!circumcircle3(prev, cur, next, c, r)) return 0;
  return r;
}

std::vector<FilletFeature> detectFillets(const Outline2d& outline, double partSize)
{
  std::vector<FilletFeature> fillets;
  const auto& v = outline.vertices;
  const size_t n = v.size();
  if (n < 6) return fillets;

  struct VertInfo {
    double r{0};
    double turn{0};  // signed area of parallelogram (turn sense)
    bool curved{false};
  };
  std::vector<VertInfo> info(n);
  for (size_t i = 0; i < n; ++i) {
    const auto& prev = v[(i + n - 1) % n];
    const auto& cur = v[i];
    const auto& next = v[(i + 1) % n];
    info[i].turn = signedTurn(prev, cur, next);
    info[i].r = localRadiusEstimate(prev, cur, next);
    const Vector2d a = cur - prev;
    const Vector2d b = next - cur;
    const double la = a.norm();
    const double lb = b.norm();
    if (la < 1e-9 || lb < 1e-9 || info[i].r < MIN_FILLET_R_MM) {
      info[i].curved = false;
      continue;
    }
    // Nearly straight if turn angle small
    const double sinTurn = std::fabs(info[i].turn) / (la * lb);
    info[i].curved = sinTurn > 0.04 && info[i].r < 2.5 * partSize;
  }

  // Walk circularly and extract curved runs with stable radius / turn sign
  size_t start = 0;
  // Begin at a non-curved vertex so runs don't wrap awkwardly
  for (size_t i = 0; i < n; ++i) {
    if (!info[i].curved) {
      start = i;
      break;
    }
  }

  auto flushRun = [&](const std::vector<size_t>& idx) {
    if (idx.size() < 3) return;

    // Fit circle from first / mid / last
    Vector2d center;
    double radius = 0;
    const Vector2d& p0 = v[idx.front()];
    const Vector2d& p1 = v[idx[idx.size() / 2]];
    const Vector2d& p2 = v[idx.back()];
    if (!circumcircle3(p0, p1, p2, center, radius)) return;
    if (radius < MIN_FILLET_R_MM || radius > 1.5 * partSize) return;

    // Validate all points lie near the circle
    double rErr = 0;
    for (size_t id : idx) {
      rErr = std::max(rErr, std::fabs((v[id] - center).norm() - radius));
    }
    if (rErr > std::max(0.12, 0.08 * radius)) return;

    // Angular span
    double angSpan = 0;
    for (size_t k = 0; k + 1 < idx.size(); ++k) {
      const Vector2d a = v[idx[k]] - center;
      const Vector2d b = v[idx[k + 1]] - center;
      if (a.norm() < 1e-9 || b.norm() < 1e-9) continue;
      angSpan += std::atan2(a.x() * b.y() - a.y() * b.x(), a.dot(b));
    }
    const double span = std::fabs(angSpan);
    // Typical corner fillet ~90°; allow rounded ends / soft fillets
    if (span < 25.0 * M_PI / 180.0 || span > 200.0 * M_PI / 180.0) return;

    FilletFeature f;
    f.cx = center.x();
    f.cy = center.y();
    f.radius = radius;
    f.midX = p1.x();
    f.midY = p1.y();
    f.angleSpan = span;
    fillets.push_back(f);
  };

  std::vector<size_t> run;
  double runR = 0;
  int runSign = 0;
  for (size_t k = 0; k < n; ++k) {
    const size_t i = (start + 1 + k) % n;
    const auto& vi = info[i];
    const int sign = (vi.turn > 0) ? 1 : (vi.turn < 0 ? -1 : 0);
    const bool same =
      vi.curved && sign != 0 &&
      (run.empty() ||
       (sign == runSign && std::fabs(vi.r - runR) <= std::max(FILLET_R_MERGE, 0.12 * runR)));

    if (same) {
      if (run.empty()) {
        runSign = sign;
        runR = vi.r;
      } else {
        runR = (runR * static_cast<double>(run.size()) + vi.r) / (run.size() + 1.0);
      }
      run.push_back(i);
    } else {
      flushRun(run);
      run.clear();
      if (vi.curved && sign != 0) {
        runSign = sign;
        runR = vi.r;
        run.push_back(i);
      }
    }
  }
  flushRun(run);

  // Deduplicate near-identical fillets (same corner found twice)
  std::vector<FilletFeature> unique;
  for (const auto& f : fillets) {
    bool dup = false;
    for (const auto& u : unique) {
      if (std::fabs(u.radius - f.radius) < FILLET_R_MERGE &&
          std::hypot(u.cx - f.cx, u.cy - f.cy) < 0.5 * f.radius) {
        dup = true;
        break;
      }
    }
    if (!dup) unique.push_back(f);
  }
  return unique;
}

std::vector<double> thinCoords(std::vector<double> coords, double minSpan, size_t maxCount)
{
  coords = mergeCoords(std::move(coords), COORD_MERGE_MM);
  if (coords.size() <= maxCount) return coords;

  // Keep ends + evenly spaced samples by index among remaining
  std::vector<double> kept;
  kept.push_back(coords.front());
  const size_t inner = maxCount - 2;
  for (size_t k = 1; k <= inner; ++k) {
    const size_t idx = 1 + (k - 1) * (coords.size() - 2) / std::max<size_t>(inner, 1);
    const size_t i = std::min(idx, coords.size() - 2);
    if (coords[i] - kept.back() >= minSpan) kept.push_back(coords[i]);
  }
  if (coords.back() - kept.back() >= minSpan * 0.5) kept.push_back(coords.back());
  else if (kept.size() >= 2) kept.back() = coords.back();
  else kept.push_back(coords.back());
  return mergeCoords(kept, COORD_MERGE_MM * 0.5);
}

ViewFeatures analyzeView(const Polygon2d& poly)
{
  ViewFeatures f;
  const BoundingBox bbox = poly.getBoundingBox();
  f.widthMm = bbox.max().x() - bbox.min().x();
  f.heightMm = bbox.max().y() - bbox.min().y();

  const Outline2d *outer = largestOutline(poly, true);
  std::vector<double> xs = {bbox.min().x(), bbox.max().x()};
  std::vector<double> ys = {bbox.min().y(), bbox.max().y()};
  if (outer) {
    addLocalExtrema(*outer, xs, ys);
  }

  const double minChain = std::max(MIN_SEGMENT_MM, 0.02 * std::max(f.widthMm, f.heightMm));
  f.xs = thinCoords(std::move(xs), minChain, MAX_CHAIN_COORDS);
  f.ys = thinCoords(std::move(ys), minChain, MAX_CHAIN_COORDS);

  // Circles / holes
  std::vector<CircleFeature> circles;
  for (const auto& o : poly.outlines()) {
    if (&o == outer) continue;
    CircleFeature c;
    if (fitCircle(o, c)) {
      circles.push_back(c);
    }
  }

  // Group identical diameters; prefer larger holes for callouts, keep one small typ
  std::sort(circles.begin(), circles.end(),
            [](const CircleFeature& a, const CircleFeature& b) { return a.diameter > b.diameter; });

  std::vector<CircleFeature> selected;
  std::map<int, int> diamCount;  // key = diameter*100
  for (const auto& c : circles) {
    const int key = static_cast<int>(std::lround(c.diameter * 100.0));
    diamCount[key]++;
  }

  std::map<int, bool> diamShown;
  for (const auto& c : circles) {
    if (selected.size() >= MAX_DIAMETER_CALLOUTS) break;
    const int key = static_cast<int>(std::lround(c.diameter * 100.0));
    // Always show largest few unique sizes; for repeats show once
    if (diamShown[key]) continue;
    // Skip very tiny relative to part unless few circles
    if (c.diameter < 0.015 * std::max(f.widthMm, f.heightMm) && circles.size() > 8 &&
        c.diameter < 1.5) {
      continue;
    }
    selected.push_back(c);
    diamShown[key] = true;
  }

  // If many small identical holes were skipped, still show one typ diameter
  if (!circles.empty()) {
    const auto& smallestCommon = circles.back();
    const int key = static_cast<int>(std::lround(smallestCommon.diameter * 100.0));
    if (!diamShown[key] && diamCount[key] >= 3) {
      selected.push_back(smallestCommon);
    }
  }

  f.circles = std::move(selected);

  // Also use large hole centers as ordinate hints (tabs with holes)
  for (const auto& c : circles) {
    if (c.diameter >= 0.08 * std::max(f.widthMm, f.heightMm)) {
      f.xs.push_back(c.cx);
      f.ys.push_back(c.cy);
    }
  }
  f.xs = thinCoords(std::move(f.xs), minChain, MAX_CHAIN_COORDS);
  f.ys = thinCoords(std::move(f.ys), minChain, MAX_CHAIN_COORDS);

  // Rounded corners / fillet arcs on outer silhouette
  if (outer) {
    auto fillets = detectFillets(*outer, std::max(f.widthMm, f.heightMm));
    std::sort(fillets.begin(), fillets.end(),
              [](const FilletFeature& a, const FilletFeature& b) { return a.radius > b.radius; });

    std::map<int, int> rCount;
    for (const auto& fil : fillets) {
      rCount[static_cast<int>(std::lround(fil.radius * 100.0))]++;
    }

    std::map<int, bool> rShown;
    for (const auto& fil : fillets) {
      if (f.fillets.size() >= MAX_FILLET_CALLOUTS) break;
      const int key = static_cast<int>(std::lround(fil.radius * 100.0));
      if (rShown[key]) continue;
      FilletFeature shown = fil;
      shown.repeatCount = std::max(1, rCount[key]);
      f.fillets.push_back(shown);
      rShown[key] = true;
    }
  }

  return f;
}

void stroke_dim_style(cairo_t *cr)
{
  cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.95);
  cairo_set_line_width(cr, 0.65);
}

void draw_label_gap(cairo_t *cr, const std::string& label, double x, double y, bool vertical)
{
  cairo_select_font_face(cr, FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, DIM_FONT);
  cairo_text_extents_t extents;
  cairo_text_extents(cr, label.c_str(), &extents);

  if (!vertical) {
    const double tx = x - extents.width / 2.0 - extents.x_bearing;
    const double ty = y;
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_rectangle(cr, tx - 1.5, ty + extents.y_bearing - 1.0, extents.width + 3.0,
                    extents.height + 2.0);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.95);
    cairo_move_to(cr, tx, ty);
    cairo_show_text(cr, label.c_str());
  } else {
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_rotate(cr, -M_PI / 2.0);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_rectangle(cr, -extents.width / 2.0 - 1.5, extents.y_bearing - 1.0, extents.width + 3.0,
                    extents.height + 2.0);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.95);
    cairo_move_to(cr, -extents.width / 2.0 - extents.x_bearing, 0);
    cairo_show_text(cr, label.c_str());
    cairo_restore(cr);
  }
}

void draw_h_segment(cairo_t *cr, double x0, double x1, double y, double yExtStart, double valueMm)
{
  const double left = std::min(x0, x1);
  const double right = std::max(x0, x1);
  if (right - left < 2.0) return;

  stroke_dim_style(cr);
  cairo_move_to(cr, left, yExtStart);
  cairo_line_to(cr, left, y + DIM_TICK * 0.6);
  cairo_move_to(cr, right, yExtStart);
  cairo_line_to(cr, right, y + DIM_TICK * 0.6);
  cairo_move_to(cr, left, y);
  cairo_line_to(cr, right, y);
  cairo_move_to(cr, left - DIM_TICK * 0.35, y - DIM_TICK);
  cairo_line_to(cr, left + DIM_TICK * 0.35, y + DIM_TICK);
  cairo_move_to(cr, right - DIM_TICK * 0.35, y - DIM_TICK);
  cairo_line_to(cr, right + DIM_TICK * 0.35, y + DIM_TICK);
  cairo_stroke(cr);
  draw_label_gap(cr, format_mm(valueMm), (left + right) / 2.0, y - 2.5, false);
}

void draw_v_segment(cairo_t *cr, double y0, double y1, double x, double xExtStart, double valueMm)
{
  const double top = std::min(y0, y1);
  const double bottom = std::max(y0, y1);
  if (bottom - top < 2.0) return;

  stroke_dim_style(cr);
  cairo_move_to(cr, xExtStart, top);
  cairo_line_to(cr, x + DIM_TICK * 0.6, top);
  cairo_move_to(cr, xExtStart, bottom);
  cairo_line_to(cr, x + DIM_TICK * 0.6, bottom);
  cairo_move_to(cr, x, top);
  cairo_line_to(cr, x, bottom);
  cairo_move_to(cr, x - DIM_TICK, top - DIM_TICK * 0.35);
  cairo_line_to(cr, x + DIM_TICK, top + DIM_TICK * 0.35);
  cairo_move_to(cr, x - DIM_TICK, bottom - DIM_TICK * 0.35);
  cairo_line_to(cr, x + DIM_TICK, bottom + DIM_TICK * 0.35);
  cairo_stroke(cr);
  draw_label_gap(cr, format_mm(valueMm), x + 3.5, (top + bottom) / 2.0, true);
}

void draw_diameter(cairo_t *cr, double pageCx, double pageCy, double pageRadius, double diameterMm,
                   int repeatCount)
{
  stroke_dim_style(cr);
  // Center mark
  const double m = std::min(4.0, pageRadius * 0.35);
  cairo_move_to(cr, pageCx - m, pageCy);
  cairo_line_to(cr, pageCx + m, pageCy);
  cairo_move_to(cr, pageCx, pageCy - m);
  cairo_line_to(cr, pageCx, pageCy + m);
  cairo_stroke(cr);

  // Diameter line at 30 degrees
  const double ang = -M_PI / 6.0;
  const double dx = std::cos(ang) * pageRadius;
  const double dy = std::sin(ang) * pageRadius;
  cairo_move_to(cr, pageCx - dx, pageCy - dy);
  cairo_line_to(cr, pageCx + dx, pageCy + dy);
  cairo_stroke(cr);

  std::string label = std::string("\u00D8") + format_mm(diameterMm);  // Ø
  if (repeatCount > 1) {
    label += " (" + std::to_string(repeatCount) + "x)";
  }
  draw_label_gap(cr, label, pageCx + dx + 6.0, pageCy - dy - 2.0, false);
}

struct GeomMap {
  double cx{0};
  double cy{0};
  double centerX{0};  // mm_to_points mid X
  double midYmm{0};
  double scale{1};

  double x(double mmX) const { return cx + (mm_to_points(mmX) - centerX) * scale; }
  double y(double mmY) const { return cy - mm_to_points(mmY - midYmm) * scale; }
};

void draw_fillet_radius(cairo_t *cr, const GeomMap& map, const FilletFeature& f, int repeatCount)
{
  const double px = map.x(f.midX);
  const double py = map.y(f.midY);
  const double cx = map.x(f.cx);
  const double cy = map.y(f.cy);

  // Outward direction from arc center through mid-arc point
  double dx = px - cx;
  double dy = py - cy;
  const double len = std::hypot(dx, dy);
  if (len < 1e-6) {
    dx = 1;
    dy = -1;
  } else {
    dx /= len;
    dy /= len;
  }

  const double leader = 10.0;
  const double lx = px + dx * leader;
  const double ly = py + dy * leader;

  stroke_dim_style(cr);
  // Small radial tick from mid toward outside
  cairo_move_to(cr, px + dx * 1.5, py + dy * 1.5);
  cairo_line_to(cr, lx, ly);
  cairo_stroke(cr);

  // Light arc hint around the fillet
  const double pageR = mm_to_points(f.radius) * map.scale;
  if (pageR > 3.0 && pageR < 80.0) {
    const double aMid = std::atan2(py - cy, px - cx);
    const double half = std::min(0.45, 0.5 * f.angleSpan);
    cairo_set_line_width(cr, 0.55);
    cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 0.55);
    cairo_arc(cr, cx, cy, pageR, aMid - half, aMid + half);
    cairo_stroke(cr);
    stroke_dim_style(cr);
  }

  std::string label = std::string("R") + format_mm(f.radius);
  if (repeatCount > 1) {
    label += " (" + std::to_string(repeatCount) + "x)";
  }
  draw_label_gap(cr, label, lx + dx * 4.0, ly + dy * 4.0, false);
}

void draw_view_dimensions(cairo_t *cr, const ViewFeatures& feat, const GeomMap& map,
                          double geomLeft, double geomRight, double geomTop, double geomBottom,
                          const Polygon2d& poly)
{
  // Count circle diameters for (Nx) labels
  std::map<int, int> diamCount;
  for (const auto& o : poly.outlines()) {
    CircleFeature c;
    if (fitCircle(o, c)) {
      diamCount[static_cast<int>(std::lround(c.diameter * 100.0))]++;
    }
  }

  // Horizontal chain (inner row) + overall (outer row if needed)
  const bool multiX = feat.xs.size() > 2;
  const double yChain = geomBottom + DIM_OFFSET;
  const double yOverall = multiX ? yChain + DIM_ROW_STEP : yChain;

  if (multiX) {
    for (size_t i = 0; i + 1 < feat.xs.size(); ++i) {
      const double a = feat.xs[i];
      const double b = feat.xs[i + 1];
      const double seg = b - a;
      if (seg < MIN_SEGMENT_MM) continue;
      // Extension from nearest geometry bottom at that x — use geomBottom
      draw_h_segment(cr, map.x(a), map.x(b), yChain, geomBottom + 1.0, seg);
    }
  }
  if (feat.widthMm > 1e-6) {
    draw_h_segment(cr, geomLeft, geomRight, yOverall, multiX ? yChain + 2.0 : geomBottom + 1.0,
                   feat.widthMm);
  }

  // Vertical chain + overall
  const bool multiY = feat.ys.size() > 2;
  const double xChain = geomRight + DIM_OFFSET;
  const double xOverall = multiY ? xChain + DIM_ROW_STEP : xChain;

  if (multiY) {
    // feat.ys sorted ascending in model Y; page Y is inverted
    for (size_t i = 0; i + 1 < feat.ys.size(); ++i) {
      const double a = feat.ys[i];
      const double b = feat.ys[i + 1];
      const double seg = b - a;
      if (seg < MIN_SEGMENT_MM) continue;
      draw_v_segment(cr, map.y(b), map.y(a), xChain, geomRight + 1.0, seg);
    }
  }
  if (feat.heightMm > 1e-6) {
    draw_v_segment(cr, geomTop, geomBottom, xOverall, multiY ? xChain + 2.0 : geomRight + 1.0,
                   feat.heightMm);
  }

  // Diameters
  for (const auto& c : feat.circles) {
    const int key = static_cast<int>(std::lround(c.diameter * 100.0));
    const int n = diamCount[key];
    const double pr = 0.5 * mm_to_points(c.diameter) * map.scale;
    draw_diameter(cr, map.x(c.cx), map.y(c.cy), std::max(pr, 2.0), c.diameter, n);
  }

  // Corner / edge fillets
  for (const auto& fil : feat.fillets) {
    draw_fillet_radius(cr, map, fil, fil.repeatCount);
  }
}
double dimMarginBottom(const ViewFeatures& f)
{
  return DIM_OFFSET + (f.xs.size() > 2 ? 2.0 : 1.0) * DIM_ROW_STEP + 6.0;
}

double dimMarginRight(const ViewFeatures& f)
{
  return DIM_OFFSET + (f.ys.size() > 2 ? 2.0 : 1.0) * DIM_ROW_STEP + 10.0;
}

void draw_view_in_cell(cairo_t *cr, const ViewCell& cell, double uniformScale)
{
  if (!cell.poly || !cell.label) return;

  const double labelBand =
    LABEL_HEIGHT + (cell.subtitle && cell.subtitle[0] != '\0' ? 10.0 : 0.0);

  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.85);
  draw_text(cr, cell.label, cell.x + 2.0, cell.y + LABEL_HEIGHT - 2.0, 10.0);
  if (cell.subtitle && cell.subtitle[0] != '\0') {
    cairo_set_source_rgba(cr, 0.55, 0.0, 0.0, 0.95);
    draw_text(cr, cell.subtitle, cell.x + 2.0, cell.y + labelBand - 2.0, 6.5);
  }

  const double contentTop = cell.y + labelBand;
  const double contentH = cell.h - labelBand;
  if (contentH <= 1.0 || cell.w <= 1.0) return;

  if (cell.poly->isEmpty()) {
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.4);
    draw_text(cr, "(empty)", cell.x + cell.w / 2.0 - 20.0, contentTop + contentH / 2.0, 9.0);
    return;
  }

  const ViewFeatures feat = analyzeView(*cell.poly);
  const double marginB = dimMarginBottom(feat);
  const double marginR = dimMarginRight(feat);

  const BoundingBox bbox = cell.poly->getBoundingBox();
  const double spanX = mm_to_points(feat.widthMm);
  const double spanY = mm_to_points(feat.heightMm);
  const double centerX = mm_to_points(bbox.min().x()) + spanX / 2.0;
  const double midYmm = 0.5 * (bbox.min().y() + bbox.max().y());

  const double drawLeft = cell.x + 4.0;
  const double drawTop = contentTop + 4.0;
  const double drawW = cell.w - marginR - 8.0;
  const double drawH = contentH - marginB - 8.0;
  const double cx = drawLeft + drawW / 2.0;
  const double cy = drawTop + drawH / 2.0;

  cairo_save(cr);
  cairo_rectangle(cr, cell.x, contentTop, cell.w, contentH);
  cairo_clip(cr);

  cairo_translate(cr, cx, cy);
  cairo_scale(cr, uniformScale, uniformScale);
  cairo_translate(cr, -centerX, mm_to_points(midYmm));

  if (cell.isSection) {
    fill_polygon_hatched(cr, *cell.poly, 1.0 / uniformScale);
  } else {
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    draw_polygon(cr, *cell.poly);
    cairo_set_line_width(cr, mm_to_points(STROKE_WIDTH_MM) / uniformScale);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke(cr);
  }
  cairo_restore(cr);

  const double geomLeft = cx - (spanX * uniformScale) / 2.0;
  const double geomRight = cx + (spanX * uniformScale) / 2.0;
  const double geomTop = cy - (spanY * uniformScale) / 2.0;
  const double geomBottom = cy + (spanY * uniformScale) / 2.0;

  GeomMap map{cx, cy, centerX, midYmm, uniformScale};
  draw_view_dimensions(cr, feat, map, geomLeft, geomRight, geomTop, geomBottom, *cell.poly);

  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.2);
  cairo_set_line_width(cr, 0.5);
  cairo_rectangle(cr, cell.x, cell.y, cell.w, cell.h);
  cairo_stroke(cr);
}

double viewSpanPts(const Polygon2d *poly, bool axisX)
{
  if (!poly || poly->isEmpty()) return 1.0;
  const BoundingBox bbox = poly->getBoundingBox();
  const double span =
    axisX ? (bbox.max().x() - bbox.min().x()) : (bbox.max().y() - bbox.min().y());
  return std::max(mm_to_points(span), 1.0);
}

}  // namespace

void export_drawing_pdf(const OrthographicDrawingViews& views, std::ostream& output,
                        const ExportInfo& exportInfo)
{
  cairo_surface_t *surface =
    cairo_pdf_surface_create_for_stream(export_pdf_write, &output, PAGE_W, PAGE_H);
  if (cairo_surface_status(surface) == cairo_status_t::CAIRO_STATUS_NULL_POINTER) {
    cairo_surface_destroy(surface);
    return;
  }

#if CAIRO_VERSION >= CAIRO_VERSION_ENCODE(1, 16, 0)
  cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_TITLE,
                                 exportInfo.title.empty() ? "OpenSCAD 2D Drawing"
                                                          : exportInfo.title.c_str());
  cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_CREATOR, EXPORT_CREATOR);
  cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_CREATE_DATE,
                                 get_current_iso8601_date_time_utc().c_str());
#endif

  cairo_t *cr = cairo_create(surface);

  const double usableW = PAGE_W - 2.0 * PAGE_MARGIN;
  const double usableH = PAGE_H - 2.0 * PAGE_MARGIN;
  // Footer band for overall size + machine-readable reconstruction hints
  const double footerH = 48.0;
  const double gridH = usableH - footerH;
  const double colW = (usableW - CELL_PAD) / 2.0;
  const double rowH = (gridH - CELL_PAD) / 2.0;
  const bool multiView = views.front && views.side;

  // Keep subtitle strings alive for the draw pass
  std::string subtitleAA;
  std::string subtitleBB;
  if (views.sectionAA && !views.sectionAA->isEmpty()) {
    subtitleAA = sectionThicknessHint(*views.sectionAA);
  }
  if (views.sectionBB && !views.sectionBB->isEmpty()) {
    subtitleBB = sectionThicknessHint(*views.sectionBB);
  }

  std::vector<ViewCell> cells;
  if (multiView) {
    // Prefer sections over a third silhouette so pocket/wall depth is visible for reverse-3D
    const bool hasAA = views.sectionAA && !views.sectionAA->isEmpty();
    const bool hasBB = views.sectionBB && !views.sectionBB->isEmpty();
    cells.push_back({views.top.get(), "Top (outer silhouette only)", nullptr, PAGE_MARGIN,
                     PAGE_MARGIN, colW, rowH, false});
    if (hasAA) {
      cells.push_back({views.sectionAA.get(), "SECTION A-A (rim cut ||YZ · hatched=SOLID)",
                       subtitleAA.c_str(), PAGE_MARGIN + colW + CELL_PAD, PAGE_MARGIN, colW, rowH,
                       true});
    } else {
      cells.push_back({views.side.get(), "Side (outer silhouette only)", nullptr,
                       PAGE_MARGIN + colW + CELL_PAD, PAGE_MARGIN, colW, rowH, false});
    }
    cells.push_back({views.front.get(), "Front (outer silhouette only)", nullptr, PAGE_MARGIN,
                     PAGE_MARGIN + rowH + CELL_PAD, colW, rowH, false});
    if (hasBB) {
      cells.push_back({views.sectionBB.get(), "SECTION B-B (rim cut ||XZ · hatched=SOLID)",
                       subtitleBB.c_str(), PAGE_MARGIN + colW + CELL_PAD,
                       PAGE_MARGIN + rowH + CELL_PAD, colW, rowH, true});
    } else {
      cells.push_back({views.side.get(), "Side (outer silhouette only)", nullptr,
                       PAGE_MARGIN + colW + CELL_PAD, PAGE_MARGIN + rowH + CELL_PAD, colW, rowH,
                       false});
    }
  } else {
    cells.push_back(
      {views.top.get(), "View", nullptr, PAGE_MARGIN, PAGE_MARGIN, usableW, usableH - 24.0, false});
  }

  // Pre-analyze for margins
  std::vector<ViewFeatures> feats;
  feats.reserve(cells.size());
  for (const auto& cell : cells) {
    if (cell.poly && !cell.poly->isEmpty()) feats.push_back(analyzeView(*cell.poly));
    else feats.emplace_back();
  }

  double uniformScale = 1e9;
  for (size_t i = 0; i < cells.size(); ++i) {
    const auto& cell = cells[i];
    if (!cell.poly || cell.poly->isEmpty()) continue;
    const double labelBand =
      LABEL_HEIGHT + (cell.subtitle && cell.subtitle[0] != '\0' ? 10.0 : 0.0);
    const double contentW = cell.w - dimMarginRight(feats[i]) - 8.0;
    const double contentH = cell.h - labelBand - dimMarginBottom(feats[i]) - 8.0;
    if (contentW <= 1.0 || contentH <= 1.0) continue;
    uniformScale =
      std::min(uniformScale, std::min(contentW / viewSpanPts(cell.poly, true),
                                      contentH / viewSpanPts(cell.poly, false)));
  }
  if (!std::isfinite(uniformScale) || uniformScale <= 0.0 || uniformScale > 1e8) {
    uniformScale = 1.0;
  }

  for (const auto& cell : cells) {
    draw_view_in_cell(cr, cell, uniformScale);
  }

  // Footer: overall size + machine-readable reconstruction hints (for AI / reverse-3D)
  cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.75);
  const double footerY = PAGE_H - PAGE_MARGIN - 8.0;
  std::string overallLine = "Units: mm";
  if (views.top && !views.top->isEmpty() && views.front && !views.front->isEmpty()) {
    const BoundingBox topBb = views.top->getBoundingBox();
    const BoundingBox frontBb = views.front->getBoundingBox();
    const double sizeX = topBb.max().x() - topBb.min().x();
    const double sizeY = topBb.max().y() - topBb.min().y();
    const double sizeZ = frontBb.max().y() - frontBb.min().y();
    overallLine = "Overall: " + format_mm(sizeX) + " \u00D7 " + format_mm(sizeY) + " \u00D7 " +
                  format_mm(sizeZ) + " mm (X\u00D7Y\u00D7Z)  |  Units: mm";
  }
  if (!exportInfo.sourceFilePath.empty()) {
    overallLine += "  |  " + exportInfo.sourceFilePath;
  }
  draw_text(cr, overallLine.c_str(), PAGE_MARGIN, footerY - 22.0, 8.0);
  draw_text(cr,
            "[RECONSTRUCT] CRITICAL: Top/Front = OUTER SILHOUETTE ONLY — do NOT extrude into a solid "
            "block.",
            PAGE_MARGIN, footerY - 11.0, 6.5);
  draw_text(cr,
            "SECTION A-A/B-B = rim material cuts; hatched=SOLID; empty in section=POCKET/HOLE; "
            "read wall/floor from sections; prefer difference(outer,cavity)+holes; units=mm",
            PAGE_MARGIN, footerY, 6.5);

  cairo_show_page(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
}

#else

void export_drawing_pdf(const OrthographicDrawingViews&, std::ostream&, const ExportInfo&)
{
  LOG(message_group::Error, "Export to PDF format was not enabled when building the application.");
}

#endif
