#pragma once

#include "Tool.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#  include <LittleFS.h>
#endif

// ─────────────────────────────────────────────────────────────────
// FileTool — general-purpose filesystem operations for the agent.
//
// Supported operations (via "op" field in argsJson):
//   read   — read file contents:
//            { "path": "/workspace/file.txt" }
//   write  — create or overwrite a file:
//            { "path": "/workspace/file.txt", "content": "..." }
//   append — append to a file (create if missing):
//            { "path": "/workspace/file.txt", "content": "..." }
//   edit   — find-and-replace text inside a file:
//            { "path": "/workspace/file.txt",
//              "old": "find this", "new": "replace with" }
//   list   — list directory entries:
//            { "path": "/workspace/" }
//
// On ESP32 all paths refer to LittleFS.
// In NATIVE_BUILD the implementation is simulated for unit tests.
// ─────────────────────────────────────────────────────────────────

static constexpr size_t FILE_TOOL_CONTENT_LEN = 2048;
static constexpr size_t FILE_TOOL_PATH_LEN    = 128;

class FileTool : public ITool {
public:
    const char* name()        const override { return "file"; }
    const char* description() const override {
        return "Read, write, append, edit, and list files on the filesystem";
    }
    const char* argSchema()   const override {
        return "{"
            "\"type\":\"object\","
            "\"properties\":{"
                "\"op\":{"
                    "\"type\":\"string\","
                    "\"enum\":[\"read\",\"write\",\"append\","
                               "\"edit\",\"list\"]"
                "},"
                "\"path\":{\"type\":\"string\"},"
                "\"content\":{\"type\":\"string\"},"
                "\"old\":{\"type\":\"string\"},"
                "\"new\":{\"type\":\"string\"}"
            "},"
            "\"required\":[\"op\",\"path\"]"
        "}";
    }

    bool execute(const char* argsJson,
                 char*       resultBuf,
                 size_t      resultLen) override
    {
        char op[16]   = {};
        char path[FILE_TOOL_PATH_LEN] = {};
        _extractStr(argsJson, "op",   op,   sizeof(op));
        _extractStr(argsJson, "path", path, sizeof(path));

        if (path[0] == '\0') {
            snprintf(resultBuf, resultLen, "{\"error\":\"path required\"}");
            return false;
        }

        if (strcmp(op, "read") == 0) {
            return _opRead(path, resultBuf, resultLen);

        } else if (strcmp(op, "write") == 0) {
            char content[FILE_TOOL_CONTENT_LEN] = {};
            _extractStr(argsJson, "content", content, sizeof(content));
            return _opWrite(path, content, false, resultBuf, resultLen);

        } else if (strcmp(op, "append") == 0) {
            char content[FILE_TOOL_CONTENT_LEN] = {};
            _extractStr(argsJson, "content", content, sizeof(content));
            return _opWrite(path, content, true, resultBuf, resultLen);

        } else if (strcmp(op, "edit") == 0) {
            char oldText[FILE_TOOL_CONTENT_LEN / 2] = {};
            char newText[FILE_TOOL_CONTENT_LEN / 2] = {};
            _extractStr(argsJson, "old", oldText, sizeof(oldText));
            _extractStr(argsJson, "new", newText, sizeof(newText));
            return _opEdit(path, oldText, newText, resultBuf, resultLen);

        } else if (strcmp(op, "list") == 0) {
            return _opList(path, resultBuf, resultLen);

        } else {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"unknown op '%s'\"}", op);
            return false;
        }
    }

private:
    // ── Operation implementations ────────────────────────────────

    static bool _opRead(const char* path, char* resultBuf, size_t resultLen) {
#ifndef NATIVE_BUILD
        if (!LittleFS.exists(path)) {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"file not found\",\"path\":\"%s\"}", path);
            return false;
        }
        File f = LittleFS.open(path, "r");
        if (!f) {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"open failed\",\"path\":\"%s\"}", path);
            return false;
        }
        // Read content, leaving room for JSON envelope
        const size_t maxContent = resultLen > 64 ? resultLen - 64 : 0;
        char* tmp = static_cast<char*>(malloc(maxContent + 1));
        if (!tmp) {
            f.close();
            snprintf(resultBuf, resultLen, "{\"error\":\"out of memory\"}");
            return false;
        }
        size_t n = f.readBytes(tmp, maxContent);
        f.close();
        tmp[n] = '\0';
        char* escaped = static_cast<char*>(malloc(n * 2 + 1));
        if (!escaped) {
            ::free(tmp);
            snprintf(resultBuf, resultLen, "{\"error\":\"out of memory\"}");
            return false;
        }
        size_t ei = _jsonEscape(tmp, n, escaped, n * 2 + 1);
        escaped[ei] = '\0';
        snprintf(resultBuf, resultLen,
                 "{\"ok\":true,\"path\":\"%s\",\"content\":\"%s\"}",
                 path, escaped);
        ::free(tmp);
        ::free(escaped);
        return true;
#else
        snprintf(resultBuf, resultLen,
                 "{\"ok\":true,\"path\":\"%s\",\"content\":\"\","
                 "\"simulated\":true}", path);
        return true;
#endif
    }

    static bool _opWrite(const char* path, const char* content, bool append,
                          char* resultBuf, size_t resultLen) {
#ifndef NATIVE_BUILD
        File f = LittleFS.open(path, append ? "a" : "w");
        if (!f) {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"open failed\",\"path\":\"%s\"}", path);
            return false;
        }
        size_t written = f.print(content);
        f.close();
        snprintf(resultBuf, resultLen,
                 "{\"ok\":true,\"path\":\"%s\",\"bytes\":%u}",
                 path, (unsigned)written);
        return true;
#else
        (void)append;
        snprintf(resultBuf, resultLen,
                 "{\"ok\":true,\"path\":\"%s\",\"simulated\":true}", path);
        return true;
#endif
    }

    static bool _opEdit(const char* path,
                        const char* oldText, const char* newText,
                        char* resultBuf, size_t resultLen) {
        if (oldText[0] == '\0') {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"old text required for edit\"}");
            return false;
        }
#ifndef NATIVE_BUILD
        if (!LittleFS.exists(path)) {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"file not found\",\"path\":\"%s\"}", path);
            return false;
        }
        File fr = LittleFS.open(path, "r");
        if (!fr) {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"open failed\",\"path\":\"%s\"}", path);
            return false;
        }
        char* src = static_cast<char*>(malloc(FILE_TOOL_CONTENT_LEN));
        if (!src) {
            fr.close();
            snprintf(resultBuf, resultLen, "{\"error\":\"out of memory\"}");
            return false;
        }
        size_t n = fr.readBytes(src, FILE_TOOL_CONTENT_LEN - 1);
        fr.close();
        src[n] = '\0';

        const char* pos = strstr(src, oldText);
        if (!pos) {
            ::free(src);
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"old text not found in file\"}");
            return false;
        }

        char* dst = static_cast<char*>(malloc(FILE_TOOL_CONTENT_LEN));
        if (!dst) {
            ::free(src);
            snprintf(resultBuf, resultLen, "{\"error\":\"out of memory\"}");
            return false;
        }

        size_t prefixLen = (size_t)(pos - src);
        size_t oldLen    = strlen(oldText);
        size_t newLen    = strlen(newText);
        size_t suffixLen = n - prefixLen - oldLen;
        size_t total     = prefixLen + newLen + suffixLen;
        if (total >= FILE_TOOL_CONTENT_LEN) {
            ::free(src); ::free(dst);
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"result too large after edit\"}");
            return false;
        }
        memcpy(dst, src, prefixLen);
        memcpy(dst + prefixLen, newText, newLen);
        memcpy(dst + prefixLen + newLen, pos + oldLen, suffixLen);
        dst[total] = '\0';

        File fw = LittleFS.open(path, "w");
        if (!fw) {
            ::free(src); ::free(dst);
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"write failed\",\"path\":\"%s\"}", path);
            return false;
        }
        fw.print(dst);
        fw.close();
        ::free(src); ::free(dst);
        snprintf(resultBuf, resultLen,
                 "{\"ok\":true,\"path\":\"%s\"}", path);
        return true;
#else
        snprintf(resultBuf, resultLen,
                 "{\"ok\":true,\"path\":\"%s\",\"simulated\":true}", path);
        return true;
#endif
    }

    static bool _opList(const char* path, char* resultBuf, size_t resultLen) {
#ifndef NATIVE_BUILD
        File dir = LittleFS.open(path);
        if (!dir || !dir.isDirectory()) {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"not a directory\",\"path\":\"%s\"}", path);
            return false;
        }
        size_t pos = 0;
        pos += snprintf(resultBuf + pos, resultLen - pos,
                        "{\"ok\":true,\"path\":\"%s\",\"entries\":[", path);
        bool first = true;
        File entry = dir.openNextFile();
        while (entry && pos < resultLen - 4) {
            if (!first) {
                pos += snprintf(resultBuf + pos, resultLen - pos, ",");
            }
            pos += snprintf(resultBuf + pos, resultLen - pos,
                            "\"%s\"", entry.name());
            first = false;
            entry = dir.openNextFile();
        }
        snprintf(resultBuf + pos, resultLen - pos, "]}");
        return true;
#else
        snprintf(resultBuf, resultLen,
                 "{\"ok\":true,\"path\":\"%s\",\"entries\":[],"
                 "\"simulated\":true}", path);
        return true;
#endif
    }

    // ── Helpers ──────────────────────────────────────────────────

    // JSON-escape src[srcLen] into dst[dstLen]; returns bytes written.
    static size_t _jsonEscape(const char* src, size_t srcLen,
                               char* dst, size_t dstLen) {
        size_t di = 0;
        for (size_t i = 0; i < srcLen && di + 2 < dstLen; ++i) {
            char c = src[i];
            if (c == '"')       { dst[di++] = '\\'; dst[di++] = '"'; }
            else if (c == '\\') { dst[di++] = '\\'; dst[di++] = '\\'; }
            else if (c == '\n') { dst[di++] = '\\'; dst[di++] = 'n'; }
            else if (c == '\r') { dst[di++] = '\\'; dst[di++] = 'r'; }
            else if (c == '\t') { dst[di++] = '\\'; dst[di++] = 't'; }
            else                { dst[di++] = c; }
        }
        return di;
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
        while (*p && i < outLen - 1) {
            if (*p == '\\' && *(p + 1) != '\0') {
                char esc = *(p + 1);
                if      (esc == '"')  { out[i++] = '"';  p += 2; }
                else if (esc == '\\') { out[i++] = '\\'; p += 2; }
                else if (esc == 'n')  { out[i++] = '\n'; p += 2; }
                else if (esc == 'r')  { out[i++] = '\r'; p += 2; }
                else if (esc == 't')  { out[i++] = '\t'; p += 2; }
                else                  { out[i++] = *p++; }
            } else if (*p == '"') {
                break;
            } else {
                out[i++] = *p++;
            }
        }
        out[i] = '\0';
    }
};
