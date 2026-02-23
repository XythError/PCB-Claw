#pragma once

#include "Channel.h"
#include "Message.h"

#ifndef NATIVE_BUILD
#  include <freertos/FreeRTOS.h>
#  include <freertos/queue.h>
#endif

static constexpr uint8_t GATEWAY_MAX_CHANNELS = 8;
static constexpr uint8_t GATEWAY_QUEUE_DEPTH  = 16;

// ─────────────────────────────────────────────────────────────────
// Gateway — control center and abstraction layer.
//
// Responsibilities:
//  • Maintains the list of registered channels
//  • Owns the central inbound message queue (unified format)
//  • Routes outgoing messages to the correct channel
//  • Calls channel->poll() each tick to harvest new messages
// ─────────────────────────────────────────────────────────────────

class Gateway {
public:
    Gateway();
    ~Gateway();

    // Register a channel (must be called before begin())
    bool addChannel(IChannel* ch);

    // Initialise all registered channels
    bool begin();

    // Call every loop tick — polls all channels and fills inbound queue
    void tick();

    // Dequeue one message for the agent to process.
    // Returns true and fills *out on success, false if queue empty.
    bool receive(Message* out, uint32_t timeoutMs = 0);

    // Send a message through the matching channel (by reply_channel id)
    bool send(const Message& msg);

    // Broadcast to all channels
    void broadcast(const Message& msg);

    // Number of registered channels
    uint8_t channelCount() const { return _count; }

    // Access a channel by index
    IChannel* channelAt(uint8_t idx);

    // Find a channel by id string
    IChannel* findChannel(const char* id);

    // Called by channel implementations to push a received message
    // into the central inbound queue.
    void push(const Message& m) { _enqueue(m); }

private:
    IChannel*  _channels[GATEWAY_MAX_CHANNELS] = {};
    uint8_t    _count = 0;

#ifndef NATIVE_BUILD
    QueueHandle_t _inboundQueue = nullptr;
#else
    // Simple ring-buffer queue for native unit tests
    Message   _nativeQueue[GATEWAY_QUEUE_DEPTH] = {};
    uint8_t   _head = 0, _tail = 0, _fill = 0;
    bool      _nativeEnqueue(const Message& m);
    bool      _nativeDequeue(Message* out);
#endif

    void _enqueue(const Message& m);
};
