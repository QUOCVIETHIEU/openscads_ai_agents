---
name: openscad-cad
description: Workflow for turning natural-language or drawing requests into complete, renderable, parametric OpenSCAD models. Adapted for OpenSCAD's own kernel (CSG + F6 render), not STEP/build123d.
---

# OpenSCAD CAD workflow

Provenance: this workflow is adapted from the CAD skill in
[earthtojake/text-to-cad](https://github.com/earthtojake/text-to-cad) (MIT License,
Copyright (c) 2026 earthtojake). The original targets STEP/build123d; this version is
rewritten for OpenSCAD's language and F6 render pipeline. No STEP, build123d, CAD Viewer,
or Python tooling is used here.

## Purpose

Turn a request (prose, dimensions, or an attached 2D drawing) into a single complete
OpenSCAD script that renders to a recognizable, positive-volume solid and matches the
stated dimensions. Apply the script with the `set_editor_code` tool; never paste OpenSCAD
into chat.

## Default assumptions

Use these first-pass defaults unless the user specifies otherwise. They are modeling
defaults, not tolerance, manufacturability, or certification claims.

- Units: millimeters.
- Origin: centered on the main part (or on a chosen functional datum); base plane XY;
  +Z is up / the extrusion direction.
- Output: closed, positive-volume solids (a single `union()` root unless the user asks
  for 2D or open geometry).
- `$fn`: 32-64 on curved solids so previews are smooth.
- Small plastic enclosure wall: 2.0-3.0 mm when unspecified.
- Cosmetic fillet radius: 1.0-3.0 mm when safe for local geometry.
- M3 / M4 / M5 normal clearance holes: 3.4 / 4.5 / 5.5 mm diameter.

Ask one focused clarification question only when a missing value makes the model
impossible, fit-critical, safety-critical, or compliance-bound. Otherwise proceed with
explicit assumptions and state them briefly in chat.

## Required workflow

Scale depth to the task. A simple part needs a short brief and a couple of checks; a
detailed part or a drawing-driven part needs the full list.

1. Classify the task: new part, modification of the current editor code, or a drawing to
   reproduce. For modifications, call `get_editor_code` first.
2. Write a short internal brief (see below) before writing any code.
3. Plan parts: list every part with an approximate size and position. Recognizable
   real-world objects need MANY parts (typically 4-8+), never one primitive.
4. Write named parameters for every controlling dimension at the top of the file.
5. Write one small `module` per part, then assemble with `translate` / `rotate` /
   `union()` / `difference()` / `hull()`.
6. Apply the FULL script with `set_editor_code`.
7. After the render result comes back, verify it against the brief's validation targets.
   If the render failed or the geometry is empty/wrong, repair and re-apply (see below).
8. In chat, write 2-5 short sentences: what you built and the key dimensions. No code.

## Internal brief (do not ask the user to fill this in)

Note, in your own reasoning, before coding:

- Model: what is being built; part or modification.
- Units and coordinate convention (origin, base plane, up axis).
- Overall dimensions (or the values you are assuming).
- Functional features: holes, slots, ribs, bosses, pockets, shells, fillets, chamfers.
- Validation targets: expected bounding box, expected part/feature count, must-not-have
  features (no extra bosses, text, decoration unless requested).
- Assumptions: only the meaningful inferred choices.

When inputs conflict, dimensioned values win over image proportions; flag a genuine
conflict instead of silently picking one.

## OpenSCAD modeling patterns

- Choose the construction that makes the spec's dimensions DIRECT parameters. Profile
  shapes: one 2D sketch plus `linear_extrude` / `rotate_extrude`. Block-and-feature
  parts: a base solid plus subtractive `difference()` features.
- Order operations so fragile steps come last and failures localize:
  base solid -> major additions -> subtractive holes/pockets -> `minkowski`/rounding
  last. Rounding and booleans are the most failure-prone steps.
- Overshoot cutting tools. Extend a `difference()` tool ~1 mm past both faces it enters
  and exits; coincident/coplanar faces cause z-fighting and unreliable booleans. For a
  through-hole in a plate of thickness `t`, cut a cylinder of height `t + 2` centered so
  it protrudes both sides.
- Modifiers (`translate`, `rotate`, `scale`, `mirror`, `color`, ...) wrap the NEXT child.
  Never assign a modifier to a variable.
- End assignments and module instantiations with `;`. Do NOT put `;` after
  `module name() { ... }` or after a bare `{ ... }` block.
- Sanity-check proportions before applying: expected bounding box vs the real object,
  wall thickness vs overall size, feature positions vs edges. Order-of-magnitude and
  collision errors render fine but look wrong.

## Common failure modes and repairs

Use the render result that comes back after `set_editor_code`. When it reports an error,
change the smallest responsible section and re-apply. Prefer at most two repair attempts.

- Parser / syntax error (WARNING/ERROR in the result): missing `;`, a `;` after a
  `module`/block, a modifier assigned to a variable, or unbalanced braces. Fix exactly
  the reported construct.
- "no top level geometry" / empty geometry: the root produced nothing. Ensure there is a
  top-level call to your assembly module (or a root `union()`), and that a `difference()`
  did not subtract everything.
- Nothing visible / wrong scale: check a diameter used where a radius was meant (or vice
  versa), an extrusion height of 0, or units off by 10x. Compare the reported bounding
  box against the brief.
- A boolean "did nothing": the tool and target had coincident faces; add the ~1 mm
  overshoot described above.
- Rounding failure or self-intersection: reduce the radius, or apply rounding to a
  narrower set of edges / later in the model.

## Chat reply (mandatory)

Do not paste OpenSCAD source into chat. Apply all code with `set_editor_code`. In chat,
briefly describe the design, its key dimensions, and useful tweaks, in the same language
as the user.
