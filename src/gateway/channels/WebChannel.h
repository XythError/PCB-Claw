#pragma once

#include "../Channel.h"
#include "../Gateway.h"

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#  include <ESPAsyncWebServer.h>
#  include <ArduinoJson.h>
#endif

// ─────────────────────────────────────────────────────────────────
// WebChannel — HTTP REST + WebSocket channel hosted on the ESP32.
//
// Endpoints:
//   POST /api/message   — send a message to the agent (JSON body)
//   GET  /api/status    — returns system status as JSON
//   WS   /ws            — bidirectional WebSocket for real-time chat
//
// Incoming JSON (POST /api/message):
//   { "sender": "web-user", "text": "..." }
//
// Outgoing JSON (responses / WS push):
//   { "id": "...", "text": "...", "ts": 12345 }
// ─────────────────────────────────────────────────────────────────

#ifndef NATIVE_BUILD

class WebChannel : public IChannel {
public:
    explicit WebChannel(Gateway& gw, uint16_t port = 80)
        : _gw(gw), _server(port), _ws("/ws") {}

    const char* id()   const override { return "web"; }
    const char* name() const override { return "HTTP/WebSocket"; }

    bool begin() override {
        // ── REST endpoint ────────────────────────────────────────
        _server.on("/api/message", HTTP_POST,
            [](AsyncWebServerRequest* req) {},
            nullptr,
            [this](AsyncWebServerRequest* req,
                   uint8_t* data, size_t len,
                   size_t index, size_t total)
            {
                JsonDocument doc;
                if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
                    req->send(400, "application/json",
                              "{\"error\":\"invalid JSON\"}");
                    return;
                }
                const char* sender = doc["sender"] | "web-user";
                const char* text   = doc["text"]   | "";
                if (strlen(text) == 0) {
                    req->send(400, "application/json",
                              "{\"error\":\"empty text\"}");
                    return;
                }
                MessageType t = (text[0] == '/') ? MessageType::COMMAND
                                                 : MessageType::TEXT;
                Message m = Message::make("web", sender, text, t);
                _gw.push(m);
                req->send(202, "application/json", "{\"status\":\"queued\"}");
            });

        _server.on("/api/status", HTTP_GET,
            [](AsyncWebServerRequest* req) {
                req->send(200, "application/json",
                          "{\"device\":\"PCB-Claw\","
                          "\"firmware\":\"1.0.0\","
                          "\"status\":\"running\"}");
            });

        // ── WebSocket ────────────────────────────────────────────
        _ws.onEvent([this](AsyncWebSocket* /*srv*/,
                           AsyncWebSocketClient* client,
                           AwsEventType type,
                           void* arg, uint8_t* data, size_t len)
        {
            if (type == WS_EVT_DATA) {
                AwsFrameInfo* info = (AwsFrameInfo*)arg;
                if (info->opcode == WS_TEXT) {
                    char buf[MSG_CONTENT_LEN];
                    size_t n = (len < MSG_CONTENT_LEN - 1) ? len : MSG_CONTENT_LEN - 1;
                    memcpy(buf, data, n);
                    buf[n] = '\0';
                    MessageType t = (buf[0] == '/') ? MessageType::COMMAND
                                                    : MessageType::TEXT;
                    char senderBuf[MSG_SENDER_LEN];
                    snprintf(senderBuf, sizeof(senderBuf), "ws-%u", client->id());
                    Message m = Message::make("web", senderBuf, buf, t);
                    snprintf(m.reply_channel, MSG_CHANNEL_LEN,
                             "ws:%u", client->id());
                    _gw.push(m);
                }
            }
        });

        _server.addHandler(&_ws);

        // Serve the bundled UI from LittleFS /www folder if present
        _server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

        _server.begin();
        _connected = true;
        return true;
    }

    bool poll() override {
        _ws.cleanupClients();
        return false;  // async — no explicit poll needed
    }

    bool send(const Message& msg) override {
        // Check for WS client target
        const char* colon = strchr(msg.reply_channel, ':');
        if (colon && strncmp(msg.reply_channel, "ws:", 3) == 0) {
            uint32_t cid = (uint32_t)atol(colon + 1);
            AsyncWebSocketClient* client = _ws.client(cid);
            if (client && client->status() == WS_CONNECTED) {
                client->text(msg.content);
                return true;
            }
        }
        // Fallback: broadcast to all WS clients
        _ws.textAll(msg.content);
        return true;
    }

    bool isConnected() const override { return _connected; }

    void maintain() override {
        _ws.cleanupClients();
    }

private:
    Gateway&          _gw;
    AsyncWebServer    _server;
    AsyncWebSocket    _ws;
    bool              _connected = false;
};

#else  // NATIVE_BUILD — stub
#include "../Channel.h"
class WebChannel : public IChannel {
public:
    WebChannel(Gateway& gw, uint16_t port = 80) : _gw(gw) { (void)port; }
    const char* id()   const override { return "web"; }
    const char* name() const override { return "HTTP/WebSocket (stub)"; }
    bool begin() override { return true; }
    bool poll()  override { return false; }
    bool send(const Message&) override { return false; }
    bool isConnected() const override { return false; }
private:
    Gateway& _gw;
};
#endif
