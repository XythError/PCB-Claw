#pragma once

#include <stdint.h>
#include <string.h>

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#else
#  include <string>
#  include <cstdint>
   using String = std::string;
#endif

// ─────────────────────────────────────────────────────────────────
// Unified Message — abstraction layer for all incoming/outgoing
// messages regardless of their originating channel.
// ─────────────────────────────────────────────────────────────────

enum class MessageType : uint8_t {
    TEXT      = 0,  // Plain conversation text
    COMMAND   = 1,  // Explicit command (starts with '/')
    WORKFLOW  = 2,  // Workflow trigger
    SYSTEM    = 3,  // Internal system message
    RESPONSE  = 4,  // Outgoing response to a user
    EVENT     = 5,  // Sensor / GPIO event
};

enum class MessagePriority : uint8_t {
    LOW    = 0,
    NORMAL = 1,
    HIGH   = 2,
    URGENT = 3,
};

// Maximum sizes kept small for stack-safety on embedded targets.
static constexpr size_t MSG_ID_LEN      = 24;
static constexpr size_t MSG_CHANNEL_LEN = 32;
static constexpr size_t MSG_SENDER_LEN  = 64;
static constexpr size_t MSG_CONTENT_LEN = 2048;  // up to 2 KB per message

struct Message {
    char            id[MSG_ID_LEN]           = {};  // Unique message ID
    char            channel_id[MSG_CHANNEL_LEN] = {}; // Originating channel
    char            reply_channel[MSG_CHANNEL_LEN] = {}; // Where to send reply
    char            sender_id[MSG_SENDER_LEN] = {}; // User / bot ID
    MessageType     type      = MessageType::TEXT;
    MessagePriority priority  = MessagePriority::NORMAL;
    char            content[MSG_CONTENT_LEN] = {};  // Main payload
    uint32_t        timestamp = 0;                  // ms since boot
    uint32_t        seq       = 0;                  // monotonic sequence number
    bool            requires_reply = true;

    // ── Factory helpers ──────────────────────────────────────────

    static Message make(const char* channel,
                        const char* sender,
                        const char* text,
                        MessageType t = MessageType::TEXT,
                        MessagePriority p = MessagePriority::NORMAL);

    static Message makeResponse(const Message& request, const char* text);

    static Message makeSystem(const char* text);

    // True if content starts with '/'
    bool isCommand() const {
        return content[0] == '/';
    }

    // Extract command name (first token after '/') into buf[len]
    void commandName(char* buf, size_t len) const;
};

// ── Inline implementations ───────────────────────────────────────

inline uint32_t _msg_next_seq() {
    static uint32_t counter = 0;
    return ++counter;
}

inline Message Message::make(const char* channel,
                              const char* sender,
                              const char* text,
                              MessageType t,
                              MessagePriority p) {
    Message m;
    uint32_t seq = _msg_next_seq();
    // Build a simple ID from seq
    snprintf(m.id, MSG_ID_LEN, "msg-%08lx", (unsigned long)seq);
    strncpy(m.channel_id,    channel, MSG_CHANNEL_LEN - 1);
    strncpy(m.reply_channel, channel, MSG_CHANNEL_LEN - 1);
    strncpy(m.sender_id,     sender,  MSG_SENDER_LEN  - 1);
    strncpy(m.content,       text,    MSG_CONTENT_LEN - 1);
    m.type      = t;
    m.priority  = p;
    m.seq       = seq;
#ifndef NATIVE_BUILD
    m.timestamp = millis();
#endif
    return m;
}

inline Message Message::makeResponse(const Message& req, const char* text) {
    Message m = make(req.reply_channel, "agent", text,
                     MessageType::RESPONSE, req.priority);
    // Respond on the same channel the request came from
    strncpy(m.reply_channel, req.channel_id, MSG_CHANNEL_LEN - 1);
    m.requires_reply = false;
    return m;
}

inline Message Message::makeSystem(const char* text) {
    return make("system", "system", text, MessageType::SYSTEM,
                MessagePriority::HIGH);
}

inline void Message::commandName(char* buf, size_t len) const {
    if (!isCommand() || len == 0) { buf[0] = '\0'; return; }
    const char* start = content + 1; // skip '/'
    size_t i = 0;
    while (i < len - 1 && start[i] && start[i] != ' ') {
        buf[i] = start[i];
        ++i;
    }
    buf[i] = '\0';
}
