#include "Gateway.h"

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#endif

// ─────────────────────────────────────────────────────────────────
// Gateway implementation
// ─────────────────────────────────────────────────────────────────

Gateway::Gateway() {
#ifndef NATIVE_BUILD
    _inboundQueue = xQueueCreate(GATEWAY_QUEUE_DEPTH, sizeof(Message));
#endif
}

Gateway::~Gateway() {
#ifndef NATIVE_BUILD
    if (_inboundQueue) vQueueDelete(_inboundQueue);
#endif
}

bool Gateway::addChannel(IChannel* ch) {
    if (!ch || _count >= GATEWAY_MAX_CHANNELS) return false;
    _channels[_count++] = ch;
    return true;
}

bool Gateway::begin() {
    bool ok = true;
    for (uint8_t i = 0; i < _count; ++i) {
        if (!_channels[i]->begin()) {
#ifndef NATIVE_BUILD
            Serial.printf("[Gateway] Channel '%s' failed to start\n",
                          _channels[i]->name());
#endif
            ok = false;
        }
    }
    return ok;
}

void Gateway::tick() {
    for (uint8_t i = 0; i < _count; ++i) {
        _channels[i]->maintain();
        _channels[i]->poll();
    }
}

void Gateway::_enqueue(const Message& m) {
#ifndef NATIVE_BUILD
    if (_inboundQueue) {
        xQueueSendToBack(_inboundQueue, &m, 0);
    }
#else
    _nativeEnqueue(m);
#endif
}

bool Gateway::receive(Message* out, uint32_t timeoutMs) {
    if (!out) return false;
#ifndef NATIVE_BUILD
    TickType_t ticks = (timeoutMs == 0) ? 0 : pdMS_TO_TICKS(timeoutMs);
    return xQueueReceive(_inboundQueue, out, ticks) == pdTRUE;
#else
    (void)timeoutMs;
    return _nativeDequeue(out);
#endif
}

bool Gateway::send(const Message& msg) {
    IChannel* ch = findChannel(msg.reply_channel);
    if (!ch) {
        // Fallback to channel_id
        ch = findChannel(msg.channel_id);
    }
    if (!ch) return false;
    return ch->send(msg);
}

void Gateway::broadcast(const Message& msg) {
    for (uint8_t i = 0; i < _count; ++i) {
        _channels[i]->send(msg);
    }
}

IChannel* Gateway::channelAt(uint8_t idx) {
    if (idx >= _count) return nullptr;
    return _channels[idx];
}

IChannel* Gateway::findChannel(const char* id) {
    if (!id) return nullptr;
    for (uint8_t i = 0; i < _count; ++i) {
        if (strcmp(_channels[i]->id(), id) == 0) return _channels[i];
    }
    return nullptr;
}

// ── Native-only ring buffer ───────────────────────────────────────
#ifdef NATIVE_BUILD
bool Gateway::_nativeEnqueue(const Message& m) {
    if (_fill >= GATEWAY_QUEUE_DEPTH) return false;
    _nativeQueue[_tail] = m;
    _tail = (_tail + 1) % GATEWAY_QUEUE_DEPTH;
    ++_fill;
    return true;
}

bool Gateway::_nativeDequeue(Message* out) {
    if (_fill == 0) return false;
    *out = _nativeQueue[_head];
    _head = (_head + 1) % GATEWAY_QUEUE_DEPTH;
    --_fill;
    return true;
}
#endif
