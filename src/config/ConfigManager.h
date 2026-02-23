#pragma once

#include <string.h>
#include <stdio.h>

#ifndef NATIVE_BUILD
#  include <LittleFS.h>
#  include <Arduino.h>
#endif

// ─────────────────────────────────────────────────────────────────
// ConfigManager — reads agent configuration from .md files stored
// in LittleFS.
//
// File format (simple key: value, lines starting with '#' ignored):
//
//   # Agent configuration
//   llm_provider: openai
//   llm_model: gpt-4o-mini
//   llm_api_key: sk-...
//   max_tokens: 512
//   temperature: 0.3
//   wifi_ssid: MyNetwork
//   wifi_password: secret
//
// Values are stored in an in-memory table and accessed by key.
// The config is reloaded by calling load() again.
// ─────────────────────────────────────────────────────────────────

static constexpr uint8_t  CONFIG_MAX_ENTRIES = 64;
static constexpr size_t   CONFIG_KEY_LEN     = 32;
static constexpr size_t   CONFIG_VAL_LEN     = 128;

struct ConfigEntry {
    char key[CONFIG_KEY_LEN] = {};
    char val[CONFIG_VAL_LEN] = {};
};

class ConfigManager {
public:
    ConfigManager() = default;

    // Load (or reload) config from all .md files in the given path.
    // Call begin() first to mount LittleFS.
    bool begin();
    bool load(const char* path = "/config/agent.md");

    // Get a string value; returns def if key not found
    const char* get(const char* key, const char* def = "") const;

    // Get an integer value
    int getInt(const char* key, int def = 0) const;

    // Get a float value
    float getFloat(const char* key, float def = 0.0f) const;

    // Set a value at runtime (not persisted unless save() is called)
    bool set(const char* key, const char* value);

    // Persist current config back to the file
    bool save(const char* path = "/config/agent.md") const;

    // Number of loaded entries
    uint8_t count() const { return _count; }

    // Fill buf with a summary of all key=value pairs
    void dump(char* buf, size_t len) const;

private:
    ConfigEntry _entries[CONFIG_MAX_ENTRIES] = {};
    uint8_t     _count = 0;

    void _parseLine(const char* line);
};

// ── Inline implementations ────────────────────────────────────────

inline bool ConfigManager::begin() {
#ifndef NATIVE_BUILD
    if (!LittleFS.begin(true)) {
        Serial.println("[Config] LittleFS mount failed — formatting...");
        return false;
    }
    Serial.println("[Config] LittleFS mounted");
#endif
    return true;
}

inline bool ConfigManager::load(const char* path) {
    _count = 0;  // reset

#ifndef NATIVE_BUILD
    if (!LittleFS.exists(path)) {
        Serial.printf("[Config] File not found: %s\n", path);
        return false;
    }
    File f = LittleFS.open(path, "r");
    if (!f) return false;

    char line[CONFIG_KEY_LEN + CONFIG_VAL_LEN + 4];
    while (f.available()) {
        size_t n = 0;
        while (f.available() && n < sizeof(line) - 1) {
            char c = (char)f.read();
            if (c == '\n') break;
            if (c != '\r') line[n++] = c;
        }
        line[n] = '\0';
        _parseLine(line);
    }
    f.close();
    Serial.printf("[Config] Loaded %d entries from %s\n", _count, path);
    return true;
#else
    (void)path;
    return true;
#endif
}

inline void ConfigManager::_parseLine(const char* line) {
    if (!line || line[0] == '#' || line[0] == '\0') return;
    if (line[0] == '-' || line[0] == ' ') return;  // skip MD bullets/headings

    const char* colon = strchr(line, ':');
    if (!colon) return;

    if (_count >= CONFIG_MAX_ENTRIES) return;
    ConfigEntry& e = _entries[_count];

    // Key: trim trailing spaces
    size_t kLen = (size_t)(colon - line);
    if (kLen == 0 || kLen >= CONFIG_KEY_LEN) return;
    memcpy(e.key, line, kLen);
    e.key[kLen] = '\0';
    // Trim trailing spaces from key
    while (kLen > 0 && e.key[kLen-1] == ' ') e.key[--kLen] = '\0';

    // Value: skip leading spaces after ':'
    const char* val = colon + 1;
    while (*val == ' ') ++val;
    size_t vLen = strlen(val);
    // Trim trailing spaces
    while (vLen > 0 && (val[vLen-1] == ' ' || val[vLen-1] == '\r')) --vLen;
    if (vLen >= CONFIG_VAL_LEN) vLen = CONFIG_VAL_LEN - 1;
    memcpy(e.val, val, vLen);
    e.val[vLen] = '\0';

    // Skip empty values
    if (e.val[0] == '\0') return;

    ++_count;
}

inline const char* ConfigManager::get(const char* key, const char* def) const {
    if (!key) return def;
    for (uint8_t i = 0; i < _count; ++i) {
        if (strcmp(_entries[i].key, key) == 0) return _entries[i].val;
    }
    return def;
}

inline int ConfigManager::getInt(const char* key, int def) const {
    const char* v = get(key, nullptr);
    return v ? atoi(v) : def;
}

inline float ConfigManager::getFloat(const char* key, float def) const {
    const char* v = get(key, nullptr);
    return v ? (float)atof(v) : def;
}

inline bool ConfigManager::set(const char* key, const char* value) {
    if (!key || !value) return false;
    // Update existing
    for (uint8_t i = 0; i < _count; ++i) {
        if (strcmp(_entries[i].key, key) == 0) {
            strncpy(_entries[i].val, value, CONFIG_VAL_LEN - 1);
            return true;
        }
    }
    // Add new
    if (_count >= CONFIG_MAX_ENTRIES) return false;
    strncpy(_entries[_count].key, key,   CONFIG_KEY_LEN - 1);
    strncpy(_entries[_count].val, value, CONFIG_VAL_LEN - 1);
    ++_count;
    return true;
}

inline bool ConfigManager::save(const char* path) const {
#ifndef NATIVE_BUILD
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.println("# PCB-Claw auto-saved configuration");
    for (uint8_t i = 0; i < _count; ++i) {
        f.printf("%s: %s\n", _entries[i].key, _entries[i].val);
    }
    f.close();
    return true;
#else
    (void)path;
    return false;
#endif
}

inline void ConfigManager::dump(char* buf, size_t len) const {
    size_t pos = 0;
    for (uint8_t i = 0; i < _count && pos < len - 1; ++i) {
        int w = snprintf(buf + pos, len - pos,
                         "%s=%s\n", _entries[i].key, _entries[i].val);
        if (w > 0) pos += (size_t)w;
    }
    if (pos < len) buf[pos] = '\0';
}
