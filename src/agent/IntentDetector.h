#pragma once

#include "../gateway/Message.h"
#include <string.h>
#include <stdio.h>

// ─────────────────────────────────────────────────────────────────
// IntentDetector — lightweight intent classifier.
//
// Classifies an incoming message into one of several intent
// categories without needing an LLM call, saving tokens.
//
// Order of evaluation:
//   1. Explicit command prefix '/'
//   2. Keyword matching against registered intents
//   3. Fallback: CHAT intent (send to LLM reasoning loop)
// ─────────────────────────────────────────────────────────────────

enum class Intent : uint8_t {
    CHAT         = 0,  // General conversation → route to LLM
    COMMAND      = 1,  // Explicit /command
    GPIO_CONTROL = 2,  // Control hardware pins
    I2C_OP       = 3,  // I2C bus operation
    SPI_OP       = 4,  // SPI bus operation
    HTTP_REQUEST = 5,  // External HTTP call
    WORKFLOW_RUN = 6,  // Run a named workflow
    STATUS_QUERY = 7,  // Query system/sensor status
    CONFIG_CHANGE= 8,  // Modify agent configuration
    UNKNOWN      = 0xFF,
};

inline const char* intentName(Intent i) {
    switch (i) {
        case Intent::CHAT:          return "chat";
        case Intent::COMMAND:       return "command";
        case Intent::GPIO_CONTROL:  return "gpio_control";
        case Intent::I2C_OP:        return "i2c_op";
        case Intent::SPI_OP:        return "spi_op";
        case Intent::HTTP_REQUEST:  return "http_request";
        case Intent::WORKFLOW_RUN:  return "workflow_run";
        case Intent::STATUS_QUERY:  return "status_query";
        case Intent::CONFIG_CHANGE: return "config_change";
        default:                    return "unknown";
    }
}

// ── Keyword table entry ───────────────────────────────────────────
struct IntentKeyword {
    const char* keyword;
    Intent      intent;
};

// Default keyword table (case-insensitive prefix/substring match)
static constexpr IntentKeyword DEFAULT_INTENT_KEYWORDS[] = {
    // GPIO
    { "gpio",      Intent::GPIO_CONTROL },
    { "pin",       Intent::GPIO_CONTROL },
    { "led",       Intent::GPIO_CONTROL },
    { "relay",     Intent::GPIO_CONTROL },
    { "pwm",       Intent::GPIO_CONTROL },
    { "digital",   Intent::GPIO_CONTROL },
    { "analog",    Intent::GPIO_CONTROL },
    // I2C
    { "i2c",       Intent::I2C_OP },
    { "sensor",    Intent::I2C_OP },
    { "bme",       Intent::I2C_OP },
    { "bmp",       Intent::I2C_OP },
    { "mpu",       Intent::I2C_OP },
    { "ina",       Intent::I2C_OP },
    // SPI
    { "spi",       Intent::SPI_OP },
    { "flash",     Intent::SPI_OP },
    { "display",   Intent::SPI_OP },
    { "tft",       Intent::SPI_OP },
    // HTTP
    { "http",      Intent::HTTP_REQUEST },
    { "fetch",     Intent::HTTP_REQUEST },
    { "request",   Intent::HTTP_REQUEST },
    { "api",       Intent::HTTP_REQUEST },
    { "url",       Intent::HTTP_REQUEST },
    // Workflow
    { "workflow",  Intent::WORKFLOW_RUN },
    { "run",       Intent::WORKFLOW_RUN },
    { "execute",   Intent::WORKFLOW_RUN },
    { "start",     Intent::WORKFLOW_RUN },
    // Status
    { "status",    Intent::STATUS_QUERY },
    { "health",    Intent::STATUS_QUERY },
    { "info",      Intent::STATUS_QUERY },
    { "memory",    Intent::STATUS_QUERY },
    { "uptime",    Intent::STATUS_QUERY },
    { "temperature",Intent::STATUS_QUERY },
    // Config
    { "config",    Intent::CONFIG_CHANGE },
    { "set ",      Intent::CONFIG_CHANGE },
    { "configure", Intent::CONFIG_CHANGE },
};

// ─────────────────────────────────────────────────────────────────

class IntentDetector {
public:
    // Detect intent from a message.
    // Fast path: no LLM call, purely local.
    static Intent detect(const Message& msg) {
        // Explicit command
        if (msg.type == MessageType::COMMAND || msg.content[0] == '/') {
            return Intent::COMMAND;
        }

        // Keyword scan (case-insensitive)
        const char* text = msg.content;
        for (const auto& kw : DEFAULT_INTENT_KEYWORDS) {
            if (_containsCI(text, kw.keyword)) return kw.intent;
        }

        return Intent::CHAT;
    }

    // Detect intent from a raw string
    static Intent detect(const char* text) {
        if (!text) return Intent::UNKNOWN;
        if (text[0] == '/') return Intent::COMMAND;
        for (const auto& kw : DEFAULT_INTENT_KEYWORDS) {
            if (_containsCI(text, kw.keyword)) return kw.intent;
        }
        return Intent::CHAT;
    }

private:
    // Case-insensitive substring search with word-boundary check.
    // Treats letters, digits, and '_' as word characters — so "led"
    // does NOT match inside "blink_led".
    static bool _containsCI(const char* haystack, const char* needle) {
        if (!haystack || !needle) return false;
        size_t hl = strlen(haystack), nl = strlen(needle);
        if (nl > hl) return false;
        for (size_t i = 0; i <= hl - nl; ++i) {
            bool match = true;
            for (size_t j = 0; j < nl; ++j) {
                char a = haystack[i + j], b = needle[j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = false; break; }
            }
            if (match) {
                // Word-boundary check: character before and after
                // must NOT be a word character (letter, digit, '_').
                bool leftOk  = (i == 0 || !_isWordChar(haystack[i - 1]));
                bool rightOk = ((i + nl) >= hl ||
                                !_isWordChar(haystack[i + nl]));
                if (leftOk && rightOk) return true;
            }
        }
        return false;
    }

    static bool _isWordChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    }
};
