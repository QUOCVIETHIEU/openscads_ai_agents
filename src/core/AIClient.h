#ifndef OPENSCAD_AICLIENT_H
#define OPENSCAD_AICLIENT_H

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

  // Asynchronous OpenAI-compatible POST request with streaming response
  void sendChatCompletionStream(const AIProfileConfig& config, const std::vector<AIChatMessage>& history,
                                ChunkCallback on_chunk, ErrorCallback on_error,
                                CompleteCallback on_complete);

  // Cancel all pending HTTP requests
  void cancelPendingRequests();

private:
  class Impl;
  std::unique_ptr<Impl> impl;
};

#endif  // OPENSCAD_AICLIENT_H
