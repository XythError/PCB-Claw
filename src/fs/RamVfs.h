#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#  include <LittleFS.h>
#  include <esp_heap_caps.h>
#endif

// ─────────────────────────────────────────────────────────────────
// RamVfs — PSRAM-backed virtual filesystem for transient agent data.
//
// Motivation: frequent LittleFS writes during the agent "thought
// process" risk shortening flash lifetime (typical NAND endurance:
// ~10k erase cycles per block).  RamVfs keeps all intermediate
// writes in PSRAM and only flushes finalised data to LittleFS when
// explicitly requested via commit() / commitAll().
//
// API summary:
//   write(path, data)    — create or overwrite in RAM (no flash wear)
//   append(path, data)   — append to existing RAM file (or create)
//   read(path, buf, len) — read from RAM; fall back to LittleFS
//   commit(path)         — flush one file to LittleFS, clear dirty flag
//   commitAll()          — flush all dirty files, returns count written
//   exists(path)         — true if the path is in the RAM cache
//   discard(path)        — free RAM slot without writing to flash
//   fileCount()          — number of active RAM-cached files
//
// Constraints:
//   • RAM_VFS_MAX_FILES  — maximum number of cached file slots (16)
//   • RAM_VFS_MAX_FILE_SIZE — maximum bytes per file (4 KB)
//
// Each data buffer is allocated from PSRAM (with fallback to heap).
// Volatile: always call commitAll() before deep-sleep or reboot.
// ─────────────────────────────────────────────────────────────────

static constexpr uint8_t  RAM_VFS_MAX_FILES     = 16;
static constexpr size_t   RAM_VFS_MAX_FILE_SIZE = 4096;   // 4 KB per slot
static constexpr size_t   RAM_VFS_PATH_LEN      = 64;

struct RamFile {
    char   path[RAM_VFS_PATH_LEN] = {};
    char*  data                   = nullptr;  // PSRAM-allocated buffer
    size_t size                   = 0;
    bool   dirty                  = false;    // true = not yet in flash
    bool   active                 = false;
};

// ─────────────────────────────────────────────────────────────────

class RamVfs {
public:
    RamVfs()  = default;
    ~RamVfs() { _freeAll(); }

    // Create or overwrite a file in RAM.  data must be null-terminated.
    bool write(const char* path, const char* data) {
        if (!path || !data) return false;
        RamFile* f = _find(path);
        if (!f) f = _allocSlot(path);
        if (!f) return false;

        size_t len = strlen(data);
        if (len >= RAM_VFS_MAX_FILE_SIZE) len = RAM_VFS_MAX_FILE_SIZE - 1;
        if (!f->data) {
            f->data = _allocBuf();
            if (!f->data) return false;
        }
        memcpy(f->data, data, len);
        f->data[len] = '\0';
        f->size  = len;
        f->dirty = true;
        return true;
    }

    // Append text to an existing RAM file, or create a new one.
    bool append(const char* path, const char* data) {
        if (!path || !data) return false;
        RamFile* f = _find(path);
        if (!f) return write(path, data);

        size_t addLen = strlen(data);
        size_t avail  = RAM_VFS_MAX_FILE_SIZE - 1 - f->size;
        if (avail == 0) return false;
        if (addLen > avail) addLen = avail;
        if (!f->data) {
            f->data = _allocBuf();
            if (!f->data) return false;
            f->size = 0;
        }
        memcpy(f->data + f->size, data, addLen);
        f->size        += addLen;
        f->data[f->size] = '\0';
        f->dirty = true;
        return true;
    }

    // Read a file.  Returns bytes read (0 on not-found).
    // Prefers the RAM copy; falls back to LittleFS when absent.
    size_t read(const char* path, char* buf, size_t bufLen) {
        if (!path || !buf || bufLen == 0) return 0;
        const RamFile* f = _find(path);
        if (f && f->data) {
            size_t n = f->size < bufLen - 1 ? f->size : bufLen - 1;
            memcpy(buf, f->data, n);
            buf[n] = '\0';
            return n;
        }
#ifndef NATIVE_BUILD
        return _readLittleFS(path, buf, bufLen);
#else
        buf[0] = '\0';
        return 0;
#endif
    }

    // Flush a single dirty file to LittleFS.  Returns false if not
    // found, not dirty, or the LittleFS write fails.
    bool commit(const char* path) {
        RamFile* f = _find(path);
        if (!f || !f->dirty || !f->data) return false;
#ifndef NATIVE_BUILD
        File lf = LittleFS.open(path, "w");
        if (!lf) return false;
        lf.write(reinterpret_cast<const uint8_t*>(f->data), f->size);
        lf.close();
#endif
        f->dirty = false;
        return true;
    }

    // Flush all dirty files to LittleFS.
    // Returns the number of files successfully written.
    uint8_t commitAll() {
        uint8_t count = 0;
        for (uint8_t i = 0; i < RAM_VFS_MAX_FILES; ++i) {
            if (_files[i].active && _files[i].dirty) {
                if (commit(_files[i].path)) ++count;
            }
        }
        return count;
    }

    // Returns true if the path is cached in RAM.
    bool exists(const char* path) const {
        return _find(path) != nullptr;
    }

    // Discard a RAM slot without writing to flash.
    void discard(const char* path) {
        RamFile* f = _find(path);
        if (!f) return;
        if (f->data) { ::free(f->data); f->data = nullptr; }
        f->size    = 0;
        f->dirty   = false;
        f->active  = false;
        f->path[0] = '\0';
    }

    // Number of active (in-RAM) file slots currently used.
    uint8_t fileCount() const {
        uint8_t n = 0;
        for (uint8_t i = 0; i < RAM_VFS_MAX_FILES; ++i) {
            if (_files[i].active) ++n;
        }
        return n;
    }

private:
    RamFile _files[RAM_VFS_MAX_FILES] = {};

    RamFile* _find(const char* path) {
        for (uint8_t i = 0; i < RAM_VFS_MAX_FILES; ++i) {
            if (_files[i].active &&
                strncmp(_files[i].path, path, RAM_VFS_PATH_LEN - 1) == 0) {
                return &_files[i];
            }
        }
        return nullptr;
    }

    const RamFile* _find(const char* path) const {
        for (uint8_t i = 0; i < RAM_VFS_MAX_FILES; ++i) {
            if (_files[i].active &&
                strncmp(_files[i].path, path, RAM_VFS_PATH_LEN - 1) == 0) {
                return &_files[i];
            }
        }
        return nullptr;
    }

    RamFile* _allocSlot(const char* path) {
        for (uint8_t i = 0; i < RAM_VFS_MAX_FILES; ++i) {
            if (!_files[i].active) {
                strncpy(_files[i].path, path, RAM_VFS_PATH_LEN - 1);
                _files[i].path[RAM_VFS_PATH_LEN - 1] = '\0';
                _files[i].active = true;
                return &_files[i];
            }
        }
        return nullptr;
    }

    // Allocate a data buffer from PSRAM (fallback to heap).
    static char* _allocBuf() {
#ifndef NATIVE_BUILD
        void* p = heap_caps_malloc(RAM_VFS_MAX_FILE_SIZE,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) p = malloc(RAM_VFS_MAX_FILE_SIZE);
        return static_cast<char*>(p);
#else
        return static_cast<char*>(malloc(RAM_VFS_MAX_FILE_SIZE));
#endif
    }

    void _freeAll() {
        for (uint8_t i = 0; i < RAM_VFS_MAX_FILES; ++i) {
            if (_files[i].data) {
                ::free(_files[i].data);
                _files[i].data   = nullptr;
            }
            _files[i].active = false;
        }
    }

#ifndef NATIVE_BUILD
    static size_t _readLittleFS(const char* path, char* buf, size_t bufLen) {
        if (!LittleFS.exists(path)) { buf[0] = '\0'; return 0; }
        File f = LittleFS.open(path, "r");
        if (!f) { buf[0] = '\0'; return 0; }
        size_t n = f.readBytes(buf, bufLen - 1);
        f.close();
        buf[n] = '\0';
        return n;
    }
#endif
};
