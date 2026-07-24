#pragma once

#include <string>

#include "json/json.hpp"

namespace AIFreeAgents {

/*! Ensure a Default profile + activeProfile exist (does not overwrite user edits). */
bool ensurePresets(nlohmann::json& settings);

/*! False for local providers that need no key; true when an API key is required. */
bool requiresApiKey(const std::string& profileName);

/*! Read apiKey for a named profile from ai_settings.json (empty if missing). */
std::string readProfileApiKey(const std::string& profileName);

/*! Current Settings activeProfile name (empty if unavailable). */
std::string activeProfileName();

/*! Model id from the active profile params (falls back to profile name). */
std::string activeModelName();

/*! True when HTTP status/body indicates rate / quota exhaustion. */
bool isLimitError(int statusCode, const std::string& responseBody);

/*! True when status/body indicates missing/invalid API key. */
bool isAuthError(int statusCode, const std::string& responseBody);

/*! Turn raw provider errors into a clear user-facing message (limit / auth / generic). */
std::string formatHttpError(int statusCode, const std::string& responseBody);

}  // namespace AIFreeAgents
