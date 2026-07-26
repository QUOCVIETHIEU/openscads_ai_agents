#ifndef OPENSCAD_AICLIENT_H
#define OPENSCAD_AICLIENT_H

#include <cctype>
#include <sstream>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include "json/json.hpp"

struct AIToolCall {
  std::string id;
  std::string type;
  std::string name;
  std::string arguments;
  // Gemini OpenAI-compat: extra_content.google.thought_signature must be
  // echoed on subsequent turns with tool calls (Gemini 3 requires this).
  nlohmann::json extra_content;
};

inline bool toolCallHasThoughtSignature(const AIToolCall& tc)
{
  auto hasSig = [](const nlohmann::json& node) -> bool {
    if (!node.is_object()) return false;
    if (node.contains("thought_signature") && node["thought_signature"].is_string() &&
        !node["thought_signature"].get<std::string>().empty()) {
      return true;
    }
    if (node.contains("thoughtSignature") && node["thoughtSignature"].is_string() &&
        !node["thoughtSignature"].get<std::string>().empty()) {
      return true;
    }
    return false;
  };

  if (hasSig(tc.extra_content)) return true;
  if (tc.extra_content.is_object() && tc.extra_content.contains("google") &&
      hasSig(tc.extra_content["google"])) {
    return true;
  }
  return false;
}

inline void applyThoughtSignatureValue(AIToolCall& tc, const std::string& signature)
{
  if (signature.empty()) return;
  if (!tc.extra_content.is_object()) {
    tc.extra_content = nlohmann::json::object();
  }
  if (!tc.extra_content.contains("google") || !tc.extra_content["google"].is_object()) {
    tc.extra_content["google"] = nlohmann::json::object();
  }
  tc.extra_content["google"]["thought_signature"] = signature;
}

inline void ensureThoughtSignature(AIToolCall& tc)
{
  if (!toolCallHasThoughtSignature(tc)) {
    // Official Gemini escape hatch when the original signature was not captured.
    applyThoughtSignatureValue(tc, "skip_thought_signature_validator");
  }
}

inline void extractThoughtSignatureFields(const nlohmann::json& tc, AIToolCall& out)
{
  if (tc.contains("extra_content") && tc["extra_content"].is_object()) {
    if (!out.extra_content.is_object()) {
      out.extra_content = nlohmann::json::object();
    }
    for (auto it = tc["extra_content"].begin(); it != tc["extra_content"].end(); ++it) {
      out.extra_content[it.key()] = it.value();
    }
  }
  // Some responses put the signature on the tool_call root.
  if (tc.contains("thought_signature") && tc["thought_signature"].is_string()) {
    applyThoughtSignatureValue(out, tc["thought_signature"].get<std::string>());
  } else if (tc.contains("thoughtSignature") && tc["thoughtSignature"].is_string()) {
    applyThoughtSignatureValue(out, tc["thoughtSignature"].get<std::string>());
  }
}

inline void parseAIToolCall(const nlohmann::json& tc, AIToolCall& out)
{
  if (tc.contains("id") && tc["id"].is_string()) {
    out.id = tc["id"].get<std::string>();
  }
  if (tc.contains("type") && tc["type"].is_string()) {
    out.type = tc["type"].get<std::string>();
  }
  if (tc.contains("function") && tc["function"].is_object()) {
    const auto& fn = tc["function"];
    if (fn.contains("name") && fn["name"].is_string()) {
      out.name = fn["name"].get<std::string>();
    }
    if (fn.contains("arguments")) {
      if (fn["arguments"].is_string()) {
        out.arguments = fn["arguments"].get<std::string>();
      } else {
        out.arguments = fn["arguments"].dump();
      }
    }
  }
  extractThoughtSignatureFields(tc, out);
}

inline nlohmann::json serializeAIToolCall(const AIToolCall& tc, bool ensureGeminiSignature = false)
{
  AIToolCall copy = tc;
  if (ensureGeminiSignature) {
    ensureThoughtSignature(copy);
  }

  nlohmann::json t = nlohmann::json::object();
  t["id"] = copy.id;
  t["type"] = copy.type.empty() ? "function" : copy.type;
  nlohmann::json fn = nlohmann::json::object();
  fn["name"] = copy.name;
  fn["arguments"] = copy.arguments;
  t["function"] = fn;
  if (!copy.extra_content.is_null() && !copy.extra_content.empty()) {
    t["extra_content"] = copy.extra_content;
  }
  return t;
}

/*! Some local OpenAI-compat servers (notably Ollama + several coder models) emit tool
 *  invocations as JSON in `message.content` instead of `message.tool_calls`. Recover them. */
inline std::string stripMarkdownCodeFence(std::string s)
{
  auto trim = [](std::string& x) {
    while (!x.empty() && std::isspace(static_cast<unsigned char>(x.front()))) x.erase(0, 1);
    while (!x.empty() && std::isspace(static_cast<unsigned char>(x.back()))) x.pop_back();
  };
  trim(s);
  if (s.rfind("```", 0) == 0) {
    const auto nl = s.find('\n');
    if (nl != std::string::npos) s = s.substr(nl + 1);
    const auto end = s.rfind("```");
    if (end != std::string::npos) s = s.substr(0, end);
    trim(s);
  }
  return s;
}

/*! Local models often emit invalid JSON escapes like \' inside strings. Soften those. */
inline std::string softenInvalidJsonEscapes(const std::string& s)
{
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '\'') {
      out.push_back('\'');
      ++i;
      continue;
    }
    out.push_back(s[i]);
  }
  return out;
}

/*! Escape raw control characters that appear inside JSON strings (invalid but common). */
inline std::string escapeRawControlsInJsonStrings(const std::string& s)
{
  std::string out;
  out.reserve(s.size() + 16);
  bool in_str = false;
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (!in_str) {
      if (c == '"') in_str = true;
      out.push_back(c);
      continue;
    }
    if (c == '\\' && i + 1 < s.size()) {
      out.push_back(c);
      out.push_back(s[++i]);
      continue;
    }
    if (c == '"') {
      in_str = false;
      out.push_back(c);
      continue;
    }
    if (c == '\n') {
      out += "\\n";
      continue;
    }
    if (c == '\r') {
      out += "\\r";
      continue;
    }
    if (c == '\t') {
      out += "\\t";
      continue;
    }
    out.push_back(c);
  }
  return out;
}

inline bool parseFirstJsonValue(const std::string& text, nlohmann::json& out)
{
  try {
    const std::string prepared = escapeRawControlsInJsonStrings(softenInvalidJsonEscapes(text));
    std::stringstream ss(prepared);
    ss >> std::ws;
    if (ss.eof()) return false;
    ss >> out;
    return !out.is_discarded();
  } catch (...) {
    return false;
  }
}

inline std::string normalizeToolArgumentsJson(nlohmann::json args)
{
  // Ollama sometimes nests {"code":{"value":"..."}} — flatten stringy leaves one level.
  if (args.is_object()) {
    for (auto it = args.begin(); it != args.end(); ++it) {
      if (it.value().is_object() && it.value().contains("value") &&
          (it.value()["value"].is_string() || it.value()["value"].is_number() ||
           it.value()["value"].is_boolean())) {
        args[it.key()] = it.value()["value"];
      }
    }
  }
  return args.is_string() ? args.get<std::string>() : args.dump();
}

inline bool parseOneEmbeddedToolCall(const nlohmann::json& node, AIToolCall& out, size_t index)
{
  std::string name;
  nlohmann::json args = nlohmann::json::object();

  if (node.contains("function") && node["function"].is_object()) {
    const auto& fn = node["function"];
    if (fn.contains("name") && fn["name"].is_string()) name = fn["name"].get<std::string>();
    if (fn.contains("arguments")) {
      if (fn["arguments"].is_string()) {
        try {
          args = nlohmann::json::parse(softenInvalidJsonEscapes(fn["arguments"].get<std::string>()));
        } catch (...) {
          args = nlohmann::json::object();
          args["raw"] = fn["arguments"];
        }
      } else {
        args = fn["arguments"];
      }
    }
  } else if (node.is_object()) {
    if (node.contains("name") && node["name"].is_string()) {
      name = node["name"].get<std::string>();
    } else if (node.contains("function_name") && node["function_name"].is_string()) {
      name = node["function_name"].get<std::string>();
    }
    if (node.contains("arguments")) {
      if (node["arguments"].is_string()) {
        try {
          args = nlohmann::json::parse(softenInvalidJsonEscapes(node["arguments"].get<std::string>()));
        } catch (...) {
          args = nlohmann::json::object();
          args["raw"] = node["arguments"];
        }
      } else if (node["arguments"].is_object() || node["arguments"].is_array()) {
        args = node["arguments"];
      }
    } else if (node.contains("parameters")) {
      args = node["parameters"];
    }
  } else {
    return false;
  }

  if (name.empty()) return false;
  // Only accept known OpenSCAD tools (avoid treating arbitrary JSON as a tool).
  if (name != "set_editor_code" && name != "get_editor_code" && name != "trigger_preview" &&
      name != "trigger_render" && name != "trigger_build") {
    return false;
  }

  out = {};
  out.id = node.value("id", "call_embedded_" + std::to_string(index));
  out.type = "function";
  out.name = name;
  out.arguments = normalizeToolArgumentsJson(args);
  return true;
}

inline bool ingestParsedToolJson(const nlohmann::json& j, std::vector<AIToolCall>& out)
{
  if (j.is_array()) {
    for (size_t i = 0; i < j.size(); ++i) {
      AIToolCall tc;
      if (j[i].is_object() && parseOneEmbeddedToolCall(j[i], tc, i)) out.push_back(tc);
    }
    return !out.empty();
  }
  if (j.is_object()) {
    if (j.contains("tool_calls") && j["tool_calls"].is_array()) {
      for (size_t i = 0; i < j["tool_calls"].size(); ++i) {
        AIToolCall tc;
        if (j["tool_calls"][i].is_object() && parseOneEmbeddedToolCall(j["tool_calls"][i], tc, i)) {
          out.push_back(tc);
        }
      }
      return !out.empty();
    }
    AIToolCall tc;
    if (parseOneEmbeddedToolCall(j, tc, 0)) {
      out.push_back(tc);
      return true;
    }
  }
  return false;
}

inline bool contentLooksLikeEmbeddedToolCall(const std::string& content)
{
  const std::string s = stripMarkdownCodeFence(content);
  return s.find("set_editor_code") != std::string::npos &&
         (s.find("arguments") != std::string::npos || s.find("parameters") != std::string::npos);
}

/*! Last-resort extraction when models emit almost-JSON (raw newlines, bad escapes, truncation). */
inline bool salvageSetEditorCode(const std::string& content, AIToolCall& out)
{
  const std::string s = softenInvalidJsonEscapes(stripMarkdownCodeFence(content));
  if (s.find("set_editor_code") == std::string::npos) return false;

  const std::string key = "\"code\"";
  size_t pos = 0;
  while ((pos = s.find(key, pos)) != std::string::npos) {
    size_t i = pos + key.size();
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i >= s.size() || s[i] != ':') {
      pos += key.size();
      continue;
    }
    ++i;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i >= s.size() || s[i] != '"') {
      pos += key.size();
      continue;
    }

    std::string code;
    code.reserve(256);
    bool closed = false;
    for (size_t j = i + 1; j < s.size(); ++j) {
      const char c = s[j];
      if (c == '\\' && j + 1 < s.size()) {
        const char n = s[j + 1];
        switch (n) {
        case '"':
        case '\\':
        case '/': code.push_back(n); break;
        case 'n': code.push_back('\n'); break;
        case 'r': code.push_back('\r'); break;
        case 't': code.push_back('\t'); break;
        case '\'': code.push_back('\''); break;
        default: code.push_back(n); break;
        }
        ++j;
        continue;
      }
      if (c == '"') {
        closed = true;
        break;
      }
      code.push_back(c);
    }

    if (!closed) {
      // Truncated JSON: drop a trailing wrapper like `\n  }\n}` if present.
      while (!code.empty() &&
             (code.back() == '}' || code.back() == ',' || code.back() == '"' ||
              std::isspace(static_cast<unsigned char>(code.back())))) {
        // Keep meaningful closing braces that belong to OpenSCAD blocks.
        if (code.back() == '}') {
          const size_t opens = static_cast<size_t>(std::count(code.begin(), code.end(), '{'));
          const size_t closes = static_cast<size_t>(std::count(code.begin(), code.end(), '}'));
          if (closes > opens) {
            code.pop_back();
            continue;
          }
          break;
        }
        code.pop_back();
      }
    }

    const bool looksLikeScad =
      code.find(';') != std::string::npos || code.find("module") != std::string::npos ||
      code.find("sphere") != std::string::npos || code.find("cube") != std::string::npos ||
      code.find("cylinder") != std::string::npos || code.find("polyhedron") != std::string::npos ||
      code.find("linear_extrude") != std::string::npos || code.find("difference") != std::string::npos ||
      code.find("union") != std::string::npos || code.find("hull") != std::string::npos;
    if (!looksLikeScad) {
      pos += key.size();
      continue;
    }

    nlohmann::json args = nlohmann::json::object();
    args["code"] = code;
    out = {};
    out.id = "call_embedded_salvage_0";
    out.type = "function";
    out.name = "set_editor_code";
    out.arguments = args.dump();
    return true;
  }
  return false;
}

inline std::vector<AIToolCall> tryParseEmbeddedToolCalls(const std::string& content)
{
  std::vector<AIToolCall> out;
  if (content.empty()) return out;
  std::string s = stripMarkdownCodeFence(content);

  nlohmann::json j;
  if (parseFirstJsonValue(s, j) && ingestParsedToolJson(j, out)) {
    return out;
  }

  // Fallback: first {...} through last } (handles leading prose).
  const auto start = s.find('{');
  const auto end = s.rfind('}');
  if (start != std::string::npos && end != std::string::npos && end > start) {
    out.clear();
    if (parseFirstJsonValue(s.substr(start, end - start + 1), j) && ingestParsedToolJson(j, out)) {
      return out;
    }
  }

  // Salvage broken / truncated tool JSON (common with local models).
  AIToolCall salvaged;
  if (salvageSetEditorCode(content, salvaged)) {
    out.push_back(salvaged);
  }
  return out;
}

inline bool isGeminiOpenAIEndpoint(const std::string& endpoint)
{
  return endpoint.find("generativelanguage.googleapis.com") != std::string::npos ||
         endpoint.find("aiplatform.googleapis.com") != std::string::npos;
}

struct AIChatMessage {
  std::string role;
  std::string content;
  std::string tool_call_id;
  std::vector<AIToolCall> tool_calls;
  // OpenAI-compat data URLs: "data:image/jpeg;base64,..."
  std::vector<std::string> images;
};

struct AIProfileConfig {
  std::string endpoint;
  std::string apiKey;
  std::string model;
  nlohmann::json parameters;
};

class HTTPClient;

class AIClient
{
public:
  using ChunkCallback = std::function<void(const std::string& chunk)>;
  using ResponseCallback =
    std::function<void(const std::string& response, const std::vector<AIToolCall>& tool_calls)>;
  using ErrorCallback = std::function<void(const std::string& error_msg)>;
  using CompleteCallback = std::function<void(const std::vector<AIToolCall>& tool_calls)>;

  AIClient(std::shared_ptr<HTTPClient> httpClient);
  ~AIClient();

  // Prevent copy, allow move
  AIClient(const AIClient&) = delete;
  AIClient& operator=(const AIClient&) = delete;
  AIClient(AIClient&&) noexcept;
  AIClient& operator=(AIClient&&) noexcept;

  // Asynchronous OpenAI-compatible POST request
  void sendChatCompletion(const AIProfileConfig& config, const std::vector<AIChatMessage>& history,
                          ResponseCallback on_response, ErrorCallback on_error);

  // Asynchronous OpenAI-compatible POST request with streaming response.
  // include_tools=false is used for the post-tool chat summary turn (Ollama often
  // re-emits fake tool JSON when tools remain advertised).
  void sendChatCompletionStream(const AIProfileConfig& config, const std::vector<AIChatMessage>& history,
                                ChunkCallback on_chunk, ErrorCallback on_error,
                                CompleteCallback on_complete, bool include_tools = true);

  // Cancel all pending HTTP requests
  void cancelPendingRequests();

private:
  class Impl;
  std::unique_ptr<Impl> impl;
};

#endif  // OPENSCAD_AICLIENT_H
