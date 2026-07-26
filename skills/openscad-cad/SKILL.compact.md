# OpenSCAD CAD workflow (compact)

Adapted from earthtojake/text-to-cad (MIT). OpenSCAD kernel + F6 render only; no STEP.

WORKFLOW every turn:
1. Plan parts: list each part with a size in mm and a position. Complex objects need MANY
   parts (4-8+), never one cube/sphere.
2. Write named parameters (e.g. `body_r=20; wall=3;`) for every important size.
3. Write one small `module` per part, then assemble with translate/rotate and
   union()/difference()/hull().
4. Apply the FULL script with `set_editor_code`. Never paste code in chat.
5. Read the render result. If it reports an error or empty geometry, fix the smallest
   responsible part and re-apply (at most 2 repair attempts).

RULES:
- Units mm. Origin centered. XY base, +Z up. `$fn=32` on curved solids.
- Modifiers (translate/rotate/scale/color) wrap the NEXT child; never assign to a var.
- End statements with `;`. No `;` after `module x(){...}` or after `{...}`.
- For a through-hole in a plate of thickness t, cut a cylinder of height t+2 so it
  protrudes both faces (avoid coincident faces).
- Apply fillets/rounding and booleans last; reduce radius if rounding fails.

REPAIR from render result:
- syntax error -> fix the reported `;`/brace/modifier.
- "no top level geometry"/empty -> ensure a top-level call to your assembly; check a
  difference() did not remove everything.
- wrong scale/invisible -> radius vs diameter, height 0, or 10x units.

CHAT: after the tool succeeds, write 2-4 sentences on parts and sizes, same language as
the user. NEVER reply with only OK / Done / Sure.
