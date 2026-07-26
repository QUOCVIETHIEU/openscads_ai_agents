#!/usr/bin/env python3
"""Score OpenSCAD text-to-CAD benchmark outputs.

This harness does NOT call any AI provider. You run the agent yourself (once with
the baseline prompt, once with the CAD skill enabled), drop each produced script as
``<benchmark-id>.scad`` into a candidate directory, and this script renders every
candidate with the OpenSCAD CLI and scores it on two layers:

  1. renders  - the script parses and renders to non-empty geometry (exit 0, STL has
                triangles).
  2. facts    - measurable facts match the spec: dimension is 3D, and the axis-aligned
                bounding box matches the expected size within ``bbox_tolerance_mm``.

Usage:
    # score one run
    python3 score.py --openscad /path/to/OpenSCAD --candidates ./out_new

    # A/B compare two runs (baseline vs skill)
    python3 score.py --openscad /path/to/OpenSCAD \
        --candidates ./out_baseline --candidates ./out_skill \
        --label baseline --label skill

The default OpenSCAD path is the local macOS build bundle.
"""

import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

DEFAULT_OPENSCAD = (
    "build/OpenSCAD.app/Contents/MacOS/OpenSCAD"
    if sys.platform == "darwin"
    else "build/openscad"
)


def parse_stl_bbox_and_count(path):
    """Return (triangle_count, (sx, sy, sz)) for an ASCII or binary STL, or (0, None)."""
    data = Path(path).read_bytes()
    if not data:
        return 0, None

    verts = []
    # ASCII STL starts with "solid" and contains "facet"; binary may also start with
    # "solid" so we additionally require the ASCII keywords to be present.
    is_ascii = data[:5] == b"solid" and b"facet" in data[:2048]
    if is_ascii:
        for line in data.decode("ascii", "ignore").splitlines():
            line = line.strip()
            if line.startswith("vertex"):
                parts = line.split()
                if len(parts) == 4:
                    verts.append(tuple(float(p) for p in parts[1:4]))
        tri_count = len(verts) // 3
    else:
        if len(data) < 84:
            return 0, None
        tri_count = struct.unpack_from("<I", data, 80)[0]
        offset = 84
        for _ in range(tri_count):
            if offset + 50 > len(data):
                break
            # 12 floats: normal(3) + 3 vertices(9); vertices start after the normal.
            vals = struct.unpack_from("<12f", data, offset)
            verts.append((vals[3], vals[4], vals[5]))
            verts.append((vals[6], vals[7], vals[8]))
            verts.append((vals[9], vals[10], vals[11]))
            offset += 50

    if not verts:
        return tri_count, None
    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    zs = [v[2] for v in verts]
    size = (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
    return tri_count, size


def render(openscad, scad_path):
    """Render a .scad to STL. Return (ok, tri_count, size, stderr)."""
    with tempfile.TemporaryDirectory() as tmp:
        stl = os.path.join(tmp, "out.stl")
        try:
            proc = subprocess.run(
                [openscad, "-o", stl, str(scad_path)],
                capture_output=True,
                text=True,
                timeout=120,
            )
        except subprocess.TimeoutExpired:
            return False, 0, None, "timeout"
        except FileNotFoundError:
            print(f"error: openscad binary not found at {openscad}", file=sys.stderr)
            sys.exit(2)
        if proc.returncode != 0 or not os.path.exists(stl):
            return False, 0, None, proc.stderr.strip()
        tri, size = parse_stl_bbox_and_count(stl)
        return (tri > 0), tri, size, proc.stderr.strip()


def bbox_ok(expected, actual, tol):
    if actual is None:
        return False
    return all(abs(e - a) <= tol for e, a in zip(expected, actual))


def score_dir(openscad, spec, candidates):
    tol = spec.get("bbox_tolerance_mm", 1.5)
    rows = []
    for bench in spec["benchmarks"]:
        bid = bench["id"]
        exp = bench["expected"]
        scad = Path(candidates) / f"{bid}.scad"
        row = {"id": bid, "attempted": scad.exists(), "renders": False, "facts": False}
        if scad.exists():
            ok, tri, size, err = render(openscad, scad)
            row["renders"] = ok
            row["facets"] = tri
            row["bbox"] = size
            row["err"] = err
            if ok:
                dim_ok = tri >= exp.get("min_facets", 1)
                box_ok = bbox_ok(exp["bbox"], size, tol)
                row["facts"] = bool(dim_ok and box_ok)
        rows.append(row)
    return rows


def summarize(label, rows):
    n = len(rows)
    attempted = sum(1 for r in rows if r["attempted"])
    renders = sum(1 for r in rows if r["renders"])
    facts = sum(1 for r in rows if r["facts"])
    print(f"\n=== {label} ===")
    print(f"{'benchmark':28} {'attempt':>8} {'render':>7} {'facts':>6}  bbox(mm)")
    for r in rows:
        box = r.get("bbox")
        box_s = "-" if not box else f"{box[0]:.1f}x{box[1]:.1f}x{box[2]:.1f}"
        print(
            f"{r['id']:28} {('y' if r['attempted'] else '-'):>8} "
            f"{('y' if r['renders'] else 'n'):>7} "
            f"{('y' if r['facts'] else 'n'):>6}  {box_s}"
        )
    print(f"-- attempted {attempted}/{n}, renders {renders}/{n}, facts {facts}/{n}")
    return {"n": n, "attempted": attempted, "renders": renders, "facts": facts}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--openscad", default=DEFAULT_OPENSCAD, help="Path to the OpenSCAD binary.")
    ap.add_argument("--spec", default=str(Path(__file__).with_name("prompts.json")))
    ap.add_argument("--candidates", action="append", required=True,
                    help="Directory of <benchmark-id>.scad files. Repeat for A/B.")
    ap.add_argument("--label", action="append", default=[],
                    help="Label for each --candidates dir (optional).")
    args = ap.parse_args()

    spec = json.loads(Path(args.spec).read_text())
    results = []
    for i, cand in enumerate(args.candidates):
        label = args.label[i] if i < len(args.label) else cand
        rows = score_dir(args.openscad, spec, cand)
        results.append((label, summarize(label, rows)))

    if len(results) > 1:
        print("\n=== A/B summary ===")
        print(f"{'run':16} {'renders':>9} {'facts':>7}")
        for label, s in results:
            print(f"{label:16} {s['renders']}/{s['n']:<7} {s['facts']}/{s['n']}")


if __name__ == "__main__":
    main()
