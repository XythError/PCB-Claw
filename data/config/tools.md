# PCB-Claw Tool Configuration
# Configure built-in tools and define custom tools.
# Lines starting with '#' are ignored.

# ── GPIO Tool ────────────────────────────────────────────────────
gpio_enabled: true
# Named pin aliases (optional — used in workflow files)
gpio_alias_led: 2
gpio_alias_relay1: 4
gpio_alias_button: 0

# ── I2C Tool ─────────────────────────────────────────────────────
i2c_enabled: true
i2c_sda: 21
i2c_scl: 22
i2c_freq: 400000

# ── SPI Tool ─────────────────────────────────────────────────────
spi_enabled: true
spi_mosi: 35
spi_miso: 37
spi_sck: 36
# Default SPI speed in Hz
spi_speed: 1000000

# ── HTTP Tool ────────────────────────────────────────────────────
http_enabled: true
# Default request timeout in milliseconds
http_timeout: 10000
# Optional: base URL prefix added to relative URLs
http_base_url:

# ── Custom Tools (self-building skills) ───────────────────────────
# The agent can generate and register new tools at runtime.
# Custom tools are stored in /workspace/tools/ as JSON files.
# Example format:
#   name: weather
#   description: Get current weather for a city
#   args: {"city": "string"}
#   script: http GET https://wttr.in/${city}?format=j1
custom_tools_path: /workspace/tools
