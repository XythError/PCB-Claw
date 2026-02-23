#pragma once

#include "../gateway/Message.h"
#include "../queue/Task.h"
#include "IntentDetector.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────────────
// TaskDecomposer — breaks a user message into one or more Tasks.
//
// For hardware intents (GPIO, I2C, SPI) the decomposer generates
// tasks directly without an LLM call, saving tokens.
//
// For CHAT intent, a single LLM reasoning task is emitted.
//
// Task lanes:
//   "llm"     — LLM inference (rate-limited, sequential)
//   "gpio"    — GPIO operations
//   "i2c"     — I2C bus operations
//   "spi"     — SPI bus operations
//   "http"    — External HTTP calls
//   "default" — General / workflow tasks
// ─────────────────────────────────────────────────────────────────

static constexpr uint8_t MAX_TASKS_PER_MESSAGE = 4;

struct DecomposedTasks {
    Task    tasks[MAX_TASKS_PER_MESSAGE];
    uint8_t count = 0;

    bool add(const Task& t) {
        if (count >= MAX_TASKS_PER_MESSAGE) return false;
        tasks[count++] = t;
        return true;
    }
};

class TaskDecomposer {
public:
    // Decompose a message into one or more tasks based on its intent.
    static DecomposedTasks decompose(const Message& msg, Intent intent) {
        DecomposedTasks result;

        switch (intent) {
            case Intent::CHAT:
                result.add(_makeLlmTask(msg, "chat"));
                break;

            case Intent::COMMAND:
                _decomposeCommand(msg, result);
                break;

            case Intent::GPIO_CONTROL:
                result.add(_makeHwTask(msg, "gpio", "gpio"));
                break;

            case Intent::I2C_OP:
                result.add(_makeHwTask(msg, "i2c", "i2c"));
                break;

            case Intent::SPI_OP:
                result.add(_makeHwTask(msg, "spi", "spi"));
                break;

            case Intent::HTTP_REQUEST:
                result.add(_makeHwTask(msg, "http", "http"));
                break;

            case Intent::WORKFLOW_RUN:
                result.add(_makeWorkflowTask(msg));
                break;

            case Intent::STATUS_QUERY:
                result.add(_makeStatusTask(msg));
                break;

            case Intent::CONFIG_CHANGE:
                result.add(_makeConfigTask(msg));
                break;

            default:
                // Unknown intent: fall back to LLM
                result.add(_makeLlmTask(msg, "fallback"));
                break;
        }

        return result;
    }

private:
    // ── Task factory helpers ──────────────────────────────────────

    static Task _makeLlmTask(const Message& msg, const char* subtype) {
        // Payload: JSON with message context
        char payload[TASK_PAYLOAD_LEN];
        snprintf(payload, sizeof(payload),
                 "{\"subtype\":\"%s\","
                 "\"text\":\"%s\","
                 "\"sender\":\"%s\"}",
                 subtype,
                 _escape(msg.content).buf,
                 msg.sender_id);
        Task t = Task::make("llm:reason", "llm", payload,
                            msg.reply_channel, 1);
        strncpy(t.origin_msg, msg.id, MSG_ID_LEN - 1);
        return t;
    }

    static Task _makeHwTask(const Message& msg,
                             const char* toolName,
                             const char* lane) {
        char payload[TASK_PAYLOAD_LEN];
        snprintf(payload, sizeof(payload),
                 "{\"tool\":\"%s\","
                 "\"text\":\"%s\","
                 "\"sender\":\"%s\"}",
                 toolName,
                 _escape(msg.content).buf,
                 msg.sender_id);
        Task t = Task::make("tool:direct", lane, payload,
                            msg.reply_channel, 2);
        strncpy(t.origin_msg, msg.id, MSG_ID_LEN - 1);
        return t;
    }

    static Task _makeWorkflowTask(const Message& msg) {
        // Extract workflow name from message (first token after "run")
        char wfName[32] = "unnamed";
        const char* p = msg.content;
        // Find workflow name keyword
        const char* runPtr = nullptr;
        const char* keywords[] = {"workflow ", "run ", "execute ", "start "};
        for (const char* kw : keywords) {
            runPtr = _strcistr(p, kw);
            if (runPtr) { runPtr += strlen(kw); break; }
        }
        if (runPtr) {
            size_t i = 0;
            while (runPtr[i] && runPtr[i] != ' ' && i < sizeof(wfName) - 1) {
                wfName[i] = runPtr[i]; ++i;
            }
            wfName[i] = '\0';
        }

        char payload[TASK_PAYLOAD_LEN];
        snprintf(payload, sizeof(payload),
                 "{\"workflow\":\"%s\","
                 "\"text\":\"%s\"}",
                 wfName,
                 _escape(msg.content).buf);
        Task t = Task::make("workflow:run", "default", payload,
                            msg.reply_channel, 1);
        strncpy(t.origin_msg, msg.id, MSG_ID_LEN - 1);
        return t;
    }

    static Task _makeStatusTask(const Message& msg) {
        char payload[TASK_PAYLOAD_LEN];
        snprintf(payload, sizeof(payload),
                 "{\"query\":\"%s\"}", _escape(msg.content).buf);
        Task t = Task::make("status:query", "default", payload,
                            msg.reply_channel, 1);
        strncpy(t.origin_msg, msg.id, MSG_ID_LEN - 1);
        return t;
    }

    static Task _makeConfigTask(const Message& msg) {
        char payload[TASK_PAYLOAD_LEN];
        snprintf(payload, sizeof(payload),
                 "{\"cmd\":\"%s\"}", _escape(msg.content).buf);
        Task t = Task::make("config:set", "default", payload,
                            msg.reply_channel, 2);
        strncpy(t.origin_msg, msg.id, MSG_ID_LEN - 1);
        return t;
    }

    static void _decomposeCommand(const Message& msg,
                                   DecomposedTasks& out) {
        char cmd[32] = {};
        msg.commandName(cmd, sizeof(cmd));

        char payload[TASK_PAYLOAD_LEN];
        snprintf(payload, sizeof(payload),
                 "{\"command\":\"%s\","
                 "\"args\":\"%s\"}",
                 cmd,
                 _escape(msg.content).buf);
        Task t = Task::make("command:exec", "default", payload,
                            msg.reply_channel, 3);
        strncpy(t.origin_msg, msg.id, MSG_ID_LEN - 1);
        out.add(t);
    }

    // ── Utility ───────────────────────────────────────────────────

    // Simple escaped string holder
    struct EscapedStr { char buf[TASK_PAYLOAD_LEN / 2]; };

    static EscapedStr _escape(const char* s) {
        EscapedStr e; size_t i = 0, j = 0;
        while (s[i] && j < sizeof(e.buf) - 2) {
            if (s[i] == '"')       { e.buf[j++] = '\\'; e.buf[j++] = '"'; }
            else if (s[i] == '\\') { e.buf[j++] = '\\'; e.buf[j++] = '\\'; }
            else if (s[i] == '\n') { e.buf[j++] = '\\'; e.buf[j++] = 'n'; }
            else                   { e.buf[j++] = s[i]; }
            ++i;
        }
        e.buf[j] = '\0';
        return e;
    }

    // Case-insensitive strstr
    static const char* _strcistr(const char* h, const char* n) {
        if (!h || !n) return nullptr;
        size_t hl = strlen(h), nl = strlen(n);
        if (nl > hl) return nullptr;
        for (size_t i = 0; i <= hl - nl; ++i) {
            bool m = true;
            for (size_t j = 0; j < nl; ++j) {
                char a = h[i+j], b = n[j];
                if (a>='A'&&a<='Z') a+=32;
                if (b>='A'&&b<='Z') b+=32;
                if (a != b) { m = false; break; }
            }
            if (m) return h + i;
        }
        return nullptr;
    }
};
