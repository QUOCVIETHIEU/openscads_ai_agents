#pragma once

#include <string>
#include <vector>

#include "json/json.hpp"

namespace AIFreeAgents {

/*! Seed built-in free agent profiles if missing (does not overwrite user edits). */
bool ensurePresets(nlohmann::json& settings);

/*! Built-in free agent profile names shown in the chat composer picker. */
std::vector<std::string> freePresetNames();

/*! False for Ollama Local (and similar); true when a provider key is required. */
bool requiresApiKey(const std::string& profileName);

/*! Read apiKey for a named profile from ai_settings.json (empty if missing). */
std::string readProfileApiKey(const std::string& profileName);

/*! Persist apiKey for a named profile. Returns false on I/O / missing profile. */
bool writeProfileApiKey(const std::string& profileName, const std::string& apiKey, std::string& errorMsg);

/*! Current Settings activeProfile name (empty if unavailable). */
std::string activeProfileName();

/*! Short signup hint / URL for free agents (empty if unknown). */
std::string apiKeySignupHint(const std::string& profileName);

/*! True when HTTP status/body indicates free-tier / rate / quota exhaustion. */
bool isLimitError(int statusCode, const std::string& responseBody);

/*! True when status/body indicates missing/invalid API key. */
bool isAuthError(int statusCode, const std::string& responseBody);

/*! Turn raw provider errors into a clear user-facing message (limit / auth / generic). */
std::string formatHttpError(int statusCode, const std::string& responseBody);

}  // namespace AIFreeAgents
