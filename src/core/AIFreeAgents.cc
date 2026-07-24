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

nlohmann::json makeEmptyDefaultProfile()
{
  nlohmann::json profile = nlohmann::json::object();
  profile["endpoint"] = "https://generativelanguage.googleapis.com/v1beta/openai";
  profile["apiKey"] = "";
  nlohmann::json params = nlohmann::json::object();
  params["model"] = "gemini-3.1-flash-lite";
  params["system_prompt"] = AIService::defaultSystemPrompt();
  params["default_prompt"] = "";
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

  if (!profiles.contains("Default") || !profiles["Default"].is_object()) {
    profiles["Default"] = makeEmptyDefaultProfile();
    changed = true;
  }

  if (!settings.contains("activeProfile") || !settings["activeProfile"].is_string() ||
      settings["activeProfile"].get<std::string>().empty() ||
      !profiles.contains(settings["activeProfile"].get<std::string>())) {
    settings["activeProfile"] = profiles.contains("Default") ? "Default"
                                                             : profiles.begin().key();
    changed = true;
  }

  return changed;
}

bool requiresApiKey(const std::string& /*profileName*/)
{
  return true;
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
