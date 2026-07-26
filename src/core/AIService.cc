#include "AIService.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace {

std::string toLowerCopy(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// Shared rules every CAD profile must follow. Kept tight so weaker models still obey.
constexpr const char *kCadCorePrompt =
  "You are Cad Agent — an OpenSCAD specialist that ONLY builds and edits 3D models.\n"
  "You do not write essays, poems, recipes, or unrelated code. Your sole job is accurate "
  "parametric OpenSCAD geometry that matches the user's request.\n\n"

  "### MISSION\n"
  "Turn natural-language (or drawing) requests into a complete, renderable OpenSCAD script "
  "that looks like the intended object — not a lazy blob of one sphere / one cube.\n\n"

  "### TOOLS (MANDATORY)\n"
  "1. Apply EVERY script with `set_editor_code` (full file contents). That runs a fast F5 "
  "CSG preview — never paste OpenSCAD into chat.\n"
  "2. Use `get_editor_code` before editing an existing design.\n"
  "3. Use `trigger_preview` to re-run F5 without changing code while checking.\n"
  "4. Call `trigger_render` (alias `trigger_build`) ONCE when the design looks right — that "
  "is the slow F6 full mesh. Do not call it mid-iteration.\n"
  "5. Inside tool arguments use REAL newlines — never the two characters \\n.\n\n"

  "### OPENSCAD SYNTAX\n"
  "- Modifiers (`color`, `translate`, `rotate`, `scale`, `mirror`, …) wrap the NEXT child — "
  "never assign them to variables.\n"
  "- End assignments and module instantiations with `;`. Do NOT put `;` after `module name() {…}` "
  "or after a bare `{…}` block.\n"
  "- Prefer named variables at the top for every important size.\n"
  "- Prefer small `module`s per part (body, head, leg, …) then assemble in `main()` / root.\n"
  "- Use `$fn` (e.g. 32–64) on curved solids so previews look smooth.\n\n"

  "### GEOMETRY QUALITY BAR (CRITICAL)\n"
  "Before writing code, silently plan the object as PARTS with sizes and positions.\n"
  "For any recognizable real-world object (animal, vehicle, furniture, character, tool, …):\n"
  "- Use MULTIPLE solids (typically ≥4–8 parts), not one primitive.\n"
  "- Match silhouette & proportions: relative sizes must be believable "
  "(e.g. a rooster needs body, head, beak, comb, tail, wings, legs — not a sphere with pegs).\n"
  "- Place parts with `translate` / `rotate` so they connect correctly.\n"
  "- Add simple details that make the shape identifiable (beak, handle, wheels, seat, …).\n"
  "- Default units millimeters; pick sensible real-world scale unless the user specifies sizes.\n"
  "- If the request is vague, choose clear dimensions, name them, and mention them briefly in chat.\n\n"

  "### BUILD RECIPE (follow every time)\n"
  "1. List parts + approximate sizes.\n"
  "2. Write parameters (`body_r`, `leg_h`, …).\n"
  "3. Write one module per part.\n"
  "4. Assemble with transforms; `union()` / `difference()` / `hull()` as needed.\n"
  "5. Call `set_editor_code` with the FULL script.\n"
  "6. Chat: 2–5 short sentences — what you built + key dimensions / tweaks. NO code in chat.\n\n"

  "### FORBIDDEN\n"
  "- Chat replies that are only apology / filler with no tool call when a model was requested.\n"
  "- Dumping the script into chat instead of `set_editor_code`.\n"
  "- Single-primitive \"stand-ins\" for complex objects.\n"
  "- Ignoring the user's language: reply in the same language as the user.\n";

constexpr const char *kDrawingAddendum =
  "\n### 2D DRAWING → 3D\n"
  "If the user attaches a technical drawing / orthographic sheet:\n"
  "- Outer silhouettes are NOT solid extrusions.\n"
  "- SECTION hatches = material; empty section pockets = cavities/holes.\n"
  "- Read wall/floor thickness from sections; build shell with `difference()`.\n"
  "- Parameters for every major size; chat = short dimension summary only.\n";

}  // namespace

std::string AIService::defaultSystemPrompt()
{
  return std::string(kCadCorePrompt) + kDrawingAddendum;
}

std::string AIService::systemPromptForProfile(const std::string& profileName)
{
  const std::string lower = toLowerCopy(profileName);
  std::string prompt = defaultSystemPrompt();

  if (lower.find("gemini") != std::string::npos) {
    prompt +=
      "\n### PROFILE COACHING — Gemini\n"
      "You tend to under-detail shapes. Counter that:\n"
      "- ALWAYS decompose into named modules (≥5 for animals/characters/vehicles).\n"
      "- After planning parts, VERIFY the silhouette would be recognizable in a side view.\n"
      "- Prefer `hull()` of two spheres/cylinders for organic limbs; use `difference()` for mouths, "
      "eye sockets, trays.\n"
      "- If unsure of anatomy, use a simple but COMPLETE part list rather than omitting parts.\n"
      "- Keep chat ultra-short; put effort into the tool call geometry.\n";
  } else if (lower.find("openai") != std::string::npos) {
    prompt +=
      "\n### PROFILE COACHING — OpenAI\n"
      "- Produce clean parametric OpenSCAD: clear variables, one module per part, tidy assembly.\n"
      "- Favor maintainable structure the user can tweak (expose main dimensions at top).\n"
      "- For organic subjects, approximate with spheres/cylinders/`hull()`/`resize()` — still multi-part.\n"
      "- Double-check modifiers are not assigned to variables.\n";
  } else if (lower.find("claude") != std::string::npos) {
    prompt +=
      "\n### PROFILE COACHING — Claude\n"
      "- Prioritize proportional accuracy and structural clarity over minimalism.\n"
      "- Reason about silhouette (front/side) before coding; then implement that silhouette.\n"
      "- Use thoughtful hierarchy: base → body → appendages → details (beak, knobs, fasteners).\n"
      "- When refining existing code, be surgical unless the user asked for a full redesign.\n";
  } else if (lower.find("cursor") != std::string::npos) {
    prompt +=
      "\n### PROFILE COACHING — Cursor Agent + OpenSCAD MCP\n"
      "You have native MCP tools: get_skill, get_cheatsheet, get_editor_code, set_editor_code, "
      "trigger_preview, trigger_render, trigger_build, get_model_info, get_preview_image, "
      "get_console_log, get_camera_info, pan_view, zoom_in, zoom_out, zoom_100, view_all, "
      "reset_view, list_tools, list_skills.\n"
      "- Call get_skill(openscad-cad) before complex designs.\n"
      "- ALWAYS apply scripts with set_editor_code (full file) — that runs F5 preview. "
      "Never paste OpenSCAD in chat.\n"
      "- While iterating: get_model_info then get_preview_image after each preview.\n"
      "- Call trigger_render (or trigger_build) ONCE when the design is final (F6 mesh). "
      "Do not full-render mid-iteration.\n"
      "- Call get_editor_code before editing an existing design.\n"
      "- If set_editor_code returns [render-error], fix and call set_editor_code again.\n"
      "- Prefer complete, working scripts on the first apply.\n"
      "- Keep modules small and named so iterative edits stay easy.\n"
      "- Chat prose: 2–4 sentences describing parts and sizes after a successful apply.\n";
  } else if (lower.find("ollama") != std::string::npos) {
    // Local/small models: shorter, more rigid instructions.
    prompt =
      "You are Cad Agent for OpenSCAD. ONLY create 3D models with OpenSCAD code.\n\n"
      "FIRST ACTION every turn: call tool `set_editor_code` with the FULL script.\n"
      "NEVER put OpenSCAD in chat. NEVER reply with only OK / Done / Sure.\n"
      "After the tool succeeds, write 2–4 sentences describing parts and sizes "
      "(same language as the user).\n\n"
      "SYNTAX: modifiers wrap children (`color(\"red\") cube(10);`). End statements with `;`.\n"
      "Use variables for sizes. Use modules for parts. Use `$fn=32` on spheres/cylinders.\n\n"
      "QUALITY RULES (do not skip):\n"
      "1. Complex objects need MANY parts (body, head, limbs, details) — NEVER one sphere/cube only.\n"
      "2. Example animal: body sphere + head sphere + beak cube/cylinder + comb + 2 wings + "
      "2 legs + tail — each in its own module, then assemble with translate/rotate.\n"
      "3. Pick real sizes in mm (e.g. body_r=20; leg_h=15;).\n"
      "4. Parts must touch / connect correctly in 3D space.\n"
      "5. Follow this order every time: parameters → modules → assembly → `set_editor_code`.\n\n"
      "If editing existing code, call `get_editor_code` first.\n"
      "If a drawing image is attached: silhouettes are not solid blocks; use SECTION views for "
      "thickness; build shells with difference().\n";
  }

  return prompt;
}

#ifndef __EMSCRIPTEN__

#include "HTTPClient.h"
#include "AIClient.h"
#include "core/AIFreeAgents.h"
#include "platform/PlatformUtils.h"
#include "json/json.hpp"
#include <cmath>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <filesystem>

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

// Load the bundled OpenSCAD CAD workflow skill (skills/openscad-cad/). Adapted from
// earthtojake/text-to-cad (MIT). Result is cached after the first read; any failure
// yields an empty string because the skill is an enhancement, not a hard requirement.
static const std::string& loadCadSkill(bool compact)
{
  static std::string cachedFull;
  static std::string cachedCompact;
  static bool triedFull = false;
  static bool triedCompact = false;

  std::string& cache = compact ? cachedCompact : cachedFull;
  bool& tried = compact ? triedCompact : triedFull;
  if (tried) {
    return cache;
  }
  tried = true;

  try {
    const std::filesystem::path dir = PlatformUtils::resourcePath("skills");
    if (dir.empty()) {
      return cache;
    }
    const std::filesystem::path file =
      dir / "openscad-cad" / (compact ? "SKILL.compact.md" : "SKILL.md");
    std::ifstream in(file);
    if (!in.is_open()) {
      return cache;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    cache = ss.str();
  } catch (...) {
    // Leave the cache empty; proceed with the base prompt only.
  }
  return cache;
}

// Local/small models (Ollama-style) get the compact skill to conserve context.
static bool profileIsLocalModel(const AIProfileConfig& config)
{
  const std::string ep = toLowerCopy(config.endpoint);
  return ep.find("ollama") != std::string::npos || ep.find("localhost") != std::string::npos ||
         ep.find("127.0.0.1") != std::string::npos || ep.find(":11434") != std::string::npos;
}

// Append the bundled CAD workflow so the model follows a consistent
// plan → parameters → modules → apply → verify loop. Skipped when the prompt
// already contains the skill (e.g. a user pasted it into a custom prompt).
static void appendCadSkill(std::string& sys_prompt, bool compact)
{
  constexpr const char *kMarker = "# OpenSCAD CAD workflow";
  if (sys_prompt.find(kMarker) != std::string::npos) {
    return;
  }
  const std::string& skill = loadCadSkill(compact);
  if (skill.empty()) {
    return;
  }
  sys_prompt += "\n\n";
  sys_prompt += skill;
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
  auto run = std::make_shared<std::function<void(int, bool)>>();
  *run = [this, &history, on_chunk, on_error, on_complete, run](int tool_round, bool include_tools) {
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
    appendCadSkill(sys_prompt, profileIsLocalModel(config));

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

    auto on_complete_wrapper = [this, &history, on_chunk, on_error, on_complete, run,
                                tool_round](const std::vector<AIToolCall>& tool_calls) {
      if (tool_calls.empty()) {
        if (on_complete) {
          on_complete();
        }
        return;
      }

      constexpr int kMaxToolRounds = 6;
      if (tool_round >= kMaxToolRounds) {
        if (on_chunk) {
          on_chunk("\n(Stopped after too many tool rounds.)");
        }
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

      bool wrote_code = false;
      bool render_error = false;
      for (const auto& tc : tool_calls) {
        std::string result;
        if (impl->tool_executor) {
          result = impl->tool_executor(tc.name, tc.arguments);
        } else {
          result = "Error: No tool executor registered.";
        }
        if (tc.name == "set_editor_code") {
          wrote_code = true;
          // The executor renders the applied code and marks a failed/empty render so we
          // can grant the model a repair turn instead of ending on a broken model.
          if (result.find("[render-error]") != std::string::npos) {
            render_error = true;
          }
        }
        ChatMessage tool_msg;
        tool_msg.role = "tool";
        tool_msg.content = result;
        tool_msg.tool_call_id = tc.id;
        history.push_back(tool_msg);
      }

      // Code applied and the render succeeded: end the turn. Local models (Ollama)
      // often emit another broken tool JSON or "OK" if given a follow-up turn, which
      // the UI then surfaces as a parse error even though the editor already has the
      // correct script and a good render.
      if (wrote_code && !render_error) {
        if (on_chunk) {
          on_chunk("Applied the OpenSCAD model to the editor. Tell me what to change next.");
        }
        if (on_complete) {
          on_complete();
        }
        return;
      }

      // Code applied but the render failed / produced no geometry: let the model fix it,
      // bounded by kMaxRepairRounds so a stuck model cannot loop forever.
      if (wrote_code && render_error) {
        constexpr int kMaxRepairRounds = 2;
        if (tool_round >= kMaxRepairRounds) {
          if (on_chunk) {
            on_chunk("The render still reports problems after repair attempts. "
                     "See the console error log for details.");
          }
          if (on_complete) {
            on_complete();
          }
          return;
        }
        (*run)(tool_round + 1, true);
        return;
      }

      (*run)(tool_round + 1, true);
    };

    impl->ai_client->sendChatCompletionStream(config, ai_history, on_chunk, on_error,
                                              on_complete_wrapper, include_tools);
  };

  (*run)(0, true);
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
  appendCadSkill(sys_prompt, profileIsLocalModel(config));

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
