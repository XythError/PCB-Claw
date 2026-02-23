#pragma once

#include "Tool.h"
#include <string.h>
#include <stdio.h>

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#  include <SPI.h>
#endif

// ─────────────────────────────────────────────────────────────────
// SpiTool — interact with SPI devices on the ESP32.
//
// Supported operations (via "op" field in argsJson):
//   transfer — full-duplex transfer:
//              { "cs": 5, "data": [0x01, 0x80, 0x00],
//                "speed": 1000000, "mode": 0 }
//   write    — write-only (discard MISO):
//              { "cs": 5, "data": [0x02, 0x10] }
//   read     — CS + read N bytes (writes 0x00):
//              { "cs": 5, "len": 3 }
//
// The ESP32-S3 supports VSPI (SCK=36, MISO=37, MOSI=35) and
// HSPI (SCK=14, MISO=12, MOSI=13) buses.
// Pin numbers are configurable through begin(mosi, miso, sck).
// ─────────────────────────────────────────────────────────────────

class SpiTool : public ITool {
public:
    SpiTool(int mosi = 35, int miso = 37, int sck = 36)
        : _mosi(mosi), _miso(miso), _sck(sck) {}

    const char* name()        const override { return "spi"; }
    const char* description() const override {
        return "Read/write SPI devices (full-duplex transfer, write, read)";
    }
    const char* argSchema()   const override {
        return "{"
            "\"type\":\"object\","
            "\"properties\":{"
                "\"op\":{\"type\":\"string\","
                    "\"enum\":[\"transfer\",\"write\",\"read\"]},"
                "\"cs\":{\"type\":\"integer\"},"
                "\"data\":{\"type\":\"array\","
                           "\"items\":{\"type\":\"integer\"}},"
                "\"len\":{\"type\":\"integer\"},"
                "\"speed\":{\"type\":\"integer\"},"
                "\"mode\":{\"type\":\"integer\","
                           "\"minimum\":0,\"maximum\":3}"
            "},"
            "\"required\":[\"op\",\"cs\"]"
        "}";
    }

    bool begin() override {
#ifndef NATIVE_BUILD
        SPI.begin(_sck, _miso, _mosi);
#endif
        return true;
    }

    bool execute(const char* argsJson,
                 char*       resultBuf,
                 size_t      resultLen) override
    {
        char op[16] = {};
        _extractStr(argsJson, "op", op, sizeof(op));
        int cs    = _extractInt(argsJson, "cs",    -1);
        int speed = _extractInt(argsJson, "speed", 1000000);
        int mode  = _extractInt(argsJson, "mode",  0);
        int len   = _extractInt(argsJson, "len",   0);

        if (cs < 0) {
            snprintf(resultBuf, resultLen, "{\"error\":\"cs required\"}");
            return false;
        }

#ifndef NATIVE_BUILD
        uint8_t txBuf[64] = {}; uint8_t txLen = 0;
        uint8_t rxBuf[64] = {};

        if (strcmp(op, "write") == 0 || strcmp(op, "transfer") == 0) {
            _extractByteArray(argsJson, txBuf, &txLen, sizeof(txBuf));
        } else if (strcmp(op, "read") == 0) {
            txLen = (len > 0 && len <= 64) ? len : 1;
        }

        SPISettings settings(speed,
                             MSBFIRST,
                             (mode == 0) ? SPI_MODE0 :
                             (mode == 1) ? SPI_MODE1 :
                             (mode == 2) ? SPI_MODE2 : SPI_MODE3);
        pinMode(cs, OUTPUT);
        digitalWrite(cs, LOW);
        SPI.beginTransaction(settings);

        for (uint8_t i = 0; i < txLen; ++i) {
            rxBuf[i] = SPI.transfer(txBuf[i]);
        }

        SPI.endTransaction();
        digitalWrite(cs, HIGH);

        size_t pos = 0;
        pos += snprintf(resultBuf + pos, resultLen - pos,
                        "{\"ok\":true,\"rx\":[");
        for (uint8_t i = 0; i < txLen; ++i) {
            pos += snprintf(resultBuf + pos, resultLen - pos,
                            "%s%d", i > 0 ? "," : "", rxBuf[i]);
        }
        snprintf(resultBuf + pos, resultLen - pos, "]}");
#else
        snprintf(resultBuf, resultLen,
                 "{\"ok\":true,\"op\":\"%s\",\"simulated\":true}", op);
#endif
        return true;
    }

private:
    int _mosi, _miso, _sck;

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
        while (*p && *p != '"' && i < outLen - 1) out[i++] = *p++;
        out[i] = '\0';
    }

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
