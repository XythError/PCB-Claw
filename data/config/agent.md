# PCB-Claw Agent Configuration
# Lines starting with '#' are ignored.
# Format: key: value
# This file is loaded from LittleFS at startup.

# ── LLM Provider ──────────────────────────────────────────────────
# Supported: openai | anthropic | custom
llm_provider: openai

# Model — choose the smallest capable model to save tokens/cost
# openai:    gpt-4o-mini  (recommended), gpt-4o
# anthropic: claude-haiku-20240307
llm_model: gpt-4o-mini

# API key — set this before flashing or update via /api/config
# WARNING: keep this secret — never commit to version control
llm_api_key:

# Custom LLM endpoint (leave empty for provider default)
# Example for Ollama: http://192.168.1.100:11434/v1/chat/completions
llm_endpoint:

# Maximum tokens in LLM response (keep small for efficiency)
max_tokens: 512

# Temperature (0.0 = deterministic, 1.0 = creative)
temperature: 0.3

# System prompt — defines the agent's personality and capabilities
system_prompt: You are PCB-Claw, an ultra-efficient AI assistant running on an ESP32-S3 microcontroller. You are precise, concise, and always respond in the language of the user. You have access to hardware tools (GPIO, I2C, SPI) and can execute workflows. Keep responses short to save memory.

# ── WiFi ──────────────────────────────────────────────────────────
wifi_ssid: YOUR_WIFI_SSID
wifi_password: YOUR_WIFI_PASSWORD

# ── Web Server ────────────────────────────────────────────────────
web_port: 80

# ── Telegram Bot ─────────────────────────────────────────────────
# Obtain from @BotFather on Telegram
telegram_token:
# Comma-separated list of allowed chat IDs (empty = allow all)
telegram_admins:

# ── Hardware Pins (ESP32-S3 defaults) ─────────────────────────────
i2c_sda: 21
i2c_scl: 22
spi_mosi: 35
spi_miso: 37
spi_sck: 36

# ── Workspace ─────────────────────────────────────────────────────
workspace_path: /workspace
