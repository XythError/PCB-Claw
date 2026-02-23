#pragma once

#include "../Channel.h"
#include "../Gateway.h"

#ifndef NATIVE_BUILD
#  include <Arduino.h>
#  include <WiFiClientSecure.h>
#  include <HTTPClient.h>
#  include <ArduinoJson.h>
#endif

// ─────────────────────────────────────────────────────────────────
// TelegramChannel — Telegram Bot API via long-polling getUpdates.
//
// Configuration keys (from channels.md):
//   telegram_token  — Bot token from @BotFather
//   telegram_admins — comma-separated list of allowed chat IDs
//
// The channel polls https://api.telegram.org/botTOKEN/getUpdates
// every POLL_INTERVAL_MS ms.  Only messages from listed admin IDs
// are accepted to prevent unauthorized access.
// ─────────────────────────────────────────────────────────────────

static constexpr uint32_t TELEGRAM_POLL_INTERVAL_MS = 5000;
static constexpr uint16_t TELEGRAM_TIMEOUT_MS       = 4000;

class TelegramChannel : public IChannel {
public:
    TelegramChannel(Gateway& gw,
                    const char* token,
                    const char* allowedChatIds = nullptr)
        : _gw(gw)
    {
        strncpy(_token, token, sizeof(_token) - 1);
        if (allowedChatIds) {
            strncpy(_admins, allowedChatIds, sizeof(_admins) - 1);
        }
    }

    const char* id()   const override { return "telegram"; }
    const char* name() const override { return "Telegram"; }

    bool begin() override {
        _connected = (strlen(_token) > 10);
#ifndef NATIVE_BUILD
        Serial.printf("[Telegram] %s (token len=%d)\n",
                      _connected ? "Ready" : "No token", strlen(_token));
#endif
        return _connected;
    }

    bool poll() override {
#ifndef NATIVE_BUILD
        uint32_t now = millis();
        if (now - _lastPoll < TELEGRAM_POLL_INTERVAL_MS) return false;
        _lastPoll = now;

        char url[256];
        snprintf(url, sizeof(url),
                 "https://api.telegram.org/bot%s/getUpdates"
                 "?offset=%ld&timeout=0&limit=10",
                 _token, (long)_updateOffset);

        HTTPClient http;
        http.setTimeout(TELEGRAM_TIMEOUT_MS);
        http.begin(url);
        int code = http.GET();
        if (code != 200) { http.end(); return false; }

        String body = http.getString();
        http.end();

        JsonDocument doc;
        if (deserializeJson(doc, body) != DeserializationError::Ok) return false;
        if (!doc["ok"].as<bool>()) return false;

        JsonArray results = doc["result"].as<JsonArray>();
        bool got = false;
        for (JsonObject upd : results) {
            long uid = upd["update_id"].as<long>();
            _updateOffset = uid + 1;

            JsonObject msg = upd["message"].as<JsonObject>();
            if (msg.isNull()) continue;

            long    chatId = msg["chat"]["id"].as<long>();
            const char* text = msg["text"].as<const char*>();
            if (!text) continue;

            // Check if sender is in the admin list
            if (!_isAllowed(chatId)) continue;

            char senderBuf[MSG_SENDER_LEN];
            snprintf(senderBuf, sizeof(senderBuf), "%ld", chatId);

            MessageType t = (text[0] == '/') ? MessageType::COMMAND
                                              : MessageType::TEXT;
            Message m = Message::make("telegram", senderBuf, text, t);
            // Store chatId so we can reply to the right chat
            snprintf(m.reply_channel, MSG_CHANNEL_LEN, "telegram:%ld", chatId);
            _gw.push(m);
            got = true;
        }
        return got;
#else
        return false;
#endif
    }

    bool send(const Message& msg) override {
#ifndef NATIVE_BUILD
        // Parse chatId from reply_channel ("telegram:<id>" or just channel_id)
        long chatId = 0;
        const char* colon = strchr(msg.reply_channel, ':');
        if (colon) {
            chatId = atol(colon + 1);
        } else {
            chatId = atol(msg.sender_id);
        }
        if (chatId == 0) return false;

        char url[256];
        snprintf(url, sizeof(url),
                 "https://api.telegram.org/bot%s/sendMessage", _token);

        JsonDocument body;
        body["chat_id"] = chatId;
        body["text"]    = msg.content;
        body["parse_mode"] = "Markdown";

        String bodyStr;
        serializeJson(body, bodyStr);

        HTTPClient http;
        http.setTimeout(TELEGRAM_TIMEOUT_MS);
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        int code = http.POST(bodyStr);
        http.end();
        return (code == 200);
#else
        return false;
#endif
    }

    bool isConnected() const override { return _connected; }

private:
    Gateway& _gw;
    char     _token[128]  = {};
    char     _admins[256] = {};  // comma-separated allowed chat IDs
    bool     _connected   = false;
    long     _updateOffset = 0;
    uint32_t _lastPoll     = 0;

    // Check whether chatId appears in the comma-separated _admins list.
    // If _admins is empty, all senders are accepted.
    bool _isAllowed(long chatId) const {
        if (_admins[0] == '\0') return true;
        char buf[32];
        snprintf(buf, sizeof(buf), "%ld", chatId);
        // Simple substring search for ",<id>," / leading / trailing
        char haystack[258];
        snprintf(haystack, sizeof(haystack), ",%s,", _admins);
        char needle[34];
        snprintf(needle, sizeof(needle), ",%s,", buf);
        return (strstr(haystack, needle) != nullptr);
    }
};
