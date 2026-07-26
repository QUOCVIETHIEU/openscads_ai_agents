#pragma once

#include "core/AIClient.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

/*! True when the profile endpoint selects the Cursor Agent CLI backend. */
bool isCursorAgentEndpoint(const std::string& endpoint);

/*! Resolve the cursor-agent / agent / `cursor agent` binary. Empty if not found. */
std::string resolveCursorAgentBinary();

/*! Absolute path to a python3 interpreter for the MCP server (empty if not found). */
std::string resolveMcpPython();

/*! Absolute path to the bundled openscad_mcp_server.py (empty if missing). */
std::string resolveMcpServerScriptPath();

/*!
 * Run Cursor Agent CLI asynchronously with OpenSCAD MCP tools.
 * Uses agent mode + project `.cursor/mcp.json` so the model can call
 * set_editor_code / get_editor_code / trigger_preview / trigger_render via the localhost bridge.
 * Falls back to fence/JSON extraction only if MCP did not apply code.
 */
void runCursorAgentChat(const AIProfileConfig& config, const std::vector<AIChatMessage>& history,
                        AIClient::ChunkCallback on_chunk, AIClient::ErrorCallback on_error,
                        AIClient::CompleteCallback on_complete,
                        const std::shared_ptr<std::atomic<bool>>& cancel_flag,
                        const std::shared_ptr<std::atomic<uint64_t>>& active_generation,
                        uint64_t generation);
