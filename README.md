# PCB-Claw
Ultra-Efficient AI Assistant in C++ for ESP32-S3-WROOM-1-N16R8

## Overview

PCB-Claw is a lightweight, token-efficient AI agent that runs on the **ESP32-S3-WROOM-1-N16R8** (16 MB Flash, 8 MB OPI-PSRAM). It exposes a unified messaging gateway with multi-channel support, a FreeRTOS-based lane queue to prevent race conditions, built-in hardware tools (GPIO, I2C, SPI), and a local HTTP + WebSocket server.

```
┌──────────────────────────────────────────────────────────────┐
│                        PCB-Claw                              │
│                                                              │
│  Channels (multi-channel input/output)                       │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │  Serial  │  │ Telegram │  │   HTTP   │  │ (Slack…) │   │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘   │
│       │              │              │              │          │
│  ┌────▼──────────────▼──────────────▼──────────────▼──────┐ │
│  │               Gateway (abstraction layer)               │ │
│  │           Unified Message format  ▲  push/send          │ │
│  └────────────────────────┬──────────┴───────────────────┘  │
│                           │                                  │
│  ┌────────────────────────▼───────────────────────────────┐ │
│  │                      Agent                              │ │
│  │  IntentDetector → TaskDecomposer → LaneQueue            │ │
│  │         ↓                                ↓              │ │
│  │   ReasoningLoop (LLM)          WorkflowEngine           │ │
│  └────────────────────────────────────────────────────────┘ │
│                                                              │
│  LaneQueue (FreeRTOS — no race conditions)                   │
│  ┌────────┐ ┌────────┐ ┌──────┐ ┌──────┐ ┌───────────┐   │
│  │  llm   │ │  gpio  │ │ i2c  │ │ spi  │ │  default  │   │
│  └────────┘ └────────┘ └──────┘ └──────┘ └───────────┘   │
│                                                              │
│  Tools                                                       │
│  GpioTool · I2cTool · SpiTool · HttpTool · (custom…)       │
└──────────────────────────────────────────────────────────────┘
```

## Features

| Feature | Details |
|---|---|
| **Gateway / Abstraction Layer** | Unified `Message` struct; channels plug in via `IChannel` interface |
| **Multi-Channel** | Serial/USB-CDC, Telegram Bot, HTTP REST + WebSocket; Slack/Mail/WhatsApp stubs |
| **Reasoning Loop** | OpenAI or Anthropic API; stateless per-message call (token-efficient) |
| **Intent Detection** | Local keyword classifier — no LLM call needed for hardware commands |
| **Task Decomposition** | Each message → 1–4 typed `Task` objects routed to the right lane |
| **Lane Queue** | FreeRTOS per-lane task queues; same-lane tasks execute strictly in order |
| **Workflow Engine** | Define multi-step workflows in `.md` files; step results are composable |
| **GPIO Tool** | `pin_mode`, `write`, `read`, `analog_read`, `pwm_write` |
| **I2C Tool** | `scan`, `write_reg`, `read_reg`, `write_raw`, `read_raw` |
| **SPI Tool** | `transfer`, `write`, `read` (full-duplex) |
| **HTTP Tool** | GET / POST / PUT / DELETE / PATCH to external APIs |
| **Local Web Server** | HTTP REST + WebSocket; serves UI from LittleFS `/www/`; minimal inline UI fallback |
| **Config via .md files** | All settings in `data/config/agent.md`; reloadable at runtime |
| **Own Workspace** | Agent saves context to LittleFS `/workspace/` |
| **Self-Building Skills** | Custom tools saved to `/workspace/tools/` |

## Project Structure

```
PCB-Claw/
├── platformio.ini            ← Build config (esp32s3 + native test env)
├── partitions_16MB.csv       ← Flash partition table with OTA + LittleFS
├── data/
│   └── config/
│       ├── agent.md          ← LLM provider, WiFi, pin config
│       ├── channels.md       ← Channel enable/disable + auth
│       ├── tools.md          ← Tool config + custom tool path
│       └── workflows.md      ← Predefined workflow definitions
├── src/
│   ├── main.cpp              ← Arduino setup() / loop()
│   ├── gateway/
│   │   ├── Message.h         ← Unified message format
│   │   ├── Channel.h         ← IChannel interface
│   │   ├── Gateway.h/.cpp    ← Routing centre
│   │   └── channels/
│   │       ├── SerialChannel.h
│   │       ├── TelegramChannel.h
│   │       └── WebChannel.h
│   ├── queue/
│   │   ├── Task.h            ← Task struct
│   │   └── LaneQueue.h       ← FreeRTOS lane queue
│   ├── tools/
│   │   ├── Tool.h            ← ITool interface
│   │   ├── ToolRegistry.h/.cpp
│   │   ├── GpioTool.h
│   │   ├── I2cTool.h
│   │   ├── SpiTool.h
│   │   └── HttpTool.h
│   ├── agent/
│   │   ├── Agent.h/.cpp      ← Main reasoning unit
│   │   ├── IntentDetector.h  ← Local keyword classifier
│   │   ├── TaskDecomposer.h  ← Message → Task(s)
│   │   ├── ReasoningLoop.h   ← LLM API call (OpenAI / Anthropic)
│   │   └── WorkflowEngine.h  ← .md workflow parser + runner
│   ├── webserver/
│   │   └── WebServer.h       ← Local HTTP + WebSocket server
│   └── config/
│       └── ConfigManager.h   ← .md file config loader
└── test/
    ├── test_queue/           ← LaneQueue unit tests
    ├── test_tools/           ← ToolRegistry unit tests
    └── test_agent/           ← IntentDetector / Workflow / Config tests
```

## Quick Start

### 1. Configure

Edit `data/config/agent.md`:
```markdown
wifi_ssid: YourNetwork
wifi_password: YourPassword
llm_api_key: sk-...
telegram_token: 123456:ABC...
```

### 2. Build & Flash (PlatformIO)

```bash
# Install PlatformIO
pip install platformio

# Build and flash firmware
pio run -e esp32s3 -t upload

# Upload LittleFS (config files)
pio run -e esp32s3 -t uploadfs

# Monitor serial output
pio device monitor
```

### 3. Run Native Unit Tests

```bash
# With g++ directly
g++ -std=c++17 -DNATIVE_BUILD -DUNIT_TEST \
    -Isrc -I/path/to/unity/src \
    test/test_agent/test_intent_detection.cpp \
    src/tools/ToolRegistry.cpp src/gateway/Gateway.cpp \
    /path/to/unity.c -o test_intent && ./test_intent

# Or with PlatformIO (when native platform is available)
pio test -e native
```

## Workflow File Format

```markdown
# blink_led
## steps
- tool: gpio
  args: {"op":"pin_mode","pin":2,"mode":"OUTPUT"}
  lane: gpio
- tool: gpio
  args: {"op":"write","pin":2,"value":1}
  lane: gpio
- tool: gpio
  args: {"op":"write","pin":2,"value":0}
  lane: gpio
```

Step fields:
- `tool` — registered tool name (`gpio`, `i2c`, `spi`, `http`)
- `args` — JSON arguments passed to the tool
- `lane` — execution lane (tasks within a lane run sequentially)
- `optional: true` — failure does not abort the workflow

Reference previous step results with `${step_N.result}`.

## LLM Providers

| Provider | Config value | Recommended model |
|---|---|---|
| OpenAI | `llm_provider: openai` | `gpt-4o-mini` |
| Anthropic | `llm_provider: anthropic` | `claude-haiku-20240307` |
| Custom / Ollama | `llm_provider: custom` + `llm_endpoint: http://...` | any |

## API Endpoints (when WiFi connected)

| Method | Path | Description |
|---|---|---|
| `POST` | `/api/message` | Send a message to the agent |
| `GET` | `/api/status` | Device + agent status JSON |
| `GET` | `/api/tools` | List all registered tools + schemas |
| `GET` | `/api/metrics` | Runtime metrics (heap, uptime, etc.) |
| `POST` | `/api/workflow` | Trigger a named workflow |
| `WS` | `/ws` | Real-time bidirectional chat |

## Hardware Defaults (ESP32-S3)

| Bus | Pins |
|---|---|
| I2C | SDA=21, SCL=22, 400 kHz |
| SPI | MOSI=35, MISO=37, SCK=36 |
| Built-in LED | GPIO 2 |

Override in `data/config/agent.md`.

