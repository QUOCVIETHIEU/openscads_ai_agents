# OpenSCAD CAD workflow (compact)

Adapted from earthtojake/text-to-cad (MIT). OpenSCAD kernel: F5 preview while iterating,
F6 mesh only when final; no STEP.

WORKFLOW every turn:
1. Plan parts: list each part with a size in mm and a position. Complex objects need MANY
   parts (4-8+), never one cube/sphere.
2. Write named parameters (e.g. `body_r=20; wall=3;`) for every important size.
3. Write one small `module` per part, then assemble with translate/rotate and
   union()/difference()/hull().
4. Apply the FULL script with `set_editor_code` (F5 preview). Never paste code in chat.
5. Read the preview result; use get_model_info + get_preview_image. If error/empty, fix
   and re-apply (at most 2 repairs). Keep using F5 / trigger_preview while iterating.
6. When the design looks right, call `trigger_render` (or `trigger_build`) ONCE for F6.

RULES:
- Units mm. Origin centered. XY base, +Z up. `$fn=32` on curved solids.
- Modifiers (translate/rotate/scale/color) wrap the NEXT child; never assign to a var.
- End statements with `;`. No `;` after `module x(){...}` or after `{...}`.
- For a through-hole in a plate of thickness t, cut a cylinder of height t+2 so it
  protrudes both faces (avoid coincident faces).
- Apply fillets/rounding and booleans last; reduce radius if rounding fails.
- Do NOT call trigger_render / trigger_build until preview looks correct.

REPAIR from preview result:
- syntax error -> fix the reported `;`/brace/modifier.
- "no top level geometry"/empty -> ensure a top-level call to your assembly; check a
  difference() did not remove everything.
- wrong scale/invisible -> radius vs diameter, height 0, or 10x units.

CHAT: after the tool succeeds, write 2-4 sentences on parts and sizes, same language as
the user. NEVER reply with only OK / Done / Sure.
