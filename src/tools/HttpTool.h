#pragma once

#include "Tool.h"
#include <string.h>
#include <stdio.h>

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#  include <HTTPClient.h>
#  include <WiFiClientSecure.h>
#endif

// ─────────────────────────────────────────────────────────────────
// HttpTool — make HTTP requests from the ESP32.
//
// Supported operations (via "method" field):
//   GET  / POST / PUT / DELETE / PATCH
//
// Arguments JSON:
//   { "method": "GET",
//     "url": "https://api.example.com/data",
//     "headers": {"Authorization": "Bearer TOKEN"},
//     "body": "{\"key\":\"value\"}",
//     "timeout": 5000 }
//
// Response JSON:
//   { "status": 200, "body": "..." }
// ─────────────────────────────────────────────────────────────────

static constexpr uint16_t HTTP_TOOL_DEFAULT_TIMEOUT = 10000;
static constexpr size_t   HTTP_RESPONSE_MAX         = 2048;

class HttpTool : public ITool {
public:
    const char* name()        const override { return "http"; }
    const char* description() const override {
        return "Make HTTP requests (GET/POST/PUT/DELETE) to external APIs";
    }
    const char* argSchema()   const override {
        return "{"
            "\"type\":\"object\","
            "\"properties\":{"
                "\"method\":{\"type\":\"string\","
                    "\"enum\":[\"GET\",\"POST\",\"PUT\","
                               "\"DELETE\",\"PATCH\"]},"
                "\"url\":{\"type\":\"string\"},"
                "\"body\":{\"type\":\"string\"},"
                "\"timeout\":{\"type\":\"integer\"}"
            "},"
            "\"required\":[\"method\",\"url\"]"
        "}";
    }

    bool execute(const char* argsJson,
                 char*       resultBuf,
                 size_t      resultLen) override
    {
        char method[8]  = {};
        char url[256]   = {};
        char body[1024] = {};
        int  timeout    = HTTP_TOOL_DEFAULT_TIMEOUT;

        _extractStr(argsJson, "method",  method, sizeof(method));
        _extractStr(argsJson, "url",     url,    sizeof(url));
        _extractStr(argsJson, "body",    body,   sizeof(body));
        int t = _extractInt(argsJson, "timeout", -1);
        if (t > 0) timeout = t;

        if (url[0] == '\0') {
            snprintf(resultBuf, resultLen, "{\"error\":\"url required\"}");
            return false;
        }

#ifndef NATIVE_BUILD
        HTTPClient http;
        http.setTimeout(timeout);
        http.begin(url);
        http.addHeader("Content-Type", "application/json");

        int code = -1;
        if (strcmp(method, "GET") == 0) {
            code = http.GET();
        } else if (strcmp(method, "POST") == 0) {
            code = http.POST((uint8_t*)body, strlen(body));
        } else if (strcmp(method, "PUT") == 0) {
            code = http.PUT((uint8_t*)body, strlen(body));
        } else if (strcmp(method, "DELETE") == 0) {
            code = http.sendRequest("DELETE");
        } else if (strcmp(method, "PATCH") == 0) {
            code = http.sendRequest("PATCH", (uint8_t*)body, strlen(body));
        } else {
            http.end();
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"unknown method '%s'\"}", method);
            return false;
        }

        if (code > 0) {
            String resp = http.getString();
            http.end();
            // Truncate response to fit in resultBuf
            size_t rlen = resp.length();
            if (rlen > HTTP_RESPONSE_MAX) rlen = HTTP_RESPONSE_MAX;
            // Escape quotes in response for embedding in JSON
            char escaped[HTTP_RESPONSE_MAX * 2];
            size_t ei = 0;
            for (size_t i = 0; i < rlen && ei < sizeof(escaped) - 2; ++i) {
                char c = resp[i];
                if (c == '"')       { escaped[ei++] = '\\'; escaped[ei++] = '"'; }
                else if (c == '\\') { escaped[ei++] = '\\'; escaped[ei++] = '\\'; }
                else if (c == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; }
                else if (c == '\r') { /* skip */ }
                else                { escaped[ei++] = c; }
            }
            escaped[ei] = '\0';
            snprintf(resultBuf, resultLen,
                     "{\"status\":%d,\"body\":\"%s\"}", code, escaped);
        } else {
            http.end();
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"HTTP failed\",\"code\":%d}", code);
            return false;
        }
#else
        snprintf(resultBuf, resultLen,
                 "{\"status\":200,\"body\":\"simulated\",\"url\":\"%s\"}",
                 url);
#endif
        return true;
    }

private:
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
        ++p;
        size_t i = 0;
        while (*p && *p != '"' && i < outLen - 1) {
            if (*p == '\\' && *(p+1) == '"') { out[i++] = '"'; p += 2; }
            else out[i++] = *p++;
        }
        out[i] = '\0';
    }
};
