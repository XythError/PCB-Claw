#pragma once

#include "Tool.h"
#include <string.h>
#include <stdio.h>

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#endif

// ─────────────────────────────────────────────────────────────────
// GpioTool — read and write ESP32 GPIO pins.
//
// Supported operations (via "op" field in argsJson):
//   pin_mode   — configure a pin direction: { "pin": 2, "mode": "OUTPUT" }
//   write      — set a digital output:      { "pin": 2, "value": 1 }
//   read       — read a digital input:      { "pin": 4 }
//   analog_read — read ADC value:           { "pin": 34 }
//   pwm_write  — set PWM duty cycle:        { "pin": 5, "duty": 128,
//                                             "freq": 5000, "channel": 0 }
//
// All operations are JSON-in / JSON-out for uniform agent interface.
// ─────────────────────────────────────────────────────────────────

class GpioTool : public ITool {
public:
    const char* name()        const override { return "gpio"; }
    const char* description() const override {
        return "Read/write ESP32 GPIO pins (digital, analog, PWM)";
    }
    const char* argSchema()   const override {
        return "{"
            "\"type\":\"object\","
            "\"properties\":{"
                "\"op\":{"
                    "\"type\":\"string\","
                    "\"enum\":[\"pin_mode\",\"write\",\"read\","
                               "\"analog_read\",\"pwm_write\"]"
                "},"
                "\"pin\":{\"type\":\"integer\"},"
                "\"mode\":{\"type\":\"string\",\"enum\":[\"INPUT\","
                           "\"OUTPUT\",\"INPUT_PULLUP\",\"INPUT_PULLDOWN\"]},"
                "\"value\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":1},"
                "\"duty\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
                "\"freq\":{\"type\":\"integer\"},"
                "\"channel\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":15}"
            "},"
            "\"required\":[\"op\",\"pin\"]"
        "}";
    }

    bool execute(const char* argsJson,
                 char*       resultBuf,
                 size_t      resultLen) override
    {
        // Minimal JSON parser — only needs int/string fields.
        // We avoid pulling in a full JSON lib at the tool level so the
        // tool remains self-contained and testable on native builds.
        int  pin   = _extractInt(argsJson, "pin",     -1);
        int  value = _extractInt(argsJson, "value",   -1);
        int  duty  = _extractInt(argsJson, "duty",    -1);
        int  freq  = _extractInt(argsJson, "freq",    5000);
        int  ch    = _extractInt(argsJson, "channel",  0);
        char op[16]   = {};
        char mode[16] = {};
        _extractStr(argsJson, "op",   op,   sizeof(op));
        _extractStr(argsJson, "mode", mode, sizeof(mode));

        if (pin < 0) {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"pin is required\"}");
            return false;
        }

#ifndef NATIVE_BUILD
        if (strcmp(op, "pin_mode") == 0) {
            uint8_t m = OUTPUT;
            if (strcmp(mode, "INPUT") == 0)          m = INPUT;
            else if (strcmp(mode, "INPUT_PULLUP") == 0)   m = INPUT_PULLUP;
            else if (strcmp(mode, "INPUT_PULLDOWN") == 0) m = INPUT_PULLDOWN;
            pinMode(pin, m);
            snprintf(resultBuf, resultLen,
                     "{\"ok\":true,\"pin\":%d,\"mode\":\"%s\"}", pin, mode);

        } else if (strcmp(op, "write") == 0) {
            if (value < 0) { snprintf(resultBuf, resultLen,
                                      "{\"error\":\"value required\"}");
                             return false; }
            digitalWrite(pin, value ? HIGH : LOW);
            snprintf(resultBuf, resultLen,
                     "{\"ok\":true,\"pin\":%d,\"value\":%d}", pin, value);

        } else if (strcmp(op, "read") == 0) {
            int v = digitalRead(pin);
            snprintf(resultBuf, resultLen,
                     "{\"ok\":true,\"pin\":%d,\"value\":%d}", pin, v);

        } else if (strcmp(op, "analog_read") == 0) {
            int v = analogRead(pin);
            snprintf(resultBuf, resultLen,
                     "{\"ok\":true,\"pin\":%d,\"raw\":%d,"
                     "\"voltage\":%.3f}",
                     pin, v, v * 3.3f / 4095.0f);

        } else if (strcmp(op, "pwm_write") == 0) {
            if (duty < 0) { snprintf(resultBuf, resultLen,
                                     "{\"error\":\"duty required\"}");
                            return false; }
            ledcSetup(ch, freq, 8);
            ledcAttachPin(pin, ch);
            ledcWrite(ch, duty);
            snprintf(resultBuf, resultLen,
                     "{\"ok\":true,\"pin\":%d,\"duty\":%d,"
                     "\"freq\":%d,\"channel\":%d}",
                     pin, duty, freq, ch);
        } else {
            snprintf(resultBuf, resultLen,
                     "{\"error\":\"unknown op '%s'\"}", op);
            return false;
        }
#else
        // Native build — simulate results for unit tests
        (void)value; (void)duty; (void)freq; (void)ch; (void)mode;
        snprintf(resultBuf, resultLen,
                 "{\"ok\":true,\"pin\":%d,\"op\":\"%s\",\"simulated\":true}",
                 pin, op);
#endif
        return true;
    }

private:
    // ── Minimal field extractors ─────────────────────────────────
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
        ++p;  // skip opening quote
        size_t i = 0;
        while (*p && *p != '"' && i < outLen - 1) out[i++] = *p++;
        out[i] = '\0';
    }
};
