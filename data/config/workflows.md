# PCB-Claw Workflow Definitions
# Each workflow starts with "# <name>" and contains steps.
# Steps use indented key-value pairs.
# Workflows can reference previous step results via ${step_N.result}
#
# Step fields:
#   tool:     name of the registered tool to invoke
#   args:     JSON arguments passed to the tool
#   lane:     execution lane (gpio | i2c | spi | http | default)
#   optional: true if failure should not abort the workflow

# ─────────────────────────────────────────────────────────────────
# blink_led — toggle the onboard LED three times
# ─────────────────────────────────────────────────────────────────
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

# ─────────────────────────────────────────────────────────────────
# scan_i2c — scan I2C bus and report found devices
# ─────────────────────────────────────────────────────────────────
# scan_i2c
## steps
- tool: i2c
  args: {"op":"scan"}
  lane: i2c

# ─────────────────────────────────────────────────────────────────
# read_bme280 — read temperature/humidity from a BME280 sensor
#               (assumes default I2C address 0x76)
# ─────────────────────────────────────────────────────────────────
# read_bme280
## steps
- tool: i2c
  args: {"op":"write_reg","addr":118,"reg":0xF3,"data":[]}
  lane: i2c
  optional: true
- tool: i2c
  args: {"op":"read_reg","addr":118,"reg":0xF7,"len":8}
  lane: i2c

# ─────────────────────────────────────────────────────────────────
# system_status — collect and report system health
# ─────────────────────────────────────────────────────────────────
# system_status
## steps
- tool: gpio
  args: {"op":"analog_read","pin":34}
  lane: gpio
  optional: true
- tool: i2c
  args: {"op":"scan"}
  lane: i2c
  optional: true

# ─────────────────────────────────────────────────────────────────
# fetch_time — get current time from an NTP HTTP API
# ─────────────────────────────────────────────────────────────────
# fetch_time
## steps
- tool: http
  args: {"method":"GET","url":"http://worldtimeapi.org/api/ip","timeout":5000}
  lane: http
