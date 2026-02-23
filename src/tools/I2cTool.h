#pragma once

#include "Tool.h"
#include <string.h>
#include <stdio.h>

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#  include <Wire.h>
#endif

// ─────────────────────────────────────────────────────────────────
// I2cTool — interact with I2C devices on the ESP32.
//
// Supported operations (via "op" field in argsJson):
//   scan       — scan bus and return found addresses: {}
//   write_reg  — write to a register:
//                { "addr": 0x48, "reg": 0x01, "data": [0xFF, 0x80] }
//   read_reg   — read from a register:
//                { "addr": 0x48, "reg": 0x00, "len": 2 }
//   write_raw  — send raw bytes: { "addr": 0x48, "data": [0x01] }
//   read_raw   — receive raw bytes: { "addr": 0x48, "len": 4 }
//
// SDA/SCL pins are configured via begin(sda, scl) or in agent.md.
// ─────────────────────────────────────────────────────────────────

class I2cTool : public ITool {
public:
    I2cTool(int sda = 21, int scl = 22, uint32_t freq = 400000)
        : _sda(sda), _scl(scl), _freq(freq) {}

    const char* name()        const override { return "i2c"; }
    const char* description() const override {
        return "Read/write I2C devices (scan, register access, raw bytes)";
    }
    const char* argSchema()   const override {
        return "{"
            "\"type\":\"object\","
            "\"properties\":{"
                "\"op\":{\"type\":\"string\","
                    "\"enum\":[\"scan\",\"write_reg\",\"read_reg\","
                               "\"write_raw\",\"read_raw\"]},"
                "\"addr\":{\"type\":\"integer\"},"
                "\"reg\":{\"type\":\"integer\"},"
                "\"len\":{\"type\":\"integer\"},"
                "\"data\":{\"type\":\"array\","
                           "\"items\":{\"type\":\"integer\"}}"
            "},"
            "\"required\":[\"op\"]"
        "}";
    }

    bool begin() override {
#ifndef NATIVE_BUILD
        Wire.begin(_sda, _scl, _freq);
#endif
        return true;
    }

    bool execute(const char* argsJson,
                 char*       resultBuf,
                 size_t      resultLen) override
    {
        char op[16] = {};
        _extractStr(argsJson, "op", op, sizeof(op));
        int addr = _extractInt(argsJson, "addr", -1);
        int reg  = _extractInt(argsJson, "reg",  -1);
        int len  = _extractInt(argsJson, "len",   1);

#ifndef NATIVE_BUILD
        if (strcmp(op, "scan") == 0) {
            size_t pos = 0;
            pos += snprintf(resultBuf + pos, resultLen - pos,
                            "{\"ok\":true,\"devices\":[");
            bool first = true;
            for (uint8_t a = 1; a < 127; ++a) {
                Wire.beginTransmission(a);
                if (Wire.endTransmission() == 0) {
                    pos += snprintf(resultBuf + pos, resultLen - pos,
                                    "%s%d", first ? "" : ",", a);
                    first = false;
                }
            }
            snprintf(resultBuf + pos, resultLen - pos, "]}");

        } else if (strcmp(op, "write_reg") == 0) {
            if (addr < 0 || reg < 0) {
                snprintf(resultBuf, resultLen,
                         "{\"error\":\"addr and reg required\"}");
                return false;
            }
            // Extract data array
            uint8_t data[32]; uint8_t dlen = 0;
            _extractByteArray(argsJson, data, &dlen, sizeof(data));
            Wire.beginTransmission(addr);
            Wire.write((uint8_t)reg);
            Wire.write(data, dlen);
            uint8_t err = Wire.endTransmission();
            snprintf(resultBuf, resultLen,
                     "{\"ok\":%s,\"err\":%d}", err == 0 ? "true" : "false", err);

        } else if (strcmp(op, "read_reg") == 0) {
            if (addr < 0 || reg < 0) {
                snprintf(resultBuf, resultLen,
                         "{\"error\":\"addr and reg required\"}");
                return false;
            }
            Wire.beginTransmission(addr);
            Wire.write((uint8_t)reg);
            Wire.endTransmission(false);
            Wire.requestFrom(addr, len);
            size_t pos = 0;
            pos += snprintf(resultBuf + pos, resultLen - pos,
                            "{\"ok\":true,\"data\":[");
            for (int i = 0; Wire.available() && i < len; ++i) {
                pos += snprintf(resultBuf + pos, resultLen - pos,
                                "%s%d", i > 0 ? "," : "", Wire.read());
            }
            snprintf(resultBuf + pos, resultLen - pos, "]}");

        } else if (strcmp(op, "write_raw") == 0) {
            if (addr < 0) {
                snprintf(resultBuf, resultLen, "{\"error\":\"addr required\"}");
                return false;
            }
            uint8_t data[32]; uint8_t dlen = 0;
            _extractByteArray(argsJson, data, &dlen, sizeof(data));
            Wire.beginTransmission(addr);
            Wire.write(data, dlen);
            uint8_t err = Wire.endTransmission();
            snprintf(resultBuf, resultLen,
                     "{\"ok\":%s,\"err\":%d}", err == 0 ? "true" : "false", err);

        } else if (strcmp(op, "read_raw") == 0) {
            if (addr < 0) {
                snprintf(resultBuf, resultLen, "{\"error\":\"addr required\"}");
                return false;
            }
            Wire.requestFrom(addr, len);
            size_t pos = 0;
            pos += snprintf(resultBuf + pos, resultLen - pos,
                            "{\"ok\":true,\"data\":[");
            for (int i = 0; Wire.available() && i < len; ++i) {
                pos += snprintf(resultBuf + pos, resultLen - pos,
                                "%s%d", i > 0 ? "," : "", Wire.read());
            }
            snprintf(resultBuf + pos, resultLen - pos, "]}");
        } else {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"unknown op '%s'\"}", op);
            return false;
        }
#else
        snprintf(resultBuf, resultLen,
                 "{\"ok\":true,\"op\":\"%s\",\"simulated\":true}", op);
#endif
        return true;
    }

private:
    int      _sda, _scl;
    uint32_t _freq;

    static int _extractInt(const char* json, const char* key, int def) {
        if (!json || !key) return def;
        char needle[TOOL_NAME_LEN + 5];
        snprintf(needle, sizeof(needle), "\"%s\":", key);
        const char* p = strstr(json, needle);
        if (!p) return def;
        p += strlen(needle);
        while (*p == ' ') ++p;
        if (*p == '-' || (*p >= '0' && *p <= '9')) return atoi(p);
        // Hex value
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            return (int)strtol(p, nullptr, 16);
        }
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
        while (*p && *p != '"' && i < outLen - 1) out[i++] = *p++;
        out[i] = '\0';
    }

    // Extract first JSON integer array into out[], max maxLen bytes
    static void _extractByteArray(const char* json,
                                   uint8_t* out, uint8_t* outLen,
                                   size_t maxLen) {
        *outLen = 0;
        const char* p = strchr(json, '[');
        if (!p) return;
        ++p;
        while (*p && *p != ']' && *outLen < maxLen) {
            while (*p == ' ' || *p == ',') ++p;
            if (*p >= '0' && *p <= '9') {
                out[(*outLen)++] = (uint8_t)atoi(p);
                while (*p >= '0' && *p <= '9') ++p;
            } else ++p;
        }
    }
};
