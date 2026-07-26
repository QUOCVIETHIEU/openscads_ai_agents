#include "core/AIFreeAgents.h"

#include "core/AIService.h"
#include "platform/PlatformUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace AIFreeAgents {

namespace {

std::string toLower(std::string s)
{
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string truncate(const std::string& s, size_t maxLen)
{
  if (s.size() <= maxLen) return s;
  return s.substr(0, maxLen) + "...";
}

std::string settingsPath()
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

bool loadSettingsJson(nlohmann::json& j, std::string& errorMsg)
{
  const std::string path = settingsPath();
  std::ifstream file(path);
  if (!file.is_open()) {
    j = nlohmann::json::object();
    ensurePresets(j);
    return true;
  }
  try {
    file >> j;
  } catch (const std::exception& e) {
    errorMsg = std::string("JSON parsing error: ") + e.what();
    return false;
  }
  ensurePresets(j);
  return true;
}

struct BuiltInProfile {
  const char *name;
  const char *endpoint;
  const char *model;
  const char *note;
};

// Keep in sync with AI Settings profile list.
constexpr int kProfileBundleVersion = 7;

const BuiltInProfile *builtInProfiles(size_t& count)
{
  // Cursor Agent CLI (cursor-agent -p). Cloud /v1/agents is NOT used here —
  // OpenSCAD speaks to the local CLI with --mode ask and applies code via
  // set_editor_code extraction.
  static const BuiltInProfile presets[] = {
    {"Gemini", "https://generativelanguage.googleapis.com/v1beta/openai", "gemini-2.0-flash",
     "Google Gemini (OpenAI-compatible endpoint)."},
    {"OpenAI", "https://api.openai.com/v1", "gpt-4o", "OpenAI Chat Completions API."},
    {"Claude", "https://api.anthropic.com/v1", "claude-sonnet-4-5",
     "Anthropic Claude. Use an OpenAI-compatible Claude gateway if the native API is not supported."},
    {"Cursor", "cursor://agent", "composer-2.5",
     "Cursor Agent CLI + OpenSCAD MCP tools (set_editor_code). Paste key from "
     "cursor.com/dashboard/integrations. Requires cursor-agent + python3."},
    {"Ollama", "http://localhost:11434/v1", "qwen2.5-coder:14b",
     "Local Ollama OpenAI-compatible server (usually no API key). Prefer qwen2.5-coder for OpenSCAD."},
  };
  count = sizeof(presets) / sizeof(presets[0]);
  return presets;
}

bool isBuiltInName(const std::string& name)
{
  size_t count = 0;
  const BuiltInProfile *presets = builtInProfiles(count);
  for (size_t i = 0; i < count; ++i) {
    if (name == presets[i].name) return true;
  }
  return false;
}

/*! Map legacy profile names onto a built-in name for key/config migration. */
std::string mapLegacyToBuiltIn(const std::string& name)
{
  const std::string lower = toLower(name);
  if (lower.find("gemini") != std::string::npos) return "Gemini";
  if (lower.find("openai") != std::string::npos || lower.find("gpt") != std::string::npos) {
    return "OpenAI";
  }
  if (lower.find("claude") != std::string::npos || lower.find("anthropic") != std::string::npos) {
    return "Claude";
  }
  if (lower.find("cursor") != std::string::npos) return "Cursor";
  if (lower.find("ollama") != std::string::npos) return "Ollama";
  return {};
}

nlohmann::json makeProfile(const BuiltInProfile& p)
{
  nlohmann::json profile = nlohmann::json::object();
  profile["endpoint"] = p.endpoint;
  profile["apiKey"] = "";
  nlohmann::json params = nlohmann::json::object();
  params["model"] = p.model;
  params["system_prompt"] = AIService::systemPromptForProfile(p.name);
  params["default_prompt"] = "";
  params["tier_note"] = p.note;
  profile["params"] = params;
  return profile;
}

void applyBuiltInDefaults(nlohmann::json& profile, const BuiltInProfile& p, bool forcePrompt)
{
  if (!profile.is_object()) {
    profile = makeProfile(p);
    return;
  }
  if (!profile.contains("endpoint") || !profile["endpoint"].is_string() ||
      profile["endpoint"].get<std::string>().empty()) {
    profile["endpoint"] = p.endpoint;
  }
  // Migrate leftover Cloud API URLs — they are not OpenAI chat/completions.
  if (std::string(p.name) == "Cursor") {
    const std::string ep = profile.value("endpoint", "");
    if (forcePrompt || ep.find("api.cursor.com") != std::string::npos ||
        ep.find("api2.cursor.sh") != std::string::npos ||
        ep.find("/v1/chat/completions") != std::string::npos) {
      profile["endpoint"] = p.endpoint;
    }
  }
  if (!profile.contains("apiKey") || !profile["apiKey"].is_string()) {
    profile["apiKey"] = "";
  }
  if (!profile.contains("params") || !profile["params"].is_object()) {
    profile["params"] = nlohmann::json::object();
  }
  auto& params = profile["params"];
  if (forcePrompt || !params.contains("model") || !params["model"].is_string() ||
      params["model"].get<std::string>().empty()) {
    params["model"] = p.model;
  }
  if (forcePrompt || !params.contains("system_prompt") || !params["system_prompt"].is_string() ||
      params["system_prompt"].get<std::string>().empty()) {
    params["system_prompt"] = AIService::systemPromptForProfile(p.name);
  }
  if (!params.contains("default_prompt")) {
    params["default_prompt"] = "";
  }
  params["tier_note"] = p.note;
}

}  // namespace

bool ensurePresets(nlohmann::json& settings)
{
  if (!settings.is_object()) {
    settings = nlohmann::json::object();
  }
  if (!settings.contains("profiles") || !settings["profiles"].is_object()) {
    settings["profiles"] = nlohmann::json::object();
  }
  auto& profiles = settings["profiles"];
  bool changed = false;

  size_t count = 0;
  const BuiltInProfile *presets = builtInProfiles(count);

  int installedBundle = 0;
  if (settings.contains("profileBundleVersion") &&
      settings["profileBundleVersion"].is_number_integer()) {
    installedBundle = settings["profileBundleVersion"].get<int>();
  }
  const bool refreshBundle = installedBundle < kProfileBundleVersion;

  // Capture legacy keys before pruning so we can migrate into built-ins.
  std::unordered_map<std::string, std::string> legacyKeys;
  for (auto it = profiles.begin(); it != profiles.end(); ++it) {
    if (!it.value().is_object()) continue;
    const std::string key = it.value().value("apiKey", "");
    if (key.empty()) continue;
    const std::string mapped = mapLegacyToBuiltIn(it.key());
    if (!mapped.empty() && !legacyKeys.count(mapped)) {
      legacyKeys[mapped] = key;
    }
  }

  // Ensure exactly the built-in set exists.
  for (size_t i = 0; i < count; ++i) {
    const auto& p = presets[i];
    if (!profiles.contains(p.name) || !profiles[p.name].is_object()) {
      profiles[p.name] = makeProfile(p);
      changed = true;
    } else if (refreshBundle) {
      applyBuiltInDefaults(profiles[p.name], p, /*forcePrompt=*/true);
      changed = true;
    }
    if (profiles[p.name].value("apiKey", "").empty()) {
      auto leg = legacyKeys.find(p.name);
      if (leg != legacyKeys.end() && !leg->second.empty()) {
        profiles[p.name]["apiKey"] = leg->second;
        changed = true;
      }
    }
  }

  // Drop everything else (OpenRouter Free, Groq, Default, NVIDIA, …).
  std::vector<std::string> toErase;
  for (auto it = profiles.begin(); it != profiles.end(); ++it) {
    if (!isBuiltInName(it.key())) {
      toErase.push_back(it.key());
    }
  }
  for (const auto& name : toErase) {
    profiles.erase(name);
    changed = true;
  }

  if (refreshBundle) {
    settings["profileBundleVersion"] = kProfileBundleVersion;
    changed = true;
  }

  if (!settings.contains("activeProfile") || !settings["activeProfile"].is_string() ||
      settings["activeProfile"].get<std::string>().empty() ||
      !profiles.contains(settings["activeProfile"].get<std::string>())) {
    settings["activeProfile"] = "Gemini";
    changed = true;
  }

  // MCP bridge defaults (enabled, automatic port).
  if (!settings.contains("mcp") || !settings["mcp"].is_object()) {
    settings["mcp"] = nlohmann::json::object({{"enabled", true}, {"port", 0}});
    changed = true;
  } else {
    auto& mcp = settings["mcp"];
    if (!mcp.contains("enabled") || !mcp["enabled"].is_boolean()) {
      mcp["enabled"] = true;
      changed = true;
    }
    if (!mcp.contains("port") || !mcp["port"].is_number_integer()) {
      mcp["port"] = 0;
      changed = true;
    }
  }

  return changed;
}

bool mcpEnabled()
{
  nlohmann::json j;
  std::string err;
  if (!loadSettingsJson(j, err)) return true;
  if (!j.contains("mcp") || !j["mcp"].is_object()) return true;
  return j["mcp"].value("enabled", true);
}

bool requiresApiKey(const std::string& profileName)
{
  return toLower(profileName).find("ollama") == std::string::npos;
}

std::string readProfileApiKey(const std::string& profileName)
{
  nlohmann::json j;
  std::string err;
  if (!loadSettingsJson(j, err)) return {};
  if (!j.contains("profiles") || !j["profiles"].is_object()) return {};
  if (!j["profiles"].contains(profileName) || !j["profiles"][profileName].is_object()) return {};
  return j["profiles"][profileName].value("apiKey", "");
}

std::string activeProfileName()
{
  nlohmann::json j;
  std::string err;
  if (!loadSettingsJson(j, err)) return {};
  if (!j.contains("activeProfile") || !j["activeProfile"].is_string()) return {};
  return j["activeProfile"].get<std::string>();
}

std::string activeModelName()
{
  nlohmann::json j;
  std::string err;
  if (!loadSettingsJson(j, err)) return {};
  if (!j.contains("activeProfile") || !j["activeProfile"].is_string()) return {};
  const std::string profileName = j["activeProfile"].get<std::string>();
  if (!j.contains("profiles") || !j["profiles"].is_object()) return profileName;
  if (!j["profiles"].contains(profileName) || !j["profiles"][profileName].is_object()) {
    return profileName;
  }
  const auto& profile = j["profiles"][profileName];
  if (profile.contains("params") && profile["params"].is_object()) {
    std::string model = profile["params"].value("model", "");
    if (model.rfind("models/", 0) == 0) {
      model = model.substr(7);
    }
    if (!model.empty()) return model;
  }
  return profileName;
}

bool isLimitError(int statusCode, const std::string& responseBody)
{
  if (statusCode == 429 || statusCode == 402) return true;
  const std::string lower = toLower(responseBody);
  static const char *kNeedles[] = {
    "rate limit",
    "rate_limit",
    "ratelimit",
    "quota",
    "resource_exhausted",
    "resource exhausted",
    "too many requests",
    "exceeded your current quota",
    "insufficient_quota",
    "payment_required",
    "payment required",
    "daily limit",
    "monthly limit",
    "tokens per day",
    "rpm limit",
    "rpd limit",
    "free tier",
    "usage limit",
  };
  for (const char *n : kNeedles) {
    if (lower.find(n) != std::string::npos) return true;
  }
  return false;
}

bool isAuthError(int statusCode, const std::string& responseBody)
{
  if (statusCode == 401 || statusCode == 403) return true;
  const std::string lower = toLower(responseBody);
  return lower.find("invalid api key") != std::string::npos ||
         lower.find("incorrect api key") != std::string::npos ||
         lower.find("api key not valid") != std::string::npos ||
         lower.find("unauthorized") != std::string::npos ||
         lower.find("permission denied") != std::string::npos ||
         lower.find("missing authentication") != std::string::npos ||
         lower.find("no api key") != std::string::npos;
}

std::string formatHttpError(int statusCode, const std::string& responseBody)
{
  const std::string detail = truncate(responseBody, 500);
  const std::string lower = toLower(responseBody);

  if (statusCode == 404 &&
      (lower.find("chat/completions") != std::string::npos ||
       lower.find("route post:/v1/chat/completions") != std::string::npos)) {
    return std::string(
             "This endpoint does not support OpenAI chat completions.\n"
             "OpenSCAD Cad Agent needs POST /v1/chat/completions "
             "(Gemini, OpenAI, Claude gateway, or Ollama).\n"
             "Cursor API keys use /v1/agents instead and cannot be used here — "
             "switch profile in AI Settings.\n\n"
             "HTTP ") +
           std::to_string(statusCode) + ": " + detail;
  }

  if (isLimitError(statusCode, responseBody)) {
    return std::string(
             "API limit reached — this profile is temporarily limited.\n"
             "Wait a bit and retry, or open AI Settings and switch profile / API key.\n\n"
             "HTTP ") +
           std::to_string(statusCode) + ": " + detail;
  }

  if (isAuthError(statusCode, responseBody)) {
    return std::string(
             "API key missing or invalid.\n"
             "Open AI Settings and paste a valid API key for the active profile.\n\n"
             "HTTP ") +
           std::to_string(statusCode) + ": " + detail;
  }

  if (statusCode == 0) {
    return std::string("Network error talking to the AI provider.\n") + detail;
  }

  return "HTTP status " + std::to_string(statusCode) + ": " + detail;
}

}  // namespace AIFreeAgents
