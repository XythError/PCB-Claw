#pragma once

#include "../Channel.h"
#include "../Gateway.h"

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#endif

// ─────────────────────────────────────────────────────────────────
// SerialChannel — reads/writes over USB-CDC (Serial) or UART.
//
// Useful for local debugging, CLI interaction, and as a fallback
// channel when WiFi is unavailable.
//
// Protocol: newline-terminated plain text.
//   Incoming: any line becomes a TEXT message.
//   Lines starting with '/' become COMMAND messages.
// ─────────────────────────────────────────────────────────────────

class SerialChannel : public IChannel {
public:
    explicit SerialChannel(Gateway& gw,
                           uint32_t baudRate = 115200,
                           const char* channelId = "serial")
        : _gw(gw), _baud(baudRate)
    {
        strncpy(_id, channelId, sizeof(_id) - 1);
    }

    const char* id()   const override { return _id; }
    const char* name() const override { return "Serial/USB-CDC"; }

    bool begin() override {
#ifndef NATIVE_BUILD
        Serial.begin(_baud);
        uint32_t t = millis();
        while (!Serial && millis() - t < 3000) delay(10);
        Serial.println("[SerialChannel] Ready");
#endif
        _connected = true;
        return true;
    }

    bool poll() override {
#ifndef NATIVE_BUILD
        while (Serial.available()) {
            char c = (char)Serial.read();
            if (c == '\n' || c == '\r') {
                if (_bufLen > 0) {
                    _buf[_bufLen] = '\0';
                    MessageType t = (_buf[0] == '/')
                                    ? MessageType::COMMAND
                                    : MessageType::TEXT;
                    Message m = Message::make(_id, "user", _buf, t);
                    _gw.push(m);
                    _bufLen = 0;
                    return true;
                }
            } else if (_bufLen < MSG_CONTENT_LEN - 1) {
                _buf[_bufLen++] = c;
            }
        }
#endif
        return false;
    }

    bool send(const Message& msg) override {
#ifndef NATIVE_BUILD
        Serial.println(msg.content);
#endif
        return true;
    }

    bool isConnected() const override { return _connected; }

private:
    Gateway&  _gw;
    uint32_t  _baud;
    char      _id[MSG_CHANNEL_LEN] = {};
    bool      _connected = false;
    char      _buf[MSG_CONTENT_LEN] = {};
    size_t    _bufLen = 0;
};
