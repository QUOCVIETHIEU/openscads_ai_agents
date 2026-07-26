#!/usr/bin/env python3
"""OpenSCAD MCP server for Cursor (stdio) and ChatGPT (Streamable HTTP).

Exposes OpenSCAD editor/render/skill tools via the desktop AI bridge.

Transports:
  stdio (default): newline-delimited JSON
  --http: Streamable HTTP on /mcp + personal OAuth stub for ChatGPT

Bridge URL resolution order:
  1. OPENSCAD_AI_BRIDGE_URL env var
  2. ai_bridge.json in the OpenSCAD user config dir
"""

from __future__ import annotations

import argparse
import json
import os
import secrets
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

SUPPORTED_PROTOCOL_VERSIONS = (
    "2025-06-18",
    "2025-03-26",
    "2024-11-05",
)
DEFAULT_PROTOCOL_VERSION = "2024-11-05"
SERVER_NAME = "openscad"
SERVER_VERSION = "1.3.3"

_EMPTY_SCHEMA: dict[str, Any] = {
    "type": "object",
    "properties": {},
    "additionalProperties": False,
}


def _tool(
    name: str,
    description: str,
    schema: dict[str, Any] | None = None,
    *,
    read_only: bool = True,
    destructive: bool = False,
) -> dict[str, Any]:
    return {
        "name": name,
        "description": description,
        "inputSchema": schema or _EMPTY_SCHEMA,
        "annotations": {
            "title": name.replace("_", " ").title(),
            "readOnlyHint": read_only,
            "destructiveHint": destructive,
            "idempotentHint": read_only,
            "openWorldHint": False,
        },
    }


TOOLS: list[dict[str, Any]] = [
    _tool("list_tools", "List OpenSCAD MCP tools and the recommended CAD workflow order."),
    _tool("list_skills", "List bundled OpenSCAD CAD skills available via get_skill."),
    _tool(
        "get_skill",
        "Load a bundled CAD workflow skill (markdown). Call this BEFORE designing complex models. "
        "Default skill is openscad-cad.",
        {
            "type": "object",
            "properties": {
                "name": {
                    "type": "string",
                    "description": "Skill folder name (default: openscad-cad).",
                },
                "compact": {
                    "type": "boolean",
                    "description": "If true, load SKILL.compact.md instead of SKILL.md.",
                },
            },
            "additionalProperties": False,
        },
    ),
    _tool("get_cheatsheet", "Short OpenSCAD syntax/modeling cheatsheet for quick reference."),
    _tool("get_editor_code", "Read the current OpenSCAD editor contents before editing an existing design."),
    _tool(
        "set_editor_code",
        "Apply a COMPLETE OpenSCAD script to the editor and run a fast F5 CSG preview. "
        "Always pass the FULL file. Never paste OpenSCAD into chat. "
        "While iterating, verify with get_model_info and get_preview_image. "
        "Do NOT call trigger_render until the design looks correct.",
        {
            "type": "object",
            "properties": {
                "code": {
                    "type": "string",
                    "description": "Full OpenSCAD source code for the editor.",
                }
            },
            "required": ["code"],
            "additionalProperties": False,
        },
        read_only=False,
        destructive=True,
    ),
    _tool(
        "trigger_preview",
        "Run a fast F5 CSG preview of the current editor contents (no full mesh). "
        "Use while iterating / checking. Prefer this over trigger_render mid-design.",
        read_only=False,
        destructive=False,
    ),
    _tool(
        "trigger_render",
        "Run a full F6 mesh render (slow) of the current editor contents. "
        "Call ONCE when the design is final and ready for export/measure. "
        "Alias: trigger_build.",
        read_only=False,
        destructive=False,
    ),
    _tool(
        "trigger_build",
        "Alias for trigger_render: full F6 mesh render. Use only when the design is final.",
        read_only=False,
        destructive=False,
    ),
    _tool(
        "get_model_info",
        "Return last/current preview or render facts: success, empty, errors, warnings, "
        "bounding box (mm), facets (after F6), and log. Use after set_editor_code / preview.",
    ),
    _tool(
        "get_preview_image",
        "Capture the current 3D viewport as a PNG image so you can SEE the model "
        "and judge proportions/parts. Call after a successful render.",
        {
            "type": "object",
            "properties": {
                "max_width": {
                    "type": "integer",
                    "description": "Max image width in pixels (default 1024).",
                }
            },
            "additionalProperties": False,
        },
    ),
    _tool(
        "get_console_log",
        "Read recent OpenSCAD console text (parser/render messages).",
        {
            "type": "object",
            "properties": {
                "max_chars": {
                    "type": "integer",
                    "description": "Max characters to return from the end of the console (default 4000).",
                }
            },
            "additionalProperties": False,
        },
    ),
    _tool(
        "get_camera_info",
        "Read the current 3D camera (projection, distance, translation, rotation).",
    ),
    _tool(
        "pan_view",
        "Pan/drag the 3D view in the screen plane (grab-hand). Units are millimeters. "
        "Positive dx moves the model right; positive dy moves it down. Optional dz moves in depth. "
        "After panning, call get_preview_image to verify framing.",
        {
            "type": "object",
            "properties": {
                "dx": {
                    "type": "number",
                    "description": "Horizontal pan in mm (positive = model moves right on screen).",
                },
                "dy": {
                    "type": "number",
                    "description": "Vertical pan in mm (positive = model moves down on screen).",
                },
                "dz": {
                    "type": "number",
                    "description": "Optional depth pan in mm (positive = away from camera).",
                },
            },
            "additionalProperties": False,
        },
        read_only=False,
    ),
    _tool(
        "zoom_in",
        "Zoom the 3D camera in (closer). Optional steps (default 1); each step is one mouse-wheel notch.",
        {
            "type": "object",
            "properties": {
                "steps": {
                    "type": "integer",
                    "description": "How many zoom steps (default 1, max 20).",
                }
            },
            "additionalProperties": False,
        },
        read_only=False,
    ),
    _tool(
        "zoom_out",
        "Zoom the 3D camera out (farther). Optional steps (default 1); each step is one mouse-wheel notch.",
        {
            "type": "object",
            "properties": {
                "steps": {
                    "type": "integer",
                    "description": "How many zoom steps (default 1, max 20).",
                }
            },
            "additionalProperties": False,
        },
        read_only=False,
    ),
    _tool(
        "zoom_100",
        "Reset zoom distance to the default 100% camera distance (keeps pan/rotation). "
        "Use view_all to frame all geometry, or reset_view for a full camera reset.",
        read_only=False,
    ),
    _tool("view_all", "Frame the camera to show all geometry (View All).", read_only=False),
    _tool("reset_view", "Reset the 3D camera to the default view.", read_only=False),
]

PROMPTS: list[dict[str, Any]] = [
    {
        "name": "create_model",
        "description": "Create a new OpenSCAD model from a user request using the CAD skill workflow.",
        "arguments": [
            {
                "name": "request",
                "description": "What to build (natural language, dimensions optional).",
                "required": True,
            }
        ],
    },
    {
        "name": "improve_model",
        "description": "Improve the current editor model: read code, apply skill, re-render, verify visually.",
        "arguments": [
            {
                "name": "goal",
                "description": "What to improve (looks, proportions, detail, printability).",
                "required": True,
            }
        ],
    },
    {
        "name": "fix_render",
        "description": "Diagnose and fix the current OpenSCAD script using console/model info.",
        "arguments": [
            {
                "name": "notes",
                "description": "Optional extra notes about what looks wrong.",
                "required": False,
            }
        ],
    },
]


def _skills_dir() -> Path | None:
    here = Path(__file__).resolve().parent
    for cand in (here.parent / "skills", here / "skills", Path.cwd() / "skills"):
        if cand.is_dir():
            return cand
    return None


def _load_local_skill(name: str = "openscad-cad", compact: bool = True) -> str:
    root = _skills_dir()
    if not root:
        return ""
    path = root / name / ("SKILL.compact.md" if compact else "SKILL.md")
    try:
        return path.read_text(encoding="utf-8")
    except OSError:
        return ""


def build_prompt_messages(name: str, arguments: dict[str, Any] | None) -> list[dict[str, Any]]:
    args = arguments or {}
    skill = _load_local_skill("openscad-cad", compact=True)
    skill_block = skill if skill else "(Call tool get_skill name=openscad-cad before coding.)"

    if name == "create_model":
        request = str(args.get("request", "")).strip() or "(missing request)"
        text = (
            "You are designing inside OpenSCAD via MCP tools.\n"
            "Follow this skill workflow strictly:\n\n"
            f"{skill_block}\n\n"
            "Tool order:\n"
            "1) get_skill (if needed)\n"
            "2) get_cheatsheet (optional)\n"
            "3) set_editor_code with FULL script (runs F5 preview)\n"
            "4) get_model_info\n"
            "5) view_all then get_preview_image — judge proportions visually\n"
            "6) repair with set_editor_code at most twice if needed\n"
            "7) When the design looks right, call trigger_render ONCE (F6 full mesh)\n"
            "Never paste OpenSCAD into chat. Do not call trigger_render while still iterating.\n\n"
            f"User request:\n{request}"
        )
        return [{"role": "user", "content": {"type": "text", "text": text}}]

    if name == "improve_model":
        goal = str(args.get("goal", "")).strip() or "Improve realism and proportions."
        text = (
            "Improve the current OpenSCAD model using MCP tools.\n"
            f"Skill:\n{skill_block}\n\n"
            "Order: get_editor_code → get_model_info → get_preview_image → "
            "plan changes → set_editor_code (F5) → get_model_info → get_preview_image → "
            "trigger_render once when final.\n"
            f"Improvement goal:\n{goal}"
        )
        return [{"role": "user", "content": {"type": "text", "text": text}}]

    if name == "fix_render":
        notes = str(args.get("notes", "")).strip()
        text = (
            "Fix OpenSCAD render/parser problems using MCP tools.\n"
            "Order: get_console_log → get_editor_code → get_model_info → "
            "set_editor_code (full fixed script, F5 preview) → get_model_info → get_preview_image. "
            "Call trigger_render only after the preview looks correct.\n"
            f"Extra notes: {notes or '(none)'}"
        )
        return [{"role": "user", "content": {"type": "text", "text": text}}]

    raise ValueError(f"Unknown prompt: {name}")


# Personal OAuth stub state (HTTP mode only).
_oauth_codes: dict[str, dict[str, Any]] = {}
_oauth_tokens: dict[str, float] = {}
_oauth_lock = threading.Lock()
_public_base = os.environ.get("OPENSCAD_MCP_PUBLIC_BASE", "").rstrip("/")


def _config_dir_candidates() -> list[Path]:
    home = Path.home()
    candidates = []
    if sys.platform == "darwin":
        candidates.append(home / "Library" / "Application Support" / "OpenSCAD")
    elif sys.platform.startswith("win"):
        appdata = os.environ.get("LOCALAPPDATA")
        if appdata:
            candidates.append(Path(appdata) / "OpenSCAD")
    else:
        xdg = os.environ.get("XDG_CONFIG_HOME")
        if xdg:
            candidates.append(Path(xdg) / "OpenSCAD")
        candidates.append(home / ".config" / "OpenSCAD")
    return candidates


def bridge_url() -> str:
    url = os.environ.get("OPENSCAD_AI_BRIDGE_URL", "").strip().rstrip("/")
    if url:
        return url
    for config_dir in _config_dir_candidates():
        info_path = config_dir / "ai_bridge.json"
        try:
            info = json.loads(info_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        url = str(info.get("url", "")).strip().rstrip("/")
        if url:
            return url
    raise RuntimeError(
        "OpenSCAD MCP bridge URL not found. Enable the MCP bridge in OpenSCAD "
        "(AI Settings -> MCP) and make sure OpenSCAD is running."
    )


def http_json(method: str, path: str, body: dict[str, Any] | None = None) -> dict[str, Any]:
    data = None if body is None else json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        bridge_url() + path,
        data=data,
        method=method,
        headers={"Content-Type": "application/json", "Accept": "application/json"},
    )
    try:
        with urllib.request.urlopen(req, timeout=600) as resp:
            raw = resp.read().decode("utf-8")
            return json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        detail = e.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"Bridge HTTP {e.code}: {detail}") from e
    except urllib.error.URLError as e:
        raise RuntimeError(
            f"Cannot reach OpenSCAD AI bridge at {bridge_url()}: {e}. "
            "Is OpenSCAD running with the AI panel open?"
        ) from e


def tool_result(text: str, is_error: bool = False) -> dict[str, Any]:
    return {
        "content": [{"type": "text", "text": text}],
        "isError": is_error,
    }


def tool_result_from_bridge(raw: str) -> dict[str, Any]:
    """Convert bridge text into MCP content blocks (text and/or image)."""
    text = str(raw or "")
    marker = "IMAGE_PNG_BASE64:"
    idx = text.find(marker)
    if idx < 0:
        return tool_result(text)

    prefix = text[:idx].strip()
    b64 = text[idx + len(marker) :].strip()
    # Strip trailing non-base64 notes if any.
    for sep in ("\n", " "):
        if sep in b64:
            b64 = b64.split(sep, 1)[0].strip()
    content: list[dict[str, Any]] = []
    if prefix:
        content.append({"type": "text", "text": prefix})
    content.append(
        {
            "type": "image",
            "data": b64,
            "mimeType": "image/png",
        }
    )
    content.append(
        {
            "type": "text",
            "text": (
                "Viewport PNG captured. Visually judge proportions, missing parts, "
                "and overall realism before finishing."
            ),
        }
    )
    return {"content": content, "isError": False}


def call_tool(name: str, arguments: dict[str, Any] | None) -> dict[str, Any]:
    args = arguments or {}
    try:
        payload = http_json("POST", "/v1/tools/call", {"name": name, "arguments": args})
        if not payload.get("ok", False):
            return tool_result(str(payload.get("error", "Unknown bridge error")), True)
        return tool_result_from_bridge(str(payload.get("result", "")))
    except Exception as e:  # noqa: BLE001
        return tool_result(str(e), True)


def negotiate_protocol_version(requested: Any) -> str:
    if isinstance(requested, str) and requested in SUPPORTED_PROTOCOL_VERSIONS:
        return requested
    return DEFAULT_PROTOCOL_VERSION


def handle_rpc(msg: dict[str, Any]) -> dict[str, Any] | None:
    """Handle one JSON-RPC message; return response dict or None for notifications."""
    method = msg.get("method")
    msg_id = msg.get("id")

    if method == "initialize":
        params = msg.get("params") or {}
        version = negotiate_protocol_version(params.get("protocolVersion"))
        return {
            "jsonrpc": "2.0",
            "id": msg_id,
            "result": {
                "protocolVersion": version,
                "capabilities": {
                    "tools": {"listChanged": False},
                    "prompts": {"listChanged": False},
                    "resources": {"subscribe": False, "listChanged": False},
                },
                "serverInfo": {
                    "name": SERVER_NAME,
                    "title": "OpenSCAD CAD Tools",
                    "version": SERVER_VERSION,
                },
                "instructions": (
                    "OpenSCAD CAD MCP with 19 tools + openscad-cad skill. "
                    "ALWAYS call get_skill(name=openscad-cad) before complex designs. "
                    "Never paste OpenSCAD into chat — use set_editor_code with the FULL script. "
                    "set_editor_code and trigger_preview run fast F5 CSG preview only. "
                    "Iterate with preview + get_model_info + get_preview_image. "
                    "Call trigger_render (or trigger_build) ONCE when the design is final (F6 mesh). "
                    "Use pan_view / zoom_in / zoom_out / zoom_100 / view_all to inspect, then get_preview_image. "
                    "Available tools: list_tools, list_skills, get_skill, get_cheatsheet, "
                    "get_editor_code, set_editor_code, trigger_preview, trigger_render, trigger_build, "
                    "get_model_info, get_preview_image, get_console_log, get_camera_info, pan_view, "
                    "zoom_in, zoom_out, zoom_100, view_all, reset_view. "
                    "Prompts: create_model, improve_model, fix_render."
                ),
            },
        }

    if method in ("notifications/initialized", "initialized"):
        return None

    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": msg_id, "result": {"tools": TOOLS}}

    if method == "tools/call":
        params = msg.get("params") or {}
        name = params.get("name", "")
        arguments = params.get("arguments") or {}
        return {"jsonrpc": "2.0", "id": msg_id, "result": call_tool(name, arguments)}

    if method == "resources/list":
        resources = []
        root = _skills_dir()
        if root:
            for skill_dir in sorted(p for p in root.iterdir() if p.is_dir()):
                for fname in ("SKILL.md", "SKILL.compact.md"):
                    path = skill_dir / fname
                    if path.is_file():
                        resources.append(
                            {
                                "uri": f"openscad://skill/{skill_dir.name}/{fname}",
                                "name": f"{skill_dir.name}/{fname}",
                                "description": f"Bundled OpenSCAD skill: {skill_dir.name}",
                                "mimeType": "text/markdown",
                            }
                        )
        return {"jsonrpc": "2.0", "id": msg_id, "result": {"resources": resources}}

    if method == "resources/read":
        params = msg.get("params") or {}
        uri = str(params.get("uri", ""))
        prefix = "openscad://skill/"
        if not uri.startswith(prefix):
            return {
                "jsonrpc": "2.0",
                "id": msg_id,
                "error": {"code": -32602, "message": f"Unknown resource URI: {uri}"},
            }
        rel = uri[len(prefix) :]
        root = _skills_dir()
        if not root:
            return {
                "jsonrpc": "2.0",
                "id": msg_id,
                "error": {"code": -32000, "message": "Skills directory not found"},
            }
        path = (root / rel).resolve()
        try:
            path.relative_to(root.resolve())
        except ValueError:
            return {
                "jsonrpc": "2.0",
                "id": msg_id,
                "error": {"code": -32602, "message": "Invalid skill path"},
            }
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as e:
            return {
                "jsonrpc": "2.0",
                "id": msg_id,
                "error": {"code": -32000, "message": str(e)},
            }
        return {
            "jsonrpc": "2.0",
            "id": msg_id,
            "result": {
                "contents": [
                    {
                        "uri": uri,
                        "mimeType": "text/markdown",
                        "text": text,
                    }
                ]
            },
        }

    if method == "prompts/list":
        return {"jsonrpc": "2.0", "id": msg_id, "result": {"prompts": PROMPTS}}

    if method == "prompts/get":
        params = msg.get("params") or {}
        name = str(params.get("name", ""))
        arguments = params.get("arguments") or {}
        try:
            messages = build_prompt_messages(name, arguments)
        except ValueError as e:
            return {
                "jsonrpc": "2.0",
                "id": msg_id,
                "error": {"code": -32602, "message": str(e)},
            }
        return {
            "jsonrpc": "2.0",
            "id": msg_id,
            "result": {
                "description": next(
                    (p["description"] for p in PROMPTS if p["name"] == name),
                    name,
                ),
                "messages": messages,
            },
        }

    if method == "ping":
        return {"jsonrpc": "2.0", "id": msg_id, "result": {}}

    if msg_id is not None:
        return {
            "jsonrpc": "2.0",
            "id": msg_id,
            "error": {"code": -32601, "message": f"Method not found: {method}"},
        }
    return None


# ---------- stdio ----------


def _read_content_length_body(first_line: str) -> dict[str, Any] | None:
    headers: dict[str, str] = {}
    line = first_line
    while True:
        if line in ("\r\n", "\n", ""):
            break
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
        raw = sys.stdin.buffer.readline()
        if not raw:
            return None
        line = raw.decode("utf-8")
    length = int(headers.get("content-length", "0"))
    if length <= 0:
        return None
    body = sys.stdin.buffer.read(length)
    if not body:
        return None
    return json.loads(body.decode("utf-8"))


def read_message() -> dict[str, Any] | None:
    while True:
        raw = sys.stdin.buffer.readline()
        if not raw:
            return None
        line = raw.decode("utf-8")
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.lower().startswith("content-length:"):
            return _read_content_length_body(line)
        return json.loads(stripped)


def write_message(msg: dict[str, Any]) -> None:
    line = json.dumps(msg, ensure_ascii=False, separators=(",", ":"))
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def run_stdio() -> int:
    try:
        bridge_url()
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(str(e) + "\n")
        sys.stderr.flush()

    while True:
        try:
            msg = read_message()
        except Exception as e:  # noqa: BLE001
            sys.stderr.write(f"Failed to parse MCP message: {e}\n")
            sys.stderr.flush()
            continue
        if msg is None:
            break
        resp = handle_rpc(msg)
        if resp is not None:
            write_message(resp)
    return 0


# ---------- HTTP / Streamable HTTP + OAuth stub ----------


def _base_url(handler: BaseHTTPRequestHandler) -> str:
    if _public_base:
        return _public_base
    host = handler.headers.get("Host") or f"{handler.server.server_address[0]}:{handler.server.server_address[1]}"
    # Prefer https when behind cloudflared (X-Forwarded-Proto).
    proto = handler.headers.get("X-Forwarded-Proto", "http").split(",")[0].strip()
    return f"{proto}://{host}"


def _oauth_metadata(base: str) -> dict[str, Any]:
    return {
        "issuer": base,
        "authorization_endpoint": f"{base}/authorize",
        "token_endpoint": f"{base}/token",
        "registration_endpoint": f"{base}/register",
        "response_types_supported": ["code"],
        "grant_types_supported": ["authorization_code", "refresh_token"],
        "code_challenge_methods_supported": ["S256", "plain"],
        "token_endpoint_auth_methods_supported": ["none", "client_secret_post"],
        "scopes_supported": ["mcp:tools"],
    }


def _protected_resource(base: str) -> dict[str, Any]:
    return {
        "resource": f"{base}/mcp",
        "authorization_servers": [base],
        "scopes_supported": ["mcp:tools"],
        "bearer_methods_supported": ["header"],
    }


class McpHttpHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt: str, *args: Any) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))
        sys.stderr.flush()

    def _send(self, status: int, body: bytes, content_type: str, extra: dict[str, str] | None = None) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header(
            "Access-Control-Allow-Headers",
            "Authorization, Content-Type, Accept, Mcp-Session-Id, Last-Event-ID",
        )
        self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
        # Critical for ChatGPT web: allow JS to read the session header.
        self.send_header("Access-Control-Expose-Headers", "Mcp-Session-Id, mcp-session-id")
        if extra:
            for k, v in extra.items():
                self.send_header(k, v)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def _send_json(self, status: int, obj: Any, extra: dict[str, str] | None = None) -> None:
        raw = json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self._send(status, raw, "application/json", extra)

    def _send_rpc(self, status: int, obj: Any, extra: dict[str, str] | None = None) -> None:
        """JSON or SSE depending on Accept (Streamable HTTP)."""
        accept = (self.headers.get("Accept") or "").lower()
        wants_sse = "text/event-stream" in accept
        wants_json = "application/json" in accept or "*/*" in accept or accept.strip() == ""
        # Prefer JSON when both allowed (simpler for most clients including ChatGPT).
        if wants_sse and not wants_json:
            payload = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
            body = f"event: message\ndata: {payload}\n\n".encode("utf-8")
            self._send(status, body, "text/event-stream", extra)
            return
        self._send_json(status, obj, extra)

    def do_OPTIONS(self) -> None:  # noqa: N802
        self._send(204, b"", "text/plain")

    def do_GET(self) -> None:  # noqa: N802
        path = urllib.parse.urlparse(self.path).path
        qs = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        base = _base_url(self)

        if path in ("/health", "/v1/health"):
            self._send_json(200, {"ok": True, "service": "openscad-mcp-http"})
            return

        if path == "/.well-known/oauth-authorization-server":
            self._send_json(200, _oauth_metadata(base))
            return

        if path in (
            "/.well-known/oauth-protected-resource",
            "/.well-known/oauth-protected-resource/mcp",
        ):
            self._send_json(200, _protected_resource(base))
            return

        if path == "/authorize":
            # Personal auto-approve OAuth: issue code and redirect.
            redirect_uri = (qs.get("redirect_uri") or [""])[0]
            state = (qs.get("state") or [""])[0]
            client_id = (qs.get("client_id") or ["chatgpt"])[0]
            code = secrets.token_urlsafe(24)
            with _oauth_lock:
                _oauth_codes[code] = {
                    "client_id": client_id,
                    "redirect_uri": redirect_uri,
                    "exp": time.time() + 300,
                }
            if redirect_uri:
                sep = "&" if "?" in redirect_uri else "?"
                loc = f"{redirect_uri}{sep}code={urllib.parse.quote(code)}"
                if state:
                    loc += f"&state={urllib.parse.quote(state)}"
                self.send_response(302)
                self.send_header("Location", loc)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            self._send(
                200,
                b"<html><body><h1>OpenSCAD MCP authorized</h1><p>You can close this window.</p></body></html>",
                "text/html; charset=utf-8",
            )
            return

        if path == "/mcp":
            # Minimal SSE open for clients that GET /mcp.
            accept = self.headers.get("Accept", "")
            if "text/event-stream" in accept:
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache")
                self.send_header("Connection", "keep-alive")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                try:
                    self.wfile.write(b": ok\n\n")
                    self.wfile.flush()
                except BrokenPipeError:
                    pass
                return
            self._send_json(200, {"ok": True, "transport": "streamable-http", "endpoint": "/mcp"})
            return

        self._send_json(404, {"ok": False, "error": "Not found"})

    def do_POST(self) -> None:  # noqa: N802
        path = urllib.parse.urlparse(self.path).path
        length = int(self.headers.get("Content-Length", "0") or "0")
        raw = self.rfile.read(length) if length > 0 else b""
        base = _base_url(self)

        if path == "/register":
            # Dynamic client registration (RFC 7591) — accept anything for personal use.
            try:
                body = json.loads(raw.decode("utf-8") or "{}")
            except ValueError:
                body = {}
            client_id = secrets.token_urlsafe(16)
            self._send_json(
                201,
                {
                    "client_id": client_id,
                    "client_id_issued_at": int(time.time()),
                    "client_secret_expires_at": 0,
                    "redirect_uris": body.get("redirect_uris") or [],
                    "token_endpoint_auth_method": "none",
                    "grant_types": ["authorization_code", "refresh_token"],
                    "response_types": ["code"],
                },
            )
            return

        if path == "/token":
            ctype = self.headers.get("Content-Type", "")
            if "application/json" in ctype:
                try:
                    form = json.loads(raw.decode("utf-8") or "{}")
                except ValueError:
                    form = {}
            else:
                form = {k: v[0] for k, v in urllib.parse.parse_qs(raw.decode("utf-8")).items()}
            grant = form.get("grant_type", "")
            if grant == "authorization_code":
                code = form.get("code", "")
                with _oauth_lock:
                    entry = _oauth_codes.pop(code, None)
                if not entry or entry.get("exp", 0) < time.time():
                    self._send_json(400, {"error": "invalid_grant"})
                    return
                token = secrets.token_urlsafe(32)
                with _oauth_lock:
                    _oauth_tokens[token] = time.time() + 86400 * 7
                self._send_json(
                    200,
                    {
                        "access_token": token,
                        "token_type": "bearer",
                        "expires_in": 86400 * 7,
                        "scope": "mcp:tools",
                    },
                )
                return
            if grant == "refresh_token":
                token = secrets.token_urlsafe(32)
                with _oauth_lock:
                    _oauth_tokens[token] = time.time() + 86400 * 7
                self._send_json(
                    200,
                    {
                        "access_token": token,
                        "token_type": "bearer",
                        "expires_in": 86400 * 7,
                        "scope": "mcp:tools",
                    },
                )
                return
            self._send_json(400, {"error": "unsupported_grant_type"})
            return

        if path == "/mcp":
            # Auth is optional for personal quick tunnels; accept Bearer if present.
            auth = self.headers.get("Authorization", "")
            if auth.lower().startswith("bearer "):
                token = auth[7:].strip()
                with _oauth_lock:
                    exp = _oauth_tokens.get(token, 0)
                if exp and exp < time.time():
                    self._send_json(401, {"error": "invalid_token"})
                    return

            try:
                msg = json.loads(raw.decode("utf-8") or "{}")
            except ValueError:
                self._send_json(400, {"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "Parse error"}})
                return

            # Batch support
            if isinstance(msg, list):
                out = []
                for item in msg:
                    if isinstance(item, dict):
                        resp = handle_rpc(item)
                        if resp is not None:
                            out.append(resp)
                self._send_rpc(200, out)
                return

            if not isinstance(msg, dict):
                self._send_json(400, {"jsonrpc": "2.0", "id": None, "error": {"code": -32600, "message": "Invalid Request"}})
                return

            resp = handle_rpc(msg)
            if resp is None:
                # Notification — empty 202
                self._send(202, b"", "application/json")
                return
            extra = {}
            if msg.get("method") == "initialize":
                extra["Mcp-Session-Id"] = secrets.token_urlsafe(12)
            self._send_rpc(200, resp, extra)
            return

        self._send_json(404, {"ok": False, "error": f"Not found: {path}", "base": base})


def run_http(host: str, port: int) -> int:
    try:
        bridge_url()
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(str(e) + "\n")
        sys.stderr.flush()

    class ReusableHTTPServer(ThreadingHTTPServer):
        allow_reuse_address = True

    try:
        server = ReusableHTTPServer((host, port), McpHttpHandler)
    except OSError as e:
        sys.stderr.write(f"Failed to bind MCP HTTP on {host}:{port}: {e}\n")
        sys.stderr.flush()
        return 1

    actual_host, actual_port = server.server_address[:2]
    sys.stderr.write(f"OpenSCAD MCP HTTP listening on http://{actual_host}:{actual_port}/mcp\n")
    sys.stderr.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="OpenSCAD MCP server")
    parser.add_argument("--http", action="store_true", help="Run Streamable HTTP (for ChatGPT)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args, _unknown = parser.parse_known_args()
    if args.http:
        return run_http(args.host, args.port)
    return run_stdio()


if __name__ == "__main__":
    raise SystemExit(main())
