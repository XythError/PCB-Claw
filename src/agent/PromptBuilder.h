#pragma once

#include "../tools/ToolRegistry.h"
#include "../config/ConfigManager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef NATIVE_BUILD
#  include <LittleFS.h>
#  include <Arduino.h>
#  include <esp_heap_caps.h>
#endif

// ─────────────────────────────────────────────────────────────────
// PromptBuilder — assembles the dynamic system prompt from multiple
// sources in LittleFS and live agent state.
//
// Build order:
//   1. Identity   (runtime info, workspace paths, tool list)
//   2. Filesystem tree  (only if PCBCLAW_PROMPT_INCLUDE_TREE == 1)
//   3. Bootstrap files: AGENT.md → SOUL.md → USER.md → IDENTITY.md
//   4. Skills     (name + location + compact description per tool)
//   5. HTTP server status
//   6. Memory     (MEMORY.md + last 3 daily notes)
//
// Workspace paths used:
//   /workspace/AGENT.md, SOUL.md, USER.md, IDENTITY.md
//   /workspace/MEMORY.md
//   /workspace/notes/<day>.md   (day = millis/86400000)
// ─────────────────────────────────────────────────────────────────

static constexpr size_t PROMPT_BUF_LEN = 4096;

// Bootstrap file paths (workspace-relative)
static constexpr const char* WS_AGENT_MD    = "/workspace/AGENT.md";
static constexpr const char* WS_SOUL_MD     = "/workspace/SOUL.md";
static constexpr const char* WS_USER_MD     = "/workspace/USER.md";
static constexpr const char* WS_IDENTITY_MD = "/workspace/IDENTITY.md";
static constexpr const char* WS_MEMORY_MD   = "/workspace/MEMORY.md";
static constexpr const char* WS_NOTES_DIR   = "/workspace/notes";

class PromptBuilder {
public:
    PromptBuilder(ToolRegistry& tools, ConfigManager& config)
        : _tools(tools), _config(config), _webServerRunning(false) {}

    void setWebServerRunning(bool running) { _webServerRunning = running; }

    // Build the complete system prompt into buf (size >= PROMPT_BUF_LEN).
    // Returns number of bytes written.
    size_t build(char* buf, size_t bufLen) {
        if (!buf || bufLen == 0) return 0;
        buf[0] = '\0';
        size_t pos = 0;

        pos = _appendIdentity(buf, pos, bufLen);

        // Optional filesystem tree
        if (_config.getInt("PCBCLAW_PROMPT_INCLUDE_TREE", 0) == 1) {
            pos = _appendFsTree(buf, pos, bufLen);
        }

        pos = _appendBootstrap(buf, pos, bufLen);
        pos = _appendSkills(buf, pos, bufLen);
        pos = _appendHttpStatus(buf, pos, bufLen);
        pos = _appendMemory(buf, pos, bufLen);

        return pos;
    }

    // Append text to MEMORY.md (long-term memory).
    bool appendMemory(const char* text) {
        if (!text || text[0] == '\0') return false;
#ifndef NATIVE_BUILD
        File f = LittleFS.open(WS_MEMORY_MD, "a");
        if (!f) { f = LittleFS.open(WS_MEMORY_MD, "w"); }
        if (!f) return false;
        f.print(text);
        size_t tLen = strlen(text);
        if (tLen > 0 && text[tLen - 1] != '\n') f.print('\n');
        f.close();
        return true;
#else
        (void)text;
        return true;
#endif
    }

    // Append text to today's daily note.
    bool appendDailyNote(const char* text) {
        if (!text || text[0] == '\0') return false;
#ifndef NATIVE_BUILD
        char path[64];
        todayNotePath(path, sizeof(path));
        File f = LittleFS.open(path, "a");
        if (!f) { f = LittleFS.open(path, "w"); }
        if (!f) return false;
        f.print(text);
        size_t tLen2 = strlen(text);
        if (tLen2 > 0 && text[tLen2 - 1] != '\n') f.print('\n');
        f.close();
        return true;
#else
        (void)text;
        return true;
#endif
    }

    // Returns today's daily note path (/workspace/notes/<day>.md).
    // Day number is derived from millis() (no RTC required).
    static void todayNotePath(char* buf, size_t len) {
#ifndef NATIVE_BUILD
        uint32_t day = millis() / 86400000UL;
        snprintf(buf, len, "%s/%lu.md", WS_NOTES_DIR, (unsigned long)day);
#else
        snprintf(buf, len, "%s/0.md", WS_NOTES_DIR);
#endif
    }

private:
    ToolRegistry&  _tools;
    ConfigManager& _config;
    bool           _webServerRunning;

    // ── Generic append (bounds-checked) ──────────────────────────
    static size_t _append(char* buf, size_t pos, size_t maxLen,
                           const char* s) {
        if (!s || pos >= maxLen - 1) return pos;
        size_t slen  = strlen(s);
        size_t avail = maxLen - 1 - pos;
        if (slen > avail) slen = avail;
        memcpy(buf + pos, s, slen);
        pos += slen;
        buf[pos] = '\0';
        return pos;
    }

    // ── Section: Identity ─────────────────────────────────────────
    size_t _appendIdentity(char* buf, size_t pos, size_t maxLen) {
        pos = _append(buf, pos, maxLen, "## Identity\n");

#ifndef NATIVE_BUILD
        uint32_t upSec = millis() / 1000UL;
        char runtime[80];
        snprintf(runtime, sizeof(runtime),
                 "Device: PCB-Claw (ESP32-S3)\nUptime: %lus\n",
                 (unsigned long)upSec);
        pos = _append(buf, pos, maxLen, runtime);
#else
        pos = _append(buf, pos, maxLen, "Device: PCB-Claw (ESP32-S3)\n");
#endif

        // Workspace paths
        const char* ws = _config.get("workspace_path", "/workspace");
        char paths[256];
        snprintf(paths, sizeof(paths),
                 "Workspace: %s\n"
                 "Memory:    %s/MEMORY.md\n"
                 "Notes:     %s/notes/\n"
                 "Bootstrap: %s/{AGENT,SOUL,USER,IDENTITY}.md\n",
                 ws, ws, ws, ws);
        pos = _append(buf, pos, maxLen, paths);

        // Live tool list (includes dynamically registered tools)
        char header[48];
        snprintf(header, sizeof(header), "Tool count: %d\nTools:\n",
                 _tools.count());
        pos = _append(buf, pos, maxLen, header);
        char toolList[512] = {};
        _tools.listTools(toolList, sizeof(toolList));
        pos = _append(buf, pos, maxLen, toolList);
        pos = _append(buf, pos, maxLen, "\n");

        return pos;
    }

    // ── Section: Filesystem tree (optional) ───────────────────────
    size_t _appendFsTree(char* buf, size_t pos, size_t maxLen) {
        pos = _append(buf, pos, maxLen, "## Filesystem\n");
#ifndef NATIVE_BUILD
        const char* dirs[] = {
            "/workspace", "/workspace/notes", "/config", nullptr
        };
        for (int i = 0; dirs[i]; ++i) {
            if (!LittleFS.exists(dirs[i])) continue;
            char line[80];
            snprintf(line, sizeof(line), "  %s/\n", dirs[i]);
            pos = _append(buf, pos, maxLen, line);
            File dir = LittleFS.open(dirs[i]);
            if (dir) {
                File entry = dir.openNextFile();
                while (entry) {
                    snprintf(line, sizeof(line), "    %s\n", entry.name());
                    pos = _append(buf, pos, maxLen, line);
                    entry = dir.openNextFile();
                }
                dir.close();
            }
        }
#else
        pos = _append(buf, pos, maxLen, "  /workspace/\n  /config/\n");
#endif
        pos = _append(buf, pos, maxLen, "\n");
        return pos;
    }

    // ── Section: Bootstrap files (AGENT → SOUL → USER → IDENTITY) ─
    size_t _appendBootstrap(char* buf, size_t pos, size_t maxLen) {
        static const char* const kPaths[] = {
            WS_AGENT_MD, WS_SOUL_MD, WS_USER_MD, WS_IDENTITY_MD, nullptr
        };
        static const char* const kLabels[] = {
            "AGENT", "SOUL", "USER", "IDENTITY", nullptr
        };

        for (int i = 0; kPaths[i]; ++i) {
            char content[1024] = {};
            if (_readFile(kPaths[i], content, sizeof(content)) > 0) {
                char header[32];
                snprintf(header, sizeof(header), "## %s\n", kLabels[i]);
                pos = _append(buf, pos, maxLen, header);
                pos = _append(buf, pos, maxLen, content);
                size_t cLen = strlen(content);
                if (cLen > 0 && content[cLen - 1] != '\n') {
                    pos = _append(buf, pos, maxLen, "\n");
                }
                pos = _append(buf, pos, maxLen, "\n");
            }
        }
        return pos;
    }

    // ── Section: Skills (compact: name @ location: description) ───
    size_t _appendSkills(char* buf, size_t pos, size_t maxLen) {
        pos = _append(buf, pos, maxLen, "## Skills\n");

        const char* ws = _config.get("workspace_path", "/workspace");
        char toolsBuf[512] = {};
        _tools.listTools(toolsBuf, sizeof(toolsBuf));

        // listTools() produces "name: description\n" lines.
        // Reformat as: "- name @ ws/tools/name: description"
        const char* p = toolsBuf;
        while (*p) {
            const char* nlPos  = strchr(p, '\n');
            const char* colPos = strchr(p, ':');
            if (!colPos || (nlPos && colPos > nlPos)) {
                if (nlPos) { p = nlPos + 1; continue; }
                break;
            }

            // Name
            char name[TOOL_NAME_LEN] = {};
            size_t nameLen = (size_t)(colPos - p);
            if (nameLen >= TOOL_NAME_LEN) nameLen = TOOL_NAME_LEN - 1;
            memcpy(name, p, nameLen);
            while (nameLen > 0 && name[nameLen - 1] == ' ') {
                name[--nameLen] = '\0';
            }

            // Description (after ": ")
            const char* descStart = colPos + 1;
            while (*descStart == ' ') ++descStart;
            size_t descLen = nlPos ? (size_t)(nlPos - descStart)
                                   : strlen(descStart);
            char desc[128] = {};
            if (descLen >= sizeof(desc)) descLen = sizeof(desc) - 1;
            memcpy(desc, descStart, descLen);

            char line[256];
            snprintf(line, sizeof(line),
                     "- %s @ %s/tools/%s: %s\n",
                     name, ws, name, desc);
            pos = _append(buf, pos, maxLen, line);

            p = nlPos ? nlPos + 1 : p + strlen(p);
        }
        pos = _append(buf, pos, maxLen, "\n");
        return pos;
    }

    // ── Section: HTTP server status ───────────────────────────────
    size_t _appendHttpStatus(char* buf, size_t pos, size_t maxLen) {
        char line[48];
        snprintf(line, sizeof(line), "HTTP Server: %s\n\n",
                 _webServerRunning ? "running" : "not running");
        pos = _append(buf, pos, maxLen, line);
        return pos;
    }

    // ── Section: Memory (MEMORY.md + recent daily notes) ─────────
    size_t _appendMemory(char* buf, size_t pos, size_t maxLen) {
        pos = _append(buf, pos, maxLen, "## Memory\n");

        // Long-term memory from MEMORY.md (header always present)
        pos = _append(buf, pos, maxLen, "### Long-term\n");
        char memBuf[1024] = {};
        if (_readFile(WS_MEMORY_MD, memBuf, sizeof(memBuf)) > 0) {
            pos = _append(buf, pos, maxLen, memBuf);
            size_t mLen = strlen(memBuf);
            if (mLen > 0 && memBuf[mLen - 1] != '\n') {
                pos = _append(buf, pos, maxLen, "\n");
            }
        }

        // Recent daily notes — last 3 days (header always present)
        pos = _append(buf, pos, maxLen, "### Recent Notes\n");
#ifndef NATIVE_BUILD
        uint32_t today = millis() / 86400000UL;
        uint32_t start = today > 2 ? today - 2 : 0;
        for (uint32_t d = start; d <= today; ++d) {
            char notePath[64];
            snprintf(notePath, sizeof(notePath),
                     "%s/%lu.md", WS_NOTES_DIR, (unsigned long)d);
            char noteBuf[512] = {};
            if (_readFile(notePath, noteBuf, sizeof(noteBuf)) > 0) {
                char hdr[48];
                snprintf(hdr, sizeof(hdr), "#### Day %lu\n", (unsigned long)d);
                pos = _append(buf, pos, maxLen, hdr);
                pos = _append(buf, pos, maxLen, noteBuf);
                size_t nLen = strlen(noteBuf);
                if (nLen > 0 && noteBuf[nLen - 1] != '\n') {
                    pos = _append(buf, pos, maxLen, "\n");
                }
            }
        }
#endif
        pos = _append(buf, pos, maxLen, "\n");
        return pos;
    }

    // ── Read a LittleFS file; returns bytes read (0 if not found) ─
    static size_t _readFile(const char* path, char* buf, size_t bufLen) {
#ifndef NATIVE_BUILD
        if (!LittleFS.exists(path)) return 0;
        File f = LittleFS.open(path, "r");
        if (!f) return 0;
        size_t n = f.readBytes(buf, bufLen - 1);
        f.close();
        buf[n] = '\0';
        return n;
#else
        (void)path; (void)buf; (void)bufLen;
        return 0;
#endif
    }
};
