#pragma once

#include "../gateway/Message.h"
#include "../tools/ToolRegistry.h"
#include <string.h>
#include <stdio.h>

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#  include <HTTPClient.h>
#  include <ArduinoJson.h>
#endif

// ─────────────────────────────────────────────────────────────────
// ReasoningLoop — token-efficient LLM reasoning engine.
//
// Design principles (ultra-efficient):
//   • System prompt is short and loaded from config
//   • Tool list is embedded in the system prompt (function-calling)
//   • Conversation history is NOT kept — each call is stateless
//     (memory is managed via the workspace, not chat history)
//   • Uses the smallest capable model (gpt-4o-mini / claude-haiku)
//   • Structured output: the LLM returns either a tool call or text
//
// Supported LLM backends:
//   openai    — OpenAI Chat Completions (default)
//   anthropic — Anthropic Messages API
//   custom    — Any OpenAI-compatible endpoint
// ─────────────────────────────────────────────────────────────────

static constexpr size_t LLM_RESPONSE_BUF = 2048;

struct LlmConfig {
    char provider[16]   = "openai";      // openai | anthropic | custom
    char model[32]      = "gpt-4o-mini"; // model identifier
    char api_key[128]   = {};            // NEVER logged or stored in flash
    char endpoint[128]  = {};            // custom endpoint URL
    char system_prompt[512] = {};        // loaded from agent.md
    uint16_t max_tokens = 512;           // keep responses concise
    float    temperature = 0.3f;         // lower = more deterministic
};

class ReasoningLoop {
public:
    explicit ReasoningLoop(ToolRegistry& tools) : _tools(tools) {}

    void configure(const LlmConfig& cfg) { _cfg = cfg; }
    const LlmConfig& config() const      { return _cfg; }

    // Run one reasoning step.
    //   userText             — user's message (already intent-classified)
    //   contextJson          — optional JSON workspace context string
    //                          (ignored when systemPromptOverride is set)
    //   resultBuf            — output buffer for assistant reply
    //   resultLen            — size of resultBuf
    //   systemPromptOverride — if non-null, replaces the config system
    //                          prompt (e.g. output from PromptBuilder)
    // Returns true on success.
    bool reason(const char* userText,
                const char* contextJson,
                char*       resultBuf,
                size_t      resultLen,
                const char* systemPromptOverride = nullptr)
    {
        if (!userText || resultLen == 0) return false;

#ifndef NATIVE_BUILD
        char toolSchemas[1024] = {};
        _tools.schemasJson(toolSchemas, sizeof(toolSchemas));

        // Build system message: use override if provided, otherwise
        // fall back to the legacy format (config prompt + tools + ctx).
        const char* sysMsg  = nullptr;
        char*       sysBuf  = nullptr;

        if (systemPromptOverride) {
            sysMsg = systemPromptOverride;
        } else {
            size_t baseLen = strlen(_cfg.system_prompt);
            size_t toolLen = strlen(toolSchemas);
            size_t ctxLen  = contextJson ? strlen(contextJson) : 2;
            size_t total   = baseLen + toolLen + ctxLen + 32;
            sysBuf = (char*)malloc(total);
            if (!sysBuf) {
                snprintf(resultBuf, resultLen, "{\"error\":\"OOM\"}");
                return false;
            }
            snprintf(sysBuf, total,
                     "%s\nAvailable tools: %s\nContext: %s",
                     _cfg.system_prompt, toolSchemas,
                     contextJson ? contextJson : "{}");
            sysMsg = sysBuf;
        }

        bool ok;
        if (strcmp(_cfg.provider, "openai") == 0 ||
            strcmp(_cfg.provider, "custom") == 0) {
            ok = _callOpenAI(userText, sysMsg, resultBuf, resultLen);
        } else if (strcmp(_cfg.provider, "anthropic") == 0) {
            ok = _callAnthropic(userText, sysMsg, resultBuf, resultLen);
        } else {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"unknown provider '%s'\"}", _cfg.provider);
            ok = false;
        }

        if (sysBuf) free(sysBuf);
        return ok;
#else
        // Native / test: echo the input
        (void)contextJson; (void)systemPromptOverride;
        snprintf(resultBuf, resultLen,
                 "{\"reply\":\"[simulated] received: %s\"}", userText);
        return true;
#endif
    }

private:
    ToolRegistry& _tools;
    LlmConfig     _cfg;

#ifndef NATIVE_BUILD
    bool _callOpenAI(const char* userText,
                     const char* sysMsg,
                     char*       out, size_t outLen)
    {
        const char* endpoint = (_cfg.endpoint[0] != '\0')
                               ? _cfg.endpoint
                               : "https://api.openai.com/v1/chat/completions";

        JsonDocument body;
        body["model"]       = _cfg.model;
        body["max_tokens"]  = _cfg.max_tokens;
        body["temperature"] = _cfg.temperature;

        JsonArray msgs = body["messages"].to<JsonArray>();

        JsonObject sys = msgs.add<JsonObject>();
        sys["role"]    = "system";
        sys["content"] = sysMsg;

        JsonObject user = msgs.add<JsonObject>();
        user["role"]    = "user";
        user["content"] = userText;

        String reqBody;
        serializeJson(body, reqBody);

        char authHeader[160];
        snprintf(authHeader, sizeof(authHeader), "Bearer %s", _cfg.api_key);

        HTTPClient http;
        http.setTimeout(15000);
        http.begin(endpoint);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", authHeader);

        int code = http.POST(reqBody);
        if (code != 200) {
            String err = http.getString();
            http.end();
            snprintf(out, outLen,
                     "{\"error\":\"LLM HTTP %d\",\"detail\":\"%s\"}",
                     code, err.c_str());
            return false;
        }

        String resp = http.getString();
        http.end();

        // Extract content from choices[0].message.content
        JsonDocument respDoc;
        if (deserializeJson(respDoc, resp) != DeserializationError::Ok) {
            snprintf(out, outLen, "{\"error\":\"bad JSON from LLM\"}");
            return false;
        }
        const char* content = respDoc["choices"][0]["message"]["content"]
                              .as<const char*>();
        if (!content) {
            snprintf(out, outLen, "{\"error\":\"no content in LLM response\"}");
            return false;
        }
        strncpy(out, content, outLen - 1);
        out[outLen - 1] = '\0';
        return true;
    }

    bool _callAnthropic(const char* userText,
                        const char* sysMsg,
                        char*       out, size_t outLen)
    {
        const char* endpoint = "https://api.anthropic.com/v1/messages";

        JsonDocument body;
        body["model"]      = _cfg.model;
        body["max_tokens"] = _cfg.max_tokens;
        body["system"]     = sysMsg;

        JsonArray msgs = body["messages"].to<JsonArray>();
        JsonObject user = msgs.add<JsonObject>();
        user["role"] = "user";
        user["content"] = userText;

        String reqBody;
        serializeJson(body, reqBody);

        HTTPClient http;
        http.setTimeout(15000);
        http.begin(endpoint);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("x-api-key", _cfg.api_key);
        http.addHeader("anthropic-version", "2023-06-01");

        int code = http.POST(reqBody);
        if (code != 200) {
            http.end();
            snprintf(out, outLen, "{\"error\":\"Anthropic HTTP %d\"}", code);
            return false;
        }

        String resp = http.getString();
        http.end();

        JsonDocument respDoc;
        if (deserializeJson(respDoc, resp) != DeserializationError::Ok) {
            snprintf(out, outLen, "{\"error\":\"bad JSON from Anthropic\"}");
            return false;
        }
        const char* content = respDoc["content"][0]["text"].as<const char*>();
        if (!content) {
            snprintf(out, outLen, "{\"error\":\"no content from Anthropic\"}");
            return false;
        }
        strncpy(out, content, outLen - 1);
        out[outLen - 1] = '\0';
        return true;
    }
#endif
};
