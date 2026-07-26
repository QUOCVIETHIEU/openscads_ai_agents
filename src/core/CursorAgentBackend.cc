#include "core/CursorAgentBackend.h"

#include "core/AIFreeAgents.h"
#include "json/json.hpp"
#include "platform/PlatformUtils.h"

#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#else
#include <windows.h>
#endif

namespace {

bool startsWith(const std::string& s, const char *prefix)
{
  const size_t n = std::strlen(prefix);
  return s.size() >= n && s.compare(0, n, prefix) == 0;
}

std::string trimCopy(std::string s)
{
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

bool fileIsExecutable(const std::filesystem::path& p)
{
  std::error_code ec;
  if (!std::filesystem::is_regular_file(p, ec)) return false;
#ifndef _WIN32
  return ::access(p.c_str(), X_OK) == 0;
#else
  return true;
#endif
}

std::string joinPath(const std::string& a, const std::string& b)
{
  std::filesystem::path p(a);
  p /= b;
  return p.string();
}

std::string homeDir()
{
  const char *home = std::getenv("HOME");
#ifdef _WIN32
  if (!home || !*home) home = std::getenv("USERPROFILE");
#endif
  return home ? std::string(home) : std::string();
}

/*! Extract first ```openscad / ```scad / ``` fence that looks like OpenSCAD. */
bool extractOpenscadFence(const std::string& content, std::string& code_out)
{
  const std::string markers[] = {"```openscad", "```scad", "```OpenSCAD", "```"};
  for (const auto& marker : markers) {
    size_t start = content.find(marker);
    while (start != std::string::npos) {
      size_t body = start + marker.size();
      if (body < content.size() && content[body] == '\r') ++body;
      if (body < content.size() && content[body] == '\n') ++body;
      const size_t end = content.find("```", body);
      if (end == std::string::npos) break;
      std::string code = content.substr(body, end - body);
      // Heuristic: real OpenSCAD usually has module/cube/sphere/cylinder/difference or ;
      if (code.find("module ") != std::string::npos || code.find("cube(") != std::string::npos ||
          code.find("sphere(") != std::string::npos || code.find("cylinder(") != std::string::npos ||
          code.find("difference(") != std::string::npos || code.find("union(") != std::string::npos ||
          code.find(";") != std::string::npos) {
        code_out = code;
        return true;
      }
      start = content.find(marker, end + 3);
    }
  }
  return false;
}

std::vector<AIToolCall> extractToolCallsFromAssistantText(const std::string& text)
{
  std::vector<AIToolCall> tools = tryParseEmbeddedToolCalls(text);
  if (!tools.empty()) return tools;

  std::string code;
  if (extractOpenscadFence(text, code)) {
    AIToolCall tc;
    tc.id = "call_cursor_fence_0";
    tc.type = "function";
    tc.name = "set_editor_code";
    nlohmann::json args = nlohmann::json::object();
    args["code"] = code;
    tc.arguments = args.dump();
    tools.push_back(tc);
  }
  return tools;
}

std::string buildPromptFromHistory(const std::vector<AIChatMessage>& history, bool use_mcp)
{
  std::ostringstream oss;
  oss << "You are helping the user design OpenSCAD models inside the OpenSCAD desktop app.\n";
  if (use_mcp) {
    oss << "Native MCP tools are available — USE THEM:\n"
           "- get_skill(name=openscad-cad, compact=true|false): load the CAD workflow skill FIRST "
           "for complex designs.\n"
           "- get_cheatsheet(): short OpenSCAD syntax reference.\n"
           "- get_editor_code(): read the current editor before editing.\n"
           "- set_editor_code(code): apply the FULL OpenSCAD script and render (F6).\n"
           "- get_model_info(): bbox/facets/errors after render.\n"
           "- get_preview_image(): capture viewport PNG and visually judge the model.\n"
           "- get_console_log / get_camera_info / pan_view / zoom_in / zoom_out / zoom_100 / "
           "view_all / reset_view / list_tools: diagnostics and camera helpers "
           "(pan_view dx/dy in mm; zoom_100 = default distance).\n"
           "Never paste OpenSCAD into chat instead of set_editor_code.\n"
           "Workflow: get_skill → set_editor_code → get_model_info → view_all → get_preview_image "
           "→ pan_view/zoom_* if needed to inspect details → repair if needed (max 2 retries).\n"
           "If set_editor_code returns [render-error], fix and call set_editor_code again.\n"
           "After a successful apply, reply with 2–4 short sentences (parts + sizes).\n";
  } else {
    oss << "When the user asks for a model or an edit, reply with exactly ONE fenced code block "
           "```openscad ... ``` containing the FULL script (not a diff), followed by 2–4 short "
           "sentences describing parts + sizes.\n";
  }
  oss << "Images (if mentioned) are design drawings attached in the OpenSCAD app.\n\n"
         "=== CONVERSATION ===\n";

  for (const auto& msg : history) {
    std::string role = msg.role;
    if (role == "tool") role = "tool_result";
    oss << "[" << role << "]\n";
    if (!msg.tool_call_id.empty()) {
      oss << "(tool_call_id=" << msg.tool_call_id << ")\n";
    }
    if (!msg.tool_calls.empty()) {
      oss << "tool_calls:\n";
      for (const auto& tc : msg.tool_calls) {
        oss << "- " << tc.name << " " << tc.arguments << "\n";
      }
    }
    if (!msg.images.empty()) {
      oss << "(" << msg.images.size()
          << " image attachment(s) were provided in the OpenSCAD app; treat them as design "
             "drawings.)\n";
    }
    oss << msg.content << "\n\n";
  }
  oss << "=== END ===\n";
  if (use_mcp) {
    oss << "Respond now. If the user asked for a model or edit, call set_editor_code with the "
           "full script via the openscad MCP tools.\n";
  } else {
    oss << "Respond now. If the user asked for a model or edit, output the full script in a "
           "single ```openscad fence.\n";
  }
  return oss.str();
}

/*! Parse one NDJSON stream-json line; append new assistant text into accum and fire on_chunk. */
void handleStreamJsonLine(const std::string& line, std::string& assistant_accum,
                          bool& saw_result_text, AIClient::ChunkCallback on_chunk)
{
  if (line.empty()) return;
  try {
    auto j = nlohmann::json::parse(line);
    if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) return;
    const std::string type = j["type"].get<std::string>();

    if (type == "assistant" && j.contains("message") && j["message"].is_object()) {
      // Skip duplicate flushes (see Cursor stream-json docs).
      const bool has_ts = j.contains("timestamp_ms") && !j["timestamp_ms"].is_null();
      const bool has_model_call = j.contains("model_call_id") && !j["model_call_id"].is_null();
      if (has_model_call) return;          // pre-tool duplicate
      if (!has_ts && saw_result_text) return;  // final flush duplicate after we already streamed

      const auto& msg = j["message"];
      if (!msg.contains("content") || !msg["content"].is_array()) return;
      for (const auto& block : msg["content"]) {
        if (!block.is_object()) continue;
        if (block.value("type", "") != "text") continue;
        if (!block.contains("text") || !block["text"].is_string()) continue;
        const std::string text = block["text"].get<std::string>();
        if (text.empty()) continue;
        // With stream-partial-output, deltas are incremental; without it, each
        // assistant event is a full segment — append either way if not already present
        // as a trailing duplicate of the accumulator.
        if (assistant_accum.size() >= text.size() &&
            assistant_accum.compare(assistant_accum.size() - text.size(), text.size(), text) == 0) {
          continue;
        }
        // If this looks like a full replay of everything so far, skip.
        if (!assistant_accum.empty() && text.find(assistant_accum) == 0 &&
            text.size() > assistant_accum.size()) {
          const std::string delta = text.substr(assistant_accum.size());
          assistant_accum = text;
          if (on_chunk && !delta.empty()) on_chunk(delta);
          continue;
        }
        assistant_accum += text;
        if (on_chunk) on_chunk(text);
      }
      return;
    }

    if (type == "result") {
      if (j.value("is_error", false)) return;
      if (j.contains("result") && j["result"].is_string()) {
        const std::string result = j["result"].get<std::string>();
        saw_result_text = true;
        // Prefer streamed assistant text; only use result if we got nothing.
        if (assistant_accum.empty() && !result.empty()) {
          assistant_accum = result;
          if (on_chunk) on_chunk(result);
        }
      }
    }
  } catch (...) {
    // ignore malformed lines
  }
}

std::string writeTempPrompt(const std::string& prompt)
{
  const auto dir = std::filesystem::temp_directory_path() / "openscad-cursor-agent";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const auto path =
    dir / ("prompt-" + std::to_string(
                         std::chrono::steady_clock::now().time_since_epoch().count()) +
           ".txt");
  {
    std::ofstream out(path);
    out << prompt;
  }
  return path.string();
}

std::string readBridgeUrl()
{
  const std::string path = PlatformUtils::userConfigPath() + "/ai_bridge.json";
  try {
    std::ifstream in(path);
    if (!in) return {};
    nlohmann::json j;
    in >> j;
    return j.value("url", "");
  } catch (...) {
    return {};
  }
}

bool readBridgeAppliedCode()
{
  const std::string path = PlatformUtils::userConfigPath() + "/ai_bridge_session.json";
  try {
    std::ifstream in(path);
    if (!in) return false;
    nlohmann::json j;
    in >> j;
    return j.value("appliedCode", false);
  } catch (...) {
    return false;
  }
}

std::string resolvePython3()
{
#ifndef _WIN32
  const char *candidates[] = {"/usr/bin/python3", "/opt/homebrew/bin/python3",
                              "/usr/local/bin/python3"};
  for (const char *c : candidates) {
    if (fileIsExecutable(c)) return c;
  }
  if (FILE *fp = popen("command -v python3 2>/dev/null", "r")) {
    char buf[1024];
    std::string line;
    if (fgets(buf, sizeof(buf), fp)) line = trimCopy(buf);
    pclose(fp);
    if (!line.empty() && fileIsExecutable(line)) return line;
  }
#endif
  return {};
}

std::string resolveMcpServerScript()
{
  // Prefer bundled resource, then source-tree relative paths for dev builds.
  const auto bundled = PlatformUtils::resourcePath("mcp") / "openscad_mcp_server.py";
  if (std::filesystem::exists(bundled)) return bundled.string();

  const char *devCandidates[] = {
    "mcp/openscad_mcp_server.py",
    "../mcp/openscad_mcp_server.py",
    "../../mcp/openscad_mcp_server.py",
  };
  for (const char *c : devCandidates) {
    std::error_code ec;
    auto p = std::filesystem::absolute(c, ec);
    if (!ec && std::filesystem::exists(p)) return p.string();
  }
  return {};
}

bool writeCursorMcpConfig(const std::filesystem::path& workspace, const std::string& bridgeUrl,
                          std::string& error)
{
  const std::string python = resolvePython3();
  const std::string script = resolveMcpServerScript();
  if (python.empty()) {
    error = "python3 not found (needed for OpenSCAD MCP server).";
    return false;
  }
  if (script.empty()) {
    error =
      "openscad_mcp_server.py not found. Rebuild so mcp/ is copied into app Resources.";
    return false;
  }
  if (bridgeUrl.empty()) {
    error =
      "OpenSCAD MCP bridge is not running.\n"
      "Enable it in AI Settings → MCP and reopen the AI chat panel.";
    return false;
  }

  const auto cursorDir = workspace / ".cursor";
  std::error_code ec;
  std::filesystem::create_directories(cursorDir, ec);

  nlohmann::json root = nlohmann::json::object();
  nlohmann::json servers = nlohmann::json::object();
  nlohmann::json openscad = nlohmann::json::object();
  openscad["type"] = "stdio";
  openscad["command"] = python;
  openscad["args"] = nlohmann::json::array({script});
  openscad["env"] = nlohmann::json::object({{"OPENSCAD_AI_BRIDGE_URL", bridgeUrl}});
  servers["openscad"] = openscad;
  root["mcpServers"] = servers;

  const auto mcpPath = cursorDir / "mcp.json";
  std::ofstream out(mcpPath);
  if (!out) {
    error = "Failed to write " + mcpPath.string();
    return false;
  }
  out << root.dump(2);
  return true;
}

/*!
 * Persistently approve the project "openscad" MCP server for this workspace.
 * `--approve-mcps` alone is not always enough in print/non-interactive mode —
 * without this, tools show up as "not loaded (needs approval)" and set_editor_code
 * is rejected.
 */
void ensureOpenscadMcpApproved(const std::string& binary, const std::filesystem::path& workspace)
{
#ifndef _WIN32
  if (binary.empty()) return;
  const std::string ws = workspace.string();
  pid_t pid = 0;
  char *argv[] = {
    const_cast<char *>(binary.c_str()),
    const_cast<char *>("--workspace"),
    const_cast<char *>(ws.c_str()),
    const_cast<char *>("mcp"),
    const_cast<char *>("enable"),
    const_cast<char *>("openscad"),
    nullptr,
  };
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  // Silence CLI chatter — we only care that approval is recorded.
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  const int rc = posix_spawnp(&pid, binary.c_str(), &actions, nullptr, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0 || pid <= 0) return;
  int status = 0;
  // Bounded wait so a hung CLI cannot block chat forever.
  for (int i = 0; i < 100; ++i) {  // ~5s
    const pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == pid || (r < 0 && errno == ECHILD)) return;
    usleep(50 * 1000);
  }
  kill(pid, SIGTERM);
  waitpid(pid, nullptr, 0);
#else
  (void)binary;
  (void)workspace;
#endif
}

#ifndef _WIN32

struct ChildProc {
  pid_t pid = -1;
  int stdout_fd = -1;
  int stderr_fd = -1;
};

bool spawnAgent(const std::string& binary, const std::vector<std::string>& args,
                const std::string& prompt, ChildProc& child, std::string& error)
{
  int in_pipe[2] = {-1, -1};
  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
    error = "Failed to create pipes for cursor-agent.";
    return false;
  }

  std::vector<char *> argv;
  argv.reserve(args.size() + 2);
  argv.push_back(const_cast<char *>(binary.c_str()));
  for (const auto& a : args) {
    argv.push_back(const_cast<char *>(a.c_str()));
  }
  argv.push_back(nullptr);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, in_pipe[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, in_pipe[0]);
  posix_spawn_file_actions_addclose(&actions, in_pipe[1]);
  posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
  posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
  posix_spawn_file_actions_addclose(&actions, err_pipe[0]);
  posix_spawn_file_actions_addclose(&actions, err_pipe[1]);

  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, binary.c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);

  close(in_pipe[0]);
  close(out_pipe[1]);
  close(err_pipe[1]);

  if (rc != 0) {
    close(in_pipe[1]);
    close(out_pipe[0]);
    close(err_pipe[0]);
    error = std::string("Failed to launch cursor-agent (") + binary + "): " + std::strerror(rc);
    return false;
  }

  // Write prompt on stdin, then close so the CLI sees EOF.
  const char *data = prompt.data();
  size_t remaining = prompt.size();
  while (remaining > 0) {
    const ssize_t n = write(in_pipe[1], data, remaining);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    data += n;
    remaining -= static_cast<size_t>(n);
  }
  close(in_pipe[1]);

  child.pid = pid;
  child.stdout_fd = out_pipe[0];
  child.stderr_fd = err_pipe[0];
  return true;
}

void terminateChild(ChildProc& child)
{
  if (child.pid > 0) {
    kill(child.pid, SIGTERM);
    // Brief grace period then force-kill.
    for (int i = 0; i < 20; ++i) {
      int status = 0;
      const pid_t r = waitpid(child.pid, &status, WNOHANG);
      if (r == child.pid || (r < 0 && errno == ECHILD)) {
        child.pid = -1;
        break;
      }
      usleep(50 * 1000);
    }
    if (child.pid > 0) {
      kill(child.pid, SIGKILL);
      waitpid(child.pid, nullptr, 0);
      child.pid = -1;
    }
  }
  if (child.stdout_fd >= 0) {
    close(child.stdout_fd);
    child.stdout_fd = -1;
  }
  if (child.stderr_fd >= 0) {
    close(child.stderr_fd);
    child.stderr_fd = -1;
  }
}

std::string readFdToString(int fd)
{
  std::string out;
  char buf[4096];
  for (;;) {
    const ssize_t n = read(fd, buf, sizeof(buf));
    if (n > 0) {
      out.append(buf, static_cast<size_t>(n));
    } else if (n == 0) {
      break;
    } else if (errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
  return out;
}

#endif  // !_WIN32

}  // namespace

std::string resolveMcpPython()
{
  return resolvePython3();
}

std::string resolveMcpServerScriptPath()
{
  return resolveMcpServerScript();
}

bool isCursorAgentEndpoint(const std::string& endpoint)
{
  const std::string e = trimCopy(endpoint);
  if (e.empty()) return false;
  if (startsWith(e, "cursor://")) return true;
  if (e == "cursor-agent" || e == "agent") return true;
  return false;
}

std::string resolveCursorAgentBinary()
{
  const std::string home = homeDir();
  const std::vector<std::string> candidates = {
    home.empty() ? std::string() : joinPath(home, ".local/bin/cursor-agent"),
    home.empty() ? std::string() : joinPath(home, ".local/bin/agent"),
    "/usr/local/bin/cursor-agent",
    "/opt/homebrew/bin/cursor-agent",
    "/Applications/Cursor.app/Contents/Resources/app/bin/cursor-agent",
  };
  for (const auto& c : candidates) {
    if (c.empty()) continue;
    if (fileIsExecutable(c)) return c;
  }

  // Fall back to PATH lookup.
#ifndef _WIN32
  if (FILE *fp = popen("command -v cursor-agent 2>/dev/null", "r")) {
    char buf[1024];
    std::string line;
    if (fgets(buf, sizeof(buf), fp)) line = trimCopy(buf);
    pclose(fp);
    if (!line.empty() && fileIsExecutable(line)) return line;
  }
  if (FILE *fp = popen("command -v agent 2>/dev/null", "r")) {
    char buf[1024];
    std::string line;
    if (fgets(buf, sizeof(buf), fp)) line = trimCopy(buf);
    pclose(fp);
    if (!line.empty() && fileIsExecutable(line)) return line;
  }
#endif
  return {};
}

void runCursorAgentChat(const AIProfileConfig& config, const std::vector<AIChatMessage>& history,
                        AIClient::ChunkCallback on_chunk, AIClient::ErrorCallback on_error,
                        AIClient::CompleteCallback on_complete,
                        const std::shared_ptr<std::atomic<bool>>& cancel_flag,
                        const std::shared_ptr<std::atomic<uint64_t>>& active_generation,
                        uint64_t generation)
{
  std::thread([config, history, on_chunk, on_error, on_complete, cancel_flag, active_generation,
               generation]() {
    auto stillActive = [&]() {
      return active_generation && active_generation->load() == generation &&
             !(cancel_flag && cancel_flag->load());
    };

#ifdef _WIN32
    if (on_error) {
      on_error(
        "Cursor Agent CLI backend is not yet implemented on Windows. "
        "Use macOS/Linux, or switch to Gemini/OpenAI/Ollama.");
    }
    return;
#else
    const std::string binary = resolveCursorAgentBinary();
    if (binary.empty()) {
      if (on_error && stillActive()) {
        on_error(
          "cursor-agent CLI not found.\n"
          "Install it with: curl https://cursor.com/install -fsS | bash\n"
          "Then restart OpenSCAD. Expected at ~/.local/bin/cursor-agent");
      }
      return;
    }
    if (config.apiKey.empty()) {
      if (on_error && stillActive()) {
        on_error(
          "Cursor API key is empty.\n"
          "Paste your key from https://cursor.com/dashboard/integrations into AI Settings.");
      }
      return;
    }

    // MCP can be disabled in AI Settings → MCP; fall back to fence extraction then.
    const bool mcp_on = AIFreeAgents::mcpEnabled();
    const std::string bridgeUrl = mcp_on ? readBridgeUrl() : std::string();
    const bool use_mcp = mcp_on && !bridgeUrl.empty();

    const std::string prompt = buildPromptFromHistory(history, use_mcp);
    const std::string prompt_path = writeTempPrompt(prompt);

    // Dedicated workspace with project MCP config for OpenSCAD tools.
    const auto ws = std::filesystem::temp_directory_path() / "openscad-cursor-workspace";
    std::error_code ec;
    std::filesystem::create_directories(ws, ec);

    if (use_mcp) {
      std::string mcp_error;
      if (!writeCursorMcpConfig(ws, bridgeUrl, mcp_error)) {
        if (on_error && stillActive()) on_error(mcp_error);
        return;
      }
      ensureOpenscadMcpApproved(binary, ws);
    } else {
      // Remove any stale project MCP config so the agent doesn't try a dead bridge.
      std::filesystem::remove(ws / ".cursor" / "mcp.json", ec);
    }

    std::vector<std::string> args;
    args.emplace_back("-p");
    // Agent mode (default) so MCP tools can run. Ask mode cannot call tools.
    args.emplace_back("--output-format");
    args.emplace_back("stream-json");
    args.emplace_back("--stream-partial-output");
    args.emplace_back("--trust");
    if (use_mcp) {
      args.emplace_back("--approve-mcps");
      args.emplace_back("--force");  // auto-allow MCP tool calls in non-interactive -p mode
      args.emplace_back("--sandbox");
      args.emplace_back("disabled");  // allow MCP → localhost OpenSCAD bridge
    }
    args.emplace_back("--api-key");
    args.emplace_back(config.apiKey);
    if (!config.model.empty() && config.model != "auto") {
      args.emplace_back("--model");
      args.emplace_back(config.model);
    }
    args.emplace_back("Read the full instructions in this file and follow them exactly: " +
                      prompt_path);
    args.emplace_back("--workspace");
    args.emplace_back(ws.string());

    ChildProc child;
    std::string spawn_error;
    if (!spawnAgent(binary, args, prompt, child, spawn_error)) {
      if (on_error && stillActive()) on_error(spawn_error);
      return;
    }

    std::string assistant_accum;
    std::string line_buf;
    std::string stderr_accum;
    bool saw_result_text = false;
    char buf[4096];

    // Make stdout non-blocking-ish via select loop with cancel checks.
    while (stillActive()) {
      fd_set rfds;
      FD_ZERO(&rfds);
      int maxfd = -1;
      if (child.stdout_fd >= 0) {
        FD_SET(child.stdout_fd, &rfds);
        maxfd = std::max(maxfd, child.stdout_fd);
      }
      if (child.stderr_fd >= 0) {
        FD_SET(child.stderr_fd, &rfds);
        maxfd = std::max(maxfd, child.stderr_fd);
      }
      if (maxfd < 0) break;

      timeval tv{};
      tv.tv_sec = 0;
      tv.tv_usec = 200 * 1000;
      const int sel = select(maxfd + 1, &rfds, nullptr, nullptr, &tv);
      if (sel < 0) {
        if (errno == EINTR) continue;
        break;
      }
      if (sel == 0) {
        // Check if child exited.
        int status = 0;
        const pid_t r = waitpid(child.pid, &status, WNOHANG);
        if (r == child.pid) {
          child.pid = -1;
          // Drain remaining.
          break;
        }
        continue;
      }

      if (child.stdout_fd >= 0 && FD_ISSET(child.stdout_fd, &rfds)) {
        const ssize_t n = read(child.stdout_fd, buf, sizeof(buf));
        if (n > 0) {
          for (ssize_t i = 0; i < n; ++i) {
            const char c = buf[i];
            if (c == '\n') {
              handleStreamJsonLine(line_buf, assistant_accum, saw_result_text, on_chunk);
              line_buf.clear();
            } else if (c != '\r') {
              line_buf.push_back(c);
            }
          }
        } else if (n == 0) {
          close(child.stdout_fd);
          child.stdout_fd = -1;
        }
      }
      if (child.stderr_fd >= 0 && FD_ISSET(child.stderr_fd, &rfds)) {
        const ssize_t n = read(child.stderr_fd, buf, sizeof(buf));
        if (n > 0) {
          stderr_accum.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
          close(child.stderr_fd);
          child.stderr_fd = -1;
        }
      }

      if (child.stdout_fd < 0 && child.stderr_fd < 0) break;
    }

    if (!stillActive()) {
      terminateChild(child);
      return;
    }

    // Drain leftovers.
    if (!line_buf.empty()) {
      handleStreamJsonLine(line_buf, assistant_accum, saw_result_text, on_chunk);
      line_buf.clear();
    }
    if (child.stdout_fd >= 0) {
      const std::string rest = readFdToString(child.stdout_fd);
      close(child.stdout_fd);
      child.stdout_fd = -1;
      size_t pos = 0;
      while (pos < rest.size()) {
        const size_t nl = rest.find('\n', pos);
        const std::string line =
          nl == std::string::npos ? rest.substr(pos) : rest.substr(pos, nl - pos);
        handleStreamJsonLine(line, assistant_accum, saw_result_text, on_chunk);
        if (nl == std::string::npos) break;
        pos = nl + 1;
      }
    }
    if (child.stderr_fd >= 0) {
      stderr_accum += readFdToString(child.stderr_fd);
      close(child.stderr_fd);
      child.stderr_fd = -1;
    }

    int status = 0;
    if (child.pid > 0) {
      waitpid(child.pid, &status, 0);
      child.pid = -1;
    }

    if (!stillActive()) return;

    const bool ok_exit = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok_exit && assistant_accum.empty()) {
      std::string msg = "cursor-agent failed";
      if (WIFEXITED(status)) {
        msg += " (exit " + std::to_string(WEXITSTATUS(status)) + ")";
      }
      const std::string err = trimCopy(stderr_accum);
      if (!err.empty()) {
        msg += ":\n" + err;
      } else {
        msg +=
          ".\nCheck your Cursor API key and that `cursor-agent` works in a terminal "
          "(`cursor-agent -p --mode ask \"hi\"`).";
      }
      if (on_error) on_error(msg);
      return;
    }

    // If MCP already applied code through the bridge, don't also synthesize
    // set_editor_code from chat fences (avoids double-apply).
    std::vector<AIToolCall> tools;
    if (!readBridgeAppliedCode()) {
      tools = extractToolCallsFromAssistantText(assistant_accum);
    }
    if (on_complete) on_complete(tools);
#endif
  }).detach();
}
