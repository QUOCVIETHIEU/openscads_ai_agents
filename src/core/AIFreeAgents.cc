#include "core/AIFreeAgents.h"

#include "core/AIService.h"
#include "platform/PlatformUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>

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

bool saveSettingsJson(const nlohmann::json& j, std::string& errorMsg)
{
  const std::string path = settingsPath();
  std::ofstream out(path);
  if (!out.is_open()) {
    errorMsg = "Could not write ai_settings.json";
    return false;
  }
  out << j.dump(4);
  return true;
}

nlohmann::json makePreset(const char *endpoint, const char *model, const char *tierNote)
{
  nlohmann::json profile = nlohmann::json::object();
  profile["endpoint"] = endpoint;
  profile["apiKey"] = "";
  nlohmann::json params = nlohmann::json::object();
  params["model"] = model;
  params["system_prompt"] = AIService::defaultSystemPrompt();
  params["default_prompt"] = "";
  params["tier"] = "free";
  params["tier_note"] = tierNote;
  profile["params"] = params;
  return profile;
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

  struct Preset {
    const char *name;
    const char *endpoint;
    const char *model;
    const char *note;
  };

  // Official free tiers (user pastes a free key once — except Ollama).
  const Preset presets[] = {
    {"Gemini Free", "https://generativelanguage.googleapis.com/v1beta/openai", "gemini-2.0-flash",
     "Free via Google AI Studio (aistudio.google.com). Rate-limited."},
    {"Groq Free", "https://api.groq.com/openai/v1", "llama-3.3-70b-versatile",
     "Free via console.groq.com. Fast open models; rate-limited."},
    {"OpenRouter Free", "https://openrouter.ai/api/v1", "openrouter/free",
     "Free via openrouter.ai — auto-picks a free model. Rate-limited."},
    {"Ollama Local", "http://localhost:11434/v1", "llama3.2",
     "Fully local — no API key. Install Ollama and pull a model first."},
  };

  for (const auto& p : presets) {
    if (profiles.contains(p.name)) continue;
    profiles[p.name] = makePreset(p.endpoint, p.model, p.note);
    changed = true;
  }

  // Keep a Default alias for first-run installs that already expect it.
  if (!profiles.contains("Default")) {
    profiles["Default"] = makePreset("https://generativelanguage.googleapis.com/v1beta/openai",
                                     "gemini-2.0-flash",
                                     "Alias of Gemini Free. Paste a free Google AI Studio key.");
    changed = true;
  }

  if (!settings.contains("activeProfile") || !settings["activeProfile"].is_string() ||
      settings["activeProfile"].get<std::string>().empty() ||
      !profiles.contains(settings["activeProfile"].get<std::string>())) {
    settings["activeProfile"] = profiles.contains("Gemini Free") ? "Gemini Free" : "Default";
    changed = true;
  }

  return changed;
}

std::vector<std::string> freePresetNames()
{
  return {"Gemini Free", "Groq Free", "OpenRouter Free", "Ollama Local"};
}

bool requiresApiKey(const std::string& profileName)
{
  return profileName != "Ollama Local";
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

bool writeProfileApiKey(const std::string& profileName, const std::string& apiKey, std::string& errorMsg)
{
  nlohmann::json j;
  if (!loadSettingsJson(j, errorMsg)) return false;
  if (!j.contains("profiles") || !j["profiles"].is_object() || !j["profiles"].contains(profileName) ||
      !j["profiles"][profileName].is_object()) {
    errorMsg = "Profile '" + profileName + "' was not found.";
    return false;
  }
  j["profiles"][profileName]["apiKey"] = apiKey;
  return saveSettingsJson(j, errorMsg);
}

std::string activeProfileName()
{
  nlohmann::json j;
  std::string err;
  if (!loadSettingsJson(j, err)) return {};
  if (!j.contains("activeProfile") || !j["activeProfile"].is_string()) return {};
  return j["activeProfile"].get<std::string>();
}

std::string apiKeySignupHint(const std::string& profileName)
{
  if (profileName.find("Groq") != std::string::npos) {
    return "Create a free key at https://console.groq.com/keys";
  }
  if (profileName.find("Gemini") != std::string::npos) {
    return "Create a free key at https://aistudio.google.com/apikey";
  }
  if (profileName.find("OpenRouter") != std::string::npos) {
    return "Create a free key at https://openrouter.ai/keys";
  }
  return "Paste the provider API key in AI Settings for this agent.";
}

bool isLimitError(int statusCode, const std::string& responseBody)
{
  if (statusCode == 429) return true;
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

  if (isLimitError(statusCode, responseBody)) {
    return std::string(
             "Free limit reached — this free agent is temporarily limited.\n"
             "Wait a bit and retry, switch to another free agent (Groq / OpenRouter / Ollama), "
             "or add/upgrade your API key in AI Settings.\n\n"
             "HTTP ") +
           std::to_string(statusCode) + ": " + detail;
  }

  if (isAuthError(statusCode, responseBody)) {
    return std::string(
             "API key missing or invalid for this agent.\n"
             "Agents menu → Open AI Settings… → select the agent → paste its API key.\n"
             "Free keys: console.groq.com/keys · aistudio.google.com/apikey · openrouter.ai/keys\n"
             "Ollama Local needs no key.\n\n"
             "HTTP ") +
           std::to_string(statusCode) + ": " + detail;
  }

  if (statusCode == 0) {
    return std::string("Network error talking to the AI provider.\n") + detail;
  }

  return "HTTP status " + std::to_string(statusCode) + ": " + detail;
}

}  // namespace AIFreeAgents
