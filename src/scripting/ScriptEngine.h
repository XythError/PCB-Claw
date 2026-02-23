#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#  include <esp_heap_caps.h>
#endif

// ─────────────────────────────────────────────────────────────────
// ScriptEngine — lightweight Lua scripting engine for dynamic skill
// execution at runtime.
//
// Provides the agent with the ability to write and execute small Lua
// scripts directly on the device without recompiling the firmware —
// enabling iterative skill development analogous to how the LLM
// generates function calls.
//
// Architecture:
//   • Each execute() call creates a fresh, sandboxed Lua state.
//   • The Lua VM heap is allocated from PSRAM to avoid competing
//     with the WiFi/TLS stacks on the internal heap.
//   • Scripts are stateless between calls (mirrors the ReasoningLoop
//     design); persistent state is managed via the RamVfs/MemoryTool.
//   • executeFile() reads a script from the RamVfs scratchpad first,
//     then falls back to LittleFS.
//
// Enabling real Lua execution (ESP32):
//   1. Add a Lua 5.1/5.3 Arduino port to lib_deps in platformio.ini,
//      e.g. a bundled lua source compiled as a library.
//   2. Define PCBCLAW_LUA_ENABLED=1 in build_flags.
//
// Without PCBCLAW_LUA_ENABLED the engine runs in stub mode — all
// scripts are accepted and a synthetic output is returned.  This
// lets the rest of the agent (intent detection, ScriptTool, lane
// routing) be tested without a full Lua toolchain.
// ─────────────────────────────────────────────────────────────────

static constexpr size_t SCRIPT_CODE_LEN   = 2048;
static constexpr size_t SCRIPT_RESULT_LEN = 512;
static constexpr size_t SCRIPT_LUA_HEAP   = 32768;  // 32 KB per execution

// Result of a single script execution.
struct ScriptResult {
    bool ok                         = false;
    char output[SCRIPT_RESULT_LEN]  = {};
    char error[128]                 = {};
};

// ─────────────────────────────────────────────────────────────────

class ScriptEngine {
public:
    ScriptEngine() = default;

    // Execute a Lua script given as a null-terminated string.
    // The Lua VM heap is taken from PSRAM when available.
    ScriptResult execute(const char* code) {
        ScriptResult res;
        if (!code || code[0] == '\0') {
            snprintf(res.error, sizeof(res.error), "empty script");
            return res;
        }

#if defined(PCBCLAW_LUA_ENABLED) && !defined(NATIVE_BUILD)
        _executeLua(code, res);
#else
        _executeStub(code, res);
#endif
        return res;
    }

    // Load a script from the filesystem (RamVfs or LittleFS) and execute it.
    ScriptResult executeFile(const char* path) {
        ScriptResult res;
        if (!path || path[0] == '\0') {
            snprintf(res.error, sizeof(res.error), "empty path");
            return res;
        }
#ifndef NATIVE_BUILD
        char code[SCRIPT_CODE_LEN] = {};
        size_t n = _readFile(path, code, sizeof(code));
        if (n == 0) {
            snprintf(res.error, sizeof(res.error),
                     "script not found: %.48s", path);
            return res;
        }
        return execute(code);
#else
        snprintf(res.output, sizeof(res.output),
                 "[stub] would execute file: %.48s", path);
        res.ok = true;
        return res;
#endif
    }

    // Returns true when real Lua execution is compiled in.
    static bool luaAvailable() {
#if defined(PCBCLAW_LUA_ENABLED)
        return true;
#else
        return false;
#endif
    }

private:
// ── Real Lua execution ────────────────────────────────────────────
#if defined(PCBCLAW_LUA_ENABLED) && !defined(NATIVE_BUILD)
    // Forward declarations; lua.h is provided by the Lua library in
    // lib_deps.  Included here only when PCBCLAW_LUA_ENABLED is set.
#  include <lua.h>
#  include <lauxlib.h>
#  include <lualib.h>

    void _executeLua(const char* code, ScriptResult& res) {
        // Allocate Lua VM heap from PSRAM (fallback to internal heap).
        void* luaHeap = heap_caps_malloc(SCRIPT_LUA_HEAP,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!luaHeap) luaHeap = malloc(SCRIPT_LUA_HEAP);
        if (!luaHeap) {
            snprintf(res.error, sizeof(res.error), "OOM: lua heap");
            return;
        }

        lua_State* L = lua_newstate(_luaAlloc, luaHeap);
        if (!L) {
            ::free(luaHeap);
            snprintf(res.error, sizeof(res.error), "lua_newstate failed");
            return;
        }

        luaL_openlibs(L);

        // Redirect print() into res.output via a captured upvalue.
        lua_pushlightuserdata(L, &res);
        lua_pushcclosure(L, _luaPrint, 1);
        lua_setglobal(L, "print");

        int rc = luaL_dostring(L, code);
        if (rc != LUA_OK) {
            const char* msg = lua_tostring(L, -1);
            snprintf(res.error, sizeof(res.error),
                     "lua: %.100s", msg ? msg : "unknown error");
            lua_pop(L, 1);
        } else {
            res.ok = true;
        }

        lua_close(L);
        ::free(luaHeap);
    }

    // PSRAM-backed Lua allocator.
    static void* _luaAlloc(void* /*ud*/, void* ptr, size_t /*osize*/, size_t nsize) {
        if (nsize == 0) { ::free(ptr); return nullptr; }
        return realloc(ptr, nsize);
    }

    // Lua print() capture: appends to res.output.
    static int _luaPrint(lua_State* L) {
        ScriptResult* res = static_cast<ScriptResult*>(
            lua_touserdata(L, lua_upvalueindex(1)));
        if (!res) return 0;
        const char* s = luaL_tolstring(L, 1, nullptr);
        if (s) {
            size_t cur = strlen(res->output);
            size_t avail = SCRIPT_RESULT_LEN - 1 - cur;
            if (avail > 0) strncat(res->output, s, avail);
        }
        return 0;
    }
#endif  // PCBCLAW_LUA_ENABLED

// ── Stub mode (native builds + no Lua library) ────────────────────
    static void _executeStub(const char* code, ScriptResult& res) {
        // Reject obviously malformed input.
        if (strchr(code, '`') != nullptr) {
            snprintf(res.error, sizeof(res.error),
                     "stub: unsupported character in script");
            return;
        }
        snprintf(res.output, sizeof(res.output),
                 "[stub] executed %zu byte(s) of Lua", strlen(code));
        res.ok = true;
    }

#ifndef NATIVE_BUILD
    // Read a script file from LittleFS.
    static size_t _readFile(const char* path, char* buf, size_t bufLen) {
        if (!LittleFS.exists(path)) return 0;
        File f = LittleFS.open(path, "r");
        if (!f) return 0;
        size_t n = f.readBytes(buf, bufLen - 1);
        f.close();
        buf[n] = '\0';
        return n;
    }
#endif
};
