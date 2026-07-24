#include "AIService.h"

std::string AIService::defaultSystemPrompt()
{
  return
    "You are the OpenSCAD Expert Assistant. You provide high-quality, surgical, and logical OpenSCAD "
    "code fixes.\n\n"
    "### YOUR CORE RULES:\n"
    "1. **Surgical Excellence**: If the user has a minor error (missing semicolon, wrong bracket), fix "
    "ONLY that specific line. Do NOT rewrite the entire script, do NOT rename variables, and do NOT "
    "change the overall logic unless explicitly asked.\n"
    "2. **OpenSCAD Syntax Mastery**:\n"
    "   - **Modifiers**: `color()`, `rotate()`, `translate()`, etc., are MODIFIERS. They apply to the "
    "next child or block. NEVER assign them to variables like `c = color(\"red\");`. Instead, use "
    "`color(\"red\") cube(10);`.\n"
    "   - **Semicolons**: Every assignment (e.g., `x = 5;`) and every module instantiation (e.g., "
    "`cube(10);`) MUST end with a semicolon. Semicolons are NOT used after module definitions `module "
    "name() { ... }` or after blocks `{ ... }`.\n"
    "3. **Tool Workflow**:\n"
    "   - YOU MUST USE `set_editor_code` to apply any code changes. This updates the editor; a full "
    "F6 render (exportable geometry) runs once when your final reply finishes. Put the full script "
    "only in that tool call — never in chat.\n"
    "   - Use `get_editor_code()` if you need to see the latest script state.\n"
    "   - Use `trigger_preview()` only if you need to re-queue a full F6 render without changing "
    "code; it still runs once at the end of the turn.\n"
    "4. **Chat replies (NO CODE)**:\n"
    "   - After applying code, reply with a short design description only: intent, key dimensions, "
    "features, and how the user can tweak parameters if useful.\n"
    "   - Do NOT paste OpenSCAD source into chat. Do NOT use fenced code blocks for the script. "
    "Do NOT repeat the editor contents. Keep replies concise — no long filler.\n"
    "5. **Formatting**: Use ACTUAL NEWLINES inside tool arguments for code. Never use literal '\\n' "
    "sequences in `set_editor_code`.\n"
    "6. **Tone**: Technical, concise, and helpful.\n\n"
    "### 2D DRAWING → 3D (when the user attaches a drawing image / orthographic PDF page):\n"
    "Treat attached images of technical drawings as engineering input and rebuild a parametric 3D "
    "OpenSCAD model. Users may attach the drawing with little or no text — infer everything from the "
    "sheet.\n"
    "1. **CRITICAL — silhouettes are NOT solids**: Labels like `Top/Front (outer silhouette only)` "
    "show the outer outline. NEVER extrude a silhouette into a solid block / filled prism.\n"
    "2. **SECTION views define depth**: `SECTION A-A` / `SECTION B-B` are material cuts "
    "(rim or midplane). **Hatched = solid plastic**. **Empty regions inside the section outline = "
    "pockets, cavities, or holes**. Read wall thickness, floor thickness, and pocket depth from "
    "SECTION views (and any `wall~` / `floor~` hints printed on them).\n"
    "3. **Read dimensions**: overall X×Y×Z, chain dims, Ø holes, R fillets from all views.\n"
    "4. **Infer solid (typical shell)**: `difference() { outer_body(); inner_cavity(); }` then "
    "subtract hole cylinders from the Top hole pattern. Align Z-up. If sections show a tray / "
    "open pocket, the cavity must remain open at the top — do not fill it.\n"
    "5. **Parameters first**: Named variables for every major size (`wall_t`, `floor_t`, `pocket_d`, "
    "`hole_d`, …).\n"
    "6. **Fidelity**: Prefer documented SECTION thicknesses over guessing. If unreadable, pick a "
    "clear named default and state it in chat.\n"
    "7. **Apply**: `set_editor_code` with the full script; chat = short dimension summary only.";
}

#ifndef __EMSCRIPTEN__

#include "HTTPClient.h"
#include "AIClient.h"
#include "core/AIFreeAgents.h"
#include "platform/PlatformUtils.h"
#include "json/json.hpp"
#include <cmath>
#include <fstream>
#include <cstdlib>

static std::string getAISettingsPath()
{
  std::string configPath = PlatformUtils::userConfigPath();
  if (configPath.empty()) {
    char *home = std::getenv("HOME");
    if (home) {
      configPath = std::string(home) + "/.openscad";
    } else {
      configPath = ".";
    }
  }
  return configPath + "/ai_settings.json";
}

// Ensure chat replies describe the design only — code goes to the editor via tools.
static void appendMandatoryChatReplyRule(std::string& sys_prompt)
{
  constexpr const char *kMarker = "### CHAT REPLY (MANDATORY):";
  if (sys_prompt.find(kMarker) != std::string::npos) {
    return;
  }
  sys_prompt +=
    "\n\n### CHAT REPLY (MANDATORY):\n"
    "Do NOT paste OpenSCAD source code into the chat (no fenced code blocks, no full scripts, no large "
    "snippets). Apply all code only via `set_editor_code`. In chat, briefly describe the design — "
    "dimensions, features, and useful tweaks — in short prose or bullets.";
}

// Always reinforce drawing→3D rules even when the user has a custom system_prompt.
static void appendMandatoryDrawingRule(std::string& sys_prompt)
{
  constexpr const char *kMarker = "### 2D DRAWING → 3D (MANDATORY ADDENDUM):";
  if (sys_prompt.find(kMarker) != std::string::npos) {
    return;
  }
  sys_prompt +=
    "\n\n### 2D DRAWING → 3D (MANDATORY ADDENDUM):\n"
    "When the user attaches a 2D drawing / orthographic sheet image: silhouettes are OUTER outlines "
    "only — NEVER extrude them into a solid block. SECTION A-A/B-B hatched areas = solid material; "
    "empty areas inside sections = pockets/cavities/holes. Prefer `difference(outer, cavity)` for "
    "trays/shells; read wall/floor thickness from sections; apply via `set_editor_code`.";
}

// Old builds seeded restrictive defaults. Strip those exact values so requests are
// unrestricted unless the user explicitly configures limits.
static bool stripLegacyDefaultLimits(nlohmann::json& params)
{
  if (!params.is_object()) return false;
  bool changed = false;
  if (params.contains("temperature") && params["temperature"].is_number() &&
      std::fabs(params["temperature"].get<double>() - 0.7) < 1e-9) {
    params.erase("temperature");
    changed = true;
  }
  if (params.contains("max_tokens")) {
    const auto& v = params["max_tokens"];
    const bool match = (v.is_number_integer() && v.get<int>() == 2048) ||
                       (v.is_number() && std::fabs(v.get<double>() - 2048.0) < 1e-9);
    if (match) {
      params.erase("max_tokens");
      changed = true;
    }
  }
  if (params.contains("context_limit")) {
    const auto& v = params["context_limit"];
    const bool match = (v.is_number_integer() && v.get<int>() == 10) ||
                       (v.is_number() && std::fabs(v.get<double>() - 10.0) < 1e-9);
    if (match) {
      params.erase("context_limit");
      changed = true;
    }
  }
  if (params.contains("default_prompt") && params["default_prompt"].is_string() &&
      params["default_prompt"].get<std::string>() ==
        "Create a sphere with radius 10 and detail level $fn=50.") {
    params.erase("default_prompt");
    changed = true;
  }
  return changed;
}

static bool loadActiveProfile(AIProfileConfig& config, std::string& error_msg)
{
  const std::string path = getAISettingsPath();
  std::ifstream file(path);
  nlohmann::json j;
  if (!file.is_open()) {
    // First run: seed a Default profile so Settings + Chat have something to use.
    j = nlohmann::json::object();
    AIFreeAgents::ensurePresets(j);
    std::ofstream out(path);
    if (out.is_open()) {
      out << j.dump(4);
    }
  } else {
    try {
      file >> j;
    } catch (const std::exception& e) {
      error_msg = std::string("JSON parsing error: ") + e.what();
      return false;
    }
    if (AIFreeAgents::ensurePresets(j)) {
      std::ofstream out(path);
      if (out.is_open()) {
        out << j.dump(4);
      }
    }
  }

  if (!j.contains("profiles") || !j["profiles"].is_object()) {
    error_msg = "ai_settings.json is missing 'profiles' object.";
    return false;
  }
  auto& profiles = j["profiles"];

  if (!j.contains("activeProfile") || !j["activeProfile"].is_string()) {
    error_msg = "ai_settings.json is missing 'activeProfile' string.";
    return false;
  }
  const std::string active_profile_name = j["activeProfile"].get<std::string>();

  if (!profiles.contains(active_profile_name) || !profiles[active_profile_name].is_object()) {
    error_msg = "Active profile '" + active_profile_name + "' was not found in 'profiles'.";
    return false;
  }
  auto& profile = profiles[active_profile_name];

  config.endpoint = profile.value("endpoint", "");
  config.apiKey = profile.value("apiKey", "");

  if (profile.contains("params") && profile["params"].is_object()) {
    auto& params = profile["params"];
    config.model = params.value("model", "");
    if (config.model.rfind("models/", 0) == 0) {
      config.model = config.model.substr(7);
    }
    stripLegacyDefaultLimits(params);
    config.parameters = params;
  } else {
    config.model = "";
    config.parameters = nlohmann::json::object();
  }

  return true;
}

class AIService::Impl
{
public:
  std::unique_ptr<HTTPClient> http_client;
  std::unique_ptr<AIClient> ai_client;
  ToolExecutor tool_executor = nullptr;

  Impl()
  {
    http_client = std::make_unique<HTTPClient>();
    ai_client = std::make_unique<AIClient>(std::shared_ptr<HTTPClient>(http_client.release()));
  }
  ~Impl() = default;

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
};

AIService::AIService() : impl(std::make_unique<Impl>())
{
}

AIService::~AIService() = default;

AIService::AIService(AIService&&) noexcept = default;
AIService& AIService::operator=(AIService&&) noexcept = default;

void AIService::registerToolExecutor(ToolExecutor executor)
{
  impl->tool_executor = std::move(executor);
}

void AIService::chatCompletionStream(std::vector<ChatMessage>& history, ChunkCallback on_chunk,
                                     ErrorCallback on_error, CompleteCallback on_complete)
{
  AIProfileConfig config;
  std::string error_msg;
  if (!loadActiveProfile(config, error_msg)) {
    if (on_error) {
      on_error(error_msg);
    }
    return;
  }

  std::vector<AIChatMessage> ai_history;
  std::string sys_prompt = AIService::defaultSystemPrompt();

  if (config.parameters.contains("system_prompt") && config.parameters["system_prompt"].is_string()) {
    const auto custom = config.parameters["system_prompt"].get<std::string>();
    if (!custom.empty()) {
      sys_prompt = custom;
    }
  }
  appendMandatoryChatReplyRule(sys_prompt);
  appendMandatoryDrawingRule(sys_prompt);

  bool already_has_system = false;
  if (!history.empty() && history[0].role == "system") {
    already_has_system = true;
  }
  if (!already_has_system) {
    ai_history.push_back({"system", sys_prompt});
  }

  for (const auto& msg : history) {
    AIChatMessage am;
    am.role = msg.role;
    am.content = msg.content;
    am.tool_call_id = msg.tool_call_id;
    am.images = msg.images;
    if (!msg.tool_calls.empty()) {
      try {
        auto tcs_json = nlohmann::json::parse(msg.tool_calls);
        if (tcs_json.is_array()) {
          for (auto& tc_json : tcs_json) {
            AIToolCall tc;
            parseAIToolCall(tc_json, tc);
            am.tool_calls.push_back(tc);
          }
        }
      } catch (...) {
      }
    }
    ai_history.push_back(am);
  }

  auto on_complete_wrapper = [this, config, &history, on_chunk, on_error, on_complete,
                              sys_prompt](const std::vector<AIToolCall>& tool_calls) {
    if (tool_calls.empty()) {
      if (on_complete) {
        on_complete();
      }
      return;
    }

    ChatMessage assistant_msg;
    assistant_msg.role = "assistant";
    assistant_msg.content = "";
    nlohmann::json tcs_arr = nlohmann::json::array();
    for (const auto& tc : tool_calls) {
      tcs_arr.push_back(serializeAIToolCall(tc));
    }
    assistant_msg.tool_calls = tcs_arr.dump();
    history.push_back(assistant_msg);

    for (const auto& tc : tool_calls) {
      std::string result;
      if (impl->tool_executor) {
        result = impl->tool_executor(tc.name, tc.arguments);
      } else {
        result = "Error: No tool executor registered.";
      }
      ChatMessage tool_msg;
      tool_msg.role = "tool";
      tool_msg.content = result;
      tool_msg.tool_call_id = tc.id;
      history.push_back(tool_msg);
    }

    chatCompletionStream(history, on_chunk, on_error, on_complete);
  };

  impl->ai_client->sendChatCompletionStream(config, ai_history, on_chunk, on_error, on_complete_wrapper);
}

void AIService::chatCompletion(const std::vector<ChatMessage>& history, ResponseCallback on_response,
                               ErrorCallback on_error)
{
  AIProfileConfig config;
  std::string error_msg;
  if (!loadActiveProfile(config, error_msg)) {
    if (on_error) {
      on_error(error_msg);
    }
    return;
  }

  std::vector<AIChatMessage> ai_history;
  std::string sys_prompt = AIService::defaultSystemPrompt();
  if (config.parameters.contains("system_prompt") && config.parameters["system_prompt"].is_string()) {
    const auto custom = config.parameters["system_prompt"].get<std::string>();
    if (!custom.empty()) {
      sys_prompt = custom;
    }
  }
  appendMandatoryChatReplyRule(sys_prompt);
  appendMandatoryDrawingRule(sys_prompt);

  bool already_has_system = false;
  if (!history.empty() && history[0].role == "system") {
    already_has_system = true;
  }
  if (!already_has_system) {
    ai_history.push_back({"system", sys_prompt});
  }

  for (const auto& msg : history) {
    AIChatMessage am;
    am.role = msg.role;
    am.content = msg.content;
    am.tool_call_id = msg.tool_call_id;
    am.images = msg.images;
    if (!msg.tool_calls.empty()) {
      try {
        auto tcs_json = nlohmann::json::parse(msg.tool_calls);
        if (tcs_json.is_array()) {
          for (auto& tc_json : tcs_json) {
            AIToolCall tc;
            parseAIToolCall(tc_json, tc);
            am.tool_calls.push_back(tc);
          }
        }
      } catch (...) {
      }
    }
    ai_history.push_back(am);
  }

  impl->ai_client->sendChatCompletion(
    config, ai_history,
    [on_response](const std::string& response, const std::vector<AIToolCall>&) {
      if (on_response) {
        on_response(response);
      }
    },
    on_error);
}

std::string AIService::getDefaultPrompt() const
{
  AIProfileConfig config;
  std::string error_msg;
  if (loadActiveProfile(config, error_msg)) {
    if (config.parameters.contains("default_prompt") &&
        config.parameters["default_prompt"].is_string()) {
      std::string prompt = config.parameters["default_prompt"].get<std::string>();
      if (!prompt.empty()) {
        return prompt;
      }
    }
  }
  return "";
}

void AIService::cancelPendingRequests()
{
  impl->ai_client->cancelPendingRequests();
}

#else  // __EMSCRIPTEN__

class AIService::Impl
{
};

AIService::AIService() : impl(std::make_unique<Impl>())
{
}
AIService::~AIService() = default;

AIService::AIService(AIService&&) noexcept = default;
AIService& AIService::operator=(AIService&&) noexcept = default;

void AIService::chatCompletionStream(std::vector<ChatMessage>&, ChunkCallback, ErrorCallback on_error,
                                     CompleteCallback)
{
  if (on_error) {
    on_error("AI service is not supported on WebAssembly.");
  }
}

void AIService::chatCompletion(const std::vector<ChatMessage>&, ResponseCallback, ErrorCallback on_error)
{
  if (on_error) {
    on_error("AI service is not supported on WebAssembly.");
  }
}

std::string AIService::getDefaultPrompt() const
{
  return "";
}

void AIService::cancelPendingRequests()
{
}

#endif  // __EMSCRIPTEN__
