#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#ifndef NATIVE_BUILD
#  include <esp_heap_caps.h>
#endif

// ─────────────────────────────────────────────────────────────────
// PsramAllocator — PSRAM-aware memory pool manager.
//
// Maintains named, fixed-size pools in PSRAM (OPI-SPIRAM) to prevent
// heap fragmentation and to protect the core WiFi/TLS stacks from
// exhaustion during heavy LLM reasoning tasks.
//
// Pool layout (configurable via PSRAM_POOL_SIZES):
//   LLM_CTX   — LLM system-prompt assembly buffer   (8 KB)
//   WORKSPACE — Agent workspace scratchpad           (16 KB)
//   SCRIPT    — Lua VM heap                          (32 KB)
//
// Design:
//   • Each pool is a single allocation; at most one caller holds it.
//   • alloc() returns the pool pointer; free() releases it.
//   • On native builds the pools fall back to regular malloc().
// ─────────────────────────────────────────────────────────────────

enum class PsramPool : uint8_t {
    LLM_CTX   = 0,   // LLM context / prompt assembly
    WORKSPACE = 1,   // Agent workspace scratchpad
    SCRIPT    = 2,   // Lua VM heap
    _COUNT    = 3,
};

static constexpr size_t PSRAM_POOL_SIZES[] = {
     8192,  // LLM_CTX    8 KB
    16384,  // WORKSPACE 16 KB
    32768,  // SCRIPT    32 KB
};

// ─────────────────────────────────────────────────────────────────

class PsramAllocator {
public:
    // Allocate a buffer from the specified pool.
    // Returns nullptr if the pool is already in use or OOM.
    static void* alloc(PsramPool pool) {
        uint8_t idx = static_cast<uint8_t>(pool);
        if (idx >= static_cast<uint8_t>(PsramPool::_COUNT)) return nullptr;
        if (_busy[idx]) return nullptr;  // pool already held by someone

        size_t sz  = PSRAM_POOL_SIZES[idx];
        void*  ptr = nullptr;

#ifndef NATIVE_BUILD
        ptr = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!ptr) ptr = malloc(sz);  // fall back to internal heap
#else
        ptr = malloc(sz);
#endif

        if (ptr) {
            _busy[idx]  = true;
            _ptrs[idx]  = ptr;
        }
        return ptr;
    }

    // Release a pool buffer previously obtained via alloc().
    static void free(PsramPool pool, void* ptr) {
        uint8_t idx = static_cast<uint8_t>(pool);
        if (idx >= static_cast<uint8_t>(PsramPool::_COUNT)) return;
        if (!_busy[idx] || _ptrs[idx] != ptr) return;
        ::free(ptr);
        _busy[idx] = false;
        _ptrs[idx] = nullptr;
    }

    // Returns true if the named pool is currently allocated.
    static bool isBusy(PsramPool pool) {
        uint8_t idx = static_cast<uint8_t>(pool);
        return (idx < static_cast<uint8_t>(PsramPool::_COUNT)) && _busy[idx];
    }

    // Returns the fixed size (in bytes) of the given pool.
    static size_t poolSize(PsramPool pool) {
        uint8_t idx = static_cast<uint8_t>(pool);
        if (idx >= static_cast<uint8_t>(PsramPool::_COUNT)) return 0;
        return PSRAM_POOL_SIZES[idx];
    }

    // Report free PSRAM bytes (hardware query on ESP32, 0 on native).
    static uint32_t freePsramBytes() {
#ifndef NATIVE_BUILD
        return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#else
        return 0;
#endif
    }

    // Fill buf with a JSON summary of pool utilisation.
    static void statsJson(char* buf, size_t len) {
        static const char* const kNames[] = { "llm_ctx", "workspace", "script" };
        size_t pos = 0;
        pos += (size_t)snprintf(buf + pos, len - pos, "{");
        for (uint8_t i = 0; i < static_cast<uint8_t>(PsramPool::_COUNT); ++i) {
            if (pos >= len - 1) break;
            pos += (size_t)snprintf(buf + pos, len - pos,
                                    "%s\"%s\":{\"busy\":%s,\"size\":%zu}",
                                    i == 0 ? "" : ",",
                                    kNames[i],
                                    _busy[i] ? "true" : "false",
                                    PSRAM_POOL_SIZES[i]);
        }
        if (pos < len - 1) buf[pos++] = '}';
        buf[pos < len ? pos : len - 1] = '\0';
    }

private:
    static bool  _busy[static_cast<uint8_t>(PsramPool::_COUNT)];
    static void* _ptrs[static_cast<uint8_t>(PsramPool::_COUNT)];
};

// ── Static member definitions ─────────────────────────────────────
inline bool  PsramAllocator::_busy[static_cast<uint8_t>(PsramPool::_COUNT)] = {};
inline void* PsramAllocator::_ptrs[static_cast<uint8_t>(PsramPool::_COUNT)] = {};
