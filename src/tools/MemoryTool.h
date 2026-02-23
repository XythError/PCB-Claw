#pragma once

#include "Tool.h"
#include <string.h>
#include <stdio.h>

#ifndef NATIVE_BUILD
#  include <LittleFS.h>
#  include <Arduino.h>
#endif

// ─────────────────────────────────────────────────────────────────
// MemoryTool — read and write agent memory from LittleFS.
//
// Operations (via "op" field in argsJson):
//   write — append text to /workspace/MEMORY.md (long-term memory)
//   note  — append text to today's daily note
//           /workspace/notes/<day>.md
//   read  — read the contents of MEMORY.md
//
// This tool is callable by the LLM so PCB-Claw can persist facts
// when the user asks it to remember something.
// ─────────────────────────────────────────────────────────────────

class MemoryTool : public ITool {
public:
    const char* name()        const override { return "memory"; }
    const char* description() const override {
        return "Read or write agent long-term memory and daily notes";
    }
    const char* argSchema()   const override {
        return "{"
            "\"type\":\"object\","
            "\"properties\":{"
                "\"op\":{"
                    "\"type\":\"string\","
                    "\"enum\":[\"write\",\"note\",\"read\"]"
                "},"
                "\"text\":{\"type\":\"string\"}"
            "},"
            "\"required\":[\"op\"]"
        "}";
    }

    bool execute(const char* argsJson,
                 char*       resultBuf,
                 size_t      resultLen) override
    {
        char op[16]    = {};
        char text[512] = {};
        _extractStr(argsJson, "op",   op,   sizeof(op));
        _extractStr(argsJson, "text", text, sizeof(text));

        if (strcmp(op, "write") == 0) {
            if (text[0] == '\0') {
                snprintf(resultBuf, resultLen,
                         "{\"error\":\"text required for write\"}");
                return false;
            }
            bool ok = _appendFile("/workspace/MEMORY.md", text);
            snprintf(resultBuf, resultLen,
                     ok ? "{\"ok\":true,\"op\":\"write\"}"
                        : "{\"error\":\"memory write failed\"}");
            return ok;

        } else if (strcmp(op, "note") == 0) {
            if (text[0] == '\0') {
                snprintf(resultBuf, resultLen,
                         "{\"error\":\"text required for note\"}");
                return false;
            }
            char path[64];
            _todayNotePath(path, sizeof(path));
            bool ok = _appendFile(path, text);
            snprintf(resultBuf, resultLen,
                     ok ? "{\"ok\":true,\"op\":\"note\"}"
                        : "{\"error\":\"note write failed\"}");
            return ok;

        } else if (strcmp(op, "read") == 0) {
            // Read MEMORY.md content into result buffer directly
            size_t n = _readFile("/workspace/MEMORY.md",
                                  resultBuf, resultLen);
            if (n == 0) {
                snprintf(resultBuf, resultLen, "{\"memory\":\"\"}");
            }
            return true;

        } else {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"unknown op '%s'\"}", op);
            return false;
        }
    }

private:
    // Append text (+ newline if missing) to a LittleFS file.
    static bool _appendFile(const char* path, const char* text) {
#ifndef NATIVE_BUILD
        File f = LittleFS.open(path, "a");
        if (!f) { f = LittleFS.open(path, "w"); }
        if (!f) return false;
        f.print(text);
        size_t tLen = strlen(text);
        if (tLen > 0 && text[tLen - 1] != '\n') f.print('\n');
        f.close();
        return true;
#else
        (void)path; (void)text;
        return true;   // silently succeed in tests
#endif
    }

    // Read a LittleFS file into buf; returns bytes read.
    static size_t _readFile(const char* path, char* buf, size_t bufLen) {
#ifndef NATIVE_BUILD
        if (!LittleFS.exists(path)) { buf[0] = '\0'; return 0; }
        File f = LittleFS.open(path, "r");
        if (!f) { buf[0] = '\0'; return 0; }
        size_t n = f.readBytes(buf, bufLen - 1);
        f.close();
        buf[n] = '\0';
        return n;
#else
        (void)path;
        snprintf(buf, bufLen, "{\"memory\":\"\"}");
        return strlen(buf);
#endif
    }

    // Today's daily note path (day = millis / 86400000).
    static void _todayNotePath(char* buf, size_t len) {
#ifndef NATIVE_BUILD
        uint32_t day = millis() / 86400000UL;
        snprintf(buf, len, "/workspace/notes/%lu.md", (unsigned long)day);
#else
        snprintf(buf, len, "/workspace/notes/0.md");
#endif
    }

    // Minimal JSON string extractor (same pattern as GpioTool).
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
