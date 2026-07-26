#!/usr/bin/env python3
"""A/B: generate OpenSCAD for each benchmark with / without the CAD skill.

Uses the user's OpenSCAD AI Settings (Gemini by default). Never prints the API key.
"""

from __future__ import annotations

import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = Path(__file__).with_name("prompts.json")
SKILL = ROOT / "skills" / "openscad-cad" / "SKILL.md"
OUT_BASE = ROOT / "benchmarks" / "openscad" / "out_baseline"
OUT_SKILL = ROOT / "benchmarks" / "openscad" / "out_skill"

BASELINE_SYSTEM = """You are Cad Agent — an OpenSCAD specialist that ONLY builds 3D models.
Turn the user's request into a complete, renderable OpenSCAD script.
Prefer named variables, modules, and millimeters.
Reply with ONLY the OpenSCAD source inside a single ```scad fenced block.
No explanations, no tool calls, no prose outside the fence.
"""

USER_WRAPPER = (
    "Write a complete OpenSCAD script for this part. "
    "Return ONLY a ```scad code block with the full script.\n\n{prompt}"
)


def load_profile():
    path = Path.home() / "Library/Application Support/OpenSCAD/ai_settings.json"
    j = json.loads(path.read_text())
    ap = j["activeProfile"]
    prof = j["profiles"][ap]
    return {
        "name": ap,
        "endpoint": prof["endpoint"].rstrip("/"),
        "api_key": prof.get("apiKey") or "",
        "model": (prof.get("params") or {}).get("model") or "gemini-2.0-flash",
    }


def extract_scad(text: str) -> str:
    if not text:
        return ""
    m = re.search(r"```(?:scad|openscad)?\s*\n(.*?)```", text, re.S | re.I)
    if m:
        return m.group(1).strip() + "\n"
    # Fallback: whole reply if it looks like OpenSCAD.
    if "module " in text or "cube(" in text or "cylinder(" in text or "difference(" in text:
        return text.strip() + "\n"
    return ""


def chat(profile, system: str, user: str, temperature: float = 0.2) -> str:
    url = profile["endpoint"]
    if not url.endswith("/chat/completions"):
        url = url + "/chat/completions"
    payload = {
        "model": profile["model"],
        "temperature": temperature,
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user},
        ],
    }
    body = json.dumps(payload).encode()
    req = urllib.request.Request(
        url,
        data=body,
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {profile['api_key']}",
            "User-Agent": "openscad-cad-skill-ab/1.0",
        },
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        data = json.load(resp)
    choice = data["choices"][0]["message"]
    content = choice.get("content") or ""
    if isinstance(content, list):
        # Some providers return content parts.
        content = "".join(
            part.get("text", "") if isinstance(part, dict) else str(part) for part in content
        )
    return content


def run_arm(profile, system: str, out_dir: Path, label: str):
    out_dir.mkdir(parents=True, exist_ok=True)
    spec = json.loads(SPEC.read_text())
    print(f"\n=== Generating {label} → {out_dir} ===")
    print(f"model={profile['model']} system_chars={len(system)}")
    for i, bench in enumerate(spec["benchmarks"]):
        bid = bench["id"]
        user = USER_WRAPPER.format(prompt=bench["prompt"])
        print(f"[{i+1}/{len(spec['benchmarks'])}] {bid} ...", flush=True)
        try:
            text = chat(profile, system, user)
            code = extract_scad(text)
            if not code:
                code = f"// FAILED TO EXTRACT CODE for {bid}\n// raw starts:\n// {text[:200]!r}\n"
                print("  WARN: no scad extracted")
            else:
                print(f"  ok: {len(code)} chars")
            (out_dir / f"{bid}.scad").write_text(code)
        except urllib.error.HTTPError as e:
            err = e.read().decode("utf-8", "ignore")[:500]
            print(f"  HTTP {e.code}: {err}")
            (out_dir / f"{bid}.scad").write_text(f"// HTTP {e.code}\n")
        except Exception as e:
            print(f"  ERROR: {e}")
            (out_dir / f"{bid}.scad").write_text(f"// ERROR: {e}\n")
        time.sleep(0.8)  # be polite to the API


def main():
    profile = load_profile()
    if not profile["api_key"]:
        print("No API key in OpenSCAD AI Settings.", file=sys.stderr)
        sys.exit(1)
    skill = SKILL.read_text()
    # Strip YAML frontmatter for the system prompt.
    if skill.startswith("---"):
        parts = skill.split("---", 2)
        if len(parts) >= 3:
            skill = parts[2].strip()
    skill_system = BASELINE_SYSTEM + "\n\n" + skill + "\n\nRemember: reply with ONLY a ```scad fence."

    arm = os.environ.get("AB_ARM", "both")  # baseline | skill | both
    if arm in ("baseline", "both"):
        run_arm(profile, BASELINE_SYSTEM, OUT_BASE, "baseline")
    if arm in ("skill", "both"):
        run_arm(profile, skill_system, OUT_SKILL, "skill")
    print("\nDone. Score with:")
    print(
        "python3 benchmarks/openscad/score.py "
        "--openscad build/OpenSCAD.app/Contents/MacOS/OpenSCAD "
        "--candidates benchmarks/openscad/out_baseline --label baseline "
        "--candidates benchmarks/openscad/out_skill --label skill"
    )


if __name__ == "__main__":
    main()
