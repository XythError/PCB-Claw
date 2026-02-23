#pragma once

#include "Tool.h"
#include "../gateway/Gateway.h"
#include "../gateway/Message.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────────────
// MessageTool — send messages through the Gateway from within
// the agent's tool-call flow.
//
// This enables the agent to proactively push notifications,
// communicate with other agents, or forward results to a
// specific channel without waiting for a new request.
//
// Supported operations (via "op" field in argsJson):
//   send      — send a message to a named channel:
//               { "channel": "serial", "text": "Hello" }
//   broadcast — send a message to all registered channels:
//               { "text": "Alert: task complete" }
//
// The Gateway pointer is injected at construction time so the
// tool is unit-testable with a null pointer (simulated on native).
// ─────────────────────────────────────────────────────────────────

class MessageTool : public ITool {
public:
    explicit MessageTool(Gateway* gw = nullptr) : _gw(gw) {}

    const char* name()        const override { return "message"; }
    const char* description() const override {
        return "Send or broadcast messages through the gateway to channels";
    }
    const char* argSchema()   const override {
        return "{"
            "\"type\":\"object\","
            "\"properties\":{"
                "\"op\":{"
                    "\"type\":\"string\","
                    "\"enum\":[\"send\",\"broadcast\"]"
                "},"
                "\"channel\":{\"type\":\"string\"},"
                "\"text\":{\"type\":\"string\"}"
            "},"
            "\"required\":[\"op\",\"text\"]"
        "}";
    }

    bool execute(const char* argsJson,
                 char*       resultBuf,
                 size_t      resultLen) override
    {
        char op[16]                        = {};
        char channel[MSG_CHANNEL_LEN]      = {};
        char text[MSG_CONTENT_LEN]         = {};

        _extractStr(argsJson, "op",      op,      sizeof(op));
        _extractStr(argsJson, "channel", channel, sizeof(channel));
        _extractStr(argsJson, "text",    text,    sizeof(text));

        if (text[0] == '\0') {
            snprintf(resultBuf, resultLen, "{\"error\":\"text required\"}");
            return false;
        }

#ifndef NATIVE_BUILD
        if (!_gw) {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"gateway not available\"}");
            return false;
        }

        if (strcmp(op, "send") == 0) {
            if (channel[0] == '\0') {
                snprintf(resultBuf, resultLen,
                         "{\"error\":\"channel required for send\"}");
                return false;
            }
            Message msg = Message::make(channel, "agent", text,
                                        MessageType::RESPONSE,
                                        MessagePriority::NORMAL);
            bool ok = _gw->send(msg);
            snprintf(resultBuf, resultLen,
                     ok ? "{\"ok\":true,\"op\":\"send\",\"channel\":\"%s\"}"
                        : "{\"error\":\"send failed\",\"channel\":\"%s\"}",
                     channel);
            return ok;

        } else if (strcmp(op, "broadcast") == 0) {
            Message msg = Message::make("broadcast", "agent", text,
                                        MessageType::RESPONSE,
                                        MessagePriority::NORMAL);
            _gw->broadcast(msg);
            snprintf(resultBuf, resultLen,
                     "{\"ok\":true,\"op\":\"broadcast\"}");
            return true;

        } else {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"unknown op '%s'\"}", op);
            return false;
        }
#else
        // Native build — simulate for unit tests
        (void)channel;
        snprintf(resultBuf, resultLen,
                 "{\"ok\":true,\"op\":\"%s\",\"simulated\":true}", op);
        return true;
#endif
    }

private:
    Gateway* _gw;

    static void _extractStr(const char* json, const char* key,
                              char* out, size_t outLen) {
        out[0] = '\0';
        if (!json || !key) return;
        char needle[TOOL_NAME_LEN + 5];
        snprintf(needle, sizeof(needle), "\"%s\":", key);
        const char* p = strstr(json, needle);
        if (!p) return;
        p += strlen(needle);
        while (*p == ' ') ++p;
        if (*p != '"') return;
        ++p;  // skip opening quote
        size_t i = 0;
        while (*p && *p != '"' && i < outLen - 1) {
            if (*p == '\\' && *(p + 1) == '"') { out[i++] = '"'; p += 2; }
            else out[i++] = *p++;
        }
        out[i] = '\0';
    }
};
