#pragma once

#include "Message.h"

// ─────────────────────────────────────────────────────────────────
// IChannel — abstract interface that every communication channel
// must implement (Serial, Telegram, Slack, Mail, WhatsApp, …).
//
// The Gateway uses this interface to send outgoing messages back
// through the originating channel without knowing its type.
// ─────────────────────────────────────────────────────────────────

class IChannel {
public:
    virtual ~IChannel() = default;

    // Unique identifier used in Message::channel_id
    virtual const char* id() const = 0;

    // Human-readable name for logging
    virtual const char* name() const = 0;

    // Called once during system startup
    virtual bool begin() = 0;

    // Called every loop iteration — poll for new messages.
    // Implementations PUSH received messages into the provided
    // FreeRTOS queue handle (or equivalent on native builds).
    // Returns true if at least one message was received.
    virtual bool poll() = 0;

    // Send a message out through this channel.
    // Called from the agent / gateway to deliver responses.
    virtual bool send(const Message& msg) = 0;

    // Whether the channel is currently connected / available
    virtual bool isConnected() const = 0;

    // Optional: called periodically to maintain the connection
    virtual void maintain() {}
};
