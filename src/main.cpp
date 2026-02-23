// ─────────────────────────────────────────────────────────────────
// PCB-Claw — Ultra-Efficient AI Assistant
// ESP32-S3-WROOM-1-N16R8
//
// Main entry point — wires all subsystems together.
// ─────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

// Gateway + channels
#include "gateway/Gateway.h"
#include "gateway/channels/SerialChannel.h"
#include "gateway/channels/TelegramChannel.h"
#include "gateway/channels/WebChannel.h"

// Lane queue
#include "queue/LaneQueue.h"

// Tools
#include "tools/ToolRegistry.h"
#include "tools/GpioTool.h"
#include "tools/I2cTool.h"
#include "tools/SpiTool.h"
#include "tools/HttpTool.h"

// Agent
#include "agent/Agent.h"

// Web server
#include "webserver/WebServer.h"

// Config
#include "config/ConfigManager.h"

// ─────────────────────────────────────────────────────────────────
// Global singletons (static storage — no heap fragmentation)
// ─────────────────────────────────────────────────────────────────

static ConfigManager configMgr;
static Gateway       gateway;
static LaneQueue     laneQueue;
static ToolRegistry  toolRegistry;

// Tools
static GpioTool gpioTool;
static I2cTool  i2cTool;
static SpiTool  spiTool;
static HttpTool httpTool;

// Channels (created after config is loaded for WiFi credentials)
static SerialChannel* serialChannel  = nullptr;
static TelegramChannel* tgChannel    = nullptr;
static WebChannel*    webChannel     = nullptr;

// Agent + web server
static Agent*     agent     = nullptr;
static WebServer* webServer = nullptr;

// ─────────────────────────────────────────────────────────────────
// WiFi helper
// ─────────────────────────────────────────────────────────────────
static bool connectWiFi(const char* ssid, const char* pass,
                        uint16_t timeoutMs = 15000) {
    if (!ssid || ssid[0] == '\0') {
        Serial.println("[WiFi] No SSID configured — skipping");
        return false;
    }
    Serial.printf("[WiFi] Connecting to %s ...\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) {
            Serial.println("[WiFi] Connection timeout");
            return false;
        }
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\n[WiFi] Connected — IP: %s\n",
                  WiFi.localIP().toString().c_str());
    return true;
}

// ─────────────────────────────────────────────────────────────────
void setup() {
    // Serial is always the first channel (no WiFi required)
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n=== PCB-Claw booting ===");

    // ── 1. Config ─────────────────────────────────────────────────
    configMgr.begin();
    configMgr.load("/config/agent.md");

    // ── 2. Tools ──────────────────────────────────────────────────
    // Configure I2C / SPI pins from config (defaults are ESP32-S3 VSPI)
    int sdaPin  = configMgr.getInt("i2c_sda",  21);
    int sclPin  = configMgr.getInt("i2c_scl",  22);
    int mosiPin = configMgr.getInt("spi_mosi", 35);
    int misoPin = configMgr.getInt("spi_miso", 37);
    int sckPin  = configMgr.getInt("spi_sck",  36);

    new (&i2cTool) I2cTool(sdaPin, sclPin);
    new (&spiTool) SpiTool(mosiPin, misoPin, sckPin);

    toolRegistry.add(&gpioTool);
    toolRegistry.add(&i2cTool);
    toolRegistry.add(&spiTool);
    toolRegistry.add(&httpTool);

    // Initialise tools that need hardware setup
    i2cTool.begin();
    spiTool.begin();

    // ── 3. Channels ───────────────────────────────────────────────
    // Serial channel (always available)
    serialChannel = new SerialChannel(gateway);
    gateway.addChannel(serialChannel);

    // WiFi-dependent channels
    const char* ssid = configMgr.get("wifi_ssid", "");
    const char* pass = configMgr.get("wifi_password", "");
    bool wifiOk = connectWiFi(ssid, pass);

    if (wifiOk) {
        // Web channel (REST + WebSocket)
        uint16_t webPort = (uint16_t)configMgr.getInt("web_port", 80);
        webChannel = new WebChannel(gateway, webPort);
        gateway.addChannel(webChannel);

        // Telegram channel (optional)
        const char* tgToken = configMgr.get("telegram_token", "");
        if (tgToken[0] != '\0') {
            const char* tgAdmins = configMgr.get("telegram_admins", "");
            tgChannel = new TelegramChannel(gateway, tgToken, tgAdmins);
            gateway.addChannel(tgChannel);
        }
    }

    // ── 4. Gateway ────────────────────────────────────────────────
    gateway.begin();

    // ── 5. Agent ──────────────────────────────────────────────────
    agent = new Agent(gateway, toolRegistry, laneQueue, configMgr);
    agent->configure();
    agent->begin();

    // ── 6. Web server (if WiFi available) ────────────────────────
    if (wifiOk) {
        webServer = new WebServer(*agent, toolRegistry,
                                  (uint16_t)configMgr.getInt("web_port", 80));
        webServer->begin();
    }

    Serial.printf("[Boot] %d tools, %d channels ready\n",
                  toolRegistry.count(), gateway.channelCount());
    Serial.println("=== PCB-Claw ready ===\n");
}

// ─────────────────────────────────────────────────────────────────
void loop() {
    // Poll all channels (harvest inbound messages)
    gateway.tick();

    // Process up to 4 messages per loop iteration
    for (uint8_t i = 0; i < 4; ++i) {
        if (!agent->process()) break;
    }

    // Yield to FreeRTOS so lane worker tasks can run
    vTaskDelay(pdMS_TO_TICKS(10));
}
