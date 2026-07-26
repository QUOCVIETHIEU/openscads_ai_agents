# OpenSCAD text-to-CAD benchmark

A small, offline A/B harness for judging whether the bundled CAD workflow skill
(`skills/openscad-cad/`) actually improves the agent's OpenSCAD output.

The prompts are adapted from the CAD benchmarks in
[earthtojake/text-to-cad](https://github.com/earthtojake/text-to-cad) (MIT) and rewritten
for OpenSCAD's mm / CSG conventions. See `prompts.json`.

## What it measures

Two layers per benchmark:

1. **renders** - the produced `.scad` parses and renders to non-empty geometry
   (OpenSCAD exits 0 and the exported STL has triangles).
2. **facts** - measurable facts match the spec: the result is 3D, has at least the
   expected triangle count, and its axis-aligned bounding box matches the expected size
   within `bbox_tolerance_mm`.

This is deliberately geometric, not semantic: passing the fact checks does not prove the
part "looks right", only that it is the right size and renders. Treat it as a regression
signal, not a certification.

## How to run an A/B comparison

The harness does not call any AI provider. You drive the agent yourself:

1. In AI Settings, use the **baseline** (skill disabled) — e.g. check out the parent
   commit, or temporarily blank the skill — and ask the agent each prompt in
   `prompts.json`. Save each editor result as `out_baseline/<benchmark-id>.scad`.
2. Repeat with the **skill enabled** (this branch) into `out_skill/<benchmark-id>.scad`.
   Keep the same model, temperature, and seed for both runs.
3. Score both:

```bash
python3 benchmarks/openscad/score.py \
  --openscad build/OpenSCAD.app/Contents/MacOS/OpenSCAD \
  --candidates out_baseline --label baseline \
  --candidates out_skill --label skill
```

The output prints a per-benchmark table for each run plus an A/B summary of
`renders` and `facts` counts. Only claim an improvement once the `skill` column
beats `baseline` on the same model.

## Notes

- Run the same model/temperature for both sides; otherwise the comparison is meaningless.
- `bbox_tolerance_mm` in `prompts.json` is intentionally loose (1.5 mm) so that
  reasonable modeling choices (e.g. fillets, chamfers) do not fail the size check.
- A missing `<benchmark-id>.scad` counts as "not attempted" (renders = facts = no).
