#pragma once

#include "Tool.h"
#include "../queue/LaneQueue.h"
#include "../queue/Task.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────────────
// SpawnTool — super-agent mechanic: spawn sub-tasks into lanes.
//
// Allows the agent to decompose work and enqueue sub-tasks into the
// LaneQueue for asynchronous processing — the foundation of the
// PCB-Claw multi-agent / workflow architecture.
//
// Supported operations (via "op" field in argsJson):
//   spawn  — enqueue a new task into a named lane:
//             { "task": "tool:gpio",
//               "lane": "gpio",
//               "payload": "{\"op\":\"read\",\"pin\":4}",
//               "priority": 1 }
//
// The LaneQueue pointer is injected at construction time so the
// tool remains unit-testable with a null pointer (simulated on
// native).
// ─────────────────────────────────────────────────────────────────

class SpawnTool : public ITool {
public:
    explicit SpawnTool(LaneQueue* queue = nullptr) : _queue(queue) {}

    const char* name()        const override { return "spawn"; }
    const char* description() const override {
        return "Spawn sub-agent tasks into named execution lanes (super-agent)";
    }
    const char* argSchema()   const override {
        return "{"
            "\"type\":\"object\","
            "\"properties\":{"
                "\"op\":{"
                    "\"type\":\"string\","
                    "\"enum\":[\"spawn\"]"
                "},"
                "\"task\":{\"type\":\"string\"},"
                "\"lane\":{\"type\":\"string\"},"
                "\"payload\":{\"type\":\"string\"},"
                "\"priority\":{"
                    "\"type\":\"integer\","
                    "\"minimum\":0,\"maximum\":3"
                "}"
            "},"
            "\"required\":[\"op\",\"task\",\"lane\",\"payload\"]"
        "}";
    }

    bool execute(const char* argsJson,
                 char*       resultBuf,
                 size_t      resultLen) override
    {
        char op[16]                     = {};
        char taskName[TASK_NAME_LEN]    = {};
        char lane[TASK_LANE_LEN]        = {};
        char payload[TASK_PAYLOAD_LEN]  = {};
        int  priority                   = 1;

        _extractStr(argsJson, "op",      op,       sizeof(op));
        _extractStr(argsJson, "task",    taskName, sizeof(taskName));
        _extractStr(argsJson, "lane",    lane,     sizeof(lane));
        _extractStr(argsJson, "payload", payload,  sizeof(payload));
        int p = _extractInt(argsJson, "priority", -1);
        if (p >= 0 && p <= 3) priority = p;

        if (strcmp(op, "spawn") != 0) {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"unknown op '%s'\"}", op);
            return false;
        }
        if (taskName[0] == '\0' || lane[0] == '\0') {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"task and lane are required\"}");
            return false;
        }

#ifndef NATIVE_BUILD
        if (!_queue) {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"queue not available\"}");
            return false;
        }

        Task t = Task::make(taskName, lane, payload, "agent",
                            (uint8_t)priority);
        bool ok = _queue->enqueue(t);
        if (ok) {
            snprintf(resultBuf, resultLen,
                     "{\"ok\":true,\"id\":%lu,\"task\":\"%s\","
                     "\"lane\":\"%s\"}",
                     (unsigned long)t.id, taskName, lane);
        } else {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"enqueue failed (lane full or stopped)\","
                     "\"task\":\"%s\",\"lane\":\"%s\"}", taskName, lane);
        }
        return ok;
#else
        // Native build — simulate for unit tests
        snprintf(resultBuf, resultLen,
                 "{\"ok\":true,\"task\":\"%s\",\"lane\":\"%s\","
                 "\"simulated\":true}", taskName, lane);
        return true;
#endif
    }

private:
    LaneQueue* _queue;

    static int _extractInt(const char* json, const char* key, int def) {
        if (!json || !key) return def;
        char needle[TOOL_NAME_LEN + 5];
        snprintf(needle, sizeof(needle), "\"%s\":", key);
        const char* p = strstr(json, needle);
        if (!p) return def;
        p += strlen(needle);
        while (*p == ' ') ++p;
        if (*p == '-' || (*p >= '0' && *p <= '9')) return atoi(p);
        return def;
    }

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
