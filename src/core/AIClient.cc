#include "AIClient.h"
#include "HTTPClient.h"
#include "core/AIFreeAgents.h"
#include "core/CursorAgentBackend.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <sstream>
#include <unordered_set>

class SSEParser
{
public:
  using TextCallback = std::function<void(const std::string& text)>;

  SSEParser(TextCallback callback) : callback_(std::move(callback)) {}

  void feed(const std::string& data)
  {
    buffer_ += data;
    size_t newline;
    while ((newline = buffer_.find('\n')) != std::string::npos) {
      std::string line = buffer_.substr(0, newline);
      buffer_ = buffer_.substr(newline + 1);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      processLine(line);
    }
  }

  void flush()
  {
    if (!buffer_.empty()) {
      processLine(buffer_);
      buffer_.clear();
    }
  }

  const std::vector<AIToolCall>& toolCalls() const { return tool_calls_; }

  void finalizeToolCalls()
  {
    if (pending_extra_content_.is_object() && !tool_calls_.empty()) {
      if (!toolCallHasThoughtSignature(tool_calls_.front())) {
        extractThoughtSignatureFields(nlohmann::json{{"extra_content", pending_extra_content_}},
                                      tool_calls_.front());
      }
    }
    // Drop trailing empty placeholders from sparse index assignment.
    while (!tool_calls_.empty() && tool_calls_.back().id.empty() && tool_calls_.back().name.empty() &&
           tool_calls_.back().arguments.empty()) {
      tool_calls_.pop_back();
    }
    // Ollama / some local models put tool invocations in content text, not tool_calls.
    if (tool_calls_.empty() && !content_accum_.empty()) {
      tool_calls_ = tryParseEmbeddedToolCalls(content_accum_);
    }
  }

private:
  std::string buffer_;
  TextCallback callback_;
  std::vector<AIToolCall> tool_calls_;
  std::string content_accum_;
  nlohmann::json pending_extra_content_;

  void processLine(const std::string& line)
  {
    std::string content = line;
    if (line.rfind("data: ", 0) == 0) {
      content = line.substr(6);
    }

    while (!content.empty() && std::isspace(static_cast<unsigned char>(content.front()))) {
      content.erase(0, 1);
    }
    while (!content.empty() && std::isspace(static_cast<unsigned char>(content.back()))) {
      content.pop_back();
    }

    if (content.empty()) {
      return;
    }

    if (content == "[DONE]") {
      return;
    }

    try {
      auto json = nlohmann::json::parse(content);
      bool matched = false;

      // OpenAI-compatible /v1/chat/completions format (content and reasoning_content)
      if (json.contains("choices") && json["choices"].is_array() && !json["choices"].empty()) {
        auto& choice = json["choices"][0];
        if (choice.contains("delta") && choice["delta"].is_object()) {
          auto& delta = choice["delta"];
          if (delta.contains("content") && !delta["content"].is_null()) {
            std::string text = delta["content"].is_string() ? delta["content"].get<std::string>()
                                                           : delta["content"].dump();
            if (!text.empty()) {
              content_accum_ += text;
              callback_(text);
              matched = true;
            }
          }
          if (delta.contains("reasoning_content")) {
            std::string text = delta["reasoning_content"].get<std::string>();
            if (!text.empty()) {
              callback_(text);
              matched = true;
            }
          }
          if (delta.contains("extra_content") && delta["extra_content"].is_object()) {
            if (!pending_extra_content_.is_object()) {
              pending_extra_content_ = nlohmann::json::object();
            }
            for (auto it = delta["extra_content"].begin(); it != delta["extra_content"].end(); ++it) {
              pending_extra_content_[it.key()] = it.value();
            }
          }
          if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
            for (auto& tc : delta["tool_calls"]) {
              if (!tc.is_object()) continue;

              size_t index = 0;
              if (tc.contains("index") && tc["index"].is_number_integer()) {
                index = tc["index"].get<size_t>();
              } else if (tc.contains("id") && tc["id"].is_string()) {
                const std::string id = tc["id"].get<std::string>();
                auto it = std::find_if(tool_calls_.begin(), tool_calls_.end(),
                                       [&id](const AIToolCall& existing) { return existing.id == id; });
                if (it != tool_calls_.end()) {
                  index = static_cast<size_t>(std::distance(tool_calls_.begin(), it));
                } else if (!tool_calls_.empty() && tool_calls_.back().id.empty()) {
                  index = tool_calls_.size() - 1;
                } else {
                  index = tool_calls_.size();
                }
              } else if (!tool_calls_.empty()) {
                index = tool_calls_.size() - 1;
              }

              if (tool_calls_.size() <= index) {
                tool_calls_.resize(index + 1);
              }
              auto& dest = tool_calls_[index];
              if (tc.contains("id") && tc["id"].is_string()) {
                dest.id = tc["id"].get<std::string>();
              }
              if (tc.contains("type") && tc["type"].is_string()) {
                dest.type = tc["type"].get<std::string>();
              }
              if (tc.contains("function") && tc["function"].is_object()) {
                auto& fn = tc["function"];
                if (fn.contains("name") && fn["name"].is_string()) {
                  dest.name = fn["name"].get<std::string>();
                }
                if (fn.contains("arguments")) {
                  if (fn["arguments"].is_string()) {
                    dest.arguments += fn["arguments"].get<std::string>();
                  } else {
                    dest.arguments += fn["arguments"].dump();
                  }
                }
              }
              extractThoughtSignatureFields(tc, dest);
            }
          }
        }
      }

      // Ollama /api/chat format (content and thinking)
      if (!matched && json.contains("message") && json["message"].is_object()) {
        auto& message = json["message"];
        if (message.contains("content") && !message["content"].is_null()) {
          std::string text = message["content"].is_string() ? message["content"].get<std::string>()
                                                           : message["content"].dump();
          if (!text.empty()) {
            content_accum_ += text;
            callback_(text);
            matched = true;
          }
        }
        if (message.contains("thinking")) {
          std::string text = message["thinking"].get<std::string>();
          if (!text.empty()) {
            callback_(text);
            matched = true;
          }
        }
        if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
          for (auto& tc : message["tool_calls"]) {
            AIToolCall tool_call;
            parseAIToolCall(tc, tool_call);
            tool_calls_.push_back(tool_call);
          }
        }
      }

      // Ollama /api/generate response format
      if (!matched && json.contains("response")) {
        std::string text = json["response"].get<std::string>();
        if (!text.empty()) {
          callback_(text);
          matched = true;
        }
      }

      // Ollama /api/generate thinking format
      if (!matched && json.contains("thinking")) {
        std::string text = json["thinking"].get<std::string>();
        if (!text.empty()) {
          callback_(text);
          matched = true;
        }
      }
    } catch (...) {
      // Ignore parsing errors on comments or partial/malformed JSON
    }
  }
};

class AIClient::Impl
{
public:
  std::shared_ptr<HTTPClient> http_client;
  std::shared_ptr<std::atomic<bool>> cancel_flag = std::make_shared<std::atomic<bool>>(false);
  std::shared_ptr<std::atomic<uint64_t>> generation = std::make_shared<std::atomic<uint64_t>>(0);

  Impl(std::shared_ptr<HTTPClient> client) : http_client(std::move(client)) {}
  ~Impl()
  {
    cancel_flag->store(true);
    generation->fetch_add(1);
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;
};

AIClient::AIClient(std::shared_ptr<HTTPClient> httpClient)
  : impl(std::make_unique<Impl>(std::move(httpClient)))
{
}

AIClient::~AIClient() = default;

AIClient::AIClient(AIClient&&) noexcept = default;
AIClient& AIClient::operator=(AIClient&&) noexcept = default;

nlohmann::json getOpenSCADTools()
{
  nlohmann::json tools = nlohmann::json::array();
  const auto emptyParams = []() {
    nlohmann::json p = nlohmann::json::object();
    p["type"] = "object";
    p["properties"] = nlohmann::json::object();
    return p;
  };
  const auto addSimple = [&](const char *name, const char *description) {
    nlohmann::json tool = nlohmann::json::object();
    tool["type"] = "function";
    nlohmann::json fn = nlohmann::json::object();
    fn["name"] = name;
    fn["description"] = description;
    fn["parameters"] = emptyParams();
    tool["function"] = fn;
    tools.push_back(tool);
  };

  {
    nlohmann::json tool = nlohmann::json::object();
    tool["type"] = "function";
    nlohmann::json fn = nlohmann::json::object();
    fn["name"] = "get_skill";
    fn["description"] =
      "Load a bundled CAD workflow skill (markdown). Call BEFORE designing complex models. "
      "Default name=openscad-cad.";
    nlohmann::json params = nlohmann::json::object();
    params["type"] = "object";
    nlohmann::json props = nlohmann::json::object();
    props["name"] = {{"type", "string"},
                     {"description", "Skill folder name (default: openscad-cad)."}};
    props["compact"] = {{"type", "boolean"},
                        {"description", "If true, load SKILL.compact.md instead of SKILL.md."}};
    params["properties"] = props;
    fn["parameters"] = params;
    tool["function"] = fn;
    tools.push_back(tool);
  }

  addSimple("list_skills", "List bundled OpenSCAD CAD skills available via get_skill.");
  addSimple("get_cheatsheet", "Short OpenSCAD syntax/modeling cheatsheet for quick reference.");

  {
    nlohmann::json set_editor_code = nlohmann::json::object();
    set_editor_code["type"] = "function";
    nlohmann::json sec_fn = nlohmann::json::object();
    sec_fn["name"] = "set_editor_code";
    sec_fn["description"] =
      "Apply complete OpenSCAD source code to the editor and run a fast F5 CSG preview. Always "
      "pass the FULL file. While iterating, verify with get_model_info and get_preview_image. "
      "Do NOT call trigger_render until the design looks correct.";
    nlohmann::json sec_params = nlohmann::json::object();
    sec_params["type"] = "object";
    nlohmann::json sec_props = nlohmann::json::object();
    nlohmann::json sec_code = nlohmann::json::object();
    sec_code["type"] = "string";
    sec_code["description"] = "The complete new code or content to put in the editor.";
    sec_props["code"] = sec_code;
    sec_params["properties"] = sec_props;
    sec_params["required"] = nlohmann::json::array({"code"});
    sec_fn["parameters"] = sec_params;
    set_editor_code["function"] = sec_fn;
    tools.push_back(set_editor_code);
  }

  addSimple("get_editor_code", "Retrieve the current source code present in the editor to inspect it.");
  addSimple("trigger_preview",
            "Run a fast F5 CSG preview of the current editor contents (no full mesh). "
            "Use while iterating / checking.");
  addSimple("trigger_render",
            "Run a full F6 mesh render (slow). Call ONCE when the design is final. "
            "Alias: trigger_build.");
  addSimple("trigger_build",
            "Alias for trigger_render: full F6 mesh render. Use only when the design is final.");
  addSimple("get_model_info",
            "Return last/current preview or render facts: success, empty, errors, warnings, "
            "bounding box (mm), facets (after F6), and log. Use after set_editor_code / preview.");

  {
    nlohmann::json tool = nlohmann::json::object();
    tool["type"] = "function";
    nlohmann::json fn = nlohmann::json::object();
    fn["name"] = "get_preview_image";
    fn["description"] =
      "Capture the current 3D viewport as a PNG so you can SEE the model and judge proportions. "
      "Call after a successful render.";
    nlohmann::json params = nlohmann::json::object();
    params["type"] = "object";
    nlohmann::json props = nlohmann::json::object();
    props["max_width"] = {{"type", "integer"},
                          {"description", "Max image width in pixels (default 1024)."}};
    params["properties"] = props;
    fn["parameters"] = params;
    tool["function"] = fn;
    tools.push_back(tool);
  }

  {
    nlohmann::json tool = nlohmann::json::object();
    tool["type"] = "function";
    nlohmann::json fn = nlohmann::json::object();
    fn["name"] = "get_console_log";
    fn["description"] = "Read recent OpenSCAD console text (parser/render messages).";
    nlohmann::json params = nlohmann::json::object();
    params["type"] = "object";
    nlohmann::json props = nlohmann::json::object();
    props["max_chars"] = {
      {"type", "integer"},
      {"description", "Max characters to return from the end of the console (default 4000)."}};
    params["properties"] = props;
    fn["parameters"] = params;
    tool["function"] = fn;
    tools.push_back(tool);
  }

  addSimple("get_camera_info",
            "Read the current 3D camera (projection, distance, translation, rotation).");

  {
    nlohmann::json tool = nlohmann::json::object();
    tool["type"] = "function";
    nlohmann::json fn = nlohmann::json::object();
    fn["name"] = "pan_view";
    fn["description"] =
      "Pan/drag the 3D view in the screen plane (grab-hand). Units are millimeters. "
      "Positive dx moves the model right; positive dy moves it down. Optional dz moves in depth. "
      "After panning, call get_preview_image to verify framing.";
    nlohmann::json params = nlohmann::json::object();
    params["type"] = "object";
    nlohmann::json props = nlohmann::json::object();
    props["dx"] = {{"type", "number"},
                   {"description", "Horizontal pan in mm (positive = model moves right on screen)."}};
    props["dy"] = {{"type", "number"},
                   {"description", "Vertical pan in mm (positive = model moves down on screen)."}};
    props["dz"] = {{"type", "number"},
                   {"description", "Optional depth pan in mm (positive = away from camera)."}};
    params["properties"] = props;
    params["additionalProperties"] = false;
    fn["parameters"] = params;
    tool["function"] = fn;
    tools.push_back(tool);
  }

  {
    nlohmann::json tool = nlohmann::json::object();
    tool["type"] = "function";
    nlohmann::json fn = nlohmann::json::object();
    fn["name"] = "zoom_in";
    fn["description"] =
      "Zoom the 3D camera in (closer). Optional steps (default 1); each step is one mouse-wheel notch.";
    nlohmann::json params = nlohmann::json::object();
    params["type"] = "object";
    nlohmann::json props = nlohmann::json::object();
    props["steps"] = {{"type", "integer"}, {"description", "How many zoom steps (default 1, max 20)."}};
    params["properties"] = props;
    params["additionalProperties"] = false;
    fn["parameters"] = params;
    tool["function"] = fn;
    tools.push_back(tool);
  }

  {
    nlohmann::json tool = nlohmann::json::object();
    tool["type"] = "function";
    nlohmann::json fn = nlohmann::json::object();
    fn["name"] = "zoom_out";
    fn["description"] =
      "Zoom the 3D camera out (farther). Optional steps (default 1); each step is one mouse-wheel notch.";
    nlohmann::json params = nlohmann::json::object();
    params["type"] = "object";
    nlohmann::json props = nlohmann::json::object();
    props["steps"] = {{"type", "integer"}, {"description", "How many zoom steps (default 1, max 20)."}};
    params["properties"] = props;
    params["additionalProperties"] = false;
    fn["parameters"] = params;
    tool["function"] = fn;
    tools.push_back(tool);
  }

  addSimple("zoom_100",
            "Reset zoom distance to the default 100% camera distance (keeps pan/rotation). "
            "Use view_all to frame all geometry, or reset_view for a full camera reset.");
  addSimple("view_all", "Frame the camera to show all geometry (View All).");
  addSimple("reset_view", "Reset the 3D camera to the default view.");
  addSimple("list_tools", "List OpenSCAD tools and the recommended CAD workflow order.");

  return tools;
}

// Profile params may include OpenSCAD-only keys (system_prompt, context_limit, …).
// Providers like Gemini reject unknown fields with HTTP 400 Invalid JSON payload.
static void mergeApiParameters(nlohmann::json& payload, const nlohmann::json& parameters)
{
  static const std::unordered_set<std::string> kAllowed = {
    "temperature", "top_p", "top_k", "n", "stop", "presence_penalty", "frequency_penalty",
    "logit_bias", "user", "seed", "response_format", "tool_choice", "parallel_tool_calls",
    "max_tokens", "max_completion_tokens", "max_output_tokens"};

  if (!parameters.is_object()) return;
  for (auto& el : parameters.items()) {
    const std::string& key = el.key();
    if (kAllowed.count(key) == 0) continue;
    payload[key] = el.value();
  }
}

static nlohmann::json buildChatMessages(const std::vector<AIChatMessage>& history,
                                        bool geminiCompat)
{
  nlohmann::json messages = nlohmann::json::array();
  for (const auto& msg : history) {
    nlohmann::json m = nlohmann::json::object();
    m["role"] = msg.role;
    if (!msg.images.empty() && msg.role == "user") {
      nlohmann::json parts = nlohmann::json::array();
      const std::string text =
        msg.content.empty() ? std::string("Please analyze the attached image(s).") : msg.content;
      parts.push_back({{"type", "text"}, {"text", text}});
      for (const auto& imageUrl : msg.images) {
        if (imageUrl.empty()) continue;
        parts.push_back({{"type", "image_url"}, {"image_url", {{"url", imageUrl}}}});
      }
      m["content"] = parts;
    } else if (!msg.content.empty() || msg.tool_calls.empty()) {
      m["content"] = msg.content;
    } else {
      m["content"] = nullptr;
    }
    if (msg.role == "tool") {
      m["tool_call_id"] = msg.tool_call_id;
      // Gemini OpenAI-compat examples include the function name on tool messages.
      for (const auto& prev : history) {
        for (const auto& tc : prev.tool_calls) {
          if (tc.id == msg.tool_call_id && !tc.name.empty()) {
            m["name"] = tc.name;
            break;
          }
        }
        if (m.contains("name")) break;
      }
    }
    if (!msg.tool_calls.empty()) {
      nlohmann::json tcs = nlohmann::json::array();
      for (size_t i = 0; i < msg.tool_calls.size(); ++i) {
        // Gemini 3 requires a thought_signature on the first function call of each step.
        const bool ensureSig = geminiCompat && i == 0;
        tcs.push_back(serializeAIToolCall(msg.tool_calls[i], ensureSig));
      }
      m["tool_calls"] = tcs;
    }
    messages.push_back(m);
  }
  return messages;
}

static std::string resolveChatCompletionsUrl(std::string endpoint_url)
{
  if (endpoint_url.find("/chat/completions") == std::string::npos &&
      endpoint_url.find("/generate") == std::string::npos &&
      endpoint_url.find("/api/chat") == std::string::npos) {
    if (!endpoint_url.empty() && endpoint_url.back() == '/') {
      endpoint_url += "chat/completions";
    } else {
      endpoint_url += "/chat/completions";
    }
  }
  return endpoint_url;
}

void AIClient::sendChatCompletion(const AIProfileConfig& config,
                                  const std::vector<AIChatMessage>& history,
                                  ResponseCallback on_response, ErrorCallback on_error)
{
  if (isCursorAgentEndpoint(config.endpoint)) {
    const uint64_t gen = impl->generation->fetch_add(1) + 1;
    impl->cancel_flag->store(false);
    auto text = std::make_shared<std::string>();
    runCursorAgentChat(
      config, history,
      [text](const std::string& chunk) { *text += chunk; }, on_error,
      [on_response, text](const std::vector<AIToolCall>& tool_calls) {
        if (on_response) on_response(*text, tool_calls);
      },
      impl->cancel_flag, impl->generation, gen);
    return;
  }

  const bool geminiCompat = isGeminiOpenAIEndpoint(config.endpoint);

  nlohmann::json payload = nlohmann::json::object();
  payload["model"] = config.model;
  payload["stream"] = false;
  payload["messages"] = buildChatMessages(history, geminiCompat);
  payload["tools"] = getOpenSCADTools();
  mergeApiParameters(payload, config.parameters);

  std::string body = payload.dump();

  HTTPClient::Headers headers;
  headers["Content-Type"] = "application/json";
  if (!config.apiKey.empty()) {
    headers["Authorization"] = "Bearer " + config.apiKey;
  }

  std::string endpoint_url = resolveChatCompletionsUrl(config.endpoint);

  impl->http_client->asyncPost(
    endpoint_url, headers, body,
    [on_response, on_error](int status_code, const std::string& response_body) {
      if (status_code >= 200 && status_code < 300) {
        try {
          auto json = nlohmann::json::parse(response_body);
          if (json.contains("choices") && json["choices"].is_array() && !json["choices"].empty()) {
            auto& choice = json["choices"][0];
            std::string content = "";
            std::vector<AIToolCall> parsed_tool_calls;
            if (choice.contains("message") && choice["message"].is_object()) {
              auto& msg = choice["message"];
              if (msg.contains("content") && !msg["content"].is_null()) {
                content = msg["content"].is_string() ? msg["content"].get<std::string>()
                                                     : msg["content"].dump();
              }
              if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
                for (auto& tc : msg["tool_calls"]) {
                  AIToolCall tool_call;
                  parseAIToolCall(tc, tool_call);
                  parsed_tool_calls.push_back(tool_call);
                }
              }
              if (parsed_tool_calls.empty() && !content.empty()) {
                parsed_tool_calls = tryParseEmbeddedToolCalls(content);
                if (!parsed_tool_calls.empty()) {
                  // Keep history clean: tool turn should not also carry the fake JSON prose.
                  content.clear();
                }
              }
            }
            on_response(content, parsed_tool_calls);
            return;
          }
          if (json.contains("message") && json["message"].is_object() &&
              json["message"].contains("content")) {
            std::string content = json["message"]["content"].is_string()
                                    ? json["message"]["content"].get<std::string>()
                                    : json["message"]["content"].dump();
            std::vector<AIToolCall> parsed_tool_calls;
            if (json["message"].contains("tool_calls") && json["message"]["tool_calls"].is_array()) {
              for (auto& tc : json["message"]["tool_calls"]) {
                AIToolCall tool_call;
                parseAIToolCall(tc, tool_call);
                parsed_tool_calls.push_back(tool_call);
              }
            }
            if (parsed_tool_calls.empty() && !content.empty()) {
              parsed_tool_calls = tryParseEmbeddedToolCalls(content);
              if (!parsed_tool_calls.empty()) content.clear();
            }
            on_response(content, parsed_tool_calls);
            return;
          }
          if (json.contains("response")) {
            on_response(json["response"].get<std::string>(), {});
            return;
          }
          on_response(response_body, {});
        } catch (...) {
          on_response(response_body, {});
        }
      } else {
        if (on_error) {
          on_error(AIFreeAgents::formatHttpError(status_code, response_body));
        }
      }
    },
    on_error);
}

void AIClient::sendChatCompletionStream(const AIProfileConfig& config,
                                        const std::vector<AIChatMessage>& history,
                                        ChunkCallback on_chunk, ErrorCallback on_error,
                                        CompleteCallback on_complete, bool include_tools)
{
  if (isCursorAgentEndpoint(config.endpoint)) {
    (void)include_tools;
    const uint64_t gen = impl->generation->fetch_add(1) + 1;
    impl->cancel_flag->store(false);
    runCursorAgentChat(config, history, on_chunk, on_error, on_complete, impl->cancel_flag,
                       impl->generation, gen);
    return;
  }

  const bool geminiCompat = isGeminiOpenAIEndpoint(config.endpoint);

  nlohmann::json payload = nlohmann::json::object();
  payload["model"] = config.model;
  payload["stream"] = true;
  payload["messages"] = buildChatMessages(history, geminiCompat);
  if (include_tools) {
    payload["tools"] = getOpenSCADTools();
  }
  mergeApiParameters(payload, config.parameters);

  std::string body = payload.dump();

  HTTPClient::Headers headers;
  headers["Content-Type"] = "application/json";
  if (!config.apiKey.empty()) {
    headers["Authorization"] = "Bearer " + config.apiKey;
  }

  std::string endpoint_url = resolveChatCompletionsUrl(config.endpoint);

  auto parser = std::make_shared<SSEParser>(on_chunk);
  // Accumulate non-2xx body so we report one complete error instead of partial chunks.
  auto error_body = std::make_shared<std::string>();
  auto error_status = std::make_shared<int>(0);

  impl->http_client->asyncPostStream(
    endpoint_url, headers, body,
    [parser, on_error, error_body, error_status](int status_code, const std::string& chunk) {
      if (status_code >= 200 && status_code < 300) {
        parser->feed(chunk);
      } else {
        *error_status = status_code;
        *error_body += chunk;
      }
    },
    on_error,
    [parser, on_complete, on_error, error_body, error_status]() {
      if (*error_status != 0) {
        if (on_error) {
          on_error(AIFreeAgents::formatHttpError(*error_status, *error_body));
        }
        return;
      }
      parser->flush();
      parser->finalizeToolCalls();
      if (on_complete) {
        on_complete(parser->toolCalls());
      }
    });
}

void AIClient::cancelPendingRequests()
{
  impl->cancel_flag->store(true);
  impl->generation->fetch_add(1);
  impl->http_client->cancelPendingRequests();
}
