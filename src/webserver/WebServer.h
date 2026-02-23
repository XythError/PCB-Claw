#pragma once

// ─────────────────────────────────────────────────────────────────
// WebServer — local HTTP server hosted on the ESP32.
//
// This is a thin wrapper around WebChannel (which uses
// ESPAsyncWebServer) plus a static file server that serves
// the embedded UI from LittleFS:/www/.
//
// Additional REST endpoints beyond WebChannel:
//   GET /api/tools    — list registered tools + their schemas
//   GET /api/config   — return current agent config (no secrets)
//   GET /api/metrics  — runtime metrics (heap, uptime, etc.)
//   POST /api/workflow — trigger a named workflow by name
//
// The server also exposes a minimal web UI via a self-contained
// HTML page served from LittleFS or compiled in as a fallback.
// ─────────────────────────────────────────────────────────────────

#include "../agent/Agent.h"
#include "../tools/ToolRegistry.h"

#ifndef NATIVE_BUILD
#  include <ESPAsyncWebServer.h>
#  include <LittleFS.h>
#  include <WiFi.h>
#  include <Arduino.h>

class WebServer {
public:
    WebServer(Agent&        agent,
              ToolRegistry& tools,
              uint16_t      port = 80)
        : _agent(agent), _tools(tools), _server(port) {}

    bool begin() {
        // ── /api/tools ────────────────────────────────────────────
        _server.on("/api/tools", HTTP_GET,
            [this](AsyncWebServerRequest* req) {
                char buf[2048] = {};
                _tools.schemasJson(buf, sizeof(buf));
                req->send(200, "application/json", buf);
            });

        // ── /api/config ───────────────────────────────────────────
        _server.on("/api/config", HTTP_GET,
            [](AsyncWebServerRequest* req) {
                // Return non-sensitive config info only
                req->send(200, "application/json",
                          "{\"device\":\"PCB-Claw\","
                          "\"version\":\"1.0.0\"}");
            });

        // ── /api/metrics ──────────────────────────────────────────
        _server.on("/api/metrics", HTTP_GET,
            [this](AsyncWebServerRequest* req) {
                char buf[512] = {};
                _agent.statusJson(buf, sizeof(buf));
                req->send(200, "application/json", buf);
            });

        // ── /api/workflow ─────────────────────────────────────────
        _server.on("/api/workflow", HTTP_POST,
            [](AsyncWebServerRequest* req) {},
            nullptr,
            [this](AsyncWebServerRequest* req,
                   uint8_t* data, size_t len,
                   size_t, size_t)
            {
                char nameBuf[WF_NAME_LEN] = {};
                // Extract "name" field from JSON body
                char body[256] = {};
                memcpy(body, data, len < sizeof(body)-1 ? len : sizeof(body)-1);
                const char* p = strstr(body, "\"name\":\"");
                if (p) {
                    p += 8;
                    size_t i = 0;
                    while (*p && *p != '"' && i < WF_NAME_LEN-1) {
                        nameBuf[i++] = *p++;
                    }
                    nameBuf[i] = '\0';
                }
                if (nameBuf[0] == '\0') {
                    req->send(400, "application/json",
                              "{\"error\":\"name required\"}");
                    return;
                }
                // Inject a workflow message into the agent
                Message m = Message::make("web", "http-user",
                                          nameBuf, MessageType::WORKFLOW);
                snprintf(m.content, MSG_CONTENT_LEN,
                         "run workflow %s", nameBuf);
                _agent.inject(m);
                req->send(202, "application/json",
                          "{\"status\":\"queued\"}");
            });

        // ── Static files from LittleFS /www ───────────────────────
        if (LittleFS.exists("/www")) {
            _server.serveStatic("/", LittleFS, "/www/")
                   .setDefaultFile("index.html");
        } else {
            // Fallback: serve inline minimal UI
            _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
                req->send(200, "text/html", _inlineUI());
            });
        }

        // ── 404 handler ───────────────────────────────────────────
        _server.onNotFound([](AsyncWebServerRequest* req) {
            req->send(404, "application/json", "{\"error\":\"not found\"}");
        });

        _server.begin();
        Serial.printf("[WebServer] Listening on http://%s/\n",
                      WiFi.localIP().toString().c_str());
        return true;
    }

private:
    Agent&         _agent;
    ToolRegistry&  _tools;
    AsyncWebServer _server;

    // Minimal self-contained UI (fallback when no LittleFS /www exists)
    static const char* _inlineUI() {
        return
            "<!DOCTYPE html><html><head>"
            "<title>PCB-Claw</title>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<style>"
            "body{font-family:monospace;background:#111;color:#0f0;padding:1em}"
            "#chat{height:60vh;overflow-y:auto;border:1px solid #0f0;padding:.5em}"
            "#input{width:80%;background:#222;color:#0f0;border:1px solid #0f0}"
            "</style></head><body>"
            "<h2>PCB-Claw</h2>"
            "<div id='chat'></div>"
            "<br><input id='input' placeholder='Message...' onkeydown=\"if(event.key==='Enter')send()\">"
            "<button onclick='send()'>Send</button>"
            "<script>"
            "var ws=new WebSocket('ws://'+location.host+'/ws');"
            "ws.onmessage=function(e){"
            "  var d=document.getElementById('chat');"
            "  d.innerHTML+='<div>&gt; '+e.data+'</div>';"
            "  d.scrollTop=d.scrollHeight;"
            "};"
            "function send(){"
            "  var i=document.getElementById('input');"
            "  var d=document.getElementById('chat');"
            "  if(i.value.trim()==='') return;"
            "  d.innerHTML+='<div style=\"color:#ff0\">You: '+i.value+'</div>';"
            "  ws.send(i.value); i.value='';"
            "}"
            "</script></body></html>";
    }
};

#else  // NATIVE_BUILD
#include "../agent/Agent.h"
class WebServer {
public:
    WebServer(Agent&, ToolRegistry&, uint16_t = 80) {}
    bool begin() { return true; }
};
#endif
