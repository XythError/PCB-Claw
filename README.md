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

---

## Hardware Realism & Constraints

### Can an AI agent really run on an ESP32-S3?

Yes — with the right architecture. PCB-Claw delegates all LLM inference to a cloud
API (OpenAI / Anthropic / any OpenAI-compatible endpoint) over WiFi. The
ESP32-S3 itself only needs to assemble prompts, fire HTTPS requests, parse JSON
responses, and execute tool calls (GPIO / I2C / SPI / HTTP). That keeps the
on-device memory footprint small enough to fit inside the hardware constraints
described below.

### ESP32-S3-WROOM-1-N16R8 Spec Sheet

| Resource | Raw Capacity | Notes |
|---|---|---|
| **CPU** | Xtensa LX7 dual-core @ 240 MHz | No FPU; integer-only Lua scripting |
| **Internal SRAM** | 512 KB | Shared with WiFi/BT stacks, FreeRTOS, and all task stacks |
| **OPI-PSRAM** | 8 MB | Octal-SPI external; ~10× slower than internal SRAM for random access |
| **Flash** | 16 MB | External SPI; code + read-only data + LittleFS all share this |
| **USB-OTG / CDC** | Full-speed USB 2.0 | Used as the serial console (SerialChannel) |
| **WiFi** | 802.11 b/g/n 2.4 GHz | Mandatory for LLM API calls; TLS handshake is CPU-heavy |
| **Bluetooth** | BLE 5 | Not used by PCB-Claw; disabled to save ~40 KB SRAM |

### Effective Memory Budget

The WiFi stack, TLS engine, and FreeRTOS scheduler consume roughly 240 KB of the
internal 512 KB SRAM at runtime, leaving about **270 KB of free internal heap**
for the application.

| Region | Size | Used for |
|---|---|---|
| WiFi + TLS stack | ~240 KB (internal SRAM) | TCP/IP, mbedTLS, 802.11 driver |
| App free heap | ~270 KB (internal SRAM) | FreeRTOS task stacks, JSON docs, short strings |
| LLM_CTX pool | 8 KB (PSRAM) | Prompt assembly buffer (`PsramAllocator`) |
| WORKSPACE pool | 16 KB (PSRAM) | Agent scratchpad / workspace reads |
| SCRIPT pool | 32 KB (PSRAM) | Lua VM heap (when Lua is enabled) |
| LLM response buf | 2 KB (internal SRAM) | `LLM_RESPONSE_BUF` in `ReasoningLoop.h` |
| Prompt build buf | 4 KB (internal SRAM) | `PROMPT_BUF_LEN` in `PromptBuilder.h` |
| Remaining PSRAM | ~7.9 MB | Available for future larger buffers |

**Key design decisions driven by these limits:**

- `max_tokens: 512` in `agent.md` — keeps LLM responses inside the 2 KB response buffer.
- Stateless per-message reasoning — no conversation history accumulates in RAM.
- PSRAM pools for every large buffer — keeps internal SRAM free for the WiFi stack.
- Static / stack-allocated singletons in `main.cpp` — avoids heap fragmentation.

### Flash Partition Layout

```
# partitions_16MB.csv
nvs        20 KB   — WiFi credentials, NVS key-value store
otadata     8 KB   — OTA slot selector
app0        3 MB   — Active firmware (OTA slot 0)
app1        3 MB   — OTA update target (slot 1)
littlefs   ~10 MB  — Config .md files + agent workspace
```

A typical PCB-Claw firmware image is roughly **1.5–2 MB**, comfortably within the
3 MB OTA slot. LittleFS gets ~10 MB for configs, workflow definitions, workspace
files, daily notes, and self-built skill JSON files.

### Comparison: PCB-Claw vs PicoClaw vs OpenClaw

| Dimension | **PCB-Claw** | **PicoClaw** | **OpenClaw** |
|---|---|---|---|
| **Target hardware** | ESP32-S3 MCU (bare-metal) | Any Linux board ≥ $10 (LicheeRV-Nano, etc.) | Desktop / server |
| **OS / runtime** | FreeRTOS (no Linux) | Linux (glibc + kernel) | Linux / macOS |
| **Language** | C++17 | Go | TypeScript |
| **RAM footprint** | ~270 KB SRAM + 8 MB PSRAM | < 10 MB | > 1 GB |
| **LLM inference** | Cloud API (WiFi required) | Cloud API (WiFi / Ethernet required) | Cloud API |
| **Startup time** | < 2 s (FreeRTOS boot) | < 1 s (Go binary, no OS boot) | 10–30 s+ (Node.js cold start) |
| **Hardware I/O** | Native GPIO / I2C / SPI | Via Linux `/sys` / `/dev` | None |
| **Scripting** | Lua 5.1 (optional) | Shell / Python via OS | Node.js / Python |
| **Filesystem** | LittleFS (~10 MB) | Host filesystem (unlimited) | Host filesystem |
| **Persistent memory** | LittleFS workspace files | Local files | Local files |
| **Approx. BOM cost** | ~$5–10 (ESP32-S3 module) | ~$10–50 (SBC with Linux) | Desktop / server (e.g. ~$600 Mac Mini, or any capable PC) |

PicoClaw is the closest architectural sibling: both delegate reasoning to a cloud
LLM and keep a small local footprint. The critical difference is that PicoClaw
requires a **Linux kernel** to function (it links against glibc, spawns subprocesses,
and uses OS-level file handles), making it unsuitable for microcontrollers. PCB-Claw
fills the gap between PicoClaw and bare hardware by speaking FreeRTOS primitives
natively.

### Agent Limitations on ESP32

| Limitation | Root cause | Mitigation in PCB-Claw |
|---|---|---|
| **No local LLM inference** | ESP32-S3 has no NPU and insufficient RAM for LLM inference — even a 1B-parameter model requires ~500 MB–2 GB depending on quantisation, far beyond 8 MB PSRAM | Offload all inference to cloud; device acts as an intelligent I/O gateway |
| **Prompt + response buffers are small** | 270 KB free SRAM; 8 MB PSRAM for larger allocs | `PROMPT_BUF_LEN = 4096`, `LLM_RESPONSE_BUF = 2048`; PSRAM pools for assembly |
| **No conversation history** | Accumulating message history would exhaust PSRAM within tens of turns | Stateless per-message design; long-term memory via LittleFS `MEMORY.md` |
| **TLS handshake latency** | Xtensa LX7 performs mbedTLS handshakes in software (~1–2 s for RSA-2048) | Reuse HTTP connections where possible; set `http.setTimeout(15000)` |
| **Single WiFi radio** | Only 2.4 GHz 802.11n; no 5 GHz, no Ethernet | Use a stable 2.4 GHz AP; RSSI-dependent throughput |
| **Scripting is Lua only** | No OS process model; Python / Go / Node.js runtimes require Linux | Optional Lua 5.1 VM with a 32 KB PSRAM heap (see `ScriptTool.h`) |
| **No subprocess / shell** | FreeRTOS has no `exec()` or shell | `SpawnTool` creates FreeRTOS tasks; `ScriptTool` runs Lua snippets |
| **Concurrent tool calls limited** | LaneQueue has a fixed number of lanes and a bounded queue depth | Per-lane queues enforce ordering; default lane handles overflow |
| **OTA requires WiFi** | Firmware updates are flashed over-the-air via HTTP | Two OTA slots in partition table; roll back on failed boot |
| **Power budget** | Peak WiFi TX draws ~240 mA @ 3.3 V | Deep-sleep between interactions if battery-powered |

### Why C++ and not Go / Python / MicroPython?

| Language | Verdict | Reason |
|---|---|---|
| **C++17** | ✅ Used | Zero-overhead abstractions, no GC pauses, deterministic stack layout, full ESP-IDF / Arduino ecosystem, Lua embedding trivial |
| **MicroPython** | ⚠️ Possible but limited | Interpreter + runtime uses ~256 KB; leaves very little for application logic; no async HTTP; slower than C++ by 10–100× for JSON parsing |
| **Go** | ❌ Not viable | Go runtime requires Linux (fork/exec, mmap, goroutine scheduler with `pthread`); no ESP-IDF port exists |
| **Python 3** | ❌ Not viable | CPython interpreter requires a POSIX OS and ~20 MB RAM; not available on ESP32 |
| **Rust (no_std)** | ⚠️ Theoretically viable | `no_std` Rust compiles to bare-metal, but the esp32s3 Rust toolchain is less mature than the Arduino / ESP-IDF C++ ecosystem used here |
| **TypeScript / Node.js** | ❌ Not viable | V8 engine requires ~50–100 MB RAM and a POSIX OS |

C++ with Arduino framework is the pragmatic choice: it gives direct hardware register
access, deterministic memory management, and a mature library ecosystem (ArduinoJson,
ESP Async WebServer, LittleFS) that has been battle-tested on ESP32 hardware.

