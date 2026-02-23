#pragma once

#include "Tool.h"
#include "../scripting/ScriptEngine.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────────────
// ScriptTool — exposes the Lua ScriptEngine as a registered ITool.
//
// The LLM (or a workflow step) can call this tool to write and
// execute small Lua scripts dynamically at runtime, without needing
// a full firmware recompile.
//
// Operations (via "op" field in argsJson):
//   exec — execute a Lua script supplied inline via "code"
//   load — execute a script read from a path on the filesystem
//          (checks RamVfs first, then LittleFS)
//
// JSON argument schema:
//   {
//     "op":   "exec" | "load",
//     "code": "<lua source>",   // required for "exec"
//     "path": "<file path>"     // required for "load"
//   }
// ─────────────────────────────────────────────────────────────────

class ScriptTool : public ITool {
public:
    const char* name() const override { return "script"; }

    const char* description() const override {
        return "Execute Lua scripts at runtime for dynamic skill development";
    }

    const char* argSchema() const override {
        return "{"
            "\"type\":\"object\","
            "\"properties\":{"
                "\"op\":{"
                    "\"type\":\"string\","
                    "\"enum\":[\"exec\",\"load\"]"
                "},"
                "\"code\":{\"type\":\"string\"},"
                "\"path\":{\"type\":\"string\"}"
            "},"
            "\"required\":[\"op\"]"
        "}";
    }

    bool execute(const char* argsJson,
                 char*       resultBuf,
                 size_t      resultLen) override
    {
        char op[16] = {};
        _extractStr(argsJson, "op", op, sizeof(op));

        ScriptResult res;

        if (strcmp(op, "exec") == 0) {
            char code[SCRIPT_CODE_LEN] = {};
            _extractStr(argsJson, "code", code, sizeof(code));
            res = _engine.execute(code);

        } else if (strcmp(op, "load") == 0) {
            char path[64] = {};
            _extractStr(argsJson, "path", path, sizeof(path));
            res = _engine.executeFile(path);

        } else {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"unknown op '%s'\"}", op);
            return false;
        }

        if (res.ok) {
            snprintf(resultBuf, resultLen,
                     "{\"ok\":true,\"output\":\"%s\"}", res.output);
        } else {
            snprintf(resultBuf, resultLen,
                     "{\"ok\":false,\"error\":\"%s\"}", res.error);
        }
        return res.ok;
    }

private:
    ScriptEngine _engine;

    // Minimal JSON string extractor (same pattern as other tools).
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
        while (*p && *p != '"' && i < outLen - 1) out[i++] = *p++;
        out[i] = '\0';
    }
};
